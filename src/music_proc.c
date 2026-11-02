/*
 * Canity — 内置程序化配乐（移植自 canity-music/horror3.c v4）
 *
 * 和原作的区别：
 *   · 去掉 WAV 编码、PlaySound 和命令行菜单，只保留合成引擎；
 *   · 不再整首离线渲染，而是在音频线程里按 1024 帧一块流式生成；
 *   · 曲子按"圈"续写，每圈音符带绝对时间，上一圈结尾的长音会自然盖进
 *     下一圈开头，所以循环没有接缝，也不需要交叉淡化；
 *   · 去掉整首的首尾淡入淡出——那是给独立播放器用的，游戏里由场景淡入淡出负责。
 *
 * 仍然保留：四个原始 composer 的音符数据、tri/sine/noise、ADSR、双二阶
 * 低通、四梳状+二全通混响、高通和 tanh 限幅。
 *
 * 线程模型：只有音频线程会碰下面的合成状态。MusicProcSetScene/SetGain
 * 只写两个原子变量，混音线程每块读一次，因此不需要锁。
 */
/* 按 common.h 顶部的模块约定：**除 common.h 外任何 .c 都不直接包含系统头**。
   原来这里直接写了 5 个系统头（windows/math/string/stdlib/stdint）——
   功能上没问题（能编过），但它开了个坏头：
   下一个人会照着抄，系统头就会慢慢散到每个文件里。
   common.h 已经把这几样全带进来了。 */
#include "common.h"
#include "music_proc.h"
#include "music.h"

/* 注意：这四首曲子的容量参数叫 maxNotes，不能叫 max ——
   windows.h（经 common.h 进来）会定义 max(a,b) 宏，同名参数会被展开成语法错误。
   现在能编过只是因为宏展开的时机凑巧，属于潜在雷，改名消掉。 */
#define MP_SR       ((double)MUSIC_PROC_RATE)
#define MP_PI       3.14159265358979323846
#define MP_TWO_PI   (2.0 * MP_PI)

#define MP_MAX_VOICES  64
#define MP_MAX_LOOP_NOTES 1024
#define MP_MAX_SCHED   2048
#define MP_SCENE_FADE_OUT 0.8
#define MP_SCENE_FADE_IN  1.2
#define MP_LOOKAHEAD      0.8

enum { TRI = 0, SINE, NOISE };

typedef struct {
    double start;      /* 相对于本圈起点的秒数 */
    double dur, freq, gain;
    int    osc;
    double attack, decay, sustain, release;
    double cutoff;
    double pan;
    double detune;
} Note;

static uint32_t g_seed = 0xDEADBEEFu;
static double frand(void) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return (double)(g_seed >> 8) / 16777216.0;
}
static double mf(int p) { return 440.0 * pow(2.0, (p - 69) / 12.0); }

static double adsr(double t, double dur, double a, double d, double s, double r) {
    if (dur <= 0) return 0;
    if (a < 0) a = 0;
    if (d < 0) d = 0;
    if (r < 0) r = 0;
    if (s < 0) s = 0;
    if (s > 1) s = 1;
    double A = a * dur, D = d * dur, R = r * dur, tot = A + D + R;
    if (tot > dur) { double k = dur / tot; A *= k; D *= k; R *= k; }
    if (t < A) return (A > 0) ? t / A : 1;
    if (t < A + D) return (D > 0) ? 1 - (1 - s) * (t - A) / D : s;
    double rel = dur - R;
    if (t < rel) return s;
    if (R > 0) return s * (rel + R - t) / R;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 混响：4 个梳状 + 2 个全通（和 horror3 相同，缓冲按 22050Hz 缩放）    */
/* ------------------------------------------------------------------ */
typedef struct { float buf[1024]; int size, pos, pad; float fb; } Comb;
typedef struct { float buf[128];  int size, pos; } Allpass;
typedef struct { Comb c[4]; Allpass a[2]; } Reverb;

static void rev_init(Reverb* r) {
    static const double cs[4] = { 0.0297, 0.0371, 0.0411, 0.0437 };
    static const float  cf[4] = { 0.80f, 0.79f, 0.78f, 0.77f };
    static const double as[2] = { 0.0050, 0.0017 };
    for (int i = 0; i < 4; i++) {
        r->c[i].size = (int)(MP_SR * cs[i]);
        r->c[i].pos = 0;
        r->c[i].fb = cf[i];
        memset(r->c[i].buf, 0, sizeof(r->c[i].buf));
    }
    for (int i = 0; i < 2; i++) {
        r->a[i].size = (int)(MP_SR * as[i]);
        r->a[i].pos = 0;
        memset(r->a[i].buf, 0, sizeof(r->a[i].buf));
    }
}

static float rev_tick(Reverb* r, float in) {
    float s = 0;
    for (int i = 0; i < 4; i++) {
        float o = r->c[i].buf[r->c[i].pos];
        r->c[i].buf[r->c[i].pos] = in + o * r->c[i].fb;
        if (++r->c[i].pos >= r->c[i].size) r->c[i].pos = 0;
        s += o;
    }
    s *= 0.25f;
    for (int i = 0; i < 2; i++) {
        float o = r->a[i].buf[r->a[i].pos];
        r->a[i].buf[r->a[i].pos] = s + o * 0.5f;
        if (++r->a[i].pos >= r->a[i].size) r->a[i].pos = 0;
        s = o - s * 0.5f;
    }
    return s;
}

/* ------------------------------------------------------------------ */
/* 四个 composer：音符生成逻辑原样保留                                 */
/* ------------------------------------------------------------------ */
#define PUSH(...) do { if (ne < maxNotes) notes[ne++] = (Note)__VA_ARGS__; } while (0)

/* ============ 1: Hollow — 两段式和弦，旋律不规整 ============ */
static int comp_hollow(Note* notes, int maxNotes, double dur) {
    int ne = 0;
    /* 前半段：Am-F-Dm-E；后半段：Dm-Bb-Gm-A */
    static const int ch1[4][3] = { {45,48,52},{41,45,48},{38,41,45},{40,44,47} };
    static const int bs1[4] = { 33,29,26,28 };
    static const int ch2[4][3] = { {38,41,45},{46,50,53},{43,46,50},{45,49,52} };
    static const int bs2[4] = { 26,34,31,33 };
    static const int m1[8] = { 69,72,71,69, 69,67,65,64 };
    static const int m2[8] = { 65,64,62,60, 62,63,64,65 };

    double cd = 8.0, half = dur / 2.0;

    for (double t = 0; t < dur - 0.01; t += 32.0) {
        for (int c = 0; c < 4; c++) {
            double ct = t + c * cd;
            if (ct >= dur - 0.01) break;
            int second = (ct >= half);
            const int (*ch)[3] = second ? ch2 : ch1;
            const int* bs = second ? bs2 : bs1;
            const int* mel = second ? m2 : m1;

            for (int i = 0; i < 3; i++) {
                PUSH({ct,cd,mf(ch[c][i]),0.10,TRI,0.30,0.20,0.85,0.35,1200.0,
                      (i - 1) * 0.35,0.0});
                PUSH({ct,cd,mf(ch[c][i]),0.07,TRI,0.30,0.20,0.85,0.35,1000.0,
                      (i - 1) * 0.35,7.0});
            }
            PUSH({ct,cd,mf(bs[c]),0.20,SINE,0.30,0.15,0.85,0.35,0.0,0.0,0.0});

            /* 前半段每和弦 2 个旋律音，后半段 4 个——密度有变化 */
            int nm = second ? 4 : 2;
            for (int m = 0; m < nm; m++) {
                if (frand() < 0.15) continue;           /* 随机跳过 */
                double mt = ct + m * (cd / nm);
                double dly = (frand() < 0.3) ? frand() * 0.4 : 0.0;
                double nd = 1.2 + frand() * 0.9;
                PUSH({mt + dly,nd,mf(mel[m]),0.15,TRI,0.05,0.10,0.70,0.45,
                      2500.0,0.0,0.0});
            }
            /* 装饰音：偶尔插入一个快速经过音 */
            if (frand() < 0.40) {
                double dt = ct + 1.0 + frand() * 3.0;
                int dn = mel[0] + ((frand() < 0.5) ? 1 : -1);
                PUSH({dt,0.30,mf(dn),0.08,TRI,0.01,0.05,0.40,0.30,3000.0,0.0,0.0});
            }
        }
    }
    return ne;
}

/* ============ 2: Crawl — 深海感：低 drone + 高音点 + 中频涌动 ============ */
static int comp_crawl(Note* notes, int maxNotes, double dur) {
    int ne = 0;
    g_seed = 0xFEEDFACEu;

    /* L1：D1 极低 drone，每 30 秒一个"呼吸"——12秒渐强、6秒保持、12秒渐弱 */
    for (double t = 0; t < dur - 0.01; t += 30.0) {
        double len = (t + 30.0 > dur) ? dur - t : 30.0;
        PUSH({t,len,mf(26),0.18,SINE,0.40,0.05,0.90,0.40,0.0,0.0,0.0});
        PUSH({t,len,mf(26),0.11,SINE,0.40,0.05,0.90,0.40,0.0,0.0,12.0});
    }

    /* L2：稀疏的高音"点"——像深海中的发光体，短促、稀疏、随机音高 */
    static const int dots[] = { 62,65,69,72,77,81,86 };
    double t = 8.0;
    while (t < dur - 5.0) {
        int p = dots[(int)(frand() * 7)];
        double g = 0.03 + frand() * 0.04;
        PUSH({t,0.8 + frand() * 1.5,mf(p),g,TRI,0.02,0.15,0.30,0.60,
              3000.0,(frand() - 0.5) * 0.8,0.0});
        t += 3.0 + frand() * 6.0;
    }

    /* L3：中频"暗流"——偶尔一股低频涌上来 */
    t = 15.0;
    while (t < dur - 15.0) {
        double f = mf(50 + (int)(frand() * 5));
        PUSH({t,6.0 + frand() * 4.0,f,0.06,SINE,0.30,0.10,0.70,0.40,
              0.0,(frand() - 0.5) * 0.6,0.0});
        t += 20.0 + frand() * 15.0;
    }

    /* L4：极低的下潜音——极少、极低 */
    t = 25.0;
    while (t < dur - 10.0) {
        PUSH({t,8.0,mf(14),0.10,SINE,0.20,0.10,0.80,0.30,0.0,0.0,0.0});
        t += 35.0 + frand() * 20.0;
    }
    return ne;
}

/* ============ 3: Fracture — 保持，稍加丰富 ============ */
static int comp_fracture(Note* notes, int maxNotes, double dur) {
    int ne = 0;
    static const int ch[4][3] = { {40,43,47},{45,48,52},{38,41,45},{47,50,54} };
    double cd = 4.0, lp = 16.0;
    for (double t = 0; t < dur - 0.01; t += lp) {
        double drift = (t / dur) * 40.0;
        for (int c = 0; c < 4; c++) {
            double ct = t + c * cd;
            for (int i = 0; i < 3; i++) {
                PUSH({ct,cd,mf(ch[c][i]),0.11,TRI,0.20,0.15,0.88,0.25,1300.0,
                      (i - 1) * 0.40,drift});
                PUSH({ct,cd,mf(ch[c][i]),0.07,TRI,0.20,0.15,0.88,0.25,1100.0,
                      (i - 1) * 0.40,drift + 8.0});
            }
            PUSH({ct,cd,mf(ch[c][0] - 12),0.22,SINE,0.15,0.10,0.85,0.30,0.0,0.0,0.0});
            for (int m = 0; m < 3; m++)
                PUSH({ct + m * 1.3,1.2,mf(ch[c][m % 3] + 24),0.10,TRI,0.05,0.10,0.50,0.40,
                      2500.0,(m - 1) * 0.30,drift});
        }
    }
    return ne;
}

/* ============ 4: Lull — 音乐盒开场，逐渐加低音和走调 ============ */
static int comp_lull(Note* notes, int maxNotes, double dur) {
    int ne = 0;
    g_seed = 0xBEEFCAFEu;

    /* L1：音乐盒旋律——从非常轻的单一音开始，慢慢变密 */
    static const int box[] = { 72,76,79,76,72,76,79,84,83,79,76,72,
                               74,77,81,77,74,77,81,86,84,81,77,74 };
    double t = 0.20;                     /* 第一个音在 0.2 秒，干净出场 */
    int mi = 0;
    while (t < dur - 6.0) {
        double drift = (t / dur) * 100.0;    /* 走调随时间累积：最多 100 音分 */
        int p = box[mi % 24];
        /* 随曲子推进，音乐盒音逐渐变密 */
        double interval = 0.9;
        if (t > dur * 0.5) interval = 0.7;
        if (t > dur * 0.8) interval = 0.55;

        /* 偶尔跳过、偶尔延迟 */
        if (frand() > 0.06) {
            double dly = (frand() < 0.2) ? frand() * 0.3 : 0.0;
            PUSH({t + dly,1.5,mf(p),0.11,TRI,0.01,0.30,0.20,0.60,
                  4000.0,0.0,drift});
            PUSH({t + dly,1.0,mf(p + 12),0.035,SINE,0.005,0.20,0.10,0.70,
                  0.0,0.0,drift});
        }
        t += interval;
        mi++;
    }

    /* L2：30 秒后低音缓慢浮现 */
    for (double s = 30.0; s < dur - 5.0; s += 20.0) {
        double fi = (s - 30.0) / (dur - 30.0);
        double g = 0.05 + fi * 0.16;
        PUSH({s,20.0,mf(36),g,SINE,0.25,0.10,0.85,0.30,0.0,0.0,0.0});
    }

    /* L3：60 秒后加入小二度低音，和 C2 形成不协和 */
    for (double s = 60.0; s < dur - 5.0; s += 15.0) {
        double fi = (s - 60.0) / (dur - 60.0);
        double g = 0.04 + fi * 0.10;
        PUSH({s,12.0,mf(37),g,SINE,0.30,0.10,0.80,0.35,0.0,0.0,0.0});
    }

    /* L4：后半段中频暗涌，走调更明显 */
    for (double s = 100.0; s < dur - 10.0; s += 8.0) {
        double drift = (s / dur) * 140.0;
        PUSH({s,6.0,mf(48 + (int)(frand() * 6)),0.06,TRI,0.20,0.15,0.70,0.40,
              1500.0,(frand() - 0.5) * 0.6,drift});
    }
    return ne;
}

typedef struct {
    const char* name;
    double dur;
    /* 响度校准：离线整曲测得的 RMS 反推，目标约 0.125（约 -18 dBFS）。
       数值只影响音量，不改变音色；改动 composer 后需要重测。 */
    float norm;
    int (*comp)(Note*, int, double);
} Track;

static const Track g_tracks[] = {
    { "Hollow",   192.0, 2.00f, comp_hollow   },
    { "Crawl",    168.0, 2.80f, comp_crawl    },
    { "Fracture", 144.0, 1.74f, comp_fracture },
    { "Lull",     200.0, 3.60f, comp_lull     },
};
#define MP_TRACK_COUNT ((int)(sizeof(g_tracks) / sizeof(g_tracks[0])))

/* ------------------------------------------------------------------ */
/* 实时调度与渲染                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    Note note;
    double phase, lp1, lp2, freq, gl, gr, alpha, pos;
    int active;
} Voice;

static Voice g_voices[MP_MAX_VOICES];
static Note g_sched[MP_MAX_SCHED];
static int g_schedCount, g_schedCursor;
static double g_playhead;       /* 当前曲目已播放秒数 */
static double g_loopDur;        /* 当前曲目一圈长度 */
static double g_nextLoop;       /* 下一圈绝对起点（秒） */
static int g_loopIndex;
static int g_curTrack = -1;

static Reverb g_rev[2];
static float g_hpX[2], g_hpY[2];
static float g_gainSmooth;
static float g_norm = 1.0f;

/* 主线程 → 音频线程的两个原子量 */
static volatile LONG g_sceneRequest = MUSIC_PROC_SILENT;
static volatile LONG g_gainMilli = 0;

static int noteCmp(const void* a, const void* b) {
    const Note* x = (const Note*)a;
    const Note* y = (const Note*)b;
    if (x->start < y->start) return -1;
    if (x->start > y->start) return 1;
    return 0;
}

static void scheduleLoop(int index) {
    static Note tmp[MP_MAX_LOOP_NOTES];
    if (g_curTrack < 0) return;
    const Track* tr = &g_tracks[g_curTrack];

    /* 每圈换一个种子：和弦骨架不变，装饰音和随机跳过不重复 */
    g_seed = 0xDEADBEEFu ^ ((uint32_t)index * 2654435761u);
    int ne = tr->comp(tmp, MP_MAX_LOOP_NOTES, tr->dur);
    double base = index * tr->dur;

    /* 先丢掉已经被渲染过的音符，再追加下一圈，数组不会无限长 */
    if (g_schedCursor > 0) {
        memmove(g_sched, g_sched + g_schedCursor,
                (size_t)(g_schedCount - g_schedCursor) * sizeof(Note));
        g_schedCount -= g_schedCursor;
        g_schedCursor = 0;
    }
    for (int i = 0; i < ne && g_schedCount < MP_MAX_SCHED; i++) {
        /* 起点已经越过圈尾的音符，原作里也会被裁掉；丢掉才能保证不压到下一圈 */
        if (tmp[i].start < 0.0 || tmp[i].start >= tr->dur - 1e-6) continue;
        g_sched[g_schedCount] = tmp[i];
        g_sched[g_schedCount].start += base;
        g_schedCount++;
    }
    if (g_schedCount > 1)
        qsort(g_sched, (size_t)g_schedCount, sizeof(Note), noteCmp);
}

static void voiceStart(const Note* n) {
    int slot = -1;
    for (int i = 0; i < MP_MAX_VOICES; i++) {
        if (!g_voices[i].active) { slot = i; break; }
    }
    if (slot < 0) return;
    Voice* v = &g_voices[slot];
    v->note = *n;
    v->phase = 0.0;
    v->lp1 = v->lp2 = 0.0;
    v->pos = 0.0;
    v->freq = n->freq * pow(2.0, n->detune / 1200.0);
    v->alpha = (n->cutoff > 0.0) ? 1.0 - exp(-MP_TWO_PI * n->cutoff / MP_SR) : 1.0;
    double ang = (n->pan + 1.0) * 0.25 * MP_PI;
    v->gl = cos(ang);
    v->gr = sin(ang);
    v->active = 1;
}

static double voiceSample(Voice* v) {
    const Note* n = &v->note;
    double env = adsr(v->pos, n->dur, n->attack, n->decay, n->sustain, n->release);
    double s;
    switch (n->osc) {
        case TRI:  s = 2.0 * fabs(2.0 * v->phase / MP_TWO_PI - 1.0) - 1.0; break;
        case SINE: s = sin(v->phase); break;
        default:   s = frand() * 2.0 - 1.0; break;
    }
    v->phase += MP_TWO_PI * v->freq / MP_SR;
    if (v->phase >= MP_TWO_PI) v->phase -= MP_TWO_PI;
    if (n->cutoff > 0.0) {
        v->lp1 += v->alpha * (s - v->lp1);
        v->lp2 += v->alpha * (v->lp1 - v->lp2);
        s = v->lp2;
    }
    s *= env * n->gain;
    v->pos += 1.0 / MP_SR;
    if (v->pos >= n->dur) v->active = 0;
    return s;
}

static float postFx(int ch, float x) {
    float d = x - g_hpX[ch] + 0.97f * g_hpY[ch];
    g_hpX[ch] = x;
    g_hpY[ch] = d;
    float wet = rev_tick(&g_rev[ch], d);
    float y = (x * 0.85f + wet * 0.15f) * g_norm;
    return tanhf(y);
}

static void startTrack(int idx) {
    g_curTrack = idx;
    g_schedCount = g_schedCursor = 0;
    g_playhead = 0.0;
    g_loopIndex = 0;
    memset(g_voices, 0, sizeof(g_voices));
    g_hpX[0] = g_hpY[0] = g_hpX[1] = g_hpY[1] = 0.0f;
    rev_init(&g_rev[0]);
    rev_init(&g_rev[1]);
    if (idx >= 0) {
        g_loopDur = g_tracks[idx].dur;
        g_norm = g_tracks[idx].norm;
        g_nextLoop = g_loopDur;
        scheduleLoop(0);
    } else {
        g_loopDur = 1.0;
        g_norm = 1.0f;
        g_nextLoop = 1.0;
    }
}

/* 场景 → 曲目。-1 表示静音（胜利/失败只留音效）。 */
static int trackForScene(int scene) {
    switch (scene) {
        case MUSIC_MENU:    return 3;   /* Lull    */
        case MUSIC_EXPLORE: return 0;   /* Hollow  */
        case MUSIC_CHASE:   return 2;   /* Fracture*/
        case MUSIC_SAFE:    return 1;   /* Crawl   */
        default:            return -1;  /* Victory / Defeat：静音 */
    }
}

/* 场景切换：先淡出旧曲，再重置合成器并淡入新曲。 */
static int  g_desiredTrack = -2;   /* -2 = 还没收到过场景 */
static int  g_switching;
static float g_sceneFade;

static void updateScene(int req, double dt) {
    int want = trackForScene(req);
    if (want != g_desiredTrack) {
        g_desiredTrack = want;
        if (want != g_curTrack) g_switching = 1;
        else g_switching = 0;      /* 又切回正在放的曲子：取消淡出 */
    }
    if (g_switching) {
        g_sceneFade -= (float)(dt / MP_SCENE_FADE_OUT);
        if (g_sceneFade <= 0.0f) {
            g_sceneFade = 0.0f;
            startTrack(g_desiredTrack);
            g_switching = 0;
        }
    } else if (g_sceneFade < 1.0f) {
        g_sceneFade += (float)(dt / MP_SCENE_FADE_IN);
        if (g_sceneFade > 1.0f) g_sceneFade = 1.0f;
    }
}

/* 提前把下一圈排进来，保证圈边界上的音符样本级准时 */
static void ensureScheduled(double t) {
    if (g_curTrack < 0) return;
    while (g_nextLoop <= t + MP_LOOKAHEAD) {
        g_loopIndex++;
        scheduleLoop(g_loopIndex);
        g_nextLoop = (double)(g_loopIndex + 1) * g_loopDur;
    }
}

void MusicProcMix(float* left, float* right, int frames) {
    if (!left || !right || frames <= 0) return;

    double dt = (double)frames / MP_SR;
    if (dt > 0.2) dt = 0.2;

    updateScene((int)InterlockedCompareExchange(&g_sceneRequest, 0, 0), dt);

    float target = (float)InterlockedCompareExchange(&g_gainMilli, 0, 0) * 0.001f;
    target *= g_sceneFade;
    float k = (float)(dt * 12.0);
    if (k > 1.0f) k = 1.0f;
    g_gainSmooth += (target - g_gainSmooth) * k;

    if (g_curTrack < 0) {
        memset(left, 0, (size_t)frames * sizeof(float));
        memset(right, 0, (size_t)frames * sizeof(float));
        g_gainSmooth = 0.0f;
        return;
    }

    ensureScheduled(g_playhead);

    for (int i = 0; i < frames; i++) {
        double t = g_playhead + (double)i / MP_SR;
        while (g_schedCursor < g_schedCount && g_sched[g_schedCursor].start <= t) {
            voiceStart(&g_sched[g_schedCursor]);
            g_schedCursor++;
        }
        double l = 0.0, r = 0.0;
        for (int v = 0; v < MP_MAX_VOICES; v++) {
            Voice* vo = &g_voices[v];
            if (!vo->active) continue;
            double s = voiceSample(vo);
            l += s * vo->gl;
            r += s * vo->gr;
        }
        left[i]  = postFx(0, (float)l) * g_gainSmooth;
        right[i] = postFx(1, (float)r) * g_gainSmooth;
    }
    g_playhead += dt;
}

void MusicProcSetScene(int scene) {
    InterlockedExchange(&g_sceneRequest, (LONG)scene);
}

void MusicProcSetGain(float gain) {
    if (!(gain >= 0.0f)) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    InterlockedExchange(&g_gainMilli, (LONG)(gain * 1000.0f + 0.5f));
}

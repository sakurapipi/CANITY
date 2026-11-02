/*
 * Canity — 音频层实现：程序化合成 + 软件混音
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "audio.h"
#include "game.h"
#include "music_proc.h"


/* ------------------------------------------------------------------ */
/* 音效：程序化合成 + 软件混音（不依赖任何素材文件）                    */
/* ------------------------------------------------------------------ */

#define AUDIO_RATE 22050
#define AUDIO_BUFS 3
#define AUDIO_SAMPLES 1024
#define MAX_VOICES 8

/* 音乐总线输出的是 [-1,1] 浮点，留约 2dB 余量和音效叠加。
   采样率必须和 music_proc.c 一致，否则音高会整体跑调。 */
#define MUSIC_MIX_SCALE 26214.0f
typedef char AudioMusicRateMustMatch[(AUDIO_RATE == MUSIC_PROC_RATE) ? 1 : -1];

static short g_sndStep[2205];      /* 0.10s 脚步 */
static short g_sndHeart[9922];     /* 0.45s 心跳 */
static short g_sndPickup[6615];    /* 0.30s 拾取 */
static short g_sndPower[19845];    /* 0.90s 通电 */
static short g_sndGate[15435];     /* 0.70s 过关 */
static short g_sndCrank[2647];     /* 0.12s 手摇 */
static short g_sndScream[13230];   /* 0.60s 尖叫 */
static short g_sndGlass[6615];     /* 0.30s 玻璃碎裂 */
static short g_sndSearch[3307];    /* 0.15s 翻箱子：金属刮擦+闷响 */
static short g_sndThrow[4410];     /* 0.20s 投掷：呼啸+出手音 */
/* v3.3 投掷物重做配套的三个音效 */
static short g_sndGlue[6615];      /* 0.30s 胶水：黏稠噗叽 */
static short g_sndEcho[9922];      /* 0.45s 回声：短叮 + 回响 */
static short g_sndDecoy[13230];    /* 0.60s 玩偶：嘎吱叫两声 */
/* v2.4.89 两只新怪的声音 */
static short g_sndWarden[35280];    /* 1.60s 守门人：极低频压迫 */
static short g_sndScavenger[26460]; /* 1.20s 清道夫：刺耳刮擦 */
static short g_sndScavStep[2646];   /* 0.12s 清道夫脚步：高速摩擦 */

typedef struct { const short* data; int len, pos; float vol; int active; } Voice;

static Voice g_voices[MAX_VOICES];
static HWAVEOUT g_wave = NULL;
static WAVEHDR g_hdr[AUDIO_BUFS];
static short g_pcm[AUDIO_BUFS][AUDIO_SAMPLES * 2];
static HANDLE g_audioEvent = NULL;
static HANDLE g_audioThread = NULL;
static volatile LONG g_audioQuit = 0;
static CRITICAL_SECTION g_audioLock;
static int g_audioOn = 0;

static void AudioCallback(HWAVEOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    (void)h; (void)inst; (void)p1; (void)p2;
    if (msg == WOM_DONE && g_audioEvent) SetEvent(g_audioEvent);
}

static void MixInto(short* out) {
    /* 音效是单声道素材，居中复制到左右；音乐自带声像，直接进立体声总线。 */
    static int mix[AUDIO_SAMPLES];
    static float musicL[AUDIO_SAMPLES];
    static float musicR[AUDIO_SAMPLES];
    int i;

    for (i = 0; i < AUDIO_SAMPLES; i++) {
        mix[i] = 0;
        musicL[i] = musicR[i] = 0.0f;
    }
    MusicProcMix(musicL, musicR, AUDIO_SAMPLES);

    EnterCriticalSection(&g_audioLock);
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &g_voices[v];
        if (!vo->active) continue;
        for (i = 0; i < AUDIO_SAMPLES; i++) {
            if (vo->pos >= vo->len) { vo->active = 0; break; }
            mix[i] += (int)(vo->data[vo->pos++] * vo->vol);
        }
    }
    LeaveCriticalSection(&g_audioLock);

    for (i = 0; i < AUDIO_SAMPLES; i++) {
        int l = mix[i] + (int)(musicL[i] * MUSIC_MIX_SCALE);
        int r = mix[i] + (int)(musicR[i] * MUSIC_MIX_SCALE);
        if (l > 32767) l = 32767;
        else if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        else if (r < -32768) r = -32768;
        out[i * 2] = (short)l;
        out[i * 2 + 1] = (short)r;
    }
}

static DWORD WINAPI AudioThreadMain(LPVOID param) {
    (void)param;
    while (!g_audioQuit) {
        WaitForSingleObject(g_audioEvent, 100);
        for (int i = 0; i < AUDIO_BUFS; i++) {
            if (g_hdr[i].dwFlags & WHDR_DONE) {
                MixInto(g_pcm[i]);
                g_hdr[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(g_wave, &g_hdr[i], sizeof(WAVEHDR));
            }
        }
    }
    return 0;
}

void PlaySfx(int id, float vol) {
    vol *= volumeF;
    if (!g_audioOn || vol <= 0.001f) return;
    const short* data = NULL;
    int len = 0;
    switch (id) {
        case SFX_STEP:   data = g_sndStep;  len = (int)(sizeof(g_sndStep) / sizeof(short)); break;
        case SFX_HEART:  data = g_sndHeart; len = (int)(sizeof(g_sndHeart) / sizeof(short)); break;
        case SFX_PICKUP: data = g_sndPickup;len = (int)(sizeof(g_sndPickup) / sizeof(short)); break;
        case SFX_POWER:  data = g_sndPower; len = (int)(sizeof(g_sndPower) / sizeof(short)); break;
        case SFX_GATE:   data = g_sndGate;  len = (int)(sizeof(g_sndGate) / sizeof(short)); break;
        case SFX_CRANK:  data = g_sndCrank; len = (int)(sizeof(g_sndCrank) / sizeof(short)); break;
        case SFX_SCREAM: data = g_sndScream;len = (int)(sizeof(g_sndScream) / sizeof(short)); break;
        case SFX_GLASS:  data = g_sndGlass; len = (int)(sizeof(g_sndGlass) / sizeof(short)); break;
        case SFX_SEARCH: data = g_sndSearch;len = (int)(sizeof(g_sndSearch) / sizeof(short)); break;
        case SFX_THROW:  data = g_sndThrow; len = (int)(sizeof(g_sndThrow) / sizeof(short)); break;
        case SFX_GLUE:   data = g_sndGlue;  len = (int)(sizeof(g_sndGlue) / sizeof(short));  break;
        case SFX_ECHO:   data = g_sndEcho;  len = (int)(sizeof(g_sndEcho) / sizeof(short));  break;
        case SFX_DECOY:  data = g_sndDecoy; len = (int)(sizeof(g_sndDecoy) / sizeof(short)); break;
        case SFX_WARDEN: data = g_sndWarden; len = (int)(sizeof(g_sndWarden) / sizeof(short)); break;
        case SFX_SCAVENGER: data = g_sndScavenger; len = (int)(sizeof(g_sndScavenger) / sizeof(short)); break;
        case SFX_SCAV_STEP: data = g_sndScavStep; len = (int)(sizeof(g_sndScavStep) / sizeof(short)); break;
        default: return;
    }
    EnterCriticalSection(&g_audioLock);
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++) if (!g_voices[i].active) { slot = i; break; }
    if (slot < 0) slot = 0;    /* 全忙就抢第一路 */
    g_voices[slot].data = data;
    g_voices[slot].len = len;
    g_voices[slot].pos = 0;
    g_voices[slot].vol = vol;
    g_voices[slot].active = 1;
    LeaveCriticalSection(&g_audioLock);
}

/* 合成：噪声、正弦、扫频，全部给 16bit 单声道 */
static void GenSounds(void) {
    unsigned int seed = 12345u;
    /* 脚步：低通噪声 + 快速衰减 */
    float lp = 0.0f;
    for (int i = 0; i < (int)(sizeof(g_sndStep) / sizeof(short)); i++) {
        seed = seed * 1664525u + 1013904223u;
        float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
        lp = lp * 0.72f + n * 0.28f;
        float t = (float)i / AUDIO_RATE;
        float env = expf(-t * 42.0f);
        float v = lp * env * 0.55f;
        g_sndStep[i] = (short)(v * 32000.0f);
    }
    /* 心跳：两声低频闷响 */
    int hl = (int)(sizeof(g_sndHeart) / sizeof(short));
    for (int i = 0; i < hl; i++) {
        float t = (float)i / AUDIO_RATE;
        float v = 0.0f;
        float th[2] = {0.0f, 0.17f};
        for (int k = 0; k < 2; k++) {
            float d = t - th[k];
            if (d < 0.0f || d > 0.22f) continue;
            float env = expf(-d * 16.0f);
            v += sinf(6.2831f * 52.0f * d) * env * (k == 0 ? 1.0f : 0.72f);
        }
        g_sndHeart[i] = (short)(v * 0.55f * 32000.0f);
    }
    /* 拾取：两声上行三角音 */
    int pl = (int)(sizeof(g_sndPickup) / sizeof(short));
    for (int i = 0; i < pl; i++) {
        float t = (float)i / AUDIO_RATE;
        float f = (t < 0.10f) ? 620.0f : 930.0f;
        float env = (t < 0.10f) ? (1.0f - t / 0.10f) : expf(-(t - 0.10f) * 22.0f);
        float v = sinf(6.2831f * f * t) * env * 0.22f;
        g_sndPickup[i] = (short)(v * 32000.0f);
    }
    /* 通电：低频嗡鸣上扫 + 开头一声咔哒 */
    int wl = (int)(sizeof(g_sndPower) / sizeof(short));
    for (int i = 0; i < wl; i++) {
        float t = (float)i / AUDIO_RATE;
        float f = 70.0f + 150.0f * (t / 0.9f);
        float env = expf(-t * 1.6f);
        float v = sinf(6.2831f * f * t) * env * 0.30f;
        if (t < 0.02f) v += ((rand() % 2000) - 1000) / 1000.0f * 0.35f;
        g_sndPower[i] = (short)(v * 32000.0f);
    }
    /* 手摇：两声棘轮咔哒 */
    {
        int cl = (int)(sizeof(g_sndCrank) / sizeof(short));
        for (int i = 0; i < cl; i++) {
            float t = (float)i / AUDIO_RATE;
            float v = 0.0f;
            float ticks[2] = {0.0f, 0.055f};
            for (int k = 0; k < 2; k++) {
                float d = t - ticks[k];
                if (d < 0.0f || d > 0.03f) continue;
                seed = seed * 1664525u + 1013904223u;
                float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
                v += n * expf(-d * 140.0f) * 0.5f;
            }
            g_sndCrank[i] = (short)(v * 32000.0f);
        }
    }
    /* 尖叫：锯齿感的上扫 + 颤抖 */
    {
        int sl = (int)(sizeof(g_sndScream) / sizeof(short));
        for (int i = 0; i < sl; i++) {
            float t = (float)i / AUDIO_RATE;
            float f = 420.0f + 260.0f * sinf(t * 3.5f) + 90.0f * sinf(t * 41.0f);
            float env = expf(-t * 3.2f);
            float v = sinf(6.2831f * f * t + 2.0f * sinf(t * 55.0f));
            g_sndScream[i] = (short)(v * env * 0.42f * 32000.0f);
        }
    }
    /* 玻璃碎裂：高通噪声爆发 + 几片细碎的高频余响 */
    {
        int bl = (int)(sizeof(g_sndGlass) / sizeof(short));
        float hp = 0.0f;
        for (int i = 0; i < bl; i++) {
            float t = (float)i / AUDIO_RATE;
            seed = seed * 1664525u + 1013904223u;
            float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
            hp = n - hp * 0.35f;                 /* 一阶高通：去掉低频，保留脆响 */
            float v = hp * expf(-t * 26.0f);
            /* 三片碎玻璃的高频点缀 */
            for (int k = 0; k < 3; k++) {
                float st = 0.045f + k * 0.038f;
                float d = t - st;
                if (d >= 0.0f && d < 0.012f) {
                    seed = seed * 1664525u + 1013904223u;
                    float sn = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
                    v += sn * expf(-d * 300.0f) * 0.55f;
                }
            }
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            g_sndGlass[i] = (short)(v * 0.55f * 32000.0f);
        }
    }
    /* 过关：噪声下扫的气流声 */
    int gl = (int)(sizeof(g_sndGate) / sizeof(short));
    lp = 0.0f;
    for (int i = 0; i < gl; i++) {
        seed = seed * 1664525u + 1013904223u;
        float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
        float t = (float)i / AUDIO_RATE;
        float k = 0.15f + 0.6f * (t / 0.7f);
        lp = lp * (1.0f - k) + n * k;
        float env = sinf(3.1416f * (t / 0.7f));
        g_sndGate[i] = (short)(lp * env * 0.5f * 32000.0f);
    }

    /* 翻箱子：金属刮擦 + 木板闷响 */
    {
        int sl = (int)(sizeof(g_sndSearch) / sizeof(short));
        float lp2 = 0.0f;
        for (int i = 0; i < sl; i++) {
            float t = (float)i / AUDIO_RATE;
            seed = seed * 1664525u + 1013904223u;
            float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
            /* 金属刮擦：高通噪声 */
            float hp = n - lp2 * 0.3f;
            lp2 = n;
            float env = expf(-t * 28.0f);
            float v = hp * env * 0.35f;
            /* 木板闷响：低频正弦下扫 */
            float f = 180.0f - 120.0f * (t / 0.15f);
            v += sinf(6.2831f * f * t) * expf(-t * 18.0f) * 0.25f;
            g_sndSearch[i] = (short)(v * 32000.0f);
        }
    }

    /* 投掷：呼啸风声 + 出手闷响 */
    {
        int tl = (int)(sizeof(g_sndThrow) / sizeof(short));
        for (int i = 0; i < tl; i++) {
            float t = (float)i / AUDIO_RATE;
            seed = seed * 1664525u + 1013904223u;
            float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
            /* 呼啸：带颤音的带通噪声 */
            float bp = n * 0.5f;
            float env = expf(-t * 12.0f);
            float v = bp * env * 0.28f;
            /* 出手：低频脉冲 */
            if (t < 0.03f) {
                float p = 1.0f - t / 0.03f;
                v += sinf(6.2831f * 60.0f * t) * expf(-t * 50.0f) * 0.4f * p;
            }
            g_sndThrow[i] = (short)(v * 32000.0f);
        }
    }

    /* ===== v3.3 投掷物重做配套的音效 =====
       全部现场合成，不引入外部资源（项目一直是零外部资产的单 exe）。 */

    /* 胶水：黏稠的「噗叽」—— 低频正弦快速下滑 + 噪声湿滑感 */
    {
        int n = (int)(sizeof(g_sndGlue) / sizeof(short));
        float lp2 = 0.0f;
        unsigned int s2 = 777u;
        for (int i = 0; i < n; i++) {
            float t = (float)i / AUDIO_RATE;
            s2 = s2 * 1664525u + 1013904223u;
            float nz = ((float)((s2 >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
            lp2 = lp2 * 0.88f + nz * 0.12f;                 /* 重低通 = 黏糊感 */
            float f = 210.0f * expf(-t * 7.0f) + 55.0f;      /* 音高快速滑到底 */
            float env = expf(-t * 9.0f);
            float v = (sinf(6.2831f * f * t) * 0.7f + lp2 * 0.5f) * env * 0.42f;
            g_sndGlue[i] = (short)(v * 32000.0f);
        }
    }

    /* 回声：短促「叮」+ 两次延迟回响（模拟洞穴回声） */
    {
        int n = (int)(sizeof(g_sndEcho) / sizeof(short));
        float echoTap[3] = {0.0f, 0.13f, 0.26f};
        float echoAmp[3] = {1.0f, 0.42f, 0.18f};
        for (int i = 0; i < n; i++) {
            float t = (float)i / AUDIO_RATE;
            float v = 0.0f;
            for (int k2 = 0; k2 < 3; k2++) {
                float d = t - echoTap[k2];
                if (d < 0.0f || d > 0.30f) continue;
                float env = expf(-d * 26.0f);
                /* 2350Hz 的金属质感 + 一点点高频泛音 */
                v += (sinf(6.2831f * 2350.0f * d) * 0.7f
                    + sinf(6.2831f * 4700.0f * d) * 0.3f)
                    * env * echoAmp[k2];
            }
            v *= 0.30f;
            g_sndEcho[i] = (short)(v * 32000.0f);
        }
    }

    /* 玩偶：木头/金属的「嘎吱」——
       两段锯齿噪声（嘎 + 吱），音高上移，像被人拧了一下 */
    {
        int n = (int)(sizeof(g_sndDecoy) / sizeof(short));
        float segT[2] = {0.0f, 0.22f};
        for (int i = 0; i < n; i++) {
            float t = (float)i / AUDIO_RATE;
            float v = 0.0f;
            for (int k2 = 0; k2 < 2; k2++) {
                float d = t - segT[k2];
                if (d < 0.0f || d > 0.20f) continue;
                /* 第二段音高更高 = 被拧紧 */
                float base = (k2 == 0) ? 380.0f : 620.0f;
                float f = base + 140.0f * (d / 0.20f);
                /* 锯齿波：木头摩擦的粗糙感 */
                float ph = f * d;
                float saw = 2.0f * (ph - floorf(ph)) - 1.0f;
                float env = expf(-d * 14.0f);
                v += saw * env * (k2 == 0 ? 0.55f : 0.70f);
            }
            v *= 0.38f;
            g_sndDecoy[i] = (short)(v * 32000.0f);
        }
    }

    /* ==================================================================
     * v2.4.89 **守门人：极低频的压迫感**
     * ------------------------------------------------------------------
     * 调研依据（psychoacoustics）：
     *   · 20~60 Hz 的持续能量给的是**体感压迫**，不是"听到声音"
     *   · 「doom throb」—— 两个频率只差 2~4 Hz 会慢慢拍频，
     *     听感像心跳/引擎，制造"有巨大东西在靠近"
     *   · 非谐波泛音（1.4x / 2.7x）让它听起来**不自然**，
     *     像金属结构被强行驱动
     * ================================================================ */
    {
        int wl2 = (int)(sizeof(g_sndWarden) / sizeof(short));
        unsigned int s2 = 98765u;
        float brown = 0.0f;
        for (int i = 0; i < wl2; i++) {
            float t = (float)i / AUDIO_RATE;

            s2 = s2 * 1664525u + 1013904223u;
            float n = ((float)((s2 >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
            brown = brown * 0.96f + n * 0.04f;          /* 极低通 -> 棕噪 */

            float env = (t < 0.25f) ? (t / 0.25f) : expf(-(t - 0.25f) * 1.5f);

            float v = 0.0f;
            v += sinf(6.2831f * 41.0f * t)   * 0.55f;   /* 基频 */
            v += sinf(6.2831f * 43.5f * t)   * 0.45f;   /* 差 2.5Hz -> 隆隆拍频 */
            v += sinf(6.2831f * 20.5f * t)   * 0.50f;   /* 次谐波：体感 */
            v += sinf(6.2831f * 57.0f * t)   * 0.22f;   /* 非谐波 1.4x */
            v += sinf(6.2831f * 110.0f * t)  * 0.10f;   /* 非谐波 2.7x */

            v *= 0.85f + 0.15f * sinf(6.2831f * 0.8f * t);   /* 慢速起伏 */
            v = v * 0.30f + brown * 2.2f;

            v *= env;
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            g_sndWarden[i] = (short)(v * 32000.0f);
        }
    }

    /* ==================================================================
     * v2.4.89 **清道夫：刺耳到毛骨悚然**
     * ------------------------------------------------------------------
     * 调研依据：
     *   · **sharpness**：能量集中在 3 kHz 以上 = 针刺感
     *     （原文："Think dentist drill, not thunder"）
     *   · **roughness**：两个分量差 20~200 Hz，耳朵分不开 -> 砂纸感
     *   · **stick-slip**：不规则的微爆发（指甲刮玻璃）。
     *     **时间必须不规则**，规则了大脑会习惯
     *   · **FM 不稳**：调制本身随机游走，耳朵抓不到稳定音高
     * ================================================================ */
    {
        int sl = (int)(sizeof(g_sndScavenger) / sizeof(short));
        unsigned int s3 = 24680u;
        float fm = 0.0f;
        float bp1 = 0.0f, bp2 = 0.0f;
        int burstLeft = 0;
        float burstAmp = 0.0f;
        int nextBurst = 400;

        for (int i = 0; i < sl; i++) {
            float t = (float)i / AUDIO_RATE;

            s3 = s3 * 1664525u + 1013904223u;
            float r = ((float)((s3 >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;

            /* 随机 FM 游走 */
            fm = fm * 0.995f + r * 0.05f;
            if (fm > 1.0f) fm = 1.0f;
            if (fm < -1.0f) fm = -1.0f;
            float fmHz = fm * 720.0f;

            /* 白噪 -> 高频带通 */
            bp1 = bp1 + 0.55f * (r - bp1);
            bp2 = bp2 + 0.55f * (bp1 - bp2);
            float hp = r - bp2;

            float am70 = 0.55f + 0.45f * sinf(6.2831f * 70.0f * t);

            if (--nextBurst <= 0) {
                burstLeft = 60 + (int)(r * 40.0f);
                if (burstLeft < 30) burstLeft = 30;
                burstAmp = 0.7f + 0.3f * r;
                nextBurst = (int)(AUDIO_RATE * 0.011f) +
                            (int)(fabsf(r) * AUDIO_RATE * 0.026f);
            }

            float burst = 0.0f;
            if (burstLeft > 0) {
                burst = hp * burstAmp * 2.2f;
                burstLeft--;
            }

            float v = sinf(6.2831f * (3300.0f + fmHz) * t) * 0.34f;
            v += sinf(6.2831f * (3380.0f - fmHz * 0.6f) * t) * 0.26f;
            v += hp * 0.55f * am70;
            v += burst;

            float env;
            if (t < 0.01f) env = t / 0.01f;
            else if (t < 0.55f) env = 1.0f;
            else env = expf(-(t - 0.55f) * 4.0f);

            v *= env * 0.55f;
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            g_sndScavenger[i] = (short)(v * 32000.0f);
        }
    }

    /* ==================================================================
     * v2.4.92 **清道夫脚步：高速摩擦**
     * ------------------------------------------------------------------
     * 用户要求：「走路的音效不太好，走路没有声音。
     *            我需要那种高速摩擦、令人毛骨悚然的声音。」
     *
     * 和"拆东西"那只不同 —— 那只长、有节奏、像撬。
     * 脚步要**短、密集、没有音高**，核心是"有东西在地上拖"。
     *
     * 合成配方：
     *   主层   5~9 kHz 高速带通噪声（摩擦的"沙"）
     *   辅层   2.6 kHz 细窄音 + 快速颤音（金属尖细的"吱"）
     *   颗粒   随机微爆发（像爪子一下一下刮过地面）
     *   包络   极快起、0.12 秒内衰减完 —— 脚步不能拖沓
     * 每次播放音高/颗粒都不同（渲染位置随机），不会听腻
     * ================================================================ */
    {
        int n = (int)(sizeof(g_sndScavStep) / sizeof(short));
        unsigned int ss = 13579u;
        float b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
        int grit = 0;
        float gritAmp = 0.0f;
        int nextGrit = 30;

        for (int i = 0; i < n; i++) {
            float t = (float)i / AUDIO_RATE;

            ss = ss * 1664525u + 1013904223u;
            float r = ((float)((ss >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;

            /* 三级高通：把噪声推到 5kHz 以上 */
            b1 = b1 + 0.72f * (r - b1);
            b2 = b2 + 0.72f * (b1 - b2);
            b3 = b3 + 0.72f * (b2 - b3);
            float hp = r - b2 - b3 * 0.5f;

            /* 高频细音：2.6kHz + 快速颤音（像金属啸叫）*/
            float vib = sinf(6.2831f * 38.0f * t) * 90.0f;
            float squeak = sinf(6.2831f * (2600.0f + vib) * t) * 0.16f;

            /* 颗粒：随机的微小刮擦爆发 */
            if (--nextGrit <= 0) {
                grit = 14 + (int)(fabsf(r) * 26.0f);
                gritAmp = 0.5f + 0.5f * fabsf(r);
                nextGrit = 40 + (int)(fabsf(r) * 120.0f);
            }
            float g = 0.0f;
            if (grit > 0) { g = hp * gritAmp * 3.0f; grit--; }

            /* 摩擦主体 */
            float v = hp * 1.35f;

            /* 极快包络：0.002 秒起，之后指数衰减 */
            float env;
            if (t < 0.002f) env = t / 0.002f;
            else env = expf(-t * 34.0f);

            v = (v + squeak + g) * env * 0.62f;
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            g_sndScavStep[i] = (short)(v * 32000.0f);
        }
    }
}

void InitAudio(void) {
    GenSounds();
    InitializeCriticalSection(&g_audioLock);
    g_audioEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_audioEvent) return;

    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = AUDIO_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = AUDIO_RATE * 4;

    if (waveOutOpen(&g_wave, WAVE_MAPPER, &fmt, (DWORD_PTR)AudioCallback, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        g_wave = NULL;
        return;
    }
    for (int i = 0; i < AUDIO_BUFS; i++) {
        memset(&g_hdr[i], 0, sizeof(WAVEHDR));
        g_hdr[i].lpData = (LPSTR)g_pcm[i];
        g_hdr[i].dwBufferLength = AUDIO_SAMPLES * 2 * sizeof(short);
        waveOutPrepareHeader(g_wave, &g_hdr[i], sizeof(WAVEHDR));
        MixInto(g_pcm[i]);
        waveOutWrite(g_wave, &g_hdr[i], sizeof(WAVEHDR));
    }
    g_audioThread = CreateThread(NULL, 0, AudioThreadMain, NULL, 0, NULL);
    g_audioOn = 1;
}

void ShutdownAudio(void) {
    if (!g_audioOn && !g_wave) return;
    g_audioOn = 0;
    InterlockedExchange(&g_audioQuit, 1);
    if (g_audioEvent) SetEvent(g_audioEvent);
    /* v2.7：等不到线程退出就别销毁锁/事件 —— 它可能还卡在
       EnterCriticalSection / WaitForSingleObject 上，
       DeleteCriticalSection / CloseHandle 都是 UB。进程马上退出，这点泄漏无所谓。 */
    int threadExited = 1;
    if (g_audioThread) {
        threadExited = (WaitForSingleObject(g_audioThread, 500) == WAIT_OBJECT_0);
        CloseHandle(g_audioThread);
        g_audioThread = NULL;
    }
    if (g_wave) {
        waveOutReset(g_wave);
        for (int i = 0; i < AUDIO_BUFS; i++) waveOutUnprepareHeader(g_wave, &g_hdr[i], sizeof(WAVEHDR));
        waveOutClose(g_wave);
        g_wave = NULL;
    }
    if (threadExited) {
        if (g_audioEvent) { CloseHandle(g_audioEvent); g_audioEvent = NULL; }
        DeleteCriticalSection(&g_audioLock);
    }
}

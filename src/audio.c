/*
 * Canity — 音频层实现：程序化合成 + 软件混音
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "audio.h"
#include "game.h"


/* ------------------------------------------------------------------ */
/* 音效：程序化合成 + 软件混音（不依赖任何素材文件）                    */
/* ------------------------------------------------------------------ */

#define AUDIO_RATE 22050
#define AUDIO_BUFS 3
#define AUDIO_SAMPLES 1024
#define MAX_VOICES 8

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
static short g_sndTick[1323];      /* 0.06s 锁盘：拨对了（清脆咔哒） */
static short g_sndDull[2647];      /* 0.12s 锁盘：拨错了（沉闷一声） */

typedef struct { const short* data; int len, pos; float vol; int active; } Voice;

static Voice g_voices[MAX_VOICES];
static HWAVEOUT g_wave = NULL;
static WAVEHDR g_hdr[AUDIO_BUFS];
static short g_pcm[AUDIO_BUFS][AUDIO_SAMPLES];
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
    int i;
    for (i = 0; i < AUDIO_SAMPLES; i++) out[i] = 0;

    EnterCriticalSection(&g_audioLock);
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &g_voices[v];
        if (!vo->active) continue;
        for (i = 0; i < AUDIO_SAMPLES; i++) {
            if (vo->pos >= vo->len) { vo->active = 0; break; }
            int sample = (int)(vo->data[vo->pos++] * vo->vol);
            int acc = out[i] + sample;
            if (acc > 32767) acc = 32767;
            else if (acc < -32768) acc = -32768;
            out[i] = (short)acc;
        }
    }
    LeaveCriticalSection(&g_audioLock);
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
        case SFX_TICK:   data = g_sndTick;  len = (int)(sizeof(g_sndTick) / sizeof(short)); break;
        case SFX_DULL:   data = g_sndDull;  len = (int)(sizeof(g_sndDull) / sizeof(short)); break;
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

    /* 锁盘拨对：清脆的金属咔哒（高频 + 噪声齿音，极快衰减） */
    {
        int tl = (int)(sizeof(g_sndTick) / sizeof(short));
        for (int i = 0; i < tl; i++) {
            float t = (float)i / AUDIO_RATE;
            seed = seed * 1664525u + 1013904223u;
            float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
            float v = sinf(6.2831f * 2350.0f * t) * 0.55f + n * 0.30f;
            v *= expf(-t * 150.0f);
            /* 一点更亮的回弹，让“咔哒”听起来是金属的 */
            float d2 = t - 0.022f;
            if (d2 > 0.0f) v += sinf(6.2831f * 3120.0f * d2) * expf(-d2 * 240.0f) * 0.30f;
            g_sndTick[i] = (short)(v * 0.75f * 32000.0f);
        }
    }

    /* 锁盘拨错：沉闷的一下（强低通噪声 + 低频正弦，慢衰减） */
    {
        int dl = (int)(sizeof(g_sndDull) / sizeof(short));
        float lp3 = 0.0f;
        for (int i = 0; i < dl; i++) {
            float t = (float)i / AUDIO_RATE;
            seed = seed * 1664525u + 1013904223u;
            float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
            lp3 = lp3 * 0.88f + n * 0.12f;                 /* 强低通：闷 */
            float f = 118.0f - 38.0f * (t / 0.12f);        /* 低频一点点下滑 */
            float v = sinf(6.2831f * f * t) * 0.45f + lp3 * 0.85f;
            v *= expf(-t * 24.0f);
            g_sndDull[i] = (short)(v * 0.55f * 32000.0f);
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
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = AUDIO_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = AUDIO_RATE * 2;

    if (waveOutOpen(&g_wave, WAVE_MAPPER, &fmt, (DWORD_PTR)AudioCallback, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        g_wave = NULL;
        return;
    }
    for (int i = 0; i < AUDIO_BUFS; i++) {
        memset(&g_hdr[i], 0, sizeof(WAVEHDR));
        g_hdr[i].lpData = (LPSTR)g_pcm[i];
        g_hdr[i].dwBufferLength = AUDIO_SAMPLES * sizeof(short);
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
    if (g_audioThread) { WaitForSingleObject(g_audioThread, 500); CloseHandle(g_audioThread); g_audioThread = NULL; }
    if (g_wave) {
        waveOutReset(g_wave);
        for (int i = 0; i < AUDIO_BUFS; i++) waveOutUnprepareHeader(g_wave, &g_hdr[i], sizeof(WAVEHDR));
        waveOutClose(g_wave);
        g_wave = NULL;
    }
    if (g_audioEvent) { CloseHandle(g_audioEvent); g_audioEvent = NULL; }
    DeleteCriticalSection(&g_audioLock);
}

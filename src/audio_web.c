/*
 * Canity — 音频分区的网页后端（WebAudio 实时合成）
 * ---------------------------------------------------------------------
 * 2026-11-06：第一版按 id % 8 分类，用户实测「音频差距很大」。
 * 现在按 **audio.h 里真实的 SFX 枚举**逐个配音，每种音效有自己的
 * 波形/频率/包络，尽量贴近桌面版的语义（脚步闷、心跳低频、
 * 拾取清脆、守门人极低频、清道夫刺耳…）。
 *
 * 仍然遵循本作约束：**零素材**，全部现场合成。
 * 与桌面波形不逐样本一致，但音色语义对齐。
 *
 * 分区规则：audio 的符号只住在 audio 分区（partition_check 规则 S3）。
 */
#include "common.h"
#include "audio.h"

/* =====================================================================
 * JS 侧合成器
 * =================================================================== */
EM_JS(void, js_audio_init, (void), {
    if (Module._actx) return;
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return;
    Module._actx = new AC();

    /* 主链：source -> master(音量) -> comp(限幅) -> destination
       限幅器很关键：并发的噪声爆发不加限幅会刺耳、破音，
       这正是"和桌面差距很大"的一部分。 */
    Module._master = Module._actx.createGain();
    Module._master.gain.value = 0.95;
    var comp = Module._actx.createDynamicsCompressor();
    comp.threshold.value = -18;
    comp.knee.value = 24;
    comp.ratio.value = 8;
    comp.attack.value = 0.004;
    comp.release.value = 0.18;
    Module._master.connect(comp);
    comp.connect(Module._actx.destination);

    // 一个共享低通，给"闷"的音色用，避免高频毛刺
    Module._noiseBuf = null;

    function unlock() {
        if (Module._actx && Module._actx.state === 'suspended') Module._actx.resume();
    }
    window.addEventListener('pointerdown', unlock, { passive: true });
    window.addEventListener('keydown', unlock, { passive: true });
});

EM_JS(void, js_play, (int id, float vol, float pan), {
    if (!Module._actx) return;
    var ctx = Module._actx;
    if (ctx.state === 'suspended') ctx.resume();

    var t0 = ctx.currentTime + 0.001;
    var out = ctx.createGain();

    /* ==================================================================
     * 2026-11-07（v2.4.2 / B4）：**真正的左右声像**
     * ------------------------------------------------------------------
     * 原来 AudioPlayWorld 把 l/r 取最大值合成一个标量音量，
     * 只保留了距离衰减，没有方向 —— 对"听声辨位"的恐怖游戏来说等于
     * 少了一条信息渠道。
     *
     * 现在把这个标量换成 StereoPannerNode 的 pan（-1 左 .. +1 右），
     * 音量仍由距离负责。不支持 StereoPanner 的浏览器
     * （老 Safari）退回单声道，功能不缺失、只是没有方向。
     * ================================================================== */
    var sink = Module._master;
    var clamped = (pan < -1) ? -1 : (pan > 1 ? 1 : pan);
    /* 2026-11-07（B6）：把实际下发的 pan 记下来，给运行期探针用。
       没有它，"真立体声"就只是读代码得出的结论，没法验证。 */
    Module._lastPan = clamped;
    Module._lastVol = vol;
    if (ctx.createStereoPanner) {
        var p = ctx.createStereoPanner();
        p.pan.value = clamped;
        p.connect(Module._master);
        sink = p;
        Module._pannerSupported = true;
    } else {
        Module._pannerSupported = false;
    }
    out.connect(sink);

    /* 2026-11-06（用户实测：**走路/怪物/低频全部听不见**）
       ------------------------------------------------------------------
       两个真原因：
         ① 游戏传进来的音量本来就小（走路 0.28，冲刺 0.50）
         ② 我用的基频太低（脚步 95Hz、心跳 62Hz、守门人 44Hz）——
            笔记本/手机喇叭根本发不出 100Hz 以下，等于没声音
       修法：增益做一次补偿；低频音色**补上 2/3 次谐波**，
       让它在小喇叭上也有"能听见的音高"。 */
    var peak = Math.max(0.02, Math.min(1.0, vol)) * 1.8;
    if (peak > 1.0) peak = 1.0;

    function osc(type, f0, f1, dur, gain, detune) {
        var o = ctx.createOscillator();
        o.type = type;
        if (detune) o.detune.value = detune;
        o.frequency.setValueAtTime(f0, t0);
        if (f1 && f1 !== f0) o.frequency.exponentialRampToValueAtTime(Math.max(20, f1), t0 + dur);
        var g = ctx.createGain();
        g.gain.setValueAtTime(0.0001, t0);
        g.gain.exponentialRampToValueAtTime(Math.max(0.001, gain * peak), t0 + Math.min(0.012, dur * 0.3));
        g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
        o.connect(g); g.connect(out);
        o.start(t0); o.stop(t0 + dur + 0.02);
        return o;
    }

    function noiseBurst(dur, lo, hi, gain, delay) {
        if (!Module._noiseBuf) {
            var n = Math.floor(ctx.sampleRate * 1.0);
            var b = ctx.createBuffer(1, n, ctx.sampleRate);
            var d = b.getChannelData(0);
            for (var i = 0; i < n; i++) d[i] = Math.random() * 2 - 1;
            Module._noiseBuf = b;
        }
        var t = t0 + (delay || 0);
        var s = ctx.createBufferSource();
        s.buffer = Module._noiseBuf;
        s.loop = true;
        var f = ctx.createBiquadFilter();
        f.type = 'bandpass';
        f.frequency.setValueAtTime(lo, t);
        if (hi && hi !== lo) f.frequency.exponentialRampToValueAtTime(Math.max(60, hi), t + dur);
        f.Q.value = 1.1;
        var g = ctx.createGain();
        g.gain.setValueAtTime(0.0001, t);
        g.gain.exponentialRampToValueAtTime(Math.max(0.001, gain * peak), t + Math.min(0.008, dur * 0.3));
        g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
        s.connect(f); f.connect(g); g.connect(out);
        s.start(t); s.stop(t + dur + 0.02);
        return s;
    }

    switch (id) {
    // ---- 0 SFX_STEP 脚步：鞋底摩擦（中频）+ 低频身音，两层都要 ----
    case 0:
        noiseBurst(0.070, 1500, 700, 0.42);   /* 能听见的那一下"嗒" */
        osc('triangle', 190, 120, 0.07, 0.34); /* 身音提到 190Hz */
        osc('sine', 380, 240, 0.05, 0.16);     /* 二次谐波：小喇叭靠它 */
        break;

    // ---- 1 SFX_HEART 心跳：低频 + 谐波堆叠 ----
    case 1:
        osc('sine', 78, 55, 0.20, 0.55);
        osc('sine', 156, 110, 0.18, 0.26);    /* 2× */
        osc('sine', 234, 165, 0.14, 0.12);    /* 3× */
        noiseBurst(0.10, 140, 90, 0.16);
        break;

    // ---- 2 SFX_PICKUP 拾取：上行两声方波 ----
    case 2:
        osc('square', 780, 780, 0.055, 0.28);
        setTimeout(function () {}, 0);
        osc('square', 1180, 1180, 0.075, 0.26);
        break;

    // ---- 3 SFX_POWER 通电：低锯齿爬升 + 电流噪声 ----
    case 3:
        osc('sawtooth', 70, 150, 0.28, 0.30);
        noiseBurst(0.20, 2400, 1200, 0.10, 0.02);
        break;

    // ---- 4 SFX_GATE 闸门：金属闷响 ----
    case 4:
        noiseBurst(0.22, 320, 160, 0.34);
        osc('sine', 72, 44, 0.26, 0.40);
        osc('triangle', 145, 120, 0.16, 0.12);
        break;

    // ---- 5 SFX_CRANK 摇柄：三下棘轮 ----
    case 5:
        noiseBurst(0.035, 1600, 1600, 0.22, 0.00);
        noiseBurst(0.035, 1500, 1500, 0.20, 0.09);
        noiseBurst(0.045, 1400, 1400, 0.18, 0.18);
        break;

    // ---- 6 SFX_SCREAM 尖叫：下滑锯齿 + 破音噪声 ----
    case 6:
        osc('sawtooth', 520, 130, 0.34, 0.42, 12);
        osc('sawtooth', 545, 140, 0.34, 0.22, -14);
        noiseBurst(0.26, 1800, 700, 0.14, 0.01);
        break;

    // ---- 7 SFX_GLASS 玻璃：高频短爆 ----
    case 7:
        noiseBurst(0.13, 4200, 2600, 0.30);
        osc('triangle', 2400, 1800, 0.06, 0.10);
        break;

    // ---- 8 SFX_SEARCH 翻找：布料摩擦 ----
    case 8:
        noiseBurst(0.11, 1300, 900, 0.16);
        noiseBurst(0.08, 1900, 1500, 0.10, 0.07);
        break;

    // ---- 9 SFX_THROW 投掷：挥空的风声 ----
    case 9:
        noiseBurst(0.16, 3000, 700, 0.20);
        break;

    // ---- 10 SFX_GLUE 胶水：黏稠下滑 ----
    case 10:
        osc('sine', 200, 70, 0.20, 0.35);
        noiseBurst(0.12, 700, 350, 0.16);
        break;

    // ---- 11 SFX_ECHO 回声：清脆叮 + 尾音 ----
    case 11:
        osc('sine', 1500, 1500, 0.10, 0.30);
        osc('sine', 2250, 2250, 0.14, 0.10);
        break;

    // ---- 12 SFX_DECOY 诱饵：木头吱呀 ----
    case 12:
        osc('sawtooth', 210, 150, 0.22, 0.18, 6);
        noiseBurst(0.10, 900, 600, 0.10, 0.05);
        break;

    // ---- 13 SFX_WARDEN 守门人：低频压迫 + 可听频段的不谐和音 ----
    case 13:
        osc('sine', 48, 36, 0.60, 0.55);
        osc('sine', 96, 72, 0.55, 0.30);      /* 2× 谐波 */
        osc('sine', 150, 108, 0.45, 0.16);    /* 3× 谐波 */
        noiseBurst(0.45, 260, 110, 0.22);     /* 低频隆隆，落在能听见的边 */
        break;

    // ---- 14 SFX_SCAVENGER 清道夫：刺耳刮擦 ----
    case 14:
        noiseBurst(0.20, 2800, 2200, 0.26);
        osc('square', 900, 700, 0.12, 0.10);
        break;

    // ---- 15 SFX_SCAV_STEP 清道夫脚步：高速摩擦 ----
    case 15:
        noiseBurst(0.06, 2200, 1800, 0.16);
        break;

    default:
        osc('triangle', 420, 380, 0.09, 0.16);
        break;
    }
});

EM_JS(void, js_set_volume, (float v), {
    if (Module._master) Module._master.gain.value = Math.max(0, Math.min(1, v)) * 0.75;
});

/* =====================================================================
 * 公开接口
 * =================================================================== */
void InitAudio(void) { js_audio_init(); }
void ShutdownAudio(void) { }

void PlaySfx(int id, float vol) { js_play(id, vol, 0.0f); }

/* 世界音效：先按听者朝向算声像与距离衰减，再交给合成器。
   这样"背后的脚步"和"正前方的脚步"听起来不一样。 */
void AudioPlayWorld(int id, float x, float y, float vol, float maxDist, int priority) {
    (void)priority;
    extern void AudioComputeGains(float, float, float, float,
                                  float, float, float, float*, float*);

    float l = 0.0f, r = 0.0f;
    float gain, pan;
    AudioComputeGains(posX, posY, dirX, dirY, x, y, maxDist, &l, &r);

    /* 常量功率声像：l 与 r 是两个声道的能量增益，l+r 就是总响度。
       原来取 max*2 会把"偏左"当成"很响"，也不携带方向信息。 */
    if (l < 0.0f) l = 0.0f;
    if (r < 0.0f) r = 0.0f;
    {
        float sum = l + r;
        if (sum <= 0.0005f) return;
        /* 距离衰减：开平方根，避免远处几乎没声（浏览器里对比太强）。 */
        gain = sqrtf(sum);
        if (gain > 1.0f) gain = 1.0f;
        /* 方向：完全偏一侧时 ±1，正中时 0。 */
        pan = (r - l) / sum;
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;
    }
    js_play(id, vol * gain, pan);
}

void AudioUpdateListener(float px, float py, float dx, float dy) {
    (void)px; (void)py; (void)dx; (void)dy;
}

/* 声像/距离衰减是**纯数学**，不是平台能力 —— 网页端也给出正确结果。 */
void AudioComputeGains(float lisX, float lisY, float lisDirX, float lisDirY,
                       float srcX, float srcY, float maxDist,
                       float* outL, float* outR) {
    float dx = srcX - lisX, dy = srcY - lisY;
    float dist = sqrtf(dx * dx + dy * dy);
    float att = 1.0f;
    if (maxDist > 0.01f) {
        att = 1.0f - dist / maxDist;
        if (att < 0.0f) att = 0.0f;
        if (att > 1.0f) att = 1.0f;
    }
    float pan = 0.0f;
    if (dist > 0.01f) {
        float nx = dx / dist, ny = dy / dist;
        pan = nx * (-lisDirY) + ny * lisDirX;
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;
    }
    if (outL) *outL = att * (0.5f - 0.5f * pan);
    if (outR) *outR = att * (0.5f + 0.5f * pan);
}

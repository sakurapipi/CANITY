/*
 * Canity — 音频分区的网页后端（WebAudio）
 * ---------------------------------------------------------------------
 * 2026-11-06（用户实测：**音频没接入**）。
 *
 * 桌面端 audio.c 是 waveOut + 自研合成 + 混音线程，整块搬到浏览器不现实。
 * 这里走另一条同样"零素材"的路：**用 WebAudio 现场合成**。
 * 每个音效按类别映射到一组振荡器/噪声参数，播放时现算包络。
 *
 * 与桌面版不保证逐波形一致 —— 但**有反馈**比"完全没声音"重要得多，
 * 而且这套合成同样是程序化的，符合本作"零外部素材"的约束。
 *
 * 分区规则：audio 的符号只住在 audio 分区（见 partition_check 规则 S3）。
 */
#include "common.h"
#include "audio.h"

/* ---------------------------------------------------------------------
 * JS 侧：一个极简的合成器
 * ------------------------------------------------------------------- */
EM_JS(void, js_audio_init, (void), {
    if (Module._actx) return;
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return;
    Module._actx = new AC();
    Module._master = Module._actx.createGain();
    Module._master.gain.value = 0.85;
    Module._master.connect(Module._actx.destination);
    Module._noiseBuf = null;

    // 浏览器自动播放策略：首次用户手势后才允许出声
    function unlock() {
        if (Module._actx && Module._actx.state === 'suspended') Module._actx.resume();
    }
    window.addEventListener('pointerdown', unlock, { passive: true });
    window.addEventListener('keydown', unlock, { passive: true });
});

EM_JS(void, js_play, (int id, float vol), {
    if (!Module._actx) return;
    var ctx = Module._actx;
    if (ctx.state === 'suspended') ctx.resume();

    // ---- 按音效 id 归类成几种音色 ----
    // 桌面版的 SFX 枚举是连续的；这里只按区间粗分，
    // 保证"不同类型的动作听起来不一样"。
    var cat = id % 8;
    var t0 = ctx.currentTime;
    var g = ctx.createGain();
    g.connect(Module._master);
    var peak = Math.max(0.02, Math.min(1.0, vol)) * 0.5;

    function tone(freq, dur, type, slideTo) {
        var o = ctx.createOscillator();
        o.type = type || 'square';
        o.frequency.setValueAtTime(freq, t0);
        if (slideTo) o.frequency.exponentialRampToValueAtTime(Math.max(30, slideTo), t0 + dur);
        o.connect(g);
        o.start(t0);
        o.stop(t0 + dur + 0.02);
        return o;
    }

    function noise(dur, bp) {
        if (!Module._noiseBuf) {
            var n = ctx.sampleRate * 0.5;
            var buf = ctx.createBuffer(1, n, ctx.sampleRate);
            var d = buf.getChannelData(0);
            for (var i = 0; i < n; i++) d[i] = Math.random() * 2 - 1;
            Module._noiseBuf = buf;
        }
        var s = ctx.createBufferSource();
        s.buffer = Module._noiseBuf;
        s.loop = true;
        var f = ctx.createBiquadFilter();
        f.type = 'bandpass';
        f.frequency.value = bp || 1200;
        f.Q.value = 0.8;
        s.connect(f); f.connect(g);
        s.start(t0); s.stop(t0 + dur + 0.02);
        return s;
    }

    var dur = 0.12;
    switch (cat) {
    case 0:  // 挥击 / 打击：短促噪声 + 低频碰撞
        dur = 0.14; noise(0.10, 1800); tone(180, 0.12, 'square', 90); break;
    case 1:  // 拾取 / 金币：上行两声
        dur = 0.16; tone(660, 0.07, 'square'); tone(990, 0.09, 'square'); break;
    case 2:  // 受伤 / 尖叫：下滑锯齿
        dur = 0.32; tone(420, 0.30, 'sawtooth', 120); noise(0.14, 900); break;
    case 3:  // 通电 / 机械：低方波
        dur = 0.26; tone(110, 0.24, 'square', 160); break;
    case 4:  // 玻璃 / 碎裂：高频噪声
        dur = 0.18; noise(0.16, 3200); break;
    case 5:  // 脚步 / 闷响：极短低频
        dur = 0.09; noise(0.07, 320); tone(90, 0.08, 'sine', 60); break;
    case 6:  // 提示 / 门：中频三角
        dur = 0.20; tone(520, 0.18, 'triangle', 380); break;
    default: // 其它：中性短音
        dur = 0.10; tone(300, 0.09, 'sine'); break;
    }

    // 包络：快起慢落，避免爆音
    g.gain.setValueAtTime(0.0001, t0);
    g.gain.exponentialRampToValueAtTime(peak, t0 + 0.012);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur + 0.05);
});

EM_JS(void, js_set_volume, (float v), {
    if (Module._master) Module._master.gain.value = Math.max(0, Math.min(1, v)) * 0.85;
});

/* ---------------------------------------------------------------------
 * 公开接口
 * ------------------------------------------------------------------- */
void InitAudio(void) { js_audio_init(); }
void ShutdownAudio(void) { }

void PlaySfx(int id, float vol) { js_play(id, vol); }

/* 世界音效：按听者朝向算声像与距离衰减，再交给合成器。
   这样"背后的脚步"和"正前方的脚步"听起来不一样。 */
void AudioPlayWorld(int id, float x, float y, float vol, float maxDist, int priority) {
    (void)priority;
    extern void AudioComputeGains(float, float, float, float,
                                  float, float, float, float*, float*);
    extern float posX, posY, dirX, dirY;
    float l = 0.0f, r = 0.0f;
    AudioComputeGains(posX, posY, dirX, dirY, x, y, maxDist, &l, &r);
    float gain = (l > r ? l : r) * 2.0f;      /* 等功率左右合成成单声道 */
    if (gain <= 0.001f) return;
    PlaySfx(id, vol * gain);
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
        pan = nx * (-lisDirY) + ny * lisDirX;   /* 右向量 = (-dirY, dirX) */
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;
    }
    if (outL) *outL = att * (0.5f - 0.5f * pan);
    if (outR) *outR = att * (0.5f + 0.5f * pan);
}

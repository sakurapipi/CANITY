/*
 * Canity — 音频分区的网页后端（静音）
 * ---------------------------------------------------------------------
 * 2026-11-06：网页构建排除 audio.c（waveOut + 线程），这个文件补上它的
 * 全部公开接口。**空实现不是偷懒**：浏览器自动播放策略要求首次用户手势
 * 之后才能 resume AudioContext，一期先把链路跑通，声音在 R7 补。
 *
 * 分区规则：这个文件只属于 audio.h 这个分区，
 * 平台层（src/platform/）不许定义任何 audio 符号。
 */
#include "common.h"
#include "audio.h"

void InitAudio(void) { }
void ShutdownAudio(void) { }
void PlaySfx(int id, float vol) { (void)id; (void)vol; }
void AudioPlayWorld(int id, float x, float y, float vol, float maxDist, int priority) {
    (void)id; (void)x; (void)y; (void)vol; (void)maxDist; (void)priority;
}
void AudioUpdateListener(float px, float py, float dx, float dy) {
    (void)px; (void)py; (void)dx; (void)dy;
}
/* 声像/距离衰减是**纯数学**，不是平台能力 ——
   所以网页端也要给出正确的计算结果，而不是一律 0。
   与 audio.c 的实现保持一致（双耳等功率 + 距离线性衰减）。 */
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
    /* 声像：源相对听者朝向的左右分量 */
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

/* src/domain/difficulty.c —— 难度倍数（纯函数，零依赖）
 *
 * 第 2 轮（2026-11-08）。所有数字集中在这里，改平衡只看这一个文件。
 */
#include "common.h"
#include "difficulty.h"

int difficulty = DIFF_NORMAL;

/* 每档的六个倍数，按 [EASY, NORMAL, HARD] 排列。
   标准档全部是恒等值（1.0 / 0 / 1.0），所以"默认手感"= 改动前。 */
static const float kThreatInterval[DIFF_COUNT] = { 1.55f, 1.00f, 0.72f };
static const float kSanityDrain[DIFF_COUNT]    = { 0.65f, 1.00f, 1.35f };
static const float kWardenBlockAdd[DIFF_COUNT] = { -6.0f, 0.0f, 6.0f };
static const float kQteWindow[DIFF_COUNT]      = { 1.60f, 1.00f, 0.75f };
static const float kWrenchWindow[DIFF_COUNT]   = { 1.45f, 1.00f, 0.85f };
static const float kBreathWindow[DIFF_COUNT]   = { 30.0f, 18.0f, 8.0f };

static int idx(void) {
    if (difficulty < 0) return 0;
    if (difficulty >= DIFF_COUNT) return DIFF_NORMAL;
    return difficulty;
}

float DiffThreatIntervalMul(void) { return kThreatInterval[idx()]; }
float DiffSanityDrainMul(void)    { return kSanityDrain[idx()]; }
float DiffWardenBlockAdd(void)    { return kWardenBlockAdd[idx()]; }
float DiffQteWindowMul(void)      { return kQteWindow[idx()]; }
float DiffWrenchWindowMul(void)   { return kWrenchWindow[idx()]; }
float DiffBreathWindow(void)      { return kBreathWindow[idx()]; }

const char* DiffNameEn(int d) {
    if (d == DIFF_EASY) return "EASY";
    if (d == DIFF_HARD) return "HARDCORE";
    return "NORMAL";
}

const char* DiffNameZh(int d) {
    if (d == DIFF_EASY) return "\xe8\xbd\xbb\xe6\x9d\xbe";       /* 轻松 */
    if (d == DIFF_HARD) return "\xe7\xa1\xac\xe6\xa0\xb8";       /* 硬核 */
    return "\xe6\xa0\x87\xe5\x87\x86";                          /* 标准 */
}

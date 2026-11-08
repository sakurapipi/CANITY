/* src/core/rng.c —— 可复现随机数（xorshift32）
 *
 * 为什么自己写而不包一层 srand/rand：
 *   C 标准库的 rand() 实现**依赖平台与 CRT**（MSVC 和 glibc 的序列不同），
 *   而本工程同时要出 Windows 原生和 Emscripten 网页两版。
 *   用固定算法的 xorshift32，两个平台跑出来的序列逐位相同 ——
 *   这样"同一份存档在网页和 PC 上重放出一模一样的房间"才成立。
 */
#include "common.h"      /* 系统头只允许出现在 common.h（分区契约 S1） */
#include "core_rng.h"
/* 不显式 include <stdlib.h>：rand() 由 common.h 带进来。 */

static unsigned int s_state = 0x9E3779B9u;

void rng_seed(unsigned int seed) {
    /* 0 会让 xorshift 永远停在 0，换成一个非零常量 */
    s_state = seed ? seed : 0x9E3779B9u;
}

unsigned int rng_next(void) {
    unsigned int x = s_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_state = x;
    return x;
}

int rng_range(int lo, int hi) {
    int span;
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    span = hi - lo + 1;
    if (span <= 0) return lo;
    return lo + (int)(rng_next() % (unsigned int)span);
}

float rng_float(void) {
    /* 取高 24 位，除以 2^24 —— 保证落在 [0,1) 且精度可控 */
    return (float)((rng_next() >> 8) & 0xFFFFFFu) / 16777216.0f;
}

int rng_legacy_next(void) {
    return rand();
}

/* src/loop_rng.c —— 局内随机数（2026-11-09，从 game.c 整簇搬出）
 *
 * ## 为什么是"整簇"
 *
 * 之前试着单独搬函数，全部失败 —— 因为它们依赖 game.c 的 `static`
 * 变量，而 static 跨文件用不了。
 *
 * 所以正确的切分单位是「状态 + 操作它的函数」，不是「单个函数」。
 */
#include "common.h"
#include "loop_api.h"

static unsigned int s_runRng = 0;

unsigned int RunRngNext(void) {
    if (s_runRng == 0) s_runRng = 0x9E3779B9u;   /* 兜底，绝不让它恒为 0 */
    s_runRng ^= s_runRng << 13;
    s_runRng ^= s_runRng >> 17;
    s_runRng ^= s_runRng << 5;
    return s_runRng;
}

void RunRngSeed(unsigned int seed) {
    s_runRng = seed ? seed : 0x9E3779B9u;
}

unsigned int RunRngState(void) { return s_runRng; }
void RunRngSetState(unsigned int st) { s_runRng = st; }

/* src/loop_world.c —— P2 世界（2026-11-09，C 段第 1 块）
 *
 * 从 `game.c` 搬出来的**世界查询**函数。这是拆 game.c 的第一块 ——
 * 先搬"零依赖、纯查询"的，证明机制可行，再搬有状态的。
 *
 * 目前：金币匣位置 / 出口位置。
 * 后续：GenerateLevel 一系的函数会陆续搬进来。
 */
#include "common.h"
#include "entity.h"
#include "state.h"
#include "world_api.h"

float GoldBreakerAt(int level) { return (25.0f + 3.0f * (float)level) * EndlessDepthBonus(level); }
float GoldExitAt(int level)    { return (30.0f + 6.0f * (float)level) * EndlessDepthBonus(level); }

int PlayerAtExit(void) {
    return (breakerOpen && Dist2(posX, posY, exitX, exitY) < EXIT_ENTER_R2 * EXIT_ENTER_R2);
}

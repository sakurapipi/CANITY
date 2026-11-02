/*
 * Canity — AI 共用工具（v8 重构第三刀）
 *
 * 为什么要有这个文件
 * ──────────────────
 * 加拾荒者 / 清道夫的时候，我各写了一份「找一个空地点」的函数：
 *     extras.c   findSpot(minD, maxD)
 *     collector.c findSpotFar()
 *     scavenger.c findStart()
 * 三份逻辑几乎一样，只差距离参数 —— 这就是要收拾的东西。
 *
 * 现在统一成 ai_common.c 里的两个函数，新加的怪一律调用它们。
 * 这也是「AI 区块」的第一块砖：AI 的公共能力集中在一处。
 */
#include "common.h"
#include "game.h"
#include "state.h"
#include "ai_common.h"

/*
 * 在玩家周围 [minD, maxD] 格找一个可站立的空地。
 * 返回 1 成功，0 失败（100 次随机都撞墙）。
 */
int AISpawnPointAround(float* ox, float* oy, float minD, float maxD) {
    if (maxD < minD) { float t = minD; minD = maxD; maxD = t; }
    for (int t = 0; t < 100; t++) {
        float ang = (float)(rand() % 6283) / 1000.0f;
        float d = minD + (float)(rand() % 1000) / 1000.0f * (maxD - minD);
        float x = posX + cosf(ang) * d;
        float y = posY + sinf(ang) * d;
        if (x < 1.5f || y < 1.5f || x > MAP_WIDTH - 1.5f || y > MAP_HEIGHT - 1.5f) continue;
        if (Blocked(x, y)) continue;
        *ox = x;
        *oy = y;
        return 1;
    }
    return 0;
}

/*
 * 从 from 点朝 to 点走一步（速度 sp，帧长 dt）。
 * 撞墙时依次尝试：只走 X → 只走 Y → 沿墙侧向绕。
 * 这样即使没有寻路，怪也不会原地卡死。
 */
void AIMoveToward(float* x, float* y, float tx, float ty, float sp, float dt) {
    float dx = tx - *x, dy = ty - *y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return;

    float nx = *x + (dx / d) * sp * dt;
    float ny = *y + (dy / d) * sp * dt;
    if (!Blocked(nx, ny)) { *x = nx; *y = ny; return; }

    if (!Blocked(nx, *y)) { *x = nx; return; }      /* 只走 X */
    if (!Blocked(*x, ny)) { *y = ny; return; }      /* 只走 Y */

    /* 两侧都堵 —— 沿墙侧向滑一小步 */
    float sx = *x + (dy / d) * sp * dt;
    float sy = *y - (dx / d) * sp * dt;
    if (!Blocked(sx, sy)) { *x = sx; *y = sy; }
}

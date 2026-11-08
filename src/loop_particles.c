/* src/loop_particles.c —— 粒子/碎片（2026-11-09，从 game.c 整簇搬出）
 *
 * 状态 g_shardTint 和操作它的两个函数一起搬 —— 这是唯一可行的切分方式
 * （函数单独搬会撞 static，第 11 轮验证过）。
 */
#include "common.h"
#include "present_api.h"

int g_shardTint = 0;   /* v2.1：下一批金币的颜色类型 */

void SpawnParticles(float x, float y, int tint) {
    g_shardTint = tint;
    SpawnShards(x, y, 0.0f, 0.0f, 0.45f);
    g_shardTint = 0;
}

void SpawnShards(float x, float y, float vx, float vy, float charge);
/* v3.3：粘胶区域（实现放在 entity.c） */

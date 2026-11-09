/* src/loop_particles.c —— 粒子/碎片（2026-11-09，从 game.c 整簇搬出）
 *
 * 状态 g_shardTint 和操作它的两个函数一起搬 —— 这是唯一可行的切分方式
 * （函数单独搬会撞 static，第 11 轮验证过）。
 */
#include "common.h"
#include "present_api.h"
#include "world_api.h"

Shard shards_debris[MAX_SHOTS * SHARD_COUNT];

static int g_shardTint = 0;   /* v2.1：下一批金币的颜色类型 */

void SpawnParticles(float x, float y, int tint) {
    g_shardTint = tint;
    SpawnShards(x, y, 0.0f, 0.0f, 0.45f);
    g_shardTint = 0;
}

void SpawnShards(float x, float y, float vx, float vy, float charge);
/* v3.3：粘胶区域（实现放在 entity.c） */


void SpawnShards(float x, float y, float vx, float vy, float charge) {
    int count = 6 + (int)(charge * 6);  /* 6~12 个金币 */
    for (int i = 0; i < MAX_SHOTS * SHARD_COUNT && count > 0; i++) {
        if (shards_debris[i].active) continue;
        shards_debris[i].x = x;
        shards_debris[i].y = y;
        shards_debris[i].z = 0.1f;
        /* 随机扩散速度 */
        float ang = (float)((double)rand()/((double)RAND_MAX+1.0))*6.283f;
        float spd = 2.0f + (float)((double)rand()/((double)RAND_MAX+1.0))*4.0f;
        shards_debris[i].vx = cosf(ang) * spd + vx * 0.1f;
        shards_debris[i].vy = sinf(ang) * spd + vy * 0.1f;
        shards_debris[i].vz = 3.0f + (float)((double)rand()/((double)RAND_MAX+1.0))*4.0f;  /* 向上弹 */
        shards_debris[i].life = MAX_SHARD_LIFE;
        shards_debris[i].age = 0.0f;
        shards_debris[i].active = 1;
        shards_debris[i].tint = (unsigned char)g_shardTint;
        count--;
    }
}

/*
 * Canity — 两只听声鬼的协同（v2.4.97 **简化版**）
 *
 * 用户决定：**不要分工了，两只都正常追击。**
 * 之前那套「封喉 / 包夹」在网格走廊里反复出问题
 * （卡在墙里、目标被 AI 覆盖、两只叠在一起），
 * 投入产出比太低，直接砍掉。
 *
 * 现在这个文件只做**两件小事**：
 *   ① 分离力 —— 两只靠太近时互相推开，不许重叠
 *   ② 被抓时重置两只的追逐计时 —— 让它们继续追，不会立刻放弃
 *
 * 移动、寻路、状态机全部交给 entity.c 原有的 UpdateMonster /
 * UpdateMonster2 —— 那套逻辑是稳定的，不再被这里覆盖。
 */
#include "state.h"
#include "common.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "entity.h"
#include "audio.h"

void FlankBuild(void) { }
void FlankReset(void) { }

/* ==================================================================
 * v2.4.99 **分离力已删除**
 * ------------------------------------------------------------------
 * 用户实测：
 *   「两只贴太近，一个不会动，一个卡墙里面。」
 *
 * 原因：分离力在**直接改 monster.x / monster.y**。
 *   ① 它和寻路移动每帧互相打架 → 一只被推回去又被寻路拉回来 → 不动
 *   ② 推送本身没有跟随格心，累积几十帧就能把一个怪挤到墙里
 *
 * 而这两只怪本来就没有"实体碰撞"的设定 —— 重叠在射线渲染里
 * 只是两张精灵叠在一起，远没有"卡死"难看。
 *
 * 结论：**不推了**。两只各自独立寻路，爱重叠就重叠。
 * ================================================================ */
void FlankUpdate(float dt) {
    (void)dt;
}

/* ==================================================================
 * 被抓：两只都不松懈
 * ================================================================== */
void FlankOnCaught(void) {
    
    if (monster.active) monster.huntTimer = 0.0f;
    if (monster2.active) {
        monster2.huntTimer = 0.0f;
        monster2.stun = 0.0f;
    }
}

/* ---- 下面这些接口保留但已空转（调用点不用改）---- */
int FlankChokeOnExitPath(float px, float py, float* gx, float* gy) {
    (void)px; (void)py; (void)gx; (void)gy;
    return 0;
}
int   FlankBlocksPlayer(float nx, float ny) { (void)nx; (void)ny; return 0; }
/* 2026-11-08（第 13 轮）：FlankIsBlocking 是常量桩（全工程零调用），已删 */
/* 2026-11-08（第 13 轮）：FlankGoalX 是常量桩（全工程零调用），已删 */
/* 2026-11-08（第 13 轮）：FlankGoalY 是常量桩（全工程零调用），已删 */
/* 2026-11-08（第 13 轮）：FlankStreak 是常量桩（全工程零调用），已删 */

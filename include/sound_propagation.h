/* ==================================================================
 * CANITY — 声学传播（v2.4-beta B03）
 * ------------------------------------------------------------------
 * 解决的问题
 *   现在"声音能传多远"只看**直线距离**：
 *       if (Dist2(x, y, m->x, m->y) > r * r) continue;
 *   于是隔着一堵墙的守门人，和一个同距离站在走廊里的守门人，
 *   听起来的可信度完全一样 —— 但玩家心里清楚：隔着墙应该更闷。
 *
 * 这个模块把「直线距离」换成「**沿可通行路径的步数**」：
 *   · 直线 3 格、中间一堵墙 → 实际要走 15 步 → 听起来很弱
 *   · 直线 3 格、走廊直通     → 3 步 → 听起来很清楚
 *
 * 为什么用 BFS 而不是 Dijkstra 加权
 *   24×24 只有 576 格，4 向 BFS 一次几微秒。
 *   材质加权（走廊/房间/积水）留到 B05 —— 那时再把 cost 换成带权。
 *   先把"隔墙"这一件最影响手感的事做对。
 *
 * 性能
 *   按事件缓存：同一个声源格只建一次场。
 *   8 只怪各读一次 = 8 次数组查询，不是 8 次 BFS。
 *   音频线程永远不调用这里 —— 只有游戏逻辑线程碰。
 * ================================================================ */
#ifndef CANITY_SOUND_PROPAGATION_H
#define CANITY_SOUND_PROPAGATION_H

#include "common.h"

#define PROP_CELLS (MAP_WIDTH * MAP_HEIGHT)
#define PROP_UNREACHABLE  (-1)

/* 以 (sx,sy) 为源建立传播距离场（单位 = 步数）。
   源格本身记为 0；墙体与不可达格记为 PROP_UNREACHABLE。
   重复用同一个源调用会命中缓存，不会重算。 */
void SoundPropBuild(float sx, float sy);

/* 某个格子的传播步数；PROP_UNREACHABLE = 走不到。
   没有先 Build 就返回 PROP_UNREACHABLE。 */
int SoundPropCostAt(float x, float y);

/* 上一次 Build 用的源格（测试与排障用） */
int SoundPropSourceCell(void);
int SoundPropBuildCount(void);   /* 真正执行过几次 BFS */

/* 统一查询：声源 (sx,sy) 发出、最大传播半径 maxRadius 的事件，
   在 (tx,ty) 还剩多大"可信度"（0..1，1 = 贴着声源）。
   内部会确保以 (sx,sy) 建场（不重复建）。
   **这是 B03 对外的唯一入口** —— 上层不需要知道 BFS 细节。 */
float SoundReach(float sx, float sy, float tx, float ty, float maxRadius);

/* 测试用：清掉缓存 */
void SoundPropReset(void);

#endif /* CANITY_SOUND_PROPAGATION_H */

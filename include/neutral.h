/*
 * Canity — 拾光虫（v2.4.1 第 11 轮）
 *
 * ==================================================================
 * 为什么先做这一只中立怪
 * ------------------------------------------------------------------
 * 玩家反馈要"中立怪物，让地图生态不再只有追杀玩家的敌人"。
 * 但中立不等于装饰：它应该影响玩家的**判断**，却不给保证的收益。
 *
 * 拾光虫平时在暗处缓慢游走，对玩家没有敌意：
 *   · 玩家靠近 → 受惊逃走（不是攻击）
 *   · 附近有噪音 → 也会受惊（可以当"环境预警"）
 *   · 靠近落地照明棒 → 会被光吸引，缓慢靠近
 *
 * 硬约束：
 *   · 不造成任何伤害
 *   · 不掉落金币/道具（否则会被刷）
 *   · 不参与物理阻挡（不进 Blocked）
 *   · 每层最多 1 只，不写入存档
 * ==================================================================
 */
#ifndef CANITY_NEUTRAL_H
#define CANITY_NEUTRAL_H

#define MOTE_MIN_LEVEL      2
#define MOTE_APPEAR_CHANCE  30     /* 百分比 */
#define MOTE_SPEED          0.45f  /* 慢 */
#define MOTE_FLEE_SPEED     1.30f  /* 受惊时快，但仍慢于玩家 */
#define MOTE_FLEE_RADIUS    4.0f   /* 玩家进到这个距离就逃 */
#define MOTE_LIGHT_ATTRACT  3.0f   /* 照明棒吸引半径 */
#define MOTE_LIGHT_KEEP     2.0f   /* 靠近到这个距离就停下 */

void  NeutralReset(void);
int   NeutralTrySpawn(int level);     /* 返回 1 = 本层有 */
void  NeutralTick(float dt);
int   NeutralCount(void);
float NeutralX(int i);
float NeutralY(int i);
int   NeutralScared(void);            /* 供渲染/测试：当前是否在逃 */

/* 仅测试用 */
void  NeutralDebugPlace(float x, float y);

#endif /* CANITY_NEUTRAL_H */

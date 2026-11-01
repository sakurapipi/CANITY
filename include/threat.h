/*
 * Canity — 威胁值 THREAT（v6）
 *
 * 局内改造树点得越多 → 属性越强 → THREAT 越高 → 怪越凶。
 * 所有效果都有硬上限，速度上限永远 < 玩家冲刺速度。
 */
#ifndef CANITY_THREAT_H
#define CANITY_THREAT_H

/* ---- 常量 ---- */

#define THREAT_MAX               18.0f

/* 每个节点的威胁权重 */
#define THREAT_W_L1               0.5f
#define THREAT_W_L2               0.8f
#define THREAT_W_L3               1.2f
#define THREAT_W_CAPSTONE         2.0f

/* 速度上限 = BASE + T × PER_POINT，硬顶 HARD_CAP
   ★ 硬顶 2.33 距玩家冲刺 2.40 留 0.07 —— 这条不能破 */
#define THREAT_SPEED_BASE         2.05f
#define THREAT_SPEED_PER_POINT   0.0155f
#define THREAT_SPEED_HARD_CAP     2.33f

/* 其余四项 */
#define THREAT_CD_PER_POINT       0.03f
#define THREAT_CD_MAX             0.54f
#define THREAT_CHASE_PER_POINT    0.25f
#define THREAT_CHASE_MAX          4.50f
#define THREAT_SPAWN_PER_POINT    0.40f
#define THREAT_SPAWN_MAX          7.20f
#define THREAT_HEAR_PER_POINT    0.015f
#define THREAT_HEAR_MAX           1.27f

/* ---- 接口 ---- */

void  ThreatReset(void);
void  ThreatAdd(float v);
void  ThreatSet(float v);
float ThreatValue(void);
float ThreatRatio(void);

void  ThreatAddForTier(int tier, int isCapstone);

float ThreatSpeedCap(void);
float ThreatCooldownDelta(void);
float ThreatChaseBonus(void);
float ThreatSpawnAdvance(void);
float ThreatHearMul(void);

#endif /* CANITY_THREAT_H */

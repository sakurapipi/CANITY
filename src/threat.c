/*
 * Canity — **威胁值 THREAT**（v6 新模块）
 *
 * 这个系统回答一个问题：
 *   玩家在局内改造树点得越多，就越强 —— 那平衡怎么维持？
 *
 * 答案：**属性越强，怪越凶**。把玩家的成长直接换算成怪物强度。
 *
 * 为什么这样设计（调研结论）
 * ────────────────────────
 *   · 「Permanent options, temporary power」——
 *     局内给强度是对的，但强度必须**有代价**，否则只是无脑点满
 *   · 威胁值让玩家**自己决定什么时候加难度**，
 *     比固定难度曲线耐玩得多
 *   · 所有效果都必须有上限 —— 否则点满就必死，玩家不敢点
 *
 * 硬约束（不能破）
 * ────────────────
 *   **速度上限永远 ≤ 2.33，玩家冲刺是 2.40。**
 *   冲刺必须是永远可信的保命手段。
 */
#include "common.h"
#include "threat.h"

/* ---- 累积 ---- */

static float s_threat = 0.0f;

void ThreatReset(void) {
    s_threat = 0.0f;
}

void ThreatAdd(float v) {
    if (v <= 0.0f) return;
    s_threat += v;
    if (s_threat > THREAT_MAX) s_threat = THREAT_MAX;
}

void ThreatSet(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > THREAT_MAX) v = THREAT_MAX;
    s_threat = v;
}

float ThreatValue(void) { extern int skillRank[]; float t=s_threat+skillRank[26]; return t>18.0f ? 18.0f : t; }

/* 0..1 的比例，UI 画进度用 */
float ThreatRatio(void) { return s_threat / THREAT_MAX; }

/* ==================================================================
 * 速度上限 —— **随点技能变化**（这是本项目最关键的一条数值约束）
 * ==================================================================
 *   2.05 + T × 0.0155，硬顶 2.33
 *   T=0   → 2.05（和原来一样）
 *   T=18  → 2.33（距冲刺 2.40 只剩 0.07）
 */
float ThreatSpeedCap(void) {
    float cap = THREAT_SPEED_BASE + ThreatValue() * THREAT_SPEED_PER_POINT;
    if (cap > THREAT_SPEED_HARD_CAP) cap = THREAT_SPEED_HARD_CAP;
    return cap;
}

/* ---- 其余四项效果（全部有上限）---- */

/* 抓捕冷却：越强越短（−0.03/点，最多 −0.54） */
float ThreatCooldownDelta(void) {
    float d = ThreatValue() * THREAT_CD_PER_POINT;
    if (d > THREAT_CD_MAX) d = THREAT_CD_MAX;
    return -d;
}

/* 追逐时长：越强追得越久（+0.25/点，最多 +4.5 秒） */
float ThreatChaseBonus(void) {
    float d = ThreatValue() * THREAT_CHASE_PER_POINT;
    if (d > THREAT_CHASE_MAX) d = THREAT_CHASE_MAX;
    return d;
}

/* 第二只出场提前（−0.4/点，最多 −7.2 秒） */
float ThreatSpawnAdvance(void) {
    float d = ThreatValue() * THREAT_SPAWN_PER_POINT;
    if (d > THREAT_SPAWN_MAX) d = THREAT_SPAWN_MAX;
    return d;
}

/* 听觉半径倍率（+1.5%/点，最多 +27%） */
float ThreatHearMul(void) {
    float m = 1.0f + ThreatValue() * THREAT_HEAR_PER_POINT;
    if (m > THREAT_HEAR_MAX) m = THREAT_HEAR_MAX;
    return m;
}

/* ---- 买节点时按层级加威胁 ---- */

void ThreatAddForTier(int tier, int isCapstone) {
    if (isCapstone) { ThreatAdd(THREAT_W_CAPSTONE); return; }
    switch (tier) {
    case 1:  ThreatAdd(THREAT_W_L1); break;
    case 2:  ThreatAdd(THREAT_W_L2); break;
    case 3:  ThreatAdd(THREAT_W_L3); break;
    default: ThreatAdd(THREAT_W_L1); break;
    }
}

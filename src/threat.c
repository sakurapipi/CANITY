/* Build pressure owns its value, quote and bounded monster modifiers.
 * Purchase weights, HUD ratio, save restoration and runtime reads share this module.
 * Gold affects sensing through economy.c and never multiplies movement speed.
 */
#include "common.h"
#include "skills.h"
#include "threat.h"

/* ---- 累积 ---- */

static float s_threat = 0.0f;

void ThreatReset(void) {
    s_threat = 0.0f;
}

void ThreatAdd(float v) {
    if (!isfinite(v) || v <= 0.0f) return;
    s_threat += v;
    if (s_threat > THREAT_MAX) s_threat = THREAT_MAX;
}

void ThreatSet(float v) {
    if (!isfinite(v) || v < 0.0f) v = 0.0f;
    if (v > THREAT_MAX) v = THREAT_MAX;
    s_threat = v;
}

void ThreatRestoreTotal(float savedTotal) {
    float skill = (float)SkillClampRank(SK_GREEDHAND, skillRank[SK_GREEDHAND]);
    ThreatSet(savedTotal - skill);
}
float ThreatValue(void) {
    float value = s_threat + (float)SkillClampRank(SK_GREEDHAND, skillRank[SK_GREEDHAND]);
    return value > THREAT_MAX ? THREAT_MAX : value;
}


/* 0..1 的比例，UI 画进度用 */
float ThreatRatio(void) { return ThreatValue() / THREAT_MAX; }

/* ==================================================================
 * 速度上限 —— **随点技能变化**（这是本项目最关键的一条数值约束）
 * ==================================================================
 *   2.05 + T × THREAT_SPEED_PER_POINT，硬顶 2.20
 *   T=0   → 2.05（和原来一样）
 *   T=18  → 2.20（使用统一硬顶）
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

float ThreatCostForTier(int tier, int isCapstone) {
    if (isCapstone) return THREAT_W_CAPSTONE;
    switch (tier) {
    case 1: return THREAT_W_L1;
    case 2: return THREAT_W_L2;
    case 3: return THREAT_W_L3;
    default: return 0.0f;
    }
}
void ThreatAddForTier(int tier, int isCapstone) {
    ThreatAdd(ThreatCostForTier(tier, isCapstone));
}


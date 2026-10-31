/*
 * Canity — 经济区块（v8 重构第四刀）
 *
 * 目前管的是「携带金币 → 强度档位」这一条链。
 * 以后 BankSettle（结算）、撤离倍数、协议都会搬进来，
 * 让所有「和钱有关」的逻辑集中在一个文件里。
 *
 * 铁律（写在代码里，防止以后改坏）：
 *   档位倍率**只作用于听觉和机制**，永远不作用于移动速度。
 *   玩家永远走得掉 —— 压力来自「藏不住」，不是「跑不过」。
 */
#include "common.h"
#include "state.h"
#include "economy.h"

/* ===== v7 **强度档位（16 档）** =====
   旧做法是连续乘数：每 5000 金币 +15%，上限 +100%。
   问题：玩家感觉不到自己「跨过了一条线」，而且它还被乘进了移动速度里
        （乘在 MONSTER_SPEED_FINAL_MAX 之后 → 带 3 万金币怪速到 2.86，
         超过玩家走速 2.0，把「玩家永远走得掉」这条铁律直接打穿）。

   新做法：按携带金币查档位表，1~16 档，每档有明确的倍率。
         而且**倍率只作用于听觉，永远不作用于移动速度**。 */
typedef struct { int minGold; float mul; } GoldTierDef;
static const GoldTierDef kGoldTiers[16] = {
    {       0, 1.00f },   /* T0  */
    {    3000, 1.05f },   /* T1  */
    {    6000, 1.10f },   /* T2  */
    {   10000, 1.15f },   /* T3  听觉 +15% */
    {   15000, 1.20f },   /* T4  */
    {   22000, 1.25f },   /* T5  记住藏身点 */
    {   32000, 1.30f },   /* T6  */
    {   46000, 1.35f },   /* T7  拾荒者出现 */
    {   65000, 1.40f },   /* T8  */
    {   91000, 1.45f },   /* T9  互相传信 */
    {  128000, 1.50f },   /* T10 */
    {  180000, 1.55f },   /* T11 回声 +50% */
    {  250000, 1.60f },   /* T12 */
    {  350000, 1.65f },   /* T13 清道夫提前 */
    {  500000, 1.70f },   /* T14 */
    {  700000, 1.80f },   /* T15 */
};

/* 当前档位 0~15 */
int GoldTier(void) {
    extern int carryGold;
    int g = carryGold > 0 ? carryGold : 0;
    int t = 0;
    for (int i = 0; i < 16; i++)
        if (g >= kGoldTiers[i].minGold) t = i;
    /* v7 B3：协议在本档位上做 ±2 修正 */
    t += ContractTierBonus();
    if (t < 0) t = 0;
    if (t > 15) t = 15;
    return t;
}

/* 当前档位的强度倍率 */
float GoldTierMul(void) {
    int t = GoldTier();
    if (t < 0) t = 0;
    if (t > 15) t = 15;
    return kGoldTiers[t].mul;
}

/* 兼容旧调用点：威胁倍率 = 档位倍率 */
float GoldThreatMul(void) { return GoldTierMul(); }

/* 听觉半径倍数：金币**全额**放大听觉 */
float GoldHearMul(void) { return GoldTierMul(); }

/* ===== v7 **速度永不受金币影响** =====
   原来这里返回 1 + (m-1)*0.40，而它是乘在 MONSTER_SPEED_FINAL_MAX 之后的，
   于是带 3 万金币时怪速能到 2.86 —— 超过玩家走速 2.0。
   现在恒为 1.0：钱多只让你「藏不住」，不让你「跑不过」。 */
float GoldSpeedMul(void) { return 1.0f; }

/* ===== v7 协议（B3）=====
   在商店里签，每局一次，签了不能撕。
   核心逻辑：**高风险 = 高撤离回报**，但回报只在活着撤离时兑现。
     死了 → 本金只退 35%，协议倍数一分不给。 */
int contractKind = 0;   /* 0 SAFE / 1 STANDARD / 2 VOLUNTARY / 3 LAST */

/* 强度档位修正：SAFE −2 / STANDARD 0 / VOLUNTARY +1 / LAST +2 */
int ContractTierBonus(void) {
    switch (contractKind) {
    case 0: return -2;
    case 2: return +1;
    case 3: return +2;
    default: return 0;
    }
}

/* 撤离倍数：只乘「撤离时的深度倍数」，死了不给 */
float ContractExitMul(void) {
    switch (contractKind) {
    case 0: return 0.80f;
    case 2: return 1.20f;
    case 3: return 1.50f;
    default: return 1.00f;
    }
}

const char* ContractName(void) {
    switch (contractKind) {
    case 0: return "SAFE PASSAGE";
    case 2: return "VOLUNTARY RISK";
    case 3: return "LAST CONTRACT";
    default: return "STANDARD";
    }
}

void ContractCycle(void) {
    contractKind = (contractKind + 1) % 4;
}

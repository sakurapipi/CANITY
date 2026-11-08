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
#include "campaign.h"
#include "daily.h"
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
/* ==================================================================
 * v7 第 ㊿ 轮：**16 档里有 13 档永远到不了**
 * ==================================================================
 * 旧阈值 T1=3000 … T15=700000。
 * 而单层收入按代码自己的注释是 102/267/447，
 * 24 层全清不花钱 ≈ 6,600 金 → 顶层只到 T2（听觉 ×1.10）。
 * T3 之后 13 档在任何模式下都摸不到。
 *
 * 后果：这是"贪"这个决策的唯一载体，也是战役第 4 关（tierMul=2）、
 *       每日 GREED（+2 档）、协议（±2 档）三套系统的共同地基。
 *       地基是平的 → 三个上层系统一起空转。
 *
 * 修法：阈值整体除以 8（并保住递进比例），让每一层都能跨档。
 * ================================================================== */
static const GoldTierDef kGoldTiers[16] = {
    {       0, 1.00f },   /* T0  */
    {     400, 1.05f },   /* T1  */
    {     800, 1.10f },   /* T2  */
    {    1300, 1.15f },   /* T3  听觉 +15% */
    {    1900, 1.20f },   /* T4  */
    {    2800, 1.25f },   /* T5  记住藏身点 */
    {    4000, 1.30f },   /* T6  */
    {    5800, 1.35f },   /* T7  拾荒者出现 */
    {    8200, 1.40f },   /* T8  */
    {   11500, 1.45f },   /* T9  互相传信 */
    {   16000, 1.50f },   /* T10 */
    {   22500, 1.55f },   /* T11 回声 +50% */
    {   31500, 1.60f },   /* T12 */
    {   44000, 1.65f },   /* T13 清道夫提前 */
    {   62000, 1.70f },   /* T14 */
    {   88000, 1.80f },   /* T15 */
};

/* 当前档位 0~15 */
int GoldTier(void) {
    /* v6：每日任务的 GREED 修正把强度档 +2（钱更值钱，但更危险） */

    /* v6 战役第 4 关「贪婪深渊」：每枚金币按 2 枚算强度 */
    float tm = CampaignTierMul();
    int g = (int)((float)(carryGold > 0 ? carryGold : 0) * tm + 0.5f);
    int t = 0;
    for (int i = 0; i < 16; i++)
        if (g >= kGoldTiers[i].minGold) t = i;
    /* v7 B3：协议在本档位上做 ±2 修正 */
    t += ContractTierBonus();
    /* v6：每日 GREED 修正再 ±2 */
    t += DailyGoldTierBonus();
    {extern int HasKeystone(int);if(HasKeystone(SK_WEATHERMAN))t+=2;}
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
int contractKind = 1;   /* 0 SAFE / 1 STANDARD / 2 VOLUNTARY / 3 LAST */

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

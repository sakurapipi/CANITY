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
#include "lang.h"
#include "skills.h"
#include "campaign.h"
#include "daily.h"
#include "state.h"
#include "economy.h"
#include "threat.h"
#include "wallet.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"

/* Carry pressure bands are independent of bank balance. A normal expedition can
 * cross several early bands; later bands remain reachable through deeper play.
 * Campaign gold weighting, contracts and daily rules adjust the effective band.
 */
typedef struct { int minGold; float mul; } GoldTierDef;
static const GoldTierDef kGoldTiers[16] = {
    {     0, 1.00f },
    {   150, 1.05f },
    {   350, 1.10f },
    {   650, 1.15f },
    {  1000, 1.20f },
    {  1500, 1.25f },
    {  2200, 1.30f },
    {  3100, 1.35f },
    {  4300, 1.40f },
    {  5800, 1.45f },
    {  7800, 1.50f },
    { 10500, 1.55f },
    { 14000, 1.60f },
    { 18500, 1.65f },
    { 24500, 1.70f },
    { 32000, 1.75f },
};

/* 当前档位 0~15 */
int GoldTier(void) {
    /* v6：每日任务的 GREED 修正把强度档 +2（钱更值钱，但更危险） */

    /* v6 战役第 4 关「贪婪深渊」：每枚金币按 2 枚算强度 */
    double tm = CampaignTierMul();
    if (!isfinite(tm) || tm < 0.0) tm = 1.0;
    double effective = (double)(carryGold > 0 ? carryGold : 0) * tm;
    int g = effective >= INT_MAX ? INT_MAX : (int)(effective + 0.5);
    int t = 0;
    for (int i = 0; i < 16; i++)
        if (g >= kGoldTiers[i].minGold) t = i;
    /* v7 B3：协议在本档位上做 ±2 修正 */
    t += ContractTierBonus();
    /* v6：每日 GREED 修正再 ±2 */
    t += DailyGoldTierBonus();
    {if(HasKeystone(SK_WEATHERMAN))t+=2;}
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
float GoldSpeedMul(void) { /* FIXED_RESULT: Gold intentionally changes hearing rather than monster movement speed. */ return 1.0f; }

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

int GoldNextThreshold(void) {
    double mul = CampaignTierMul();
    if (!isfinite(mul) || mul <= 0.0) return -1;
    int bonus = ContractTierBonus() + DailyGoldTierBonus() + (HasKeystone(SK_WEATHERMAN) ? 2 : 0);
    int next = GoldTier() + 1 - bonus;
    if (GoldTier() >= 15 || next >= 16) return -1;
    if (next < 1) next = 1;
    double required = ceil((double)kGoldTiers[next].minGold / mul);
    return required >= INT_MAX ? INT_MAX : (int)required;
}
float EconomyHearMul(void) {
    /* Gold and build power are additive pressures, not an unbounded product. */
    float combined = 1.0f + (GoldHearMul() - 1.0f) + (ThreatHearMul() - 1.0f);
    return combined > 2.0f ? 2.0f : combined;
}
float EconomyPressureRatio(void) {
    return 0.5f * ((float)GoldTier() / 15.0f + ThreatRatio());
}

int EconomyRewardedProfit(int survived, float* performance) {
    float pm=1.0f;
    if(survived) {
        if(!tookDamage)pm*=1.5f;
        if(!runSpotted)pm*=1.3f;
        if(!runLightUse)pm*=1.2f;
        if(pm>2.34f)pm=2.34f;
    }
    if(performance)*performance=pm;
    double profit=(double)WalletRun()-(carryAtRunStart>0?carryAtRunStart:0);
    if(profit<=0.0)return 0;
    if(survived)profit*=pm*ExtractDepthMul(currentLevel)*ContractExitMul()*SkillExtractMul();
    if(!isfinite(profit)||profit<0.0)return 0;
    return profit>=(double)INT_MAX?INT_MAX:(int)(profit+.5);
}

int bankRiskPlan=1;
int BankCarryAmount(void) {
    int step=bankCarryPct;
    if(step<0)step=0;
    if(step>=BANK_CARRY_STEPS)step=BANK_CARRY_STEPS-1;
    return (int)((double)WalletBank()*(step*BANK_STEP_PCT)/100.0+.5);
}
int BankPreviewTier(void) {
    static const int bonus[4]={-2,0,1,2};
    int plan=bankRiskPlan;
    if(plan<0 || plan>3)plan=1;
    int gold=BankCarryAmount(),tier=0;
    for(int i=0;i<16;i++)if(gold>=kGoldTiers[i].minGold)tier=i;
    tier+=bonus[plan]+(HasKeystone(SK_WEATHERMAN)?2:0);
    if(tier<0)tier=0;
    if(tier>15)tier=15;
    return tier;
}
float BankPreviewHearing(void) {
    float skill=(float)SkillClampRank(SK_GREEDHAND,skillRank[SK_GREEDHAND]);
    return kGoldTiers[BankPreviewTier()].mul+skill*THREAT_HEAR_PER_POINT;
}
float BankRiskExitMul(int plan) {
    static const float value[4]={.8f,1.0f,1.2f,1.5f};
    return plan>=0 && plan<4?value[plan]:1.0f;
}
const char* BankRiskName(int plan) {
    static const char* en[4]={"SAFE PASSAGE","STANDARD","VOLUNTARY RISK","LAST CONTRACT"};
    static const char* zh[4]={"稳健撤离","标准行动","自愿冒险","背水一战"};
    if(plan<0 || plan>3)plan=1;
    return Tr(en[plan],zh[plan]);
}

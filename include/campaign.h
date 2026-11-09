/*
 * Canity — 战役模式（v6 重做）
 *
 * 改了什么
 * ────────
 * 原来 5 关只是「倍率微调」：理智 ×0.55、怪速 ×1.18、耗电 ×2…
 * 玩家感觉不到"这是另一个关卡"，只觉得数值变了。
 *
 * 现在：**每关只改一条规则**，而且这条规则必须是玩家立刻能感觉到的。
 *   1 黑暗工厂  手电永远关不掉        → 教"光会招怪"
 *   2 无声区    任何噪音召来第二只    → 教学"噪音 = 直接失败"
 *   3 守门人之城 通电即刷守门人+出口远 → 教学"规划一次突围"
 *   4 贪婪深渊  强度档 ×2 生效        → 教学"赚多少收手"
 *   5 最后一道门 追逐永不放弃          → 终极考试
 *
 * 设计依据：好关卡的共同点不是"更大更难"，而是**有一条新规则**。
 */
#ifndef CANITY_CAMPAIGN_H
#define CANITY_CAMPAIGN_H

#include "common.h"

#define CAMPAIGN_STAGE_COUNT 5

typedef struct {
    const char* nameEn;
    const char* nameZh;
    const char* ruleEn;        /* 这一关的规则（一句话） */
    const char* ruleZh;

    /* ---- 规则标志 ---- */
    int   flashlightLocked;    /* 1 = 手电永远开，关不掉 */
    float noiseAlertMul;       /* 噪音半径倍率（1.0 = 不变） */
    int   wardenOnPower;       /* 1 = 通电立刻刷守门人 */
    float exitDistMul;         /* 出口距离倍率 */
    float tierMul;             /* 携带金币算强度档时的倍率 */
    int   noGiveUp;            /* 1 = 追逐永不放弃（14 秒规则失效） */

    /* ---- 关卡规模 ---- */
    int   layers;              /* 这一关有几层 */
    int   fuses;               /* 每层保险丝需求 */

    /* ---- 环境倍率（辅助气氛）---- */
    float speedMul;
    float sanityMul;
    float batteryMul;
    float goldMul;
} CampaignDef;

extern const CampaignDef kCampaign[];
extern int campaignMode;
extern int campaignStage;
/* v7：每关的层数 / 保险丝（原来这两个字段没人读） */
int CampaignLayersNow(void);
int CampaignStartFloor(int stage);
int CampaignReward(int stage);
int CampaignWardenImmediate(void);
float CampaignExitDistNow(void);
int CampaignFusesNow(void);

const char* CampaignName(int stage);
const char* CampaignRule(int stage);

/* 当前战役关的规则查询（非战役模式时全部返回"不变"） */
int   CampaignFlashlightLocked(void);
float CampaignNoiseAlertMul(void);
int   CampaignWardenOnPower(void);
float CampaignExitDistMul(void);
float CampaignTierMul(void);
int   CampaignNoGiveUp(void);
float CampaignSpeedMulNow(void);
float CampaignSanityMulNow(void);
float CampaignBatteryMulNow(void);
float CampaignGoldMulNow(void);

void CampaignBuildFloor(void);
int CampaignThemeNow(void);
#endif /* CANITY_CAMPAIGN_H */
const char* CampaignRuleZh(int stage);

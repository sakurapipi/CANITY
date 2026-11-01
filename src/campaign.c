/*
 * Canity — 战役 5 关（v6）
 *
 * 每关只改一条规则。加/改关卡 = 改这一张表。
 */
#include <stddef.h>
#include "common.h"
#include "campaign.h"

const CampaignDef kCampaign[] = {
    /* ---- 1 黑暗工厂：手电永远关不掉 ---- */
    {
        "DARK FACTORY", "黑暗工厂",
        "Your flashlight cannot be turned off.",
        "手电永远关不掉。",
        1,          /* flashlightLocked */
        1.00f,      /* noiseAlertMul */
        0,          /* wardenOnPower */
        1.00f,      /* exitDistMul */
        1.00f,      /* tierMul */
        0,          /* noGiveUp */
        1, 4,       /* layers, fuses */
        1.00f, 0.55f, 1.00f, 1.00f
    },
    /* ---- 2 无声区：任何噪音都会召来第二只猎手 ---- */
    {
        "SILENT ZONE", "无声区",
        "Every noise you make is heard everywhere. Sprint at your own risk.",
        "任何噪音都会传遍全层。冲刺等于自杀。",
        0,
        3.00f,      /* 噪音半径 ×3 */
        0,
        1.00f,
        1.00f,
        0,
        2, 3,
        1.00f, 1.00f, 1.00f, 1.00f
    },
    /* ---- 3 守门人之城：通电即刷守门人，出口更远 ---- */
    {
        "WARDEN'S CITY", "守门人之城",
        "The Warden wakes the moment you power the breaker. The exit is far.",
        "配电箱一通电，守门人立刻出现。出口比平时远一倍。",
        0,
        1.00f,
        1,          /* wardenOnPower */
        2.00f,      /* exitDistMul */
        1.00f,
        0,
        1, 6,
        1.05f, 1.00f, 1.00f, 1.30f
    },
    /* ---- 4 贪婪深渊：强度档 ×2 ---- */
    {
        "GREED ABYSS", "贪婪深渊",
        "Every coin you carry counts double against you. Gold is doubled too.",
        "你带的每一枚金币都以双倍计入强度。金币收益也翻倍。",
        0,
        1.00f,
        0,
        1.00f,
        2.00f,      /* tierMul */
        0,
        3, 3,
        1.00f, 1.00f, 1.00f, 2.00f
    },
    /* ---- 5 最后一道门：追逐永不放弃 ---- */
    {
        "THE LAST DOOR", "最后一道门",
        "Nothing gives up. Everything you avoided is waiting here.",
        "没有东西会放弃。你躲过的一切都在这里等你。",
        0,
        1.15f,
        1,
        1.30f,
        1.50f,
        1,          /* noGiveUp */
        1, 8,
        1.10f, 1.25f, 1.20f, 1.50f
    },
};

typedef char kCampaign_count_check[(sizeof(kCampaign)/sizeof(kCampaign[0]) == CAMPAIGN_STAGE_COUNT) ? 1 : -1];

/* ---- 查询 ---- */

static const CampaignDef* cur(void) {
    if (!campaignMode) return NULL;
    if (campaignStage < 0 || campaignStage >= CAMPAIGN_STAGE_COUNT) return NULL;
    return &kCampaign[campaignStage];
}

const char* CampaignName(int stage) {
    if (stage < 0 || stage >= CAMPAIGN_STAGE_COUNT) return "?";
    return kCampaign[stage].nameEn;
}
const char* CampaignRule(int stage) {
    if (stage < 0 || stage >= CAMPAIGN_STAGE_COUNT) return "";
    return kCampaign[stage].ruleEn;
}

/* v7 第 62 轮：中文规则访问器（原来只有 En） */
const char* CampaignRuleZh(int stage) {
    if (stage < 0 || stage >= CAMPAIGN_STAGE_COUNT) return "";
    return kCampaign[stage].ruleZh;
}

int CampaignFlashlightLocked(void) {
    const CampaignDef* d = cur();
    return d ? d->flashlightLocked : 0;
}
float CampaignNoiseAlertMul(void) {
    const CampaignDef* d = cur();
    return d ? d->noiseAlertMul : 1.0f;
}
int CampaignWardenOnPower(void) {
    const CampaignDef* d = cur();
    return d ? d->wardenOnPower : 0;
}
float CampaignExitDistMul(void) {
    const CampaignDef* d = cur();
    return d ? d->exitDistMul : 1.0f;
}
float CampaignTierMul(void) {
    const CampaignDef* d = cur();
    return d ? d->tierMul : 1.0f;
}
int CampaignNoGiveUp(void) {
    const CampaignDef* d = cur();
    return d ? d->noGiveUp : 0;
}
float CampaignSpeedMulNow(void) {
    const CampaignDef* d = cur();
    return d ? d->speedMul : 1.0f;
}
float CampaignSanityMulNow(void) {
    const CampaignDef* d = cur();
    return d ? d->sanityMul : 1.0f;
}
float CampaignBatteryMulNow(void) {
    const CampaignDef* d = cur();
    return d ? d->batteryMul : 1.0f;
}
float CampaignGoldMulNow(void) {
    const CampaignDef* d = cur();
    return d ? d->goldMul : 1.0f;
}


/* ==================================================================
 * v6 第 ㉓ 轮：把最后两条战役规则接上
 * ==================================================================
 * 第 ⑧ 轮写好了 wardenOnPower / exitDistMul 两个字段，
 * 但**没有任何系统读它们** —— 第 3 关和第 5 关的规则是空的。
 * ================================================================== */

/* 第 3 关「守门人之城」：通电立刻刷守门人（不等防守计时） */
int CampaignWardenImmediate(void) {
    const CampaignDef* d = cur();
    return d ? d->wardenOnPower : 0;
}

/* 第 3/5 关：出口距离倍率（>1 = 更远） */
float CampaignExitDistNow(void) {
    const CampaignDef* d = cur();
    return d ? d->exitDistMul : 1.0f;
}

/* ==================================================================
 * v7 第 ㊿ 轮：**layers / fuses 两个字段填好了但全工程 0 个读取点**
 * ==================================================================
 * 设计文档说每关规模各异（1/2/1/3/1 层，保险丝 4/3/6/3·3·4/8），
 * 而实际层数由写死的 CAMPAIGN_LEVELS_PER_STAGE = 3 决定
 * → **5 关全是 3 层**，玩家感受到的是"还是那三层，怪快一点"。
 *
 * 文档自己的关卡原则是「好关卡不是更大更难，而是有一条新规则」。
 * 现在把这两个字段接出来。
 * ================================================================ */
int CampaignLayersNow(void) {
    if (campaignStage < 0 || campaignStage >= CAMPAIGN_STAGE_COUNT) return 3;
    {
        int n = kCampaign[campaignStage].layers;
        return (n > 0 && n <= 8) ? n : 3;
    }
}

int CampaignFusesNow(void) {
    if (campaignStage < 0 || campaignStage >= CAMPAIGN_STAGE_COUNT) return 0;
    {
        int n = kCampaign[campaignStage].fuses;
        return (n > 0 && n <= 8) ? n : 0;
    }
}


int CampaignStartFloor(int stage) {
    int floor = 1;
    for (int i = 0; i < stage && i < CAMPAIGN_STAGE_COUNT; ++i) floor += kCampaign[i].layers;
    return floor;
}
int CampaignReward(int stage) {
    static const int rewards[] = {3000, 4000, 6000, 8000, 15000};
    return stage >= 0 && stage < CAMPAIGN_STAGE_COUNT ? rewards[stage] : 0;
}

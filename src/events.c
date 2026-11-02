/*
 * Canity — 特殊事件表（v6）
 *
 * 10 个事件，每个都真的改一条规则。
 * 加事件 = 在 kEvents[] 里加一行，别处不用动。
 */
#include "common.h"
#include "events.h"

#define NONE  1.0f   /* 不变 */
#define P100  100

/* 顺序必须和 common.h 的 EV_ 枚举一致 */
const EventDef kEvents[] = {
    /* ---- EV_BLOOD_MOON 血月：怪更快，钱更多 ---- */
    {
        "BLOOD MOON", "血月", "BLOOD",
        "Monsters move 20% faster. Gold +50%.",
        "怪物速度 +20%，金币 +50%。",
        12, 1500,
        { 1.20f, 1.00f, NONE, NONE, 1.50f, NONE, NONE, 0, P100 }
    },
    /* ---- EV_BLACKOUT 停电：电量吃紧，视野变差 ---- */
    {
        "BLACKOUT", "停电", "BLACK",
        "Battery drains 80% faster. Battery cap halved.",
        "耗电 +80%，本层电池上限砍半。",
        12, 1000,
        { NONE, NONE, 1.80f, NONE, NONE, NONE, NONE, 0, 50 }
    },
    /* ---- EV_SILENCE 死寂：你更安静，但它们更灵敏 ---- */
    {
        "DEAD SILENCE", "死寂", "SILENT",
        "Your noise -60%, but monster hearing +40%.",
        "你的噪音 -60%，但怪物听觉 +40%。",
        10, 800,
        { NONE, 1.40f, NONE, NONE, NONE, 0.40f, NONE, 0, P100 }
    },
    /* ---- EV_SUPPLY 空投：纯正面，金币和道具都多 ---- */
    {
        "SUPPLY DROP", "空投", "SUPPLY",
        "Gold +30%. Extra throwables this floor.",
        "金币 +30%，本层多给投掷物。",
        12, 600,
        { NONE, NONE, NONE, NONE, 1.30f, NONE, NONE, 0, P100 }
    },
    /* ---- EV_FLOOD 洪水：走不快，声音传得远 ---- */
    {
        "FLOOD", "洪水", "FLOOD",
        /* v7 第 60 轮：**删掉"你更慢"**
           事件结构里根本没有"玩家移速"字段，
           而 noiseMul=1.40f 是真的生效的。留着一半假话只会让玩家学错策略。 */
        "You are 40% louder on the wet floor.",
        "地板积水：噪音 +40%。",
        10, 1200,
        { NONE, NONE, NONE, NONE, NONE, 1.40f, NONE, 0, P100 }
    },
    /* ---- EV_WHISPER 低语：拿理智换钱 ---- */
    {
        "WHISPERS", "低语", "WHISPER",
        "Sanity drains 50% faster. Gold +40%.",
        "理智流失 +50%，金币 +40%。",
        10, 1300,
        { NONE, NONE, NONE, 1.50f, 1.40f, NONE, NONE, 0, P100 }
    },
    /* ---- EV_MIRROR 镜像：多刷三只游荡怪 ---- */
    {
        "MIRROR", "镜像", "MIRROR",
        /* v7 第 60 轮：**删掉"3 只游荡怪"**
           ExtrasSpawnOne() 是空函数（v7 删游荡怪后没接回来），
           而 monsterSpeedMul=1.10 / sanityDrainMul=1.10 是真的。 */
        "Monsters 10% faster. Sanity drains 10% faster.",
        "本层怪速 +10%，理智流失 +10%。",
        8, 1600,
        { 1.10f, NONE, NONE, 1.10f, NONE, NONE, NONE, 3, P100 }
    },
    /* ---- EV_HEAT 热浪：电池难受，怪略快 ---- */
    {
        "HEAT WAVE", "热浪", "HEAT",
        "Battery cap -30%. Monsters 10% faster.",
        "电池上限 -30%，怪速 +10%。",
        10, 1100,
        { 1.10f, NONE, NONE, NONE, NONE, NONE, NONE, 0, 70 }
    },
    /* ---- EV_FOG 浓雾：不容易被听见（"看不远"那条没实现，描述已删） ---- */
    {
        "FOG", "浓雾", "FOG",
        "Monster hearing -20% this floor.",
        "本层怪物听觉 -20%。",
        10, 1400,
        { NONE, 0.80f, NONE, NONE, NONE, NONE, NONE, 0, P100 }
    },
    /* ---- EV_LUCKY 幸运：纯正面，低权重 ---- */
    {
        "LUCKY FIND", "幸运", "LUCKY",
        "Gold doubled this floor.",
        "本层金币翻倍。",
        8, 300,
        { NONE, NONE, NONE, NONE, 2.00f, NONE, NONE, 0, P100 }
    },
};

typedef char kEvents_count_check[(sizeof(kEvents)/sizeof(kEvents[0])==EVENT_COUNT) ? 1 : -1];

/* 越界时用的"无效果" */
static const EventEffect kNoEffect = {
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, 0, P100
};

/* v7：给 entity.c 用的取值函数（避免把结构体类型暴露出去） */
float EventMonsterHearMul(int ev) {
    const EventEffect* fx = EventEffectFor(ev);
    return fx ? fx->monsterHearMul : 1.0f;
}

const EventEffect* EventEffectFor(int ev) {
    if (ev < 0 || ev >= EVENT_COUNT) return &kNoEffect;
    return &kEvents[ev].fx;
}

const char* EventNameEn(int ev) {
    return (ev < 0 || ev >= EVENT_COUNT) ? "" : kEvents[ev].nameEn;
}
const char* EventNameZh(int ev) {
    return (ev < 0 || ev >= EVENT_COUNT) ? "" : kEvents[ev].nameZh;
}
const char* EventShortEn(int ev) {
    return (ev < 0 || ev >= EVENT_COUNT) ? "?" : kEvents[ev].shortEn;
}
const char* EventDescEn(int ev) {
    return (ev < 0 || ev >= EVENT_COUNT) ? "" : kEvents[ev].descEn;
}
const char* EventDescZh(int ev) {
    return (ev < 0 || ev >= EVENT_COUNT) ? "" : kEvents[ev].descZh;
}
int EventScoreOf(int ev) {
    return (ev < 0 || ev >= EVENT_COUNT) ? 0 : kEvents[ev].score;
}

/* 按权重抽；用调用方给的种子，保证可复现 */
int EventPickRandom(unsigned int* seed) {
    if (!seed) return -1;
    int total = 0;
    for (int i = 0; i < EVENT_COUNT; i++) total += kEvents[i].weight;
    if (total <= 0) return -1;

    unsigned int r = (*seed = *seed * 1664525u + 1013904223u) % (unsigned int)total;
    int acc = 0;
    for (int i = 0; i < EVENT_COUNT; i++) {
        acc += kEvents[i].weight;
        if ((int)r < acc) return i;
    }
    return EVENT_COUNT - 1;
}

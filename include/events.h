/*
 * Canity — 特殊事件（v6 表驱动重做）
 *
 * 改了什么
 * ────────
 * 原来：7 个事件散落在 game.c 的 if-else 链里，加分在另一处 switch 里。
 * 加一个事件要改 3 个地方，而且很容易漏掉"它到底改了什么规则"。
 *
 * 现在：**一张表**。每个事件就是一行 EventDef，
 *       游戏各处只读这些倍率。加事件 = 加一行。
 *
 * 设计原则（用户要求）
 * ────────────────────
 * 「经过多轮迭代不会发生变化的东西给完全程序化」
 *   · 事件的效果 = 一组倍率，不是一段代码
 *   · 加/改事件不动逻辑
 *   · 玩家能感知到每个事件"这层到底哪里不一样"
 */
#ifndef CANITY_EVENTS_H
#define CANITY_EVENTS_H

/* 事件的规则修改（默认值见 EventEffectFor 的返回）*/
typedef struct {
    float monsterSpeedMul;   /* 怪物速度倍率 */
    float monsterHearMul;    /* 听觉半径倍率 */
    float batteryDrainMul;   /* 电池耗电倍率 */
    float sanityDrainMul;    /* 理智流失倍率 */
    float goldMul;           /* 金币收益倍率 */
    float noiseMul;          /* 玩家噪音倍率 */
    float fogMul;            /* 视野倍率（未接） */
    int   extraMonsters;     /* 额外刷几只游荡怪 */
    int   batteryCapPct;     /* 本层电池上限百分比，100 = 不变 */
} EventEffect;

typedef struct {
    const char* nameEn;
    const char* nameZh;
    const char* shortEn;     /* 小地图/HUD 用的缩写 */
    const char* descEn;
    const char* descZh;
    int   weight;            /* 抽取权重 */
    int   score;             /* 通过本层给的分 */
    EventEffect fx;
} EventDef;

extern const EventDef kEvents[];

/* 取某个事件的规则（ev 越界时返回"无效果"） */
const EventEffect* EventEffectFor(int ev);

/* 事件名（给 UI 用） */
const char* EventNameEn(int ev);
const char* EventNameZh(int ev);
const char* EventShortEn(int ev);
const char* EventDescEn(int ev);
const char* EventDescZh(int ev);
int         EventScoreOf(int ev);

/* 按权重随机抽一个事件；无事件时返回 -1 */
int EventPickRandom(unsigned int* seed);


/* Hearing modifier of a floor event. */
float EventMonsterHearMul(int ev);

#endif /* CANITY_EVENTS_H */

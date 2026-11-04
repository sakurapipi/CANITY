/*
 * Canity — 怪物图鉴（BESTIARY）v6 重做
 *
 * 改了什么
 * ────────
 * 原来：4 个字符串（名字/标签/行为/对策），没有任何数据。
 *
 * 现在：
 *   · **数值全部是函数指针** —— 运行时读真实的怪物常量
 *     改一个怪物速度，图鉴自动跟着变，一处都不用改
 *   · **三阶揭示** —— 遇见 1 次看名字，逃脱 3 次看行为，逃脱 5 次看对策
 *   · **玩家战绩** —— 遇到 / 逃脱 / 被抓 各自计数
 *   · **危险度** —— 从速度/听觉/追逐时长算出来，1~5 骷髅
 *
 * 调研依据：RPG Maker 论坛的开发者共识 ——
 *   "数值本身无意义，有价值的是独特机制 / 弱点 / 对策"
 *   "两段揭示（初见只有名字，之后才给细节）最受好评"
 */
#ifndef CANITY_BESTIARY_H
#define CANITY_BESTIARY_H

#include "common.h"

#define BESTIARY_COUNT 7   /* 2026-11-05：+1 宝箱怪 */

/* ---- 揭示阶段 ---- */
enum {
    BST_HIDDEN  = 0,   /* 没见过：显示 ??? */
    BST_NAMED   = 1,   /* 见过 1 次：名字 + 标签 + 剪影 + 危险度 */
    BST_BEHAVED = 2,   /* 逃脱 3 次：+ 行为 + 实时数值 */
    BST_COUNTER = 3    /* 逃脱 5 次：+ 对策 */
};

typedef struct {
    const char* nameEn;
    const char* nameZh;
    const char* tagEn;        /* NOISE / LIGHT / QTE / GOLD / TIME / ROAD */
    const char* tagZh;
    int         spriteId;     /* 用烘焙好的精灵当剪影 */

    /* ---- 程序化：运行时读真实常量 ---- */
    float (*getSpeed)(void);  /* 实际速度（格/秒） */
    float (*getHear)(void);   /* 听觉半径（格），0 = 无听觉 */
    float (*getChase)(void);  /* 追逐时长（秒），0 = 不追 */

    /* ---- 文案 ---- */
    const char* behaviorEn;
    const char* behaviorZh;
    const char* counterEn;
    const char* counterZh;
} BestiaryEntry;

extern const BestiaryEntry bestiary[BESTIARY_COUNT];
extern int bestiarySel;

/* ---- 认识进度 ---- */
extern int bestiarySeen;                     /* 位掩码：见过 */
extern int bestiaryEscapes[BESTIARY_COUNT];  /* 每种怪的逃脱次数 */
extern int bestiaryCaught[BESTIARY_COUNT];   /* 每种怪的被抓次数 */

void BestiaryMarkSeen(int id);
int  BestiaryIsSeen(int id);
int  BestiarySeenAll(void);

/* 逃脱一次 / 被抓一次（驱动揭示阶段） */
void BestiaryMarkEscaped(int id);
void BestiaryMarkCaught(int id);

/* 当前揭示阶段（BST_*） */
int BestiaryStage(int id);
void BestiaryBeginFloor(void);
void BestiarySave(int* values);
void BestiaryLoad(const int* values);

/* 危险度 1~5（从真实常量算） */
int BestiaryDanger(int id);

#endif /* CANITY_BESTIARY_H */
void BestiaryNoteFloor(int id, int level);   /* v7：遇到过的层数也算学习进度 */

/*
 * Canity — 每日任务（v6 新模块）
 *
 * 设计参考：Stick War: Saga 的 Daily Battles
 *   "Daily Battles change each day at 00:00 UTC, with the current one
 *    being drawn from a pool of scenarios."
 *
 * 关键洞察：它**不是随机生成**，是"从手写场景池里轮换"。
 * 真正让每天不同的是**特殊规则**，不是随机数字。
 *
 * 我们的做法：日期种子 + 三池组合
 *   ① 目标池（做什么）      8 张
 *   ② 修正池（世界怎么变）   8 张
 *   ③ 限制池（你不能做什么） 6 张
 *   → 8 × 8 × 6 = 384 种组合，只写 22 张卡片
 *
 * 种子 = 年月日 → **同一天全世界一样**（公平）
 */
#undef CANITY_DAILY_IMPL
#ifndef CANITY_DAILY_H
#define CANITY_DAILY_H

#include "common.h"

/* ---- 三个池的 id ---- */
enum {
    DQ_COLLECT = 0, DQ_ESCAPE, DQ_FLOOR, DQ_SEARCH,
    DQ_KNOCK, DQ_THROW, DQ_SURVIVE, DQ_NOHIT, DQ_COUNT
};
enum {
    DM_NORMAL = 0, DM_FAST, DM_DARK, DM_GREED,
    DM_SILENT, DM_HUNT, DM_RICH, DM_THIN, DM_COUNT
};
enum {
    DR_NONE = 0, DR_NOSPRINT, DR_NOLIGHT, DR_NOHIDE,
    DR_NOWRENCH, DR_NOMAP, DR_COUNT
};

/* ---- 今天的任务 ---- */
void  DailyTick(void);          /* 每帧：跨天自动刷新 */
void DailyGenerateForDate(unsigned int seed);
void DailyBeginRun(void);
void DailyObserve(int goal, int value);
float DailyBatteryCapMul(void);
float DailyCrateCountMul(void);
void  DailyGenerate(void);      /* 强制重新生成 */
/* v7：进度存盘（5 个 int：goalId, goalParam, progress, done, streak） */
void  DailySaveTo(int* a);
void  DailyLoadFrom(const int* a);
unsigned int DailySeedToday(void);

int   DailyGoalId(void);
int   DailyGoalParam(void);
int   DailyModId(void);
int   DailyRestrictId(void);
int   DailyDone(void);
int   DailyStreak(void);

/* 进度（0..1） */
float DailyProgress(void);
/* v6 第 41 轮：原始进度值（HUD 显示 3200/5000 用） */
int   DailyCurrent(void);
void  DailySetProgress(int v);
void  DailyMarkDone(void);

/* 修正池的效果（给别的系统读） */
float DailyMonsterSpeedMul(void);
float DailyBatteryDrainMul(void);
float DailyGoldMul(void);
int   DailyGoldTierBonus(void);
float DailyNoiseMul(void);
float DailySecondSpawnOffset(void);

/* 限制池的判定 */
int DailyForbidSprint(void);
int DailyForbidLight(void);
int DailyForbidHide(void);
int DailyForbidWrench(void);
int DailyForbidMonsterMap(void);

/* 文案 */
const char* DailyGoalTextEn(void);
const char* DailyGoalTextZh(void);
const char* DailyModTextEn(void);
const char* DailyModTextZh(void);
const char* DailyRestrictTextEn(void);
const char* DailyRestrictTextZh(void);

#endif /* CANITY_DAILY_H */

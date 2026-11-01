/*
 * Canity — 每日任务生成器（v6）
 *
 * 日期种子 + 三池组合 = 384 种，只写 22 张卡片。
 * 同一天全世界一样（种子 = 年月日）。
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "common.h"
#include "daily.h"
#include "game.h"
#include "wallet.h"
#include "audio.h"

/* ---------------- 确定性 RNG ---------------- */

static unsigned int DailyHash(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

unsigned int DailySeedToday(void) {
    time_t t = time(NULL);
    struct tm* lt = localtime(&t);
    if (!lt) return 20260101u;
    return (unsigned int)((lt->tm_year + 1900) * 10000
                        + (lt->tm_mon + 1) * 100
                        +  lt->tm_mday);
}

/* ---------------- 状态 ---------------- */

typedef struct {
    unsigned int seed;
    int goalId, modId, restrictId;
    int goalParam;
    int progress;
    int done;
    int streak;
    unsigned int lastCompleted;
    int samples[DQ_COUNT];
} DailyQuest;

static DailyQuest s_daily;
static int s_ready = 0;

/* ---------------- 池子数据 ---------------- */

static const int kGoalMin[DQ_COUNT] = { 3000, 1, 5, 20, 5, 5, 8, 3 };
static const int kGoalMax[DQ_COUNT] = { 8000, 2, 12, 40, 15, 15, 15, 6 };

static const char* kGoalEn[DQ_COUNT] = {
    "Collect %d gold", "Escape %d time(s)", "Reach floor %d", "Search %d containers",
    "Knock back %d times", "Hit with throwables %d times",
    "Survive %d minutes in one run", "Take no sanity loss for %d floors"
};
static const char* kGoalZh[DQ_COUNT] = {
    "收集 %d 金币", "撤离 %d 次", "到达第 %d 层", "打开 %d 个箱子",
    "击退 %d 次", "投掷命中 %d 次",
    "单局存活 %d 分钟", "连续 %d 层不掉理智"
};

static const char* kModEn[DM_COUNT] = {
    "", "Monsters 15% faster", "Battery drains 30% faster",
    "Gold +50%, but threat tier +2", "All noise -40%",
    "Second hunter arrives 10s earlier", "Crates give +80% gold, 30% fewer",
    "Battery cap -25%"
};
static const char* kModZh[DM_COUNT] = {
    "", "怪物速度 +15%", "耗电 +30%",
    "金币 +50%，但强度档 +2", "所有噪音 -40%",
    "第二只猎手提前 10 秒", "箱子金币 +80%，数量 -30%",
    "电池上限 -25%"
};

static const char* kResEn[DR_COUNT] = {
    "", "No sprinting", "No flashlight", "No hide spots",
    "No wrench", "Monsters hidden on map"
};
static const char* kResZh[DR_COUNT] = {
    "", "不能冲刺", "不能开手电", "不能使用藏身点",
    "不能使用扳手", "怪物不上小地图"
};

/* ---------------- 生成 ---------------- */

/* 抽一张；neutralPermille = 抽到"无修正"的概率（千分比） */
static int PickWeighted(unsigned int h, int count, int neutralIdx, int neutralPermille) {
    unsigned int r = h % 1000u;
    if ((int)r < neutralPermille) return neutralIdx;
    int pick = (int)((h >> 10) % (unsigned int)(count - 1));
    return pick >= neutralIdx ? pick + 1 : pick;
}

void DailyGenerateForDate(unsigned int seed) {
    unsigned int h1 = DailyHash(seed);
    unsigned int h2 = DailyHash(seed ^ 0xA5A5A5A5u);
    unsigned int h3 = DailyHash(seed ^ 0x5A5A5A5Au);

    if (s_daily.lastCompleted && seed != s_daily.lastCompleted) {
        int y=seed/10000, m=(seed/100)%100, d=seed%100;
        struct tm day = {0}; day.tm_year=y-1900; day.tm_mon=m-1; day.tm_mday=d-1; day.tm_isdst=-1;
        time_t yesterday=mktime(&day); struct tm* prev=localtime(&yesterday);
        unsigned int prevDate=prev ? (unsigned)((prev->tm_year+1900)*10000+(prev->tm_mon+1)*100+prev->tm_mday) : 0;
        if (s_daily.lastCompleted != prevDate) s_daily.streak=0;
    }
    memset(s_daily.samples,0,sizeof(s_daily.samples));
    s_daily.seed       = seed;
    s_daily.goalId     = (int)(h1 % DQ_COUNT);
    s_daily.modId      = PickWeighted(h2, DM_COUNT, DM_NORMAL, 300);
    s_daily.restrictId = PickWeighted(h3, DR_COUNT, DR_NONE, 400);

    {
        int lo = kGoalMin[s_daily.goalId];
        int hi = kGoalMax[s_daily.goalId];
        int span = hi - lo + 1;
        int v = lo + (int)((h1 >> 8) % (unsigned int)span);
        if (s_daily.goalId == DQ_COLLECT || s_daily.goalId == DQ_SEARCH ||
            s_daily.goalId == DQ_KNOCK || s_daily.goalId == DQ_THROW) v = ((v + 4) / 5) * 5;
        if (v > hi) v=hi;
        s_daily.goalParam = v;
    }

    s_daily.progress = 0;
    s_daily.done = 0;
    s_ready = 1;
}

/* ==================================================================
 * v7 第 ㊿ 轮：**每日任务进度存盘**
 * ==================================================================
 * 原来 s_daily 整个结构不进存档，开机首帧 !s_ready 就重新生成：
 *   · 今天做了一半的进度归零
 *   · 连击天数归零
 *   · 「连续 7 天 +5,000」在正常开机关机的节奏下**不可能达成**
 *
 * 现在 seed / goalId / goalParam 由日期决定（可重算），
 * 只有 progress / done / streak 需要存 —— 但为了稳，五个都存。
 * ================================================================ */
void DailyGenerate(void) { DailyGenerateForDate(DailySeedToday()); }
void DailyBeginRun(void) { memset(s_daily.samples, 0, sizeof(s_daily.samples)); }
void DailySaveTo(int* a) {
    if (!a) return;
    if (!s_ready) DailyGenerate();
    a[0]=s_daily.goalId; a[1]=s_daily.goalParam; a[2]=s_daily.progress;
    a[3]=s_daily.done; a[4]=s_daily.streak; a[5]=(int)s_daily.seed;
    a[6]=(int)s_daily.lastCompleted;
    for(int j=0;j<DQ_COUNT;++j) a[7+j]=s_daily.samples[j];
}
void DailyLoadFrom(const int* a) {
    if (!a) return;
    memset(&s_daily,0,sizeof(s_daily));
    s_daily.streak=a[4]>0 ? a[4] : 0;
    s_daily.lastCompleted=(unsigned)a[6];
    DailyGenerate();
    if ((unsigned)a[5]!=s_daily.seed) return;
    /* The date is authoritative, including quests from the v25 generator. */
    if (a[0]<0 || a[0]>=DQ_COUNT || a[1]<1 || a[1]>kGoalMax[a[0]]) return;
    s_daily.goalId=a[0]; s_daily.goalParam=a[1];
    s_daily.progress=a[2]>0 ? a[2] : 0;
    if(s_daily.progress>s_daily.goalParam) s_daily.progress=s_daily.goalParam;
    s_daily.done=a[3] && s_daily.progress==s_daily.goalParam;
    for(int j=0;j<DQ_COUNT;++j) s_daily.samples[j]=a[7+j]>0 ? a[7+j] : 0;
}
void DailyObserve(int goal, int value) {
    if (!s_ready) DailyGenerate();
    if (goal<0 || goal>=DQ_COUNT || trainingMode) return;
    if(value<0) value=0;
    int previous=s_daily.samples[goal]; s_daily.samples[goal]=value;
    if(goal!=s_daily.goalId || s_daily.done) return;
    if(goal==DQ_COLLECT || goal==DQ_ESCAPE || goal==DQ_SEARCH || goal==DQ_KNOCK || goal==DQ_THROW) {
        if(value>previous) DailySetProgress(s_daily.progress+value-previous);
    } else DailySetProgress(value);
}

void DailyTick(void) {
    if (!s_ready) { DailyGenerate(); return; }
    if (DailySeedToday() != s_daily.seed) {
        DailyGenerate();
        extern int totalGoldEarned,runSearched,knockCountRun,throwHitCountRun,dailyEscapeCount;
        s_daily.samples[DQ_COLLECT]=totalGoldEarned;s_daily.samples[DQ_SEARCH]=runSearched;
        s_daily.samples[DQ_KNOCK]=knockCountRun;s_daily.samples[DQ_THROW]=throwHitCountRun;s_daily.samples[DQ_ESCAPE]=dailyEscapeCount;
    }
}

/* ---------------- 查询 ---------------- */

int DailyGoalId(void)      { return s_daily.goalId; }
int DailyGoalParam(void)   { return s_daily.goalParam; }
int DailyModId(void)       { return s_daily.modId; }
int DailyRestrictId(void)  { return s_daily.restrictId; }
int DailyDone(void)        { return s_daily.done; }
int DailyStreak(void)      { return s_daily.streak; }

float DailyProgress(void) {
    if (s_daily.goalParam <= 0) return 0.0f;
    float f = (float)s_daily.progress / (float)s_daily.goalParam;
    if (f > 1.0f) f = 1.0f;
    return f;
}

/* v6 第 ㊶ 轮：HUD 要显示"3200/5000"，所以需要原始值 */
int DailyCurrent(void) { return s_daily.progress; }

void DailySetProgress(int v) {
    if (!s_ready) DailyGenerate();
    if (v > s_daily.goalParam) v=s_daily.goalParam;
    if (v > s_daily.progress) s_daily.progress = v;
    if (!s_daily.done && s_daily.progress >= s_daily.goalParam) {
        DailyMarkDone();
    }
}

void DailyMarkDone(void) {
    if (s_daily.done) return;
    s_daily.done = 1;
    s_daily.streak++;
    s_daily.lastCompleted = s_daily.seed;

    int reward = 800;
    if (s_daily.streak == 3) reward += 1500;
    if (s_daily.streak == 7) reward += 5000;

    WalletAddBank(reward);
    {
        extern const char* pickupMsg;
        extern float pickupMsgTimer;
        extern void PlaySfx(int, float);
        static char buf[80];
        snprintf(buf, sizeof(buf), "DAILY DONE  +%d GOLD", reward);
        pickupMsg = buf;
        pickupMsgTimer = 2.5f;
        PlaySfx(SFX_POWER, 1.0f);
    }
    { extern void SaveGame(void); SaveGame(); }
}

/* ---------------- 修正池效果 ---------------- */

float DailyMonsterSpeedMul(void) {
    if (!dailyMode) return 1.0f;
    switch (s_daily.modId) {
    case DM_FAST:  return 1.15f;
    case DM_HUNT:  return 1.00f;
    default:       return 1.00f;
    }
}
float DailyBatteryDrainMul(void) {
    if (!dailyMode) return 1.0f;
    switch (s_daily.modId) {
    case DM_DARK:  return 1.30f;
    default:       return 1.00f;
    }
}
float DailyGoldMul(void) {
    if (!dailyMode) return 1.0f;
    switch (s_daily.modId) {
    case DM_GREED: return 1.50f;
    case DM_RICH:  return 1.80f;
    default:       return 1.00f;
    }
}
int DailyGoldTierBonus(void) {
    return dailyMode && (s_daily.modId == DM_GREED) ? 2 : 0;
}
float DailyNoiseMul(void) {
    return (dailyMode && s_daily.modId == DM_SILENT) ? 0.60f : 1.00f;
}
float DailySecondSpawnOffset(void) {
    return (dailyMode && s_daily.modId == DM_HUNT) ? -10.0f : 0.0f;
}

/* ---------------- 限制池判定 ---------------- */

int DailyForbidSprint(void)     { return dailyMode && s_daily.restrictId == DR_NOSPRINT; }
int DailyForbidLight(void)      { return dailyMode && s_daily.restrictId == DR_NOLIGHT; }
int DailyForbidHide(void)       { return dailyMode && s_daily.restrictId == DR_NOHIDE; }
int DailyForbidWrench(void)     { return dailyMode && s_daily.restrictId == DR_NOWRENCH; }
int DailyForbidMonsterMap(void) { return dailyMode && s_daily.restrictId == DR_NOMAP; }

/* ---------------- 文案 ---------------- */

const char* DailyGoalTextEn(void) {
    static char b[96];
    snprintf(b, sizeof(b), kGoalEn[s_daily.goalId], s_daily.goalParam);
    return b;
}
const char* DailyGoalTextZh(void) {
    static char b[96];
    snprintf(b, sizeof(b), kGoalZh[s_daily.goalId], s_daily.goalParam);
    return b;
}
const char* DailyModTextEn(void)  { return kModEn[s_daily.modId]; }
const char* DailyModTextZh(void)  { return kModZh[s_daily.modId]; }
const char* DailyRestrictTextEn(void) { return kResEn[s_daily.restrictId]; }
const char* DailyRestrictTextZh(void) { return kResZh[s_daily.restrictId]; }

float DailyBatteryCapMul(void) { return dailyMode && s_daily.modId==DM_THIN ? 0.75f : 1.0f; }
float DailyCrateCountMul(void) { return dailyMode && s_daily.modId==DM_RICH ? 0.70f : 1.0f; }

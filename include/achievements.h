/*
 * Canity — 成就系统（v6 重建 · 表驱动）
 *
 * 改了什么
 * ────────
 * 原来：40 个成就，判定逻辑是 game.c 里 200 多行 if-else，
 *       名字和描述另外两个字符串数组 —— 加一个成就要改 4 个地方。
 *       而且至少 8 个成就引用的是已删除的概念（金币 / 遗物 / 24 层）。
 *
 * 现在：**32 个成就，一张表**。
 *       每个成就 = 一行定义 + 一个 kind（怎么判）+ 一个 target（判多少）。
 *       CheckAchievements 只做一件事：按 kind 读值、比较、解锁。
 *
 * 设计依据（Steam 成就解锁率的漏斗规律）
 *   · 第一个成就应该接近 100% 解锁率（新手引导）
 *   · 越往后越少，最后一个 < 1%
 *   · 所以 P1「第一次呼吸」= 进游戏就解锁
 */
#ifndef CANITY_ACHIEVEMENTS_H
#define CANITY_ACHIEVEMENTS_H

#include "common.h"

/* ---- 成就的判定方式 ---- */
typedef enum {
    AK_FLAG = 0,     /* game.c 直接置位（特殊条件） */
    AK_LEVEL,        /* 到达第 N 层 */
    AK_TOTALGOLD,    /* 累计获得 N 金币 */
    AK_CARRY,        /* 携带 N 金币 */
    AK_BANK,         /* 银行 N 金币 */
    AK_THROWN,       /* 累计投掷 N 次 */
    AK_HIDE,         /* 累计藏身 N 次 */
    AK_KNOCK,        /* 累计击退 N 次 */
    AK_THROWHIT,     /* 累计投掷命中 N 次 */
    AK_BESTIARY,     /* 见过 N 种怪 */
    AK_SKILLNODES,   /* 局外技能树点亮 N 个 */
    AK_THREAT,       /* 单局 THREAT 达到 N */
    AK_ECHOCELLS,    /* 回声弹累计揭示 N 格; stable legacy kind */
    AK_BESTIARYMASTER /* all current dossiers reach their counter stage */
} AchKind;

typedef enum {
    ACH_FLAG_STARTED = 1,
    ACH_FLAG_TEN_MINUTES = 2,
    ACH_FLAG_HALF_HOUR = 3,
    ACH_FLAG_CLAWBACK = 4,
    ACH_FLAG_CLEAN_ESCAPE = 5,
    ACH_FLAG_PERFECT_ESCAPE = 6,
    ACH_FLAG_WARDEN_CLEAN = 7,
    ACH_FLAG_NO_CONTAINER = 8,
    ACH_FLAG_LIGHTLESS = 9,
    ACH_FLAG_UNDER_WARDEN = 10,
    ACH_FLAG_DOUBLE_HUNT = 11,
    ACH_FLAG_GOLD_ON_FLOOR_12 = 12,
    ACH_FLAG_META_RESET = 13,
    ACH_FLAG_FULL_HIDE = 14,
    ACH_FLAG_FIRST_EXIT = 15,
    ACH_FLAG_COUNT = 16
} AchFlag;

#define ACH_GOLD_FLOOR_12 12000

/* ---- 成就分类 ---- */
enum {
    ACH_CAT_PROGRESS = 0,
    ACH_CAT_ECONOMY,
    ACH_CAT_SKILL,
    ACH_CAT_COLLECT,
    ACH_CAT_SECRET,
    ACH_CAT_COUNT
};

typedef struct {
    const char* nameEn;
    const char* nameZh;
    const char* descEn;
    const char* descZh;
    int     category;    /* ACH_CAT_* */
    int     reward;      /* 金币 */
    int     hidden;      /* 1 = 未解锁显示 ??? */
    AchKind kind;
    int     target;
    const char* steamId; /* Steam API Name，NULL = 不上报 */
} AchDef;

/* ---- 运行时状态（进存档） ---- */
typedef struct {
    int id;
    char name[ACH_NAME_MAX];
    char desc[ACH_DESC_MAX];
    int unlocked;
    int progress;
    int target;
} Achievement;

extern Achievement g_achievements[ACH_COUNT];

/* 2026-11-09：登记进内容中枢（幂等） */
void AchievementRegister(void);
extern const AchDef kAchDefs[];
extern int achSelection;
extern int achHovered;

/* ---- 给成就系统看的"世界快照"----
   game.c 每帧填一次，成就逻辑只读这个结构 ——
   这样 achievements.c 不依赖任何游戏全局。 */
typedef struct {
    int level;
    int gameWon;
    int totalGoldEarned;
    int carryGold;
    int bankGold;
    int totalThrown;
    int hideCount;
    int knockCount;
    int throwHitCount;
    int bestiarySeenMask;
    int skillNodesLit;
    int maxThreatThisRun;
    int echoCellsTotal;
    /* v6 第 40 轮：**原来是 flags[8]，但 AK_FLAG 的 target 用到 14**
       于是 target 2~7 走 flags[2..7]，target 10~14 又走 flags[2..6] ——
       两组成就读同一个槽：解了"十分钟"会连带解"守门人视野内撤离"。
       现在放宽到 16，target 直接当下标。 */
    int flags[ACH_FLAG_COUNT];
} AchSnap;

void InitAchievements(void);
void AchCheck(const AchSnap* s);

/* 手动解锁（特殊成就用） */
void AchUnlock(int idx);
void AchRecordFlag(AchFlag flag);

/* 进度累加（AK_* 之外的计数型） */
void AchAddProgress(int idx, int amount);

/* v6 成就墙分类筛选 */
extern int achFilter;                 /* 0..4 = 某类，5 = 全部 */
int  AchVisibleCount(void);
int  AchIdAtRow(int row);
void AchCycleFilter(int delta);
int  AchUnlockedInFilter(void);
const char* AchCatNameEn(int c);
const char* AchCatNameZh(int c);

#endif /* CANITY_ACHIEVEMENTS_H */

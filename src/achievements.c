/*
 * Canity — 成就系统（v6 重建）
 *
 * 32 个成就，一张表。加成就 = 加一行。
 */
#include "common.h"
#include "achievements.h"
#include "wallet.h"
#include "audio.h"
#include "music.h"

Achievement g_achievements[ACH_COUNT];
int achSelection = 0;
int achHovered = -1;

/* ==================================================================
 * v6 **成就墙分类筛选**
 * ==================================================================
 * 32 个成就平铺成一列，玩家找不到"我还能做什么"。
 * 按分类筛后每类只有 4~8 个，一屏能看完。
 *
 * achFilter: 0..4 = 只看该类，5 = 全部
 * ================================================================== */
int achFilter = 5;   /* 默认全部 */

const char* AchCatNameEn(int c) {
    switch (c) {
    case ACH_CAT_PROGRESS: return "PROGRESS";
    case ACH_CAT_ECONOMY:  return "ECONOMY";
    case ACH_CAT_SKILL:    return "SKILL";
    case ACH_CAT_COLLECT:  return "COLLECT";
    case ACH_CAT_SECRET:   return "SECRET";
    default:               return "ALL";
    }
}
const char* AchCatNameZh(int c) {
    switch (c) {
    case ACH_CAT_PROGRESS: return "进程";
    case ACH_CAT_ECONOMY:  return "经济";
    case ACH_CAT_SKILL:    return "技巧";
    case ACH_CAT_COLLECT:  return "收藏";
    case ACH_CAT_SECRET:   return "隐藏";
    default:               return "全部";
    }
}

/* 当前筛选下有多少行 */
int AchVisibleCount(void) {
    if (achFilter >= ACH_CAT_COUNT) return ACH_COUNT;
    int n = 0;
    for (int i = 0; i < ACH_COUNT; i++)
        if (kAchDefs[i].category == achFilter) n++;
    return n;
}

/* 第 row 行对应哪个成就 id（-1 = 越界） */
int AchIdAtRow(int row) {
    if (row < 0) return -1;
    if (achFilter >= ACH_CAT_COUNT) {
        return (row < ACH_COUNT) ? row : -1;
    }
    int n = 0;
    for (int i = 0; i < ACH_COUNT; i++) {
        if (kAchDefs[i].category != achFilter) continue;
        if (n == row) return i;
        n++;
    }
    return -1;
}

/* 切分类：TAB / Q / E */
void AchCycleFilter(int delta) {
    achFilter += delta;
    if (achFilter < 0) achFilter = ACH_CAT_COUNT;
    if (achFilter > ACH_CAT_COUNT) achFilter = 0;
    achSelection = 0;
}

/* 当前筛选下已解锁几个 */
int AchUnlockedInFilter(void) {
    int n = 0;
    for (int i = 0; i < ACH_COUNT; i++) {
        if (achFilter < ACH_CAT_COUNT && kAchDefs[i].category != achFilter) continue;
        if (g_achievements[i].unlocked) n++;
    }
    return n;
}

/* ==================================================================
 * 32 个成就
 *   P 进程 8 / E 经济 6 / K 技巧 8 / C 收藏 4 / S 隐藏 6
 * ================================================================== */
#define ACH_DEF(en,zh,de,dz,cat,reward,hidden,kind,target,steam) {en,zh,de,dz,cat,reward,hidden,kind,target,steam},
const AchDef kAchDefs[] = {
#include "achievement_defs.inc"
};
#undef ACH_DEF

typedef char kAchDefs_count_check[(sizeof(kAchDefs)/sizeof(kAchDefs[0]) == ACH_COUNT) ? 1 : -1];

/* ==================================================================
 * 初始化 / 解锁 / 进度
 * ================================================================== */

void InitAchievements(void) {
    for (int i = 0; i < ACH_COUNT; i++) {
        g_achievements[i].id = i;
        snprintf(g_achievements[i].name, ACH_NAME_MAX, "%s", kAchDefs[i].nameEn);
        snprintf(g_achievements[i].desc, ACH_DESC_MAX, "%s", kAchDefs[i].descEn);
        g_achievements[i].target = kAchDefs[i].target;
    }
}

void AchUnlock(int idx) {
    if (idx < 0 || idx >= ACH_COUNT) return;
    if (g_achievements[idx].unlocked) return;

    g_achievements[idx].unlocked = 1;
    g_achievements[idx].progress = g_achievements[idx].target;

    /* 奖励进银行（成就属于"跨局"的东西） */
    WalletAddBank(kAchDefs[idx].reward);
    MusicNotify(MUSIC_EVENT_ACHIEVEMENT);

    {
        extern const char* pickupMsg;
        extern float pickupMsgTimer;
        extern void PlaySfx(int, float);
        extern int uiDirty;
        static char abuf[72];
        snprintf(abuf, sizeof(abuf), "+%d GOLD  %s",
                 kAchDefs[idx].reward, kAchDefs[idx].nameEn);
        pickupMsg = abuf;
        pickupMsgTimer = 2.2f;
        PlaySfx(SFX_POWER, 0.9f);
        uiDirty = 1;
    }

    /* Steam 上报（没连接时是空操作） */
    if (kAchDefs[idx].steamId) {
        extern int SteamZoneUnlockAchievement(const char*);
        SteamZoneUnlockAchievement(kAchDefs[idx].steamId);
    }

    {
        extern void SaveGame(void);
        SaveGame();
    }
}

void AchAddProgress(int idx, int amount) {
    if (idx < 0 || idx >= ACH_COUNT) return;
    if (g_achievements[idx].unlocked) return;
    g_achievements[idx].progress += amount;
    if (g_achievements[idx].progress >= g_achievements[idx].target) {
        AchUnlock(idx);
    }
}

/* ==================================================================
 * 每帧检查：按 kind 读快照里的值
 * ================================================================== */

/* 把某个计数值拿来比 target */
static void checkValue(int idx, int value, int target) {
    if (g_achievements[idx].unlocked) return;
    if (value > g_achievements[idx].progress) {
        g_achievements[idx].progress = value;
    }
    if (value >= target) AchUnlock(idx);
}

/* 位掩码里数 1 的个数 */
static int popcount6(int m) {
    int c = 0;
    for (int i = 0; i < 8; i++) if ((m >> i) & 1) c++;
    return c;
}

void AchCheck(const AchSnap* s) {
    if (!s) return;

    for (int i = 0; i < ACH_COUNT; i++) {
        if (g_achievements[i].unlocked) continue;
        const AchDef* d = &kAchDefs[i];

        switch (d->kind) {
        case AK_FLAG:
            /* v6 第 40 轮：**target 直接当下标**
               原来分了两段（1~7 和 8~14 各减 8），结果两组撞在同一个槽。
               现在 flags[16]，target 1..14 一一对应。 */
            if (d->target >= 0 && d->target < 16 && s->flags[d->target]) AchUnlock(i);
            break;
        case AK_LEVEL:      checkValue(i, s->level,            d->target); break;
        case AK_TOTALGOLD:  checkValue(i, s->totalGoldEarned,  d->target); break;
        case AK_CARRY:      checkValue(i, s->carryGold,        d->target); break;
        case AK_BANK:       checkValue(i, s->bankGold,         d->target); break;
        case AK_THROWN:     checkValue(i, s->totalThrown,      d->target); break;
        case AK_HIDE:       checkValue(i, s->hideCount,        d->target); break;
        case AK_KNOCK:      checkValue(i, s->knockCount,       d->target); break;
        case AK_THROWHIT:   checkValue(i, s->throwHitCount,    d->target); break;
        case AK_BESTIARY:   checkValue(i, popcount6(s->bestiarySeenMask), d->target); break;
        case AK_SKILLNODES: checkValue(i, s->skillNodesLit,    d->target); break;
        case AK_THREAT:     checkValue(i, s->maxThreatThisRun, d->target); break;
        case AK_ECHOCELLS:  checkValue(i, s->echoCellsTotal,   d->target); break;
        }
    }
}

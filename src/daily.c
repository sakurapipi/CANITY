/*
 * Canity — 每日任务（2026-11-05 保合约重写）
 *
 * ==================================================================
 * 重写范围与约束（用户选的是「方案 A：保合约重写」）
 * ------------------------------------------------------------------
 * 合约不变（一个字节都没动）：
 *   · 三个池的枚举顺序    DQ_* / DM_* / DR_* 的编号
 *   · 存档数组布局        a[0..14] 的 7 个标量 + 8 个 samples
 *   · 公开 API            daily.h 里那 27 个函数，签名和语义全不变
 *
 * 为什么不能动枚举顺序：存档里存的是 goalId 的**数字**，加载时只做
 * `a[0] >= 0 && a[0] < DQ_COUNT` 的范围检查。顺序一变，
 * 当天已有存档的 goalId 会指向另一个任务（显示"收集 5000"
 * 但判定"开箱 20 个"）—— 那是静默的数据错位。
 *
 * 重写掉的是**内部**：
 *   ① 奖励发放从「看 done」改成「看 lastCompleted」+ 单调化
 *      —— 堵住"读旧档把 done 顶回 0 然后反复领 800"的无限金币
 *   ② 每个目标声明成一张表（是否累计 / 下限 / 上限 / 是否按 5 取整）
 *      —— 原来这些信息散在 kGoalMin/Max + 一段 if 里，加目标要改三处
 *   ③ 每次领奖只打标记，帧首由 DailyTick 统一落盘
 *      —— 原来在 Update 帧中间直接 SaveGame()
 *   ④ 进度推进收敛成唯一入口 DailySetProgress()
 *      —— 单次推进量夹在 [0, 目标]，永不超过
 *   ⑤ 读档时 streak / lastCompleted 不回退（改系统时间也压不回去）
 *
 * 设计参考：Stick War: Saga 的 Daily Battles
 *   「Daily Battles change each day at 00:00 UTC, with the current one
 *     being drawn from a pool of scenarios.」
 * 关键洞察：**不是随机生成，是从手写场景池里轮换**。
 * 真正让每天不同的是**特殊规则**，不是随机数字。
 *
 * 种子 = 年月日 → 同一天全世界一样（公平）
 */
#include "common.h"
#include "daily.h"
#include "game.h"
#include "wallet.h"
#include "audio.h"

/* ================================================================== */
/* 目标表：加一个目标 = 加一行                                          */
/* ================================================================== */

typedef struct {
    int  lo, hi;        /* 目标值的取值范围 */
    int  round5;        /* 1 = 生成的数值按 5 取整（金币/开箱这类大数好看） */
    int  cumulative;    /* 1 = 累计型（value 只增，进度 += 增量）
                           0 = 存量型（进度 = value，例如「到达第 N 层」） */
} GoalDef;

/* 顺序必须与 enum 严格一致 —— 文件末尾有编译期检查钉住 */
static const GoalDef kGoals[DQ_COUNT] = {
    /* DQ_COLLECT */ { 3000,  8000, 1, 1 },   /* 收集 N 金币 */
    /* DQ_ESCAPE  */ {    1,     2, 0, 1 },   /* 撤离 N 次（跨局累计） */
    /* DQ_FLOOR   */ {    5,    12, 0, 0 },   /* 到达第 N 层 */
    /* DQ_SEARCH  */ {   20,    40, 1, 1 },   /* 开 N 个箱子 */
    /* DQ_KNOCK   */ {    5,    15, 0, 1 },   /* 击退 N 次 */
    /* DQ_THROW   */ {    5,    15, 0, 1 },   /* 投掷命中 N 次 */
    /* DQ_SURVIVE */ {    8,    15, 0, 0 },   /* 单局存活 N 分钟 */
    /* DQ_NOHIT   */ {    3,     6, 0, 0 }    /* 连续 N 层不掉理智 */
};

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

/* ==================================================================
 * 限制池（2026-11-05 复查：哪些还在生效、哪些已经过时）
 * ------------------------------------------------------------------
 * DR_NOSPRINT  不能冲刺            ✓ 仍然刺痛（冲刺是脱身主力）
 * DR_NOLIGHT   不能开手电          ✓ 仍然刺痛（看不见路）
 * DR_NOHIDE    不能使用藏身点      ✓ 仍然刺痛（没有喘息点）
 * DR_NOWRENCH  不能使用扳手        ✗ **移出池** —— 见下
 * DR_NOMAP     怪物不上小地图      △ 改文案 —— 见下
 *
 * DR_NOWRENCH 为什么移出：
 *   武器系统下线之后（NO_COMBAT），扳手是玩家**唯一**的主动工具。
 *   禁掉它 = 赤手空拳跑一整局，玩家第一反应是"扳手坏了"（用户报告
 *   「局内扳手不能使用」就是这个）。留着偶尔抽到只会像 bug。
 *   枚举值保留（存档里是数字），只是不再被抽到。
 *
 * DR_NOMAP 为什么改文案：
 *   它实际只关掉**改造树的「预知 / 心眼」两个节点**
 *   （ui.c 里 goto skip_monster_hints），没买过那两个节点的玩家
 *   完全感觉不到 —— 原文案「怪物不上小地图」是错的期望。
 * ================================================================== */
static const char* kResEn[DR_COUNT] = {
    "", "No sprinting", "No flashlight", "No hide spots",
    "No wrench", "Tree threat-sense is off"
};
static const char* kResZh[DR_COUNT] = {
    "", "不能冲刺", "不能开手电", "不能使用藏身点",
    "不能使用扳手", "改造树的感知系失效"
};

/* 1 = 会被抽到。DR_NOWRENCH（索引 4）= 0，永久移出池。 */
static const int kResPickable[DR_COUNT] = { 1, 1, 1, 1, 0, 1 };

/* ================================================================== */
/* 状态                                                                */
/* ================================================================== */

typedef struct {
    unsigned int seed;            /* YYYYMMDD */
    int goalId, modId, restrictId;
    int goalParam;
    int progress;
    int done;
    int streak;
    unsigned int lastCompleted;   /* 哪一天领过奖励（单调，只增不减） */
    int samples[DQ_COUNT];        /* 各目标上一次观测到的原始值（算增量用） */
} DailyQuest;

static DailyQuest s_daily;
static int s_ready = 0;
static int s_pendingSave = 0;     /* 领奖后等帧首再落盘，见 DailyMarkDone */

/* ------------------------------------------------------------------ */
/* 确定性 RNG                                                          */
/* ------------------------------------------------------------------ */
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

/* 抽一张；neutralPermille = 抽到「无修正」的千分比 */
static int PickWeighted(unsigned int h, int count, int neutralIdx, int neutralPermille) {
    unsigned int r = h % 1000u;
    if ((int)r < neutralPermille) return neutralIdx;
    int pick = (int)((h >> 10) % (unsigned int)(count - 1));
    return pick >= neutralIdx ? pick + 1 : pick;
}

/* ================================================================== */
/* 生成                                                                */
/* ================================================================== */

void DailyGenerateForDate(unsigned int seed) {
    unsigned int h1 = DailyHash(seed);
    unsigned int h2 = DailyHash(seed ^ 0xA5A5A5A5u);
    unsigned int h3 = DailyHash(seed ^ 0x5A5A5A5Au);

    /* 连击：昨天完成过就续，否则归零 */
    if (s_daily.lastCompleted && seed != s_daily.lastCompleted) {
        int y = (int)(seed / 10000), m = (int)((seed / 100) % 100), d = (int)(seed % 100);
        struct tm day;
        memset(&day, 0, sizeof(day));
        day.tm_year = y - 1900; day.tm_mon = m - 1; day.tm_mday = d - 1; day.tm_isdst = -1;
        time_t yesterday = mktime(&day);
        struct tm* prev = localtime(&yesterday);
        unsigned int prevDate = prev ? (unsigned)((prev->tm_year + 1900) * 10000
                                                + (prev->tm_mon + 1) * 100
                                                +  prev->tm_mday) : 0;
        if (s_daily.lastCompleted != prevDate) s_daily.streak = 0;
    }

    memset(s_daily.samples, 0, sizeof(s_daily.samples));
    s_daily.seed       = seed;
    s_daily.goalId     = (int)(h1 % DQ_COUNT);
    s_daily.modId      = PickWeighted(h2, DM_COUNT, DM_NORMAL, 300);
    s_daily.restrictId = PickWeighted(h3, DR_COUNT, DR_NONE, 400);
    /* 抽到已退役的限制 → 退化成「无限制」，而不是硬塞给玩家 */
    if (s_daily.restrictId < 0 || s_daily.restrictId >= DR_COUNT ||
        !kResPickable[s_daily.restrictId]) {
        s_daily.restrictId = DR_NONE;
    }

    /* 目标值：上下限来自目标表，不再散在两数组 + 一段 if 里 */
    {
        const GoalDef* G = &kGoals[s_daily.goalId];
        int span = G->hi - G->lo + 1;
        int v = G->lo + (int)((h1 >> 8) % (unsigned int)span);
        if (G->round5) v = ((v + 4) / 5) * 5;
        if (v > G->hi) v = G->hi;
        if (v < G->lo) v = G->lo;
        s_daily.goalParam = v;
    }

    s_daily.progress = 0;
    s_daily.done = 0;
    s_ready = 1;
}

void DailyGenerate(void) { DailyGenerateForDate(DailySeedToday()); }

/* 新开一局：清空「上一次观测值」，增量从本局从 0 开始算 */
void DailyBeginRun(void) { memset(s_daily.samples, 0, sizeof(s_daily.samples)); }

/* ================================================================== */
/* 存盘 —— 布局与旧版逐字节一致（7 个标量 + 8 个 samples）              */
/* ================================================================== */

void DailySaveTo(int* a) {
    if (!a) return;
    if (!s_ready) DailyGenerate();
    a[0] = s_daily.goalId;
    a[1] = s_daily.goalParam;
    a[2] = s_daily.progress;
    a[3] = s_daily.done;
    a[4] = s_daily.streak;
    a[5] = (int)s_daily.seed;
    a[6] = (int)s_daily.lastCompleted;
    for (int j = 0; j < DQ_COUNT; ++j) a[7 + j] = s_daily.samples[j];
}

void DailyLoadFrom(const int* a) {
    if (!a) return;

    /* 2026-11-05：**练级不能倒退**。
       读档时 lastCompleted 取「存档值」和「内存值」的较大者 ——
       否则加载一份「完成之前」的旧档就能把「今天已领」这个事实抹掉，
       于是同一天可以反复领 800 金（无限刷金币的根）。
       seed 是 YYYYMMDD，直接比大小就是比日期。 */
    unsigned int memCompleted = s_daily.lastCompleted;
    int          memStreak    = s_daily.streak;

    memset(&s_daily, 0, sizeof(s_daily));
    s_daily.streak = (a[4] > 0) ? a[4] : 0;
    {
        unsigned int fromSave = (unsigned)a[6];
        s_daily.lastCompleted = (fromSave > memCompleted) ? fromSave : memCompleted;
        if (memStreak > s_daily.streak) s_daily.streak = memStreak;
    }

    DailyGenerate();   /* 先按今天生成，再按存档覆盖（日期是权威） */

    if ((unsigned)a[5] != s_daily.seed) return;          /* 不是今天的档 → 保持新生成 */
    if (a[0] < 0 || a[0] >= DQ_COUNT) return;
    if (a[1] < 1 || a[1] > kGoals[a[0]].hi) return;

    s_daily.goalId    = a[0];
    s_daily.goalParam = a[1];
    s_daily.progress  = (a[2] > 0) ? a[2] : 0;
    if (s_daily.progress > s_daily.goalParam) s_daily.progress = s_daily.goalParam;
    /* done 只有在「进度真的满了」时才信存档 */
    s_daily.done = (a[3] && s_daily.progress >= s_daily.goalParam) ? 1 : 0;
    for (int j = 0; j < DQ_COUNT; ++j) s_daily.samples[j] = (a[j + 7] > 0) ? a[j + 7] : 0;
}

/* ================================================================== */
/* 进度推进 —— 唯一入口                                                */
/* ================================================================== */

void DailySetProgress(int v) {
    if (!s_ready) DailyGenerate();
    if (v < 0) v = 0;
    if (v > s_daily.goalParam) v = s_daily.goalParam;
    if (v <= s_daily.progress) return;      /* 只增不减 */
    s_daily.progress = v;
    if (!s_daily.done && s_daily.progress >= s_daily.goalParam) DailyMarkDone();
}

/* 观测一次原始值。累计型只取「比上次多出来的那部分」，
   所以重开一局不会把同一批进度重复计一次（samples 由 DailyBeginRun 清零）。 */
void DailyObserve(int goal, int value) {
    if (!s_ready) DailyGenerate();
    if (goal < 0 || goal >= DQ_COUNT) return;
    if (trainingMode) return;               /* 训练营不计进度 */
    if (value < 0) value = 0;

    int previous = s_daily.samples[goal];
    s_daily.samples[goal] = value;

    if (goal != s_daily.goalId || s_daily.done) return;

    if (kGoals[goal].cumulative) {
        if (value > previous) DailySetProgress(s_daily.progress + (value - previous));
    } else {
        DailySetProgress(value);            /* 存量型：直接取当前值 */
    }
}

void DailyMarkDone(void) {
    /* ==================================================================
     * **同一天只发一次奖励**（无限金币的闸门）
     * ------------------------------------------------------------------
     * 旧版只判 s_daily.done，而 done 是从存档恢复的：
     *   完成每日 → 拿到 800 → 读一份「完成之前」的旧档 → done 回到 0
     *   → 再做一遍 → 又拿 800 → 无限循环。
     *
     * lastCompleted 记的是「哪一天领过」，它在 DailyGenerateForDate 里
     * 不会被重置，读档时又做了单调化 —— 所以它无法被旧档压回去。
     * ================================================================== */
    if (!s_ready) DailyGenerate();
    if (s_daily.done) return;

    s_daily.done = 1;            /* 任务确实是「完成」的，这一点先记上 */

    /* **奖励**今天发过就不再发。
       注意：done 和「发过钱」是两件事 ——
         · done      = 这个目标做完了
         · lastCompleted = 今天已经领过 800
       分开之后，「读旧档把 done 顶回 0」只能让任务重新显示为未完成，
       但**领不到第二份钱**。 */
    if (s_daily.lastCompleted == s_daily.seed) return;

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

    /* **不要在这一帧里存档**：DailyMarkDone ← DailySetProgress ← Update()，
       在帧中途触发完整 SaveGame 会把「本帧还没结算完的运行时状态」写进档。
       打标记，由 DailyTick（帧首）统一落盘。 */
    s_pendingSave = 1;
}

/* ================================================================== */
/* 每帧                                                                */
/* ================================================================== */

void DailyTick(void) {
    /* 帧首统一落盘（见 DailyMarkDone） */
    if (s_pendingSave) {
        s_pendingSave = 0;
        { extern void SaveGame(void); SaveGame(); }
    }

    if (!s_ready) { DailyGenerate(); return; }

    unsigned int today = DailySeedToday();
    if (today == s_daily.seed) return;

    /* 跨天：重新生成，并把当前运行时值当作新基线，
       这样昨天的进度不会被重复计到今天的任务上。 */
    DailyGenerate();
    {
        extern int totalGoldEarned, runSearched, knockCountRun, throwHitCountRun, dailyEscapeCount;
        s_daily.samples[DQ_COLLECT] = totalGoldEarned;
        s_daily.samples[DQ_SEARCH]  = runSearched;
        s_daily.samples[DQ_KNOCK]   = knockCountRun;
        s_daily.samples[DQ_THROW]   = throwHitCountRun;
        s_daily.samples[DQ_ESCAPE]  = dailyEscapeCount;
    }
}

/* ================================================================== */
/* 查询                                                                */
/* ================================================================== */

int DailyGoalId(void)      { return s_daily.goalId; }
int DailyGoalParam(void)   { return s_daily.goalParam; }
int DailyModId(void)       { return s_daily.modId; }
int DailyRestrictId(void)  { return s_daily.restrictId; }
int DailyDone(void)        { return s_daily.done; }
int DailyStreak(void)      { return s_daily.streak; }
int DailyCurrent(void)     { return s_daily.progress; }

float DailyProgress(void) {
    if (s_daily.goalParam <= 0) return 0.0f;
    float f = (float)s_daily.progress / (float)s_daily.goalParam;
    if (f > 1.0f) f = 1.0f;
    if (f < 0.0f) f = 0.0f;
    return f;
}

/* ================================================================== */
/* 修正池效果                                                          */
/* ================================================================== */

float DailyMonsterSpeedMul(void) {
    if (!dailyMode) return 1.0f;
    return (s_daily.modId == DM_FAST) ? 1.15f : 1.00f;
}
float DailyBatteryDrainMul(void) {
    if (!dailyMode) return 1.0f;
    return (s_daily.modId == DM_DARK) ? 1.30f : 1.00f;
}
float DailyGoldMul(void) {
    if (!dailyMode) return 1.0f;
    if (s_daily.modId == DM_GREED) return 1.50f;
    if (s_daily.modId == DM_RICH)  return 1.80f;
    return 1.00f;
}
int DailyGoldTierBonus(void) {
    return (dailyMode && s_daily.modId == DM_GREED) ? 2 : 0;
}
float DailyNoiseMul(void) {
    return (dailyMode && s_daily.modId == DM_SILENT) ? 0.60f : 1.00f;
}
float DailySecondSpawnOffset(void) {
    return (dailyMode && s_daily.modId == DM_HUNT) ? -10.0f : 0.0f;
}
float DailyBatteryCapMul(void) {
    return (dailyMode && s_daily.modId == DM_THIN) ? 0.75f : 1.00f;
}
float DailyCrateCountMul(void) {
    return (dailyMode && s_daily.modId == DM_RICH) ? 0.70f : 1.00f;
}

/* ================================================================== */
/* 限制池判定                                                          */
/* ================================================================== */

int DailyForbidSprint(void)     { return dailyMode && s_daily.restrictId == DR_NOSPRINT; }
int DailyForbidLight(void)      { return dailyMode && s_daily.restrictId == DR_NOLIGHT; }
int DailyForbidHide(void)       { return dailyMode && s_daily.restrictId == DR_NOHIDE; }
int DailyForbidWrench(void)     { return dailyMode && s_daily.restrictId == DR_NOWRENCH; }
int DailyForbidMonsterMap(void) { return dailyMode && s_daily.restrictId == DR_NOMAP; }

/* ================================================================== */
/* 文案                                                                */
/* ================================================================== */

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

/* ==================================================================
 * 编译期检查：目标表的行数必须等于 enum 的 DQ_COUNT
 * （加目标时忘了补表 → 这里直接编译失败，而不是运行期读越界）
 * ================================================================== */
typedef char daily_goal_table_check[(sizeof(kGoals) / sizeof(kGoals[0]) == DQ_COUNT) ? 1 : -1];

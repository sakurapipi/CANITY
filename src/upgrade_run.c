/*
 * Canity — 局内强化实现（v3.0）
 */
#include "common.h"
#include "upgrade_run.h"
#include "game.h"
#include "lang.h"
#include "audio.h"
#include "bench.h"

static int upg[RUN_UPG_COUNT];

void RunUpgReset(void) {
    for (int i = 0; i < RUN_UPG_COUNT; i++) upg[i] = 0;
}

int RunUpgLevel(int kind) {
    if (kind < 0 || kind >= RUN_UPG_COUNT) return 0;
    return upg[kind];
}

int RunUpgCost(int kind) {
    if (kind < 0 || kind >= RUN_UPG_COUNT) return -1;
    if (upg[kind] >= RUN_UPG_MAX) return -1;
    /* 60 / 130 / 220 —— 一局大约能满 1~2 条线 */
    static const int costs[RUN_UPG_MAX]  = { 60, 130, 220 };
    /* v7：扳手是「拿到就能用」的能力升级，价格单独一档 */
    static const int wrench[RUN_UPG_MAX] = { 300, 800, 1800 };
    if (kind == 4) return wrench[upg[kind]];
    return costs[upg[kind]];
}

/* 2026-11-08（第 1 轮）：测试钩子 —— 直接把某条升级设到指定等级。
   生产路径不调用；测试要验"低级扳手也该有硬直"必须有它。 */
void RunUpgSetLevel(int kind, int lv) {
    if (kind < 0 || kind >= RUN_UPG_COUNT) return;
    if (lv < 0) lv = 0;
    if (lv > RUN_UPG_MAX) lv = RUN_UPG_MAX;
    upg[kind] = lv;
}

int RunUpgTryBuy(int kind) {
    if (kind < 0 || kind >= RUN_UPG_COUNT) return 1;
    if (upg[kind] >= RUN_UPG_MAX) return 2;
    int cost = RunUpgCost(kind);
    if (cost < 0) return 2;
    if (carryGold < cost) return 4;
    { extern int WalletSpendRun(int); WalletSpendRun(cost); }
    upg[kind]++;
    PlaySfx(SFX_POWER, 0.9f);
    return 0;
}

const char* RunUpgName(int kind) {
    switch (kind) {
    case 0: return "CARRY";
    case 1: return "RESISTANCE";
    case 2: return "STAMINA";
    case 3: return "FLASHLIGHT";
    case 4: return "WRENCH KNOCK";
    default: return "MINIMAP";
    }
}

const char* RunUpgNameZh(int kind) {
    switch (kind) {
    case 0: return "携带量";
    case 1: return "角色抗性";
    case 2: return "体能";
    case 3: return "手电效率";
    case 4: return "扳手击退";
    default: return "小地图省电";
    }
}

const char* RunUpgDesc(int kind) {
    switch (kind) {
    case 0: return "Throwable capacity +2 / lv";
    case 1: return "Lose 10% less sanity when grabbed";
    case 2: return "Sprint stamina -15% / lv";
    case 3: return "Flashlight drain -15% / lv";
    case 4: return "Push monsters back. Lv2+ becomes cooldown.";
    default: return "Minimap drain -2% / lv (10 to 4 per second)";
    }
}

const char* RunUpgDescZh(int kind) {
    switch (kind) {
    case 0: return "投掷物携带上限 +2 / 级";
    case 1: return "被抓时少掉 10% 理智 / 级";
    case 2: return "冲刺体力消耗 -15% / 级";
    case 3: return "手电耗电 -15% / 级";
    case 4: return "推开怪物。2 级起改为冷却制。";
    default: return "小地图每秒耗电 -2%（10 → 4）";
    }
}

/* ---- 效果 ---- */
/* v3.0：武器取消 → 这一条改成携带量
   v3.3：**真正接到全部投掷物上**，不再只作用于瓶子。
   审计报告：「CARRY 只进 BottleCap（只管瓶子），文案写投掷物上限」。 */
float RunUpgDamageMul(void)   { return 1.0f; }
int   RunUpgCarryBonus(void)  { return 2 * upg[0]; }
float RunUpgResistMul(void)   { return 1.0f - 0.10f * (float)upg[1]; }
float RunUpgStaminaMul(void)  { return 1.0f - 0.15f * (float)upg[2]; }
float RunUpgFlashMul(void)    { return 1.0f - 0.15f * (float)upg[3]; }

/* ===== 2026-11-05 用户要求：小地图省电 =====
   基准每秒 10%，每级 -2 个百分点，3 级封顶 → 10 / 8 / 6 / 4。
   **大地图不耗电**（见 game.c：全屏地图暂停且不扣电），
   所以这条只作用于小窗地图。 */
int RunUpgMapDrainLevel(void) {
    int lv = upg[5];
    if (lv < 0) lv = 0;
    if (lv > RUN_UPG_MAX) lv = RUN_UPG_MAX;
    return lv;
}
float RunUpgMapDrainPct(void) {
    float v = MAP_BATTERY_DRAIN - 2.0f * (float)RunUpgMapDrainLevel();
    if (v < 1.0f) v = 1.0f;
    return v;
}

/* 存档只给 5 个 int（前缀兼容不能改结构），第 6 项单独读 */
void RunUpgLoadExtra(int minimapLevel) {
    int v = minimapLevel;
    if (v < 0) v = 0;
    if (v > RUN_UPG_MAX) v = RUN_UPG_MAX;
    upg[5] = v;
}

/* ===== 扳手击退（2026-11-06 改为纯冷却）=====
   0 级（默认）  冷却 8 秒      推开 2 格    无硬直
   1 级          冷却 7 秒      推开 3 格    无硬直
   2 级          冷却 6 秒      推开 3 格    无硬直
   3 级          冷却 5 秒      推开 4 格    硬直 1 秒 */
int RunUpgWrenchLevel(void) {
    int lv = upg[4];
    if (lv < 0) lv = 0;
    if (lv > 3) lv = 3;
    return lv;
}
/* ==================================================================
 * 2026-11-06（用户要求）：**扳手取消次数限制，改成纯冷却**
 * ------------------------------------------------------------------
 * 用户原话：「扳手取消使用限制，改为 8 秒使用一次，
 *           升级的属性改为每升 1 级减 1 秒，冷却上限为 5 秒。」
 *
 * 旧行为：0~1 级是"每层 N 次"（次数制），2 级 6 秒、3 级 3 秒。
 * 新行为：**永远走冷却制**，不返回 -1（-1 会让调用方切到次数制）。
 *   0 级 8 秒 → 1 级 7 → 2 级 6 → 3 级 5
 *   **下限 5 秒**，再升也不会更短。
 *
 * 为什么这样更好：
 *   次数制让玩家"舍不得用"，攒到 Boss 层；
 *   冷却制鼓励把它当常规脱身手段，节奏更连续。
 * ================================================================ */
float RunUpgWrenchCooldown(void) {
    int lv = RunUpgWrenchLevel();
    float cd = 8.0f - (float)lv;    /* 每级 -1 秒 */
    if (cd < 5.0f) cd = 5.0f;       /* 冷却上限（最短）5 秒 */
    return cd;
}
int RunUpgWrenchCharges(void) {
    return ((RunUpgWrenchLevel() <= 0) ? 2 : 4)+BenchKnockCharges();
}
float RunUpgWrenchRange(void) {
    int lv = RunUpgWrenchLevel();
    return (lv >= 3) ? 4.0f : (lv >= 1 ? 3.0f : 2.0f);
}
float RunUpgWrenchStun(void) {
    /* ==================================================================
     * 2026-11-08（第 1 轮）：**低级也要有硬直**
     * ------------------------------------------------------------------
     * 原来 level < 3 一律返回 0 —— 升级前砸中怪只有击退、零硬直，
     * 怪立刻又贴上来。玩家原话：「扳手没啥效果」。
     * 现在给一条下限曲线，升级仍然明显（0.25 → 1.0）。
     * ================================================================ */
    int lv = RunUpgWrenchLevel();
    if (lv <= 0) return 0.25f;
    if (lv == 1) return 0.45f;
    if (lv == 2) return 0.70f;
    return 1.0f;
}

void RunUpgLoadFromSave(const int* arr) {
    /* 存档里只有 5 个 int（runUpgSave[5]），第 6 项由 RunUpgLoadExtra 单独读，
       所以这里只回填前 5 条 —— 用 RUN_UPG_COUNT 会越界读。 */
    for (int i = 0; i < RUN_UPG_COUNT && i < 5; i++) {
        int v = arr ? arr[i] : 0;
        if (v < 0) v = 0;
        if (v > RUN_UPG_MAX) v = RUN_UPG_MAX;
        upg[i] = v;
    }
}

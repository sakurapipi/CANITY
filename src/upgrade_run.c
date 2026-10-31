/*
 * Canity — 局内强化实现（v3.0）
 */
#include "common.h"
#include "upgrade_run.h"
#include "game.h"
#include "lang.h"
#include "audio.h"

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

int RunUpgTryBuy(int kind) {
    if (kind < 0 || kind >= RUN_UPG_COUNT) return 1;
    if (upg[kind] >= RUN_UPG_MAX) return 2;
    int cost = RunUpgCost(kind);
    if (cost < 0) return 2;
    if (shards < cost) return 4;
    shards -= cost;
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
    default: return "WRENCH KNOCK";
    }
}

const char* RunUpgNameZh(int kind) {
    switch (kind) {
    case 0: return "携带量";
    case 1: return "角色抗性";
    case 2: return "体能";
    case 3: return "手电效率";
    default: return "扳手击退";
    }
}

const char* RunUpgDesc(int kind) {
    switch (kind) {
    case 0: return "Throwable capacity +2 / lv";
    case 1: return "Lose 10% less sanity when grabbed";
    case 2: return "Sprint stamina -15% / lv";
    case 3: return "Flashlight drain -15% / lv";
    default: return "Push monsters back. Lv2+ becomes cooldown.";
    }
}

const char* RunUpgDescZh(int kind) {
    switch (kind) {
    case 0: return "投掷物携带上限 +2 / 级";
    case 1: return "被抓时少掉 10% 理智 / 级";
    case 2: return "冲刺体力消耗 -15% / 级";
    case 3: return "手电耗电 -15% / 级";
    default: return "推开怪物。2 级起改为冷却制。";
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

/* ===== v7 扳手击退 =====
   0 级（默认）  每层 2 次      推开 2 格    无硬直
   1 级          每层 4 次      推开 3 格    无硬直
   2 级          冷却 6 秒      推开 3 格    无硬直
   3 级          冷却 3 秒      推开 4 格    硬直 1 秒 */
int RunUpgWrenchLevel(void) {
    int lv = upg[4];
    if (lv < 0) lv = 0;
    if (lv > 3) lv = 3;
    return lv;
}
float RunUpgWrenchCooldown(void) {
    int lv = RunUpgWrenchLevel();
    if (lv <= 1) return -1.0f;      /* 次数制 */
    return (lv == 2) ? 6.0f : 3.0f;
}
int RunUpgWrenchCharges(void) {
    return (RunUpgWrenchLevel() <= 0) ? 2 : 4;
}
float RunUpgWrenchRange(void) {
    int lv = RunUpgWrenchLevel();
    return (lv >= 3) ? 4.0f : (lv >= 1 ? 3.0f : 2.0f);
}
float RunUpgWrenchStun(void) {
    return (RunUpgWrenchLevel() >= 3) ? 1.0f : 0.0f;
}

void RunUpgLoadFromSave(const int* arr) {
    for (int i = 0; i < RUN_UPG_COUNT; i++) {
        int v = arr ? arr[i] : 0;
        if (v < 0) v = 0;
        if (v > RUN_UPG_MAX) v = RUN_UPG_MAX;
        upg[i] = v;
    }
}

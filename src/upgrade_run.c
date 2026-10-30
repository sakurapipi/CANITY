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
    static const int costs[RUN_UPG_MAX] = { 60, 130, 220 };
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
    default: return "FLASHLIGHT";
    }
}

const char* RunUpgNameZh(int kind) {
    switch (kind) {
    case 0: return "携带量";
    case 1: return "角色抗性";
    case 2: return "体能";
    default: return "手电效率";
    }
}

const char* RunUpgDesc(int kind) {
    switch (kind) {
    case 0: return "Throwable capacity +2 / lv";
    case 1: return "Lose 10% less sanity when grabbed";
    case 2: return "Sprint stamina -15% / lv";
    default: return "Flashlight drain -15% / lv";
    }
}

const char* RunUpgDescZh(int kind) {
    switch (kind) {
    case 0: return "投掷物携带上限 +2 / 级";
    case 1: return "被抓时少掉 10% 理智 / 级";
    case 2: return "冲刺体力消耗 -15% / 级";
    default: return "手电耗电 -15% / 级";
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

void RunUpgLoadFromSave(const int* arr) {
    for (int i = 0; i < RUN_UPG_COUNT; i++) {
        int v = arr ? arr[i] : 0;
        if (v < 0) v = 0;
        if (v > RUN_UPG_MAX) v = RUN_UPG_MAX;
        upg[i] = v;
    }
}

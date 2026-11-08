/*
 * Canity — 局内强化（v3.0）
 *
 * 用**金币**（用户叫它"晶体"）在安全屋买的永久强化，每局重置。
 * 4 条线，每条 3 级：
 *   0 WEAPON       武器伤害 +12%/级
 *   1 RESIST       受到伤害 -10%/级
 *   2 STAMINA      冲刺消耗 -15%/级
 *   3 FLASHLIGHT   手电耗电 -15%/级
 */
#ifndef CANITY_UPGRADE_RUN_H
#define CANITY_UPGRADE_RUN_H

#define RUN_UPG_COUNT 6   /* 2026-11-05：+1 条 MINIMAP（小地图耗电） */
#define RUN_UPG_MAX   3

void  RunUpgReset(void);
int   RunUpgLevel(int kind);
int   RunUpgCost(int kind);          /* -1 = 满级 */
int   RunUpgTryBuy(int kind);        /* 0=成功 2=满级 4=金币不够 */
const char* RunUpgName(int kind);    /* 英文名 */
const char* RunUpgNameZh(int kind);
const char* RunUpgDesc(int kind);    /* 精确效果 */
const char* RunUpgDescZh(int kind);

/* ---- 效果查询 ---- */
/* RunUpgDamageMul 已删（第 13 轮：常量桩零调用）*/
float RunUpgResistMul(void);         /* 受伤倍率 */
float RunUpgStaminaMul(void);        /* 冲刺消耗倍率 */
float RunUpgFlashMul(void);          /* 手电耗电倍率 */
/* v7：扳手击退（常驻能力，等级由商店升级） */
int   RunUpgWrenchLevel(void);       /* 0..3 */
float RunUpgWrenchCooldown(void);    /* <0 = 用次数制，>=0 = 冷却秒数 */
int   RunUpgWrenchCharges(void);     /* 每层可用次数（冷却制时无意义） */
float RunUpgWrenchRange(void);       /* 推开几格 */
float RunUpgWrenchStun(void);        /* 硬直秒数 */
int   RunUpgCarryBonus(void);        /* v3.0：投掷物携带上限 +2/级 */
/* 2026-11-05：小地图耗电 —— 每级 -2 个百分点，3 级封顶 */
int   RunUpgMapDrainLevel(void);     /* 0..3 */
float RunUpgMapDrainPct(void);       /* 当前每秒耗电百分比（10 / 8 / 6 / 4） */
void  RunUpgLoadExtra(int minimapLevel);   /* 存档单独读第 6 项 */
void  RunUpgLoadFromSave(const int* arr);   /* v3.0 从存档载入 */

#endif

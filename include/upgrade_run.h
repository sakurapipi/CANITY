/*
 * Canity — 局内强化（v3.0）
 *
 * 用**碎片**（用户叫它"晶体"）在安全屋买的永久强化，每局重置。
 * 4 条线，每条 3 级：
 *   0 WEAPON       武器伤害 +12%/级
 *   1 RESIST       受到伤害 -10%/级
 *   2 STAMINA      冲刺消耗 -15%/级
 *   3 FLASHLIGHT   手电耗电 -15%/级
 */
#ifndef CANITY_UPGRADE_RUN_H
#define CANITY_UPGRADE_RUN_H

#define RUN_UPG_COUNT 4
#define RUN_UPG_MAX   3

void  RunUpgReset(void);
int   RunUpgLevel(int kind);
int   RunUpgCost(int kind);          /* -1 = 满级 */
int   RunUpgTryBuy(int kind);        /* 0=成功 2=满级 4=碎片不够 */
const char* RunUpgName(int kind);    /* 英文名 */
const char* RunUpgNameZh(int kind);
const char* RunUpgDesc(int kind);    /* 精确效果 */
const char* RunUpgDescZh(int kind);

/* ---- 效果查询 ---- */
float RunUpgDamageMul(void);         /* 武器伤害倍率 */
float RunUpgResistMul(void);         /* 受伤倍率 */
float RunUpgStaminaMul(void);        /* 冲刺消耗倍率 */
float RunUpgFlashMul(void);          /* 手电耗电倍率 */
int   RunUpgCarryBonus(void);        /* v3.0：投掷物携带上限 +2/级 */
void  RunUpgLoadFromSave(const int* arr);   /* v3.0 从存档载入 */

#endif

#ifndef CANITY_RELICS_H
#define CANITY_RELICS_H

#include "common.h"

/* ------------------------------------------------------------------ */
/* v2.0 遗物（局内临时成长）                                            */
/*   每层通关三选一，每个必带一条负面。同类可叠加（叠满 3 层质变）。      */
/* ------------------------------------------------------------------ */

#define RELIC_COUNT 20
#define RELIC_CHOICES 3

typedef struct {
    const char* nameEn;
    const char* nameZh;
    const char* descEn;
    const char* descZh;
    int   rarity;          /* 0 普通 / 1 稀有 / 2 传说 */
    float dmgMul;          /* 伤害倍率 */
    float speedMul;        /* 移速倍率 */
    float sanityMul;       /* 理智流失倍率 */
    float batteryMul;      /* 手电耗电倍率 */
    float noiseMul;        /* 噪声倍率 */
    float shardMul;        /* 碎片收入倍率 */
    float healAdd;         /* 安全屋回智加成（0.25 = +25%） */
    float maxSanAdd;       /* 理智上限增减（负数 = 削上限） */
} RelicDef;

extern const RelicDef relicDefs[RELIC_COUNT];

/* 本局持有（每层叠加） */
extern int relicOwned[RELIC_COUNT];
extern int relicTotal;

void  RelicResetRun(void);
void  RelicTake(int id);

/* 聚合效果（未持有任何遗物时全部为 1.0 / 0.0） */
float RelicDamageMul(void);
float RelicSpeedMul(void);
float RelicSanityMul(void);
float RelicBatteryMul(void);
float RelicNoiseMul(void);
float RelicShardMul(void);
float RelicHealAdd(void);
float RelicMaxSanAdd(void);

#endif

#ifndef CANITY_ECONOMY_H
#define CANITY_ECONOMY_H
/* Canity — 经济区块（v8） */

/* 强度档位：携带金币 → 0~15 档 */
int   GoldTier(void);
float GoldTierMul(void);     /* 1.00 ~ 1.80 */
float GoldHearMul(void);     /* 听觉半径倍数（= 档位倍率） */
float GoldSpeedMul(void);    /* 恒为 1.0 —— 速度永不受金币影响 */

/* v7 协议（B3） */
extern int  contractKind;            /* 0 SAFE / 1 STANDARD / 2 VOLUNTARY / 3 LAST */
int         ContractTierBonus(void); /* 强度档位修正 */
float       ContractExitMul(void);   /* 撤离倍数 */
const char* ContractName(void);
void        ContractCycle(void);

#endif

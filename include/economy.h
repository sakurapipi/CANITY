#ifndef CANITY_ECONOMY_H
#define CANITY_ECONOMY_H
/* Canity — 经济区块（v8） */

/* 强度档位：携带金币 → 0~15 档 */
int   GoldTier(void);
extern int bankRiskPlan;
int BankCarryAmount(void);
int BankPreviewTier(void);
float BankPreviewHearing(void);
float BankRiskExitMul(int plan);
const char* BankRiskName(int plan);
float GoldTierMul(void);     /* 1.00 ~ 1.75 */
float GoldHearMul(void);     /* 听觉半径倍数（= 档位倍率） */
float GoldSpeedMul(void);    /* 恒为 1.0 —— 速度永不受金币影响 */

int EconomyRewardedProfit(int survived, float* performance);
int GoldNextThreshold(void); /* -1 at maximum or no gold-driven next band */
float EconomyHearMul(void);
float EconomyPressureRatio(void);

/* v7 协议（B3） */
extern int  contractKind;            /* 0 SAFE / 1 STANDARD / 2 VOLUNTARY / 3 LAST */
int         ContractTierBonus(void); /* 强度档位修正 */
float       ContractExitMul(void);   /* 撤离倍数 */
const char* ContractName(void);
void        ContractCycle(void);

#endif

/*
 * Canity — 钱包接口（v6 货币统一）
 *
 *   carryGold  局内携带（可花、可被偷、决定强度档）
 *   bankGold   银行（跨局）
 *
 * **花钱一律走 WalletSpendRun / WalletSpendBank。**
 * 自检会扫源码：除了 wallet.c，任何 .c 里不许出现 `carryGold -=` / `bankGold -=`。
 */
#ifndef CANITY_WALLET_H
#define CANITY_WALLET_H

/* 查询 */
int WalletRun(void);
int WalletBank(void);
int WalletTotal(void);

/* 够不够 */
int WalletCanPayRun(int n);
int WalletCanPayBank(int n);

/* 花钱：返回 1 = 成功，0 = 钱不够（不会扣成负数） */
int WalletSpendRun(int n);
int WalletSpendBank(int n);

/* 收钱 */
void WalletAddRun(int n);
void WalletAddBank(int n);

/* 被偷：返回实际被偷走的数量 */
int WalletStealRun(int n);

/* 银行 → 携带（开局取款），返回实际取出数量 */
int WalletWithdraw(int n);

/* 结算：survived=1 撤离 / 0 死亡；profit = 本局净赚 */
void WalletSettle(int survived, int profit);
int WalletPreviewSettle(int survived, int profit);


/* Run-start principal and settlement preview. */
extern int carryAtRunStart;
int BankPreviewGain(int survived);

void MoneyBagsSave(float* positions,int* amounts);
void MoneyBagsLoad(const float* positions,const int* amounts);
#endif /* CANITY_WALLET_H */

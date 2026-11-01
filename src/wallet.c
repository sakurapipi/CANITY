/*
 * Canity — 钱包（v6 货币统一的唯一入口）
 *
 * 为什么要有这个模块
 * ──────────────────
 * 统一前项目里有三个货币变量（shards / pendingShards / carryGold），
 * 同一个"钱"在不同系统里是两个变量 —— 改一处忘一处就会算错。
 *
 * 现在只有两个位置：
 *
 *      carryGold   局内携带（可花、可被偷、决定强度档）
 *      bankGold    银行（跨局、不参与局内消费）
 *
 * **所有花钱的地方都必须走这里**。好处：
 *   · 以后加"折扣 / 税 / 手续费"只改这一个文件
 *   · 永远不会出现"某个系统忘了检查余额"
 *   · 自检可以扫源码：除了本文件，不许出现 `carryGold -=` / `bankGold -=`
 *
 * 收入（+=）不强制走这里 —— 收入的来源点很分散且语义各异
 * （箱子 / 撤离 / 成就 / 每日任务），强行统一反而不好读。
 */
#include <stdio.h>
#include "common.h"
#include "wallet.h"

/* ---- 查询 ---- */

int WalletRun(void)  { extern int carryGold; return carryGold; }
int WalletBank(void) { extern int bankGold;  return bankGold;  }

int WalletTotal(void) {
    return WalletRun() + WalletBank();
}

int WalletCanPayRun(int n) {
    if (n <= 0) return 1;
    return WalletRun() >= n;
}

int WalletCanPayBank(int n) {
    if (n <= 0) return 1;
    return WalletBank() >= n;
}

/* ---- 花钱 ---- */

int WalletSpendRun(int n) {
    extern int carryGold;
    if (n <= 0) return 1;
    if (carryGold < n) return 0;
    carryGold -= n;
    return 1;
}

int WalletSpendBank(int n) {
    extern int bankGold;
    if (n <= 0) return 1;
    if (bankGold < n) return 0;
    bankGold -= n;
    return 1;
}

/* ---- 收钱 ---- */

void WalletAddRun(int n) {
    extern int carryGold;
    if (n <= 0) return;
    carryGold += n;
}

void WalletAddBank(int n) {
    extern int bankGold;
    if (n <= 0) return;
    bankGold += n;
    { extern int bankHistMax; if(bankGold>bankHistMax) bankHistMax=bankGold; }
}

/* ---- 被偷（拾荒者）---- */

int WalletStealRun(int n) {
    extern int carryGold;
    if (n <= 0) return 0;
    if (carryGold <= 0) return 0;
    if (n > carryGold) n = carryGold;
    carryGold -= n;
    return n;
}

/* ---- 转账：银行 → 携带（开局取款）---- */

int WalletWithdraw(int n) {
    extern int carryGold;
    if (n <= 0) return 0;
    if (!WalletSpendBank(n)) return 0;
    carryGold += n;
    return n;
}

/* ---- 结算：撤离 / 死亡 ---- */

int WalletPreviewSettle(int survived, int profit) {
    extern int carryGold, bankGold, carryAtRunStart, bankHistMax;

    int gain;
    if (survived) {
        int p = profit;
        if (p < 0) p = 0;
        /* 银行博弈：携带越多，倍数越高（保留原有机制） */
        float ratio = (bankHistMax > 0)
                    ? (float)carryAtRunStart / (float)bankHistMax : 1.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        float mult = 1.0f + ratio;
        if (mult > BANK_MAX_MULT) mult = BANK_MAX_MULT;
        int principal=carryGold<carryAtRunStart ? carryGold : carryAtRunStart;
        gain = principal + (int)((float)p * mult + 0.5f);

    } else {
        gain = (int)((float)carryGold * BANK_DEATH_REFUND + 0.5f);

    }

    return gain;
}

void WalletSettle(int survived,int profit) {
    extern int bankGold,bankHistMax,bankStreak,carryGold,carryAtRunStart;
    int gain=WalletPreviewSettle(survived,profit);
    if(survived) bankStreak++; else bankStreak=0;
    bankGold += gain;
    if (bankGold > bankHistMax) bankHistMax = bankGold;

    carryGold = 0;
    carryAtRunStart = 0;
}

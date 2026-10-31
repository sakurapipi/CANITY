#ifndef CANITY_HINTS_H
#define CANITY_HINTS_H
/* Canity — 一次性教学提示（v7 B4） */
#define HINT_THROW       0
#define HINT_HIDE_SHOP   1
#define HINT_GOLD        2
#define HINT_EXIT        3
#define HINT_CARRY_TIER  4
#define HINT_CAUGHT      5
#define HINT_WRENCH      6
#define HINT_COUNT       7

void HintsReset(void);
void HintOnce(int id);
void HintOnceText(int id, const char* zh, const char* en);
#endif

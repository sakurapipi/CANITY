/* Narrow contract: owned by the corresponding loop module. */
#ifndef CANITY_RUN_REWARDS_H
#define CANITY_RUN_REWARDS_H

#include "common.h"

extern int skillPickOptions[INRUN_CHOICES];
extern int skillPickSel, pickMode, skillPickRound, skillPickTotal;
void RewardScheduleForLevel(int level);
void RewardResetPending(void);
void OpenSkillPick(void);
int OpenRelicPick(void);
void CloseSkillPick(void);

#endif

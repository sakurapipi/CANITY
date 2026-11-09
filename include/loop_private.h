/* Internal orchestration contract for loop modules, not domain state access. */
#ifndef CANITY_LOOP_PRIVATE_H
#define CANITY_LOOP_PRIVATE_H
#include "common.h"

enum { START_NORMAL = 0, START_ENDLESS, START_DAILY, START_CAMPAIGN, START_TRAINING,
       START_GUIDE };

void LoopQueueStart(int mode, int stage);
void LoopResetDecoyCooldown(void);
void LoopResetMapHold(void);
void LoopClearInputEdges(void);
void LoopCancelTransientInput(void);
void LoopCheckAchievements(void);
void LoopResetHuntAchievements(void);
int LoopIsMouseButton(int vk);
void LoopSelectVisibleSlot(int n);

/* Game-owned orchestration state used by these loop stages. */
extern float walkTimer;
extern float walkStepTimer;
extern int exitGoDeeper;
extern float lastExtractDepthMul;
extern float slashMaxDist;
extern int ultKind;
extern int safehouseHoverRow;
extern int safehouseHoverTab;
extern float floorStartTime;
extern int dailyClearedToday;
extern int g_throwOk;

void EnsureThrowSelectedValid(void);

void UpdateExplored(void);

void BankSettle(int survived);

float GoldSearchAt(int level);

void MouseToLogical(int* mx, int* my);

void HitTestMenu(int mx, int my);

#endif

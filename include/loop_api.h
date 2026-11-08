/* include/loop_api.h —— 循环/模式（2026-11-09，第 10 轮）
 *
 * 从 game.h 按职责抽出。**game.h 未改动**，两边并存。
 */
#ifndef CANITY_LOOP_API_H
#define CANITY_LOOP_API_H

#include "common.h"

/* 状态机（循环层） */
extern GameState currentState;

void ResetRunModeFlags(void);
extern int g_forceMod;
void Update(float dt);
extern int autoPaused;
extern int trainingMode, trainingTotal, trainingLast, trainingCount, trainingDps;
const char* TutorialObjective(void);
const char* TutorialHint(void);
int TutorialStepNumber(void);
int TutorialStepTotal(void);
void TutorialBegin(void);
extern int trainingHist[6];
extern float trainingDpsTimer;
extern int skillPickSel;
void OpenSkillPick(void);
int OpenRelicPick(void);
extern int pickMode;
extern int campaignMode;
extern const char* campaignName(int stage);
void CloseSkillPick(void);
void StartGame(void);
void StartDailyRun(void);
void StartCampaign(int stage);
void PauseGame(void);

extern GameState prevState;
extern int tutorialDone;

void ClearKeyEdges(void);

/* 局内随机数（原 game.c，已拆出） */
unsigned int RunRngNext(void);
void RunRngSeed(unsigned int seed);
unsigned int RunRngState(void);
void RunRngSetState(unsigned int st);

#endif

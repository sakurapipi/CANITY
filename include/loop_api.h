/* include/loop_api.h —— 循环/模式（2026-11-09，第 10 轮）
 *
 * 从 game.h 按职责抽出。**game.h 未改动**，两边并存。
 */
#ifndef CANITY_LOOP_API_H
#define CANITY_LOOP_API_H

#include "common.h"

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

#endif

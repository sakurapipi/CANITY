/* Narrow contract: owned by the corresponding loop module. */
#ifndef CANITY_TUTORIAL_H
#define CANITY_TUTORIAL_H

#include "common.h"

/* 教程步骤枚举（原 game.c，拆块需要） */
enum {
    TUT_MOVE = 0,
    TUT_LOOK,
    TUT_SPRINT,
    TUT_LIGHT,
    TUT_MAP,
    TUT_SEARCH,
    TUT_CRANK,
    TUT_SELECT,
    TUT_THROW,
    TUT_BEACON,
    TUT_DECOY,
    TUT_KNOCK,
    TUT_HIDE,       /* 第 4 轮：被追的时候该往哪躲 —— 玩家原话「要怎么躲」 */
    TUT_PULSE,
    TUT_LOCKS,
    TUT_SHOP,
    TUT_UPGRADE,
    TUT_RHYTHM,
    TUT_BREAKER,
    TUT_EXIT,
    TUT_DONE
};

extern int tutorialStep, tutorialDone, tutorialGuideOn;
extern int guideMode;
const char* TutorialObjective(void);
const char* TutorialHint(void);
int TutorialStepNumber(void);
int TutorialStepTotal(void);
void TutorialBegin(void);
void TutorialResetMode(void);
void TutorialRestartGuide(void);
void TutorialRecordThrow(void);
void TutorialRecordNoise(void);
void TutorialRecordMapOpened(void);
void TutorialVisitStation(int station);
void TutorialUpdate(float dt);
void TrainingBuildArena(void);

enum {TUTOR_WRENCH,TUTOR_PULSE,TUTOR_PHASE,TUTOR_DECOY,TUTOR_SUPPLY,TUTOR_UPGRADE,TUTOR_RHYTHM};
void TutorialRecordAction(int action);
void TutorialTarget(float* x,float* y);
int TutorialGraduationReward(void);
#endif

/* src/loop_tutorial.c —— 教程（2026-11-09，从 game.c 拆出） */
#include "common.h"
#include "entity.h"
#include "state.h"
#include "loop_api.h"

int TutorialStepTotal(void) { return TUT_DONE - TUT_MOVE; }

int TutorialStepNumber(void) {
    if (tutorialDone) return TutorialStepTotal();
    return tutorialStep + 1;
}

void TutorialBegin(void) {
    tutorialStep = TUT_MOVE;
    tutorialDone = 0;
    tutorialTimer = 0.0f;
    tutorialActionTimer = 0.0f;
    tutorialStartX = posX;
    tutorialStartY = posY;
    tutorialStartDirX = dirX;
    tutorialStartDirY = dirY;
    tutorialLightOff = 0;
    tutorialThrown = 0;
    tutorialKnocked = 0;
    tutorialMapOpened = 0;
    uiDirty = 1;
    tutorialGuideOn = 0;   /* v3.3：进训练营默认不展开引导 */
}

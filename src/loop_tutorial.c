/* src/loop_tutorial.c —— 教程（2026-11-09，从 game.c 拆出） */
#include "common.h"
#include "loop_api.h"

int TutorialStepTotal(void) { return TUT_DONE - TUT_MOVE; }

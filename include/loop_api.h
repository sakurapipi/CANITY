/* include/loop_api.h —— loop 契约（2026-11-09，第 8 轮）
 *
 * 从 game.h 按职责抽出来的第 4 块。**game.h 未改动**，两边暂时并存。
 *
 * 目的：只关心loop的文件，不必认识 game.h 里全部 255 个全局。
 */
#ifndef CANITY_LOOP_API_H
#define CANITY_LOOP_API_H

#include "common.h"

extern GameState currentState, prevState;
extern int levelEvent;
extern int eventBonus;
extern int tutorialStep, tutorialDone;
extern int dailyMode;
extern int campaignMode;
extern int campaignStage;

#endif

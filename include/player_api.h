/* include/player_api.h —— player 契约（2026-11-09，第 8 轮）
 *
 * 从 game.h 按职责抽出来的第 1 块。**game.h 未改动**，两边暂时并存。
 *
 * 目的：只关心player的文件，不必认识 game.h 里全部 255 个全局。
 */
#ifndef CANITY_PLAYER_API_H
#define CANITY_PLAYER_API_H

#include "common.h"

extern int carryGold;
extern float stepTimer;
extern int cranking;
extern int holdingNow;
extern float wrenchHitFlashT;
extern float wrenchSwingFxT;
extern float ghostStillT;
extern float bobPhase;
extern int flashlightOffLevel;
extern int sprintedThisLevel;

#endif

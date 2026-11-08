/* include/player_api.h —— player
 *
 * 2026-11-09（第 7 轮）：从 game.h 里按职责抽出来的第 1 块。
 *
 * **这一轮只抄不删** —— game.h 里的原声明还在，两边并存。
 * 等 39 个引用者逐个改成 include 这个头之后，才会从 game.h 删掉重复。
 *
 * 目的：让只需要 player 的文件，不必认识 game.h 里全部 160 个全局。
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

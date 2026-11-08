/* include/present_api.h —— present 契约（2026-11-09，第 8 轮）
 *
 * 从 game.h 按职责抽出来的第 3 块。**game.h 未改动**，两边暂时并存。
 *
 * 目的：只关心present的文件，不必认识 game.h 里全部 255 个全局。
 */
#ifndef CANITY_PRESENT_API_H
#define CANITY_PRESENT_API_H

#include "common.h"

extern int uiDirty;
extern double g_timeSec;
extern const char* pickupMsg;
extern float pickupMsgTimer;
void ForceCursor(int visible);
extern float g_shakeOffX, g_shakeOffY;
void AddShake(float seconds, float mag);

#endif

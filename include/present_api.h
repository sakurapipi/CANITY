/* include/present_api.h —— present
 *
 * 2026-11-09（第 7 轮）：从 game.h 里按职责抽出来的第 3 块。
 *
 * **这一轮只抄不删** —— game.h 里的原声明还在，两边并存。
 * 等 39 个引用者逐个改成 include 这个头之后，才会从 game.h 删掉重复。
 *
 * 目的：让只需要 present 的文件，不必认识 game.h 里全部 160 个全局。
 */
#ifndef CANITY_PRESENT_API_H
#define CANITY_PRESENT_API_H

#include "common.h"

extern int uiDirty;
extern double lastRenderMs;         /* 最近一帧（含呈现与 HUD）的耗时 */
extern double g_timeSec;            /* 主循环时钟，渲染用它做动画 */
extern const char* pickupMsg;
extern float pickupMsgTimer;
void ForceCursor(int visible);
void AddShake(float seconds, float mag);

#endif

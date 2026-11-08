/* include/loop_api.h —— loop
 *
 * 2026-11-09（第 7 轮）：从 game.h 里按职责抽出来的第 4 块。
 *
 * **这一轮只抄不删** —— game.h 里的原声明还在，两边并存。
 * 等 39 个引用者逐个改成 include 这个头之后，才会从 game.h 删掉重复。
 *
 * 目的：让只需要 loop 的文件，不必认识 game.h 里全部 160 个全局。
 */
#ifndef CANITY_LOOP_API_H
#define CANITY_LOOP_API_H

#include "common.h"

extern int dailyMode;      /* v2.0：每日挑战 */
extern int campaignMode;   /* v2.0：战役模式 */
extern int campaignStage;  /* 0..4 */
void StartGame(void);
void StartDailyRun(void);   /* v2.0 每日挑战 */
void StartCampaign(int stage);  /* v2.0 战役 */

#endif

/* include/world_api.h —— world 契约（2026-11-09，第 8 轮）
 *
 * 从 game.h 按职责抽出来的第 2 块。**game.h 未改动**，两边暂时并存。
 *
 * 目的：只关心world的文件，不必认识 game.h 里全部 255 个全局。
 */
#ifndef CANITY_WORLD_API_H
#define CANITY_WORLD_API_H

#include "common.h"

extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
extern int explored[MAP_HEIGHT][MAP_WIDTH];
extern int currentLevel;
extern float exitX, exitY;
extern int containerCount;
extern Breaker breaker;
extern int fusesHeld;
extern int fusesNeeded;
extern int breakerOpen;
extern int fusesCollectedRun;

#endif

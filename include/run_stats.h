/* Shared contract; no system headers outside common.h. */
#ifndef CANITY_RUN_STATS_H
#define CANITY_RUN_STATS_H

#include "common.h"

/* Run/floor bookkeeping, owned by game.c. Save layout is unchanged. */
extern int bountyRecovered, metaResetDone;
extern int throwHitCountRun, echoCellsTotalRun, dailyEscapeCount;
extern int lastRunGold, lastRunKept, lastRunCaught;
extern int floorsNoLight, floorsFlawless, floorsPacifist, floorsFast, floorsQuiet;
extern int floorsNoSanityLoss, wardenCleanFloors, fullHideDone;
extern int floorTookDamage, floorSpotted, hideCount, totalThrown;
extern float crankSecondsTotal, doubleHuntSeconds;

#endif

#ifndef CANITY_FLOOR_ROUTES_H
#define CANITY_FLOOR_ROUTES_H
/* Generation only: never invoke after actors, loot or hide spots are placed. */
int FloorRoutesConnect(int wallBudget);
void FloorRoutesBraid(int targetPercent);
int FloorRoutesWithinDeadEndLimit(int targetPercent);
#endif

/* Narrow contract: owned by the corresponding loop module. */
#ifndef CANITY_ITEM_EFFECTS_H
#define CANITY_ITEM_EFFECTS_H

#include "common.h"

extern SmokeCloud smokes[MAX_SMOKE];
extern GlueZone glues[MAX_GLUE];
extern Decoy decoysArr[MAX_DECOY];
void SpawnSmoke(float x, float y);
void SpawnGlue(float x, float y);
void PlaceDecoy(float x, float y);
void ArmDecoy(void);
int DecoyIntercept(void);
int PointInSmoke(float x, float y);
int SegmentInSmoke(float x0, float y0, float x1, float y1);
int PlayerInSmoke(void);
void UpdateSmokes(float dt);


/* Shared contract, declared by the owning subsystem. */
extern int flares, bottles, phaseBeacons;
extern int bricks, acids, smokeBombs, firePots, gluePots, mirrors;
extern int echoBolts, decoys, throwSelected;

#endif

#ifndef CANITY_DECOY_RUNTIME_H
#define CANITY_DECOY_RUNTIME_H
#include "common.h"

extern Decoy decoysArr[MAX_DECOY];
void PlaceDecoy(float x, float y);
void ArmDecoy(void);
int DecoyHitNear(float x, float y);
void ShatterDecoy(int slot);
void DecoyResetFloor(void);
void DecoyTick(float dt);
#endif

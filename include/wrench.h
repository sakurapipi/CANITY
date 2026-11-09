/* Narrow contract: owned by the corresponding loop module. */
#ifndef CANITY_WRENCH_H
#define CANITY_WRENCH_H

#include "common.h"

extern float wrenchSwingT, wrenchCharge;
extern float wrenchHitFlashT, wrenchSwingFxT;
void WrenchStartSwing(float charge);
void WrenchSwingStartForTest(float charge);
void WrenchTick(float dt);
void WrenchKnockUpdate(float dt);


/* Per-floor wrench usage state. */
extern int wrenchLastUsed;

#endif

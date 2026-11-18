#ifndef CANITY_THROWING_RUNTIME_H
#define CANITY_THROWING_RUNTIME_H
#include "common.h"
#include "shot_spec.h"

/* Internal boundary between projectile integration and impact rules. */
int ShotStoneLockNear(float x, float y, float radius);
void StoneHitTargets(Shot *stone, float radius, int landed);
int ShotHitsActor(Shot *projectile, const ShotSpec *spec);
void FinishShot(int index);
#endif

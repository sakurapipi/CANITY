#ifndef CANITY_PROGRESSION_H
#define CANITY_PROGRESSION_H
#include "common.h"
enum {
    PG_DAMP_RADIUS,
    PG_DAMP_TIME,
    PG_BLIND_TIME,
    PG_DUST_TIME,
    PG_JAM_TIME,
    PG_SOLVENT_TIME,
    PG_TETHER_RANGE,
    PG_TRACE_RANGE
};
float ProgressionToolValue(int stat);
int ProgressionAcousticLink(float *ax, float *ay, float *bx, float *by);
int ProgressionInAcousticLink(float x, float y);
void ProgressionWrenchContact(Monster *target);
void ProgressionTetherLanding(float *x, float *y);
#endif

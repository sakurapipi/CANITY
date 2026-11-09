/* Shared contract; no system headers outside common.h. */
#ifndef CANITY_ECHO_MARKS_H
#define CANITY_ECHO_MARKS_H

#include "common.h"

/* Echo scan state and markers, currently implemented in game.c. */
#define ECHO_MARK_MAX 24
extern float echoScanT, echoScanX, echoScanY;
extern float echoMarkX[ECHO_MARK_MAX], echoMarkY[ECHO_MARK_MAX], echoMarkT[ECHO_MARK_MAX];
extern int echoMarkActive[ECHO_MARK_MAX], echoMarkKind[ECHO_MARK_MAX];
void EchoMarksReset(void);
void EchoMarksScan(float sx, float sy, float rad);
void EchoMarksTick(float dt);

#endif

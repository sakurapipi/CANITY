/* Narrow contract: owned by the corresponding loop module. */
#ifndef CANITY_PLAYER_TICK_H
#define CANITY_PLAYER_TICK_H

#include "common.h"

typedef struct PlayerFrameInput {
    int forward;
    int strafe;
    int moving;
} PlayerFrameInput;

/* 0 means the existing LAST RESORT path consumed this tick; caller returns. */
int PlayerTickVitals(float dt, int inSafe);
void PlayerTickBattery(float dt);
/* Session-local grace state is reset on new run, leave and continue. */
void PlayerResetSprintGrace(void);
void PlayerTickSprintGrace(float dt, int wantSprint);
PlayerFrameInput PlayerPrepareMovement(float dt);
void PlayerApplyMovement(float dt, const PlayerFrameInput* input);

extern int counterKillUsed;
#endif

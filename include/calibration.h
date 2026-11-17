/* Narrow contract: owned by the corresponding loop module. */
#ifndef CANITY_CALIBRATION_H
#define CANITY_CALIBRATION_H

#include "common.h"

extern int calibActive, calibResult;
extern float calibPos, calibZoneC, calibCooldown, calibResultTimer;
float CalibZoneScale(void);
float CalibSpeedScale(void);
float CalibProgress(void);
int CalibReadyState(void);
float CalibLeadIn(void);
float CalibLaneFlash(int lane);
int CalibLaneJudgement(int lane); /* -1 miss, 1 good, 2 perfect */
float CalibLastGain(int lane,int batteryGain);
void CalibStart(void);
void CalibLock(void);
void CalibTick(float dt);
void CalibHandleInput(int inSafe);
void CalibResetRun(void);
int CalibNoteCount(void);
int CalibNoteLane(int note);
float CalibNoteOffset(int note);
int CalibErrors(void);
float CalibBatteryRestored(void);
int CalibCombo(void);
int CalibPendingReward(void);
void CalibAccumulateDrain(float amount);

void CalibStartAtStation(void);
void CalibInputKey(int key);
#endif

/* include/player_api.h —— 玩家（2026-11-09，第 10 轮）
 *
 * 从 game.h 按职责抽出。**game.h 未改动**，两边并存。
 */
#ifndef CANITY_PLAYER_API_H
#define CANITY_PLAYER_API_H

#include "common.h"

extern int qteHint;
extern float flashTime;
extern float healFlash;
extern int flares, bottles;
extern int echoBolts;
extern int decoys;
extern int throwSelected;
extern int hideSpotCount;
extern int hiddenIn;
extern int hideSpotNear;
extern float crankTimer;
extern float throwCd;
extern float crankClick;
extern int cranking;
extern int isCharging;
extern float heartTimer;
extern int searchedCount;
extern float holdTimer;
void WrenchStartSwing(float charge);
void WrenchTick(float dt);
void UnstickPlayer(void);
extern int runSearched;
extern float knifeCd;
extern int slashActive;
extern float slashX, slashY, slashDX, slashDY, slashTravel, slashFade;
extern float slashHitStop;
extern float hitFlash, muzzleFlash;
void AddHitFlash(float amount);
extern int slashDmg;
extern float weaponAtkCharge;
extern float decoySelfTimer;
extern float smokeDim;
int PlayerInSmoke(void);
extern int bricks, acids, smokeBombs;
extern int firePots, gluePots, mirrors;
extern int g_dmgIgnoreArmor;
int ApplyDamage(Monster* m, int base, int type, float sx, float sy);
extern int lastHitDamage;
extern float lastHitTimer;
extern int lastHitBackstab;
extern float leechHintTimer;
float SanityDrainRate(void);
int BottleCap(void);
float SanityMax(void);
float StaminaMax(void);
extern float energy;
int EnergyMax(void);
extern int calibActive, calibAttempts, calibResult;
extern float calibPos, calibZoneC, calibCooldown, calibResultTimer;
void CalibStart(void);
void CalibLock(void);
void CalibTick(float dt);
float CalibZoneScale(void);
float CalibSpeedScale(void);
int BatteryMax(void);
extern int bottlesKilled;
extern int faceDodged;
extern int crankTimeTotal;
extern int tookDamage;

extern float ghostStillT;
extern int holdingNow;
extern float stepTimer;
extern float wrenchHitFlashT;
extern float wrenchSwingFxT;
extern int sprintedThisLevel;

#endif

/* include/world_api.h —— 世界（2026-11-09，第 9 轮）
 *
 * 从 game.h 按职责抽出。**game.h 未改动**，两边并存。
 */
#ifndef CANITY_WORLD_API_H
#define CANITY_WORLD_API_H

#include "common.h"

extern int gameWon;
extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
extern int explored[MAP_HEIGHT][MAP_WIDTH];
extern int levelEvent;
extern const char* const eventNames[EVENT_COUNT];
int EventScore(int ev);
extern int levelMod;
extern float modSanityMul;
extern float modSpeedMul;
extern float modBatteryMul, modShardMul, modNoiseMul;
extern float levelBanner;
extern float safeZoneMsg;
extern int upgBattery;
extern int upgSpeed;
extern int upgQuiet;
extern int upgSanity;
extern int upgEco;
extern int upgRest;
extern Thrown thrown[MAX_THROWN];
extern Shot shot[MAX_SHOTS];
extern Shard shards_debris[MAX_SHOTS * SHARD_COUNT];
extern SafeRoom safeRoom;
extern Container containers[MAX_CONTAINERS];
extern HideSpot hideSpots[HIDESPOT_MAX];
extern int lockedThisFloor;
extern float runTime;
extern int hasSave;
extern int bestLevel;
extern int bestEndlessLevel;
extern int bestEndlessScore;
extern int gameCleared;
int EndlessUnlocked(void);
extern int runInProgress;
extern int g_bindings[ACT_COUNT];
extern const char* actionNames[ACT_COUNT];
int Blocked(float x, float y);
void GenerateLevel(void);
int ShotBlocked(float x, float y);
extern int bestMainScore;
extern int bankStreak;
extern int modsSeenMask;
extern int modsSeenCount;
void SpawnParticles(float x, float y, int tint);
extern int monsterMuted;
void SpawnSmoke(float x, float y);
void UpdateSmokes(float dt);
extern SmokeCloud smokes[MAX_SMOKE];
extern ScoreBreakdown lastScore;
extern int skillPickOptions[INRUN_CHOICES];
extern int bossLevel;
extern int bossDefeated;
extern int dailyMode;
extern int dailySeed;
int UpgradeLevel(int kind);
const char* UpgradeName(int kind);
int UpgradeCost(int kind);
void SkipOneLevel(void);
extern int shortcutUsed;
extern int totalGoldEarned;

#endif

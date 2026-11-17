#ifndef CANITY_MONSTER_EVOLUTION_H
#define CANITY_MONSTER_EVOLUTION_H
#include "common.h"
#include "suspicion.h"
#define EVO_ACTORS 11
#define EVO_ZONES 3
#define EVO_HIDE_MEMORIES 4
enum {
    EVO_HUNTER = 0,
    EVO_LIGHT = 3,
    EVO_SPIDER = 5,
    EVO_COLLECTOR = 6,
    EVO_MIMIC = 7,
    EVO_SCAV = 9,
    EVO_WARDEN = 10
};
enum { EVO_ZONE_WEB = 1, EVO_ZONE_COINS = 2 };
typedef struct {
    float x, y, timer, cooldown, aux, aimX, aimY, signal, resist;
    unsigned flags, event;
    int phase, count;
} EvoActor;
typedef struct {
    float x, y, radius, ttl;
    int kind;
} EvoZone;
typedef struct {
    int spot;
    float ttl;
} EvoHideMemory;
typedef struct {
    EvoActor actors[EVO_ACTORS];
    EvoZone zones[EVO_ZONES];
    EvoHideMemory hides[EVO_HIDE_MEMORIES];
    unsigned sequence, soundIds[SUSP_MAX], sharedEvents[SUSP_MAX];
    int sharedCursor, previousHide;
} EvolutionState;
extern EvolutionState evolution;
void EvolutionResetFloor(void);
void EvolutionTick(float dt);
int EvolutionPermit(int owner, float x, float y, float duration);
void EvolutionRelease(int owner);
void EvolutionSoundIssued(int slot);
void EvolutionHunterHeard(int index, int source);
int EvolutionHunterOverride(Monster *actor, float dt);
int EvolutionLightUpdate(Monster *actor, int *has, int source, float *tx, float *ty, float dt);
float EvolutionLightRange(void);
float EvolutionHearingRange(float base);
void EvolutionWebMiss(float x, float y, int anchors);
float EvolutionWebSlow(float x, float y);
int EvolutionClearWeb(float x, float y, float radius);
void EvolutionForgetHide(int spot);
int EvolutionHideRemembered(int spot);
void EvolutionDrawWorld(void);
float EvolutionPoseSignal(int sprite);
int EvolutionMimicUpdate(int index, float dt);
#endif

#include "event_runtime.h"
/* Bounded sidecars: no SaveBlob layout changes and no hidden player tracking. */
#include "common.h"
#include "monster_evolution.h"
#include "threat_tier.h"
#include "player_api.h"
#include "world_api.h"
#include "entity.h"
#include "audio.h"
#include "monster_rules.h"
#include "threat.h"
EvolutionState evolution;
typedef struct {
    int owner;
    float x, y, ttl;
} Permit;
static Permit permits[4];
void EvolutionResetFloor(void) {
    memset(&evolution, 0, sizeof(evolution));
    evolution.previousHide = -1;
    for (int i = 0; i < EVO_HIDE_MEMORIES; i++)
        evolution.hides[i].spot = -1;
    memset(permits, 0, sizeof(permits));
}
void EvolutionRelease(int owner) {
    for (int i = 0; i < 4; i++)
        if (permits[i].owner == owner + 1)
            permits[i].ttl = 0;
}
int EvolutionPermit(int owner, float x, float y, float duration) {
    int freeSlot = -1;
    for (int i = 0; i < 4; i++) {
        Permit *p = &permits[i];
        if (p->ttl <= 0) {
            freeSlot = i;
            continue;
        }
        if (p->owner == owner + 1)
            return 1;
        if (Dist2(posX, posY, x, y) <= 25 && Dist2(posX, posY, p->x, p->y) <= 25)
            return 0;
    }
    if (freeSlot < 0)
        return 0;
    permits[freeSlot] = (Permit){owner + 1, x, y, fminf(5, fmaxf(.1f, duration))};
    return 1;
}
void EvolutionSoundIssued(int slot) {
    if (slot >= 0 && slot < SUSP_MAX) {
        if (++evolution.sequence == 0) {
            evolution.sequence = 1;
            memset(evolution.sharedEvents, 0, sizeof(evolution.sharedEvents));
            memset(evolution.soundIds, 0, sizeof(evolution.soundIds));
        }
        evolution.soundIds[slot] = evolution.sequence;
    }
}
void EvolutionTick(float dt) {
    if (!(dt > 0) || !isfinite(dt))
        return;
    for (int i = 0; i < EVO_ACTORS; i++) {
        EvoActor *a = &evolution.actors[i];
        a->cooldown = fmaxf(0, a->cooldown - dt);
        a->signal = fmaxf(0, a->signal - dt);
        a->resist = fmaxf(0, a->resist - dt);
    }
    for (int i = 0; i < EVO_ZONES; i++)
        evolution.zones[i].ttl = fmaxf(0, evolution.zones[i].ttl - dt);
    for (int i = 0; i < 4; i++)
        permits[i].ttl = fmaxf(0, permits[i].ttl - dt);
    for (int i = 0; i < EVO_HIDE_MEMORIES; i++)
        evolution.hides[i].ttl = fmaxf(0, evolution.hides[i].ttl - dt);
    if (hiddenIn != evolution.previousHide) {
        int old = evolution.previousHide;
        if (MonsterTierAtLeast(1) && old >= 0 && old < hideSpotCount) {
            int slot = 0;
            for (int i = 0; i < EVO_HIDE_MEMORIES; i++) {
                if (evolution.hides[i].spot == old) {
                    slot = i;
                    break;
                }
                if (evolution.hides[i].ttl < evolution.hides[slot].ttl)
                    slot = i;
            }
            evolution.hides[slot] = (EvoHideMemory){old, 12};
        }
        evolution.previousHide = hiddenIn;
    }
}
void EvolutionForgetHide(int spot) {
    for (int i = 0; i < EVO_HIDE_MEMORIES; i++)
        if (evolution.hides[i].spot == spot)
            evolution.hides[i].ttl = 0;
}
int EvolutionHideRemembered(int spot) {
    for (int i = 0; i < EVO_HIDE_MEMORIES; i++)
        if (evolution.hides[i].spot == spot && evolution.hides[i].ttl > 0)
            return 1;
    return 0;
}
float EvolutionHearingRange(float base) {
    return fminf(22 * THREAT_HEAR_MAX, base * (MonsterTierAtLeast(2) ? 1.05f : 1));
}
float EvolutionLightRange(void) {
    return fminf(20, 16 * MonsterThreatScale(SPAWN_LIGHT) * (MonsterTierAtLeast(2) ? 1.05f : 1));
}
void EvolutionWebMiss(float x, float y, int anchors) {
    if (!MonsterTierAtLeast(1))
        return;
    for (int i = 0; i < 2; i++)
        evolution.zones[i].ttl = 0;
    if (x < 1 || y < 1 || x >= MAP_WIDTH - 1 || y >= MAP_HEIGHT - 1 || Blocked(x, y))
        return;
    if (anchors && !Blocked(x - .8f, y) && !Blocked(x + .8f, y) && !Blocked(x, y - 1) &&
        !Blocked(x, y + 1)) {
        evolution.zones[0] = (EvoZone){x - .65f, y, .70f, 2.5f*(EventBloodMoon()?1.10f:1), EVO_ZONE_WEB};
        evolution.zones[1] = (EvoZone){x + .65f, y, .70f, 2.5f*(EventBloodMoon()?1.10f:1), EVO_ZONE_WEB};
    } else
        evolution.zones[0] = (EvoZone){x, y, .6f, 2*(EventBloodMoon()?1.10f:1), EVO_ZONE_WEB};
}
float EvolutionWebSlow(float x, float y) {
    float mul = 1;
    for (int i = 0; i < 2; i++) {
        EvoZone *z = &evolution.zones[i];
        if (z->ttl > 0 && Dist2(x, y, z->x, z->y) < z->radius * z->radius)
            mul = evolution.zones[1].ttl > 0 ? .85f : .9f;
    }
    return mul;
}
int EvolutionClearWeb(float x, float y, float r) {
    int n = 0;
    for (int i = 0; i < 2; i++)
        if (evolution.zones[i].ttl > 0 &&
            Dist2(x, y, evolution.zones[i].x, evolution.zones[i].y) <= r * r &&
            HasLOS(x, y, evolution.zones[i].x, evolution.zones[i].y)) {
            evolution.zones[i].ttl = 0;
            n++;
        }
    return n;
}

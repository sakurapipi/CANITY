#include "common.h"
#include "monster_evolution.h"
#include "threat_tier.h"
#include "entity.h"
#include "world_api.h"
#include "player_api.h"
#include "audio.h"
static int Shared(unsigned event) {
    for (int i = 0; i < SUSP_MAX; i++)
        if (evolution.sharedEvents[i] == event)
            return 1;
    return 0;
}
void EvolutionHunterHeard(int index, int source) {
    if (index < 0 || index >= MON_HUNTER_MAX || source < 0 || source >= SUSP_MAX ||
        !MonsterTierAtLeast(1))
        return;
    EvoActor *a = &evolution.actors[index];
    if(a->resist>0)return;
    unsigned event = evolution.soundIds[source];
    if (!event) {
        EvolutionSoundIssued(source);
        event = evolution.soundIds[source];
    }
    if (a->event == event)
        return;
    a->event = event;
    a->flags &= ~3u;
    a->phase = 0;
    a->x = susp[source].x;
    a->y = susp[source].y;
    if (MonsterTierAtLeast(3) && a->cooldown <= 0 && susp[source].strength >= 40 &&
        !Shared(event)) {
        int other = 0;
        for (int i = 0; i < MON_HUNTER_MAX; i++)
            if (i != index && monHunters[i].active && monHunters[i].state == 0 &&
                Dist2(monHunters[i].x, monHunters[i].y, monHunters[index].x, monHunters[index].y) <=
                    64)
                other = 1;
        int pending = 0;
        for (int i = 0; i < MON_HUNTER_MAX; i++)
            pending |= evolution.actors[i].aux > 0;
        if (other && !pending) {
            a->aux = 1.2f;
            a->flags |= 2u;
            evolution.sharedEvents[evolution.sharedCursor] = event;
            evolution.sharedCursor = (evolution.sharedCursor + 1) % SUSP_MAX;
        }
    }
}
int EvolutionHunterOverride(Monster *actor, float dt) {
    int id = (int)(actor - monHunters);
    if (id < 0 || id >= MON_HUNTER_MAX || !actor->active || actor->stun > 0 ||
        !MonsterTierAtLeast(1))
        return 0;
    EvoActor *a = &evolution.actors[id];
    if(a->resist>0)return 0;
    if (a->aux > 0) {
        a->aux = fmaxf(0, a->aux - dt);
        if (a->aux == 0) {
            for (int i = 0; i < MON_HUNTER_MAX; i++)
                if (i != id && monHunters[i].active && monHunters[i].state == 0 &&
                    Dist2(actor->x, actor->y, monHunters[i].x, monHunters[i].y) <= 64 &&
                    HasLOS(actor->x, actor->y, monHunters[i].x, monHunters[i].y)) {
                    float x = floorf(a->x) + .5f, y = floorf(a->y) + .5f;
                    if (Blocked(x, y))
                        break;
                    Monster *peer = &monHunters[i];
                    peer->tx = x;
                    peer->ty = y;
                    peer->state = 2;
                    peer->stateTimer = 2;
                    peer->memX = x;
                    peer->memY = y;
                    peer->pathLen = peer->pathIdx = 0;
                    peer->repath = 0;
                    evolution.actors[i].event = a->event;
                    evolution.actors[i].flags |= 3u;
                    a->cooldown = 12;
                    break;
                }
        }
    }
    if (a->event && !(a->flags & 1u) && Dist2(actor->x, actor->y, a->x, a->y) < .64f) {
        a->flags |= 1u;
        a->phase = 1;
        a->timer = .9f;
        a->signal = .9f;
        AudioPlayWorld(SFX_ECHO, actor->x, actor->y, .22f, 8, 1);
    }
    if (a->phase == 1) {
        float reach = 0;
        int source = SuspicionBestReach(actor->x, actor->y, .9f, EvolutionHearingRange(22), &reach);
        if (source >= 0 && evolution.soundIds[source] != a->event &&
            susp[source].strength * reach > 40) {
            actor->state = 1;
            actor->tx = susp[source].x;
            actor->ty = susp[source].y;
            actor->suspTarget = source;
            actor->repath = 0;
            EvolutionHunterHeard(id, source);
            return 0;
        }
        a->timer -= dt;
        if (a->timer > 0)
            return 1;
        a->phase = 0;
        actor->state = 2;
        actor->memX = a->x;
        actor->memY = a->y;
        actor->stateTimer = fmaxf(actor->stateTimer, .9f);
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            float x = floorf(a->x) + .5f + dx[k], y = floorf(a->y) + .5f + dy[k];
            if (!Blocked(x, y)) {
                actor->tx = x;
                actor->ty = y;
                actor->repath = 0;
                actor->pathLen = actor->pathIdx = 0;
                break;
            }
        }
    }
    return 0;
}

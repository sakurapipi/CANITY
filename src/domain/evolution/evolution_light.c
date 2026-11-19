#include "skills.h"
#include "present_api.h"
#include "common.h"
#include "monster_evolution.h"
#include "threat_tier.h"
#include "entity.h"
#include "player_api.h"
#include "world_api.h"
#include "audio.h"
int EvolutionLightUpdate(Monster *actor, int *has, int source, float *tx, float *ty, float dt) {
    int index = (int)(actor - lightBeasts);
    if (index < 0 || index >= LIGHTBEAST_MAX)
        return 0;
    EvoActor *a = &evolution.actors[EVO_LIGHT + index];
    if (!actor->active || actor->stun > 0) {
        a->phase = 0;
        a->aux = 0;
        EvolutionRelease(EVO_LIGHT + index);
        return 0;
    }
    if (a->signal > 0 && (a->flags & 8u)) {
        *has = 0;
        actor->state = 0;
        actor->pathLen = 0;
        return 1;
    }
    a->flags &= ~8u;
    if (a->flags & 16u) {
        a->flags = (a->flags & ~16u) | 32u;
        a->timer = .6f * SkillClampRank(SK_BREAKER, skillRank[SK_BREAKER]);
    }
    if (a->flags & 32u) {
        if (hiddenIn >= 0 || playerInSafe || a->timer <= 0 || Blocked(a->x, a->y)) {
            a->flags &= ~32u;
            a->timer = 0;
        } else {
            a->timer = fmaxf(0, a->timer - dt);
            *has = 1;
            *tx = a->x;
            *ty = a->y;
            return 0;
        }
    }

    if (hiddenIn >= 0 || playerInSafe) {
        a->phase = 0;
        a->aux = 0;
        a->timer = 0;
        *has = 0;
        actor->pathLen = 0;
        EvolutionRelease(EVO_LIGHT + index);
        return 0;
    }
    int real = *has;
    if (real && source != a->count) {
        a->count = source;
        if (a->phase == 1) {
            a->phase = 0;
            a->cooldown = 14;
            EvolutionRelease(EVO_LIGHT + index);
        }
        if (a->phase < 2)
            a->aux = 0;
        a->flags &= ~1u;
    }
    if (real) {
        a->x = *tx;
        a->y = *ty;
        a->timer = .9f;
    } else {
        a->timer = fmaxf(0, a->timer - dt);
        if (a->phase < 2)
            a->aux = 0;
        a->flags &= ~1u;
        if (MonsterTierAtLeast(1) && a->timer > 0) {
            *has = 1;
            *tx = a->x;
            *ty = a->y;
        }
    }
    if (!MonsterTierAtLeast(3))
        return 0;
    if (a->phase && !EvolutionPermit(EVO_LIGHT + index, actor->x, actor->y, 2.7f)) {
        a->phase = 0;
        a->aux = 0;
        a->cooldown = 14;
        return 0;
    }
    if (a->phase == 1) {
        if (!real) {
            a->phase = 0;
            a->aux = 0;
            a->cooldown = 14;
            EvolutionRelease(EVO_LIGHT + index);
            return 0;
        }
        a->aimX = *tx;
        a->aimY = *ty;
        a->signal = fmaxf(a->signal, .12f);
        a->aux -= dt;
        if (a->aux > 0)
            return 1;
        float dx = a->aimX - actor->x, dy = a->aimY - actor->y, len = sqrtf(dx * dx + dy * dy);
        a->aimX = len > .01f ? dx / len : 0;
        a->aimY = len > .01f ? dy / len : 0;
        a->aux = 1.8f;
        a->phase = 2;
    }
    if (a->phase == 2) {
        float step = fminf(a->aux, 4 * dt), x = actor->x + a->aimX * step,
              y = actor->y + a->aimY * step;
        if (Blocked(x, y) || !HasLOS(actor->x, actor->y, x, y))
            a->aux = 0;
        else {
            actor->x = x;
            actor->y = y;
            a->aux -= step;
        }
        if (a->aux <= 0) {
            a->phase = 3;
            a->aux = .8f;
            a->cooldown = 14;
        }
        return 2;
    }
    if (a->phase == 3) {
        a->aux -= dt;
        if (a->aux <= 0) {
            a->phase = 0;
            EvolutionRelease(EVO_LIGHT + index);
        }
        return 1;
    }
    if (real && !(a->flags & 1u) && a->cooldown <= 0) {
        a->aux += dt;
        if (a->aux >= 1.8f && Dist2(actor->x, actor->y, *tx, *ty) <= 36 &&
            EvolutionPermit(EVO_LIGHT + index, actor->x, actor->y, 2.7f)) {
            a->flags |= 1u;
            a->phase = 1;
            a->aux = .6f;
            a->signal = a->aux;
            AudioPlayWorld(SFX_GLASS, actor->x, actor->y, .7f, 14, 3);
            return 1;
        }
    }
    return 0;
}

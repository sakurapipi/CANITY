#include "common.h"
#include "monster_evolution.h"
#include "threat_tier.h"
#include "mimic.h"
#include "entity.h"
#include "player_api.h"
#include "world_api.h"
#include "audio.h"
int EvolutionMimicUpdate(int index, float dt) {
    Mimic *m = &mimics[index];
    EvoActor *a = &evolution.actors[EVO_MIMIC + index];
    if (!MonsterTierAtLeast(1))
        return 0;
    if (m->state == MIM_DISGUISED) {
        float d = Dist2(m->x, m->y, posX, posY);
        if (d < 6.76f && !(a->flags & 2u) && a->cooldown <= 0 && HasLOS(m->x, m->y, posX, posY)) {
            a->flags |= 2;
            a->cooldown = 12;
            AudioPlayWorld(SFX_GLASS, m->x, m->y, .15f, 5, 1);
        }
        if (d > 16)
            a->flags &= ~2u;
        return 0;
    }
    if (m->state != MIM_ANGRY || m->stun > 0 || hiddenIn >= 0 || m->timer >= 8.4f) {
        a->phase = 0;
        EvolutionRelease(EVO_MIMIC + index);
        return 0;
    }
    if (a->phase) {
        a->timer -= dt;
        if (a->phase == 1 && a->timer <= 0) {
            float dx = posX - m->x, dy = posY - m->y, d = sqrtf(dx * dx + dy * dy);
            if (d <= 1.6f && d > .01f && (dx * a->aimX + dy * a->aimY) / d > .85f &&
                HasLOS(m->x, m->y, posX, posY))
                stamina = fmaxf(0, stamina - 8);
            a->phase = 2;
            a->timer = .5f;
        } else if (a->phase == 2 && a->timer <= 0) {
            a->phase = 0;
            EvolutionRelease(EVO_MIMIC + index);
        }
        return 1;
    }
    float dx = posX - m->x, dy = posY - m->y, d = sqrtf(dx * dx + dy * dy);
    if (MonsterTierAtLeast(3) && !(a->flags & 1u) && d >= 1 && d <= 1.6f &&
        HasLOS(m->x, m->y, posX, posY) && EvolutionPermit(EVO_MIMIC + index, m->x, m->y, 2)) {
        a->flags |= 1;
        a->aimX = dx / d;
        a->aimY = dy / d;
        a->phase = 1;
        a->timer = .95f;
        a->signal = .95f;
        AudioPlayWorld(SFX_METAL_SCRAPE, m->x, m->y, .6f, 10, 3);
        return 1;
    }
    return 0;
}

#include "skills.h"
#include "progression.h"
#include "run_stats.h"
/* Inventory commits only after successful direct application or pool allocation.
 * Ordinary thrown tools can miss; tether/trace/solvent validate before use. */
#include "common.h"
#include "tactical_tools.h"
#include "monster_evolution.h"
#include "threat_tier.h"
#include "state.h"
#include "player_api.h"
#include "world_api.h"
#include "entity.h"
#include "control_effect.h"
#include "audio.h"
#include "loop_api.h"
#include "lang.h"
#include "present_api.h"
#include "mimic.h"
#include "warden.h"
static void Failed(const char *en, const char *zh) {
    pickupMsg = Tr(en, zh);
    pickupMsgTimer = 1.5f;
    uiDirty = 1;
}
float TacticalFootstepMul(float x, float y, float hush) {
    float best = ProgressionInAcousticLink(x, y) ? fminf(hush, .20f) : hush;
    for (int i = 0; i < 2; i++)
        if (tactical.acoustic[i].ttl > 0 &&
            Dist2(x, y, tactical.acoustic[i].x, tactical.acoustic[i].y) <=
                ProgressionToolValue(PG_DAMP_RADIUS) * ProgressionToolValue(PG_DAMP_RADIUS) &&
            HasLOS(x, y, tactical.acoustic[i].x, tactical.acoustic[i].y))
            best = fminf(best, .20f);
    return best / fmaxf(.01f, hush);
}
/* Newly emitted non-footstep sounds are absorbed too; existing heard events
 * and contact attacks are deliberately not erased. Solid walls block the field. */
float TacticalNoiseMul(float x, float y) {
    if (ProgressionInAcousticLink(x, y))
        return .30f;
    for (int i = 0; i < 2; i++) {
        const ToolZone *z = &tactical.acoustic[i];
        if (z->ttl > 0 &&
            Dist2(x, y, z->x, z->y) <=
                ProgressionToolValue(PG_DAMP_RADIUS) * ProgressionToolValue(PG_DAMP_RADIUS) &&
            HasLOS(x, y, z->x, z->y))
            return .30f;
    }
    return 1;
}
int TacticalEmergencySolvent(void) {
    if (ControlCurrent() != CTRL_SPIDER_WEB || tactical.inventory[TOOL_SOLVENT] <= 0)
        return 0;
    int bound = SkillOwned(SK_NIGHTWALKER) || throwSelected == TOOL_FIRST_ITEM + TOOL_SOLVENT;
    for (int i = 0; i < 4; i++)
        bound |= tactical.quick[i] == TOOL_FIRST_ITEM + TOOL_SOLVENT;
    if (!bound || !ControlUseSolvent(ProgressionToolValue(PG_SOLVENT_TIME)))
        return 0;
    tactical.inventory[TOOL_SOLVENT]--;
    return 1;
}
void TacticalTick(float dt) {
    if (tactical.solvent.ttl > 0) {
        tactical.solvent.ttl = fmaxf(0, tactical.solvent.ttl - dt);
        EvolutionClearWeb(tactical.solvent.x, tactical.solvent.y, 2.5f);
    }
    for (int i = 0; i < 2; i++)
        tactical.acoustic[i].ttl = fmaxf(0, tactical.acoustic[i].ttl - dt);
    for (int i = 0; i < 12; i++)
        tactical.dust[i] = fmaxf(0, tactical.dust[i] - dt);
    tactical.prismFlicker = fmaxf(0, tactical.prismFlicker - dt);
}
int TacticalTryUse(int item, float charge) {
    int tool = item - TOOL_FIRST_ITEM;
    if (tool < 0 || tool >= TOOL_COUNT || tactical.inventory[tool] <= 0)
        return 0;
    if (tool == TOOL_TETHER) {
        if (!CollectorMagneticPull(0) || !CollectorMagneticPull(1)) {
            Failed("NO STOLEN LOOT IN REACH", "范围内没有可追回赃款");
            return 0;
        }
        tactical.inventory[tool]--;
        return 1;
    }
    if (tool == TOOL_TRACE) {
        if (!MonsterTierAtLeast(1)) {
            Failed("NO OLD HIDE TRACE", "当前档位没有回查痕迹");
            return 0;
        }
        int best = -1;
        float dist = ProgressionToolValue(PG_TRACE_RANGE) * ProgressionToolValue(PG_TRACE_RANGE);
        for (int i = 0; i < hideSpotCount; i++)
            if (hideSpots[i].active && EvolutionHideRemembered(i) &&
                HasLOS(posX, posY, hideSpots[i].x, hideSpots[i].y)) {
                float d = Dist2(posX, posY, hideSpots[i].x, hideSpots[i].y);
                if (d <= dist) {
                    best = i;
                    dist = d;
                }
            }
        if (best < 0) {
            Failed("NO OLD HIDE TRACE", "附近没有旧藏点痕迹");
            return 0;
        }
        EvolutionForgetHide(best);
        tactical.inventory[tool]--;
        return 1;
    }
    if (tool == TOOL_SOLVENT) {
        if (TacticalEmergencySolvent())
            return 1;
        int target = 0;
        for (int i = 0; i < 2; i++)
            if (evolution.zones[i].ttl > 0 &&
                Dist2(posX, posY, evolution.zones[i].x, evolution.zones[i].y) <= 25 &&
                HasLOS(posX, posY, evolution.zones[i].x, evolution.zones[i].y))
                target = 1;
        if (!target) {
            Failed("NO WEB IN REACH", "投掷范围内没有蛛网");
            return 0;
        }
    }
    int freeSlot = -1, acoustic = 0;
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!shot[i].active)
            freeSlot = i;
        else if (shot[i].kind == TOOL_SHOT_FIRST + TOOL_ACOUSTIC)
            acoustic++;
    }
    if (tool == TOOL_ACOUSTIC) {
        for (int i = 0; i < 2; i++)
            acoustic += tactical.acoustic[i].ttl > 0;
        if (acoustic >= 2) {
            Failed("DAMPENER AREAS FULL", "吸声区域已满");
            return 0;
        }
    }
    float x = posX + dirX * .25f, y = posY + dirY * .25f;
    if (freeSlot < 0 || Blocked(x, y)) {
        Failed("NO ROOM", "没有安全投掷空间");
        return 0;
    }
    Shot *s = &shot[freeSlot];
    memset(s, 0, sizeof(*s));
    s->x = x;
    s->y = y;
    s->z = SHOT_HEIGHT;
    s->kind = TOOL_SHOT_FIRST + tool;
    s->charge = fmaxf(.1f, fminf(1, charge));
    float speed = tool == TOOL_PRISM ? 9 + 5 * s->charge : 5 + 4 * s->charge;
    s->vx = dirX * speed;
    s->vy = dirY * speed;
    s->vz = tool == TOOL_PRISM ? .15f : 2.0f;
    s->life = 2;
    s->ignoreTarget = -1;
    s->active = 1;
    tactical.inventory[tool]--;
    totalThrown++;
    PlaySfx(SFX_THROW, .4f);
    return 1;
}
void TacticalImpact(int kind, float x, float y) {
    int tool = kind - TOOL_SHOT_FIRST;
    if (tool < 0 || tool >= TOOL_COUNT)
        return;
    if (x < 1 || y < 1 || x >= MAP_WIDTH - 1 || y >= MAP_HEIGHT - 1 || Blocked(x, y))
        return;
    if (tool == TOOL_ACOUSTIC) {
        for (int i = 0; i < 2; i++)
            if (tactical.acoustic[i].ttl <= 0) {
                tactical.acoustic[i] =
                    (ToolZone){x, y, ProgressionToolValue(PG_DAMP_TIME), TOOL_ACOUSTIC};
                break;
            }
    } else if (tool == TOOL_SOLVENT) {
        EvolutionClearWeb(x, y, 2.5f);
        tactical.solvent = (ToolZone){x, y, 1.5f, TOOL_SOLVENT};
    } else if (tool == TOOL_PRISM) {
        for (int i = 0; i < LIGHTBEAST_MAX; i++)
            if (lightBeasts[i].active && Dist2(x, y, lightBeasts[i].x, lightBeasts[i].y) <= 2.25f &&
                HasLOS(x, y, lightBeasts[i].x, lightBeasts[i].y)) {
                EvoActor *a = &evolution.actors[EVO_LIGHT + i];
                if (a->resist > 0)
                    continue;
                a->resist = 10;
                a->signal = ProgressionToolValue(PG_BLIND_TIME);
                if (SkillOwned(SK_BREAKER)) {
                    a->flags |= 16u;
                    a->x = x;
                    a->y = y;
                }
                a->flags |= 8;
                a->phase = 0;
                a->aux = 0;
                a->timer = 0;
                lightBeasts[i].pathLen = lightBeasts[i].pathIdx = 0;
                lightBeasts[i].state = 0;
                lightBeasts[i].tx = lightBeasts[i].x;
                lightBeasts[i].ty = lightBeasts[i].y;
                EvolutionRelease(EVO_LIGHT + i);
            }
        if (flashlightOn && Dist2(posX, posY, x, y) < 2.25f)
            tactical.prismFlicker = .5f;
    } else if (tool == TOOL_REVEAL) {
        int best = -1;
        float dist = 6.25f;
        for (int i = 0; i < containerCount; i++)
            if (containers[i].active && !containers[i].searched &&
                HasLOS(x, y, containers[i].x, containers[i].y)) {
                float d = Dist2(x, y, containers[i].x, containers[i].y);
                if (d < dist) {
                    best = i;
                    dist = d;
                }
            }
        if (best >= 0)
            tactical.dust[best] = ProgressionToolValue(PG_DUST_TIME);
    } else if (tool == TOOL_JAMMER && WardenActive() &&
               Dist2(x, y, WardenX(), WardenY()) <= (RunSkillOwned(IR_WIDEPULSE) ? 9.0f : 6.25f) &&
               HasLOS(x, y, WardenX(), WardenY())) {
        EvoActor *a = &evolution.actors[EVO_WARDEN];
        if (a->resist > 0)
            return;
        a->resist = 12;
        a->signal = ProgressionToolValue(PG_JAM_TIME);
        a->flags |= 8;
        if (a->phase == 2) {
            a->phase = 0;
            a->flags |= 2;
            EvolutionRelease(EVO_WARDEN);
        }
        a->aux = 0;
    }
    AudioPlayWorld(SFX_ECHO, x, y, .25f, 5, 1);
}

#include "progression.h"
#include "present_api.h"
/* Local world-space cues only: existing projection/depth, never camera motion. */
#include "common.h"
#include "render.h"
#include "monster_evolution.h"
#include "tactical_tools.h"
#include "threat_tier.h"
#include "player_api.h"
#include "world_api.h"
#include "entity.h"
#include "state.h"
#include "spider.h"
#include "mimic.h"
#include "warden.h"
#include "sprites.h"
static void GroundMark(float x, float y, float r, unsigned color, int broken) {
    if (Dist2(posX, posY, x, y) > 144 || !HasLOS(posX, posY, x, y))
        return;
    for (int i = 0; i < 48; i++) {
        if (broken && i % 6 < 2)
            continue;
        float angle = i * 6.2831853f / 48;
        float wx = x + cosf(angle) * r, wy = y + sinf(angle) * r;
        if (Blocked(wx, wy))
            continue;
        SpriteBox b = ProjectSprite(wx, wy, .025f, .02f);
        if (!b.ok || b.ty < .2f || b.centerX < 0 || b.centerX >= renderW ||
            b.ty > zBuffer[b.centerX])
            continue;
        int size = (int)RenderProjectionScale(b.ty) / 100 + 1;
        if (size > 3)
            size = 3;
        for (int dy = 0; dy < size; dy++)
            for (int dx = 0; dx < size; dx++)
                PutPixelBlend(b.centerX + dx, b.baseY + dy, color, .7f);
    }
}
static int ActorPosition(int i, float *x, float *y) {
    if (i < 3) {
        if (!monHunters[i].active)
            return 0;
        *x = monHunters[i].x;
        *y = monHunters[i].y;
    } else if (i < 5) {
        if (!lightBeasts[i - 3].active)
            return 0;
        *x = lightBeasts[i - 3].x;
        *y = lightBeasts[i - 3].y;
    } else if (i == EVO_SPIDER) {
        if (!SpiderActive())
            return 0;
        *x = SpiderX();
        *y = SpiderY();
    } else if (i == EVO_COLLECTOR) {
        if (!CollectorActive())
            return 0;
        *x = CollectorX();
        *y = CollectorY();
    } else if (i == EVO_MIMIC || i == EVO_MIMIC + 1) {
        if (mimics[i - EVO_MIMIC].state != MIM_ANGRY)
            return 0;
        *x = mimics[i - EVO_MIMIC].x;
        *y = mimics[i - EVO_MIMIC].y;
    } else if (i == EVO_SCAV) {
        if (!ScavengerActive())
            return 0;
        *x = ScavengerX();
        *y = ScavengerY();
    } else {
        if (!WardenActive())
            return 0;
        *x = WardenX();
        *y = WardenY();
    }
    return 1;
}
void EvolutionDrawWorld(void) {
    for (int i = 0; i < EVO_ACTORS; i++) {
        EvoActor *a = &evolution.actors[i];
        float x, y;
        if ((a->signal <= 0 && !a->phase) || !ActorPosition(i, &x, &y))
            continue;
        int blinded = i>=EVO_LIGHT && i<EVO_LIGHT+2 && (a->flags & 8u) && a->signal>0;
        GroundMark(x, y, .4f, blinded ? 0xD8F5FC : ThreatTierColor(CurrentRunThreatTier()), 0);
        if (a->phase && (i == EVO_WARDEN || i == EVO_MIMIC || i == EVO_MIMIC + 1)) {
            float range = i == EVO_WARDEN ? 4.0f : 1.6f;
            for (float step = .35f; step < range; step += .35f)
                GroundMark(x + a->aimX * step, y + a->aimY * step, .06f,
                           ThreatTierColor(CurrentRunThreatTier()), 1);
        }
        if (i == EVO_SPIDER && a->phase == 1) {
            GroundMark(a->aimX - .65f, a->aimY, .35f, 0xC5B0E2, 1);
            GroundMark(a->aimX + .65f, a->aimY, .35f, 0xC5B0E2, 1);
        }
    }
    for (int i = 0; i < EVO_ZONES; i++) {
        EvoZone *z = &evolution.zones[i];
        if (z->ttl <= 0)
            continue;
        GroundMark(z->x, z->y, z->radius, z->kind == EVO_ZONE_WEB ? 0xA3A9BA : 0xC7AE78,
                   z->kind == EVO_ZONE_COINS);
    }
}
void TacticalDrawWorld(void) {
    float ax,ay,bx,by;
    if(ProgressionAcousticLink(&ax,&ay,&bx,&by)) {
        float length=sqrtf(Dist2(ax,ay,bx,by));
        for(float step=0;step<length;step+=.55f)
            GroundMark(ax+(bx-ax)*step/length,ay+(by-ay)*step/length,.08f,0x86BFC0,1);
    }

    if (tactical.solvent.ttl > 0)
        GroundMark(tactical.solvent.x, tactical.solvent.y, 2.5f, 0xA1C1A7, 1);
    for (int i = 0; i < 2; i++) {
        if (tactical.acoustic[i].ttl <= 0) continue;
        ToolZone *z=&tactical.acoustic[i];
        GroundMark(z->x,z->y,ProgressionToolValue(PG_DAMP_RADIUS),0x8FC8D2,1);
        if(Dist2(posX,posY,z->x,z->y)<144 && HasLOS(posX,posY,z->x,z->y))
            DrawBakedProp(SPR_TOOL_BASE+TOOL_ACOUSTIC,z->x,z->y,0,.15f,1);
    }
    for (int i = 0; i < containerCount && i < 12; i++)
        if (tactical.dust[i] > 0 && containers[i].active && !containers[i].searched) {
            Container *c = &containers[i];
            GroundMark(c->x, c->y, .38f, 0xD7D4B8, 1);
            if (c->mimicHost) {
                float wave = .03f * sinf((float)g_timeSec * 3);
                GroundMark(c->x + wave, c->y, .23f + wave, 0xC7BCA3, 0);
            }
        }
}
float EvolutionPoseSignal(int sprite) {
    int actor = -1;
    if (sprite >= SPR_MON_HUNTER && sprite <= SPR_MON_HUNTER_SW)
        actor = EVO_HUNTER;
    else if (sprite >= SPR_MON_HUNTER2 && sprite <= SPR_MON_HUNTER2_SW)
        actor = EVO_HUNTER + 1;
    else if (sprite >= SPR_MON_HUNTER3 && sprite <= SPR_MON_HUNTER3_SW)
        actor = EVO_HUNTER + 2;
    else if (sprite >= SPR_MON_LIGHT && sprite <= SPR_MON_LIGHT_SW)
        actor = EVO_LIGHT;
    else if (sprite == SPR_PROP_SPIDER)
        actor = EVO_SPIDER;
    else if (sprite >= SPR_MON_LEECH && sprite <= SPR_MON_LEECH_SW)
        actor = EVO_COLLECTOR;
    else if (sprite == SPR_MON_SHADE)
        actor = EVO_SCAV;
    else if (sprite == SPR_MON_SPIDER || sprite == SPR_MON_WARDEN_CHASE)
        actor = EVO_WARDEN;
    else if (sprite == SPR_MON_MIMIC)
        actor = EVO_MIMIC;
    return actor >= 0 ? fminf(1, evolution.actors[actor].signal) : 0;
}

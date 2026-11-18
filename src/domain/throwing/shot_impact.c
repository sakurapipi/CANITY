#include "throwing_runtime.h"
#include "echo_marks.h"
#include "phase.h"
#include "player_api.h"
#include "loop_api.h"
#include "world_api.h"
#include "present_api.h"
#include "item_effects.h"
#include "throwing.h"
#include "audio.h"
#include "entity.h"
#include "state.h"
#include "tactical_tools.h"
#include "weapons.h"
#include "spider.h"
#include "warden.h"
#include "mimic.h"
#include "extras.h"
#include "lang.h"

extern int chargeBonus(float);

/* ==================================================================
 * 2026-11-05：**投石 → 破门石**（方案 B）
 * ------------------------------------------------------------------
 * 用户原话：「局里有个叫投石的技能的设计是怎么样的」→ 结论是它没有
 * 不可替代的功能（噪音输给诱饵、功能性输给烟雾/胶水）。
 *
 * 现在给它一件**别的道具都做不到**的事：
 *     丢过去的石头砸在锁箱上 → 锁直接崩开，跳过锁盘小游戏。
 *
 * 为什么这个功能该归它：
 *   · 物理上最自然 —— 抛物线扔一块石头砸锁，本来就是这件事
 *   · 锁盘现在有 6 种、开一把 8~15 秒，而它提供「用噪音换时间」的解法
 *   · 代价明确且够痛：20 格噪音，砸完就得跑
 *
 * 和「宝箱怪的扳手解法」是同一族设计：
 *   同一个障碍（锁/伪装），给两种完全不同的应对（动手 / 动脑）。
 * ================================================================ */
#define STONE_LOCK_RADIUS   1.35f
#define STONE_LOCK_NOISE   20.0f     /* 砸锁的噪音 —— 这是它的代价 */

/* 落点 x,y 附近有没有锁箱；有就砸开。返回 1 = 砸开了。
   ⚠ 只处理 `locked`，不碰 `mimicHost`（宝箱怪不是锁箱）。 */
/* 2026-11-06 仅测试用：StoneBreakLockAt 原来是 static，
   而"一次落点只解一把锁"这条契约必须能被直接验证（弹道落点不可控）。
   沿用 SaveTestBlob / SpiderDebugPlace 的先例，只导出不改行为。 */
static int StoneBreakLockAt(float x, float y);

int StoneBreakLockAtTest(float x, float y) { return StoneBreakLockAt(x, y); }

int ShotStoneLockNear(float x, float y, float radius) {
    int best = -1;
    float bestD = radius * radius;
    for (int i = 0; i < containerCount && i < MAX_CONTAINERS; i++) {
        if (!containers[i].active || !containers[i].locked || containers[i].mimicHost) continue;
        float d = Dist2(x, y, containers[i].x, containers[i].y);
        if (d < bestD && HasLOS(x, y, containers[i].x, containers[i].y)) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

static int StoneBreakLockAt(float x, float y) {
    int best = ShotStoneLockNear(x, y, STONE_LOCK_RADIUS);
    if (best < 0) return 0;

    containers[best].locked   = 0;      /* 锁崩开 */
    containers[best].lockType = 0;
    containers[best].lockCode = 0;
    containers[best].searched = 0;

    float boxX = containers[best].x, boxY = containers[best].y;
    MakeNoise(boxX, boxY, STONE_LOCK_NOISE);
    AudioPlayWorld(SFX_IMPACT, boxX, boxY, .90f, 20, 3);
    AudioPlayWorld(SFX_METAL_SCRAPE, boxX, boxY, .72f, 22, 3);
    SpawnParticles(boxX, boxY, 2);
    float kick = 1 / (1 + .24f * sqrtf(Dist2(posX, posY, boxX, boxY)));
    AddShake(.21f, .23f * kick);

    pickupMsg = (g_lang == LANG_ZH) ? "石头砸开了锁！" : "THE STONE BROKE THE LOCK!";
    pickupMsgTimer = 1.6f;
    {  uiDirty = 1; }
    return 1;
}

static void ShotImpact(int i, float x, float y) {
    const ShotSpec *sp = ShotSpecFor(shot[i].kind);
    int kind = shot[i].kind;
    float noise = sp->noise;
    if (sp->noiseScaled) noise += shot[i].charge * 12.0f;
    if (noise > 0) MakeNoise(x, y, noise);
    if (sp->effect == FX_TACTICAL) { TacticalImpact(kind, x, y); return; }
    if (sp->effect == FX_SMOKE) {
        SpawnSmoke(x, y);
        return;
    }
    if (sp->effect == FX_GLUE) {
        SpawnGlue(x, y);
        PlaySfx(SFX_GLUE, 0.85f);
        SpawnShards(x, y, shot[i].vx, shot[i].vy, shot[i].charge);
        return;
    }
    if (sp->effect == FX_ECHO_SCAN) {
        /* 回声弹：声呐扫描 —— 揭地形 + 标记资源 + 暴露活物 */



        int cx0 = (int)x, cy0 = (int)y;
        int rad = 14;
        int gy, gx;

        echoScanT = 6.0f;
        echoScanX = x;
        echoScanY = y;

        for (gy = cy0 - rad; gy <= cy0 + rad; gy++) {
            if (gy < 0 || gy >= MAP_HEIGHT) continue;
            for (gx = cx0 - rad; gx <= cx0 + rad; gx++) {
                int ddx, ddy;
                if (gx < 0 || gx >= MAP_WIDTH) continue;
                ddx = gx - cx0; ddy = gy - cy0;
                if (ddx * ddx + ddy * ddy > rad * rad) continue;
                explored[gy][gx] = 1;
            }
        }
        {
          EchoMarksScan(x, y, ECHO_RADIUS); }
        pickupMsg = (g_lang == LANG_ZH) ? "声呐扫描：资源已标出（有声）"
                                        : "SONAR: RESOURCES MARKED (LOUD)";
        pickupMsgTimer = 1.8f;
        {
          AudioPlayWorld(SFX_ECHO, x, y, 0.85f, 22.0f, 2); }
        return;
    }
    if (sp->effect == FX_DECOY) {
        if (!Blocked(x,y)) PlaceDecoy(x,y);
        else if (!Blocked(posX,posY)) PlaceDecoy(posX,posY);
        return;
    }
    if (sp->effect == FX_PHASE) {

        PhaseOnThrowLanded(kind, x, y);
        return;
    }

    SpawnShards(x, y, shot[i].vx, shot[i].vy, shot[i].charge);
}

static Monster *StoneTarget(int slot) {
    return slot < MON_HUNTER_MAX ? &monHunters[slot] : &lightBeasts[slot-MON_HUNTER_MAX];
}

void StoneHitTargets(Shot *stone, float radius, int landed) {
    for (int slot=0;slot<MON_HUNTER_MAX+LIGHTBEAST_MAX;slot++) {
        Monster *actor=StoneTarget(slot);
        unsigned bit=1u<<slot;
        if (!actor->active || (stone->hitMask & bit) ||
            Dist2(stone->x, stone->y, actor->x, actor->y) >= radius*radius ||
            !HasLOS(stone->x, stone->y, actor->x, actor->y)) continue;
        stone->hitMask |= bit;
        if (landed) {
            actor->stun=fmaxf(actor->stun,.35f);
        } else {
            ApplyDamage(actor,0,DT_SHARD,stone->x,stone->y);
            SpawnShards(stone->x,stone->y,0,0,.25f);
        }
    }
}

static void EndStoneFlight(int index) {
    Shot *stone = &shot[index];
    float x = stone->x, y = stone->y;
    if (!StoneBreakLockAt(x, y)) {
        MakeNoise(x, y, 8 + 12 * stone->charge);
        AudioPlayWorld(SFX_IMPACT, x, y, .45f + .25f * stone->charge, 16, 2);
        AddShake(.09f, .06f / (1 + .3f * sqrtf(Dist2(posX, posY, x, y))));
    }
    SpawnShards(x, y, stone->vx * .25f, stone->vy * .25f, stone->charge);
    if (CollectorActive() && Dist2(x,y,CollectorX(),CollectorY())<1.44f &&
        HasLOS(x,y,CollectorX(),CollectorY())) CollectorBurstBounty();
    if (!trainingMode) StoneHitTargets(stone,1.5f,1);
    stone->active = 0;
}

static int NearActor(float x, float y, float ax, float ay, float radius) {
    return Dist2(x,y,ax,ay) < radius*radius && HasLOS(x,y,ax,ay);
}

static int OtherActorNear(float x, float y, float radius) {
    if (SpiderActive() && NearActor(x,y,SpiderX(),SpiderY(),radius)) return 1;
    if (CollectorActive() && NearActor(x,y,CollectorX(),CollectorY(),radius)) return 1;
    if (ScavengerActive() && NearActor(x,y,ScavengerX(),ScavengerY(),radius)) return 1;
    if (WardenActive() && NearActor(x,y,WardenX(),WardenY(),radius)) return 1;
    for (int i=0;i<LEECH_MAX;i++)
        if (leeches[i].active && NearActor(x,y,leeches[i].x,leeches[i].y,radius)) return 1;
    for (int i=0;i<EXTRA_MAX;i++)
        if (extras[i].active && NearActor(x,y,extras[i].x,extras[i].y,radius)) return 1;
    for (int i=0;i<MIM_MAX;i++)
        if (mimics[i].state==MIM_ANGRY && NearActor(x,y,mimics[i].x,mimics[i].y,radius)) return 1;
    return 0;
}

int ThrowableActorNear(float x, float y, float radius) {
    for (int i=0;i<MON_HUNTER_MAX+LIGHTBEAST_MAX;i++) {
        Monster *actor=StoneTarget(i);
        if (actor->active && NearActor(x,y,actor->x,actor->y,radius)) return 1;
    }
    return OtherActorNear(x,y,radius);
}

int ShotHitsActor(Shot *projectile, const ShotSpec *spec) {
    const float radius = SHOT_RADIUS + .24f;
    if (projectile->z >= (projectile->kind==TOOL_SHOT_FIRST+TOOL_PRISM?2.6f:1.9f)) return 0;
    for (int slot=0;slot<MON_HUNTER_MAX+LIGHTBEAST_MAX;slot++) {
        Monster *actor=StoneTarget(slot);
        if (!actor->active || (projectile->ignoreTargetTime>0 && projectile->ignoreTarget==slot) ||
            !NearActor(projectile->x,projectile->y,actor->x,actor->y,radius)) continue;
        if (spec->effect==FX_GLUE) {
            actor->stun=fmaxf(actor->stun,2.f);
            actor->glued=1;
        }
        if (spec->effect!=FX_SHARDS) return 1;
        if (projectile->kind==9 && projectile->charge>.5f && !projectile->mirrorRebounded) {
            float vx=projectile->vx,vy=projectile->vy;
            float scale=.8f/sqrtf(1.f+.22f*.22f);
            projectile->vx=(-vx-vy*.22f)*scale;
            projectile->vy=(-vy+vx*.22f)*scale;
            projectile->charge=0;
            projectile->mirrorRebounded=1;
            projectile->ignoreTarget=slot;
            projectile->ignoreTargetTime=.18f;
            return 0;
        }
        if (spec->dmg<=0) return 1;
        int bonus=projectile->kind==2?3:projectile->kind==3?2:0;
        int damage=spec->dmg+chargeBonus(projectile->charge)*bonus;
        int type=spec->dmgType;
        if (projectile->kind==6) {
            if (projectile->dmg>0) damage=projectile->dmg;
            type=WeaponDamageType(curWeapon);
        }
        g_dmgIgnoreArmor=spec->ignoresArmor;
        ApplyDamage(actor,damage,type,projectile->x,projectile->y);
        g_dmgIgnoreArmor=0;
        if (projectile->pierce) {projectile->pierce=0;return 0;}
        return 1;
    }
    return OtherActorNear(projectile->x,projectile->y,radius);
}

void FinishShot(int index) {
    Shot *projectile=&shot[index];
    if (projectile->kind==1) { EndStoneFlight(index);return; }
    ShotImpact(index,projectile->x,projectile->y);
    if (ShotSpecFor(projectile->kind)->effect==FX_SHARDS)
        AudioPlayWorld(SFX_GLASS,projectile->x,projectile->y,.7f,16,2);
    projectile->active=0;
}

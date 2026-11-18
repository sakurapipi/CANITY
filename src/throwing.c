#include "throwing.h"
#include "throwing_runtime.h"
#include "tactical_tools.h"
#include "hints.h"
#include "audio.h"
#include "player_api.h"
#include "state.h"
#include "world_api.h"
#include "present_api.h"

extern int g_throwOk;

void ThrowItem(int type, float charge) {
    {  HintOnce(0); }   /* v7 B4: 第一次投掷 */
    g_throwOk = 0;
    if (charge < 0.1f) charge = 0.1f;
    if (charge > 1.0f) charge = 1.0f;

    float speed = SHOT_SPEED + (SHOT_MAX_SPEED - SHOT_SPEED) * charge;
    float angle = 0.35f * charge;  /* 抛物线角度：蓄力越高越平直 */

    if (type == 1) {
        /* A tap reaches the nearby lock; charging restores the longer arc. */
        float power = (charge - .1f) / .9f;
        const float stoneHorizontal = 6.75f + 8.45f * power;
        const float stoneLift = 2.15f - .8f * power;
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (shot[i].active) continue;
            shot[i].x = posX;
            shot[i].y = posY;
            shot[i].z = SHOT_HEIGHT;
            shot[i].vx = dirX * stoneHorizontal;
            shot[i].vy = dirY * stoneHorizontal;
            shot[i].vz = stoneLift;
            shot[i].life = 2.0f;
            shot[i].age = 0.0f;
            shot[i].charge = charge;
            shot[i].kind = 1;
            shot[i].dmg = 0;
            shot[i].pierce = 0;
            shot[i].mirrorRebounded = 0;
            shot[i].ignoreTarget = -1;
            shot[i].ignoreTargetTime = 0.0f;
            shot[i].hitMask = 0;
            shot[i].active = 1; g_throwOk = 1;
            break;
        }
        if (g_throwOk) {
            PlaySfx(SFX_THROW, .42f + .36f * charge);
            AddShake(.08f, .045f * charge);
        }
        return;
    }

    /* ---- v3.3 投掷物重做：2 烟雾 / 3 胶水 / 4 回声，全部走 shot 弹道 ----
       槽位：0 光球 / 1 投石 / 2 烟雾 / 3 胶水 / 4 回声 / 5 诱饵（C 键放置，不走这里）
       shot.kind 约定：1 投石 / 4 烟雾 / 8 胶水 / 10 回声 */
    /* ==================================================================
     * 2026-11-06（用户要求）：**两套相位投掷物**
     * ------------------------------------------------------------------
     * 槽 6 = 相位信标：落地成为回溯点，再按 Q 传送回去
     * 槽 7 = 相位锚  ：落地**立刻**把玩家拉过去
     *
     * 两者都走 shot 弹道（和烟雾/胶水同一套），所以飞行、撞墙、
     * 落地的物理都是现成的，不需要新写一套投掷流程。
     * kind 约定：12 = 信标 / 13 = 锚
     * ================================================================ */
    if (type == 5 || type == 6 || type == 7) {
        int kind = type==5?SHOT_KIND_DECOY:type==6?12:13;
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (shot[i].active) continue;
            shot[i].x = posX;
            shot[i].y = posY;
            shot[i].z = SHOT_HEIGHT;
            shot[i].vx = dirX * speed * cosf(angle);
            shot[i].vy = dirY * speed * cosf(angle);
            shot[i].vz = speed * sinf(angle);
            shot[i].life = 2.0f;
            shot[i].age = 0.0f;
            shot[i].charge = charge;
            shot[i].kind = kind;
            shot[i].dmg = 0;
            shot[i].pierce = 0;
            shot[i].mirrorRebounded = 0;
            shot[i].ignoreTarget = -1;
            shot[i].ignoreTargetTime = 0.0f;
            shot[i].hitMask = 0;
            shot[i].active = 1;
            g_throwOk = 1;
            break;
        }
        PlaySfx(SFX_THROW, 0.6f);
        return;
    }

    if (type >= 2) {
        int kind = (type == 2) ? 4 : (type == 3) ? 8 : 10;
        float itemSpeed = speed;
        float itemAngle = angle;
        float itemHeight = 1.0f;
        if (type == 2) {
            /* 烟雾弹：轻、抛得高、滞空久，落点好控制 */
            itemSpeed *= 1.05f;
            itemAngle = angle * 1.25f + 0.06f;
        } else if (type == 3) {
            /* 胶水瓶：重、低抛、飞得近 —— 它就是用来扔脚下的 */
            itemSpeed *= 0.88f;
            itemAngle *= 0.75f;
            itemHeight = 0.94f;
        } else {
            /* 回声弹：小、轻、平飞 */
            itemSpeed *= 1.10f;
            itemAngle *= 0.80f;
        }
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (shot[i].active) continue;
            shot[i].x = posX;
            shot[i].y = posY;
            shot[i].z = SHOT_HEIGHT * (type == 4 ? 0.9f : itemHeight);
            shot[i].vx = dirX * itemSpeed * cosf(itemAngle);
            shot[i].vy = dirY * itemSpeed * cosf(itemAngle);
            shot[i].vz = itemSpeed * sinf(itemAngle);
            shot[i].life = (type == 4) ? 1.6f : 2.0f;
            shot[i].age = 0.0f;
            shot[i].charge = charge;
            shot[i].kind = kind;
            shot[i].dmg = 0;
            shot[i].pierce = 0;   /* v3.3：镜片已删，不再有穿透弹 */
            shot[i].mirrorRebounded = 0;
            shot[i].ignoreTarget = -1;
            shot[i].ignoreTargetTime = 0.0f;
            shot[i].hitMask = 0;
            shot[i].active = 1; g_throwOk = 1;
            break;
        }
        PlaySfx(SFX_THROW, 0.6f);
        return;
    }

    /* 荧光棒：抛物线飞行，落地后转为静止发光体 */
    float flareSpeed = 6.5f + charge * 3.0f;   /* v1.11：6.5~9.5 格/秒（原来满蓄力太远） */
    /* v1.11：仰角从 0.4~0.7 压到 0.12~0.22 rad（7°~13°）。
       之前仰角太大，荧光棒升到 3~4 倍视高，而视角不能往上看 ——
       玩家看到的就是"扔出去先空两秒，第三秒球才从上面掉下来"。
       压平之后出手就能看到弧线。 */
    float flareAngle = 0.12f + charge * 0.10f;
    for (int i = 0; i < MAX_THROWN; i++) {
        if (thrown[i].active) continue;
        /* v1.10：出手点往前挪 0.45 格。
           原来与玩家同一位置 → 投影距离 ty≈0，被 ProjectSprite 的
           "太近就丢弃"挡掉，画面上就是"扔出去先消失一下再出现"。 */
        thrown[i].x = posX + dirX * 0.45f;
        thrown[i].y = posY + dirY * 0.45f;
        thrown[i].z = SHOT_HEIGHT * 0.45f;   /* v1.11：从腹部/胯部出手，不再从头顶 */
        thrown[i].vz = flareSpeed * sinf(flareAngle);
        thrown[i].vx = dirX * flareSpeed * cosf(flareAngle);
        thrown[i].vy = dirY * flareSpeed * cosf(flareAngle);
        thrown[i].type = 0;        /* 0=荧光棒 */
        thrown[i].life = FLARE_LIFE_SECONDS;    /* 落地后发光时长 */
        thrown[i].active = 1; g_throwOk = 1;
        break;
    }
    PlaySfx(SFX_THROW, 0.6f);  /* 投掷专用音效：呼啸+出手闷响 */
}

void UpdateShots(float dt) {
    for (int i=0;i<MAX_SHOTS;i++) {
        Shot *projectile=&shot[i];
        if (!projectile->active) continue;
        projectile->life-=dt;
        projectile->age+=dt;
        projectile->ignoreTargetTime=fmaxf(0,projectile->ignoreTargetTime-dt);

        const ShotSpec *spec=ShotSpecFor(projectile->kind);
        int stone=projectile->kind==1;
        int tactical=spec->effect==FX_TACTICAL;
        /* Preserve the higher gravity of the legacy utility throws. */
        float verticalScale=stone || tactical ? 1.f : 2.f;
        float gravity=stone?14.2f:tactical?
            (projectile->kind==TOOL_SHOT_FIRST+TOOL_PRISM?GRAVITY*.08f:GRAVITY):16.f+GRAVITY;
        float stepLength=hypotf(hypotf(projectile->vx,projectile->vy),verticalScale*projectile->vz)*dt;
        int steps=(int)(stepLength/.12f)+1;
        float stepTime=dt/steps;
        for (int step=0;step<steps && projectile->active;step++) {
            float nx=projectile->x+projectile->vx*stepTime;
            float ny=projectile->y+projectile->vy*stepTime;
            if (tactical ? Blocked(nx,ny) : ShotBlocked(nx,ny)) { FinishShot(i);break; }
            projectile->x=nx;
            projectile->y=ny;
            projectile->z+=(projectile->vz-.5f*gravity*stepTime)*stepTime*verticalScale;
            projectile->vz-=gravity*stepTime;

            if (projectile->z<=0) { projectile->z=0;FinishShot(i);break; }
            if (stone) {
                if (projectile->z<2.1f && ShotStoneLockNear(nx,ny,.57f)>=0) { FinishShot(i);break; }
                if (projectile->z<1.9f) StoneHitTargets(projectile,SHOT_RADIUS+.24f,0);
            } else if (ShotHitsActor(projectile,spec)) {
                FinishShot(i);
            }
        }
        if (projectile->active && projectile->life<=0) FinishShot(i);
    }

    /* 更新玻璃金币 */
    for (int i = 0; i < MAX_SHOTS * SHARD_COUNT; i++) {
        if (!shards_debris[i].active) continue;
        shards_debris[i].age += dt;
        shards_debris[i].life -= dt;
        shards_debris[i].x += shards_debris[i].vx * dt;
        shards_debris[i].y += shards_debris[i].vy * dt;
        shards_debris[i].z += shards_debris[i].vz * dt;
        shards_debris[i].vz -= GRAVITY * dt;
        if (shards_debris[i].z <= 0.0f) {
            shards_debris[i].z = 0.0f;
            shards_debris[i].vz *= -0.3f;  /* 反弹衰减 */
            shards_debris[i].vx *= 0.7f;
            shards_debris[i].vy *= 0.7f;
        }
        if (shards_debris[i].life <= 0.0f) {
            shards_debris[i].active = 0;
        }
    }
}

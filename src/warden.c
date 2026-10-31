/*
 * Canity — THE WARDEN 守门人（v7 方案 B1）
 *
 * 设计（见方案 v7 第六部分 ⑥）
 * ────────────────────────────
 * 它把「通电 → 走到出口」这段最无脑的路，变成唯一一段需要读地图的路。
 *
 *   出现      配电箱通电之后
 *   巡逻      配电箱 ↔ 出口 之间来回走（30~40 格的路）
 *   速度      0.7 格/秒 —— 全场最慢
 *   感知      只对「快速移动」有反应
 *               冲刺     6 格发现
 *               正常走路 3 格发现
 *               站着不动 **永远发现不了**
 *
 *   堵门      被它发现 → 走到出口站在门口
 *             你走进出口区域 → 被送回本层起点（不掉理智、不是死亡）
 *             持续 25 秒
 *   解除后    ① 强制走向离出口最远的巡逻端点（≥20 格）
 *             ② 45 秒内不会再次堵门
 *
 * 不会发生：不追人 / 不一击必杀 / 不永久堵门
 */
#include <math.h>
#include "common.h"
#include "game.h"
#include "state.h"
#include "ai_common.h"
#include "audio.h"
#include "lang.h"
#include "warden.h"

#define WARDEN_SPEED        0.7f    /* 格/秒 */
#define WARDEN_SIGHT_SPRINT 6.0f    /* 冲刺时多少格内发现 */
#define WARDEN_SIGHT_WALK   3.0f    /* 走路时多少格内发现 */
#define WARDEN_BLOCK_TIME   25.0f   /* 堵门持续 */
#define WARDEN_COOLDOWN     45.0f   /* 解除后多少秒内不再堵门 */
#define WARDEN_RETREAT      20.0f   /* 解除后至少离开出口多少格 */
#define WARDEN_EXIT_R       3.0f    /* 进出口区域的判定半径 */
#define WARDEN_MOVE_EPS     0.02f   /* 每帧位移超过这个才算「在动」 */

typedef struct {
    int   active;
    float x, y;
    float tx, ty;          /* 当前巡逻目标端点 */
    int   headingToExit;   /* 1 = 正朝出口走 */
    float blockT;          /* >0 = 正在堵门 */
    float coolT;           /* >0 = 堵门冷却中 */
    float sightHold;       /* 连续看见多久 */
} Warden;

Warden warden;

int   WardenActive(void)  { return warden.active; }
float WardenX(void)       { return warden.x; }
float WardenY(void)       { return warden.y; }
int   WardenBlocking(void){ return warden.blockT > 0.0f; }

void WardenReset(void) {
    warden.active = 0;
    warden.x = warden.y = 0.0f;
    warden.tx = warden.ty = 0.0f;
    warden.headingToExit = 0;
    warden.blockT = 0.0f;
    warden.coolT = 0.0f;
    warden.sightHold = 0.0f;
}

/* 通电之后才出现：站在配电箱旁边，开始往出口走 */
void WardenSpawnIfPowered(void) {
    extern int breakerOpen;
    extern Breaker breaker;      /* 配电箱 */
    extern float exitX, exitY;    /* 出口坐标 */
    WardenReset();
    if (!breakerOpen) return;
    if (!breaker.active) return;

    /* 起点 = 配电箱旁边 1.5 格，找一个能站的地方 */
    float ox = 0.0f, oy = 0.0f;
    int found = 0;
    for (int t = 0; t < 60; t++) {
        float ang = (float)(rand() % 6283) / 1000.0f;
        float x = breaker.x + cosf(ang) * 1.6f;
        float y = breaker.y + sinf(ang) * 1.6f;
        if (x < 1.2f || y < 1.2f || x > MAP_WIDTH - 1.2f || y > MAP_HEIGHT - 1.2f) continue;
        if (Blocked(x, y)) continue;
        ox = x; oy = y; found = 1; break;
    }
    if (!found) return;

    warden.x = ox;
    warden.y = oy;
    warden.tx = exitX;
    warden.ty = exitY;
    warden.headingToExit = 1;
    warden.active = 1;
    PlaySfx(SFX_STEP, 0.5f);
}

/* 把玩家送回本层起点（不是死亡，不掉理智） */
static void SendPlayerToStart(void) {
    extern float posX, posY;
    extern int   resetInputState;
    posX = 2.5f;
    posY = 2.5f;
    /* 起点可能被墙占 —— 附近找一个空的 */
    if (Blocked(posX, posY)) {
        for (int r = 1; r < 8 && Blocked(posX, posY); r++) {
            posX = 2.5f + (float)r;
            posY = 2.5f;
        }
    }
    resetInputState = 1;
    AddHitFlash(0.6f);
    AddShake(0.20f, 0.16f);
    PlaySfx(SFX_SCREAM, 0.55f);
    sprintf(pickupMsg, g_lang == LANG_ZH ? "被送回了起点" : "SENT BACK TO START");
    pickupMsgTimer = 2.0f;
}

void UpdateWarden(float dt) {
    if (!warden.active) return;

    extern float posX, posY;
    extern float exitX, exitY;

    if (warden.coolT > 0.0f) warden.coolT -= dt;

    /* ---- 堵门中 ---- */
    if (warden.blockT > 0.0f) {
        warden.blockT -= dt;
        if (warden.blockT <= 0.0f) {
            /* 解除：强制走向离出口最远的端点 */
            warden.blockT = 0.0f;
            warden.coolT = WARDEN_COOLDOWN;
            float dx = warden.x - exitX, dy = warden.y - exitY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 0.01f) { dx = 1.0f; dy = 0.0f; d = 1.0f; }
            warden.tx = exitX + (dx / d) * WARDEN_RETREAT;
            warden.ty = exitY + (dy / d) * WARDEN_RETREAT;
            warden.headingToExit = 0;
            if (Blocked(warden.tx, warden.ty)) {
                warden.tx = exitX + (dx / d) * 8.0f;
                warden.ty = exitY + (dy / d) * 8.0f;
            }
        } else {
            /* 玩家走进出口区域 → 送回起点 */
            float pdx = posX - exitX, pdy = posY - exitY;
            if (pdx * pdx + pdy * pdy < WARDEN_EXIT_R * WARDEN_EXIT_R) {
                SendPlayerToStart();
            }
            /* 堵门期间它站在门口不动 */
            return;
        }
    }

    /* ---- 感知：只对「正在移动」有反应 ---- */
    {
        float mdx = posX - warden.x, mdy = posY - warden.y;
        float md2 = mdx * mdx + mdy * mdy;

        /* 本帧位移：用玩家速度近似判断 —— 冲刺/走路/静止 */
        extern float playerMoveSpeedNow;      /* 由 game.c 每帧写入 */
        int  sprinting = (playerMoveSpeedNow > 2.4f);
        int  moving    = (playerMoveSpeedNow > WARDEN_MOVE_EPS);
        float sight = sprinting ? WARDEN_SIGHT_SPRINT
                                : (moving ? WARDEN_SIGHT_WALK : 0.0f);

        if (sight > 0.0f && md2 < sight * sight) {
            warden.sightHold += dt;
            if (warden.sightHold >= 0.35f && warden.coolT <= 0.0f) {
                /* 发现 → 开始堵门 */
                warden.blockT = WARDEN_BLOCK_TIME;
                warden.sightHold = 0.0f;
                warden.tx = exitX;
                warden.ty = exitY;
                PlaySfx(SFX_HEART, 0.8f);
                {
                    sprintf(pickupMsg, g_lang == LANG_ZH ? "守卫堵住了出口" : "THE WARDEN BLOCKS THE EXIT");
                    pickupMsgTimer = 2.0f;
                }
            }
        } else {
            warden.sightHold = 0.0f;
        }
    }

    /* ---- 巡逻：朝当前端点走 ---- */
    {
        float dx = warden.tx - warden.x, dy = warden.ty - warden.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 0.8f) {
            /* 到端点 → 换另一头 */
            if (warden.headingToExit) {
                extern Breaker breaker;
                warden.tx = breaker.x;
                warden.ty = breaker.y;
                warden.headingToExit = 0;
            } else {
                warden.tx = exitX;
                warden.ty = exitY;
                warden.headingToExit = 1;
            }
        } else {
            AIMoveToward(&warden.x, &warden.y, warden.tx, warden.ty, WARDEN_SPEED, dt);
        }
    }
}

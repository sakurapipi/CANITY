/*
 * Canity — THE SCAVENGER 清道夫（v7 新怪）
 *
 * 设计目标（见 方案 v7 第六部分 ⑤）
 * ────────────────────────────────
 * 它闻的不是位置，是**「你待太久了」**。
 *
 * 触发（满足任一）
 *   · 本层累计停留 > 120 秒
 *   · 连续躲进藏身点 2 次（中间没搜过箱子）
 *
 * 到达玩家待最久的那个房间后
 *   玩家在藏身点里 → 提前 1.5 秒给「柜门被拉开」音效 → 拉出
 *                    扣 15 理智，藏身点本层作废
 *   玩家不在       → 吃这个房间里未搜的箱子，每箱 3 秒
 *
 * 为什么这样设计
 *   藏身点现在是「100% 安全的紧急按钮」，连续用两次就有人来开门。
 *   而且它不是倒计时处刑 —— 它慢（1.0 格/秒）、要走路过来，
 *   玩家有一整层的时间可以跑掉。
 */
#include <math.h>
#include "state.h"   /* v8: 走统一的全局状态入口 */
#include "common.h"
#include "game.h"
#include "entity.h"
#include "audio.h"
#include "lang.h"
#include "skills.h"

#define SCAV_TRIGGER_TIME  120.0f   /* 本层停留多少秒触发 */
#define SCAV_SPEED          1.0f    /* 格/秒，慢 */
#define SCAV_GRAB_WARN      1.5f    /* 拉出前的预警秒数 */
#define SCAV_SANITY_LOSS   15.0f    /* 被抓走扣多少理智（v7：25 → 15） */
#define SCAV_EAT_TIME       3.0f    /* 吃一个箱子几秒 */

typedef struct {
    int   active;
    float x, y;
    float tx, ty;         /* 目标房间中心 */
    float warnT;          /* >0 = 正在拉柜门 */
    float eatT;           /* 正在吃箱子的进度 */
    int   eating;         /* 正在吃的箱子下标，-1 = 没在吃 */
} Scavenger;

Scavenger scavenger;

int   ScavengerActive(void)  { return scavenger.active; }
float ScavengerX(void)       { return scavenger.x; }
float ScavengerY(void)       { return scavenger.y; }
float ScavengerWarnT(void)   { return scavenger.warnT; }

void ScavengerReset(void) {
    scavenger.active = 0;
    scavenger.x = scavenger.y = 0.0f;
    scavenger.tx = scavenger.ty = 0.0f;
    scavenger.warnT = 0.0f;
    scavenger.eatT = 0.0f;
    scavenger.eating = -1;
}

/* 朝目标走一步；撞墙就沿墙滑动（不会原地卡死） */
static void stepToward(float* x, float* y, float tx, float ty, float sp, float dt) {
    float dx = tx - *x, dy = ty - *y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return;
    float nx = *x + (dx / d) * sp * dt;
    float ny = *y + (dy / d) * sp * dt;
    if (!Blocked(nx, ny)) { *x = nx; *y = ny; return; }
    /* 沿墙滑动：只走 X 或只走 Y */
    if (!Blocked(nx, *y)) { *x = nx; return; }
    if (!Blocked(*x, ny)) { *y = ny; return; }
    /* 两侧都堵 —— 平移一点绕过去 */
    float sx = *x + (dy / d) * sp * dt;
    float sy = *y - (dx / d) * sp * dt;
    if (!Blocked(sx, sy)) { *x = sx; *y = sy; }
}

/* 在玩家周围 10~16 格找一个起点 */
static int findStart(float* ox, float* oy) {
    for (int t = 0; t < 100; t++) {
        float ang = (float)(rand() % 6283) / 1000.0f;
        float d = 10.0f + (float)(rand() % 600) / 100.0f;
        float x = posX + cosf(ang) * d;
        float y = posY + sinf(ang) * d;
        if (x < 1.5f || y < 1.5f || x > MAP_WIDTH - 1.5f || y > MAP_HEIGHT - 1.5f) continue;
        if (Blocked(x, y)) continue;
        *ox = x; *oy = y;
        return 1;
    }
    return 0;
}

/* 触发检查 —— 每帧调用，内部只做轻量判断 */
void ScavengerTickTrigger(float dt) {
    extern float floorTime;
    extern int   hideStreak;
    if (scavenger.active) return;

    int timeUp  = (floorTime >= SCAV_TRIGGER_TIME);
    int hideUp  = (hideStreak >= 2);
    if (!timeUp && !hideUp) return;

    float x, y;
    if (!findStart(&x, &y)) return;

    /* 目标 = 玩家当前所在位置（近似「待最久的房间」） */
    scavenger.x = x;
    scavenger.y = y;
    scavenger.tx = posX;
    scavenger.ty = posY;
    scavenger.warnT = 0.0f;
    scavenger.eatT = 0.0f;
    scavenger.eating = -1;
    scavenger.active = 1;
    PlaySfx(SFX_GLASS, 0.45f);
    pickupMsg = (g_lang == LANG_ZH) ? "有东西在靠近…" : "SOMETHING IS COMING";
    pickupMsgTimer = 2.0f;
}

void UpdateScavenger(float dt) {
    if (!scavenger.active) return;

    float dx = scavenger.tx - scavenger.x, dy = scavenger.ty - scavenger.y;
    float d = sqrtf(dx * dx + dy * dy);

    /* ---- ① 拉柜门 ---- */
    if (scavenger.warnT > 0.0f) {
        scavenger.warnT -= dt;
        if (scavenger.warnT <= 0.0f) {
            extern int hiddenIn;
            if (hiddenIn >= 0) {
                sanity -= SCAV_SANITY_LOSS * RunDamageTakenMul();
                if (sanity < 0.0f) sanity = 0.0f;
                AddHitFlash(0.85f);
                AddShake(0.28f, 0.20f);
                /* 藏身点本层作废 */
                {   /* 直接清掉藏身状态（没有现成的退出函数，就地处理） */
                    extern int hiddenIn;
                    hiddenIn = -1;
                    extern int resetInputState;
                    resetInputState = 1;
                }
                PlaySfx(SFX_SCREAM, 0.7f);
                pickupMsg = (g_lang == LANG_ZH) ? "它把柜门拉开了！" : "IT OPENED THE LOCKER!";
                pickupMsgTimer = 2.2f;
            }
            scavenger.warnT = 0.0f;
            scavenger.active = 0;      /* 干完活就走 */
            return;
        }
        return;
    }

    /* ---- ② 已经在房间附近 ---- */
    if (d < 1.6f) {
        extern int hiddenIn;
        if (hiddenIn >= 0) {
            /* 玩家躲在里面 → 开始拉柜门（先给 1.5 秒预警） */
            scavenger.warnT = SCAV_GRAB_WARN;
            PlaySfx(SFX_STEP, 0.9f);
            pickupMsg = (g_lang == LANG_ZH) ? "柜门被抓住了！" : "THE LOCKER IS GRABBED!";
            pickupMsgTimer = 1.4f;
            return;
        }
        /* 玩家不在 → 吃箱子。
           **关键安全规则**：只吃 content == 0（空箱）。
           content == 1 是保险丝（通关必需），content == 2 是电池 ——
           这两种一律跳过。宁可它空手走人，也绝不让玩家因为箱子被吃掉而卡关。 */
        if (scavenger.eating < 0) {
            extern Container containers[MAX_CONTAINERS];
            extern int containerCount;
            int best = -1;
            float bestD = 1e9f;
            for (int ci = 0; ci < containerCount; ci++) {
                if (!containers[ci].active) continue;
                if (containers[ci].searched) continue;
                if (containers[ci].content != 0) continue;   /* ← 保险丝 / 电池 一律不吃 */
                float cdx = containers[ci].x - scavenger.x;
                float cdy = containers[ci].y - scavenger.y;
                float cd2 = cdx * cdx + cdy * cdy;
                if (cd2 > 36.0f) continue;                   /* 只吃身边的（6 格内） */
                if (cd2 < bestD) { bestD = cd2; best = ci; }
            }
            if (best < 0) {
                /* 这个房间没有能吃的东西 → 白跑一趟，走人 */
                scavenger.active = 0;
                return;
            }
            scavenger.eating = best;
            scavenger.eatT = 0.0f;
            pickupMsg = (g_lang == LANG_ZH) ? "它在翻箱子！" : "IT IS RIFLING YOUR CRATES!";
            pickupMsgTimer = 1.6f;
        }

        scavenger.eatT += dt;
        if (scavenger.eatT >= SCAV_EAT_TIME) {
            extern Container containers[MAX_CONTAINERS];
            int ci = scavenger.eating;
            if (ci >= 0 && ci < MAX_CONTAINERS && containers[ci].active) {
                /* 再确认一次：绝不吞掉保险丝 / 电池 */
                if (containers[ci].content == 0) {
                    containers[ci].searched = 1;   /* 被吃空 */
                }
            }
            PlaySfx(SFX_GLASS, 0.5f);
            scavenger.eatT = 0.0f;
            scavenger.eating = -1;
            scavenger.active = 0;      /* 吃完一个就走 */
        }
        return;
    }

    /* ---- ③ 走过去 ---- */
    stepToward(&scavenger.x, &scavenger.y, scavenger.tx, scavenger.ty, SCAV_SPEED, dt);
}

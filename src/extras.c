#include <math.h>
#include "extras.h"
#include "common.h"
#include "entity.h"
#include "game.h"
#include "audio.h"
#include "skills.h"

Extra extras[EXTRA_MAX];

void ExtrasReset(void) {
    for (int i = 0; i < EXTRA_MAX; i++) {
        extras[i].active = 0;
        extras[i].kind = -1;
        extras[i].timer = 0.0f;
        extras[i].cool = 0;
    }
}

/* 在离玩家 4~9 格、且在空地上的位置找点 */
static int findSpot(float* ox, float* oy, float minD, float maxD) {
    for (int t = 0; t < 80; t++) {
        float ang = (float)(rand() % 6283) / 1000.0f;
        float d = minD + (float)(rand() % 1000) / 1000.0f * (maxD - minD);
        float x = posX + cosf(ang) * d;
        float y = posY + sinf(ang) * d;
        if (x < 1.2f || y < 1.2f || x > MAP_WIDTH - 1.2f || y > MAP_HEIGHT - 1.2f) continue;
        if (Blocked(x, y)) continue;
        *ox = x;
        *oy = y;
        return 1;
    }
    return 0;
}

void ExtrasSpawnForLevel(int level) {
    ExtrasReset();
    /* v2.0：第 1 层就出怪，数量随层数线性涨到 6 只 */
    int n = 1 + (level - 1) / 3;
    if (n < 1) n = 1;
    if (n > EXTRA_MAX) n = EXTRA_MAX;

    for (int i = 0; i < n; i++) {
        float x, y;
        if (!findSpot(&x, &y, 5.0f, 10.0f)) continue;
        extras[i].x = x;
        extras[i].y = y;
        extras[i].active = 1;
        extras[i].kind = rand() % 3;
        extras[i].timer = 1.0f + (float)(rand() % 200) / 100.0f;
        extras[i].cool = 0;
    }
}

void UpdateExtras(float dt) {
    for (int i = 0; i < EXTRA_MAX; i++) {
        Extra* e = &extras[i];
        if (!e->active) continue;
        if (e->cool > 0) e->cool--;
        e->timer -= dt;

        float dx = posX - e->x, dy = posY - e->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 0.001f) d = 0.001f;
        float nx = dx / d, ny = dy / d;

        switch (e->kind) {
        case 0: {   /* SHADE：只在关灯时存在；关灯时直线逼近 */
            int lightOn = (flashlightOn && battery > 0.0f);
            if (lightOn) break;                 /* 开灯 = 它不存在 */
            float sp = 1.35f * dt;
            if (d > 0.55f) {
                float tx = e->x + nx * sp, ty = e->y + ny * sp;
                if (!Blocked(tx, ty)) { e->x = tx; e->y = ty; }
            }
            break;
        }
        case 1: {   /* SPIDER：极快，但只在计时器循环里冲刺 */
            if (e->timer <= 0.0f) {
                e->timer = 1.6f;
                float sp = 1.4f;               /* 一次冲 1.4 格 */
                float tx = e->x + nx * sp, ty = e->y + ny * sp;
                if (!Blocked(tx, ty)) { e->x = tx; e->y = ty; }
            }
            break;
        }
        default: {  /* ECHO：不动，定期闪现到玩家附近 */
            if (e->timer <= 0.0f) {
                e->timer = 7.5f;
                float x, y;
                if (findSpot(&x, &y, 2.5f, 5.0f)) { e->x = x; e->y = y; }
                if (d < 6.0f) PlaySfx(SFX_STEP, 0.25f);
            }
            break;
        }
        }

        /* 接触伤害：扣理智 + 把怪弹开，避免连续每帧扣 */
        dx = posX - e->x; dy = posY - e->y;
        d = sqrtf(dx * dx + dy * dy);
        if (d < 0.62f && e->cool <= 0) {
            float dmg = (e->kind == 0) ? 5.0f : (e->kind == 1 ? 4.0f : 6.0f);   /* v2.0：怪变多了，单只伤害下调 */
            sanity -= dmg * RunDamageTakenMul();
            if (sanity < 0.0f) sanity = 0.0f;
            AddHitFlash(0.7f);
            AddShake(0.22f, 0.16f);
            PlaySfx(SFX_HEART, 0.9f);
            e->cool = 90;                       /* 约 0.75 秒 */
            /* 弹开到 2 格之外 */
            float bx = (d < 0.001f) ? 1.0f : -dx / d;
            float by = (d < 0.001f) ? 0.0f : -dy / d;
            e->x += bx * 2.0f;
            e->y += by * 2.0f;
            if (Blocked(e->x, e->y)) {
                float x, y;
                if (findSpot(&x, &y, 4.0f, 8.0f)) { e->x = x; e->y = y; }
            }
        }
    }
}

int ExtrasCount(void) { return EXTRA_MAX; }

const Extra* ExtraAt(int i) {
    if (i < 0 || i >= EXTRA_MAX) return NULL;
    return &extras[i];
}

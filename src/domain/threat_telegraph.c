/* src/domain/threat_telegraph.c —— 威胁预告（第 3 轮）
 *
 * 纯表现层：只读怪的位置，绝不改 AI 状态。
 */
#include "common.h"
#include "threat_telegraph.h"
#include "audio.h"
#include "entity.h"

/* 三档距离边界（格）。玩家视野大约 8~10 格，所以"远"排在视野外， */
/* 让玩家先听见、再看见 —— 顺序反了就没有预告效果。 */
#define TT_FAR_R     12.0f
#define TT_MID_R      7.0f

/* 各档冷却（秒）与音效 */
static const float kCooldown[3] = { 6.0f, 3.5f, 2.0f };

static float s_cd[3] = { 0.0f, 0.0f, 0.0f };
static float s_nearest = 1.0e9f;
static int   s_band = -1;
static float s_flickerT = 0.0f;      /* 闪烁剩余时间 */
static float s_flickerPhase = 0.0f;

void TelegraphReset(void) {
    s_cd[0] = s_cd[1] = s_cd[2] = 0.0f;
    s_nearest = 1.0e9f;
    s_band = -1;
    s_flickerT = 0.0f;
    s_flickerPhase = 0.0f;
}

float TelegraphNearestDist(void) { return s_nearest; }
int   TelegraphBand(void)        { return s_band; }

float TelegraphFlicker(void) {
    if (s_flickerT <= 0.0f) return 0.0f;
    /* 用相位做出"忽明忽暗"，而不是一个平滑的调光 */
    float v = 0.5f + 0.5f * sinf(s_flickerPhase * 26.0f);
    float fade = (s_flickerT > 0.4f) ? 1.0f : (s_flickerT / 0.4f);
    return v * fade * 0.5f;
}

/* 找一个"活着的、离玩家最近"的怪的距离和坐标 */
static void nearestThreat(float* outD, float* outX, float* outY) {
    /* 注意：MON_HUNTER_MAX / LIGHTBEAST_MAX 是宏（common.h / entity.h），
       不能再 extern 声明 —— 展开成 `extern int 3;` 会语法错。
       头文件已经提供了 monHunters[] / lightBeasts[] / parasite 的声明。 */
    float best = 1.0e9f, bx = 0.0f, by = 0.0f;
    int i;

    for (i = 0; i < MON_HUNTER_MAX; i++) {
        if (!monHunters[i].active) continue;
        {
            float dx = monHunters[i].x - posX, dy = monHunters[i].y - posY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < best) { best = d; bx = monHunters[i].x; by = monHunters[i].y; }
        }
    }
    for (i = 0; i < LIGHTBEAST_MAX; i++) {
        if (!lightBeasts[i].active) continue;
        {
            float dx = lightBeasts[i].x - posX, dy = lightBeasts[i].y - posY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < best) { best = d; bx = lightBeasts[i].x; by = lightBeasts[i].y; }
        }
    }
    if (parasite.active) {
        float dx = parasite.x - posX, dy = parasite.y - posY;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < best) { best = d; bx = parasite.x; by = parasite.y; }
    }
    *outD = best;
    *outX = bx;
    *outY = by;
}

static const int kSfx[3] = { SFX_STEP, SFX_SCAV_STEP, SFX_HEART };

int TelegraphTick(float dt) {
    float d, tx, ty;
    int band, i;

    nearestThreat(&d, &tx, &ty);
    s_nearest = d;

    if (d > TT_FAR_R) {
        s_band = -1;
        for (i = 0; i < 3; i++) if (s_cd[i] > 0.0f) s_cd[i] -= dt;
        if (s_flickerT > 0.0f) s_flickerT -= dt;
        return 0;
    }

    band = (d < TT_MID_R) ? 2 : 1;

    /* ==================================================================
     * 看见它了就不预告 —— 预告的意义是"还没看见"。
     * 判定：有视线（没被墙挡）**且**它在玩家前方 ~60° 内。
     * 所以转身背对它，预告会恢复 —— 这正是"背后有东西"的恐怖来源。
     * ================================================================ */
    {

        float vx = tx - posX, vy = ty - posY;
        float vd = sqrtf(vx * vx + vy * vy);
        int inFront = 0;
        if (vd > 0.001f) {
            float dot = (vx / vd) * dirX + (vy / vd) * dirY;
            inFront = (dot > 0.5f);      /* 约 60° 半角 */
        }
        if (inFront && HasLOS(posX, posY, tx, ty)) {
            s_band = -1;
            return 0;
        }
    }

    s_band = band;
    for (i = 0; i < 3; i++) if (s_cd[i] > 0.0f) s_cd[i] -= dt;

    if (s_cd[band] <= 0.0f) {
        /* 越近越响；远档压得很低，只当"背景里有东西" */
        float vol = (band == 2) ? 0.55f : (band == 1 ? 0.35f : 0.20f);
        AudioPlayWorld(kSfx[band], tx, ty, vol, 22.0f, 1);
        s_cd[band] = kCooldown[band];
        if (band == 2) s_flickerT = 1.2f;   /* 近档才带手电闪烁 */
    }

    if (s_flickerT > 0.0f) {
        s_flickerT -= dt;
        s_flickerPhase += dt;
    }
    return 1;
}

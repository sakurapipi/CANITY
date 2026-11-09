/*
 * Canity — 拾光虫实现（v2.4.1 第 11 轮）
 * 头部注释见 include/neutral.h。
 */
#include "common.h"
#include "neutral.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"

typedef struct {
    int   active;
    float x, y;
    float tx, ty;
    float wanderT;
    int   scared;
} Mote;

static Mote s_mote;

extern int   worldMap[MAP_HEIGHT][MAP_WIDTH];
extern float posX, posY;

void NeutralReset(void) { memset(&s_mote, 0, sizeof(s_mote)); }

int   NeutralCount(void) { return s_mote.active ? 1 : 0; }
int   NeutralScared(void) { return s_mote.scared; }
float NeutralX(int i) { (void)i; return s_mote.x; }
float NeutralY(int i) { (void)i; return s_mote.y; }

void NeutralDebugPlace(float x, float y) {
    memset(&s_mote, 0, sizeof(s_mote));
    s_mote.active = 1;
    s_mote.x = x; s_mote.y = y;
    s_mote.tx = x; s_mote.ty = y;
    s_mote.wanderT = 999.0f;
}

int NeutralTrySpawn(int level) {
    if (level < MOTE_MIN_LEVEL) return 0;
    if (rand() % 100 >= MOTE_APPEAR_CHANCE) return 0;
    for (int t = 0; t < 200; t++) {
        int x = 2 + rand() % (MAP_WIDTH - 4);
        int y = 2 + rand() % (MAP_HEIGHT - 4);
        if (worldMap[y][x]) continue;
        float d2 = Dist2((float)x + 0.5f, (float)y + 0.5f, posX, posY);
        if (d2 < 25.0f) continue;      /* 别一生成就贴脸 */
        NeutralDebugPlace((float)x + 0.5f, (float)y + 0.5f);
        return 1;
    }
    return 0;
}

static void MoveTo(float dt, float tx, float ty, float speed) {
    float dx = tx - s_mote.x, dy = ty - s_mote.y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.02f) return;
    float step = speed * dt;
    if (step > d) step = d;
    float nx = s_mote.x + dx / d * step;
    float ny = s_mote.y + dy / d * step;
    int cx = (int)nx, cy = (int)ny;
    if (cx >= 1 && cy >= 1 && cx < MAP_WIDTH - 1 && cy < MAP_HEIGHT - 1 && !worldMap[cy][cx]) {
        s_mote.x = nx;
        s_mote.y = ny;
    }
}

static void PickWander(void) {
    int cx = (int)s_mote.x, cy = (int)s_mote.y;
    for (int t = 0; t < 20; t++) {
        int nx = cx + (rand() % 7) - 3;
        int ny = cy + (rand() % 7) - 3;
        if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
        if (worldMap[ny][nx]) continue;
        s_mote.tx = (float)nx + 0.5f;
        s_mote.ty = (float)ny + 0.5f;
        return;
    }
}

void NeutralTick(float dt) {
    if (!s_mote.active) return;

    /* ---- ① 受惊：玩家进到 MOTE_FLEE_RADIUS 就背离逃走 ---- */
    float pd2 = Dist2(s_mote.x, s_mote.y, posX, posY);
    if (pd2 < MOTE_FLEE_RADIUS * MOTE_FLEE_RADIUS) {
        s_mote.scared = 1;
        float dx = s_mote.x - posX, dy = s_mote.y - posY;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 0.01f) { dx = 1.0f; dy = 0.0f; d = 1.0f; }
        MoveTo(dt, s_mote.x + dx / d * 3.0f, s_mote.y + dy / d * 3.0f, MOTE_FLEE_SPEED);
        return;
    }
    s_mote.scared = 0;

    /* ---- ② 被落地照明棒吸引：靠近但保持距离 ---- */
    {
           /* 真实类型在 game.h */
        int best = -1; float bestD2 = MOTE_LIGHT_ATTRACT * MOTE_LIGHT_ATTRACT;
        for (int i = 0; i < MAX_THROWN; i++) {
            if (!thrown[i].active) continue;
            if (thrown[i].type != 0) continue;
            float d2 = Dist2(thrown[i].x, thrown[i].y, s_mote.x, s_mote.y);
            if (d2 > bestD2) continue;
            bestD2 = d2; best = i;
        }
        if (best >= 0) {
            float ddx = thrown[best].x - s_mote.x;
            float ddy = thrown[best].y - s_mote.y;
            float dd = sqrtf(ddx * ddx + ddy * ddy);
            if (dd > MOTE_LIGHT_KEEP) {
                MoveTo(dt, thrown[best].x, thrown[best].y, MOTE_SPEED);
                return;
            }
            /* 已经够近：停下观察 */
            return;
        }
    }

    /* ---- ③ 平时缓慢漫游 ---- */
    s_mote.wanderT -= dt;
    if (s_mote.wanderT <= 0.0f) {
        s_mote.wanderT = 3.0f;
        PickWander();
    }
    MoveTo(dt, s_mote.tx, s_mote.ty, MOTE_SPEED);

    /* ==================================================================
     * 刻意没有任何 sanity / 金币 / 道具写入 —— 它不伤害也不产出。
     * ================================================================== */
}

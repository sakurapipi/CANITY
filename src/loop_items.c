/* Transient item pools: smoke, glue and decoys. */
#include "common.h"
#include "item_effects.h"
#include "world_api.h"
#include "player_api.h"
#include "state.h"
#include "mimic.h"


int flares = 0, bottles = 0;
int phaseBeacons = 0;   /* 相位信标（唯一的一种相位投掷物） */
int bricks = 0, acids = 0, smokeBombs = 0;   /* v1.16 新投掷物 */
int firePots = 0, gluePots = 0, mirrors = 0; /* v2.0 三个新投掷物（v3.3：火/镜已删，只剩 gluPots 在用） */
int echoBolts = 0;   /* 2026-11-05 回声弹 = 资源雷达：ECHO_RADIUS 内标容器 30 秒 / 活物 5 秒 */
int decoys = 0;      /* v3.3 玩家诱饵玩偶：C 键放置，每 DECOY_SHOUT_EVERY 秒发声 */
int throwSelected = 0;        /* 0=荧光棒, 1=玻璃瓶, 2=预留 */

SmokeCloud smokes[MAX_SMOKE];
GlueZone  glues[MAX_GLUE];     /* v3.3 粘胶区域 */


void SpawnSmoke(float x, float y) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (smokes[i].active) continue;
        smokes[i].x = x; smokes[i].y = y;
        smokes[i].life = SMOKE_LIFE;
        smokes[i].active = 1;
        return;
    }
}

void SpawnGlue(float x, float y) {
    int best = -1;
    float bestLife = 1e9f;
    for (int i = 0; i < MAX_GLUE; i++) {
        if (!glues[i].active) { best = i; bestLife = -1.0f; break; }
        if (glues[i].life < bestLife) { bestLife = glues[i].life; best = i; }
    }
    if (best < 0) return;
    glues[best].x = x;
    glues[best].y = y;
    glues[best].life = GLUE_LIFE;
    glues[best].active = 1;
    /* 2026-11-05：宝箱怪不是普通怪，胶水管线扫不到它 —— 单独通知一次。
       命中后它硬直 2 秒，玩家可以趁机搜空壳（铁律二十二 登记点）。 */
    MimicOnGlue(x, y);
}

int PointInSmoke(float x, float y) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (!smokes[i].active) continue;
        float dx = x - smokes[i].x, dy = y - smokes[i].y;
        if (dx * dx + dy * dy < SMOKE_RADIUS * SMOKE_RADIUS) return 1;
    }
    return 0;
}

int SegmentInSmoke(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return PointInSmoke(x0, y0);
    dx /= d; dy /= d;
    for (float t = 0.0f; t <= d; t += 0.35f)
        if (PointInSmoke(x0 + dx * t, y0 + dy * t)) return 1;
    return PointInSmoke(x1, y1);
}

int PlayerInSmoke(void) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (!smokes[i].active) continue;
        float dx = posX - smokes[i].x, dy = posY - smokes[i].y;
        if (dx * dx + dy * dy < SMOKE_RADIUS * SMOKE_RADIUS) return 1;
    }
    return 0;
}

void UpdateSmokes(float dt) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (!smokes[i].active) continue;
        smokes[i].life -= dt;
        if (smokes[i].life <= 0.0f) smokes[i].active = 0;
    }
    /* v3.3：粘胶区域 / 回声扫描的生命周期 */
    for (int i = 0; i < MAX_GLUE; i++) {
        if (!glues[i].active) continue;
        glues[i].life -= dt;
        if (glues[i].life <= 0.0f) { glues[i].active = 0; glues[i].life = 0.0f; }
    }
}

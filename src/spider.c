/*
 * Canity — 缚丝蛛实现（v2.4.1 第 5~6 轮）
 * 头部注释见 include/spider.h。
 */
#include "difficulty.h"
#include "common.h"
#include "entity.h"
#include "spider.h"
#include "control_effect.h"
#include "audio.h"

/* 2026-11-06（分区 S3）：PlaySfx 的原型本来就在 audio.h 里，
   这里不许再声明一遍 —— 跨分区只认头文件的公开符号。 */
#include "audio.h"

typedef struct {
    int   active;
    int   state;
    float x, y;
    float aimT;       /* 前摇剩余 */
    float cdT;        /* 冷却剩余 */
    float wanderT;
    float tx, ty;     /* 漫游目标 */
} Spider;

/* 2026-11-06：被扳手砸掉后的不刷新冷却 */
static float s_respawnCd = 0.0f;

static Spider s_spider;

/* 外部依赖（都在别处实现） */
extern int   worldMap[MAP_HEIGHT][MAP_WIDTH];
extern float posX, posY;
extern int   hiddenIn;

void SpiderReset(void) {
    memset(&s_spider, 0, sizeof(s_spider));
    s_spider.state = SPIDER_IDLE;
    s_respawnCd = 0.0f;
}

/* ==================================================================
 * 2026-11-06（用户要求）：**扳手砸中 → 蜘蛛消失，30 秒不刷新**
 * ------------------------------------------------------------------
 * 用户原话：「也要出一个克制蜘蛛的方法，就是用那个扳手砸中蜘蛛的话，
 *           蜘蛛会消失，然后 30 秒之内都不会刷新在场上」。
 * ================================================================== */
void SpiderDispel(void) {
    if (!s_spider.active) return;
    s_spider.active = 0;
    s_respawnCd = SPIDER_RESPAWN_COOLDOWN;
    PlaySfx(SFX_GLASS, 0.8f);
    {
        extern int g_lang;
        extern const char* pickupMsg;
        extern float pickupMsgTimer;
        pickupMsg = (g_lang == 1) ? "蜘蛛被砸散了" : "THE SPIDER SCATTERS";
        pickupMsgTimer = 1.6f;
    }
}

float SpiderRespawnCd(void) { return s_respawnCd; }

int SpiderActive(void) { return s_spider.active; }
int SpiderState(void)  { return s_spider.state; }
float SpiderX(void)    { return s_spider.x; }
float SpiderY(void)    { return s_spider.y; }

/* 找一个离玩家不太近、可站立的格子 */
static int SpawnCell(float* ox, float* oy) {
    for (int tries = 0; tries < 200; tries++) {
        int x = 2 + rand() % (MAP_WIDTH - 4);
        int y = 2 + rand() % (MAP_HEIGHT - 4);
        if (worldMap[y][x]) continue;
        float d2 = Dist2((float)x + 0.5f, (float)y + 0.5f, posX, posY);
        /* 距离 > 6 格才算"潜伏点"，避免一进门就贴脸 */
        if (d2 < 36.0f) continue;
        *ox = (float)x + 0.5f;
        *oy = (float)y + 0.5f;
        return 1;
    }
    return 0;
}

int SpiderTrySpawn(int level) {
    if (s_respawnCd > 0.0f) return 0;      /* 刚被砸掉，本层不再刷新 */
    if (level < SPIDER_MIN_LEVEL) return 0;
    {
        int chance = SPIDER_APPEAR_BASE + (level - SPIDER_MIN_LEVEL) * SPIDER_APPEAR_STEP;
        if (chance > SPIDER_APPEAR_CAP) chance = SPIDER_APPEAR_CAP;
        if (rand() % 100 >= chance) return 0;
    }
    float x, y;
    if (!SpawnCell(&x, &y)) return 0;
    memset(&s_spider, 0, sizeof(s_spider));
    s_spider.active = 1;
    s_spider.state  = SPIDER_PATROL;
    s_spider.x = x; s_spider.y = y;
    s_spider.tx = x; s_spider.ty = y;
    return 1;
}

static void PickWander(void) {
    int cx = (int)s_spider.x, cy = (int)s_spider.y;
    for (int t = 0; t < 24; t++) {
        int nx = cx + (rand() % 9) - 4;
        int ny = cy + (rand() % 9) - 4;
        if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
        if (worldMap[ny][nx]) continue;
        s_spider.tx = (float)nx + 0.5f;
        s_spider.ty = (float)ny + 0.5f;
        return;
    }
    s_spider.tx = s_spider.x;
    s_spider.ty = s_spider.y;
}

static void MoveToward(float dt, float tx, float ty, float speed) {
    float dx = tx - s_spider.x, dy = ty - s_spider.y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.05f) return;
    float step = speed * dt;
    if (step > d) step = d;
    float nx = s_spider.x + dx / d * step;
    float ny = s_spider.y + dy / d * step;
    /* 简单避墙：目标格是墙就不走 */
    int cx = (int)nx, cy = (int)ny;
    if (cx >= 0 && cy >= 0 && cx < MAP_WIDTH && cy < MAP_HEIGHT && !worldMap[cy][cx]) {
        s_spider.x = nx;
        s_spider.y = ny;
    }
}

/* 2026-11-06 仅测试用：把蜘蛛放到指定位置并激活。
   生成是 30% 概率的，测试需要确定性。 */
void SpiderDebugPlace(float x, float y) {
    memset(&s_spider, 0, sizeof(s_spider));
    s_spider.active = 1;
    s_spider.state  = SPIDER_PATROL;
    s_spider.x = x; s_spider.y = y;
    s_spider.tx = x; s_spider.ty = y;
    s_spider.wanderT = 999.0f;   /* 不让它自己乱走 */
}

/* ==================================================================
 * 2026-11-06（用户要求）：**F3 召唤一只缚丝蛛**
 * ------------------------------------------------------------------
 * 用户原话：「把 F3 键改成召唤那个吐丝怪物」。
 * 跳脸怪已经删除，这个调试键改成召唤蜘蛛，放在玩家前方 3~5 格。
 * ================================================================ */
int SpiderSpawnNearPlayer(void) {
    for (int tries = 0; tries < 200; tries++) {
        float d = 3.0f + (float)(rand() % 3);
        float ang = (float)(rand() % 63) * 0.1f;
        float cx = posX + cosf(ang) * d;
        float cy = posY + sinf(ang) * d;
        int mx = (int)cx, my = (int)cy;
        if (mx < 1 || my < 1 || mx >= MAP_WIDTH - 1 || my >= MAP_HEIGHT - 1) continue;
        if (worldMap[my][mx]) continue;
        SpiderDebugPlace(cx, cy);
        return 1;
    }
    /* 找不到就放在玩家脚下（至少能召唤出来，方便调试） */
    SpiderDebugPlace(posX + 0.6f, posY);
    return 1;
}

void SpiderTick(float dt) {
    if (s_respawnCd > 0.0f) {
        s_respawnCd -= dt;
        if (s_respawnCd < 0.0f) s_respawnCd = 0.0f;
    }
    if (!s_spider.active) return;
    /* 藏起来的时候蜘蛛也"看不见"玩家 —— 和听声鬼一致 */
    int playerHidden = (hiddenIn >= 0);

    switch (s_spider.state) {
    case SPIDER_PATROL:
        s_spider.wanderT -= dt;
        if (s_spider.wanderT <= 0.0f) {
            s_spider.wanderT = 2.5f;
            PickWander();
        }
        MoveToward(dt, s_spider.tx, s_spider.ty, SPIDER_MOVE_SPEED);

        /* 发现玩家：距离够近 + 有视线 + 不在藏身 */
        if (!playerHidden) {
            float d2 = Dist2(s_spider.x, s_spider.y, posX, posY);
            if (d2 <= SPIDER_RANGE * SPIDER_RANGE &&
                HasLOS(s_spider.x, s_spider.y, posX, posY)) {
                s_spider.state = SPIDER_AIM;
                s_spider.aimT  = SPIDER_AIM_TIME;
                PlaySfx(SFX_SCREAM, 0.45f);   /* 前摇听得见 */
            }
        }
        break;

    case SPIDER_AIM:
        /* 前摇期间站定抬起前肢；玩家跑出射程或断视线就放弃 */
        s_spider.aimT -= dt;
        {
            float d2 = Dist2(s_spider.x, s_spider.y, posX, posY);
            if (playerHidden || d2 > SPIDER_RANGE * SPIDER_RANGE ||
                !HasLOS(s_spider.x, s_spider.y, posX, posY)) {
                s_spider.state = SPIDER_COOLDOWN;
                s_spider.cdT   = SPIDER_COOLDOWN_TIME * 0.5f;
                break;
            }
        }
        if (s_spider.aimT <= 0.0f) s_spider.state = SPIDER_FIRE;
        break;

    case SPIDER_FIRE: {
        /* 命中判定：仍在射程与视线内才算命中 */
        float d2 = Dist2(s_spider.x, s_spider.y, posX, posY);
        int hit = (!playerHidden) &&
                  (d2 <= SPIDER_RANGE * SPIDER_RANGE) &&
                  HasLOS(s_spider.x, s_spider.y, posX, posY);
        if (hit) {
            /* 交给控制层。被拒（抱脸虫正在生效 / 免疫中）就直接进冷却，
               **绝不**再排队等下一次。 */
            /* 第 5 轮：这是现在唯一的"限时按键"事件，难度也要作用于它
               （轻松 ×1.6 窗口，硬核 ×0.75） */
            if (ControlBeginTimed(CTRL_SPIDER_WEB,
                                  SPIDER_WEB_WINDOW * DiffQteWindowMul(),
                                  SPIDER_WEB_REDUCE,
                                  SPIDER_WEB_MIN)) {
                PlaySfx(SFX_GLUE, 0.9f);
                /* 2026-11-06（用户要求）：命中时屏幕上给明确提示 */
                extern int g_lang;
                extern const char* pickupMsg;
                extern float pickupMsgTimer;
                pickupMsg = (g_lang == 1)
                    ? "被蛛网缠住了 —— 连按空格，最快 1 秒"
                    : "WEBBED - MASH SPACE, 1s MINIMUM";
                pickupMsgTimer = 2.2f;
            }
        }
        s_spider.state = SPIDER_COOLDOWN;
        s_spider.cdT   = SPIDER_COOLDOWN_TIME;
        break;
    }

    case SPIDER_COOLDOWN:
        s_spider.cdT -= dt;
        if (s_spider.cdT <= 0.0f) s_spider.state = SPIDER_PATROL;
        break;

    default:
        s_spider.state = SPIDER_PATROL;
        break;
    }

    /* ==================================================================
     * 零伤害硬约束
     * ------------------------------------------------------------------
     * 这里**刻意**没有任何 sanity / tookDamage / hp 写入。
     * 测试用"束缚全程 sanity 差值 = 0"钉住它 —— 以后谁给蜘蛛加伤害，
     * 那条断言会立刻红。
     * ================================================================== */
}

/*
 * Canity — THE COLLECTOR 拾荒者（v2.4.64 重写）
 *
 * 修之前实测的四个问题
 * ────────────────────
 *  ① **走路会「闪一下」**
 *     每帧都重建 BFS 距离场（玩家换格就重建），而梯度移动只在
 *     格心之间跳 —— 于是它的位置在两个格心之间来回抖。
 *     现在：距离场每 0.35 秒才重建一次，移动用连续速度 + 沿墙滑。
 *
 *  ② **瓶子砸中没反应**
 *     命中判定 1.2 格太紧（瓶子有抛物线，落点常差半格）。
 *     现在放宽到 2.0 格，并且加了硬直 + 后退反馈。
 *
 *  ③ **背包颜色不表达状态**
 *     原来只靠一点点色调差。现在明确：
 *       身上没钱 → **白色**（干净的布袋）
 *       身上有钱 → **金色**（装满赃款）
 *
 *  ④ **可能卡死不消失**
 *     45 秒上限只在「没钱」时生效（有钱时它在跑）。
 */
#include "state.h"
#include "common.h"
#include "game.h"
#include "entity.h"
#include "audio.h"
#include "lang.h"

#define COLLECTOR_SPEED        1.70f   /* v2.4.67 追你时的速度（慢于玩家走速 2.0，走得掉） */
#define COLLECTOR_FLEE_SPEED   2.30f   /* v2.4.67 **得手后逃跑速度** ——
                                          玩家走 2.0 / 跑 2.4，所以 2.3 是
                                          「比你走路快、比你冲刺慢一点点」：
                                          必须真的跑起来才追得上。 */
/* ===== v2.4.71 **偷钱比例不固定：带得越多，偷得越狠** =====
   用户要求：「偷钱比例不固定，越往后面你带的钱越多，他的偷钱比例越大。」

     基础        6%
     每 20000 金  +1%
     上限        15%

   携带       比例      实际被偷（按该持有量）
       20k     7%        1 400
       60k     9%        5 400
      120k    12%       14 400
      200k    15%       30 000   ← 封顶
   这样后期「带太多钱在身上」真的会变成风险。 */
#define COLLECTOR_STEAL_BASE    6
#define COLLECTOR_STEAL_PER_20K 1
#define COLLECTOR_STEAL_MAX     15
#define COLLECTOR_FLEE_TIME    10.0f
#define COLLECTOR_HITS_TO_DROP 2
#define COLLECTOR_HIT_WINDOW   6.0f
#define COLLECTOR_TOUCH        0.75f
#define COLLECTOR_BOTTLE_R     2.00f
#define COLLECTOR_BFS_EVERY    0.35f
#define COLLECTOR_LIFE_MAX     45.0f

typedef struct {
    int   active;
    float x, y;
    int   bounty;
    int   hits;
    float hitTimer;
    float fleeTimer;
    float lifeT;
    float bfsT;
    float stunT;
} Collector;

Collector collector;

extern float MoneyBagX(int), MoneyBagY(int), MoneyBagLife(int);
extern int   MoneyBagAmount(int);
extern void  MoneyBagTake(int idx);
extern void  MoneyBagDropAt(float x, float y, int amount);

int   CollectorActive(void)   { return collector.active; }
float CollectorX(void)        { return collector.x; }
float CollectorY(void)        { return collector.y; }
int   CollectorBounty(void)   { return collector.bounty; }

/* v6：图鉴读这里，保证"改速度 → 图鉴自动变" */
float CollectorSpeedNow(void) { return COLLECTOR_SPEED; }
float CollectorFleeSpeedNow(void) { return COLLECTOR_FLEE_SPEED; }

int   CollectorHasMoney(void) { return collector.bounty > 0; }

static short colB[MAP_HEIGHT][MAP_WIDTH];

static void colBfsBuild(void) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            colB[y][x] = -1;

    int sx = (int)posX, sy = (int)posY;
    if (sx < 0 || sx >= MAP_WIDTH || sy < 0 || sy >= MAP_HEIGHT) return;

    static int qx[MAP_WIDTH * MAP_HEIGHT];
    static int qy[MAP_WIDTH * MAP_HEIGHT];
    int head = 0, tail = 0;

    colB[sy][sx] = 0;
    qx[tail] = sx; qy[tail] = sy; tail++;

    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };

    while (head < tail) {
        int cx = qx[head], cy = qy[head];
        head++;
        int nd = colB[cy][cx] + 1;
        if (nd > 200) continue;
        for (int i = 0; i < 4; i++) {
            int nx = cx + dxs[i], ny = cy + dys[i];
            if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
            if (worldMap[ny][nx] != 0) continue;
            if (colB[ny][nx] >= 0) continue;
            colB[ny][nx] = (short)nd;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
}

static void colStepToPlayer(float* x, float* y, float sp, float dt) {
    int cx = (int)(*x), cy = (int)(*y);
    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return;

    int here = colB[cy][cx];
    if (here < 0) return;

    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };
    int bestI = -1, bestD = here;
    for (int i = 0; i < 4; i++) {
        int nx = cx + dxs[i], ny = cy + dys[i];
        if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
        int d = colB[ny][nx];
        if (d < 0) continue;
        if (d < bestD) { bestD = d; bestI = i; }
    }
    if (bestI < 0) {
        float dx = posX - *x, dy = posY - *y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > 0.05f) {
            float nx = *x + (dx / d) * sp * dt;
            float ny = *y + (dy / d) * sp * dt;
            if (!Blocked(nx, ny)) { *x = nx; *y = ny; }
        }
        return;
    }

    float tx = (float)(cx + dxs[bestI]) + 0.5f;
    float ty = (float)(cy + dys[bestI]) + 0.5f;
    float dx = tx - *x, dy = ty - *y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return;

    float nx = *x + (dx / d) * sp * dt;
    float ny = *y + (dy / d) * sp * dt;
    if (!Blocked(nx, ny)) { *x = nx; *y = ny; return; }
    if (!Blocked(nx, *y)) { *x = nx; return; }
    if (!Blocked(*x, ny)) { *y = ny; return; }
    float sx2 = *x + (dy / d) * sp * dt;
    float sy2 = *y - (dx / d) * sp * dt;
    if (!Blocked(sx2, sy2)) { *x = sx2; *y = sy2; }
}

static int findSpotFar(float* ox, float* oy) {
    for (int t = 0; t < 100; t++) {
        float ang = (float)(rand() % 6283) / 1000.0f;
        float d = 8.0f + (float)(rand() % 600) / 100.0f;
        float x = posX + cosf(ang) * d;
        float y = posY + sinf(ang) * d;
        if (x < 1.5f || y < 1.5f || x > MAP_WIDTH - 1.5f || y > MAP_HEIGHT - 1.5f) continue;
        if (Blocked(x, y)) continue;
        *ox = x; *oy = y;
        return 1;
    }
    return 0;
}

void CollectorReset(void) {
    collector.active = 0;
    collector.x = collector.y = 0.0f;
    collector.bounty = 0;
    collector.hits = 0;
    collector.hitTimer = 0.0f;
    collector.fleeTimer = 0.0f;
    collector.lifeT = 0.0f;
    collector.bfsT = 0.0f;
    collector.stunT = 0.0f;
}

void CollectorSpawnIfEligible(void) {
    /* v2.4.98 EXIT_SPAWN_GUARD：玩家站在出口附近时不刷新 ——
       否则怪会贴着脸生成，直接把正在选"继续/撤离"的玩家撞死。 */
    {
        extern int breakerOpen, exitChoiceOpen;
        extern float exitX, exitY, posX, posY;
        if (exitChoiceOpen) return;
        if (breakerOpen) {
            extern float Dist2(float, float, float, float);
            if (Dist2(posX, posY, exitX, exitY) < 9.0f) return;   /* 3 格内 */
        }
    }

    extern int carryGold;
    extern int GoldTier(void);
    CollectorReset();
    /* v2.4.71 平调：门槛 46000/T7 → 20000/T4 —— 用户要求「让他早点出来」。
       20000 大约是前期两三层就能攒到的量。 */
    if (carryGold < 20000) return;
    if (GoldTier() < 4) return;
    float x, y;
    if (!findSpotFar(&x, &y)) return;
    collector.x = x;
    collector.y = y;
    collector.active = 1;
    colBfsBuild();
    { extern void BestiaryMarkSeen(int); BestiaryMarkSeen(3); }
    pickupMsg = (g_lang == LANG_ZH) ? "有东西在闻你的钱味…" : "SOMETHING SMELLS YOUR GOLD";
    pickupMsgTimer = 2.0f;
}

void CollectorForceSpawnAt(float x, float y) {
    CollectorReset();
    collector.x = x;
    collector.y = y;
    collector.active = 1;
    colBfsBuild();
    { extern void BestiaryMarkSeen(int); BestiaryMarkSeen(3); }
}

void CollectorBurstBounty(void) {
    if (!collector.active) return;

    collector.stunT = 0.8f;

    if (collector.bounty <= 0) {
        collector.fleeTimer = 3.0f;
        PlaySfx(SFX_GLASS, 0.75f);
        AddShake(0.12f, 0.09f);
        pickupMsg = (g_lang == LANG_ZH) ? "它被砸退了" : "IT STAGGERED";
        pickupMsgTimer = 1.3f;
        return;
    }

    MoneyBagDropAt(collector.x, collector.y, collector.bounty);
    collector.bounty = 0;
    collector.hits = 0;
    collector.fleeTimer = COLLECTOR_FLEE_TIME;
    PlaySfx(SFX_GLASS, 1.0f);
    AddShake(0.18f, 0.14f);
    pickupMsg = (g_lang == LANG_ZH) ? "瓶子砸中！赃款掉了出来" : "BOTTLE HIT! BOUNTY DROPPED";
    pickupMsgTimer = 2.0f;
}

/* ===== v2.4.87 **砸几下才吐钱，跟强度档挂勾** =====
   用户要求：「砸一次就把钱吐出来，然后随着等级增长以后要砸两次，
   跟那个强度有关联。」

     强度档 0~5   → 1 下
     强度档 6~8   → 2 下
     强度档 9+    → 3 下

   前期一次就能抢回来，玩家敢上去打；
   后期要多砸几下，逼你在"追它"和"它跑掉"之间做取舍。 */
static int CollectorHitsToDrop(void) {
    extern int GoldTier(void);
    int t = GoldTier();
    if (t >= 9) return 3;
    if (t >= 6) return 2;
    return 1;
}

void CollectorHitByWrench(float hx, float hy) {
    if (!collector.active) return;
    float dx = collector.x - hx, dy = collector.y - hy;
    if (dx * dx + dy * dy > 4.0f) return;

    if (collector.hitTimer > COLLECTOR_HIT_WINDOW) collector.hits = 0;
    collector.hitTimer = 0.0f;
    collector.hits++;

    /* ===== v2.4.87 **修乱码** =====
       原来这里是 `char hb[64];` —— 一个**局部数组**。
       pickupMsg 指向它，函数一返回那块栈就被复用了，
       UI 去读的时候已经是别的数据 → 屏幕上就是一串乱码。
       必须用 static。 */
    if (collector.hits < CollectorHitsToDrop()) {
        static char hb[64];
        snprintf(hb, sizeof(hb), (g_lang == LANG_ZH) ? "命中！再打 %d 下掉赃款"
                                                     : "HIT! %d MORE TO DROP",
                 CollectorHitsToDrop() - collector.hits);
        pickupMsg = hb;
        pickupMsgTimer = 1.2f;
        AddShake(0.10f, 0.08f);
    }

    if (collector.hits >= CollectorHitsToDrop() && collector.bounty > 0) {
        MoneyBagDropAt(collector.x, collector.y, collector.bounty);
        collector.bounty = 0;
        collector.hits = 0;
        PlaySfx(SFX_GLASS, 0.85f);
        { extern int bountyRecovered; bountyRecovered++; }   /* v7：CLAWBACK 成就用 */
        pickupMsg = (g_lang == LANG_ZH) ? "赃款抢回来了！" : "BOUNTY RECOVERED";
        pickupMsgTimer = 1.6f;
    }
}

void UpdateCollector(float dt) {
    { extern int trainingMode; if (trainingMode) return; }
    if (!collector.active) return;

    collector.hitTimer += dt;
    collector.lifeT += dt;

    collector.bfsT += dt;
    if (collector.bfsT >= COLLECTOR_BFS_EVERY) {
        collector.bfsT = 0.0f;
        colBfsBuild();
    }

    if (collector.stunT > 0.0f) {
        collector.stunT -= dt;
        if (collector.stunT < 0.0f) collector.stunT = 0.0f;
        return;
    }

    if (collector.fleeTimer > 0.0f) {
        collector.fleeTimer -= dt;
        if (collector.fleeTimer <= 0.0f) {
            collector.active = 0;
            return;
        }

        /* ===== v2.4.73 **逃跑不再直撞墙** =====
           原来它直线远离玩家，撞墙就贴着墙不动 —— 读起来像"卡住"。
           现在：优先沿 BFS 距离场**往高处爬**（离玩家更远），
           每个候选方向都做碰撞检查，撞墙就换下一个。 */
        int cx = (int)collector.x, cy = (int)collector.y;
        int moved = 0;

        if (cx >= 0 && cy >= 0 && cx < MAP_WIDTH && cy < MAP_HEIGHT && colB[cy][cx] >= 0) {
            static const int dxs[4] = { 1, -1, 0, 0 };
            static const int dys[4] = { 0, 0, 1, -1 };
            int here = colB[cy][cx];
            int bestI = -1, bestD = here;
            for (int i = 0; i < 4; i++) {
                int nx = cx + dxs[i], ny = cy + dys[i];
                if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
                int d = colB[ny][nx];
                if (d < 0) continue;
                if (d > bestD) { bestD = d; bestI = i; }
            }
            if (bestI >= 0) {
                float tx = (float)(cx + dxs[bestI]) + 0.5f;
                float ty = (float)(cy + dys[bestI]) + 0.5f;
                float ddx = tx - collector.x, ddy = ty - collector.y;
                float dd = sqrtf(ddx * ddx + ddy * ddy);
                if (dd > 0.001f) {
                    float nx = collector.x + (ddx / dd) * COLLECTOR_FLEE_SPEED * dt;
                    float ny = collector.y + (ddy / dd) * COLLECTOR_FLEE_SPEED * dt;
                    if (!Blocked(nx, ny)) { collector.x = nx; collector.y = ny; moved = 1; }
                    else if (!Blocked(nx, collector.y)) { collector.x = nx; moved = 1; }
                    else if (!Blocked(collector.x, ny)) { collector.y = ny; moved = 1; }
                }
            }
        }

        /* 梯度走不了 → 退回"直线远离 + 沿墙滑" */
        if (!moved) {
            float dx = collector.x - posX, dy = collector.y - posY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 0.001f) {
                float nx = collector.x + (dx / d) * COLLECTOR_FLEE_SPEED * dt;
                float ny = collector.y + (dy / d) * COLLECTOR_FLEE_SPEED * dt;
                if (!Blocked(nx, ny)) { collector.x = nx; collector.y = ny; }
                else if (!Blocked(nx, collector.y)) collector.x = nx;
                else if (!Blocked(collector.x, ny)) collector.y = ny;
                else {
                    /* 前后左右四个方向蒙一个能走的 */
                    static const float ax4[4] = { 1, -1, 0, 0 };
                    static const float ay4[4] = { 0, 0, 1, -1 };
                    for (int i = 0; i < 4; i++) {
                        float tx2 = collector.x + ax4[i] * COLLECTOR_FLEE_SPEED * dt;
                        float ty2 = collector.y + ay4[i] * COLLECTOR_FLEE_SPEED * dt;
                        if (!Blocked(tx2, ty2)) { collector.x = tx2; collector.y = ty2; break; }
                    }
                }
            }
        }
        return;
    }

    if (collector.lifeT >= COLLECTOR_LIFE_MAX && collector.bounty <= 0) {
        collector.active = 0;
        return;
    }

    /* ===== v2.4.67 **锁定玩家** =====
       用户反馈：它不会攻击、不会偷钱、只会钻墙。
       根因是原来「钱袋优先」的目标选择 —— 场上有钱袋时它会绕去找钱袋，
       而那套直线移动没有沿墙滑，撞墙就贴上去，读起来就是"钻墙"。
       现在**只锁玩家**，一路沿 BFS 梯度走过来。 */
    colStepToPlayer(&collector.x, &collector.y, COLLECTOR_SPEED, dt);

    /* 摸到你 → 偷钱 + 逃跑 */
    {
        float dx = posX - collector.x, dy = posY - collector.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < COLLECTOR_TOUCH) {
            extern int carryGold;
            int pct = COLLECTOR_STEAL_BASE + (carryGold / 20000) * COLLECTOR_STEAL_PER_20K;
            if (pct > COLLECTOR_STEAL_MAX) pct = COLLECTOR_STEAL_MAX;
            int steal = carryGold * pct / 100;
            if (steal < 1 && carryGold > 0) steal = 1;
            if (steal > 0) {
                { extern int WalletStealRun(int); WalletStealRun(steal); }
                collector.bounty += steal;
                AddHitFlash(0.55f);
                AddShake(0.18f, 0.14f);
                PlaySfx(SFX_HEART, 0.8f);
                {
                    static char sm[96];
                    snprintf(sm, sizeof(sm), (g_lang == LANG_ZH)
                             ? "它偷走了 %d 金币！" : "IT STOLE %d GOLD!",
                             steal);
                    pickupMsg = sm;
                    pickupMsgTimer = 2.0f;
                }
            } else {
                pickupMsg = (g_lang == LANG_ZH) ? "它摸了你一下（你没带钱）"
                                                : "IT GRABBED YOU (NO GOLD)";
                pickupMsgTimer = 1.4f;
            }
            /* 得手 → 转身逃 */
            collector.fleeTimer = COLLECTOR_FLEE_TIME;
            if (d > 0.001f) {
                float bx = collector.x - (dx / d) * 1.2f;
                float by = collector.y - (dy / d) * 1.2f;
                if (!Blocked(bx, by)) { collector.x = bx; collector.y = by; }
            }
        }
    }
}

void CollectorForcePlace(float x, float y) {
    collector.active = 1;
    collector.x = x;
    collector.y = y;
}

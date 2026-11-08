/*
 * Canity — THE SCAVENGER 清道夫（v2.4.18 机制修缮）
 *
 * 修之前的四个实测 bug（用户报告）
 * ────────────────────────────────
 *  ① 「我一离开柜子，他就不追击我了」
 *       → 目标坐标在生成时**只锁一次**，之后玩家跑多远它都往老地方走。
 *  ② 「他平常站在那里不动」
 *       → 目标点不可达时 d 永远 > 1.6，状态机卡在「走过去」永远不结束。
 *  ③ 「再次进柜子，他也不管了」
 *       → 上面那个卡住的实例 active 恒为 1，
 *          `if (scavenger.active) return;` 让触发检查再也不执行。
 *  ④ 「进不同的房间也会出 bug」
 *       → 只有直线靠近 + 沿墙滑动，跨房间必卡。
 *
 * 修法
 * ────
 *  · **持续重新锁定**：每 1.0 秒把目标改成玩家当前位置（躲着就锁藏身点）
 *  · **卡死检测**：2 秒内位移 < 0.25 格 → 判定卡住 → 直接绕到玩家附近重生
 *  · **寿命上限**：60 秒还没干成事就自己走（绝不永久站着）
 *  · **到达后的行为明确**：躲着 → 拉柜门；没躲 → 翻箱 3 秒 → 走；
 *    没箱子可翻 → 原地翻找 2 秒 → 走（有动作，不是站着）
 *  · **可以重来**：走掉之后 25 秒冷却，冷却完 hideStreak 归零，可再次触发
 */
#include "state.h"
#include "economy.h"
#include "common.h"
#include "game.h"
#include "entity.h"
#include "audio.h"
#include "lang.h"
#include "skills.h"

#define SCAV_TRIGGER_TIME  180.0f   /* v2.4.40 平调：150→180。
                                       一层正常 2~4 分钟，150 秒几乎每层必来。 */   /* v2.4.19 平衡：120→150。
                                       对比：寻声怪是「一进层就在」的常驻压力，
                                       清道夫是**额外**压力，120 秒几乎每层必触发，
                                       150 秒才让「快点走」这个决策有意义。 */
#define SCAV_SPEED          1.55f   /* v2.4.40 平调：1.4→1.55。
                                       玩家走速 2.0 / 冲刺 2.4 ——
                                       1.4 让「走路也甩得掉」，压迫感不足；
                                       1.55 逼你必须跑，才配得上「它一直追你」。 */    /* 格/秒（原来 1.0 太慢，追不上人） */
#define SCAV_GRAB_WARN      1.5f    /* 拉出前的预警秒数 */
/* ===== v2.4.36 **被撬柜子的理智惩罚重新设计** =====
   用户要求：改成**扣 20% 当前理智**，并且随「带入金币」和「强度档位」逐渐增加。

   为什么用百分比而不是定值：
     定值 12 在 20 理智时是致命的，在 90 理智时几乎无感 ——
     百分比让它**永远疼，但永远不致死**，符合「这个游戏不让你死，只让你变糟」。

   三段叠加：
     基础      20%
     金币加成  carryGold / 10000 × 0.6%   （封顶 +15%）
     档位加成  GoldTier() × 0.8%          （封顶 +15%）
     总计      20% ~ 50%

   对比：寻声怪抓一次扣 18 点**定值**理智，不碰柜子；
        清道夫扣的是百分比 + 撬坏柜子，代价更重，但也更可控。 */
#define SCAV_SAN_PCT_BASE     0.20f   /* 基础 20% */
#define SCAV_SAN_PCT_GOLD     0.006f  /* 每 10000 金币 +0.6% */
#define SCAV_SAN_PCT_GOLD_CAP 0.15f   /* 金币加成封顶 +15% */
#define SCAV_SAN_PCT_TIER     0.008f  /* 每档强度 +0.8% */
#define SCAV_SAN_PCT_TIER_CAP 0.15f   /* 档位加成封顶 +15% */
#define SCAV_SAN_PCT_MAX      0.50f   /* 总上限 50% */

/* 算出本次要扣多少**点**理智 */
static float scavSanityLoss(void) {
    extern int carryGold;

    float pct = SCAV_SAN_PCT_BASE;

    float g = (float)(carryGold > 0 ? carryGold : 0) / 10000.0f * SCAV_SAN_PCT_GOLD;
    if (g > SCAV_SAN_PCT_GOLD_CAP) g = SCAV_SAN_PCT_GOLD_CAP;
    pct += g;

    float t = (float)GoldTier() * SCAV_SAN_PCT_TIER;
    if (t > SCAV_SAN_PCT_TIER_CAP) t = SCAV_SAN_PCT_TIER_CAP;
    pct += t;

    if (pct > SCAV_SAN_PCT_MAX) pct = SCAV_SAN_PCT_MAX;

    return sanity * pct;      /* 按**当前理智**算 */
}
#define SCAV_EAT_TIME       2.5f    /* v2.4.40 平调：3.0→2.5。它只待 25 秒，
                                       3 秒吃一个太拖拉，经常一个都没吃完就走了。 */    /* 吃一个箱子几秒 */
#define SCAV_RETARGET       1.0f    /* 每隔多久重新锁定玩家 */
/* v2.4.39 **追击时间**：它出来之后最多追你这么久，到点就消失。
   期间无条件一直朝你走（BFS 梯度每帧指向你）。
   45 秒 ≈ 一个房间来回 + 一次喘息，够压迫但不至于追到窒息。 */
#define SCAV_LIFE_MAX      25.0f    /* v2.4.40 平调：45→25。25 秒够它穿过两个房间，但不会追到你烦躁；到点就消失回冷却。 */
#define SCAV_ARRIVE_R        1.2f    /* v2.4.19 平衡：1.8→1.2。
                                       对比：拾荒者接触半径 0.70，寻声怪抓人 0.55 平方。
                                       1.8 会让它「隔着一个身位就把你拽出来」，读感像作弊。 */
#define SCAV_COOLDOWN      35.0f    /* v2.4.40 平调：40→35。追得短，冷却同比降。 */    /* v2.4.19 平衡：25→40。
                                       对比：守门人解除堵门后冷却 45 秒。
                                       清道夫一次就能撬坏柜子 + 扣理智，惩罚更重，
                                       冷却不该比守门人短。 */
#define SCAV_STUCK_MOVE     0.25f   /* 2 秒内位移小于这个 = 卡住 */
#define SCAV_SEARCH_TIME    2.0f    /* 到了但没箱子翻 → 原地翻找几秒 */

/* ===== v2.4.28 **玩家跑出柜子后被它逮到** =====
   用户要求：玩家意识到它来了、从藏身点跑出来，
   如果它追上玩家 ——
     · 玩家吃 **30% 减速，持续 10 秒**
     · 理智按 **25 → 50** 随层数成长地扣（复用正式关卡的强度算法）
   这条走的是「逃跑失败」的代价，和「被堵在柜子里」是两种惩罚。 */
/* 2026-11-03 用户强化清道夫：
     「减速时间调整为减 30% 移速，持续两分钟」
   原来是 35% / 8 秒 —— 改成 30% / 120 秒。
   幅度小一点、但持续时间拉长 15 倍：
   被抓一次之后，接下来两分钟你都在瘸着跑，
   能不能在减速期里完成本层是新的压力点。 */
#define SCAV_CATCH_SLOW      0.30f   /* 减速 30% */
#define SCAV_CATCH_SLOW_T  120.0f    /* 持续 120 秒（2 分钟） */
#define SCAV_CATCH_SAN_MIN   20.0f   /* v2.4.40 平调：25→20（起始层）。 */   /* 第 1 层扣 25 理智 */
#define SCAV_CATCH_SAN_MAX   45.0f   /* v2.4.40 平调：50→45（封顶）。
                                       因为柜子损坏还要额外花 450+ 金币。 */   /* 上限 50 */
#define SCAV_CATCH_STEP      0.030f  /* 每层 +3%（和 MONSTER_SPEED_STEP 同量级） */
/* v2.4.35 **关键修复**：逮到半径必须 > 到达半径（1.2）。
   原来写 0.85 < 1.2 —— 它一进 1.2 就被「到达」分支接管（翻箱/翻找），
   永远走不到 0.85，所以逮到逻辑**一次都没触发过**。
   用户看到的就是：出现一行字 → 不抓人 → 消失。 */
#define SCAV_CATCH_R         1.45f   /* 逮到判定半径（必须 > SCAV_ARRIVE_R） */

typedef struct {
    int   active;
    float x, y;
    float tx, ty;          /* 目标：玩家（或藏身点）的位置，动态更新 */
    float warnT;           /* >0 = 正在拉柜门 */
    int   grabSpot;        /* 正在拉的那个柜子下标（-1 = 没有）。
                            * 第 1 轮（2026-11-08）：必须记下来 —— 否则玩家在预警里
                            * 出柜，hiddenIn 变 -1 就查不到是哪个柜子被拽了。 */
    float eatT;            /* 正在吃箱子的进度 */
    int   eating;          /* 正在吃的箱子下标，-1 = 没在吃 */
    float stepAcc;     /* v2.4.92 脚步累计距离 */
    float life;            /* 剩余寿命 */
    float retarget;        /* 距离下次重新锁定 */
    float searchT;         /* 原地翻找剩余时间 */
    float lastX, lastY;    /* 卡死检测用 */
    float stuckT;          /* 卡死检测计时 */
} Scavenger;

Scavenger scavenger;
static float scavCooldown = 0.0f;   /* 走掉之后的冷却 */

int   ScavengerActive(void)   { return scavenger.active; }
float ScavengerSpeedNow(void) { return SCAV_SPEED; }
int   ScavengerRifling(void) { return scavenger.eating >= 0; }
float ScavengerX(void)       { return scavenger.x; }
float ScavengerY(void)       { return scavenger.y; }
float ScavengerWarnT(void)   { return scavenger.warnT; }

/* ==================================================================
 * 2026-11-08（用户实测）：**清道夫的"待机触发"是一次性上锁**
 * ------------------------------------------------------------------
 * 旧代码：timeUp = (floorTime >= 180)。floorTime 是本层累计时间，
 * 第 180 秒之后它永远是 true —— 冷却一走完就立刻再来，
 * 变成"180 秒后每 60 秒一次，永不停止"。
 * 玩家原话：「两个 CD 感觉是独立的，导致清道夫 CD 特别快」。
 *
 * 现在用"距离上次它离开过了多久"：它不在场时才累加，出现/离开都归零。
 * ================================================================ */
static float s_quietT = 0.0f;

void ScavengerReset(void) {
    scavenger.active = 0;
    scavenger.x = scavenger.y = 0.0f;
    scavenger.tx = scavenger.ty = 0.0f;
    scavenger.warnT = 0.0f;
    scavenger.eatT = 0.0f;
    scavenger.eating = -1;
    scavenger.life = 0.0f;
    scavenger.retarget = 0.0f;
    scavenger.searchT = 0.0f;
    scavenger.stuckT = 0.0f;
    scavCooldown = 0.0f;
    s_quietT = 0.0f;
}

/* ===== v2.4.22 **多方向绕障** =====
   原来只会「直线 → 沿墙滑 X → 沿墙滑 Y → 垂直平移」这 4 个方向。
   跨房间时它会在墙角来回蹭，2 秒不动就触发卡死检测，
   而卡死检测的处理是**瞬移到玩家附近** —— 玩家看到的就是「追着追着消失了」。

   现在改成：以「指向目标」为基准，按 0° ±25° ±50° ±75° ±100° ±130° ±160° 180°
   依次试探，**第一个不撞墙的方向就是这一步**。
   效果等价于简易的沿墙跟随：碰到墙会自己贴着墙绕，不需要瞬移。 */
static void stepToward(float* x, float* y, float tx, float ty, float sp, float dt) {
    float dx = tx - *x, dy = ty - *y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return;

    float base = atan2f(dy, dx);
    static const float offs[13] = {
        0.00f,
        0.44f, -0.44f,      /* ±25° */
        0.87f, -0.87f,      /* ±50° */
        1.31f, -1.31f,      /* ±75° */
        1.75f, -1.75f,      /* ±100° */
        2.27f, -2.27f,      /* ±130° */
        2.79f, -2.79f       /* ±160° */
    };

    float stepLen = sp * dt;
    for (int i = 0; i < 13; i++) {
        float a = base + offs[i];
        float nx = *x + cosf(a) * stepLen;
        float ny = *y + sinf(a) * stepLen;
        if (!Blocked(nx, ny)) { *x = nx; *y = ny; return; }
    }
    /* 13 个方向全堵 = 真的被围死了，这一帧不动（交给上层处理） */
}

/* ===== v2.4.38 **出生点改走 BFS，只放在真正可达的过道上** =====
   用户实测：它卡在墙里（d 不变）。
   根因是出生点用「浮点随机/环形扫描」选的 ——
   选中的点可能在墙的另一侧、或者贴墙卡在角落，直线靠近没有路。

   现在改成：从玩家格做一次 BFS 洪水填充，
   得到一个「每格到玩家多少步」的距离场 bfsD[][]。
   出生点从**距离 8~16 步的可达格中心**里选，
   移动时也**沿着 bfsD 梯度往下走**（永远朝玩家方向、永远有路）。
   这样它不可能卡在墙里，也不可能走到不可达的地方。 */

static short bfsD[MAP_HEIGHT][MAP_WIDTH];
static int   bfsOK = 0;

/* 从玩家所在格做 BFS，填充 bfsD（-1 = 不可达） */
static void bfsBuild(void) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            bfsD[y][x] = -1;

    int sx = (int)posX, sy = (int)posY;
    if (sx < 0 || sx >= MAP_WIDTH || sy < 0 || sy >= MAP_HEIGHT) { bfsOK = 0; return; }

    static int qx[MAP_WIDTH * MAP_HEIGHT];
    static int qy[MAP_WIDTH * MAP_HEIGHT];
    int head = 0, tail = 0;

    bfsD[sy][sx] = 0;
    qx[tail] = sx; qy[tail] = sy; tail++;

    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };

    while (head < tail) {
        int cx = qx[head], cy = qy[head];
        head++;
        int nd = bfsD[cy][cx] + 1;
        if (nd > 200) continue;
        for (int i = 0; i < 4; i++) {
            int nx = cx + dxs[i], ny = cy + dys[i];
            if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
            if (worldMap[ny][nx] != 0) continue;
            if (bfsD[ny][nx] >= 0) continue;
            bfsD[ny][nx] = (short)nd;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
    bfsOK = 1;
}

/* 出生点：从可达格里挑一个 8~16 步远、且格子中心不撞墙的 */
static int findStart(float* ox, float* oy) {
    bfsBuild();

    int bestX = -1, bestY = -1, bestScore = -1;

    for (int gy = 1; gy < MAP_HEIGHT - 1; gy++) {
        for (int gx = 1; gx < MAP_WIDTH - 1; gx++) {
            int d = bfsD[gy][gx];
            if (d < 8 || d > 16) continue;          /* 太近会突脸，太远来得慢 */
            float cx = (float)gx + 0.5f;
            float cy = (float)gy + 0.5f;
            if (Blocked(cx, cy)) continue;
            /* 优先挑「四邻都是空地」的格 —— 过道中间，不会贴墙卡住 */
            int open = 0;
            if (!Blocked(cx + 0.5f, cy)) open++;
            if (!Blocked(cx - 0.5f, cy)) open++;
            if (!Blocked(cx, cy + 0.5f)) open++;
            if (!Blocked(cx, cy - 0.5f)) open++;
            int score = open * 100 + d;
            if (score > bestScore) { bestScore = score; bestX = gx; bestY = gy; }
        }
    }

    if (bestX >= 0) {
        *ox = (float)bestX + 0.5f;
        *oy = (float)bestY + 0.5f;
        return 1;
    }

    /* 退一步：任何可达格（>=4 步）都行 */
    for (int gy = 1; gy < MAP_HEIGHT - 1; gy++) {
        for (int gx = 1; gx < MAP_WIDTH - 1; gx++) {
            if (bfsD[gy][gx] < 4) continue;
            float cx = (float)gx + 0.5f;
            float cy = (float)gy + 0.5f;
            if (Blocked(cx, cy)) continue;
            *ox = cx; *oy = cy;
            return 1;
        }
    }

    /* 彻底没路（理论上不会）：放在玩家身后 4 格 */
    *ox = posX - dirX * 4.0f;
    *oy = posY - dirY * 4.0f;
    return 1;
}

/* ===== v2.4.38 **沿 BFS 梯度前进（永远有路，不会卡墙）** =====
   每一步都往「bfsD 最小的那个邻格」的格中心走。
   因为 bfsD 是从玩家格扩散出来的，梯度方向一定通向玩家，
   所以只可能卡在「当前格已经是 0 步」的情况 —— 那说明它已经到玩家身上了。 */
static void stepAlongBfs(float* x, float* y, float sp, float dt) {
    int cx = (int)(*x), cy = (int)(*y);
    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return;

    int here = bfsD[cy][cx];
    if (here < 0) {
        /* 掉到不可达格里了（理论上不该发生）—— 重新建场并原地不动一帧 */
        bfsBuild();
        return;
    }
    if (here == 0) return;   /* 已在玩家格 */

    /* 4 邻里找 bfsD 最小的 */
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };
    int bestI = -1, bestD = here;
    for (int i = 0; i < 4; i++) {
        int nx = cx + dxs[i], ny = cy + dys[i];
        if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
        int d = bfsD[ny][nx];
        if (d < 0) continue;
        if (d < bestD) { bestD = d; bestI = i; }
    }
    if (bestI < 0) return;

    /* 朝那个格子中心走 */
    float tx = (float)(cx + dxs[bestI]) + 0.5f;
    float ty = (float)(cy + dys[bestI]) + 0.5f;
    stepToward(x, y, tx, ty, sp, dt);
}

/* ===== v2.4.31 **直接锁定玩家所在位置** =====
   用户要求：「把锁定方式改为直接锁定那个玩家所在位置」。
   原来是每 1 秒才刷新一次目标，中间那一秒它还往老地方走 ——
   读感就是「追丢了 / 突然转方向」。
   现在**每帧都锁玩家当前坐标**（躲着时锁藏身点），
   它永远朝着你此刻所在的位置走。 */
static void retargetToPlayer(void) {
    extern int hiddenIn;
    extern HideSpot hideSpots[];
    if (hiddenIn >= 0 && hiddenIn < HIDESPOT_MAX && hideSpots[hiddenIn].active) {
        scavenger.tx = hideSpots[hiddenIn].x;
        scavenger.ty = hideSpots[hiddenIn].y;
    } else {
        scavenger.tx = posX;      /* 玩家实时坐标 */
        scavenger.ty = posY;
    }
}

/* 触发检查 —— 每帧调用，内部只做轻量判断 */

void ScavengerTickTrigger(float dt) {
    /* 2026-11-08：floorTime 不再参与触发判定（它只增不减，会永久上锁），
       改成 s_quietT。这里不再 extern floorTime。 */
    extern int   hideStreak;
    extern int   trainingMode;
    if (trainingMode) return;
    /* 2026-11-08：安静计时**在冷却期间也走** —— 否则"安静 180 秒"实际是
       215 秒（35 秒冷却 + 180 秒），和玩家理解的字面意思不符。
       它不在场时才累加；出场/离开各自归零。 */
    if (!scavenger.active) s_quietT += dt;

    if (scavCooldown > 0.0f) {
        scavCooldown -= dt;
        if (scavCooldown < 0.0f) scavCooldown = 0.0f;
        /* v2.4.27 **关键修复**：原来这里只递减、从不 return ——
           于是冷却形同虚设，走掉之后下一帧就又能触发。
           用户实测「冷却没有生效」，就是这一行漏了。 */
        return;
    }
    if (scavenger.active) return;

    /* 2026-11-08：**不再用本层累计时间**（那会永久上锁）。
       现在要求"安静满 180 秒"才由时间触发。 */
    int timeUp  = (s_quietT >= SCAV_TRIGGER_TIME);
    /* **连续躲 3 次**清道夫才出发。
       （我在这条上反复过两次：2 → 3 → 2 → 3，是我不该擅自改。
         现在定死 3，并写清楚：躲 1 次安全，躲 2 次安全，第 3 次它来。） */
    int hideUp  = (hideStreak >= 3);
    if (!timeUp && !hideUp) return;

    float x, y;
    findStart(&x, &y);   /* v2.4.31：永远成功，不再因为找不到出生点就放弃 */

    scavenger.x = x;
    scavenger.y = y;
    scavenger.warnT = 0.0f;
    scavenger.eatT = 0.0f;
    scavenger.eating = -1;
    scavenger.life = SCAV_LIFE_MAX;
    scavenger.retarget = 0.0f;
    scavenger.searchT = 0.0f;
    scavenger.lastX = x;
    scavenger.lastY = y;
    scavenger.stuckT = 0.0f;
    scavenger.active = 1;
    s_quietT = 0.0f;          /* 2026-11-08：出场即重置安静计时 */
    retargetToPlayer();

    { extern void BestiaryMarkSeen(int); BestiaryMarkSeen(4); }
    PlaySfx(SFX_GLASS, 0.45f);
    pickupMsg = (g_lang == LANG_ZH) ? "有东西在靠近…" : "SOMETHING IS COMING";
    pickupMsgTimer = 2.0f;
}

/* 干完活走人：进入冷却，并把 hideStreak 清零，这样之后还能再来 */
static void scavengerLeave(void) {
    scavenger.active = 0;
    scavenger.eating = -1;
    scavenger.warnT = 0.0f;
    scavenger.grabSpot = -1;
    scavenger.searchT = 0.0f;
    scavCooldown = SCAV_COOLDOWN;
    s_quietT = 0.0f;          /* 2026-11-08：安静计时也一起归零 */
    { extern int hideStreak; hideStreak = 0; }
}

void UpdateScavenger(float dt) {
    /* v2.4.98 EXIT_SPAWN_GUARD：玩家站在出口附近时不清道夫不出现 ——
       否则它会贴着脸生成，把正在选"继续/撤离"的玩家撞死。 */
    {
        extern int breakerOpen, exitChoiceOpen;
        extern float Dist2(float, float, float, float);
        extern float exitX, exitY;
        if (exitChoiceOpen) { scavenger.active = 0; return; }
        if (breakerOpen && Dist2(posX, posY, exitX, exitY) < 9.0f) {
            if (!scavenger.active) return;   /* 还没出场就推迟 */
        }
    }

    { extern int trainingMode; if (trainingMode) return; }
    if (!scavenger.active) return;

    /* ---- ⓪ 寿命 & 卡死检测（修 bug②：不再永久站着）---- */
    /* ===== v2.4.39 **固定追击时间** =====
       用户要求：应该一直追我，追够一段时间之后消失并进入冷却。
       原来那套「在动就续命」是反的 —— 只要它一直在走就永远不走，
       你跑多远它跟多远，没有尽头。

       现在：`chaseT` 一旦开始就**只减不增**，归零就消失 + 冷却。
       这期间它会一直朝你走（BFS 梯度每帧指向你）。 */
    scavenger.life -= dt;
    if (scavenger.life <= 0.0f) {
        scavengerLeave();        /* 追够了 → 消失 + 40 秒冷却 */
        return;
    }

    scavenger.stuckT += dt;
    if (scavenger.stuckT >= 2.0f) {
        float moved = fabsf(scavenger.x - scavenger.lastX) + fabsf(scavenger.y - scavenger.lastY);
        scavenger.stuckT = 0.0f;
        scavenger.lastX = scavenger.x;
        scavenger.lastY = scavenger.y;
        if (moved < SCAV_STUCK_MOVE) {
            /* v2.4.22：**不再瞬移** —— 瞬移在玩家眼里就是「消失了」。
               改成：给它一个 sideways 强推，靠 13 方向步进自己挤出来。 */
            int freed = 0;
            for (int t = 0; t < 8 && !freed; t++) {
                float a = (float)(rand() % 6283) / 1000.0f;
                float nx = scavenger.x + cosf(a) * 0.35f;
                float ny = scavenger.y + sinf(a) * 0.35f;
                if (!Blocked(nx, ny)) {
                    scavenger.x = nx;
                    scavenger.y = ny;
                    freed = 1;
                }
            }
            /* 连挪 8 次都挪不出去，才算真的废了 —— 这时才走人 */
            if (!freed) { scavengerLeave(); return; }
            scavenger.lastX = scavenger.x;
            scavenger.lastY = scavenger.y;
        }
    }

    /* ---- ① 拉柜门（先 1.5 秒预警）---- */
    if (scavenger.warnT > 0.0f) {
        scavenger.warnT -= dt;
        if (scavenger.warnT <= 0.0f) {
            extern int hiddenIn;
            extern HideSpot hideSpots[];
            int spot = scavenger.grabSpot;

            if (hiddenIn >= 0 && hiddenIn == spot) {
                /* ---- (a) 玩家还在柜里：原样结算并把他踢出来 ---- */
                /* v2.4.36：按当前理智的 20%~50% 扣，随金币和强度档成长 */
                sanity -= scavSanityLoss() * RunDamageTakenMul();
                if (sanity < 0.0f) sanity = 0.0f;
                AddHitFlash(0.85f);
                AddShake(0.28f, 0.20f);
                if (spot >= 0 && spot < HIDESPOT_MAX) {
                    hideSpots[spot].broken = 1;   /* 真的撬坏，要花钱修 */
                }
                { extern int resetInputState; hiddenIn = -1; resetInputState = 1; }
                PlaySfx(SFX_SCREAM, 0.7f);
                pickupMsg = (g_lang == LANG_ZH) ? "它把柜门拉开了！" : "IT OPENED THE LOCKER!";
                pickupMsgTimer = 2.2f;
                scavenger.grabSpot = -1;
                scavengerLeave();
                return;
            }

            /* ================================================================
             * ---- (b) 玩家在预警里跑出来了（2026-11-08 修）----
             * ----------------------------------------------------------------
             * 旧代码这里直接 scavengerLeave()：玩家零代价脱身，怪凭空消失。
             * 玩家原话：「只需要在他刚抓住柜门的时候出来就消失了」。
             *
             * 现在：柜门已经被它拽住了 —— **柜子照样坏**（要花钱修），
             * 但它**不再走人**，继续追你。跑掉能躲过理智惩罚，
             * 代价是柜子报废 + 它继续在场上，不再是白嫖。
             * ================================================================ */
            if (spot >= 0 && spot < HIDESPOT_MAX && hideSpots[spot].active) {
                hideSpots[spot].broken = 1;
                PlaySfx(SFX_GLASS, 0.7f);
                pickupMsg = (g_lang == LANG_ZH)
                    ? "它把柜门拽坏了 —— 它还在找你"
                    : "IT RIPPED THE LOCKER APART - IT IS STILL HUNTING";
                pickupMsgTimer = 2.2f;
            }
            scavenger.grabSpot = -1;
            scavenger.warnT = 0.0f;
            /* 重置翻箱状态，让它专心追人 */
            scavenger.eating = -1;
            scavenger.searchT = 0.0f;
            scavenger.life = (scavenger.life < 12.0f) ? 12.0f : scavenger.life;
            return;
        }
        return;
    }

    /* ---- ② **每帧**锁定玩家当前位置 ---- */
    retargetToPlayer();

    float dx = scavenger.tx - scavenger.x, dy = scavenger.ty - scavenger.y;
    float d = sqrtf(dx * dx + dy * dy);

    /* ===== v2.4.29 **玩家跑出柜子后被逮到** =====
       条件：玩家不在藏身点里，且清道夫贴到 0.85 格内。
       惩罚：30% 减速 120 秒 + 理智按 25→50 随层成长。
       这是「临阵脱逃失败」的代价 —— 和「被堵在柜子里」区分开：
         躲在里面被抓   = 固定 12 理智 + 柜子被撬坏
         跑出来被抓     = 25~50 理智 + 120 秒瘸腿（但不坏柜子）
    */
    {
        extern int hiddenIn;
        if (hiddenIn < 0) {
            float cdx = posX - scavenger.x, cdy = posY - scavenger.y;
            if (cdx * cdx + cdy * cdy < SCAV_CATCH_R * SCAV_CATCH_R) {
                /* 理智：25 起，按层成长，封顶 50 */
                float loss = SCAV_CATCH_SAN_MIN
                           + SCAV_CATCH_STEP * (float)(currentLevel > 0 ? currentLevel - 1 : 0)
                             * (SCAV_CATCH_SAN_MAX - SCAV_CATCH_SAN_MIN);
                if (loss > SCAV_CATCH_SAN_MAX) loss = SCAV_CATCH_SAN_MAX;
                sanity -= loss * RunDamageTakenMul();
                /* 2026-11-08（第 4 轮）：第一次被抓住时教"按 K 丢钱引开" */
                { extern void HintOnce(int); HintOnce(5); }
                /* 第 9 轮「以牙还牙」：挨了一下之后 3 秒跑更快 */
                { extern int SkillCounterBackOwned(void);
                  extern float retaliateT;
                  if (SkillCounterBackOwned()) retaliateT = 3.0f; }
                if (sanity < 0.0f) sanity = 0.0f;

                /* 减速 30% 持续 120 秒 —— 走现成的 FX_SLOW 通道，
                   game.c 里 slowMul = 1 - FxStrength(FX_SLOW) * 0.45f，
                   所以 0.30 / 0.45 ≈ 0.667 正好得到 30% 减速。 */
                ApplyFx(FX_SLOW, SCAV_CATCH_SLOW / 0.45f, SCAV_CATCH_SLOW_T);

                AddHitFlash(0.80f);
                AddShake(0.30f, 0.22f);
                PlaySfx(SFX_SCREAM, 0.85f);
                pickupMsg = (g_lang == LANG_ZH)
                    ? "它抓住了你！减速 30%，持续 2 分钟"
                    : "IT CAUGHT YOU!  -30% SPEED FOR 2 MIN";
                pickupMsgTimer = 2.2f;
                { extern int floorSpotted, runSpotted;
                  floorSpotted = 1; runSpotted = 1; }
                /* 第 9 轮「反杀」：每局一次，抓住你的瞬间把它打退 */
                {
                    extern int SkillCounterKillOwned(void);
                    extern int counterKillUsed;
                    if (SkillCounterKillOwned() && !counterKillUsed) {
                        counterKillUsed = 1;
                        sanity += loss;          /* 撤销这次掉理智 */
                        if (sanity > 100.0f) sanity = 100.0f;
                        pickupMsg = (g_lang == LANG_ZH)
                            ? "反杀！它被打了回去" : "COUNTERKILL! IT BACKS OFF";
                        pickupMsgTimer = 2.2f;
                        AddShake(0.30f, 0.20f);
                        scavengerLeave();
                        return;
                    }
                }
                scavengerLeave();      /* 抓完就走，进入 40 秒冷却 */
                return;
            }
        }
    }

    /* ---- ③ 到达目标附近 ---- */
    if (d < SCAV_ARRIVE_R) {
        extern int hiddenIn;

        /* (a) 玩家躲着 → 拉柜门 */
        if (hiddenIn >= 0) {
            scavenger.warnT = SCAV_GRAB_WARN;
            scavenger.grabSpot = hiddenIn;
        /* v2.4-beta B02：刮擦声来自柜门外那一边 */
        { extern void AudioPlayWorld(int, float, float, float, float, int);
          AudioPlayWorld(SFX_SCAVENGER, scavenger.x, scavenger.y, 0.85f, 18.0f, 3); }
            PlaySfx(SFX_STEP, 0.9f);
            pickupMsg = (g_lang == LANG_ZH) ? "柜门被抓住了！" : "THE LOCKER IS GRABBED!";
            pickupMsgTimer = 1.4f;
            return;
        }

        /* (b) 玩家没躲 → 翻箱。安全规则：只吃 content == 0（空箱），
               保险丝 / 电池一律不动，宁可不吃也不能让玩家卡关。 */
        if (scavenger.eating < 0 && scavenger.searchT <= 0.0f) {
            extern Container containers[MAX_CONTAINERS];
            extern int containerCount;
            int best = -1;
            float bestD = 1e9f;
            for (int ci = 0; ci < containerCount; ci++) {
                if (!containers[ci].active) continue;
                if (containers[ci].searched) continue;
                if (containers[ci].content != 0) continue;
                float cdx = containers[ci].x - scavenger.x;
                float cdy = containers[ci].y - scavenger.y;
                float cd2 = cdx * cdx + cdy * cdy;
                if (cd2 > 36.0f) continue;               /* 6 格内 */
                if (cd2 < bestD) { bestD = cd2; best = ci; }
            }
            if (best >= 0) {
                scavenger.eating = best;
                scavenger.eatT = 0.0f;
                pickupMsg = (g_lang == LANG_ZH) ? "它在翻箱子！" : "IT IS RIFLING YOUR CRATES!";
                pickupMsgTimer = 1.6f;
            } else {
                /* 没箱子可翻 → 原地翻找 2 秒再走（有动作，不是傻站着） */
                scavenger.searchT = SCAV_SEARCH_TIME;
                pickupMsg = (g_lang == LANG_ZH) ? "它到处翻找…" : "IT IS SEARCHING…";
                pickupMsgTimer = 1.6f;
            }
        }

        /* (c) 原地翻找计时 */
        if (scavenger.searchT > 0.0f) {
            scavenger.searchT -= dt;
            if (scavenger.searchT <= 0.0f) { scavengerLeave(); }
            return;
        }

        /* (d) 吃箱子 */
        if (scavenger.eating >= 0) {
            scavenger.eatT += dt;
            if (scavenger.eatT >= SCAV_EAT_TIME) {
                extern Container containers[MAX_CONTAINERS];
                int ci = scavenger.eating;
                if (ci >= 0 && ci < MAX_CONTAINERS && containers[ci].active &&
                    containers[ci].content == 0) {
                    containers[ci].searched = 1;
                }
                PlaySfx(SFX_GLASS, 0.5f);
                scavengerLeave();
            }
        }
        return;
    }

    /* ---- ④ 沿 BFS 梯度走过去（永远有路）---- */
    /* v2.4.39 **关键修复**：BFS 距离场原来是出生时建一次就不再更新 ——
       于是梯度永远指向「你召唤它那一刻站的位置」，
       你跑开之后它走到原地就停了（用户：「只追到召唤位置」）。
       现在**玩家换格就重建**，梯度永远指向你当前所在。 */
    {
        static int lastPX = -999, lastPY = -999;
        int pxNow = (int)posX, pyNow = (int)posY;
        if (!bfsOK || pxNow != lastPX || pyNow != lastPY) {
            bfsBuild();
            lastPX = pxNow;
            lastPY = pyNow;
        }
    }
    /* ===== v2.4.92 **走路要有声音** =====
       用户要求：「他走路的音效不太好，走路没有声音。
       我需要那种高速摩擦、令人毛骨悚然的声音。」

       做法：
         · 按**实际走过的距离**触发（不是按时间）——
           它被墙挡住时不会继续播脚步，这点很重要
         · 每 0.55 格一步 → 速度 1.55 时大约每 0.35 秒一声
         · 音量随距离衰减（12 格外听不见），但地板/墙的遮挡不做处理
         · 有 5% 的随机跳过，让节奏不机械 */
    {
        float oxp = scavenger.x, oyp = scavenger.y;
        stepAlongBfs(&scavenger.x, &scavenger.y, SCAV_SPEED, dt);

        float mdx = scavenger.x - oxp, mdy = scavenger.y - oyp;
        float moved = sqrtf(mdx * mdx + mdy * mdy);

        scavenger.stepAcc += moved;
        if (scavenger.stepAcc >= 0.55f) {
            scavenger.stepAcc = 0.0f;
            if ((rand() % 100) >= 5) {          /* 5% 跳过，节奏不机械 */
                float dxp = scavenger.x - posX, dyp = scavenger.y - posY;
                float dist = sqrtf(dxp * dxp + dyp * dyp);
                if (dist < 12.0f) {
                    float vol = 1.0f - dist / 12.0f;
                    vol = 0.22f + vol * 0.78f;  /* 远处也留一点，让你听见它在靠近 */
                    /* v2.4-beta B02：脚步带位置 —— 玩家能听出它从哪边绕过来。
                       原来只是按距离调音量，方向信息完全丢失。 */
                    { extern void AudioPlayWorld(int, float, float, float, float, int);
                      AudioPlayWorld(SFX_SCAV_STEP, scavenger.x, scavenger.y,
                                     vol, 12.0f, 1); }
                }
            }
        }
    }
}

/* v2.3.9：训练营用 —— 强制把清道夫摆到指定位置当标本 */
/* ==================================================================
 * 2026-11-08（第 1 轮）：**测试钩子**（生产路径不调用）
 * ------------------------------------------------------------------
 * 玩家反馈的 bug 是"抓柜门时玩家跑出来 → 它凭空消失"。
 * 那条路径要复现必须能设置"它抓住了哪个柜子"并推进预警，
 * 而 scavenger 结构体是文件私有的 —— 所以留这两个钩子。
 * 先例：StoneBreakLockAtTest（game.c）。
 * ================================================================== */
/* 第 2026-11-08：测试用 —— 让它立刻离场（走真实的 scavengerLeave 路径，
   连带把 scavCooldown 和 s_quietT 都重置） */
void ScavengerForceLeaveTest(void) { scavengerLeave(); }
float ScavengerQuietTTest(void) { return s_quietT; }

void ScavengerGrabTest(int spot) {
    scavenger.active = 1;
    scavenger.warnT = SCAV_GRAB_WARN;
    scavenger.grabSpot = spot;
    if (scavenger.life < 20.0f) scavenger.life = 20.0f;
}

int ScavengerWarnExpireTest(void) {
    scavenger.warnT = 0.0001f;
    UpdateScavenger(0.01f);
    return scavenger.active;
}

void ScavengerForcePlace(float x, float y) {
    scavenger.active = 1;
    scavenger.x = x;
    scavenger.y = y;
    scavenger.life = 1e9f;
}

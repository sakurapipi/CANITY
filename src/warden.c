/*
 * Canity — THE WARDEN 守门人（v2.4.42 机制修缮）
 *
 * 修之前的四个实测问题
 * ────────────────────
 *  ① **发现玩家后原地站 25 秒**
 *     代码是一发现就 `blockT = 25`，而 blockT>0 的分支第一件事是
 *     `return;`（站着不动）。它根本还没走到出口就开始"堵门计时"，
 *     于是玩家看到：它发现我了，然后站在原地发呆半分钟，我直接出门。
 *
 *  ② **走到出口要三十多秒**
 *     速度 0.7 格/秒，配电箱到出口常常 20~40 格 → 30~57 秒。
 *     可堵门只持续 25 秒 —— 它还没走到，计时已经结束了。
 *
 *  ③ **巡逻跨房间会卡**
 *     只用 AIMoveToward（直线 + 沿墙滑），隔着墙就蹭住不动。
 *
 *  ④ **冲刺判定永远不成立**
 *     `sprinting = speed > 2.4f`，而玩家冲刺上限就是 2.4 —— 差一个等号，
 *     所以 6 格视野从来没生效过，只有走路时的 3 格。
 *
 * 新设计
 * ──────
 *   四个明确状态：
 *     PATROL   配电箱 ↔ 出口 来回走，速度 0.7（慢，看得见、躲得开）
 *     RUSH     发现玩家 → **用 1.7 的速度走向出口**（这才是威胁）
 *     BLOCK    真正走到出口（≤1.2 格）才开始 25 秒堵门
 *     RETREAT  堵门结束 → 走向离出口 20 格外的点，45 秒冷却
 *
 *   寻路：从出口做一次 BFS，得到「每格到出口多少步」的距离场，
 *         巡逻和赶路都沿梯度走 —— 跨房间也不会卡。
 */
#include <math.h>
#include "common.h"
#include "game.h"
#include "state.h"
#include "ai_common.h"
#include "audio.h"
#include "lang.h"
#include "warden.h"

/* v2.4.51：状态效果（减速） */
extern void ApplyFx(int kind, float strength, float duration);

#define WARDEN_SPEED          0.70f   /* 巡逻速度（全场最慢） */
#define WARDEN_RUSH_SPEED     1.70f   /* 发现玩家后赶去出口的速度 */
#define WARDEN_SIGHT_SPRINT   6.0f    /* 冲刺时多少格内发现 */
#define WARDEN_SIGHT_WALK     3.0f    /* 走路时多少格内发现 */
#define WARDEN_SPRINT_SPEED   2.30f   /* 超过这个算冲刺（原来写 2.4，永远不成立） */
#define WARDEN_BLOCK_TIME    18.0f    /* v2.4.48 平调：25 → 18 秒。
                                       25 秒是"被迫站着等"，18 秒还能忍。 */    /* 堵门持续 */
#define WARDEN_MAX_BLOCKS      2      /* v2.4.49 **一局最多堵两次**。
                                         用户实测：它堵在窄走廊里直接把关卡堵死，
                                         过不去 = 卡关。加次数上限是最直接的保险。 */
#define WARDEN_COOLDOWN      35.0f    /* v2.4.48 平调：45 → 35 秒。 */    /* 解除后多少秒内不再堵门 */
#define WARDEN_RETREAT       20.0f    /* 解除后至少离开出口多少格 */
#define WARDEN_EXIT_R         1.80f   /* v2.4.48 平调：2.2 → 1.8。
                                        出口格本身 + 半格余量，路过不该被吞。 */    /* v2.4.44 玩家进出口区域判定半径（3.0 太大，路过就被传） */
#define WARDEN_TOUCH_R        1.30f   /* v2.4.48 平调：2.20 -> 1.30。2.2 格读感像空气墙 */
/* ===== v2.4.47 **视野锥 + 5 秒追击 / 5 秒失明** =====
   用户要求：
     · 给守门人一个视野（锥形，不是 360 度）
     · 进入追击后 **5 秒内无代价直接看到玩家**（追你，不靠视线）
     · 5 秒后**失去视野**，持续 5 秒（这段时间它变白 = 没电形态）
     · 5 秒失明结束后重新开始
   这条设计让它变成「周期性压迫」而不是「一直知道你在哪」。 */
#define WARDEN_CHASE_TIME      5.0f    /* 追击持续 */
#define WARDEN_LOST_TIME       5.0f    /* 失去视野持续 */
#define WARDEN_CONE_DIST       8.0f    /* v2.4.48 平调：9 → 8 格 */    /* 视野锥长度 */
#define WARDEN_CONE_HALF       0.60f
/* ===== v2.4.52 **听觉** =====
   用户要求「给守门人增加一点听觉，这样更灵敏」。
   视觉是锥形（必须朝着你、必须看见），听觉是全向的（穿墙也能听见）。

     冲刺  听 7 格      ← 跑步声在走廊里传得很远
     走路  听 4 格
     静止  听 0 格      ← 站着不动它听不到

   听觉不需要累积时间（声音是瞬间的），听到就直接进入追击。 */
#define WARDEN_HEAR_SPRINT     7.0f
#define WARDEN_HEAR_WALK       4.0f   /* v2.4.48 平调：半角 41° → 34°（全角 69°）。
                                       82° 太宽，几乎"正前方 90 度全能看见"。 */   /* 视野锥半角 ≈ 41°（全角 82°） */
#define WARDEN_CHASE_SPEED     2.15f   /* v2.4.88：1.75 -> 2.15。
                                          玩家走 2.0 / 跑 2.4，2.15 是
                                          「你必须跑起来才甩得掉」的档位。 */   /* v2.4.48 平调：1.90 → 1.75。
                                       玩家走 2.0 / 跑 2.4，1.75 让"慢走也能拉开一点"，
                                       但停下来就会被追上。 */   /* 追击速度（比巡逻快，仍慢于玩家走速 2.0） */   /* v2.4.46 用户要求：不加移速，**加大攻击范围**（1.0→2.2） */   /* v2.4.44 **贴身判定**：走到它身上会被推开 */
#define WARDEN_ARRIVE_EXIT    1.2f    /* 它自己到出口多近算「站到位可以堵了」 */
#define WARDEN_ARRIVE_PT      0.9f    /* 巡逻端点到达判定 */
#define WARDEN_MOVE_EPS       0.02f

typedef enum { WD_PATROL = 0, WD_RUSH, WD_BLOCK, WD_RETREAT, WD_CHASE, WD_LOST } WardenState;

typedef struct {
    int   active;
    float x, y;
    float tx, ty;
    int   headingToExit;   /* 巡逻方向：1 = 正朝出口走 */
    WardenState state;
    float blockT;
    float coolT;
    float sightHold;
    float stuckT;
    float lastX, lastY;
    float facing;          /* 朝向弧度（视野锥指向） */
    float stateT;          /* 当前状态已持续多久 */
    int   blockCount;      /* v2.4.49：本局已经堵了几次门（上限 2） */
    float hearNow;         /* v2.4.53：当前听觉半径（0 = 听不到），调试用 */
} Warden;

Warden warden;

/* ---- BFS 距离场（以出口为源点）---- */
static short wdB[MAP_HEIGHT][MAP_WIDTH];

static void wdBfsBuild(void) {
    extern float exitX, exitY;
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            wdB[y][x] = -1;

    int sx = (int)exitX, sy = (int)exitY;
    if (sx < 0 || sx >= MAP_WIDTH || sy < 0 || sy >= MAP_HEIGHT) return;

    static int qx[MAP_WIDTH * MAP_HEIGHT];
    static int qy[MAP_WIDTH * MAP_HEIGHT];
    int head = 0, tail = 0;

    wdB[sy][sx] = 0;
    qx[tail] = sx; qy[tail] = sy; tail++;

    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };

    while (head < tail) {
        int cx = qx[head], cy = qy[head];
        head++;
        int nd = wdB[cy][cx] + 1;
        if (nd > 220) continue;
        for (int i = 0; i < 4; i++) {
            int nx = cx + dxs[i], ny = cy + dys[i];
            if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
            if (worldMap[ny][nx] != 0) continue;
            if (wdB[ny][nx] >= 0) continue;
            wdB[ny][nx] = (short)nd;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
}

/* 沿「到出口的 BFS 梯度」走一步；climb=1 则反着走（远离出口） */
static void wdStepGradient(float* x, float* y, float sp, float dt, int climb) {
    int cx = (int)(*x), cy = (int)(*y);
    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return;

    int here = wdB[cy][cx];
    if (here < 0) { wdBfsBuild(); return; }

    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };

    int bestI = -1, bestD = here;
    for (int i = 0; i < 4; i++) {
        int nx = cx + dxs[i], ny = cy + dys[i];
        if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
        int d = wdB[ny][nx];
        if (d < 0) continue;
        if (climb ? (d > bestD) : (d < bestD)) { bestD = d; bestI = i; }
    }
    if (bestI < 0) {
        /* 已经在端点（堵门位置 / 最远处）→ 原地不动 */
        return;
    }

    /* ===== v2.4.88 **修「卡脚」** =====
       用户反馈：守门人有时候会卡住，导致追击速度跟不上。

       根因：原来只朝"最陡的那一个邻格"走，而 AIMoveToward 撞墙就停。
       站在墙角时那个格心可能在斜对角，直线过去正好被墙角挡住 ——
       于是一直贴着墙不动。

       现在按优先级试四个方向：
         ① 最陡邻格
         ② 次陡邻格
         ③ 只走 X（沿墙滑）
         ④ 只走 Y
       还不行就累计 stuckT，超过 0.5 秒重建距离场并侧移一小步。 */
    float ox = *x, oy = *y;

    /* 收集候选：按陡度排序 */
    int cand[4], nc = 0;
    for (int i = 0; i < 4; i++) {
        int nx = cx + dxs[i], ny = cy + dys[i];
        if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
        int d = wdB[ny][nx];
        if (d < 0) continue;
        if (!(climb ? (d > here) : (d < here))) continue;
        cand[nc++] = i;
    }
    /* 最陡的排前面 */
    for (int a = 0; a < nc; a++) {
        for (int b = a + 1; b < nc; b++) {
            int da = wdB[cy + dys[cand[a]]][cx + dxs[cand[a]]];
            int db = wdB[cy + dys[cand[b]]][cx + dxs[cand[b]]];
            int better = climb ? (db > da) : (db < da);
            if (better) { int t = cand[a]; cand[a] = cand[b]; cand[b] = t; }
        }
    }

    /* ① 依次尝试每个候选格心 */
    for (int i = 0; i < nc; i++) {
        float tx = (float)(cx + dxs[cand[i]]) + 0.5f;
        float ty = (float)(cy + dys[cand[i]]) + 0.5f;
        AIMoveToward(x, y, tx, ty, sp, dt);
        if (*x != ox || *y != oy) {
            warden.stuckT = 0.0f;
            return;
        }
    }

    /* ② 沿墙滑：只走 X 或只走 Y */
    {
        float step = sp * dt;
        float dx = 0.0f, dy = 0.0f;
        if (nc > 0) {
            dx = dxs[cand[0]] * step;
            dy = dys[cand[0]] * step;
        } else {
            dx = step; dy = step;
        }
        if (!Blocked(*x + dx, *y)) { *x += dx; warden.stuckT = 0.0f; return; }
        if (!Blocked(*x, *y + dy)) { *y += dy; warden.stuckT = 0.0f; return; }
        /* 两个轴各自再试一次 */
        if (!Blocked(*x + step, *y)) { *x += step; warden.stuckT = 0.0f; return; }
        if (!Blocked(*x, *y + step)) { *y += step; warden.stuckT = 0.0f; return; }
        if (!Blocked(*x - step, *y)) { *x -= step; warden.stuckT = 0.0f; return; }
        if (!Blocked(*x, *y - step)) { *y -= step; warden.stuckT = 0.0f; return; }
    }

    /* ③ 真的卡住了：重建距离场，并把自己推到当前格中心 */
    warden.stuckT += dt;
    if (warden.stuckT > 0.5f) {
        warden.stuckT = 0.0f;
        wdBfsBuild();
        float ccx = (float)cx + 0.5f, ccy = (float)cy + 0.5f;
        if (!Blocked(ccx, ccy)) { *x = ccx; *y = ccy; }
    }
}

int WardenWatching(void) { return warden.active && (warden.state==WD_CHASE || warden.state==WD_BLOCK); }
int   WardenActive(void)  { return warden.active; }
float WardenX(void)       { return warden.x; }

float WardenSpeedNow(void)      { return WARDEN_SPEED; }
float WardenChaseSpeedNow(void) { return WARDEN_CHASE_SPEED; }

float WardenY(void)       { return warden.y; }
int   WardenBlocking(void){ return warden.state == WD_BLOCK && warden.blockT > 0.0f; }

void WardenReset(void) {
    warden.active = 0;
    warden.x = warden.y = 0.0f;
    warden.tx = warden.ty = 0.0f;
    warden.headingToExit = 0;
    warden.state = WD_PATROL;
    warden.blockT = 0.0f;
    warden.coolT = 0.0f;
    warden.sightHold = 0.0f;
    warden.stuckT = 0.0f;
    warden.lastX = warden.lastY = 0.0f;
    warden.facing = 0.0f;
    warden.stateT = 0.0f;
    warden.blockCount = 0;
}

/* 通电之后才出现：站在配电箱旁边 */
void WardenSpawnIfPowered(void) {
    extern int breakerOpen;
    extern Breaker breaker;
    extern float exitX, exitY;

    if (!breakerOpen) return;
    if (!breaker.active) {
        /* v6 战役第 3 关：只要通电，不等 breaker.active 的防守计时 */
        extern int CampaignWardenImmediate(void);
        if (!CampaignWardenImmediate()) return;
    }
    if (warden.active) return;          /* v2.4.42：已经在了就别重置 */

    WardenReset();

    /* ===== v2.4.46 **改成出生在出口，不再出生在配电箱旁边** =====
       用户实测：「刚打开配电箱的时候应该刷新在出口，
       不然刷新在配电箱直接给我送走了」——
       配电箱旁边就是玩家，一出生 1 格内就被推开，属于设计事故。
       现在它一开始就站在出口（也就是它要守的地方）。 */

    /* ===== v2.4.43 **出生点确定性扫描（保留作为兜底）** =====
       原来用 60 次随机采样在配电箱周围 1.6 格找空地。
       可配电箱通常是**贴墙**放的，1.6 格半径内大部分方向都是墙，
       随机撞几次就全落空 → `if (!found) return;` → **守门人根本不出现**。
       （用户实测：「完全失效，一点作用没有」。）

       现在：
         ① 绕配电箱 16 个方向 × 3 个半径（1.2 / 1.8 / 2.6）逐个试
         ② 还不行就找配电箱**所在格的四邻空地**
         ③ 再不行就直接站在配电箱格上（贴脸也能用） */
    float ox = exitX, oy = exitY;
    int found = 0;

    for (int ring = 0; ring < 3 && !found; ring++) {
        float rad = 1.2f + (float)ring * 0.7f;
        for (int i = 0; i < 16 && !found; i++) {
            float a = (float)i * (6.28318f / 16.0f);
            float x = exitX + cosf(a) * rad;
            float y = exitY + sinf(a) * rad;
            if (x < 1.2f || y < 1.2f) continue;
            if (x > MAP_WIDTH - 1.2f || y > MAP_HEIGHT - 1.2f) continue;
            if (Blocked(x, y)) continue;
            ox = x; oy = y; found = 1;
        }
    }
    if (!found) {
        int bx = (int)exitX, by = (int)exitY;
        static const int ddx[4] = { 1, -1, 0, 0 };
        static const int ddy[4] = { 0, 0, 1, -1 };
        for (int i = 0; i < 4 && !found; i++) {
            int nx = bx + ddx[i], ny = by + ddy[i];
            if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
            if (worldMap[ny][nx] != 0) continue;
            ox = (float)nx + 0.5f;
            oy = (float)ny + 0.5f;
            found = 1;
        }
    }
    if (!found) {
        ox = exitX;          /* 最后兜底：直接站在出口格 */
        oy = exitY;
        found = 1;
    }

    warden.x = ox;
    warden.y = oy;
    warden.tx = exitX;
    warden.ty = exitY;
    warden.headingToExit = 1;
    warden.state = WD_PATROL;
    warden.active = 1;
    warden.lastX = ox;
    warden.lastY = oy;
    wdBfsBuild();

    { extern void BestiaryMarkSeen(int); BestiaryMarkSeen(5); }
    PlaySfx(SFX_STEP, 0.5f);
}

/* ===== v2.4.46 **把玩家送回「离出口最远的位置」** =====
   用户要求：传送回出生点的机制改为传送离出口最远的位置。
   原来硬编码 (2.5, 2.5) —— 那是地图左上角，跟出口在哪毫无关系，
   有时候把你传到离出口只有两步的地方，等于没惩罚。

   新做法：
     ① 遍历全图，找**空地格中心**里「离出口最远」的那个
     ② 用 BFS（wdB）的距离场 —— 用**走的步数**而不是直线距离，
        这样不会被「隔着一堵墙很近」骗到
     ③ 找不到就退回扫描直线距离最远的格

   不是死亡、不掉理智，只是把你搬到最远的地方。 */
static void SendPlayerToStart(void) {
    extern float posX, posY;
    extern float exitX, exitY;
    extern int   resetInputState;

    float bestX = 2.5f, bestY = 2.5f;
    int bestD = -1;

    if (!wdB[0][0] && wdB[0][0] < 0) {
        /* 距离场可能是空表 —— 建一次 */
    }
    wdBfsBuild();

    for (int gy = 1; gy < MAP_HEIGHT - 1; gy++) {
        for (int gx = 1; gx < MAP_WIDTH - 1; gx++) {
            if (worldMap[gy][gx] != 0) continue;
            int d = wdB[gy][gx];
            if (d < 0) continue;                    /* 不可达，别传送过去 */
            if (d > bestD) {
                bestD = d;
                bestX = (float)gx + 0.5f;
                bestY = (float)gy + 0.5f;
            }
        }
    }

    /* 兜底：BFS 全是 -1 时，用直线距离找最远 */
    if (bestD < 0) {
        float farD2 = -1.0f;   /* 注意：不能用 far，那是 Windows 的老宏 */
        for (int gy = 1; gy < MAP_HEIGHT - 1; gy++) {
            for (int gx = 1; gx < MAP_WIDTH - 1; gx++) {
                if (worldMap[gy][gx] != 0) continue;
                float dx = (float)gx + 0.5f - exitX;
                float dy = (float)gy + 0.5f - exitY;
                float d2 = dx * dx + dy * dy;
                if (d2 > farD2) {
                    farD2 = d2;
                    bestX = (float)gx + 0.5f;
                    bestY = (float)gy + 0.5f;
                }
            }
        }
    }

    posX = bestX;
    posY = bestY;
    resetInputState = 1;
    AddHitFlash(0.6f);
    AddShake(0.22f, 0.18f);
    PlaySfx(SFX_SCREAM, 0.55f);
    pickupMsg = (g_lang == LANG_ZH) ? "被扔到了离出口最远的地方"
                                    : "THROWN TO THE FARTHEST POINT";
    pickupMsgTimer = 2.2f;
}


/* v2.4.54：调试用的 WardenStateName / WardenBlockT 已删除 */
/* v2.4.47：渲染层用 —— 追击中变红、失明中变白（没电形态） */
int WardenChasing(void) { return warden.state == WD_CHASE; }
int WardenLost(void)    { return warden.state == WD_LOST; }

void UpdateWarden(float dt) {
    { extern int trainingMode; if (trainingMode) return; }
    if (!warden.active) return;

    extern float posX, posY;
    extern float exitX, exitY;
    extern float playerMoveSpeedNow;

    if (warden.coolT > 0.0f) {
        warden.coolT -= dt;
        if (warden.coolT < 0.0f) warden.coolT = 0.0f;
    }

    /* ===== v2.4.51 **只有堵门才推人** =====
       用户反复反馈「存在堵门」—— 之前 CHASE 状态（追你）也会推人，
       而它追你的时候往往就在走廊里，一次推送 = 把你扔到地图另一头，
       在窄走廊里读起来就是"被它堵住过不去"。

       现在把两件事拆开：
         BLOCK  守在出口 → 你进出口范围 → 推回最远处（这是机制本体）
         CHASE  追上你   → **不传送**，只让你减速 40% 持续 4 秒
       这样它永远不会「堵住一条路」，只会「守住一个点」。 */
    if (warden.state == WD_BLOCK) {
        float tdx = posX - warden.x, tdy = posY - warden.y;
        if (tdx * tdx + tdy * tdy < WARDEN_TOUCH_R * WARDEN_TOUCH_R) {
            SendPlayerToStart();
            pickupMsg = (g_lang == LANG_ZH) ? "守门人把你推开了" : "THE WARDEN SHOVES YOU BACK";
            pickupMsgTimer = 2.0f;
            return;
        }
    }

    /* ============ 状态机 ============ */
    switch (warden.state) {

    /* ---------- 堵门：站在出口，计时 ---------- */
    case WD_BLOCK: {
        warden.blockT -= dt;

        /* 玩家走进出口区域 → 送回起点 */
        float pdx = posX - exitX, pdy = posY - exitY;
        if (pdx * pdx + pdy * pdy < WARDEN_EXIT_R * WARDEN_EXIT_R) {
            SendPlayerToStart();
            pickupMsg = (g_lang == LANG_ZH) ? "出口被守门人堵住了" : "THE WARDEN GUARDS THE EXIT";
            pickupMsgTimer = 2.0f;
        }

        if (warden.blockT <= 0.0f) {
            /* 解除 → 撤退 */
            warden.blockT = 0.0f;
            warden.coolT = WARDEN_COOLDOWN;
            warden.state = WD_RETREAT;
            float dx = warden.x - exitX, dy = warden.y - exitY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 0.01f) { dx = 1.0f; dy = 0.0f; d = 1.0f; }
            warden.tx = exitX + (dx / d) * WARDEN_RETREAT;
            warden.ty = exitY + (dy / d) * WARDEN_RETREAT;
        }
        return;
    }

    /* ---------- 撤退：走远，冷却 ---------- */
    case WD_RETREAT: {
        wdStepGradient(&warden.x, &warden.y, WARDEN_SPEED, dt, 1);

        /* 离出口够远了 → 回到巡逻 */
        float dx = warden.x - exitX, dy = warden.y - exitY;
        if (dx * dx + dy * dy >= WARDEN_RETREAT * WARDEN_RETREAT * 0.6f) {
            warden.state = WD_PATROL;
            warden.headingToExit = 1;
            warden.tx = exitX;
            warden.ty = exitY;
        }
        /* 巡逻端点也要更新一下，免得刚撤退完就往出口跑 */
        return;
    }

    /* ---------- 追击：5 秒内无代价直接看到你 ---------- */
    case WD_CHASE: {
        warden.stateT += dt;
        if (warden.stateT >= WARDEN_CHASE_TIME) {
            /* 5 秒到了 → 失去视野，变白（没电形态），停 5 秒 */
            warden.state = WD_LOST;
            warden.stateT = 0.0f;
            pickupMsg = (g_lang == LANG_ZH) ? "守门人失去了视野"
                                            : "THE WARDEN LOST SIGHT";
            pickupMsgTimer = 1.8f;
            PlaySfx(SFX_STEP, 0.6f);
            return;
        }
        /* **无代价追踪**：不管视线、不管你在哪，直接朝你走 */
        AIMoveToward(&warden.x, &warden.y, posX, posY, WARDEN_CHASE_SPEED, dt);
        {
            float fx = posX - warden.x, fy = posY - warden.y;
            if (fx * fx + fy * fy > 0.01f) warden.facing = atan2f(fy, fx);
        }
        /* v2.4.52 **抓到就传送回去**（恢复）
           上一版为了防"走廊堵路"把传送改成了减速，
           结果用户反馈「把我传送走的技能失效」——
           传送是守门人的核心威胁，不能拿掉。
           防堵路已经由「巡逻不推人 + 一局最多堵 2 次」解决了。 */
        {
            float tdx = posX - warden.x, tdy = posY - warden.y;
            if (tdx * tdx + tdy * tdy < WARDEN_TOUCH_R * WARDEN_TOUCH_R) {
                SendPlayerToStart();
                pickupMsg = (g_lang == LANG_ZH) ? "守门人抓住了你！" : "THE WARDEN CAUGHT YOU!";
                pickupMsgTimer = 2.2f;
                warden.state = WD_LOST;      /* 抓住之后它也「泄力」5 秒 */
                warden.stateT = 0.0f;
            }
        }
        return;
    }

    /* ---------- 失明：5 秒，白模，重新开始 ---------- */
    case WD_LOST: {
        warden.stateT += dt;
        if (warden.stateT >= WARDEN_LOST_TIME) {
            warden.state = WD_PATROL;
            warden.stateT = 0.0f;
            warden.headingToExit = 1;
            warden.tx = exitX;
            warden.ty = exitY;
        }
        return;
    }

    /* ---------- 赶去出口：发现玩家之后 ---------- */
    case WD_RUSH: {
        float dx = exitX - warden.x, dy = exitY - warden.y;
        float d = sqrtf(dx * dx + dy * dy);

        if (d <= WARDEN_ARRIVE_EXIT) {
            /* v2.4.49：次数用完就不再堵，直接回去巡逻（避免卡关） */
            if (warden.blockCount >= WARDEN_MAX_BLOCKS) {
                warden.state = WD_PATROL;
                warden.stateT = 0.0f;
                warden.headingToExit = 0;
                warden.tx = exitX;
                warden.ty = exitY;
                return;
            }
            /* **真正站到位了才开计时** —— 这是修 bug① 的关键 */
            warden.state = WD_BLOCK;
            warden.blockCount++;
            warden.blockT = WARDEN_BLOCK_TIME;
            PlaySfx(SFX_HEART, 0.8f);
            pickupMsg = (g_lang == LANG_ZH) ? "守卫堵住了出口" : "THE WARDEN BLOCKS THE EXIT";
            pickupMsgTimer = 2.0f;
            return;
        }
        wdStepGradient(&warden.x, &warden.y, WARDEN_RUSH_SPEED, dt, 0);
        return;
    }

    /* ---------- 巡逻 ---------- */
    case WD_PATROL:
    default: {
        break;
    }
    }

    /* ============ 感知：**视野锥** ============ */
    if (warden.coolT <= 0.0f) {
        float mdx = posX - warden.x, mdy = posY - warden.y;
        float md2 = mdx * mdx + mdy * mdy;

        int inCone = 0;
        if (md2 < WARDEN_CONE_DIST * WARDEN_CONE_DIST) {
            float angTo = atan2f(mdy, mdx);
            float diff = angTo - warden.facing;
            /* 归一到 -pi..pi */
            while (diff >  3.14159265f) diff -= 6.28318530f;
            while (diff < -3.14159265f) diff += 6.28318530f;
            if (diff < 0.0f) diff = -diff;

            /* 锥内**且**玩家在动（静止仍然看不见）*/
            extern float playerMoveSpeedNow;
            if (diff <= WARDEN_CONE_HALF && playerMoveSpeedNow > WARDEN_MOVE_EPS) {
                inCone = 1;
            }
        }

        /* ---- v2.4.53 **听觉：全向、穿墙、**立刻触发** ---- ----
           上一版让听觉和视觉共用 0.28 秒的累积计时，
           可声音是瞬间的 —— 你跑过去的那一下它早该听见了，
           累积计时反而让它"迟钝"（用户：「听觉系统还是不行」）。
           现在：听到就进追击，不累积。 */
        int heard = 0;
        {
            extern float playerMoveSpeedNow;
            float hearR = (playerMoveSpeedNow >= WARDEN_SPRINT_SPEED) ? WARDEN_HEAR_SPRINT
                        : (playerMoveSpeedNow > WARDEN_MOVE_EPS)      ? WARDEN_HEAR_WALK
                        : 0.0f;
            warden.hearNow = hearR;
            if (hearR > 0.0f && md2 < hearR * hearR) heard = 1;
        }

        /* 视觉仍然累积 0.28 秒（要"看一眼"才确认） */
        if (inCone) warden.sightHold += dt; else warden.sightHold = 0.0f;

        if (heard || (inCone && warden.sightHold >= 0.28f)) {
            warden.sightHold = 0.0f;
            warden.state = WD_CHASE;
            PlaySfx(SFX_WARDEN, 1.0f);   /* v2.4.89 追击低频吼叫 */          /* ← 直接进入 5 秒追击 */
            warden.stateT = 0.0f;
            PlaySfx(SFX_HEART, 0.85f);
            pickupMsg = heard
                ? ((g_lang == LANG_ZH) ? "守门人听见你了！" : "THE WARDEN HEARD YOU")
                : ((g_lang == LANG_ZH) ? "守门人盯上你了！" : "THE WARDEN HAS YOU IN SIGHT");
            pickupMsgTimer = 2.0f;
            return;
        }
    }

    /* ============ 巡逻：**沿 BFS 梯度在出口 6~8 步之间来回** ============
       v2.4.51 重写。之前的问题：
         · 「往外走」用的是 AIMoveToward 直冲一个乘法算出来的点
           （exitX + (w.x-exitX)*3.0f），这个点可能算到墙里 ——
           目标进墙 → 它永远到不了 → 原地蹭墙，看着像"路径有问题"
         · 两个方向只有回家那一段走梯度

       现在两个方向都走梯度：
         出去  wdStepGradient(climb=1)   沿距离场往"离出口更远"爬
         回来  wdStepGradient(climb=0)   沿距离场往"离出口更近"降
       没有目标坐标，也就不存在"目标点进墙"这种问题。 */
    {
        int wcx = (int)warden.x, wcy = (int)warden.y;
        int st = (wcx >= 0 && wcy >= 0 && wcx < MAP_WIDTH && wcy < MAP_HEIGHT)
                 ? wdB[wcy][wcx] : -1;

        if (st < 0) {
            /* 掉出距离场（极少见）→ 重建，并往出口挪一步 */
            wdBfsBuild();
            AIMoveToward(&warden.x, &warden.y, exitX, exitY, WARDEN_SPEED, dt);
            return;
        }

        /* 到达外圈 → 掉头；回到出口附近 → 再出去 */
        if (!warden.headingToExit && st >= 7)  warden.headingToExit = 1;
        if (warden.headingToExit  && st <= 1)  warden.headingToExit = 0;

        float oldX = warden.x, oldY = warden.y;
        wdStepGradient(&warden.x, &warden.y, WARDEN_SPEED, dt,
                       warden.headingToExit ? 0 : 1);

        /* 朝向跟着位移走 —— 视野锥靠它 */
        float mvx = warden.x - oldX, mvy = warden.y - oldY;
        if (mvx * mvx + mvy * mvy > 1e-6f) warden.facing = atan2f(mvy, mvx);
    }
}


/* 训练营用 —— 强制摆到指定位置当标本 */
void WardenForcePlace(float x, float y) {
    warden.active = 1;
    warden.x = x;
    warden.y = y;
    warden.state = WD_PATROL;
    warden.blockT = 0.0f;
}

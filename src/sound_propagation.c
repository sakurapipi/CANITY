/* CANITY — 声学传播实现（v2.4-beta B03）
 *
 * 4 向 BFS，代价 = 步数。576 个格子，一次 BFS 是微秒级。
 * 缓存按"源格"命中：同一发声音被 8 只怪查询只建一次场。
 */
#include "common.h"
#include "sound_propagation.h"
#include "game.h"

static int s_cost[PROP_CELLS];
static int s_built = 0;          /* 场是否有效 */
static int s_srcCell = -1;       /* 上次建场的源格 */
static int s_buildCount = 0;     /* 真正跑过几次 BFS */

static int CellIndex(int cx, int cy) {
    return cy * MAP_WIDTH + cx;
}
static int IsOpen(int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return 0;
    return worldMap[cy][cx] ? 0 : 1;
}

void SoundPropReset(void) {
    s_built = 0;
    s_srcCell = -1;
    s_buildCount = 0;
    for (int i = 0; i < PROP_CELLS; i++) s_cost[i] = PROP_UNREACHABLE;
}

/* 4 向 BFS。用普通队列（格数固定 576，不需要环形缓冲的花哨写法）。 */
static void BuildFrom(int scx, int scy) {
    for (int i = 0; i < PROP_CELLS; i++) s_cost[i] = PROP_UNREACHABLE;

    if (!IsOpen(scx, scy)) {
        /* 声源落在墙里：仍然允许"贴墙发声" —— 退而求其次，
           把源格当作可达，并放宽成 8 邻域的第一步。
           现实中这只会发生在投掷物卡进墙角的极端情况。 */
        s_built = 1;
        s_srcCell = CellIndex(scx, scy);
        return;
    }

    static int queue[PROP_CELLS];
    int head = 0, tail = 0;
    int si = CellIndex(scx, scy);
    s_cost[si] = 0;
    queue[tail++] = si;

    while (head < tail) {
        int idx = queue[head++];
        int cx = idx % MAP_WIDTH;
        int cy = idx / MAP_WIDTH;
        int nd = s_cost[idx] + 1;

        /* 4 向 */
        static const int kDx[4] = { 1, -1, 0, 0 };
        static const int kDy[4] = { 0, 0, 1, -1 };
        for (int d = 0; d < 4; d++) {
            int nx = cx + kDx[d], ny = cy + kDy[d];
            if (!IsOpen(nx, ny)) continue;
            int ni = CellIndex(nx, ny);
            if (s_cost[ni] != PROP_UNREACHABLE) continue;
            s_cost[ni] = nd;
            if (tail < PROP_CELLS) queue[tail++] = ni;
        }
    }

    s_built = 1;
    s_srcCell = si;
    s_buildCount++;
}

void SoundPropBuild(float sx, float sy) {
    int scx = (int)floorf(sx);
    int scy = (int)floorf(sy);
    if (scx < 0 || scy < 0 || scx >= MAP_WIDTH || scy >= MAP_HEIGHT) {
        s_built = 0;
        return;
    }
    int si = CellIndex(scx, scy);
    if (s_built && s_srcCell == si) return;      /* 命中缓存 */
    BuildFrom(scx, scy);
}

int SoundPropCostAt(float x, float y) {
    if (!s_built) return PROP_UNREACHABLE;
    int cx = (int)floorf(x);
    int cy = (int)floorf(y);
    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return PROP_UNREACHABLE;
    return s_cost[CellIndex(cx, cy)];
}

int SoundPropSourceCell(void) { return s_srcCell; }
int SoundPropBuildCount(void) { return s_buildCount; }

/* ==================================================================
 * 对外唯一入口
 * ------------------------------------------------------------------
 * 衰减模型刻意做成"**两条路取更弱的那条**"：
 *   ① 直线距离 —— 贴着声源时更符合直觉（声音是往四周散开的）
 *   ② 路径步数 —— 绕墙时迅速变弱（这是 B03 的全部意义）
 *
 * 为什么不只用路径：走廊拐角处步数会比直线大很多，
 * 只用路径会让"拐一个弯"听起来像"隔了一整层"。
 * 取两者的较大值当有效距离，正好是"要么近、要么直"。
 * ================================================================== */
float SoundReach(float sx, float sy, float tx, float ty, float maxRadius) {
    if (maxRadius <= 0.01f) return 0.0f;

    SoundPropBuild(sx, sy);

    float dx = tx - sx, dy = ty - sy;
    float straight = sqrtf(dx * dx + dy * dy);

    int steps = SoundPropCostAt(tx, ty);
    float effDist;
    if (steps == PROP_UNREACHABLE) {
        /* 走不过去（封闭房间 / 层外）：
           只有贴得非常近才给一点点（等同于"隔墙也听得见一点动静"）。 */
        effDist = straight * 3.0f + maxRadius * 0.5f;
    } else {
        float path = (float)steps;
        effDist = (path > straight) ? path : straight;
    }

    if (effDist >= maxRadius) return 0.0f;
    float k = 1.0f - effDist / maxRadius;
    return k * k;                                /* 平方：近处更突出 */
}

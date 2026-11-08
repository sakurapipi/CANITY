/* ==================================================================
 * CANITY — 主题房间放置（v2.4-rc C3）
 * ------------------------------------------------------------------
 * 位置：在原有「5 个 2×2 随机小房间」**之后**调用。
 *   这样开关关闭时地图与旧版**逐位相同** —— 旧代码一行没动。
 *   房间随后覆盖上去，所以房间里的柱子不会被 blob 打穿。
 *
 * 放不下怎么办（计划 D2 / D4）
 *   任何一条检查不过就**跳过这一个房间**，剩下的 blob 照旧。
 *   绝不因为房间放不下就让整层生成失败。
 *
 * 保留区
 *   出生区 1..3、出口周围 3 格、已放置房间周围 1 格。
 *   配电箱/容器那时还没放置，所以不在检查范围 —— 由 C4 的资源规划
 *   反过来避让房间（房间更"硬"，先放）。
 * ================================================================ */
#include "common.h"
#include "hatch.h"
#include "cell_meta.h"
#include "room_template.h"
#include "map_rooms.h"
#include "core_rng.h"
#include "game.h"

/* ==================================================================
 * 2026-11-08（第 6 轮）：**关卡重放要能跨版本复现**
 * ------------------------------------------------------------------
 * 房间生成的全部随机都走这里。老存档（rngVersion=1）走 C 标准库 rand()
 * 序列 —— 和玩家当年看到的房间逐格一致；新存档（2）走 xorshift32，
 * 跨平台（Windows / Emscripten）序列相同。
 * ================================================================ */
static unsigned int RoomRand(void) {
    extern unsigned int rng_world_next(void);
    return rng_world_next();
}

extern int explored[MAP_HEIGHT][MAP_WIDTH];

#define ROOM_KEEP_EXIT   3      /* 出口周围 3 格不许压 */
#define ROOM_KEEP_SPAWN  4      /* 出生区 1..3，外扩 1 格 */
#define ROOM_MIN_GAP     1      /* 房间之间至少隔 1 格 */
/* 2026-11-06（R5 实测修正）：**贴外墙的房间读起来不像房间**。
   地图边界 1 格是外墙，房间若紧挨着它，外侧就没有"自己的墙"，
   看起来只是走廊变宽。留 2 格边距，房间四围都是真正的内墙。 */
#define ROOM_MARGIN      2

/* 房间必须**挖进实心岩体**：候选矩形里至少 60% 当前是墙。
   挖在已有空地上 = 把一条走廊扩宽，看不出是房间；
   挖在实心区里 = 出现一个独立空间，四周都是自己挖出来的墙。 */
#define ROOM_SOLID_NUM   6
#define ROOM_SOLID_DEN   10
/* ==================================================================
 * 2026-11-06（R5 第三次修正）：**房间自带围墙和门**
 * ------------------------------------------------------------------
 * 前两版都在纠结"放在哪"：
 *   挖在空地上 → 和走廊连成一片，里面什么也看不出来
 *   只在实心岩体里挖 → 98% 的层一个都放不下（实测）
 *
 * 这两个方向都不对。真正的问题是**模板没有自己的围墙**。
 * 现在改成：
 *
 *   ┌─────────────┐  ← 外圈是墙（画在模板外面，程序生成）
 *   │ ┌─────────┐ │
 *   │ │ 模板瓦片 │ │  ← 内圈才是模板内容（柱子/货架/隔断）
 *   │ └─────────┘ │
 *   └──┬───────┬──┘  ← 两个门（相对的两边，中间各一格）
 *      │       │
 *      └───────┴──── 从门向外挖 2 格，接到迷宫上
 *
 * 这样一来：
 *   · 房间是**围起来的**，不会和走廊连成一片 → 「里面没有东西」消失
 *   · 模板的柱子货架是**在围墙里面**的 → 看得见、有意义
 *   · 门 + 向外 2 格连接 → **不可能出现封死的房间**
 *   · 放在哪都行 → 不再需要"实心岩体"这个几乎不存在的条件
 *
 * 代价：占地变成 (w+2)×(h+2)。5×5 的模板占 7×7 = 49 格（576 的 8.5%）。
 * ================================================================ */
#define ROOM_BORDER      1      /* 外圈墙厚 */
#define ROOM_DOOR_DEPTH  2      /* 从门向外挖几格 */

RoomProp roomProps[MAX_ROOM_PROPS];
int roomPropCount = 0;

void RoomPropReset(void) {
    for (int i = 0; i < MAX_ROOM_PROPS; i++) {
        roomProps[i].x = 0.0f; roomProps[i].y = 0.0f;
        roomProps[i].kind = 0; roomProps[i].active = 0;
        roomProps[i].searchLeft = 0; roomProps[i].searched = 0;
    }
    roomPropCount = 0;
}

int RoomPropAdd(float x, float y, int kind) {
    if (roomPropCount >= MAX_ROOM_PROPS) return 0;
    roomProps[roomPropCount].x = x;
    roomProps[roomPropCount].y = y;
    roomProps[roomPropCount].kind = kind;
    roomProps[roomPropCount].active = 1;
    /* 家具是实体：挡住玩家和怪。高度按种类给，渲染层据此算投影。 */
    roomProps[roomPropCount].blocks = 1;
    roomProps[roomPropCount].height = 0.62f;
    /* 搜查次数：货架最多，箱子其次 */
    roomProps[roomPropCount].searchLeft =
        (kind == ROOMPROP_SHELF) ? 3 : 2;
    roomProps[roomPropCount].searched = 0;
    roomPropCount++;
    return 1;
}

void RoomPropRemoveLast(void) {
    if (roomPropCount <= 0) return;
    roomPropCount--;
    roomProps[roomPropCount].active = 0;
    roomProps[roomPropCount].x = 0.0f;
    roomProps[roomPropCount].y = 0.0f;
}

int PropBlocksAt(int cx, int cy) {
    for (int i = 0; i < roomPropCount; i++) {
        if (!roomProps[i].active || !roomProps[i].blocks) continue;
        if ((int)roomProps[i].x == cx && (int)roomProps[i].y == cy) return 1;
    }
    return 0;
}

/* ==================================================================
 * v2.4-rc P3：层表
 * ------------------------------------------------------------------
 * 货架：框架(0) + 三层板(1/2/3)，层间留真实空隙 ——
 *       走动时底板与顶板相对屏幕的位移不同，眼睛会读成立体。
 * 控制台：柜体(0) + 抬起的斜面板(1)。
 * ================================================================ */
int RoomPropLayers(int kind, PropLayer* out, int maxOut) {
    if (!out || maxOut <= 0) return 0;

    if (kind == ROOMPROP_SHELF) {
        if (maxOut < 4) return 0;
        float bh = 0.085f;
        out[0].baseZ = 0.00f; out[0].halfH = 0.62f;  out[0].spr = 0;   /* 框架 */
        out[1].baseZ = 0.05f; out[1].halfH = bh;     out[1].spr = 1;   /* 底板 */
        out[2].baseZ = 0.29f; out[2].halfH = bh;     out[2].spr = 1;   /* 中板 */
        out[3].baseZ = 0.53f; out[3].halfH = bh;     out[3].spr = 1;   /* 顶板 */
        return 4;
    }
    if (kind == ROOMPROP_CONSOLE) {
        if (maxOut < 2) return 0;
        out[0].baseZ = 0.00f; out[0].halfH = 0.34f; out[0].spr = 0;    /* 柜体 */
        out[1].baseZ = 0.34f; out[1].halfH = 0.28f; out[1].spr = 1;    /* 斜面板 */
        return 2;
    }
    if (kind == ROOMPROP_CRATES) {
        /* 木箱堆：两层箱子叠起来，层间留空隙 → 有视差、也读作"堆" */
        if (maxOut < 2) return 0;
        out[0].baseZ = 0.00f; out[0].halfH = 0.26f; out[0].spr = 0;
        out[1].baseZ = 0.26f; out[1].halfH = 0.22f; out[1].spr = 1;
        return 2;
    }
    return 0;
}

int RoomPropLayerSprite(int kind, int layerIndex) {
    PropLayer lp[PROP_MAX_LAYERS];
    int n = RoomPropLayers(kind, lp, PROP_MAX_LAYERS);
    if (layerIndex < 0 || layerIndex >= n) return -1;
    return lp[layerIndex].spr;
}

int RoomPropAt(float x, float y) {
    int cx = (int)floorf(x), cy = (int)floorf(y);
    for (int i = 0; i < roomPropCount; i++) {
        if (!roomProps[i].active) continue;
        if ((int)roomProps[i].x == cx && (int)roomProps[i].y == cy) return i;
    }
    return -1;
}

static int RoomReachable(void);   /* FurnishRoom 要用 */

/* 给一个房间放家具：沿内圈墙角挑地板格。
   featureId 决定放什么：
     SHELF（仓储室）→ 货架
     POWER（供电室）→ 控制台
     其它          → 货架（货架更通用，锅炉房/储藏间也合理） */
static void FurnishRoom(const RoomTemplate* t, int fx, int fy) {
    if (!t) return;
    int innerX = fx + ROOM_BORDER;
    int innerY = fy + ROOM_BORDER;
    /* 2026-11-06（用户要求）：3~4 件 → 2~3 件。
       家具是**布景**，不是资源；堆太多既挡视线又显得每间房都一样满。 */
    int want = 2 + (int)(RoomRand() % 2);          /* 每间 2~3 件（含木箱堆） */
    int placed = 0;

    /* 2026-11-06：新三种房间的家具搭配。
       供电室/地狱用控制台（人造装置），其余用货架/木箱堆。
       血肉洞窟用木箱堆 —— "洞窟里有整齐货架"读起来是错的。 */
    int propKind;
    switch (t->featureId) {
    case ROOM_FEATURE_POWER:
    case ROOM_FEATURE_HELL:  propKind = ROOMPROP_CONSOLE; break;
    case ROOM_FEATURE_FLESH: propKind = ROOMPROP_CRATES;  break;
    default:                 propKind = ROOMPROP_SHELF;   break;
    }
    /* C：再放 1~2 个木箱堆 —— 房间不能只有货架一种东西。
       它们走同一套"放一件→验连通→不行就撤"的流程。 */
    int crates = 1;   /* 2026-11-06：固定 1 堆，不再随机 1~2 */

    /* ==================================================================
     * 2026-11-06：**家具只放角落**
     * ------------------------------------------------------------------
     * 用户实测：「架子和箱子有时候会堵路，因为它们是实体。」
     *
     * 之前 pass 0 是"任意贴墙格" —— 贴着房间一侧墙的中间也会放，
     * 于是 1 格宽的过道被从中间截断。
     * 现在按优先级：
     *   pass 0 = **角落**（同时在两条边上）  ← 最优先，绝不挡路
     *   pass 1 = 其他贴墙格（角落放不下才退）
     *   pass 2 = 房间内部（最后兜底，数量也受限）
     * 而且每放一件都验一次房间连通 + 出口可达，不通就撤。
     * ================================================================ */
    for (int pass = 0; pass < 3 && placed < want; pass++) {
        if (pass == 2 && placed >= 2) break;   /* 内部最多补 2 件，别把房间塞满 */
        for (int y = 0; y < t->h && placed < want; y++) {
            for (int x = 0; x < t->w && placed < want; x++) {
                int edgeX = (x == 0 || x == t->w - 1);
                int edgeY = (y == 0 || y == t->h - 1);
                int edge  = edgeX || edgeY;
                int corner = edgeX && edgeY;
                if (pass == 0 && !corner) continue;
                if (pass == 1 && (corner || !edge)) continue;
                if (pass == 2 && edge) continue;

                unsigned char tile = t->tiles[y][x];
                if (tile != ROOM_TILE_FLOOR) continue;

                /* 别和已放的家具撞同一格 */
                int cx = innerX + x, cy = innerY + y;
                int taken = 0;
                for (int k = 0; k < roomPropCount; k++) {
                    if (!roomProps[k].active) continue;
                    if ((int)roomProps[k].x == cx && (int)roomProps[k].y == cy) taken = 1;
                }
                if (taken) continue;

                int kind2 = (crates > 0) ? ROOMPROP_CRATES : propKind;
                if (!RoomPropAdd((float)cx + 0.5f, (float)cy + 0.5f, kind2)) continue;
                /* P2：**放完立刻验连通**。堵死就撤掉这一件。
                   宁可这间少一件家具，也不能让玩家进不去或出不来。 */
                if (!RoomReachable()) { RoomPropRemoveLast(); continue; }
                if (kind2 == ROOMPROP_CRATES) crates--;
                placed++;
            }
        }
    }
}

/* 模板瓦片 → worldMap */
static int TileToMap(unsigned char t) {
    switch (t) {
        case ROOM_TILE_WALL:
        case ROOM_TILE_PILLAR:
            return 1;                    /* 实心 */
        case ROOM_TILE_WATER:
        case ROOM_TILE_FLOOR:
        default:
            return 0;                    /* 可通行 */
    }
}

static int AcousticFor(unsigned char t, int featureId) {
    if (t == ROOM_TILE_WATER) return ACOUSTIC_WATER;
    if (featureId == ROOM_FEATURE_POWER) return ACOUSTIC_METAL;
    if (featureId == ROOM_FEATURE_SHELF) return ACOUSTIC_HARD;
    return ACOUSTIC_DEFAULT;
}

static unsigned FlagsFor(int featureId) {
    switch (featureId) {
        case ROOM_FEATURE_STEAM: return CELLFLAG_STEAM;
        case ROOM_FEATURE_SHELF: return CELLFLAG_SHELF;
        case ROOM_FEATURE_POWER: return CELLFLAG_POWER;
        default: return 0u;
    }
}

/* 占地 = 模板 + 一圈墙 */
static int FootprintW(const RoomTemplate* t) { return t->w + ROOM_BORDER * 2; }
static int FootprintH(const RoomTemplate* t) { return t->h + ROOM_BORDER * 2; }

/* 挖开一格（越界忽略） */
static void ClearCell(int cx, int cy) {
    if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) return;
    worldMap[cy][cx] = 0;
    explored[cy][cx] = 0;
}

/* ==================================================================
 * 从门向外挖，**一直挖到接上迷宫为止**（上限 8 格）。
 * ------------------------------------------------------------------
 * 第一版只挖 2 格，实测 400 个种子里有 201 个层出现封死的房间 ——
 * 因为 2 格经常只是挖出一个"口袋"，根本没接到迷宫上。
 *
 * 现在的判据很直接：只要走到一个**本来就开着**的格子，就算接上了。
 * 返回 1 = 接上了；0 = 8 格内没接上（调用方会回滚整个房间）。
 * ================================================================ */
#define ROOM_DOOR_MAX 8

static int CarveDoorTunnel(int doorX, int doorY, int stepX, int stepY) {
    /* ==================================================================
     * 2026-11-06（用户实测）：**挖不通的盲道必须填回去**
     * ------------------------------------------------------------------
     * 用户原话：「这是主题房间的出口处……这种地方也有死路」。
     *
     * 根因：这个函数沿一个方向连挖最多 8 格，挖到空地就算成功；
     * 挖到头没接上就 return 0 —— **但已经挖开的格子留在那里**。
     * 一扇门接上、另一扇挖成盲道的情况下（很常见），
     * 房间外面就多出一条死胡同，玩家从房间出来必须原路退回。
     *
     * 现在记住挖过的格子，失败就全部填回去（连同 explored）。
     * ================================================================ */
    int carvedX[ROOM_DOOR_MAX], carvedY[ROOM_DOOR_MAX], carvedN = 0;
    int cx = doorX, cy = doorY;
    for (int i = 0; i < ROOM_DOOR_MAX; i++) {
        cx += stepX; cy += stepY;
        if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) break;
        if (!worldMap[cy][cx]) return 1;      /* 本来就是空地 = 已经接上迷宫 */
        ClearCell(cx, cy);
        carvedX[carvedN] = cx;
        carvedY[carvedN] = cy;
        carvedN++;
    }
    /* 没接上：把挖开的盲道填回去 */
    for (int i = 0; i < carvedN; i++) {
        worldMap[carvedY[i]][carvedX[i]] = 1;
        explored[carvedY[i]][carvedX[i]] = 0;
    }
    return 0;
}

/* 从 (1,1) 做一次 BFS，检查这个房间的所有地板格都能走到。 */
static int RoomReachable(void) {
    static int seen[MAP_HEIGHT][MAP_WIDTH];
    static int qx[MAP_WIDTH * MAP_HEIGHT], qy[MAP_WIDTH * MAP_HEIGHT];

    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) seen[y][x] = 0;

    if (worldMap[1][1]) return 1;              /* 出生点被墙堵着：这是别的问题，不判死 */
    int head = 0, tail = 0;
    seen[1][1] = 1; qx[tail] = 1; qy[tail] = 1; tail++;

    while (head < tail) {
        int cx = qx[head], cy = qy[head]; head++;
        static const int dxs[4] = { 1, -1, 0, 0 };
        static const int dys[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; k++) {
            int nx = cx + dxs[k], ny = cy + dys[k];
            if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
            if (seen[ny][nx] || worldMap[ny][nx]) continue;
            /* v2.4-rc P2：**挡路的家具也算墙** ——
               否则"放完家具再验连通"会漏掉家具把通道堵死的情况。 */
            if (PropBlocksAt(nx, ny)) continue;
            seen[ny][nx] = 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }

    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (cellMeta[y][x].roomId == 0) continue;
            if (worldMap[y][x]) continue;
            /* 家具自己占的格当然走不到 —— 那是**它挡住**的，不是堵死。
               不排除的话每放一件家具都会判定失败，全部回滚。 */
            if (PropBlocksAt(x, y)) continue;
            if (!seen[y][x]) return 0;         /* 有房间地板走不到 = 封死 */
        }

    /* ==================================================================
     * 2026-11-06（v2.4-rc）：**出口也必须还走得到**
     * ------------------------------------------------------------------
     * 1000 种子洪泛里还剩 0.7% 的层报 "exit unreachable"。
     * 原因不在房间内部 —— 房间自己是通的 ——
     * 而是房间的**围墙 + 家具**把通往出口的那条走廊切断了。
     *
     * 所以放房间时要把"出口从出生点可达"也当成约束：
     * 一堵新墙如果切断了唯一的通路，这个位置就不许放。
     * ================================================================== */
    {
        extern float exitX, exitY;
        int ex = (int)exitX, ey = (int)exitY;
        if (ex >= 0 && ey >= 0 && ex < MAP_WIDTH && ey < MAP_HEIGHT) {
            if (!worldMap[ey][ex] && !seen[ey][ex]) return 0;
        }
    }
    return 1;
}

/* 把房间放到**左上角外墙** (fx,fy)。
   返回 1 = 成功且连通；0 = 放不下或**不连通**（此时已回滚，地图没被改）。 */
static int CarveRoomBordered(const RoomTemplate* t, int fx, int fy, int roomId, int doorRot) {
    int fw = FootprintW(t), fh = FootprintH(t);

    /* ---- ① 快照，失败要能整体回滚 ---- */
    static int savedMap[MAP_HEIGHT][MAP_WIDTH];
    static CellMeta savedMeta[MAP_HEIGHT][MAP_WIDTH];
    memcpy(savedMap, worldMap, sizeof(savedMap));
    memcpy(savedMeta, cellMeta, sizeof(savedMeta));

    /* ---- ② 外圈全画成墙，内圈放模板瓦片 ---- */
    for (int y = 0; y < fh; y++) {
        for (int x = 0; x < fw; x++) {
            int cx = fx + x, cy = fy + y;
            if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) {
                memcpy(worldMap, savedMap, sizeof(savedMap));
                return 0;
            }
            int border = (x == 0 || y == 0 || x == fw - 1 || y == fh - 1);
            if (border) {
                worldMap[cy][cx] = 1;
            } else {
                worldMap[cy][cx] = TileToMap(t->tiles[y - ROOM_BORDER][x - ROOM_BORDER]);
            }
        }
    }

    /* ---- ③ 打两个门（相对的两边，取中点）并向外挖到接上 ---- */
    int midX = fw / 2;
    int midY = fh / 2;
    int okA, okB;
    /* ==================================================================
     * 2026-11-06（用户实测）：**挖不通的门要重新封死**
     * ------------------------------------------------------------------
     * 门洞在调用隧道之前就被挖开了。如果隧道挖 8 格没接上迷宫
     * （房间贴地图边界时很常见），隧道会回填，但**门洞留在环上** ——
     * 外面是实心岩，里面是房间，等于墙上开了个洞通向石头。
     *
     * test_rooms_stay_sealed 抓到的 (33,18) roomId=0 就是这个。
     * ================================================================ */
    int doorAX, doorAY, doorBX, doorBY;
    if (doorRot == 0) {
        doorAX = fx + midX;       doorAY = fy;
        doorBX = fx + midX;       doorBY = fy + fh - 1;
        worldMap[doorAY][doorAX] = 0;
        worldMap[doorBY][doorBX] = 0;
        okA = CarveDoorTunnel(doorAX, doorAY, 0, -1);
        okB = CarveDoorTunnel(doorBX, doorBY, 0, 1);
    } else {
        doorAX = fx;              doorAY = fy + midY;
        doorBX = fx + fw - 1;     doorBY = fy + midY;
        worldMap[doorAY][doorAX] = 0;
        worldMap[doorBY][doorBX] = 0;
        okA = CarveDoorTunnel(doorAX, doorAY, -1, 0);
        okB = CarveDoorTunnel(doorBX, doorBY, 1, 0);
    }
    if (!okA) { worldMap[doorAY][doorAX] = 1; explored[doorAY][doorAX] = 0; }
    if (!okB) { worldMap[doorBY][doorBX] = 1; explored[doorBY][doorBX] = 0; }

    /* ---- ④ 登记属性（只登记内圈，外圈是普通墙）---- */
    CellMetaFillRoom(fx + ROOM_BORDER, fy + ROOM_BORDER, t->w, t->h, roomId,
                     t->themeId, ACOUSTIC_DEFAULT, FlagsFor(t->featureId));
    for (int y = 0; y < t->h; y++)
        for (int x = 0; x < t->w; x++) {
            int cx = fx + ROOM_BORDER + x, cy = fy + ROOM_BORDER + y;
            if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) continue;
            cellMeta[cy][cx].acousticId =
                (unsigned char)AcousticFor(t->tiles[y][x], t->featureId);
        }

    /* ==================================================================
     * 2026-11-06：**给外墙环打标记**，渲染层据此画房间专属腰线。
     * 只写 flags 与 themeId，**不写 roomId** ——
     * roomId 一旦写到墙上，"房间地面格数 / 可达性"的统计就会把墙算进去。
     * ================================================================ */
    {
        int fx0 = fx + ROOM_BORDER - 1, fy0 = fy + ROOM_BORDER - 1;
        int fx1 = fx + ROOM_BORDER + t->w, fy1 = fy + ROOM_BORDER + t->h;
        for (int y = fy0; y <= fy1; y++) {
            for (int x = fx0; x <= fx1; x++) {
                if (x < 0 || y < 0 || x >= MAP_WIDTH || y >= MAP_HEIGHT) continue;
                /* 只标环上的格（内圈已经填过，跳过） */
                int onRing = (x == fx0 || x == fx1 || y == fy0 || y == fy1);
                if (!onRing) continue;
                cellMeta[y][x].themeId =
                    (unsigned char)((t->themeId >= 0 && t->themeId < THEME_COUNT) ? t->themeId : 0);
                cellMeta[y][x].flags |= (unsigned char)CELLFLAG_ROOMWALL;
            }
        }
    }

    /* ---- ⑤ 两个门都接不上，或者挖完发现房间走不到 → 全部回滚 ---- */
    /* 家具放在连通性检查**之后**：它会读 worldMap 找地板格，
       而这里已经确认房间是通的。 */
    if ((!okA && !okB) || !RoomReachable()) {
        memcpy(worldMap, savedMap, sizeof(savedMap));
        memcpy(cellMeta, savedMeta, sizeof(savedMeta));
        /* roomCount 可能被 CellMetaFillRoom 抬高过，重算 */
        roomCount = 0;
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++)
                if (cellMeta[y][x].roomId > roomCount) roomCount = cellMeta[y][x].roomId;
        return 0;
    }

    FurnishRoom(t, fx, fy);

    /* ==================================================================
     * 2026-11-06：**舱门** —— 一个房间一扇，门就是主题控制器
     * ------------------------------------------------------------------
     * 选位：优先房间内部的**角落**（同时在两条边上），
     * 这样门不挡过道；找不到角落就退到任意贴墙格。
     * 门本身**不参与碰撞**（blocks = 0）：
     * 它是"装置"，不是障碍物 —— 挡路会让房间变窄，和上一轮
     * "家具不要堵路"的教训是同一条。
     * ================================================================ */
    {
        extern int HatchPlaceInRoom(int fx, int fy, int w, int h, int themeId);
        HatchPlaceInRoom(fx + ROOM_BORDER, fy + ROOM_BORDER, t->w, t->h, t->themeId);
    }
    return 1;
}

/* 保留区检查（用工地的**外墙角**判断） */
static int ReservationsOk(int fx, int fy, int fw, int fh) {
    extern float exitX, exitY;
    for (int y = fy; y < fy + fh; y++) {
        for (int x = fx; x < fx + fw; x++) {
            /* 出生区 1..3 外扩到 4 */
            if (x < 4 && y < 4) return 0;
            /* 出口 3 格内 */
            float dx = (float)x - exitX;
            float dy = (float)y - exitY;
            if (dx * dx + dy * dy < 9.0f) return 0;
            /* 别的房间 */
            const CellMeta* m = CellMetaAt(x, y);
            if (m && m->roomId != 0) return 0;
        }
    }
    /* 房间之间留 1 格缝 */
    for (int y = fy - 1; y < fy + fh + 1; y++)
        for (int x = fx - 1; x < fx + fw + 1; x++) {
            if (x < 0 || y < 0 || x >= MAP_WIDTH || y >= MAP_HEIGHT) continue;
            const CellMeta* m = CellMetaAt(x, y);
            if (m && m->roomId != 0) return 0;
        }
    return 1;
}

/* 试放一个模板（任意模板），成功返回 1 */
/* 2026-11-06：模板级诊断计数。
   为什么要它：上一版只看"主题出现率"，结果发现某张模板出现 0%，
   但分不清"没被尝试"还是"每次都被回滚"。这两个原因要改的地方完全不同。 */
int RoomTplTried[ROOM_TEMPLATE_COUNT];
int RoomTplPlaced[ROOM_TEMPLATE_COUNT];

/* ==================================================================
 * 2026-11-06（用户要求）：**主题按深度分层，不再每层都可能出任何一种**
 * ------------------------------------------------------------------
 * 用户原话：
 *   「不同的主题应该只能刷在那些特定的主题的风格，而不是每一层都有
 *     多种不同的主题房间」
 *
 * 模板 → 主题（见 room_template.c）：
 *   0 锈工厂(5) / 1 监狱(1) / 2 冰窖(4) / 3 病房(3)
 *   4 砖墙(0)   / 5 地狱(2) / 6 血肉洞窟(6)
 *
 * 分层（越深越凶）：
 *   第 1~3 层   砖墙、监狱           —— 像废弃生活区
 *   第 4~6 层   冰窖、病房           —— 像医疗/冷藏区
 *   第 7~9 层   锈工厂、地狱         —— 像工业/仪式区
 *   第 10 层起  血肉洞窟（+ 锈工厂） —— 像生物质核心
 *
 * 这样每一段深度的"房间长相"是统一的，玩家能感觉到自己在地下越走越深，
 * 而不是每层随机抽一个完全无关的场景。
 * ================================================================ */
static int ThemeTemplateAllowed(int tpl, int level) {
    if (level <= 3)  return (tpl == 4 || tpl == 1);
    if (level <= 6)  return (tpl == 2 || tpl == 3);
    if (level <= 9)  return (tpl == 0 || tpl == 5);
    return (tpl == 6 || tpl == 0);
}

/* 在 [idMin,idMax] 里挑一个本层允许的模板；挑不到返回 -1 */
static int PickAllowedTemplate(int idMin, int idMax, int level) {
    int cand[ROOM_TEMPLATE_COUNT], n = 0;
    for (int i = idMin; i <= idMax && i < ROOM_TEMPLATE_COUNT; i++)
        if (i >= 0 && ThemeTemplateAllowed(i, level)) cand[n++] = i;
    if (n <= 0) return -1;
    return cand[RoomRand() % n];
}

static int TryPlace(int placed, int idMin, int idMax) {
    extern int currentLevel;
    int id = PickAllowedTemplate(idMin, idMax, currentLevel);
    if (id < 0) return 0;                 /* 本层这一档没有允许的主题 */
    if (id >= 0 && id < ROOM_TEMPLATE_COUNT) RoomTplTried[id]++;
    int rot = RoomRand() % 4;
    RoomTemplate t;
    if (!RoomTemplateRotate(id, rot, &t)) return 0;
    if (t.w <= 0 || t.h <= 0) return 0;

    int fw = FootprintW(&t), fh = FootprintH(&t);
    int spanX = MAP_WIDTH - 2 - fw;
    int spanY = MAP_HEIGHT - 2 - fh;
    if (spanX < 1 || spanY < 1) return 0;

    int fx = 1 + (int)(RoomRand() % (unsigned)spanX);
    int fy = 1 + (int)(RoomRand() % (unsigned)spanY);

    /* ==================================================================
     * 2026-11-06：**房间不许压出生区**
     * ------------------------------------------------------------------
     * GenerateLevel 最后会把 (1..3, 1..3) 强行挖空作出生点。
     * 如果房间的围墙正好落在这块里，那面墙会被挖穿 ——
     * 表现为"房间漏了"（test_rooms_stay_sealed 就是被这个红的）。
     * ================================================================ */
    if (fx <= 3 && fy <= 3) return 0;
    if (fx + fw - 1 < 4 && fy <= 3) return 0;
    if (fy + fh - 1 < 4 && fx <= 3) return 0;

    if (!ReservationsOk(fx, fy, fw, fh)) return 0;

    int doorRot = (int)(RoomRand() % 2);
    if (!CarveRoomBordered(&t, fx, fy, placed + 1, doorRot)) return 0;
    if (id >= 0 && id < ROOM_TEMPLATE_COUNT) RoomTplPlaced[id]++;
    return 1;
}

/* ==================================================================
 * 放置
 * ------------------------------------------------------------------
 * 第一轮试大模板（0..2），第二轮用小模板（3）兜底。
 * 因为房间自带围墙和门，**不要求实心岩体**，
 * 所以出现率可以做到接近 100%。
 * 宁可不放，也不放坏的 —— 但没有"坏"的形态了。
 * ================================================================ */
/* 前置声明：下面 GenerateThemeRooms 末尾要用（定义在本文件靠后） */
static void RoomDumpIfRequested(int placed);

int GenerateThemeRooms(int wantCount) {
    RoomPropReset();
    if (wantCount <= 0) return 0;

    int placed = 0, attempts = 0;

    /* ==================================================================
     * 2026-11-06：**放置策略改造**
     * ------------------------------------------------------------------
     * 原来的写法是"先用 0..2 放满，放不满再用 3 兜底"。
     * 实测（500 种子）：第二档**一次都没跑过** —— 因为第一档通常已经
     * 放满了 wantCount（1~2 间），于是 3..6 那四张模板永远见不到，
     * 新加的三张主题房等于白加。
     *
     * 现在：第一间房有 45% 概率直接从扩展档（3..6）里挑，
     * 保证跨种子一定能看到全部 7 种。第二间房全范围随机。
     * ================================================================ */
    /* 2026-11-06：档位不再按"大/小模板"分，而是由 ThemeTemplateAllowed
       按深度过滤；这里只要把**全部模板**交给 TryPlace，
       它自己会在允许集合里挑。分档保留是为了让失败重试有不同位置。 */
    while (placed < wantCount && attempts < 360) {
        attempts++;
        if (TryPlace(placed, 0, ROOM_TEMPLATE_COUNT - 1)) placed++;
    }
    RoomDumpIfRequested(placed);
    return placed;
}

/* ==================================================================
 * 诊断：把**游戏真正生成的那一层**打成 ASCII 文件。
 * ------------------------------------------------------------------
 * 为什么需要它：tools/map_gen_dump.c 复制了 DFS，但 RoomRand() 的调用序列
 * 和真实游戏不一样，生成出来的层也不同。判断"房间里到底有没有东西"
 * 必须看真实的那一张图。
 *
 * 为什么不用 stdio：本工程的 partition 规则要求系统头只出现在 common.h，
 * 游戏源码里出现 <stdio.h> 会被 partition_check 拦下。
 * 所以这里直接用 Win32 写文件（windows.h 由 common.h 提供）。
 *
 * 默认关闭；设了环境变量 CANITY_ROOM_DUMP=<路径> 才写，每层追加一段。
 * ================================================================ */
static void RoomDumpIfRequested(int placed) {
#ifdef CANITY_WEB
    /* 2026-11-06（R4）：房间 ASCII dump 是桌面调试工具（要往 exe 旁边写文件），
       网页构建整段跳过 —— 注意用 #else 而不是 return：
       后面那些 Win32 调用在网页端**根本不能被编译**。 */
    (void)placed;
#else
    char path[MAX_PATH];
    DWORD got = GetEnvironmentVariableA("CANITY_ROOM_DUMP", path, (DWORD)sizeof(path));
    if (got == 0 || got >= sizeof(path)) return;

    static char buf[4096];
    int n = 0;
    n += _snprintf(buf + n, (size_t)(sizeof(buf) - (size_t)n),
                   "=== rooms=%d exit=(%.1f,%.1f) ===\r\n", placed, exitX, exitY);
    for (int y = 0; y < MAP_HEIGHT && n < (int)sizeof(buf) - 40; y++) {
        for (int x = 0; x < MAP_WIDTH && n < (int)sizeof(buf) - 4; x++) {
            int wall = worldMap[y][x];
            unsigned char rid = cellMeta[y][x].roomId;
            char c;
            if (rid != 0) c = wall ? '+' : ',';   /* + 房间结构  , 房间地面 */
            else          c = wall ? '#' : '.';
            buf[n++] = c;
        }
        buf[n++] = '\r';
        buf[n++] = '\n';
    }
    buf[n++] = '\r'; buf[n++] = '\n';

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, buf, (DWORD)n, &written, NULL);
    CloseHandle(h);
#endif
}

int roomSeedUsed = 0;



/* ==================================================================
 * 重放：读档后用保存的种子把房间重建出来。
 * ------------------------------------------------------------------
 * 前提：worldMap 已经从存档恢复（房间的墙本来就在里面），
 * 且 exitX/exitY 也已经恢复 —— 房间放置会读它们做保留区判断。
 * 重放会重写同样的墙/地面（幂等），并重建 cellMeta 与 roomProps。
 * ================================================================ */
int RoomReplay(int seed, int wantCount) {
    /* ==================================================================
     * 2026-11-06：**重放前必须先清舱门表**
     * ------------------------------------------------------------------
     * GenerateThemeRooms -> CarveRoomBordered -> HatchPlaceInRoom 是追加式
     * （HatchAdd 往表尾塞）。这里不清的话，每读一次档就在旧表上再追加
     * 一批：第二次读档时同一间房会同时存在两扇门，重叠在一起，
     * 而且上一层的门会残留到这一层。
     * ================================================================ */
    HatchReset();
    if (wantCount <= 0) { roomSeedUsed = 0; RoomPropReset(); return 0; }
    roomSeedUsed = seed;
    { extern void rng_world_seed(unsigned int);
      rng_world_seed((unsigned)seed); }
    return GenerateThemeRooms(wantCount);
}

int RoomCount(void) { return roomCount; }

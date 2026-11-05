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
#include "cell_meta.h"
#include "room_template.h"
#include "map_rooms.h"
#include "game.h"
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
    int want = 3 + (int)(rand() % 2);          /* 每间 3~4 件（含木箱堆） */
    int placed = 0;

    int propKind = (t->featureId == ROOM_FEATURE_POWER) ? ROOMPROP_CONSOLE : ROOMPROP_SHELF;
    /* C：再放 1~2 个木箱堆 —— 房间不能只有货架一种东西。
       它们走同一套"放一件→验连通→不行就撤"的流程。 */
    int crates = 1 + (int)(rand() % 2);

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
    int cx = doorX, cy = doorY;
    for (int i = 0; i < ROOM_DOOR_MAX; i++) {
        cx += stepX; cy += stepY;
        if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) return 0;
        if (!worldMap[cy][cx]) return 1;      /* 本来就是空地 = 已经接上迷宫 */
        ClearCell(cx, cy);
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
    if (doorRot == 0) {
        worldMap[fy][fx + midX] = 0;
        worldMap[fy + fh - 1][fx + midX] = 0;
        okA = CarveDoorTunnel(fx + midX, fy, 0, -1);
        okB = CarveDoorTunnel(fx + midX, fy + fh - 1, 0, 1);
    } else {
        worldMap[fy + midY][fx] = 0;
        worldMap[fy + midY][fx + fw - 1] = 0;
        okA = CarveDoorTunnel(fx, fy + midY, -1, 0);
        okB = CarveDoorTunnel(fx + fw - 1, fy + midY, 1, 0);
    }

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
static int TryPlace(int placed, int idMin, int idMax) {
    int id = idMin + (int)(rand() % (unsigned)(idMax - idMin + 1));
    int rot = rand() % 4;
    RoomTemplate t;
    if (!RoomTemplateRotate(id, rot, &t)) return 0;
    if (t.w <= 0 || t.h <= 0) return 0;

    int fw = FootprintW(&t), fh = FootprintH(&t);
    int spanX = MAP_WIDTH - 2 - fw;
    int spanY = MAP_HEIGHT - 2 - fh;
    if (spanX < 1 || spanY < 1) return 0;

    int fx = 1 + (int)(rand() % (unsigned)spanX);
    int fy = 1 + (int)(rand() % (unsigned)spanY);

    if (!ReservationsOk(fx, fy, fw, fh)) return 0;

    int doorRot = (int)(rand() % 2);
    if (!CarveRoomBordered(&t, fx, fy, placed + 1, doorRot)) return 0;
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

    while (placed < wantCount && attempts < 120) {
        attempts++;
        if (TryPlace(placed, 0, 2)) placed++;
    }
    attempts = 0;
    while (placed < wantCount && attempts < 120) {
        attempts++;
        if (TryPlace(placed, 3, 3)) placed++;
    }
    RoomDumpIfRequested(placed);
    return placed;
}

/* ==================================================================
 * 诊断：把**游戏真正生成的那一层**打成 ASCII 文件。
 * ------------------------------------------------------------------
 * 为什么需要它：tools/map_gen_dump.c 复制了 DFS，但 rand() 的调用序列
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
    if (wantCount <= 0) { roomSeedUsed = 0; RoomPropReset(); return 0; }
    roomSeedUsed = seed;
    srand((unsigned)seed);
    return GenerateThemeRooms(wantCount);
}

int RoomCount(void) { return roomCount; }

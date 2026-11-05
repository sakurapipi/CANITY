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
            seen[ny][nx] = 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }

    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (cellMeta[y][x].roomId == 0) continue;
            if (worldMap[y][x]) continue;
            if (!seen[y][x]) return 0;         /* 有房间地板走不到 = 封死 */
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
int GenerateThemeRooms(int wantCount) {
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
    return placed;
}

int RoomCount(void) { return roomCount; }

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

/* 每一格在 worldMap 里的落地值 */
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

/* 出生区外扩 + 出口周围 + 已放置房间，全都不许压 */
static int ReservedBlocked(int ox, int oy, int w, int h) {
    for (int y = oy; y < oy + h; y++) {
        for (int x = ox; x < ox + w; x++) {
            if (x < ROOM_KEEP_SPAWN && y < ROOM_KEEP_SPAWN) return 1;
            /* 出口（game.h 里声明为 float） */
            {
                extern float exitX, exitY;
                float dx = (float)x - exitX;
                float dy = (float)y - exitY;
                if (dx * dx + dy * dy < (float)(ROOM_KEEP_EXIT * ROOM_KEEP_EXIT)) return 1;
            }
            /* 已在别的房间里 */
            const CellMeta* m = CellMetaAt(x, y);
            if (m && m->roomId != 0) return 1;
        }
    }
    /* 房间之间留缝：检查外扩一圈 */
    for (int y = oy - ROOM_MIN_GAP; y < oy + h + ROOM_MIN_GAP; y++) {
        for (int x = ox - ROOM_MIN_GAP; x < ox + w + ROOM_MIN_GAP; x++) {
            if (x < 0 || y < 0 || x >= MAP_WIDTH || y >= MAP_HEIGHT) continue;
            const CellMeta* m = CellMetaAt(x, y);
            if (m && m->roomId != 0) return 1;
        }
    }
    return 0;
}

/* 目标矩形里"当前是墙"的比例够不够高。 */
static int SolidEnough(int ox, int oy, int w, int h) {
    int walls = 0, total = 0;
    for (int y = oy; y < oy + h; y++) {
        if (y < 0 || y >= MAP_HEIGHT) continue;
        for (int x = ox; x < ox + w; x++) {
            if (x < 0 || x >= MAP_WIDTH) continue;
            total++;
            if (worldMap[y][x]) walls++;
        }
    }
    if (total <= 0) return 0;
    return walls * ROOM_SOLID_DEN >= total * ROOM_SOLID_NUM;
}

static void CarveRoom(const RoomTemplate* t, int ox, int oy, int roomId) {
    for (int y = 0; y < t->h; y++)
        for (int x = 0; x < t->w; x++)
            worldMap[oy + y][ox + x] = TileToMap(t->tiles[y][x]);

    CellMetaFillRoom(ox, oy, t->w, t->h, roomId,
                     t->themeId, ACOUSTIC_DEFAULT, FlagsFor(t->featureId));

    /* 逐格细化声学属性：积水 / 金属 / 硬地 */
    for (int y = 0; y < t->h; y++)
        for (int x = 0; x < t->w; x++) {
            int cx = ox + x, cy = oy + y;
            if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) continue;
            cellMeta[cy][cx].acousticId =
                (unsigned char)AcousticFor(t->tiles[y][x], t->featureId);
        }
}

/* 一次放置尝试。strict=1 时要求边距 2 + 实心 60%；=0 时放宽到边距 1 + 实心 35%。
   返回 1 = 放下去了。 */
static int TryPlaceOne(int wantCount, int placed, int strict) {
    int id = rand() % ROOM_TEMPLATE_COUNT;
    int rot = rand() % 4;
    RoomTemplate t;
    if (!RoomTemplateRotate(id, rot, &t)) return 0;
    if (t.w <= 0 || t.h <= 0) return 0;
    (void)wantCount;

    int margin = strict ? ROOM_MARGIN : 1;
    int ox = 1 + rand() % (MAP_WIDTH - 2 - t.w);
    int oy = 1 + rand() % (MAP_HEIGHT - 2 - t.h);

    if (!RoomTemplateFits(&t, ox, oy, margin)) return 0;
    if (ReservedBlocked(ox, oy, t.w, t.h)) return 0;

    if (strict) {
        if (!SolidEnough(ox, oy, t.w, t.h)) return 0;
    } else {
        /* 放宽：只要不是整片空地就行 */
        int walls = 0, total = 0;
        for (int y = oy; y < oy + t.h; y++)
            for (int x = ox; x < ox + t.w; x++) {
                total++;
                if (worldMap[y][x]) walls++;
            }
        if (total <= 0 || walls * 100 < total * 35) return 0;
    }

    CarveRoom(&t, ox, oy, placed + 1);
    return 1;
}

/* ==================================================================
 * 两段式放置（R5 实测修正）
 * ------------------------------------------------------------------
 * 只跑严格档：seed 7 一个房间都放不下（rooms=0）。
 * 只跑放宽档：房间会挖在已有空地上，读起来像"走廊变宽"。
 * 所以先严格、不够再放宽 —— 优先要"围得住的房间"，
 * 实在没有才退一步，保证每层都有东西可看。
 * ================================================================ */
int GenerateThemeRooms(int wantCount) {
    if (wantCount <= 0) return 0;

    int placed = 0;
    int attempts = 0;

    /* 第一段：严格（边距 2 + 实心 60%） */
    while (placed < wantCount && attempts < 150) {
        attempts++;
        if (TryPlaceOne(wantCount, placed, 1)) placed++;
    }
    /* 第二段：放宽（边距 1 + 实心 35%） */
    attempts = 0;
    while (placed < wantCount && attempts < 150) {
        attempts++;
        if (TryPlaceOne(wantCount, placed, 0)) placed++;
    }
    return placed;
}

int RoomCount(void) { return roomCount; }

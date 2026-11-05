/* CANITY — 主题房间模板数据与纯函数（v2.4-rc C01）
 *
 * 三个模板，每个只承担**一个**主要特征（计划里的取舍）：
 *   0 锅炉维护室  柱子在中间，锈工厂色调，蒸汽
 *   1 废弃仓储室  货架把空间切成窄通道，监狱色调
 *   2 供电控制室  一侧开放一侧有隔断，冰窖色调
 *
 * 全部 3–4 格见方：24×24 里再大就把 DFS 走廊挤没了。
 */
#include "common.h"
#include "room_template.h"

/* ==================================================================
 * 2026-11-06（v2.4-rc R5）：**模板第二轮 —— 做大、做明显**
 * ------------------------------------------------------------------
 * 第一版是 4×4，锅炉房只有一块 1×1 柱子。实测结论：
 *   「看得出来[颜色]，柱子货架隔断那些都是没有」
 * 原因不是没挖，而是**太轻**：24×24 的地图里 DFS 走廊是 1 格宽，
 * 你从一个 4×4 的空格里穿过一块 1×1 的墙垛，读起来就是个墙墩，
 * 不像"这是个锅炉房"。
 *
 * 第二版：
 *   · 面积放大到 5×5 / 6×5 —— 空间本身要有"走进一个房间"的体量
 *   · 障碍物成组：2×1 柱墩、5 格货架带、L 形隔断
 *   · 每个模板都保证有**至少一条 1 格宽的窄通道**，
 *     让玩家在房间里也要侧身走 —— 这是"这里和走廊不一样"的关键体感
 * ================================================================ */

/* ---- 0 锅炉维护室 5×5 ------------------------------------------------
 * 中央 2×1 柱墩 + 一个 L 形拐角：进门看不到房间另一头，
 * 必须绕着柱墩走。锈工厂色调。 */
static const unsigned char kBoiler[ROOM_MAX_H][ROOM_MAX_W] = {
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 1, 1, 0, 0 },
    { 0, 0, 1, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 }
};

/* ---- 1 废弃仓储室 6×5 ------------------------------------------------
 * 两条 4 格长的货架带，中间夹一条 1 格宽的通路 ——
 * 这是"仓库"最好认的形状：长条、重复、只能顺着一排走。 */
static const unsigned char kWarehouse[ROOM_MAX_H][ROOM_MAX_W] = {
    { 0, 1, 1, 1, 1, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 1, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 }
};

/* ---- 2 供电控制室 5×5 ------------------------------------------------
 * 右侧一道 3 格高 + 2 格向内的 L 形隔断，围出一个半封闭的控制台角落。 */
static const unsigned char kPower[ROOM_MAX_H][ROOM_MAX_W] = {
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 1, 1, 0 },
    { 0, 0, 0, 1, 0, 0 },
    { 0, 0, 0, 1, 1, 0 },
    { 0, 0, 0, 0, 0, 0 }
};

static const RoomTemplate kTemplates[ROOM_TEMPLATE_COUNT] = {
    /* id, w, h, tiles, themeId, featureId */
    { 0, 5, 5, { { 0 } }, 5, ROOM_FEATURE_STEAM },   /* 锈工厂 */
    { 1, 6, 5, { { 0 } }, 1, ROOM_FEATURE_SHELF },   /* 监狱 */
    { 2, 5, 5, { { 0 } }, 4, ROOM_FEATURE_POWER }    /* 冰窖 */
};

/* 静态初始化里不能直接写二维数组的整块拷贝，所以第一次用时填一次。
   这样做的好处：表本身仍然是 const 语义（外部拿不到可写指针）。 */
static RoomTemplate s_tpl[ROOM_TEMPLATE_COUNT];
static int s_ready = 0;

static void EnsureTemplates(void) {
    if (s_ready) return;
    for (int i = 0; i < ROOM_TEMPLATE_COUNT; i++) {
        s_tpl[i] = kTemplates[i];
        for (int y = 0; y < ROOM_MAX_H; y++)
            for (int x = 0; x < ROOM_MAX_W; x++)
                s_tpl[i].tiles[y][x] = 0;
    }
    for (int y = 0; y < ROOM_MAX_H; y++)
        for (int x = 0; x < ROOM_MAX_W; x++) {
            s_tpl[0].tiles[y][x] = kBoiler[y][x];
            s_tpl[1].tiles[y][x] = kWarehouse[y][x];
            s_tpl[2].tiles[y][x] = kPower[y][x];
        }
    s_ready = 1;
}

const RoomTemplate* RoomTemplateGet(int id) {
    EnsureTemplates();
    if (id < 0 || id >= ROOM_TEMPLATE_COUNT) return NULL;
    return &s_tpl[id];
}

int RoomTemplateRotate(int id, int rot, RoomTemplate* out) {
    EnsureTemplates();
    if (!out) return 0;
    if (id < 0 || id >= ROOM_TEMPLATE_COUNT) return 0;
    if (rot < 0 || rot > 3) rot = 0;

    const RoomTemplate* src = &s_tpl[id];
    *out = *src;

    for (int r = 0; r < rot; r++) {
        RoomTemplate tmp = *out;
        /* 90° 顺时针：新宽 = 旧高，新高 = 旧宽
              out->tiles[x][h-1-y] = in->tiles[y][x] */
        out->w = tmp.h;
        out->h = tmp.w;
        for (int y = 0; y < ROOM_MAX_H; y++)
            for (int x = 0; x < ROOM_MAX_W; x++)
                out->tiles[y][x] = 0;
        for (int y = 0; y < tmp.h; y++) {
            for (int x = 0; x < tmp.w; x++) {
                int nx = tmp.h - 1 - y;
                int ny = x;
                out->tiles[ny][nx] = tmp.tiles[y][x];
            }
        }
    }
    return 1;
}

int RoomTemplateFloorCount(const RoomTemplate* t) {
    if (!t) return 0;
    int n = 0;
    for (int y = 0; y < t->h && y < ROOM_MAX_H; y++)
        for (int x = 0; x < t->w && x < ROOM_MAX_W; x++) {
            unsigned char c = t->tiles[y][x];
            if (c == ROOM_TILE_FLOOR || c == ROOM_TILE_WATER) n++;
        }
    return n;
}

int RoomTemplateFits(const RoomTemplate* t, int ox, int oy, int margin) {
    if (!t) return 0;
    if (margin < 0) margin = 0;
    if (ox < 1 + margin) return 0;
    if (oy < 1 + margin) return 0;
    if (ox + t->w > MAP_WIDTH - 1 - margin) return 0;
    if (oy + t->h > MAP_HEIGHT - 1 - margin) return 0;
    /* 至少要有一半是可通行格，否则放进来只是个实心块 */
    int floors = RoomTemplateFloorCount(t);
    if (floors * 2 < t->w * t->h) return 0;
    return 1;
}

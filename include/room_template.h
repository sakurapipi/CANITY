/* ==================================================================
 * CANITY — 主题房间模板（v2.4-rc C01）
 * ------------------------------------------------------------------
 * 设计约束（来自计划里已核对的事实）：
 *
 *   ① 地图使用既有36×36，容器槽12、精灵上限64，不另设缩小的活动区。
 *   ② **不新建材质系统** —— 现有 7 种主题是"全层一套贴图"，
 *      再做一套就是双倍纹理常驻内存。房间的色调由 themeId 决定，
 *      渲染层按**列**做一次 tint（每列一次，不是每像素）。
 *   ③ 模板本身是**纯数据 + 纯函数**，不碰 worldMap、不碰 rand()。
 *      这样它能在 test_canity 里被完整单测，也能在生成失败时被安全跳过。
 *
 * 为什么模板只有 6×6 上限：36×36 中模板仍控制在 6×6，保持房间与走廊的比例，
 * 再大就会把 DFS 走廊挤没，玩家会失去"探索"的感觉。
 * ================================================================ */
#ifndef CANITY_ROOM_TEMPLATE_H
#define CANITY_ROOM_TEMPLATE_H

#include "common.h"

#define ROOM_MAX_W 6
#define ROOM_MAX_H 6
/* 2026-11-06：4 -> 7。补上三个**一直没进地图**的主题：
   砖墙 BRICK / 地狱 HELL / 血肉洞窟 FLESH CAVE。
   之前 7 种全局主题只用了 4 种，而没用到的三种恰恰最有辨识度。 */
#define ROOM_TEMPLATE_COUNT 7

/* tiles 取值 */
#define ROOM_TILE_FLOOR  0
#define ROOM_TILE_WALL   1
#define ROOM_TILE_PILLAR 2
#define ROOM_TILE_WATER  3

/* featureId */
#define ROOM_FEATURE_NONE   0
#define ROOM_FEATURE_STEAM  1     /* 锅炉房：蒸汽 */
#define ROOM_FEATURE_SHELF  2     /* 仓储室：货架 */
#define ROOM_FEATURE_POWER  3     /* 配电室：供电 */
#define ROOM_FEATURE_BRICK  4     /* 砖墙：拆墙 */
#define ROOM_FEATURE_HELL   5     /* 地狱：血祭 */
#define ROOM_FEATURE_FLESH  6     /* 血肉洞窟：喂食 */

typedef struct {
    int id;
    int w, h;
    unsigned char tiles[ROOM_MAX_H][ROOM_MAX_W];
    int themeId;      /* 复用现有 7 主题编号，决定墙面色调 */
    int featureId;
} RoomTemplate;

/* 取模板；越界返回 NULL */
const RoomTemplate* RoomTemplateGet(int id);

/* 旋转 90°×rot（rot 0..3，顺时针）。
   宽高一起交换，tiles 跟着转。rot 越界按 0 处理。
   失败返回 0，成功返回 1。 */
int RoomTemplateRotate(int id, int rot, RoomTemplate* out);

/* 模板里有几格是**可通行**的（FLOOR / WATER）。 */
int RoomTemplateFloorCount(const RoomTemplate* t);

/* 纯几何：把模板放到左上角 (ox,oy)，会不会越界。
   margin = 距地图外墙至少留几格（1 = 不许贴外墙）。
   不检查保留区 —— 那是调用方的事（出生区/出口/配电箱）。 */
int RoomTemplateFits(const RoomTemplate* t, int ox, int oy, int margin);

#endif /* CANITY_ROOM_TEMPLATE_H */

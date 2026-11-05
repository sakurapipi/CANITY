/* ==================================================================
 * CANITY — 区域属性表（v2.4-rc C2）
 * ------------------------------------------------------------------
 * 为什么需要它
 *   主题房间要"看得出不同"，声学传播要知道"这是什么地面"，
 *   以后的玩法事件要知道"这块是蒸汽区还是积水区" ——
 *   三件事看的是同一份数据。与其各写一套全局数组，不如一张表。
 *
 * 体积
 *   24×24 × 4 B = **2.25 KB**，静态常驻。可以整体清零，不涉及分配。
 *
 * 不做什么
 *   · 不进存档块。房间放置是 (worldMap + roomSeed) 的确定性函数，
 *     读档后重放一次就能重建（见 C6）。
 *   · 不存具体家具/剧情状态 —— 那是实体层的事。
 * ================================================================ */
#ifndef CANITY_CELL_META_H
#define CANITY_CELL_META_H

#include "common.h"

/* acousticId */
#define ACOUSTIC_DEFAULT 0
#define ACOUSTIC_HARD    1   /* 金属/瓷砖：余响长 */
#define ACOUSTIC_METAL   2   /* 管道/货架 */
#define ACOUSTIC_WATER   3   /* 积水：脚步更响 */
#define ACOUSTIC_OPEN    4   /* 开阔：传得远 */

/* flags 位 */
#define CELLFLAG_STEAM  (1u << 0)
#define CELLFLAG_SHELF  (1u << 1)
#define CELLFLAG_POWER  (1u << 2)
#define CELLFLAG_WATER  (1u << 3)

typedef struct {
    unsigned char roomId;      /* 0 = 普通区；1..n = 第 n 个房间 */
    unsigned char themeId;     /* 0..THEME_COUNT-1 */
    unsigned char acousticId;
    unsigned char flags;
} CellMeta;

extern CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];
extern int roomCount;

void CellMetaReset(void);

/* 把一块矩形区域标记成某个房间（越界自动夹住）。 */
void CellMetaFillRoom(int x0, int y0, int w, int h,
                      int roomId, int themeId, int acousticId, unsigned flags);

/* 单格查询；越界返回默认值。 */
const CellMeta* CellMetaAt(int cx, int cy);

/* 渲染用：这一格的墙面 tint（0 = 不改色，否则是 0..255 的混合强度）。
   普通区恒为 0 —— 让 render 走原路径，零成本。 */
unsigned char CellTintFor(int cx, int cy);

/* 渲染用：房间的**补光色**（加色量，0..255 通道）。
   返回 0 = 不改，1 = 已写入 outR / outG / outB。
   注意这是"给墙面的环境光加一点房间色"，不是给贴图染色 ——
   不改纹理集，所以不需要第二套贴图，也不吃额外纹理内存。 */
int CellTintRGB(int cx, int cy, int* r, int* g, int* b);

/* 声学用：这一格属于哪种地面。 */
int CellAcousticAt(float x, float y);

#endif /* CANITY_CELL_META_H */

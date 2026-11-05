#ifndef CANITY_MAP_ROOMS_H
#define CANITY_MAP_ROOMS_H

#include "common.h"

/* ==================================================================
 * 主题房间放置（v2.4-rc C3）
 * ------------------------------------------------------------------
 * 在旧版「5 个 2×2 随机小房间」之后调用。
 * 返回**实际放下**的房间数（可能少于 wantCount —— 放不下就跳过，
 * 绝不因此让整层生成失败）。
 * 会写 worldMap 与 cellMeta。
 * ================================================================ */
int GenerateThemeRooms(int wantCount);

/* 当前层已放置的房间数（等于 cellMeta 里最大的 roomId）。 */
int RoomCount(void);

/* ==================================================================
 * 房间家具（v2.4-rc）
 * ------------------------------------------------------------------
 * 之前模板里的"货架"只是**一格墙**，在黑暗里就是一根墙垛。
 * 现在改成真正的道具精灵，放在房间内圈、贴着墙角。
 *
 * 只做装饰，不参与碰撞 —— 所以刻意贴着房间内墙边缘放，
 * 玩家贴着墙走时不会穿过它。
 * ================================================================ */
#define MAX_ROOM_PROPS 16
#define ROOMPROP_SHELF   0
#define ROOMPROP_CONSOLE 1

typedef struct {
    float x, y;
    int   kind;
    int   active;
    int   blocks;     /* 1 = 挡路（参与 Blocked）；0 = 纯装饰 */
    float height;     /* 总高（墙高 = 1.0） */
} RoomProp;

extern RoomProp roomProps[MAX_ROOM_PROPS];
extern int roomPropCount;

void RoomPropReset(void);
int  RoomPropAdd(float x, float y, int kind);   /* 返回 1 = 放下 */
void RoomPropRemoveLast(void);                  /* 回滚最后一件（连通性检查失败时用） */

/* 这一格有没有挡路的家具。给 game.c 的 Blocked() 用。 */
int  PropBlocksAt(int cx, int cy);

/* 这个位置有没有家具（含装饰）。测试用。 */
int  RoomPropAt(float x, float y);

/* ==================================================================
 * v2.4-rc P3：**分层伪立体**
 * ------------------------------------------------------------------
 * 一个物件 = 1~4 个精灵层，每层有自己的 baseZ（底边离地高度）
 * 和 halfH（投影高度）。走动时层间有视差 ——
 * 这是 billboard 单图做不到的立体感来源。
 * 墙高 = 1.0；层叠加后不得超过 1.0。
 * ================================================================== */
#define PROP_MAX_LAYERS 4

typedef struct {
    float baseZ;
    float halfH;
    int   spr;
} PropLayer;

/* 返回层数（0..PROP_MAX_LAYERS）；未知 kind 返回 0。 */
int RoomPropLayers(int kind, PropLayer* out, int maxOut);

/* 把某层映射到精灵 id —— 单独抽出来是为了让单测能验证，
   不需要初始化渲染器（精灵在测试环境里是空指针）。 */
int RoomPropLayerSprite(int kind, int layerIndex);

#endif /* CANITY_MAP_ROOMS_H */

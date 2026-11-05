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
} RoomProp;

extern RoomProp roomProps[MAX_ROOM_PROPS];
extern int roomPropCount;

void RoomPropReset(void);
int  RoomPropAdd(float x, float y, int kind);   /* 返回 1 = 放下 */

#endif /* CANITY_MAP_ROOMS_H */

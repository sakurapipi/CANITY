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

#endif /* CANITY_MAP_ROOMS_H */

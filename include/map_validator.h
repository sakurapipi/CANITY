/* ==================================================================
 * CANITY — 地图校验器（v2.4-rc C07）
 * ------------------------------------------------------------------
 * 只查**几何与房间**，不依赖玩法状态（容器/保险丝/怪物）。
 * 原因：玩法状态的合法性由生成流程自己保证，而地图几何一旦坏了
 * 就是"整层打不过去"，必须有一个独立的、只读的判据。
 *
 * 用法：
 *   游戏里不调用（每帧都跑没意义）；
 *   tools/map_gen_dump --validate N 跑 N 个种子，统计失败率。
 * ================================================================== */
#ifndef CANITY_MAP_VALIDATOR_H
#define CANITY_MAP_VALIDATOR_H

#include "common.h"

typedef struct {
    int ok;                    /* 1 = 全部通过 */
    int rooms;                 /* 本层房间数 */
    int roomFloorCells;        /* 房间地板格数 */
    int roomCellsUnreachable;  /* 走不到的房间地板格 */
    int propCount;             /* 家具数 */
    int propsOnWall;           /* 落在墙上的家具（不该发生） */
    int propsOutsideRoom;      /* 落在房间外的家具（不该发生） */
    int spawnOpen;             /* 出生点可通行 */
    int exitReachable;         /* 出口可达 */
    int floors;                /* 全图可通行格数 */
    int isolatedFloors;        /* 从出生点走不到的可通行格 */
    char firstError[64];
} MapCheck;

/* 只读：不改 worldMap / cellMeta / roomProps 里的任何东西。 */
void MapValidate(MapCheck* out);

#endif /* CANITY_MAP_VALIDATOR_H */

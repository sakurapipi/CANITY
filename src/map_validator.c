/* CANITY — 地图校验器实现（v2.4-rc C07）
 *
 * 全部是只读遍历 + 一次 BFS，不分配、不写、不调 rand()。
 * 所以在游戏里、在测试里、在洪泛工具里都能安全调用。
 */
#include "common.h"
#include "map_topology.h"
#include "map_validator.h"
#include "cell_meta.h"
#include "map_rooms.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"

static void SetErr(MapCheck* m, const char* msg) {
    if (!m || m->firstError[0]) return;
    int i = 0;
    while (msg[i] && i < (int)sizeof(m->firstError) - 1) {
        m->firstError[i] = msg[i];
        i++;
    }
    m->firstError[i] = 0;
}

void MapValidate(MapCheck* out) {
    if (!out) return;
    int i;
    for (i = 0; i < (int)sizeof(*out); i++) ((char*)out)[i] = 0;
    out->ok = 1;

    /* ---- ① 出生点 ---- */
    if (worldMap[1][1]) {
        out->spawnOpen = 0;
        out->ok = 0;
        SetErr(out, "spawn cell is a wall");
    } else {
        out->spawnOpen = 1;
    }

    /* ---- ② BFS：从出生点出发，墙与**挡路家具**都算障碍 ---- */
    static int seen[MAP_HEIGHT][MAP_WIDTH];
    static int qx[MAP_WIDTH * MAP_HEIGHT], qy[MAP_WIDTH * MAP_HEIGHT];
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) seen[y][x] = 0;

    int head = 0, tail = 0;
    if (!worldMap[1][1]) {
        seen[1][1] = 1; qx[tail] = 1; qy[tail] = 1; tail++;
    }
    while (head < tail) {
        int cx = qx[head], cy = qy[head]; head++;
        static const int dxs[4] = { 1, -1, 0, 0 };
        static const int dys[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; k++) {
            int nx = cx + dxs[k], ny = cy + dys[k];
            if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
            if (seen[ny][nx] || worldMap[ny][nx]) continue;
            if (PropBlocksAt(nx, ny)) continue;
            seen[ny][nx] = 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }

    /* ---- ③ 房间地板格必须全部可达 ---- */
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (cellMeta[y][x].roomId == 0) continue;
            if (worldMap[y][x]) continue;
            if (PropBlocksAt(x, y)) continue;
            out->roomFloorCells++;
            if (!seen[y][x]) out->roomCellsUnreachable++;
        }
    out->rooms = RoomCount();
    if (out->roomCellsUnreachable > 0) {
        out->ok = 0;
        SetErr(out, "room floor cells unreachable");
    }

    /* ---- ④ 家具不许压墙、不许跑到房间外 ---- */
    {


        out->propCount = roomPropCount;
        for (int k = 0; k < roomPropCount && k < MAX_ROOM_PROPS; k++) {
            if (!roomProps[k].active) continue;
            int cx = (int)roomProps[k].x, cy = (int)roomProps[k].y;
            if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) { out->propsOnWall++; continue; }
            if (worldMap[cy][cx]) out->propsOnWall++;
            if (cellMeta[cy][cx].roomId == 0) out->propsOutsideRoom++;
        }
        if (out->propsOnWall > 0) { out->ok = 0; SetErr(out, "a room prop sits on a wall"); }
        if (out->propsOutsideRoom > 0) { out->ok = 0; SetErr(out, "a room prop sits outside its room"); }
    }

    /* ---- ⑤ 全图连通性：孤立空地太多说明生成器把地图切碎了 ---- */
    for (int y = 1; y < MAP_HEIGHT - 1; y++)
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (worldMap[y][x] || PropBlocksAt(x,y)) continue;
            out->floors++;
            if (!seen[y][x]) out->isolatedFloors++;
        }

    /* ---- ⑥ 出口可达 ---- */
    {
        int ex = (int)exitX, ey = (int)exitY;
        if (ex < 0 || ey < 0 || ex >= MAP_WIDTH || ey >= MAP_HEIGHT) {
            out->exitReachable = 0;
            out->ok = 0;
            SetErr(out, "exit out of map bounds");
        } else {
            out->exitReachable = seen[ey][ex] ? 1 : 0;
            if (!out->exitReachable) {
                out->ok = 0;
                SetErr(out, "exit unreachable from spawn");
            }
        }
    }

    /* ---- ⑦ 孤立空地：**只报告，不判死** ----
           孤立空间本身不是死局 —— 只要保险丝/配电箱/出口都不在里面。
           真正的保证由 game.c 的"容器候选按可达性过滤"给出。
           这里留一个比例字段，供洪泛统计观察生成器的切碎程度。 */
    if (out->floors > 0 && out->isolatedFloors > 0) {
        /* Blocking furniture is excluded above; every remaining floor must connect. */
        out->ok = 0;
        SetErr(out, "map contains unreachable walkable floor cells");
    }
    int geometry[MAP_HEIGHT][MAP_WIDTH];
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)geometry[y][x]=worldMap[y][x] || PropBlocksAt(x,y);
    MapTopology topology=MapTopologyMeasure(&geometry[0][0],MAP_WIDTH,MAP_HEIGHT);
    if(!MapTopologyAccepts(topology)){out->ok=0;SetErr(out,"topology exceeds reachability/dead-end bounds");}
    if(out->rooms>1){out->ok=0;SetErr(out,"more than one themed room");}

}

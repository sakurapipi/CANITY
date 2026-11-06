/* ==================================================================
 * CANITY — 离线地图生成 / dump（v2.4-rc 诊断 + 种子洪泛工具）
 * ------------------------------------------------------------------
 * 为什么要它
 *   ① "房间的柱子到底挖出来没有" —— 靠读代码和猜都没用，
 *      必须看**真实生成出来的那张图**。
 *   ② R9 的 1000 种子洪泛本来就需要一个不依赖窗口/纹理/声音的
 *      生成器，这个工具就是那个生成器。
 *
 * 它**复制**了 game.c 里 GenerateLevel 的开头三段（DFS / 环路 / 2x2 小房间），
 * 然后调用**真正的** GenerateThemeRooms。所以：
 *   · 房间放置逻辑是真代码，不是重写
 *   · 迷宫骨架是复制来的，如果 game.c 那边改了，这里要同步
 *     （所以我把行号写在下面，方便对照）
 *
 * 对照位置：game.c 1712-1753（DFS / 环路 / 小房间）
 *
 * 用法
 *   map_gen_dump <seed>            打印这一层
 *   map_gen_dump <seed> --rooms    只打印房间格（用不同符号）
 * ================================================================ */
#include "common.h"
#include "room_template.h"
#include "cell_meta.h"
#include "map_rooms.h"
#include "map_validator.h"
#include "cell_meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* map_rooms.c 通过 game.h 引用这三个符号，这里给它们定义 */
int   worldMap[MAP_HEIGHT][MAP_WIDTH];
int   explored[MAP_HEIGHT][MAP_WIDTH];
float exitX = 12.5f, exitY = 12.5f;

static void CarveMaze(void) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) worldMap[y][x] = 1;

    int stackX[MAP_WIDTH * MAP_HEIGHT], stackY[MAP_WIDTH * MAP_HEIGHT], top = 0;
    int cx = 1, cy = 1;
    worldMap[cy][cx] = 0; stackX[top] = cx; stackY[top] = cy; top++;
    int dxs[] = { 2, 0, -2, 0 }, dys[] = { 0, 2, 0, -2 };

    while (top > 0) {
        cx = stackX[top - 1]; cy = stackY[top - 1];
        int validDirs[4], count = 0;
        for (int i = 0; i < 4; i++) {
            int nx = cx + dxs[i], ny = cy + dys[i];
            if (nx > 0 && nx < MAP_WIDTH - 1 && ny > 0 && ny < MAP_HEIGHT - 1 &&
                worldMap[ny][nx] == 1)
                validDirs[count++] = i;
        }
        if (count > 0) {
            int dir = validDirs[rand() % count];
            int nx = cx + dxs[dir], ny = cy + dys[dir];
            worldMap[cy + dys[dir] / 2][cx + dxs[dir] / 2] = 0;
            worldMap[ny][nx] = 0;
            stackX[top] = nx; stackY[top] = ny; top++;
        } else top--;
    }

    for (int y = 1; y < MAP_HEIGHT - 1; y++)
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (worldMap[y][x] == 1 &&
                ((worldMap[y][x - 1] == 0 && worldMap[y][x + 1] == 0) ||
                 (worldMap[y - 1][x] == 0 && worldMap[y + 1][x] == 0))) {
                if (rand() % 100 < 22) worldMap[y][x] = 0;   /* 与 game.c 同步 */
            }
        }

    /* 死路连通（与 game.c 同步，2026-11-06） */
    for (int dpass = 0; dpass < 4; dpass++) {
        for (int y = 1; y < MAP_HEIGHT - 1; y++) {
            for (int x = 1; x < MAP_WIDTH - 1; x++) {
                if (worldMap[y][x]) continue;
                int openN = 0;
                if (!worldMap[y][x - 1]) openN++;
                if (!worldMap[y][x + 1]) openN++;
                if (!worldMap[y - 1][x]) openN++;
                if (!worldMap[y + 1][x]) openN++;
                if (openN != 1) continue;
                static const int dxs2[4] = { 1, -1, 0, 0 };
                static const int dys2[4] = { 0, 0, 1, -1 };
                int done = 0;
                for (int k = 0; k < 4 && !done; k++) {
                    int wx = x + dxs2[k], wy = y + dys2[k];
                    if (wx < 1 || wy < 1 || wx >= MAP_WIDTH - 1 || wy >= MAP_HEIGHT - 1) continue;
                    if (!worldMap[wy][wx]) continue;
                    int bx = wx + dxs2[k], by = wy + dys2[k];
                    if (bx < 1 || by < 1 || bx >= MAP_WIDTH - 1 || by >= MAP_HEIGHT - 1) continue;
                    if (!worldMap[by][bx]) {
                        worldMap[wy][wx] = 0;
                        done = 1;
                    } else {
                        /* 3 格远视：隔两堵墙也是空地 → 打第二格，保留门框 */
                        int c2x = bx + dxs2[k], c2y = by + dys2[k];
                        if (c2x < 1 || c2y < 1 || c2x >= MAP_WIDTH - 1 || c2y >= MAP_HEIGHT - 1) continue;
                        if (worldMap[c2y][c2x]) continue;
                        worldMap[by][bx] = 0;
                        done = 1;
                    }
                }
            }
        }
    }

    for (int i = 0; i < 3; i++) {   /* 与 game.c 同步：5 -> 3 */
        int rx = 2 + rand() % (MAP_WIDTH - 4);
        int ry = 2 + rand() % (MAP_HEIGHT - 4);
        for (int oy = 0; oy < 2; oy++)
            for (int ox = 0; ox < 2; ox++)
                worldMap[ry + oy][rx + ox] = 0;
    }
}

/* ==================================================================
 * 连通性：从出生点 (1,1) 做 BFS，看每个房间的**内部**格是否都能走到。
 * 这是"房间自带的门和向外挖的连接"到底成不成立的唯一判据 ——
 * 封死的房间会让整层无法通关。
 * ================================================================ */
static int CheckRoomReachability(int* outRoomCells, int* outUnreachable) {
    static int seen[MAP_HEIGHT][MAP_WIDTH];
    static int qx[MAP_WIDTH * MAP_HEIGHT], qy[MAP_WIDTH * MAP_HEIGHT];
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) seen[y][x] = 0;

    int head = 0, tail = 0;
    if (worldMap[1][1]) { *outRoomCells = 0; *outUnreachable = 0; return 0; }
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

    int cells = 0, bad = 0;
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (cellMeta[y][x].roomId == 0) continue;
            if (worldMap[y][x]) continue;        /* 只查地板格 */
            cells++;
            if (!seen[y][x]) bad++;
        }
    *outRoomCells = cells;
    *outUnreachable = bad;
    return 1;
}

/* ==================================================================
 * 出口选择：**和 game.c 的 FindFarthestFromCell 同语义**
 * ------------------------------------------------------------------
 * 第一版这里是"直线距离最远的空格"，结果把出口选到了
 * 跟出生点不连通的孤立空地上 —— 1000 种子洪泛报出 1.7% 的
 * "exit unreachable"，那是**工具的 bug**，不是游戏的。
 * 游戏里一直用的是 BFS。
 * ================================================================ */
static void PickExitLikeGame(void) {
    static int dist[MAP_HEIGHT][MAP_WIDTH];
    static int qx[MAP_WIDTH * MAP_HEIGHT], qy[MAP_WIDTH * MAP_HEIGHT];
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) dist[y][x] = -1;

    int head = 0, tail = 0;
    if (worldMap[1][1]) { dist[1][1] = 0; qx[tail] = 1; qy[tail] = 1; tail++; }
    else { dist[1][1] = 0; qx[tail] = 1; qy[tail] = 1; tail++; }

    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        int cx = qx[head], cy = qy[head]; head++;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dxs[k], ny = cy + dys[k];
            if (nx < 1 || nx >= MAP_WIDTH - 1 || ny < 1 || ny >= MAP_HEIGHT - 1) continue;
            if (worldMap[ny][nx]) continue;
            if (PropBlocksAt(nx, ny)) continue;
            if (dist[ny][nx] >= 0) continue;
            dist[ny][nx] = dist[cy][cx] + 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }

    int best = -1;
    exitX = 1.5f; exitY = 1.5f;
    for (int y = 1; y < MAP_HEIGHT - 1; y++)
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (dist[y][x] > best) { best = dist[y][x]; exitX = (float)x; exitY = (float)y; }
        }

    /* 与 game.c 同步：撤离点至少两条通路 */
    {
        int ex = (int)exitX, ey = (int)exitY;
        if (ex >= 1 && ey >= 1 && ex < MAP_WIDTH - 1 && ey < MAP_HEIGHT - 1 && !worldMap[ey][ex]) {
            int openN = 0;
            for (int k = 0; k < 4; k++) {
                int nx = ex + dxs[k], ny = ey + dys[k];
                if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
                if (!worldMap[ny][nx]) openN++;
            }
            if (openN < 2) {
                for (int k = 0; k < 4; k++) {
                    int wx = ex + dxs[k], wy = ey + dys[k];
                    if (wx < 1 || wy < 1 || wx >= MAP_WIDTH - 1 || wy >= MAP_HEIGHT - 1) continue;
                    if (!worldMap[wy][wx]) continue;
                    int bx = wx + dxs[k], by = wy + dys[k];
                    if (bx < 1 || by < 1 || bx >= MAP_WIDTH - 1 || by >= MAP_HEIGHT - 1) continue;
                    if (worldMap[by][bx]) continue;
                    worldMap[wy][wx] = 0;
                    break;
                }
            }
        }
    }
}

static void PrintMap(int seed, int roomsOnly) {
    printf("=== seed %d   rooms=%d ===\n", seed, RoomCount());
    printf("    ");
    for (int x = 0; x < MAP_WIDTH; x++) printf("%d", x % 10);
    printf("\n");
    for (int y = 0; y < MAP_HEIGHT; y++) {
        printf("%3d ", y);
        for (int x = 0; x < MAP_WIDTH; x++) {
            int wall = worldMap[y][x];
            unsigned char rid = cellMeta[y][x].roomId;
            char c;
            if (roomsOnly && rid == 0) c = ' ';
            else if (rid != 0) c = wall ? '+' : ',';   /* + 房间结构  , 房间地面 */
            else c = wall ? '#' : '.';
            putchar(c);
        }
        putchar('\n');
    }
}

static long g_exitLonely = 0;
static long g_deadEnds = 0;   /* 只有 1 个开口的空地格 */
static long g_deadEndSeeds = 0;
static long g_theme[8] = { 0 };   /* 主题出现次数（0..7） */
static long g_props = 0;
static long g_deadRoom = 0;   /* 房间内部的凹角 */
static long g_valFail = 0;
static long g_valRooms = 0;
static long g_valRoomCells = 0;
static long g_valUnreach = 0;
static long g_valIsoFloors = 0;
static long g_valFloors = 0;
static int  g_valFirstFailSeed = -1;
static char g_valFirstErr[64] = { 0 };
static long g_sealCells = 0, g_sealBad = 0;
static int  g_sealSeeds = 0;

/* 生成一层（不含打印），统计用 */
static int GenerateOne(int seed) {
    srand((unsigned)seed);
    CarveMaze();
    PickExitLikeGame();
    CellMetaReset();
    int want = 1 + (rand() % 2);
    int r = GenerateThemeRooms(want);
    g_props += roomPropCount;

    /* 死路：四邻里只有 1 个是空地的位置。
       2026-11-06：**拆成"迷宫格"和"房间内"两项** ——
       房间模板里的柱子会自然形成凹角，混在一起数会看不清问题在哪。 */
    {
        int dead = 0, deadRoom = 0;
        for (int y = 1; y < MAP_HEIGHT - 1; y++) {
            for (int x = 1; x < MAP_WIDTH - 1; x++) {
                if (worldMap[y][x]) continue;
                int openN = 0;
                if (!worldMap[y][x - 1]) openN++;
                if (!worldMap[y][x + 1]) openN++;
                if (!worldMap[y - 1][x]) openN++;
                if (!worldMap[y + 1][x]) openN++;
                if (openN != 1) continue;
                if (cellMeta[y][x].roomId != 0) deadRoom++;
                else dead++;
            }
        }
        g_deadEnds += dead;
        g_deadRoom += deadRoom;
        if (dead > 20) g_deadEndSeeds++;
    }

    /* 主题分布：靠 cellMeta 的 themeId 统计，能证明新房间真的生成出来了 */
    {
        int seen[8] = { 0 };
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) {
                extern CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];
                /* 只数**房间内**的格子：房间外的 themeId 是 0（砖墙），
                   全数的话 t0 永远 100%，等于什么都没测。 */
                if (cellMeta[y][x].roomId == 0) continue;
                unsigned char th = cellMeta[y][x].themeId;
                if (th < 8) seen[th] = 1;
            }
        for (int k = 0; k < 8; k++) if (seen[k]) g_theme[k]++;
    }

    /* 出口通路数：< 2 就是"只有一个方向能进" */
    {
        int ex = (int)exitX, ey = (int)exitY;
        int openN = 0;
        if (ex >= 1 && ey >= 1 && ex < MAP_WIDTH - 1 && ey < MAP_HEIGHT - 1) {
            static const int ex4[4] = { 1, -1, 0, 0 };
            static const int ey4[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; k++) {
                int nx = ex + ex4[k], ny = ey + ey4[k];
                if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
                if (!worldMap[ny][nx]) openN++;
            }
        }
        if (openN < 2) g_exitLonely++;
    }

    {
        MapCheck mc;
        MapValidate(&mc);
        g_valRooms += mc.rooms;
        g_valRoomCells += mc.roomFloorCells;
        g_valUnreach += mc.roomCellsUnreachable;
        g_valIsoFloors += mc.isolatedFloors;
        g_valFloors += mc.floors;
        if (!mc.ok) {
            g_valFail++;
            if (g_valFirstFailSeed < 0) {
                g_valFirstFailSeed = seed;
                int i2 = 0;
                while (mc.firstError[i2] && i2 < 63) { g_valFirstErr[i2] = mc.firstError[i2]; i2++; }
                g_valFirstErr[i2] = 0;
            }
        }
    }

    {
        int cells = 0, bad = 0;
        CheckRoomReachability(&cells, &bad);
        g_sealCells += cells;
        g_sealBad += bad;
        if (bad > 0) g_sealSeeds++;
    }
    return r;
}


/* ==================================================================
 * 2026-11-06：**工具桩**
 * ------------------------------------------------------------------
 * map_rooms.c 的房间放置结束时会调 HatchPlaceInRoom()，
 * 而那个函数在 game.c 里（要读金币/电量等玩法全局）。
 * 这个工具只验证地形与房间，不链 game.c —— 所以给一个桩。
 * 返回 -1 = "放不下"，对地形统计没有任何影响。
 * ================================================================ */
/* 2026-11-06：家具已改成可穿透（用户要求），dump 里的可达性判断也要跟上 */


int HatchPlaceInRoom(int fx, int fy, int w, int h, int themeId) {
    (void)fx; (void)fy; (void)w; (void)h; (void)themeId;
    return -1;
}
/* RoomReplay 现在会先 HatchReset()，工具不链 hatch.c，补桩 */
void HatchReset(void) { }

int main(int argc, char** argv) {
    int seed = (argc > 1) ? atoi(argv[1]) : 1;
    int roomsOnly = 0;
    int statN = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--rooms")) roomsOnly = 1;
        else if (!strcmp(argv[i], "--stat") && i + 1 < argc) statN = atoi(argv[++i]);
    }

    if (statN > 0) {
        int hist[8] = { 0 };
        int unplaced = 0;
        for (int i = 0; i < statN; i++) {
            int r = GenerateOne(i + 1);
            if (r < 0) r = 0;
            if (r > 7) r = 7;
            hist[r]++;
            if (r == 0) unplaced++;
        }
        printf("seeds=%d  ", statN);
        for (int k = 0; k < 8; k++)
            if (hist[k]) printf("rooms=%d:%d  ", k, hist[k]);
        printf("\nno-room-rate=%.1f%%\n", 100.0 * (double)unplaced / (double)statN);
        printf("room-floor-cells=%ld  unreachable=%ld  sealed-seeds=%d/%d\n",
               g_sealCells, g_sealBad, g_sealSeeds, statN);
        printf("exit-approaches<2: %ld/%d (%.1f%%)\n", g_exitLonely, statN,
               100.0 * (double)g_exitLonely / (double)statN);
        {
            extern int RoomTplTried[ROOM_TEMPLATE_COUNT];
            extern int RoomTplPlaced[ROOM_TEMPLATE_COUNT];
            printf("template tried/placed: ");
            for (int k = 0; k < ROOM_TEMPLATE_COUNT; k++)
                printf("t%d=%d/%d ", k, RoomTplTried[k], RoomTplPlaced[k]);
            printf("\n");
        }
        printf("theme-rooms seen per seed: ");
        for (int k = 0; k < 8; k++) {
            if (g_theme[k] > 0) printf("t%d=%.1f%% ", k, 100.0 * (double)g_theme[k] / (double)statN);
        }
        printf("\n");
        printf("dead-ends(maze): avg=%.1f per level | dead-ends(in rooms): avg=%.1f\n",
               (double)g_deadEnds / (double)statN,
               (double)g_deadRoom / (double)statN);
        printf("dead-ends: avg=%.1f per level, seeds with >20: %ld/%d\n",
               (double)g_deadEnds / (double)statN, g_deadEndSeeds, statN);
        printf("room-props=%ld  avg-per-level=%.1f\n", g_props,
               (double)g_props / (double)statN);
        printf("VALIDATE: fail=%ld/%d (%.2f%%)  rooms=%ld  room-cells=%ld  unreachable=%ld\n",
               g_valFail, statN, 100.0 * (double)g_valFail / (double)statN,
               g_valRooms, g_valRoomCells, g_valUnreach);
        printf("VALIDATE: floors=%ld  isolated=%ld (%.2f%%)\n",
               g_valFloors, g_valIsoFloors,
               g_valFloors ? 100.0 * (double)g_valIsoFloors / (double)g_valFloors : 0.0);
        if (g_valFirstFailSeed >= 0)
            printf("VALIDATE: first failing seed=%d  err=%s\n", g_valFirstFailSeed, g_valFirstErr);
        return 0;
    }

    srand((unsigned)seed);

    CarveMaze();

    /* 出口：和游戏一样用 BFS 最远格（原来这里是直线最远 ——
       那会把出口选在跟出生点不连通的孤岛上，制造假的"死局"）。 */
    PickExitLikeGame();

    CellMetaReset();
    /* 2026-11-06：跟游戏同步 —— 前两层 1 间，之后 2 间 */
    { int want = 2; (void)GenerateThemeRooms(want); }

    PrintMap(seed, roomsOnly);
    return 0;
}

/* 2026-11-16：从 render.c 拆出的**地图渲染**（域内解耦，行为不变）。
   为什么单独成文件：render.c 已到 4200 行，超出模块预算（2500）；
   地图/图例这一簇只依赖公共绘制 API 与 MAP_* 常量，边界干净，
   是风险最低的第一块。
   S3/S4：本文件不拥有任何全局状态，只读 explored/mapFullScreen 等既有符号。 */
#include "common.h"
#include "render.h"
#include "ui.h"
#include "ui_text.h"
#include "map_rooms.h"
#include "upgrade_run.h"
#include "player_api.h"
#include "world_api.h"
#include "lang.h"
#include "state.h"
#include "plat.h"
#include "present_api.h"
#include "echo_marks.h"

/* 原来定义在 render.c 里的小地图格子尺寸；随本簇一起搬过来。 */
#define MAP_CORNER_CELL 8

static void DrawMapLegend(void) {

    struct { unsigned int col; const char* zh; const char* en; } items[6] = {
        /* v3.3 **图例颜色两两分开** ——
           试玩实测：「YOU 和 SAFE 都是绿色，FUSE 和 HIDE 都是青色，两两分不清」。
           现在每个标记一个色相：绿 / 金 / 橙 / 灰蓝 / 蓝 / 浅青。 */
        { 0x0030FF60u, "你",      "YOU"    },   /* 亮绿 */
        { 0x00E8C040u, "出口",    "EXIT"   },   /* 金 */
        { 0x00FF8040u, "保险丝箱","FUSE"   },   /* 橙（原青，与 HIDE 撞） */
        { 0x00B0C0D0u, "配电箱",  "BREAKER"},   /* 灰蓝 */
        { 0x004090FFu, "安全屋",  "SAFE"   },   /* 蓝（原绿，与 YOU 撞） */
        { 0x0090E0FFu, "藏身点","HIDE"   },   /* 浅青 */
    };

    int rowH = TextHeightWin() + UIS(6);
    int boxW = UIS(150);
    int boxH = rowH * 6 + UIS(16);
    int bx0 = winW - boxW - UIS(24);
    int by0 = winH - boxH - UIS(24);
    BlendRectWin(bx0, by0, bx0 + boxW, by0 + boxH, 0x000A0E12u, 0.86f);
    FrameRectWin(bx0, by0, bx0 + boxW, by0 + boxH, 0x00C8B060u, 0.7f, 1);
    for (int k = 0; k < 6; k++) {
        int cy = by0 + UIS(8) + k * rowH + TextHeightWin() / 2;
        BlendRectWin(bx0 + UIS(10), cy - UIS(5),
                     bx0 + UIS(20), cy + UIS(5), items[k].col, 1.0f);
        DrawTextWin(bx0 + UIS(28), by0 + UIS(8) + k * rowH,
                    (g_lang == 1) ? items[k].zh : items[k].en, 0x00C8C4BCu);
    }
}

void RenderMap(void) {
    int cellSize, mapW, mapH, mapStartX, mapStartY;
    if (mapFullScreen) {
        cellSize = (renderW < renderH ? renderW : renderH) / (MAP_WIDTH + 2);
        if (cellSize < 3) cellSize = 3;
        mapW = MAP_WIDTH * cellSize;
        mapH = MAP_HEIGHT * cellSize;
        mapStartX = (renderW - mapW) / 2;
        mapStartY = (renderH - mapH) / 2;
    } else {
        cellSize = MAP_CORNER_CELL;
        /* ==================================================================
         * 2026-11-06：**地图变大不该让 HUD 变大**
         * ------------------------------------------------------------------
         * 24x24 时是 24*8 = 192px；36x36 变成 288px ——
         * 多出来的高度直接压到了右侧的状态提示（用户截图）。
         *
         * HUD 的占地面积应该是**固定**的：把格子缩到 190px 以内，
         * 地图越大格子越小，但面板尺寸不变。
         * ================================================================ */
        {
            int target = UIS(190);
            int maxCell = target / MAP_WIDTH;
            if (maxCell < 3) maxCell = 3;
            if (cellSize > maxCell) cellSize = maxCell;
        }
        mapW = MAP_WIDTH * cellSize;
        /* v3.0 **BUG 修复**：mapH 原来在 else 分支里**没有初始化**，
           而下面 `while (mapH > room ...)` 直接读它 ——
           读到栈上的垃圾值。垃圾值小的时候循环不跑，
           mapH 保持垃圾，于是 mapStartY 和地图高度全错。
           （gcc -Wmaybe-uninitialized 早就报了这条。） */
        mapH = MAP_HEIGHT * cellSize;
        /* v3.0：**小地图必须同时避开道具栏和 LIGHT/RUN** ——
           历史上踩了两次：
             ① 贴右下角       → 压住 LIGHT/RUN 两条
             ② 只抬不管上界   → 顶到右上角的道具栏（1 FLARE / 2 BOTTLE）
           这次算清楚可用区间；塞不下就**缩小格子**，绝不叠 HUD。 */
        {
            int itemsBot = UIS(115);             /* 顶部留白：给 LEVEL/SANITY 那一列让位 */
            /* 可用高度 = 顶部留白 到 底部 UI 带的上沿 */
            int room = (renderH - UIS(152)) - itemsBot;
            if (room < UIS(60)) room = UIS(60);

            /* 塞不下就缩格子（3px 是下限，再小就看不清了） */
            while (mapH > room && cellSize > 3) {
                cellSize--;
                mapW = MAP_WIDTH * cellSize;
                mapH = MAP_HEIGHT * cellSize;
            }
            /* v6 **小地图位置：右侧、底边贴着 LIGHT/RUN 条上沿** ——
               真实布局调查（800x600）：
                 · 道具栏三行   y 409..501（右下角！不是右上角）
                 · LIGHT/RUN 条 y 443 起
                 · WANDERING     y 207（已上移让开）
               所以地图必须整体待在 y=443 以上，否则会压住道具栏。
               我上一轮把它推到右下角，正好撞进道具栏（用户截图）。 */
            /* v6 **底部 UI 带 = 从 renderH-UIS(152) 往下**：
                 · 道具栏三行（右侧）  y 409..501
                 · LIGHT/RUN 条（左侧） y 443 起
               地图底边必须停在 401 以上，否则压住道具栏（用户截图）。 */
            int uiBandTop = renderH - UIS(152);        /* 401 */
            mapStartX = renderW - mapW - UIS(14);
            mapStartY = uiBandTop - mapH;
            if (mapStartY < itemsBot) mapStartY = itemsBot;   /* 上界：别顶到画面顶部 */
            if (mapStartX < 0) mapStartX = 0;
            if (mapStartY < 0) mapStartY = 0;
        }
    }

    if (mapFullScreen) {
        /* 全屏：整个画面压暗，突出地图 */
        int pad = cellSize;
        for (int y = mapStartY - pad; y < mapStartY + mapH + pad; y++)
            for (int x = mapStartX - pad; x < mapStartX + mapW + pad; x++)
                PutPixelBlend(x, y, 0x00000000, 0.72f);
    } else {
        /* 小窗：只给地图区域加个边框底板，不压暗整个画面 */
        int pad = 2;
        for (int y = mapStartY - pad; y < mapStartY + mapH + pad; y++) {
            if (y < 0 || y >= renderH) continue;
            for (int x = mapStartX - pad; x < mapStartX + mapW + pad; x++) {
                if (x < 0 || x >= renderW) continue;
                PutPixelBlend(x, y, 0x00000000, 0.82f);
            }
        }
    }

    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            unsigned int color = 0;
            float alpha = 0.8f;
            if (explored[y][x] == 0) { color = 0x00050505; alpha = 0.95f; }
            else {
                if (worldMap[y][x] == 1) color = 0x00888888;
                else { color = 0x00000000; alpha = 0.3f; }
                if ((int)exitX == x && (int)exitY == y) { color = 0x0000FF00; alpha = 1.0f; }
            }
            int bx = mapStartX + x * cellSize, by = mapStartY + y * cellSize;
            for (int dy = 0; dy < cellSize - 1; dy++)
                for (int dx = 0; dx < cellSize - 1; dx++)
                    PutPixelBlend(bx + dx, by + dy, color, alpha);
        }
    }

    /* 容器（未搜过 = 黄点闪烁）/ 配电箱（青点）/ 出口（绿或红）
       闪烁用主循环的连续时钟 g_timeSec，不用 GetTickCount()：
       后者的 15.6ms 台阶会让闪烁看起来像掉帧。 */
    for (int i = 0; i < containerCount; i++) {
        if (!containers[i].active || containers[i].searched) continue;
        /* v2.7：这些坐标是**存档里读回来的**，坏存档可以是任意值，
           直接当下标会越界读。先按地图尺寸拦一道。 */
        int cxi = (int)containers[i].x, cyi = (int)containers[i].y;
        if (cxi < 0 || cxi >= MAP_WIDTH || cyi < 0 || cyi >= MAP_HEIGHT) continue;
        if (!explored[cyi][cxi]) continue;
        if ((int)(g_timeSec * 4.5f) % 2) continue;
        int px = mapStartX + (int)containers[i].x * cellSize + cellSize / 2;
        int py = mapStartY + (int)containers[i].y * cellSize + cellSize / 2;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) PutPixel(px + dx, py + dy, 0x00E8C040);
    }
    if (breaker.active &&
        (int)breaker.x >= 0 && (int)breaker.x < MAP_WIDTH &&
        (int)breaker.y >= 0 && (int)breaker.y < MAP_HEIGHT &&
        explored[(int)breaker.y][(int)breaker.x]) {
        int px = mapStartX + (int)breaker.x * cellSize + cellSize / 2;
        int py = mapStartY + (int)breaker.y * cellSize + cellSize / 2;
        unsigned int c = breakerOpen ? 0x0030FF60 : 0x0030D0D0;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++)
                if (dx * dx + dy * dy <= 9) PutPixel(px + dx, py + dy, c);
    }

    /* v1.8：安全屋在小地图上标出来（绿色十字）—— 不然转头就忘在哪 */
    if (safeRoom.active && explored[(int)safeRoom.y][(int)safeRoom.x]) {
        int sx = mapStartX + (int)safeRoom.x * cellSize + cellSize / 2;
        int sy = mapStartY + (int)safeRoom.y * cellSize + cellSize / 2;
        for (int d = -3; d <= 3; d++) {
            PutPixel(sx + d, sy, 0x0060FF90);
            PutPixel(sx, sy + d, 0x0060FF90);
        }
    }

    /* v3.0：**藏身点也要标出来** ——
       逃生玩法里「哪里有柜子能躲」是最关键的信息，
       原来小地图上只有箱子/配电箱/安全屋/出口，藏身点完全不可见。
       画成青白色的菱形（区别于箱子的黄点），已探索过的才显示。 */
    for (int i = 0; i < hideSpotCount; i++) {
        if (!hideSpots[i].active) continue;
        int hxi = (int)hideSpots[i].x, hyi = (int)hideSpots[i].y;
        if (hxi < 0 || hxi >= MAP_WIDTH || hyi < 0 || hyi >= MAP_HEIGHT) continue;
        if (!explored[hyi][hxi]) continue;
        int px = mapStartX + hxi * cellSize + cellSize / 2;
        int py = mapStartY + hyi * cellSize + cellSize / 2;
        /* 菱形：比箱子的 5x5 方块小一点，一眼能分开 */
        /* v2.4.23：**坏掉的藏身点画红色 + 叉** ——
           用户要求「维修点要在地图上显示为红色」。
           坏了但还没修的点是最要紧的信息：它决定你还有没有退路。 */
        if (hideSpots[i].broken) {
            /* 红色菱形（比正常的更暗一档，不抢眼但一眼能认） */
            for (int d = -2; d <= 2; d++) {
                int span = 2 - (d < 0 ? -d : d);
                for (int e = -span; e <= span; e++)
                    PutPixel(px + e, py + d, 0x00FF3020);
            }
            /* 打一个叉：坏掉 = 不可用，形状上也要变 */
            for (int d = -3; d <= 3; d++) {
                PutPixel(px + d, py + d, 0x00FF6050);
                PutPixel(px + d, py - d, 0x00FF6050);
            }
        } else {
            for (int d = -2; d <= 2; d++) {
                int span = 2 - (d < 0 ? -d : d);
                for (int e = -span; e <= span; e++)
                    PutPixel(px + e, py + d, 0x0090E0FF);
            }
        }
    }

    /* ==================================================================
     * 2026-11-05：**回声弹标记**（资源雷达）
     * ------------------------------------------------------------------
     * 四种标记、四种形状/颜色，扫一眼能分开：
     *   EMK_MON    活物      红菱形（会动，5 秒）
     *   EMK_CRATE  普通容器   白方块（30 秒）
     *   EMK_FUSE   保险丝箱   **金方块** ← 玩家真正要找的东西
     *   EMK_MIMIC  宝箱怪     **红色闪烁方块** ← 一眼看出哪个箱子是假的
     * 铁律九 [AD-09]：威胁与资源的可读性预算不同 ——
     *   资源用实心方块（安静、好找），威胁用菱形 + 亮芯（更刺眼）。
     * ================================================================== */
    {



        int flash = ((int)(gameTime * 6.0f) & 1);   /* 宝箱怪红闪：每秒 3 次 */
        for (int i = 0; i < 24; i++) {
            if (!echoMarkActive[i]) continue;
            if (echoMarkT[i] <= 0.0f) continue;
            int mx = (int)echoMarkX[i], my = (int)echoMarkY[i];
            if (mx < 0 || mx >= MAP_WIDTH || my < 0 || my >= MAP_HEIGHT) continue;
            int px2 = mapStartX + mx * cellSize + cellSize / 2;
            int py2 = mapStartY + my * cellSize + cellSize / 2;
            int kind = echoMarkKind[i];
            if(kind!=0){
                int live=0;for(int ci=0;ci<containerCount;ci++){
                    if(containers[ci].active && !containers[ci].searched && Dist2(containers[ci].x,containers[ci].y,echoMarkX[i],echoMarkY[i])<.1f){live=1;break;}
                }
                if(!live){echoMarkActive[i]=0;continue;}
            }

            if (kind == 0) {
                /* 活物：红菱形 + 亮芯 */
                for (int d = -3; d <= 3; d++) {
                    int span = 3 - (d < 0 ? -d : d);
                    for (int e = -span; e <= span; e++) {
                        int edge = (e == -span || e == span || d == -3 || d == 3);
                        PutPixel(px2 + e, py2 + d, edge ? 0x00FF3020u : 0x00601010u);
                    }
                }
                PutPixel(px2, py2, 0x00FFD0D0u);
            } else {
                /* 资源：实心 5x5 方块，按类型换色 */
                unsigned int core, edge;
                if (kind == 2)      { core = 0x00FFE070u; edge = 0x00806020u; }   /* 保险丝：金 */
                else if (kind == 3) { core = flash ? 0x00FF4030u : 0x00701010u;   /* 宝箱怪：红闪 */
                                      edge = 0x00FFB0A0u; }
                else                { core = 0x00D8D8D8u; edge = 0x00505050u; }   /* 普通：白 */
                for (int d = -2; d <= 2; d++)
                    for (int e = -2; e <= 2; e++) {
                        int isEdge = (d == -2 || d == 2 || e == -2 || e == 2);
                        PutPixel(px2 + e, py2 + d, isEdge ? edge : core);
                    }
            }
        }
    }

    int pmX = mapStartX + (int)(posX * cellSize), pmY = mapStartY + (int)(posY * cellSize);
    int r = cellSize / 3 + 1;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) PutPixel(pmX + dx, pmY + dy, 0x0000FF00);

    for (float t = 0; t < 1.5f; t += 0.1f) {
        int lx = mapStartX + (int)((posX + dirX * t) * cellSize);
        int ly = mapStartY + (int)((posY + dirY * t) * cellSize);
        PutPixel(lx, ly, 0x00FFFFFF);
    }

    /* 标题：当前是哪种形态 + 还能按什么 + **代价**
       v2.4 起看图要花电，这个数字必须一直在眼前，否则玩家不会知道自己
       正在为什么买单。 */
    {
        /* 2026-11-05：**数字必须跟着强化走**。
           原来写死 "-10/S"，玩家升了「小地图省电」还是显示 -10，等于骗人。
           另外全屏地图现在**不耗电**（游戏也暂停），所以标 FREE 而不是数字。 */

        static char labelBuf[80];
        if (mapFullScreen)
            snprintf(labelBuf, sizeof(labelBuf), "MAP  FREE   M: CLOSE   HOLD TAB");
        else
            snprintf(labelBuf, sizeof(labelBuf), "MAP  -%d/S   HOLD TAB   M: FULL",
                     (int)(RunUpgMapDrainPct() + 0.5f));
        const char* label = labelBuf;
        int tw = TextWidthRaw(label);
        int tx = mapFullScreen ? (renderW - tw) / 2 : mapStartX;
        /* 小地图在右下角，标题从左往右排会顶出窗口 —— 夹回来 */
        if (tx + tw > renderW - 2) tx = renderW - 2 - tw;
        if (tx < 2) tx = 2;
        int ty = mapFullScreen ? (mapStartY - cellSize * 2 - FONT_H) : (mapStartY - FONT_H - 2);
        if (ty < 0) ty = 2;
        /* 底下垫一条深色条，浅色文字才看得清 */
        for (int y = ty - 2; y < ty + FONT_H + 2; y++) {
            if (y < 0 || y >= renderH) continue;
            for (int x = tx - 3; x < tx + tw + 3; x++) {
                if (x < 0 || x >= renderW) continue;
                PutPixelBlend(x, y, 0x00000000, 0.7f);
            }
        }
        PutTextRaw(tx, ty, label, 0x00B0C0D0u);
    }
    if (mapFullScreen) DrawMapLegend();   /* v3.0：全屏才画图例 */

}

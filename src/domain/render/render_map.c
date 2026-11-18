#include "protocol.h"
#include "room_theme.h"
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
#include "hatch.h"
#include "upgrade_run.h"
#include "player_api.h"
#include "world_api.h"
#include "lang.h"
#include "state.h"
#include "plat.h"
#include "present_api.h"
#include "echo_marks.h"
#include "map_markers.h"

/* 原来定义在 render.c 里的小地图格子尺寸；随本簇一起搬过来。 */
#define MAP_CORNER_CELL 8

static int LegendWidth(void) {
    int width=winW*30/100;
    if(width>UIS(170))width=UIS(170);
    if(width<96)width=96;
    return width;
}
static void DrawMapLegend(void) {
    static const struct {int kind;const char* en;const char* zh;} items[]={
        {-1,"YOU","你"}, {MAP_ICON_EXIT,"EXIT","出口"},
        {MAP_ICON_COIL,"DISPATCH COIL","调度线圈"}, {MAP_ICON_RECEIVER,"WITNESS RECEIVER","证词接收器"},
        {MAP_ICON_SHUTTER,"PRESSURE SHUTTER","泄压闸"}, {MAP_ICON_TERMINAL,"TERMINAL","剧情终端"}, {MAP_ICON_RECORD,"EVIDENCE","关键证据"},
        {MAP_ICON_CRATE,"CHEST","宝箱"}, {MAP_ICON_FUSE,"FUSE CHEST","保险丝箱"},
        {MAP_ICON_BREAKER,"BREAKER","配电箱"}, {MAP_ICON_SAFE,"SAFE ROOM","安全屋"},
        {MAP_ICON_HIDE,"HIDE SPOT","隐藏点"}, {MAP_ICON_BROKEN,"BROKEN HIDE","损坏隐藏点"},
        {MAP_ICON_DEVICE,"ROOM DEVICE","主题装置"}, {MAP_ICON_SPENT,"DEVICE USED","装置已使用"},
        {MAP_ICON_THREAT,"ECHO THREAT","回声威胁"}, {MAP_ICON_MIMIC,"MIMIC","宝箱怪"}
    };
    int indices[sizeof(items)/sizeof(items[0])],count=0;
    for (unsigned i=0;i<sizeof(items)/sizeof(items[0]);i++) {
        int kind=items[i].kind;
        if (kind>=MAP_ICON_COIL && kind<MAP_ICON_TERMINAL &&
            (!(protocolRun.flags&PROTOCOL_MISSIONS) || kind!=MAP_ICON_COIL+protocolRun.story-1)) continue;
        if (kind==MAP_ICON_TERMINAL && !protocolRun.story) continue;
        if (kind==MAP_ICON_RECORD && !ProtocolPeacefulStory()) continue;
        indices[count++]=(int)i;
    }
    int row=UIS(18);if(row<FONT_H+5)row=FONT_H+5;
    int width=LegendWidth(),height=row*(count+1)+UIS(12);
    int visible=count;
    while(height>winH-12 && row>FONT_H+1){row--;height=row*(count+1)+UIS(12);}
    while(height>winH-12 && visible>1){visible--;height=row*(visible+1)+UIS(12);}
    int x=winW-width-UIS(8),y=(winH-height)/2;
    if(y<UIS(6))y=UIS(6);
    BlendRectWin(x,y,x+width,y+height,0x0A1018,.95f);
    FrameRectWin(x,y,x+width,y+height,0x526A80,.8f,1);
    for(int rowIndex=0;rowIndex<visible;rowIndex++) {
        int i=indices[rowIndex];
        int ix=x+6,iy=y+6+rowIndex*row;
        unsigned color=items[i].kind<0?0x30FF60:MapIconColor(items[i].kind);
        for(int gy=0;gy<5;gy++)for(int gx=0;gx<5;gx++)
            if(items[i].kind<0?(gx==2 || gy==2):MapIconPixel(items[i].kind,gx,gy))
                BlendRectWin(ix+gx*2,iy+gy*2,ix+gx*2+2,iy+gy*2+2,color,1);
        char label[80];snprintf(label,sizeof(label),"%s",g_lang==LANG_ZH?items[i].zh:items[i].en);
        UiTrimSmallText(label,width-25);
        DrawTextSmallLiteralWin(x+22,iy,label,0xC5D7E8);
    }
    char note[96];snprintf(note,sizeof(note),"%s",Tr("WHITE DOT: SHARED CELL","白点：同格多项"));
    UiTrimSmallText(note,width-12);
    DrawTextSmallLiteralWin(x+6,y+6+visible*row,note,0xA9B6C5);
}

void RenderMap(void) {
    int cellSize, mapW, mapH, mapStartX, mapStartY;
    if (mapFullScreen) {
        int legendWidth=LegendWidth()*renderW/(winW>0?winW:1);
        int available=renderW-legendWidth-16;
        cellSize = (available < renderH ? available : renderH) / (MAP_WIDTH + 2);
        if (cellSize < 3) cellSize = 3;
        mapW = MAP_WIDTH * cellSize;
        mapH = MAP_HEIGHT * cellSize;
        mapStartX = (renderW - legendWidth - mapW) / 2;
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

    int gridReveal=RoomThemeMapRevealed();
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            unsigned int color = 0;
            float alpha = 0.8f;
            if (!gridReveal && explored[y][x] == 0) { color = 0x00050505; alpha = 0.95f; }
            else {
                if (worldMap[y][x] == 1) color = gridReveal ? 0x00567380 : 0x00888888;
                else if (gridReveal) { color = 0x00214657; alpha = .8f; }
                else { color = 0x00000000; alpha = 0.3f; }
            }
            int bx = mapStartX + x * cellSize, by = mapStartY + y * cellSize;
            for (int dy = 0; dy < cellSize - 1; dy++)
                for (int dx = 0; dx < cellSize - 1; dx++)
                    PutPixelBlend(bx + dx, by + dy, color, alpha);
        }
    }

    DrawMapMarkers(mapStartX,mapStartY,cellSize);
    /* Player is last, but remains inside its own cell instead of masking neighbours. */
    int pmX=mapStartX+(int)(posX*cellSize);
    int pmY=mapStartY+(int)(posY*cellSize);
    int radius=(cellSize-1)/2;if(radius>2)radius=2;if(radius<1)radius=1;
    for(int dy=-radius;dy<=radius;dy++)for(int dx=-radius;dx<=radius;dx++)
        if(dx*dx+dy*dy<=radius*radius)PutPixel(pmX+dx,pmY+dy,0x30FF60);
    for(float t=0;t<.5f;t+=.1f)PutPixel(pmX+(int)(dirX*t*cellSize),pmY+(int)(dirY*t*cellSize),0xFFFFFF);
    UiTrack(mapStartX*winW/renderW,mapStartY*winH/renderH,
            mapW*winW/renderW,mapH*winH/renderH,"map panel");

    /* 标题：当前是哪种形态 + 还能按什么 + **代价**
       v2.4 起看图要花电，这个数字必须一直在眼前，否则玩家不会知道自己
       正在为什么买单。 */
    {
        /* 2026-11-05：**数字必须跟着强化走**。
           原来写死 "-10/S"，玩家升了「小地图省电」还是显示 -10，等于骗人。
           另外全屏地图现在**不耗电**（游戏也暂停），所以标 FREE 而不是数字。 */

        static char labelBuf[80];
        if (mapFullScreen)
            snprintf(labelBuf, sizeof(labelBuf), "MAP FREE  M: CLOSE");
        else if (RoomThemeFreeMapRemaining()>0)
            snprintf(labelBuf,sizeof(labelBuf),"MAP FREE %.1fs / GRID LINK",(double)RoomThemeFreeMapRemaining());
        else
            snprintf(labelBuf, sizeof(labelBuf), "MAP  -%d/S   HOLD TAB   M: FULL",
                     (int)(RunUpgMapDrainPct() + 0.5f));
        const char* label = labelBuf;
        int tw = TextWidthRaw(label);
        int tx = mapFullScreen ? mapStartX+(mapW-tw)/2 : mapStartX;
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

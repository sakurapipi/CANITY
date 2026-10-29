/*
 * Canity — UI 层实现：点阵字库 / 文字 / 菜单与弹窗 / HUD / 最终呈现
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "ui.h"
#include "game.h"
#include "entity.h"
#include "render.h"

/* 前置声明 */
static void DrawAchievementsWindow(MenuGeo g);
static void DrawAchievementTooltip(int mx, int my, int hoveredIndex, MenuGeo g);

/* 成就页固定显示 10 行；面板/行高/列表起点都走 UIS，按窗口高度等比缩放 */
#define ACH_VISIBLE_ROWS 10
#define ACH_PANEL_W      UIS(600)
#define ACH_PANEL_H      UIS(400)


static const char* modNames[MOD_COUNT] = {"DARK", "HUNTED", "CRAMPED", "LEAKY", "RICH", "CALM"};
static const char* modDesc[MOD_COUNT] = {
    "FLASHLIGHT STARTS LOW",
    "THE THING MOVES FASTER",
    "FEWER CONTAINERS",
    "SANITY DRAINS FASTER",
    "MORE CONTAINERS",
    "SANITY DRAINS SLOWER"
};

/* 大厅：碎片货币 + 六项永久升级 */
static const char* themeNames[4] = {"BRICK", "PRISON", "HELL", "WARD"};
/* ------------------------------------------------------------------ */
/* UI：字体缓存 / 半透明面板 / 文字                                     */
/* ------------------------------------------------------------------ */

/* 8x12 点阵字库，ASCII 32..126（由 GDI 位图字体 1:1 采样生成）
   FONT_W / FONT_H 定义在文件前部（RenderMap 需要提前用）。 */
static const unsigned char fontData[95][FONT_H] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x20,0x20,0x20,0x20,0x20,0x20,0x00,0x20,0x00,0x00}, /* ! */
    {0x00,0x00,0x28,0x28,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x00,0x00,0x00,0x28,0x28,0xFC,0x28,0xFC,0x50,0x50,0x00,0x00}, /* # */
    {0x00,0x00,0x10,0x38,0x50,0x50,0x38,0x14,0x24,0x78,0x20,0x00}, /* $ */
    {0x00,0x00,0xE4,0xA8,0xF0,0x10,0x20,0x5C,0x54,0x9C,0x00,0x00}, /* % */
    {0x00,0x00,0x70,0x50,0x50,0x60,0xE8,0x98,0x88,0x7C,0x00,0x00}, /* & */
    {0x00,0x00,0x20,0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x00,0x00,0x08,0x10,0x10,0x20,0x20,0x20,0x20,0x10,0x10,0x08}, /* ( */
    {0x00,0x00,0x40,0x20,0x20,0x10,0x10,0x10,0x10,0x20,0x20,0x40}, /* ) */
    {0x00,0x00,0x10,0x54,0x38,0x54,0x10,0x00,0x00,0x00,0x00,0x00}, /* * */
    {0x00,0x00,0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x30,0x60}, /* , */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x70,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x00,0x00}, /* . */
    {0x00,0x00,0x04,0x08,0x08,0x10,0x10,0x10,0x20,0x20,0x40,0x00}, /* / */
    {0x00,0x00,0x00,0x70,0x88,0x98,0xA8,0xC8,0x88,0x70,0x00,0x00}, /* 0 */
    {0x00,0x00,0x00,0x30,0x50,0x10,0x10,0x10,0x10,0x7C,0x00,0x00}, /* 1 */
    {0x00,0x00,0x00,0x38,0x44,0x04,0x08,0x10,0x20,0x7C,0x00,0x00}, /* 2 */
    {0x00,0x00,0x00,0x70,0x08,0x08,0x30,0x08,0x08,0x70,0x00,0x00}, /* 3 */
    {0x00,0x00,0x00,0x18,0x28,0x48,0x88,0xFC,0x08,0x08,0x00,0x00}, /* 4 */
    {0x00,0x00,0x00,0x78,0x40,0x40,0x70,0x08,0x08,0x70,0x00,0x00}, /* 5 */
    {0x00,0x00,0x00,0x38,0x60,0x40,0x78,0x44,0x44,0x38,0x00,0x00}, /* 6 */
    {0x00,0x00,0x00,0x7C,0x04,0x08,0x08,0x10,0x10,0x20,0x00,0x00}, /* 7 */
    {0x00,0x00,0x00,0x38,0x44,0x64,0x38,0x4C,0x44,0x38,0x00,0x00}, /* 8 */
    {0x00,0x00,0x00,0x38,0x44,0x44,0x3C,0x04,0x08,0x70,0x00,0x00}, /* 9 */
    {0x00,0x00,0x00,0x00,0x30,0x30,0x00,0x00,0x30,0x30,0x00,0x00}, /* : */
    {0x00,0x00,0x00,0x00,0x30,0x30,0x00,0x00,0x30,0x30,0x30,0x60}, /* ; */
    {0x00,0x00,0x00,0x00,0x08,0x10,0x20,0x20,0x10,0x08,0x00,0x00}, /* < */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x7C,0x00,0x7C,0x00,0x00,0x00}, /* = */
    {0x00,0x00,0x00,0x00,0x40,0x20,0x10,0x10,0x20,0x40,0x00,0x00}, /* > */
    {0x00,0x00,0x30,0x18,0x08,0x08,0x30,0x20,0x00,0x20,0x00,0x00}, /* ? */
    {0x00,0x00,0x38,0x4C,0x44,0x94,0xB4,0xB4,0xB8,0x80,0x40,0x78}, /* @ */
    {0x00,0x00,0x00,0x10,0x28,0x28,0x28,0x7C,0x44,0x44,0x00,0x00}, /* A */
    {0x00,0x00,0x00,0x78,0x44,0x44,0x78,0x44,0x44,0x78,0x00,0x00}, /* B */
    {0x00,0x00,0x00,0x38,0x60,0x40,0x40,0x40,0x60,0x38,0x00,0x00}, /* C */
    {0x00,0x00,0x00,0x78,0x4C,0x44,0x44,0x44,0x4C,0x78,0x00,0x00}, /* D */
    {0x00,0x00,0x00,0x78,0x40,0x40,0x78,0x40,0x40,0x78,0x00,0x00}, /* E */
    {0x00,0x00,0x00,0x78,0x40,0x40,0x78,0x40,0x40,0x40,0x00,0x00}, /* F */
    {0x00,0x00,0x00,0x78,0x40,0x80,0x98,0x88,0xC8,0x78,0x00,0x00}, /* G */
    {0x00,0x00,0x00,0x48,0x48,0x48,0x78,0x48,0x48,0x48,0x00,0x00}, /* H */
    {0x00,0x00,0x00,0x7C,0x10,0x10,0x10,0x10,0x10,0x7C,0x00,0x00}, /* I */
    {0x00,0x00,0x00,0x78,0x08,0x08,0x08,0x08,0x08,0x70,0x00,0x00}, /* J */
    {0x00,0x00,0x00,0x48,0x50,0x50,0x60,0x50,0x50,0x48,0x00,0x00}, /* K */
    {0x00,0x00,0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x78,0x00,0x00}, /* L */
    {0x00,0x00,0x00,0x48,0x48,0xB4,0xB4,0x84,0x84,0x84,0x00,0x00}, /* M */
    {0x00,0x00,0x00,0x48,0x68,0x68,0x68,0x58,0x58,0x48,0x00,0x00}, /* N */
    {0x00,0x00,0x00,0x78,0xCC,0x84,0x84,0x84,0xCC,0x78,0x00,0x00}, /* O */
    {0x00,0x00,0x00,0x78,0x44,0x44,0x78,0x40,0x40,0x40,0x00,0x00}, /* P */
    {0x00,0x00,0x00,0x70,0xC8,0x88,0x88,0x88,0x98,0x70,0x20,0x18}, /* Q */
    {0x00,0x00,0x00,0x70,0x48,0x48,0x70,0x50,0x48,0x44,0x00,0x00}, /* R */
    {0x00,0x00,0x00,0x3C,0x40,0x40,0x38,0x04,0x04,0x78,0x00,0x00}, /* S */
    {0x00,0x00,0x00,0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x00}, /* T */
    {0x00,0x00,0x00,0x44,0x44,0x44,0x44,0x44,0x44,0x38,0x00,0x00}, /* U */
    {0x00,0x00,0x00,0x44,0x44,0x44,0x28,0x28,0x28,0x10,0x00,0x00}, /* V */
    {0x00,0x00,0x00,0x84,0x84,0x84,0xB4,0x78,0x58,0x48,0x00,0x00}, /* W */
    {0x00,0x00,0x00,0x84,0x48,0x30,0x30,0x30,0x48,0xCC,0x00,0x00}, /* X */
    {0x00,0x00,0x00,0x82,0x44,0x28,0x10,0x10,0x10,0x10,0x00,0x00}, /* Y */
    {0x00,0x00,0x00,0x7C,0x08,0x08,0x10,0x20,0x20,0x78,0x00,0x00}, /* Z */
    {0x00,0x00,0x38,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x38}, /* [ */
    {0x00,0x00,0x40,0x20,0x20,0x10,0x10,0x10,0x08,0x08,0x04,0x00}, /* \ */
    {0x00,0x00,0x70,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x70}, /* ] */
    {0x00,0x00,0x00,0x20,0x50,0x50,0x00,0x00,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFC}, /* _ */
    {0x00,0x60,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x00,0x00,0x38,0x04,0x3C,0x44,0x4C,0x3C,0x00,0x00}, /* a */
    {0x00,0x00,0x40,0x40,0x78,0x64,0x44,0x44,0x44,0x78,0x00,0x00}, /* b */
    {0x00,0x00,0x00,0x00,0x38,0x60,0x40,0x40,0x40,0x38,0x00,0x00}, /* c */
    {0x00,0x00,0x04,0x04,0x3C,0x44,0x44,0x44,0x4C,0x34,0x00,0x00}, /* d */
    {0x00,0x00,0x00,0x00,0x38,0x44,0x7C,0x40,0x60,0x3C,0x00,0x00}, /* e */
    {0x00,0x00,0x1C,0x20,0x20,0xF8,0x20,0x20,0x20,0x20,0x00,0x00}, /* f */
    {0x00,0x00,0x00,0x00,0x3E,0x44,0x44,0x78,0x40,0x7C,0x44,0x78}, /* g */
    {0x00,0x00,0x40,0x40,0x78,0x48,0x48,0x48,0x48,0x48,0x00,0x00}, /* h */
    {0x00,0x00,0x18,0x00,0x70,0x10,0x10,0x10,0x10,0x78,0x00,0x00}, /* i */
    {0x00,0x00,0x18,0x00,0x78,0x08,0x08,0x08,0x08,0x08,0x08,0x70}, /* j */
    {0x00,0x00,0x40,0x40,0x48,0x50,0x60,0x60,0x50,0x48,0x00,0x00}, /* k */
    {0x00,0x00,0x70,0x10,0x10,0x10,0x10,0x10,0x10,0x78,0x00,0x00}, /* l */
    {0x00,0x00,0x00,0x00,0x7C,0x54,0x54,0x54,0x54,0x54,0x00,0x00}, /* m */
    {0x00,0x00,0x00,0x00,0x78,0x48,0x48,0x48,0x48,0x48,0x00,0x00}, /* n */
    {0x00,0x00,0x00,0x00,0x38,0x44,0x44,0x44,0x44,0x38,0x00,0x00}, /* o */
    {0x00,0x00,0x00,0x00,0x78,0x64,0x44,0x44,0x44,0x78,0x40,0x40}, /* p */
    {0x00,0x00,0x00,0x00,0x3C,0x44,0x44,0x44,0x4C,0x34,0x04,0x04}, /* q */
    {0x00,0x00,0x00,0x00,0x78,0x48,0x40,0x40,0x40,0x40,0x00,0x00}, /* r */
    {0x00,0x00,0x00,0x00,0x38,0x40,0x60,0x18,0x08,0x70,0x00,0x00}, /* s */
    {0x00,0x00,0x20,0x20,0xF8,0x20,0x20,0x20,0x20,0x38,0x00,0x00}, /* t */
    {0x00,0x00,0x00,0x00,0x48,0x48,0x48,0x48,0x48,0x78,0x00,0x00}, /* u */
    {0x00,0x00,0x00,0x00,0x44,0x44,0x44,0x28,0x28,0x10,0x00,0x00}, /* v */
    {0x00,0x00,0x00,0x00,0x84,0xA8,0xA8,0xB8,0xD8,0x48,0x00,0x00}, /* w */
    {0x00,0x00,0x00,0x00,0xCC,0x48,0x30,0x30,0x48,0xCC,0x00,0x00}, /* x */
    {0x00,0x00,0x00,0x00,0x44,0x44,0x48,0x28,0x30,0x10,0x20,0xE0}, /* y */
    {0x00,0x00,0x00,0x00,0x78,0x10,0x10,0x20,0x20,0x78,0x00,0x00}, /* z */
    {0x00,0x00,0x30,0x20,0x20,0x20,0xC0,0x20,0x20,0x20,0x20,0x30}, /* { */
    {0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10}, /* | */
    {0x00,0x00,0x30,0x10,0x10,0x10,0x0C,0x10,0x10,0x10,0x10,0x30}, /* } */
    {0x00,0x00,0x00,0x00,0x00,0x00,0xE8,0xB8,0x00,0x00,0x00,0x00}, /* ~ */
};

/* 文字直接写进像素缓冲：和面板同一层，不存在“文字层丢失” */
static int TextScale(void) {
    int s = (int)(renderH * 2.0 / 600.0 + 0.5);
    if (s < 1) s = 1;
    if (s > 8) s = 8;
    return s;
}

static int TextAdvWin(void) {
    return FONT_W * TextScale() * winW / renderW;
}

static int TextWidthWin(const char* t) {
    return (int)strlen(t) * TextAdvWin();
}

static int TextHeightWin(void) {
    return FONT_H * TextScale() * winH / renderH;
}

/* xWin/yWin 用窗口坐标，内部换算到渲染坐标 */
static void DrawTextWin(int xWin, int yWin, const char* t, unsigned int color) {
    int s = TextScale();
    int px = xWin * renderW / winW;
    int py = yWin * renderH / winH;
    for (const char* p = t; *p; p++) {
        int ch = (unsigned char)*p;
        if (ch < 32 || ch > 126) ch = '?';
        const unsigned char* g = fontData[ch - 32];
        for (int r = 0; r < FONT_H; r++) {
            unsigned char bits = g[r];
            if (!bits) continue;
            for (int c = 0; c < FONT_W; c++) {
                if (!(bits & (1u << (FONT_W - 1 - c)))) continue;
                int bx = px + c * s, by = py + r * s;
                for (int dy = 0; dy < s; dy++) {
                    int y = by + dy;
                    if (y < 0 || y >= renderH) continue;
                    unsigned int* row = pixels + (size_t)y * renderW;
                    for (int dx = 0; dx < s; dx++) {
                        int x = bx + dx;
                        if (x >= 0 && x < renderW) row[x] = color;
                    }
                }
            }
        }
        px += FONT_W * s;
    }
}

static void DrawTextCenterWin(int cxWin, int yWin, const char* t, unsigned int color) {
    DrawTextWin(cxWin - TextWidthWin(t) / 2, yWin, t, color);
}

/* 在**渲染坐标**上画 1:1 的点阵字（小地图标题用）。
   与 DrawTextWin 的区别：那个用窗口坐标（会缩放），这个直接落在渲染缓冲上。 */
void PutTextRaw(int x, int y, const char* t, unsigned int color) {
    for (const char* p = t; *p; p++) {
        int ch = (unsigned char)*p;
        if (ch < 32 || ch > 126) ch = '?';
        const unsigned char* g = fontData[ch - 32];
        for (int r = 0; r < FONT_H; r++) {
            unsigned char bits = g[r];
            if (!bits) continue;
            for (int c = 0; c < FONT_W; c++) {
                if (!(bits & (1u << (FONT_W - 1 - c)))) continue;
                PutPixel(x + c, y + r, color);
            }
        }
        x += FONT_W;
    }
}

/* 渲染坐标下的文字宽度（1:1 点阵） */
int TextWidthRaw(const char* t) {
    return (int)strlen(t) * FONT_W;
}

/* 标题加粗：右移一格再画一遍 */
static void DrawTitleWin(const char* t, int yWin, unsigned int color) {
    DrawTextCenterWin(winW / 2, yWin - 2 * TextScale(), t, color);
    DrawTextCenterWin(winW / 2 + 1, yWin - 2 * TextScale(), t, color);
}

/* UI 尺寸随窗口高度缩放（以 600 为基准） */
int UIS(int v) {
    int s = (int)(v * (winH / 600.0) + 0.5);
    return s < 10 ? 10 : s;
}
static MenuGeo MakeGeo(const char* title, int count, int rowH, int pad, int width, int titlePx, int stateIndex) {
    MenuGeo g;
    memset(&g, 0, sizeof(g));
    g.title = title;
    g.count = count;
    g.rowH = rowH;

    int titleH = titlePx;
    int innerH = titleH + UIS(24) + count * rowH;
    int panelH = pad * 2 + innerH;
    int panelW = width;
    if (panelW > winW - 60) panelW = winW - 60;
    if (panelW < 160) panelW = 160;

    g.panel.left = (winW - panelW) / 2;
    g.panel.right = g.panel.left + panelW;
    g.panel.top = (winH - panelH) / 2;
    g.panel.bottom = g.panel.top + panelH;
    g.titleY = g.panel.top + pad;
    g.firstY = g.titleY + titleH + UIS(24);

    /* 计算面板内容区高度，决定最多显示多少行 */
    int contentH = g.panel.bottom - g.firstY;
    g.maxVisibleRows = contentH / rowH;
    if (g.maxVisibleRows < 1) g.maxVisibleRows = 1;

    /* 使用持久化的滚动偏移 */
    g.scrollOffset = menuScrollOffsets[stateIndex];
    /* 修正偏移：不能超过最大值 */
    int maxOffset = g.count - g.maxVisibleRows;
    if (maxOffset < 0) maxOffset = 0;
    if (g.scrollOffset > maxOffset) g.scrollOffset = maxOffset;

    return g;
}

MenuGeo GetMenuGeo(void) {
    switch (currentState) {
        case STATE_MAIN_MENU: return MakeGeo("CANITY", MainMenuCount(), UIS(56), UIS(38), UIS(520), UIS(72), STATE_MAIN_MENU);
        case STATE_HUB:       return MakeGeo("THE HUB", HUB_ROWS, UIS(50), UIS(34), UIS(700), UIS(52), STATE_HUB);
        case STATE_HELP:      return MakeGeo("HELP", 13, UIS(29), UIS(22), UIS(1000), UIS(30), STATE_HELP);
        case STATE_PAUSED:    return MakeGeo("PAUSED", 3, UIS(56), UIS(38), UIS(520), UIS(56), STATE_PAUSED);
        case STATE_GAMEOVER:  return MakeGeo(gameWon ? "YOU ESCAPED" : "YOU LOST YOUR MIND", 3, UIS(48), UIS(34), UIS(660), UIS(40), STATE_GAMEOVER);
        case STATE_SETTINGS:  return MakeGeo(PageTitle(settingsPage), SettingsRowCount(), UIS(44), UIS(30), UIS(680), UIS(44), STATE_SETTINGS);
        case STATE_SKILLS: {
            /* 标题动态带上残卷数量：这是唯一花残卷的地方 */
            static char skillTitle[64];
            sprintf(skillTitle, "SKILLS    %d SCROLL", scrolls);
            return MakeGeo(skillTitle, SKILL_COUNT, UIS(46), UIS(30), UIS(780), UIS(46), STATE_SKILLS);
        }
        case STATE_ACHIEVEMENTS: {
            /* 成就页使用自己的 600x400 面板（见 DrawAchievementsWindow）。
               不能用 MakeGeo：那个会把面板高度算成 count*rowH（17 行约 800px），
               导致 maxVisibleRows == count，滚动偏移永远被夹成 0，滚动条就成了死的。
               这里让几何和实际绘制的列表完全一致：10 行、行高 UIS(30)。 */
            MenuGeo g;
            memset(&g, 0, sizeof(g));
            g.title = "ACHIEVEMENTS";
            g.count = ACH_TOTAL + 1;          /* 24 条成就 + Back */
            g.rowH = UIS(30);

            int panelW = ACH_PANEL_W;
            int panelH = ACH_PANEL_H;
            if (panelW > winW - UIS(40)) panelW = winW - UIS(40);
            if (panelH > winH - UIS(40)) panelH = winH - UIS(40);
            if (panelW < 200) panelW = 200;
            if (panelH < 160) panelH = 160;

            g.panel.left = (winW - panelW) / 2;
            g.panel.right = g.panel.left + panelW;
            g.panel.top = (winH - panelH) / 2;
            g.panel.bottom = g.panel.top + panelH;
            g.titleY = g.panel.top + UIS(20);
            g.firstY = g.panel.top + UIS(60);

            g.maxVisibleRows = ACH_VISIBLE_ROWS;
            int contentH = g.panel.bottom - g.firstY - UIS(10);
            if (g.maxVisibleRows * g.rowH > contentH)
                g.maxVisibleRows = contentH / g.rowH;
            if (g.maxVisibleRows < 1) g.maxVisibleRows = 1;

            g.scrollOffset = menuScrollOffsets[STATE_ACHIEVEMENTS];
            int maxOffset = g.count - g.maxVisibleRows;
            if (maxOffset < 0) maxOffset = 0;
            if (g.scrollOffset < 0) g.scrollOffset = 0;
            if (g.scrollOffset > maxOffset) g.scrollOffset = maxOffset;
            return g;
        }
        default: break;
    }
    return MakeGeo("", 0, UIS(44), UIS(30), UIS(400), UIS(44), -1);
}

MenuGeo GetPopupGeo(void) {
    if (popupItem < 0 || popupItem >= SETTING_COUNT) return GetMenuGeo();
    const SettingItem* it = &settings[popupItem];
    if (it->kind == SET_SLIDER) {
        /* 滑块面板：标题 + 数值 + 轨道 */
        MenuGeo g;
        memset(&g, 0, sizeof(g));
        g.title = it->name;
        g.count = 0;
        int pad = UIS(26), titlePx = UIS(34);
        int contentH = UIS(140);
        int panelW = UIS(460);
        if (panelW > winW - 80) panelW = winW - 80;
        int panelH = pad * 2 + titlePx + UIS(24) + contentH;
        g.panel.left = (winW - panelW) / 2;
        g.panel.right = g.panel.left + panelW;
        g.panel.top = (winH - panelH) / 2;
        g.panel.bottom = g.panel.top + panelH;
        g.titleY = g.panel.top + pad;
        g.firstY = g.titleY + titlePx + UIS(24);
        g.rowH = contentH;
        return g;
    }
    return MakeGeo(it->name, it->count, UIS(42), UIS(26), UIS(460), UIS(34), -1);
}

/* 滑块轨道（窗口坐标） */
RECT SliderTrackRect(MenuGeo g) {
    RECT r;
    int pad = UIS(40);
    r.left = g.panel.left + pad;
    r.right = g.panel.right - pad;
    r.top = g.firstY + UIS(46);
    r.bottom = r.top + UIS(12);
    return r;
}

/* 滑块值 -> 窗口 x */
int SliderValueToX(MenuGeo g, float v, float lo, float hi) {
    RECT t = SliderTrackRect(g);
    if (hi <= lo) return t.left;
    float f = (v - lo) / (hi - lo);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return t.left + (int)(f * (t.right - t.left));
}

/* 窗口 x -> 滑块值 */
float SliderXToValue(MenuGeo g, int x, float lo, float hi, float step) {
    RECT t = SliderTrackRect(g);
    float f = (t.right == t.left) ? 0.0f : (float)(x - t.left) / (float)(t.right - t.left);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    float v = lo + f * (hi - lo);
    v = lo + floorf((v - lo) / step + 0.5f) * step;   /* 吸附到步长 */
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

RECT RowRect(MenuGeo g, int i) {
    RECT r;
    r.left = g.panel.left + UIS(18);
    r.right = g.panel.right - UIS(18);
    /* 应用滚动偏移：只绘制可见范围内的行 */
    int visibleIndex = i - g.scrollOffset;
    r.top = g.firstY + visibleIndex * g.rowH;
    r.bottom = r.top + g.rowH - UIS(8);
    return r;
}

int MainMenuCount(void) {
    int endlessUnlocked = (bestLevel >= 10) || gameWon;
    if (endlessUnlocked) {
        return hasSave ? 8 : 7;
    }
    return hasSave ? 7 : 6;
}

static int MenuItemIndex(void) {
    if (currentState == STATE_SETTINGS) return settingsSelection;
    if (currentState == STATE_HUB) return hubSelection;
    if (currentState == STATE_ACHIEVEMENTS) return achSelection;
    return menuSelection;
}

static const char* MenuLabel(int i) {
    static char buf[128];
    switch (currentState) {
        case STATE_MAIN_MENU: {
            static const char* withSave[] = {"Continue", "New Run", "Hub", "Achievements", "Help", "Settings", "Quit"};
            static const char* noSave[] = {"New Run", "Hub", "Achievements", "Help", "Settings", "Quit"};
            static const char* withEndless[] = {"Continue", "New Run", "Endless", "Hub", "Achievements", "Help", "Settings", "Quit"};
            static const char* noSaveEndless[] = {"New Run", "Endless", "Hub", "Achievements", "Help", "Settings", "Quit"};
            /* Endless 解锁条件：通关过第 10 层 */
            int endlessUnlocked = (bestLevel >= 10) || gameWon;
            if (endlessUnlocked) {
                return hasSave ? withEndless[i] : noSaveEndless[i];
            }
            return hasSave ? withSave[i] : noSave[i];
        }
        case STATE_PAUSED: {
            static const char* s[] = {"Resume", "Settings", "Main Menu"};
            return s[i];
        }
        case STATE_HELP: {
            /* 只放玩法提示，不再列键位 —— 键位可在 Settings 里改绑，
               静态表一改绑就过时；真正的键位看 Settings → Movement / Actions / Menu Keys。
               行数要控制在 14 行以内：面板高度 = 14×28 + 标题 + 内边距
               ≈ 500，在 600 高的窗口里才留得下边距。 */
            static const char* helps[] = {
                "KEYS: SEE SETTINGS > MOVEMENT /",
                "      ACTIONS / MENU KEYS.",
                "",
                "RUNNING AND SEARCHING MAKE NOISE.",
                "WALKING IS SILENT.",
                "",
                "BOTTLE: THROW IT AT A MONSTER TO HIT IT.",
                "  3 BOTTLES DRIVE IT AWAY, BUT THE SOUND",
                "  GIVES AWAY YOUR POSITION.",
                "",
                "FACE JUMPER: PRESS SPACE TO DODGE IT.",
                "  IF IT LANDS ON YOUR FACE, PRESS THE KEY.",
                "  MISS BOTH AND YOU LOSE SANITY."
            };
            const int helpRows = (int)(sizeof(helps) / sizeof(helps[0]));
            if (i >= 0 && i < helpRows) return helps[i];
            return "";
        }
        case STATE_HUB: {
            static char hb[96];
            if (i > UPGRADE_COUNT) return "Back";
            if (i == UPGRADE_COUNT) {
                sprintf(hb, "Skills: %s LV%d   %d SCROLL   (open)",
                        SkillName(selectedSkill), SkillLevel(selectedSkill), scrolls);
                return hb;
            }
            int cost = UpgradeCost(i);
            if (cost < 0) sprintf(hb, "%s   LV%d/%d   MAX", UpgradeName(i), UpgradeLevel(i), UPG_MAX);
            else sprintf(hb, "%s   LV%d/%d   %d SHARDS", UpgradeName(i), UpgradeLevel(i), UPG_MAX, cost);
            return hb;
        }
        case STATE_GAMEOVER: return "Return to Menu";
        case STATE_SKILLS: {
            static char sb[128];
            if (i >= SKILL_COUNT - 1) return "Back";
            int sk = i + 1;
            const char* mark = (sk == selectedSkill) ? "[*]" : "[ ]";
            int lv = SkillLevel(sk);
            int cost = SkillUpgradeCost(sk);
            if (cost < 0)
                sprintf(sb, "%s %s   LV%d/%d   CD %.0fs   MAX",
                        mark, SkillName(sk), lv, SKILL_UPG_MAX, SkillCooldown(sk));
            else
                sprintf(sb, "%s %s   LV%d/%d   CD %.0fs   %d SCROLL",
                        mark, SkillName(sk), lv, SKILL_UPG_MAX, SkillCooldown(sk), cost);
            return sb;
        }
        case STATE_ACHIEVEMENTS: {
            if (i >= ACH_TOTAL) return "Back";
            static char ab[256];
            if (g_achievements[i].unlocked) {
                sprintf(ab, "[*] %s  (%d/%d)", g_achievements[i].name,
                        g_achievements[i].progress, g_achievements[i].target);
            } else {
                /* 简短显示：只显示名字和进度，描述放在右侧列（通过 DrawMenuText 处理） */
                sprintf(ab, "[ ] %s  %d/%d", g_achievements[i].name,
                        g_achievements[i].progress, g_achievements[i].target);
            }
            return ab;
        }
        case STATE_SETTINGS: {
            if (settingsPage == SETPAGE_ROOT) {
                if (i < 3) return rootCategoryNames[i];
                return "Back";
            }
            if (settingsPage == SETPAGE_BINDDIR) {
                if (i < 3) return bindCategoryNames[i];
                return "Back";
            }
            int idx[SETTING_COUNT];
            int n = PageItems(idx);
            if (i >= n) return "Back";
            const SettingItem* it = &settings[idx[i]];
            if (IsKeyBindItem(it)) {
                return it->name;  /* 键位行：名称左对齐，键名右对齐（DrawMenuText 里画） */
            }
            if (it->kind == SET_SLIDER) {
                if (it->fvalue == &gammaValue) sprintf(buf, "%s: %.2f", it->name, *it->fvalue);
                else if (it->fvalue == &volumeF) sprintf(buf, "%s: %.0f%%", it->name, *it->fvalue * 100.0f);
                else sprintf(buf, "%s: %.1fx", it->name, *it->fvalue);
            } else {
                int dispVal = *it->value;
                if (dispVal >= 0 && dispVal < it->count) {
                    sprintf(buf, "%s: %s", it->name, it->options[dispVal]);
                } else {
                    sprintf(buf, "%s: %s", it->name, it->options[0]);
                }
            }
            return buf;
        }
        default: return "";
    }
}

/* 窗口坐标 -> 渲染坐标的矩形填充（半透明） */
static void BlendRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha) {
    int rx0 = x0 * renderW / winW, rx1 = x1 * renderW / winW;
    int ry0 = y0 * renderH / winH, ry1 = y1 * renderH / winH;
    if (rx0 < 0) rx0 = 0;
    if (ry0 < 0) ry0 = 0;
    if (rx1 > renderW) rx1 = renderW;
    if (ry1 > renderH) ry1 = renderH;

    int r = (int)((color >> 16) & 0xFF), g = (int)((color >> 8) & 0xFF), b = (int)(color & 0xFF);
    int ia = (int)(alpha * 256.0f);
    int na = 256 - ia;

    for (int y = ry0; y < ry1; y++) {
        unsigned int* row = pixels + (size_t)y * renderW;
        for (int x = rx0; x < rx1; x++) {
            unsigned int d = row[x];
            int nr = (r * ia + ((int)((d >> 16) & 0xFF)) * na) >> 8;
            int ng = (g * ia + ((int)((d >> 8) & 0xFF)) * na) >> 8;
            int nb = (b * ia + ((int)(d & 0xFF)) * na) >> 8;
            row[x] = (unsigned int)((nr << 16) | (ng << 8) | nb);
        }
    }
}

static void FrameRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha, int thick) {
    BlendRectWin(x0, y0, x1, y0 + thick, color, alpha);
    BlendRectWin(x0, y1 - thick, x1, y1, color, alpha);
    BlendRectWin(x0, y0, x0 + thick, y1, color, alpha);
    BlendRectWin(x1 - thick, y0, x1, y1, color, alpha);
}

/* 主面板（不含弹窗）：弹窗必须画在列表文字之上，否则文字会压在弹窗上 */
void DrawMenuPanels(void) {
    MenuGeo g = GetMenuGeo();
    BlendRectWin(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, 0x000a1016, 0.94f);
    FrameRectWin(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, 0x00801818, 0.85f, UIS(2));

    /* 弹窗开着时不再高亮背后的列表行 */
    int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);
    int sel = MenuItemIndex();

    if (!popupOpen && sel >= g.scrollOffset && sel < g.scrollOffset + g.maxVisibleRows) {
        RECT r = RowRect(g, sel);
        BlendRectWin(r.left, r.top, r.right, r.bottom, 0x00205020, 0.55f);
    }
}

/* 弹窗面板：不透明，盖住下面的列表文字 */
void DrawPopupPanel(void) {
    MenuGeo p = GetPopupGeo();
    const SettingItem* it = &settings[popupItem];

    BlendRectWin(p.panel.left, p.panel.top, p.panel.right, p.panel.bottom, 0x0004080c, 1.0f);
    FrameRectWin(p.panel.left, p.panel.top, p.panel.right, p.panel.bottom, 0x00b02020, 0.95f, UIS(2));

    if (it->kind == SET_SLIDER) {
        RECT t = SliderTrackRect(p);
        int bend = UIS(3);
        BlendRectWin(t.left - bend, t.top - bend, t.right + bend, t.bottom + bend, 0x00404040, 0.9f);
        BlendRectWin(t.left, t.top, t.right, t.bottom, 0x00141414, 0.95f);
        int kx = SliderValueToX(p, *it->fvalue, it->fmin, it->fmax);
        BlendRectWin(t.left, t.top, kx, t.bottom, 0x0030a060, 0.95f);
        int k = UIS(8);
        BlendRectWin(kx - k, t.top - k, kx + k, t.bottom + k, 0x00d8e8d8, 0.95f);
        FrameRectWin(kx - k, t.top - k, kx + k, t.bottom + k, 0x00ffffff, 0.55f, 1);
    } else if (popupSelection >= 0 && popupSelection < p.count) {
        RECT r = RowRect(p, popupSelection);
        BlendRectWin(r.left, r.top, r.right, r.bottom, 0x00306030, 0.65f);
    }
}

static void DrawTextInRow(const char* text, RECT row, unsigned int color) {
    int tw = TextWidthWin(text), th = TextHeightWin();
    int x = row.left + ((row.right - row.left) - tw) / 2;
    int y = row.top + ((row.bottom - row.top) - th) / 2;
    DrawTextWin(x, y, text, color);
}

/* 左对齐的一行：说明文字用。
   居中对齐遇到长句子会超出面板边缘（CONTROLS 页的说明就踩过这个坑）。 */
static void DrawTextInRowLeft(const char* text, RECT row, unsigned int color) {
    int th = TextHeightWin();
    int y = row.top + ((row.bottom - row.top) - th) / 2;
    DrawTextWin(row.left + UIS(8), y, text, color);
}

static void DrawMenuText(void) {
    MenuGeo g = GetMenuGeo();
    int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);

    /* 成就页面：使用专用现代化渲染 */
    if (currentState == STATE_ACHIEVEMENTS) {
        DrawAchievementsWindow(g);
        return;
    }

    unsigned int titleColor = (currentState == STATE_MAIN_MENU) ? 0x00BE1414u : 0x00D23C3Cu;
    /* 通关结局的标题用绿色 —— 和「理智耗尽」的暗红区分开 */
    if (currentState == STATE_GAMEOVER && gameWon) titleColor = 0x0060E070u;
    if (currentState == STATE_MAIN_MENU) {
        char sb[48];
        sprintf(sb, "SHARDS %d", shards);
        DrawTextCenterWin(winW / 2, winH - UIS(56), sb, 0x0060D0FFu);
        char bb[48];
        sprintf(bb, "BEST LEVEL %d", bestLevel);
        DrawTextCenterWin(winW / 2, winH - UIS(30), bb, 0x00808080u);
        /* 版本号：右下角低调显示，方便对着存档确认是哪个版本 */
        DrawTextWin(winW - TextWidthWin(CANITY_VERSION_STR) - UIS(14),
                    winH - UIS(30), CANITY_VERSION_STR, 0x00585858u);
    }
    DrawTitleWin(g.title, g.titleY, titleColor);

    int sel = MenuItemIndex();
    int idx[SETTING_COUNT];
    int pageItemCount = 0;
    if (currentState == STATE_SETTINGS && settingsPage != SETPAGE_ROOT && settingsPage != SETPAGE_BINDDIR) {
        pageItemCount = PageItems(idx);
    }

    /* 确保选中的项在可见范围内 */
    if (sel >= 0 && sel < g.count) {
        if (sel < g.scrollOffset) {
            g.scrollOffset = sel;
        } else if (sel >= g.scrollOffset + g.maxVisibleRows) {
            g.scrollOffset = sel - g.maxVisibleRows + 1;
        }
    }

    /* 将调整后的滚动偏移保存回全局数组，持久化到下次调用 */
    if (currentState >= 0 && currentState < 8) {
        menuScrollOffsets[currentState] = g.scrollOffset;
    }

    int startRow = g.scrollOffset;
    int endRow = g.scrollOffset + g.maxVisibleRows;
    if (endRow > g.count) endRow = g.count;

    for (int i = startRow; i < endRow; i++) {
        const SettingItem* it = NULL;
        if (currentState == STATE_SETTINGS && settingsPage != SETPAGE_ROOT && settingsPage != SETPAGE_BINDDIR) {
            if (i < pageItemCount) it = &settings[idx[i]];
            /* i >= pageItemCount 是 Back 行，it 保持 NULL */
        }

        unsigned int c;
        if (popupOpen) c = 0x00383E3Au;                       /* 弹窗后面的列表压暗 */
        else if (currentState == STATE_HUB && i < UPGRADE_COUNT) {
            int cost = UpgradeCost(i);
            if (cost < 0) c = 0x00707070u;
            else if (shards >= cost) c = (i == sel) ? 0x008CFF8Cu : 0x00A0FFA0u;
            else c = (i == sel) ? 0x00FF8080u : 0x00B06060u;
        }
        else if (currentState == STATE_SKILLS && i < SKILL_COUNT - 1) {
            int sk = i + 1;
            if (sk == selectedSkill) c = (i == sel) ? 0x008CFF8Cu : 0x00A8E0B0u;
            else c = (i == sel) ? 0x008CFF8Cu : 0x009B9B9Bu;
        }
        else c = (i == sel) ? 0x008CFF8Cu : 0x009B9B9Bu;

        if (currentState == STATE_GAMEOVER && i > 0) {
            /* 复盘：第 2、3 行放统计（通关与失败共用这一页，标题区分） */
            char line[96];
            int mins = (int)(runTime / 60.0f);
            int secs = (int)runTime - mins * 60;
            if (i == 1) sprintf(line, "LEVEL %d    TIME %d:%02d", currentLevel, mins, secs);
            else sprintf(line, "CRATES %d    BATTERY %d%%", searchedCount, (int)(battery / (float)BatteryMax() * 100.0f));
            DrawTextInRow(line, RowRect(g, i), (i == sel) ? 0x008CFF8Cu : 0x009B9B9Bu);
            continue;
        }

        /* CONTROLS 页的说明文字用左对齐 ——
           居中对齐遇到长句子会超出面板左右边缘。 */
        if (currentState == STATE_HELP) {
            DrawTextInRowLeft(MenuLabel(i), RowRect(g, i), c);
            continue;
        }

        /* 键位绑定行：单行「名称 ····· 键名」，值右对齐。
           之前是两行式（标签一行 + 值一行），但行高 UIS(44) 扣掉边距只剩
           36px，装不下两行 24px 的文字，标签会压到相邻行 —— 显示不全。 */
        if (it && IsKeyBindItem(it)) {
            RECT row = RowRect(g, i);
            int th = TextHeightWin();
            int y = row.top + ((row.bottom - row.top) - th) / 2;
            DrawTextWin(row.left + UIS(8), y, it->name, c);
            int act = BindActionOfItem(it);
            const char* valStr;
            if (act >= 0 && act == rebindingAct) {
                valStr = "...";          /* 正在改绑这一项 */
            } else {
                int vk = *it->value;
                int dispVal = -1;
                for (int k = 0; k < OPT_KEY_COUNT; k++) {
                    if (optKeyVKs[k] == vk) { dispVal = k; break; }
                }
                if (dispVal >= 0) valStr = optKeys[dispVal];
                else valStr = (vk == 0) ? "None" : "?";
            }
            int tw = TextWidthWin(valStr);
            DrawTextWin(row.right - UIS(8) - tw, y, valStr, 0x0070B0D0u);
            continue;
        }

        DrawTextInRow(MenuLabel(i), RowRect(g, i), c);
    }

    /* 绘制滚动指示器 */
    if (g.count > g.maxVisibleRows) {
        int barH = g.panel.bottom - g.firstY;
        int thumbH = (int)((float)g.maxVisibleRows / g.count * barH);
        if (thumbH < 20) thumbH = 20;
        int thumbY = g.firstY + (int)((float)g.scrollOffset / (g.count - g.maxVisibleRows) * (barH - thumbH));
        int barX = g.panel.right - UIS(12);
        BlendRectWin(barX, g.firstY, barX + UIS(8), g.panel.bottom, 0x00202020, 0.5f);
        BlendRectWin(barX, thumbY, barX + UIS(8), thumbY + thumbH, 0x008CFF8C, 0.8f);

        /* 顶部/底部箭头提示 */
        if (g.scrollOffset > 0) {
            DrawTextCenterWin(g.panel.left + (g.panel.right - g.panel.left) / 2, g.firstY - UIS(20), "^", 0x008CFF8C);
        }
        if (g.scrollOffset + g.maxVisibleRows < g.count) {
            DrawTextCenterWin(g.panel.left + (g.panel.right - g.panel.left) / 2, g.panel.bottom + UIS(4), "v", 0x008CFF8C);
        }
    }
}

/* ============================================================
 * 成就页面专用渲染：精确布局
 * ============================================================ */
static void DrawAchievementsWindow(MenuGeo g) {
    int panelX = g.panel.left;
    int panelY = g.panel.top;
    int panelW = g.panel.right - g.panel.left;
    int panelH = g.panel.bottom - g.panel.top;

    unsigned int panelBg = 0x001E1E1Eu;      /* RGB(30,30,30) */
    unsigned int panelBorder = 0x00646464u;  /* RGB(100,100,100) */
    BlendRectWin(panelX, panelY, panelX + panelW, panelY + panelH, panelBg, 1.0f);
    FrameRectWin(panelX, panelY, panelX + panelW, panelY + panelH, panelBorder, 1.0f, 2);

    /* === 标题：panelY + UIS(20)，Gold RGB(200,150,50) === */
    unsigned int titleColor = 0x00C89632u;  /* RGB(200,150,50) */
    DrawTextCenterWin(panelX + panelW / 2, panelY + UIS(20), "ACHIEVEMENTS", titleColor);

    /* === 列表区域：起始 Y = panelY + UIS(60)，行高 UIS(30) ===
       全部尺寸走 UIS 等比缩放，避免高分辨率下文字撑破固定 600x400 面板而被截断。 */
    int listStartY = panelY + UIS(60);   /* 列表起始Y */
    int rowHeight = g.rowH;              /* 行高 UIS(30) */
    int maxVisibleRows = g.maxVisibleRows;
    int listX = panelX + UIS(10);
    int listW = panelW - UIS(40);        /* 右侧给滚动条留出空隙 */
    if (listW < UIS(80)) listW = UIS(80);
    int trackHeight = maxVisibleRows * rowHeight;
    int contentBottom = panelY + panelH - UIS(10);
    if (listStartY + trackHeight > contentBottom)
        trackHeight = contentBottom - listStartY;
    if (trackHeight < rowHeight) trackHeight = rowHeight;

    int sel = MenuItemIndex();  /* achSelection */

    /* 循环：i 是屏幕行索引，actualIndex 是数据索引 */
    for (int i = 0; i < maxVisibleRows; i++) {
        int actualIndex = g.scrollOffset + i;   /* 数据索引 */
        if (actualIndex >= g.count) break;

        int drawY = listStartY + (i * rowHeight);   /* 屏幕上的Y */
        if (drawY + rowHeight > contentBottom) break;

        /* 行背景高亮：严格限制在面板内 */
        if (actualIndex == sel) {
            unsigned int highlight = 0x00325032u;   /* RGB(50,80,50) */
            BlendRectWin(listX, drawY, listX + listW, drawY + rowHeight, highlight, 1.0f);
        }

        /* 成就文本（先判 ACH_TOTAL，Back 行不能再解引用 g_achievements） */
        char line[128];
        unsigned int textColor;
        int unlocked = 0;
        if (actualIndex >= ACH_TOTAL) {
            strcpy(line, "Back");
            textColor = 0x00808080u;
        } else {
            const Achievement* ach = &g_achievements[actualIndex];
            unlocked = ach->unlocked;
            textColor = unlocked ? 0x00E8E8E8u : 0x00A0A0A0u;
            if (unlocked) {
                sprintf(line, "[*] %s  (%d/%d)", ach->name, ach->progress, ach->target);
            } else {
                sprintf(line, "[ ] %s  %d/%d", ach->name, ach->progress, ach->target);
            }
        }

        int th = TextHeightWin();
        int ty = drawY + (rowHeight - th) / 2;
        DrawTextWin(listX + UIS(6), ty, line, textColor);

        /* 解锁标记：只有右侧确实放得下才画，别压到成就文字上 */
        if (unlocked) {
            const char* done = "DONE";
            int dw = TextWidthWin(done);
            int dx = listX + listW - UIS(8) - dw;
            if (dx > listX + UIS(6) + TextWidthWin(line) + UIS(12)) {
                DrawTextWin(dx, ty, done, 0x0040C040u);
            }
        }
    }

    /* === 滚动条滑块：轨道高度 = 可见行总高 === */
    if (g.count > maxVisibleRows) {
        int trackX = panelX + panelW - UIS(18);
        int trackW = UIS(10);
        int trackY = listStartY;

        unsigned int trackColor = 0x00323232u;   /* RGB(50,50,50) */
        unsigned int thumbColor = 0x00969696u;   /* RGB(150,150,150) */

        /* 轨道背景 */
        BlendRectWin(trackX, trackY, trackX + trackW, trackY + trackHeight, trackColor, 1.0f);

        int thumbHeight = (maxVisibleRows * trackHeight) / g.count;
        if (thumbHeight < UIS(20)) thumbHeight = UIS(20);
        if (thumbHeight > trackHeight) thumbHeight = trackHeight;

        int maxScroll = g.count - maxVisibleRows;
        if (maxScroll <= 0) maxScroll = 1;
        int thumbY = trackY + (g.scrollOffset * (trackHeight - thumbHeight) / maxScroll);
        if (thumbY < trackY) thumbY = trackY;
        if (thumbY + thumbHeight > trackY + trackHeight)
            thumbY = trackY + trackHeight - thumbHeight;

        /* 绘制拇指（留 2px 间隙模拟圆角） */
        BlendRectWin(trackX + 2, thumbY + 2, trackX + trackW - 2, thumbY + thumbHeight - 2, thumbColor, 1.0f);
    }

    /* === Tooltip：鼠标悬停在成就上时显示详细说明 === */
    if (achHovered >= 0 && achHovered < ACH_TOTAL) {
        DrawAchievementTooltip(0, 0, achHovered, g);  /* mx/my 不用，函数内部用全局鼠标位置 */
    }
}

static void DrawAchievementTooltip(int mx, int my, int hoveredIndex, MenuGeo g) {
    (void)mx; (void)my; (void)g;
    if (hoveredIndex < 0 || hoveredIndex >= ACH_TOTAL) return;
    if (currentState != STATE_ACHIEVEMENTS) return;

    const Achievement* ach = &g_achievements[hoveredIndex];
    if (!ach->desc[0]) return;  /* 没有描述则不显示 */

    /* 使用全局鼠标位置 g_mousePos */
    int tipX = g_mousePos.x + UIS(16);
    int tipY = g_mousePos.y + UIS(8);

    /* 计算文本宽度，确保不超出屏幕 */
    int textW = TextWidthWin(ach->desc);
    int textH = TextHeightWin();
    int pad = UIS(8);
    int tipW = textW + pad * 2;
    int tipH = textH + pad * 2;

    /* 边界修正 */
    if (tipX + tipW > winW - UIS(10)) tipX = winW - UIS(10) - tipW;
    if (tipY + tipH > winH - UIS(10)) tipY = winH - UIS(10) - tipH;
    if (tipX < UIS(10)) tipX = UIS(10);
    if (tipY < UIS(10)) tipY = UIS(10);

    /* 背景 */
    BlendRectWin(tipX, tipY, tipX + tipW, tipY + tipH, 0x00181818u, 0.95f);
    FrameRectWin(tipX, tipY, tipX + tipW, tipY + tipH, 0x00606060u, 0.9f, 1);

    /* 文字 */
    DrawTextWin(tipX + pad, tipY + pad, ach->desc, 0x00E0E0E0u);
}

/* 改绑提示框：居中，跟设置面板同样的画法。
   rebindingAct >= 0 时由 RenderFrame 在主面板之后调用。 */
static void DrawRebindOverlay(void) {
    if (currentState != STATE_SETTINGS || rebindingAct < 0 || rebindingAct >= ACT_COUNT) return;

    int pad = UIS(26), titlePx = UIS(34);
    int panelW = UIS(520);
    if (panelW > winW - 80) panelW = winW - 80;
    int textW = TextWidthWin(actionNames[rebindingAct]);
    if (panelW < textW + UIS(60)) panelW = textW + UIS(60);
    int panelH = pad * 2 + titlePx + UIS(24) + TextHeightWin() * 3 + UIS(24);

    int left = (winW - panelW) / 2;
    int top = (winH - panelH) / 2;
    int right = left + panelW;
    int bottom = top + panelH;

    BlendRectWin(left, top, right, bottom, 0x0004080c, 1.0f);
    FrameRectWin(left, top, right, bottom, 0x00b02020, 0.95f, UIS(2));
    DrawTitleWin("REBIND", top + pad, 0x00E65A5Au);
    DrawTextCenterWin(winW / 2, top + pad + titlePx + UIS(24), actionNames[rebindingAct], 0x00A0FFA0u);
    if (rebindConflict >= 0 && rebindConflict < ACT_COUNT) {
        /* 冲突确认：告诉玩家会顶掉哪个动作的绑定，再按一次才生效 */
        char cbuf[96];
        sprintf(cbuf, "%s USES THIS KEY", actionNames[rebindConflict]);
        DrawTextCenterWin(winW / 2, bottom - pad - TextHeightWin() * 2, cbuf, 0x00FFC040u);
        DrawTextCenterWin(winW / 2, bottom - pad - TextHeightWin(), "PRESS AGAIN TO REPLACE", 0x008C8C8Cu);
    } else {
        DrawTextCenterWin(winW / 2, bottom - pad - TextHeightWin() * 2, "PRESS ANY KEY", 0x008C8C8Cu);
        DrawTextCenterWin(winW / 2, bottom - pad - TextHeightWin(), "ESC TO CANCEL", 0x006E6E6Eu);
    }
}

/* 把一次按键交给改绑流程。返回 1 = 这个键被改绑消费掉了。
   Esc = 取消；冲突时第一次按键只显示确认提示（不静默抢键），
   同一个键再按一次才执行替换。
   鼠标键也能绑（WM_L/R/MBUTTONDOWN 走这里）。 */
int RebindConsumeKey(int vk) {
    if (rebindingAct < 0 || rebindingAct >= ACT_COUNT) return 0;
    if (vk == VK_ESCAPE) {
        rebindingAct = -1;
        rebindConflict = -1;
        uiDirty = 1;
        return 1;
    }
    /* 冲突检测：这个键已经绑在别的动作上了吗？ */
    int conflict = -1;
    for (int a = 0; a < ACT_COUNT; a++) {
        if (a != rebindingAct && g_bindings[a] == vk) { conflict = a; break; }
    }
    if (conflict >= 0 && rebindConflict != conflict) {
        /* 第一次按：只确认，不抢键。同一个键再按一次才执行替换。 */
        rebindConflict = conflict;
        uiDirty = 1;
        return 1;
    }
    /* 无冲突，或者玩家已经确认替换：清掉旧动作上的这个键 */
    if (conflict >= 0) g_bindings[conflict] = 0;
    rebindConflict = -1;
    g_bindings[rebindingAct] = vk;
    rebindingAct = -1;
    uiDirty = 1;
    return 1;
}

/* 弹窗里的文字：必须在弹窗面板之后画 */
void DrawPopupText(void) {
    if (currentState != STATE_SETTINGS || !settingsPopupOpen) return;
    MenuGeo p = GetPopupGeo();
    const SettingItem* it = &settings[popupItem];
    DrawTitleWin(p.title, p.titleY, 0x00E65A5Au);

    if (it->kind == SET_SLIDER) {
        char val[32];
        int isGamma = (it->fvalue == &gammaValue);
        if (isGamma) sprintf(val, "%.2f", *it->fvalue);
        else sprintf(val, "%.1fx", *it->fvalue);
        DrawTextCenterWin(winW / 2, p.firstY, val, 0x00A0FFA0u);

        RECT t = SliderTrackRect(p);
        char l[32], r[32];
        if (isGamma) {
            sprintf(l, "%.2f", it->fmin);
            sprintf(r, "%.2f", it->fmax);
        } else {
            sprintf(l, "%.1fx", it->fmin);
            sprintf(r, "%.1fx", it->fmax);
        }
        DrawTextWin(t.left, t.bottom + UIS(10), l, 0x008C8C8Cu);
        DrawTextWin(t.right - TextWidthWin(r), t.bottom + UIS(10), r, 0x008C8C8Cu);

        const char* hint = "DRAG OR A/D   ENTER OK";
        DrawTextCenterWin(winW / 2, p.panel.bottom - UIS(38), hint, 0x006E6E6Eu);
    } else {
        for (int i = 0; i < p.count; i++) {
            unsigned int c = (i == popupSelection) ? 0x00A0FFA0u : 0x00A0A0A0u;
            DrawTextInRow(settings[popupItem].options[i], RowRect(p, i), c);
        }
    }
}

/* 进度条：底色 + 填充 + 细边框，和文字一样画在像素层 */
static void DrawBar(int x, int y, int w, int h, float frac, unsigned int color) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    BlendRectWin(x, y, x + w, y + h, 0x00000000, 0.65f);
    int fw = (int)(w * frac);
    if (fw > 0) BlendRectWin(x, y, x + fw, y + h, color, 0.92f);
    FrameRectWin(x, y, x + w, y + h, 0x00909090, 0.55f, 1);
}
/* ---------------- 开锁锁盘（v1.8.0） ----------------
   布局、绘制、命中测试共用同一套几何（和菜单 MakeGeo 一个思路）：
   鼠标点击由 game.c 调 LockHitTest，绘制在本文件，两边不会对不上。 */

typedef struct { int x0, y0, x1, y1; } LockBox;

static int LockIn(LockBox b, int x, int y) {
    return x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1;
}

static LockBox LockPanelBox(void) {
    int pw = UIS(540);
    /* 调波器要放下示波器 + 三个旋钮，面板比别的锁高一点 */
    int ph = (lockType == LOCK_TYPE_TUNE) ? UIS(520) : UIS(440);
    LockBox p;
    p.x0 = (winW - pw) / 2;
    p.y0 = (winH - ph) / 2;
    p.x1 = p.x0 + pw;
    p.y1 = p.y0 + ph;
    return p;
}

/* 第 d 个拨盘的三块：上箭头 / 数字格 / 下箭头 */
static void LockDialBox(int d, LockBox* up, LockBox* digit, LockBox* down) {
    LockBox p = LockPanelBox();
    int box = UIS(96), gap = UIS(34);
    int total = LOCK_DIALS * box + (LOCK_DIALS - 1) * gap;
    int bx = p.x0 + (p.x1 - p.x0 - total) / 2 + d * (box + gap);
    int by = p.y0 + UIS(122);
    up->x0 = bx; up->y0 = by - UIS(38); up->x1 = bx + box; up->y1 = by - UIS(10);
    digit->x0 = bx; digit->y0 = by; digit->x1 = bx + box; digit->y1 = by + box;
    down->x0 = bx; down->y0 = by + box + UIS(10); down->x1 = bx + box; down->y1 = by + box + UIS(38);
}

/* 接线锁：左右两列端子。side 0 = 左，1 = 右 */
static void LockWireNodeBox(int side, int idx, LockBox* b) {
    LockBox p = LockPanelBox();
    int size = UIS(44);
    int x = (side == 0) ? (p.x0 + UIS(110)) : (p.x1 - UIS(110) - size);
    int y = p.y0 + UIS(132) + idx * UIS(70);
    b->x0 = x; b->y0 = y; b->x1 = x + size; b->y1 = y + size;
}

/* 均衡器：第 band 根推子的柱体 / 上箭头 / 下箭头 */
static void LockEqBarBox(int band, LockBox* bar) {
    LockBox p = LockPanelBox();
    int bw = UIS(64), bh = UIS(170), gap = UIS(46);
    int total = LOCK_EQ_BANDS * bw + (LOCK_EQ_BANDS - 1) * gap;
    int x0 = p.x0 + (p.x1 - p.x0 - total) / 2 + band * (bw + gap);
    int y1 = p.y0 + UIS(295);
    bar->x0 = x0; bar->y0 = y1 - bh; bar->x1 = x0 + bw; bar->y1 = y1;
}

static void LockEqArrowBox(int band, int isUp, LockBox* b) {
    LockBox bar;
    LockEqBarBox(band, &bar);
    b->x0 = bar.x0;
    b->x1 = bar.x1;
    if (isUp) { b->y1 = bar.y0 - UIS(8); b->y0 = b->y1 - UIS(30); }
    else      { b->y0 = bar.y1 + UIS(8); b->y1 = b->y0 + UIS(30); }
}

/* 调波器：示波器区域 + 三个旋钮（每个旋钮左右各一个 - / + 按钮） */
static void LockTuneScopeBox(LockBox* s) {
    LockBox p = LockPanelBox();
    s->x0 = p.x0 + UIS(90);
    s->y0 = p.y0 + UIS(82);
    s->x1 = p.x1 - UIS(90);
    s->y1 = p.y0 + UIS(282);
}

static void LockTuneBoxes(int i, LockBox* dec, LockBox* knob, LockBox* inc) {
    LockBox p = LockPanelBox();
    int ks = UIS(76), bs = UIS(36), gap = UIS(90);
    int total = LOCK_TUNE_KNOBS * ks + (LOCK_TUNE_KNOBS - 1) * gap;
    int kx = p.x0 + (p.x1 - p.x0 - total) / 2 + i * (ks + gap);
    int ky = p.y0 + UIS(312);
    knob->x0 = kx; knob->y0 = ky; knob->x1 = kx + ks; knob->y1 = ky + ks;

    dec->x0 = kx - bs - UIS(10);
    dec->y0 = ky + (ks - bs) / 2;
    dec->x1 = dec->x0 + bs;
    dec->y1 = dec->y0 + bs;

    inc->x0 = kx + ks + UIS(10);
    inc->y0 = dec->y0;
    inc->x1 = inc->x0 + bs;
    inc->y1 = inc->y0 + bs;
}

/* 底部只有一个 BACK 按钮：谜题一解开就自动开锁，不需要 TRY */
static LockBox LockBackBox(void) {
    LockBox p = LockPanelBox();
    int bw = UIS(200), bh = UIS(48);
    int x0 = p.x0 + (p.x1 - p.x0 - bw) / 2;
    int y0 = p.y1 - UIS(95);
    LockBox b = { x0, y0, x0 + bw, y0 + bh };
    return b;
}

int LockHitTest(int mx, int my, int* a) {
    if (a) *a = -1;
    if (LockIn(LockBackBox(), mx, my)) return LOCK_HIT_BACK;

    if (lockType == LOCK_TYPE_DIAL) {
        for (int d = 0; d < LOCK_DIALS; d++) {
            LockBox up, dg, dn;
            LockDialBox(d, &up, &dg, &dn);
            if (LockIn(dg, mx, my)) { if (a) *a = d; return LOCK_HIT_DIAL; }
            if (LockIn(up, mx, my)) { if (a) *a = d; return LOCK_HIT_UP; }
            if (LockIn(dn, mx, my)) { if (a) *a = d; return LOCK_HIT_DOWN; }
        }
    } else if (lockType == LOCK_TYPE_WIRE) {
        for (int i = 0; i < LOCK_WIRES; i++) {
            LockBox b;
            LockWireNodeBox(0, i, &b);
            if (LockIn(b, mx, my)) { if (a) *a = i; return LOCK_HIT_NODE_L; }
            LockWireNodeBox(1, i, &b);
            if (LockIn(b, mx, my)) { if (a) *a = i; return LOCK_HIT_NODE_R; }
        }
    } else if (lockType == LOCK_TYPE_EQ) {
        for (int band = 0; band < LOCK_EQ_BANDS; band++) {
            LockBox up, dn;
            LockEqArrowBox(band, 1, &up);
            LockEqArrowBox(band, 0, &dn);
            if (LockIn(up, mx, my)) { if (a) *a = band; return LOCK_HIT_BAR_UP; }
            if (LockIn(dn, mx, my)) { if (a) *a = band; return LOCK_HIT_BAR_DOWN; }
        }
    } else if (lockType == LOCK_TYPE_TUNE) {
        for (int k = 0; k < LOCK_TUNE_KNOBS; k++) {
            LockBox dec, kn, inc;
            LockTuneBoxes(k, &dec, &kn, &inc);
            if (LockIn(dec, mx, my)) { if (a) *a = k; return LOCK_HIT_KNOB_DEC; }
            if (LockIn(inc, mx, my)) { if (a) *a = k; return LOCK_HIT_KNOB_INC; }
        }
    }
    return LOCK_HIT_NONE;
}

/* 一个带悬停反馈的按钮 */
static void LockDrawButton(LockBox b, const char* label, int hot, unsigned int accent) {
    BlendRectWin(b.x0, b.y0, b.x1, b.y1, hot ? 0x00323A42u : 0x001A2026u, 0.98f);
    FrameRectWin(b.x0, b.y0, b.x1, b.y1, hot ? accent : 0x004A525Au, 0.95f, hot ? 2 : 1);
    DrawTextCenterWin((b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2 - TextHeightWin() / 2,
                      label, hot ? 0x00FFE080u : 0x00C8D0D8u);
}

/* 开锁覆盖层（v1.8.0）：游戏**不暂停** —— 只是挡住移动与搜索，怪照常过来。
   所以先把整屏压暗，让面板从场景里浮出来，而不是在花花绿绿的背景上叠半透明块。 */
static void LockDrawDialBody(int hit, int ha) {
    for (int d = 0; d < LOCK_DIALS; d++) {
        LockBox up, dg, dn;
        LockDialBox(d, &up, &dg, &dn);
        int sel = (d == lockCursor);
        int hotUp = (hit == LOCK_HIT_UP && ha == d);
        int hotDn = (hit == LOCK_HIT_DOWN && ha == d);
        int hotDg = (hit == LOCK_HIT_DIAL && ha == d);

        LockDrawButton(up, "^", hotUp, 0x00E0C060u);
        LockDrawButton(dn, "v", hotDn, 0x00E0C060u);

        unsigned int fill = sel ? 0x00202832u : 0x0013181Du;
        unsigned int edge = sel ? 0x00E0C060u : (hotDg ? 0x00988A50u : 0x004A525Au);
        BlendRectWin(dg.x0, dg.y0, dg.x1, dg.y1, fill, 0.98f);
        FrameRectWin(dg.x0, dg.y0, dg.x1, dg.y1, edge, 0.95f, sel ? UIS(2) : 1);
        /* 这一格已经拨对：内侧描一圈细绿边（声音之外的视觉兜底） */
        if (LockDialSolved(d)) {
            int t = UIS(2);
            FrameRectWin(dg.x0 + t, dg.y0 + t, dg.x1 - t, dg.y1 - t,
                         0x0040FF80u, 0.85f, 2);
        }

        char dbuf[8];
        sprintf(dbuf, "%d", lockDigits[d] % LOCK_DIGITS);
        DrawTextCenterWin((dg.x0 + dg.x1) / 2, (dg.y0 + dg.y1) / 2 - TextHeightWin() / 2,
                          dbuf, sel ? 0x00FFE080u : 0x00D0D8E0u);
    }

    /* 解谜提示：声音是主反馈，绿色圈是视觉兜底 */
    LockBox p = LockPanelBox();
    DrawTextCenterWin(winW / 2, p.y0 + UIS(286), "CLICK + GREEN RING = RIGHT DIGIT", 0x00B0B8C0u);
    DrawTextCenterWin(winW / 2, p.y0 + UIS(310), "THUD = WRONG", 0x00889098u);
}

/* 端子颜色：0 红 / 1 绿 / 2 蓝 */
static unsigned int LockTermColor(int c) {
    if (c == 0) return 0x00E05050u;
    if (c == 1) return 0x0050D060u;
    if (c == 2) return 0x005090E0u;
    return 0x00909090u;
}

/* 粗线（Bresenham），用于接线锁的导线 */
static void LockDrawLine(int x0, int y0, int x1, int y1, unsigned int c) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = (dx < 0) ? -dx : dx, ady = (dy < 0) ? -dy : dy;
    int sx = (dx < 0) ? -1 : 1, sy = (dy < 0) ? -1 : 1;
    int err = adx - ady;
    for (int guard = 0; guard < 4000; guard++) {
        BlendRectWin(x0, y0, x0 + UIS(3), y0 + UIS(3), c, 0.95f);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -ady) { err -= ady; x0 += sx; }
        if (e2 < adx) { err += adx; y0 += sy; }
    }
}

static void LockDrawWireBody(int hit, int ha) {
    LockBox p = LockPanelBox();

    /* 先画已经接好的线，压在端子下面 */
    for (int i = 0; i < LOCK_WIRES; i++) {
        if (lockWireLink[i] < 0) continue;
        LockBox lb, rb;
        LockWireNodeBox(0, i, &lb);
        LockWireNodeBox(1, lockWireLink[i], &rb);
        LockDrawLine((lb.x0 + lb.x1) / 2, (lb.y0 + lb.y1) / 2,
                     (rb.x0 + rb.x1) / 2, (rb.y0 + rb.y1) / 2,
                     LockTermColor(LockWireColor(i)));
    }

    for (int i = 0; i < LOCK_WIRES; i++) {
        LockBox b;
        LockWireNodeBox(0, i, &b);
        BlendRectWin(b.x0, b.y0, b.x1, b.y1, LockTermColor(LockWireColor(i)), 0.95f);
        int selHere = (lockWireSel == i);
        unsigned int edge = selHere ? 0x00FFE080u
                          : (hit == LOCK_HIT_NODE_L && ha == i) ? 0x00FFFFFFu : 0x00202020u;
        FrameRectWin(b.x0, b.y0, b.x1, b.y1, edge, 0.95f, selHere ? UIS(3) : UIS(2));

        LockWireNodeBox(1, i, &b);
        BlendRectWin(b.x0, b.y0, b.x1, b.y1, LockTermColor(LockWireRightColor(i)), 0.95f);
        FrameRectWin(b.x0, b.y0, b.x1, b.y1,
                     (hit == LOCK_HIT_NODE_R && ha == i) ? 0x00FFFFFFu : 0x00202020u,
                     0.95f, UIS(2));
    }

    DrawTextCenterWin(winW / 2, p.y0 + UIS(96),
                      (lockWireSel >= 0) ? "PICK A MATCHING TERMINAL" : "PICK A LEFT TERMINAL",
                      (lockWireSel >= 0) ? 0x00E0C060u : 0x00707880u);
}

static void LockDrawEqBody(int hit, int ha) {
    LockBox p = LockPanelBox();
    for (int band = 0; band < LOCK_EQ_BANDS; band++) {
        LockBox bar, up, dn;
        LockEqBarBox(band, &bar);
        LockEqArrowBox(band, 1, &up);
        LockEqArrowBox(band, 0, &dn);

        int solved = (lockEqVal[band] == LockEqTarget(band));
        LockDrawButton(up, "^", hit == LOCK_HIT_BAR_UP && ha == band, 0x00E0C060u);
        LockDrawButton(dn, "v", hit == LOCK_HIT_BAR_DOWN && ha == band, 0x00E0C060u);

        /* 槽 */
        BlendRectWin(bar.x0, bar.y0, bar.x1, bar.y1, 0x0013181Du, 0.98f);
        FrameRectWin(bar.x0, bar.y0, bar.x1, bar.y1, 0x004A525Au, 0.9f, 1);

        /* 当前高度 */
        int barH = bar.y1 - bar.y0;
        int hgt = barH * (lockEqVal[band] + 1) / LOCK_EQ_STEPS;
        BlendRectWin(bar.x0 + UIS(4), bar.y1 - hgt, bar.x1 - UIS(4), bar.y1 - UIS(4),
                     solved ? 0x0040C060u : 0x004080C0u, 0.95f);

        /* 目标高度：一条绿色标线 —— 这就是视觉线索 */
        int thgt = barH * (LockEqTarget(band) + 1) / LOCK_EQ_STEPS;
        int ty = bar.y1 - thgt;
        BlendRectWin(bar.x0 - UIS(6), ty, bar.x1 + UIS(6), ty + UIS(3), 0x0040FF80u, 0.9f);
    }
    DrawTextCenterWin(winW / 2, p.y0 + UIS(96), "PUSH EACH BAR TO THE GREEN LINE", 0x00707880u);
}

/* 调波器：画一条正弦波。amp/freq/phase 都是 0..7 的整数档。 */
static void LockTuneWave(LockBox a, int amp, int freq, int phase, unsigned int color) {
    int w = a.x1 - a.x0;
    if (w <= 0) return;
    int midY = (a.y0 + a.y1) / 2;
    float cycles = 1.0f + (float)freq * 3.0f / 7.0f;              /* 1 ~ 4 个周期 */
    float ph = (float)phase * 6.2831853f / (float)LOCK_TUNE_STEPS;
    float ampScale = 0.18f + 0.82f * (float)amp / 7.0f;
    float maxAmp = (float)(a.y1 - a.y0) * 0.42f;
    int prevX = 0, prevY = 0;
    for (int i = 0; i <= w; i += 2) {
        float u = (float)i / (float)w;
        float s = sinf(6.2831853f * cycles * u + ph);
        int x = a.x0 + i;
        int y = midY - (int)(s * maxAmp * ampScale);
        if (i > 0) LockDrawLine(prevX, prevY, x, y, color);
        prevX = x;
        prevY = y;
    }
}

static void LockDrawTuneBody(int hit, int ha) {
    static const char* knobNames[LOCK_TUNE_KNOBS] = {
        "1.AMPLITUDE", "2.FREQUENCY", "3.PHASE"
    };
    LockBox p = LockPanelBox();
    LockBox scope;
    LockTuneScopeBox(&scope);

    /* 示波器：底 + 边框 + 中线和竖分隔 */
    BlendRectWin(scope.x0, scope.y0, scope.x1, scope.y1, 0x00060C0Au, 0.96f);
    FrameRectWin(scope.x0, scope.y0, scope.x1, scope.y1, 0x00384A44u, 0.9f, UIS(2));
    int midY = (scope.y0 + scope.y1) / 2;
    BlendRectWin(scope.x0 + UIS(4), midY, scope.x1 - UIS(4), midY + 1, 0x001A2624u, 0.9f);
    for (int q = 1; q <= 3; q++) {
        int gx = scope.x0 + (scope.x1 - scope.x0) * q / 4;
        BlendRectWin(gx, scope.y0 + UIS(4), gx + 1, scope.y1 - UIS(4), 0x001A2624u, 0.9f);
    }

    /* 目标波形（暗）+ 玩家波形（亮）：重合即匹配 */
    LockTuneWave(scope, LockTuneTarget(0), LockTuneTarget(1), LockTuneTarget(2), 0x004A5A62u);
    LockTuneWave(scope, lockTuneVal[0], lockTuneVal[1], lockTuneVal[2], 0x0060FFC0u);

    for (int k = 0; k < LOCK_TUNE_KNOBS; k++) {
        LockBox dec, kn, inc;
        LockTuneBoxes(k, &dec, &kn, &inc);
        int onTarget = (lockTuneVal[k] == LockTuneTarget(k));

        LockDrawButton(dec, "-", hit == LOCK_HIT_KNOB_DEC && ha == k, 0x00E0C060u);
        LockDrawButton(inc, "+", hit == LOCK_HIT_KNOB_INC && ha == k, 0x00E0C060u);

        BlendRectWin(kn.x0, kn.y0, kn.x1, kn.y1, 0x001A2026u, 0.98f);
        FrameRectWin(kn.x0, kn.y0, kn.x1, kn.y1,
                     onTarget ? 0x0040FF80u : 0x005A6268u, 0.95f, onTarget ? UIS(2) : 1);

        /* 指针：0 档朝上，顺时针一圈 */
        float ang = (float)lockTuneVal[k] * 6.2831853f / (float)LOCK_TUNE_STEPS - 1.5707963f;
        int cx = (kn.x0 + kn.x1) / 2, cy = (kn.y0 + kn.y1) / 2;
        int r = (kn.x1 - kn.x0) / 2 - UIS(8);
        LockDrawLine(cx, cy, cx + (int)(cosf(ang) * r), cy + (int)(sinf(ang) * r),
                     onTarget ? 0x0040FF80u : 0x00E0C060u);

        DrawTextCenterWin((kn.x0 + kn.x1) / 2, kn.y1 + UIS(10), knobNames[k],
                          onTarget ? 0x0080E0A0u : 0x00A8B0B8u);
    }

    DrawTextCenterWin(winW / 2, p.y0 + UIS(236), "MATCH THE BRIGHT WAVE TO THE DIM ONE", 0x00707880u);
}

/* 开锁覆盖层总入口：四种锁型共用同一块面板与底栏 */
static void DrawLockOverlay(void) {
    LockBox p = LockPanelBox();
    int mx = g_mousePos.x, my = g_mousePos.y;
    int ha = -1;
    int hit = LockHitTest(mx, my, &ha);

    BlendRectWin(0, 0, winW, winH, 0x00000000u, 0.55f);

    BlendRectWin(p.x0, p.y0, p.x1, p.y1, 0x000A0E12u, 0.97f);
    FrameRectWin(p.x0, p.y0, p.x1, p.y1, 0x00C8A040u, 0.85f, UIS(2));

    DrawTextCenterWin(winW / 2, p.y0 + UIS(24), "LOCKED", 0x00E05050u);

    if (lockType == LOCK_TYPE_DIAL) {
        DrawTextCenterWin(winW / 2, p.y0 + UIS(52), "TURN THE DIALS UNTIL EVERY ONE CLICKS", 0x0090A0A8u);
        LockDrawDialBody(hit, ha);
    } else if (lockType == LOCK_TYPE_WIRE) {
        DrawTextCenterWin(winW / 2, p.y0 + UIS(52), "CONNECT EACH LEFT TERMINAL TO ITS COLOR", 0x0090A0A8u);
        LockDrawWireBody(hit, ha);
        DrawTextCenterWin(winW / 2, p.y0 + UIS(336), "WRONG COLOR = THUD", 0x00889098u);
    } else if (lockType == LOCK_TYPE_EQ) {
        DrawTextCenterWin(winW / 2, p.y0 + UIS(52), "MATCH EVERY BAR TO ITS GREEN LINE", 0x0090A0A8u);
        LockDrawEqBody(hit, ha);
    } else {
        DrawTextCenterWin(winW / 2, p.y0 + UIS(52), "TUNE THE SCOPE UNTIL THE WAVES MATCH", 0x0090A0A8u);
        LockDrawTuneBody(hit, ha);
    }

    LockDrawButton(LockBackBox(), "BACK", hit == LOCK_HIT_BACK, 0x00C05050u);

    DrawTextCenterWin(winW / 2, p.y1 - UIS(32), "MOUSE ONLY    -    ANY KEY TO LEAVE", 0x00707880u);
}

void DrawHUD(void) {
    char buf[96];

    /* 穿墙期间整屏薄薄压一层蓝：让玩家立刻意识到「我现在在墙里」 */
    if (phaseActive) BlendRectWin(0, 0, winW, winH, 0x0030A0FFu, 0.10f);

    /* ------------------------------------------------------------------ */
    /* HUD 详细度                                                        */
    /* ------------------------------------------------------------------ */
    /* 三档（设置里的 HUD Detail）：
         0 Minimal —— 只留必须实时知道的：数值、电量体力、状态效果
         1 Normal  —— 加：投掷物数量（有库存时才显示）、SAFE ZONE 短提示
         2 Full    —— 加：投掷物数量常驻、SAFE ZONE 常驻
       词条横幅在任何档位都只在进关后 5 秒出现（levelBanner 控制），
       因为它是「一次性信息」，常驻反而是噪音。 */

    sprintf(buf, "LEVEL %d", currentLevel);
    DrawTextWin(UIS(20), UIS(18), buf, 0x0000FF00u);

    /* 当前词条：常驻显示在 LEVEL 那一行的右边 —— **不新增一行**。
       词条是难度曲线的一部分（HUNTED 怪更快、LEAKY 理智掉更快），
       玩家看不见就不知道自己在被什么惩罚。
       用暗橙色区分于绿色的 LEVEL。
       详细说明（"THE THING MOVES FASTER"）仍然只在进关 5 秒的横幅里出现，
       常驻的是「提醒」，横幅是「解释」。 */
    if (levelMod >= 0 && levelMod < MOD_COUNT) {
        int lx = UIS(20) + TextWidthWin(buf) + UIS(14);
        sprintf(buf, "%s", modNames[levelMod]);
        DrawTextWin(lx, UIS(18), buf, 0x00C08040u);
    }

    sprintf(buf, "SANITY: %.0f%%", sanity);
    DrawTextWin(UIS(20), UIS(52), buf, (sanity < 30.0f) ? 0x00FF3C3Cu : 0x0000FFFFu);

    if (breakerOpen) sprintf(buf, "POWER ON");
    else sprintf(buf, "FUSES %d/%d", fusesHeld, fusesNeeded);
    DrawTextWin(UIS(20), UIS(86), buf, breakerOpen ? 0x0030FF60u : 0x00E8C040u);

    /* 状态效果：常驻显示在 FUSES 下面。
       玩家必须知道自己被怎么了 —— 否则「视野突然变窄」会被当成 bug。
       纵向排布（UIS 是设计分辨率，600 高）：
         86  FUSES
         116 状态效果 1   142 状态效果 2   168 状态效果 3
         196 FLARE / BOTTLE */
    {
        static const char* fxName[FX_COUNT] = { "BLINDED", "SLOWED", "DRAINING" };
        int fy = UIS(116);
        for (int i = 0; i < FX_COUNT; i++) {
            float s = FxStrength(i);
            if (s <= 0.0f) continue;
            sprintf(buf, "%s %d%%", fxName[i], (int)(s * 100.0f + 0.5f));
            DrawTextWin(UIS(20), fy, buf, 0x00FF6060u);
            fy += UIS(26);
        }
    }

    /* 投掷物数量。
       Minimal：不显示。
       Normal：只在身上有货时才显示（平时不占地方）。
       Full：一直显示。 */
    if (hudDetail >= 2 || (hudDetail >= 1 && (flares > 0 || bottles > 0))) {
        /* 高亮当前选中的槽位：选中项前加 *，数量为 0 时显示灰色 */
        char slot1[32], slot2[32];
        if (flares > 0) sprintf(slot1, "%s[1] FLARE %d", (throwSelected == 0 ? "*" : ""), flares);
        else sprintf(slot1, "%s[1] FLARE 0", (throwSelected == 0 ? "*" : ""));
        if (bottles > 0) sprintf(slot2, "%s[2] BOTTLE %d", (throwSelected == 1 ? "*" : ""), bottles);
        else sprintf(slot2, "%s[2] BOTTLE 0", (throwSelected == 1 ? "*" : ""));
        sprintf(buf, "%s   %s", slot1, slot2);
        DrawTextWin(UIS(20), UIS(196), buf, 0x0090A0A0u);
    }

    /* ------------------------------------------------------------------ */
    /* 跳脸虫 QTE                                                        */
    /* ------------------------------------------------------------------ */
    /* 这是整个事件里最关键的一块 UI。玩家只有不到 2 秒的反应时间，
       所以提示必须：**巨大、居中、带明确的倒计时条**。
       任何含糊都会让玩家觉得是游戏 bug。 */
    if (parasite.active && jumperPhase != JMP_IDLE && jumperPhase != JMP_DONE) {
        float frac = 0.0f;
        char prompt[48];
        unsigned int col = 0x00FFE060u;

        if (jumperPhase == JMP_DODGE) {
            frac = jumperTimer / JUMPER_DODGE_WINDOW;
            /* 提示跟随绑定：改绑后提示和实际按键保持一致 */
            const char* dodgeName = VkName(g_bindings[ACT_DODGE]);
            if (!dodgeName) dodgeName = "???";
            sprintf(prompt, "PRESS %s TO DODGE", dodgeName);
            col = 0x00FF8040u;
        } else {
            frac = jumperTimer / JUMPER_QTE_WINDOW;
            /* 阶段二的候选键池与 UpdateParasite 同源：取移动四键的绑定 */
            static const ActionId poolSrc[JMP_KEY_COUNT] = {
                ACT_MOVE_FWD, ACT_MOVE_BACK, ACT_MOVE_LEFT, ACT_MOVE_RIGHT
            };
            int vk = g_bindings[poolSrc[jumperQteKey % JMP_KEY_COUNT]];
            const char* nm = VkName(vk);
            sprintf(prompt, "%s", nm ? nm : "???");
            col = 0x00FF3030u;
        }
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;

        /* 阶段二：把要按的键画得非常大，一眼就能看到 */
        if (jumperPhase == JMP_QTE) {
            /* 用点阵字放大绘制：把字符画成一个方块阵 */
            int keyW = TextWidthWin(prompt) * 3;
            int keyH = TextHeightWin() * 3;
            int kx = (winW - keyW) / 2;
            int ky = winH / 2 - keyH / 2;
            /* 底板 */
            BlendRectWin(kx - UIS(10), ky - UIS(6), kx + keyW + UIS(10), ky + keyH + UIS(6),
                         0x00202020, 0.75f);
            FrameRectWin(kx - UIS(10), ky - UIS(6), kx + keyW + UIS(10), ky + keyH + UIS(6),
                         col, 0.9f, 2);
            /* 把字放大 3 倍画 */
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    DrawTextWin(kx + (i) * (keyW / 3), ky + (j) * (keyH / 3), prompt, col);
                }
            }
            /* 英文说明只在 Full 档显示 */
            if (qteHint >= 1)
                DrawTextCenterWin(winW / 2, ky - UIS(30), "IT IS ON YOUR FACE", 0x00FF6060u);
        } else {
            /* 阶段一：Minimal 档只显示一个躲开键图标（跟随绑定），
               Full 档才显示英文说明。 */
            if (qteHint >= 1) {
                DrawTextCenterWin(winW / 2, winH / 2 - UIS(40), prompt, col);
            } else {
                const char* dodgeName = VkName(g_bindings[ACT_DODGE]);
                if (!dodgeName) dodgeName = "???";
                int keyW = TextWidthWin(dodgeName) * 2;
                int keyH = TextHeightWin() * 2;
                int kx = (winW - keyW) / 2;
                int ky = winH / 2 - keyH / 2;
                BlendRectWin(kx - UIS(10), ky - UIS(6), kx + keyW + UIS(10), ky + keyH + UIS(6),
                             0x00202020, 0.75f);
                FrameRectWin(kx - UIS(10), ky - UIS(6), kx + keyW + UIS(10), ky + keyH + UIS(6),
                             col, 0.9f, 2);
                for (int i = 0; i < 2; i++)
                    for (int j = 0; j < 2; j++)
                        DrawTextWin(kx + i * (keyW / 2), ky + j * (keyH / 2), dodgeName, col);
            }
        }

        /* 倒计时条：屏幕中央下方一条，越短越红 */
        int barW = UIS(240), barH = UIS(10);
        int bx = (winW - barW) / 2;
        int by = winH / 2 + UIS(56);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00202020, 0.8f);
        unsigned int barCol = col;
        if (frac < 0.35f) barCol = 0x00FF2020u;    /* 快没了 -> 变红 */
        BlendRectWin(bx, by, bx + (int)(barW * frac), by + barH, barCol, 0.95f);
        FrameRectWin(bx, by, bx + barW, by + barH, 0x00808080, 0.7f, 1);
    }

    /* 词条横幅：只在进关后 5 秒出现。
       词条**名**已经常驻在 LEVEL 那一行右边了，所以这里只重复「说明」——
       玩家刚进关时需要知道这一层的规则具体是什么。 */
    if (levelBanner > 0.0f) {
        int y = UIS(258);
        if (levelMod >= 0) {
            DrawTextWin(UIS(20), y, modDesc[levelMod], 0x00C08050u);
        }
        DrawTextWin(UIS(20), y + UIS(30), themeNames[themeId], 0x00B08040u);
    }

    if (showFps) {
        sprintf(buf, "FPS %d  %.1f/%.1fms  %dx%d", currentFps, lastRenderMs, frameMsMax, renderW, renderH);
        DrawTextWin(winW - UIS(20) - TextWidthWin(buf), UIS(18), buf, 0x00C8C8C8u);
    }

    /* 拾取提示：屏幕中央上方，2秒后淡出 */
    if (pickupMsg && pickupMsgTimer > 0.0f) {
        int tw = TextWidthWin(pickupMsg);
        int tx = (winW - tw) / 2;
        int ty = UIS(80);
        float alpha = pickupMsgTimer / 2.0f;
        if (alpha > 1.0f) alpha = 1.0f;
        DrawTextWin(tx, ty, pickupMsg, 0x00E8E060u);
    }

    /* 技能槽：左下，LIGHT 条上面一行。冷却好 = 绿，冷却中 = 灰，生效中 = 亮蓝 */
    {
        int sy = winH - UIS(126);
        const char* key = VkName(g_bindings[ACT_SKILL]);
        const char* sname = SkillName(runSkill);
        int activeNow = (runSkill == SKILL_PHASE && phaseActive) ||
                        (runSkill == SKILL_SIGHT && sightActive);
        float left = (runSkill == SKILL_SIGHT) ? skillSightTimer : skillPhaseTimer;
        float full = SkillDuration(runSkill);
        char sbuf[64];
        unsigned int scol;
        if (activeNow) {
            sprintf(sbuf, "%s  %s  %.1fs", key, sname, left);
            scol = 0x0060D0FFu;
        } else if (skillCooldownTimer > 0.0f) {
            sprintf(sbuf, "%s  %s  %.0fs", key, sname, skillCooldownTimer);
            scol = 0x00888888u;
        } else {
            sprintf(sbuf, "%s  %s  READY", key, sname);
            scol = 0x0060E0A0u;
        }
        DrawTextWin(UIS(20), sy, sbuf, scol);

        int sbw = UIS(180), sbh = UIS(5);
        int sbx = UIS(20), sby = sy + TextHeightWin() + UIS(4);
        float frac;
        if (activeNow) frac = (full > 0.0f) ? left / full : 0.0f;
        else if (skillCooldownTimer > 0.0f) frac = 1.0f - skillCooldownTimer / SkillCooldown(runSkill);
        else frac = 1.0f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        BlendRectWin(sbx, sby, sbx + sbw, sby + sbh, 0x00202020u, 0.75f);
        if (frac > 0.0f) BlendRectWin(sbx, sby, sbx + (int)(sbw * frac), sby + sbh, scol, 0.95f);
        FrameRectWin(sbx, sby, sbx + sbw, sby + sbh, 0x00808080u, 0.6f, 1);
    }

    /* 手电电量 / 体力：左下两条细条，标签按实际文字宽度排版 */
    int barW = UIS(180), barH = UIS(12);
    int labelX = UIS(20);
    int barX = labelX + TextWidthWin("LIGHT") + UIS(12);
    int y1 = winH - UIS(96), y2 = winH - UIS(74);

    DrawTextWin(labelX, y1 - UIS(3), "LIGHT", 0x00A0A0A0u);
    /* 手电条颜色：开启=黄绿，关闭但有电=暗黄，无电=暗红 */
    unsigned int lightBarCol;
    if (!flashlightOn && battery <= 0.0f) lightBarCol = 0x00603030u;  /* 无电暗红 */
    else if (!flashlightOn) lightBarCol = 0x00806030u;  /* 关闭暗黄 */
    else lightBarCol = 0x00E8E060u;  /* 开启亮黄绿 */
    float batMax = (float)BatteryMax();
    DrawBar(barX, y1, barW, barH, battery / batMax, lightBarCol);
    DrawTextWin(barX + barW + UIS(12), y1 - UIS(3), flashlightOn ? "ON" : "OFF",
                flashlightOn ? 0x00E8E060u : 0x00A08080u);

    DrawTextWin(labelX, y2 - UIS(3), "RUN", 0x00A0A0A0u);
    DrawBar(barX, y2, barW, barH, stamina / 100.0f,
            (stamina < 25.0f) ? 0x00FF6A3Cu : 0x0040C060u);

    /* 交互提示 + 长按进度 */
    if (hoverKind != 0) {
        char pbuf[64];
        int lockedHere = (hoverKind == 1 && hoverIndex >= 0 && hoverIndex < MAX_CONTAINERS &&
                          contLocked[hoverIndex]);
        if (hoverKind == 4) sprintf(pbuf, "PRESS E  TAKE CODE");
        else if (hoverKind == 3) {
            if (hiddenCodesFound >= hiddenCodeCount) sprintf(pbuf, "PRESS E  OPEN HIDDEN DOOR");
            else sprintf(pbuf, "HIDDEN DOOR   CODES %d/%d", hiddenCodesFound, hiddenCodeCount);
        }
        else if (hoverKind == 1 && lockedHere) sprintf(pbuf, "PRESS E  UNLOCK");
        else if (hoverKind == 1) sprintf(pbuf, "HOLD E  SEARCH");
        else if (fusesHeld >= fusesNeeded) sprintf(pbuf, "HOLD E  POWER ON");
        else sprintf(pbuf, "NEED %d MORE FUSE", fusesNeeded - fusesHeld);

        int tw = TextWidthWin(pbuf);
        int px = (winW - tw) / 2;
        int py = winH - UIS(150);
        DrawTextWin(px, py, pbuf, (hoverKind == 2 && fusesHeld < fusesNeeded) ? 0x00C05050u
                                  : (lockedHere ? 0x00E0B040u : 0x00E8E8E8u));

        int barW = UIS(240), barH = UIS(8);
        int bx = (winW - barW) / 2;
        int by = py + UIS(30);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00202020, 0.8f);
        /* 上锁的箱子是「按一下」不是「长按」，不画进度条 */
        if (holdingNow) {
            float f = holdTimer / SEARCH_TIME;
            if (f > 1.0f) f = 1.0f;
            BlendRectWin(bx, by, bx + (int)(barW * f), by + barH, 0x00C8E8C8, 0.95f);
        }
        FrameRectWin(bx, by, bx + barW, by + barH, 0x00808080, 0.7f, 1);
    }

    /* SAFE ZONE：画在**屏幕中央下方**，不占用左侧信息列。
       之前它画在 (20, 190)，跟 FLARE/BOTTLE（也是 196 附近）重叠，
       玩家反馈过字叠在一起。
       Normal 档是「闪一下就消失」：进入安全屋时 safeZoneMsg 置 2 秒，
       之后不再显示 —— 常驻在安全屋里其实没什么用（你知道自己在哪）。 */
    if (safeRoom.active && Dist2(posX, posY, safeRoom.x, safeRoom.y) < SAFE_ZONE_RADIUS * SAFE_ZONE_RADIUS) {
        if (hudDetail >= 2) {
            DrawTextCenterWin(winW / 2, winH - UIS(196), "SAFE ZONE", 0x0030D0D0u);
        } else if (hudDetail >= 1 && safeZoneMsg > 0.0f) {
            DrawTextCenterWin(winW / 2, winH - UIS(196), "SAFE ZONE", 0x0030D0D0u);
        }
    }

    /* 蓄力投掷进度条：按住鼠标左键时显示 */
    int mouseFree = (currentState != STATE_PLAYING);
    /* 只有选中道具有库存时才显示蓄力条 */
    int hasItem = (throwSelected == 1 && bottles > 0) || (throwSelected == 0 && flares > 0);
    if (isCharging && mouseCharge > 0.0f && !mouseFree && hasItem) {
        int barW = UIS(280), barH = UIS(14);
        int bx = (winW - barW) / 2;
        int by = winH / 2 + UIS(100);  /* 更靠下，避开 QTE 倒计时条 */
        /* 背景：深色半透明，带明显边框 */
        BlendRectWin(bx - 2, by - 2, bx + barW + 2, by + barH + 2, 0x00000000, 0.9f);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00101020, 0.95f);
        /* 填充：蓝→青→红渐变感 */
        unsigned int chargeCol;
        if (mouseCharge < 0.33f) chargeCol = 0x0040E0FFu;
        else if (mouseCharge < 0.66f) chargeCol = 0x0040FF80u;
        else if (mouseCharge < 1.0f) chargeCol = 0x00E0FF40u;
        else chargeCol = 0x00FF4040u;  /* 满蓄力红色警告 */
        BlendRectWin(bx, by, bx + (int)(barW * mouseCharge), by + barH, chargeCol, 1.0f);
        /* 发光边框 */
        FrameRectWin(bx, by, bx + barW, by + barH, chargeCol, 1.0f, 2);
        /* 标签带阴影 */
        DrawTextCenterWin(winW / 2, by - UIS(24), "CHARGING THROW", chargeCol);
        /* 百分比数值 */
        char pct[16];
        sprintf(pct, "%d%%", (int)(mouseCharge * 100));
        DrawTextCenterWin(winW / 2, by + barH + UIS(6), pct, 0x00FFFFFFu);
    }

    if (cranking) {
        int barW = UIS(240), barH = UIS(8);
        int bx = (winW - barW) / 2;
        int by = winH - UIS(120);
        DrawTextCenterWin(winW / 2, by - UIS(34), "CRANKING", 0x00E8C040u);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00202020, 0.8f);
        float cc = crankTimer / 2.4f;
        if (cc > 1.0f) cc = 1.0f;
        BlendRectWin(bx, by, bx + (int)(barW * cc), by + barH, 0x00E8C040, 0.95f);
        FrameRectWin(bx, by, bx + barW, by + barH, 0x00808080, 0.7f, 1);
    }

    /* 碎片（SHARDS）是大厅货币，游戏过程中用不到 —— 不放进 HUD。
       「M CLOSE MAP」也删掉了：地图标题上已经写着
       「M: FULL  M: CLOSE」，再在底部挂一行是重复的。 */
}

/* ------------------------------------------------------------------ */
/* 一帧的呈现                                                          */
/* ------------------------------------------------------------------ */
void RenderFrame(void) {
    HDC hdc = GetDC(g_hwnd);
    if (!hdc) return;

    int showScene = (currentState == STATE_PLAYING) ||
                    (currentState == STATE_PAUSED) ||
                    (currentState == STATE_SETTINGS && prevState == STATE_PAUSED);

    if (showScene) {
        Render3D();
        /* 透视：隔墙标记，画在场景之上（不参与 zBuffer 遮挡） */
        if (sightActive) RenderSightOverlay();
        /* 跳脸虫：屏幕空间 overlay，盖在场景之上、HUD 之下 */
        if (currentState == STATE_PLAYING) DrawParasiteSprite();
        if (flashTime > 0.0f)
            memset(pixels, 0xFF, (size_t)renderW * renderH * sizeof(unsigned int));
    } else {
        memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    }

    /* 文字也在这一层，先合成再一次性呈现；弹窗必须在列表文字之后画 */
    if (currentState != STATE_PLAYING) {
        int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);
        DrawMenuPanels();
        DrawMenuText();
        if (popupOpen) {
            DrawPopupPanel();
            DrawPopupText();
        }
        /* 改绑提示框：最顶层，盖住所有菜单 */
        DrawRebindOverlay();
    } else {
        DrawHUD();
        /* 开锁覆盖层画在 HUD 之上：它是当前唯一可操作的东西 */
        if (lockActive) DrawLockOverlay();
    }

    /* 小地图永远画在最上层：它是常驻小窗，菜单/锁盘/暂停都不该把它盖住，
       也不该让它消失 —— 只有玩家自己按 M 才关。 */
    if (showScene && isMapOpen) RenderMap();

    if (g_useDIB) {
        if (renderW == winW && renderH == winH) {
            BitBlt(hdc, 0, 0, winW, winH, g_memDC, 0, 0, SRCCOPY);
        } else {
            SetStretchBltMode(hdc, COLORONCOLOR);
            StretchBlt(hdc, 0, 0, winW, winH, g_memDC, 0, 0, renderW, renderH, SRCCOPY);
        }
    } else {
        StretchDIBits(hdc, 0, 0, winW, winH, 0, 0, renderW, renderH,
                      pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    ReleaseDC(g_hwnd, hdc);
}

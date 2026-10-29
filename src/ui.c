/*
 * Canity — UI 层实现：点阵字库 / 文字 / 菜单与弹窗 / HUD / 最终呈现
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "ui.h"
#include "lang.h"
#include "weapons.h"
#include "shop.h"
#include "bench.h"
#include "lockbox.h"   /* v3.0 锁盘 */   /* v2.7 改造树 */
#include "ui_theme.h"
#include "sprites.h"
#include "relics.h"
#include "game.h"
#include "skills.h"
#include "bestiary.h"
#include "score.h"
#include "entity.h"
#include "render.h"
#include "save.h"

/* 前置声明 */
static void DrawAchievementsWindow(MenuGeo g);

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
/* v1.8：7 种主题。原来只有 4 项，但 themeNames[themeId] 在主题 ≥4 时
   越界读到 NULL → DrawTextWin 解引用空指针 → 直接崩溃。 */
static const char* themeNames[THEME_COUNT] = {
    "BRICK", "PRISON", "HELL", "WARD", "ICE VAULT", "RUST WORKS", "FLESH CAVE"
};
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
    /* v2.2：UI 在渲染空间排版，字形固定 2 倍（12x2=24 渲染像素），
       整帧一起被拉伸到窗口，所以任何分辨率下比例都一致。 */
    return (renderH < 300) ? 1 : 2;
}

static int TextAdvWin(void) {
    /* v3.0：同 TextHeightWin —— 用高度基准，保证字符宽高比不变 */
    return FONT_W * TextScale() * renderH / 458;
}

int TextWidthWin(const char* t) {
    /* v2.0：先过一遍翻译表，再按是否含中文选字形 */
    t = Transl(t);
    if (CjkHasNonAscii(t))
        return CjkTextWidth(t) * renderH / 458;   /* v3.0：同一基准 */
    return (int)strlen(t) * TextAdvWin();
}

/* v1.8：行文本按可用宽度截断。
   字号会随窗口尺寸变化（TextAdvWin），固定字符串在窄窗口/非 16:9 下会撑出面板，
   所以统一在绘制前剪一次，末尾用 ".." 表示省略。 */

/* v2.9：技能树节点名的中文并列表（SkillDef 没中文字段，这里按英文名查） */
static const char* SkillNameZh(const char* en) {
    static const char* tbl[][2] = {
        { "TYPE WORK",       "熟能生巧" },
        { "RELOAD RHYTHM",   "换弹节奏" },
        { "CARTOGRAPHER",    "制图师" },
        { "BREAKER HAND",    "配电手" },
        { "FUSE SMITH",      "保险丝匠" },
        { "SHORTCUT",        "捷径" },
        { "EXECUTIONER",     "处刑人" },
        { "WEATHERMAN",      "观测者" },
        { "LAST RESORT",     "绝境反击" },
        { "SALVAGE",         "打捞" },
    };
    for (int i = 0; i < (int)(sizeof(tbl) / sizeof(tbl[0])); i++)
        if (strcmp(tbl[i][0], en) == 0) return tbl[i][1];
    return en;
}

static void ClampRowText(char* buf, int availPx) {
    int adv = TextAdvWin();
    if (adv <= 0) return;
    /* v2：原来多减了 1 个字符，"THROW POWER" 这种 11 字符的名字明明放得下
       却被截成 "THROW PO.."。这里按实际能容纳的字符数算，
       截断时最后两位写 ".."，总宽度仍然不超 availPx。 */
    /* v2.0 修复：原来按字节数切，中文行尾会切在汉字中间 →
        MultiByteToWideChar 得到 U+FFFD，屏幕上就是一个「▯」。
       现在按 UTF-8 码点数，并且只在完整字符边界处截断。 */
    if (CjkHasNonAscii(buf)) {
        int limit = availPx - adv * 2;          /* 留 ".." 的位置 */
        if (limit < adv * 4) limit = adv * 4;
        while (CjkTextWidth(buf) * winW / renderW > limit) {
            size_t n = strlen(buf);
            if (n == 0) break;
            /* 往回退到上一个 UTF-8 起始字节 */
            size_t k = n - 1;
            while (k > 0 && (buf[k] & 0xC0) == 0x80) k--;
            buf[k] = 0;
            if (k == 0) break;
        }
        /* v2.9：**只有真的截断了才补 ".."**。
           原来无条件补，导致没超宽的每一行中文都以 ".." 结尾
           （改造台描述、遗物三选一都是这样）。 */
        {
            int truncated = (CjkTextWidth(buf) * winW / renderW > limit);
            size_t n2 = strlen(buf);
            if (truncated && n2 + 2 < 128) { buf[n2] = '.'; buf[n2 + 1] = '.'; buf[n2 + 2] = 0; }
        }
        return;
    }
    int maxc = availPx / adv;
    if (maxc < 8) maxc = 8;
    int n = (int)strlen(buf);
    if (n > maxc) {
        buf[maxc - 2] = '.';
        buf[maxc - 1] = '.';
        buf[maxc] = 0;
    }
}

int TextHeightWin(void) {
    /* v3.0：**字号必须和 UIS() 站在同一基准**。
       原来返回 FONT_H*TextScale() *winH/renderH，而 SyncWindowSize
       每帧把 winW/winH 覆盖成 renderW/renderH → 那个比值恒为 1，
       于是字高恒定 24px；但 UIS() 跟 renderH 缩放。
       两者只在 renderH=458（旧的 28 万像素预算）时相等 ——
       现在预算是 90 万（renderH≈711），UIS 系统性大 1.55 倍，
       表现就是「框越大、字相对越小」，高分辨率像空面板。
       改成跟 renderH 比 458 的设计基准，两套比例就永远一致。 */
    return FONT_H * TextScale() * renderH / 458;
}

/* xWin/yWin 用窗口坐标，内部换算到渲染坐标 */
/* v1.9：UI 布局自检 —— 每次画文字都记下它的矩形。
   改动 UI 后按 F9 就会把这一帧的所有文字矩形写入 ui_layout.txt，
   tools/ui_layout_check.py 读它做「矩形是否重叠」的判定 —— 不用猜像素。 */
#define UI_RECT_MAX 256
typedef struct { int x0, y0, x1, y1; char label[24]; } UiRectRec;
static UiRectRec g_uiRects[UI_RECT_MAX];
static int g_uiRectCount = 0;

void UiLayoutBegin(void) { g_uiRectCount = 0; }

static void UiTrack(int x, int y, int w, int h, const char* label) {
    if (g_uiRectCount >= UI_RECT_MAX) return;
    UiRectRec* r = &g_uiRects[g_uiRectCount++];
    r->x0 = x; r->y0 = y; r->x1 = x + w; r->y1 = y + h;
    int i = 0;
    if (label) {
        for (; i < 23 && label[i]; i++) r->label[i] = label[i];
    }
    r->label[i] = 0;
}

void DumpUILayout(const char* path) {
    /* 写到 exe 所在目录，避免受工作目录影响 */
    char full[MAX_PATH];
    GetModuleFileNameA(NULL, full, MAX_PATH);
    char* slash = strrchr(full, '\\');
    if (slash) slash[1] = 0; else full[0] = 0;
    strncat(full, path, MAX_PATH - strlen(full) - 1);

    FILE* f = fopen(full, "w");
    if (!f) {
        /* v2：exe 同目录写不进去时（实测 game\ 下 fopen 失败，存档也是同一个坑），
           退回 %TEMP%，否则自检脚本永远看不到任何东西。 */
        const char* tmp = getenv("TEMP");
        if (!tmp || !*tmp) tmp = getenv("TMP");
        if (tmp && *tmp) {
            snprintf(full, sizeof(full), "%s\\canity_ui_layout.txt", tmp);
            f = fopen(full, "w");
        }
    }
    if (!f) return;
    fprintf(f, "# CANITY UI layout dump  rects=%d  canvas=%dx%d\n", g_uiRectCount, winW, winH);
    for (int i = 0; i < g_uiRectCount; i++) {
        UiRectRec* r = &g_uiRects[i];
        fprintf(f, "%d %d %d %d %s\n", r->x0, r->y0, r->x1, r->y1, r->label);
    }
    fclose(f);
}

void DrawTextWin(int xWin, int yWin, const char* t, unsigned int color) {
    t = Transl(t);          /* v2.0：绘制层统一翻译 */
    UiTrack(xWin, yWin, TextWidthWin(t), TextHeightWin(), t);
    if (CjkHasNonAscii(t)) {
        CjkDrawRaw(xWin * renderW / winW, yWin * renderH / winH, t, color);
        return;
    }
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

void DrawTextCenterWin(int cxWin, int yWin, const char* t, unsigned int color) {
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

/* v2.2 修复：UI 尺寸必须跟着**实际文字高度**缩放，而不是窗口高度。
 *
 * 原来的 bug（用户反馈「窗口一大，UI 全是 bug」）：
 *   UIS()    = v * winH / 600            <- 线性跟窗口高
 *   文字高度 = 24 * winH / renderH        <- renderH 被 PIXEL_BUDGET 卡在 ~400
 * 大窗口下文字高度按 2.7 倍涨、UIS 只按 1.8 倍涨 -> 字比行高还高，
 * 面板、页脚、每一行全部溢出重叠。
 *
 * 现在统一：所有 UI 尺寸 = 设计值 x (实际文字高 / 600 高时的文字高)。 */
#define UI_BASE_TH 31          /* 800x600 时 TextHeightWin() 的实测值 */

int UIS(int v) {
    /* v2.2：UI 在渲染空间排版，基准高度 = 800x600 时的 renderH（458）。
       这样字号（TH=24 渲染像素）和行高永远同比例，窗口多大都一样。 */
    long s = ((long)v * renderH * 2 + 458) / 916;
    if (s < 6) s = 6;
    if (s > 2000) s = 2000;
    return (int)s;
}

/* ============================================================
 * 技能树 v2 —— 节点式布局（3 列 × 4 行 + 底部跨支 1 行）
 *
 * 为什么不用 UIS()：UIS 是按窗口高度线性缩放的，而字号是
 * 12*2*(winH/renderH) —— renderH 又被像素预算压过一道，
 * 所以 800x600 与 1920x1080 下「文字/画布」的比例并不相同。
 * 这里所有尺寸都以 **TH（文字高度）** 为单位，两种分辨率才一致。
 * ============================================================ */
typedef struct {
    RECT panel;                 /* 面板外框 */
    RECT node[SKILL_COUNT];     /* 每个节点的方块（窗口坐标） */
    int  colCX[SKILL_GRID_COLS];
    int  rowCY[SKILL_GRID_ROWS];
    int  nodeW, nodeH;          /* 分支节点尺寸 */
    int  crossH;                /* 跨支节点高度（比分支节点矮一档） */
    int  titleY, curY;          /* 标题 / 页码+货币 两行 */
    int  hdrY, barY;            /* 分支配色标题 / 6 段进度条 */
    int  gridTop, gridBottom;
    int  crossTop, crossBot;
    int  detail1Y, detail2Y;    /* 底部详情两行 */
    int  hintY;                 /* 最底部按键提示 */
    int  padX;
} SkillTreeLayout;

/* 分支主色：只用在标题、进度条、连线上，不参与节点状态色 */
static const unsigned int kBranchColor[4] = {
    0x0040D8FFu,   /* A 潜行 青 */
    0x0050E070u,   /* B 续航 绿 */
    0x00FF6050u,   /* C 反击 红 */
    0x00B080FFu,   /* 跨支   紫 */
};
/* v2.9：分支名走翻译（原来只有英文） */
static const char* kBranchName[4] = { "SILENCE", "ECONOMY", "DELIVERY", "SHAPE" };
static const char* kBranchNameZh[4] = { "沉寂", "节流", "投放", "塑形" };
;

/* 面板内竖向预算，单位 = TH/10。
 *   TEXT 部分（文字行 + 跨支行高度）不吃压缩
 *   GAP  部分在窗口不够高时按比例压缩（k10 = 10 表示不压缩）
 *   WANT 是网格想占的高度（4 个节点 ×1.75TH + 3 个间隔 ×1/3TH） */
#define SST_TEXT10 74
#define SST_GAP10  30
#define SST_WANT10 80

static void SkillTreeCompute(SkillTreeLayout* L) {
    int TH = TextHeightWin();
    if (TH < 8) TH = 8;
    memset(L, 0, sizeof(*L));

    int m = TH / 2;
    L->panel.left   = m;
    L->panel.top    = m;
    L->panel.right  = winW - m;
    L->panel.bottom = winH - m;
    int panelH = L->panel.bottom - L->panel.top;
    if (panelH < TH * 8) panelH = TH * 8;      /* 极小窗口兜底 */

    L->padX = (TH * 4) / 5;
    int innerL = L->panel.left + L->padX;
    int innerR = L->panel.right - L->padX;
    int innerW = innerR - innerL;

    /* 间距压缩系数：窗口越矮压得越多，下限 0.5 */
    int k10;
    {
        int avail10 = (panelH * 10) / TH;      /* 面板高度换成 TH/10 单位 */
        k10 = ((avail10 - SST_TEXT10 - SST_WANT10) * 10) / SST_GAP10;
        if (k10 < 5)  k10 = 5;
        if (k10 > 10) k10 = 10;
    }
#define SST_T(u) ((TH * (u)) / 10)                  /* 不压缩的固定高度 */
#define SST_G(u) ((TH * (u) * k10) / 100)           /* 可压缩的间距 */

    L->titleY = L->panel.top + SST_G(5);
    L->curY   = L->titleY + SST_T(10) + SST_G(2);
    L->hdrY   = L->curY   + SST_T(10) + SST_G(2);
    L->barY   = L->hdrY   + SST_T(10) + SST_G(1);
    L->gridTop = L->barY  + SST_T(2)  + SST_G(3);

    int bottom   = L->panel.bottom - SST_G(5);
    L->hintY     = bottom - SST_T(10);
    L->detail2Y  = L->hintY    - SST_G(2) - SST_T(10);
    L->detail1Y  = L->detail2Y - SST_G(2) - SST_T(10);
    L->crossBot  = L->detail1Y - SST_G(4);
    L->crossH    = SST_T(12);
    L->crossTop  = L->crossBot - L->crossH;
    L->gridBottom = L->crossTop - SST_G(4);

    /* 网格：4 行 + 3 个间隔，多出来的高度居中，不够就夹到上下限 */
    int gridH = L->gridBottom - L->gridTop;
    if (gridH < TH * 2) gridH = TH * 2;
    int gap = TH / 3;
    if (gap < 4) gap = 4;
    int nodeH = (gridH - 3 * gap) / 4;
    if (nodeH > (TH * 22) / 10) nodeH = (TH * 22) / 10;
    if (nodeH < (TH * 13) / 10) nodeH = (TH * 13) / 10;
    L->nodeH = nodeH;

    int usedH = 4 * nodeH + 3 * gap;
    int y0 = L->gridTop + (gridH > usedH ? (gridH - usedH) / 2 : 0);
    for (int r = 0; r < 4; r++) L->rowCY[r] = y0 + r * (nodeH + gap) + nodeH / 2;
    L->rowCY[4] = L->crossTop + L->crossH / 2;

    /* 列：三列平分，列间距 0.35TH（再宽就装不下 "THROW POWER"） */
    int gapX = (TH * 35) / 100;
    if (gapX < 6) gapX = 6;
    int nodeW = (innerW - (SKILL_GRID_COLS - 1) * gapX) / SKILL_GRID_COLS;   /* v2.0：3 列 → 4 列 */
    if (nodeW < TH * 3) nodeW = TH * 3;
    L->nodeW = nodeW;
    for (int c = 0; c < SKILL_GRID_COLS; c++)
        L->colCX[c] = innerL + gapX + nodeW / 2 + c * (nodeW + gapX);

    for (int id = 0; id < SKILL_COUNT; id++) {
        int c = SkillNodeCol(id), r = SkillNodeRow(id);
        int h = (r == 4) ? L->crossH : nodeH;
        RECT rc;
        rc.left   = L->colCX[c] - nodeW / 2;
        rc.right  = rc.left + nodeW;
        rc.top    = L->rowCY[r] - h / 2;
        rc.bottom = rc.top + h;
        L->node[id] = rc;
    }
#undef SST_T
#undef SST_G
}

/* ============================================================
 * 主菜单布局（v2.2 重做）
 *
 * 原来的问题：面板固定 520 宽，8~10 行按 46px 硬排，
 * "Achievements" 12 个字符几乎顶满行宽，货币和 BEST LEVEL 还露在面板外面，
 * 整页看起来很挤。
 *
 * 现在：面板按文字高度定宽（26 TH），行高在 1.2~1.75 TH 之间自适应，
 * 标题区 / 行区 / 页脚三块分开，页脚画在面板里面。
 * ============================================================ */
typedef struct {
    RECT panel;
    int  count;
    int  titleY, subY;
    int  firstY, rowH;
    int  footRuleY, footY;
    int  padX;
} MainMenuLayout;

/* 主菜单行数（含不固定的 Continue / Endless） */
static int MainMenuRowCount(void);

static void MainMenuCompute(MainMenuLayout* L) {
    int TH = TextHeightWin();
    if (TH < 8) TH = 8;
    memset(L, 0, sizeof(*L));

    int n = MainMenuRowCount();
    if (n < 1) n = 1;
    L->count = n;
    L->padX = TH;

    int panelW = TH * 18;
    if (panelW > winW - TH) panelW = winW - TH;
    if (panelW < TH * 12) panelW = TH * 12;

    int titleH  = (TH * 24) / 10;  /* CANITY + 副标题，留够上方呼吸位 */
    int gapTop  = (TH * 8) / 10;
    int gapRows = (TH * 11) / 10;  /* 副标题和第一行之间别贴在一起 */
    int gapFoot = (TH * 6) / 10;
    int footH   = (TH * 3) / 2;    /* 页脚：一条细线 + 一行字 */

    int rowH = (TH * 135) / 100;
    int bottomPad = (TH * 6) / 10;      /* 页脚和面板下边框之间的呼吸位 */
    int panelH = titleH + gapTop + gapRows + n * rowH + gapFoot + footH + bottomPad;
    int maxH = winH - TH;

    if (panelH > maxH) {
        /* 行数多（10 行）时压行高，文字本身不缩 */
        int fixed = titleH + gapTop + gapRows + gapFoot + footH + bottomPad;
        int avail = maxH - fixed;
        rowH = (n > 0) ? avail / n : rowH;
        if (rowH < (TH * 11) / 10) rowH = (TH * 11) / 10;
        panelH = fixed + n * rowH;
        if (panelH > maxH) panelH = maxH;
    }
    L->rowH = rowH;

    int left = (winW - panelW) / 2;
    L->panel.left   = left;
    L->panel.right  = left + panelW;
    L->panel.top    = (winH - panelH) / 2;
    L->panel.bottom = L->panel.top + panelH;

    L->titleY = L->panel.top + gapTop;
    L->subY   = L->titleY + TH + TH / 3;
    L->firstY = L->titleY + titleH;

    L->footRuleY = L->panel.bottom - bottomPad - footH;
    L->footY     = L->footRuleY + (footH - TH) / 2;
}

/* 主菜单行数（与 MenuLabel 里的那张表保持一致）：Continue / Endless 会增减一行 */
static int MainMenuRowCount(void) {
    return MainMenuCount();
}

/* ============================================================
 * 成就页布局（v2.3 重做）
 *
 * 这一页原来的毛病：
 *   ① 每行是 "[*] NAME  (3/5)" 拼成一整串居中画，长名字 + 大字号会顶出面板
 *   ② 说明是跟着鼠标飘的 tooltip，位置用的是**客户区像素**却当逻辑画布坐标用，
 *      窗口一大 tooltip 就飘到屏幕外（用户截图里那条横出来一截就是这个）
 *   ③ 面板固定 600x400，和 Hub / 技能树的风格完全不统一
 *
 * 现在：三列（状态框 / 名称 / 进度）+ 底部固定说明条（自动折行），
 * 尺寸全部按文字高度算，任何窗口都不出界。
 * ============================================================ */
typedef struct {
    RECT panel;
    int  count;
    int  visible;
    int  titleY;
    int  firstY, rowH;
    int  stripRuleY, stripY1, stripY2;
    int  padX;
} AchGeo;

static void AchCompute(AchGeo* A) {
    int TH = TextHeightWin();
    if (TH < 8) TH = 8;
    memset(A, 0, sizeof(*A));
    A->count = ACH_COUNT + 1;          /* + Back */
    A->padX = TH;

    int panelW = TH * 24;
    if (panelW > winW - TH) panelW = winW - TH;
    if (panelW < TH * 12) panelW = TH * 12;

    int maxH = winH - TH;
    int titleH   = TH;
    int gapTop   = (TH * 6) / 10;
    int gapRows  = (TH * 6) / 10;
    int gapStrip = (TH * 4) / 10;
    int stripH   = (TH * 24) / 10;     /* 两行说明 */
    int gapBot   = (TH * 5) / 10;
    int fixed = titleH + gapTop + gapRows + gapStrip + stripH + gapBot;

    int visible = A->count;
    if (visible > ACH_VISIBLE_ROWS) visible = ACH_VISIBLE_ROWS;
    int rowH = (TH * 13) / 10;
    /* 窗口不够高就少显示几行，不把文字压扁 */
    while (visible > 5 && fixed + visible * rowH > maxH) visible--;
    A->visible = visible;

    int avail = maxH - fixed;
    rowH = (visible > 0) ? avail / visible : rowH;
    if (rowH > (TH * 15) / 10) rowH = (TH * 15) / 10;
    if (rowH < (TH * 11) / 10) rowH = (TH * 11) / 10;
    A->rowH = rowH;

    int panelH = fixed + visible * rowH;
    if (panelH > maxH) panelH = maxH;

    int left = (winW - panelW) / 2;
    A->panel.left = left;
    A->panel.right = left + panelW;
    A->panel.top = (winH - panelH) / 2;
    A->panel.bottom = A->panel.top + panelH;

    A->titleY = A->panel.top + gapTop;
    A->firstY = A->titleY + titleH + gapRows;
    A->stripRuleY = A->firstY + visible * rowH + gapStrip;
    A->stripY1 = A->stripRuleY + (TH * 3) / 10;
    A->stripY2 = A->stripY1 + TH;
}

/* 按空格折行：拆成至多 maxLines 行，每行不超过 availPx；返回实际行数 */
static int WrapText(const char* text, int availPx, char lines[][96], int maxLines) {
    int n = 0;
    char cur[96] = "";
    const char* p = text;
    while (*p && n < maxLines) {
        const char* sp = strchr(p, ' ');
        int len = sp ? (int)(sp - p) : (int)strlen(p);
        char word[48];
        if (len > 47) len = 47;
        memcpy(word, p, (size_t)len);
        word[len] = 0;
        char cand[96];
        if (cur[0]) snprintf(cand, sizeof(cand), "%s %s", cur, word);
        else        snprintf(cand, sizeof(cand), "%s", word);
        if (cur[0] && TextWidthWin(cand) > availPx) {
            snprintf(lines[n++], 96, "%s", cur);
            snprintf(cur, sizeof(cur), "%s", word);
        } else {
            snprintf(cur, sizeof(cur), "%s", cand);
        }
        p += len;
        while (*p == ' ') p++;
    }
    if (cur[0] && n < maxLines) snprintf(lines[n++], 96, "%s", cur);
    return n;
}

static MenuGeo MakeGeo(const char* title, int count, int rowH, int pad, int width, int titlePx, int stateIndex) {
    MenuGeo g;
    memset(&g, 0, sizeof(g));
    g.title = title;
    g.count = count;
    g.rowH = rowH;

    int titleH = titlePx;
    /* v2.9：**行高兜底** —— 原来 rowH 用 UIS 缩、文字高用 TH 缩，
       两套比例反向，窗口一大行就压字（报告 严重 #1）。
       这里保证行高至少容得下一行文字。 */
    {
        int minRow = TextHeightWin() + UIS(4);
        if (rowH < minRow) rowH = minRow;
    }
    int innerH = titleH + UIS(24) + count * rowH;
    /* v2.4：安全屋多留一条校准条的位置（画在面板底部空白区） */
    if (stateIndex == STATE_SAFEHOUSE) innerH += TextHeightWin() + UIS(40);
    int panelH = pad * 2 + innerH;
    /* v1.8：行数多时面板会长过窗口（技能树 16 行在 640px 窗口里就溢出了）。
       这里把高度夹到窗口内，再按夹后的高度重算可见行数 —— 剩下的靠滚动。 */
    {
        int maxH = winH - UIS(40);
        if (maxH < UIS(240)) maxH = UIS(240);
        if (panelH > maxH) panelH = maxH;
    }
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
    /* v2.7：stateIndex 可能是 -1（弹窗几何），原来直接下标读 → 越界读 */
    g.scrollOffset = (stateIndex >= 0 && stateIndex < 16)
                     ? menuScrollOffsets[stateIndex] : 0;
    /* 修正偏移：不能超过最大值 */
    int maxOffset = g.count - g.maxVisibleRows;
    if (maxOffset < 0) maxOffset = 0;
    if (g.scrollOffset > maxOffset) g.scrollOffset = maxOffset;

    return g;
}

MenuGeo GetMenuGeo(void) {
    switch (currentState) {
        case STATE_MAIN_MENU: {
            /* v2.2：主菜单用自己的布局（宽面板 + 页脚在面板内） */
            MainMenuLayout M;
            MainMenuCompute(&M);
            MenuGeo g;
            memset(&g, 0, sizeof(g));
            g.title = "CANITY";
            g.count = M.count;
            g.rowH = M.rowH;
            g.panel = M.panel;
            g.titleY = M.titleY;
            g.firstY = M.firstY;
            g.maxVisibleRows = M.count;   /* 全部可见，不滚动 */
            g.scrollOffset = 0;
            return g;
        }
        case STATE_HUB:
            if (hubPage == 1) {
                /* v2 技能树：面板几何由技能树自己的布局决定（全屏式大面板），
                   不走 MakeGeo 的"列表行"那套 —— 节点不是行。 */
                SkillTreeLayout L;
                SkillTreeCompute(&L);
                MenuGeo g;
                memset(&g, 0, sizeof(g));
                g.title = "SKILL TREE";
                g.count = SKILL_COUNT;
                g.rowH = L.nodeH;
                g.panel = L.panel;
                g.titleY = L.titleY;
                g.firstY = L.gridTop;
                g.maxVisibleRows = SKILL_COUNT;   /* 15 个节点全部可见，不滚动 */
                g.scrollOffset = 0;
                return g;
            }
            return MakeGeo("THE HUB", HUB_ROWS, UIS(50), UIS(30), UIS(700), UIS(46), STATE_HUB);
        case STATE_SKILL_PICK:
            return MakeGeo(pickMode == 1 ? Tr("CHOOSE A RELIC", "选择遗物")
                                         : Tr("CHOOSE ONE", "三选一"),
                           INRUN_CHOICES, UIS(48), UIS(30), UIS(780), UIS(56), STATE_SKILL_PICK);
        case STATE_WEAPON_PICK: return MakeGeo(Tr("CHOOSE WEAPON", "选择初始武器"), WPN_COUNT, UIS(52), UIS(32), UIS(760), UIS(60), STATE_WEAPON_PICK);
        /* v2.9：安全屋不画大红标题 —— 顶部页签已经说清是哪个页面，
           那行「SAFE HOUSE - SHOP」横在中间会把改造树整个遮住（用户截图）。
           传空串让 MakeGeo 跳过标题绘制。 */
        case STATE_SAFEHOUSE: return MakeGeo("", ShopRowCount(), UIS(50), UIS(32), UIS(720), UIS(58), STATE_SAFEHOUSE);   /* 高度在 MakeGeo 里另算 */
        case STATE_BESTIARY:  return MakeGeo("BESTIARY", BESTIARY_COUNT + 1, UIS(38), UIS(26), UIS(900), UIS(46), STATE_BESTIARY);
        case STATE_HELP:      return MakeGeo(helpPage == 1 ? Tr("HELP  -  RULES", "帮助  -  规则") : Tr("HELP  -  CONTROLS", "帮助  -  按键"),
                                            HELP_PAGE_ROWS, UIS(29), UIS(22), UIS(1000), UIS(30), STATE_HELP);
        case STATE_PAUSED:    return MakeGeo("PAUSED", 3, UIS(56), UIS(38), UIS(520), UIS(56), STATE_PAUSED);
        case STATE_GAMEOVER:  return MakeGeo(gameWon ? "YOU ESCAPED" : "YOU LOST YOUR MIND", 3, UIS(48), UIS(34), UIS(660), UIS(40), STATE_GAMEOVER);
        case STATE_SETTINGS:  return MakeGeo(PageTitle(settingsPage), SettingsRowCount(), UIS(44), UIS(30), UIS(680), UIS(44), STATE_SETTINGS);
        case STATE_ACHIEVEMENTS: {
            /* v2.3: geometry and DrawAchievementsWindow share AchCompute, so
               the clickable rows and the drawn rows can never drift apart. */
            AchGeo A;
            AchCompute(&A);
            MenuGeo g;
            memset(&g, 0, sizeof(g));
            g.title = "ACHIEVEMENTS";
            g.count = A.count;
            g.rowH = A.rowH;
            g.panel = A.panel;
            g.titleY = A.titleY;
            g.firstY = A.firstY;
            g.maxVisibleRows = A.visible;

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

extern int safehouseHoverRow;   /* v3.0：鼠标悬停行（定义在 game.c） */
extern int safehouseHoverTab;   /* v3.0：悬停页签 */

RECT RowRect(MenuGeo g, int i) {
    RECT r;
    /* v3.0：安全屋的页签挪到了左侧竖排，内容行必须让开那一条 ——
       否则第一列文字会被 sidebar 盖住。 */
    int leftPad = UIS(18);
    if (currentState == STATE_SAFEHOUSE) {
        int tx0, ty0, tx1, ty1;
        PageTabRect(0, &tx0, &ty0, &tx1, &ty1);
        leftPad = (tx1 - g.panel.left) + UIS(30);
    }
    r.left = g.panel.left + leftPad;
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
        return hasSave ? 12 : 11;   /* v2.2：+战役 +每日 */
    }
    return hasSave ? 11 : 10;
}

static int MenuItemIndex(void) {
    if (currentState == STATE_SETTINGS) return settingsSelection;
    if (currentState == STATE_HUB) return hubSelection;
    if (currentState == STATE_SKILL_PICK) return skillPickSel;
    if (currentState == STATE_WEAPON_PICK) return weaponPickSel;
    if (currentState == STATE_SAFEHOUSE) return shopRow;
    if (currentState == STATE_ACHIEVEMENTS) return achSelection;
    if (currentState == STATE_BESTIARY) return bestiarySel;
    return menuSelection;
}

static const char* MenuLabel(int i) {
    static char buf[128];
    switch (currentState) {
        case STATE_MAIN_MENU: {
            static const char* withSave[] = {"Continue", "New Run", "Hub", "Achievements", "Bestiary", "Help", "Settings", "Campaign", "Daily Run", "Training", "Quit"};
            /* v2.0 中文菜单 */
            static const char* withSaveZh[] = {"继续", "新游戏", "大厅", "成就", "图鉴", "帮助", "设置", "战役", "每日挑战", "训练营", "退出"};
            static const char* noSaveZh[]   = {"新游戏", "大厅", "成就", "图鉴", "帮助", "设置", "战役", "每日挑战", "训练营", "退出"};
            static const char* withEndlessZh[]   = {"继续", "新游戏", "无尽", "大厅", "成就", "图鉴", "帮助", "设置", "战役", "每日挑战", "训练营", "退出"};
            static const char* noSaveEndlessZh[] = {"新游戏", "无尽", "大厅", "成就", "图鉴", "帮助", "设置", "战役", "每日挑战", "训练营", "退出"};
            static const char* noSave[] = {"New Run", "Hub", "Achievements", "Bestiary", "Help", "Settings", "Campaign", "Daily Run", "Training", "Quit"};
            static const char* withEndless[] = {"Continue", "New Run", "Endless", "Hub", "Achievements", "Bestiary", "Help", "Settings", "Campaign", "Daily Run", "Training", "Quit"};
            static const char* noSaveEndless[] = {"New Run", "Endless", "Hub", "Achievements", "Bestiary", "Help", "Settings", "Campaign", "Daily Run", "Training", "Quit"};
            /* Endless 解锁条件：通关过第 10 层 */
            int endlessUnlocked = (bestLevel >= 10) || gameWon;
            int zh = (g_lang == LANG_ZH);
            if (endlessUnlocked) {
                if (zh) return hasSave ? withEndlessZh[i] : noSaveEndlessZh[i];
                return hasSave ? withEndless[i] : noSaveEndless[i];
            }
            if (zh) return hasSave ? withSaveZh[i] : noSaveZh[i];
            return hasSave ? withSave[i] : noSave[i];
        }
        case STATE_PAUSED: {
            static const char* s[] = {"Resume", "Settings", "Main Menu"};
            static const char* szh[] = {"继续游戏", "设置", "主菜单"};
            return (g_lang == LANG_ZH) ? szh[i] : s[i];
        }
        case STATE_BESTIARY: {
            static char bb[192];
            if (i >= BESTIARY_COUNT) return "Back";
            if (!BestiaryIsSeen(i)) {
                sprintf(bb, "???   [LOCKED]");
            } else {
                sprintf(bb, "%s  [%s]", bestiary[i].name, bestiary[i].tag);
            }
            return bb;
        }
        case STATE_HELP: {
            /* 只放玩法提示，不再列键位 —— 键位可在 Settings 里改绑，
               静态表一改绑就过时；真正的键位看 Settings → Movement / Actions / Menu Keys。
               行数要控制在 14 行以内：面板高度 = 14×28 + 标题 + 内边距
               ≈ 500，在 600 高的窗口里才留得下边距。 */
            /* v1.8：前 10 行是按键说明书，直接读当前绑定 ——
               新玩家最需要的就是"我该按哪个键"，而且改键之后这里会跟着变。 */
            static char kb[128];
            int ridx = i + (helpPage == 1 ? HELP_PAGE_ROWS : 0);   /* v2.2：分页 */
            if (ridx >= 0 && ridx < HELP_KEY_ROWS) {
                switch (ridx) {
                case 0: sprintf(kb, "%s:  %s %s %s %s", Tr("MOVE","移动"),
                                VkName(g_bindings[ACT_MOVE_FWD]), VkName(g_bindings[ACT_MOVE_LEFT]),
                                VkName(g_bindings[ACT_MOVE_BACK]), VkName(g_bindings[ACT_MOVE_RIGHT])); break;
                case 1: sprintf(kb, "%s: %s  (噪声)", Tr("SPRINT","冲刺"), VkName(g_bindings[ACT_SPRINT])); break;
                case 2: sprintf(kb, "%s: 长按 %s  (噪声)", Tr("SEARCH","翻找"), VkName(g_bindings[ACT_INTERACT])); break;
                case 3: sprintf(kb, "%s: 长按 %s  (+32)", Tr("CRANK","摇电"), VkName(g_bindings[ACT_CRANK])); break;
                case 4: sprintf(kb, "%s: %s", Tr("LIGHT","手电"), VkName(g_bindings[ACT_FLASHLIGHT])); break;
                case 5: sprintf(kb, "%s: %s (全图)  长按 TAB (小图)  -10/秒", Tr("MAP","地图"), VkName(g_bindings[ACT_MAP])); break;
                case 6: sprintf(kb, "%s: %s", Tr("THROW","投掷"), VkName(g_bindings[ACT_THROW])); break;
                case 7: sprintf(kb, "%s: %s %s %s  (滚轮: 全部)", Tr("SLOT","选槽"),
                                VkName(g_bindings[ACT_SELECT_1]), VkName(g_bindings[ACT_SELECT_2]),
                                VkName(g_bindings[ACT_SELECT_3])); break;
                case 8: sprintf(kb, "%s: %s  (敲击制造噪音)", Tr("KNOCK","敲击"),
                                VkName(g_bindings[ACT_CANCEL_CHARGE])); break;
                default: sprintf(kb, "%s: %s  闪避: %s", Tr("PAUSE","暂停"),
                                VkName(g_bindings[ACT_PAUSE]), VkName(g_bindings[ACT_DODGE])); break;
                }
                {
                    MenuGeo hg = GetMenuGeo();
                    ClampRowText(kb, (hg.panel.right - hg.panel.left) - UIS(44));
                }
                return kb;
            }
            static const char* helps[] = {
                "SEARCH CONTAINERS FOR FUSES,",   /* v2.9：拆两行，原来后半句被截掉 */
    "THEN POWER THE BREAKER.",
                "",
                "RUNNING AND SEARCHING MAKE NOISE.",
                "WALKING IS QUIET, BUT 5S OF IT STARTS TO WHISPER.",
                "",
                "BOTTLE: THROW IT AT A MONSTER TO HIT IT.",
                "  THROW A BOTTLE TO MAKE NOISE AND LURE IT AWAY.",
                "  GIVES AWAY YOUR POSITION.",
                "",
                "FACE JUMPER: PRESS SPACE TO DODGE IT.",
                "  IF IT LANDS ON YOUR FACE, PRESS THE KEY.",
                "  MISS BOTH AND YOU LOSE SANITY."
            };
            const int helpRows = (int)(sizeof(helps) / sizeof(helps[0]));
            {
                int j = i - HELP_KEY_ROWS;
                if (j >= 0 && j < helpRows) {
                    static char hb2[192];
                    sprintf(hb2, "%s", helps[j]);
                    MenuGeo hg = GetMenuGeo();
                    ClampRowText(hb2, (hg.panel.right - hg.panel.left) - UIS(44));
                    return hb2;
                }
            }
            return "";
        }
        case STATE_SAFEHOUSE: {
            return ShopRowText(i);
        }
        case STATE_WEAPON_PICK: {
            static char wb[128];
            static const char* resName[WRES_COUNT] = { "STAMINA", "BATTERY", "SANITY", "ENERGY" };   /* v2.3：三把武器都烧能量 */
            if (i < 0 || i >= WPN_COUNT) return "";
            sprintf(wb, "%s   %s -%.0f   LV%d", Tr(weaponDefs[i].dispName, weaponDefs[i].nameZh),
                    resName[weaponDefs[i].resource], weaponDefs[i].cost,
                    weaponRank[i]);
            return wb;
        }
        case STATE_SKILL_PICK: {
            static char pb[128];
            if (i < 0 || i >= INRUN_CHOICES || skillPickOptions[i] < 0) return "";
            int id = skillPickOptions[i];
            if (pickMode == 1) {
                const RelicDef* rd = &relicDefs[id];
                sprintf(pb, "%s: %s", Tr(rd->nameEn, rd->nameZh),
                        Tr(rd->descEn, rd->descZh));
            } else {
                /* v2.9：三选一用中文（原来 24 条技能整页英文） */
        sprintf(pb, "%s: %s", Tr(inRunDefs[id].name, inRunDefs[id].nameZh),
                Tr(inRunDefs[id].desc, inRunDefs[id].descZh));
            }
            {
                MenuGeo g = GetMenuGeo();
                ClampRowText(pb, (g.panel.right - g.panel.left) - UIS(56));
            }
            return pb;
        }
        case STATE_HUB: {
            static char hb[128];
            if (hubPage == 1) {
                /* v1.8 技能树页 */
                if (i >= SKILL_COUNT) return "Back";
                const SkillDef* d = &skillDefs[i];
                int r = skillRank[i];
                int cost = SkillCost(i);
                if (r >= d->maxRank) {
                    sprintf(hb, "%s   MAX", d->name);
                } else if (d->isKeystone) {
                    int pts = (d->branch < 3) ? SkillBranchPoints(d->branch) : 0;
                    if (r > 0) {
                        sprintf(hb, "*%s  OWNED", d->name);
                    } else if (activeKeystone >= 0) {
                        sprintf(hb, "*%s  LOCKED (OTHER ULT)", d->name);
                    } else if (d->branch < 3 && pts < SKILL_POINTS_FOR_KEYSTONE) {
                        sprintf(hb, "*%s  ULT %c %d/%d", d->name,
                                'A' + d->branch, pts, SKILL_POINTS_FOR_KEYSTONE);
                    } else {
                        sprintf(hb, "*%s  ULT %dSH %dC", d->name, cost, d->coreCost);
                    }
                } else if (cost < 0) {
                    sprintf(hb, "%s  MAX", d->name);
                } else {
                    sprintf(hb, "%s  %d/%d  %dSH", d->name, r, d->maxRank, cost);
                }
                {
                    MenuGeo g = GetMenuGeo();
                    ClampRowText(hb, (g.panel.right - g.panel.left) - UIS(56));
                }
                return hb;
            }
            if (i >= UPGRADE_COUNT) return "Back";
            int cost = UpgradeCost(i);
            if (cost < 0) sprintf(hb, "%s   LV%d/%d   MAX", UpgradeName(i), UpgradeLevel(i), UPG_MAX);
            else sprintf(hb, "%s   LV%d/%d   %d SHARDS", UpgradeName(i), UpgradeLevel(i), UPG_MAX, cost);
            return hb;
        }
        case STATE_GAMEOVER: return "Return to Menu";
        case STATE_ACHIEVEMENTS: {
            if (i >= ACH_COUNT) return "Back";
            static char ab[256];
            if (g_achievements[i].unlocked) {
                sprintf(ab, "[*] %s  (%d/%d)", g_achievements[i].name,
                        g_achievements[i].progress, g_achievements[i].target);
            } else {
                /* 简短显示：只显示名字和进度，描述放在右侧列（通过 DrawMenuText 处理） */
                sprintf(ab, "[ ] %s  %d/%d  +1C/250S", g_achievements[i].name,
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
                    /* v2.9：选项值也走翻译（原来 LOW/MEDIUM/HIGH 全英文） */
                    sprintf(buf, "%s: %s", it->name,
                            Tr(it->options[dispVal], it->options[dispVal]));
                } else {
                    sprintf(buf, "%s: %s", it->name,
                            Tr(it->options[0], it->options[0]));
                }
            }
            return buf;
        }
        default: return "";
    }
}

/* 窗口坐标 -> 渲染坐标的矩形填充（半透明） */
void BlendRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha) {
    int rx0 = x0 * renderW / winW, rx1 = x1 * renderW / winW;
    /* v2.1：窗口像素折回渲染缓冲时，1px 的线在 1080p/1440p 会算成 0 宽直接消失。
       兜底保证至少 1 个渲染像素。 */
    if (rx1 <= rx0) rx1 = rx0 + 1;
    if (x1 - x0 >= 1 && rx1 - rx0 < 1) rx1 = rx0 + 1;
    int ry0 = y0 * renderH / winH, ry1 = y1 * renderH / winH;
    if (ry1 <= ry0) ry1 = ry0 + 1;   /* v2.1：同上的纵向兜底 */
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

void FrameRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha, int thick) {
    BlendRectWin(x0, y0, x1, y0 + thick, color, alpha);
    BlendRectWin(x0, y1 - thick, x1, y1, color, alpha);
    BlendRectWin(x0, y0, x0 + thick, y1, color, alpha);
    BlendRectWin(x1 - thick, y0, x1, y1, color, alpha);
}

/* 主面板（不含弹窗）：弹窗必须画在列表文字之上，否则文字会压在弹窗上 */

/* v2.4：安全屋的校准条。
   一根横条，滑块左右扫，按空格锁定；中间亮区是普通区，更窄的白色是完美区。 */

/* v2.5：校准条本体（位置由调用方给）。
   拆出来是因为「游戏内按空格开始小游戏」时，安全屋面板根本没画，
   原来画在面板里的条就永远看不到。 */
static void DrawCalibBar(int bx, int by, int barW, int barH) {
    if (barW < UIS(120)) return;
    BlendRectWin(bx, by, bx + barW, by + barH, 0x00101418, 0.92f);
    {
        float zsc = CalibZoneScale();   /* v2.6：随层数缩小 */
        int zx0 = bx + (int)((calibZoneC - CALIB_ZONE_W * zsc) * barW);
        int zx1 = bx + (int)((calibZoneC + CALIB_ZONE_W * zsc) * barW);
        BlendRectWin(zx0, by, zx1, by + barH, 0x00307A4A, 0.85f);
        int px0 = bx + (int)((calibZoneC - CALIB_PERFECT_W * zsc) * barW);
        int px1 = bx + (int)((calibZoneC + CALIB_PERFECT_W * zsc) * barW);
        BlendRectWin(px0, by, px1, by + barH, 0x00E8F0A0, 0.95f);
    }
    {
        int mx = bx + (int)(calibPos * barW);
        int mw = UIS(4);
        BlendRectWin(mx - mw / 2, by - UIS(3), mx + mw / 2, by + barH + UIS(3),
                     calibActive ? 0x00FFFFFFu : 0x00606060u, 0.98f);
    }
    FrameRectWin(bx, by, bx + barW, by + barH, 0x00708090u, 0.8f, 1);
}

static void DrawCalibStrip(MenuGeo g) {
    /* v2.4：画在面板内部的**底部空白区**（面板已为它加高）。
       画在面板里才能保证每帧被不透明面板盖住，不会留残影。 */
    int pad = UIS(34);
    int barW = (g.panel.right - g.panel.left) - pad * 2;
    if (barW < UIS(160)) return;
    int bx = g.panel.left + pad;
    int barH = UIS(16);
    int by = g.panel.bottom - UIS(30) - barH;
    char line[128];

    /* 标题行：剩余次数 / 冷却 */
    if (calibActive) {
        sprintf(line, "%s", Tr("CALIBRATION - PRESS SPACE TO LOCK", "校准中 - 按空格锁定"));
    } else if (calibCooldown > 0.0f) {
        sprintf(line, "%s   %.0fs", Tr("CALIBRATION   COOLING", "校准   冷却中"), calibCooldown);
    } else {
        sprintf(line, "%s   [SPACE]", Tr("CALIBRATION", "校准"));
    }
    /* v2.4：先铺一层不透明底 —— 否则每帧重画会在同一位置叠字（残影） */
    BlendRectWin(bx - UIS(8), by - TextHeightWin() - UIS(8),
                 bx + barW + UIS(8), by + barH + UIS(6), 0x000A0E14, 0.96f);

    DrawTextWin(bx, by - TextHeightWin() - UIS(4), line,
                calibActive ? 0x00FFE060u : (calibAttempts > 0 && calibCooldown <= 0.0f ? 0x00A0D0F0u : 0x00808080u));

    DrawCalibBar(bx, by, barW, barH);   /* v2.5：条本体抽成共用函数 */

    /* 结果提示 */
    if (calibResultTimer > 0.0f && calibResult) {
        const char* msg = "";
        unsigned int col = 0x00808080u;
        if (calibResult == 1) { msg = Tr("PERFECT", "完美"); col = 0x00FFE060u; }
        else if (calibResult == 2) { msg = Tr("CALIBRATED", "成功"); col = 0x0080E080u; }
        else { msg = Tr("MISSED", "失手"); col = 0x00FF6040u; }
        DrawTextWin(bx + barW - TextWidthWin(msg), by - TextHeightWin() - UIS(4), msg, col);
    }
}


/* v2.8：局内改造树 —— **可平移缩放的画布**（思维导图式）。
   节点用世界坐标，绘制时统一变换：
       sx = (worldX - treeVX) * treeVK + winW/2
   好处：窗口多小都不会崩，节点数量可以无限加。

   顶部一排按钮 = 原来的四页（商店/改装/改造树/休息），
   点一下或按左右键切页，不用记 TAB。 */

/* 页签按钮：几何由 DrawPageTabs 计算，ClickPageTabs 复用同一套 */
void PageTabRect(int idx, int* x0, int* y0, int* x1, int* y1) {
    /* v3.0：页签改成**左侧竖排**。
       调研结论：横向 5 个页签在窄面板里必然挤（要么截字、要么压余额），
       商店类 UI 的通用解法是竖排 sidebar —— 宽度压力归零，
       页签再多也只是往下排，不需要改布局。

       布局：
          面板左边 26% 宽的一列放 5 个页签
          右边 74% 是内容区 */
    int th = TextHeightWin();
    int panelW = winW - UIS(32);
    int sideW  = panelW * 26 / 100;
    if (sideW < UIS(140)) sideW = UIS(140);
    if (sideW > UIS(220)) sideW = UIS(220);

    /* v3.0：**必须贴着面板左边界** ——
       原来用 UIS(16)+UIS(10) 这种绝对定位，而安全屋面板是居中的，
       1280x720 / 1080p 下整列 5 个页签挂在面板框外 32px（兼容性审查 #2）。 */
    int bx0;
    {
        MenuGeo pg = GetMenuGeo();
        bx0 = pg.panel.left + UIS(14);
    }
    int tabH = th + UIS(14);
    int gap  = UIS(8);
    int by0 = UIS(16) + th + UIS(28) + idx * (tabH + gap);

    *x0 = bx0;
    *y0 = by0;
    *x1 = bx0 + sideW;
    *y1 = by0 + tabH;
}



static void DrawPageTabs(void) {
    static const char* names[SAFEHOUSE_PAGES] = { "SHOP", "BENCH", "TREE", "REST", "UPGRADE" };
    static const char* namesZh[SAFEHOUSE_PAGES] = { "商店", "改装", "改造树", "休息", "强化" };

    int th = TextHeightWin();
    int x0, y0, x1, y1;
    PageTabRect(0, &x0, &y0, &x1, &y1);

    /* 左侧竖分隔线：把 sidebar 和内容区切开 */
    int sepX = x1 + UIS(10);
    BlendRectWin(sepX, y0 - UIS(8), sepX + 1, winH - UIS(40), 0x00C8B060u, 0.45f);

    for (int i = 0; i < SAFEHOUSE_PAGES; i++) {
        PageTabRect(i, &x0, &y0, &x1, &y1);
        int active = (shopPage == i);
        int hover  = (safehouseHoverTab == i);

        BlendRectWin(x0, y0, x1, y1,
                     active ? 0x00C8A040 : (hover ? 0x00303838 : 0x00161C1C),
                     active ? 0.95f : 0.85f);
        FrameRectWin(x0, y0, x1, y1,
                     active ? 0x00FFE070u : (hover ? 0x00A0C0C0u : 0x00505858u),
                     0.9f, active ? 2 : 1);

        /* 选中时左侧加一根粗竖条，扫一眼就知道在哪一页 */
        if (active) {
            BlendRectWin(x0 - UIS(6), y0, x0 - UIS(1), y1, 0x00FFE070u, 0.95f);
        }

        const char* t = Tr(names[i], namesZh[i]);
        DrawTextWin(x0 + UIS(12), y0 + (y1 - y0 - th) / 2, t,
                    active ? 0x00100C08u : 0x0090A0A0u);
    }
}


static void DrawBenchTree(MenuGeo g) {
    (void)g;
    int th = TextHeightWin();

    /* v2.9：第一次进树页自动居中 —— 原来 treeVX/VY 默认 0，
       第一列框 -35..115，左半截在屏外（截图可见）。 */
    {
        static int treeCentred = 0;
        if (!treeCentred) {
            treeCentred = 1;
            BenchFocusNode(shopNodeSel);
        }
        if (currentState != STATE_SAFEHOUSE) treeCentred = 0;
    }
    int M = UIS(16);

    /* ---- 背景 ---- */
    BlendRectWin(M, M, winW - M, winH - M, 0x000A0E14, 0.97f);
    FrameRectWin(M, M, winW - M, winH - M, 0x00C8B060u, 0.9f, 1);

    /* ---- 顶部：页签 + 余额 ---- */
    DrawPageTabs();
    {
        static char sb[64];
        sprintf(sb, "SHARDS %d", shards);
        DrawTextWin(winW - M - UIS(16) - TextWidthWin(sb), UIS(14), sb, 0x00E8C040u);
        /* 缩放指示 */
        sprintf(sb, "%.1fx", treeVK);
        DrawTextWin(winW - M - UIS(16) - TextWidthWin(sb), UIS(14) + th + UIS(6), sb, 0x009AA8A8u);
    }

    /* ---- 节点（世界坐标 -> 屏幕） ---- */
    /* v2.9：框宽**从世界间距推**，不再从文字高推 ——
       从文字高推的话，窗口一大 TH 变大，框也跟着变宽，
       最后宽到和列间距一样，相邻节点就贴在一起（截图里 MAGAZI|HIDE）。
       现在：框宽 = 列间距 - 固定缝隙，永远留得住缝。 */
    int boxW = (int)((BENCH_NODE_WX - 46.0f) * treeVK);
    int boxH = (int)((BENCH_NODE_WY - 52.0f) * treeVK);
    if (boxW < 26) boxW = 26;
    if (boxH < 16) boxH = 16;
    /* 但高度至少要放得下一行字 */
    if (boxH < th / 2) boxH = th / 2;

    for (int id = 0; id < BENCH_COUNT; id++) {
        float wx = BenchNodeWorldX(id);
        float wy = BenchNodeWorldY(id);
        /* v2.9：screen = world * k + v（和思维导图一致） */
        int cx = (int)(wx * treeVK + treeVX);
        int cy = (int)(wy * treeVK + treeVY);
        int bx = cx - boxW / 2, by = cy - boxH / 2;

        /* 屏幕外的直接跳过（节点多了也不掉帧） */
        if (bx > winW || by > winH || bx + boxW < 0 || by + boxH < 0) continue;

        int owned = BenchHas(id);
        int prereq = BenchPrereqOk(id);
        int affordable = (shards >= benchNodes[id].cost);
        int sel = (id == shopNodeSel);

        /* 同列竖连线（画在节点下面） */
        if (id % BENCH_ROWS > 0) {
            int pid = id - 1;
            int pcx = (int)(BenchNodeWorldX(pid) * treeVK + treeVX);
            int pcy = (int)(BenchNodeWorldY(pid) * treeVK + treeVY);
            int upOwned = BenchHas(pid);
            int y0 = pcy + boxH / 2, y1 = cy - boxH / 2;
            int steps = (y1 - y0) / 6;
            for (int s = 0; s <= steps; s++) {
                int yy = y0 + s * 6;
                if (!upOwned && (s % 2)) continue;      /* 未解锁 = 虚线 */
                BlendRectWin(pcx - 1, yy, pcx + 2, yy + 4,
                             upOwned ? 0x00C8A040u : 0x00404040u, upOwned ? 0.95f : 0.7f);
            }
        }

        unsigned int fill, frame, textc;
        if (owned)           { fill = 0x00C8A040u; frame = 0x00FFE070u; textc = 0x00100C08u; }
        else if (!prereq)    { fill = 0x00161412u; frame = 0x00383028u; textc = 0x00504840u; }
        else if (affordable) { fill = 0x00202828u; frame = 0x00A0C0C0u; textc = 0x00E8F0F0u; }
        else                 { fill = 0x00201818u; frame = 0x00704040u; textc = 0x00C08080u; }

        BlendRectWin(bx, by, bx + boxW, by + boxH, fill, owned ? 0.95f : 0.88f);
        FrameRectWin(bx, by, bx + boxW, by + boxH,
                     sel ? 0x00FFFFFFu : frame, 0.95f, sel ? 3 : 1);

        if (treeVK > 0.35f) {
            /* v2.9：**名字独占框内一行，价格画在框下方**。
               之前挤同一行，800x600 下框内只有 138px，
               "MAGAZINE"(160px) 截断后价格还叠上去 → 糊成一坨。 */
            char nb[48];
            /* v2.9：框内也用中文名（原来只有底部详情是中文，同屏一中一英） */
            sprintf(nb, "%s%s", benchNodes[id].star ? "* " : "",
                    Tr(benchNodes[id].name, benchNodes[id].nameZh));
            ClampRowText(nb, boxW - UIS(10));
            DrawTextWin(bx + UIS(5), by + (boxH - th) / 2, nb, textc);

            if (treeVK > 0.55f) {
                char pb[24];
                sprintf(pb, "%d", benchNodes[id].cost);
                DrawTextWin(bx + UIS(5), by + boxH + UIS(2), pb,
                            owned ? 0x00807850u : (affordable ? 0x00A0E0A0u : 0x00C06060u));
            }
        }
    }

    /* ---- 底部详情 ---- */
    {
        /* v2.9：原来 dy=winH-96，三行累加后第三行 570 撞提示行 574。
           现在从下往上倒推：留出提示行 + 三行正文。 */
        int lineH = th + UIS(4);
        int dy = winH - UIS(10) - th - 3 * lineH;
        int id = shopNodeSel;
        if (id < 0 || id >= BENCH_COUNT) id = 0;
        static char l2[128];

        DrawTextWin(UIS(28), dy, Tr(benchNodes[id].name, benchNodes[id].nameZh), 0x00FFFFFFu);
        sprintf(l2, "%d %s", benchNodes[id].cost, Tr("SHARDS", "碎片"));
        DrawTextWin(UIS(240), dy, l2, shards >= benchNodes[id].cost ? 0x00A0E0A0u : 0x00E06060u);

        dy += lineH;
        {
            static char db[128];
            sprintf(db, "%s", Tr(benchNodes[id].desc, benchNodes[id].descZh));
            ClampRowText(db, winW - UIS(56));
            DrawTextWin(UIS(28), dy, db, 0x00C8D0D0u);
        }

        dy += lineH;
        if (BenchHas(id)) {
            DrawTextWin(UIS(28), dy, Tr("OWNED", "已拥有"), 0x00C8A040u);
        } else if (!BenchPrereqOk(id)) {
            DrawTextWin(UIS(28), dy, Tr("REQUIRES THE NODE ABOVE IT", "需要先买同一列的上一层"), 0x00E0A040u);
        } else if (shards < benchNodes[id].cost) {
            sprintf(l2, "%s %d", Tr("SHORT BY", "还差"), benchNodes[id].cost - shards);
            DrawTextWin(UIS(28), dy, l2, 0x00E06060u);
        } else {
            DrawTextWin(UIS(28), dy, Tr("ENTER TO BUY", "回车购买"), 0x0060FF90u);
        }

        {
            const char* kh = Tr("WHEEL ZOOM  DRAG PAN  HOME  ESC",
                                "滚轮缩放  拖拽平移  HOME  ESC");
            DrawTextWin(winW - UIS(24) - TextWidthWin(kh), winH - UIS(16) - th - UIS(6), kh, 0x009AA8A8u);
        }
    }
}


void DrawMenuPanels(void) {
    MenuGeo g = GetMenuGeo();
    /* v2.7：改造树是**整屏**页面，不走下面的通用面板 ——
       否则旧面板和标题会画在树底下（截图里能看到 SAFE HOUSE - SHOP 残留）。 */
    if (currentState == STATE_SAFEHOUSE && shopPage == SHP_TREE) {
        DrawBenchTree(g);
        return;
    }
    /* v2.2：9-slice 铆接金属板（四角螺丝 + 双线边 + 底部警示条） */
    if (SpritesReady()) {
        UiPanel9(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, 0);
    } else {
        BlendRectWin(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, 0x000a1016, 0.94f);
        FrameRectWin(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, 0x00801818, 0.85f, UIS(2));
    }

    /* v2.8：安全屋的**所有**页都要显示页签（不只改造树那一页） */
    if (currentState == STATE_SAFEHOUSE) {
        DrawPageTabs();
        /* v3.0：余额移到**页签下面**独立一行 ——
           5 个页签变宽后右端会压到余额（截图里 SHARDS 被 UPGRADE 盖住）。 */
        {
            static char sb2[64];
            sprintf(sb2, "SHARDS %d", shards);
            int bx0, by0, bx1, by1;
            PageTabRect(0, &bx0, &by0, &bx1, &by1);
            DrawTextWin(winW - UIS(20) - TextWidthWin(sb2),
                        by1 + UIS(6), sb2, 0x00E8C040u);
        }
    }

    /* ===== v2.2 统一装饰：所有界面共用，不再各写各的 ===== */
    {
        int L2 = g.panel.left, T2 = g.panel.top, R2 = g.panel.right, B2 = g.panel.bottom;
        int cx2 = (L2 + R2) / 2;
        int inner = UIS(30);

        /* 角标 */
        UiCornerMarks(L2, T2, R2, B2);

        /* 标题下横线：主菜单/技能树除外（它们自己有排版） */
        if (currentState != STATE_MAIN_MENU &&
            !(currentState == STATE_HUB && hubPage == 1)) {
            int ty = g.titleY + TextHeightWin() + TextHeightWin() / 2;
            UiSectionRule(cx2, ty, (R2 - L2) - inner * 2);
        }

        /* 页脚横线 + 操作提示：只在有行列表的界面画 */
        if (currentState != STATE_MAIN_MENU) {
            int fy = B2 - UIS(26);
            /* v2.9：**线要画在最后一行文字下面**。
               原来 y - TH/2，主菜单第 10 行「Quit」正好被这条线横切
               （用户截图：Quit 上有条小横线）。
               现在先算出最后一行的底边，线放它下面 4px。 */
            int lastRowBottom = g.firstY + (g.count - 1) * g.rowH + TextHeightWin();
            int lineY = lastRowBottom + UIS(4);
            if (lineY > fy) lineY = fy;          /* 不越过面板底 */
            UiFooterRule(L2 + inner, R2 - inner, lineY);
        }
    }

    if (currentState == STATE_SAFEHOUSE) DrawCalibStrip(g);

    /* v1.22b：Hub 分页指示**挪到面板外**（屏幕右下角），
       原来画在面板里会和最后一行 Back 的高亮条重叠。 */
    if (currentState == STATE_HUB && hubPage == 0) {
        char pg[40];
        sprintf(pg, "TAB  PAGE %d/2", hubPage + 1);
        int tw2 = TextWidthWin(pg);
        /* v1.23b：挪到**面板右上角外面**，避开底部的技能说明文字 */
        DrawTextWin(winW - tw2 - UIS(16), UIS(8), pg, 0x00C08040u);
    }

    /* 弹窗开着时不再高亮背后的列表行 */
    int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);
    int sel = MenuItemIndex();
    /* v2 技能树不是行列表：选中框由 DrawSkillTree 自己画在节点上 */
    int rowHighlight = !(currentState == STATE_HUB && hubPage == 1);
    /* v2.2：主菜单/技能树自己画选中态，通用行高亮一律关掉 */
    if (currentState == STATE_MAIN_MENU) rowHighlight = 0;

    /* v2.2：主菜单自己画选中态；通用行高亮只在真正的行列表界面生效 */
    if (currentState == STATE_MAIN_MENU) rowHighlight = 0;

    if (rowHighlight && !popupOpen &&
        sel >= g.scrollOffset && sel < g.scrollOffset + g.maxVisibleRows) {
        RECT r = RowRect(g, sel);
        /* v2.2：底纹 + 左竖线 + 三角光标（不再是纯绿条） */
        UiRowSel(r.left, r.top, r.right, r.bottom, 0.85f);
    }

    /* v3.0：**鼠标悬停高亮** —— 鼠标移到哪一行，哪一行就亮起来。
       没有这个的话玩家得靠猜才知道能点哪（用户反馈不想按回车）。 */
    if (currentState == STATE_SAFEHOUSE && safehouseHoverRow >= 0 &&
        safehouseHoverRow != sel && safehouseHoverRow < g.count) {
        RECT hr = RowRect(g, safehouseHoverRow);
        BlendRectWin(hr.left, hr.top, hr.right, hr.bottom, 0x00405060u, 0.55f);
        FrameRectWin(hr.left, hr.top, hr.right, hr.bottom, 0x0080A0B0u, 0.7f, 1);
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

/* ============================================================
 * 技能树 v2 绘制
 * ============================================================ */

/* 节点四档状态色（用户定的顺序）：无色透明 -> 白 -> 金 -> keystone 彩色流转 */
static void SkillNodeColors(int id, unsigned int* fill, unsigned int* border,
                            unsigned int* text, float* alpha) {
    const SkillDef* d = &skillDefs[id];
    int rank = skillRank[id];
    if (d->isKeystone && rank > 0) {
        static const unsigned int rb[6] = {
            0x00FF6060u, 0x00FFB040u, 0x00FFF060u,
            0x0060FF80u, 0x0060C0FFu, 0x00C080FFu
        };
        int k = (int)(g_timeSec * 6.0f) % 6;
        if (k < 0) k += 6;
        *fill = rb[k]; *border = rb[k]; *text = 0x000A0A0Au; *alpha = 0.95f;
        return;
    }
    /* 四级色阶：未买（暗） → 1 级（白）→ 2 级（金）→ 满级（钻石） */
    if (rank > 0 && rank >= d->maxRank) {
        /* 钻石：冰蓝色 + 极慢的呼吸，和 gold 一眼区分 */
        int ph = (int)(g_timeSec * 2.2f) & 1;
        *fill = ph ? 0x00A8F0FFu : 0x0090E4FFu;
        *border = 0x00E8FFFFu;
        *text = 0x00081018u;
        *alpha = 0.97f;
    }
    else if (rank >= 2) { *fill = 0x00FFD040u; *border = 0x00FFE880u; *text = 0x00101010u; *alpha = 0.95f; }
    else if (rank == 1) { *fill = 0x00E8E8E8u; *border = 0x00FFFFFFu; *text = 0x00101010u; *alpha = 0.92f; }
    else                { *fill = 0x00101010u; *border = 0x00303030u; *text = 0x00606A70u; *alpha = 0.55f; }
}

/* 这一步能不能买（含"花 3 核心换 keystone"） */
static int SkillNodeBuyable(int id) {
    if (SkillCanBuy(id)) return 1;
    return SkillSwitchable(id) ? 1 : 0;
}

/* 锁住的原因；NULL = 现在就能买（已满也返回 NULL，由调用方显示 MAXED） */
static const char* SkillNodeLockText(int id) {
    const SkillDef* d = &skillDefs[id];
    int cost = SkillCost(id);
    if (cost < 0) return NULL;
    if (d->prereq >= 0 && skillRank[d->prereq] <= 0) return "BUY THE NODE ABOVE";
    if (d->isKeystone && skillRank[id] <= 0) {
        if (!SkillKeystoneUnlocked(id))
            return (d->branch == 3) ? "NEEDS A BRANCH ULT" : "NEEDS 6 PTS IN BRANCH";
        if (activeKeystone >= 0 && activeKeystone != id) {
            if (SkillSwitchable(id)) return "ENTER-SWAP 3 CORES";
            return "ANOTHER ULT IS ACTIVE";
        }
    }
    if (shards < cost) return "NOT ENOUGH SHARDS";
    if (d->coreCost > 0 && cores < d->coreCost) return "NOT ENOUGH CORES";
    return NULL;
}

/* 虚线框。th 必须跟着文字高度缩放：
   绘制用的是**窗口坐标**，但真正落笔在 renderW x renderH 的缓冲里，
   1440p 全屏时 1 个窗口像素只有 0.27 个缓冲像素 —— 1px 的线会
   直接消失（第一版在全屏下虚线框整个不见了）。 */
static void SkillDrawDashFrame(int x0, int y0, int x1, int y1,
                               unsigned int col, float a, int dash, int gap, int th) {
    if (dash < 2) dash = 2;
    if (gap < 2) gap = 2;
    if (th < 2) th = 2;
    for (int x = x0; x < x1; x += dash + gap) {
        int xe = x + dash; if (xe > x1) xe = x1;
        BlendRectWin(x, y0, xe, y0 + th, col, a);
        BlendRectWin(x, y1 - th, xe, y1, col, a);
    }
    for (int y = y0; y < y1; y += dash + gap) {
        int ye = y + dash; if (ye > y1) ye = y1;
        BlendRectWin(x0, y, x0 + th, ye, col, a);
        BlendRectWin(x1 - th, y, x1, ye, col, a);
    }
}

/* 同列上下节点之间的连线：走线颜色 = 该分支主色，越往下点亮越实 */
static void SkillDrawConnectors(const SkillTreeLayout* L) {
    int TH = TextHeightWin();
    int bw = TH / 8; if (bw < 2) bw = 2;
    for (int c = 0; c < SKILL_GRID_COLS; c++) {
        for (int r = 0; r < 3; r++) {
            int a = SkillNodeIdAt(c, r);
            int b = SkillNodeIdAt(c, r + 1);
            if (a < 0 || b < 0) continue;
            int x = L->colCX[c];
            int y0 = L->node[a].bottom;
            int y1 = L->node[b].top;
            if (y1 <= y0 + 2) continue;
            int ownA = skillRank[a] > 0;
            int ownB = skillRank[b] > 0;
            unsigned int col = kBranchColor[skillDefs[b].branch];
            float al = ownA ? (ownB ? 0.90f : 0.55f) : 0.16f;
            BlendRectWin(x - bw / 2, y0, x - bw / 2 + bw, y1, col, al);
            /* 中段一颗小方块，看起来像"节点之间的一道锁" */
            int mid = (y0 + y1) / 2;
            BlendRectWin(x - bw, mid - bw, x + bw, mid + bw,
                         ownA ? col : 0x00343434u, ownA ? 0.9f : 0.55f);
        }
    }
}

static void SkillDrawNode(const SkillTreeLayout* L, int id, int isSel) {
    RECT r = L->node[id];
    int TH = TextHeightWin();
    unsigned int fill, border, text;
    float a;
    SkillNodeColors(id, &fill, &border, &text, &a);

    if (skillRank[id] <= 0 && SkillNodeBuyable(id)) {
        /* 现在能买：边框换成会呼吸的亮绿 —— 一眼看出下一步点哪 */
        int ph = (int)(g_timeSec * 2.0f) & 1;
        border = ph ? 0x00A8FFA8u : 0x0050A050u;
        text = 0x00D0FFD0u;
    }

    BlendRectWin(r.left, r.top, r.right, r.bottom, fill, a);
    int bt = TH / 12; if (bt < 2) bt = 2;
    FrameRectWin(r.left, r.top, r.right, r.bottom, border, 0.95f, bt);

    /* 名字 */
    char nm[40];
    sprintf(nm, "%s", Tr(skillDefs[id].name, SkillNameZh(skillDefs[id].name)));
    ClampRowText(nm, (r.right - r.left) - (TH * 2) / 5);
    int nameY = r.top + ((r.bottom - r.top) - TH) * 38 / 100;
    DrawTextCenterWin((r.left + r.right) / 2, nameY, nm, text);

    /* 等级标记：普通节点 = 底部两个小方块（空心/实心）；keystone = 底部一颗菱形。
       之前把 keystone 的菱形画在左边，长名字（METABOLISM）第一字母直接被盖住。 */
    int pw = TH / 6; if (pw < 3) pw = 3;
    int py = r.bottom - pw - (TH * 14) / 100;
    if (py < nameY + TH) py = nameY + TH;
    int cx = (r.left + r.right) / 2;
    if (skillDefs[id].isKeystone) {
        int s = pw + 1;
        int on = skillRank[id] > 0;
        unsigned int col = on ? text : kBranchColor[3];
        for (int dy = -s; dy <= s; dy++) {
            int span = s - (dy < 0 ? -dy : dy);
            BlendRectWin(cx - span, py + dy, cx + span + 1, py + dy + 1,
                         col, on ? 0.95f : 0.55f);
        }
    } else {
        int n = skillDefs[id].maxRank;
        int sp = pw / 2; if (sp < 2) sp = 2;
        int totalW = n * pw + (n - 1) * sp;
        int x0 = cx - totalW / 2;
        unsigned int onCol = (skillRank[id] > 0) ? text : kBranchColor[skillDefs[id].branch];
        for (int i = 0; i < n; i++) {
            int sx = x0 + i * (pw + sp);
            if (i < skillRank[id]) BlendRectWin(sx, py, sx + pw, py + pw, onCol, 0.95f);
            else                   FrameRectWin(sx, py, sx + pw, py + pw, onCol, 0.60f, 2);
        }
    }

    /* 选中框：先一道黑隔离环，再一圈金框 + 四角加粗短线。
       隔离环是必须的 —— 白/金节点的填充色和选中框太接近，
       只画亮框的话在白色节点上几乎看不见（第一版就是这样）。 */
    if (isSel) {
        int gap = 2;
        FrameRectWin(r.left - gap, r.top - gap, r.right + gap, r.bottom + gap,
                     0x00000000u, 0.90f, 2);
        int o = gap + 3;
        unsigned int sc = 0x00FFE080u;
        FrameRectWin(r.left - o, r.top - o, r.right + o, r.bottom + o, sc, 0.95f, 2);
        int arm = TH / 3;
        BlendRectWin(r.left - o - 2, r.top - o - 2, r.left - o + arm, r.top - o + 1, sc, 1.0f);
        BlendRectWin(r.right + o - arm, r.top - o - 2, r.right + o + 2, r.top - o + 1, sc, 1.0f);
        BlendRectWin(r.left - o - 2, r.bottom + o - 1, r.left - o + arm, r.bottom + o + 2, sc, 1.0f);
        BlendRectWin(r.right + o - arm, r.bottom + o - 1, r.right + o + 2, r.bottom + o + 2, sc, 1.0f);
    }
}

void DrawSkillTree(void) {
    SkillTreeLayout L;
    SkillTreeCompute(&L);
    int TH = TextHeightWin();
    int sel = hubSelection;
    if (sel < 0 || sel >= SKILL_COUNT) sel = 0;

    /* 标题 */
    DrawTitleWin("SKILL TREE", L.titleY, 0x00D23C3Cu);

    /* 左：一行提示（属性面板已并入节点，不再有页码） + 右：货币 */
    {
        DrawTextWin(L.panel.left + L.padX, L.curY,
                    "MAX 3 RANKS", 0x00808080u);

        int cx = L.panel.right - L.padX;
        char mb[48];
        sprintf(mb, "CORES %d", cores);
        int w = TextWidthWin(mb);
        DrawTextWin(cx - w, L.curY, mb, 0x00FFC040u);
        cx -= w + (TH * 4) / 5;
        sprintf(mb, "SHARDS %d", shards);
        w = TextWidthWin(mb);
        DrawTextWin(cx - w, L.curY, mb, 0x0060D0FFu);
    }

    /* 三个分支：标题 + 6 段投点进度 */
    for (int c = 0; c < SKILL_GRID_COLS; c++) {
        char hb[40];
        /* v2.0：4 列之后列宽只有 ~180px，再拼上前缀字母会互相压字 —— 只画名字 */
        sprintf(hb, "%s", Tr(kBranchName[c], kBranchNameZh[c]));
        DrawTextCenterWin(L.colCX[c], L.hdrY, hb, kBranchColor[c]);

        int pts = SkillBranchPoints(c);
        /* 6 段小方块：宽而矮的条在 800x600 下会糊成一条线，
           所以缩到列宽的 3/5、加高、加大间隙。 */
        int bwid = (L.nodeW * 3) / 5;
        int bh = TH / 5; if (bh < 4) bh = 4;
        int sg = TH / 6; if (sg < 4) sg = 4;
        int sw = (bwid - 5 * sg) / 6;
        if (sw < 3) sw = 3;
        int bx = L.colCX[c] - bwid / 2;
        for (int i = 0; i < 6; i++) {
            int sx = bx + i * (sw + sg);
            int on = (i < pts);
            BlendRectWin(sx, L.barY, sx + sw, L.barY + bh,
                         on ? kBranchColor[c] : 0x003A3A3Au, on ? 0.95f : 0.9f);
        }
    }

    /* 跨支行：虚线框（不画连线 —— 它们不属于任何一列） */
    {
        int x0 = L.node[SkillNodeIdAt(0, 4)].left  - TH / 4;
        int x1 = L.node[SkillNodeIdAt(2, 4)].right + TH / 4;
        int y0 = L.crossTop - TH / 6;
        int y1 = L.crossBot + TH / 6;
        SkillDrawDashFrame(x0, y0, x1, y1, kBranchColor[3], 0.40f,
                           TH / 3, TH / 4, TH / 10);
    }

    /* 连线先画，节点盖住端点 */
    SkillDrawConnectors(&L);

    for (int id = 0; id < SKILL_COUNT; id++) SkillDrawNode(&L, id, id == sel);

    /* ---- 底部详情条 ---- */
    {
        const SkillDef* d = &skillDefs[sel];
        int lv = skillRank[sel];
        int cost = SkillCost(sel);
        const char* lock = SkillNodeLockText(sel);
        int px = L.panel.left + L.padX;
        int rightEdge = L.panel.right - L.padX;
        char b1[96], b2[96];

        /* 第一行左：名字（右移一格重画一遍 = 加粗） */
        unsigned int nameCol;
        if (!d->isKeystone && lv >= d->maxRank && lv > 0) nameCol = 0x00A8F0FFu;   /* 满级 = 钻石 */
        else if (lv > 0) nameCol = d->isKeystone ? 0x00FFD040u : 0x00FFFFFFu;
        else             nameCol = 0x00B4B4B4u;
        sprintf(b1, "%s", d->name);
        DrawTextWin(px, L.detail1Y, b1, nameCol);
        DrawTextWin(px + 1, L.detail1Y, b1, nameCol);
        int leftEnd = px + TextWidthWin(b1) + (TH * 3) / 4;

        /* 第一行右：MAXED / 锁定原因 / 等级+花费。
           锁住的时候**不画等级**，把那段宽度让给原因 —— 否则
           "NEEDS A BRANCH ULT" 会被截断成 "NEEDS A BRANCH.."，等于没说。 */
        unsigned int rc = 0x00A0FFA0u;
        if (cost < 0) {
            sprintf(b2, "MAXED"); rc = 0x00FFD040u;
        } else if (lock) {
            sprintf(b2, "%s", lock); rc = 0x00E08040u;
        } else {
            sprintf(b2, "LV %d/%d", lv, d->maxRank);
            DrawTextWin(leftEnd, L.detail1Y, b2, 0x00909090u);
            leftEnd += TextWidthWin(b2) + TH / 2;
            if (d->coreCost > 0) sprintf(b2, "%d SHARDS + %d CORE", cost, d->coreCost);
            else                 sprintf(b2, "%d SHARDS", cost);
        }
        ClampRowText(b2, rightEdge - leftEnd);
        DrawTextWin(rightEdge - TextWidthWin(b2), L.detail1Y, b2, rc);

        /* 第二行：效果说明 */
        sprintf(b2, "%s", d->desc);
        ClampRowText(b2, rightEdge - px);
        DrawTextWin(px, L.detail2Y, b2, 0x008A8A8Au);
    }

    /* 底部按键提示 */
    DrawTextCenterWin((L.panel.left + L.panel.right) / 2, L.hintY,
                      "ENTER BUY   ARROWS MOVE   ESC BACK", 0x00707070u);
}

/* 逻辑画布坐标 -> 节点 id（没点中 -1）。
   game.c 的鼠标悬停/点击走这个 —— 技能树不是行列表，按行高去猜会选中错的节点。 */
int SkillNodeHitTest(int mx, int my) {
    SkillTreeLayout L;
    SkillTreeCompute(&L);
    /* 先按扩过一圈的方块判，鼠标擦边也能选中 */
    for (int pass = 0; pass < 2; pass++) {
        int pad = (pass == 0) ? TextHeightWin() / 4 : 0;
        for (int id = 0; id < SKILL_COUNT; id++) {
            RECT r = L.node[id];
            if (mx >= r.left - pad && mx <= r.right + pad &&
                my >= r.top - pad && my <= r.bottom + pad) return id;
        }
    }
    return -1;
}

/* ============================================================
 * 主菜单绘制（v2.2）
 *
 * 和技能树一样，这一页不再走"通用行列表"：
 * 标题 / 行 / 页脚三块分开，选中行有左侧亮条 + 右箭头，
 * 最后四行（Help / Settings / Training / Quit）前面加一条分组细线。
 * ============================================================ */
static void DrawMainMenu(const MainMenuLayout* M) {
    int TH = TextHeightWin();
    int n = M->count;
    int sel = menuSelection;
    if (sel < 0 || sel >= n) sel = 0;
    int cx = (M->panel.left + M->panel.right) / 2;

    /* 标题：故意用双空格拉开字距 —— 点阵字没有字距控制，这是唯一的办法 */
    UiTitle(cx, M->titleY, "C  A  N  I  T  Y");
    UiHint(cx, M->subY, "RAYCASTER HORROR");

    int split = n - 4;                 /* 系统组（Help/Settings/Training/Quit）之前分组 */
    for (int i = 0; i < n; i++) {
        int top = M->firstY + i * M->rowH;
        int y = top + (M->rowH - TH) / 2;
        int left = M->panel.left + M->padX;
        int right = M->panel.right - M->padX;

        if (split > 0 && i == split) {
            /* 画在行与行之间的空隙正中：上一行文字底 → 本行文字顶 的中点就是 top。
               （用 top - rowH/6 会直接切到上一行文字的底边，第一版就是这样） */
            int lth = TH / 12; if (lth < 2) lth = 2;
            int ly = top - lth / 2;
            BlendRectWin(left, ly, right, ly + lth, 0x00605A66u, 0.95f);
        }

        int isSel = (i == sel);
        if (isSel) {
            /* v2.2：底纹 + 左竖线（光标另画在文字那一行，避免行高不一致时错位） */
            int rt = top + M->rowH / 10;
            int rb = top + M->rowH - M->rowH / 10;
            BlendRectWin(left - TH / 2, rt, right + TH / 2, rb, UI_ACCENT, 0.10f);
            BlendRectWin(left - TH / 2, rt, left - TH / 2 + TH / 8, rb, UI_ACCENT, 0.95f);
            /* v2.2：主菜单不再叠三角光标 —— 行高与文字基线对不齐时会错行，
               左竖线 + 琥珀文字已经足够表达选中。 */
        }

        char num[12];   /* v2.2：sprintf %d 最多 11 字节 */
        sprintf(num, "%d", i + 1);
        DrawTextWin(left, y, num, isSel ? 0x00FFE080u : 0x00969AA6u);

        DrawTextWin(left + TH * 2, y, MenuLabel(i),
                    isSel ? UI_ACCENT : UI_INK);

        if (isSel) {
            const char* arrow = ">";
            DrawTextWin(right - TextWidthWin(arrow), y, arrow, 0x0060E070u);
        }
    }

    /* 页脚：细线 + 一行数据，全部**在面板里面**（原来露在面板外，很难看） */
    {
        int ft = TH / 10; if (ft < 2) ft = 2;
        /* 左右各内缩半行：面板四角那四条装饰短条会压到页脚首尾字符 */
        int fx0 = M->panel.left + M->padX + TH / 2;
        int fx1 = M->panel.right - M->padX - TH / 2;
        BlendRectWin(fx0, M->footRuleY, fx1, M->footRuleY + ft, 0x00502A32u, 0.95f);
    }

    /* 页脚：左边货币、右边最佳层数；
       版本号挪到屏幕右下角（面板内塞三项，800x600 下必然撞车） */
    char lstr[48], rstr[48];
    sprintf(lstr, "SHARDS %d", shards);
    sprintf(rstr, "BEST LEVEL %d", bestLevel);
    int lx = M->panel.left + M->padX + TH / 2;
    int rx = M->panel.right - M->padX - TH / 2;
    DrawTextWin(lx, M->footY, lstr, 0x0060D0FFu);
    int wr = TextWidthWin(rstr);
    DrawTextWin(rx - wr, M->footY, rstr, 0x00808088u);

    DrawTextWin(winW - TextWidthWin(CANITY_VERSION_STR) - UIS(8),
                winH - TextHeightWin() - UIS(4), CANITY_VERSION_STR, 0x007C8088u);
}

static void DrawMenuText(void) {
    MenuGeo g = GetMenuGeo();
    int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);

    /* 成就页面：使用专用现代化渲染 */
    if (currentState == STATE_ACHIEVEMENTS) {
        DrawAchievementsWindow(g);
        return;
    }

    /* v2 技能树：整体由 DrawSkillTree 自己画，不再走"列表行"那套 */
    if (currentState == STATE_HUB && hubPage == 1) {
        DrawSkillTree();
        return;
    }

    /* v2.2 主菜单：自己的布局（标题 / 行 / 页脚） */
    if (currentState == STATE_MAIN_MENU) {
        MainMenuLayout M;
        MainMenuCompute(&M);
        DrawMainMenu(&M);
        return;
    }

    unsigned int titleColor = (currentState == STATE_MAIN_MENU) ? 0x00BE1414u : 0x00D23C3Cu;
    /* 通关结局的标题用绿色 —— 和「理智耗尽」的暗红区分开 */
    if (currentState == STATE_GAMEOVER && gameWon) titleColor = 0x0060E070u;
    if (currentState == STATE_MAIN_MENU) {
        char sb[48];
        /* 页脚位置按"文字实际高度"算，不写死偏移 ——
           否则大字库下会被窗口下沿切掉半行（BEST LEVEL 就中过）。
           y 是文字左上角，所以最后一行要留出一个文字高度。 */
        int th = TextHeightWin();
        int y2 = winH - th - UIS(10);
        int y1 = y2 - th - UIS(6);
        /* v1.8：货币不再是裸文字 —— 前面加一颗菱形水晶图标 */
        sprintf(sb, "SHARDS  %d", shards);
        {
            int tw = TextWidthWin(sb);
            int tx = (winW - tw) / 2;
            int cy2 = y1 + TextHeightWin() / 2;
            int s2 = UIS(5);
            for (int dy = -s2; dy <= s2; dy++) {
                int span = s2 - (dy < 0 ? -dy : dy);
                for (int dx = -span; dx <= span; dx++) {
                    unsigned int c = (dx == 0 || dy == 0) ? 0x00E8FBFFu : 0x0040B8F0u;
                    /* 屏幕坐标直接写像素池（菜单态整屏都在同一个缓冲里） */
                    int px = (tx + dx - UIS(14)) * renderW / winW;
                    int py = (cy2 + dy) * renderH / winH;
                    PutPixel(px, py, c);
                }
            }
            DrawTextWin(tx, y1, sb, 0x0060D0FFu);
        }
        char bb[48];
        sprintf(bb, "BEST LEVEL %d", bestLevel);
        DrawTextCenterWin(winW / 2, y2, bb, 0x00808080u);
        /* 版本号：右下角低调显示，方便对着存档确认是哪个版本 */
        DrawTextWin(winW - TextWidthWin(CANITY_VERSION_STR) - UIS(14),
                    y2, CANITY_VERSION_STR, 0x00585858u);
    }
    /* v2.9：标题为空就不画 —— 安全屋靠顶部页签表明位置，
       那行大红「SAFE HOUSE - SHOP」会把改造树整个盖住（用户要求去掉）。 */
    if (g.title && g.title[0]) DrawTitleWin(g.title, g.titleY, titleColor);

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

    /* v1.8：图鉴底部两行 —— 威胁 + 对策（未解锁则提示去遇到它） */
    if (currentState == STATE_BESTIARY && sel >= 0 && sel < BESTIARY_COUNT) {
        char l1[192], l2[192];
        MenuGeo bg = GetMenuGeo();
        int avail = (bg.panel.right - bg.panel.left) - UIS(56);
        if (!BestiaryIsSeen(sel)) {
            sprintf(l1, "UNKNOWN. ENCOUNTER IT IN A RUN TO UNLOCK.");
            ClampRowText(l1, avail);
            DrawTextCenterWin(winW / 2, winH - UIS(46), l1, 0x00808080u);
        } else {
            sprintf(l1, "THREAT: %s", bestiary[sel].threat);
            sprintf(l2, "COUNTER: %s", bestiary[sel].counter);
            ClampRowText(l1, avail);
            ClampRowText(l2, avail);
            DrawTextCenterWin(winW / 2, winH - UIS(46), l1, 0x00E0A050u);
            DrawTextCenterWin(winW / 2, winH - UIS(22), l2, 0x0060C0FFu);
        }
    }

    /* THE HUB 升级页：价格列宽度按**本页最宽的价格**算一次，
       等级点列锚在它左边。这样每行都对齐，也不会出现
       "120 SHARDS" 压到等级点上的情况。 */
    int hubPriceZone = 0;
    if (currentState == STATE_HUB && hubPage == 0) {
        for (int k = 0; k < UPGRADE_COUNT; k++) {
            char t[40];
            int c2 = UpgradeCost(k);
            if (c2 < 0) sprintf(t, "MAX"); else sprintf(t, "%d SHARDS", c2);
            int w2 = TextWidthWin(t);
            if (w2 > hubPriceZone) hubPriceZone = w2;
        }
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
        else c = (i == sel) ? 0x008CFF8Cu : 0x009B9B9Bu;

        if (currentState == STATE_GAMEOVER && i > 0) {
            /* 复盘：第 2、3 行放统计（通关与失败共用这一页，标题区分） */
            char line[96];
            int mins = (int)(runTime / 60.0f);
            int secs = (int)runTime - mins * 60;
            if (i == 1) sprintf(line, "LEVEL %d    TIME %d:%02d", currentLevel, mins, secs);
            else if (gameWon)
                sprintf(line, "SCORE %d  D%d C%d K%d T%d",
                        lastScore.score, lastScore.s_depth, lastScore.s_coll,
                        lastScore.s_kill, lastScore.s_time);
            else
                sprintf(line, "SCORE %d  RUN FAILED", lastScore.score);
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

        /* ---- v2.1：THE HUB 升级页改成三列 ----
           原来是「名字 + LV + 价格」拼成一整串再居中画，窗口一窄就顶出面板
           （实测 800x600 下 QUIETER / ECO / REST 三行都出界被切）。
           现在拆成三列，每一列各自对齐、各自截断，永远不会出界。 */
        if (currentState == STATE_HUB && hubPage == 0 && i < UPGRADE_COUNT) {
            RECT row = RowRect(g, i);
            int th2 = TextHeightWin();
            int y = row.top + ((row.bottom - row.top) - th2) / 2;
            int x0 = row.left + UIS(12);
            int x1 = row.right - UIS(12);

            /* 右列：价格（右对齐，买不起变暗红） */
            char price[40];
            int cost = UpgradeCost(i);
            unsigned int priceCol;
            if (cost < 0) {
                sprintf(price, "MAX");
                priceCol = 0x00E8C040u;
            } else {
                sprintf(price, "%d SHARDS", cost);
                priceCol = (shards >= cost) ? 0x0060D0FFu : 0x00C86868u;
            }
            int pw = TextWidthWin(price);
            DrawTextWin(x1 - pw, y, price, priceCol);

            /* 中列：等级用 3 个小方块表示（实心 = 已买），比 "LV2/3" 省地方也更直观。
               列位置锚在价格列左边 —— 跟着每行自己的价格浮动会让方块左右乱跳。 */
            int lv = UpgradeLevel(i);
            int sq = th2 / 2; if (sq < 6) sq = 6;
            int sp = sq / 2; if (sp < 2) sp = 2;
            int lvW = UPG_MAX * sq + (UPG_MAX - 1) * sp;
            int lvX = x1 - hubPriceZone - UIS(20) - lvW;
            int sy = row.top + ((row.bottom - row.top) - sq) / 2;
            for (int k = 0; k < UPG_MAX; k++) {
                int sx = lvX + k * (sq + sp);
                if (k < lv) BlendRectWin(sx, sy, sx + sq, sy + sq, 0x00C8E8FFu, 0.95f);
                else        FrameRectWin(sx, sy, sx + sq, sy + sq, 0x00485868u, 0.9f, 2);
            }

            /* 左列：名字，右边界卡到等级点左边 —— 放不下就 ".." 截断 */
            char nm[64];
            sprintf(nm, "%s", UpgradeName(i));
            ClampRowText(nm, lvX - UIS(14) - x0);
            DrawTextWin(x0, y, nm, c);
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
    AchGeo A;
    AchCompute(&A);
    int TH = TextHeightWin();
    int sel = achSelection;

    /* 面板：和 Hub / 技能树统一成深色 + 红色描边，不再单独一套灰 */
    BlendRectWin(A.panel.left, A.panel.top, A.panel.right, A.panel.bottom,
                 0x000a1016u, 0.96f);
    FrameRectWin(A.panel.left, A.panel.top, A.panel.right, A.panel.bottom,
                 0x00801818u, 0.85f, UIS(2));

    int cx = (A.panel.left + A.panel.right) / 2;
    DrawTextCenterWin(cx, A.titleY, "ACHIEVEMENTS", 0x00C89632u);

    int left  = A.panel.left + A.padX;
    int right = A.panel.right - A.padX;

    /* 已解锁数量放标题行右侧 */
    int unlockedCount = 0;
    for (int i = 0; i < ACH_COUNT; i++) if (g_achievements[i].unlocked) unlockedCount++;
    char cnt[32];
    sprintf(cnt, "%d/%d", unlockedCount, ACH_COUNT);
    DrawTextWin(right - TextWidthWin(cnt), A.titleY, cnt, 0x00C89632u);

    int boxS = TH / 2;                  /* 状态小方块 */
    if (boxS < 6) boxS = 6;
    int nameX = left + boxS + TH / 2;
    char progSample[] = "999/999";
    int progW = TextWidthWin(progSample);

    for (int i = 0; i < A.visible; i++) {
        int idx = g.scrollOffset + i;
        if (idx >= A.count) break;
        int top = A.firstY + i * A.rowH;
        if (top + A.rowH > A.stripRuleY) break;
        int y = top + (A.rowH - TH) / 2;
        int isSel = (idx == sel);

        if (isSel) {
            int bt = top + A.rowH / 8, bb = top + A.rowH - A.rowH / 8;
            BlendRectWin(left - TH / 2, bt, right + TH / 2, bb, 0x001C3A1Cu, 0.85f);
            BlendRectWin(left - TH / 2, bt, left - TH / 2 + TH / 6, bb,
                         0x0060E070u, 0.95f);
        }

        if (idx >= ACH_COUNT) {          /* Back 行 */
            DrawTextWin(nameX, y, "Back", isSel ? 0x0098FF98u : 0x00808080u);
            continue;
        }

        const Achievement* a = &g_achievements[idx];

        /* 状态方块：解锁 = 实心绿，未解锁 = 空心 */
        int by = y + (TH - boxS) / 2;
        if (a->unlocked) {
            BlendRectWin(left, by, left + boxS, by + boxS, 0x0040D060u, 0.95f);
        } else {
            FrameRectWin(left, by, left + boxS, by + boxS, 0x00545860u, 0.9f, 2);
        }

        unsigned int nameCol = a->unlocked ? 0x00E8E8E8u
                                           : (isSel ? 0x0098FF98u : 0x009A9A9Au);
        char nm[64];
        sprintf(nm, "%s", a->name);
        ClampRowText(nm, (right - progW - TH) - nameX);
        DrawTextWin(nameX, y, nm, nameCol);

        /* 进度：右对齐，解锁后变绿 */
        char pr[32];
        sprintf(pr, "%d/%d", a->progress, a->target);
        DrawTextWin(right - TextWidthWin(pr), y, pr,
                    a->unlocked ? 0x0040D060u : (isSel ? 0x00C8C8C8u : 0x00747880u));
    }

    /* 右侧滚动条 */
    if (A.count > A.visible) {
        int trackW = TH / 8; if (trackW < 3) trackW = 3;
        int tx = A.panel.right - trackW - TH / 4;
        int ty = A.firstY;
        int th2 = A.visible * A.rowH;
        BlendRectWin(tx, ty, tx + trackW, ty + th2, 0x00303038u, 0.9f);
        int thumbH = (A.visible * th2) / A.count;
        if (thumbH < TH) thumbH = TH;
        int maxScroll = A.count - A.visible;
        int thumbY = ty + (maxScroll > 0 ? (g.scrollOffset * (th2 - thumbH) / maxScroll) : 0);
        if (thumbY + thumbH > ty + th2) thumbY = ty + th2 - thumbH;
        BlendRectWin(tx, thumbY, tx + trackW, thumbY + thumbH, 0x00808090u, 0.95f);
    }

    /* 底部说明条：固定位置 + 自动折行，永远不会跑出面板 */
    {
        int rt = TH / 10; if (rt < 2) rt = 2;
        BlendRectWin(left, A.stripRuleY, right, A.stripRuleY + rt, 0x00502A32u, 0.95f);

        if (sel >= 0 && sel < ACH_COUNT) {
            const Achievement* a = &g_achievements[sel];
            char wl[2][96];
            int n = WrapText(a->desc, right - left, wl, 2);
            for (int k = 0; k < n; k++)
                DrawTextWin(left, k == 0 ? A.stripY1 : A.stripY2, wl[k], 0x00C0C0C8u);
            if (n == 0)
                DrawTextWin(left, A.stripY1, "-", 0x00808088u);
        } else {
            DrawTextWin(left, A.stripY1, "BACK TO MENU", 0x00808088u);
        }

        /* 滚动位置提示（右侧，和说明分开放） */
        if (A.count > A.visible) {
            char sc[48];
            sprintf(sc, "%d-%d/%d", g.scrollOffset + 1,
                    g.scrollOffset + A.visible, A.count);
            DrawTextWin(right - TextWidthWin(sc), A.stripY2, sc, 0x00606068u);
        }
    }
}

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
/* v2.1：准星。直接写渲染缓冲像素，避免 UI 缩放把线算成 0 宽。
   散布随蓄力扩大 —— 玩家能看出"这一下有多重"。 */
static void DrawCrosshair(void) {
    if (!pixels || renderW < 8 || renderH < 8) return;
    int cx = renderW / 2, cy = renderH / 2;
    int gap = 3 + (int)(mouseCharge * 7.0f);
    int len = 4;
    unsigned int col = 0x00D8E8F0u;
    for (int i = 0; i < len; i++) {
        int yy1 = cy - gap - i, yy2 = cy + gap + i;
        int xx1 = cx - gap - i, xx2 = cx + gap + i;
        if (yy1 >= 0 && yy1 < renderH) { pixels[(size_t)yy1 * renderW + cx] = col; if (cx + 1 < renderW) pixels[(size_t)yy1 * renderW + cx + 1] = col; }
        if (yy2 >= 0 && yy2 < renderH) { pixels[(size_t)yy2 * renderW + cx] = col; if (cx + 1 < renderW) pixels[(size_t)yy2 * renderW + cx + 1] = col; }
        if (xx1 >= 0 && yy1 >= 0) pixels[(size_t)cy * renderW + xx1] = col;
        if (xx2 >= 0 && xx2 < renderW) pixels[(size_t)cy * renderW + xx2] = col;
        if (yy1 >= 0 && yy1 < renderH && xx1 >= 0) pixels[(size_t)yy1 * renderW + xx1] = col;
        if (yy2 >= 0 && yy2 < renderH && xx2 < renderW) pixels[(size_t)yy2 * renderW + xx2] = col;
    }
    if (cx >= 1 && cx + 1 < renderW && cy >= 1 && cy + 1 < renderH) {
        pixels[(size_t)cy * renderW + cx] = 0x00FFFFFFu;
        pixels[(size_t)(cy - 1) * renderW + cx] = 0x00FFFFFFu;
        pixels[(size_t)(cy + 1) * renderW + cx] = 0x00FFFFFFu;
        pixels[(size_t)cy * renderW + cx - 1] = 0x00FFFFFFu;
        pixels[(size_t)cy * renderW + cx + 1] = 0x00FFFFFFu;
    }
}

void DrawHUD(void) {
    if (currentState == STATE_PLAYING && !isMapOpen) DrawCrosshair();
    /* v2.1：Boss 血条（屏幕上方居中） */














    char buf[96];

    /* ------------------------------------------------------------------ */
    /* HUD 详细度                                                        */
    /* ------------------------------------------------------------------ */
    /* 三档（设置里的 HUD Detail）：
         0 Minimal —— 只留必须实时知道的：数值、电量体力、状态效果
         1 Normal  —— 加：投掷物数量（有库存时才显示）、SAFE ZONE 短提示
         2 Full    —— 加：投掷物数量常驻、SAFE ZONE 常驻
       词条横幅在任何档位都只在进关后 5 秒出现（levelBanner 控制），
       因为它是「一次性信息」，常驻反而是噪音。 */

    sprintf(buf, Tr("LEVEL %d", "第 %d 层"), currentLevel);
        if (campaignMode) {
            char cb[64];
            sprintf(cb, Tr("  CAMPAIGN %d/5 %s", "  战役 %d/5 %s"),
                    campaignStage + 1, campaignName(campaignStage));
            strncat(buf, cb, sizeof(buf) - strlen(buf) - 1);
        }
        if (dailyMode) {
            char db[48];
            sprintf(db, Tr("  DAILY %d", "  每日 %d"), dailySeed % 10000);
            strncat(buf, db, sizeof(buf) - strlen(buf) - 1);
        }
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
    /* v1.8：本层的随机事件（通过本层会给本局加分） */
    /* v1.8：事件**不再单独占一行**。
       原来画在 y=UIS(36)，而文字高度在小窗口下就有 30 多逻辑像素，
       结果和 LEVEL / SANITY 叠在一起（截图里就是这样）。
       现在并到首行右侧，跟词条同一行，永远不会撞。 */
    if (levelEvent >= 0 && levelEvent < EVENT_COUNT) {
        int ex = UIS(20) + TextWidthWin("LEVEL 99") + UIS(14);
        if (levelMod >= 0 && levelMod < MOD_COUNT) {
            char mb[32];
            sprintf(mb, "%s", modNames[levelMod]);
            ex += TextWidthWin(mb) + UIS(14);
        }
        /* 宽度不够时自动缩写，别让文字被窗口裁掉 */
        static const char* const evShort[EVENT_COUNT] = {"BLOOD", "POWER", "SILENT", "SUPPLY", "FLOOD", "WHISP", "MIRROR"};   /* v2.2: was 4 of 7 -> NULL in %s */
        int avail = winW - ex - UIS(12);
        sprintf(buf, "EVENT: %s +%d", eventNames[levelEvent], EventScore(levelEvent));
        if (TextWidthWin(buf) > avail) {
            sprintf(buf, "EV: %s +%d", evShort[levelEvent] ? evShort[levelEvent] : "?", EventScore(levelEvent));
            if (TextWidthWin(buf) > avail) sprintf(buf, "+%d", EventScore(levelEvent));
        }
        DrawTextWin(ex, UIS(18), buf, 0x00E060C0u);
    }

    /* v1.8：待上缴碎片 + 绣刀冷却。
       注意：右上角 FPS 那一行也占着 y=UIS(18)，所以这两行要排在它下面，
       否则开 FPS 时会叠在一起。 */
    {
        int hy = UIS(18);
        if (showFps) hy += TextHeightWin() + UIS(6);
        /* v3.0：**删掉 HUD 上的 PENDING N** ——
       玩家实测「一整个小时都是 PENDING 12」，既看不懂又永远不变。
       待上缴碎片只在安全屋/结算页显示就够了。 */


























    }

    {
        int dg = 0;
        for (int i = 0; i < LEECH_MAX; i++) if (leeches[i].active && leeches[i].draining) dg = 1;
        if (dg) DrawTextCenterWin(winW / 2, UIS(18), "DRAIN", 0x00FF3030u);
    }

    /* v2.9：左列改用文字高累加，不再写死 18/52/86 ——
       1080p 下原来 LEVEL 15..80 和 SANITY 44..109 压 36px（报告 严重 #3）。 */
    int hudTH = TextHeightWin();
    /* v2.9：**左上角暗底已彻底删除**（用户要求）。
       那块半透明黑边界很硬，在左上角看起来像一块阴影/渲染错误。
       对比度已由文字颜色本身解决（都提到 4.8:1 以上），不再需要底衬。 */
    int hudY1 = UIS(18) + hudTH + UIS(4);
    sprintf(buf, "SANITY: %.0f%%", sanity);
    DrawTextWin(UIS(20), hudY1, buf, (sanity < 30.0f) ? 0x00FF3C3Cu : 0x0000FFFFu);
    int hudY2 = hudY1 + hudTH + UIS(4);

    if (breakerOpen) sprintf(buf, "POWER ON");
    else sprintf(buf, "FUSES %d/%d", fusesHeld, fusesNeeded);
    DrawTextWin(UIS(20), hudY2, buf, breakerOpen ? 0x0030FF60u : 0x00E8C040u);

    /* 状态效果：常驻显示在 FUSES 下面。
       玩家必须知道自己被怎么了 —— 否则「视野突然变窄」会被当成 bug。
       纵向排布（UIS 是设计分辨率，600 高）：
         86  FUSES
         116 状态效果 1   142 状态效果 2   168 状态效果 3
         196 FLARE / BOTTLE */
    {
        static const char* fxName[FX_COUNT] = { "BLINDED", "SLOWED", "DRAINING" };
        int fy = UIS(18) + TextHeightWin() * 3 + UIS(8);   /* v2.9：跟 FUSES 累加 */
        for (int i = 0; i < FX_COUNT; i++) {
            float s = FxStrength(i);
            if (s <= 0.0f) continue;
            sprintf(buf, "%s %d%%", fxName[i], (int)(s * 100.0f + 0.5f));
            DrawTextWin(UIS(20), fy, buf, 0x00FF6060u);
            fy += hudTH + UIS(4);
        }
    }

    /* v1.14：**右上角**滚动道具栏。
       原来放在左侧中部，和关卡主题名（如 ICE VAULT）撞在一起。
       现在右对齐贴右上角，纵向排列，滚轮切换时平滑滚动。
       加武器只改 ITEM_SLOT_COUNT + 两个数组。 */
    {
        static const char* slotName[ITEM_SLOT_COUNT] = { "FLARE", "BOTTLE", "BRICK", "ACID", "SMOKE",
                                                  "FIREPOT", "GLUE", "MIRROR" };
static const char* slotNameZh[ITEM_SLOT_COUNT] = { "照明弹", "瓶子", "砖头", "酸液", "烟雾弹",
                                                    "燃烧瓶", "胶瓶", "镜面弹" };

        static float scroll = 0.0f;
        static double lastT = 0.0;

        double nowT = g_timeSec;
        float dtS = (float)(nowT - lastT);
        if (dtS <= 0.0f || dtS > 0.30f) dtS = 0.016f;
        lastT = nowT;
        {
            /* v1.17：只给"有货的槽位"排序号，空槽不参与滚动 */
            int cArr[ITEM_SLOT_COUNT] = { flares, bottles, bricks, acids, smokeBombs, firePots, gluePots, mirrors };
            int selVis = 0;
            for (int i = 0; i < ITEM_SLOT_COUNT; i++) {
                if (cArr[i] <= 0) continue;
                if (i == throwSelected) break;
                selVis++;
            }
            scroll += ((float)selVis - scroll) * (dtS * 15.0f);
        }

        int counts[ITEM_SLOT_COUNT];
        counts[0] = flares;
        counts[1] = bottles;
        counts[2] = bricks;
        counts[3] = acids;
        counts[4] = smokeBombs;
    counts[5] = firePots;      /* v2.0：三个新投掷物 */
    counts[6] = gluePots;
    counts[7] = mirrors;

        /* v2.7：判据改成「任一槽有货」。原来只看 flare/bottle，
       所以手上只剩砖头/酸/烟雾弹时整条道具栏会消失。 */
    int anyItem = (flares > 0 || bottles > 0 || bricks > 0 || acids > 0 ||
                   smokeBombs > 0 || firePots > 0 || gluePots > 0 || mirrors > 0);
    int showList = (hudDetail >= 2) || (hudDetail >= 1 && anyItem);
        if (showList) {
            /* v2.6：**只显示三行** —— 上一件 / 当前 / 下一件。
               原来是有几种就铺几行，训练营里 8 种全有时铺满右半边屏幕，
               和最上面那行提示、右边的 WORD 全都压在一起。
               现在固定三行：中间是当前选中的（带高亮），上下各露出一部分。 */
            int rowH = TextHeightWin() + UIS(8);
            int margin = UIS(20);

            /* 收集"有货"的槽 */
            int vis[ITEM_SLOT_COUNT], nvis = 0, curVis = -1;
            for (int i = 0; i < ITEM_SLOT_COUNT; i++) {
                if (counts[i] <= 0) continue;
                if (i == throwSelected) curVis = nvis;
                vis[nvis++] = i;
            }
            if (nvis > 0) {
                if (curVis < 0) curVis = 0;
                /* 锚点：当前行固定在 y0，上面一行露一点，下面一行露一点 */
                int y0 = UIS(44);   /* v2.6：18 太靠边会被切掉顶行，下移到 44 */

                for (int k = -1; k <= 1; k++) {
                    /* v3.0：**件数不够就不画上下行** ——
                       原来 nvis=2 时，上一行和下一行都循环取到同一件，
                       截图里 "1 FLARE x1" 出现两次。 */
                    if (nvis == 1 && k != 0) continue;
                    if (nvis == 2 && k != 0) {
                        /* 两件时：只画当前 + 另一件（各一行），不重复 */
                        if (curVis + k < 0 || curVis + k >= nvis) continue;
                    }
                    int idx = curVis + k;
                    while (idx < 0) idx += nvis;
                    while (idx >= nvis) idx -= nvis;

                    int i = vis[idx];
                    int y = y0 + k * rowH;
                    char line[48];
                    int slotNo = idx + 1;
                    if (slotNo <= 3) sprintf(line, "%d %s x%d", slotNo, Tr(slotName[i], slotNameZh[i]), counts[i]);
                    else             sprintf(line, "  %s x%d", Tr(slotName[i], slotNameZh[i]), counts[i]);

                    int tw = TextWidthWin(line);
                    int x = winW - margin - tw;
                    int selected = (k == 0);
                    if (!selected) {
                        /* 上下两行压暗，表示"还能切过去" */
                        DrawTextWin(x, y, line, 0x00606C6C);
                        continue;
                    }
                    BlendRectWin(x - UIS(6), y - UIS(3),
                                 x + tw + UIS(8), y + TextHeightWin() + UIS(1),
                                 0x00205030u, 0.55f);
                    BlendRectWin(x + tw + UIS(4), y - UIS(3),
                                 x + tw + UIS(7), y + TextHeightWin() + UIS(1),
                                 0x0060FF90u, 0.85f);
                    DrawTextWin(x, y, line, 0x00E8FFE0u);
                }
            }
        }
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
            /* v2.0 修复：原来是 9 次同字号错位绘制 —— 数值上都对不上，
               屏幕上是一堆小字拼贴，而这是整局最关键的两秒提示。
               改成居中单次绘制（字号已经是「一格大字」，够醒目）。 */
            DrawTextCenterWin(winW / 2, ky + (keyH - TextHeightWin()) / 2, prompt, col);
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
        int y = UIS(204);   /* v2.6：左列固定带（状态效果之下） */
        if (levelMod >= 0) {
            DrawTextWin(UIS(20), y, modDesc[levelMod], 0x00C08050u);
        }
        DrawTextWin(UIS(20), y + TextHeightWin() + UIS(6), themeNames[(themeId >= 0 && themeId < THEME_COUNT) ? themeId : 0], 0x00B08040u);
    }

    if (showFps) {
        sprintf(buf, "FPS %d  %.1f/%.1fms  %dx%d", currentFps, lastRenderMs, frameMsMax, renderW, renderH);
        DrawTextWin(winW - UIS(20) - TextWidthWin(buf), UIS(18), buf, 0x00C8C8C8u);
    }

    /* 拾取提示：屏幕中央上方，2秒后淡出 */
    if (pickupMsg && pickupMsgTimer > 0.0f) {
        /* 提示文本统一走窗口内绘制，长文本会被 ClampRowText 之外的地方撑出屏幕 */
        int tw = TextWidthWin(pickupMsg);
        int tx = (winW - tw) / 2;
        /* v3.0：**用「屏幕高度的百分比」而不是 UIS 固定值** ——
           UIS 会跟 renderH 缩放，小窗口时 150 换算出来仍会和藏身提示撞
           （验证报告：800×600 下 "HOLD E - HIDE" 与 "NEED 2 MORE FUSE" 重叠 2408 px²）。
           改成贴在 LEVEL/SANITY/FUSES 那几行下面，永远不碰下半屏。 */
        int ty = UIS(150);
        if (ty < TextHeightWin() * 5) ty = TextHeightWin() * 5;   /* 至少在第 5 行文字以下 */

        float alpha = pickupMsgTimer / 2.0f;
        if (alpha > 1.0f) alpha = 1.0f;
        DrawTextWin(tx, ty, pickupMsg, 0x00E8E060u);
    }

    /* 手电电量 / 体力：左下两条细条，标签按实际文字宽度排版 */
    int barW = UIS(180), barH = UIS(12);
    int labelX = UIS(20);
    /* v3.0：能量条已删，只剩 LIGHT / RUN 两条，按最宽标签算起点 */
    int barX = labelX + TextWidthWin("LIGHT") + UIS(14);
    /* v1.9：两行标签原来固定差 UIS(22)，而一行文字就有 30 逻辑像素高，
       于是 LIGHT 和 RUN 叠在一起（截图里能看到两个词压成一团）。
       现在行距 = 实际文字高度 + 一点间隙，任何窗口尺寸都不会重叠。 */
    int rowGap = TextHeightWin() + UIS(8);
    int y1 = winH - UIS(60) - rowGap;
    int y2 = y1 + rowGap;
    int y0 = y1 - rowGap;         /* v2.3：能量条在手电条**上方** */

    /* v2.5：**游戏内**正在进行校准 → 把条画在屏幕底部中央。
       原来只有安全屋面板里画，而小游戏是从游戏内按空格开的，
       面板根本没画，所以条永远看不见。 */
    if (currentState == STATE_PLAYING && calibActive) {
        /* v2.5：放在屏幕**中上方**的空白带 —— 原来在 winH-150，
           正好和武器行 / ENERGY 那一带重叠，被压住看不见。 */
        /* v2.6：收窄 —— 原来 winW-200 太宽，左端会压到状态效果那几行 */
        int bw2 = winW - UIS(320);
        if (bw2 < UIS(180)) bw2 = winW - UIS(120);
        int bx2 = (winW - bw2) / 2;
        int bh2 = UIS(20);
        int by2 = UIS(146);   /* v2.6：跟着提示上移 */
        /* 明显的外框 + 不透明底，保证一眼能看到 */
        BlendRectWin(bx2 - UIS(10), by2 - UIS(8), bx2 + bw2 + UIS(10), by2 + bh2 + UIS(8),
                     0x000A0E14, 0.94f);
        FrameRectWin(bx2 - UIS(10), by2 - UIS(8), bx2 + bw2 + UIS(10), by2 + bh2 + UIS(8),
                     0x00C8B060u, 0.95f, 1);
        DrawCalibBar(bx2, by2, bw2, bh2);
    }

    /* v2.5：站在安全区里 → 屏幕下方给一行提示（用户要求的那行字） */
    if (playerInSafe && currentState == STATE_PLAYING) {
        const char* hint = NULL;
        unsigned int hcol = 0x00E8E060u;
        static char cool[96];
        if (calibActive) {
            hint = Tr("SPACE: LOCK CALIBRATION", "空格：锁定校准");
            hcol = 0x00FFFFFFu;
        } else if (calibCooldown > 0.0f) {
            sprintf(cool, "%s %.0fs", Tr("CALIB COOLING", "校准冷却"), calibCooldown);
            hint = cool;
            hcol = 0x00C08040u;
        } else {
            hint = Tr("SPACE: START CALIBRATION", "空格：开始校准");
        }
        /* v2.6：安全区里的操作提醒放到**左下角**（RUN 条下面）——
           原来挂在屏幕中间，直接压在校准条上。 */
        {
            const char* shopHint = Tr("T: SHOP    SPACE: CALIBRATE", "T：商店    空格：校准");
            int sx = UIS(20);
            int sy = UIS(424);
            int sw = TextWidthWin(shopHint);
            BlendRectWin(sx - UIS(6), sy - UIS(3), sx + sw + UIS(6),
                         sy + TextHeightWin() + UIS(3), 0x000A0E14, 0.90f);
            DrawTextWin(sx, sy, shopHint, 0x00C8B060u);
        }

        if (hint) {
            /* v2.5：挪到屏幕**最顶端居中** —— 原来在 3/4 高处会和武器行、
               道具栏、建议提示叠在一起。顶上先铺一条暗底防残影。 */
            int tw3 = TextWidthWin(hint);
            /* v2.6：限宽 —— 右侧还有道具栏，长句会横着压过去 */
            int maxW = winW - UIS(180) * 2;
            if (maxW < UIS(200)) maxW = winW - UIS(80);
            if (tw3 > maxW) { hint = Tr("SPACE: CALIBRATE", "空格：校准"); tw3 = TextWidthWin(hint); }
            int hx = (winW - tw3) / 2;
            /* v2.6：固定在**校准条正上方**那一条带里 */
            int hy = UIS(146) - TextHeightWin() - UIS(8);   /* v2.9：贴着条顶往上排 */
            BlendRectWin(hx - UIS(10), hy - UIS(4), hx + tw3 + UIS(10),
                         hy + TextHeightWin() + UIS(4), 0x000A0E14, 0.94f);
            DrawTextWin(hx, hy, hint, hcol);
        }
    }

    /* v2.5：站在安全屋里时把「回智封顶」写出来，否则玩家会以为安全屋坏了 */
    /* v2.6：这段原来画在 winH-60-2*rowGap-TH（≈310），正好压在武器行上。
       现在合并进 SAFE ZONE 那一行（262），不再单独占一条带。 */

    /* v3.0：**能量条已删除** —— 武器取消后这一整套都没意义了 */














    /* ===== v3.0 逃生向 HUD：藏身提示 + 被发现状态 =====
       怪已经打不死了，「它现在有没有在追我」是唯一需要盯的信息。 */
    {
        extern int hideSpotNear, hiddenIn, hoverKind;
        extern Monster monster;
        int hudTH3 = TextHeightWin();
        int hy3 = winH - UIS(60) - rowGap * 2 - hudTH3 - UIS(6);

        /* ① 藏身提示 */
        if (hiddenIn >= 0) {
            /* v3.0：**只保留左下角那一条** ——
               玩家实测中间和左下各有一条 HIDDEN，重复且抢画面中央。 */
        } else if (hideSpotNear >= 0 && currentState == STATE_PLAYING) {
            DrawTextWin(UIS(20), winH - UIS(150),
                        Tr("PRESS E  -  HIDE", "按 E 躲进去"), 0x0090D0F0u);
        }

        /* ② 敌人状态（三态雷达）—— 怪不可杀后这是唯一的威胁指标 */
        /* v3.0：**两种追捕者都要显示** ——
           原来只画寻声怪，光敏怪（lightBeast）完全不在雷达上，
           玩家开着灯被它锁定时屏幕上一点提示都没有。 */
        if (currentState == STATE_PLAYING) {
            extern Monster lightBeast;
            struct { Monster* m; const char* tag; } radar[2] = {
                { &monster,    NULL },
                { &lightBeast, "L" },
            };
            int ry = winH - UIS(150);
            for (int ri2 = 0; ri2 < 2; ri2++) {
                Monster* mm = radar[ri2].m;
                if (!mm->active) continue;
                const char* st = NULL;
                unsigned int sc = 0x0080FF80u;
                /* v3.0：**不播报 AI 状态** ——
                   玩家实测「HUNTING/SEARCHING 等于把怪的 AI 明着播报，恐怖感直接归零」。
                   改成只有「有威胁 / 没威胁」两档氛围提示，不告诉玩家它在干嘛。 */
                if (mm->state == 1)      { st = Tr("!!", "!!");  sc = 0x00FF3020u; }
                else if (mm->state == 2) { st = Tr("?", "?");    sc = 0x00E0A020u; }
                else                      { st = "";             sc = 0x0080C080u; }
                char rb[48];
                if (!st[0]) continue;                  /* 游荡时不显示任何东西 */
                if (radar[ri2].tag) sprintf(rb, "%s [%s]", st, radar[ri2].tag);
                else                sprintf(rb, "%s", st);
                int tw3 = TextWidthWin(rb);
                DrawTextWin(winW - UIS(24) - tw3, ry, rb, sc);
                ry -= TextHeightWin() + UIS(6);
            }
        }
    }

    DrawTextWin(labelX, y1 - UIS(3), "LIGHT", 0x00A0A0A0u);
    /* 手电条颜色：开启=黄绿，关闭但有电=暗黄，无电=暗红 */
    unsigned int lightBarCol;
    if (!flashlightOn && battery <= 0.0f) lightBarCol = 0x00603030u;  /* 无电暗红 */
    else if (!flashlightOn) lightBarCol = 0x00806030u;  /* 关闭暗黄 */
    else lightBarCol = 0x00E8E060u;  /* 开启亮黄绿 */
    float batMax = (float)BatteryMax();
    /* v3.0：改用统一 UiBar */
    UiBar(barX, y1, barW, barH, battery / batMax, UI_BAR_NORMAL, lightBarCol);
    DrawTextWin(barX + barW + UIS(12), y1 - UIS(3), flashlightOn ? "ON" : "OFF",
                flashlightOn ? 0x00E8E060u : 0x00A08080u);

    /* v2.4：看图要花电 —— 开着图就在手电旁边挂一个持续的耗电提示，
       否则玩家只会觉得"电量掉得莫名其妙"。 */
    if (isMapOpen) {
        const char* tag = "MAP -10/s";
        int tx = barX + barW + UIS(12) + TextWidthWin("OFF") + UIS(14);
        if (tx + TextWidthWin(tag) < winW - UIS(8)) {
            DrawTextWin(tx, y1 - UIS(3), tag, 0x00FF9060u);
        }
    }

    /* ===== v3.0 改造树感知系：预知 + 心眼 ===== */
    {
        extern int hiddenIn;
        extern Monster monster;
        extern float posX, posY;
        if (currentState == STATE_PLAYING) {
            /* ① 预知：怪物进 8 格 → 屏幕边缘泛红（不需要看见它） */
            if (BenchPremonition() && monster.active) {
                float dx = monster.x - posX, dy = monster.y - posY;
                float d2 = dx * dx + dy * dy;
                if (d2 < 64.0f) {
                    /* 越近越红：边缘一圈渐变 */
                    float k = 1.0f - d2 / 64.0f;
                    if (k > 1.0f) k = 1.0f;
                    int band = UIS(10);   /* v3.0：40 → 10，原来每帧 160 次半透明叠加，画面抽搐 */
                    for (int i = 0; i < band; i++) {
                        float a = 0.55f * k * (1.0f - (float)i / (float)band);
                        BlendRectWin(0, i, winW, i + 1, 0x00FF0000u, a);
                        BlendRectWin(0, winH - 1 - i, winW, winH - i, 0x00FF0000u, a);
                        BlendRectWin(i, 0, i + 1, winH, 0x00FF0000u, a);
                        BlendRectWin(winW - 1 - i, 0, winW - i, winH, 0x00FF0000u, a);
                    }
                }
            }
            /* ② 心眼：藏身时显示怪物距离 */
            if (BenchInnerEye() && hiddenIn >= 0 && monster.active) {
                float dx = monster.x - posX, dy = monster.y - posY;
                float dm = sqrtf(dx * dx + dy * dy);
                char ibuf[64];
                /* v3.0：挪到**顶部固定带**并加底色 ——
                   原来画在 winH/2+100，被场景/其他覆盖层吃掉，
                   验证报告实测像素差 0/2048（完全没画出来）。 */
                sprintf(ibuf, "%s %.1f", Tr("DIST", "距离"), (double)dm);
                {
                    int tw4 = TextWidthWin(ibuf);
                    int tx4 = (winW - tw4) / 2;
                    int ty4 = UIS(112);
                    BlendRectWin(tx4 - UIS(10), ty4 - UIS(6),
                                 tx4 + tw4 + UIS(10), ty4 + TextHeightWin() + UIS(6),
                                 0x00081018u, 0.88f);
                    DrawTextWin(tx4, ty4, ibuf, 0x0090D0F0u);
                }
            }
        }
    }

    /* v3.0：**噪音环** —— 扔瓶子/敲墙后显示声音强度，2.2 秒衰减 */
    {
        extern float noiseDisplay;
        if (noiseDisplay > 0.01f && currentState == STATE_PLAYING) {
            int nr = UIS(26);   /* v3.0：固定半径，只让亮度随强度变 —— 原来半径也变，四角乱跳 */
            int ncx = winW / 2, ncy = winH / 2;
            unsigned int nc = (noiseDisplay > 0.7f) ? UI_DANGER
                            : (noiseDisplay > 0.4f) ? UI_WARN : UI_INFO;
            /* 四个弧段（简单画法：四角的小方框，避免画圆的开销） */
            for (int a = 0; a < 4; a++) {
                int ax = ncx + ((a & 1) ? nr : -nr) - UIS(3);
                int ay = ncy + ((a & 2) ? nr : -nr) - UIS(3);
                BlendRectWin(ax, ay, ax + UIS(6), ay + UIS(6), nc, 0.85f * noiseDisplay);
            }
        }
    }

    DrawTextWin(labelX, y2 - UIS(3), "RUN", 0x00A0A0A0u);
    UiBar(barX, y2, barW, barH, stamina / StaminaMax(),
          (stamina < 25.0f) ? UI_BAR_WARN : UI_BAR_NORMAL, 0x0040C060u);
    /* v3.0：**武器行已删除** */












    /* v1.15：命中伤害数字（屏幕中央偏下，向上浮 + 淡出） */
    if (0 && lastHitTimer > 0.0f && lastHitDamage > 0) {
        char dbuf[32];
        sprintf(dbuf, "-%d", lastHitDamage);
        float a = lastHitTimer / 0.7f;
        if (a > 1.0f) a = 1.0f;
        int rise = (int)((1.0f - a) * UIS(26));
        int tw2 = TextWidthWin(dbuf);
        int dx2 = winW / 2 - tw2 / 2 + UIS(60);
        int dy2 = winH / 2 + UIS(40) - rise;
        unsigned int dc = lastHitBackstab ? 0x00FFE060u : 0x00F0F0F0u;
        int rr = (int)(((dc >> 16) & 0xFF) * a);
        int gg = (int)(((dc >> 8) & 0xFF) * a);
        int bb = (int)((dc & 0xFF) * a);
        DrawTextWin(dx2, dy2, dbuf, (unsigned int)((rr << 16) | (gg << 8) | bb));
        if (lastHitBackstab) {
            /* v3.0：SNEAK! 已删 —— 怪不可杀，偷袭没有意义 */
        }
    }

    /* v2.6：训练场右侧的「SPECIMEN DMG」绿色面板**已停用**（用户要求）——
       现在命中时屏幕上会直接跳伤害数字，这块面板重复且挡视线。
       代码保留，需要时把 #if 0 改成 #if 1 就能恢复。 */
#if 0
    /* v1.20：训练场右侧伤害面板（布局按文字高度算，杜绝重叠） */
    if (trainingMode) {
        int th = TextHeightWin();
        int pad = UIS(10);
        int pw = UIS(210);
        int ph = pad + th * 5 + UIS(28) + pad;     /* 标题 + 4 行 + 柱状图 + 上下留白 */
        int px2 = winW - pw - UIS(20);
        int py2 = winH / 2 - ph / 2;

        BlendRectWin(px2, py2, px2 + pw, py2 + ph, 0x00000810u, 0.92f);
        FrameRectWin(px2, py2, px2 + pw, py2 + ph, 0x0040C080u, 0.9f, UIS(2));

        char tb[48];
        int ty = py2 + pad;
        DrawTextWin(px2 + pad, ty, "SPECIMEN DMG", 0x0060FFB0u);
        ty += th;

        sprintf(tb, "TOTAL %d", trainingTotal);
        DrawTextWin(px2 + pad, ty, tb, 0x00FFFFFFu);
        ty += th;

        sprintf(tb, "LAST  %d", trainingLast);
        DrawTextWin(px2 + pad, ty, tb, 0x00FFE060u);
        ty += th;

        sprintf(tb, "HITS  %d", trainingCount);
        DrawTextWin(px2 + pad, ty, tb, 0x0090A0A0u);
        ty += th;

        sprintf(tb, "DPS   %d", trainingDps);
        DrawTextWin(px2 + pad, ty, tb, 0x0080D0FFu);
        ty += th + UIS(8);

        /* 柱状图：在 DPS 下方留出完整一块 */
        int barsH = UIS(20);
        int by = ty + barsH;
        for (int i = 0; i < 6; i++) {
            int h2 = trainingHist[i] * UIS(3);
            if (h2 > barsH) h2 = barsH;
            if (h2 <= 0) continue;
            BlendRectWin(px2 + pad + i * UIS(13), by - h2,
                         px2 + pad + i * UIS(13) + UIS(9), by, 0x0060FFB0u, 0.85f);
        }
    }
#endif


    /* 交互提示 + 长按进度 */
    if (hoverKind != 0) {
        char pbuf[64];
        /* v2.7：提示要用当前绑定，改过键之后写死 E 就是骗人 */
        if (hoverKind == 1)
            sprintf(pbuf, "HOLD %s  SEARCH", VkName(g_bindings[ACT_INTERACT]));
        else if (fusesHeld >= fusesNeeded)
            sprintf(pbuf, "HOLD %s  POWER ON", VkName(g_bindings[ACT_INTERACT]));
        else sprintf(pbuf, "NEED %d MORE FUSE", fusesNeeded - fusesHeld);

        int tw = TextWidthWin(pbuf);
        int px = (winW - tw) / 2;
        int py = winH - UIS(150);
        DrawTextWin(px, py, pbuf, (hoverKind == 2 && fusesHeld < fusesNeeded) ? 0x00C05050u : 0x00E8E8E8u);

        int barW = UIS(240), barH = UIS(8);
        int bx = (winW - barW) / 2;
        int by = py + UIS(30);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00202020, 0.8f);
        if (holdingNow) {
            float f = holdTimer / SEARCH_TIME;
            if (f > 1.0f) f = 1.0f;
            BlendRectWin(bx, by, bx + (int)(barW * f), by + barH, 0x00C8E8C8, 0.95f);
        }
        FrameRectWin(bx, by, bx + barW, by + barH, 0x00808080, 0.7f, 1);
    }

    /* v2.6：SAFE ZONE 原来单独占一条带（winH/2+40），会和主题名/武器行抢位置。
       现在并进 FUSES 那一行（见上方的 FUSES 绘制），不再单独画。 */



    /* 蓄力投掷进度条：按住鼠标左键时显示 */
    int mouseFree = (currentState != STATE_PLAYING) || (isMapOpen && mapFullScreen);
    /* 只有选中道具有库存时才显示蓄力条 */
    int hasItem = itemHasStock();   /* v1.18：按当前槽位判断，五种道具都能蓄力 */
    if (isCharging && mouseCharge > 0.0f && !mouseFree && hasItem) {
        int barW = UIS(280), barH = UIS(14);
        int bx = (winW - barW) / 2;
        /* v2.9：上移到 winH/2+48 —— 原来 +100 让百分比落在 420..451，
           正好压住 y=425..456 的武器行（报告 严重 #5）。 */
        int by = winH / 2 + UIS(48);
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
        DrawTextCenterWin(winW / 2, by - UIS(24), Tr("CHARGING KNOCK", "敲击蓄力"), chargeCol);
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
/* 存档失败横幅                                                        */
/* ------------------------------------------------------------------ */
/* 以前写完不检查返回值：写不进去就一声不响，玩家玩一整局、重开全没了，
   还会以为是自己没退出/存档坏了。现在失败就在屏幕顶上钉一条红带，
   把 Win32 错误码和实际尝试的路径一起写出来 —— 至少排查有据可查。 */
static void DrawSaveStatusBanner(void) {
    if (g_saveState != 2) return;
    int th = TextHeightWin();
    int h = th + th / 2;
    char msg[160];
    sprintf(msg, "SAVE FAILED  (ERR %d = %s)", g_saveError,
            (g_saveError == 5) ? "ACCESS DENIED" :
            (g_saveError == 0) ? "UNKNOWN" : (const char*)"SEE CODE");
    ClampRowText(msg, winW - UIS(20));

    /* v2.9：挪到屏幕**底部** —— 原来钉在最顶上，把安全屋的页签整行盖住了
       （用户截图：SAVE FAILED 压在 SHOP/BENCH/TREE/REST 上）。 */
    int by0 = winH - h - th - UIS(4);
    BlendRectWin(0, by0, winW, by0 + h, 0x00240000u, 1.0f);
    BlendRectWin(0, by0, winW, by0 + UIS(2), 0x00C03030u, 0.95f);
    DrawTextCenterWin(winW / 2, by0 + (h - th) / 2, msg, 0x00FF9090u);

    /* 第二行小字：试过的第一个位置，方便玩家/我定位是哪个目录 */
    char pathLine[MAX_PATH + 8];
    sprintf(pathLine, "TRIED: %s", g_savePath[0] ? g_savePath : "(none)");
    ClampRowText(pathLine, winW - UIS(20));
    /* 只在上方还有点空间时才画，小窗口里不硬塞 */
    DrawTextCenterWin(winW / 2, by0 - th - UIS(2), pathLine, 0x00B06060u);
}

/* ------------------------------------------------------------------ */
/* 一帧的呈现                                                          */
/* ------------------------------------------------------------------ */
void RenderFrame(void) {
    UiLayoutBegin();
    HDC hdc = GetDC(g_hwnd);
    if (!hdc) return;

    int showScene = (currentState == STATE_PLAYING) ||
                    (currentState == STATE_PAUSED) ||
                    (currentState == STATE_SETTINGS && prevState == STATE_PAUSED) ||
                    (currentState == STATE_SAFEHOUSE);

    if (showScene) {
        /* v2.1：相机抖动（偏移玩家位置再还原，所有投影自动跟着抖） */
        float _sx = posX, _sy = posY;
        posX += g_shakeOffX;
        posY += g_shakeOffY;
        Render3D();
        posX = _sx;
        posY = _sy;
        if (currentState == STATE_PLAYING && isMapOpen) RenderMap();
        /* 跳脸虫：屏幕空间 overlay，盖在场景之上、HUD 之下 */
        if (currentState == STATE_PLAYING) DrawParasiteSprite();
        if (flashTime > 0.0f)
            memset(pixels, 0xFF, (size_t)renderW * renderH * sizeof(unsigned int));

        /* v2.1：受伤红闪（越靠边缘越红）+ 枪口闪光 */
        if (hitFlash > 0.0f) {
            float a = hitFlash * 0.42f;
            int cxx = renderW / 2, cyy = renderH / 2;
            float maxd = (float)(cxx * cxx + cyy * cyy);
            for (int y = 0; y < renderH; y++) {
                unsigned int* row = pixels + (size_t)y * renderW;
                for (int x = 0; x < renderW; x++) {
                    float dx = (float)(x - cxx), dy = (float)(y - cyy);
                    float t = (dx * dx + dy * dy) / maxd;      /* 0 中心 -> 1 角落 */
                    if (t < 0.35f) continue;
                    float k = (t - 0.35f) / 0.65f;
                    float al = a * k;
                    unsigned int d = row[x];
                    int dr = (int)((d >> 16) & 0xFF), dg = (int)((d >> 8) & 0xFF), db = (int)(d & 0xFF);
                    int nr = (int)(dr * (1.0f - al) + 220 * al);
                    int ng = (int)(dg * (1.0f - al) + 20 * al);
                    int nb = (int)(db * (1.0f - al) + 20 * al);
                    row[x] = ((unsigned int)nr << 16) | ((unsigned int)ng << 8) | (unsigned int)nb;
                }
            }
        }
        if (muzzleFlash > 0.0f) {
            /* 屏幕下方中央的一团暖光，像枪口焰 */
            int cxx = renderW / 2, cyy = (int)(renderH * 0.86f);
            int rad = (int)(renderW * 0.10f * (0.5f + muzzleFlash * 0.5f));
            if (rad < 6) rad = 6;
            for (int dy = -rad; dy <= rad; dy++) {
                int y = cyy + dy;
                if (y < 0 || y >= renderH) continue;
                unsigned int* row = pixels + (size_t)y * renderW;
                for (int dx = -rad; dx <= rad; dx++) {
                    int x = cxx + dx;
                    if (x < 0 || x >= renderW) continue;
                    float d2 = (float)(dx * dx + dy * dy) / (float)(rad * rad);
                    if (d2 > 1.0f) continue;
                    float al = (1.0f - d2) * muzzleFlash * 0.55f;
                    unsigned int d = row[x];
                    int dr = (int)((d >> 16) & 0xFF), dg = (int)((d >> 8) & 0xFF), db = (int)(d & 0xFF);
                    int nr = (int)(dr * (1.0f - al) + 255 * al);
                    int ng = (int)(dg * (1.0f - al) + 225 * al);
                    int nb = (int)(db * (1.0f - al) + 140 * al);
                    row[x] = ((unsigned int)nr << 16) | ((unsigned int)ng << 8) | (unsigned int)nb;
                }
            }
        }
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
        /* 改绑提示框：最顶层，盖住所有菜单 */
        DrawRebindOverlay();
        /* 存档写不进去必须吼出来（见 save.c 的说明） */
        DrawSaveStatusBanner();
    } else if (isMapOpen && mapFullScreen) {
        /* 全屏地图：不画 HUD —— 整个屏幕就是地图，别在上面压一堆数值。
           小窗地图则照常画 HUD（它只是右下角一个叠加层）。 */
    } else {
        DrawHUD();
    }

    /* v3.0：**帧末覆盖层** —— 不管什么状态都要画
       （锁盘只在游戏中打开；藏身遮罩也只在游戏中生效）。
       原来这两块写在 if (currentState != STATE_PLAYING) 分支里，
       结果玩游戏时锁盘一帧都不显示、藏身也没有任何视觉反馈。 */
    DrawLockOverlay();
    {
        extern int hiddenIn;
        if (hiddenIn >= 0) {
            /* v3.0：**藏进去要看不出外面** ——
               玩家实测原来的 winH/5 上下边太薄，"走廊、怪、墙全都看得见"，
               只像盖了块棕色矩形。改成上下各 37%（只留一条 26% 高的横缝），
               左右各 18%，模拟从柜门缝往外看。 */
            int bandT = winH * 37 / 100;
            int bandS = winW * 18 / 100;
            BlendRectWin(0, 0, winW, bandT, 0x00000000u, 0.96f);
            BlendRectWin(0, winH - bandT, winW, winH, 0x00000000u, 0.96f);
            BlendRectWin(0, 0, bandS, winH, 0x00000000u, 0.90f);
            BlendRectWin(winW - bandS, 0, winW, winH, 0x00000000u, 0.90f);
            /* 四角再压一层，避免从对角溜出去 */
            BlendRectWin(0, 0, bandS, bandT, 0x00000000u, 1.0f);
            BlendRectWin(winW - bandS, 0, winW, bandT, 0x00000000u, 1.0f);
            BlendRectWin(0, winH - bandT, bandS, winH, 0x00000000u, 1.0f);
            BlendRectWin(winW - bandS, winH - bandT, winW, winH, 0x00000000u, 1.0f);
        }
    }


    /* v1.8：等比呈现 + 黑边（letterbox）。
       原来把图直接拉满客户区，在 21:9 / 16:9 宽屏上文字会被横向拉胖。
       现在按逻辑画布比例等比缩放并居中，其余填黑 —— 任何分辨率都不变形。 */
    {
        RECT cr;
        GetClientRect(g_hwnd, &cr);
        int cw = cr.right - cr.left;
        int ch = cr.bottom - cr.top;
        if (cw <= 0 || ch <= 0) { ReleaseDC(g_hwnd, hdc); return; }

        /* v1.8：改成**真铺满**（不再等比例留黑边）——
           玩家要的是最大化后整屏都是画面，接受轻微拉伸。 */
        int dw = cw, dh = ch;
        int dx = 0, dy = 0;

        /* 离屏合成：黑底 + 缩放后的画面先在内存 DC 里拼好，
           再一次性 BitBlt 到窗口。直接往窗口 DC 先填黑再画内容会闪
           （两次呈现之间屏幕上看得到黑底）。 */
        static HDC g_bufDC = NULL;
        static HBITMAP g_bufBmp = NULL;
        static int g_bufW = -1, g_bufH = -1;
        if (!g_bufDC || g_bufW != cw || g_bufH != ch) {
            if (g_bufBmp) { DeleteObject(g_bufBmp); g_bufBmp = NULL; }
            if (g_bufDC) { DeleteDC(g_bufDC); g_bufDC = NULL; }
            g_bufDC = CreateCompatibleDC(hdc);
            if (g_bufDC) {
                g_bufBmp = CreateCompatibleBitmap(hdc, cw, ch);
                SelectObject(g_bufDC, g_bufBmp);
                g_bufW = cw; g_bufH = ch;
            }
        }
        /* v2.0 性能：画面已经铺满整个客户区（dw==cw、dh==ch、dx==dy==0），
           那么"填黑 + 画进离屏 + 整屏 BitBlt"三步里，填黑 100% 被覆盖、
           BitBlt 又是一次整屏拷贝 —— 1080p 下光这两步就 ~10ms。
           直接画到窗口 DC。只有真出现黑边（未来若恢复等比缩放）才走离屏路径。 */
        int fullBleed = (dx == 0 && dy == 0 && dw == cw && dh == ch);
        if (fullBleed) {
            /* v2.9：HALFTONE 双线性缩放。
               原来用默认 BLACKONWHITE（块复制），16x24 的字按 1.31/1.82/2.73
               非整数放大时，2px 笔画会变 2/3px 或 5/6px 交替 → 字发毛
               （清晰度审查 #7）。 */
            /* v2.9：**COLORONCOLOR（近邻）**，不再用 HALFTONE。
               HALFTONE 在 GDI 里是 4x4 抖动，1080p 全屏每帧要 10~20ms ——
               这就是「全屏帧数严重下降」的原因。近邻缩放快 30 倍，
               而且点阵字保持硬边，符合像素风。 */
            SetStretchBltMode(hdc, COLORONCOLOR);
            if (g_useDIB)
                StretchBlt(hdc, 0, 0, cw, ch, g_memDC, 0, 0, renderW, renderH, SRCCOPY);
            else
                StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, renderW, renderH,
                              pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
        } else if (g_bufDC) {
            RECT all = { 0, 0, cw, ch };
            HBRUSH blk = (HBRUSH)GetStockObject(BLACK_BRUSH);
            FillRect(g_bufDC, &all, blk);
            if (g_useDIB)
                StretchBlt(g_bufDC, dx, dy, dw, dh, g_memDC, 0, 0, renderW, renderH, SRCCOPY);
            else
                StretchDIBits(g_bufDC, dx, dy, dw, dh, 0, 0, renderW, renderH,
                              pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
            BitBlt(hdc, 0, 0, cw, ch, g_bufDC, 0, 0, SRCCOPY);
        }
    }

    ReleaseDC(g_hwnd, hdc);
}


/* ================================================================== */
/* v2.2 UI 主题实现（合并进 ui.c，因为要用到本文件的 static 绘制原语）    */
/* ================================================================== */
#include "ui_theme.h"

#define UI_INSET 20

void UiInit(void) { }

void UiPanel9(int x0, int y0, int x1, int y1, int hot) {
    if (!SpritesReady()) return;
    int id = hot ? SPR_UI_PANEL_HOT : SPR_UI_PANEL;
    int S = 128;
    int c = UI_INSET;
    int w = x1 - x0, h = y1 - y0;
    if (w < c * 2 + 2 || h < c * 2 + 2) return;
    DrawSpriteSub(id, 0, 0, c, c, x0, y0, c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, S - c, 0, c, c, x0 + w - c, y0, c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, 0, S - c, c, c, x0, y0 + h - c, c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, S - c, S - c, c, c, x0 + w - c, y0 + h - c, c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, c, 0, S - 2 * c, c, x0 + c, y0, w - 2 * c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, c, S - c, S - 2 * c, c, x0 + c, y0 + h - c, w - 2 * c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, 0, c, c, S - 2 * c, x0, y0 + c, c, h - 2 * c, 1.0f, 1.0f);
    DrawSpriteSub(id, S - c, c, c, S - 2 * c, x0 + w - c, y0 + c, c, h - 2 * c, 1.0f, 1.0f);
    DrawSpriteSub(id, c, c, S - 2 * c, S - 2 * c,
                  x0 + c, y0 + c, w - 2 * c, h - 2 * c, 1.0f, 1.0f);
}

void UiCursor(int x, int y, int size, float alpha) {
    if (!SpritesReady()) return;
    if (size < 6) size = 6;
    if (size > 48) size = 48;
    DrawSprite(SPR_UI_CURSOR, x, y, size, size, 1.0f, alpha, 0, 0.0f);
}

void UiRowSel(int x0, int y0, int x1, int y1, float alpha) {
    int h = y1 - y0;
    if (h < 4) return;
    if (SpritesReady())
        DrawSpriteSub(SPR_UI_ROW, 0, 0, 128, 128, x0, y0, x1 - x0, h, 1.0f, alpha);
    int barW = h / 8;
    if (barW < 2) barW = 2;
    BlendRectWin(x0, y0 + h / 6, x0 + barW, y1 - h / 6, UI_ACCENT, alpha * 0.95f);
    int cs = h * 2 / 3;
    if (cs < 8) cs = 8;
    UiCursor(x0 + barW + 2, y0 + (h - cs) / 2, cs, alpha);
}

/* v2.2：分组细线 —— 中间亮两头渐隐，用于把列表分成逻辑段 */

/* ==================== v3.0 统一组件 ====================
 * 勘查报告指出：6 套手写面板、3 套选中态、7 条手写进度条。
 * 这三个函数把它们收编，新界面只允许调用这三个。
 * ==================================================== */

void UiPanel(int x0, int y0, int x1, int y1, int kind) {
    if (kind == UI_PANEL_BARE) return;
    unsigned int edge, fill;
    switch (kind) {
    case UI_PANEL_HOT:    edge = UI_EDGE_HOT;  fill = UI_BG_PANEL; break;
    case UI_PANEL_DANGER: edge = UI_DANGER;    fill = 0x00240000u;  break;
    default:              edge = UI_EDGE;      fill = UI_BG_PANEL;  break;
    }
    BlendRectWin(x0, y0, x1, y1, fill, 0.97f);
    FrameRectWin(x0, y0, x1, y1, edge, 0.95f, 1);
}

void UiRow(int x0, int y0, int x1, int y1, int state, float alpha) {
    switch (state) {
    case UI_ROW_SEL:
        BlendRectWin(x0, y0, x1, y1, UI_BG_SEL, 0.85f * alpha);
        BlendRectWin(x0 - UIS(SP1), y0, x0 - UIS(SP1) + UIS(3), y1, UI_ACCENT, 0.95f);
        break;
    case UI_ROW_HOVER:
        BlendRectWin(x0, y0, x1, y1, UI_BG_HOVER, 0.55f * alpha);
        FrameRectWin(x0, y0, x1, y1, UI_INFO, 0.7f, 1);
        break;
    case UI_ROW_DISABLED:
        BlendRectWin(x0, y0, x1, y1, UI_BG_LOCKED, 0.60f * alpha);
        break;
    case UI_ROW_LOCKED:
        BlendRectWin(x0, y0, x1, y1, UI_BG_LOCKED, 0.85f * alpha);
        break;
    default:
        BlendRectWin(x0, y0, x1, y1, UI_BG_ROW, 0.45f * alpha);
        break;
    }
}

void UiBar(int x, int y, int w, int h, float frac, int kind, unsigned int col) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    unsigned int c = col;
    if (kind == UI_BAR_DANGER) c = UI_DANGER;
    else if (kind == UI_BAR_WARN) c = UI_WARN;
    BlendRectWin(x, y, x + w, y + h, 0x00202020u, 0.80f);
    if (frac > 0.0f) BlendRectWin(x, y, x + (int)((float)w * frac), y + h, c, 0.95f);
    FrameRectWin(x, y, x + w, y + h, UI_EDGE_DIM, 0.7f, 1);
}

void UiSectionRule(int cx, int y, int w) {
    int th = TextHeightWin() / 10;
    if (th < 2) th = 2;
    int half = w / 2;
    BlendRectWin(cx - half, y, cx + half, y + th, UI_ACCENT, 0.18f);
    BlendRectWin(cx - half / 4, y, cx + half / 4, y + th, UI_ACCENT, 0.55f);
}

/* 页脚分隔线 —— 比分组线更暗，贴着面板底部 */
void UiFooterRule(int x0, int x1, int y) {
    int th = TextHeightWin() / 12;
    if (th < 2) th = 2;
    BlendRectWin(x0, y, x1, y + th, UI_INK_DIM, 0.55f);
}

/* 面板四角的 L 形角标（9-slice 的螺丝之外再补一层"仪表面板"感） */
void UiCornerMarks(int x0, int y0, int x1, int y1) {
    int arm = TextHeightWin();
    int th = TextHeightWin() / 9;
    if (th < 2) th = 2;
    /* 只画内侧两个角，和 9-slice 的螺丝错开 */
    BlendRectWin(x0 + arm, y0 + arm / 2, x0 + arm * 2, y0 + arm / 2 + th, UI_ACCENT, 0.35f);
    BlendRectWin(x0 + arm, y0 + arm / 2, x0 + arm / 2 + th, y0 + arm, UI_ACCENT, 0.35f);
    BlendRectWin(x1 - arm * 2, y1 - arm / 2, x1 - arm, y1 - arm / 2 + th, UI_ACCENT, 0.35f);
    BlendRectWin(x1 - arm / 2 - th, y1 - arm, x1 - arm / 2, y1 - arm / 2, UI_ACCENT, 0.35f);
}

void UiTitle(int cx, int y, const char* s) {
    DrawTextCenterWin(cx, y, s, UI_INK_TITLE);
}

void UiLabel(int x, int y, unsigned int col, const char* s) {
    DrawTextWin(x, y, s, col);
}

void UiHint(int cx, int y, const char* s) {
    DrawTextCenterWin(cx, y, s, UI_INK_DIM);
}

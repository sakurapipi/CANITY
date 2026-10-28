/* ------------------------------------------------------------------ */
/* v2.0 中文字形渲染                                                    */
/*                                                                      */
/* 游戏的字库是 32..126 的位图字体，画不了中文。这里用 GDI 现搓一层：      */
/* 把 UTF-8 字符串画到一张 32bpp DIB 上，读回覆盖率，再按目标颜色混合     */
/* 进渲染缓冲。对上层完全透明 —— DrawTextWin/TextWidthWin 里有非 ASCII    */
/* 就自动走这条路，调用点一行都不用改。                                  */
/* ------------------------------------------------------------------ */
#include <windows.h>
#include <string.h>
#include "common.h"
#include "render.h"

extern unsigned int* pixels;
extern int renderW, renderH;
extern int winW, winH;

static int   cjkInited = 0;
static int   cjkPixH = 0;            /* 当前字体像素高度 */
static HFONT cjkFont = NULL;
static HDC   cjkDC = NULL;
static HBITMAP cjkBmp = NULL;
static unsigned int* cjkBits = NULL;
static int   cjkBmpW = 0, cjkBmpH = 0;

int CjkHasNonAscii(const char* s) {
    if (!s) return 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++)
        if (*p >= 0x80) return 1;
    return 0;
}

/* 目标像素高度：和位图字体在渲染分辨率下的高度一致 */
static int CjkPixelHeight(void) {
    int h = renderH / 22;            /* 约等于 TH 在渲染分辨率下的高度 */
    if (h < 10) h = 10;
    if (h > 48) h = 48;
    return h;
}

static void CjkEnsureBitmap(int w, int h) {
    if (cjkBmp && w <= cjkBmpW && h <= cjkBmpH) return;
    if (cjkBmp) {
        /* v2.0 修复：对象还被 SelectObject 在 DC 里就 DeleteObject ——
           Windows 会拒绝删除，句柄直接泄漏（每次改分辨率漏一个 HBITMAP）。 */
        if (cjkDC) SelectObject(cjkDC, (HGDIOBJ)NULL);   /* 取消选中位图 */
        DeleteObject(cjkBmp);
        cjkBmp = NULL;
        cjkBits = NULL;
    }
    if (!cjkDC) cjkDC = CreateCompatibleDC(NULL);
    if (!cjkDC) return;

    int nw = w < 512 ? 512 : w + 64;
    int nh = h < 64 ? 64 : h + 16;

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = nw;
    bmi.bmiHeader.biHeight = -nh;            /* 自上而下 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    cjkBmp = CreateDIBSection(cjkDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!cjkBmp || !bits) { cjkBmp = NULL; return; }
    SelectObject(cjkDC, cjkBmp);
    cjkBits = (unsigned int*)bits;
    cjkBmpW = nw;
    cjkBmpH = nh;
}

static void CjkEnsureFont(void) {
    int ph = CjkPixelHeight();
    if (cjkInited && ph == cjkPixH && cjkFont) return;
    if (cjkFont) {
        /* v2.0 修复：字体还被选中就删 → 泄漏一个 HFONT（每次改分辨率一个） */
        if (cjkDC) SelectObject(cjkDC, GetStockObject(SYSTEM_FONT));
        DeleteObject(cjkFont);
        cjkFont = NULL;
    }
    if (!cjkDC) {
        cjkDC = CreateCompatibleDC(NULL);
        if (!cjkDC) return;
    }
    /* 优先雅黑，退到系统默认无衬线 */
    cjkFont = CreateFontW(-ph, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                          L"Microsoft YaHei");
    if (!cjkFont)
        cjkFont = CreateFontW(-ph, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                              L"Arial");
    if (cjkFont) SelectObject(cjkDC, cjkFont);
    cjkPixH = ph;
    cjkInited = 1;
}

/* 渲染分辨率下的宽度 */
int CjkTextWidth(const char* utf8) {
    if (!utf8 || !*utf8) return 0;
    CjkEnsureFont();
    if (!cjkDC || !cjkFont) return 0;
    wchar_t wb[512];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wb, 512);
    if (n <= 1) return 0;
    SIZE sz;
    if (!GetTextExtentPoint32W(cjkDC, wb, n - 1, &sz)) return 0;
    return (int)sz.cx;
}

/* 画：x,y 是**渲染坐标**（调用方转换好） */
void CjkDrawRaw(int x, int y, const char* utf8, unsigned int color) {
    if (!utf8 || !*utf8 || !pixels) return;
    CjkEnsureFont();
    if (!cjkDC || !cjkFont) return;

    wchar_t wb[512];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wb, 512);
    if (n <= 1) return;

    SIZE sz;
    if (!GetTextExtentPoint32W(cjkDC, wb, n - 1, &sz)) return;
    int tw = sz.cx + 4;
    int th = cjkPixH + 6;
    CjkEnsureBitmap(tw, th);
    if (!cjkBmp || !cjkBits) return;

    /* 清成黑色（覆盖率 0） */
    memset(cjkBits, 0, (size_t)cjkBmpW * cjkBmpH * 4);

    SetBkMode(cjkDC, TRANSPARENT);
    SetTextColor(cjkDC, RGB(255, 255, 255));
    TextOutW(cjkDC, 2, 2, wb, n - 1);

    int cr = (int)((color >> 16) & 0xFF);
    int cg = (int)((color >> 8) & 0xFF);
    int cb = (int)(color & 0xFF);

    for (int row = 0; row < th; row++) {
        int py = y + row;
        if (py < 0 || py >= renderH) continue;
        unsigned int* dst = pixels + (size_t)py * renderW;
        unsigned int* src = cjkBits + (size_t)row * cjkBmpW;
        for (int col = 0; col < tw; col++) {
            int px = x + col;
            if (px < 0 || px >= renderW) continue;
            unsigned int s = src[col];
            int b = (int)(s & 0xFF);
            int g = (int)((s >> 8) & 0xFF);
            int r = (int)((s >> 16) & 0xFF);
            int a = (r + g + b) / 3;
            if (a < 8) continue;
            if (a > 255) a = 255;
            unsigned int d = dst[px];
            int dr = (int)((d >> 16) & 0xFF);
            int dg = (int)((d >> 8) & 0xFF);
            int db = (int)(d & 0xFF);
            int nr = (cr * a + dr * (255 - a)) / 255;
            int ng = (cg * a + dg * (255 - a)) / 255;
            int nb = (cb * a + db * (255 - a)) / 255;
            dst[px] = ((unsigned int)nr << 16) | ((unsigned int)ng << 8) | (unsigned int)nb;
        }
    }
}

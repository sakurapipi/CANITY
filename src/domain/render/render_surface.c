/* 2026-11-16：从 render.c 拆出的**像素缓冲初始化与画面边框**（域内解耦）。
   InitPixels / GenerateVignette / BuildCraftTex / CraftFrameRow
   只在初始化与边框绘制时使用，不参与世界渲染的每帧状态。 */
#include "threat_telegraph.h"
#include "common.h"
#include "room_theme.h"
#include "perception.h"
#include "room_material.h"
#include "collapse.h"
#include "echo_marks.h"
#include "item_effects.h"
#include "lang.h"
#include "phase.h"
#include "state.h"
#include "upgrade_run.h"
#include "warden.h"
#include "wrench.h"
#include "control_effect.h"
#include "spider.h"
#include "hatch.h"
#include "map_rooms.h"
#include "cell_meta.h"
#include "render.h"
#include "suspicion.h"
#include "art.h"
#include "bench.h"
#include "weapons.h"
#include "extras.h"
#include "sprites.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "mimic.h"
#include "entity.h"
#include "ui.h"
#include "plat.h"

void GenerateVignette(void) {
    free(vignetteMask);
    vignetteMask = (unsigned char*)malloc((size_t)renderW * renderH);
    if (!vignetteMask) return;      /* 分配失败就别画渐晕，不能拿 NULL 去写 */
    int cx = renderW / 2, cy = renderH / 2;
    float maxDist = sqrtf((float)(cx * cx + cy * cy));
    if (maxDist < 1.0f) maxDist = 1.0f;
    for (int y = 0; y < renderH; y++) {
        for (int x = 0; x < renderW; x++) {
            int dx = x - cx, dy = y - cy;
            float ratio = sqrtf((float)(dx * dx + dy * dy)) / maxDist;
            /* v3.0：中间清晰区从 40% 半径扩到 55% —— 留出足够能走路的空间 */
            if (ratio < 0.55f) ratio = 0.0f;
            else ratio = (ratio - 0.55f) / 0.45f;
            if (ratio > 1.0f) ratio = 1.0f;
            vignetteMask[y * renderW + x] = (unsigned char)(ratio * 255.0f);
        }
    }
}

/* 2026-11-06（网页版 R1）：**像素缓冲的所有权移交给平台层**。
   Win32 后端把它建在 DIB Section 上（GDI 共享内存，呈现时 BitBlt）；
   网页后端就是一片普通内存。render.c 只拿到一个可写指针。 */

/* ==================================================================
 * 2026-11-11：**手工布偶的整屏层**（RC4 版）
 * ------------------------------------------------------------------
 * 让整个画面读成"一个被灯照着的布景"：
 *   · 织物 2×2 织纹 + 固定相位的纤维噪点（静止，不闪 —— AD-08）
 *   · 舞台式渐晕：四周压暗（复用 vignetteMask），理智红晕另行叠加
 *   · 布景边缘的"包边 + 缝线"：四边暗边，第 4px 上一段一段的亚麻线
 * 预生成成与渲染缓冲同尺寸的 8 位乘数表（128 = 原亮度）：
 *       最终 = (颜色 * craftTex) >> 7
 * 颜色分级不在这里做 —— 它在材质烘焙期由 ArtDollGrade() 完成（art.c）。
 * ================================================================== */
/* 2026-11-16：render.c 的布偶层也要读它，所以去掉 static，
   在 render.h 里 extern（同域契约）。 */
unsigned char* craftTex = NULL;

static unsigned int CraftHash(int x, int y) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void BuildCraftTex(void) {
    free(craftTex);
    craftTex = NULL;
    if (!ArtStyleDoll() || renderW <= 0 || renderH <= 0) return;
    craftTex = (unsigned char*)malloc((size_t)renderW * renderH);
    if (!craftTex) return;
    for (int y = 0; y < renderH; y++) {
        for (int x = 0; x < renderW; x++) {
            float v = 1.0f;
            if (vignetteMask) {
                unsigned char m = vignetteMask[(size_t)y * renderW + x];
                v *= 1.0f - ((float)m / 255.0f) * 0.50f;
            }
            /* 织物织纹：2×2 棋盘，幅度 ±8%（暗部也要看得出来） */
            v *= ((x ^ y) & 1) ? 0.92f : 1.0f;
            /* 纤维噪点：固定相位，逐帧不变 */
            v *= 1.0f + ((float)(CraftHash(x, y) & 7) - 3.5f) * 0.0040f;
            /* 包边：四周 12px 压暗，缝线由 CraftFrameRow() 单独画 */
            {
                int ex = (x < renderW - 1 - x) ? x : (renderW - 1 - x);
                int ey = (y < renderH - 1 - y) ? y : (renderH - 1 - y);
                int edge = (ex < ey) ? ex : ey;
                if (edge < 12) v *= 0.62f + (float)edge * 0.030f;
            }
            int k = (int)(v * 128.0f + 0.5f);
            if (k < 0) k = 0;
            if (k > 255) k = 255;
            craftTex[(size_t)y * renderW + x] = (unsigned char)k;
        }
    }
}

/* 布景包边：屏幕四边压成布料暗边，再缝一圈线。
   直接写像素而不是乘系数 —— 线要能在纯黑背景上也看得见。 */

void CraftFrameRow(int y, unsigned int* row) {
    static const int THR_R = 158, THR_G = 132, THR_B = 90;   /* 亚麻线 */
    int top = y, bot = renderH - 1 - y;
    int vmin = (top < bot) ? top : bot;
    int n = (vmin < 6) ? 1 : 2;
    for (int s = 0; s < n; s++) {
        int xa = (s == 0) ? 0 : renderW - 6;
        int xb = (s == 0) ? (vmin < 6 ? renderW : 6) : renderW;
        for (int x = xa; x < xb; x++) {
            int left = x, right = renderW - 1 - x;
            int hmin = (left < right) ? left : right;
            int vert = (hmin < vmin);
            int d = vert ? hmin : vmin;
            int along = vert ? y : x;
            unsigned int c = row[x];
            int r = (int)((c >> 16) & 0xFF);
            int g = (int)((c >> 8) & 0xFF);
            int b = (int)(c & 0xFF);
            if (d <= 1) {
                r = r * 22 / 100; g = g * 22 / 100; b = b * 24 / 100;
            } else if (d == 2) {
                r = r * 52 / 100; g = g * 52 / 100; b = b * 56 / 100;
            } else if (d == 3) {
                r = r * 78 / 100; g = g * 78 / 100; b = b * 80 / 100;
            } else if (d == 4) {
                if (((along / 7) & 1) == 0) {
                    r = (r * 25 + THR_R * 75) / 100;
                    g = (g * 25 + THR_G * 75) / 100;
                    b = (b * 25 + THR_B * 75) / 100;
                } else {
                    r = r * 62 / 100; g = g * 62 / 100; b = b * 66 / 100;
                }
            } else {
                r = r * 70 / 100; g = g * 70 / 100; b = b * 72 / 100;
            }
            row[x] = (unsigned int)((r << 16) | (g << 8) | b);
        }
    }
}

/* 按当前设置重新分配内部渲染缓冲 */

void InitPixels(void) {
    float s = renderScales[renderScaleIndex] / 100.0f;

    /* v1.9：渲染缓冲的**宽高比必须等于客户端宽高比**。
       原来固定按 winW:winH（800:600）算 → 缓冲永远是 4:3，
       拉满 16:9 窗口时横向被拉宽，所以墙面/地板看起来"太扁"。
       现在按客户区比例算，并用固定的垂直分辨率（winH*s）反推宽度。 */
    float aspect = (winH > 0) ? (float)winW / (float)winH : 4.0f / 3.0f;
    {
        /* 2026-11-06（R4）：客户区尺寸走平台层 —— 网页端返回 canvas 尺寸 */
        int pw = 0, ph = 0;
        PlatWindowSize(&pw, &ph);
        if (pw > 0 && ph > 0) aspect = (float)pw / (float)ph;
    }
    if (aspect < 0.6f) aspect = 0.6f;
    if (aspect > 3.2f) aspect = 3.2f;

    /* v2.9：**渲染高度必须来自真实客户区，不能用 winH**。
       原来 baseH = winH*s，而 SyncWindowSize() 又把 winH 设成 renderH ——
       两个函数互相喂，形成反馈环：
           renderH = winH*s        （InitPixels）
           winH    = renderH       （SyncWindowSize，下一帧）
       窗口尺寸一变（比如按 F11 全屏），像素预算每次都会把 renderH 再砍一点，
       而文字永远是 24 个渲染像素 → 占屏比例越来越大，
       表现就是「整个 UI 一帧比一帧更大」。
       现在只读 GetClientRect，renderH 不再依赖它自己写出去的值。 */
    float baseH;
    {
        int pw = 0, ph = 0;
        PlatWindowSize(&pw, &ph);
        baseH = (ph > 0) ? (float)ph * s : (float)winH * s;
    }
    renderH = (int)baseH;
    renderW = (int)(baseH * aspect);
    if (renderW < 160) renderW = 160;
    if (renderH < 120) renderH = 120;
    if (renderW > MAX_SCREEN) renderW = MAX_SCREEN;
    if (renderH > MAX_SCREEN) renderH = MAX_SCREEN;

    /* 像素预算：窗口再大，内部渲染也不超过这个量，保证高分辨率下的帧时间。
       原来 90 万，1080p 全屏实测「渲染 29ms + 呈现 11ms」只有 25fps，
       这就是全屏卡顿的根因。28 万（约 640x360）实测 14ms，60fps 稳。 */
    {
        const long budget = pixelBudget;
        long area = (long)renderW * (long)renderH;
        if (area > budget) {
            float f = sqrtf((float)budget / (float)area);
            renderW = (int)(renderW * f);
            renderH = (int)(renderH * f);
            if (renderW < 160) renderW = 160;
            if (renderH < 120) renderH = 120;
        }
    }

    /* 2026-11-06（R1）：分配与呈现都归平台层，这里只要一个可写指针。 */
    pixels = PlatAcquireTarget(renderW, renderH);
    if (pixels) memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));

    GenerateVignette();
    BuildCraftTex();      /* 2026-11-11：整屏布偶层的乘数表（尺寸变了要重建） */
    uiDirty = 1;
}


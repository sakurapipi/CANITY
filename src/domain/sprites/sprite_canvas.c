#include "sprite_bake.h"
#include "common.h"
#include "render.h"
#include "art.h"

static SpriteBakeCanvasState canvas = {1, {0}};
#undef RASTER_W
#undef RASTER_H
#define RASTER_W (CANVAS_W * canvas.scale)
#define RASTER_H (CANVAS_H * canvas.scale)
SpriteBakeCanvasState* SpriteBakeCanvas(void) { return &canvas; }



void cvClear(void) {
    memset(canvas.pixels, 0, sizeof(canvas.pixels));
}

static inline void cvPxRaw(int x, int y, unsigned int col, float a) {
    if (x < 0 || x >= RASTER_W || y < 0 || y >= RASTER_H) return;
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    unsigned int* p = &canvas.pixels[(size_t)y * RASTER_W + x];
    unsigned int src = col & 0x00FFFFFFu;
    if (a >= 0.999f) { *p = 0xFF000000u | src; return; }
    unsigned int old = *p;
    if ((old >> 24) == 0) {
        *p = ((unsigned int)(a * 255.0f) << 24) | src;
        return;
    }
    float oa = (float)(old >> 24) / 255.0f;
    float na = a + oa * (1.0f - a);
    if (na <= 0.0001f) { *p = 0; return; }
    int orr = (old >> 16) & 0xFF, og = (old >> 8) & 0xFF, ob = old & 0xFF;
    int sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    int rr = (int)((sr * a + orr * oa * (1.0f - a)) / na + 0.5f);
    int gg = (int)((sg * a + og * oa * (1.0f - a)) / na + 0.5f);
    int bb = (int)((sb * a + ob * oa * (1.0f - a)) / na + 0.5f);
    /* v3.0：拆成三行 —— 原来挤在一行，gcc 报 -Wmisleading-indentation，
       看起来像后两个 if 被第一个保护，其实不是。 */
    if (rr > 255) rr = 255;
    if (gg > 255) gg = 255;
    if (bb > 255) bb = 255;
    *p = ((unsigned int)(na * 255.0f) << 24) | (rr << 16) | (gg << 8) | bb;
}

void cvPx(int x,int y,unsigned int col,float a){
    for(int j=0;j<canvas.scale;j++)for(int i=0;i<canvas.scale;i++)
        cvPxRaw(x*canvas.scale+i,y*canvas.scale+j,col,a);
}

/* 扫描线填充多边形（画布坐标） */
void cvPoly(const float* pts, int n, unsigned int col, float a) {
    float scaled[32];
    if(n<3 || n>16)return;
    for(int i=0;i<n*2;i++)scaled[i]=pts[i]*canvas.scale;
    pts=scaled;

    if (n < 3) return;
    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < n; i++) {
        float y = pts[i * 2 + 1];
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    int y0 = (int)floorf(minY), y1 = (int)ceilf(maxY);
    if (y0 < 0) y0 = 0;
    if (y1 > RASTER_H - 1) y1 = RASTER_H - 1;
    for (int y = y0; y <= y1; y++) {
        float fy = (float)y + 0.5f;
        float xs[16];
        int nx = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float ay = pts[i * 2 + 1], by = pts[j * 2 + 1];
            if ((ay <= fy && by > fy) || (by <= fy && ay > fy)) {
                float t = (fy - ay) / (by - ay);
                if (nx < 16) xs[nx++] = pts[i * 2] + t * (pts[j * 2] - pts[i * 2]);
            }
        }
        /* 简单插入排序 */
        for (int i = 1; i < nx; i++) {
            float v = xs[i];
            int k = i - 1;
            while (k >= 0 && xs[k] > v) { xs[k + 1] = xs[k]; k--; }
            xs[k + 1] = v;
        }
        for (int i = 0; i + 1 < nx; i += 2) {
            int xa = (int)ceilf(xs[i] - 0.5f), xb = (int)floorf(xs[i + 1] - 0.5f);
            for (int x = xa; x <= xb; x++) cvPxRaw(x, y, col, a);
        }
    }
}

void cvEllipse(float cx, float cy, float rx, float ry, unsigned int col, float a) {
    cx*=canvas.scale;cy*=canvas.scale;rx*=canvas.scale;ry*=canvas.scale;
    if (rx < 0.5f) rx = 0.5f;
    if (ry < 0.5f) ry = 0.5f;
    int x0 = (int)(cx - rx) - 1, x1 = (int)(cx + rx) + 1;
    int y0 = (int)(cy - ry) - 1, y1 = (int)(cy + ry) + 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f - cx) / rx;
            float dy = ((float)y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) cvPxRaw(x, y, col, a);
        }
    }
}

/* 粗线段（画手柄、支架） */
void cvLine(float x0, float y0, float x1, float y1, float wid,
                   unsigned int col, float a) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len, ny = dx / len;
    float hw = wid * 0.5f;
    float pts[8] = {
        x0 + nx * hw, y0 + ny * hw,
        x1 + nx * hw, y1 + ny * hw,
        x1 - nx * hw, y1 - ny * hw,
        x0 - nx * hw, y0 - ny * hw
    };
    cvPoly(pts, 4, col, a);
}

/* 竖向渐变多边形（体积感的主要来源） */
void cvPolyGrad(const float* pts, int n, unsigned int top, unsigned int bot, float a) {
    float scaled[32];
    if(n<3 || n>16)return;
    for(int i=0;i<n*2;i++)scaled[i]=pts[i]*canvas.scale;
    pts=scaled;

    if (n < 3) return;
    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < n; i++) {
        float y = pts[i * 2 + 1];
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    if (maxY - minY < 0.5f) return;
    int y0 = (int)floorf(minY), y1 = (int)ceilf(maxY);
    if (y0 < 0) y0 = 0;
    if (y1 > RASTER_H - 1) y1 = RASTER_H - 1;
    int tr = (top >> 16) & 0xFF, tg = (top >> 8) & 0xFF, tb = top & 0xFF;
    int br = (bot >> 16) & 0xFF, bg = (bot >> 8) & 0xFF, bb = bot & 0xFF;
    for (int y = y0; y <= y1; y++) {
        float t = ((float)y - minY) / (maxY - minY);
        unsigned int col = (unsigned int)(
            ((int)(tr + (br - tr) * t) << 16) |
            ((int)(tg + (bg - tg) * t) << 8) |
            (int)(tb + (bb - tb) * t));
        float fy = (float)y + 0.5f;
        float xs[16];
        int nx = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float ay = pts[i * 2 + 1], by = pts[j * 2 + 1];
            if ((ay <= fy && by > fy) || (by <= fy && ay > fy)) {
                float tt = (fy - ay) / (by - ay);
                if (nx < 16) xs[nx++] = pts[i * 2] + tt * (pts[j * 2] - pts[i * 2]);
            }
        }
        for (int i = 1; i < nx; i++) {
            float v = xs[i];
            int k = i - 1;
            while (k >= 0 && xs[k] > v) { xs[k + 1] = xs[k]; k--; }
            xs[k + 1] = v;
        }
        for (int i = 0; i + 1 < nx; i += 2) {
            int xa = (int)ceilf(xs[i] - 0.5f), xb = (int)floorf(xs[i + 1] - 0.5f);
            for (int x = xa; x <= xb; x++) cvPxRaw(x, y, col, a);
        }
    }
}

/* 锈斑 / 噪点：让表面不干净 */
static unsigned int g_rng = 0x12345u;
unsigned int SpriteBakeRandom(void) {
    g_rng = g_rng * 1103515245u + 12345u;
    return (g_rng >> 16) & 0x7FFF;
}

void cvSpeckle(int cx, int cy, int radius, int count, unsigned int col, float a) {
    for (int i = 0; i < count; i++) {
        int dx = (int)(SpriteBakeRandom() % (unsigned)(radius * 2 + 1)) - radius;
        int dy = (int)(SpriteBakeRandom() % (unsigned)(radius * 2 + 1)) - radius;
        if (dx * dx + dy * dy > radius * radius) continue;
        cvPx(cx + dx, cy + dy, col, a);
    }
}

/* 加法辉光（符印、枪口余温） */
void cvGlow(float cx, float cy, float r, unsigned int col, float a) {
    cx*=canvas.scale;cy*=canvas.scale;r*=canvas.scale;
    int x0 = (int)(cx - r) - 1, x1 = (int)(cx + r) + 1;
    int y0 = (int)(cy - r) - 1, y1 = (int)(cy + r) + 1;
    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= RASTER_H) continue;
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || x >= RASTER_W) continue;
            float dx = ((float)x + 0.5f - cx) / r;
            float dy = ((float)y + 0.5f - cy) / r;
            float d2 = dx * dx + dy * dy;
            if (d2 > 1.0f) continue;
            float k = (1.0f - d2);
            unsigned int* p = &canvas.pixels[(size_t)y * RASTER_W + x];
            unsigned int old = *p;
            int oa = (old >> 24);
            int orr = (old >> 16) & 0xFF, og = (old >> 8) & 0xFF, ob = old & 0xFF;
            int sr = (col >> 16) & 0xFF, sg = (col >> 8) & 0xFF, sb = col & 0xFF;
            int rr = orr + (int)(sr * k * a);
            int gg = og + (int)(sg * k * a);
            int bb = ob + (int)(sb * k * a);
            if (rr > 255) rr = 255;
            if (gg > 255) gg = 255;
            if (bb > 255) bb = 255;
            int na = oa + (int)(k * a * 255.0f);
            if (na > 255) na = 255;
            *p = ((unsigned int)na << 24) | (rr << 16) | (gg << 8) | bb;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 降采样 + 亮度 LUT                                                    */
/* ------------------------------------------------------------------ */
void cvFinish(Sprite* s) {
    int outputScale=canvas.scale;
    int ow=SPR_W*outputScale,oh=SPR_H*outputScale;
    int dollStyle=ArtStyleDoll();      /* 循环外取一次 */
    unsigned int* resized=(unsigned int*)realloc(s->px,(size_t)ow*oh*sizeof(unsigned int));
    if(resized)s->px=resized;
    else {outputScale=1;ow=SPR_W;oh=SPR_H;}
    int sample=SUP*canvas.scale/outputScale;
    s->w=ow;s->h=oh;
    s->bx0 = ow; s->by0 = oh; s->bx1 = -1; s->by1 = -1;
    for (int y = 0; y < oh; y++) {
        for (int x = 0; x < ow; x++) {
            int r = 0, g = 0, b = 0, a = 0;
            for (int dy = 0; dy < sample; dy++) {
                for (int dx = 0; dx < sample; dx++) {
                    unsigned int p = canvas.pixels[(size_t)(y * sample + dy) * RASTER_W + (x * sample + dx)];
                    int pa = p >> 24;
                    r += ((p >> 16) & 0xFF) * pa / 255;
                    g += ((p >> 8) & 0xFF) * pa / 255;
                    b += (p & 0xFF) * pa / 255;
                    a += pa;
                }
            }
            int n = sample * sample;
            /* v3.0：**alpha 归一化**（建模审查 #6）。
               原来颜色是先乘 alpha 再除以 n，没有除回 alpha 总和 ——
               半透明边缘的颜色被 alpha 拉黑，每张精灵因此有一圈黑晕，
               细轮廓光最脏。正确做法：颜色 = Σ(c·a) / Σa。 */
            int outA = a / n;
            if (outA > 0) {
                if (x < s->bx0) s->bx0 = x;
                if (y < s->by0) s->by0 = y;
                if (x > s->bx1) s->bx1 = x;
                if (y > s->by1) s->by1 = y;
            }
            int rr, gg, bb;
            if (a > 0) {
                rr = r * 255 / a;
                gg = g * 255 / a;
                bb = b * 255 / a;
            } else {
                rr = gg = bb = 0;
            }
            if (rr > 255) rr = 255;
            if (gg > 255) gg = 255;
            if (bb > 255) bb = 255;
            if (dollStyle && outA > 0) {
                unsigned int q = ArtDollGrade(((unsigned int)rr << 16) |
                                              ((unsigned int)gg << 8) |
                                              (unsigned int)bb);
                rr = (int)((q >> 16) & 0xFFu);
                gg = (int)((q >> 8) & 0xFFu);
                bb = (int)(q & 0xFFu);
            }
            s->px[(size_t)y * ow + x] = ((unsigned int)outA << 24) |
                                           (unsigned int)rr << 16 |
                                           (unsigned int)gg << 8 |
                                           (unsigned int)bb;
        }
    }
    for (int i = 0; i < SPR_SHADES; i++) {
        float k = (float)i / (float)(SPR_SHADES - 1);
        if (k < 0.08f) k = 0.08f;
        int v = (int)(k * 255.0f);
        if (v > 255) v = 255;
        s->shade[i] = (unsigned int)((v << 16) | (v << 8) | v);
    }

    /* ==================================================================
     * 2026-11-11：**布偶画风的剪边与织纹**（烘焙期一次算完，运行时零成本）
     * ------------------------------------------------------------------
     * 让每个物件都读成"从布上剪下来、贴进布景"的一块：
     *   ① 剪边：轮廓向内的一个窄带提亮成亚麻纤维色
     *   ② 接触阴影：剪边外侧再压暗一圈（布片贴住布景的 AO）
     *   ③ 织物：规则 2×2 棋盘（按分辨率放大），静止、不闪（AD-08）
     * 只改颜色、不改 alpha：剪影、包围盒、命中框一律不动（AD-01/AD-11）。
     *
     * RC4 的精灵是 512（canvas.scale），这里按 ow/128 缩放带宽 ——
     * 否则同一条 1px 边在 512 上只有原来的 1/4 宽，等于没做。
     * 性能：只处理包围盒；AO 用「从轮廓膨胀」而不是逐像素查 5×5，
     * 否则 69 张 512×512 会让启动多等一秒以上。
     * ================================================================== */
    if (dollStyle) {
        int sid = SpriteBakeIndex(s);
        if (!(sid >= SPR_UI_PANEL && sid < SPR_HATCH_0)) {
            int S = ow / 128;                 /* 512 → 4 */
            if (S < 1) S = 1;
            int x0 = s->bx0, y0 = s->by0, x1 = s->bx1, y1 = s->by1;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 >= ow) x1 = ow - 1;
            if (y1 >= oh) y1 = oh - 1;
            if (x1 > x0 && y1 > y0) {
                size_t msz = (size_t)ow * oh;
                unsigned char* emask = (unsigned char*)calloc(msz, 1);
                unsigned char* rim = (unsigned char*)calloc(msz, 1);
                unsigned char* ao = (unsigned char*)calloc(msz, 1);
                if (emask && rim && ao) {
                    /* ① 找轮廓：有像素、且四邻里有透明 */
                    for (int y = y0; y <= y1; y++) {
                        for (int x = x0; x <= x1; x++) {
                            size_t i = (size_t)y * ow + x;
                            if ((s->px[i] >> 24) < 40) continue;
                            if (x <= 0 || y <= 0 || x >= ow - 1 || y >= oh - 1 ||
                                (s->px[i - 1] >> 24) < 40 || (s->px[i + 1] >> 24) < 40 ||
                                (s->px[i - ow] >> 24) < 40 || (s->px[i + ow] >> 24) < 40)
                                emask[i] = 1;
                        }
                    }
                    /* ② 膨胀两次：rim 带宽 S，AO 带宽 2S */
                    for (int pass = 0; pass < 2; pass++) {
                        int rad = (pass == 0) ? S : 2 * S;
                        unsigned char* dst = (pass == 0) ? rim : ao;
                        for (int y = y0; y <= y1; y++) {
                            for (int x = x0; x <= x1; x++) {
                                if (!emask[(size_t)y * ow + x]) continue;
                                int ya = y - rad, yb = y + rad;
                                int xa = x - rad, xb = x + rad;
                                if (ya < 0) ya = 0;
                                if (xb >= ow) xb = ow - 1;
                                if (yb >= oh) yb = oh - 1;
                                if (xa < 0) xa = 0;
                                for (int yy = ya; yy <= yb; yy++)
                                    memset(dst + (size_t)yy * ow + xa, 1, (size_t)(xb - xa + 1));
                            }
                        }
                    }
                    /* ③ 上色 */
                    for (int y = y0; y <= y1; y++) {
                        for (int x = x0; x <= x1; x++) {
                            size_t i = (size_t)y * ow + x;
                            if ((s->px[i] >> 24) < 8) continue;
                            unsigned int c = s->px[i];
                            int r = (int)((c >> 16) & 0xFF);
                            int g = (int)((c >> 8) & 0xFF);
                            int b = (int)(c & 0xFF);
                            if (rim[i]) {
                                r += (198 - r) * 30 / 100;
                                g += (184 - g) * 30 / 100;
                                b += (158 - b) * 30 / 100;
                            } else if (ao[i]) {
                                r = (r * 224) >> 8;
                                g = (g * 224) >> 8;
                                b = (b * 224) >> 8;
                            } else if (((x / S) + (y / S)) & 1) {
                                r = (r * 247) >> 8;
                                g = (g * 247) >> 8;
                                b = (b * 247) >> 8;
                            }
                            s->px[i] = (c & 0xFF000000u) |
                                       ((unsigned int)r << 16) |
                                       ((unsigned int)g << 8) |
                                       (unsigned int)b;
                        }
                    }
                }
                free(emask);
                free(rim);
                free(ao);
            }
        }
    }
}

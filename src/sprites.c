/*
 * Canity — v2.1 烘焙精灵管线
 *
 * 调研依据（见 docs/建模重造_调研与设计.md）：
 *   Doom / Build / Wolfenstein 的 sprite 都是**预烘焙位图 + 索引色查表**，
 *   运行时只做「查表 + 拷像素」。这个项目原来每帧用矩形现拼形状，是反的。
 *
 * 这里实现：
 *   1) 2× 超采样画布 + 基础图元（多边形/椭圆/线段/渐变/噪点/辉光）
 *   2) 降采样 → 抗锯齿边缘
 *   3) 24 档亮度 LUT → 运行时零浮点
 *   4) 双线性 blit + alpha + 可选 z 测试
 */

#include <math.h>
#include <string.h>
#include "sprites.h"
#include "common.h"
#include "render.h"

/* ------------------------------------------------------------------ */
/* 画布（2× 超采样）                                                    */
/* ------------------------------------------------------------------ */
#define SUP 2
#define CANVAS_W (SPR_W * SUP)
#define CANVAS_H (SPR_H * SUP)

static unsigned int g_canvas[CANVAS_W * CANVAS_H];

static Sprite g_sprites[SPR_COUNT];
static int g_ready = 0;

static void cvClear(void) {
    memset(g_canvas, 0, sizeof(g_canvas));
}

static inline void cvPx(int x, int y, unsigned int col, float a) {
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) return;
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    unsigned int* p = &g_canvas[(size_t)y * CANVAS_W + x];
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
    if (rr > 255) rr = 255; if (gg > 255) gg = 255; if (bb > 255) bb = 255;
    *p = ((unsigned int)(na * 255.0f) << 24) | (rr << 16) | (gg << 8) | bb;
}

/* 扫描线填充多边形（画布坐标） */
static void cvPoly(const float* pts, int n, unsigned int col, float a) {
    if (n < 3) return;
    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < n; i++) {
        float y = pts[i * 2 + 1];
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    int y0 = (int)floorf(minY), y1 = (int)ceilf(maxY);
    if (y0 < 0) y0 = 0;
    if (y1 > CANVAS_H - 1) y1 = CANVAS_H - 1;
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
            for (int x = xa; x <= xb; x++) cvPx(x, y, col, a);
        }
    }
}

static void cvEllipse(float cx, float cy, float rx, float ry, unsigned int col, float a) {
    if (rx < 0.5f) rx = 0.5f;
    if (ry < 0.5f) ry = 0.5f;
    int x0 = (int)(cx - rx) - 1, x1 = (int)(cx + rx) + 1;
    int y0 = (int)(cy - ry) - 1, y1 = (int)(cy + ry) + 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f - cx) / rx;
            float dy = ((float)y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) cvPx(x, y, col, a);
        }
    }
}

/* 粗线段（画手柄、支架） */
static void cvLine(float x0, float y0, float x1, float y1, float wid,
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
static void cvPolyGrad(const float* pts, int n, unsigned int top, unsigned int bot, float a) {
    if (n < 3) return;
    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < n; i++) {
        float y = pts[i * 2 + 1];
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    if (maxY - minY < 0.5f) { cvPoly(pts, n, top, a); return; }
    int y0 = (int)floorf(minY), y1 = (int)ceilf(maxY);
    if (y0 < 0) y0 = 0;
    if (y1 > CANVAS_H - 1) y1 = CANVAS_H - 1;
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
            for (int x = xa; x <= xb; x++) cvPx(x, y, col, a);
        }
    }
}

/* 锈斑 / 噪点：让表面不干净 */
static unsigned int g_rng = 0x12345u;
static unsigned int rnd(void) {
    g_rng = g_rng * 1103515245u + 12345u;
    return (g_rng >> 16) & 0x7FFF;
}

static void cvSpeckle(int cx, int cy, int radius, int count, unsigned int col, float a) {
    for (int i = 0; i < count; i++) {
        int dx = (int)(rnd() % (unsigned)(radius * 2 + 1)) - radius;
        int dy = (int)(rnd() % (unsigned)(radius * 2 + 1)) - radius;
        if (dx * dx + dy * dy > radius * radius) continue;
        cvPx(cx + dx, cy + dy, col, a);
    }
}

/* 加法辉光（符印、枪口余温） */
static void cvGlow(float cx, float cy, float r, unsigned int col, float a) {
    int x0 = (int)(cx - r) - 1, x1 = (int)(cx + r) + 1;
    int y0 = (int)(cy - r) - 1, y1 = (int)(cy + r) + 1;
    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= CANVAS_H) continue;
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || x >= CANVAS_W) continue;
            float dx = ((float)x + 0.5f - cx) / r;
            float dy = ((float)y + 0.5f - cy) / r;
            float d2 = dx * dx + dy * dy;
            if (d2 > 1.0f) continue;
            float k = (1.0f - d2);
            unsigned int* p = &g_canvas[(size_t)y * CANVAS_W + x];
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
static void cvFinish(Sprite* s) {
    s->w = SPR_W;
    s->h = SPR_H;
    for (int y = 0; y < SPR_H; y++) {
        for (int x = 0; x < SPR_W; x++) {
            int r = 0, g = 0, b = 0, a = 0;
            for (int dy = 0; dy < SUP; dy++) {
                for (int dx = 0; dx < SUP; dx++) {
                    unsigned int p = g_canvas[(size_t)(y * SUP + dy) * CANVAS_W + (x * SUP + dx)];
                    int pa = p >> 24;
                    r += ((p >> 16) & 0xFF) * pa / 255;
                    g += ((p >> 8) & 0xFF) * pa / 255;
                    b += (p & 0xFF) * pa / 255;
                    a += pa;
                }
            }
            int n = SUP * SUP;
            /* v3.0：**alpha 归一化**（建模审查 #6）。
               原来颜色是先乘 alpha 再除以 n，没有除回 alpha 总和 ——
               半透明边缘的颜色被 alpha 拉黑，每张精灵因此有一圈黑晕，
               细轮廓光最脏。正确做法：颜色 = Σ(c·a) / Σa。 */
            int outA = a / n;
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
            s->px[(size_t)y * SPR_W + x] = ((unsigned int)outA << 24) |
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
}

/* ------------------------------------------------------------------ */
/* 三把武器（用户点名"很粗糙"的那部分）                                  */
/* ------------------------------------------------------------------ */
static void BakeWrench(void) {
    cvClear();
    float c = CANVAS_W * 0.5f;

    /* 整体轴向：从右下 45° 伸向左上（画面里读作"被握着"） */
    float ax0 = c + 44, ay0 = 246;      /* 握把末端（画面外） */
    float ax1 = c - 26, ay1 = 96;       /* 头颈 */
    float dx = ax1 - ax0, dy = ay1 - ay0;
    float L = sqrtf(dx * dx + dy * dy);
    float ux = dx / L, uy = dy / L;      /* 轴向单位向量 */
    float vx = -uy, vy = ux;             /* 法向 */

    /* ---- 手柄：外描边 + 主体 + 受光面 ---- */
    cvLine(ax0 + 5, ay0 + 5, ax1 + 5, ay1 + 5, 34, 0x00080A0Eu, 1.0f);
    cvLine(ax0, ay0, ax1, ay1, 28, 0x00484036u, 1.0f);
    /* 受光面：沿法向偏移一条细带 */
    cvLine(ax0 + vx * 6, ay0 + vy * 6, ax1 + vx * 6, ay1 + vy * 6, 8, 0x006E6456u, 1.0f);
    /* 阴影面 */
    cvLine(ax0 - vx * 7, ay0 - vy * 7, ax1 - vx * 7, ay1 - vy * 7, 6, 0x00241E18u, 1.0f);

    /* 缠带：四道，垂直轴向 */
    for (int i = 0; i < 4; i++) {
        float t = 0.42f + (float)i * 0.13f;
        float bx = ax0 + dx * t, by = ay0 + dy * t;
        cvLine(bx + vx * 16, by + vy * 16, bx - vx * 16, by - vy * 16, 7, 0x00201814u, 1.0f);
        cvLine(bx + vx * 14 + ux * 2, by + vy * 14 + uy * 2,
               bx - vx * 14 + ux * 2, by - vy * 14 + uy * 2, 4, 0x00463428u, 1.0f);
    }

    /* ---- 头部：管钳的开口钳口，开口朝左上 ---- */
    float hx = ax1, hy = ay1;
    /* 外轮廓（闭合多边形，先在 2× 画布上） */
    float head[20] = {
        hx - 30, hy - 46,      /* 上颚外角 */
        hx +  34, hy - 40,     /* 上颚根部 */
        hx +  44, hy - 12,     /* 右侧 */
        hx +  30, hy + 22,     /* 下颚根部 */
        hx -  26, hy + 26,     /* 下颚外角 */
        hx -  40, hy + 2,      /* 左侧 */
        hx -  34, hy - 22,     /* 左上 */
        hx - 30, hy - 34,      /* v3.0：上颚内凹（原来这点和末点重复） */
        hx - 34, hy - 6,
        hx - 38, hy - 26
    };
    /* 先把外框放大 8% 当描边 */
    float outline[20];
    for (int i = 0; i < 10; i++) {
        outline[i * 2] = hx + (head[i * 2] - hx) * 1.10f;
        outline[i * 2 + 1] = hy + (head[i * 2 + 1] - hy) * 1.10f;
    }
    cvPoly(outline, 10, 0x00080A0Eu, 1.0f);
    cvPolyGrad(head, 10, 0x00B4AC9Cu, 0x005A5348u, 1.0f);

    /* 开口：把嘴巴挖成背景色（用画布擦除） */
    {
        float mouth[8] = {
            hx - 40, hy - 30,
            hx - 4,  hy - 26,
            hx - 6,  hy + 10,
            hx - 38, hy + 8
        };
        /* 用 alpha 擦出开口：直接写 0 */
        int y0 = (int)hy - 34, y1 = (int)hy + 14;
        for (int y = y0; y <= y1; y++) {
            if (y < 0 || y >= CANVAS_H) continue;
            for (int x = (int)hx - 44; x <= (int)hx; x++) {
                if (x < 0 || x >= CANVAS_W) continue;
                float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
                /* 点在张开的多边形内？ */
                int inside = 0;
                for (int i = 0; i < 4; i++) {
                    int j = (i + 1) % 4;
                    float ay = mouth[i * 2 + 1], by = mouth[j * 2 + 1];
                    if ((ay <= fy && by > fy) || (by <= fy && ay > fy)) {
                        float t = (fy - ay) / (by - ay);
                        if (fx < mouth[i * 2] + t * (mouth[j * 2] - mouth[i * 2])) inside ^= 1;
                    }
                }
                if (inside) g_canvas[(size_t)y * CANVAS_W + x] = 0;
            }
        }
    }

    /* 上下颚的齿：只画在开口边缘，短而粗 */
    for (int i = 0; i < 3; i++) {
        float tx = hx - 36 + (float)i * 10.0f;
        cvLine(tx, hy - 30, tx + 2, hy - 20, 4, 0x00D8D0C0u, 1.0f);   /* 上齿 */
        cvLine(tx, hy + 8, tx + 2, hy - 2, 4, 0x00C0B8A8u, 1.0f);     /* 下齿 */
    }

    /* 调节螺母：头部右侧一个六角 */
    {
        float nx = hx + 26, ny = hy + 2;
        cvEllipse(nx, ny, 12.0f, 12.0f, 0x00080A0Eu, 1.0f);
        cvEllipse(nx, ny, 9.0f, 9.0f, 0x00908878u, 1.0f);
        cvEllipse(nx - 2, ny - 2, 3.4f, 3.4f, 0x00302A22u, 1.0f);
    }

    /* 锈斑 */
    cvSpeckle((int)hx, (int)hy + 4, 30, 90, 0x00703018u, 0.5f);
    cvSpeckle((int)(ax0 + dx * 0.55f), (int)(ay0 + dy * 0.55f), 22, 60, 0x00602810u, 0.4f);

    /* 摇柄：头部下侧伸出的曲柄（"crank" 的由来） */
    {
        float kx = hx + 10, ky = hy + 30;
        cvEllipse(kx, ky, 8.0f, 8.0f, 0x00080A0Eu, 1.0f);
        cvEllipse(kx, ky, 5.6f, 5.6f, 0x007A7262u, 1.0f);
        cvLine(kx, ky + 6, kx + 4, ky + 26, 6, 0x00080A0Eu, 1.0f);
        cvLine(kx, ky + 6, kx + 4, ky + 26, 4, 0x00665E50u, 1.0f);
    }

    cvFinish(&g_sprites[SPR_WRENCH]);
}

static void BakePistol(void) {
    cvClear();
    float c = CANVAS_W * 0.5f;

    /* 折开式信号枪。三版草图对比后定这一版：
       ① 直管手枪  —— 和扳手剪影太像，认不出
       ② 转轮手枪  —— 细节太多，128px 下糊成一块
       ③ 折开式（本版）—— 喇叭口 + 铰链 + 独立握把，三个特征块在剪影上分得开 */

    float bx = c - 4.0f;        /* 枪管中线 */
    float muzzleY = 58.0f;      /* 喇叭口位置 */
    float hingeY  = 150.0f;     /* 铰链 */
    float gripY   = 236.0f;     /* 握把底 */

    /* ---- 喇叭口：外扩的锥形，这是信号枪唯一的身份特征 ---- */
    {
        float m[8] = { bx - 30, muzzleY - 26, bx + 30, muzzleY - 26,
                       bx + 16, muzzleY + 8,  bx - 16, muzzleY + 8 };
        float o[8];
        for (int i = 0; i < 4; i++) {
            o[i * 2] = bx + (m[i * 2] - bx) * 1.10f;
            o[i * 2 + 1] = muzzleY + (m[i * 2 + 1] - muzzleY) * 1.12f;
        }
        cvPoly(o, 4, 0x00080A0Eu, 1.0f);
        cvPolyGrad(m, 4, 0x00A0A0AEu, 0x00484C58u, 1.0f);
        /* 口径：黑洞 + 余温 */
        cvEllipse(bx, muzzleY - 20, 14.0f, 5.5f, 0x000C0C10u, 1.0f);
        cvEllipse(bx, muzzleY - 20, 9.0f, 3.2f, 0x0020100Cu, 1.0f);
        cvGlow(bx, muzzleY - 19, 18.0f, 0x00FF6020u, 0.40f);
        /* 口沿高光 */
        cvLine(bx - 24, muzzleY - 24, bx + 24, muzzleY - 24, 2.4f, 0x00D8D8E0u, 0.75f);
    }

    /* ---- 枪管：短而粗，两侧各一条加强筋 ---- */
    {
        float b[8] = { bx - 15, muzzleY + 6, bx + 15, muzzleY + 6,
                       bx + 13, hingeY,        bx - 13, hingeY };
        cvPoly(b, 4, 0x00080A0Eu, 1.0f);
        float b2[8] = { bx - 11, muzzleY + 10, bx + 11, muzzleY + 10,
                        bx + 9, hingeY - 3,     bx - 9, hingeY - 3 };
        cvPolyGrad(b2, 4, 0x00707480u, 0x003A3E48u, 1.0f);
        cvLine(bx - 8, muzzleY + 14, bx - 8, hingeY - 8, 2.0f, 0x009098A4u, 0.6f);
    }

    /* ---- 铰链：一颗横向的圆轴，折开结构的证据 ---- */
    {
        cvEllipse(bx, hingeY - 6, 13.0f, 11.0f, 0x00080A0Eu, 1.0f);
        cvEllipse(bx, hingeY - 6, 10.0f, 8.0f, 0x00A89878u, 1.0f);
        cvEllipse(bx, hingeY - 6, 4.2f, 3.6f, 0x00302A22u, 1.0f);
    }

    /* ---- 药室：打开时能看到的红膛 ---- */
    {
        cvEllipse(bx, hingeY + 12, 16.0f, 7.0f, 0x00080A0Eu, 1.0f);
        cvEllipse(bx, hingeY + 12, 12.0f, 4.6f, 0x00C03010u, 1.0f);
        cvEllipse(bx - 3, hingeY + 11, 5.0f, 2.0f, 0x00FFA060u, 0.9f);
        cvGlow(bx, hingeY + 12, 20.0f, 0x00FF5020u, 0.32f);
    }

    /* ---- 握把：向后下方掏空，带防滑纹和一颗底板螺丝 ---- */
    {
        float g[8] = { bx - 13, hingeY + 16,
                       bx + 13, hingeY + 16,
                       bx + 30, gripY,
                       bx + 2,  gripY };
        float go[8];
        for (int i = 0; i < 4; i++) {
            go[i * 2] = bx + (g[i * 2] - bx) * 1.12f;
            go[i * 2 + 1] = g[i * 2 + 1];
        }
        cvPoly(go, 4, 0x00080A0Eu, 1.0f);
        cvPolyGrad(g, 4, 0x00644A32u, 0x002A1E16u, 1.0f);
        /* 防滑纹 */
        for (int i = 0; i < 5; i++) {
            float gy = hingeY + 30.0f + (float)i * 11.0f;
            float gw = 11.0f + (float)i * 0.6f;
            cvLine(bx + 1 - gw * 0.2f, gy, bx + 1 + gw, gy + 2.0f, 2.6f, 0x00180F0Au, 0.85f);
        }
        cvEllipse(bx + 12, gripY - 8, 3.4f, 3.4f, 0x00A89878u, 0.95f);
    }

    /* ---- 扳机护圈 + 扳机 ---- */
    {
    /* v3.0：cvEllipse(..., 0.0f) 是空调用（alpha=0 直接 return），删掉 */
        for (int a = 20; a <= 160; a += 8) {
            float rad = (float)a * 3.14159f / 180.0f;
            float ax = bx - 6 + cosf(rad) * 13.0f;
            float ay = hingeY + 34 + sinf(rad) * 11.0f;
            cvEllipse(ax, ay, 2.0f, 2.0f, 0x00342C24u, 1.0f);
        }
        cvLine(bx - 6, hingeY + 22, bx - 6, hingeY + 34, 3.4f, 0x00807868u, 1.0f);
    }

    /* ---- 锈迹 ---- */
    cvSpeckle((int)bx, (int)hingeY - 10, 26, 70, 0x00703818u, 0.45f);
    cvSpeckle((int)bx + 14, (int)gripY - 40, 20, 40, 0x00602810u, 0.40f);

    cvFinish(&g_sprites[SPR_PISTOL]);
}

static void BakeSigil(void) {
    cvClear();
    float c = CANVAS_W * 0.5f;
    float cy = 118;
    /* 手：托着符印 */
    cvEllipse(c, cy + 62, 34.0f, 22.0f, 0x000A0C10u, 1.0f);
    cvEllipse(c, cy + 60, 30.0f, 18.0f, 0x00382C26u, 1.0f);
    cvEllipse(c - 6, cy + 54, 16.0f, 9.0f, 0x005A4638u, 0.9f);

    /* 外层符文环：用 24 段小方块拼成 */
    int R = 46;
    for (int i = 0; i < 24; i++) {
        float ang = (float)i * 6.2831853f / 24.0f;
        float ax = c + cosf(ang) * (float)R;
        float ay = cy + sinf(ang) * (float)R * 0.62f;
        unsigned int col = (i % 3 == 0) ? 0x00E8F8FFu : 0x0060C8FFu;
        float sz = (i % 3 == 0) ? 3.4f : 2.2f;
        cvEllipse(ax, ay, sz, sz, col, 0.95f);
    }
    /* 内层环 */
    for (int i = 0; i < 12; i++) {
        float ang = (float)i * 6.2831853f / 12.0f + 0.26f;
        float ax = c + cosf(ang) * 26.0f;
        float ay = cy + sinf(ang) * 16.0f;
        cvEllipse(ax, ay, 1.8f, 1.8f, 0x00A8E0FFu, 0.85f);
    }

    /* 中央菱形核心 */
    float core[8] = {
        c, cy - 26,
        c + 16, cy,
        c, cy + 26,
        c - 16, cy
    };
    cvPoly(core, 4, 0x00081420u, 1.0f);
    float core2[8] = {
        c, cy - 19,
        c + 11, cy,
        c, cy + 19,
        c - 11, cy
    };
    cvPolyGrad(core2, 4, 0x00E8FBFFu, 0x003090C0u, 1.0f);

    /* 辉光 */
    cvGlow(c, cy, 54.0f, 0x0040C8FFu, 0.30f);
    cvGlow(c, cy, 26.0f, 0x00C8F0FFu, 0.30f);

    /* 三条能量触须：往手的方向收拢 */
    for (int i = -1; i <= 1; i++) {
        float ox = c + (float)i * 22.0f;
        cvLine(c + (float)i * 10.0f, cy + 22.0f, ox, cy + 52.0f, 2.4f, 0x0060C8FFu, 0.55f);
    }

    cvFinish(&g_sprites[SPR_SIGIL]);
}

/* ------------------------------------------------------------------ */
/* 投掷物（左手持）                                                     */
/* ------------------------------------------------------------------ */
static void BakeItem(int id, int kind) {
    cvClear();
    float c = CANVAS_W * 0.5f, cy = 128.0f;
    unsigned int main = 0x0040A060u, dark = 0x00102818u, hi = 0x0080E0A0u;
    int tall = 1, wide = 0;
    switch (kind) {
    case 0: main = 0x00E04020u; dark = 0x00300808u; hi = 0x00FFC080u; tall = 1; break; /* 荧光棒 */
    case 1: main = 0x0030A850u; dark = 0x000A2010u; hi = 0x00A0F0C0u; tall = 1; break; /* 玻璃瓶 */
    case 2: main = 0x00805038u; dark = 0x00181010u; hi = 0x00C08060u; tall = 0; wide = 1; break; /* 砖头 */
    case 3: main = 0x00A0C020u; dark = 0x00182008u; hi = 0x00E0F060u; tall = 1; break; /* 酸瓶 */
    case 4: main = 0x00808088u; dark = 0x00181820u; hi = 0x00C8C8D0u; tall = 1; break; /* 烟雾弹 */
    case 5: main = 0x00C04818u; dark = 0x00301008u; hi = 0x00FFA040u; tall = 1; break; /* 燃烧瓶 */
    case 6: main = 0x00C8C040u; dark = 0x00302810u; hi = 0x00F0F080u; tall = 1; break; /* 胶水瓶 */
    default: main = 0x00B0C8D8u; dark = 0x00203040u; hi = 0x00F0FFFFu; tall = 0; wide = 0; break; /* 镜片 */
    }
    /* 手 */
    cvEllipse(c, cy + 40, 40.0f, 26.0f, 0x000A0C10u, 1.0f);
    cvEllipse(c, cy + 38, 36.0f, 22.0f, 0x00382C26u, 1.0f);

    if (wide) {
        /* 砖头：一块带缺口的方砖 */
        float b[8] = { c - 42, cy - 26, c + 40, cy - 32, c + 44, cy + 20, c - 38, cy + 26 };
        cvPoly(b, 4, 0x000A0C10u, 1.0f);
        float b2[8] = { c - 36, cy - 20, c + 34, cy - 25, c + 37, cy + 14, c - 32, cy + 19 };
        cvPolyGrad(b2, 4, hi, main, 1.0f);
        cvSpeckle((int)c, (int)cy, 30, 40, dark, 0.5f);
    } else if (kind == 7) {
        /* 镜片：一片斜置的薄圆盘 */
        cvEllipse(c, cy, 44.0f, 18.0f, 0x000A0C10u, 1.0f);
        cvEllipse(c, cy, 40.0f, 14.0f, main, 0.9f);
        cvEllipse(c - 10, cy - 5, 18.0f, 6.0f, hi, 0.75f);
        cvLine(c - 34, cy + 6, c + 30, cy - 8, 3, 0x00F0FFFFu, 0.5f);
    } else {
        /* 瓶子/棒：柱体 + 瓶颈 + 标签 */
        float h = tall ? 74.0f : 46.0f;
        cvLine(c, cy - h, c, cy + 26, 46, 0x000A0C10u, 1.0f);
        cvLine(c, cy - h, c, cy + 24, 40, main, 1.0f);
        cvLine(c - 10, cy - h + 6, c - 10, cy + 20, 10, hi, 0.55f);
        /* 瓶颈 */
        cvLine(c, cy - h - 18, c, cy - h + 4, 22, 0x000A0C10u, 1.0f);
        cvLine(c, cy - h - 16, c, cy - h + 2, 16, main, 1.0f);
        /* 标签 */
        cvLine(c, cy - 20, c, cy + 6, 42, dark, 0.85f);
        cvLine(c, cy - 16, c, cy + 2, 34, hi, 0.45f);
        /* 高光 */
        cvEllipse(c - 12, cy - h + 22, 4.0f, 10.0f, 0x00FFFFFFu, 0.22f);
    }
    cvFinish(&g_sprites[id]);
}

/* ------------------------------------------------------------------ */
/* 怪物（七种，各自独立剪影）                                            */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* 怪物（七种，重做版）                                                 */
/*                                                                     */
/* 上一版是"几何拼贴"：圆头 + 方胸 + 直线手脚，读起来像象形图。           */
/* 这一版按解剖结构画：                                              */
/*   脊柱 → 胸腔（含肋骨）→ 骨盆 → 分节四肢（上臂/前臂/爪）              */
/*   并且给每个物种一个「贴合机制的剪影」：                             */
/*     寻声鬼  —— 看不见的眼睛（它靠听），下颌张得极大                    */
/*     追光鬼  —— 一对又亮又大的眼睛，身体被拉长成"光柱"                 */
/*     偷电怪  —— 矮胖、多眼、背上插着导管                               */
/*   所有形状都叠三层色（暗/中/受光）+ 左缘一条轮廓光，做出体积。          */
/* ------------------------------------------------------------------ */

/* 一条带锥度的肢体：从 (x0,y0) 到 (x1,y1)，起点宽 w0、终点宽 w1 */
static void Limb(float x0, float y0, float x1, float y1, float w0, float w1,
                 unsigned int edge, unsigned int body, unsigned int hi) {
    float dx = x1 - x0, dy = y1 - y0;
    float L = sqrtf(dx * dx + dy * dy);
    if (L < 0.001f) return;
    float nx = -dy / L, ny = dx / L;

    float e[8] = {
        x0 + nx * (w0 * 0.5f + 2), y0 + ny * (w0 * 0.5f + 2),
        x1 + nx * (w1 * 0.5f + 2), y1 + ny * (w1 * 0.5f + 2),
        x1 - nx * (w1 * 0.5f + 2), y1 - ny * (w1 * 0.5f + 2),
        x0 - nx * (w0 * 0.5f + 2), y0 - ny * (w0 * 0.5f + 2)
    };
    cvPoly(e, 4, edge, 1.0f);

    float b[8] = {
        x0 + nx * (w0 * 0.5f), y0 + ny * (w0 * 0.5f),
        x1 + nx * (w1 * 0.5f), y1 + ny * (w1 * 0.5f),
        x1 - nx * (w1 * 0.5f), y1 - ny * (w1 * 0.5f),
        x0 - nx * (w0 * 0.5f), y0 - ny * (w0 * 0.5f)
    };
    cvPolyGrad(b, 4, hi, body, 1.0f);
}

static void BakeMonster(int id, int kind, int pose) {
    cvClear();
    float c = CANVAS_W * 0.5f;

    int side = (pose >= 2) ? 1 : 0;
    int walk = (pose == 1 || pose == 3) ? 1 : 0;

    /* v3.0：走路时**不再整体下沉** —— 原来 baseY+4 让脚每次迈步都往地里踩 */
    float baseY = 244.0f;
    float lean  = walk ? 3.0f : 0.0f;

    /* 每个物种的调色板：暗边 / 中间调 / 受光 */
    unsigned int edge = 0x00080A0Eu, body = 0x00262C38u, hi = 0x003E4658u;
    float heightK = 1.0f;

    switch (kind) {
    case 0:  /* 寻声鬼：瘦高，冷灰蓝 */
        edge = 0x00080A0Eu; body = 0x00262C38u; hi = 0x003E4658u; heightK = 1.00f; break;
    case 1:  /* 追光鬼：更细长，偏白 */
        edge = 0x000A0C10u; body = 0x00303A48u; hi = 0x00526074u; heightK = 1.12f; break;
    case 2:  /* 偷电怪：矮胖，偏绿 */
        edge = 0x00060A08u; body = 0x00223428u; hi = 0x003A5442u; heightK = 0.78f; break;
    case 3:  /* 跳脸虫：暖褐 */
        edge = 0x000C0806u; body = 0x00342A20u; hi = 0x00524030u; heightK = 0.62f; break;
    case 4:  /* 影子：紫黑，几乎没实体 */
        edge = 0x0006040Cu; body = 0x001C1628u; hi = 0x002E2444u; heightK = 1.05f; break;
    case 5:  /* 盲螨：土黄，极扁 */
        edge = 0x000C0A04u; body = 0x00322914u; hi = 0x00503820u; heightK = 0.45f; break;
    default: /* 回响：钟形 */
        edge = 0x000A080Cu; body = 0x002A2234u; hi = 0x00443850u; heightK = 1.00f; break;
    }

    float H = 196.0f * heightK;         /* v2.2：拉满画布高度，怪物才显得高 */
    float footY = baseY;
    float headY = footY - H;
    float cx = c + lean;

    /* ================= 人形：寻声鬼 / 追光鬼 / 偷电怪 ================= */
    if (kind == 0 || kind == 1 || kind == 2) {
        float narrow = (kind == 1) ? 0.82f : (kind == 2 ? 1.35f : 1.0f);   /* 体型胖瘦 */
        float shoulderW = 30.0f * narrow;
        if (side) shoulderW *= 0.68f;

        float shoulderY = headY + H * 0.22f;
        float waistY    = headY + H * 0.55f;
        float pelvisY   = headY + H * 0.66f;

        /* --- 腿：大腿 + 小腿 + 脚，两段各带一点弯 --- */
        /* v3.0：**迈步**而不是「两腿对称外张」（建模审查 #2）。
           原来走路帧两腿同时外张（膝 ±5.8px、踝 ±0.1px），
           看起来是蹲一下，脚还随 baseY 往地里沉 4px。
           现在：一条腿往前伸、另一条往后蹬，髋部抬高、身体微倾。
           walkDir 让 pose1 和 pose3 的迈步方向相反，两个走路帧才不一样。 */
        int walkDir = (pose == 3) ? -1 : 1;
        float stepAmt = walk ? 9.0f : 0.0f;
        for (int s2 = -1; s2 <= 1; s2 += 2) {
            float hx = cx + (float)s2 * shoulderW * 0.34f;
            /* s2 * walkDir = +1 的那条腿在前，-1 的在后 */
            float ph = (float)(s2 * walkDir);
            float kneeX = hx + ph * stepAmt * 0.55f;
            float kneeY = (pelvisY + footY) * 0.5f - ph * stepAmt * 0.20f;
            float ankleX = hx + ph * stepAmt * 0.95f;
            Limb(hx, pelvisY, kneeX, kneeY, 13.0f, 10.0f, edge, body, hi);
            Limb(kneeX, kneeY, ankleX, footY - 4.0f, 10.0f, 7.0f, edge, body, hi);
            cvEllipse(ankleX, footY - 3.0f - (walk ? ph * 2.0f : 0.0f), 7.0f, 3.4f, edge, 1.0f);
        }

        /* --- 骨盆 --- */
        float pel[8] = { cx - shoulderW * 0.36f, pelvisY - 10,
                         cx + shoulderW * 0.36f, pelvisY - 10,
                         cx + shoulderW * 0.30f, pelvisY + 8,
                         cx - shoulderW * 0.30f, pelvisY + 8 };
        cvPoly(pel, 4, edge, 1.0f);
        cvPolyGrad(pel, 4, hi, body, 1.0f);

        /* --- 躯干：肩宽 → 腰窄 → 骨盆，用 6 边形 --- */
        float torso[12] = {
            cx - shoulderW, shoulderY,
            cx + shoulderW, shoulderY,
            cx + shoulderW * 0.62f, waistY,
            cx + shoulderW * 0.36f, pelvisY,
            cx - shoulderW * 0.36f, pelvisY,
            cx - shoulderW * 0.62f, waistY
        };
        float outline[12];
        for (int i = 0; i < 6; i++) {
            outline[i * 2] = cx + (torso[i * 2] - cx) * 1.06f;
            outline[i * 2 + 1] = torso[i * 2 + 1];
        }
        cvPoly(outline, 6, edge, 1.0f);
        cvPolyGrad(torso, 6, hi, body, 1.0f);

        /* --- 肋骨：5 道弧线 --- */
        for (int i = 0; i < 5; i++) {
            float ry = shoulderY + 10.0f + (float)i * ((waistY - shoulderY) * 0.16f);
            float rw = shoulderW * (0.88f - (float)i * 0.10f);
            cvLine(cx - rw, ry, cx + rw, ry + 2.0f, 2.0f, edge, 0.5f);
        }
        /* 脊柱中线 */
        cvLine(cx, shoulderY + 4, cx, pelvisY, 2.0f, edge, 0.6f);

        /* --- 手臂：上臂 + 前臂 + 爪 --- */
        float armSwing = walk ? 0.0f : 0.0f;
        for (int s2 = -1; s2 <= 1; s2 += 2) {
            float sx = cx + (float)s2 * shoulderW * 0.92f;
            float ex = cx + (float)s2 * (shoulderW + 10.0f) + armSwing;
            float ey = shoulderY + H * 0.20f;
            float wx = cx + (float)s2 * (shoulderW + 6.0f);
            float wy = shoulderY + H * 0.34f + (walk ? (float)s2 * 6.0f : 0.0f);
            Limb(sx, shoulderY, ex, ey, 11.0f, 8.0f, edge, body, hi);
            Limb(ex, ey, wx, wy, 8.0f, 6.0f, edge, body, hi);
            /* 三根手指 */
            for (int f = -1; f <= 1; f++) {
                float fx = wx + (float)f * 3.0f + (float)s2 * 2.0f;
                Limb(wx, wy, fx, wy + 11.0f, 3.0f, 1.4f, edge, body, hi);
            }
        }

        /* --- 脖子 --- */
        Limb(cx, headY + H * 0.19f, cx, shoulderY + 2.0f, 13.0f, 17.0f, edge, body, hi);

        /* --- 头 --- */
        float hry = (kind == 2) ? 15.0f : 19.0f;
        float hrx = (kind == 2) ? 17.0f : 15.0f;
        float hy = headY + hry + 2.0f;
        cvEllipse(cx, hy, hrx + 2.0f, hry + 2.0f, edge, 1.0f);
        cvEllipse(cx, hy - 1.0f, hrx, hry, body, 1.0f);
        cvEllipse(cx - hrx * 0.30f, hy - hry * 0.30f, hrx * 0.45f, hry * 0.40f, hi, 0.75f);

        if (kind == 0) {
            /* 寻声鬼：没有眼睛 —— 它靠听。取而代之的是一张咧开的下颌。 */
            float jaw[8] = { cx - hrx * 0.85f, hy + 4,
                             cx + hrx * 0.85f, hy + 4,
                             cx + hrx * 0.55f, hy + hry + 12.0f,
                             cx - hrx * 0.55f, hy + hry + 12.0f };
            cvPoly(jaw, 4, 0x00040208u, 1.0f);
            /* 牙 */
            for (int i = 0; i < 5; i++) {
                float tx = cx - hrx * 0.62f + (float)i * (hrx * 0.31f);
                cvLine(tx, hy + 5, tx + 1.5f, hy + 12, 2.4f, 0x00C8C0A8u, 0.9f);
                cvLine(tx, hy + hry + 10, tx + 1.5f, hy + hry + 2, 2.4f, 0x00B0A890u, 0.9f);
            }
            /* 眼窝：两个凹坑，不发光 */
            cvEllipse(cx - hrx * 0.42f, hy - 4, 5.0f, 4.0f, 0x00020406u, 0.9f);
            cvEllipse(cx + hrx * 0.42f, hy - 4, 5.0f, 4.0f, 0x00020406u, 0.9f);
        } else if (kind == 1) {
            /* 追光鬼：一对又大又亮的眼睛（它追光） */
            cvEllipse(cx - hrx * 0.40f, hy - 2, 7.0f, 6.0f, 0x00080A10u, 1.0f);
            cvEllipse(cx + hrx * 0.40f, hy - 2, 7.0f, 6.0f, 0x00080A10u, 1.0f);
            cvEllipse(cx - hrx * 0.40f, hy - 2, 5.0f, 4.2f, 0x00D8F4FFu, 1.0f);
            cvEllipse(cx + hrx * 0.40f, hy - 2, 5.0f, 4.2f, 0x00D8F4FFu, 1.0f);
            cvGlow(cx, hy - 2, 26.0f, 0x0060C0FFu, 0.30f);
        } else {
            /* 偷电怪：一簇小眼睛 + 背上的导管 */
            for (int i = -1; i <= 1; i++) {
                cvEllipse(cx + (float)i * 7.0f, hy - 4, 3.6f, 3.0f, 0x00080A08u, 1.0f);
                cvEllipse(cx + (float)i * 7.0f, hy - 4, 2.4f, 1.9f, 0x0070FFA0u, 1.0f);
            }
            for (int i = 0; i < 3; i++) {
                float bx = cx + (float)(i - 1) * 12.0f;
                cvLine(bx, shoulderY + 4, bx + 4.0f, shoulderY - 22.0f, 4.0f, edge, 1.0f);
                cvLine(bx, shoulderY + 4, bx + 4.0f, shoulderY - 22.0f, 2.4f, 0x0040A060u, 0.9f);
            }
        }

    /* ================= 非人形 ================= */
    } else if (kind == 5) {
        /* 盲螨：五节横body + 八条细腿 */
        for (int i = 0; i < 5; i++) {
            float sx = cx - 52.0f + (float)i * 26.0f;
            float sy = footY - 30.0f + sinf((float)i * 1.1f + (walk ? 1.6f : 0.0f)) * 5.0f;
            float rr = 17.0f - fabsf((float)i - 2.0f) * 2.2f;
            cvEllipse(sx, sy, rr + 2, rr + 2, edge, 1.0f);
            cvEllipse(sx, sy - 1, rr, rr, body, 1.0f);
            cvEllipse(sx - rr * 0.3f, sy - rr * 0.35f, rr * 0.45f, rr * 0.35f, hi, 0.6f);
        }
        for (int i = 0; i < 8; i++) {
            float sx = cx - 56.0f + (float)i * 16.0f;
            float kx = sx + (float)((i & 1) ? 10 : -10);
            Limb(sx, footY - 32.0f, kx, footY - 12.0f, 4.0f, 3.0f, edge, body, hi);
            Limb(kx, footY - 12.0f, kx + (float)((i & 1) ? 6 : -6), footY, 3.0f, 2.0f, edge, body, hi);
        }
    } else if (kind == 3) {
        /* 跳脸虫：圆身 + 四对足 + 正面的大嘴 */
        float rr = 40.0f;
        float cy2 = footY - 52.0f;
        cvEllipse(cx, cy2, rr + 2, rr + 2, edge, 1.0f);
        cvEllipse(cx, cy2 - 1, rr, rr, body, 1.0f);
        cvEllipse(cx - rr * 0.3f, cy2 - rr * 0.35f, rr * 0.42f, rr * 0.34f, hi, 0.7f);
        /* 嘴 */
        cvEllipse(cx, cy2 + 8, 15.0f, 8.0f, 0x00040204u, 1.0f);
        for (int i = 0; i < 4; i++) {
            float tx = cx - 11.0f + (float)i * 7.0f;
            cvLine(tx, cy2 + 2, tx + 1.0f, cy2 + 8, 2.2f, 0x00C8C0A0u, 0.9f);
        }
        for (int i = 0; i < 4; i++) {
            float a = 0.5f + (float)i * 0.62f;
            float lx = cx + cosf(a) * 58.0f, ly = cy2 + sinf(a) * 40.0f;
            Limb(cx, cy2 - 6.0f, lx, ly, 6.0f, 3.0f, edge, body, hi);
        }
    } else if (kind == 4) {
        /* 影子：只有一团边缘 + 撕裂的下摆，没有实心身体 */
        for (int i = 0; i < 5; i++) {
            float sx = cx + (float)(i - 2) * 12.0f;
            float sy = footY - 70.0f - (float)(2 - abs(i - 2)) * 12.0f;
            cvEllipse(sx, sy, 20.0f, 30.0f, edge, 0.30f);
        }
        for (int i = 0; i < 7; i++) {
            float sx = cx - 40.0f + (float)i * 13.0f;
            float dx2 = (float)((i & 1) ? 10 : -10);
            Limb(sx, footY - 50.0f, sx + dx2, footY, 7.0f, 1.5f, edge, body, hi);
        }
        /* 一对极小的紫眼 —— 影子几乎没有实体，只留两点 */
        cvEllipse(cx - 8, footY - 88.0f, 3.0f, 2.4f, 0x007040A0u, 0.9f);
        cvEllipse(cx + 8, footY - 88.0f, 3.0f, 2.4f, 0x007040A0u, 0.9f);
    } else {
        /* 回响：倒挂的钟形 + 纵向裂口 */
        float top = footY - H;
        float bell[10] = {
            cx - 30, top + 6,
            cx + 30, top + 6,
            cx + 42, top + H * 0.62f,
            cx + 16, footY - 6,
            cx - 16, footY - 6
        };
        float outline[10];
        for (int i = 0; i < 5; i++) {
            outline[i * 2] = cx + (bell[i * 2] - cx) * 1.08f;
            outline[i * 2 + 1] = bell[i * 2 + 1];
        }
        cvPoly(outline, 5, edge, 1.0f);
        cvPolyGrad(bell, 5, hi, body, 1.0f);
        for (int i = 0; i < 4; i++) {
            float lx = cx - 20.0f + (float)i * 13.0f;
            cvLine(lx, top + 20, lx + 2.0f, footY - 20, 2.0f, edge, 0.55f);
        }
        cvEllipse(cx, top + 16, 10.0f, 6.0f, 0x00060408u, 1.0f);
        cvEllipse(cx, top + 16, 6.0f, 3.4f, 0x00A0FFC0u, 0.85f);
    }

    /* 左缘轮廓光：让剪影在暗场里"立"起来 */
    {
        int w = CANVAS_W;
        for (int y = 0; y < CANVAS_H; y++) {
            for (int x = 1; x < w; x++) {
                unsigned int a = g_canvas[(size_t)y * w + x];
                unsigned int b = g_canvas[(size_t)y * w + x - 1];
                if ((a >> 24) > 128 && (b >> 24) < 32) {
                    unsigned int px2 = g_canvas[(size_t)y * w + x];
                    int r = (px2 >> 16) & 0xFF, g2 = (px2 >> 8) & 0xFF, bb = px2 & 0xFF;
                    r = r + 34; g2 = g2 + 34; bb = bb + 42;
                    if (r > 255) r = 255;
                    if (g2 > 255) g2 = 255;
                    if (bb > 255) bb = 255;
                    g_canvas[(size_t)y * w + x] = 0xFF000000u | (r << 16) | (g2 << 8) | bb;
                }
            }
        }
    }

    cvFinish(&g_sprites[id]);
}

static void BakeProp(int id, int kind) {
    cvClear();
    float c = CANVAS_W * 0.5f;
    float baseY = 244.0f;

    if (kind == 0) {            /* 箱子：木条 + 铁角 */
        float b[8] = { c - 46, baseY - 96, c + 46, baseY - 96, c + 46, baseY, c - 46, baseY };
        cvPoly(b, 4, 0x000A0806u, 1.0f);
        float b2[8] = { c - 40, baseY - 90, c + 40, baseY - 90, c + 40, baseY - 4, c - 40, baseY - 4 };
        cvPolyGrad(b2, 4, 0x00906A40u, 0x00402C18u, 1.0f);
        for (int i = 0; i < 3; i++)
            cvLine(c - 40, baseY - 74 + (float)i * 26, c + 40, baseY - 74 + (float)i * 26, 3.0f, 0x00301C0Cu, 0.8f);
        cvLine(c - 40, baseY - 90, c - 40, baseY - 4, 8.0f, 0x00605848u, 1.0f);
        cvLine(c + 40, baseY - 90, c + 40, baseY - 4, 8.0f, 0x00605848u, 1.0f);
        cvSpeckle((int)c, (int)baseY - 50, 36, 50, 0x00301808u, 0.4f);
    } else if (kind == 1) {     /* 配电箱：铁柜 + 警示条 */
        float b[8] = { c - 34, baseY - 108, c + 34, baseY - 108, c + 34, baseY, c - 34, baseY };
        cvPoly(b, 4, 0x000A0C10u, 1.0f);
        float b2[8] = { c - 29, baseY - 102, c + 29, baseY - 102, c + 29, baseY - 4, c - 29, baseY - 4 };
        cvPolyGrad(b2, 4, 0x00586068u, 0x00202830u, 1.0f);
        /* 黄黑警示条 */
        for (int i = 0; i < 4; i++)
            cvLine(c - 24, baseY - 92 + (float)i * 8, c + 24, baseY - 88 + (float)i * 8, 5.0f, 0x00D8B020u, 0.85f);
        cvEllipse(c, baseY - 40, 8.0f, 8.0f, 0x00A02010u, 1.0f);
        cvLine(c, baseY - 40, c, baseY - 52, 3.0f, 0x00F0F0F0u, 0.6f);
    } else if (kind == 2) {     /* 出口：门框 + 光 */
        float b[8] = { c - 40, baseY - 150, c + 40, baseY - 150, c + 40, baseY, c - 40, baseY };
        cvPoly(b, 4, 0x000A0C10u, 1.0f);
        float b2[8] = { c - 33, baseY - 142, c + 33, baseY - 142, c + 33, baseY - 4, c - 33, baseY - 4 };
        cvPolyGrad(b2, 4, 0x00C0E8FFu, 0x00306080u, 1.0f);
        cvGlow(c, baseY - 70, 60.0f, 0x0060C0FFu, 0.35f);
    } else if (kind == 4) {     /* 翻过的空箱：盖子掀开、里面发黑 */
        float base[8] = { c - 44, baseY - 58, c + 44, baseY - 58, c + 44, baseY, c - 44, baseY };
        cvPoly(base, 4, 0x000A0806u, 1.0f);
        float inner[8] = { c - 38, baseY - 52, c + 38, baseY - 52, c + 38, baseY - 5, c - 38, baseY - 5 };
        cvPolyGrad(inner, 4, 0x00402C18u, 0x00140E08u, 1.0f);
        /* 箱口：一条深色横带，读作“空的” */
        float hole[8] = { c - 34, baseY - 55, c + 34, baseY - 55, c + 30, baseY - 40, c - 30, baseY - 40 };
        cvPoly(hole, 4, 0x00060404u, 1.0f);
        /* 掀起的盖子：向后上方斜 */
        float lid[8] = { c - 48, baseY - 56, c - 42, baseY - 104, c + 38, baseY - 112, c + 46, baseY - 60 };
        cvPoly(lid, 4, 0x00120C08u, 1.0f);
        float lid2[8] = { c - 42, baseY - 60, c - 36, baseY - 98, c + 34, baseY - 105, c + 40, baseY - 64 };
        cvPolyGrad(lid2, 4, 0x00906A40u, 0x00302014u, 1.0f);
        cvLine(c - 44, baseY - 58, c + 44, baseY - 58, 3.0f, 0x00201810u, 0.9f);
    } else if (kind == 5) {     /* 未解锁的箱子：普通木箱 + 正面金锁 */
        float b[8] = { c - 46, baseY - 96, c + 46, baseY - 96, c + 46, baseY, c - 46, baseY };
        cvPoly(b, 4, 0x000A0806u, 1.0f);
        float b2[8] = { c - 40, baseY - 90, c + 40, baseY - 90, c + 40, baseY - 4, c - 40, baseY - 4 };
        cvPolyGrad(b2, 4, 0x00906A40u, 0x00402C18u, 1.0f);
        for (int i = 0; i < 3; i++)
            cvLine(c - 40, baseY - 74 + (float)i * 26, c + 40, baseY - 74 + (float)i * 26, 3.0f, 0x00301C0Cu, 0.8f);
        cvLine(c - 40, baseY - 90, c - 40, baseY - 4, 8.0f, 0x00605848u, 1.0f);
        cvLine(c + 40, baseY - 90, c + 40, baseY - 4, 8.0f, 0x00605848u, 1.0f);
        /* 锁体 + 锁梁 */
        float lock[8] = { c - 18, baseY - 58, c + 18, baseY - 58, c + 18, baseY - 22, c - 18, baseY - 22 };
        cvPoly(lock, 4, 0x006A4E10u, 1.0f);
        cvPoly(lock, 4, 0x00C8A030u, 0.65f);
        cvLine(c - 10, baseY - 58, c - 10, baseY - 72, 4.0f, 0x00E0C050u, 1.0f);
        cvLine(c + 10, baseY - 58, c + 10, baseY - 72, 4.0f, 0x00E0C050u, 1.0f);
        cvLine(c - 10, baseY - 72, c + 10, baseY - 72, 4.0f, 0x00E0C050u, 1.0f);
        cvEllipse(c, baseY - 40, 4.0f, 5.0f, 0x00201808u, 1.0f);
    } else if (kind == 6) {     /* 藏身点：垃圾桶 */
        float body[8] = { c - 34, baseY - 150, c + 34, baseY - 150, c + 42, baseY, c - 42, baseY };
        cvPoly(body, 4, 0x000A0C0Au, 1.0f);
        float body2[8] = { c - 28, baseY - 144, c + 28, baseY - 144, c + 35, baseY - 5, c - 35, baseY - 5 };
        cvPolyGrad(body2, 4, 0x00607868u, 0x00283028u, 1.0f);
        for (int i = -2; i <= 2; i++)
            cvLine(c + (float)i * 13.0f, baseY - 140, c + (float)i * 16.0f, baseY - 8, 2.0f, 0x00202820u, 0.75f);
        cvEllipse(c, baseY - 150, 36.0f, 9.0f, 0x003A4A40u, 1.0f);
        cvEllipse(c, baseY - 152, 30.0f, 6.0f, 0x00060604u, 0.95f);
        cvLine(c - 38, baseY - 158, c + 38, baseY - 158, 5.0f, 0x00809878u, 0.9f);
        cvLine(c - 38, baseY - 158, c - 34, baseY - 168, 4.0f, 0x00607858u, 0.8f);
        cvLine(c + 38, baseY - 158, c + 34, baseY - 168, 4.0f, 0x00607858u, 0.8f);
        cvLine(c - 34, baseY - 168, c + 34, baseY - 168, 4.0f, 0x00809878u, 0.85f);
    } else {
        /* 安全屋（v2.2 第三次重做）
           第一版是营火 -> 一颗蛋。第二版是沙袋+立杆+油灯 -> 一根路灯 + 两块鹅卵石 + 一张饼。
           教训（来自精灵审计）：
             · 圆/椭圆只读作"石头、土豆、蛋"；要读作"沙袋"必须是圆角矩形 + 束口线
             · 一个物件代表不了"一块区域"，必须先画地面圆环再画门框
             · 地面的"光池"如果 alpha 被写成 255，就是一张不透明薄饼
           这一版按审计的建议：地面断续圆环（留一个入口缺口）+ 门框 + 挂灯 + 沙袋堆。 */
        float baseYY = baseY - 2.0f;

        /* ① 地面圆环：2px 断续弧线，留一个缺口当入口。
              画在画布最下方，环宽铺满 —— "范围"是靠这个读出来的。 */
        {
            float rx = 108.0f, ry = 16.0f;
            for (int a = 0; a < 360; a += 6) {
                /* 入口缺口：60~120 度不画 */
                if (a > 55 && a < 125) continue;
                float rad = (float)a * 3.14159f / 180.0f;
                float ax = c + cosf(rad) * rx;
                float ay = baseYY + sinf(rad) * ry;
                cvEllipse(ax, ay, 2.2f, 1.8f, 0x00E0A050u, 0.55f);
            }
        }

        /* ② 门框：两根立柱 + 一根横梁，顶到画布 90% */
        {
            float fx0 = c - 46.0f, fx1 = c + 46.0f;
            float topY = baseY - 196.0f;
            /* 立柱 */
            for (int i = 0; i < 2; i++) {
                float px = (i == 0) ? fx0 : fx1;
                float post[8] = { px - 5, topY, px + 5, topY, px + 5, baseYY - 6, px - 5, baseYY - 6 };
                cvPoly(post, 4, 0x00100C08u, 1.0f);
                float post2[8] = { px - 3, topY + 3, px + 3, topY + 3, px + 3, baseYY - 9, px - 3, baseYY - 9 };
                cvPolyGrad(post2, 4, 0x00907850u, 0x004A3A22u, 1.0f);
            }
            /* 横梁 */
            float lintel[8] = { fx0 - 9, topY - 12, fx1 + 9, topY - 12, fx1 + 9, topY, fx0 - 9, topY };
            cvPoly(lintel, 4, 0x00100C08u, 1.0f);
            float lintel2[8] = { fx0 - 6, topY - 9, fx1 + 6, topY - 9, fx1 + 6, topY - 2, fx0 - 6, topY - 2 };
            cvPolyGrad(lintel2, 4, 0x00A08458u, 0x00544228u, 1.0f);
            /* 门洞内：暖色薄雾（比外面亮一档，但**半透明**） */
            for (int yy = (int)topY; yy < (int)(baseYY - 8); yy++) {
                for (int xx = (int)(fx0 + 5); xx < (int)(fx1 - 4); xx++) {
                    if (xx < 0 || xx >= CANVAS_W || yy < 0 || yy >= CANVAS_H) continue;
                    unsigned int* q = &g_canvas[(size_t)yy * CANVAS_W + xx];
                    unsigned int old = *q;
                    if ((old >> 24) > 40) continue;        /* 别盖住已画的东西 */
                    int orr = (old >> 16) & 0xFF, og = (old >> 8) & 0xFF, ob = old & 0xFF;
                    orr += 26; og += 18; ob += 8;
                    int na = (old >> 24) + 70;
                    if (na > 190) na = 190;
                    if (orr > 255) orr = 255;
                    if (og > 255) og = 255;
                    if (ob > 255) ob = 255;
                    *q = ((unsigned int)na << 24) | (orr << 16) | (og << 8) | ob;
                }
            }
        }

        /* ③ 挂灯：吊在横梁正中 */
        {
            float lx = c, ly = baseY - 178.0f;
            cvLine(lx, ly - 8, lx, ly, 2.0f, 0x00100C08u, 1.0f);
            float cage[8] = { lx - 10, ly, lx + 10, ly, lx + 7, ly + 24, lx - 7, ly + 24 };
            float co[8];
            for (int i = 0; i < 4; i++) {
                co[i * 2] = lx + (cage[i * 2] - lx) * 1.15f;
                co[i * 2 + 1] = ly + (cage[i * 2 + 1] - ly) * 1.08f;
            }
            cvPoly(co, 4, 0x00140C06u, 1.0f);
            cvPoly(cage, 4, 0x006A5220u, 1.0f);
            cvEllipse(lx, ly + 13.0f, 6.5f, 7.5f, 0x00FFE8A0u, 1.0f);
            cvEllipse(lx, ly + 13.0f, 3.6f, 4.4f, 0x00FFFFFFu, 0.92f);
            float cap[8] = { lx - 12, ly - 1, lx + 12, ly - 1, lx + 5, ly - 8, lx - 5, ly - 8 };
            cvPoly(cap, 4, 0x00140C06u, 1.0f);
            cvPoly(cap, 4, 0x007A5E28u, 0.9f);
            /* 只留一个 glow，且收紧半径 */
            cvGlow(lx, ly + 13.0f, 30.0f, 0x00FFB050u, 0.28f);
        }

        /* ④ 沙袋：2-1-2 堆在门框右侧。
              圆角矩形 + 束口线 + 缝线 —— 椭圆读作石头，这是审计里最关键的一条。 */
        {
            float bags[5][3] = {
                { c + 66.0f, baseYY - 10.0f, 1.0f },
                { c + 66.0f, baseYY - 26.0f, 1.0f },
                { c + 88.0f, baseYY - 10.0f, 1.0f },
                { c + 88.0f, baseYY - 26.0f, 1.0f },
                { c + 77.0f, baseYY - 42.0f, 1.0f }
            };
            for (int i = 0; i < 5; i++) {
                float bx = bags[i][0], by = bags[i][1];
                float w = 30.0f, h = 16.0f;
                /* 圆角矩形：中心实心 + 两端半圆 */
                cvEllipse(bx - w * 0.5f + 7.0f, by, 7.0f, h * 0.5f, 0x00120E0Au, 1.0f);
                cvEllipse(bx + w * 0.5f - 7.0f, by, 7.0f, h * 0.5f, 0x00120E0Au, 1.0f);
                for (int yy = (int)(by - h * 0.5f); yy <= (int)(by + h * 0.5f); yy++) {
                    for (int xx = (int)(bx - w * 0.5f + 6); xx <= (int)(bx + w * 0.5f - 6); xx++) {
                        cvPx(xx, yy, 0x00120E0Au, 1.0f);
                    }
                }
                /* 里面填土黄 */
                cvEllipse(bx - w * 0.5f + 8.0f, by - 1.0f, 6.0f, h * 0.5f - 1.5f, 0x00584A30u, 1.0f);
                cvEllipse(bx + w * 0.5f - 8.0f, by - 1.0f, 6.0f, h * 0.5f - 1.5f, 0x00584A30u, 1.0f);
                for (int yy = (int)(by - h * 0.5f + 2); yy <= (int)(by + h * 0.5f - 2); yy++) {
                    for (int xx = (int)(bx - w * 0.5f + 6); xx <= (int)(bx + w * 0.5f - 6); xx++) {
                        cvPx(xx, yy, 0x00584A30u, 1.0f);
                    }
                }
                /* 束口线（两端各一道竖线）+ 中间缝线 */
                cvLine(bx - w * 0.5f + 9.0f, by - h * 0.5f + 1, bx - w * 0.5f + 9.0f, by + h * 0.5f - 1, 1.6f, 0x00362C1Au, 0.9f);
                cvLine(bx + w * 0.5f - 9.0f, by - h * 0.5f + 1, bx + w * 0.5f - 9.0f, by + h * 0.5f - 1, 1.6f, 0x00362C1Au, 0.9f);
                cvLine(bx - w * 0.5f + 10.0f, by, bx + w * 0.5f - 10.0f, by + 0.5f, 1.2f, 0x0074643Cu, 0.7f);
                /* 顶部受光 */
                cvLine(bx - w * 0.5f + 10.0f, by - h * 0.5f + 2, bx + w * 0.5f - 10.0f, by - h * 0.5f + 2, 1.4f, 0x007C6A44u, 0.6f);
            }
        }
    }
    cvFinish(&g_sprites[id]);
}


/* ------------------------------------------------------------------ */
/* UI 资源（v2.2）                                                      */
/*   9-slice 母版：四角各一颗螺丝，双线边框（外亮内暗），底部警示条。      */
/*   四角 12px 不拉伸，四条边拉伸 —— 见 UiPanel()。                      */
/* ------------------------------------------------------------------ */
static void BakeUiPanel(int id, int hot) {
    cvClear();
    unsigned int edgeHi = hot ? 0x00E0A050u : 0x00807868u;
    unsigned int edgeLo = hot ? 0x00604020u : 0x00342C24u;
    unsigned int fill   = hot ? 0x00241E16u : 0x001C1814u;   /* v2.2：提亮，暗场里要看得见 */

    /* 底 */
    float bg[8] = { 4, 4, 252, 4, 252, 252, 4, 252 };
    cvPoly(bg, 4, fill, 0.97f);
    /* 双线边框 */
    float o[8] = { 2, 2, 254, 2, 254, 254, 2, 254 };
    cvPoly(o, 4, edgeHi, 0.9f);
    float i1[8] = { 6, 6, 250, 6, 250, 250, 6, 250 };
    cvPoly(i1, 4, edgeLo, 0.9f);
    float i2[8] = { 9, 9, 247, 9, 247, 247, 9, 247 };
    cvPoly(i2, 4, fill, 1.0f);

    /* 底部警示条（工业感的主要来源） */
    for (int x = 8; x < 248; x += 16) {
        float st[8] = { (float)x, 244, (float)x + 8, 244, (float)x + 16, 250, (float)x + 8, 250 };
        cvPoly(st, 4, hot ? 0x00C08020u : 0x00504820u, 0.75f);
    }

    /* 四角螺丝 */
    int sx[4] = { 12, 244, 12, 244 }, sy[4] = { 12, 12, 244, 244 };
    for (int i = 0; i < 4; i++) {
        cvEllipse((float)sx[i], (float)sy[i], 6.0f, 6.0f, 0x000A0806u, 1.0f);
        cvEllipse((float)sx[i], (float)sy[i], 4.0f, 4.0f, 0x00908878u, 1.0f);
        cvLine((float)sx[i] - 3, (float)sy[i], (float)sx[i] + 3, (float)sy[i] + 1, 1.6f, 0x00302820u, 1.0f);
    }

    cvFinish(&g_sprites[id]);
}


static void BakeUiCursor(void) {
    cvClear();
    /* 三角箭头：实心 + 描边 */
    float t[6] = { 40, 30, 40, 226, 150, 128 };
    float t2[6] = { 52, 52, 52, 204, 128, 128 };
    cvPoly(t, 3, 0x000A0806u, 1.0f);
    cvPolyGrad(t2, 3, 0x00FFD880u, 0x00C08020u, 1.0f);
    cvFinish(&g_sprites[SPR_UI_CURSOR]);
}

static void BakeUiRow(void) {
    cvClear();
    /* 选中行底：中间亮两头暗的横带 */
    for (int y = 0; y < CANVAS_H; y++) {
        float t = ((float)y / (float)(CANVAS_H - 1)) - 0.5f;
        float k = 1.0f - fabsf(t) * 2.2f;
        if (k < 0.0f) continue;
        for (int x = 0; x < CANVAS_W; x++) {
            float fx = (float)x / (float)(CANVAS_W - 1);
            float fade = 1.0f;
            if (fx < 0.10f) fade = fx / 0.10f;
            if (fx > 0.90f) fade = (1.0f - fx) / 0.10f;
            cvPx(x, y, 0x00C8A050u, k * fade * 0.22f);
        }
    }
    cvFinish(&g_sprites[SPR_UI_ROW]);
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */
void SpritesInit(void) {
    if (g_ready) return;
    for (int i = 0; i < SPR_COUNT; i++) {
        g_sprites[i].w = SPR_W;
        g_sprites[i].h = SPR_H;
        g_sprites[i].px = (unsigned int*)malloc(sizeof(unsigned int) * SPR_W * SPR_H);
        g_sprites[i].shade = (unsigned int*)malloc(sizeof(unsigned int) * SPR_SHADES);
        if (!g_sprites[i].px || !g_sprites[i].shade) { g_ready = 0; return; }
        memset(g_sprites[i].px, 0, sizeof(unsigned int) * SPR_W * SPR_H);
    }
    BakeWrench();
    BakePistol();
    BakeSigil();
    for (int i = 0; i < 8; i++) BakeItem(SPR_ITEM_FLARE + i, i);
    for (int p = 0; p < 4; p++) {
        BakeMonster(SPR_MON_HUNTER + p, 0, p);
        BakeMonster(SPR_MON_LIGHT  + p, 1, p);
        BakeMonster(SPR_MON_LEECH  + p, 2, p);
    }
    BakeMonster(SPR_MON_JUMPER, 3, 0);
    BakeMonster(SPR_MON_SHADE, 4, 0);
    BakeMonster(SPR_MON_SPIDER, 5, 0);
    BakeMonster(SPR_MON_ECHO, 6, 0);
    BakeProp(SPR_PROP_CRATE, 0);
    BakeProp(SPR_PROP_BREAKER, 1);
    BakeProp(SPR_PROP_EXIT, 2);
    BakeProp(SPR_PROP_SAFE, 3);
    BakeProp(SPR_PROP_CRATE_OPEN, 4);
    BakeProp(SPR_PROP_CRATE_LOCKED, 5);
    BakeProp(SPR_PROP_TRASH, 6);
    BakeUiPanel(SPR_UI_PANEL, 0);
    BakeUiPanel(SPR_UI_PANEL_HOT, 1);
    BakeUiCursor();
    BakeUiRow();
    g_ready = 1;
}

Sprite* SpriteGet(int id) {
    if (!g_ready || id < 0 || id >= SPR_COUNT) return NULL;
    return &g_sprites[id];
}

int SpritesReady(void) { return g_ready; }

/* ------------------------------------------------------------------ */
/* 运行时 blit：双线性 + 亮度查表 + alpha + 深度测试                      */
/* ------------------------------------------------------------------ */
void DrawSpriteTint(int id, int x, int y, int w, int h,
                    float light, float alpha, int zTest, float zDist,
                    unsigned int tint, float tintMix) {
    if (!g_ready || w <= 0 || h <= 0) return;
    if (alpha <= 0.003f) return;
    Sprite* s = &g_sprites[id];

    int li = (int)(light * (float)(SPR_SHADES - 1) + 0.5f);
    if (li < 0) li = 0;
    if (li >= SPR_SHADES) li = SPR_SHADES - 1;
    unsigned int lum = s->shade[li];
    int lr = (lum >> 16) & 0xFF;      /* 0..255 的亮度系数 */

    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= renderH) continue;
        float v = ((float)dy + 0.5f) / (float)h * (float)s->h - 0.5f;
        int ty0 = (int)floorf(v);
        float fy = v - (float)ty0;
        int ty1 = ty0 + 1;
        if (ty0 < 0) ty0 = 0;
        if (ty1 < 0) ty1 = 0;
        if (ty0 > s->h - 1) ty0 = s->h - 1;
        if (ty1 > s->h - 1) ty1 = s->h - 1;
        const unsigned int* row0 = s->px + (size_t)ty0 * s->w;
        const unsigned int* row1 = s->px + (size_t)ty1 * s->w;
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= renderW) continue;
            if (zTest && zDist >= zBuffer[px]) continue;
            float u = ((float)dx + 0.5f) / (float)w * (float)s->w - 0.5f;
            int tx0 = (int)floorf(u);
            float fx = u - (float)tx0;
            int tx1 = tx0 + 1;
            if (tx0 < 0) tx0 = 0;
            if (tx1 < 0) tx1 = 0;
            if (tx0 > s->w - 1) tx0 = s->w - 1;
            if (tx1 > s->w - 1) tx1 = s->w - 1;
            unsigned int p00 = row0[tx0], p10 = row0[tx1];
            unsigned int p01 = row1[tx0], p11 = row1[tx1];
            float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy);
            float w01 = (1 - fx) * fy, w11 = fx * fy;
            float a = ((p00 >> 24) * w00 + (p10 >> 24) * w10 +
                       (p01 >> 24) * w01 + (p11 >> 24) * w11) / 255.0f;
            if (a < 0.004f) continue;
            int r = (int)(((p00 >> 16) & 0xFF) * w00 + ((p10 >> 16) & 0xFF) * w10 +
                          ((p01 >> 16) & 0xFF) * w01 + ((p11 >> 16) & 0xFF) * w11);
            int g = (int)(((p00 >> 8) & 0xFF) * w00 + ((p10 >> 8) & 0xFF) * w10 +
                          ((p01 >> 8) & 0xFF) * w01 + ((p11 >> 8) & 0xFF) * w11);
            int b = (int)((p00 & 0xFF) * w00 + (p10 & 0xFF) * w10 +
                          (p01 & 0xFF) * w01 + (p11 & 0xFF) * w11);
            /* 亮度查表（一次乘法替代三次浮点除） */
            r = (r * lr) >> 8;
            g = (g * lr) >> 8;
            b = (b * lr) >> 8;
            if (tintMix > 0.001f) {
                int tr = (tint >> 16) & 0xFF, tg = (tint >> 8) & 0xFF, tb = tint & 0xFF;
                r = r + (int)((tr - r) * tintMix);
                g = g + (int)((tg - g) * tintMix);
                b = b + (int)((tb - b) * tintMix);
            }
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            PutPixelBlend(px, py, (unsigned int)((r << 16) | (g << 8) | b),
                          a * alpha);
        }
    }
}

void DrawSprite(int id, int x, int y, int w, int h,
                float light, float alpha, int zTest, float zDist) {
    DrawSpriteTint(id, x, y, w, h, light, alpha, zTest, zDist, 0, 0.0f);
}

/* 子矩形 blit（9-slice / 图标裁切） */
void DrawSpriteSub(int id, int sx, int sy, int sw, int sh,
                   int dx, int dy, int dw, int dh, float light, float alpha) {
    if (!g_ready || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    if (alpha <= 0.003f) return;
    Sprite* s = &g_sprites[id];
    int li = (int)(light * (float)(SPR_SHADES - 1) + 0.5f);
    if (li < 0) li = 0;
    if (li >= SPR_SHADES) li = SPR_SHADES - 1;
    int lr = (s->shade[li] >> 16) & 0xFF;

    int x0 = sx < 0 ? 0 : sx;
    int y0 = sy < 0 ? 0 : sy;
    int x1 = sx + sw; if (x1 > s->w) x1 = s->w;
    int y1 = sy + sh; if (y1 > s->h) y1 = s->h;
    if (x1 <= x0 || y1 <= y0) return;

    for (int j = 0; j < dh; j++) {
        int py = dy + j;
        if (py < 0 || py >= renderH) continue;
        int srcY = y0 + (j * sh) / dh;
        if (srcY >= s->h) srcY = s->h - 1;
        for (int i = 0; i < dw; i++) {
            int px = dx + i;
            if (px < 0 || px >= renderW) continue;
            int srcX = x0 + (i * sw) / dw;
            if (srcX >= s->w) srcX = s->w - 1;
            unsigned int p = s->px[(size_t)srcY * s->w + srcX];
            float a = (float)(p >> 24) / 255.0f;
            if (a < 0.004f) continue;
            int r = (int)(((p >> 16) & 0xFF) * lr) >> 8;
            int g = (int)(((p >> 8) & 0xFF) * lr) >> 8;
            int b = (int)((p & 0xFF) * lr) >> 8;
            PutPixelBlend(px, py, (unsigned int)((r << 16) | (g << 8) | b), a * alpha);
        }
    }
}

/* 水平镜像 blit */
void DrawSpriteFlip(int id, int x, int y, int w, int h,
                    float light, float alpha, int zTest, float zDist) {
    if (!g_ready || w <= 0 || h <= 0 || alpha <= 0.003f) return;
    Sprite* s = &g_sprites[id];
    int li = (int)(light * (float)(SPR_SHADES - 1) + 0.5f);
    if (li < 0) li = 0;
    if (li >= SPR_SHADES) li = SPR_SHADES - 1;
    int lr = (s->shade[li] >> 16) & 0xFF;
    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= renderH) continue;
        int sy = (dy * s->h) / h;
        if (sy >= s->h) sy = s->h - 1;
        const unsigned int* row = s->px + (size_t)sy * s->w;
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= renderW) continue;
            if (zTest && zDist >= zBuffer[px]) continue;
            int sx = s->w - 1 - (dx * s->w) / w;
            if (sx < 0) sx = 0;
            unsigned int p = row[sx];
            float a = (float)(p >> 24) / 255.0f;
            if (a < 0.004f) continue;
            int r = (int)(((p >> 16) & 0xFF) * lr) >> 8;
            int g = (int)(((p >> 8) & 0xFF) * lr) >> 8;
            int b = (int)((p & 0xFF) * lr) >> 8;
            PutPixelBlend(px, py, (unsigned int)((r << 16) | (g << 8) | b), a * alpha);
        }
    }
}

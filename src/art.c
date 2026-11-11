/*
 * CANITY — 共享美术运算实现。
 * 规范在 include/art_direction.h；这里提供跨模型复用的颜色运算、
 * 距离可读性、斜角／内凹受光、有序抖动。
 *
 * 模块边界：具体造型与变体在 sprites.c，世界绘制在 render.c，
 * UI 主题在 ui_theme.h，布局与合成在 ui.c，音乐调度在 music.c。
 * 共享算法集中管理，具体表现留在实际负责的模块。
 */
#include "common.h"
#include "entity.h"
#include "art.h"

/* ================================================================== */
/* §1 调色板常量                                                       */
/* ================================================================== */
/*
 * 规则（来自 art_direction.h 铁律十六／十九）：
 *   身体一律中性暗灰；**颜色只留给识别特征**。
 *   所以这里只定义三档灰 + 少量特征色。
 */
const unsigned int ART_GREY_EDGE = 0x00080A0Cu;   /* 最暗：轮廓 / 背面 */
const unsigned int ART_GREY_BODY = 0x002A2C30u;   /* 主体 */
const unsigned int ART_GREY_LIT  = 0x00444A52u;   /* 受光面 */
const unsigned int ART_GREY_HOT  = 0x006A7480u;   /* 高光边（只用在被光直射的一侧） */

const unsigned int ART_EXIT_GAP_ON  = 0x0060F0D0u;  /* 出口门缝：通电 */
const unsigned int ART_EXIT_GAP_OFF = 0x000C1014u;  /* 出口门缝：断电 */
const unsigned int ART_BEAM         = 0x0060F0D0u;  /* 光柱 */

/* ================================================================== */
/* §2 距离可读性曲线                                                   */
/* ================================================================== */

/*
 * 怪物：越看不见越可怕。
 *   2.5 格内全亮 / 11 格外只剩 5%
 */
float ArtMonsterLight(float wx, float wy) {

    float dx = wx - posX, dy = wy - posY;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= 2.5f) return 1.0f;
    if (dist >= 11.0f) return 0.05f;
    float k = 1.0f - (dist - 2.5f) / (11.0f - 2.5f);
    if (k < 0.0f) k = 0.0f;
    k = powf(k, 1.9f);
    return (k < 0.05f) ? 0.05f : k;
}

/*
 * 道具：比怪物温和一档，不然玩家找不到箱子 / 配电箱 / 出口。
 *   3.5 格内全亮 / 13 格外剩 12%
 */
float ArtPropLight(float wx, float wy) {

    float dx = wx - posX, dy = wy - posY;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= 3.5f) return 1.0f;
    if (dist >= 13.0f) return 0.12f;
    float k = 1.0f - (dist - 3.5f) / (13.0f - 3.5f);
    if (k < 0.0f) k = 0.0f;
    k = powf(k, 1.45f);
    return (k < 0.12f) ? 0.12f : k;
}

/* ================================================================== */
/* §3 仿 3D 光照                                                       */
/* ================================================================== */

/*
 * 斜角（bevel）光照。
 * u, v 是归一化坐标（0..1，0=左/顶）。
 *
 * 原理：光从**左上方**来 ——
 *   上边缘 + 左边缘   → 亮（受光）
 *   下边缘 + 右边缘   → 暗（背光）
 * 只要左右 1~2 像素的明暗差，平面就会读成「有厚度」。
 *
 * thickness 是斜角宽度占比（0.02 = 2% 的边）。
 */
float ArtBevelK(float u, float v, float thickness, float strength) {
    float k = 1.0f;
    /* 上边缘受光 */
    if (v < thickness) {
        float t = 1.0f - v / thickness;            /* 1 = 最上 */
        k += t * strength;
    }
    /* 下边缘背光 */
    if (v > 1.0f - thickness) {
        float t = (v - (1.0f - thickness)) / thickness;
        k -= t * strength * 0.85f;
    }
    /* 左边缘受光（弱一点，主光在上方） */
    if (u < thickness) {
        float t = 1.0f - u / thickness;
        k += t * strength * 0.6f;
    }
    /* 右边缘背光 */
    if (u > 1.0f - thickness) {
        float t = (u - (1.0f - thickness)) / thickness;
        k -= t * strength * 0.5f;
    }
    if (k < 0.15f) k = 0.15f;
    if (k > 1.9f) k = 1.9f;
    return k;
}

/*
 * 内凹（recessed panel）光照 —— 和 ArtBevelK 相反：
 *   上边缘暗（凹进去的阴影）、下边缘亮（被光照到的内壁）。
 * 用来做门板内凹、屏幕凹陷这类。
 */
float ArtRecessK(float u, float v, float inset, float strength) {
    float k = 1.0f;
    if (v > inset && v < inset + 0.08f) k -= strength;            /* 内凹上壁 */
    if (v > 0.92f - inset && v < 1.0f - inset) k += strength * 0.7f; /* 内凹下壁 */
    if (u > inset && u < inset + 0.06f) k += strength * 0.35f;    /* 左侧内壁受光 */
    if (u > 0.94f - inset && u < 1.0f - inset) k -= strength * 0.5f;
    if (k < 0.2f) k = 0.2f;
    return k;
}

/* ================================================================== */
/* §4 抖动                                                             */
/* ================================================================== */

/*
 * 4×4 Bayer 有序抖动矩阵。
 * 返回 0..15。
 *
 * 为什么需要：软件的平滑渐变在低分辨率像素画里会显得「糊」，
 * 而有序抖动在同样颜色数量下能画出**分级清晰**的渐层，
 * 观感更接近手绘像素画，也更省色阶。
 */
int ArtBayer4(int x, int y) {
    static const int m[16] = {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5
    };
    int ix = x & 3;
    int iy = y & 3;
    return m[iy * 4 + ix];
}

/*
 * 抖动阈值：把连续值 t(0..1) 抖成 0/1。
 * scale 越小抖动越明显，越大越平滑。
 */
int ArtDitherOn(float t, int x, int y, float scale) {
    if (t <= 0.0f) return 0;
    if (t >= 1.0f) return 1;
    float thr = (float)ArtBayer4(x, y) / 16.0f;
    return (t * scale > thr) ? 1 : 0;
}

/* ================================================================== */
/* §5 通用绘制辅助                                                     */
/* ================================================================== */

/* 把颜色按 k 缩放并夹到 0..255，返回打包好的 0xRRGGBB */
unsigned int ArtScale(unsigned int c, float k) {
    unsigned int r = (unsigned int)(((c >> 16) & 0xFF) * k);
    unsigned int g = (unsigned int)(((c >> 8) & 0xFF) * k);
    unsigned int b = (unsigned int)((c & 0xFF) * k);
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return (r << 16) | (g << 8) | b;
}

/* ================================================================== */
/* §6 画风开关                                                         */
/* ================================================================== */

/*
 * 2026-11-11：手工布偶画风（Coraline 式）。
 *
 * 为什么做成开关而不是直接替换：
 *   · 这是一次整体色调与材质的方向改变，必须能和原版并排看（AD-29 证据分级）；
 *   · 出问题时一句 `set CANITY_STYLE=classic` 就能回到已验收的画面；
 *   · 渲染与烘焙都读同一个判定，不会出现"精灵换了、整屏没换"的半套状态。
 */
int ArtStyleDoll(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* e = getenv("CANITY_STYLE");
        if (e && (strcmp(e, "classic") == 0 || strcmp(e, "0") == 0))
            cached = 0;
        else
            cached = 1;
    }
    return cached;
}

/* ================================================================== */
/* §7 布偶画风的材质分级                                               */
/* ================================================================== */

/*
 * 为什么分级放在**材质烘焙期**而不是每帧后处理：
 *   把"去饱和 + 暖光"做进贴图与精灵，等于"这些材料本来就是这个颜色"，
 *   每帧只乘一次织纹系数即可。beta 上实测（800×600）每帧省下约 3ms。
 *   而且分级落在反照率上是物理正确的：手电的冷光仍然能照出冷暖对比。
 *
 * 两步：
 *   ① 去饱和 38%，同时把亮度往暖灰偏 —— 各主题拉回同一条基线；
 *   ② 高光偏琥珀、阴影偏冷青 —— 定格动画的灯光关系。
 */
static unsigned char g_gradeR[256], g_gradeG[256], g_gradeB[256];
static int g_gradeReady = 0;

static void BuildDollGrade(void) {
    for (int i = 0; i < 256; i++) {
        int v = i;
        int hi = v * v / 255;
        int lo = (255 - v) * (255 - v) / 255;
        int r = v + (hi * 46) / 255 - (lo * 6) / 255 + 6;
        int g = v + (hi * 18) / 255 + 2;
        int b = v - (hi * 30) / 255 + (lo * 12) / 255 - 8;
        if (r < 0) r = 0; else if (r > 255) r = 255;
        if (g < 0) g = 0; else if (g > 255) g = 255;
        if (b < 0) b = 0; else if (b > 255) b = 255;
        g_gradeR[i] = (unsigned char)r;
        g_gradeG[i] = (unsigned char)g;
        g_gradeB[i] = (unsigned char)b;
    }
    g_gradeReady = 1;
}

unsigned int ArtDollGrade(unsigned int c) {
    if (!ArtStyleDoll()) return c;          /* classic：一个字节都不动 */
    if (!g_gradeReady) BuildDollGrade();
    int r = (int)((c >> 16) & 0xFFu);
    int g = (int)((c >> 8) & 0xFFu);
    int b = (int)(c & 0xFFu);
    int lum = (r * 77 + g * 151 + b * 28) >> 8;
    r = (r * 159 + (lum + ((lum * 14) >> 8)) * 97) >> 8;
    g = (g * 159 + lum * 97) >> 8;
    b = (b * 159 + (lum - ((lum * 12) >> 8)) * 97) >> 8;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((unsigned int)g_gradeR[r] << 16) |
           ((unsigned int)g_gradeG[g] << 8) |
           (unsigned int)g_gradeB[b];
}

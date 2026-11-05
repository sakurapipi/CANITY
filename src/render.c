/*
 * Canity — 渲染层实现：像素缓冲 / 贴图与查表 / 多线程光栅 / 3D / 精灵 / 地图
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "map_rooms.h"
#include "cell_meta.h"
#include "render.h"
#include "suspicion.h"
#include "art.h"
#include "bench.h"
#include "weapons.h"
#include "extras.h"
#include "sprites.h"
#include "game.h"
#include "mimic.h"
#include "entity.h"
#include "ui.h"

int renderW = 800, renderH = 600;    /* 内部渲染分辨率 */
static const float fovPlaneLen[5] = {0.6494f, 0.7673f, 0.9163f, 1.0926f, 1.2972f};
unsigned int* pixels = NULL;
unsigned char* vignetteMask = NULL;
BITMAPINFO bmi;

unsigned int texture[TEX_HEIGHT][TEX_WIDTH];
unsigned int floorTexture[TEX_HEIGHT][TEX_WIDTH];
unsigned int ceilTexture[TEX_HEIGHT][TEX_WIDTH];

float zBuffer[MAX_SCREEN];
float flashTab[MAX_SCREEN];
int ceilTab[MAX_SCREEN];
float floorGain[MAX_SCREEN];   /* 0.52 + cone*1.30，按列预计算 */
static int floorGainI[MAX_SCREEN];   /* v1.9：同一增益的定点版（×256），地板内层免掉浮点 */
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

/* 像素缓冲直接建在 DIB Section 上：GDI 走共享内存快路径，
   呈现时用 BitBlt 而不是每次从用户内存 StretchDIBits */
HDC g_memDC = NULL;
HBITMAP g_dib = NULL;
void* g_dibBits = NULL;
/* v2.7：pixels 是 malloc 出来的（1）还是 GDI DIB 的 bits（0）。
   只有 1 的时候才能 free —— 对 DIB bits 调 free 会把 GDI 内存交给 CRT。 */
static int g_pixelsOwned = 0;
int g_useDIB = 0;

/* 按当前设置重新分配内部渲染缓冲 */
void InitPixels(void) {
    float s = renderScales[renderScaleIndex] / 100.0f;

    /* v1.9：渲染缓冲的**宽高比必须等于客户端宽高比**。
       原来固定按 winW:winH（800:600）算 → 缓冲永远是 4:3，
       拉满 16:9 窗口时横向被拉宽，所以墙面/地板看起来"太扁"。
       现在按客户区比例算，并用固定的垂直分辨率（winH*s）反推宽度。 */
    float aspect = (winH > 0) ? (float)winW / (float)winH : 4.0f / 3.0f;
    if (g_hwnd) {
        RECT cr;
        if (GetClientRect(g_hwnd, &cr) && cr.right > 0 && cr.bottom > 0)
            aspect = (float)cr.right / (float)cr.bottom;
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
        RECT cbase;
        if (g_hwnd && GetClientRect(g_hwnd, &cbase) && cbase.bottom > 0)
            baseH = (float)cbase.bottom * s;
        else
            baseH = (float)winH * s;      /* 没有窗口时才退回 winH */
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

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = renderW;
    bmi.bmiHeader.biHeight = -renderH;   /* 负数 = 自上而下 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (g_memDC) { DeleteDC(g_memDC); g_memDC = NULL; }
    if (g_dib) { DeleteObject(g_dib); g_dib = NULL; }
    g_dibBits = NULL;
    /* v2.7：pixels 的来源必须记清楚。上一轮如果是 GDI 的 DIB bits，
       它已经被 DeleteObject 一起释放了，**不能再 free()** —— 那是把 GDI 内存
       交给 CRT 分配器，堆直接坏掉。InitPixels 会被 WM_SIZE / 改渲染比例 /
       动态分辨率反复调用，只要哪次 CreateDIBSection 失败就踩中。 */
    if (g_pixelsOwned) { free(pixels); g_pixelsOwned = 0; }
    pixels = NULL;

    g_memDC = CreateCompatibleDC(NULL);
    if (g_memDC) g_dib = CreateDIBSection(g_memDC, &bmi, DIB_RGB_COLORS, &g_dibBits, NULL, 0);
    if (g_memDC && g_dib && g_dibBits) {
        SelectObject(g_memDC, g_dib);
        pixels = (unsigned int*)g_dibBits;
        g_useDIB = 1;
        g_pixelsOwned = 0;          /* 归 GDI 管，不能 free */
    } else {
        if (g_memDC) { DeleteDC(g_memDC); g_memDC = NULL; }
        if (g_dib) { DeleteObject(g_dib); g_dib = NULL; }
        g_useDIB = 0;
        pixels = (unsigned int*)malloc((size_t)renderW * renderH * sizeof(unsigned int));
        g_pixelsOwned = (pixels != NULL);
    }
    if (pixels) memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));

    GenerateVignette();
    uiDirty = 1;
}

void BuildShadeTables(void);
void GenerateThemeTextures(int id);

static unsigned char noiseTab[256];
static int noiseReady = 0;

unsigned int floorBaseA = 0x00151515u, floorBaseB = 0x00202020u;
int themeId = 0;
/* v1.12：视角上下 = 地平线偏移（y-shear，Doom 的做法）。
   范围 ±35% 屏高，单位是渲染像素。 */
float pitchRatio = 0.0f;   /* -1..1，映射到 ±0.45 rad ≈ ±26° */
int horizon = 0;           /* 兼容旧代码：真俯仰不再用它平移 */
static float camPitch = 0.0f;
static float pitchCos = 1.0f, pitchSin = 0.0f;
static int   focal = 600;  /* 焦距 = 渲染高度（与原 lineHeight 一致） */

/* v1.13 真俯仰：把相机在竖直方向真的转过去，
   墙的上下沿、精灵、地板/天花板都按旋转后的视锥重新投影。 */
void UpdatePitch(void) {
    camPitch = pitchRatio * 0.45f;
    pitchCos = cosf(camPitch);
    pitchSin = sinf(camPitch);
    focal = renderH;
    horizon = 0;
}
/* ---- 地板/天花板预乘表 ----
   地板是全屏最贵的部分（占 Render3D 的一半以上），每像素原本要做
   3 次「贴图通道 × 亮度」的乘法 + 3 次截断比较。

   关键观察：程序化生成的地板贴图**只有 4 种颜色**（复古化把颜色量化到 8 级，
   再加上 2x2 块对齐，同一主题内实际用到的颜色极少）。
   所以可以把「颜色 × 亮度」预先乘好，做成一张很小的表：

       premulFloor[颜色索引][亮度等级]  →  最终像素

   表大小 = 4 色 × 256 级 × 4 字节 = 4KB（地板）+ 4KB（天花板），
   完全落在 L1 缓存里。

   > 注意：HANDOFF §13.2 记录过「墙壁预乘成 512KB 查表反而更慢」——
   > 那是表太大导致 L2 未命中。这里表只有 4KB，结论正好相反。

   亮度等级怎么定：
   地板的 k8（shadeLut 校正后的亮度）实测只到 **250**（手电最亮处），
   所以等级用 8 级步长就够：level = k8 >> 3 -> 0..31，表只要 4*32*4 = 512 字节。
   但为了留余量（Gamma 调大时 shadeLut 输出范围不变，实际 k8 上限也不变），
   这里仍按 256 级建表（k8 >> 2，步长 4），精度足够且表才 4KB。 */
#define PREMUL_LEVELS 256          /* 亮度等级数 */
#define PREMUL_MAXCOL 16           /* 单张贴图最多用到的颜色数（实测 4） */
#define PREMUL_K8_SHIFT 2          /* k8 >> 2 -> 0..255 的等级索引 */

static unsigned int premulFloor[PREMUL_MAXCOL][PREMUL_LEVELS];
static unsigned int premulCeil[PREMUL_MAXCOL][PREMUL_LEVELS];
/* v2.0：墙面凹凸光照 —— 由贴图亮度算梯度，墙上不再是纯平面。
   每像素只多一次 byte load + 一次加法。 */
static signed char bump[TEX_HEIGHT][TEX_WIDTH];

/* v2.0：mipmap。墙/地在远处一个像素吃掉多个 texel，单点采样就是摩尔纹
   （128 贴图的细节反而变成噪点）。这里预生成 5 级，按每像素 texel 数选级。 */
#define MIP_LEVELS 5
static unsigned int mipChain[MIP_LEVELS][TEX_WIDTH * TEX_HEIGHT];
static unsigned char floorColorIdx[TEX_HEIGHT][TEX_WIDTH];  /* 贴图 -> 颜色索引 */
static unsigned char ceilColorIdx[TEX_HEIGHT][TEX_WIDTH];
static int nFloorColors = 0, nCeilColors = 0;

/* 把贴图重新索引成「颜色索引」，并建好预乘表。
   贴图本身保持不变（墙体、精灵等还在用），只是额外存一份索引。 */
/* v1.8：配色数硬上限。
   地板/天花板预乘表按颜色数分配列（PREMUL_MAXCOL），
   贴图颜色一旦超过这个数就会写越界 —— 加纹理细节时踩过（直接崩溃）。
   统计阶段就夹住，宁可少几种颜色也绝不越界。 */

static void BuildPremulTables(void) {
    unsigned int fcols[PREMUL_MAXCOL], ccols[PREMUL_MAXCOL];
    /* v2.7：从 0 起算。原来从 1 开始，导致下面第一轮 fcols[0]/ccols[0]
       读到的是未初始化的栈内容（今天恰好第一个真颜色拿索引 1 才没显现）。 */
    nFloorColors = 0;
    nCeilColors = 0;

    for (int y = 0; y < TEX_HEIGHT; y++) {
        for (int x = 0; x < TEX_WIDTH; x++) {
            unsigned int c = floorTexture[y][x] & 0xFFFFFFu;
            int fi = -1;
            for (int i = 0; i < nFloorColors; i++) if (fcols[i] == c) { fi = i; break; }
            if (fi < 0) {
                if (nFloorColors < PREMUL_MAXCOL) { fi = nFloorColors; fcols[nFloorColors++] = c; }
                else fi = PREMUL_MAXCOL - 1;      /* 颜色用超了，退回最后一个 */
            }
            floorColorIdx[y][x] = (unsigned char)fi;

            unsigned int cc = ceilTexture[y][x] & 0xFFFFFFu;
            int gi = -1;
            for (int i = 0; i < nCeilColors; i++) if (ccols[i] == cc) { gi = i; break; }
            if (gi < 0) {
                if (nCeilColors < PREMUL_MAXCOL) { gi = nCeilColors; ccols[nCeilColors++] = cc; }
                else gi = PREMUL_MAXCOL - 1;
            }
            ceilColorIdx[y][x] = (unsigned char)gi;
        }
    }

    /* 亮度等级 l -> k8 = l * 4（k8 上限 1023，256 级正好铺满） */
    for (int ci = 0; ci < nFloorColors; ci++) {
        int r = (int)((fcols[ci] >> 16) & 0xFF), g = (int)((fcols[ci] >> 8) & 0xFF), b = (int)(fcols[ci] & 0xFF);
        for (int l = 0; l < PREMUL_LEVELS; l++) {
            int k = l * 4;
            int rr = (r * k) >> 8, gg = (g * k) >> 8, bb = (b * k) >> 8;
            if (rr > 255) rr = 255;
            if (gg > 255) gg = 255;
            if (bb > 255) bb = 255;
            premulFloor[ci][l] = (unsigned int)((rr << 16) | (gg << 8) | bb);
        }
    }
    /* 天花板用的乘数 ceilTab[x] 是 0..255 量纲（(通道*ck)>>8），
       而表里统一用 k8 量纲（0..1023），所以等级 = ck（对应 k = ck*4）。 */
    for (int ci = 0; ci < nCeilColors; ci++) {
        int r = (int)((ccols[ci] >> 16) & 0xFF), g = (int)((ccols[ci] >> 8) & 0xFF), b = (int)(ccols[ci] & 0xFF);
        for (int l = 0; l < PREMUL_LEVELS; l++) {
            int k = l * 4;
            int rr = (r * k) >> 8, gg = (g * k) >> 8, bb = (b * k) >> 8;
            if (rr > 255) rr = 255;
            if (gg > 255) gg = 255;
            if (bb > 255) bb = 255;
            premulCeil[ci][l] = (unsigned int)((rr << 16) | (gg << 8) | bb);
        }
    }
}

/* v1.8：7 种关卡主题，纹理全部在**进层时**生成一次（运行时零成本）。
   0 砖墙 / 1 监狱 / 2 地狱 / 3 病房 / 4 冰窖 / 5 锈工厂 / 6 血肉洞窟
   统一规范：
     · 所有贴图按 2x2 像素块生成（复古观感）
     · 颜色量化到 5 位（每通道 8 档），供 premul 查表使用
     · 墙面/地板/天花板各有一套花纹，细节靠 hash（px*py 之类）而非随机，
       省 rand() 调用，也保证同一主题每次生成结果一致 */
static void SetThemeLight(int id);   /* v2.1 主题光色（雾/手电），定义在下面 */
static void DrawGroundShadow(int cx, int cyFloor, int halfW, float ty);   /* v2.1 落地阴影 */

void GenerateThemeTextures(int id) {
    SetThemeLight(id);      /* v2.1：连雾色和手电色一起换 */
    if (id < 0 || id >= THEME_COUNT) id = 0;
    themeId = id;

    switch (id) {
    case 1:  floorBaseA = 0x0014161au; floorBaseB = 0x001c1f24u; break;  /* 监狱 */
    case 2:  floorBaseA = 0x001a0c0cu; floorBaseB = 0x00241010u; break;  /* 地狱 */
    case 3:  floorBaseA = 0x00181c18u; floorBaseB = 0x0020261fu; break;  /* 病房 */
    case 4:  floorBaseA = 0x00141c22u; floorBaseB = 0x00243038u; break;  /* 冰窖 */
    case 5:  floorBaseA = 0x00201610u; floorBaseB = 0x002c2014u; break;  /* 锈工厂 */
    case 6:  floorBaseA = 0x00180e14u; floorBaseB = 0x00241620u; break;  /* 血肉洞窟 */
    default: floorBaseA = 0x00151515u; floorBaseB = 0x00202020u; break;  /* 砖墙 */
    }

    for (int y = 0; y < TEX_HEIGHT; y++) {
        for (int x = 0; x < TEX_WIDTH; x++) {
            int px = x & ~1, py = y & ~1;      /* 2x2 复古块 */
            int r, g, b;

            /* ---------------- 墙面 ---------------- */
            switch (id) {
            case 1: {   /* 监狱：横铁栏 + 立柱 + 铆点 + 锈迹 */
                r = 34 + (px * py) % 12; g = 38 + (x * y) % 12; b = 44 + (px * py) % 14;
                if (py % 12 == 0) { r = 20; g = 24; b = 30; }
                if (px % 24 == 0) { r = 26; g = 30; b = 36; }
                if (py % 12 == 1) { r += 14; g += 14; b += 16; }
                if (((px % 24) == 12) && ((py % 12) == 6)) { r = 78; g = 82; b = 88; }
                if (rand() % 110 == 0) { r = 62; g = 44; b = 30; }
                break; }
            case 2: {   /* 地狱：裂纹 + 余烬 */
                r = 52 + (px * py) % 24; g = 8 + (px * py) % 6; b = 6;
                if (((x + y * 3) % 37) == 0) { r = 12; g = 4; b = 4; }
                if (rand() % 70 == 0) { r = 120; g = 30; b = 8; }
                if (rand() % 150 == 0) { r = 200; g = 96; b = 24; }
                break; }
            case 3: {   /* 病房：小瓷砖 + 霉斑 + 碎瓷 */
                r = 66 + (px * py) % 10; g = 70 + (x * y) % 10; b = 64 + (px * py) % 8;
                if (py % 16 == 0 || px % 16 == 0) { r = 38; g = 42; b = 38; }
                if (rand() % 55 == 0) { r = 34; g = 46; b = 34; }
                if (rand() % 210 == 0) { r = 96; g = 98; b = 92; }
                break; }
            case 4: {   /* 冰窖：冰缝 + 霜点 + 反光 */
                r = 28 + (px * py) % 14; g = 40 + (x * y) % 16; b = 52 + (px * py) % 18;
                if (px % 32 == 0) { r = 18; g = 26; b = 36; }
                if ((x % 7) == 0 && (y % 13) == 0) { r = 96; g = 132; b = 162; }
                if (rand() % 130 == 0) { r = 150; g = 190; b = 210; }
                break; }
            case 5: {   /* 锈工厂：板缝 + 铆钉 + 锈斑 */
                r = 46 + (px * py) % 16; g = 30 + (x * y) % 12; b = 18 + (px * py) % 8;
                if (py % 10 == 0) { r = 30; g = 20; b = 12; }
                if ((px % 16 == 0) && (py % 10) != 0) { r += 14; g += 10; b += 8; }
                if ((px % 16 == 8) && (py % 10 == 5)) { r = 92; g = 72; b = 50; }
                if (rand() % 80 == 0) { r = 84; g = 48; b = 22; }
                break; }
            case 6: {   /* 血肉洞窟：筋络 + 亮筋 + 暗孔 */
                r = 44 + (px * py) % 18; g = 14 + (x * y) % 10; b = 26 + (px * py) % 12;
                if (((x * x + y * y * 2) % 53) == 0) { r = 92; g = 24; b = 42; }
                if (rand() % 110 == 0) { r = 128; g = 42; b = 64; }
                if (rand() % 160 == 0) { r = 16; g = 6; b = 10; }
                break; }
            default: {  /* 砖墙：错缝砖 + 深色砖 + 红斑 */
                r = 40 + (px * py) % 20; g = 10 + (x * y) % 10; b = 10;
                int off = ((py / 16) % 2) * 16;
                if (py % 16 == 0) { r = 10; g = 10; b = 10; }
                else if ((px + off) % 32 == 0) { r = 10; g = 10; b = 10; }
                else if (rand() % 60 == 0) { r = 96; g = 24; b = 20; }
                if (rand() % 140 == 0) { r = 150; g = 30; b = 20; }
                break; }
            }
            r = (r >> 5) << 5; g = (g >> 5) << 5; b = (b >> 5) << 5;
            if (r > 248) r = 248;
            if (g > 248) g = 248;
            if (b > 248) b = 248;
            texture[y][x] = (unsigned int)((r << 16) | (g << 8) | b);

            /* 地砖：32px 砖块 + 勾缝 + 污渍（只产生 4 种颜色，premul 表安全） */
            {
                int fr = (int)((floorBaseA >> 16) & 0xFF);
                int fg = (int)((floorBaseA >> 8) & 0xFF);
                int fb = (int)(floorBaseA & 0xFF);
                int dark = ((px / 32 + py / 32) % 2) ? 0 : -8;
                if ((px % 32) < 2 || (py % 32) < 2) { fr = 12; fg = 12; fb = 12; }
                else if (rand() % 70 == 0) { fr = fr * 6 / 10; fg = fg * 6 / 10; fb = fb * 6 / 10; }
                else { fr += dark; fg += dark; fb += dark; }
                if (fr < 0) fr = 0;
                if (fg < 0) fg = 0;
                if (fb < 0) fb = 0;
                floorTexture[y][x] = (unsigned int)((fr << 16) | (fg << 8) | fb);
            }
            /* 天花板：混凝土 + 每隔 32px 一道横梁 */
            {
                int cr = 16 + (px * py) % 8, cg = 16 + (px * py) % 8, cb = 18 + (px * py) % 8;
                if (py % 32 < 3) { cr = 9; cg = 9; cb = 10; }
                else if (rand() % 90 == 0) { cr = 26; cg = 24; cb = 22; }
                ceilTexture[y][x] = (unsigned int)((cr << 16) | (cg << 8) | cb);
            }
        }
    }

    if (!noiseReady) {
        for (int i = 0; i < 256; i++) noiseTab[i] = (unsigned char)(rand() % 40);
        noiseReady = 1;
    }
    BuildPremulTables();   /* 地板/天花板预乘表（贴图变了就要重建） */
    /* v2.0：生成 mip 链（只在换主题时跑一次） */
    for (int y = 0; y < TEX_HEIGHT; y++)
        for (int x = 0; x < TEX_WIDTH; x++)
            mipChain[0][y * TEX_WIDTH + x] = texture[y][x];
    for (int L = 1; L < MIP_LEVELS; L++) {
        int dw = TEX_WIDTH >> L, dh = TEX_HEIGHT >> L;
        int sw = dw << 1;
        for (int y = 0; y < dh; y++) {
            for (int x = 0; x < dw; x++) {
                int r = 0, g = 0, b = 0;
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        unsigned int t = mipChain[L - 1][(y * 2 + dy) * sw + (x * 2 + dx)];
                        r += (t >> 16) & 0xFF; g += (t >> 8) & 0xFF; b += t & 0xFF;
                    }
                }
                mipChain[L][y * dw + x] = (unsigned int)(((r / 4) << 16) | ((g / 4) << 8) | (b / 4));
            }
        }
    }

    /* v2.0：由贴图亮度算一遍凹凸梯度（只在换主题时跑一次） */
    for (int y = 0; y < TEX_HEIGHT; y++) {
        for (int x = 0; x < TEX_WIDTH; x++) {
            int xm = (x - 1) & (TEX_WIDTH - 1), xp = (x + 1) & (TEX_WIDTH - 1);
            int ym = (y - 1) & (TEX_HEIGHT - 1), yp = (y + 1) & (TEX_HEIGHT - 1);
            unsigned int a = texture[ym][xm], b = texture[yp][xp];
            int la = (int)(((a >> 16) & 0xFF) + ((a >> 8) & 0xFF) + (a & 0xFF)) / 3;
            int lb = (int)(((b >> 16) & 0xFF) + ((b >> 8) & 0xFF) + (b & 0xFF)) / 3;
            int d = (lb - la) / 4;
            if (d > 26) d = 26;
            if (d < -26) d = -26;
            bump[y][x] = (signed char)d;
        }
    }

    BuildShadeTables();
}

/* ------------------------------------------------------------------ */
/* 地图生成                                                            */
/* ------------------------------------------------------------------ */
/* 按 FOV 设置重建相机平面：长度 = tan(fov/2)，方向与朝向垂直
   状态效果 FX_BLIND 会把视野往最窄的一档压 —— 这是「致盲」的实现。
   不改 fovIndex 本身（那是玩家的设置项），而是算一个临时的有效 FOV，
   这样效果一结束，视野立刻回到玩家设的值。 */
void ApplyFov(void) {
    float L = fovPlaneLen[fovIndex];
    float blind = FxStrength(FX_BLIND);
    if (blind > 0.0f) {
        /* 与最窄档（66 度，fovPlaneLen[0]）线性插值 */
        float narrow = fovPlaneLen[0];
        L = narrow + (L - narrow) * (1.0f - blind);
    }
    /* v1.9：像素必须是方的。
       射线投射里 屏幕像素宽/高的比例 = (renderW/2)/L : renderH ，
       要保持 1:1 就得 L = (renderW/renderH)/2。
       原来的 L 是按 4:3 调好的，所以这里乘上「实际宽高比 / 4:3」即可 ——
       效果是宽屏上**水平视野变宽**（Hor+），而不是把画面拉扁。 */
    {
        float aspect = (renderH > 0) ? (float)renderW / (float)renderH : 4.0f / 3.0f;
        L *= aspect / (4.0f / 3.0f);
    }
    planeX = -dirY * L;
    planeY = dirX * L;
}

/* 该位置是否离已有物件太近（防止箱子/配电箱/花/怪互相叠在一起） */
/* ------------------------------------------------------------------ */
/* 3D 渲染                                                             */
/* ------------------------------------------------------------------ */

/* 把世界坐标投影成屏幕矩形（billboard） */
typedef struct { int ok; float ty; int x0, x1, y0, y1; } SpriteBox;

/* v2.2：精灵亮度统一公式。
   审计发现同一件事有 4 份拷贝（箱子 / 怪 / 偷电怪 / 光敏怪），
   衰减曲线和下限（0.10 / 0.12 / 0.14）都不一样 —— 调一处另三处不动。
   现在全部走这一个函数。 */
static float SpriteShade(float ty, float minK) {
    float k = 1.0f - ty / 16.0f;
    if (k < minK) k = minK;
    if (k > 1.0f) k = 1.0f;
    return k;
}

void DrawBakedProp(int spr, float wx, float wy, float baseZ, float halfH, float lightK);
void DrawBakedPropArt(int spr, float wx, float wy, float baseZ, float halfH, float lightK);   /* v2.1 */

/* ==================================================================
 * 2026-11-06（v2.4-rc P1）：**lift 改成 baseZ**
 * ------------------------------------------------------------------
 * 原来第 3 个参数叫 lift，作用是从**顶部**把精灵往下裁：
 *     s.y0 = yTop + unit * lift;
 * 于是它只能让物件"变矮"，不能让底边离开地面 ——
 * 想做"货架三层叠起来"根本做不到。
 *
 * baseZ 是把**底边**抬到指定高度：
 *     cyBase = cyFloor - unit * baseZ;
 * 这样同一个物件可以用 2~3 层精灵叠出真实的高度差，
 * 走动时层间有视差，才像立体的。
 * baseZ = 0 时与旧行为**逐像素相同**。
 * ================================================================ */
static SpriteBox ProjectSprite(float wx, float wy, float baseZ, float halfH) {
    SpriteBox s;
    s.ok = 0;
    float dx = wx - posX, dy = wy - posY;
    float invDet = 1.0f / (planeX * dirY - dirX * planeY);
    float tx = invDet * (dirY * dx - dirX * dy);
    float ty = invDet * (-planeY * dx + planeX * dy);
    if (ty <= 0.02f) return s;   /* v1.10：太近就丢弃的阈值放宽，避免出手瞬间消失 */

    /* v1.13 真俯仰：地面点的竖直偏移旋转后再投影 */
    float v2 = (-0.5f) * pitchCos - ty * pitchSin;
    float d2 = (-0.5f) * pitchSin + ty * pitchCos;
    if (d2 < 0.05f) return s;
    float unit = (float)focal / d2;
    int cx = (int)((renderW / 2.0f) * (1.0f + tx / ty));
    int half = (int)(unit * 0.28f);
    int cyFloor = renderH / 2 - (int)(v2 * unit);
    /* P1：底边按 baseZ 抬高（baseZ=0 时与原实现完全一致） */
    int cyBase = cyFloor - (int)(unit * baseZ);
    int hgt = (int)(unit * halfH);
    int yTop = cyBase - hgt;

    s.ty = ty;
    s.x0 = cx - half; if (s.x0 < 0) s.x0 = 0;
    s.x1 = cx + half; if (s.x1 >= renderW) s.x1 = renderW - 1;
    s.y0 = yTop; if (s.y0 < 0) s.y0 = 0;
    s.y1 = cyBase; if (s.y1 >= renderH) s.y1 = renderH - 1;
    s.ok = (s.x1 > s.x0 && s.y1 > s.y0);
    return s;
}
/* ---------------- 精灵：统一的投影 + 排序绘制 ---------------- */

/* v1.9：精灵也吃光照。
   原来箱子 / 安全区 / 配电箱 / 光球都是 PutPixelAdd 平涂，
   不看距离也不看手电 —— 远处的箱子和贴脸一样亮，很出戏。
   每个精灵绘制前用它的距离算系数写进 spriteLight，PutPixelAdd 内部统一乘。 */
float spriteLight = 1.0f;

/* v2.1：每个主题自己的雾色 / 手电色。
   原来 7 个主题只换贴图颜色，雾和光是一套死常量，
   所以"冰窖"和"血肉洞窟"只差墙纸 —— 切换时玩家感觉不到换了地方。 */
static int themeFogR = 20, themeFogG = 25, themeFogB = 33;   /* 距离雾 */
static int themeLitR = 96, themeLitG = 98, themeLitB = 96;   /* 手电加色 */
static int themeAmbR = 22, themeAmbG = 27, themeAmbB = 36;   /* 墙体环境雾 */

static void SetThemeLight(int id) {
    switch (id) {
    case 0: themeFogR=22; themeFogG=24; themeFogB=28; themeLitR=104; themeLitG=100; themeLitB=92;  break; /* BRICK    暖灰 */
    case 1: themeFogR=18; themeFogG=22; themeFogB=30; themeLitR=86;  themeLitG=92;  themeLitB=104; break; /* PRISON   冷灰 */
    case 2: themeFogR=40; themeFogG=14; themeFogB=8;  themeLitR=130; themeLitG=80;  themeLitB=60;  break; /* HELL     暖红 */
    case 3: themeFogR=20; themeFogG=28; themeFogB=36; themeLitR=96;  themeLitG=108; themeLitB=112; break; /* WARD     青白 */
    case 4: themeFogR=16; themeFogG=30; themeFogB=44; themeLitR=80;  themeLitG=120; themeLitB=140; break; /* ICE      冰蓝 */
    case 5: themeFogR=28; themeFogG=22; themeFogB=16; themeLitR=120; themeLitG=96;  themeLitB=70;  break; /* RUST     锈黄 */
    default:themeFogR=26; themeFogG=8;  themeFogB=16; themeLitR=124; themeLitG=72;  themeLitB=84;  break; /* FLESH    暗紫 */
    }
}

static float SpriteAtten(float dist) {
    int lightOn = (flashlightOn && battery > 0.0f);
    float d = lightOn ? 1.0f / (1.0f + dist * (0.20f/BenchLightRangeMul()))
                      : 1.0f / (1.0f + dist * 0.45f);
    if (d < 0.10f) d = 0.10f;      /* 贴脸的东西别全黑 */
    if (d > 1.00f) d = 1.00f;
    return d;
}

static void DrawCrateSprite(int i) {
    if (i < 0 || i >= containerCount) return;
    Container* c = &containers[i];
    /* 2026-11-05：这个槽被宝箱怪占着 —— 它由 DrawMimicSprite 自己画。
       这里再画一次就会出现「箱子上叠一个箱子」。 */
    if (c->mimicHost) return;
    if (c->searched) {
        /* 翻过：盖子掀开、里面黑 */
        DrawBakedProp(SPR_PROP_CRATE_OPEN, c->x, c->y, 0.0f, 0.34f, 0.55f);
    } else if (c->locked) {
        /* 未解锁：正常木箱 + 金锁 */
        DrawBakedProp(SPR_PROP_CRATE_LOCKED, c->x, c->y, 0.0f, 0.42f, 1.0f);
    } else {
        DrawBakedProp(SPR_PROP_CRATE, c->x, c->y, 0.0f, 0.42f, 1.0f);
    }
}

/* 2026-11-05：宝箱怪。
   伪装态 / 识破态 → 华丽箱子（呼吸起伏在 mimic.c 的 MimicTick 里推进相位）
   现形态         → 箱子站起来的那只 */
static void DrawMimicSprite(int i) {
    if (i < 0 || i >= mimicCount) return;
    Mimic* m = &mimics[i];
    if (m->state == MIM_NONE || m->state == MIM_GONE) return;

    if (m->state == MIM_ANGRY) {
        DrawBakedProp(SPR_MON_MIMIC, m->x, m->y, 0.0f, 0.52f, 1.0f);
    } else {
        /* 破绽①：呼吸。用相位算，不依赖全局时钟 —— 每只相位不同 */
        float bob = sinf(m->breathPh) * 0.012f;
        DrawBakedProp(SPR_PROP_CRATE_MIMIC, m->x, m->y + bob, 0.0f, 0.42f, 1.0f);
    }
}

/* v3.0：藏身点渲染。
   原来全项目 render.c/ui.c 对 hideSpots 零引用 —— 玩家根本看不见
   衣柜/床底/桌下，只有按下 E 之后闪 1.6 秒的一行字。
   这里复用箱子的精灵，缩小 + 压暗，和真箱子区分开。 */
/* 藏身点：垃圾桶。之前复用箱子精灵缩小压暗，玩家根本分不清是箱子还是藏身点。 */
static void DrawHideSpotSprite(int i) {
    if (i < 0 || i >= hideSpotCount) return;
    HideSpot* h = &hideSpots[i];
    if (!h->active) return;
    float k = (h->kind == 0) ? 1.00f : (h->kind == 1 ? 0.90f : 0.82f);
    DrawBakedProp(SPR_PROP_TRASH, h->x, h->y, 0.0f, 0.46f, 0.72f * k);
}
static void DrawBreakerSprite(void) {
    if (!breaker.active) return;
    float k = breakerOpen ? 1.15f : 0.95f;
    DrawBakedProp(SPR_PROP_BREAKER, breaker.x, breaker.y, 0.0f, 0.52f, k);
}

/* ------------------------------------------------------------------ */

/* v2.1：场景道具统一走烘焙精灵（原来是 relX/relY 硬编码的字符画） */
/* ===== v2.4.3 **道具的距离衰减** =====
   用户要求：道具也要「远处看不清、近处才看得清」。
   但道具比怪物**温和一档** —— 太黑会让玩家找不到箱子/配电箱。
     · 怪物：2.5 格全亮，11 格外只剩 5%
     · 道具：3.5 格全亮，13 格外剩 12%
   调参在这里改。 */
/* v2.4.3: 距离曲线已统一到 src/art.c 的 ArtPropLight() */
static float PropProximityLight(float wx, float wy) { return ArtPropLight(wx, wy); }

void DrawBakedProp(int spr, float wx, float wy, float baseZ, float halfH, float lightK) {
    SpriteBox s = ProjectSprite(wx, wy, baseZ, halfH);
    if (!s.ok) return;
    /* v3.2：贴脸时直接不画。箱子/配电箱没有碰撞，玩家可以站进模型里，
       这时 billboard 的投影会被放大到几倍屏幕、看起来像视角畸变。
       近平面裁掉比硬拉一个巨大精灵更干净；交互提示仍然保留。 */
    if (s.ty < 0.55f) return;
    int w = s.x1 - s.x0, h = s.y1 - s.y0;
    if (w < 2 || h < 2) return;
    /* v3.2：道具按**投影高度**缩放，不再用 max(w,h)。
       箱子的 halfH=0.34，但固定 half 宽是 0.56；
       用 max 会把箱子按宽度放大 1.65 倍，贴脸时尤其像一个巨大的方块。
       烘焙图的画布是正方形的，按高度画才能保持半高参数的真实含义。 */
    int size = h;

    float light = SpriteShade(s.ty, 0.12f) * PropProximityLight(wx, wy);   /* v2.4.3 距离衰减 */
    DrawGroundShadow((s.x0 + s.x1) / 2, s.y1, (s.x1 - s.x0) / 3, s.ty);

    /* ==================================================================
     * 2026-11-06：**老物件保持原尺寸**
     * ------------------------------------------------------------------
     * 上一轮我给 Sprite 加了内容包围盒，并把包围盒高度映射成 halfH ——
     * 结果箱子/垃圾桶等老物件的视觉尺寸放大了约 2.7 倍
     * （它们的艺术只占画布的 37%，而 halfH 原来是按画布给的）。
     * 用户反馈：「箱子模型过大，隐藏点模型过大」。
     *
     * 这里恢复原有语义：**halfH 是画布高度**，按正方形绘制。
     * 需要"按艺术高度"精确控制的新物件（房间家具）走 DrawBakedPropArt。
     * ================================================================ */
    {
        int x = (s.x0 + s.x1) / 2 - size / 2;
        int y = s.y1 - size;
        DrawSprite(spr, x, y, size, size, light * lightK, 1.0f, 1, s.ty);
        return;
    }
}

/* 按**内容包围盒**绘制的版本：halfH 是艺术本身的高度，
   底边对齐地面，宽度按包围盒宽高比还原。
   房间家具（货架/控制台）用它 —— 它们的体积需要精确控制。 */
void DrawBakedPropArt(int spr, float wx, float wy, float baseZ, float halfH, float lightK) {
    SpriteBox s = ProjectSprite(wx, wy, baseZ, halfH);
    if (!s.ok) return;
    /* 贴脸不画（和旧路径一致，避免巨型精灵糊屏） */
    if (s.ty < 0.55f) return;
    int boxW = s.x1 - s.x0;
    int h = s.y1 - s.y0;
    if (boxW < 2 || h < 2) return;

    float light = SpriteShade(s.ty, 0.12f) * PropProximityLight(wx, wy);
    DrawGroundShadow((s.x0 + s.x1) / 2, s.y1, boxW / 3, s.ty);

    /* ==================================================================
     * 2026-11-06：**只画艺术包围盒，别画整块画布**
     * ------------------------------------------------------------------
     * 上一版把整个 128x128 画布缩放到"包围盒高度 = halfH"，
     * 于是画布被放大了 SPR_H/bboxH 倍 —— 箱子实测 2.67 倍。
     * 那 2.67 倍里绝大部分是全透明像素，但每个像素仍要走一遍采样循环。
     * 这就是用户说的"每次路过都掉帧"。
     *
     * 现在直接用带深度测试的子矩形 blit，源矩形就是包围盒本身：
     *   · 目标高度 = 投影高度
     *   · 目标宽度 = 投影高度 × 包围盒宽高比
     *   · 画的像素数从 (2.67×size)² 降到 size² 以内，约 7 倍开销削减
     * ================================================================ */
    Sprite* sp = SpriteGet(spr);
    int bw = 0, bh = 0;
    if (sp) { bw = sp->bx1 - sp->bx0 + 1; bh = sp->by1 - sp->by0 + 1; }

    if (sp && bw > 0 && bh > 0) {
        int dw = (int)((long)h * bw / bh);
        if (dw < 1) dw = 1;
        int dx = (s.x0 + s.x1) / 2 - dw / 2;
        int dy = s.y1 - h;
        DrawSpriteSubZ(spr, sp->bx0, sp->by0, bw, bh, dx, dy, dw, h,
                       light * lightK, 1.0f, s.ty);
    } else {
        /* 包围盒无效（全透明）：退回正方形绘制 */
        int x = (s.x0 + s.x1) / 2 - h / 2;
        DrawSprite(spr, x, s.y1 - h, h, h, light * lightK, 1.0f, 1, s.ty);
    }
}
/* v2.1：怪物改用烘焙精灵                                              */
/*   旧版是一个函数服务两种怪、共享一张剪影，只换眼睛颜色 —— 7 只怪长得   */
/*   一样。新版每只怪一张 128×128 烘焙图（见 sprites.c 的 BakeMonster）。  */
/* ------------------------------------------------------------------ */
/* v2.2：朝向选择（Doom 的 R_ProjectSprite 同款思路）
   算「玩家 → 怪」的角度与怪朝向的差，落到 4 个扇区；
   侧面用水平镜像覆盖另一侧 —— 只烘 2 个朝向就能覆盖 4 个视角。 */
static int a_back_light = 0;   /* v2.2：本次绘制是否背向镜头 */
static void MonsterSpriteSelect(int base, float wx, float wy, const Monster* m,
                                int* outSpr, int* outFlip, int* outFrame) {
    float dx = wx - posX, dy = wy - posY;
    /* v3.0：**朝向判据换掉**。
       原来用「怪在屏幕上的方位角」，但默认 16:9 + FOV85 的可视半角只有 50.7°，
       永远进不了 60° 的侧面扇区 → dir 恒为 0，6 张侧面帧全是白烘的图。
       现在用「玩家→怪 的射线」与「怪自己面朝的方向」之间的夹角：
       怪正对着你 = 正面，背对着你 = 背面，侧身 = 侧面。 */
    float ang = atan2f(dy, dx);
    float rel = ang - m->facing;          /* 怪朝向与「怪→玩家」方向的差 */
    /* rel≈π 表示怪正面朝着玩家 */
    rel = rel - 3.14159265f;
    while (rel > 3.14159265f) rel -= 6.2831853f;
    while (rel < -3.14159265f) rel += 6.2831853f;
    while (rel > 3.14159265f) rel -= 6.2831853f;
    while (rel < -3.14159265f) rel += 6.2831853f;
    float a = fabsf(rel);

    int dir;      /* 0 正面 / 1 侧面 */
    int flip = 0;
    a_back_light = 0;   /* v3.0：每次进来先复位，否则上一只怪的背光压暗会漏到下一只 */
    if (a < 1.047f) {            /* ±60°：正面 */
        dir = 0;
    } else if (a < 2.094f) {     /* 60~120°：侧面 */
        dir = 1;
        flip = (rel < 0.0f) ? 1 : 0;
    } else {                     /* 背后：用侧面剪影（看不到脸），再压暗一档 */
        dir = 1;
        flip = (rel < 0.0f) ? 1 : 0;
        a_back_light = 1;
    }

    /* v3.0：走路帧用**每只怪自己的相位**（建模审查 #3）。
       静止/硬直时相位不动，所以不会再原地踏步，多只怪也不会同相。 */
    int frame = ((int)(m->walkPhase * 3.0f)) & 1;

    if (dir == 0 && a < 1.047f) a_back_light = 0;
    *outSpr = base + dir * 2 + frame;
    *outFlip = flip;
    *outFrame = frame;
}

static void DrawBakedMonster(int spr, float wx, float wy, float halfH,
                             float lightK, int zTest, float facing, float walkPhase) {
    SpriteBox s = ProjectSprite(wx, wy, 0.0f, halfH);
    if (!s.ok) return;
    /* 同理：怪贴脸时 sprite 投影会铺满屏幕，先裁掉。 */
    if (s.ty < 0.55f) return;
    int w = s.x1 - s.x0;
    int h = s.y1 - s.y0;
    if (w < 2 || h < 2) return;
    /* 投影框通常是 1:1；保持长宽比但让精灵按高度对齐地面 */
    int size = (w > h) ? w : h;
    int x = (s.x0 + s.x1) / 2 - size / 2;
    int y = s.y1 - size;

    float light = SpriteShade(s.ty, 0.12f);
    light *= lightK;

    /* 脚下阴影：画在精灵之前（否则会被自己的 z 裁掉） */
    DrawGroundShadow((s.x0 + s.x1) / 2, s.y1, (s.x1 - s.x0) / 3, s.ty);

    /* v2.2：只有三只主怪有 4 帧，其余单帧 */
    int isMain = (spr == SPR_MON_HUNTER || spr == SPR_MON_LIGHT || spr == SPR_MON_LEECH
                  || spr == SPR_MON_HUNTER2 || spr == SPR_MON_HUNTER3);
    if (isMain) {
        int useSpr, flip, frame;
        /* v3.0：朝向/相位由**调用方传进来** ——
       原来写死 &monster，追光鬼拿到的是寻声鬼的数据。 */
        {
            Monster tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.active = 1;
            tmp.x = wx; tmp.y = wy;
            tmp.facing = facing;
            tmp.walkPhase = walkPhase;
            MonsterSpriteSelect(spr, wx, wy, &tmp, &useSpr, &flip, &frame);
        }
        float dim = (a_back_light ? 0.72f : 1.0f);   /* 背向镜头时压暗 */
        if (flip) DrawSpriteFlip(useSpr, x, y, size, size, light * dim, 1.0f, zTest, s.ty);
        else      DrawSprite(useSpr, x, y, size, size, light * dim, 1.0f, zTest, s.ty);
        spr = useSpr;     /* 眼睛按实际帧的颜色走 */
    } else {
        DrawSprite(spr, x, y, size, size, light, 1.0f, zTest, s.ty);
    }

    /* v2.1：眼睛自发光。烘焙图里留了黑眼窝，这里加两个加法光点 ——
       黑暗里身体全黑，但眼睛一直亮着。 */
    {
        /* v3.0：眼睛发光色目前没有真正用上（烘焙图里已经带暖色眼窝），
           保留常量并显式标记，免得每次编译都报一条 set-but-not-used。 */
        int ec = 0x00FF4020u;
        (void)ec;
        int base = (spr >= SPR_MON_HUNTER  && spr < SPR_MON_HUNTER  + 4) ? SPR_MON_HUNTER  :
                   (spr >= SPR_MON_HUNTER2 && spr < SPR_MON_HUNTER2 + 4) ? SPR_MON_HUNTER2 :
                   (spr >= SPR_MON_HUNTER3 && spr < SPR_MON_HUNTER3 + 4) ? SPR_MON_HUNTER3 :
                   (spr >= SPR_MON_LIGHT   && spr < SPR_MON_LIGHT   + 4) ? SPR_MON_LIGHT   :
                   (spr >= SPR_MON_LEECH   && spr < SPR_MON_LEECH   + 4) ? SPR_MON_LEECH : spr;
        spr = base;
        if (spr == SPR_MON_LIGHT)  ec = 0x00C8F0FFu;
        else if (spr == SPR_MON_SHADE)  ec = 0x007040A0u;
        else if (spr == SPR_MON_SPIDER) ec = 0x00FFD040u;
        else if (spr == SPR_MON_ECHO)   ec = 0x00A0FFC0u;
        else if (spr == SPR_MON_LEECH)  ec = 0x0040FF80u;

        /* v2.2：完全删除渲染端的眼睛叠加 —— 审计指出烘焙图里的眼睛
           在 17.8%/42%/27% 三个不同高度，叠加层统一按 30% 画，
           等于每只怪都有两套位置不对的眼睛。眼睛只由烘焙图负责。 */
    }
}

/* ===== v2.4.6 **距离压迫感**（全部怪物共用的唯一入口）=====
   用户反馈：只有新怪有衰减，听声怪/追光怪还是老样子，整体不统一。
   所以抽成这个函数，所有怪物绘制都必须经过它。

   规则：离玩家越近越亮、越远越黑。
     ≤ NPROX_NEAR  → 全亮（它就在你面前）
     ≥ NPROX_FAR   → 只留 NPROX_FLOOR 的一线轮廓
     中间          → 幂曲线，近处慢降、远处急降

   为什么这样有压迫感：看不清才是最恐怖的。
   8 格外只剩黑影，你直到 4~5 格才确定那是什么 —— 那时候已经晚了。 */
/* v2.4.3: 距离曲线已统一到 src/art.c 的 ArtMonsterLight() */
static float ProximityLight(float wx, float wy) { return ArtMonsterLight(wx, wy); }

static void DrawBeastSprite(Monster* m, unsigned int eyeColor, int baseSpr) {
    if (!m->active) return;
    if (!SpritesReady()) {
        /* 烘焙失败时退回旧路径：至少不黑屏 */
        SpriteBox s = ProjectSprite(m->x, m->y, 0.0f, 0.86f);
        if (!s.ok) return;
        for (int x = s.x0; x < s.x1; x++) {
            if (x < 0 || x >= renderW) continue;
            if (s.ty >= zBuffer[x]) continue;
            for (int y = s.y0; y < s.y1; y++) {
                if (y < 0 || y >= renderH) continue;
                pixels[(size_t)y * renderW + x] = 0x00141418u;
            }
        }
        (void)eyeColor;
        return;
    }

    extern int bossLevel;
    /* 变体由调用方指定：第一只 SPR_MON_HUNTER、第二只 HUNTER2、第三只 HUNTER3。
       原来这里写死 HUNTER/LIGHT 二选一，所以三只长得一样。 */
    int spr = (baseSpr > 0) ? baseSpr
              : ((eyeColor == 0x00c8f0ffu) ? SPR_MON_LIGHT : SPR_MON_HUNTER);
    float halfH = 0.86f;
    float lightK = 1.0f;
    unsigned int tint = 0;
    float tintMix = 0.0f;

    /* v2.4.6：听声怪 / 追光怪 也走同一套距离衰减 */
    lightK *= ProximityLight(m->x, m->y);

    if (bossLevel && m == &monster) {
        halfH = 1.45f;
        tint = 0x00FF2010u;      /* Boss 泛红 */
        tintMix = 0.35f;
    }
    /* 硬直时闪白 */
    if (m->stun > 0.0f) {
        float k = m->stun / MONSTER_STUN;
        if (k > 1.0f) k = 1.0f;
        tint = 0x00FFFFFFu;
        tintMix = k * 0.75f;
    }

    if (!tintMix) {
        DrawBakedMonster(spr, m->x, m->y, halfH, lightK, 1, m->facing, m->walkPhase);
    } else {
        SpriteBox s = ProjectSprite(m->x, m->y, 0.0f, halfH);
        if (!s.ok) return;
        int size = (s.x1 - s.x0);
        int hh = s.y1 - s.y0;
        if (hh > size) size = hh;
        int x = (s.x0 + s.x1) / 2 - size / 2;
        int y = s.y1 - size;
        float light = SpriteShade(s.ty, 0.12f);
        DrawGroundShadow((s.x0 + s.x1) / 2, s.y1, (s.x1 - s.x0) / 3, s.ty);
        DrawSpriteTint(spr, x, y, size, size, light * lightK, 1.0f, 1, s.ty, tint, tintMix);
    }
}

static void DrawMonster2Sprite(void) {
    if (!monster2.active) return;
    DrawBeastSprite(&monster2, 0x00ff0000, SPR_MON_HUNTER2);
}
/* 第三只听声鬼（120 秒后出现）。配色再暗一档，
   三只同屏时玩家能从轮廓上分出"又来了一只"。 */
static void DrawMonster3Sprite(void) {
    if (!monster3.active) return;
    DrawBeastSprite(&monster3, 0x00c02050, SPR_MON_HUNTER3);
}
static void DrawMonsterSprite(void) { DrawBeastSprite(&monster, 0x00ff4020u, SPR_MON_HUNTER); }

/* v1.8 偷电怪：体型 55%，暗色，只有被手电光锥照到才可见。
   "看不见但电量在掉" 就是它的压迫感来源，所以这里宁可画得几乎融进黑里。 */
static void DrawLeechSprite(int idx) {
    if (idx < 0 || idx >= LEECH_MAX) return;
    Leech* L = &leeches[idx];
    if (!L->active) return;

    /* 可见性：
       · 平时只有被手电照到才看得见；
       · 但它正在抽你电的时候，会漏出蓝色电光 —— 这是唯一的"它在了"提示。 */
    int lit = 0;
    {
        float adx = L->x - posX, ady = L->y - posY;
        float d2a = adx * adx + ady * ady;
        if (flashlightOn && battery > 0.0f && d2a <= 64.0f) {
            float a1 = atan2f(ady, adx), a2 = atan2f(dirY, dirX);
            float df = a1 - a2;
            while (df > 3.14159f) df -= 6.28318f;
            while (df < -3.14159f) df += 6.28318f;
            if (fabsf(df) <= 0.55f && HasLOS(posX, posY, L->x, L->y)) lit = 1;
        }
    }
    if (!lit && !L->draining) return;
    float dx = L->x - posX, dy = L->y - posY;
    float d2 = dx * dx + dy * dy;
    if (d2 > 64.0f) return;
    float ang = atan2f(dy, dx), face = atan2f(dirY, dirX);
    float diff = ang - face;
    while (diff > 3.14159f) diff -= 6.28318f;
    while (diff < -3.14159f) diff += 6.28318f;
    if (fabsf(diff) > 0.55f) return;
    if (!HasLOS(posX, posY, L->x, L->y)) return;

    SpriteBox s = ProjectSprite(L->x, L->y, 0.0f, 0.47f);
    if (!s.ok) return;
    spriteLight = SpriteAtten(s.ty);
    int spanX = s.x1 - s.x0, spanY = s.y1 - s.y0;
    if (spanX <= 0 || spanY <= 0) return;
    float dyRel = 32.0f / (float)spanY;

    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        int relX = (x - s.x0) * 32 / spanX;
        float fy = 0.0f;
        unsigned int* col = pixels + (size_t)s.y0 * renderW + x;
        int wrote = 0;
        for (int y = s.y0; y < s.y1; y++, fy += dyRel, col += renderW) {
            int relY = (int)fy;
            int cx = 16;
            int w = (relY < 10) ? 5 : 7;
            if (relY > 3 && relY < 28 && relX > cx - w && relX < cx + w) {
                float sh = (1.0f - s.ty / 16.0f) * 0.55f;
                if (sh < 0.10f) sh = 0.10f;
                int r = (int)(26 * sh), g = (int)(34 * sh), b = (int)(40 * sh);
                /* 电光裂纹：偶尔亮一下，提示"它在偷电" */
                if (((relY * 7 + relX * 3) % 23) == 0) { r = 40; g = 150; b = 220; }
                *col = (unsigned int)((r << 16) | (g << 8) | b);
                wrote = 1;
            }
        }
        if (wrote && s.ty < zBuffer[x]) zBuffer[x] = s.ty;
    }
}
static void DrawLightBeastSprite(void) { DrawBeastSprite(&lightBeast, 0x00c8f0ffu, SPR_MON_LIGHT); }

/* 跳脸虫的脸：预渲染成一张 128x128 的纹理，每帧只做采样 + 缩放。
   逐像素现画的话，800x600 实测 11ms/帧 —— QTE 是实时反应，
   掉帧就等于「玩家看不清提示」，所以必须预渲染。

   纹理格式：高字节 = 颜色索引（1..6），低 24 位 = 原始 RGB。
   每帧要按「逼近程度」整体调亮度，所以查表 g_jumperShade[色][亮度档]，
   避免逐像素做浮点乘加（脸几乎占满屏幕，这是热路径上的主要开销）。 */
#define JUMPER_TEX_SIZE 128
#define JMP_COL_EDGE  1      /* 外缘 */
#define JMP_COL_SKIN  2      /* 皮肤 */
#define JMP_COL_DEEP  3      /* 皮肤深色 */
#define JMP_COL_MOUTH 4      /* 口腔 */
#define JMP_COL_TOOTH 5      /* 牙 */
#define JMP_COL_EYE   6      /* 眼睛（单独处理亮度） */
#define JMP_SHADE_STEPS 64   /* 亮度档数 */

static unsigned int g_jumperTex[JUMPER_TEX_SIZE * JUMPER_TEX_SIZE];
static unsigned int g_jumperShade[8][JMP_SHADE_STEPS];
static int g_jumperTexReady = 0;

static void BuildJumperTexture(void) {
    if (g_jumperTexReady) return;
    int S = JUMPER_TEX_SIZE;
    float c = (S - 1) * 0.5f;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float dx = ((float)x - c) / c;
            float dy = ((float)y - c) / c;
            float d2 = dx * dx + dy * dy;
            unsigned int col = 0;
            unsigned int idx = 0;
            if (d2 <= 1.0f) {
                if (d2 > 0.7396f) { col = 0x00140c08; idx = JMP_COL_EDGE; }   /* 外缘 */
                else if (d2 > 0.5184f) { col = 0x002c1a12; idx = JMP_COL_SKIN; }
                else { col = 0x003c2418; idx = JMP_COL_DEEP; }

                /* 嘴：中央的大裂口 */
                float mdy = (dy - 0.12f) * 2.3f;
                float md2 = dx * dx + mdy * mdy;
                if (md2 < 0.3025f && dy > -0.15f) {
                    if (md2 < 0.1764f) { col = 0x00000000; idx = JMP_COL_MOUTH; }  /* 口腔全黑 */
                    else { col = 0x00201010; idx = JMP_COL_MOUTH; }
                    /* 牙：上下两排锯齿 */
                    if (md2 > 0.16f) {
                        int toothRow = (int)((dy + 0.15f) * 40.0f);
                        if ((toothRow & 1) == 0) { col = 0x00d8d0b0; idx = JMP_COL_TOOTH; }
                    }
                }

                /* 眼睛：两个发光的点 */
                float edy = (dy + 0.42f) * 1.5f;
                float edy2 = edy * edy;
                float ex1 = dx + 0.34f, ex2 = dx - 0.34f;
                float e1sq = ex1 * ex1 + edy2;
                float e2sq = ex2 * ex2 + edy2;
                if (e1sq < 0.0169f || e2sq < 0.0169f) { col = 0x00ff0000u; idx = JMP_COL_EYE; }
            }
            g_jumperTex[(size_t)y * S + x] = (idx << 24) | (col & 0x00FFFFFFu);
        }
    }

    /* 亮度查表：色 × 亮度档。
       亮度因子 = 0.45 + 0.55*t，t 在 0..1，分成 64 档。 */
    {
        static const unsigned int base[8] = {
            0,
            0x00140c08,   /* EDGE */
            0x002c1a12,   /* SKIN */
            0x003c2418,   /* DEEP */
            0x00000000,   /* MOUTH（黑） */
            0x00d8d0b0,   /* TOOTH */
            0x00ff0000,   /* EYE（用不到，这里只是占位） */
            0
        };
        for (int ci = 0; ci < 8; ci++) {
            for (int s = 0; s < JMP_SHADE_STEPS; s++) {
                float t = (float)s / (float)(JMP_SHADE_STEPS - 1);
                float k = 0.45f + 0.55f * t;
                unsigned int b = base[ci];
                int r = (int)(((b >> 16) & 0xFF) * k);
                int g = (int)(((b >> 8) & 0xFF) * k);
                int bl = (int)((b & 0xFF) * k);
                g_jumperShade[ci][s] = (unsigned int)((r << 16) | (g << 8) | bl);
            }
        }
    }
    g_jumperTexReady = 1;
}

/* 跳脸虫：它不是世界里的一个 billboard，而是**扑向镜头的东西**。
   所以用屏幕空间直接绘制：以画面中心为基准，尺寸随逼近程度放大，
   阶段二（跿上脸）时几乎占满整个屏幕 —— 这是它最吓人的瞬间。 */
void DrawParasiteSprite(void) {
    if (!parasite.active) return;
    BuildJumperTexture();

    /* 逼近程度 0..1：ramp 在阶段一被用作进度，阶段二恒为 1 */
    float t = parasite.ramp;
    if (jumperPhase == JMP_DODGE || jumperPhase == JMP_QTE) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    /* 亮度查表的下标（每帧算一次，不是每像素） */
    int shadeIdx = (int)(t * (float)(JMP_SHADE_STEPS - 1) + 0.5f);
    if (shadeIdx < 0) shadeIdx = 0;
    if (shadeIdx >= JMP_SHADE_STEPS) shadeIdx = JMP_SHADE_STEPS - 1;
    int eyeG = 90 + (int)(165.0f * t);
    if (eyeG > 255) eyeG = 255;

    /* 尺寸：阶段一从小到大，阶段二几乎铺满屏幕 */
    float sizeF;
    if (jumperPhase == JMP_QTE) sizeF = 1.15f;
    else if (jumperPhase == JMP_DODGE) sizeF = 0.55f + 0.35f * t;
    else sizeF = 0.18f + 0.37f * t;

    int half = (int)((float)(renderH < renderW ? renderH : renderW) * 0.42f * sizeF);
    if (half < 4) half = 4;
    if (half * 2 >= renderH) half = renderH / 2 - 1;

    int cx = renderW / 2;
    int cy = renderH / 2;

    /* 抖动：逼近时疯狂抖动，制造不安感 */
    float jitterAmp = (jumperPhase == JMP_QTE) ? 7.0f : (jumperPhase == JMP_DODGE ? 4.0f : 1.5f);
    float jx = sinf((float)g_timeSec * 47.0f) * jitterAmp;
    float jy = sinf((float)g_timeSec * 39.0f) * jitterAmp;

    /* 呼吸：整体缓慢缩放，像在喘气 */
    float breathe = 1.0f + sinf((float)g_timeSec * 12.0f) * 0.05f * (0.4f + t);
    int h = (int)(half * breathe);
    if (h < 3) h = 3;

    /* ------------------------------------------------------------ */
    /* 关键：脸是**预渲染**的纹理，不是逐像素算出来的。            */
    /* 逐像素画一张脸要 15 次浮点乘加，800x600 下实测 11ms/帧 —— */
    /* QTE 是实时反应，掉帧就等于「玩家看不见提示」。              */
    /* 改成：纹理只画一次（64x64），每帧只是采样 + 缩放。          */
    /* ------------------------------------------------------------ */
    const unsigned int* src = g_jumperTex;
    int sw = JUMPER_TEX_SIZE;

    /* 纹理坐标范围：从左上角覆盖到右下角。
       注意是**双向缩放**：源纹理 128x128 要放大成 (2h+1) 见方，
       所以必须遍历**目标**像素、反查源纹理。
       早先写成「遍历源像素、每源像素只画 1 个目标点」，
       画面只剩 1/81 的内容 —— 性能指标很漂亮，画面全黑。
       **这就是「光看性能提升不够」的又一个例子。**

       性能：逐像素做 (dx*sw)/side 整数除法，578 见方就是 33 万次除法，
       实测 4.3ms —— 除法是瓶颈。改成：
         1) 定点数增量代替除法（sx16 每步加固定值，右移取整）
         2) 按圆形轮廓裁剪每行的 x 范围（圆外整段跳过） */
    int side = 2 * h + 1;
    int x0 = cx - h;
    int y0 = cy - h;
    /* 抖动：整块纹理偏移，不逐像素算 */
    x0 += (int)jx;
    y0 += (int)jy;

    /* 定点步长：每前进一个目标像素，源纹理坐标前进 (sw/side) */
    int step16 = (sw << 16) / side;
    if (step16 < 1) step16 = 1;

    /* 纹理空间的圆形轮廓参数 */
    float tc = (sw - 1) * 0.5f;
    float tr2 = tc * tc;

    for (int dy = 0; dy < side; dy++) {
        int py = y0 + dy;
        if (py < 0 || py >= renderH) continue;

        /* 反查源纹理行 */
        int sy = (dy * sw) / side;
        const unsigned int* srow = src + (size_t)sy * sw;

        /* 按圆裁剪这一行的 x 范围：
           源行相对圆心的竖直偏移 -> 允许的半宽 -> 换算成目标 x 范围 */
        float dyS = (float)sy - tc;
        float rem = tr2 - dyS * dyS;
        int dxLo = 0, dxHi = side - 1;
        if (rem > 0.0f) {
            float halfW = sqrtf(rem);
            float sxLo = tc - halfW, sxHi = tc + halfW;
            dxLo = (int)(sxLo * (float)side / (float)sw);
            dxHi = (int)(sxHi * (float)side / (float)sw);
            if (dxLo < 0) dxLo = 0;
            if (dxHi > side - 1) dxHi = side - 1;
        }

        unsigned int* row = pixels + (size_t)py * renderW;
        /* 定点源坐标：从 dxLo 对应的源位置开始 */
        int sx16 = (dxLo * sw / side) << 16;

        for (int dx = dxLo; dx <= dxHi; dx++) {
            int px = x0 + dx;
            if (px < 0 || px >= renderW) { sx16 += step16; continue; }
            unsigned int c = srow[sx16 >> 16];
            sx16 += step16;
            unsigned int ci = c >> 24;
            if (!ci) continue;                  /* 透明 */

            /* 眼睛要随逼近程度变亮：单独处理 */
            if (ci == JMP_COL_EYE) {
                row[px] = (unsigned int)(eyeG << 16);   /* 血红的眼睛 */
                continue;
            }
            /* 其余颜色走亮度查表 */
            row[px] = g_jumperShade[ci][shadeIdx];
        }
    }
}

/* 荧光棒：飞行中/落地发光，按 z 投影 */
/* ---------------- v1.11：统一弹道精灵（荧光棒 / 玻璃瓶） ----------------
   重写原因：老实现是逐列比 zBuffer，投射物贴墙飞时列与列互相打架，
   看上去就是"时不时消失一下"。这里把规则收敛成三条：

     ① 遮挡**只判中心一列**（带 0.85 深度偏移）→ 要么整颗可见，要么整颗被挡
     ② 球体内部不再做深度测试 → 没有逐像素打架
     ③ 半径下限 3px、上限 90px → 贴脸不会缩成点，远处不会糊满屏
*/
/* v1.11：光球是真实光源。
   墙面按列的命中点算「离光球多远」，越近越亮并带暖色 ——
   这样扔出去的荧光棒会把周围照亮，而不是自己亮、环境不动。 */
extern Thrown thrown[MAX_THROWN];

static float FlareLightAt(float wx, float wy) {
    float best = 0.0f;
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        float dx = thrown[i].x - wx, dy = thrown[i].y - wy;
        float d2 = dx * dx + dy * dy;
        const float R = 4.6f;
        if (d2 > R * R) continue;
        float f = 1.0f - sqrtf(d2) / R;
        f = f * f * 0.95f;
        /* v3.3 **亮度随剩余寿命衰减** ——
           原来 25 秒内亮度恒定，然后「啪」一下灭掉，玩家没有任何预兆。
           现在最后 8 秒线性变暗到 25%，熄灭前会明显发暗，
           「该找下一个光源了」变成可感知的信息。 */
        float life = thrown[i].life;
        float dim = 1.0f;
        if (life < 8.0f) {
            dim = 0.25f + 0.75f * (life / 8.0f);
            if (dim < 0.25f) dim = 0.25f;
        }
        f *= dim;
        if (f > best) best = f;
    }
    return best;
}

/* v2.2：把一次相机投影抽成函数（原来有三份拷贝，后两份没算 pitch）。
   ProjectSprite 用 pitchCos/pitchSin，另外两处直接用 renderH/2，
   抬头/低头时就会和墙错开。这里统一。 */
static int ProjectPoint(float wx, float wy, float z,
                        int* outX, int* outY, float* outUnit, float* outTy) {
    float dx = wx - posX, dy = wy - posY;
    float det = planeX * dirY - dirX * planeY;
    if (det == 0.0f) return 0;
    float invDet = 1.0f / det;
    float tx = invDet * (dirY * dx - dirX * dy);
    float ty = invDet * (-planeY * dx + planeX * dy);
    if (ty <= 0.05f) return 0;
    float unit = (float)renderH / ty;
    *outX = (int)((renderW / 2.0f) * (1.0f + tx / ty));
    /* 地平线按 pitch 偏移（和 ProjectSprite 一致） */
    float hz = (float)renderH * 0.5f + (float)horizon;
    *outY = (int)(hz + unit * (0.5f - z));
    *outUnit = unit;
    *outTy = ty;
    return 1;
}

static void DrawOrbSprite(float wx, float wy, float z, float worldR,
                          unsigned int coreCol, unsigned int haloCol,
                          float alphaMul) {
    /* v2.1：光球是自发光体，不能继承上一张精灵的距离衰减。
       spriteLight 原来只在出口光柱那里被显式置 1，其余自发光精灵
       会沿用"上一个画完的精灵"的衰减 —— 荧光棒的亮度取决于旁边的箱子有多近。 */
    spriteLight = 1.0f;
    int cx, cy, cyFloor;
    float unit, ty;
    {
        float hz = (float)renderH * 0.5f + (float)horizon;
        float dx0 = wx - posX, dy0 = wy - posY;
        float det = planeX * dirY - dirX * planeY;
        if (det == 0.0f) return;
        float invDet = 1.0f / det;
        float tx = invDet * (dirY * dx0 - dirX * dy0);
        ty = invDet * (-planeY * dx0 + planeX * dy0);
        if (ty <= 0.05f) return;
        unit = (float)renderH / ty;
        cx = (int)((renderW / 2.0f) * (1.0f + tx / ty));
        cyFloor = (int)(hz + unit * 0.5f);
        cy = (int)(hz + unit * (0.5f - z));
    }

    int r = (int)(unit * worldR);
    if (r < 3) r = 3;
    if (r > 90) r = 90;
    if (cx + r < 0 || cx - r >= renderW || cy + r < 0 || cy - r >= renderH) return;

    /* ① 遮挡只判一次 */
    int ocx = cx < 0 ? 0 : (cx >= renderW ? renderW - 1 : cx);
    /* v2.0：去掉 0.85 偏置 —— 它让墙后 18% 距离内的光源仍然可见 */
    if (ty >= zBuffer[ocx]) return;

    /* ② 地面阴影：飞得越高越小越淡 */
    if (z > 0.02f) {
        float k = 1.0f / (1.0f + z * 1.6f);
        int sr = (int)(r * 0.75f * k);
        if (sr < 2) sr = 2;
        for (int sy = -sr; sy <= sr; sy++) {
            for (int sx = -sr; sx <= sr; sx++) {
                if (sx * sx + (sy * 2) * (sy * 2) > sr * sr) continue;
                int px = cx + sx, py = cyFloor + sy;
                if (px < 0 || px >= renderW || py < 0 || py >= renderH) continue;
                if (ty >= zBuffer[px]) continue;    /* v2.0：阴影不再画到墙后 */
                PutPixelBlend(px, py, 0x00000000u, 0.32f);
            }
        }
    }

    /* ③ 大范围柔光晕：让地面/墙面看起来被照亮 */
    {
        int gr = r * 3;
        if (gr > 120) gr = 120;
        float flick0 = 0.88f + 0.12f * sinf((float)g_timeSec * 21.0f);
        for (int yy = -gr; yy <= gr; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            for (int xx = -gr; xx <= gr; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                float d2 = (float)(xx * xx + yy * yy) / (float)(gr * gr);
                if (d2 >= 1.0f) continue;
                /* v2.0：光晕逐像素查深度 —— 半径最大 120px，原来整团能穿进隔壁房间 */
                if (ty >= zBuffer[px]) continue;
                float a = 1.0f - d2;
                PutPixelAdd(px, py, haloCol, a * a * 0.10f * flick0);
            }
        }
    }

    /* ② 球体：核心白 + 外圈色，加色混合 */
    float flick = 0.88f + 0.12f * sinf((float)g_timeSec * 21.0f);
    for (int yy = -r; yy <= r; yy++) {
        int py = cy + yy;
        if (py < 0 || py >= renderH) continue;
        for (int xx = -r; xx <= r; xx++) {
            int px = cx + xx;
            if (px < 0 || px >= renderW) continue;
            float d2 = (float)(xx * xx + yy * yy) / (float)(r * r);
            if (d2 >= 1.0f) continue;
            if (ty >= zBuffer[px]) continue;    /* v2.0：球体本身也要挡 */
            float a = 1.0f - d2;
            PutPixelAdd(px, py, haloCol, a * a * alphaMul * flick);
            if (d2 < 0.30f) PutPixelAdd(px, py, coreCol, (0.30f - d2) * 2.6f * alphaMul * flick);
        }
    }
}

/* 荧光棒（落地发光体也用同一套，只是 z=0、半径略大） */
static void DrawFlareSprite(int idx) {
    if (!thrown[idx].active) return;
    /* v3.3：最后 8 秒精灵也跟着变暗，和环境亮度同步 */
    float a = 1.0f;
    if (thrown[idx].life < 8.0f) {
        a = 0.30f + 0.70f * (thrown[idx].life / 8.0f);
        if (a < 0.30f) a = 0.30f;
    }
    float size = 0.17f * (0.75f + 0.25f * a);
    DrawOrbSprite(thrown[idx].x, thrown[idx].y, thrown[idx].z,
                  size, 0x00fff8e0u, 0x00ff9050u, a);
}

/* ---------------- v1.19 投掷物建模 ----------------
   砖头 = 横放的矩形块；酸瓶 = 细长瓶身 + 瓶颈；烟雾弹 = 罐体；
   玻璃瓶沿用小球（它本来就是飞行的碎玻璃反光）。 */
/* ===== v3.3 玩家诱饵「玩偶」的模型 =====
   用户要求建模。设计意图：
     · 看得出是个人偶（有头有身子有腿），不是又一个道具罐
     · 未激活时整体暗哑 —— 一眼能看出「还没生效」
     · 激活后一只眼睛亮起 + 随叫声做缩放脉冲 —— 一眼能看出「它在工作」
   全部用 PutPixelBlend 现场画，沿用项目零外部资源的原则。 */
/* ===== v7 新怪物建模 =====
   风格延续听声怪 / 追光怪：
     · 同一套 ProjectSprite 投影 + zBuffer 遮挡
     · 同一套 DrawGroundShadow 地面阴影
     · 同一套 SpriteShade 距离衰减
     · 眼白高光 = 怪物的「可识别点」
   区别只在轮廓和配色，让玩家一眼能分出「这是哪一只」。

   三只怪各有一个视觉识别点：
     拾荒者  矮胖 + 金色；身上有赃款时整体变亮
     清道夫  高瘦 + 病绿；正在开柜门时眼白变红
     回声    极矮 + 冷白微光；随时间衰减变暗
*/
/* ===== v2.4.0 新怪物建模 —— 直接复用成功的烘焙精灵 =====
   用户明确评价：听声怪 / 追光怪的模型是最好的，新做的三个「长得都很奇怪」。
   所以放弃自己用椭圆拼身体的方案，改成**用同一套烘焙精灵 + 不同色调**：
     · 主体剪影、走路帧、朝向帧、阴影、距离光衰 —— 和那两只完全一致
     · 只在颜色上区分：拾荒者暗金 / 清道夫病绿 / 守门人暗蓝
   这样质感立刻对齐，而且不用新画任何精灵。 */
static void DrawTintedBeast(int spr, float wx, float wy, float halfH,
                            unsigned int tint, float tintMix) {
    if (!SpritesReady()) {
        /* 烘焙失败时的兜底：至少画个剪影，不黑屏 */
        SpriteBox sb = ProjectSprite(wx, wy, 0.0f, halfH);
        if (!sb.ok) return;
        for (int x = sb.x0; x < sb.x1; x++) {
            if (x < 0 || x >= renderW) continue;
            if (sb.ty >= zBuffer[x]) continue;
            for (int y = sb.y0; y < sb.y1; y++) {
                if (y < 0 || y >= renderH) continue;
                pixels[(size_t)y * renderW + x] = 0x00141418u;
            }
        }
        return;
    }

    SpriteBox s = ProjectSprite(wx, wy, 0.0f, halfH);
    if (!s.ok) return;

    int size = (s.x1 - s.x0);
    int hh = s.y1 - s.y0;
    if (hh > size) size = hh;
    if (size < 2) return;

    int x = (s.x0 + s.x1) / 2 - size / 2;
    int y = s.y1 - size;

    /* v2.4.6：统一走 ProximityLight()，和新怪完全一致 */
    {
        float light = SpriteShade(s.ty, 0.12f) * ProximityLight(wx, wy);
        DrawGroundShadow((s.x0 + s.x1) / 2, s.y1, (s.x1 - s.x0) / 3, s.ty);
        DrawSpriteTint(spr, x, y, size, size, light, 1.0f, 1, s.ty, tint, tintMix);
    }
}

static void DrawNewMonsters(void) {
    /* ---- 拾荒者：**只换麻袋颜色**，身体保持原样 ---- */
    {
        extern int   CollectorActive(void), CollectorHasMoney(void);
        extern float CollectorX(void), CollectorY(void);
        if (CollectorActive()) {
            /* v2.4.66：整体染色会把明暗关系洗掉，观感廉价。
               现在只把**麻袋**（烘焙时画的洋红 key）换成白/金，
               身体的三层暗灰完全不动。 */
            if (CollectorHasMoney()) SpriteAccentSet(0x00FFC040u);   /* 金：装满赃款 */
            else                     SpriteAccentSet(0x00E8F0FFu);   /* 白：空袋子 */
            DrawTintedBeast(SPR_MON_LEECH, CollectorX(), CollectorY(), 0.86f, 0, 0.0f);
            SpriteAccentClear();
        }
    }
    /* ---- 清道夫：高瘦 + 病绿；开柜门时眼白转红 ---- */
    {
        extern int   ScavengerActive(void), ScavengerRifling(void);
        extern float ScavengerX(void), ScavengerY(void), ScavengerWarnT(void);
        if (ScavengerActive()) {
            /* v2.4.7：撬柜门 **或** 正在翻箱子，两种都算警戒 */
            int grabbing = (ScavengerWarnT() > 0.0f) || ScavengerRifling();
            /* 清道夫：用追光怪的身体（更高瘦） + 病绿色调 */
            DrawTintedBeast(SPR_MON_SHADE, ScavengerX(), ScavengerY(),
                            1.00f, 0x00FF4030u, grabbing ? 0.28f : 0.00f);   /* v2.4.2 方案A：原色；撬柜门才加轻红调 */
        }
    }
    /* ---- 守门人：**两个烘焙变体，眼睛颜色直接烘死** ---- */
    {
        extern int   WardenActive(void);
        extern int   WardenChasing(void);
        extern float WardenX(void), WardenY(void);
        if (WardenActive()) {
            /* v2.4.81 **不再用洋红 key 换色**。
               调研结论：颜色变化用调色盘是对的，但**前提是 key 色绝对不能漏到屏幕上**。
               我在这条上翻车三次（品红眼睛、边缘杂色、绿色残留）。
               守门人只有两种状态，直接烘两个变体最稳：
                 SPR_MON_SPIDER   = 冷青眼（巡逻 / 堵门 / 失明）
                 SPR_MON_LEECH_W  = 血红眼（追击）
               一个 SpriteAccent 都不调，也就没有 key 泄漏的可能。 */
            int spr = WardenChasing() ? SPR_MON_LEECH_W : SPR_MON_SPIDER;
            DrawTintedBeast(spr, WardenX(), WardenY(), 0.94f, 0, 0.0f);
        }
    }
    /* ---- 回声：极矮 + 冷白微光，强度越弱越暗 ---- */
    {
        extern SuspicionPoint susp[SUSP_MAX];
        for (int i = 0; i < SUSP_MAX; i++) {
            if (susp[i].strength <= 0.0f) continue;
            if (susp[i].source != SUSP_THROW) continue;
            float a = susp[i].strength / 60.0f;   /* 刚扔出=1，衰减到 0 */
            if (a > 1.0f) a = 1.0f;
            if (a < 0.15f) a = 0.15f;
            /* 回响：小号的追光怪精灵 + 冷白偏青的色调，半透明感 */
            /* v2.4.2：回声用自己烘焙的模型（同心环 + 尖角核心），
               并让尺寸随剩余强度做一次「呼吸」，看起来在持续广播。 */
            DrawTintedBeast(SPR_MON_ECHO, susp[i].x, susp[i].y,
                            0.26f + 0.18f * a, 0x00FFFFFFu, 0.0f);
        }
    }
}

/* ===== v7 钱袋 =====
   用最小的箱子道具当模型，但外面套一圈金色脉冲，
   肉眼一眼能认出「那是钱」。金额越大脉冲越亮。 */
static void DrawMoneyBagModel(int idx) {
    extern float MoneyBagX(int), MoneyBagY(int), MoneyBagLife(int);
    extern int MoneyBagAmount(int);
    if (MoneyBagAmount(idx) <= 0) return;
    float wx = MoneyBagX(idx), wy = MoneyBagY(idx);
    float lf = MoneyBagLife(idx);
    /* 快过期时闪烁，提醒玩家去捡 */
    float blink = 1.0f;
    if (lf < 8.0f) blink = 0.55f + 0.45f * (float)((int)(lf * 6.0f) % 2);
    DrawBakedProp(SPR_PROP_CRATE, wx, wy, 0.0f, 0.22f, 0.85f * blink);
}

/* ==================================================================
 * 2026-11-05 重做 + 打磨：**世界里放着的假人玩偶**
 * ------------------------------------------------------------------
 * 用户对照图：手持精灵（左下角）已经认可，地上这个原来还是
 * "圆头 + 方身体 + 中间深色方块 + 两根牙签腿"，读起来像纸箱。
 *
 * 第二轮反馈：「再打磨细节，比如两条手臂、身下/身体上那个小黑点、两条腿」。
 *
 * ==================================================================
 * 调研：C 里做程序化像素建模，能加细节的六种手段（本文件全用了）
 * ------------------------------------------------------------------
 * ① **有序织纹**（weave）：按 2×2 棋盘给每个像素乘 0.90 的暗系数。
 *    麻布的"糙"来自规则的小尺度明暗交替；比随机噪点干净，缩小时
 *    会自然混成均匀亮度，不会闪。
 *
 * ② **多段明暗**（banding）：把一个平面切成 5 段而不是 3 段。
 *    段数 = 圆柱感的来源；3 段看起来像贴纸，5 段才有体积。
 *
 * ③ **环境光遮蔽**（AO）：两个部件相接的那一行**额外压暗**。
 *    这是让「拼起来的几何」读成「一个整体」的关键一步 ——
 *    关节处不暗，各部件就像浮在空中。
 *
 * ④ **负空间**（negative space）：手臂和身体之间留 1px 背景缝。
 *    不靠颜色区分部件，靠**看得见后面**区分（铁律二）。
 *
 * ⑤ **缝线虚线**（dash）：`x += 2` 的短划线，比实线更像手缝。
 *    实线读作"印刷的边框"，虚线才读作"针脚"。
 *
 * ⑥ **边缘光**（rim light）：左上轮廓提亮 1px。
 *    暗场里它负责把剪影从背景里"剥"出来。
 * ================================================================ */

/* 带边界检查的绘制小工具（PutPixelBlend 本身不查边界） */
static void DcyPut(int x, int y, unsigned int c, float a) {
    if (x < 0 || x >= renderW || y < 0 || y >= renderH) return;
    PutPixelBlend(x, y, c, a);
}
/* 麻布织纹：2×2 棋盘，每格第二半稍暗。weave=0 时退化成普通点 */
static void DcyCloth(int x, int y, unsigned int c, float a, int weave) {
    if (x < 0 || x >= renderW || y < 0 || y >= renderH) return;
    if (weave && (((x >> 1) + (y >> 1)) & 1)) a *= 0.90f;
    PutPixelBlend(x, y, c, a);
}
static void DcyBar(int x0, int x1, int y, unsigned int c, float a) {
    if (y < 0 || y >= renderH) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; x++) DcyPut(x, y, c, a);
}
static void DcyVBar(int x, int y0, int y1, unsigned int c, float a) {
    if (x < 0 || x >= renderW) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) DcyPut(x, y, c, a);
}
/* 实心矩形（补丁用） */
static void DcyRect(int x0, int y0, int x1, int y1, unsigned int c, float a) {
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) DcyBar(x0, x1, y, c, a);
}
/* 虚线边框（针脚） */
static void DcyDashRect(int x0, int y0, int x1, int y1, unsigned int c, float a) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int x = x0; x <= x1; x += 2) { DcyPut(x, y0, c, a); DcyPut(x, y1, c, a); }
    for (int y = y0; y <= y1; y += 2) { DcyPut(x0, y, c, a); DcyPut(x1, y, c, a); }
}

static void DrawDecoyModel(int idx) {
    extern Decoy decoysArr[MAX_DECOY];
    if (idx < 0 || idx >= MAX_DECOY) return;
    if (!decoysArr[idx].active) return;

    Decoy* d = &decoysArr[idx];

    /* 叫声脉冲：shoutT 刚重置时最大，之后衰减 —— 视觉上「它刚叫过」 */
    float pulse = 1.0f;
    if (d->armed) {
        float p = d->shoutT / DECOY_SHOUT_EVERY;      /* 1 → 0 */
        if (p > 1.0f) p = 1.0f;
        if (p > 0.82f) pulse = 1.0f + (p - 0.82f) / 0.18f * 0.22f;   /* 最高 +22% */
    }

    SpriteBox sb = ProjectSprite(d->x, d->y, 0.0f, 0.55f * pulse);
    if (!sb.ok) return;
    int cx = (sb.x0 + sb.x1) / 2;
    int baseY = sb.y1;                 /* 脚底 */
    int h = (sb.y1 - sb.y0);           /* 整体高度 */
    if (h < 8) return;                 /* 太远就别画，免得糊成一个点 */

    /* ==================================================================
     * **两个形态**（2026-11-05 用户要求）
     * ----------------------------------------------------------------
     * 反馈：「两个形态的差异度不大，就是有没有光的区别。」
     * 旧版确实只改了亮度和一点点颜色 —— 剪影一模一样，
     * 玩家在暗场里根本分不出它到底在不在叫。
     *
     * 现在给两个**几何上不同**的形态（铁律二／铁律三十：状态变体要能认出
     * 是同一个角色，但要一眼看出状态不同）：
     *
     *   形态 A「静默」（刚落地，还没开始叫）
     *     · 身体被压扁：bodyH 40%（不是 44%）→ 像一袋软布
     *     · 肩膀收窄：wt -1
     *     · **头低垂**：headDrop 往下沉 h/16，几乎埋进肩膀
     *     · 稻草**垂着**（重力）
     *     · 嘴是缝起来的十字
     *     · 两眼全暗
     *
     *   形态 B「鸣叫」（已激活，正在引怪）
     *     · 身体挺直：bodyH 46%，肩膀 +1（胸口撑开）
     *     · **头抬起来**：headDrop = 0，脖子完全露出来
     *     · 稻草**炸开**（在震动）
     *     · 嘴**张开**：深色菱形口 + 锯齿边，里面是暗红
     *     · 单眼琥珀亮起 + 光晕
     *     · **两圈声音波纹**从嘴部向外扩散
     *
     * 判读顺序（缩小到 20px 也成立）：
     *   ① 头是不是埋在肩膀里  ② 嘴是缝的还是张的  ③ 有没有波纹
     * ================================================================ */
    const int armed = d->armed ? 1 : 0;

    /* 纵向：静默态身体被压扁，头往下沉 */
    int legH   = h * 28 / 100;
    int bodyH  = h * (armed ? 46 : 40) / 100;
    int neckH  = h * (armed ? 6 : 8) / 100;
    int headD  = h - legH - bodyH - neckH;
    if (headD < 3) headD = 3;
    int headR  = headD / 2;
    if (headR < 2) headR = 2;

    /* 头低垂：静默态往下沉，几乎埋进肩膀 —— 最直观的形态差 */
    int headDrop = armed ? 0 : (h / 16);

    /* 横向：鸣叫时胸口撑开，静默时塌下去 */
    int wt = h / 11 + (armed ? 1 : 0);  if (wt < 2) wt = 2;
    int wb = h / 8;                     if (wb <= wt) wb = wt + 1;

    /* ---- 尺寸门槛：够大才开高成本细节 ---- */
    const int weave = (h >= 34);        /* 织纹：太小会变噪点 */
    const int fine  = (h >= 48);        /* 精细件：膝盖/脚/补丁 */

    /* ---- 调色板：和 sprites.c 的 SPR_ITEM_DECOY 对齐 ---- */
    float dim  = d->armed ? 1.0f : 0.78f;
    const unsigned int cEdge = 0x00161A1Cu;
    const unsigned int cLit  = d->armed ? 0x00B4A88Cu : 0x00887E68u;
    const unsigned int cMid  = d->armed ? 0x00938A72u : 0x006E6656u;
    const unsigned int cShd  = d->armed ? 0x00625C4Eu : 0x00463F36u;
    const unsigned int cDeep = d->armed ? 0x00403A30u : 0x002C2720u;  /* 5 段的第 5 档 */
    const unsigned int cSeam = 0x00302820u;
    const unsigned int cRope = 0x00806848u;
    const unsigned int cRopeL= 0x00B09060u;
    const unsigned int cStraw= 0x00C8A040u;
    const unsigned int cHead = d->armed ? 0x00C0B498u : 0x008A8070u;

    /* ================================================================
     * 5 段明暗（手段②）：把一个平面从左到右切成 5 档。
     * 3 档像贴纸，5 档才有圆柱的体积。
     * ============================================================== */
    #define DCY_SHADE(px, halfW) \
        (((px) * 5 < -(halfW) * 3) ? cLit  : \
         ((px) * 5 < -(halfW) * 1) ? cMid  : \
         ((px) * 5 <  (halfW) * 1) ? cMid  : \
         ((px) * 5 <  (halfW) * 3) ? cShd  : cDeep)

    /* ================= 腿（先画，脚在最下） ================= */
    {
        int legW = h / 22; if (legW < 1) legW = 1;
        int lx0 = cx - wb / 2;
        int lx1 = cx + wb / 2;
        /* 膝盖位置：腿高的 45% */
        int kneeY = baseY - legH * 45 / 100;
        /* 脚：腿高 14%，比腿本身宽 1px → 读作"踩在地上" */
        int footH = legH * 14 / 100; if (footH < 1) footH = 1;

        for (int i = 0; i < legH; i++) {
            int py = baseY - i;
            /* 手段③ AO：膝盖那一行压暗，让它像"折了一下"而不是"一根棍" */
            float ao = 1.0f;
            if (i == legH - (baseY - kneeY)) ao = 0.72f;
            else if (i == legH - (baseY - kneeY) - 1) ao = 0.86f;
            /* 手段④ 负空间：两腿之间留 1px 背景（若宽度够） */
            int outK = legW + 1;
            DcyPut(lx0 - outK, py, cEdge, 0.95f);
            DcyPut(lx1 + outK, py, cEdge, 0.95f);
            for (int k = -legW; k <= legW; k++) {
                /* 5 段明暗按 k/legW 走 */
                unsigned int c = DCY_SHADE(k, legW);
                /* 脚掌区域更宽 + 更亮（受光面朝上） */
                if (i < footH) { c = (k < 0) ? cLit : cMid; }
                DcyCloth(lx0 + k, py, c, dim * ao, weave);
                DcyCloth(lx1 + k, py, c, dim * ao, weave);
            }
            /* 脚掌外扩的 1px */
            if (i < footH) {
                DcyPut(lx0 - legW - 1, py, cMid, dim * 0.9f);
                DcyPut(lx1 + legW + 1, py, cMid, dim * 0.9f);
            }
        }
        /* 脚底线 + 脚踝缝（手段⑤ 虚线） */
        DcyBar(lx0 - legW - 1, lx0 + legW + 1, baseY, cSeam, 0.9f);
        DcyBar(lx1 - legW - 1, lx1 + legW + 1, baseY, cSeam, 0.9f);
        if (fine) {
            int ankY = baseY - footH - 1;
            for (int k = -legW; k <= legW; k += 2) {
                DcyPut(lx0 + k, ankY, cSeam, 0.8f);
                DcyPut(lx1 + k, ankY, cSeam, 0.8f);
            }
        }
    }

    /* ================================================================
     * 两条手臂 —— 关键改动：**跟着身体轮廓走**，并且留真实的负空间
     * ----------------------------------------------------------------
     * 上一版的问题：手臂画在固定 x（`cx - wb`），而身体是梨形、半宽
     * 每行都在变。结果手臂**埋进了身体里**，只剩一道竖纹，看不出是手臂。
     *
     * 现在：按每一行的 halfW 算手臂位置，往外偏 4px
     *     身体轮廓  cx-halfW-1
     *     背景缝    cx-halfW-2      ← 手段④ 负空间：真的透出背景
     *     手臂描边  cx-halfW-3
     *     手臂填充  cx-halfW-4 / -5
     *     手臂描边  cx-halfW-6
     * ============================================================== */
    {
        int bodyTop = baseY - legH - bodyH;
        /* 手臂的纵向范围：从肩膀稍下到手 */
        int armTopI = bodyH / 8;
        int armBotI = bodyH - bodyH / 10;
        if (armBotI <= armTopI) armBotI = armTopI + 1;

        #define DCY_HALFW(i) (wt + (wb - wt) * (i) / (bodyH > 1 ? bodyH - 1 : 1))

        /* 肘部：手臂范围的中点 */
        int elbowI = armTopI + (armBotI - armTopI) * 48 / 100;

        for (int i = armTopI; i <= armBotI; i++) {
            int py = bodyTop + i;
            int halfW = DCY_HALFW(i);
            int axOut  = cx - halfW - 6;    /* 外侧描边 */
            int axA    = cx - halfW - 5;    /* 填充：受光 */
            int axB    = cx - halfW - 4;    /* 填充：主体 */
            int axIn   = cx - halfW - 3;    /* 内侧描边（负空间那一侧） */
            /* 手段③ AO：肘部两行压暗，让手臂有"折了一下"的关节 */
            float ao = (i == elbowI) ? 0.72f : ((i == elbowI - 1 || i == elbowI + 1) ? 0.86f : 1.0f);

            DcyPut(axOut, py, cEdge, 0.95f);
            DcyCloth(axA, py, cLit, dim * ao, weave);
            DcyCloth(axB, py, cMid, dim * ao, weave);
            DcyPut(axIn,  py, cEdge, 0.95f);
            /* 负空间：内侧描边再往内一格保持背景（不画 = 保持原像素） */
        }

        /* 手：末端一块比手臂略宽的缝口 + 两道针脚 */
        {
            int py = bodyTop + armBotI + 1;
            int halfW = DCY_HALFW(armBotI);
            int hx0 = cx - halfW - 6;
            int hx1 = cx - halfW - 3;
            DcyBar(hx0, hx1, py,     cEdge, 0.95f);
            DcyBar(hx0, hx1, py + 1, cMid,  dim);
            DcyBar(hx0, hx1, py + 2, cEdge, 0.95f);
            if (fine) {
                DcyPut(hx0 + 1, py + 1, cLit,  dim * 0.95f);   /* 手背受光 */
                DcyPut(hx0 + 2, py + 1, cSeam, dim);           /* 针脚 */
                DcyPut(hx0 + 3, py + 1, cSeam, dim);
            }
        }

        /* 右臂：**断了**，只剩一截残根 + 锯齿断口 + 稻草 */
        {
            int stubTopI = bodyH / 4;
            int stubBotI = stubTopI + bodyH / 6;
            for (int i = stubTopI; i <= stubBotI; i++) {
                int py = bodyTop + i;
                int halfW = DCY_HALFW(i);
                int sxIn  = cx + halfW + 3;
                int sxB   = cx + halfW + 4;
                int sxA   = cx + halfW + 5;
                int sxOut = cx + halfW + 6;
                DcyPut(sxIn,  py, cEdge, 0.95f);
                DcyCloth(sxB, py, cShd, dim, weave);
                DcyCloth(sxA, py, cMid, dim, weave);
                DcyPut(sxOut, py, cEdge, 0.95f);
            }
            /* 断口：锯齿状 —— 读作"被撕开"，不是"被切齐" */
            {
                int py = bodyTop + stubBotI + 1;
                int halfW = DCY_HALFW(stubBotI);
                for (int k = 0; k <= 6; k++) {
                    int jy = py + ((k & 1) ? 0 : 1);
                    DcyPut(cx + halfW + 3 + k, jy, cSeam, 1.0f);
                }
                /* 稻草：从断口向外炸；长度用索引确定，不用 rand（保证每帧一致） */
                int nStraw = (h >= 40) ? 5 : 2;
                for (int s = 0; s < nStraw; s++) {
                    int len = h / 14 + ((s * 3) % 5);
                    int dx = (s - nStraw / 2);
                    for (int k = 0; k < len; k++) {
                        /* 形态差：鸣叫时稻草**炸开**（往外，像在震），
                           静默时**垂下来**（顺着重力） */
                        int sx2 = cx + halfW + 6 + dx + (armed ? k / 2 : 0);
                        int sy2 = py + 1 + k + (armed ? -k / 3 : 0);
                        DcyPut(sx2, sy2, cStraw,
                               0.88f - 0.05f * (float)k);
                    }
                }
            }
        }
        #undef DCY_HALFW
    }

    /* ================= 身体：梨形 + 5 段明暗 + 织纹 + 两条麻袋缝 ================= */
    {
        int yTop = baseY - legH - bodyH;
        for (int i = 0; i < bodyH; i++) {
            int py = yTop + i;
            int halfW = wt + (wb - wt) * i / (bodyH > 1 ? bodyH - 1 : 1);
            DcyPut(cx - halfW - 1, py, cEdge, 0.95f);
            DcyPut(cx + halfW + 1, py, cEdge, 0.95f);
            for (int x = -halfW; x <= halfW; x++)
                DcyCloth(cx + x, py, DCY_SHADE(x, halfW), dim, weave);
        }
        /* 手段⑤ 两条麻袋竖缝：从肩走到胯，虚线 —— 让它像"布做的" */
        if (fine) {
            for (int i = 2; i < bodyH - 2; i += 2) {
                int halfW = wt + (wb - wt) * i / (bodyH > 1 ? bodyH - 1 : 1);
                DcyPut(cx - halfW / 2, yTop + i, cSeam, 0.62f);
                DcyPut(cx + halfW / 2, yTop + i, cSeam, 0.45f);
            }
        }
        /* 补丁：偏左下、比原来小、四边虚线针脚 + 内部织纹 */
        if (fine && bodyH >= 10 && wb >= 4) {
            int pw = wb * 3 / 4;  if (pw < 2) pw = 2;
            int ph = bodyH / 3;   if (ph < 2) ph = 2;
            int pxc = cx - wb / 2;
            int pyc = yTop + bodyH * 3 / 5;
            DcyRect(pxc - pw / 2, pyc, pxc + pw / 2, pyc + ph, cSeam, 0.90f * dim);
            /* 补丁内部稍亮一点（它是叠在身体上的一层布） */
            DcyRect(pxc - pw / 2 + 1, pyc + 1, pxc + pw / 2 - 1, pyc + ph - 1,
                    cShd, 0.55f * dim);
            DcyDashRect(pxc - pw / 2 - 1, pyc - 1, pxc + pw / 2 + 1, pyc + ph + 1,
                        cSeam, 0.95f);
        }
        /* ================= **修「小黑点」** =================
           用户说的「身体下面 / 腿上面那个小黑点」，是两腿之间漏出来的背景。
           原来 hem 处什么都没有，于是那 1~2 像素的暗背景看起来像破了个洞。
           现在：① 加一条下摆（hem）把身体和腿之间封住
                 ② 中间再补一条竖缝，让"两腿之间"是**有意的接缝**而不是洞 */
        {
            int hemY = baseY - legH;
            DcyBar(cx - wb - 1, cx + wb + 1, hemY,     cEdge, 0.95f);
            DcyBar(cx - wb,     cx + wb,     hemY + 1, cShd,  dim);
            DcyBar(cx - wb,     cx + wb,     hemY + 2, cEdge, 0.85f);
            /* 两腿之间：从 hem 往下 2px 的接缝，封掉那个黑洞 */
            DcyVBar(cx, hemY + 1, hemY + (fine ? 3 : 2), cSeam, 0.9f);
            if (fine) DcyBar(cx - 1, cx + 1, hemY + 1, cSeam, 0.7f);
        }
    }

    /* ================= 脖子 + 绕颈麻绳（地标） ================= */
    {
        int neckY = baseY - legH - bodyH - neckH;
        int nw = wt / 2;
        /* 脖子：深色、比身体窄 —— 头和身体不再黏在一起 */
        for (int i = 0; i < neckH; i++)
            DcyBar(cx - nw, cx + nw, neckY + i, cSeam, 0.88f);
        /* 脖子两侧的 AO：头压下来的影子 */
        DcyVBar(cx - nw - 1, neckY, neckY + neckH - 1, cEdge, 0.8f);
        DcyVBar(cx + nw + 1, neckY, neckY + neckH - 1, cEdge, 0.8f);

        int ropeY0 = neckY + (neckH > 3 ? 1 : 0);
        int ropeY1 = neckY + neckH - 2;
        if (ropeY1 < ropeY0) ropeY1 = ropeY0;
        int rw = wt + (wb - wt) / 3;

        /* 第 1 圈：描边 + 绳色 + 左上受光边（三段做圆绳的体积） */
        DcyBar(cx - rw, cx + rw, ropeY0 - 1, cEdge, 0.9f);
        DcyBar(cx - rw, cx + rw, ropeY0,     cRope, dim);
        {   int litEnd = cx + rw - rw / 2;
            DcyBar(cx - rw, litEnd, ropeY0, cRopeL, dim * 0.55f); }
        /* 第 2 圈（脖子够高才画） */
        if (neckH >= 4) DcyBar(cx - rw + 1, cx + rw - 1, ropeY1, cRope, dim);
        /* 绳结 + 垂下的绳头（右侧） */
        if (h >= 26) {
            int kx = cx + rw;
            DcyPut(kx,     ropeY0 - 1, cEdge, 1.0f);
            DcyPut(kx,     ropeY0,     cRopeL, dim);
            DcyPut(kx + 1, ropeY0,     cRope,  dim);
            DcyPut(kx,     ropeY0 + 1, cRope,  dim);
            int hang = neckH + bodyH / 5;
            for (int k = 0; k < hang; k++) {
                int hx = kx + (k > 2 ? 1 : 0);
                DcyPut(hx,     ropeY0 + 2 + k, cRope, dim * 0.92f);
                if (fine && (k & 1)) DcyPut(hx - 1, ropeY0 + 2 + k, cRopeL, dim * 0.4f);
            }
        }
    }

    /* ================= 头 ================= */
    {
        int headCY = baseY - legH - bodyH - neckH - headR + headDrop;
        for (int dy = -headR - 1; dy <= headR + 1; dy++) {
            for (int dx = -headR - 1; dx <= headR + 1; dx++) {
                float dd = sqrtf((float)(dx * dx + dy * dy));
                int px = cx + dx, py = headCY + dy;
                if (dd > (float)headR) {
                    if (dd <= (float)headR + 1.2f) DcyPut(px, py, cEdge, 0.95f);
                    continue;
                }
                /* 球面 5 段：用 (-dx-dy)/D 做"左上受光"的投影量 */
                float lit = (float)(-dx - dy) / (float)(headD + 1);
                unsigned int col = (lit >  0.42f) ? cHead :
                                   (lit >  0.08f) ? cHead :
                                   (lit > -0.30f) ? cMid  : cShd;
                if (lit > 0.42f && headR >= 5) col = cLit;   /* 高光区用最亮档 */
                DcyCloth(px, py, col, dim, weave);
            }
        }
        /* 手段⑥ 边缘光：左上 1px 提亮，暗场里把剪影剥出来 */
        if (headR >= 4) {
            for (int dy = -headR; dy <= 0; dy++) {
                int dx = -(int)(sqrtf((float)(headR * headR - dy * dy)) + 0.5f);
                DcyPut(cx + dx, headCY + dy, cLit, 0.55f * dim);
            }
        }

        int eyeY = headCY - headR / 4;
        if (headR >= 3) {
            /* 纽扣眼：深色扣 + 1 像素受光点 */
            int ex = cx - headR / 2;
            int r  = (headR >= 6) ? 1 : 0;
            DcyBar(ex - r, ex + r, eyeY, 0x00282018u, 1.0f);
            if (r) {
                DcyBar(ex - r, ex + r, eyeY + 1, 0x00282018u, 1.0f);
                DcyBar(ex - r, ex + r, eyeY - 1, 0x00282018u, 1.0f);
                DcyPut(ex - 1, eyeY - 1, 0x00908870u, 0.9f);   /* 扣子受光点 */
                DcyPut(ex + 1, eyeY + 1, 0x00140E08u, 0.9f);   /* 扣眼 */
            }
            /* 空洞眼：纯黑，比纽扣眼小一圈。
               鸣叫态时**左眼（纽扣那颗）换成琥珀**——只有一颗亮，
               两眼全亮就变成"机器人在看你"，不像坏掉的玩偶。 */
            if (armed) {
                DcyPut(ex, eyeY - 1, 0x00FFC860u, 1.0f);
                if (r) DcyPut(ex - 1, eyeY - 1, 0x00FFE0A0u, 1.0f);
            }
            int ex2 = cx + headR / 2;
            DcyPut(ex2, eyeY, 0x00140E08u, 1.0f);
            if (headR >= 5) { DcyPut(ex2, eyeY + 1, 0x00140E08u, 1.0f);
                              DcyPut(ex2 + 1, eyeY, 0x00140E08u, 0.9f); }
        }

        /* ---- 嘴：**两个形态的第二个判读点** ---- */
        if (headR >= 5) {
            int my = headCY + headR / 2;
            if (!armed) {
                /* 形态 A：缝起来的十字 —— 它是"闭着"的 */
                DcyBar(cx - headR / 2, cx + headR / 3, my, cSeam, 0.92f);
                DcyVBar(cx - headR / 5, my - 1, my + 1, cSeam, 0.92f);
            } else {
                /* 形态 B：**张开的口** —— 菱形，越大越明显
                   （菱形而不是椭圆：在这个尺度上椭圆会糊成一个点） */
                int mw = headR / 2 + 1;
                int mh = headR / 3 + 1;
                for (int dy = -mh; dy <= mh; dy++) {
                    int half = mw - (mw * (dy < 0 ? -dy : dy)) / (mh + 1);
                    if (half < 0) half = 0;
                    for (int dx = -half; dx <= half; dx++)
                        DcyPut(cx + dx, my + dy, 0x00181012u, 1.0f);   /* 口内：比黑更深的暖黑 */
                }
                /* 口的上下缘：锯齿（读作"撕裂开的布"） */
                for (int k = -mw; k <= mw; k += 2) {
                    DcyPut(cx + k, my - mh - 1, cSeam, 0.9f);
                    DcyPut(cx + k + 1, my + mh + 1, cSeam, 0.7f);
                }
                /* 口腔深处一点暗红 —— 只有它让"张嘴"读得出来 */
                DcyPut(cx, my, 0x00401A14u, 1.0f);
                if (headR >= 7) DcyPut(cx, my + 1, 0x00401A14u, 0.85f);
            }
        }

        /* 激活态：头部一圈暖光（表示它在响，不把眼睛点亮成灯泡） */
        if (armed && headR >= 4) {
            int gr = headR + 1;
            for (int dy = -gr; dy <= gr; dy++) {
                for (int dx = -gr; dx <= gr; dx++) {
                    float dd = sqrtf((float)(dx * dx + dy * dy));
                    if (dd > (float)gr || dd < (float)headR) continue;
                    float a = (1.0f - dd / (float)gr) * 0.16f * pulse;
                    DcyPut(cx + dx, headCY + dy, 0x00C89040u, a);
                }
            }
        }

        /* ==================================================================
         * **声音波纹**：鸣叫态独有的第三层判读
         * ----------------------------------------------------------------
         * 两圈从头部向外扩散的弧，半径由 shoutT 驱动（1 → 0 表示
         * 这一次叫已经过去多久）。只在头部的**上半圈**画，
         * 腰线以下不画 —— 否则远处会看起来像"地上有光圈"。
         * 用点画（每 2 个像素一个）而不是实线：实线太抢戏。
         * ================================================================ */
        if (armed && h >= 24) {
            float t = 1.0f - d->shoutT / DECOY_SHOUT_EVERY;   /* 0 → 1 */
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            for (int ring = 0; ring < 2; ring++) {
                float ph = t + (float)ring * 0.5f;
                if (ph > 1.0f) ph -= 1.0f;
                float rr = (float)headR + 2.0f + ph * (float)h * 0.14f;
                float a  = (1.0f - ph) * 0.30f * pulse;
                if (a <= 0.02f) continue;
                int steps = (int)(rr * 3.2f) + 8;
                for (int s2 = 0; s2 < steps; s2 += 2) {
                    float ang = -3.14159f * (float)s2 / (float)steps;   /* 上半圈 */
                    int px = cx + (int)(cosf(ang) * rr);
                    int py = headCY + (int)(sinf(ang) * rr);
                    DcyPut(px, py, 0x00D8B070u, a);
                }
            }
        }
    }
    #undef DCY_SHADE
}

static void DrawShotModel(int idx) {
    float wx = shot[idx].x, wy = shot[idx].y, z = shot[idx].z;
    int cx, cy;
    float unit, ty;
    if (!ProjectPoint(wx, wy, z, &cx, &cy, &unit, &ty)) return;
    int occ = cx < 0 ? 0 : (cx >= renderW ? renderW - 1 : cx);
    if (ty * 0.85f >= zBuffer[occ]) return;

    int kind = shot[idx].kind;
    if (kind == 6) {
        /* v2.0 信号枪弹：一小团明黄的光点 + 外晕 */
        int r = (int)(unit * 0.05f); if (r < 2) r = 2;
        for (int dy = -r * 2; dy <= r * 2; dy++) {
            for (int dx = -r * 2; dx <= r * 2; dx++) {
                int px = cx + dx, py = cy + dy;
                if (px < 0 || px >= renderW || py < 0 || py >= renderH) continue;
                float d = sqrtf((float)(dx * dx + dy * dy)) / (float)(r * 2);
                if (d > 1.0f) continue;
                unsigned int col = (d < 0.45f) ? 0x00FFF8D0u
                                 : (d < 0.75f ? 0x00FFD040u : 0x00C06020u);
                PutPixelBlend(px, py, col, 0.95f - 0.5f * d);
            }
        }
        return;
    }
    /* ===== v3.3 新投掷物的飞行模型 =====
       kind 1  投石：不规则灰石，带高光面
       kind 3  胶水：矮胖圆罐 + 瓶颈，青绿色，看得出是「黏的」
       kind 10 回声：细长的哨形，淡紫，尖端朝前
       （kind 2 砖头 / kind 7 火瓶 / kind 9 镜片 已随系统删除） */

    if (kind == 1) {
        /* 投石：不规则多边形石块 */
        int w = (int)(unit * 0.075f); if (w < 3) w = 3;
        int h = (int)(unit * 0.065f); if (h < 3) h = 3;
        for (int yy = -h; yy <= h; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            /* 不规则边缘：宽度随行变化，看起来像块石头不是圆球 */
            int wob = ((yy * 37) ^ 0x55) % 3 - 1;
            int halfW = w + wob - (yy > h / 2 ? 1 : 0);
            for (int xx = -halfW; xx <= halfW; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                unsigned int c;
                if (yy < 0 && xx < 0)      c = 0x00B4B0A8u;   /* 左上高光面 */
                else if (yy > 0)           c = 0x0056504Au;   /* 下方阴影面 */
                else                       c = 0x00888478u;   /* 中间 */
                PutPixelBlend(px, py, c, 0.96f);
            }
        }
        return;
    }
    if (kind == 8) {
        /* 胶水瓶：矮胖罐 + 短瓶颈，青绿色，表面有湿滑高光 */
        int w = (int)(unit * 0.085f); if (w < 3) w = 3;
        int h = (int)(unit * 0.085f); if (h < 4) h = 4;
        for (int yy = -h; yy <= h; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            /* 上 1/3 收成瓶颈，下面是胖罐身 */
            int halfW = (yy < -h * 2 / 3) ? (w / 2 + 1) : w;
            for (int xx = -halfW; xx <= halfW; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                unsigned int c;
                if (yy < -h * 2 / 3)       c = 0x005A9A6Eu;   /* 瓶颈：深青 */
                else if (xx > halfW - 2)   c = 0x0090F0B4u;   /* 右侧湿滑高光 */
                else if (yy > h / 2)       c = 0x002A4A38u;   /* 罐底阴影 */
                else                       c = 0x004C8A62u;   /* 罐身 */
                PutPixelBlend(px, py, c, 0.96f);
            }
        }
        return;
    }
    if (kind == 10) {
        /* 回声弹：细长哨形，投掷方向拉长，淡紫色，尖端带一点亮 */
        float vlen = sqrtf(shot[idx].vx * shot[idx].vx + shot[idx].vy * shot[idx].vy);
        int len = (int)(unit * 0.11f); if (len < 4) len = 4;
        int rad = (int)(unit * 0.032f); if (rad < 2) rad = 2;
        /* 沿飞行方向画一条细长的锥形 */
        for (int s2 = -len; s2 <= len; s2++) {
            float t2 = (float)s2 / (float)len;          /* -1 尾  +1 尖 */
            int rr = (int)(rad * (1.0f - t2 * 0.55f));
            if (rr < 1) rr = 1;
            for (int d = -rr; d <= rr; d++) {
                int px, py;
                if (vlen > 0.01f) {
                    float ux = shot[idx].vx / vlen, uy = shot[idx].vy / vlen;
                    /* 屏幕方向近似：用 unit 缩放，不做完整透视旋转 */
                    px = cx + (int)(ux * (float)s2 * unit * 0.02f) + d;
                    py = cy + (int)(uy * (float)s2 * unit * 0.02f);
                } else {
                    px = cx + d; py = cy + s2;
                }
                if (px < 0 || px >= renderW || py < 0 || py >= renderH) continue;
                unsigned int c = (t2 > 0.55f) ? 0x00E8D0FFu : 0x009878C8u;
                PutPixelBlend(px, py, c, 0.94f);
            }
        }
        return;
    }

    if (kind == 2) {
        /* 砖头（已废弃，保留代码以防老存档里的 shot 还在飞） */
        int w = (int)(unit * 0.17f); if (w < 3) w = 3;
        int h = (int)(unit * 0.08f); if (h < 2) h = 2;
        for (int yy = -h; yy <= h; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            for (int xx = -w; xx <= w; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                int shade = (yy < 0) ? 1 : 0;               /* 上亮下暗 */
                unsigned int c = shade ? 0x00C8A888u : 0x00886848u;
                PutPixelBlend(px, py, c, 0.95f);
            }
        }
    } else if (kind == 3) {
        /* 酸瓶：瓶身 + 瓶颈 */
        int w = (int)(unit * 0.09f); if (w < 2) w = 2;
        int h = (int)(unit * 0.14f); if (h < 4) h = 4;
        for (int yy = -h; yy <= h; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            int halfW = (yy < -h * 2 / 3) ? w / 2 : w;      /* 上半段收成瓶颈 */
            for (int xx = -halfW; xx <= halfW; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                unsigned int c = (yy > h / 3) ? 0x0080FF60u : 0x00C0FF90u;
                PutPixelBlend(px, py, c, 0.95f);
            }
        }
    } else if (kind == 4) {
        /* 烟雾弹：圆柱罐 */
        int w = (int)(unit * 0.10f); if (w < 3) w = 3;
        int h = (int)(unit * 0.15f); if (h < 4) h = 4;
        for (int yy = -h; yy <= h; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            for (int xx = -w; xx <= w; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                unsigned int c = 0x00909090u;
                if (xx > w - 2) c = 0x00C8C8C8u;            /* 右侧高光 */
                if (yy < -h + 2) c = 0x00B0B0B0u;           /* 顶部盖 */
                PutPixelBlend(px, py, c, 0.95f);
            }
        }
    } else {
        DrawOrbSprite(wx, wy, z, 0.12f, 0x00ffffffu, 0x0060c0ffu, 0.95f);
    }
}

/* v2.9 剑气：竖着的一道月牙刀光。
   - 刀身中间厚、两头尖（不是椭圆，椭圆看着像光球）
   - 中间亮、边缘青
   - 身后拖两层半透明的残影，做出"劈出去"的速度感
   渲染层只能用 PutPixelBlend（BlendRectWin 在 ui.c）。 */
/* v2.0 额外威胁：用一张剪影 + 按种类换眼睛颜色/体型 */
static void DrawExtraSprite(int idx) {
    const Extra* e = ExtraAt(idx);
    if (!e || !e->active) return;
    if (!SpritesReady()) return;
    int spr = SPR_MON_ECHO;
    if (e->kind == 0) spr = SPR_MON_SHADE;
    else if (e->kind == 1) spr = SPR_MON_SPIDER;
    float halfH = (e->kind == 0) ? 1.05f : (e->kind == 1 ? 0.42f : 0.95f);
    DrawBakedMonster(spr, e->x, e->y, halfH, 0.85f, 1, 0.0f, (float)g_timeSec * 2.0f);
}

static void DrawSlashBlade(float wx, float wy, float scale, float alpha) {
    if (alpha <= 0.02f) return;
    SpriteBox s = ProjectSprite(wx, wy, 0.0f, 0.62f);
    if (!s.ok) return;

    int cx = (s.x0 + s.x1) / 2;
    int occ = cx < 0 ? 0 : (cx >= renderW ? renderW - 1 : cx);
    if (s.ty * 0.90f >= zBuffer[occ]) return;

    int hh = (s.y1 - s.y0) / 2;
    if (hh < 3) hh = 3;
    hh = (int)(hh * scale);
    int bw = hh / 5;
    if (bw < 2) bw = 2;
    int ycen = (s.y0 + s.y1) / 2;

    for (int y = -hh; y <= hh; y++) {
        float u = (float)y / (float)hh;
        int w = (int)(bw * (1.0f - u * u));      /* 中间厚两头尖 */
        if (w < 1) continue;
        int py = ycen + y;
        if (py < 0 || py >= renderH) continue;
        for (int dx = -w; dx <= w; dx++) {
            int px = cx + dx;
            if (px < 0 || px >= renderW) continue;
            float k = 1.0f - (float)(dx < 0 ? -dx : dx) / (float)(w + 1);
            unsigned int col = (k > 0.66f) ? 0x00F0FFFFu
                             : (k > 0.33f) ? 0x0098E8FFu : 0x0040A8FFu;
            PutPixelBlend(px, py, col, alpha * (0.45f + 0.55f * k));
        }
    }
    /* 刃口：朝前的一侧加一条白线 */
    if (alpha > 0.5f) {
        for (int y = -hh; y <= hh; y++) {
            float u = (float)y / (float)hh;
            int w = (int)(bw * (1.0f - u * u));
            int px = cx + w, py = ycen + y;
            if (px >= 0 && px < renderW && py >= 0 && py < renderH)
                PutPixelBlend(px, py, 0x00FFFFFFu, 0.55f * alpha);
        }
    }
}

static void DrawSlashSprite(void) {
    if (!slashActive) return;
    /* 残影：沿飞行方向往回拖两层 */
    DrawSlashBlade(slashX - slashDX * 0.55f, slashY - slashDY * 0.55f, 0.80f, 0.35f);
    DrawSlashBlade(slashX - slashDX * 1.10f, slashY - slashDY * 1.10f, 0.62f, 0.18f);
    DrawSlashBlade(slashX, slashY, 1.0f, 1.0f);
}

static void DrawShotSprite(int idx) {
    if (!shot[idx].active) return;
    DrawShotModel(idx);
}

/* v1.18：烟幕团 —— 地面上一团不断翻涌的灰烟 */
/* v3.3 地面胶水：一滩青色黏液。
   和烟团的关键区别：烟是**压暗**（遮蔽），胶水是**提亮**（反光湿滑），
   所以这里用加法式的青绿高光，同时把底色往暗里压一点点做出对比。
   画法照抄烟团的椭圆 + 圆心衰减，但颜色和遮罩相反。 */
/* v3.3 地面胶水：**贴地的横截面**，不是立体的团块。
   用户反馈：「胶水地面贴图是立体的，我希望你做成横截面的」。
   原来的写法用 ProjectSprite 拿一个竖直 billboard，
   在透视下看起来像地上立着的一团东西。
   现在改成**世界空间网格采样**：在半径内撒一圈采样点，
   每点单独投影 —— 远的点在屏幕上更靠上，自然就变成被压扁的地面椭圆，
   这才是正确的「地面贴花」画法。 */
static void DrawGluePuddle(void) {
    spriteLight = 1.0f;
    extern GlueZone glues[MAX_GLUE];
    for (int i = 0; i < MAX_GLUE; i++) {
        if (!glues[i].active) continue;
        float t = glues[i].life / GLUE_LIFE;         /* 1 → 0 */
        if (t > 1.0f) t = 1.0f;
        if (t < 0.0f) t = 0.0f;
        float fade = 0.35f + 0.65f * t;

        /* 网格密度：半径 2.5 格 → 采样 22x22，步长约 0.23 格。
           太稀会看到断点，太密单帧开销上去了（软件光栅，得省）。 */
        const int N = 22;
        float step = (GLUE_RADIUS * 2.0f) / (float)(N - 1);
        int cellR = (int)(UIS(3));
        if (cellR < 1) cellR = 1;

        for (int gy = 0; gy < N; gy++) {
            for (int gx = 0; gx < N; gx++) {
                float wx = glues[i].x - GLUE_RADIUS + (float)gx * step;
                float wy = glues[i].y - GLUE_RADIUS + (float)gy * step;
                float dx = wx - glues[i].x, dy = wy - glues[i].y;
                float d2n = (dx * dx + dy * dy) / (GLUE_RADIUS * GLUE_RADIUS);
                if (d2n > 1.0f) continue;              /* 圆外不画 */

                int px, py;
                float unit, ty;
                /* z = 0：贴地。这正是「横截面」的关键 —— 不用 billboard */
                if (!ProjectPoint(wx, wy, 0.0f, &px, &py, &unit, &ty)) continue;
                if (px < 0 || px >= renderW || py < 0 || py >= renderH) continue;
                if (ty >= zBuffer[px]) continue;       /* 被墙挡住就不画 */

                /* 形状：圆心最浓 → 边缘化开；再加一点不规则 */
                float core = 1.0f - d2n;
                float wob = 0.90f + 0.10f * sinf((float)gx * 1.7f + (float)gy * 2.3f);
                unsigned int col;
                float a;
                if (core > 0.72f) {                    /* 中心高光 */
                    col = 0x006fd08au;
                    a = (0.16f + 0.20f * core) * fade;
                } else if (d2n > 0.78f) {               /* 边缘一圈反光 */
                    col = 0x005cb87cu;
                    a = (0.34f + 0.28f * core) * fade * wob;
                } else {                                /* 主体 */
                    col = 0x003a6b4au;
                    a = (0.30f + 0.40f * core) * fade * wob;
                }
                if (a > 0.85f) a = 0.85f;
                if (a <= 0.02f) continue;

                /* 每个采样点画一个小方块，方块之间自然连成一片 */
                int x0 = px - cellR, x1 = px + cellR;
                int y0 = py - cellR, y1 = py + cellR;
                if (x0 < 0) x0 = 0;
                if (y0 < 0) y0 = 0;
                if (x1 >= renderW) x1 = renderW - 1;
                /* 竖直方向压扁 —— 地面贴花在屏幕上总是扁的 */
                int yShrink = cellR / 2;
                if (yShrink < 1) yShrink = 1;
                y1 = py + yShrink;
                if (y1 >= renderH) y1 = renderH - 1;
                for (int yy = y0; yy <= y1; yy++)
                    for (int xx = x0; xx <= x1; xx++)
                        PutPixelBlend(xx, yy, col, a);
            }
        }
    }
}

static void DrawSmokeCloud(void) {
    spriteLight = 1.0f;      /* v2.1：烟团不从精灵衰减继承 */

    extern SmokeCloud smokes[MAX_SMOKE];
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (!smokes[i].active) continue;
        float t = smokes[i].life / SMOKE_LIFE;      /* 1 → 0 */
        float grow = 1.0f;                          /* v1.18：落地直接满尺寸，不再慢慢长大 */
        /* v2.0：烟必须"减光"而不是"发光"。
           原来用 DrawOrbSprite 是加法混合 —— 暗走廊里烟团亮得像三盏灯，
           和"遮蔽视线"的机制完全矛盾。这里改成直接压暗画面。 */
        {
            SpriteBox sb = ProjectSprite(smokes[i].x, smokes[i].y, 0.0f, 1.15f * grow);
            if (sb.ok) {
                int cx = (sb.x0 + sb.x1) / 2, cyy = (sb.y0 + sb.y1) / 2;
                int rx = (sb.x1 - sb.x0) / 2, ry = (sb.y1 - sb.y0) / 2;
                float amt = 0.55f * t + 0.12f;
                if (rx > 0 && ry > 0) {
                    for (int y = -ry; y <= ry; y++) {
                        int py = cyy + y;
                        if (py < 0 || py >= renderH) continue;
                        for (int x = -rx; x <= rx; x++) {
                            int px = cx + x;
                            if (px < 0 || px >= renderW) continue;
                            /* v2.0 修复：烟原来完全不查深度，暗房里像墙上糊了一块灰 */
                            if (sb.ty >= zBuffer[px]) continue;
                            float u = (float)(x * x) / (float)(rx * rx) +
                                      (float)(y * y) / (float)(ry * ry);
                            if (u > 1.0f) continue;
                            float a = amt * (1.0f - u * 0.7f);
                            PutPixelBlend(px, py, 0x00303034u, a);
                        }
                    }
                }
            }
        }
    }
}

static void DrawShardSprite(void) {
    spriteLight = 1.0f;      /* v2.1：金币是自发光的 */

    for (int i = 0; i < MAX_SHOTS * SHARD_COUNT; i++) {
        if (!shards_debris[i].active) continue;
        SpriteBox s = ProjectSprite(shards_debris[i].x, shards_debris[i].y, shards_debris[i].z / 10.0f, 0.05f);
        if (!s.ok) continue;

        int cx = (s.x0 + s.x1) / 2;
        int cy = (s.y0 + s.y1) / 2;
        float lifeRatio = shards_debris[i].life / MAX_SHARD_LIFE;
        float alpha = 0.7f * lifeRatio;
        if (alpha < 0.05f) continue;

        /* v2.1：金币按 tint 分色（玻璃 / 血 / 尘），尺寸随距离缩放 */
        unsigned int scol = 0x00d0e0ffu;
        if (shards_debris[i].tint == 1)      scol = 0x00c02818u;   /* 血 */
        else if (shards_debris[i].tint == 2) scol = 0x00c0a070u;   /* 尘 */
        int rad = (s.x1 - s.x0) >> 1;
        if (rad < 1) rad = 1;
        if (rad > 4) rad = 4;
        for (int dy = -rad; dy <= rad; dy++) {
            for (int dx = -rad; dx <= rad; dx++) {
                if (dx == 0 && dy == 0) continue;
                if (abs(dx) + abs(dy) > rad) continue;     /* 菱形 */
                int x = cx + dx, y = cy + dy;
                if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
                if (s.ty >= zBuffer[x]) continue;
                PutPixelAdd(x, y, scol, alpha);
            }
        }
        /* 中心 */
        if (cx >= 0 && cx < renderW && cy >= 0 && cy < renderH && s.ty < zBuffer[cx])
            PutPixelAdd(cx, cy, 0x00ffffffu, alpha * 1.5f);
    }
}

/* v2.1：安全屋。原来画的是一朵粉色花 + 地上紫斑 —— 在一个恐怖游戏里
   像误入了别的游戏。改成"一根矮的暖色光柱 + 地面光斑"，和出口那根
   冷色光柱形成对照（暖 = 安全，冷 = 出口）。 */
static void DrawFlowerSprite(void) {
    if (!safeRoom.active) return;
    /* v2.2 修复（用户实测：站到安全区里，灯糊满视野）：
       ① 这盏灯只有半米高，halfH 从 0.50 降到 0.30
       ② 玩家已经在安全屋里（距离 < 1.3 格）就干脆不画 ——
          你已经站在里面了，不该被自己的营地挡住视线 */
    {
        float dx = posX - safeRoom.x, dy = posY - safeRoom.y;
        if (dx * dx + dy * dy < 1.69f) return;
    }
    DrawBakedProp(SPR_PROP_SAFE, safeRoom.x, safeRoom.y, 0.0f, 0.30f, 1.0f);
}

/* ===== v2.4.4 **出口重新设计 v2**（按 art_direction.h + art.c）=====
   v2.4.3 那版还是太平：一整块灰 + 一条黑缝，没有体积感。

   这一版加了四层细节，全部用 src/art.c 的仿 3D 原语：

   ① 斜角边框   ArtBevelK —— 左上受光、右下背光，1~2 像素的明暗差就出厚度
   ② 内凹门板   ArtRecessK —— 门框里再嵌一层，上壁暗、下壁亮，读作「凹进去」
   ③ 门缝透视   缝从上往下加宽（近大远小），两侧被光溢出的边逐级变亮
   ④ 光柱抖动   用 ArtDitherOn 做有序抖动，渐层是分级清晰的像素画，不是糊成一团

   地标仍然是「门上方那道竖向光柱」，只有出口有。
*/
static void DrawExitBeamSprite(void) {
    float halfH = breakerOpen ? 1.05f : 0.72f;
    SpriteBox s = ProjectSprite(exitX, exitY, 0.0f, halfH);
    if (!s.ok) return;
    if (s.ty < 0.55f) return;

    int w = s.x1 - s.x0, h = s.y1 - s.y0;
    if (w < 3 || h < 3) return;

    DrawGroundShadow((s.x0 + s.x1) / 2, s.y1, w / 3, s.ty);

    float light = SpriteShade(s.ty, 0.12f) * ArtPropLight(exitX, exitY);

    /* 呼吸脉冲：通电时让远处注意到「那边在动」 */
    float pulse = 1.0f;
    if (breakerOpen) {
        extern float gameTime;
        pulse = 0.82f + 0.18f * sinf(gameTime * 2.4f);
    }

    for (int x = s.x0; x < s.x1; x++) {
        if (x < 0 || x >= renderW) continue;
        if (s.ty >= zBuffer[x]) continue;
        float u = (float)(x - s.x0) / (float)w;

        for (int y = s.y0; y < s.y1; y++) {
            if (y < 0 || y >= renderH) continue;
            float v = (float)(y - s.y0) / (float)h;

            /* ---- 外框范围：四角切掉，像嵌在墙里 ---- */
            if (u < 0.06f || u > 0.94f || v < 0.04f) continue;

            /* ---- 门缝：上窄下宽（近大远小） ---- */
            float gapW = (breakerOpen ? 0.26f : 0.055f) * (0.72f + 0.28f * v);
            float du = (u - 0.5f) * 2.0f;
            int isGap = (du > -gapW && du < gapW);

            unsigned int c;
            float k;

            if (isGap) {
                /* 门缝内部：通电亮青，断电几乎全黑 */
                c = breakerOpen ? ART_EXIT_GAP_ON : ART_EXIT_GAP_OFF;
                k = light * (breakerOpen ? pulse : 0.5f);
                /* 缝的深处更暗（往里看是黑的） */
                k *= 0.55f + 0.45f * fabsf(du) / (gapW > 0.001f ? gapW : 0.001f);
            } else {
                /* ---- 门框：斜角 + 内凹两层光照 ---- */
                float lit = ArtBevelK(u, v, 0.045f, 0.55f)
                          * ArtRecessK(u, v, 0.10f, 0.42f);

                /* 三段材质：外框暗灰 / 内板中灰 / 靠缝的受光边更亮 */
                float distToGap = fabsf(du) - gapW;
                if (distToGap < 0.10f) {
                    c = ART_GREY_HOT;                 /* 被门缝的光照亮的内边 */
                } else if (u > 0.14f && u < 0.86f) {
                    c = ART_GREY_LIT;                 /* 内凹门板 */
                } else {
                    c = ART_GREY_BODY;                /* 外框 */
                }
                k = light * lit;

                /* 通电时，门缝的光会往外溢出一圈 */
                if (breakerOpen && distToGap < 0.26f) {
                    float spill = 1.0f - distToGap / 0.26f;
                    k *= 1.0f + spill * 0.55f * pulse;
                }

                /* 竖向拉丝纹理：每 5 像素一条极淡的暗线，给出表面质感 */
                if (((y - s.y0) % 5) == 0) k *= 0.93f;
            }

            pixels[(size_t)y * renderW + x] = ArtScale(c, k);
        }
    }

    /* ---- 光柱：地标。有序抖动让它像像素画的光，不是模糊渐变 ---- */
    if (breakerOpen) {
        int cxp = (s.x0 + s.x1) / 2;
        int beamHalf = (w / 8 > 2) ? w / 8 : 2;
        int beamH = (int)((s.y1 - s.y0) * 1.15f);
        int beamTop = s.y0 - beamH;
        if (beamTop < 0) beamTop = 0;

        for (int x = cxp - beamHalf - 2; x <= cxp + beamHalf + 2; x++) {
            if (x < 0 || x >= renderW) continue;
            if (s.ty >= zBuffer[x]) continue;

            float dxp = (float)(x - cxp) / (float)(beamHalf + 3);
            float edge = 1.0f - dxp * dxp;            /* 中间最亮，两侧衰减 */
            if (edge <= 0.0f) continue;

            for (int y = beamTop; y < s.y0; y++) {
                if (y < 0 || y >= renderH) continue;
                /* t: 0 = 光柱顶端（最淡）  1 = 接地处（最亮） */
                float t = (float)(y - beamTop) / (float)(beamH + 1);
                float a = edge * t * t * pulse;

                /* 有序抖动：a 越小，落点的像素越稀疏 —— 像真正的光束 */
                if (!ArtDitherOn(a, x, y, 1.35f)) continue;

                float k = light * (0.45f + 0.55f * a);
                pixels[(size_t)y * renderW + x] = ArtScale(ART_BEAM, k);
            }
        }
    }
}

/* 所有 billboard 按距离从远到近画，近的盖住远的（画家算法）
   数组上界 MAX_SPRITES = 容器 + 投掷物 + 配电箱 + 两只怪 + 安全屋 + 出口光柱，
   写满时会被下面的断言拦下来，不会像以前那样悄悄越界。 */
/* v3.0 改造树「听觉」：把最近的噪音源标成地面一点，玩家能"听"到方向 */
/* v3.0：靠近可藏家具时给一圈冷色描边 —— 让玩家一眼看出"这个能躲" */
static void DrawHideSpotHighlight(void) {
    extern int hideSpotNear;
    if (hideSpotNear < 0 || hideSpotNear >= hideSpotCount) return;
    HideSpot* h = &hideSpots[hideSpotNear];
    if (!h->active) return;
    /* v3.3 **删掉这里的 SPR_PROP_SAFE** ——
       用户截图：「隐藏点把原来那个灯的模型删掉，保留作者留的垃圾桶」。
       原来藏身点被画了两次：
         ① DrawHideSpotSprite  → SPR_PROP_TRASH（作者 v3.3 的垃圾桶）✅
         ② 这里                  → SPR_PROP_SAFE（我早期的柜子，顶上带灯）
       两个精灵叠在同一格，看起来就是「垃圾桶上面压了个带灯的柜子」。
       现在只保留作者的垃圾桶。靠近提示改由 HUD 的 PRESS E / HIDE 承担，
       不再额外画一层模型。 */
    (void)h;
}

static void DrawHearingMarker(void) {
    if (!BenchHearing()) return;
    extern float noiseDisplay, noiseSrcX, noiseSrcY;
    if (noiseDisplay <= 0.01f) return;
    float dx = noiseSrcX - posX, dy = noiseSrcY - posY;
    if (dx * dx + dy * dy > 400.0f) return;   /* 20 格内才显示 */
    /* 在地面投影一个闪烁的点（用 billboard 精灵代替） */
    DrawBakedProp(SPR_PROP_CRATE, noiseSrcX, noiseSrcY, 0.0f, 0.18f, 0.45f + 0.3f * noiseDisplay);
}

void RenderSprites(void) {
    struct { float d2; int kind, idx; } list[MAX_SPRITES];
    int n = 0;

    for (int i = 0; i < containerCount; i++) {
        if (!containers[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(containers[i].x, containers[i].y, posX, posY);
        list[n].kind = 0;
        list[n].idx = i;
        n++;
    }
    /* v3.3：藏身点必须真的进渲染列表。
       原来这里没有插入，case 12 却去画 hideSpots[idx]，
       而 kind=12 又被 Extras 占用 → 垃圾桶画在错误的位置/根本不画，
       玩家只看到随机出现的 HIDE 提示。 */
    for (int i = 0; i < hideSpotCount; i++) {
        if (!hideSpots[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(hideSpots[i].x, hideSpots[i].y, posX, posY);
        list[n].kind = 12;
        list[n].idx = i;
        n++;
    }

    /* 2026-11-05：宝箱怪也走渲染列表 —— 这样它和箱子/怪共享同一套
       深度排序与光照，不会出现"穿墙显示"或"永远在最上面"。 */
    for (int i = 0; i < mimicCount; i++) {
        if (mimics[i].state == MIM_NONE || mimics[i].state == MIM_GONE) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(mimics[i].x, mimics[i].y, posX, posY);
        list[n].kind = 15;
        list[n].idx = i;
        n++;
    }
    /* v2.4-rc：房间家具也走同一个渲染列表，共享深度排序与光照 */
    {
        extern RoomProp roomProps[MAX_ROOM_PROPS];
        extern int roomPropCount;
        for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++) {
            if (!roomProps[i].active) continue;
            if (n >= MAX_SPRITES) break;
            list[n].d2 = Dist2(roomProps[i].x, roomProps[i].y, posX, posY);
            list[n].kind = 16;
            list[n].idx = i;
            n++;
        }
    }
    if (breaker.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(breaker.x, breaker.y, posX, posY);
            list[n].kind = 1;
            list[n].idx = 0;
            n++;
        }
    }
    if (monster.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(monster.x, monster.y, posX, posY);
            list[n].kind = 2;
            list[n].idx = 0;
            n++;
        }
    }
    /* v2.4.12：第二只听声怪也进渲染列表 */
    {

        if (monster2.active && n < MAX_SPRITES) {
            list[n].d2 = Dist2(monster2.x, monster2.y, posX, posY);
            list[n].kind = 13;
            list[n].idx = 0;
            n++;
        }
        /* 第三只（120 秒后出现） */
        if (monster3.active && n < MAX_SPRITES) {
            list[n].d2 = Dist2(monster3.x, monster3.y, posX, posY);
            list[n].kind = 14;
            list[n].idx = 0;
            n++;
        }
    }
    if (safeRoom.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(safeRoom.x, safeRoom.y, posX, posY);
            list[n].kind = 7;
            list[n].idx = 0;
            n++;
        }
    }
    if (lightBeast.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(lightBeast.x, lightBeast.y, posX, posY);
            list[n].kind = 5;
            list[n].idx = 0;
            n++;
        }
    }
    /* v1.8 偷电怪 */
    for (int i = 0; i < LEECH_MAX; i++) {
        if (!leeches[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(leeches[i].x, leeches[i].y, posX, posY);
        list[n].kind = 9;
        list[n].idx = i;
        n++;
    }
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(thrown[i].x, thrown[i].y, posX, posY);
        list[n].kind = 6;
        list[n].idx = i;
        n++;
    }
    /* 飞行中的玻璃瓶 */
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!shot[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(shot[i].x, shot[i].y, posX, posY);
        list[n].kind = 8;
        list[n].idx = i;
        n++;
    }
    /* v2.0 额外威胁（影子/盲螨/回响） */
    for (int i = 0; i < ExtrasCount(); i++) {
        const Extra* e = ExtraAt(i);
        if (!e || !e->active) continue;
        /* 影子在开灯时不渲染 */
        if (e->kind == 0 && flashlightOn && battery > 0.0f) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(e->x, e->y, posX, posY);
        list[n].kind = 11;
        list[n].idx = i;
        n++;
    }

    /* v2.9 剑气 */
    if (slashActive) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(slashX, slashY, posX, posY);
            list[n].kind = 10;
            list[n].idx = 0;
            n++;
        }
    }
    /* 跳脸虫不在这个表里 —— 它是屏幕空间的 overlay，
       要盖住整个场景，由 RenderFrame 直接绘制。 */
    if (n < MAX_SPRITES) {
        list[n].d2 = Dist2(exitX, exitY, posX, posY);
        list[n].kind = 3;
        list[n].idx = 0;
        n++;
    }

    /* 插入排序：远的先画 */
    for (int i = 1; i < n; i++) {
        int j = i - 1;
        float d2 = list[i].d2;
        int kind = list[i].kind, idx = list[i].idx;
        while (j >= 0 && list[j].d2 < d2) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1].d2 = d2;
        list[j + 1].kind = kind;
        list[j + 1].idx = idx;
    }

    for (int i = 0; i < n; i++) {
        switch (list[i].kind) {
            case 0: DrawCrateSprite(list[i].idx); break;
            case 1: DrawBreakerSprite(); break;
            case 2: DrawMonsterSprite(); break;
            case 13: DrawMonster2Sprite(); break;   /* v2.4.12 第二只听声怪 */
            case 14: DrawMonster3Sprite(); break;   /* 第三只听声鬼 */
            case 15: DrawMimicSprite(list[i].idx); break;   /* 2026-11-05 宝箱怪 */
        case 9: DrawLeechSprite(list[i].idx); break;
            case 5: DrawLightBeastSprite(); break;
            case 6: DrawFlareSprite(list[i].idx); break;
            case 8: DrawShotSprite(list[i].idx); break;
            case 10: DrawSlashSprite(); break;
            case 12: DrawHideSpotSprite(list[i].idx); break;   /* v3.0 藏身点 */
        case 16: {   /* v2.4-rc P3：房间家具（分层伪立体） */
            extern RoomProp roomProps[MAX_ROOM_PROPS];
            int ri = list[i].idx;
            if (ri >= 0 && ri < MAX_ROOM_PROPS && roomProps[ri].active) {
                /* ================================================================
                 * 2026-11-06（**精灵数过多导致掉帧**）
                 * ----------------------------------------------------------------
                 * 用户实测：「货架算三个精灵，箱子算两个精灵，精灵过多，
                 * 每次路过都掉帧。」
                 *
                 * 分层伪立体的视觉收益，远小于它带来的绘制开销 ——
                 * 尤其 36x36 之后一层有更多家具。
                 * 现在**每个家具只画 1 个精灵**，改由 searchLeft 承担内容量：
                 *   货架 3 次 / 木箱堆 2 次 / 控制台 2 次
                 * ================================================================ */
                int oneSpr = SPR_PROP_SHELF;
                if (roomProps[ri].kind == ROOMPROP_CONSOLE)     oneSpr = SPR_PROP_CONSOLE;
                else if (roomProps[ri].kind == ROOMPROP_CRATES) oneSpr = SPR_PROP_CRATE;
                DrawBakedPropArt(oneSpr, roomProps[ri].x, roomProps[ri].y, 0.0f, 0.62f, 1.0f);
                break;
                break;
            }
            break;
        }
            case 11: DrawExtraSprite(list[i].idx); break;
            case 7: DrawFlowerSprite(); break;
            default: DrawExitBeamSprite(); break;
        }
    }
    /* v1.18：烟幕团（不参与深度排序，直接在最后叠加） */
    DrawHideSpotHighlight();   /* v3.0 藏身点靠近提示 */
    DrawHearingMarker();   /* v3.0 听觉 */
    DrawSmokeCloud();
    DrawGluePuddle();   /* v3.3 地面胶水 */
    /* v3.3：诱饵玩偶 */
    {
        extern Decoy decoysArr[MAX_DECOY];
        for (int di = 0; di < MAX_DECOY; di++)
            if (decoysArr[di].active) DrawDecoyModel(di);
    }
    /* v2.3.5：地上的钱袋 */
    for (int mb = 0; mb < 3; mb++) DrawMoneyBagModel(mb);
    /* v2.3.5：三只新怪 + 回响（v2.3.3 误删过一次，导致全部隐形） */
    DrawNewMonsters();

    DrawShardSprite();
}

/* ------------------------------------------------------------------ */
/* 并行渲染：画面切成横带，主线程 + 工作线程一起画                       */
/* ------------------------------------------------------------------ */

#define MAX_PARTS 9   /* 1 主线程 + 最多 8 个工作线程 */
typedef struct {
    HANDLE hThread;
    HANDLE hStart;   /* 自动复位：主线程派活 */
    HANDLE hDone;    /* 自动复位：工作线程完工 */
    int y0, y1;      /* 地板 / 后处理的行范围 */
    int x0, x1;      /* 墙体的列范围 */
    int phase;       /* 1=地板 2=墙体 3=后处理 */
} Worker;

static Worker g_workers[MAX_PARTS];
static int g_workerCount = 0;   /* 不含主线程 */
static int g_parts = 1;
static volatile LONG g_stop = 0;

/* 亮度 → gamma 校正后的亮度，查表代替逐像素 powf。
   索引是「亮度 × 256」，值域 0..900（对应亮度 0..3.5）。
   地板与墙体都走这张表，gamma 滑块对整个画面生效。 */
#define SHADE_LUT_MAX 1024
static int shadeLut[SHADE_LUT_MAX];

static void RenderFloorBand(int y0, int y1);
static void RenderWallBand(int x0, int x1);
static void RenderPostBand(int y0, int y1);
static DWORD WINAPI WorkerMain(LPVOID param) {
    Worker* w = (Worker*)param;
    for (;;) {
        if (WaitForSingleObject(w->hStart, INFINITE) != WAIT_OBJECT_0) break;
        if (g_stop) break;
        if (w->phase == 1) RenderFloorBand(w->y0, w->y1);
        else if (w->phase == 2) RenderWallBand(w->x0, w->x1);
        else if (w->phase == 3) RenderPostBand(w->y0, w->y1);
        SetEvent(w->hDone);
    }
    return 0;
}

void InitWorkers(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int want = (int)si.dwNumberOfProcessors - 1;
    if (want > MAX_PARTS - 1) want = MAX_PARTS - 1;
    if (want < 0) want = 0;

    g_workerCount = 0;
    g_stop = 0;   /* 允许重复初始化（例如重启线程池） */
    for (int i = 0; i < want; i++) {
        Worker* w = &g_workers[i];
        w->hStart = CreateEvent(NULL, FALSE, FALSE, NULL);
        w->hDone = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!w->hStart || !w->hDone) break;
        w->phase = 0;
        w->hThread = CreateThread(NULL, 0, WorkerMain, w, 0, NULL);
        if (!w->hThread) break;
        g_workerCount++;
    }
    g_parts = g_workerCount + 1;
}

void ShutdownWorkers(void) {
    g_stop = 1;
    for (int i = 0; i < g_workerCount; i++) SetEvent(g_workers[i].hStart);
    for (int i = 0; i < g_workerCount; i++) {
        WaitForSingleObject(g_workers[i].hThread, 1000);
        CloseHandle(g_workers[i].hThread);
        CloseHandle(g_workers[i].hStart);
        CloseHandle(g_workers[i].hDone);
    }
    g_workerCount = 0;
    g_parts = 1;
}

static void SplitRange(int lo, int hi, int part, int parts, int* outLo, int* outHi) {
    int n = hi - lo;
    if (n < 0) n = 0;
    *outLo = lo + n * part / parts;
    *outHi = lo + n * (part + 1) / parts;
}

/* 派活并等所有分片画完；worker 数可以是 0（单线程退化） */
/* 一旦某次等 worker 超时，就永久降级成单线程渲染 */
static int g_workersBroken = 0;

void RunPhase(int phase) {
    int y0, y1;
    if (g_workersBroken) {          /* 不用线程了：整帧主线程画，宁可掉帧不崩 */
        if (phase == 1) { RenderFloorBand(0, renderH); return; }
        if (phase == 2) { RenderWallBand(0, renderW); return; }
        RenderPostBand(0, renderH);
        return;
    }
    for (int i = 0; i < g_workerCount; i++) {
        /* v2.0：三个相位统一按 (workerCount+1) 等分，主线程吃最后一份。
           原来 phase 1 让主线程独画上半屏、8 个 worker 只分下半屏 ——
           实际加速比只有 ~2× 而不是 9×。 */
        if (phase == 1) { SplitRange(0, renderH, i, g_parts, &g_workers[i].y0, &g_workers[i].y1); }
        else if (phase == 2) { SplitRange(0, renderW, i, g_parts, &g_workers[i].x0, &g_workers[i].x1); }
        else { SplitRange(0, renderH, i, g_parts, &g_workers[i].y0, &g_workers[i].y1); }
        g_workers[i].phase = phase;
        SetEvent(g_workers[i].hStart);
    }

    int p = g_workerCount;
    if (phase == 1) {
        /* 主线程吃第 g_workerCount 份（最后一段），和 worker 不重叠 */
        int a0, a1;
        SplitRange(0, renderH, g_workerCount, g_parts, &a0, &a1);
        RenderFloorBand(a0, a1);
    }
    else if (phase == 2) { int x0, x1; SplitRange(0, renderW, p, g_parts, &x0, &x1); RenderWallBand(x0, x1); }
    else { SplitRange(0, renderH, p, g_parts, &y0, &y1); RenderPostBand(y0, y1); }

    for (int i = 0; i < g_workerCount; i++) {
        /* v2.7：超时绝不能再往下走 —— worker 还在往 pixels/zBuffer 写，
           下一相位（甚至 InitPixels 重建 DIB）会跟它并发写同一块缓冲。 */
        if (WaitForSingleObject(g_workers[i].hDone, 2000) != WAIT_OBJECT_0) {
            g_workersBroken = 1;
            break;
        }
    }
    if (g_workersBroken) {
        /* 兜底：这一帧剩下的部分全部在主线程补完 */
        if (phase == 1) RenderFloorBand(0, renderH);
        else if (phase == 2) RenderWallBand(0, renderW);
        else RenderPostBand(0, renderH);
    }
}

/* 亮度 → gamma 校正后的亮度，查表代替逐像素 powf。
   索引是「亮度 × 256」，值域 0..1023（对应亮度 0..4.0）。

   曲线说明：直接用 pow(v, 1/g) 会踩一个坑 ——
   v > 1（过曝区，比如手电近处的地板）时，1/g < 1 会把值「压回来」，
   于是 gamma 调大反而让亮的地方变暗，和「数值越大越亮」的预期相反。

   修复方式：**只动 v > 1 那一段**，v <= 1 保持原曲线不变：
     - v <= 1：out = pow(v, 1/g) × 256      （与原实现完全一致）
     - v >  1：原样输出（过曝部分不再被压暗）
   这样既修好了过曝区被压暗的问题，又不会顺带改变墙体的既有观感 ——
   墙体亮度 lit 通常在 0.1~1.5 之间，那一段曲线必须原封不动。 */
#define SHADE_ONE 256      /* 亮度 1.0 对应的索引 */

void BuildShadeTables(void) {
    float inv = 1.0f / gammaValue;
    for (int i = 0; i < SHADE_LUT_MAX; i++) {
        int o;
        if (i <= SHADE_ONE) {
            float v = (float)i / 256.0f;
            float g = powf(v, inv);
            o = (int)(g * 256.0f + 0.5f);
        } else {
            o = i;   /* 过曝区原样保留 */
        }
        if (o < 0) o = 0;
        if (o > SHADE_LUT_MAX - 1) o = SHADE_LUT_MAX - 1;
        shadeLut[i] = o;
    }
}

/* 地板：棋盘只有两种底色，亮度查表；出口发光只在出口所在格判断 */
static void RenderFloorBand(int y0, int y1) {
    int halfH = renderH / 2;
    int centery = halfH + horizon;
    /* v1.13：真俯仰后不再需要这两个端点射线（逐行按角度求交） */
    (void)0;
    int exitBX = (int)(exitX * 4.0f), exitBY = (int)(exitY * 4.0f);
    int safeBX = (int)(safeRoom.x * 4.0f), safeBY = (int)(safeRoom.y * 4.0f);
    int lightOnF = (flashlightOn && battery > 0.0f);

    /* v1.12：y-shear 之后地板与天花板**不再是镜像关系**（镜像只在 horizon==0 时成立），
       所以这里改成单次全屏通道：每一行按 y 相对地平线判上下，分别采样地板/天花板。
       之前那套"地板行 + 镜像天花板"在抬头时会留下一条黑带，低头时上下颠倒。 */
    for (int y = y0; y < y1; y++) {
        unsigned int* row = pixels + (size_t)y * renderW;
        /* v1.13 真俯仰：该行的视线竖直分量（旋转后） */
        float cyv = (float)(centery - y) / (float)focal;
        float ry = pitchSin + pitchCos * cyv;
        float kk = pitchCos - cyv * pitchSin;
        int isCeil = (ry > 0.0f);
        float t;
        if (isCeil) {
            float den = ry;
            if (den < 0.004f) den = 0.004f;
            t = 0.5f / den;
        } else {
            float den = -ry;
            if (den < 0.004f) den = 0.004f;
            t = 0.5f / den;
        }
        if (t > 60.0f) t = 60.0f;
        /* v1.13b 修正：射线 = F + R·cameraX + U·cameraY，
           其中 R（平面向量）的系数**恒为 1**，不该乘 (cos - cy·sin)。
           上一版把整个水平方向都乘了 kk，pitch 越大横向拉伸越狠 → 鱼眼扭曲。 */
        float stepX = t * planeX * 2.0f / renderW;
        float stepY = t * planeY * 2.0f / renderW;
        float fx = posX + t * kk * dirX - t * planeX;
        float fy = posY + t * kk * dirY - t * planeY;
        float rowDistance = t;

        /* 亮度：环境光 + 手电的平方衰减 */
        float ambF = 1.0f / (1.0f + rowDistance * (lightOnF ? 0.75f : 0.55f));
        float reachF = 1.0f - rowDistance / 5.5f;
        if (reachF < 0.0f) reachF = 0.0f;
        reachF *= reachF;
        extern float smokeDim;
        float shadow = (ambF * 0.28f + (lightOnF ? reachF * 0.95f : 0.0f)) * smokeDim;
        int shadowI = (int)(shadow * 256.0f);
        if (shadowI < 0) shadowI = 0;
        if (shadowI > 1023) shadowI = 1023;

        /* 距离雾（天花板减半：天花板本来就不该被照亮太多） */
        float fogAmt = rowDistance * 0.075f;
        if (fogAmt > 0.8f) fogAmt = 0.8f;
        if (!lightOnF) fogAmt *= 0.22f;
        if (isCeil) fogAmt *= 0.5f;
        unsigned int fogR = (unsigned int)(fogAmt * (float)themeFogR);
        unsigned int fogG = (unsigned int)(fogAmt * (float)themeFogG);
        unsigned int fogB = (unsigned int)(fogAmt * (float)themeFogB);

        /* ==================================================================
         * 2026-11-06（v2.4-rc）：**地板和天花板也要按房间上色**
         * ------------------------------------------------------------------
         * 实测反馈：「还是只有一些发光的墙，其他什么都没有。」
         *
         * 那是必然的 —— 我只给墙面加了补光，**地板没加**。
         * 而第一人称视角里，画面下半部分几乎全是地板；
         * 只让墙发光，结果就是"角落里有一段墙在发亮"，
         * 完全构不成"我走进了一个房间"。
         *
         * 这里用每行的 shadow 预乘一个系数，所以远处/暗处的地板
         * 补光同样会被压下去，不会出现"远处的房间亮得反常"。
         * 成本：每像素一次数组取值 + 一次移位 —— 地板本来就最贵，
         * 这点增量实测无感。
         * ================================================================ */
        int tintMul = (int)(shadow * 256.0f * 1.7f);
        if (tintMul < 0) tintMul = 0;
        if (tintMul > 1024) tintMul = 1024;
        extern CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];
        extern int roomCount;

        for (int x = 0; x < renderW; x++) {
            int cX = (int)fx, cY = (int)fy;
            int tx2 = ((int)((fx - cX) * (float)TEX_WIDTH)) & (TEX_WIDTH - 1);
            int ty2 = ((int)((fy - cY) * (float)TEX_HEIGHT)) & (TEX_HEIGHT - 1);
            tx2 &= ~1; ty2 &= ~1;

            unsigned int color;
            if (!isCeil) {
                int k8 = (shadowI * floorGainI[x]) >> 8;
                if (k8 < 0) k8 = 0;
                else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
                k8 = shadeLut[k8];
                int lvl = k8 >> PREMUL_K8_SHIFT;
                color = premulFloor[floorColorIdx[ty2][tx2]][lvl];

                /* 房间补光：先叠，后面安全屋/出口的特效可以覆盖它 */
                if (roomCount > 0 &&
                    (unsigned)cX < (unsigned)MAP_WIDTH && (unsigned)cY < (unsigned)MAP_HEIGHT) {
                    unsigned char rid = cellMeta[cY][cX].roomId;
                    if (rid != 0) {
                        int t = cellMeta[cY][cX].themeId;
                        if (t < 0 || t >= THEME_COUNT) t = 0;
                        int tr = CellThemeTintR(t), tg = CellThemeTintG(t), tb = CellThemeTintB(t);
                        int cr = (int)((color >> 16) & 0xFFu) + ((tr * tintMul) >> 8);
                        int cg = (int)((color >> 8) & 0xFFu)  + ((tg * tintMul) >> 8);
                        int cb = (int)(color & 0xFFu)         + ((tb * tintMul) >> 8);
                        if (cr > 255) cr = 255;
                        if (cg > 255) cg = 255;
                        if (cb > 255) cb = 255;
                        color = (unsigned int)((cr << 16) | (cg << 8) | cb);
                    }
                }

                /* 安全屋花圃 */
                if (safeRoom.active) {
                    int bx = (int)(fx * 4.0f), by = (int)(fy * 4.0f);
                    int sbx = bx - safeBX, sby = by - safeBY;
                    if (sbx >= -2 && sbx <= 2 && sby >= -2 && sby <= 2) {
                        float d2s = (float)(sbx * sbx + sby * sby) / 8.0f;
                        float gs = 1.0f - d2s;
                        if (gs > 0.0f) {
                            int r2 = (int)(70 * gs), g2 = (int)(48 * gs), b2 = (int)(58 * gs);
                            color = (unsigned int)((r2 << 16) | (g2 << 8) | b2);
                        }
                    }
                }

                /* 出口光池 */
                {
                    int bx = (int)(fx * 4.0f), by = (int)(fy * 4.0f);
                    int dbx = bx - exitBX, dby = by - exitBY;
                    if (dbx >= -3 && dbx <= 3 && dby >= -3 && dby <= 3) {
                        float d2 = (float)(dbx * dbx + dby * dby) / 9.0f;
                        float glow = 1.0f - d2;
                        if (glow > 0.0f) {
                            float k = (shadow * 0.45f + 0.60f) * glow;
                            if (breakerOpen) {
                                int r2 = (int)(40 * k), g2 = (int)(235 * k), b2 = (int)(105 * k);
                                if (r2 > 255) r2 = 255;
                                if (g2 > 255) g2 = 255;
                                if (b2 > 255) b2 = 255;
                                color = (unsigned int)((r2 << 16) | (g2 << 8) | b2);
                            } else {
                                int r2 = (int)(120 * k), g2 = (int)(24 * k);
                                if (r2 > 255) r2 = 255;
                                color = (unsigned int)((r2 << 16) | (g2 << 8));
                            }
                        }
                    }
                }

                /* 雾 + 光球的暖光（简化：只按距离加暖色） */
                if (fogR | fogG | fogB) {
                    unsigned int cr = (color >> 16) & 0xFF, cg = (color >> 8) & 0xFF, cb = color & 0xFF;
                    cr += fogR; if (cr > 255) cr = 255;
                    cg += fogG; if (cg > 255) cg = 255;
                    cb += fogB; if (cb > 255) cb = 255;
                    color = (cr << 16) | (cg << 8) | cb;
                }
            } else {
                int cl = ceilTab[x] >> PREMUL_K8_SHIFT;
                if (cl < 0) cl = 0;
                else if (cl > PREMUL_LEVELS - 1) cl = PREMUL_LEVELS - 1;
                color = premulCeil[ceilColorIdx[ty2][tx2]][cl];
            }

            row[x] = color;
            fx += stepX;
            fy += stepY;
        }
    }
}

/* 墙体：DDA 求交后整列查表，内层只有一次乘法和一次写 */
static void RenderWallBand(int x0, int x1) {
    int halfH = renderH / 2;
    for (int x = x0; x < x1; x++) {
        float cameraX = 2.0f * x / (float)renderW - 1.0f;
        float rayDirX = dirX + planeX * cameraX;
        float rayDirY = dirY + planeY * cameraX;

        int mapX = (int)posX, mapY = (int)posY;
        float deltaDistX = (rayDirX == 0) ? 1e30f : fabsf(1.0f / rayDirX);
        float deltaDistY = (rayDirY == 0) ? 1e30f : fabsf(1.0f / rayDirY);
        float sideDistX, sideDistY;
        int stepX, stepY, hit = 0, side = 0;

        if (rayDirX < 0) { stepX = -1; sideDistX = (posX - mapX) * deltaDistX; }
        else { stepX = 1; sideDistX = (mapX + 1.0f - posX) * deltaDistX; }
        if (rayDirY < 0) { stepY = -1; sideDistY = (posY - mapY) * deltaDistY; }
        else { stepY = 1; sideDistY = (mapY + 1.0f - posY) * deltaDistY; }

        while (hit == 0) {
            if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
            else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
            if (mapX < 0 || mapX >= MAP_WIDTH || mapY < 0 || mapY >= MAP_HEIGHT) break;
            if (worldMap[mapY][mapX] > 0) hit = 1;
        }

        float perpWallDist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
        if (perpWallDist < 0.1f) perpWallDist = 0.1f;
        zBuffer[x] = perpWallDist;

        /* v1.13 真俯仰：墙的下沿(v=-眼高)与上沿(v=1-眼高)各自旋转后投影。
           同一列 z 不变 → 屏幕上仍是线性，贴图线性采样依然正确。 */
        int lineHeight = (int)(renderH / perpWallDist);
        (void)lineHeight;
        const float eyeH = 0.5f;
        float vb2 = (-eyeH) * pitchCos - perpWallDist * pitchSin;
        float db2 = (-eyeH) * pitchSin + perpWallDist * pitchCos;
        float vt2 = (1.0f - eyeH) * pitchCos - perpWallDist * pitchSin;
        float dt2 = (1.0f - eyeH) * pitchSin + perpWallDist * pitchCos;
        if (db2 < 0.05f) db2 = 0.05f;
        if (dt2 < 0.05f) dt2 = 0.05f;
        int yBottom = (int)((float)halfH - vb2 * (float)focal / db2);
        int yTop    = (int)((float)halfH - vt2 * (float)focal / dt2);
        if (yBottom <= yTop) continue;
        int startF = yTop;
        int drawStart = yTop < 0 ? 0 : yTop;
        int drawEnd = yBottom > renderH ? renderH : yBottom;
        if (drawEnd <= drawStart) continue;

        float wallX = (side == 0) ? (posY + perpWallDist * rayDirY) : (posX + perpWallDist * rayDirX);
        wallX -= floorf(wallX);
        int texX = (int)(wallX * (float)TEX_WIDTH);
        if (texX < 0) texX = 0;
        if (texX > TEX_WIDTH - 1) texX = TEX_WIDTH - 1;
        if ((side == 0 && rayDirX > 0) || (side == 1 && rayDirY < 0)) texX = TEX_WIDTH - texX - 1;

        float span = (float)(yBottom - yTop);
        if (span < 1.0f) span = 1.0f;
        float step = (float)TEX_HEIGHT / span;
        /* v2.0：每像素吃掉的 texel 数 → mip 级。step>1.5 说明在缩小，退到低一级 */
        int mipLod = 0;
        {
            float st = step;
            while (st > 1.5f && mipLod < MIP_LEVELS - 1) { st *= 0.5f; mipLod++; }
        }
        const unsigned int* mipSrc = mipChain[mipLod];
        int mipW = TEX_WIDTH >> mipLod;
        int mipMaskY = (TEX_HEIGHT >> mipLod) - 1;
        int mipShift = mipLod;
        float texPos = (float)(drawStart - startF) * step;

        /* v1.9 光照：
           ① 距离衰减改成 1/(1+d*k) 曲线 —— 近处亮、远处平滑收黑，没有硬断崖
           ② 距离雾：远处叠加一点冷色（青蓝），空间感立刻出来
           ③ 手电光斑用 smoothstep 软边，不再是硬切一刀 */
        /* v1.10：手电削弱。
           原来开灯只有 1/(1+d*0.22) 的衰减 —— 10 格外还亮着，等于整张地图都看得见。
           现在拆成两部分：
             环境光 amb：永远很暗（开灯 0.42 倍，关灯更暗）
             手电项   ：有效距离 8 格，按 (1-d/8)² 衰减，超出即 0
           所以光照范围由"手电照到哪"决定，不再是"整条走廊"。 */
        int lightOnW = (flashlightOn && battery > 0.0f);
        float amb = 1.0f / (1.0f + perpWallDist * (lightOnW ? 0.70f : 0.50f));
        /* v1.12：手电换成和荧光棒同一套光效 ——
           锥形（横向 flashTab）× 平方距离衰减，再叠一层暖色加色。
           原来只有"乘法提亮"，画面偏冷、也不像有个灯泡在照。 */
        float reach = 1.0f - perpWallDist / (9.0f*BenchLightRangeMul());
        if (reach < 0.0f) reach = 0.0f;
        reach *= reach;
        float flash = flashTab[x];
        float torch = lightOnW ? (flash * reach * 0.62f) : 0.0f;
        extern float smokeDim;
        float lit = (amb * 0.26f + torch) * smokeDim;
        if (side == 1) lit *= 0.62f;

        float cone = flash - 0.35f;
        if (cone < 0.0f) cone = 0.0f;
        cone = cone * cone * (3.0f - 2.0f * cone > 1.0f ? 1.0f : 3.0f - 2.0f * cone);   /* smoothstep */

        /* 距离雾（冷色，加法叠加，零额外采样） */
        float fog = perpWallDist * 0.085f;
        if (fog > 0.85f) fog = 0.85f;
        if (!lightOnW) fog *= 0.25f;    /* 留一点，纯粹压黑会没有纵深 */

        /* 光斑也受有效距离约束：远处不许再加亮 */
        /* 光球照明：命中点离任一荧光棒越近越亮（暖色） */
        float hx = posX + rayDirX * perpWallDist;
        float hy = posY + rayDirY * perpWallDist;
        float fl = FlareLightAt(hx, hy);
        lit += fl;
        if (lit > 1.35f) lit = 1.35f;

        /* v1.12：手电改**白光**（略偏冷），和荧光棒的暖橙拉开差别 */
        int addR = (int)((cone * reach * (float)(themeLitR + 24) * 0.42f + fog * (float)themeAmbR
                          + fl * 210.0f + torch * (float)themeLitR) * smokeDim);
        int addG = (int)((cone * reach * (float)(themeLitG + 24) * 0.42f + fog * (float)themeAmbG
                          + fl * 150.0f + torch * (float)themeLitG) * smokeDim);
        int addB = (int)((cone * reach * (float)(themeLitB + 24) * 0.42f + fog * (float)themeAmbB
                          + fl * 78.0f  + torch * (float)themeLitB) * smokeDim);

        /* ==================================================================
         * 2026-11-06（v2.4-rc C4）：**主题房间的补光色**
         * ------------------------------------------------------------------
         * 计划里明确"不做第二套纹理集"（双倍纹理常驻内存），
         * 所以房间的辨识度靠**墙面补光**表现：
         *   同一张贴图，但这个房间的光偏冷/偏橙/偏紫。
         *
         * 成本：**每列一次**（不是每像素），普通区直接返回 0 跳过。
         * 这也是为什么不做地板 —— 地板是逐像素的，加查表会掉帧。
         * ================================================================ */
        {
            extern int CellTintRGB(int, int, int*, int*, int*);
            int tr = 0, tg = 0, tb = 0;
            if (CellTintRGB(mapX, mapY, &tr, &tg, &tb)) {
                float k = smokeDim;          /* 浓烟里补光同样被压 */
                addR += (int)(tr * k);
                addG += (int)(tg * k);
                addB += (int)(tb * k);
            }
        }
        int k8 = (int)(lit * 256.0f);
        if (k8 < 0) k8 = 0;
        else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
        k8 = shadeLut[k8];      /* gamma 校正，和地板共用同一张表 */

        unsigned int* dst = pixels + (size_t)drawStart * renderW + x;

        /* v1.9：墙脚 / 墙顶 AO —— 沿墙面上下各压暗 16 像素，
           墙角有了"厚度"，不再是均匀一片贴图 */
        int aoBot = drawEnd - 16;
        int aoTop = drawStart + 16;

        /* v2.1：竖直方向的光斑。
           原来亮度只按 x 算（flashTab[x]），同一列从上到下完全一样，
           整面墙就是一块均匀灰板 —— 贴纸感的真正来源。
           这里预生成 64 项竖直权重（中间亮、上下收），每像素多一次查表。 */
        int vLut[64];
        {
            for (int i = 0; i < 64; i++) {
                float v = ((float)i + 0.5f) / 64.0f;
                float dv = v - 0.46f;                    /* 略偏上：视线落点在下半 */
                float fall = 1.0f - dv * dv * 3.2f;      /* 中间 1，两端 <0 */
                if (fall < 0.18f) fall = 0.18f;
                vLut[i] = (int)(fall * 256.0f);
            }
        }

        for (int y = drawStart; y < drawEnd; y++) {
            int texY = ((int)texPos) & (TEX_HEIGHT - 1);
            texPos += step;
            /* v2.0：走了 mip 就按低位采样（lod 0 时与原路径完全等价） */
            unsigned int t = mipSrc[((texY >> mipShift) & mipMaskY) * mipW + (texX >> mipShift)];
            int vy = (int)(((float)(y - drawStart) * 64.0f) / (float)(drawEnd - drawStart + 1));
            if (vy < 0) vy = 0;
            if (vy > 63) vy = 63;
            int k8v = (k8 * vLut[vy]) >> 8;              /* 竖直光斑 */
            k8v += bump[texY][texX];                     /* v2.0：凹凸 */
            if (k8v < 0) k8v = 0;
            if (k8v > 1023) k8v = 1023;
            int r = (int)(((t >> 16) & 0xFF) * k8v) >> 8;
            int g = (int)(((t >> 8) & 0xFF) * k8v) >> 8;
            int b = (int)((t & 0xFF) * k8v) >> 8;

            r += addR; g += addG; b += addB;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            *dst = (unsigned int)((r << 16) | (g << 8) | b);
            dst += renderW;
        }

        /* v1.9：AO 从逐像素判断里挪出来，改成两段小循环。
           热点循环里少两次比较 + 三次乘法，而这两段只有 32 行。 */
        {
            int yb0 = aoBot > drawStart ? aoBot : drawStart;
            unsigned int* p = pixels + (size_t)yb0 * renderW + x;
            for (int y = yb0; y < drawEnd; y++, p += renderW) {
                int f = 256 - (y - aoBot) * 13;
                if (f < 116) f = 116;
                unsigned int c = *p;
                int rr = (int)((c >> 16) & 0xFF) * f >> 8;
                int gg = (int)((c >> 8) & 0xFF) * f >> 8;
                int bb = (int)(c & 0xFF) * f >> 8;
                *p = (unsigned int)((rr << 16) | (gg << 8) | bb);
            }
            int yet = aoTop < drawEnd ? aoTop : drawEnd;
            p = pixels + (size_t)drawStart * renderW + x;
            for (int y = drawStart; y < yet; y++, p += renderW) {
                int f = 256 - (aoTop - y) * 11;
                if (f < 140) f = 140;
                unsigned int c = *p;
                int rr = (int)((c >> 16) & 0xFF) * f >> 8;
                int gg = (int)((c >> 8) & 0xFF) * f >> 8;
                int bb = (int)(c & 0xFF) * f >> 8;
                *p = (unsigned int)((rr << 16) | (gg << 8) | bb);
            }
        }
    }
}

/* 后处理：低理智红边 + 回血白闪，按行并行
   这一段只在理智 < 50 时才跑，但跑起来是全屏逐像素的。
   优化点：把「乘强度」和「除 300」从每像素挪到每帧一次（各 256 项的查表），
   顺便用递增代替乘 3 来定位噪点。 */
static void RenderPostBand(int y0, int y1) {
    int intensity = 0;
    if (vignetteOn && sanity < 50.0f) {
        /* v3.0：**红晕封顶 60%** ——
           用户截图：6% 理智时整屏红噪点，「几乎看不见路」。
           原来满强度 255（88%），恐怖氛围变成了致盲。 */
        intensity = (int)((50.0f - sanity) / 50.0f * 255.0f * 0.60f);
        if (intensity > 153) intensity = 153;
        if (intensity < 0) intensity = 0;
    }
    int heal = (healFlash > 0.0f);
    /* 低理智红边的噪点固定相位。旧版按 61Hz 改变整张噪点图，
       会把本来应该是渐晕的效果看成全屏闪烁/抽搐；红边强度仍会
       随理智变化，但纹理本身保持稳定。 */
    int frameSeed = 17;

    /* 每帧一次的查表：alpha = mask*intensity>>8，nz = (noise-20)*intensity/300 */
    unsigned char alphaLut[256];
    signed char noiseLut[256];
    if (intensity > 0) {
        for (int i = 0; i < 256; i++) {
            alphaLut[i] = (unsigned char)((i * intensity) >> 8);
            int nz = (int)noiseTab[i] - 20;
            noiseLut[i] = (signed char)(nz * intensity / 300);
        }
    }

    for (int y = y0; y < y1; y++) {
        unsigned int* row = pixels + (size_t)y * renderW;
        if (intensity > 0) {
            const unsigned char* mask = vignetteMask + (size_t)y * renderW;
            /* 噪点索引必须精确等于 (x*3 + y*5 + frameSeed) & 255。
               不能用「每列 +3」代替 —— x*3 超过 255 后要回绕，
               递增法在回绕点对不齐，噪点会整片错位。
               所以这里保留 x*3 的乘法，但把「乘强度/除 300」挪到查表。 */
            const int noiseBase = (y * 5 + frameSeed) & 255;
            for (int x = 0; x < renderW; x++) {
                int maskVal = mask[x];
                if (maskVal == 0) continue;
                int alpha = alphaLut[maskVal];
                if (alpha == 0) continue;
                unsigned int dst = row[x];
                int r = (int)((dst >> 16) & 0xFF), g = (int)((dst >> 8) & 0xFF), b = (int)(dst & 0xFF);
                r = r + (((255 - r) * alpha) >> 8);
                g = g - ((g * alpha) >> 8);
                b = b - ((b * alpha) >> 8);
                {
                    int nz = noiseLut[(x * 3 + noiseBase) & 255];
                    r += nz; g += nz; b += nz;
                    if (r < 0) r = 0; else if (r > 255) r = 255;
                    if (g < 0) g = 0; else if (g > 255) g = 255;
                    if (b < 0) b = 0; else if (b > 255) b = 255;
                }
                row[x] = (unsigned int)((r << 16) | (g << 8) | b);
            }
        }
        if (heal) {
            for (int x = 0; x < renderW; x++) {
                unsigned int dst = row[x];
                int r = (int)((dst >> 16) & 0xFF), g = (int)((dst >> 8) & 0xFF), b = (int)(dst & 0xFF);
                row[x] = (unsigned int)(((r + (255 - r) * 3 / 10) << 16) |
                                        ((g + (255 - g) * 3 / 10) << 8) |
                                        (b + (255 - b) * 3 / 10));
            }
        }
    }
}

/* v2.1：落地阴影。全项目只有抛出的光球有假阴影，怪物/箱子全都没有，
   于是所有东西都像"浮在地面上"。这里在精灵脚下压一个扁椭圆。 */
static void DrawGroundShadow(int cx, int cyFloor, int halfW, float ty) {
    if (halfW < 2) return;
    int rx = halfW;
    int ry = halfW / 3;
    if (ry < 1) ry = 1;
    int top = cyFloor - ry;
    for (int y = -ry; y <= ry; y++) {
        int py = top + y + ry;
        if (py < 0 || py >= renderH) continue;
        if ((float)py < 0.0f) continue;
        for (int x = -rx; x <= rx; x++) {
            int px = cx + x;
            if (px < 0 || px >= renderW) continue;
            float u = (float)(x * x) / (float)(rx * rx) + (float)(y * y) / (float)(ry * ry);
            if (u > 1.0f) continue;
            /* v2.0 修复：阴影原来不查深度，墙后的精灵阴影会透出来
               （用户实测「能隔墙看到影子」）。 */
            if (ty >= zBuffer[px]) continue;
            float a = 0.42f * (1.0f - u * 0.55f);
            PutPixelBlend(px, py, 0x00000000u, a);
        }
    }
}

/* ------------------------------------------------------------------ */
/* v2.1 第一人称手持模型                                                */
/*   原来玩家手上什么都没有：准星指向哪里、手上拿的是什么全靠想象。      */
/*   这里是纯手工绘制的：前臂 + 拳头 + 手持物，带走路摆动和开火后坐。     */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* v2.1 第一人称手持模型（烘焙版）                                       */
/*                                                                     */
/* 旧版是每帧用 5~10 个 VMRect 现拼矩形 —— 没有抗锯齿、没有细节、没有     */
/* 动画帧。新版走 Doom/Build 的正统做法：启动时把武器烘成 128×128 位图，   */
/* 运行时只做「双线性 blit + 亮度查表」。                                */
/* ------------------------------------------------------------------ */
static void DrawViewModel(void) {
    /* v3.0：**藏进柜子还举着扳手**是最出戏的一处 ——
       无战斗改造后第一人称模型不该再出现，藏身时更要藏起来。 */
    {
        extern int hiddenIn;
#if NO_COMBAT
        /* ===== v2.4.62 **这就是"左手看不到"的根因** =====
           原来写的是 `if (hiddenIn >= 0 || 1) return;` ——
           `|| 1` 让这个条件**永远成立**，于是 DrawViewModel()
           每一帧都在第一行返回：右手扳手、左手投掷物**都没画过**。
           用户反复说「左手没有东西」，根因在这里，不在模型。 */
        if (hiddenIn >= 0) return;   /* 无战斗：只在藏身时不画 */
#else
        if (hiddenIn >= 0) return;
#endif
    }

    if (!pixels || !SpritesReady()) return;

    /* 走路摆动 + 开火后坐 */
    float sway = sinf(bobPhase) * (float)renderW * 0.006f;
    /* v3.0：lift 原来用 |sin(φ/2)|，纵向频率只有横向的 1/2 ——
       手持模型会「横着摆两下、竖向才起一下」，而且 |sin| 谷底有速度拐点。
       改成 |sin(φ)|，两轴同频，走路手感一致（建模审查 #5）。 */
    float lift = fabsf(sinf(bobPhase)) * (float)renderH * 0.007f;
    float kick = weaponAtkCharge * (float)renderH * 0.010f
               + muzzleFlash * (float)renderH * 0.016f;

    int size = (int)((float)renderH * 0.54f);
    if (size < 64) size = 64;

    /* ==================================================================
     * 2026-11-05 用户要求：**只留扳手建模**
     * ------------------------------------------------------------------
     * 这里原来按 curWeapon 换三套手持模型：
     *   WPN_MELEE  → SPR_WRENCH   （摇把扳手）
     *   WPN_RANGED → SPR_PISTOL   （信号枪，已下线的武器系统）
     *   WPN_SPELL  → SPR_SIGIL    （符印/法术发射器，已下线的武器系统）
     *
     * 而每日模式会 `curWeapon = dailySeed % WPN_COUNT`、战役会
     * `curWeapon = stage % WPN_COUNT` —— 于是**随机抽到法术时，
     * 屏幕上会卡出那个发射器建模**（用户报告）。
     *
     * NO_COMBAT 之后本作只有扳手一件工具，所以直接钉死。
     * SPR_PISTOL / SPR_SIGIL 两个精灵与烘焙函数一并删除。
     * ================================================================== */
    const int sid = SPR_WRENCH;
    float light = 0.85f;   /* 挥击动画会按帧微调亮度，所以不能是 const */

    int cx = (int)((float)renderW * 0.72f + sway);
    int cy = (int)((float)renderH * 1.04f + lift - kick);
    /* ===== v2.4.83 **五帧挥击动画** =====
       用户要求「动画多来几帧」。
       五帧足够读出「起手 → 后摆 → 挥到 → 收势 → 回位」，
       再多在这个尺度上（69px 手持模型）已经分辨不出来。

         帧 0  起手：向后下、缩到 92%，压暗
         帧 1  后摆：最大后移、最小（88%）
         帧 2  挥到：**最前、最大（112%）、最亮** ← 命中在这一帧
         帧 3  收势：稍微回收、105%
         帧 4  回位：接近原位、98%
    */
    {
        extern float wrenchSwingT;
        if (wrenchSwingT > 0.0f) {
            float prog = 1.0f - wrenchSwingT / 0.14f;
            if (prog < 0.0f) prog = 0.0f;
            if (prog > 1.0f) prog = 1.0f;
            int fr = (int)(prog * 5.0f);
            if (fr > 4) fr = 4;

            /* 每帧的偏移（-1 后 / +1 前）与缩放/亮度 */
            static const float kDx[5] = { -0.020f, -0.052f,  0.048f,  0.022f,  0.004f };
            static const float kDy[5] = {  0.022f,  0.050f, -0.036f, -0.012f,  0.002f };
            static const int   kSz[5] = { 92,      88,      112,     105,      98 };
            static const float kLi[5] = { 0.90f,   0.82f,   1.14f,   1.04f,    0.99f };

            cx += (int)((float)renderW * kDx[fr]);
            cy += (int)((float)renderH * kDy[fr]);
            size = size * kSz[fr] / 100;
            light *= kLi[fr];
        }
    }
    int px = cx - size / 2;
    int py = cy - size;
    DrawSprite(sid, px, py, size, size, light, 1.0f, 0, 0.0f);

    /* 开火瞬间在武器前端补一团光 */
    /* 2026-11-05：**没有枪了** —— 枪口闪光永远不画（原来按 curWeapon 判，
   而 curWeapon 会被每日/战役随机成法术，于是偶尔闪一下不存在的光）。 */
    if (0) {
        int gx = cx;
        int gy = py + (int)((float)size * 0.18f);
        int gr = (int)((float)size * 0.10f * (0.5f + muzzleFlash * 0.5f));
        if (gr < 2) gr = 2;
        for (int y = -gr; y <= gr; y++) {
            int yy = gy + y;
            if (yy < 0 || yy >= renderH) continue;
            for (int x = -gr; x <= gr; x++) {
                int xx = gx + x;
                if (xx < 0 || xx >= renderW) continue;
                float d2 = (float)(x * x + y * y) / (float)(gr * gr);
                if (d2 >= 1.0f) continue;
                PutPixelAdd(xx, yy, 0x00FFB060u, (1.0f - d2) * muzzleFlash * 0.8f);
            }
        }
    }

    /* 左手：托着当前选中的投掷物 */
    {
        extern int flares, bottles, smokeBombs, gluePots, echoBolts, decoys;
        /* v6 **槽位表必须和 ITEM_SLOT_COUNT(6) 一致** ——
           原来用旧的 8 个变量初始化 6 元素数组：多出初值被丢弃，
           索引整体错位（槽 2..5 读到砖头/酸液/燃烧瓶等已删除物品）。 */
        int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs,
                                        gluePots, echoBolts, decoys };
        int sel = throwSelected;
        if (sel >= 0 && sel < ITEM_SLOT_COUNT && counts[sel] > 0) {
            /* v2.9：按报告重算 —— 原来 100px 大、落进枪模框内、亮度 0.55，
               结果是"更近的左手物体反而更小更暗"，而且底边被屏幕裁掉 9px。
               现在：69px、x=0.40、y=0.95，落在资源条右边、枪模左边。 */
            int isize = (int)((float)renderH * 0.15f);
            if (isize < 24) isize = 24;
            /* v2.6：原来在 renderW*0.10（最左边），被 ENERGY/LIGHT/RUN
               三条资源条整个压住。挪到中间偏右、手电筒 ON 文字的右边。 */
            int ix = (int)((float)renderW * 0.40f - sway);
            int iy = (int)((float)renderH * 0.95f + lift - kick * 0.6f);
            /* v3.0：亮度 0.75 → 0.92（枪是 0.85~1.0），alpha 0.95 → 1.0。
               注释写着"亮一点"但实际比枪暗，同一只手的两个物体不该有明暗断层
               （建模审查 #12）。 */
            /* ===== v2.4.59 **手持模型映射表** =====
               原来是 `SPR_ITEM_FLARE + sel` 直接顺序对应 ——
               但槽位是 FLARE/SLING/SMOKE/GLUE/ECHO/DECOY，
               精灵却是 FLARE/BOTTLE/BRICK/ACID/SMOKE/FIRE，
               于是「烟雾弹拿的是砖头」「回声弹拿的是烟雾弹」。
               现在用显式表，每个槽位拿到对的模型。 */
            static const int slotSpr[ITEM_SLOT_COUNT] = {
                SPR_ITEM_FLARE, SPR_ITEM_SLING, SPR_ITEM_SMOKE,
                SPR_ITEM_GLUE,  SPR_ITEM_ECHO,  SPR_ITEM_DECOY
            };
            DrawSprite(slotSpr[sel], ix, iy - isize, isize, isize,
                       0.92f, 1.0f, 0, 0.0f);
        }
    }
}

void Render3D(void) {
    int halfH = renderH / 2;

    /* 只清上半屏（下半会被地板整片覆盖） */
    memset(pixels, 0, (size_t)(halfH + 1) * renderW * sizeof(unsigned int));

    /* 手电筒：开着是横向聚光，关掉/没电就整体压暗；电量低于 25% 会闪 */
    float flick = 1.0f;
    if (flashlightOn && battery > 0.0f && battery < 25.0f) {
        float t = (float)g_timeSec;
        float wobble = sinf(t * 7.5f) * sinf(t * 2.1f);      /* 慢速拍频，不会跟帧率打架 */
        flick = 0.78f + 0.22f * wobble;
        if (flick < 0.45f) flick = 0.45f;
    }
    int lightOn = (flashlightOn && battery > 0.0f);
    for (int x = 0; x < renderW; x++) {
        float f;
        if (lightOn) {
            /* 中心特别亮：核心 1.15 倍，边缘 0.55，用平方做成聚光斑 */
            float t = fabsf(x - renderW * 0.5f) / (renderW * 0.5f);
            if (t > 1.0f) t = 1.0f;
            float core = 1.0f - t * t * 0.75f;
            f = 0.40f + core * 0.75f;      /* 0.40 .. 1.15 */
            f *= flick;
        } else {
            /* v1.9：手电熄了就该「几乎看不见」。
               原来 0.32 太亮 —— 关灯还能看清走廊，手电就没意义了。 */
            f = 0.13f;   /* 0.09 太黑，抬一点：贴脸能看清，远处仍黑 */
        }
        flashTab[x] = f;
        float cone = f > 0.40f ? (f - 0.40f) / 0.75f : 0.0f;
        /* 基础值交给逐行的 shadow 决定（含有效距离），这里只加手电中心光斑 */
        floorGain[x] = 1.0f + cone * 1.10f;
        {
            int gi = (int)(floorGain[x] * 256.0f);
            if (gi < 0) gi = 0;
            if (gi > 1023) gi = 1023;
            floorGainI[x] = gi;
        }
        /* v2.1：天花板原来最亮只有 7/255（乘完贴图），上半屏是死黑。
           抬到 0.55 基数 + 150 上限，再乘上限乘的距离雾。 */
        ceilTab[x] = (int)((0.55f + cone * 0.75f) * 150.0f);
    }

    RunPhase(1);
    RunPhase(2);
    RenderSprites();
    DrawViewModel();                 /* v2.1 第一人称手持模型 */
    if ((vignetteOn && sanity < 50.0f) || healFlash > 0.0f) RunPhase(3);
}
/* 小地图（在内部渲染分辨率上绘制）
   两种形态：
     角落小窗（默认）：cellSize 固定 8 -> 24*8 = 192px 见方，
                       贴在右下角，不挡视野，可以边走边看。
     全屏：cellSize 按屏幕大小算，铺满中央，用来规划路线。
   之前只有全屏一种形态 —— 打开就得停下，关掉才看得见游戏，
   失去了「导航」的意义。 */
#define MAP_CORNER_CELL 8

/* v3.0：**全屏地图的图例** ——
   玩家实测：「绿点是我吗？中间青点是谁？两个黄点是什么？出口在哪？」
   原来一个标注都没有，只能靠猜。 */
static void DrawMapLegend(void) {
    extern int winW, winH;
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
    extern int g_lang;
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
        extern float echoMarkX[], echoMarkY[], echoMarkT[];
        extern int   echoMarkActive[], echoMarkKind[];
        extern float gameTime;
        int flash = ((int)(gameTime * 6.0f) & 1);   /* 宝箱怪红闪：每秒 3 次 */
        for (int i = 0; i < 24; i++) {
            if (!echoMarkActive[i]) continue;
            if (echoMarkT[i] <= 0.0f) continue;
            int mx = (int)echoMarkX[i], my = (int)echoMarkY[i];
            if (mx < 0 || mx >= MAP_WIDTH || my < 0 || my >= MAP_HEIGHT) continue;
            int px2 = mapStartX + mx * cellSize + cellSize / 2;
            int py2 = mapStartY + my * cellSize + cellSize / 2;
            int kind = echoMarkKind[i];

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
        extern float RunUpgMapDrainPct(void);
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

/* ------------------------------------------------------------------ */
/* 逻辑更新（固定步长，dt = LOGIC_STEP）                                */
/* ------------------------------------------------------------------ */

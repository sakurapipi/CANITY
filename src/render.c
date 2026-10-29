/*
 * Canity — 渲染层实现：像素缓冲 / 贴图与查表 / 多线程光栅 / 3D / 精灵 / 地图
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "render.h"
#include "bench.h"
#include "weapons.h"
#include "extras.h"
#include "sprites.h"
#include "game.h"
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


static void DrawBakedProp(int spr, float wx, float wy, float halfH, float lightK);   /* v2.1 */

static SpriteBox ProjectSprite(float wx, float wy, float lift, float halfH) {
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
    int hgt = (int)(unit * halfH);
    int yTop = cyFloor - hgt;

    s.ty = ty;
    s.x0 = cx - half; if (s.x0 < 0) s.x0 = 0;
    s.x1 = cx + half; if (s.x1 >= renderW) s.x1 = renderW - 1;
    s.y0 = yTop + (int)(unit * lift); if (s.y0 < 0) s.y0 = 0;
    s.y1 = cyFloor; if (s.y1 >= renderH) s.y1 = renderH - 1;
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
    float d = lightOn ? 1.0f / (1.0f + dist * 0.20f)
                      : 1.0f / (1.0f + dist * 0.45f);
    if (d < 0.10f) d = 0.10f;      /* 贴脸的东西别全黑 */
    if (d > 1.00f) d = 1.00f;
    return d;
}

static void DrawCrateSprite(int i) {
    if (i < 0 || i >= containerCount) return;
    Container* c = &containers[i];
    if (c->searched) {
        DrawBakedProp(SPR_PROP_CRATE, c->x, c->y, 0.34f, 0.55f);
    } else {
        DrawBakedProp(SPR_PROP_CRATE, c->x, c->y, 0.42f, 1.0f);
    }
}



/* v3.0：藏身点渲染。
   原来全项目 render.c/ui.c 对 hideSpots 零引用 —— 玩家根本看不见
   衣柜/床底/桌下，只有按下 E 之后闪 1.6 秒的一行字。
   这里复用箱子的精灵，缩小 + 压暗，和真箱子区分开。 */
static void DrawHideSpotSprite(int i) {
    if (i < 0 || i >= hideSpotCount) return;
    HideSpot* h = &hideSpots[i];
    if (!h->active) return;
    /* v3.0：**换成保险箱精灵 + 三种明显不同的高度** ——
       原来三种都用箱子的精灵，只差缩放（1.0/0.78/0.62），
       和真箱子只差 30% 大小，玩家分不清哪个能保命（体验评审）。
       现在：衣柜最高、床底最矮、桌下居中，且统一压暗成青灰色，
       和暖色木箱一眼区分。 */
    float k = (h->kind == 0) ? 1.00f : (h->kind == 1 ? 0.55f : 0.75f);
    DrawBakedProp(SPR_PROP_SAFE, h->x, h->y, 0.34f, 0.62f * k);
}
static void DrawBreakerSprite(void) {
    if (!breaker.active) return;
    float k = breakerOpen ? 1.15f : 0.95f;
    DrawBakedProp(SPR_PROP_BREAKER, breaker.x, breaker.y, 0.52f, k);
}


/* ------------------------------------------------------------------ */

/* v2.1：场景道具统一走烘焙精灵（原来是 relX/relY 硬编码的字符画） */
static void DrawBakedProp(int spr, float wx, float wy, float halfH, float lightK) {
    SpriteBox s = ProjectSprite(wx, wy, 0.0f, halfH);
    if (!s.ok) return;
    int w = s.x1 - s.x0, h = s.y1 - s.y0;
    if (w < 2 || h < 2) return;
    int size = (w > h) ? w : h;
    int x = (s.x0 + s.x1) / 2 - size / 2;
    int y = s.y1 - size;
    float light = SpriteShade(s.ty, 0.12f);
    DrawGroundShadow((s.x0 + s.x1) / 2, s.y1, (s.x1 - s.x0) / 3, s.ty);
    DrawSprite(spr, x, y, size, size, light * lightK, 1.0f, 1, s.ty);
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
    int isMain = (spr == SPR_MON_HUNTER || spr == SPR_MON_LIGHT || spr == SPR_MON_LEECH);
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
        int base = (spr >= SPR_MON_HUNTER && spr < SPR_MON_HUNTER + 4) ? SPR_MON_HUNTER :
                   (spr >= SPR_MON_LIGHT  && spr < SPR_MON_LIGHT  + 4) ? SPR_MON_LIGHT :
                   (spr >= SPR_MON_LEECH  && spr < SPR_MON_LEECH  + 4) ? SPR_MON_LEECH : spr;
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

static void DrawBeastSprite(Monster* m, unsigned int eyeColor) {
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
    int spr = (eyeColor == 0x00c8f0ffu) ? SPR_MON_LIGHT : SPR_MON_HUNTER;
    float halfH = 0.86f;
    float lightK = 1.0f;
    unsigned int tint = 0;
    float tintMix = 0.0f;

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

static void DrawMonsterSprite(void) { DrawBeastSprite(&monster, 0x00ff4020u); }

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
static void DrawLightBeastSprite(void) { DrawBeastSprite(&lightBeast, 0x00c8f0ffu); }

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
    DrawOrbSprite(thrown[idx].x, thrown[idx].y, thrown[idx].z,
                  0.17f, 0x00fff8e0u, 0x00ff9050u, 1.00f);
}

/* ---------------- v1.19 投掷物建模 ----------------
   砖头 = 横放的矩形块；酸瓶 = 细长瓶身 + 瓶颈；烟雾弹 = 罐体；
   玻璃瓶沿用小球（它本来就是飞行的碎玻璃反光）。 */
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
    if (kind == 2) {
        /* 砖头：0.34 格宽 × 0.16 格高的矩形 */
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
    spriteLight = 1.0f;      /* v2.1：碎片是自发光的 */

    for (int i = 0; i < MAX_SHOTS * SHARD_COUNT; i++) {
        if (!shards_debris[i].active) continue;
        SpriteBox s = ProjectSprite(shards_debris[i].x, shards_debris[i].y, shards_debris[i].z / 10.0f, 0.05f);
        if (!s.ok) continue;

        int cx = (s.x0 + s.x1) / 2;
        int cy = (s.y0 + s.y1) / 2;
        float lifeRatio = shards_debris[i].life / MAX_SHARD_LIFE;
        float alpha = 0.7f * lifeRatio;
        if (alpha < 0.05f) continue;

        /* v2.1：碎片按 tint 分色（玻璃 / 血 / 尘），尺寸随距离缩放 */
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
    DrawBakedProp(SPR_PROP_SAFE, safeRoom.x, safeRoom.y, 0.30f, 1.0f);
}


static void DrawExitBeamSprite(void) {
    if (!breakerOpen) {
        /* 没通电：门只画个暗框 */
        DrawBakedProp(SPR_PROP_EXIT, exitX, exitY, 0.62f, 0.22f);
        return;
    }
    DrawBakedProp(SPR_PROP_EXIT, exitX, exitY, 1.05f, 1.0f);
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
    /* 用两次不同大小的半透明方块模拟光晕（软件光栅下最省的画法） */
    DrawBakedProp(SPR_PROP_SAFE, h->x, h->y, 0.40f, 0.70f);
}

static void DrawHearingMarker(void) {
    if (!BenchHearing()) return;
    extern float noiseDisplay, noiseSrcX, noiseSrcY;
    if (noiseDisplay <= 0.01f) return;
    float dx = noiseSrcX - posX, dy = noiseSrcY - posY;
    if (dx * dx + dy * dy > 400.0f) return;   /* 20 格内才显示 */
    /* 在地面投影一个闪烁的点（用 billboard 精灵代替） */
    DrawBakedProp(SPR_PROP_CRATE, noiseSrcX, noiseSrcY, 0.18f, 0.45f + 0.3f * noiseDisplay);
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
        list[n].kind = 12;
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
        case 9: DrawLeechSprite(list[i].idx); break;
            case 5: DrawLightBeastSprite(); break;
            case 6: DrawFlareSprite(list[i].idx); break;
            case 8: DrawShotSprite(list[i].idx); break;
            case 10: DrawSlashSprite(); break;
            case 12: DrawHideSpotSprite(list[i].idx); break;   /* v3.0 藏身点 */
            case 11: DrawExtraSprite(list[i].idx); break;
            case 7: DrawFlowerSprite(); break;
            default: DrawExitBeamSprite(); break;
        }
    }
    /* v1.18：烟幕团（不参与深度排序，直接在最后叠加） */
    DrawHideSpotHighlight();   /* v3.0 藏身点靠近提示 */
    DrawHearingMarker();   /* v3.0 听觉 */
    DrawSmokeCloud();

    /* 玻璃碎片：不在深度排序里，直接画在最后（覆盖在所有 billboard 之上） */
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
        float reach = 1.0f - perpWallDist / 9.0f;
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
        int k8 = (int)(lit * 256.0f);
        if (k8 < 0) k8 = 0;
        else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
        k8 = shadeLut[k8];      /* gamma 校正，和地板共用同一张表 */

        const unsigned int* src = &texture[0][texX];
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
        int occ = cx < 0 ? 0 : (cx >= renderW ? renderW - 1 : cx);
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
        if (hiddenIn >= 0 || 1) return;   /* 无战斗：彻底不画手持模型 */
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

    int sid = SPR_WRENCH;
    float light = 0.85f;
    if (curWeapon == WPN_RANGED)      { sid = SPR_PISTOL; light = 0.90f; }
    else if (curWeapon == WPN_SPELL)  { sid = SPR_SIGIL;  light = 1.00f; }

    int cx = (int)((float)renderW * 0.72f + sway);
    int cy = (int)((float)renderH * 1.04f + lift - kick);
    int px = cx - size / 2;
    int py = cy - size;
    DrawSprite(sid, px, py, size, size, light, 1.0f, 0, 0.0f);

    /* 开火瞬间在武器前端补一团光 */
    if (muzzleFlash > 0.02f && curWeapon != WPN_MELEE) {
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
        extern int flares, bottles, bricks, acids, smokeBombs;
        extern int firePots, gluePots, mirrors;
        int counts[ITEM_SLOT_COUNT] = { flares, bottles, bricks, acids, smokeBombs,
                                        firePots, gluePots, mirrors };
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
            DrawSprite(SPR_ITEM_FLARE + sel, ix, iy - isize, isize, isize,
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
        { 0x0030FF60u, "你",      "YOU"    },
        { 0x00E8C040u, "出口",    "EXIT"   },
        { 0x0030D0D0u, "保险丝箱","FUSE"   },
        { 0x00B0C0D0u, "配电箱",  "BREAKER"},
        { 0x0060FF90u, "安全屋",  "SAFE"   },
        { 0x0090E0FFu, "藏身点","HIDE"   },
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
            int barTop   = renderH - UIS(120);   /* LIGHT/RUN 上沿 */
            int itemsBot = UIS(150);             /* 右上角道具栏下沿 */
            int room = barTop - itemsBot;
            if (room < UIS(60)) room = UIS(60);

            /* 塞不下就缩格子（3px 是下限，再小就看不清了） */
            while (mapH > room && cellSize > 3) {
                cellSize--;
                mapW = MAP_WIDTH * cellSize;
                mapH = MAP_HEIGHT * cellSize;
            }
            mapStartX = renderW - mapW - UIS(10);
            mapStartY = barTop - mapH;
            if (mapStartY < itemsBot) mapStartY = itemsBot;
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
        for (int d = -2; d <= 2; d++) {
            int span = 2 - (d < 0 ? -d : d);
            for (int e = -span; e <= span; e++)
                PutPixel(px + e, py + d, 0x0090E0FF);
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
        const char* label = mapFullScreen
            ? "MAP  -10 BAT/S   M: CLOSE   HOLD TAB"
            : "MAP  -10/S   HOLD TAB   M: FULL";
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

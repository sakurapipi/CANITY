/*
 * Canity — 渲染层实现：像素缓冲 / 贴图与查表 / 多线程光栅 / 3D / 精灵 / 地图
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "render.h"
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
void GenerateVignette(void) {
    free(vignetteMask);
    vignetteMask = (unsigned char*)malloc((size_t)renderW * renderH);
    int cx = renderW / 2, cy = renderH / 2;
    float maxDist = sqrtf((float)(cx * cx + cy * cy));
    if (maxDist < 1.0f) maxDist = 1.0f;
    for (int y = 0; y < renderH; y++) {
        for (int x = 0; x < renderW; x++) {
            int dx = x - cx, dy = y - cy;
            float ratio = sqrtf((float)(dx * dx + dy * dy)) / maxDist;
            if (ratio < 0.4f) ratio = 0.0f;
            else ratio = (ratio - 0.4f) / 0.6f;
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
int g_useDIB = 0;

/* 按当前设置重新分配内部渲染缓冲 */
void InitPixels(void) {
    float s = renderScales[renderScaleIndex] / 100.0f;
    renderW = (int)(winW * s);
    renderH = (int)(winH * s);
    if (renderW < 160) renderW = 160;
    if (renderH < 120) renderH = 120;
    if (renderW > MAX_SCREEN) renderW = MAX_SCREEN;
    if (renderH > MAX_SCREEN) renderH = MAX_SCREEN;

    /* 像素预算：窗口再大，内部渲染也不超过这个量，保证高分辨率下的帧时间。
       原来 90 万，1080p 全屏实测「渲染 29ms + 呈现 11ms」只有 25fps，
       这就是全屏卡顿的根因。28 万（约 640x360）实测 14ms，60fps 稳。 */
    {
        const long budget = PIXEL_BUDGET;
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

    g_memDC = CreateCompatibleDC(NULL);
    if (g_memDC) g_dib = CreateDIBSection(g_memDC, &bmi, DIB_RGB_COLORS, &g_dibBits, NULL, 0);
    if (g_memDC && g_dib && g_dibBits) {
        SelectObject(g_memDC, g_dib);
        pixels = (unsigned int*)g_dibBits;
        g_useDIB = 1;
    } else {
        if (g_memDC) { DeleteDC(g_memDC); g_memDC = NULL; }
        if (g_dib) { DeleteObject(g_dib); g_dib = NULL; }
        g_useDIB = 0;
        free(pixels);
        pixels = (unsigned int*)malloc((size_t)renderW * renderH * sizeof(unsigned int));
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
static unsigned char floorColorIdx[TEX_HEIGHT][TEX_WIDTH];  /* 贴图 -> 颜色索引 */
static unsigned char ceilColorIdx[TEX_HEIGHT][TEX_WIDTH];
static int nFloorColors = 0, nCeilColors = 0;

/* 把贴图重新索引成「颜色索引」，并建好预乘表。
   贴图本身保持不变（墙体、精灵等还在用），只是额外存一份索引。 */
static void BuildPremulTables(void) {
    unsigned int fcols[PREMUL_MAXCOL], ccols[PREMUL_MAXCOL];
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

void GenerateThemeTextures(int id) {
    themeId = id;
    if (id == 1) { floorBaseA = 0x0014161au; floorBaseB = 0x001c1f24u; }
    else if (id == 2) { floorBaseA = 0x001a0c0cu; floorBaseB = 0x00241010u; }
    else if (id == 3) { floorBaseA = 0x00181c18u; floorBaseB = 0x0020261fu; }
    else { floorBaseA = 0x00151515u; floorBaseB = 0x00202020u; }

    for (int y = 0; y < TEX_HEIGHT; y++) {
        for (int x = 0; x < TEX_WIDTH; x++) {
            /* 复古：2x2 像素块 + 8 级色阶 */
            int px = x & ~1, py = y & ~1;
            int r, g, b;
            switch (id) {
                case 1:   /* 监狱：冷灰蓝 + 铁栏 */
                    r = 34 + (px * py) % 12; g = 38 + (x * y) % 12; b = 44 + (px * py) % 14;
                    if (py % 12 == 0 || px % 24 == 0) { r = 22; g = 26; b = 32; }
                    if (px % 8 == 0) { r -= 10; g -= 10; b -= 10; }
                    break;
                case 2:   /* 地狱：暗红 + 裂纹 */
                    r = 52 + (px * py) % 24; g = 8 + (px * py) % 6; b = 6;
                    if (rand() % 22 == 0) { r = 8; g = 3; b = 3; }
                    if (rand() % 60 == 0) { r = 150; g = 40; b = 10; }
                    break;
                case 3:   /* 病房：浅瓷砖 + 污渍 */
                    r = 66 + (px * py) % 10; g = 70 + (x * y) % 10; b = 64 + (px * py) % 8;
                    if (py % 16 == 0 || px % 16 == 0) { r = 38; g = 42; b = 38; }
                    if (rand() % 45 == 0) { r = 30; g = 40; b = 30; }
                    break;
                default:  /* 砖墙 */
                    r = 40 + (px * py) % 20; g = 10 + (x * y) % 10; b = 10;
                    if (rand() % 50 == 0) { r = 150; g = 0; b = 0; }
                    if (py % 16 == 0 || (px + ((py / 16) % 2) * 16) % 32 == 0) { r = 10; g = 10; b = 10; }
                    break;
            }
            r = (r >> 5) << 5; g = (g >> 5) << 5; b = (b >> 5) << 5;
            if (r > 248) r = 248;
            if (g > 248) g = 248;
            if (b > 248) b = 248;
            texture[y][x] = (unsigned int)((r << 16) | (g << 8) | b);

            /* 地砖：32px 砖块 + 勾缝 + 污渍 */
            {
                int fr = (int)((floorBaseA >> 16) & 0xFF), fg = (int)((floorBaseA >> 8) & 0xFF), fb = (int)(floorBaseA & 0xFF);
                int dark = ((px / 32 + py / 32) % 2) ? 0 : -8;
                if ((px % 32) < 2 || (py % 32) < 2) { fr = 12; fg = 12; fb = 12; }   /* 勾缝 */
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
    planeX = -dirY * L;
    planeY = dirX * L;
}

/* 该位置是否离已有物件太近（防止箱子/配电箱/花/怪互相叠在一起） */
/* ------------------------------------------------------------------ */
/* 3D 渲染                                                             */
/* ------------------------------------------------------------------ */

/* 把世界坐标投影成屏幕矩形（billboard） */
typedef struct { int ok; float ty; int x0, x1, y0, y1; } SpriteBox;

static SpriteBox ProjectSprite(float wx, float wy, float lift, float halfH) {
    SpriteBox s;
    s.ok = 0;
    float dx = wx - posX, dy = wy - posY;
    float invDet = 1.0f / (planeX * dirY - dirX * planeY);
    float tx = invDet * (dirY * dx - dirX * dy);
    float ty = invDet * (-planeY * dx + planeX * dy);
    if (ty <= 0.08f) return s;

    float unit = renderH / ty;                  /* 一个世界单位多少像素 */
    int cx = (int)((renderW / 2.0f) * (1.0f + tx / ty));
    int half = (int)(unit * 0.28f);
    int cyFloor = renderH / 2 + (int)(unit * 0.5f);   /* 地面所在的屏幕 y */
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

static void DrawCrateSprite(int i) {
    Container* c = &containers[i];
    SpriteBox s = ProjectSprite(c->x, c->y, 0.0f, 0.40f);
    if (!s.ok) return;

    float lightFactor = (flashlightOn && battery > 0.0f) ? 1.0f : 0.35f;
    int spanX = s.x1 - s.x0, spanY = s.y1 - s.y0;
    int searched = c->searched;
    float dyRel = 32.0f / (float)spanY;

    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        int relX = (x - s.x0) * 32 / spanX;
        float fy = 0.0f;
        unsigned int* col = pixels + (size_t)s.y0 * renderW + x;
        int wrote = 0;
        for (int y = s.y0; y < s.y1; y++, fy += dyRel, col += renderW) {
            int relY = (int)fy;
            unsigned int color = 0;
            if (relX > 3 && relX < 29 && relY > 3 && relY < 31) {
                color = ((relX / 6) % 2) ? 0x00462f13 : 0x003d2910;
                if (relX == 6 || relX == 13 || relX == 20 || relX == 27) color = 0x00221607;
                if (relY == 6 || relY == 18 || relY == 30) color = 0x00221607;
                if ((relX < 8 || relX > 24) && (relY < 10 || relY > 25)) color = 0x005e5e5e;
                if ((relX < 8 || relX > 24) && (relY < 10 || relY > 25) && (relX == 4 || relX == 28 || relY == 4)) color = 0x00343434;
                {
                    int d1 = relX - relY, d2 = (31 - relX) - relY;
                    if (d1 > -2 && d1 < 2) color = 0x00301f0d;
                    if (d2 > -2 && d2 < 2) color = 0x00301f0d;
                }
                if (relY < 10) {
                    if (!searched) color = (relY < 6) ? 0x00543a1a : 0x004a3416;
                    else color = (relY < 8) ? 0x002a1a0a : 0x000c0803;
                }
                /* 上锁的箱子：正面加一枚金色锁扣，隔着几格也能认出来 */
                if (!searched && i < MAX_CONTAINERS && contLocked[i] &&
                    relX > 11 && relX < 21 && relY > 12 && relY < 21) {
                    int edge = (relX == 12 || relX == 20 || relY == 13 || relY == 20);
                    color = edge ? 0x00a07820 : 0x00e8c040;
                }
                if (searched && relX > 8 && relX < 24 && relY > 9 && relY < 20) {
                    color = 0x000c0803;
                    if (relX > 13 && relX < 19 && relY > 11 && relY < 17) {
                        if (c->content == 1) color = 0x00e8c040;
                        else if (c->content == 2) color = 0x0040c060;
                        else if (c->content == 3) color = 0x00d04040;
                        else if (c->content == 4) color = 0x00ffb050;
                        else if (c->content == 5) color = 0x00a0d0ff;
                        else if (c->content == CONTENT_SCROLL) color = 0x00ffd060;   /* 高级箱：金色 */
                    }
                }
            } else if (relX > 1 && relX < 31 && relY > 1 && relY < 32) {
                color = 0x00180f06;
            }
            if (!color) continue;
            float sh = (1.0f - s.ty / 15.0f) * lightFactor;
            if (sh < 0.15f) sh = 0.15f;
            int r = (int)(((color >> 16) & 0xFF) * sh);
            int g = (int)(((color >> 8) & 0xFF) * sh);
            int b = (int)((color & 0xFF) * sh);
            *col = (unsigned int)((r << 16) | (g << 8) | b);
            wrote = 1;
        }
        if (wrote && s.ty < zBuffer[x]) zBuffer[x] = s.ty;   /* 让后面的精灵被它遮挡 */
    }
}

static void DrawBreakerSprite(void) {
    if (!breaker.active) return;
    SpriteBox s = ProjectSprite(breaker.x, breaker.y, 0.24f, 0.34f);
    if (!s.ok) return;

    float lightFactor = (flashlightOn && battery > 0.0f) ? 1.0f : 0.35f;
    int spanX = s.x1 - s.x0, spanY = s.y1 - s.y0;
    float dyRel = 32.0f / (float)spanY;

    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        int relX = (x - s.x0) * 32 / spanX;
        float fy = 0.0f;
        unsigned int* col2 = pixels + (size_t)s.y0 * renderW + x;
        int wrote = 0;
        for (int y = s.y0; y < s.y1; y++, fy += dyRel, col2 += renderW) {
            int relY = (int)fy;
            unsigned int color = 0;
            if (relX > 5 && relX < 27 && relY > 2 && relY < 30) {
                color = 0x00606668;
                if (relX > 8 && relX < 24 && relY > 5 && relY < 20) color = 0x00404a4e;
                if (relX > 12 && relX < 20 && relY > 8 && relY < 14)
                    color = breakerOpen ? 0x0030ff60 : 0xff2828;
            } else if (relX > 3 && relX < 29 && relY > 1 && relY < 31) {
                color = 0x00202426;
            }
            if (!color) continue;
            float sh = (1.0f - s.ty / 15.0f) * lightFactor;
            if (sh < 0.2f) sh = 0.2f;
            if (color == 0x0030ff60 || color == 0xff2828) sh = 1.0f;
            int r = (int)(((color >> 16) & 0xFF) * sh);
            int g = (int)(((color >> 8) & 0xFF) * sh);
            int b = (int)((color & 0xFF) * sh);
            *col2 = (unsigned int)((r << 16) | (g << 8) | b);
            wrote = 1;
        }
        if (wrote && s.ty < zBuffer[x]) zBuffer[x] = s.ty;
    }
}

static void DrawBeastSprite(Monster* m, unsigned int eyeColor) {
    if (!m->active) return;
    SpriteBox s = ProjectSprite(m->x, m->y, 0.0f, 0.86f);
    if (!s.ok) return;

    int spanX = s.x1 - s.x0, spanY = s.y1 - s.y0;
    float dyRel = 32.0f / (float)spanY;
    float light = (flashlightOn && battery > 0.0f) ? 1.0f : 0.55f;

    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        int relX = (x - s.x0) * 32 / spanX;
        float fy = 0.0f;
        unsigned int* col = pixels + (size_t)s.y0 * renderW + x;
        int wrote = 0;
        for (int y = s.y0; y < s.y1; y++, fy += dyRel, col += renderW) {
            int relY = (int)fy;
            unsigned int color = 0;
            int eye = 0;
            int headW = 4, bodyW;
            if (relY < 11) bodyW = headW + (relY / 4);
            else if (relY < 18) bodyW = 8;
            else bodyW = 6;
            int cx = 16;
            if (relY > 1 && relY < 31 && relX > cx - bodyW && relX < cx + bodyW) {
                color = 0x00141418;
                if (relX <= cx - bodyW + 1 || relX >= cx + bodyW - 1) color = 0x000a0a0c;
                if (relY > 4 && relY < 8) {
                    if ((relX > cx - 4 && relX < cx - 1) || (relX > cx + 1 && relX < cx + 4)) {
                        color = 0x00ff3020;
                        eye = 1;
                    }
                }
            }
            if (!color) continue;
            if (eye) {
                PutPixelAdd(x, y, eyeColor, 0.85f);
            } else {
                float sh = (1.0f - s.ty / 16.0f) * light;
                if (sh < 0.12f) sh = 0.12f;
                int r = (int)(((color >> 16) & 0xFF) * sh);
                int g = (int)(((color >> 8) & 0xFF) * sh);
                int b = (int)((color & 0xFF) * sh);
                /* 硬直期间身体发白 —— 玩家一眼看出「我砸中了它」。
                   这是砸中唯一可靠的视觉反馈（音效只是一瞬间）。 */
                if (m->stun > 0.0f) {
                    float k = m->stun / MONSTER_STUN;
                    if (k > 1.0f) k = 1.0f;
                    r = r + (int)((235 - r) * k);
                    g = g + (int)((245 - g) * k);
                    b = b + (int)((255 - b) * k);
                }
                *col = (unsigned int)((r << 16) | (g << 8) | b);
            }
            wrote = 1;
        }
        if (wrote && s.ty < zBuffer[x]) zBuffer[x] = s.ty;
    }
}

static void DrawMonsterSprite(void) { DrawBeastSprite(&monster, 0x00ff4020u); }
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
static void DrawFlareSprite(int idx) {
    if (!thrown[idx].active) return;
    /* 飞行中用 z 投影，落地用地面投影 */
    float lift = thrown[idx].z > 0.0f ? thrown[idx].z / 10.0f : 0.0f;
    SpriteBox s = ProjectSprite(thrown[idx].x, thrown[idx].y, lift, 0.30f);
    if (!s.ok) return;
    int spanX = s.x1 - s.x0;
    int spanY = s.y1 - s.y0;

    /* 荧光棒：三层光晕 + 慢速闪烁 */
    float flick = 0.82f + 0.18f * sinf((float)g_timeSec * 9.0f);
    float cx = spanX * 0.5f, cy = spanY * 0.55f;
    float rad = spanX * 0.95f;
    if (rad < 4.0f) rad = 4.0f;
    int R = (int)rad;
    for (int dy = -R; dy <= R; dy++) {
        int y = (int)(s.y0 + cy) + dy;
        if (y < 0 || y >= renderH) continue;
        for (int dx = -R; dx <= R; dx++) {
            int x = (int)(s.x0 + cx) + dx;
            if (x < 0 || x >= renderW) continue;
            if (s.ty >= zBuffer[x]) continue;
            float d2 = (float)(dx * dx + dy * dy) / (rad * rad);
            if (d2 >= 1.0f) continue;
            float a = 1.0f - d2;
            /* 核心最亮，外圈是暖橙色雾 */
            PutPixelAdd(x, y, 0x00ffd070u, a * a * a * 1.15f * flick);
            if (d2 < 0.16f) PutPixelAdd(x, y, 0x00fff8e0u, 0.55f * flick);
        }
    }
    /* 飞行中在投影下方画个小影子 */
    if (thrown[idx].z > 0.0f) {
        int sx = s.x0 + spanX / 2;
        int sy = s.y1 - 2;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (dx*dx + dy*dy <= 4)
                    PutPixelAdd(sx + dx, sy + dy, 0x00101010u, 0.4f);
    }
}

/* 飞行中的玻璃瓶：抛物线轨迹 + 瓶身 + 拖影 + 发光 */
static void DrawShotSprite(int idx) {
    if (!shot[idx].active) return;

    /* 计算屏幕投影：考虑高度 z */
    SpriteBox s = ProjectSprite(shot[idx].x, shot[idx].y, shot[idx].z / 10.0f, 0.15f);  /* 稍大投影 */
    if (!s.ok) return;

    int cx = (s.x0 + s.x1) / 2;
    int cy = (s.y0 + s.y1) / 2;
    float light = (flashlightOn && battery > 0.0f) ? 1.0f : 0.7f;

    /* 拖影：更长、更亮、渐变色 */
    for (int t = 1; t <= 6; t++) {
        float alpha = 0.25f / t;
        int gx = cx - (int)(shot[idx].vx * t * 0.025f * renderW / 800.0f);
        int gy = cy - (int)(shot[idx].vy * t * 0.025f * renderH / 600.0f);
        if (gx >= 0 && gx < renderW && gy >= 0 && gy < renderH && s.ty < zBuffer[gx]) {
            /* 拖影颜色随时间从蓝变青 */
            unsigned int trailCol = (t <= 2) ? 0x0040e0ffu : 0x0080ffe0u;
            PutPixelAdd(gx, gy, trailCol, alpha * light);
        }
    }

    /* 瓶身：根据高度调整大小，最小也要 3 像素半径 */
    int size = 3 + (int)(5.0f * (1.0f - shot[idx].z / 4.0f));
    if (size < 3) size = 3;
    for (int dy = -size; dy <= size; dy++) {
        for (int dx = -size; dx <= size; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
            if (s.ty >= zBuffer[x]) continue;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist > size) continue;
            float a = 1.0f - dist / (size + 0.5f);
            /* 瓶身：玻璃蓝绿，边缘更亮 */
            unsigned int bottleCol = (dist < size * 0.4f) ? 0x0060ffffu : 0x0040c0d0u;
            PutPixelAdd(x, y, bottleCol, a * 0.95f * light);
        }
    }
    /* 核心高光 */
    if (cx >= 0 && cx < renderW && cy >= 0 && cy < renderH && s.ty < zBuffer[cx])
        PutPixelAdd(cx, cy, 0x00ffffffu, 1.0f * light);
    /* 次级高光环 */
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx*dx + dy*dy <= 1)
                PutPixelAdd(cx+dx, cy+dy, 0x00e0ffffu, 0.6f * light);

    /* 抛物线轨迹预览：更亮、更长、实线感 */
    if (shot[idx].age < 0.8f) {
        for (int k = 1; k <= 12; k++) {
            float ft = k * 0.12f;
            float px = shot[idx].x + shot[idx].vx * ft;
            float py = shot[idx].y + shot[idx].vy * ft;
            float pz = shot[idx].z + shot[idx].vz * ft - 0.5f * GRAVITY * ft * ft;
            if (pz <= 0.0f) break;
            SpriteBox ps = ProjectSprite(px, py, pz / 10.0f, 0.04f);
            if (ps.ok) {
                int gx = (ps.x0 + ps.x1) / 2;
                int gy = (ps.y0 + ps.y1) / 2;
                if (gx >= 0 && gx < renderW && gy >= 0 && gy < renderH) {
                    float trailAlpha = 0.5f * (1.0f - k * 0.07f);
                    PutPixelAdd(gx, gy, 0x0060c0ffu, trailAlpha);
                }
            }
        }
    }
}

/* 绘制玻璃碎片 */
static void DrawShardSprite(void) {
    for (int i = 0; i < MAX_SHOTS * SHARD_COUNT; i++) {
        if (!shards_debris[i].active) continue;
        SpriteBox s = ProjectSprite(shards_debris[i].x, shards_debris[i].y, shards_debris[i].z / 10.0f, 0.05f);
        if (!s.ok) continue;

        int cx = (s.x0 + s.x1) / 2;
        int cy = (s.y0 + s.y1) / 2;
        float lifeRatio = shards_debris[i].life / MAX_SHARD_LIFE;
        float alpha = 0.7f * lifeRatio;
        if (alpha < 0.05f) continue;

        /* 碎片：小菱形闪光 */
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                if (abs(dx) + abs(dy) != 1) continue;  /* 菱形 */
                int x = cx + dx, y = cy + dy;
                if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
                if (s.ty >= zBuffer[x]) continue;
                PutPixelAdd(x, y, 0x00d0e0ffu, alpha);
            }
        }
        /* 中心 */
        if (cx >= 0 && cx < renderW && cy >= 0 && cy < renderH && s.ty < zBuffer[cx])
            PutPixelAdd(cx, cy, 0x00ffffffu, alpha * 1.5f);
    }
}

static void DrawFlowerSprite(void) {
    if (!safeRoom.active) return;
    SpriteBox s = ProjectSprite(safeRoom.x, safeRoom.y, 0.0f, 0.42f);
    if (!s.ok) return;
    int spanX = s.x1 - s.x0, spanY = s.y1 - s.y0;
    float dyRel = 32.0f / (float)spanY;
    float pulse = 0.75f + 0.25f * sinf((float)g_timeSec * 2.0f);
    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        int relX = (x - s.x0) * 32 / spanX;
        float fy = 0.0f;
        unsigned int* col = pixels + (size_t)s.y0 * renderW + x;
        int wrote = 0;
        for (int y = s.y0; y < s.y1; y++, fy += dyRel, col += renderW) {
            int relY = (int)fy;
            /* 茎 */
            if (relX > 15 && relX < 18 && relY > 14) { *col = 0x00305020; wrote = 1; continue; }
            /* 花瓣 */
            int dx = relX - 16, dy = relY - 11;
            int d2 = dx * dx + dy * dy;
            if (d2 < 30) {
                if (d2 < 8) PutPixelAdd(x, y, 0x00fff0a0u, 0.9f * pulse);
                else PutPixelAdd(x, y, 0x00e060a0u, 0.55f * pulse);
                wrote = 1;
            }
        }
        if (wrote && s.ty < zBuffer[x]) zBuffer[x] = s.ty;
    }
}

static void DrawExitBeamSprite(void) {
    SpriteBox s = ProjectSprite(exitX, exitY, 0.0f, 1.05f);
    if (!s.ok) return;
    int spanX = s.x1 - s.x0;
    int spanY = s.y1 - s.y0;
    if (spanX <= 0 || spanY <= 0) return;

    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        float fx = (float)(x - s.x0) / (float)spanX;
        float edge = 1.0f - fabsf(fx - 0.5f) * 2.0f;
        if (edge <= 0.0f) continue;
        for (int y = s.y0; y < s.y1; y++) {
            float fy = (float)(y - s.y0) / (float)spanY;
            float a = edge * edge * (0.30f + 0.70f * fy);
            if (breakerOpen) PutPixelAdd(x, y, 0x0040FF70u, a * 0.50f);
            else if (edge > 0.80f) PutPixelAdd(x, y, 0x00C03030u, a * 0.28f);
        }
    }
}

/* 所有 billboard 按距离从远到近画，近的盖住远的（画家算法）
   数组上界 MAX_SPRITES = 容器 + 投掷物 + 配电箱 + 两只怪 + 安全屋 + 出口光柱，
   写满时会被下面的断言拦下来，不会像以前那样悄悄越界。 */
/* 隐藏房间的密码碎片：地上一枚发光的青色菱片，会呼吸闪烁 */
static void DrawCodeSprite(int idx) {
    if (idx < 0 || idx >= hiddenCodeCount) return;
    float wx = hiddenCodeX[idx] + 0.5f, wy = hiddenCodeY[idx] + 0.5f;
    SpriteBox s = ProjectSprite(wx, wy, 0.0f, 0.18f);
    if (!s.ok) return;
    int cx = (s.x0 + s.x1) / 2, cy = (s.y0 + s.y1) / 2;
    if (cx < 0 || cx >= renderW) return;
    if (s.ty >= zBuffer[cx]) return;

    float pulse = 0.65f + 0.35f * sinf((float)g_timeSec * 4.0f);
    int rad = 6 + (int)(pulse * 2.0f);
    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (d > rad) continue;
            unsigned int c = (d <= 2) ? 0x00B0FFF0u : 0x0040D0C0u;
            PutPixelBlend(cx + dx, cy + dy, c, 0.55f + 0.3f * pulse);
        }
    }
}

void RenderSprites(void) {    struct { float d2; int kind, idx; } list[MAX_SPRITES];
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
    /* 隐藏房间的密码碎片 */
    for (int i = 0; i < hiddenCodeCount; i++) {
        if (hiddenCodeTaken[i]) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(hiddenCodeX[i] + 0.5f, hiddenCodeY[i] + 0.5f, posX, posY);
        list[n].kind = 9;
        list[n].idx = i;
        n++;
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
            case 5: DrawLightBeastSprite(); break;
            case 6: DrawFlareSprite(list[i].idx); break;
            case 8: DrawShotSprite(list[i].idx); break;
            case 7: DrawFlowerSprite(); break;
            case 9: DrawCodeSprite(list[i].idx); break;
            default: DrawExitBeamSprite(); break;
        }
    }
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
void RunPhase(int phase) {
    int y0, y1;
    for (int i = 0; i < g_workerCount; i++) {
        if (phase == 1) { SplitRange(renderH / 2 + 1, renderH, i, g_parts, &g_workers[i].y0, &g_workers[i].y1); }
        else if (phase == 2) { SplitRange(0, renderW, i, g_parts, &g_workers[i].x0, &g_workers[i].x1); }
        else { SplitRange(0, renderH, i, g_parts, &g_workers[i].y0, &g_workers[i].y1); }
        g_workers[i].phase = phase;
        SetEvent(g_workers[i].hStart);
    }

    int p = g_workerCount;
    if (phase == 1) { SplitRange(renderH / 2 + 1, renderH, p, g_parts, &y0, &y1); RenderFloorBand(y0, y1); }
    else if (phase == 2) { int x0, x1; SplitRange(0, renderW, p, g_parts, &x0, &x1); RenderWallBand(x0, x1); }
    else { SplitRange(0, renderH, p, g_parts, &y0, &y1); RenderPostBand(y0, y1); }

    for (int i = 0; i < g_workerCount; i++) {
        /* 超时也要继续，宁可掉一帧也不能卡死 */
        WaitForSingleObject(g_workers[i].hDone, 300);
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
/* ---------------- 透视技能：隔墙标出实体 ----------------
   不参与 zBuffer 遮挡 —— 直接画在墙上面，就是“看见墙后”的意思。 */
static void SightMark(const SpriteBox* s, unsigned int c) {
    if (!s->ok) return;
    int cx = (s->x0 + s->x1) / 2;
    int cy = (s->y0 + s->y1) / 2;
    int r = (s->x1 - s->x0) / 2 + 2;
    if (r < 5) r = 5;
    if (r > 40) r = 40;
    for (int i = -r; i <= r; i++) {
        PutPixel(cx + i, cy - r, c);
        PutPixel(cx + i, cy + r, c);
        PutPixel(cx - r, cy + i, c);
        PutPixel(cx + r, cy + i, c);
    }
}

void RenderSightOverlay(void) {
    if (!sightActive) return;
    SpriteBox s;

    s = ProjectSprite(monster.x, monster.y, 0.0f, 0.42f);
    SightMark(&s, 0x00FF4848u);
    if (lightBeast.active) {
        s = ProjectSprite(lightBeast.x, lightBeast.y, 0.0f, 0.42f);
        SightMark(&s, 0x00FF70C0u);
    }
    for (int i = 0; i < containerCount; i++) {
        if (!containers[i].active || containers[i].searched) continue;
        s = ProjectSprite(containers[i].x, containers[i].y, 0.0f, 0.40f);
        SightMark(&s, 0x00E8C040u);
    }
    if (breaker.active) {
        s = ProjectSprite(breaker.x, breaker.y, 0.0f, 0.45f);
        SightMark(&s, 0x0030D0D0u);
    }
    s = ProjectSprite(exitX, exitY, 0.0f, 0.60f);
    SightMark(&s, 0x0040F060u);
}

static void RenderFloorBand(int y0, int y1) {
    int halfH = renderH / 2;
    float rayDirX0 = dirX - planeX, rayDirY0 = dirY - planeY;
    float rayDirX1 = dirX + planeX, rayDirY1 = dirY + planeY;
    int exitBX = (int)(exitX * 4.0f), exitBY = (int)(exitY * 4.0f);
    int safeBX = (int)(safeRoom.x * 4.0f), safeBY = (int)(safeRoom.y * 4.0f);

    for (int y = y0; y < y1; y++) {
        float rowDistance = (0.5f * renderH) / (float)(y - halfH);
        float stepX = rowDistance * (rayDirX1 - rayDirX0) / renderW;
        float stepY = rowDistance * (rayDirY1 - rayDirY0) / renderW;
        float fx = posX + rowDistance * rayDirX0;
        float fy = posY + rowDistance * rayDirY0;
        float shadow = 1.0f - rowDistance / 10.0f;
        if (shadow < 0.0f) shadow = 0.0f;
        unsigned int* row = pixels + (size_t)y * renderW;

        for (int x = 0; x < renderW; x++) {
            /* 地板与天花板都采样真纹理；亮度按列/行预乘 */
            int cX = (int)fx, cY = (int)fy;
            int tx2 = ((int)((fx - cX) * (float)TEX_WIDTH)) & (TEX_WIDTH - 1);
            int ty2 = ((int)((fy - cY) * (float)TEX_HEIGHT)) & (TEX_HEIGHT - 1);
            tx2 &= ~1; ty2 &= ~1;

            /* 亮度：shadow（行常量）× floorGain（列常量）→ k8 → gamma → 等级 */
            float bright = shadow * floorGain[x];
            int k8 = (int)(bright * 256.0f);
            if (k8 < 0) k8 = 0; else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
            k8 = shadeLut[k8];      /* gamma 校正：地板也走查表 */
            int lvl = k8 >> PREMUL_K8_SHIFT;   /* 0..255 */

            /* 查预乘表：省掉 3 次乘法 + 3 次截断 */
            unsigned int color = premulFloor[floorColorIdx[ty2][tx2]][lvl];

            /* 天花板：镜像行
               原版是 (通道 * ck) >> 8，ck 是 0..255 的乘数；
               表里用 k8 量纲（等级 l 对应乘数 l*4），所以等级 = ck >> 2。 */
            {
                int my2 = renderH - y - 1;
                if (my2 >= 0 && my2 < renderH) {
                    int cl = ceilTab[x] >> PREMUL_K8_SHIFT;
                    if (cl > PREMUL_LEVELS - 1) cl = PREMUL_LEVELS - 1;
                    pixels[(size_t)my2 * renderW + x] = premulCeil[ceilColorIdx[ty2][tx2]][cl];
                }
            }

            /* 安全屋：暖色花圃（只有附近才需要格子坐标） */
            if (safeRoom.active) {
                int bx = (int)(fx * 4.0f), by = (int)(fy * 4.0f);
                int sbx = bx - safeBX, sby = by - safeBY;
                if (sbx >= -2 && sbx <= 2 && sby >= -2 && sby <= 2) {
                    float d2s = (float)(sbx * sbx + sby * sby) / 8.0f;
                    float gs = 1.0f - d2s;
                    if (gs > 0.0f) {
                        int r = (int)(70 * gs), g = (int)(48 * gs), b = (int)(58 * gs);
                        color = (unsigned int)((r << 16) | (g << 8) | b);
                    }
                }
            }

            /* 出口：一个 1.5 格见方的光池，通电才是绿色 */
            {
                int bx = (int)(fx * 4.0f), by = (int)(fy * 4.0f);
                int dbx = bx - exitBX, dby = by - exitBY;
                if (dbx >= -3 && dbx <= 3 && dby >= -3 && dby <= 3) {
                    float d2 = (float)(dbx * dbx + dby * dby) / 9.0f;
                    float glow = 1.0f - d2;
                    if (glow > 0.0f) {
                        float k = (shadow * 0.45f + 0.60f) * glow;
                        if (breakerOpen) {
                            int r = (int)(40 * k), g = (int)(235 * k), b = (int)(105 * k);
                            if (r > 255) r = 255;
                            if (g > 255) g = 255;
                            if (b > 255) b = 255;
                            color = (unsigned int)((r << 16) | (g << 8) | b);
                        } else {
                            int r = (int)(120 * k), g = (int)(24 * k);
                            if (r > 255) r = 255;
                            color = (unsigned int)((r << 16) | (g << 8));
                        }
                    }
                }
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

        /* 穿墙期间墙画成「幽灵墙」：压暗 + 偏蓝，能看清结构但不完全挡视线 */
        int ghostWall = phaseActive;

        int lineHeight = (int)(renderH / perpWallDist);
        int drawStart = -lineHeight / 2 + halfH;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + halfH;
        if (drawEnd > renderH) drawEnd = renderH;
        if (drawEnd <= drawStart) continue;

        float wallX = (side == 0) ? (posY + perpWallDist * rayDirY) : (posX + perpWallDist * rayDirX);
        wallX -= floorf(wallX);
        int texX = (int)(wallX * (float)TEX_WIDTH);
        if (texX < 0) texX = 0;
        if (texX > TEX_WIDTH - 1) texX = TEX_WIDTH - 1;
        if ((side == 0 && rayDirX > 0) || (side == 1 && rayDirY < 0)) texX = TEX_WIDTH - texX - 1;

        float step = (float)TEX_HEIGHT / lineHeight;
        float texPos = (drawStart - halfH + lineHeight / 2.0f) * step;

        /* 亮度 = 距离衰减 × 手电；手电中心再补一层白，墙才「亮得起来」 */
        float shadow = 1.0f - perpWallDist / 12.0f;
        if (shadow < 0.0f) shadow = 0.0f;
        if (side == 1) shadow *= 0.62f;
        float lit = shadow * flashTab[x];
        float flash = flashTab[x];
        float cone = flash > 0.45f ? (flash - 0.45f) : 0.0f;
        int addR = (int)(cone * shadow * 150.0f);
        int addG = (int)(cone * shadow * 128.0f);
        int addB = (int)(cone * shadow * 106.0f);
        int k8 = (int)(lit * 256.0f);
        if (k8 < 0) k8 = 0;
        else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
        k8 = shadeLut[k8];      /* gamma 校正，和地板共用同一张表 */

        const unsigned int* src = &texture[0][texX];
        unsigned int* dst = pixels + (size_t)drawStart * renderW + x;
        for (int y = drawStart; y < drawEnd; y++) {
            int texY = ((int)texPos) & (TEX_HEIGHT - 1);
            texPos += step;
            unsigned int t = src[(texY << 6)];
            int r = (int)(((t >> 16) & 0xFF) * k8) >> 8;
            int g = (int)(((t >> 8) & 0xFF) * k8) >> 8;
            int b = (int)((t & 0xFF) * k8) >> 8;
            r += addR; g += addG; b += addB;
            if (ghostWall) {
                r = (r * 35) / 100 + 8;
                g = (g * 35) / 100 + 16;
                b = (b * 55) / 100 + 38;
            }
            /* 隐藏房间的暗门：染成暖金色，走到跟前能看出“这里有门” */
            if (worldMap[mapY][mapX] == HIDDEN_DOOR_TILE) {
                r = r / 2 + 105;
                g = g / 2 + 78;
                b = b / 3 + 18;
            }
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            *dst = (unsigned int)((r << 16) | (g << 8) | b);
            dst += renderW;
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
        intensity = (int)((50.0f - sanity) / 50.0f * 255.0f);
        if (intensity > 255) intensity = 255;
        if (intensity < 0) intensity = 0;
    }
    int heal = (healFlash > 0.0f);
    int frameSeed = (int)(g_timeSec * 61.0f) * 17;

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
            f = 0.32f;   /* 手电熄灭时的环境光 */
        }
        flashTab[x] = f;
        float cone = f > 0.40f ? (f - 0.40f) / 0.75f : 0.0f;
        floorGain[x] = 0.52f + cone * 1.30f;
        ceilTab[x] = (int)((0.30f + cone * 0.75f) * 110.0f);
    }

    RunPhase(1);
    RunPhase(2);
    RenderSprites();
    if ((vignetteOn && sanity < 50.0f) || healFlash > 0.0f) RunPhase(3);
}
/* 小地图（在内部渲染分辨率上绘制）
   只有一种形态：右下角小窗，cellSize 固定 8 -> 24*8 = 192px 见方，
   不挡视野，可以边走边看。全屏大地图已经删掉 ——
   它要求玩家停下来看，反而失去了「导航」的意义。 */
#define MAP_CORNER_CELL 8

void RenderMap(void) {
    int cellSize = MAP_CORNER_CELL;
    int mapW = MAP_WIDTH * cellSize;
    int mapH = MAP_HEIGHT * cellSize;
    /* 贴右下角，留出一点边距 */
    int mapStartX = renderW - mapW - UIS(10);
    int mapStartY = renderH - mapH - UIS(10);
    if (mapStartX < 0) mapStartX = 0;
    if (mapStartY < 0) mapStartY = 0;

    /* 只给地图区域加个边框底板，不压暗整个画面 */
    {
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
        if (!explored[(int)containers[i].y][(int)containers[i].x]) continue;
        if ((int)(g_timeSec * 4.5f) % 2) continue;
        int px = mapStartX + (int)containers[i].x * cellSize + cellSize / 2;
        int py = mapStartY + (int)containers[i].y * cellSize + cellSize / 2;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) PutPixel(px + dx, py + dy, 0x00E8C040);
    }
    if (breaker.active && explored[(int)breaker.y][(int)breaker.x]) {
        int px = mapStartX + (int)breaker.x * cellSize + cellSize / 2;
        int py = mapStartY + (int)breaker.y * cellSize + cellSize / 2;
        unsigned int c = breakerOpen ? 0x0030FF60 : 0x0030D0D0;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++)
                if (dx * dx + dy * dy <= 9) PutPixel(px + dx, py + dy, c);
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

    /* 标题：告诉玩家再按一次 M 会关掉地图。小窗模式下标题画在地图上方。 */
    {
        const char* label = "MAP   M: CLOSE";
        int tw = TextWidthRaw(label);
        int tx = mapStartX;
        int ty = mapStartY - FONT_H - 2;
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
}

/* ------------------------------------------------------------------ */
/* 逻辑更新（固定步长，dt = LOGIC_STEP）                                */
/* ------------------------------------------------------------------ */

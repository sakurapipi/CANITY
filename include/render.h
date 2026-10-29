/*
 * Canity — 渲染层：像素缓冲 / 贴图与查表 / 多线程光栅 / 3D / 精灵 / 地图
 *
 * 只依赖 common.h 的类型；运行期要读的游戏状态通过 game.h / entity.h 取。
 */
#ifndef CANITY_RENDER_H
#define CANITY_RENDER_H

#include "common.h"

/* ---- 内部分辨率与像素缓冲 ---- */
extern int renderW, renderH;
extern int pixelBudget;      /* v1.9：动态像素预算 */
extern float pitchRatio;      /* v1.12：视角上下（地平线偏移比例） */
extern int horizon;           /* v1.12：地平线偏移像素 */
extern int dynamicRes;       /* v1.9：自动画质开关 */
extern unsigned int* pixels;
extern unsigned char* vignetteMask;
extern BITMAPINFO bmi;

/* ---- 贴图 ---- */
extern unsigned int texture[TEX_HEIGHT][TEX_WIDTH];
extern unsigned int floorTexture[TEX_HEIGHT][TEX_WIDTH];
extern unsigned int ceilTexture[TEX_HEIGHT][TEX_WIDTH];

/* ---- 按列预计算表 ---- */
extern float zBuffer[MAX_SCREEN];
extern float flashTab[MAX_SCREEN];
extern int ceilTab[MAX_SCREEN];
extern float floorGain[MAX_SCREEN];   /* 0.52 + cone*1.30，按列预计算 */

/* ---- GDI 双缓冲 ---- */
extern HDC g_memDC;
extern HBITMAP g_dib;
extern void* g_dibBits;
extern int g_useDIB;

/* ---- 主题 ---- */
extern unsigned int floorBaseA, floorBaseB;
extern int themeId;

/* ---- 像素写入原语 ----
   这是全屏最热的路径，保留 static inline：每个编译单元一份，编译器照常内联。
   （ui.c 的文字绘制也要用 PutPixel。） */
static inline void PutPixel(int x, int y, unsigned int color) {
    if (x >= 0 && x < renderW && y >= 0 && y < renderH) pixels[y * renderW + x] = color;
}

static inline void PutPixelBlend(int x, int y, unsigned int color, float alpha) {
    if (x < 0 || x >= renderW || y < 0 || y >= renderH) return;
    unsigned int dst = pixels[y * renderW + x];
    int r = (int)(((color >> 16) & 0xFF) * alpha + ((dst >> 16) & 0xFF) * (1.0f - alpha));
    int g = (int)(((color >> 8) & 0xFF) * alpha + ((dst >> 8) & 0xFF) * (1.0f - alpha));
    int b = (int)((color & 0xFF) * alpha + (dst & 0xFF) * (1.0f - alpha));
    pixels[y * renderW + x] = (unsigned int)((r << 16) | (g << 8) | b);
}

/* 加色混合：灯光用，越亮越接近白 */
extern float spriteLight;   /* v1.9：当前精灵的光照衰减系数 */

static inline void PutPixelAdd(int x, int y, unsigned int color, float a) {
    a *= spriteLight;                      /* 精灵统一吃光照 */
    if (a <= 0.004f) return;
    if (x < 0 || x >= renderW || y < 0 || y >= renderH) return;
    unsigned int d = pixels[(size_t)y * renderW + x];
    int r = (int)((d >> 16) & 0xFF) + (int)(((color >> 16) & 0xFF) * a);
    int g = (int)((d >> 8) & 0xFF) + (int)(((color >> 8) & 0xFF) * a);
    int b = (int)(d & 0xFF) + (int)((color & 0xFF) * a);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    pixels[(size_t)y * renderW + x] = (unsigned int)((r << 16) | (g << 8) | b);
}

/* ---- 生命周期 ---- */
void GenerateVignette(void);
void InitPixels(void);
void UpdateDynamicRes(double renderMs);   /* v1.9：动态分辨率 */
void GenerateThemeTextures(int id);
void BuildShadeTables(void);
void ApplyFov(void);
void UpdatePitch(void);
void InitWorkers(void);
void ShutdownWorkers(void);

/* ---- 光栅化 ---- */
void Render3D(void);
void RenderSprites(void);
void RenderMap(void);
void DrawParasiteSprite(void);
/* 透视技能：隔墙标出怪物 / 容器 / 配电箱 / 出口（没有它就不画） */
void RenderSightOverlay(void);

/* 基准测试用：单独跑某个渲染阶段（1=地板 2=墙体 3=后处理） */
void RunPhase(int phase);

/* v2.0 中文字形（cjktext.c） */
int  CjkHasNonAscii(const char* s);
int  CjkTextWidth(const char* utf8);
void CjkDrawRaw(int x, int y, const char* utf8, unsigned int color);

#endif /* CANITY_RENDER_H */

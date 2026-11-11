/* 世界内截图工具（美术验收用，不进发布包）。
 *
 * 用法：world_shot.exe <out.ppm> [theme] [frames]
 *   theme  -1 = 用本层生成的主题；0..6 = 强制某个主题的贴图
 *   frames    逻辑步数（默认 180 = 1.5 秒）
 *
 * 为什么需要它：art_direction.h 铁律六十六要求"改完看一眼真实画面"，
 * 而主程序要开窗口、要人点。这个工具在同一条 RenderFrame 路径上取一帧，
 * 和实机画面逐像素一致（HUD 也在 pixels 里）。
 */
#include "common.h"
#include "render.h"
#include "sprites.h"
#include "game.h"
#include "achievements.h"
#include "ui.h"
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>

/* g_dbgMark 由 src/diagnostics.c 定义（RC4 起），这里不再重复定义。 */

static void writePpm(const char* path, int w, int h, const unsigned int* px) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot open %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned int p = px[i];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)((p >> 16) & 0xFF);
        rgb[1] = (unsigned char)((p >> 8) & 0xFF);
        rgb[2] = (unsigned char)(p & 0xFF);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s (%dx%d)\n", path, w, h);
}

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "world.ppm";
    int theme = (argc > 2) ? atoi(argv[2]) : -1;
    int frames = (argc > 3) ? atoi(argv[3]) : 180;
    if (frames < 1) frames = 1;

    InitAchievements();
    InitPixels();
    SpritesInit();
    StartGame();
    if (theme >= 0) GenerateThemeTextures(theme);

    for (int i = 0; i < frames; i++) {
        g_timeSec = (double)i / 120.0;
        Update(1.0f / 120.0f);
    }
    RenderFrame();

    if (!pixels || renderW <= 0 || renderH <= 0) {
        printf("no framebuffer\n");
        return 2;
    }
    writePpm(out, renderW, renderH, pixels);

    /* 20 帧平均渲染耗时：画风层的开销要用同一台机器、同一帧对比才成立 */
    {
        double t0 = PlatNow();
        for (int i = 0; i < 20; i++) RenderFrame();
        double ms = (PlatNow() - t0) * 1000.0 / 20.0;
        printf("avg render %.2f ms\n", ms);
    }
    /* 把绘制耗时也报出来，省得靠猜 */
    printf("theme=%d state=%d render=%dx%d\n", theme, (int)currentState, renderW, renderH);
    return 0;
}

/* 把指定的手持精灵导出成 PPM，用来肉眼检查建模。
 * 用法：sprite_dump.exe <outdir> [spriteId ...]
 * 不传 id 就导出全部 SPR_ITEM_* 与 SPR_PHASE_HAND。 */
#include "common.h"
#include "render.h"
#include "sprites.h"
#include "achievements.h"   /* InitAchievements */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile int g_dbgMark = 0;

static void writePpm(const char* path, int w, int h, const unsigned int* px) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
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
    printf("  wrote %s (%dx%d)\n", path, w, h);
}

int main(int argc, char** argv) {
    const char* dir = (argc > 1) ? argv[1] : ".";
    char path[512];

    InitAchievements();
    InitPixels();
    SpritesInit();

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            int id = atoi(argv[i]);
            if (id < 0 || id >= SPR_COUNT) continue;
            const Sprite* sp = SpriteGet(id);
            if (!sp) continue;
            snprintf(path, sizeof(path), "%s/spr_%03d.ppm", dir, id);
            writePpm(path, sp->w, sp->h, sp->px);
        }
    } else {
        int ids[8] = { SPR_ITEM_FLARE, SPR_ITEM_SLING, SPR_ITEM_SMOKE,
                       SPR_ITEM_GLUE, SPR_ITEM_ECHO, SPR_ITEM_DECOY,
                       SPR_PHASE_HAND, SPR_ITEM_FLARE };
        for (int i = 0; i < 7; i++) {
            const Sprite* sp = SpriteGet(ids[i]);
            if (!sp) continue;
            snprintf(path, sizeof(path), "%s/spr_%03d.ppm", dir, ids[i]);
            writePpm(path, sp->w, sp->h, sp->px);
        }
    }
    return 0;
}

/* ==================================================================
 * CANITY — 存档地图 dump（v2.4-rc 诊断工具）
 * ------------------------------------------------------------------
 * 为什么要它
 *   "房间有没有放进去、柱子有没有挖出来"这类问题，
 *   靠看源码和猜都没用 —— 必须看**真实生成出来的那张图**。
 *
 * 做法
 *   存档里就有 worldMap（SaveBlob 的一个字段），
 *   所以直接解析 .sav 就能拿到上一层的完整地图，
 *   不需要在游戏里加任何调试代码。
 *
 * 用法
 *   gcc tools/map_dump.c -Iinclude -std=c99 -o map_dump
 *   map_dump <canity0.sav>
 *
 * 输出
 *   '#' = 墙   '.' = 空地   'P' = 玩家   'E' = 出口
 * ================================================================ */
#include "common.h"
#include "save_format.h"
#include "save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: map_dump <save.sav>\n");
        return 2;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 2; }

    unsigned char* raw = (unsigned char*)malloc((size_t)n);
    if (!raw) { fclose(f); return 2; }
    if (fread(raw, 1, (size_t)n, f) != (size_t)n) { free(raw); fclose(f); return 2; }
    fclose(f);

    if (n <= (long)sizeof(SaveEnvelope)) {
        fprintf(stderr, "file too small for an envelope\n");
        free(raw);
        return 1;
    }

    SaveEnvelope h;
    memcpy(&h, raw, sizeof(h));
    if (h.magic != SAVE_ENVELOPE_MAGIC) {
        fprintf(stderr, "not a CSAV envelope (magic 0x%08X)\n", h.magic);
        free(raw);
        return 1;
    }

    int payLen = (int)(n - (long)sizeof(SaveEnvelope));
    if (!SaveEnvelopeValid(&h, raw + sizeof(h), payLen)) {
        fprintf(stderr, "envelope checksum mismatch (corrupt save)\n");
        free(raw);
        return 1;
    }

    const void* body = NULL;
    int size = 0;
    if (!SaveContainerFind(raw + sizeof(h), payLen, SC_LEGACY29, &body, &size) || !body) {
        fprintf(stderr, "no SC_LEGACY29 chunk\n");
        free(raw);
        return 1;
    }

    SaveBlob blob;
    memset(&blob, 0, sizeof(blob));
    int copy = (size < (int)sizeof(blob)) ? size : (int)sizeof(blob);
    memcpy(&blob, body, (size_t)copy);

    printf("version=%d  level=\n", blob.version);

    int px = (int)blob.posX, py = (int)blob.posY;
    int ex = (int)blob.exitX, ey = (int)blob.exitY;

    printf("    ");
    for (int x = 0; x < MAP_WIDTH; x++) printf("%d", x % 10);
    printf("\n");
    for (int y = 0; y < MAP_HEIGHT; y++) {
        printf("%3d ", y);
        for (int x = 0; x < MAP_WIDTH; x++) {
            char c;
            if (x == px && y == py) c = 'P';
            else if (x == ex && y == ey) c = 'E';
            else c = blob.worldMap[y][x] ? '#' : '.';
            putchar(c);
        }
        putchar('\n');
    }

    /* 死胡同计数（1 格宽走廊的端头），顺便给个"这张图有多像迷宫"的指标 */
    int deadEnds = 0, floors = 0;
    for (int y = 1; y < MAP_HEIGHT - 1; y++)
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (blob.worldMap[y][x]) continue;
            floors++;
            int open = 0;
            if (!blob.worldMap[y - 1][x]) open++;
            if (!blob.worldMap[y + 1][x]) open++;
            if (!blob.worldMap[y][x - 1]) open++;
            if (!blob.worldMap[y][x + 1]) open++;
            if (open == 1) deadEnds++;
        }
    printf("\nfloors=%d  deadEnds=%d\n", floors, deadEnds);

    free(raw);
    return 0;
}

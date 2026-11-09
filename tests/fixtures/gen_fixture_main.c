/* =====================================================================
 * 存档夹具生成器（fixture generator）
 * ---------------------------------------------------------------------
 * 目的：用**旧版本自己的序列化器**产出真实布局的 .sav，
 *       给 v2.4.2 的旧档迁移测试当夹具。
 *
 * 为什么不用「从新 SaveBlob 截断」：那是伪造 —— 新版结构体末尾多了字段，
 * 截断出来的字节在旧解码器眼里可能恰好合法，测不出偏移错误。
 *
 * 做法：链接旧 tag 的全部游戏源码（排除 main.c 与网页后端），
 *       用这个文件顶替入口，设置最小可信状态后调用旧版的 SaveGame()。
 * ===================================================================== */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 旧版本 main.c 里的调试标记（被部分模块引用） */
#ifndef CANITY_SHARED_DIAGNOSTICS
volatile int g_dbgMark = 0;
#endif

/* 各版本都存在的全局（都在 game.c 或 save.c 里） */
extern int   runInProgress;      /* 200: 当前是否有一局未结束 */
extern int   currentLevel;
extern float posX, posY, dirX, dirY;
extern float sanity, battery;
extern int   g_saveSlot;

extern void SaveGame(void);

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "fixture.sav";
    char name[260];
    int found = 0;
    FILE* f = NULL;

    g_saveSlot = 0;
#ifdef _WIN32
    _putenv_s("CANITY_DATA_DIR", ".");
#endif

    /* 构造一个「有对局」的最小状态。
       只看字段是否存在，不追求玩法合理 —— 夹具要的是**布局**，
       不是一局真实的游戏进度。 */
    runInProgress = 1;
    currentLevel  = 3;
    posX = 12.5f; posY = 9.5f;
    dirX = 0.8f;  dirY = 0.6f;
    sanity  = 61.5f;
    battery = 47.0f;

    SaveGame();

    /* SaveGame 自己选候选路径；扫出产物后改名成 argv[1] */
    for (int i = 0; i < 8 && !found; i++) {
        snprintf(name, sizeof(name), "canity%d.sav", i);
        f = fopen(name, "rb");
        if (f) { fclose(f); found = 1; }
    }
    if (!found) {
        snprintf(name, sizeof(name), "canity.sav");
        f = fopen(name, "rb");
        if (f) { fclose(f); found = 1; }
    }
    if (!found) {
        printf("FAIL: SaveGame 没有产出文件\n");
        return 2;
    }
    remove(out);
    if (rename(name, out) != 0) {
        printf("FAIL: 无法重命名 %s -> %s\n", name, out);
        return 3;
    }
    {
        long sz = 0;
        f = fopen(out, "rb");
        if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
        printf("OK: %s  %ld bytes\n", out, sz);
    }
    return 0;
}

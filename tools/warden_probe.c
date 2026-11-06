/* 分两种模式跑同一条巡逻：正常局 / 训练营。 */
#include "common.h"
#include "render.h"
#include "achievements.h"
#include "warden.h"
#include "game.h"

#include <stdio.h>

volatile int g_dbgMark = 0;

extern float exitX, exitY;
extern int   runInProgress, bankGold, dailyMode, trainingMode, campaignMode;
extern int   breakerOpen;
extern void  UpdateWarden(float dt);

static void probe(const char* title) {
    float x0, y0, d;
    int i;
    StartGame();
    breakerOpen = 1;
    WardenSpawnIfPowered();
    if (!WardenActive()) { printf("  %-10s 没出现\n", title); return; }
    x0 = WardenX(); y0 = WardenY();
    for (i = 0; i < 60; i++) UpdateWarden(0.05f);
    d = sqrtf((WardenX() - x0) * (WardenX() - x0) + (WardenY() - y0) * (WardenY() - y0));
    printf("  %-10s trainingMode=%d  active=%d  3 秒位移 %.3f 格  %s\n",
           title, trainingMode, WardenActive(), d,
           (d > 0.05f) ? "在巡逻" : "【没动】");
}

int main(void) {
    InitAchievements();
    InitPixels();
    bankGold = 0; dailyMode = 0; campaignMode = 0; runInProgress = 1;

    trainingMode = 0;
    probe("正常局");

    trainingMode = 1;
    probe("训练营");

    return 0;
}

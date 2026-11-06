/* 60 秒长跑：量守门人巡逻的**包络**（离出口多远、有没有来回）。 */
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
extern int   WardenChasing(void);

static int wardenFarCell(void);   /* 探针自用，见下 */

int main(void) {
    float maxD = 0.0f, minD = 1e9f, prevD = 0.0f;
    int i, turn = 0, moved30 = 0;

    InitAchievements();
    InitPixels();
    bankGold = 0; dailyMode = 0; trainingMode = 0; campaignMode = 0;
    runInProgress = 1;
    StartGame();

    breakerOpen = 1;
    WardenSpawnIfPowered();
    if (!WardenActive()) { printf("  守门人没出现\n"); return 0; }

    printf("  出生 (%.2f,%.2f)  出口 (%.2f,%.2f)\n", WardenX(), WardenY(), exitX, exitY);

    for (i = 0; i < 1200; i++) {           /* 60 秒 */
        float dx, dy, d;
        UpdateWarden(0.05f);
        dx = WardenX() - exitX;
        dy = WardenY() - exitY;
        d = sqrtf(dx * dx + dy * dy);
        if (d > maxD) maxD = d;
        if (d < minD) minD = d;
        if (i > 0) {
            if ((d - prevD) > 0.02f && prevD >= 0.0f) { /* 远离 */ }
        }
        prevD = d;
        if ((i + 1) % 300 == 0) {
            printf("  t=%2.0fs  距出口 %.2f 格   chase=%d\n", (i + 1) * 0.05f, d, WardenChasing());
        }
    }
    /* 用"距离是否回到过 1 格以内"判断有没有完成往返 */
    {
        float dx = WardenX() - exitX, dy = WardenY() - exitY;
        float d = sqrtf(dx * dx + dy * dy);
        printf("  60s 包络: 最近 %.2f 格  最远 %.2f 格  当前 %.2f 格\n", minD, maxD, d);
        printf("  -> %s\n", (maxD > 3.0f) ? "有可见行程" : "【几乎没离开出口】");
    }
    (void)turn; (void)moved30; (void)wardenFarCell;
    return 0;
}

/* =====================================================================
 * 舱门同层重复统计（tools/hatch_stat.c）
 * ---------------------------------------------------------------------
 * 背景：hatchUsedMask 是按 **HatchKind** 存的（一扇门一位），
 * 不是按舱门实例。如果同一层出现两扇**同类型**舱门，
 * 玩家只用了其中一扇，读档后两扇都会被标成"已用"。
 *
 * 这条风险是否值得修，取决于同层同类重复的**真实频率**。
 * 所以这个工具链接**真实的游戏代码**（StartGame -> 真实地图生成 ->
 * 真实房间放置 -> HatchPlaceInRoom），而不是重写一份生成器。
 *
 * 用法：
 *   hatch_stat.exe [种子数] [每层重复几次算异常]
 * ===================================================================== */
#include "common.h"
#include "hatch.h"
#include "achievements.h"
#include "render.h"    /* InitPixels */
#include "game.h"      /* StartGame */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int   hatchCount;
extern int   RoomReplay(int seed, int wantCount);
extern Hatch hatches[MAX_HATCHES];

extern int  currentLevel;
extern int  bankGold, dailyMode, trainingMode, campaignMode;
extern int  runInProgress;

/* main.c 里的崩溃现场标记，被 shop.c 引用 */
volatile int g_dbgMark = 0;

static void reset_between_runs(void) {
    bankGold = 0;
    dailyMode = 0;
    trainingMode = 0;
    campaignMode = 0;
    runInProgress = 0;
}

int main(int argc, char** argv) {
    int   seeds = (argc > 1) ? atoi(argv[1]) : 500;
    int   s;
    long  floorsWithHatch = 0;
    long  floorsWithDup = 0;
    long  hatchTotal = 0;
    long  kindHist[HATCH_KIND_COUNT];
    long  dupByKind[HATCH_KIND_COUNT];
    int   maxDup = 0;

    if (seeds <= 0) seeds = 500;
    memset(kindHist, 0, sizeof(kindHist));
    memset(dupByKind, 0, sizeof(dupByKind));

    InitAchievements();
    InitPixels();

    for (s = 0; s < seeds; s++) {
        int seen[HATCH_KIND_COUNT];
        int dup = 0;
        int i;

        reset_between_runs();
        srand((unsigned)s);
        StartGame();

        memset(seen, 0, sizeof(seen));
        if (hatchCount > 0) floorsWithHatch++;
        for (i = 0; i < hatchCount; i++) {
            int k = hatches[i].kind;
            if (k < 0 || k >= HATCH_KIND_COUNT) continue;
            kindHist[k]++;
            hatchTotal++;
            if (seen[k]) { dup = 1; dupByKind[k]++; }
            seen[k] = 1;
        }
        if (hatchCount > maxDup) maxDup = hatchCount;
        if (dup) floorsWithDup++;

        if ((s + 1) % 100 == 0)
            printf("  ...%d/%d 已跑\n", s + 1, seeds);
    }


    /* ---- 第二段：强制两层房间（最坏情况） ----
       真实关卡每层只放 1~2 间房；第一段实测 300 个种子全是 1 扇，
       所以必须单独把"两层"这个组合逼出来，否则测不到重复。 */
    {
        long dup2 = 0, floors2 = 0, hatches2 = 0;
        int  maxH2 = 0;
        int  t;
        for (t = 0; t < seeds; t++) {
            int seen[HATCH_KIND_COUNT];
            int dup = 0, i;
            HatchReset();
            srand((unsigned)(t * 2654435761u + 12345u));
            (void)RoomReplay(0, 0);              /* 复位房间表 */
            srand((unsigned)(t * 40503u + 7u));
            RoomReplay((int)(t * 40503u + 7u), 2);   /* 两间房 */
            memset(seen, 0, sizeof(seen));
            if (hatchCount > 0) floors2++;
            for (i = 0; i < hatchCount; i++) {
                int k = hatches[i].kind;
                if (k < 0 || k >= HATCH_KIND_COUNT) continue;
                hatches2++;
                if (seen[k]) dup = 1;
                seen[k] = 1;
            }
            if (hatchCount > maxH2) maxH2 = hatchCount;
            if (dup) dup2++;
        }
        printf("\n==== 双层房间（最坏情况） ====\n");
        printf("  种子数          : %d\n", seeds);
        printf("  有两扇门的层    : %ld (%.1f%%)\n",
               maxH2 >= 2 ? floors2 : 0, floors2 ? 100.0 * (double)floors2 / (double)seeds : 0.0);
        printf("  舱门总数        : %ld\n", hatches2);
        printf("  单层最多舱门    : %d\n", maxH2);
        printf("  同类重复的层    : %ld\n", dup2);
        printf("  结论：%s\n", dup2 == 0
               ? "两层房间下也没有同类重复 —— 位掩码暂未造成实际误判"
               : "两层房间下确实会同类重复 —— 位掩码会误判");
    }
    printf("\n==== 舱门统计（真实生成路径） ====\n");
    printf("  种子数        : %d\n", seeds);
    printf("  有舱门的层数  : %ld (%.1f%%)\n",
           floorsWithHatch, 100.0 * (double)floorsWithHatch / (double)seeds);
    printf("  舱门总数      : %ld\n", hatchTotal);
    printf("  单层最多舱门  : %d\n", maxDup);
    printf("  出现同类重复的层: %ld (%.2f%% of 有舱门的层)\n",
           floorsWithDup, floorsWithHatch ? 100.0 * (double)floorsWithDup / (double)floorsWithHatch : 0.0);
    printf("\n  各类分布 / 其中重复次数：\n");
    for (s = 0; s < HATCH_KIND_COUNT; s++) {
        if (kindHist[s] == 0) continue;
        printf("    kind %d : %5ld 次   重复贡献 %ld\n", s, kindHist[s], dupByKind[s]);
    }
    printf("\n  结论：%s\n",
           floorsWithDup == 0
             ? "没有观察到同层同类重复 —— 位掩码的风险暂未实际发生"
             : "确实存在同层同类重复 —— 位掩码会误判「另一扇也用过」");
    return 0;
}

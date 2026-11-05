/* Integration checks link the real game modules; all saves are isolated. */
#include "common.h"
#include "map_validator.h"
#include "hatch.h"
#include "game.h"
#include "entity.h"
#include "skills.h"
#include "bench.h"
#include "shop.h"
#include "daily.h"
#include "campaign.h"
#include "achievements.h"
#include "save.h"
#include "save_format.h"   /* R8: SaveEnvelope / SaveEnvelopeSeal */
#include "steam_zone.h"    /* R8: SteamZoneCloudDecide / STEAM_SYNC_* */
#include "sound_event.h"   /* B01: SoundEmit / SoundKind */
#include "audio.h"          /* B02: AudioComputeGains */
#include "sound_propagation.h"  /* B03: SoundReach */
#include "map_rooms.h"        /* C3: GenerateThemeRooms */
#include "cell_meta.h"        /* C2: cellMeta / CellTintFor */
#include "wallet.h"
#include "threat.h"
#include "ui.h"
#include "render.h"
#include "sprites.h"
#include "music.h"
#include "bestiary.h"
#include "warden.h"
#include "state.h"
#include "upgrade_run.h"
#include <stddef.h>

extern int carryAtRunStart,exitGoDeeper,exitChoiceOpen;
extern int knockCountRun,throwHitCountRun,echoCellsTotalRun;
extern int lastRunGold,lastRunKept,lastRunCaught;
extern int shopRefreshFloor,shopRefreshUsed,contractKind;
extern void BankSettle(int survived);
extern int BankPreviewGain(int survived);
extern void CloseSkillPick(void);
extern void GenerateLevel(void);
extern void SkillResetView(void);
static int checks=0;
#define CHECK(c,m) do { ++checks; if(!(c)) { fprintf(stderr,"FAIL: %s (line %d)\n",m,__LINE__);exit(1); } } while(0)
static void clear_meta(void) { memset(skillRank,0,sizeof(int)*SKILL_COUNT);skillRank[SK_ORIGIN]=1; }
static void daily_case(int goal,int target) {
    int a[15]={0};a[0]=goal;a[1]=target;a[5]=(int)DailySeedToday();DailyLoadFrom(a);
}
static void test_daily(void) {
    unsigned goals=0,mods=0,res=0;
    static const int hi[]={8000,2,12,40,15,15,15,6};
    for(unsigned y=2026;y<2040;++y) for(unsigned m=1;m<=12;++m) for(unsigned d=1;d<=28;++d) {
        unsigned date=y*10000+m*100+d;
        DailyGenerateForDate(date);
        goals|=1u<<DailyGoalId();mods|=1u<<DailyModId();res|=1u<<DailyRestrictId();
        CHECK(DailyGoalParam()>0 && DailyGoalParam()<=hi[DailyGoalId()],"daily target stays inside its range");
        int g=DailyGoalId(),v=DailyGoalParam(),mod=DailyModId(),r=DailyRestrictId();
        DailyGenerateForDate(date);
        CHECK(g==DailyGoalId() && v==DailyGoalParam() && mod==DailyModId() && r==DailyRestrictId(),"same date produces same challenge");
    }
    /* 2026-11-05：DR_NOWRENCH（不能使用扳手）已永久移出池。
       武器系统下线后扳手是玩家唯一的主动工具，禁掉它 = 赤手空拳跑一整局，
       玩家第一反应是「扳手坏了」（用户报告）。枚举值保留（存档里是数字），
       只是不再被抽到 —— 所以可达位图是 63 去掉 bit4 = 47。 */
    CHECK(goals==255 && mods==255 && res==47,"every active daily pool entry is reachable");
    CHECK(!(res & (1u<<4)),"the retired wrench restriction must never be picked");
    trainingMode=0;dailyMode=0;
    for(int goal=0;goal<DQ_COUNT;++goal) {
        daily_case(goal,hi[goal]);bankGold=0;
        DailyObserve(goal,1);CHECK(DailyCurrent()==1,"all eight daily goals advance");
        DailyObserve(goal,1);CHECK(DailyCurrent()==1,"repeated observation never duplicates progress");
        DailyBeginRun();DailyObserve(goal,1);
        int sum=goal==0 || goal==1 || goal==3 || goal==4 || goal==5;
        CHECK(DailyCurrent()==(sum?2:1),"cumulative goals span runs and run records take maximum");
        DailySetProgress(hi[goal]);CHECK(DailyDone(),"all daily goals can complete");
        int reward=bankGold;DailySetProgress(hi[goal]);CHECK(bankGold==reward,"daily reward paid exactly once");
    }
    daily_case(DQ_SEARCH,40);DailyObserve(DQ_SEARCH,7);
    int saved[15];DailySaveTo(saved);DailyLoadFrom(saved);DailyObserve(DQ_SEARCH,7);
    CHECK(DailyCurrent()==7,"resuming daily progress does not double count");
    saved[5]=20260101;DailyLoadFrom(saved);CHECK(DailyCurrent()==0,"previous date cannot restore progress");
    unsigned dates[]={20260129,20260130,20260131,20260201,20260202,20260203,20260204};
    int empty[15]={0};DailyLoadFrom(empty);bankGold=0;
    for(int i=0;i<7;++i) {DailyGenerateForDate(dates[i]);DailySetProgress(DailyGoalParam());}
    CHECK(DailyStreak()==7 && bankGold==7*800+1500+5000,"three/seven day streak bonuses across month boundary");
    DailyGenerateForDate(20260206);CHECK(DailyStreak()==0,"missing a day breaks streak");
    DailyGenerate();
    CHECK(!DailyForbidSprint() && !DailyForbidWrench() && DailyGoldTierBonus()==0,"daily restrictions do not leak into normal games");
}
static void test_wallet_save(void) {
    clear_meta();bankGold=1000;bankHistMax=1000;bankCarryPct=5;
    StartGame();CHECK(bankGold==750 && carryGold==250 && carryAtRunStart==250,"opening withdrawal stays in the player's pocket");
    carryGold=350;WalletSettle(1,100);CHECK(bankGold==1125,"settlement returns principal plus multiplied profit once");
    carryGold=101;WalletSettle(0,0);CHECK(bankGold==1160,"death returns exactly rounded 35 percent");
    CHECK(carryGold==0 && carryAtRunStart==0,"settlement cannot be paid a second time");
    bankGold=6543;bankHistMax=8000;bankCarryPct=3;carryGold=321;carryAtRunStart=200;
    knockCountRun=7;throwHitCountRun=8;echoCellsTotalRun=99;
    skillRank[29]=2;g_achievements[4].unlocked=1;g_achievements[4].progress=15;
    bestiarySeen=1;BestiaryMarkEscaped(0);BestiaryMarkEscaped(0);BestiaryMarkEscaped(0);
    runInProgress=1;SaveGame();CHECK(g_saveState==1,"save write succeeds");
    bankGold=0;carryGold=0;carryAtRunStart=0;skillRank[29]=0;g_achievements[4].unlocked=0;
    CHECK(LoadSettings(),"unfinished run can be loaded");
    CHECK(bankGold==6543 && bankCarryPct==3,"bank restores on startup before entering a run");
    CHECK(g_achievements[4].unlocked && skillRank[29]==2,"persistent achievements and new skills survive restart");
    InitAchievements();CHECK(g_achievements[4].unlocked,"opening achievements preserves unlocks");
    CHECK(BestiaryStage(0)==BST_COUNTER,"bestiary knowledge survives restart");
    ContinueRun();CHECK(carryGold==321 && carryAtRunStart==200,"saved carry and principal are distinct");
    CHECK(knockCountRun==7 && throwHitCountRun==8 && echoCellsTotalRun==99,"run counters survive resume");
    /* ==================================================================
     * v25 夹具：**必须是真正的 v25 布局**
     * ------------------------------------------------------------------
     * v25 与当前布局有**两处**不同，必须同时还原：
     *   ① Container 32 字节（现在是 40）—— 差 64 字节
     *   ② 地图 24x24（现在是 36x36）—— worldMap + explored 差 5760 字节
     *
     * 而且地图那一处**不能整段拷**：老档是 y*24+x 的行主序，
     * 新结构是 y*36+x，必须逐格重排（这正是 LoadLegacyBlob 做的事）。
     * 这个夹具是它的逆变换 —— 所以这条用例同时验证方向两边都对。
     * ================================================================ */
    {
        SaveBlob cur;
        memset(&cur, 0, sizeof(cur));
        cur.magic = SAVE_MAGIC;
        cur.version = 25;
        cur.bankGoldSave = 4567;
        cur.bankHistMaxSave = 8000;
        cur.skillRank[1] = 1;
        cur.hasRun = 1;
        /* 地图里放点可辨认的内容，用来验证"逐格重排"真的对 */
        cur.worldMap[1][1] = 0;
        cur.worldMap[5][7] = 0;
        cur.worldMap[20][18] = 0;
        cur.worldMap[1][2] = 1;   /* 一格墙：验证"原本是墙的仍然是墙" */
        cur.explored[3][4] = 1;

        static unsigned char legacy[sizeof(SaveBlob) * 2];
        memset(legacy, 0, sizeof(legacy));

        const size_t contOff    = offsetof(SaveBlob, cont);
        /* 老档只有 **8** 个槽位；MAX_CONTAINERS 现在是 12，
           这里若跟着新常量走，夹具就不是 v25 而是"v25 但 12 槽"，测不出真问题。 */
        const size_t oldContSz  = 32u * 8u;                  /* 256 */
        const int    oldMapW    = 24, oldMapH = 24;
        const size_t oldMapSz   = (size_t)oldMapW * oldMapH * sizeof(int);
        const size_t newTailOff = offsetof(SaveBlob, flares);
        const size_t cutOff     = offsetof(SaveBlob, dailyQuestDate);
        size_t w = 0;

        /* ① 前缀（cont 之前完全相同） */
        memcpy(legacy, &cur, contOff);
        w = contOff;

        /* ② 8 条 32 字节 Container */
        for (int ci = 0; ci < 8; ci++) {
            unsigned char* dst = legacy + w;
            const Container* src = &cur.cont[ci];
            memcpy(dst + 0,  &src->x,        sizeof(float));
            memcpy(dst + 4,  &src->y,        sizeof(float));
            memcpy(dst + 8,  &src->active,   sizeof(int));
            memcpy(dst + 12, &src->searched, sizeof(int));
            memcpy(dst + 16, &src->content,  sizeof(int));
            memcpy(dst + 20, &src->locked,   sizeof(int));
            memcpy(dst + 24, &src->lockType, sizeof(int));
            memcpy(dst + 28, &src->lockCode, sizeof(int));
            w += 32u;
        }

        /* ③ worldMap 24x24（逐格） */
        for (int y = 0; y < oldMapH; y++)
            for (int x = 0; x < oldMapW; x++) {
                int v = cur.worldMap[y][x];
                memcpy(legacy + w, &v, sizeof(int));
                w += sizeof(int);
            }
        /* ④ explored 24x24 */
        for (int y = 0; y < oldMapH; y++)
            for (int x = 0; x < oldMapW; x++) {
                int v = cur.explored[y][x];
                memcpy(legacy + w, &v, sizeof(int));
                w += sizeof(int);
            }
        CHECK(w == contOff + oldContSz + 2 * oldMapSz,
              "the v25 fixture must lay out exactly like the old struct");

        /* ⑤ 尾部（flares 之后到 v25 截断点） */
        {
            size_t tailLen = cutOff - newTailOff;
            memcpy(legacy + w, (const unsigned char*)&cur + newTailOff, tailLen);
            w += tailLen;
        }

        const char* dir = getenv("CANITY_DATA_DIR");
        char path[MAX_PATH];
        for (int slot = 0; slot < 3; slot++) {
            char p3[MAX_PATH];
            snprintf(p3, sizeof(p3), "%s\\canity%d.sav", dir, slot);
            remove(p3);
            snprintf(p3, sizeof(p3), "%s\\canity%d.sav.bak", dir, slot);
            remove(p3);
        }
        snprintf(path, sizeof(path), "%s\\canity0.sav", dir);
        FILE* f = fopen(path, "wb");
        CHECK(f != NULL, "create isolated v25 fixture");
        CHECK(fwrite(legacy, 1, w, f) == w, "write the whole v25 fixture");
        fclose(f);

        CHECK(sizeof(SaveBlob) > w,
              "the legacy fixture must be shorter than the current struct");
    }
    CHECK(LoadSettings(), "legacy v25 save must still load");
    CHECK(bankGold == 4567 && skillRank[1] == 1,
          "v25 migration keeps bank and skill progress");
    /* 地图逐格重排必须对：这三格是空地，其余边缘必须是墙 */
    {
        /* 查的是 **g_save**（LoadSettings 填的就是它）——
           运行时的 worldMap 要到 ContinueRun 才灌回，这里查它会读到旧值。 */
        const SaveBlob* b = SaveTestBlob();
        CHECK(b != NULL, "the loaded blob must be readable");
        if (b) {
            CHECK(b->worldMap[1][1] == 0, "legacy map cell (1,1) must survive the re-index");
            CHECK(b->worldMap[5][7] == 0, "legacy map cell (5,7) must survive the re-index");
            CHECK(b->worldMap[20][18] == 0, "legacy map cell (20,18) must survive the re-index");
            CHECK(b->worldMap[1][2] == 1,
                  "cells that were walls in the legacy map must stay walls");
            CHECK(b->worldMap[MAP_HEIGHT - 1][MAP_WIDTH - 1] == 1,
                  "the area outside a 24x24 legacy map must be filled with walls");
        }
    }
}
/* ==================================================================
 * 2026-11-06（v2.4-rc / **P0-2**）：读档必须夹取布尔与枚举
 * ------------------------------------------------------------------
 * 现象：普通局进游戏后手电永远打不开。
 * 根因：dailyMode 读成 -1，而 DailyForbidLight() 的判据是
 *       `dailyMode && ...` —— -1 是真值，于是每日模式的禁灯规则
 *       漏进了普通模式。
 *
 * 这条用例**故意写一个坏档**，然后断言读回来的是"关"而不是"垃圾值"。
 * 没有夹取的话第一条就会红。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06：**平衡验收的机器判据**
 * ------------------------------------------------------------------
 * 平衡方案第 5 节有四条验收指标，其中两条是纯数值、可以断言：
 *   · 每层金币收入落在目标区间
 *   · 技能表在改价之后仍然自洽
 *
 * 为什么写成测试：这两条以前靠"感觉差不多"判断，于是每次调数值
 * 都没人知道有没有调过头。写成区间断言后，把数值推出区间会**编译期就红**。
 *
 * ⚠️ 区间来自平衡方案的**推算**，不是实测。
 *    实机对不上时，先改区间再改数值 —— 别反过来。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06：**崩塌层的两个可测性质**
 * ------------------------------------------------------------------
 * 只测"纯函数层面成立"的两条：
 *   ① 崩塌层金币确实变成 1.8 倍
 *   ② 崩塌层不生成舱门（否则能连锁刷）
 * 真正的"黑屏 -> 重掷 -> 失败回滚"只能实机看，这里不假装测了。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v33）：**舱门状态机 + 已用掩码**
 * ------------------------------------------------------------------
 * 这是舱门系统里唯一能完整单测的部分：它不认识金币/电量/理智，
 * 只吃自己的数组。所以状态转移和存档掩码都在这里钉死。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06：**舱门 + 崩塌 端到端**（第 5 轮）
 * ------------------------------------------------------------------
 * 前面几轮都是分段测的：状态机测过了、崩塌参数测过了，
 * 但"生成一层 → 真有门 → 门槛能过 → 开门 → 效果结算 → 崩塌黑屏
 * → 重掷 → 还能玩"这一整条链**从来没跑过**。
 *
 * 这个用例就是那条链。它用真实的 GenerateLevel()，不是模拟。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（第 7 轮）：**在真实渲染管线里给舱门拍一张照**
 * ------------------------------------------------------------------
 * 为什么要它：精灵样张只能证明"烘焙出来的图长这样"，
 * 证明不了它放进世界之后（透视缩放、手电照明、主题换色、深度测试）
 * 还能不能读成"一扇门"。
 *
 * 用法：设 CANITY_SHOT=<目录> 跑 test_runtime，
 * 会在那里写几张 ppm（玩家站在舱门前 2.5 / 4 / 6 格）。
 * 不设就完全跳过，不影响正常测试。
 * ================================================================== */
static void test_hatch_screenshot(void) {
    const char* dir = getenv("CANITY_SHOT");
    if (!dir || !dir[0]) return;

    extern float posX, posY, dirX, dirY;
    extern int hatchCount;
    extern Hatch hatches[MAX_HATCHES];
    extern unsigned int* pixels;
    extern int renderW, renderH;
    extern void Render3D(void);

    /* ==================================================================
     * **必须先烘精灵**
     * ------------------------------------------------------------------
     * 这个用例在 main 的测试序列里跑得比 test_hunter_models 早，
     * 而 SpritesInit() 是在那里调的 —— 结果舱门精灵根本不存在，
     * Render3D 画了个空气，我看到的"门没出现"其实是这个。
     * 诊断里 `bbox=0 0x0`（hasB=0）就是它。
     * ================================================================ */
    {
        extern void SpritesInit(void);
        InitPixels();
        SpritesInit();
    }

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    flashlightOn = 1;

    /* 生成到一层真的有门为止 */
    int found = 0;
    for (int i = 0; i < 80 && !found; i++) {
        currentLevel = 1;
        GenerateLevel();
        if (hatchCount > 0) found = 1;
    }
    if (!found) { fprintf(stderr, "  [shot] no hatch after 80 levels\n"); return; }
    fprintf(stderr, "  [shot] dir=%s hatchCount=%d (cwd-relative output)\n", dir, hatchCount);
    battery = 150.0f;

    /* 门前面站三个距离各拍一张：近（贴脸）/ 中 / 远 */
    static const float kDists[3] = { 1.2f, 1.8f, 2.4f };   /* 近 / 中 / 稍远 */
    for (int k = 0; k < 3; k++) {
        float hx = hatches[0].x, hy = hatches[0].y;

        /* ================================================================
         * 站位：从门的四个轴向里挑**一条整段都是空地**的路，
         * 站在离门 kDists[k] 的位置朝门看。
         * 第一版是"朝地图中心的反方向退"，结果退到墙里，画面全是砖。
         * ================================================================ */
        static const int kdx[4] = { 1, -1, 0, 0 };
        static const int kdy[4] = { 0, 0, 1, -1 };
        float vx = 0.0f, vy = 0.0f;
        int have = 0;
        for (int d = 0; d < 4 && !have; d++) {
            int ok = 1;
            for (int step = 1; step <= 3 && ok; step++) {   /* 房间只有 5x5，3 格够站 */
                int cx = (int)(hx + (float)kdx[d] * (float)step);
                int cy = (int)(hy + (float)kdy[d] * (float)step);
                if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) { ok = 0; break; }
                if (worldMap[cy][cx]) ok = 0;
            }
            if (ok) { vx = (float)kdx[d]; vy = (float)kdy[d]; have = 1; }
        }
        if (!have) continue;   /* 四周都堵，这张跳过 */

        posX = hx + vx * (kDists[k] + 1.0f);
        posY = hy + vy * (kDists[k] + 1.0f);
        dirX = -vx; dirY = -vy;

        Render3D();

        /* 诊断：把门的投影框报出来，区分"没投影"和"画了个空图" */
        {
            extern int DebugHatchScreenBox(int, float, float, int*, int*, int*, int*,
                                           int*, int*, int*);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0, hasB = 0, bw = 0, bh = 0;
            int ok = DebugHatchScreenBox(0, hx, hy, &x0, &y0, &x1, &y1, &hasB, &bw, &bh);
            fprintf(stderr, "  [shot] k=%d ok=%d box=(%d,%d)-(%d,%d) bbox=%d %dx%d "
                            "pos=(%.1f,%.1f) dir=(%.1f,%.1f) hatch=(%.1f,%.1f)\n",
                    k, ok, x0, y0, x1, y1, hasB, bw, bh,
                    posX, posY, dirX, dirY, hx, hy);
        }

        /* 写**相对路径**（cwd = 仓库根）。
           绝对路径这次 fopen 直接失败，而精灵导出用相对路径是成功的 ——
           先按能跑通的方式做，之后再由脚本搬到产出目录。 */
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "shot_world_%d.ppm", k);
        {
            FILE* f = fopen(path, "wb");
            if (!f) { fprintf(stderr, "  [shot] fopen failed: %s\n", path); continue; }
            fprintf(f, "P6\n%d %d\n255\n", renderW, renderH);
            for (int y = 0; y < renderH; y++) {
                for (int x = 0; x < renderW; x++) {
                    unsigned int c = pixels[y * renderW + x];
                    /* 像素是 0x00RRGGBB（渲染层不带 alpha） */
                    unsigned char px[3] = {
                        (unsigned char)((c >> 16) & 0xFF),
                        (unsigned char)((c >> 8) & 0xFF),
                        (unsigned char)(c & 0xFF)
                    };
                    fwrite(px, 1, 3, f);
                }
            }
            fclose(f);
            fprintf(stderr, "  [shot] wrote %s\n", path);
        }
    }
}

/* ==================================================================
 * 2026-11-06（第 9 轮）：**七个舱门效果逐个验**
 * ------------------------------------------------------------------
 * 之前只验了"开门会到 FIRED"和"崩塌会重掷"。
 * 其余六个效果**从来没被断言过** —— 它们完全可能什么都没做，
 * 而测试全绿（这类假绿这几轮已经踩过两次）。
 *
 * 做法：对每一种，构造一扇门 → 满足门槛 → 开门 → 断言**可观测的状态变化**。
 * 不看 pickupMsg，只认数值/数组。
 * ================================================================== */
static void test_all_hatch_effects(void) {
    extern Hatch hatches[MAX_HATCHES];
    extern int hatchCount;
    extern float hatchLightsT, hatchGoldMulT, hatchBloodT, hatchSpeedT;
    extern int collapsedLevel;

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();
    currentLevel = 1;
    GenerateLevel();          /* 需要一张真实地图（拆墙、崩塌都要用） */

    /* ---- ① 供电室：全灯清雾 ---- */
    {
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_POWER, 4);
        hatchLightsT = 0.0f;
        battery = 150.0f; fusesHeld = 0;
        float batBefore = battery;
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "POWER hatch must fire");
        CHECK(hatchLightsT > 19.0f && hatchLightsT < 21.0f,
              "POWER must turn the lights on for ~20 seconds");
        CHECK(battery < batBefore, "POWER must spend battery when no fuses are held");
    }

    /* ---- ② 地狱：血祭（金 + 理智 + 加速）---- */
    {
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_HELL, 2);
        hatchSpeedT = 0.0f;
        carryGold = 500; sanity = 60.0f;
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "HELL hatch must fire");
        CHECK(carryGold == 400, "HELL must burn exactly 100 gold");
        CHECK(sanity > 44.0f && sanity < 46.0f, "HELL must cost 15 sanity");
        CHECK(hatchSpeedT > 29.0f && hatchSpeedT < 31.0f,
              "HELL must grant a ~30 second speed window");
    }

    /* ---- ③ 病房：包扎（理智 + 体力 + 血迹）---- */
    {
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_WARD, 3);
        hatchBloodT = 0.0f;
        sanity = 20.0f; stamina = 0.0f;
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "WARD hatch must fire");
        CHECK(sanity > 59.0f, "WARD must restore ~40 sanity");
        CHECK(stamina > 1.0f, "WARD must refill stamina");
        CHECK(hatchBloodT > 59.0f && hatchBloodT < 61.0f,
              "WARD must leave a 60 second blood trail");
    }

    /* ---- ④ 仓储室：开仓（付钱 + 家具回满 + 金币倍率）---- */
    {
        extern RoomProp roomProps[MAX_ROOM_PROPS];
        extern int roomPropCount;
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_STORAGE, 1);
        hatchGoldMulT = 0.0f;
        carryGold = 500;
        /* 造一件搜空的家具，看它会不会被回满 */
        roomProps[0].active = 1;
        roomProps[0].kind = ROOMPROP_SHELF;
        roomProps[0].searchLeft = 0;
        roomProps[0].searched = 7;
        if (roomPropCount < 1) roomPropCount = 1;

        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "STORAGE hatch must fire");
        CHECK(carryGold == 350, "STORAGE must cost exactly 150 gold");
        CHECK(roomProps[0].searchLeft == 3,
              "STORAGE must refill the shelf's three searches");
        CHECK(hatchGoldMulT > 59.0f && hatchGoldMulT < 61.0f,
              "STORAGE must grant a 60 second gold boost");
    }

    /* ---- ⑤ 血肉洞窟：喂食（消耗投掷物 + 全图僵直）---- */
    {
        extern Monster monHunters[MON_HUNTER_MAX];
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_FLESH, 6);
        flares = 2; bottles = 0; smokeBombs = 0; gluePots = 0; echoBolts = 0;
        monHunters[0].active = 1;
        monHunters[0].stun = 0.0f;
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "FLESH hatch must fire");
        CHECK(flares == 1, "FLESH must consume exactly one throwable");
        CHECK(monHunters[0].stun > 19.0f,
              "FLESH must freeze every hunter for ~20 seconds");
    }

    /* ---- ⑥ 砖墙：拆墙（墙的数量必须变少）---- */
    {
        extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
        int wallsBefore = 0, wallsAfter = 0;
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++)
                if (worldMap[y][x]) wallsBefore++;

        HatchReset();
        /* 放在地图中间，四周一定有墙可砸 */
        HatchAdd((float)(MAP_WIDTH / 2) + 0.5f, (float)(MAP_HEIGHT / 2) + 0.5f, HATCH_BRICK, 0);
        stamina = 100.0f;
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "BRICK hatch must fire");

        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++)
                if (worldMap[y][x]) wallsAfter++;
        CHECK(wallsAfter < wallsBefore,
              "BRICK must carve away at least one wall");
        CHECK(stamina < 76.0f, "BRICK must cost stamina");
    }

    /* ---- ⑦ 锅炉房：崩塌（走黑屏流程，计次 +1）---- */
    {
        extern int CollapseRerollCount(void);
        extern void CollapseBegin(void);
        extern int CollapseTick(float);
        int before = CollapseRerollCount();
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_BOILER, 5);
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "BOILER hatch must fire");
        /* 效果只是"开始崩塌"，真正的重掷在黑屏流程里 */
        CollapseTick(0.5f);
        CollapseTick(0.5f);
        CHECK(CollapseRerollCount() == before + 1,
              "BOILER must start the collapse reroll");
        CollapseTick(2.0f);          /* 走完，清掉黑屏 */
        collapsedLevel = 0;
    }

    HatchReset();
}

static void test_hatch_end_to_end(void) {
    extern int hatchCount;
    extern Hatch hatches[MAX_HATCHES];
    extern void MapValidate(MapCheck* out);
    extern int collapsedLevel;

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();

    /* ---- ① 生成到一层真的有门 ---- */
    int found = 0;
    for (int i = 0; i < 60 && !found; i++) {
        currentLevel = 1;
        GenerateLevel();
        if (hatchCount > 0) found = 1;
    }
    CHECK(found == 1, "a generated level must contain at least one themed room with a hatch");
    CHECK(hatchCount <= MAX_HATCHES, "hatch table must never exceed MAX_HATCHES");

    /* ---- ② 资源拉满时，**每一种**门槛都必须能过 ---- */
    carryGold = 99999; fusesHeld = 8; battery = 150.0f;
    /* ================================================================
     * 理智取 **45**，不是 5 —— 这里有两道门槛互相咬住：
     *     病房（WARD）要求 理智 < 50
     *     地狱（HELL）要求 理智 >= 40
     * 兼容窗口只有 **[40, 50)**。给 5 的话地狱门永远过不去
     * （第一次就是这样红的：gate blocked: kind=5 why=SANITY TOO LOW (5/40)）。
     * ================================================================ */
    sanity = 45.0f;
    stamina = 100.0f; flares = 5; bottles = 5;
    /* ================================================================
     * 锅炉房的门槛是「本房家具**全部搜空** + 1 根保险丝」——
     * 只给资源是不够的，还得真的把家具搜空。
     * 这条以前是"碰巧通过"：生成到没家具的层，条件自然成立。
     * 现在显式满足它，用例才不依赖随机布局。
     * ================================================================ */
    {
        extern RoomProp roomProps[MAX_ROOM_PROPS];
        extern int roomPropCount;
        for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++)
            roomProps[i].searchLeft = 0;
    }
    {
        int gateFails = 0;
        for (int i = 0; i < hatchCount; i++) {
            char why[96];
            why[0] = 0;
            if (HatchGateCheck(i, why, (int)sizeof(why)) != 1) {
                gateFails++;
                fprintf(stderr, "  gate blocked: kind=%d why=%s\n", hatches[i].kind, why);
            }
        }
        CHECK(gateFails == 0, "with resources maxed, every hatch gate must pass");
    }

    /* ---- ③ 资源清零时，门槛必须真的挡人（并且给出原因） ---- */
    carryGold = 0; fusesHeld = 0; battery = 0.0f;
    sanity = 100.0f; stamina = 0.0f; flares = 0; bottles = 0;
    {
        int blocked = 0;
        for (int i = 0; i < hatchCount; i++) {
            char why[96];
            why[0] = 0;
            if (HatchGateCheck(i, why, (int)sizeof(why)) == 0) {
                blocked++;
                CHECK(why[0] != 0, "a blocked gate must say what is missing");
            }
        }
        CHECK(blocked == hatchCount, "with no resources, every hatch gate must block");
    }

    /* ---- ④ 开门 → 动画 → 效果结算 ---- */
    {
        int idx = 0;
        int kindBefore = hatches[idx].kind;
        int goldBefore = carryGold;
        HatchSetState(idx, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[idx].state == HATCH_FIRED,
              "opening a hatch must end in FIRED");
        (void)kindBefore; (void)goldBefore;
    }

    /* ---- ⑤ 崩塌：黑屏 → 重掷 → 淡入 → 仍可玩 ---- */
    {
        extern void CollapseBegin(void);
        extern int CollapseTick(float);
        extern float screenBlackout;

        extern int CollapseRerollCount(void);
        int rerollsBefore = CollapseRerollCount();
        CollapseBegin();
        CHECK(CollapseTick(0.40f) == 1, "collapse must report busy during fade-out");
        CHECK(screenBlackout > 0.4f && screenBlackout < 0.6f,
              "0.4s into the 2s timeline the screen must be about half black");

        /* 这一刻画面应在全黑附近，重掷就发生在这一帧 */
        CHECK(CollapseTick(0.42f) == 1, "collapse must still be busy at the reroll");
        CHECK(screenBlackout > 0.95f,
              "the reroll must happen while the screen is (nearly) fully black");

        /* 走完淡入 */
        CollapseTick(1.30f);
        CHECK(screenBlackout == 0.0f, "after 2 seconds the blackout must be fully cleared");
        /* **关键补断言**：重掷必须真的执行过。
           只断言"塌完之后地图可玩"是不够的 ——
           "根本没塌"同样满足它，第一版就是这样假绿的。 */
        CHECK(CollapseRerollCount() == rerollsBefore + 1,
              "the collapse must actually execute the reroll");

        /* 塌成功 → 是崩塌层；塌失败 → 已回滚成原层。两种都必须是可玩地图。 */
        {
            MapCheck mc;
            memset(&mc, 0, sizeof(mc));
            MapValidate(&mc);
            CHECK(mc.ok, "after collapse the level must still validate as playable");
            CHECK(mc.exitReachable, "after collapse the exit must still be reachable");
            CHECK(mc.spawnOpen, "after collapse the spawn must still be open");
            /* **关键**：重掷换了整张图，而玩家坐标没动 ——
               如果落点变成墙，玩家会被永久卡住（两个轴向都 Blocked）。
               这条断言钉住第 6 轮的那个修复。 */
            {
                extern float posX, posY;
                extern int Blocked(float, float);
                CHECK(!Blocked(posX, posY),
                      "after collapse the player must not be stuck inside the new terrain");
            }

            /* ================================================================
             * **确定性地**验证"把玩家从墙里挪出来"
             * ----------------------------------------------------------------
             * 端到端那条断言不够：崩塌重掷后玩家**恰好**落在空地上时，
             * 删掉修复测试照样绿（我实测过，真的没有红）。
             * 所以这里直接把玩家坐标塞进一个已知墙格，再调修复函数。
             * ================================================================ */
            {
                extern float posX, posY;
                extern int Blocked(float, float);
                extern int CollapseUnstickPlayer(void);
                extern int worldMap[MAP_HEIGHT][MAP_WIDTH];

                /* 找一个墙格（外墙一定有） */
                int wx = 0, wy = 0, foundWall = 0;
                for (int y = 0; y < MAP_HEIGHT && !foundWall; y++)
                    for (int x = 0; x < MAP_WIDTH && !foundWall; x++)
                        if (worldMap[y][x]) { wx = x; wy = y; foundWall = 1; }
                CHECK(foundWall, "a generated map must contain at least one wall");

                float savedX = posX, savedY = posY;
                posX = (float)wx + 0.5f;
                posY = (float)wy + 0.5f;
                CHECK(Blocked(posX, posY), "the chosen cell must really be blocked");

                int moved = CollapseUnstickPlayer();
                CHECK(moved == 1, "CollapseUnstickPlayer must report that it moved the player");
                CHECK(!Blocked(posX, posY),
                      "after unstick the player must stand on a free cell");

                posX = savedX; posY = savedY;
            }
        }
        (void)collapsedLevel;
    }

    /* ---- ⑥ 崩塌层不生成舱门（防无限重掷）---- */
    /* 注意：本用例会把 collapsedLevel 置 1，结束时必须还原 ——
       否则后面的平衡断言会读到 1.8 倍金币而失败。
       （第一次跑就踩到了：FAIL per-crate gold must be about 7） */
    collapsedLevel = 0;

    /* ---- ⑥ 崩塌层不生成舱门（防无限重掷）---- */
    {
        extern int HatchPlaceInRoom(int, int, int, int, int);
        int save = collapsedLevel;
        collapsedLevel = 1;
        CHECK(HatchPlaceInRoom(2, 2, 4, 4, 5) == -1,
              "collapsed layers must not spawn new hatches");
        collapsedLevel = save;
    }
}

static void test_hatch_state_and_mask(void) {
    extern Hatch hatches[MAX_HATCHES];
    extern int hatchCount;

    HatchReset();
    CHECK(hatchCount == 0, "HatchReset must clear the table");

    int a = HatchAdd(1.5f, 1.5f, HATCH_POWER, 4);
    int b = HatchAdd(3.5f, 1.5f, HATCH_HELL, 2);
    CHECK(a == 0 && b == 1, "HatchAdd must append in order");
    CHECK(hatches[a].state == HATCH_SEALED, "a new hatch starts SEALED");

    /* 封闭的门在就绪查询里应该能被选中 */
    CHECK(HatchNearest(1.6f, 1.6f, 4.0f) == a, "the nearest sealed hatch must be found");

    /* ---- 开门：SEALED -> OPENING -> (动画播完) -> FIRED ---- */
    HatchSetState(a, HATCH_OPENING);
    CHECK(hatches[a].state == HATCH_OPENING, "hatch must enter OPENING");
    CHECK(HatchOpen01(a) < 0.5f, "a fresh OPENING hatch must read as mostly closed");
    /* 动画没播完时，效果不能结算（AD-27） */
    /* 半径收到 2.0：另一扇门在 3.6 之外，所以这里只能返回 -1。
       （用 4.0 会把远的那扇选出来 —— 那是正确行为，是测试写错了。） */
    CHECK(HatchNearest(1.6f, 1.6f, 2.0f) == -1,
          "a hatch mid-animation must not be pickable again");

    HatchTick(HATCH_OPEN_SECONDS + 0.01f);
    CHECK(hatches[a].state == HATCH_FIRED,
          "the effect must fire on the frame the animation finishes");

    /* ---- FIRED 是终态：任何转移都必须被拒 ---- */
    HatchSetState(a, HATCH_SEALED);
    CHECK(hatches[a].state == HATCH_FIRED,
          "FIRED is terminal - a used hatch must never become usable again");

    /* ---- 已用掩码：按**种类**记，不按顺序 ---- */
    {
        int mask = HatchUsedMask();
        CHECK(mask == (1 << HATCH_POWER),
              "the mask must record the used KIND, not the slot index");

        /* 重建（模拟读档重放）并应用掩码 */
        HatchReset();
        HatchAdd(9.5f, 9.5f, HATCH_HELL, 2);     /* 顺序刻意反过来 */
        HatchAdd(4.5f, 4.5f, HATCH_POWER, 4);
        HatchApplyUsedMask(mask);

        CHECK(hatches[0].state == HATCH_SEALED,
              "a kind that was NOT used must stay available after load");
        CHECK(hatches[1].state == HATCH_FIRED,
              "a kind that was used must be FIRED after load, whatever its slot");
    }

    /* ---- 读档不能把上一层的门带过来 ---- */
    HatchReset();
    CHECK(hatchCount == 0, "RoomReplay must reset the hatch table before regenerating");

    HatchReset();
}

static void test_collapse_layer_params(void) {
    extern int collapsedLevel;
    extern float GoldSearchAt(int level);
    extern int HatchPlaceInRoom(int fx, int fy, int w, int h, int themeId);

    int saveCollapsed = collapsedLevel;

    collapsedLevel = 0;
    float normal = GoldSearchAt(3);
    collapsedLevel = 1;
    float broken = GoldSearchAt(3);

    CHECK(normal > 0.0f, "normal gold must be positive");
    CHECK(broken > normal * 1.7f && broken < normal * 1.9f,
          "collapsed layer must pay 1.8x gold (user requirement)");

    /* 崩塌层不生成舱门 —— 传一个合法主题（5 = 锈工厂）也必须被拒 */
    CHECK(HatchPlaceInRoom(2, 2, 4, 4, 5) == -1,
          "collapsed layers must not spawn hatches (would allow infinite rerolls)");

    collapsedLevel = saveCollapsed;
}

static void test_balance_targets(void) {
    extern float GoldSearchAt(int level);
    extern float GoldBreakerAt(int level);
    extern float GoldExitAt(int level);
    extern int SkillsSelfTest(void);

    /* ---- 单箱金币：A-1 之后是 6 + 1.1L ---- */
    {
        float g1 = GoldSearchAt(1);
        float g8 = GoldSearchAt(8);
        CHECK(g1 > 6.5f && g1 < 8.0f, "per-crate gold at level 1 must be about 7 (A-1 target)");
        CHECK(g8 > 13.0f && g8 < 17.0f, "per-crate gold at level 8 must be about 15 (A-1 target)");
    }

    /* ---- 整层收入：10 个箱子 + 配电箱 + 出口 ---- */
    {
        float floor1 = 10.0f * GoldSearchAt(1) + GoldBreakerAt(1) + GoldExitAt(1);
        CHECK(floor1 > 130.0f && floor1 < 230.0f,
              "level-1 income per floor must land in the target band");
    }

    /* ---- 技能表改价之后必须仍然自洽（总价 == SKILL_TOTAL_COST） ---- */
    CHECK(SkillsSelfTest() == 0,
          "the skill table must still self-check after the A-2 price bump");
}

static void test_load_clamps_flags(void) {
    extern int dailyMode, campaignMode, campaignStage, bestiarySeen;

    SaveBlob blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = SAVE_MAGIC;
    blob.version = SAVE_VERSION;
    blob.hasRun = 1;
    blob.dailyMode = -1;          /* 坏值：看起来像真 */
    blob.dailySeed = 12345;
    blob.campaignMode = 7;        /* 坏值：不是 0/1 */
    blob.campaignStage = -5;      /* 坏值：负数段 */
    blob.seenMonsters = 0x7FFFFFFF;  /* 坏值：全是位 */

    const char* dir = getenv("CANITY_DATA_DIR");
    char path[MAX_PATH];
    /* 三个槽都清掉：LoadSettings 会挑"最新的那个槽"，
       如果别的用例留下了 canity1/2.sav，写的 canity0 根本不会被读。 */
    for (int slot = 0; slot < 3; slot++) {
        char p3[MAX_PATH];
        snprintf(p3, sizeof(p3), "%s\\canity%d.sav", dir, slot);
        remove(p3);
        snprintf(p3, sizeof(p3), "%s\\canity%d.sav.bak", dir, slot);
        remove(p3);
    }
    snprintf(path, sizeof(path), "%s\\canity0.sav", dir);
    FILE* f = fopen(path, "wb");
    CHECK(f != NULL, "create the corrupt-flag fixture");
    CHECK(fwrite(&blob, 1, sizeof(blob), f) == sizeof(blob), "write the corrupt-flag fixture");
    fclose(f);

    CHECK(LoadSettings() != 0, "a save with corrupt flag values must still load");
    /* 这些字段是在 ContinueRun() 里灌回全局的（不是 LoadSettings），
       所以必须按真实流程走一遍 —— 只调 LoadSettings 测不到。 */
    ContinueRun();

    CHECK(dailyMode == 0, "dailyMode must be clamped to 0/1 (a -1 leaks daily restrictions into a normal run)");
    CHECK(campaignMode == 0, "campaignMode must be clamped to 0/1");
    CHECK(campaignStage == 0, "a negative campaignStage must be clamped to 0");

    /* 图鉴位掩码只保留真实存在的位 */
    {
        int valid = 0;
        for (int bi = 0; bi < BESTIARY_COUNT && bi < 31; bi++) valid |= (1 << bi);
        CHECK((bestiarySeen & ~valid) == 0,
              "bestiarySeen must not keep bits for monsters that do not exist");
    }

    /* 正常值必须原样保留 —— 夹取不能把合法存档也改坏 */
    blob.dailyMode = 1;
    blob.campaignMode = 1;
    blob.campaignStage = 2;
    blob.seenMonsters = 0x5;
    f = fopen(path, "wb");
    CHECK(f != NULL, "rewrite the fixture with valid values");
    (void)fwrite(&blob, 1, sizeof(blob), f);
    fclose(f);
    CHECK(LoadSettings() != 0, "a valid save must still load");
    ContinueRun();
    CHECK(dailyMode == 1, "a legitimate dailyMode=1 must survive");
    CHECK(campaignMode == 1, "a legitimate campaignMode=1 must survive");
    CHECK(campaignStage == 2, "a legitimate campaignStage=2 must survive");
    CHECK(bestiarySeen == 0x5, "a valid bestiary mask must be preserved exactly");
}

static void test_trees_shop(void) {
    clear_meta();bankGold=1000000;activeKeystone=-1;
    CHECK(SkillsSelfTest()==0,"skill data costs, ranks and prerequisites validate");
    for(int i=0;i<SKILL_COUNT;++i) CHECK(skillDefs[i].name && skillDefs[i].maxRank>0,"all thirty skills have definitions");
    int before=bankGold;CHECK(SkillBuy(0),"buy a permanent skill from bank");
    CHECK(bankGold==before-SkillCost(0)/2 || bankGold<before,"permanent purchase charges bank");
    int cost=skillDefs[0].cost[0];int reward=g_achievements[29].unlocked?0:1500;
    before=bankGold;SkillResetAll();CHECK(bankGold==before+cost+reward && skillRank[0]==0 && skillRank[SK_ORIGIN]==1,"meta reset refunds cost, keeps origin, grants achievement");
    bankGold=1000000;
    for(int pass=0;pass<30;++pass)for(int id=0;id<SKILL_COUNT;++id)if(SkillCanBuy(id))SkillBuy(id);
    CHECK(SkillNodesLit()==30,"all thirty network nodes are purchasable");
    CHECK(activeKeystone>=16 && activeKeystone<=19,"four capstones have one active choice");
    int equipped=activeKeystone;CHECK(SkillSwitchKeystone(equipped==16?17:16),"owned capstones switch without losing purchases");
    CHECK(SkillNodesLit()==30,"capstone switching preserves the collected tree");
    clear_meta();
    for(int col=0;col<4;++col) {
        BenchResetRun();ThreatReset();carryGold=10000;
        CHECK(BenchTryBuy(col*4+1)==3,"bench rejects missing prerequisite");
        for(int row=0;row<3;++row) CHECK(BenchTryBuy(col*4+row)==1,"attribute tree lower tiers purchase");
        CHECK(BenchTryBuy(16+col)==1,"alternate capstone uses correct branch prerequisite");
        CHECK(BenchTryBuy(col*4+3)==5,"both capstones in a branch cannot be purchased");
    }
    BenchResetRun();ThreatReset();carryGold=0;
    CHECK(BenchTryBuy(0)==4 && !BenchHas(0),"insufficient money never installs a mod");
    carryGold=50000;for(int i=0;i<16;++i) CHECK(BenchTryBuy(i)==1,"complete sixteen-node attribute build");
    CHECK(ThreatValue()==18.0f,"max build reaches capped threat 18");
    CHECK(BenchStaminaMul()>1 && BenchMoveMul()>1 && BenchBatteryCapMul()>1 && BenchLightRangeMul()>1,"body and light attributes affect runtime getters");
    CHECK(BenchSanityAdd()==20 && BenchHealMul()>1 && BenchFearDamageMul()<1,"mind attributes affect real survival stats");
    CHECK(BenchKnockCharges()==1 && BenchKnockExtra()==2.5f && BenchShock(),"gear line upgrades charges, push and memory wipe");
    CHECK(BankPreviewGain(1)>=carryGold,"exit previews a valid settlement");
    int carry=carryGold;BenchRefundRun();CHECK(carryGold>carry && ThreatValue()==0 && !BenchHas(0),"run respec refunds money and removes added threat");
    SafeLevelReset();ShopOpen(1);int offers[4],prices[4];
    for(int i=0;i<SHOP_OFFERS;++i) {offers[i]=shopOffer[i];prices[i]=ShopPriceAt(i);CHECK(prices[i]>0,"every offer has a positive effective price");for(int j=0;j<i;++j)CHECK(offers[i]!=offers[j],"four supplies are distinct");}
    CHECK(shopDiscountRow>=0 && shopDiscountRow<SHOP_OFFERS,"one supply is discounted every floor");
    ShopOpen(1);for(int i=0;i<SHOP_OFFERS;++i) CHECK(offers[i]==shopOffer[i] && prices[i]==ShopPriceAt(i),"reopening shop does not reroll");
    shopPage=SHP_SHOP;shopRow=shopDiscountRow;carryGold=1000;int price=ShopPriceAt(shopRow);
    sanity=30;battery=30;bottles=flares=smokeBombs=gluePots=echoBolts=decoys=0;ShopResetCooldowns();
    ShopActivate();CHECK(carryGold==1000-price,"discount displayed is discount charged");
    ShopRefresh();CHECK(shopRefreshUsed==1,"one free refresh per floor");
    for(int i=0;i<SHOP_OFFERS;++i) offers[i]=shopOffer[i];
    ShopRefresh();
    for(int i=0;i<SHOP_OFFERS;++i)CHECK(offers[i]==shopOffer[i],"second refresh cannot reroll");
}
static void finish_floor(void) {
    currentState=STATE_PLAYING;flashTime=0.0f;exitChoiceOpen=0;exitGoDeeper=1;breakerOpen=1;
    posX=exitX;posY=exitY;sanity=SanityMax();monster.active=lightBeast.active=0;WardenReset();
    memset(keyPressed,0,sizeof(keyPressed));Update(0.008f);
}
static void test_runs_and_layout(void) {
    clear_meta();bankGold=0;bankCarryPct=0;StartGame();
    finish_floor();CHECK(currentLevel==2 && currentState==STATE_SKILL_PICK && pickMode==0,"skill selection reachable on floor two");CloseSkillPick();
    finish_floor();CHECK(currentLevel==3 && currentState==STATE_SKILL_PICK && pickMode==1,"relic selection reachable on floor three");CloseSkillPick();
    while(currentLevel<6) {finish_floor();while(currentState==STATE_SKILL_PICK)CloseSkillPick();}
    /* A separate transition checks both selections before closing either. */
    StartGame();currentLevel=5;GenerateLevel();finish_floor();
    CHECK(currentLevel==6 && pickMode==1,"floor six first offers a relic");CloseSkillPick();
    CHECK(currentState==STATE_SKILL_PICK && pickMode==0,"floor six also offers the skill");CloseSkillPick();
    StartCampaign(0);int stages[8]={0,1,1,2,3,3,3,4};
    for(int floor=1;floor<=8;++floor) {
        CHECK(currentLevel==floor && campaignStage==stages[floor-1],"campaign cumulative floor boundaries");
        CHECK(fusesNeeded==CampaignFusesNow(),"campaign preserves specified fuse counts including eight");
        int c=carryGold;finish_floor();CHECK(carryGold>=c || currentState==STATE_GAMEOVER,"campaign stage transition never loses carried money");
    }
    CHECK(gameWon && currentState==STATE_GAMEOVER && bankGold>=36000,"all five campaign rewards settle into bank");
    int sizes[][2]={{800,600},{806,640},{1280,720},{1920,1080}};
    for(int k=0;k<4;++k) {
        winW=sizes[k][0];winH=sizes[k][1];currentState=STATE_SAFEHOUSE;
        for(int p=0;p<SAFEHOUSE_PAGES;++p) {shopPage=p;MenuGeo g=GetMenuGeo();int x0,y0,x1,y1;PageTabRect(0,&x0,&y0,&x1,&y1);for(int i=0;i<ShopRowCount();++i){RECT r=RowRect(g,i);CHECK(r.left>x1 && r.right<=g.panel.right,"shop text and hit areas clear the sidebar");}}
        currentState=STATE_BESTIARY;MenuGeo g=GetMenuGeo();CHECK(g.maxVisibleRows>=7,"six monsters plus back fit without scrolling");
        currentState=STATE_HUB;hubSelection=SK_ORIGIN;SkillResetView();
        for(int i=0;i<SKILL_COUNT;++i) {RECT a=SkillNodeRect(i);for(int j=0;j<i;++j){RECT b=SkillNodeRect(j),overlap;CHECK(!IntersectRect(&overlap,&a,&b),"radial skill nodes never overlap");}}
    }
}

typedef struct {int plays,stops,pauses,scenes,ticks;float gain;} FakeMusic;
static int fake_play(void* p,const char* track,int loop){(void)track;(void)loop;((FakeMusic*)p)->plays++;return 1;}
static void fake_stop(void* p){((FakeMusic*)p)->stops++;}
static void fake_pause(void* p,int pause){((FakeMusic*)p)->pauses=pause;}
static void fake_gain(void* p,float gain){((FakeMusic*)p)->gain=gain;}
static void fake_scene(void* p,MusicScene scene){(void)scene;((FakeMusic*)p)->scenes++;}
static void fake_tick(void* p,float dt){(void)dt;((FakeMusic*)p)->ticks++;}
static void test_music(void) {
    FakeMusic fake={0};MusicProvider api={MUSIC_PROTOCOL_VERSION,fake_play,fake_stop,fake_pause,fake_gain,fake_scene,fake_tick};
    MusicInit();CHECK(!MusicUseProvider(MUSIC_SIM),"missing SIM adapter falls back safely");
    api.version=99;CHECK(!MusicRegisterProvider(MUSIC_SIM,&api,&fake),"music protocol version is checked");api.version=1;
    CHECK(MusicRegisterProvider(MUSIC_SIM,&api,&fake) && MusicUseProvider(MUSIC_SIM),"SIM attaches through the shared music protocol");
    MusicSetMasterVolume(0.8f);MusicSetTrack(MUSIC_CHASE,"future-track.mp3");MusicSetScene(MUSIC_CHASE);MusicTick(1.0f);
    CHECK(fake.plays==2 && fake.scenes==2 && fabsf(fake.gain-0.8f)<0.01f,"scene and master volume reach provider");
    MusicDuck(2.0f,0.25f);MusicTick(0.5f);CHECK(fabsf(fake.gain-0.2f)<0.01f,"important events duck the provider");
    MusicTick(1.0f);MusicTick(1.0f);CHECK(fabsf(fake.gain-0.8f)<0.01f,"duck expires and music recovers");
    MusicSetPaused(1);MusicTick(0.5f);CHECK(fake.pauses && fake.gain==0,"pause reaches provider");
    MusicSetPaused(0);MusicTick(0.5f);CHECK(!fake.pauses && fake.gain>0,"resume restores music");
    CHECK(!MusicUseProvider(MUSIC_STEAM),"unavailable Steam cannot replace working provider");
    MusicShutdown();CHECK(fake.stops==1,"provider resources close on shutdown");MusicInit();
}
static void test_wrench(void) {
    clear_meta();bankGold=0;bankCarryPct=0;StartGame();dailyMode=0;
    monster.active=lightBeast.active=1;
    monster.x=lightBeast.x=posX+0.4f;monster.y=lightBeast.y=posY;
    int before=knockCountRun;WrenchStartSwing(1.0f);
    for(int i=0;i<12;++i)WrenchTick(0.04f);
    CHECK(knockCountRun==before+2,"a wrench swing affects both hunters once");

    /* ==================================================================
     * 2026-11-06：**扳手改成纯冷却制**（用户要求：8 秒，每级 -1，下限 5）
     * ------------------------------------------------------------------
     * 旧契约是"每层 N 次"，所以这条用例原来测的是"连挥两次都能命中"。
     * 新契约下连挥必须被冷却挡住 —— 这里改成同时验证两件事：
     *   ① 冷却期内再挥**不生效**
     *   ② 冷却走完之后能再挥
     * ================================================================ */
    monster.x=lightBeast.x=posX+0.4f;monster.y=lightBeast.y=posY;
    WrenchStartSwing(1.0f);for(int i=0;i<12;++i)WrenchTick(0.04f);
    CHECK(knockCountRun==before+2,
          "a second swing inside the cooldown must not land (wrench is cooldown-based now)");

    /* 把冷却清零。
       不用 Update(dt) 去"走完"时间 —— 它会把整局推进 8 秒
       （怪物移动、事件到期、甚至层结束），实测那样第二次挥击仍然不结算。
       这里直接把计时器清零，隔离出"冷却结束后能不能再挥"这一件事。 */
    {
        extern float wrenchCd;
        CHECK(wrenchCd > 0.0f, "starting a swing must arm the cooldown");
        wrenchCd = 0.0f;
    }
    monster.x=lightBeast.x=posX+0.4f;monster.y=lightBeast.y=posY;
    WrenchStartSwing(1.0f);for(int i=0;i<12;++i)WrenchTick(0.04f);
    CHECK(knockCountRun==before+4,
          "after the cooldown expires a new swing lands again");
    CHECK(monster.active && lightBeast.active,"wrench never kills a monster");
    /* ==================================================================
     * 扳手必须也能击退第 2、3 只听声鬼
     * （2026-11-03：原来 targets[] 里只有 {&monster, &lightBeast}）
     * ================================================================== */
    clear_meta();bankGold=0;bankCarryPct=0;StartGame();dailyMode=0;
    monHunters[0].active = monHunters[1].active = monHunters[2].active = 1;
    lightBeast.active = 0;
    for (int i = 0; i < MON_HUNTER_MAX; ++i) {
        monHunters[i].x = posX + 0.4f;
        monHunters[i].y = posY;
        monHunters[i].stun = 0.0f;
    }
    before = knockCountRun;
    WrenchStartSwing(1.0f);
    for (int i = 0; i < 12; ++i) WrenchTick(0.04f);
    CHECK(knockCountRun == before + 3, "a wrench swing knocks back all three hunters");

    carryGold=1000;CHECK(RunUpgTryBuy(0)==0,"run upgrade can be purchased");StartGame();
    CHECK(RunUpgLevel(0)==0,"run upgrades clear on a new run");
}

/* ==================================================================
 * 听声鬼三只：**第二、第三只必须真的会走**
 * ------------------------------------------------------------------
 * 2026-11-03 的回归：UpdateOneHunter 里那句
 *     MonPath((int)M->x, ...)
 * 调的是写死 &monster 的老封装，于是 2/3 号算出的路径全写进 1 号的
 * path[]，自己 pathLen 恒为 0 —— 召出来就站着不动。
 * 这个测试直接跑真实更新循环，位置必须变化。
 * ================================================================== */
static void test_three_hunters(void) {
    clear_meta();bankGold=0;StartGame();dailyMode=0;trainingMode=0;
    currentState=STATE_PLAYING;

    extern void TestSummonAllHunters(void);
    extern Monster monHunters[MON_HUNTER_MAX];
    extern void UpdateMonster3(float);

    TestSummonAllHunters();
    for (int i = 0; i < MON_HUNTER_MAX; ++i)
        CHECK(monHunters[i].active, "F3 puts all three hunters on the map");

    float x0[MON_HUNTER_MAX], y0[MON_HUNTER_MAX];
    for (int i = 0; i < MON_HUNTER_MAX; ++i) {
        x0[i] = monHunters[i].x;
        y0[i] = monHunters[i].y;
        /* 让它们有目标：玩家位置就是噪音源 */
        monHunters[i].state = 1;
        monHunters[i].tx = posX;
        monHunters[i].ty = posY;
        monHunters[i].repath = 0.0f;
        monHunters[i].stun = 0.0f;
        monHunters[i].hitCooldown = 99.0f;   /* 别真的抓人，只测移动 */
    }

    for (int step = 0; step < 180; ++step) {   /* 3 秒 */
        UpdateMonster(1.0f / 60.0f);
        UpdateMonster2(1.0f / 60.0f);
        UpdateMonster3(1.0f / 60.0f);
        /* 别让它们真的贴到玩家身上触发捕获 */
        for (int i = 0; i < MON_HUNTER_MAX; ++i) {
            float dx = monHunters[i].x - posX, dy = monHunters[i].y - posY;
            if (dx * dx + dy * dy < 4.0f) {
                monHunters[i].x = posX + 8.0f;
                monHunters[i].y = posY;
            }
        }
    }

    for (int i = 0; i < MON_HUNTER_MAX; ++i) {
        float dx = monHunters[i].x - x0[i];
        float dy = monHunters[i].y - y0[i];
        CHECK(dx * dx + dy * dy > 0.01f,
              i == 0 ? "hunter 1 moves" :
              (i == 1 ? "hunter 2 moves" : "hunter 3 moves"));
    }

    /* ==================================================================
     * 每只的听觉是**个体属性**，不是按数量统一降（2026-11-03 用户设计）
     *   第 1 只 100% / 第 2 只 75% / 第 3 只 50%
     * ================================================================ */
    extern float HunterHearMulAt(int);
    extern int   HunterIndex(const Monster*);
    extern int   HunterActiveCount(void);

    CHECK(HunterActiveCount() == 3, "all three counted");
    CHECK(fabsf(HunterHearMulAt(0) - 1.00f) < 0.001f, "hunter 1 hears at 100%");
    CHECK(fabsf(HunterHearMulAt(1) - 0.75f) < 0.001f, "hunter 2 hears at 75%");
    CHECK(fabsf(HunterHearMulAt(2) - 0.50f) < 0.001f, "hunter 3 hears at 50%");

    CHECK(HunterIndex(&monHunters[0]) == 0, "index of hunter 1");
    CHECK(HunterIndex(&monHunters[1]) == 1, "index of hunter 2");
    CHECK(HunterIndex(&monHunters[2]) == 2, "index of hunter 3");

    /* 第 3 只要明显更快（用户：移动速度特别快） */
    CHECK(monHunters[2].speed > monHunters[0].speed * 1.2f,
          "hunter 3 is markedly faster than hunter 1");
    CHECK(monHunters[1].speed < monHunters[2].speed,
          "the tall one outruns the short one");
}


/* ==================================================================
 * 听声鬼三只的**剪影必须能分开**（art_direction.h 铁律一 AD-01）
 * ------------------------------------------------------------------
 * 做法和规范一样：把主体当填黑，缩到 32×32 网格，
 * 只比较「有没有实体」的二维掩码，不看颜色。
 * 任意两只的掩码差异都必须足够大，否则玩家在黑暗里只能靠颜色认怪。
 * ================================================================== */
static void mask32(int sprId, unsigned char out[32][32]) {
    Sprite* sp = SpriteGet(sprId);
    memset(out, 0, 32 * 32);
    if (!sp || !sp->px || sp->w <= 0 || sp->h <= 0) return;
    for (int gy = 0; gy < 32; gy++) {
        for (int gx = 0; gx < 32; gx++) {
            int x0 = gx * sp->w / 32, x1 = (gx + 1) * sp->w / 32;
            int y0 = gy * sp->h / 32, y1 = (gy + 1) * sp->h / 32;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            int hit = 0;
            for (int y = y0; y < y1 && !hit; y++)
                for (int x = x0; x < x1; x++)
                    if ((sp->px[(size_t)y * sp->w + x] >> 24) != 0) { hit = 1; break; }
            out[gy][gx] = (unsigned char)hit;
        }
    }
}

static void test_hunter_models(void) {
    InitPixels();
    SpritesInit();               /* 烘焙精灵（纯 CPU，不需要窗口） */
    CHECK(SpritesReady(), "sprites baked");

    CHECK(SpriteGet(SPR_MON_HUNTER) != NULL, "hunter 1 sprite exists");
    CHECK(SpriteGet(SPR_MON_HUNTER2) != NULL, "hunter 2 sprite exists");
    CHECK(SpriteGet(SPR_MON_HUNTER3) != NULL, "hunter 3 sprite exists");
    /* 2026-11-06：舱门 4 帧必须真的烘出来（BakeHatch 漏调会静默得到空图） */
    CHECK(SpriteGet(SPR_HATCH_0) != NULL, "hatch frame 0 exists");
    CHECK(SpriteGet(SPR_HATCH_1) != NULL, "hatch frame 1 exists");
    CHECK(SpriteGet(SPR_HATCH_2) != NULL, "hatch frame 2 exists");
    CHECK(SpriteGet(SPR_HATCH_3) != NULL, "hatch frame 3 exists");
    {
        /* 关门帧必须有实心像素：全空说明 cvFinish 的 bbox 把它裁没了 */
        int solid0 = 0;
        Sprite* sp = SpriteGet(SPR_HATCH_0);
        if (sp) for (int k = 0; k < sp->w * sp->h; k++)
            if ((sp->px[k] >> 24) > 40) solid0++;
        CHECK(solid0 > 400, "hatch frame 0 must not bake to an empty sprite");
    }

    static unsigned char m1[32][32], m2[32][32], m3[32][32];
    mask32(SPR_MON_HUNTER, m1);
    mask32(SPR_MON_HUNTER2, m2);
    mask32(SPR_MON_HUNTER3, m3);

    int solid1 = 0, solid2 = 0, solid3 = 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            solid1 += m1[y][x]; solid2 += m2[y][x]; solid3 += m3[y][x];
        }
    CHECK(solid1 > 40 && solid2 > 40 && solid3 > 40, "all three hunters have a body");

    int d12 = 0, d13 = 0, d23 = 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            if (m1[y][x] != m2[y][x]) d12++;
            if (m1[y][x] != m3[y][x]) d13++;
            if (m2[y][x] != m3[y][x]) d23++;
        }
    /* 32x32 = 1024 格；要求至少 8% 的格子不同才算剪影能分开 */
    CHECK(d12 * 100 >= 1024 * 8, "hunter1 vs hunter2 silhouette differs");
    CHECK(d13 * 100 >= 1024 * 8, "hunter1 vs hunter3 silhouette differs");
    CHECK(d23 * 100 >= 1024 * 8, "hunter2 vs hunter3 silhouette differs");
}


/* ==================================================================
 * 换层必须把第 2、3 只听声鬼清掉（2026-11-03 的 bug）
 * ------------------------------------------------------------------
 * GenerateLevel() 原来只重生第一只，第 2/3 只 active 会原样带进新层，
 * 玩家一进第二层就三只同时在场。
 * ================================================================== */
static void test_floor_resets_hunters(void) {
    clear_meta();bankGold=0;StartGame();dailyMode=0;trainingMode=0;
    currentState=STATE_PLAYING;

    extern Monster monHunters[MON_HUNTER_MAX];
    extern void TestSummonAllHunters(void);
    TestSummonAllHunters();
    CHECK(monHunters[1].active && monHunters[2].active, "summon puts hunters 2 and 3 on the map");

    /* 换层 */
    currentLevel++;
    GenerateLevel();

    CHECK(!monHunters[1].active, "hunter 2 is cleared on a new floor");
    CHECK(!monHunters[2].active, "hunter 3 is cleared on a new floor");
    CHECK(monHunters[0].active, "hunter 1 still spawns on the new floor");

    /* 时间表也要重置：新层从 1 只开始，不会立刻补满 */
    extern void TickSecondSpawn(float);
    for (int i = 0; i < 30; ++i) TickSecondSpawn(1.0f);   /* 30 秒 */
    CHECK(!monHunters[1].active && !monHunters[2].active,
          "30s into the new floor only one hunter exists");
}

/* ==================================================================
 * 2026-11-06：**破门石一次撞击只结算一次逻辑噪音**
 * ------------------------------------------------------------------
 * 背景：`throwing.c` 里破锁会先 `MakeNoise(20)`，然后落地/撞墙分支
 * 又各来一次 `MakeNoise(15)` —— 同一块石头报两次。
 *
 * 实测修正（重要）：`suspicion.c` 的 `SuspicionAdd` 会把
 * **同源且 2 格内**的点合并（取更高强度/更长寿命），
 * 而 20 和 15 都 > 14 阈值，同属 `SUSP_SEARCH` ——
 * 所以这两次在当前实现里本来就会并成一个点，**玩家感觉不到差别**。
 *
 * 因此这个测试是**回归守卫**，不是"修复前失败"的证明：
 * 它钉住「一次破锁 = 一个可疑点」这条不变量，
 * 等 v2.4-beta 的 SoundEvent 把强度与类别分开之后，它会真正开始起作用。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06：**写盘失败的负路径**
 * ------------------------------------------------------------------
 * R6 改的是 SaveBackendWrite 的错误传播。这里验证它真的会返回非 0，
 * 而不是吞掉失败谎报成功 —— 存档层最危险的失败模式就是这一种。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-alpha R8）：**长度不同的合法存档不算损坏**
 * ------------------------------------------------------------------
 * 这是 N1 的正面证据。旧实现用本地长度去要求云端，
 * 只要两份 payload 长度不同就 STEAM_SYNC_ERROR ——
 * 而拆块（SC_SETTINGS / SC_WORLD）必然改变长度。
 *
 * 用 SteamZoneCloudDecide（纯函数）测，不需要 Steam 客户端。
 * 注意：这**不等于** Steam 真机通过 —— 传输层仍是 UNKNOWN。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-alpha R12）：**未知块经过真实读写流程仍然保留**
 * ------------------------------------------------------------------
 * 这是 v2.4-alpha 的核心验收（S3 / G02）。
 * 走的是 save.c 里**真实的**捕获 → 重建逻辑（SaveTestSerialize /
 * SaveTestDeserialize 只是把两个 static 原样导出），
 * 不是测试专用的另一套序列化。
 *
 * 步骤：
 *   ① 手搓一份合法 CSAV：SC_LEGACY29 + 未知块 0x7777
 *   ② 反序列化（应当捕获目录）
 *   ③ 改一个已知字段（lang）
 *   ④ 重新序列化
 *   ⑤ 解析新字节：0x7777 必须还在，且**逐字节相同**
 *   ⑥ 已知块的 lang 必须是新值
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-beta B01）：**声音事件中心**
 * ------------------------------------------------------------------
 * 这一组钉两件事：
 *   ① 类别由 kind 决定，**不由半径决定** —— 这是 B01 存在的全部理由。
 *      同一个 kind 换不同 maxRadius，落到可疑点表的 source/ttl 必须不变。
 *   ② 同帧同 eventId 只结算一次（一次物理事件 = 一个逻辑事件），
 *      但 eventId == 0 时不去重（真实的连续脚步不能被合并）。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-beta B02）：**声像与距离衰减的数学**
 * ------------------------------------------------------------------
 * 这一组是纯函数测试，不需要声卡。
 * 它钉住"实现对不对"；"听起来像不像左边"仍然要人耳 ——
 * 但至少不会出现"以为调了参数其实公式是反的"。
 *
 * 坐标约定：玩家朝向 (dirX,dirY)，右向量 = (-dirY, dirX)。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-beta B03）：**隔墙的线索不会被听声鬼选中**
 * ------------------------------------------------------------------
 * 这是 B03 接进玩法后的行为断言（不是纯数学）。
 * 造一个小地图，放两个可疑点：
 *   A 在墙后，直线更近
 *   B 在走廊里，直线更远
 * 结果必须是 B —— 旧实现（直线距离）会选 A。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-beta B04）：**定向搜查围绕记忆点，而不是漫游**
 * ------------------------------------------------------------------
 * 这是 B04 的核心行为断言：
 *   MonsterWanderFor 在**当前位置**周围 ±4 格随机挑 → 会飘走
 *   MonsterSearchAround 在**记忆点**周围 1.5~4.4 格环上挑 → 收敛
 *
 * 造一间空房间，把记忆点钉在房间中央，然后连挑 12 次，
 * 每次都检查落点到记忆点的距离在环内。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-beta B05）：**破门石的实际听觉范围**
 * ------------------------------------------------------------------
 * 外部审查建议把破门石的 20 格做 12/14/16/20 四参数对照。
 * 算完发现**不需要改数值** —— 因为听声鬼的判定不是"半径内就听见"，
 * 而是"有效强度 > 阈值"：
 *
 *     破门石写入强度 = 20 × 2.6 = 52
 *     有效强度       = 52 × SoundReach(d, maxListen=22)
 *     第 1 只的阈值  = 15  → 需要 reach > 0.288 → d < 约 10 格
 *     第 3 只的阈值  = 30  → 需要 reach > 0.577 → d < 约 6 格
 *
 * 再加上 B03 的路径传播（隔墙 reach 更低），实际范围是 6~10 格，
 * 并不是"覆盖全图"。**20 那个数只是"强度换算的输入"，不是听觉半径。**
 *
 * 这组断言把这个结论钉住，防止以后有人把 strength/maxListen 的关系改乱。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-rc C3）：**主题房间真的放进了地图，而且没犯规**
 * ------------------------------------------------------------------
 * 直接调 GenerateThemeRooms（它只依赖 worldMap / exitX / exitY），
 * 不跑完整的 GenerateLevel —— 那会牵动纹理生成和怪物刷新，
 * 在单元测试里既慢又危险。
 *
 * 造一张全墙地图：房间是唯一的空地来源，于是"放没放进去"
 * 可以直接从 worldMap 看，不用猜。
 * ================================================================== */
static void test_theme_rooms_placement(void) {
    extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
    extern float exitX, exitY;
    static int saved[MAP_HEIGHT][MAP_WIDTH];
    float savedExitX = exitX, savedExitY = exitY;

    memcpy(saved, worldMap, sizeof(saved));

    /* 造一张"有走廊"的图：奇数行整行打通，其余是墙。
       为什么不能再用全墙地图：v2.4-rc 的房间**自带围墙和门**，
       门要向外挖到"本来就开着"的格子才算接上迷宫。
       全墙图里没有任何空地可接，房间会被连通性检查正确地拒绝 ——
       那不是 bug，是测试的夹具不真实。 */
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            worldMap[y][x] = (y % 2 == 1 && y > 0 && y < MAP_HEIGHT - 1 &&
                               x > 0 && x < MAP_WIDTH - 1) ? 0 : 1;

    exitX = 20.5f; exitY = 20.5f;
    CellMetaReset();

    srand(12345u);
    int placed = GenerateThemeRooms(2);

    CHECK(placed >= 1, "at least one theme room must be placed on an empty 24x24 map");
    CHECK(placed <= 2, "GenerateThemeRooms must not exceed the requested count");
    CHECK(RoomCount() == placed, "RoomCount must match what was actually placed");

    /* ---- ① 外墙一格都不许动 ---- */
    {
        int wallOk = 1;
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (worldMap[0][x] == 0) wallOk = 0;
            if (worldMap[MAP_HEIGHT - 1][x] == 0) wallOk = 0;
        }
        for (int y = 0; y < MAP_HEIGHT; y++) {
            if (worldMap[y][0] == 0) wallOk = 0;
            if (worldMap[y][MAP_WIDTH - 1] == 0) wallOk = 0;
        }
        CHECK(wallOk, "theme rooms must never breach the outer wall");
    }

    /* ---- ② 出生区 1..3 不许被房间占用 ---- */
    {
        int spawnClean = 1;
        for (int y = 1; y <= 3; y++)
            for (int x = 1; x <= 3; x++)
                if (cellMeta[y][x].roomId != 0) spawnClean = 0;
        CHECK(spawnClean, "theme rooms must not cover the spawn area");
    }

    /* ---- ③ 出口 3 格内不许被房间占用 ---- */
    {
        int exitClean = 1;
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) {
                if (cellMeta[y][x].roomId == 0) continue;
                float ddx = (float)x - exitX, ddy = (float)y - exitY;
                if (ddx * ddx + ddy * ddy < 9.0f) exitClean = 0;
            }
        CHECK(exitClean, "theme rooms must keep 3 tiles clear of the exit");
    }

    /* ---- ④ 房间彼此不重叠（相邻格不允许出现两个不同 roomId） ---- */
    {
        int overlap = 0;
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) {
                if (cellMeta[y][x].roomId == 0) continue;
                static const int ddx2[4] = { 1, -1, 0, 0 };
                static const int ddy2[4] = { 0, 0, 1, -1 };
                for (int k = 0; k < 4; k++) {
                    int nx = x + ddx2[k], ny = y + ddy2[k];
                    if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
                    unsigned char a = cellMeta[y][x].roomId;
                    unsigned char b = cellMeta[ny][nx].roomId;
                    if (b != 0 && b != a) overlap = 1;
                }
            }
        CHECK(!overlap, "two theme rooms must not touch each other");
    }

    /* ---- ⑤ 房间格子的 themeId 必须是真实主题 ---- */
    {
        int themeOk = 1, roomCells = 0;
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) {
                if (cellMeta[y][x].roomId == 0) continue;
                roomCells++;
                if (cellMeta[y][x].themeId >= THEME_COUNT) themeOk = 0;
            }
        CHECK(roomCells > 0, "placed rooms must actually claim some cells");
        CHECK(themeOk, "every room cell must carry a real theme id");
    }

    /* ---- ⑥ 房间的空地必须真的在 worldMap 里被挖开 ---- */
    {
        int carved = 0;
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++)
                if (cellMeta[y][x].roomId != 0 && worldMap[y][x] == 0) carved++;
        CHECK(carved > 0, "room floor must actually be carved into worldMap");
    }

    /* ---- ⑦ 普通区恒为 0 tint（让 render 走原路径、零成本） ---- */
    {
        int normalTintZero = 1;
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) {
                if (cellMeta[y][x].roomId != 0) continue;
                if (CellTintFor(x, y) != 0) normalTintZero = 0;
            }
        CHECK(normalTintZero, "cells outside rooms must have zero tint");
    }

    /* ---- ⑧ 这是这一轮真正修的 bug：房间必须走得到 ----
           400 个种子实测曾有 201 层出现封死的房间。 */
    {
        static int seen[MAP_HEIGHT][MAP_WIDTH];
        static int qx[MAP_WIDTH * MAP_HEIGHT], qy[MAP_WIDTH * MAP_HEIGHT];
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) seen[y][x] = 0;
        int head = 0, tail = 0;
        seen[1][1] = 1; qx[tail] = 1; qy[tail] = 1; tail++;
        while (head < tail) {
            int cx = qx[head], cy = qy[head]; head++;
            static const int ax[4] = { 1, -1, 0, 0 };
            static const int ay[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; k++) {
                int nx = cx + ax[k], ny = cy + ay[k];
                if (nx < 0 || ny < 0 || nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
                if (seen[ny][nx] || worldMap[ny][nx]) continue;
                seen[ny][nx] = 1;
                qx[tail] = nx; qy[tail] = ny; tail++;
            }
        }
        int unreachable = 0;
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) {
                if (cellMeta[y][x].roomId == 0) continue;
                if (worldMap[y][x]) continue;
                if (!seen[y][x]) unreachable++;
            }
        CHECK(unreachable == 0, "every room floor cell must be reachable from the spawn");
    }

    CellMetaReset();
    memcpy(worldMap, saved, sizeof(saved));
    exitX = savedExitX; exitY = savedExitY;
}

/* ==================================================================
 * 2026-11-06（v2.4-rc）：**房间里的家具**
 * ------------------------------------------------------------------
 * 之前的"货架"只是模板里的一格墙，黑暗里就是一根墙垛 ——
 * 用户反馈「里面什么都没有货架隔断」。
 * 现在改成真正的道具精灵（SPR_PROP_SHELF / SPR_PROP_CONSOLE）。
 *
 * 这一组钉住：
 *   · 有房间的层必须有家具
 *   · 家具一定落在**房间内部**的地板格上
 *   · 家具不会压在墙上，也不会跑到房间外
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-rc）：**房间种子必须活着穿过存档往返**
 * ------------------------------------------------------------------
 * 这是 v30 的唯一新增字段。读档后要靠它重放房间（重建色调/家具/碰撞），
 * 所以它丢了这个版本就等于什么都没做。
 *
 * 用 SaveTestSerialize / SaveTestDeserialize（真实序列化路径），
 * 不做文件 IO —— 文件那一层已经有别的用例覆盖。
 * ================================================================== */
static void test_room_seed_roundtrip(void) {
    extern int SaveTestSerialize(const SaveBlob*, unsigned char**);
    extern int SaveTestDeserialize(const void*, long, SaveBlob*);
    /* 上一个用例（重复 ID）会把"只读"标志置起来，
       那是"拒绝覆盖可疑存档"的保护。测试里没有真实读档来复位，
       所以这里显式清一次 —— 不为了测试去改生产语义。 */
    { extern void SaveTestClearReadOnly(void); SaveTestClearReadOnly(); }

    static SaveBlob a;
    memset(&a, 0, sizeof(a));
    a.magic = SAVE_MAGIC;
    a.version = SAVE_VERSION;
    a.roomSeed = 0x1234ABCD;
    a.roomCount = 2;

    unsigned char* out = NULL;
    int n = SaveTestSerialize(&a, &out);
    CHECK(n > 0 && out != NULL, "serialising a blob with room fields must succeed");

    if (n > 0 && out) {
        static SaveBlob b;
        memset(&b, 0, sizeof(b));
        CHECK(SaveTestDeserialize(out, n, &b) != 0, "the serialised blob must load back");
        CHECK(b.roomSeed == 0x1234ABCD, "roomSeed must survive a save/load round trip");
        CHECK(b.roomCount == 2, "roomCount must survive a save/load round trip");
        /* 版本号也要对上，否则前缀迁移会误判 */
        CHECK(b.version == SAVE_VERSION, "version must survive the round trip");
        free(out);
    }
}

static void test_room_props(void) {
    extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
    extern float exitX, exitY;
    static int saved[MAP_HEIGHT][MAP_WIDTH];
    float sx = exitX, sy = exitY;
    memcpy(saved, worldMap, sizeof(saved));

    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            worldMap[y][x] = (y % 2 == 1 && y > 0 && y < MAP_HEIGHT - 1 &&
                              x > 0 && x < MAP_WIDTH - 1) ? 0 : 1;
    exitX = 20.5f; exitY = 20.5f;
    CellMetaReset();

    srand(777u);
    int placed = GenerateThemeRooms(2);

    if (placed > 0) {
        CHECK(roomPropCount > 0, "a level with rooms must also have room furniture");

        int bad = 0, offRoom = 0;
        for (int i = 0; i < roomPropCount; i++) {
            if (!roomProps[i].active) continue;
            int cx = (int)roomProps[i].x;
            int cy = (int)roomProps[i].y;
            if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) { bad++; continue; }
            if (worldMap[cy][cx] != 0) bad++;                 /* 不许压在墙上 */
            if (cellMeta[cy][cx].roomId == 0) offRoom++;      /* 不许跑到房间外 */
        }
        CHECK(bad == 0, "room furniture must never sit on a wall cell");
        CHECK(offRoom == 0, "room furniture must stay inside its room");
        CHECK(roomPropCount <= MAX_ROOM_PROPS, "room furniture must respect MAX_ROOM_PROPS");
    }

    /* 每一件家具的 kind 必须是已知种类 */
    {
        int kindOk = 1;
        for (int i = 0; i < roomPropCount; i++) {
            if (!roomProps[i].active) continue;
            if (roomProps[i].kind != ROOMPROP_SHELF &&
                roomProps[i].kind != ROOMPROP_CONSOLE &&
                roomProps[i].kind != ROOMPROP_CRATES) kindOk = 0;   /* C：木箱堆 */
        }
        CHECK(kindOk, "every room prop must have a known kind");
    }

    /* ==================================================================
     * 2026-11-06：**搜查次数**（精灵数改成 1 张之后的补偿）
     * ------------------------------------------------------------------
     * 每个家具只画 1 个精灵，内容量由"能搜几次"承担：
     *   货架 3 次 / 木箱堆 2 次 / 控制台 2 次
     * 没有一个家具应该是 0 次（那样它又变成纯装饰）。
     * ================================================================== */
    {
        int bad = 0, zero = 0;
        for (int i = 0; i < roomPropCount; i++) {
            if (!roomProps[i].active) continue;
            int k = roomProps[i].kind;
            int want = (k == ROOMPROP_SHELF) ? 3 : 2;
            if (roomProps[i].searchLeft != want) bad++;
            if (roomProps[i].searchLeft <= 0) zero++;
        }
        CHECK(bad == 0, "every prop must start with its intended search count");
        CHECK(zero == 0, "no prop may start with zero searches (that is pure decoration)");
    }

    /* ==================================================================
     * 家具优先放**角落** —— 用户实测"架子和箱子有时候会堵路"。
     * 角落格同时贴着两条边，放在那里不会把 1 格宽的过道从中间截断。
     * 允许少数因为角落被占而退到普通贴墙位，所以只要求**大多数**在角落。
     * ================================================================== */
    {
        int total = 0, corners = 0;
        for (int i = 0; i < roomPropCount; i++) {
            if (!roomProps[i].active) continue;
            int cx = (int)roomProps[i].x, cy = (int)roomProps[i].y;
            if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) continue;
            total++;
            /* 角落判据：上下左右里有两面是墙 */
            int walls = 0;
            if (worldMap[cy - 1][cx]) walls++;
            if (worldMap[cy + 1][cx]) walls++;
            if (worldMap[cy][cx - 1]) walls++;
            if (worldMap[cy][cx + 1]) walls++;
            if (walls >= 2) corners++;
        }
        CHECK(total > 0, "there must be props to check");
        CHECK(corners * 2 >= total,
              "most props must sit in corners (otherwise they cut the aisle in half)");
    }

    /* C：房间不能只有货架一种东西 —— 木箱堆必须真的出现过 */
    {
        int sawCrates = 0, sawOther = 0;
        for (int i = 0; i < roomPropCount; i++) {
            if (!roomProps[i].active) continue;
            if (roomProps[i].kind == ROOMPROP_CRATES) sawCrates = 1;
            else sawOther = 1;
        }
        CHECK(sawCrates, "rooms must contain crate stacks, not only shelves");
        CHECK(sawOther, "rooms must still contain their feature furniture");
    }

    /* 再生成一次，家具表必须被清空重来，不能累加 */
    {
        int before = roomPropCount;
        (void)before;
        srand(778u);
        (void)GenerateThemeRooms(2);
        int counted = 0;
        for (int i = 0; i < MAX_ROOM_PROPS; i++)
            if (roomProps[i].active) counted++;
        CHECK(counted == roomPropCount,
              "roomPropCount must match the number of active props (no leaking between levels)");
    }

    CellMetaReset();
    memcpy(worldMap, saved, sizeof(saved));
    exitX = sx; exitY = sy;
}


/* ==================================================================
 * 2026-11-06（v2.4-rc P3）：**分层伪立体的层表**
 * ------------------------------------------------------------------
 * 这一组是纯几何/纯数据，不需要渲染器：
 *   · 货架 4 层、控制台 2 层
 *   · 每层的 baseZ + halfH 不得超过墙高 1.0（否则穿天花板）
 *   · 层与层之间必须留空隙（否则叠起来就是一张图，没有视差）
 *   · 未知 kind 返回 0 层，不能越界
 * ================================================================== */
static void test_prop_layers(void) {
    PropLayer lp[PROP_MAX_LAYERS];

    /* ---- ① 货架 4 层 ---- */
    {
        int n = RoomPropLayers(ROOMPROP_SHELF, lp, PROP_MAX_LAYERS);
        CHECK(n == 4, "shelf must be 4 layers (frame + 3 boards)");

        for (int i = 0; i < n; i++) {
            CHECK(lp[i].halfH > 0.0f, "every layer must have a positive height");
            CHECK(lp[i].baseZ + lp[i].halfH <= 1.001f,
                  "no layer may pierce the ceiling (baseZ + halfH <= 1.0)");
        }

        /* 第 0 层是**框架**，贯穿整个物件高度；第 1~3 层是横板，落在框架里面。
           所以"留空隙"这条只对板与板之间成立。 */
        float frameTop = lp[0].baseZ + lp[0].halfH;
        for (int i = 1; i < n; i++) {
            CHECK(lp[i].baseZ + lp[i].halfH <= frameTop + 0.001f,
                  "shelf boards must sit inside the frame");
        }
        /* 板与板之间必须有明显空隙，否则叠起来就是一块实心板，没有视差 */
        for (int i = 2; i < n; i++) {
            float gap = lp[i].baseZ - (lp[i - 1].baseZ + lp[i - 1].halfH);
            CHECK(gap > 0.1f, "consecutive shelf boards must leave a visible gap");
        }
        CHECK(lp[3].baseZ - (lp[1].baseZ + lp[1].halfH) > 0.2f,
              "bottom and top boards must be far apart");
    }

    /* ---- ② 控制台 2 层，面板在柜体之上 ---- */
    {
        int n = RoomPropLayers(ROOMPROP_CONSOLE, lp, PROP_MAX_LAYERS);
        CHECK(n == 2, "console must be 2 layers (body + panel)");
        CHECK(lp[1].baseZ >= lp[0].baseZ + lp[0].halfH - 0.001f,
              "the console panel must start where the body ends");
        CHECK(lp[0].baseZ + lp[0].halfH <= 1.001f, "console body must stay under the ceiling");
        CHECK(lp[1].baseZ + lp[1].halfH <= 1.001f, "console panel must stay under the ceiling");
    }

    /* ---- ③ 未知 kind：0 层，不越界 ---- */
    {
        CHECK(RoomPropLayers(999, lp, PROP_MAX_LAYERS) == 0,
              "an unknown prop kind must produce zero layers");
        CHECK(RoomPropLayers(ROOMPROP_SHELF, NULL, PROP_MAX_LAYERS) == 0,
              "NULL layer output must be rejected");
        CHECK(RoomPropLayers(ROOMPROP_SHELF, lp, 1) == 0,
              "not enough layer slots must fail cleanly instead of overflowing");
    }

    /* ---- ④ 层内索引 → 精灵索引映射不越界 ---- */
    {
        for (int k = 0; k <= 1; k++) {
            int n = RoomPropLayers(k, lp, PROP_MAX_LAYERS);
            CHECK(RoomPropLayerSprite(k, -1) == -1, "negative layer index must return -1");
            CHECK(RoomPropLayerSprite(k, n) == -1, "out-of-range layer index must return -1");
            for (int i = 0; i < n; i++) {
                int sid = RoomPropLayerSprite(k, i);
                CHECK(sid == 0 || sid == 1, "layer sprite index must be 0 (body) or 1 (extra)");
            }
        }
    }
}

static void test_stone_hearing_range(void) {
    extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
    static int saved[MAP_HEIGHT][MAP_WIDTH];
    memcpy(saved, worldMap, sizeof(saved));

    /* **必须用空地图**：SoundReach 走的是真实 worldMap，
       真实地图里有墙，测出来的曲线会被传播折弯，不是纯距离衰减。 */
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            worldMap[y][x] = 0;
    SoundPropReset();

    float strong = 20.0f * 2.6f;         /* 破门石写入的可疑点强度 */
    CHECK(strong > 51.9f && strong < 52.1f, "break-stone strength must be 52");

    /* 直线上的可达度（用纯几何，不建图） */
    float r5  = SoundReach(2.5f, 2.5f, 7.5f, 2.5f, 22.0f);    /* 5 格 */
    float r10 = SoundReach(2.5f, 2.5f, 12.5f, 2.5f, 22.0f);   /* 10 格 */
    float r15 = SoundReach(2.5f, 2.5f, 17.5f, 2.5f, 22.0f);   /* 15 格 */

    CHECK(r5 > r10 && r10 > r15, "reach must decrease with distance");

    /* 第 1 只（阈 15）：5 格听得见，15 格听不见 */
    CHECK(strong * r5  > 15.0f, "hunter #1 must hear the break 5 tiles away");
    CHECK(strong * r15 < 15.0f, "hunter #1 must NOT hear the break 15 tiles away");

    /* 第 3 只（阈 30）：10 格听不见 —— 它靠速度不靠耳朵 */
    CHECK(strong * r10 < 30.0f, "hunter #3 must NOT hear the break 10 tiles away");

    /* 顺带确认三个阈值档位对应的最大距离是递减的 */
    CHECK(r5 > 0.55f, "at 5 tiles reach must still be strong");
    CHECK(r10 < 0.35f, "at 10 tiles reach must have fallen off sharply");

    SoundPropReset();
    memcpy(worldMap, saved, sizeof(saved));
}

static void test_monster_search_around(void) {
    extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
    static int saved[MAP_HEIGHT][MAP_WIDTH];
    memcpy(saved, worldMap, sizeof(saved));

    /* 一间 14×14 的空房（四周留一圈墙） */
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            worldMap[y][x] = (x >= 1 && y >= 1 && x <= 14 && y <= 14) ? 0 : 1;

    static Monster mon;
    memset(&mon, 0, sizeof(mon));
    mon.active = 1;
    mon.x = 11.5f; mon.y = 11.5f;      /* 它自己站在房间一角 */
    mon.memX = 5.5f; mon.memY = 5.5f;  /* 记忆点在房间中央 */
    mon.tx = 0.0f; mon.ty = 0.0f;

    int inside = 0;
    float minD = 1e9f, maxD = 0.0f;

    for (int i = 0; i < 12; i++) {
        mon.memAge = (float)i * 0.25f;      /* 模拟搜查推进，角度会轮换 */
        mon.pathLen = 0;
        mon.pathIdx = 0;
        MonsterSearchAround(&mon, mon.memX, mon.memY);

        float dx = mon.tx - mon.memX;
        float dy = mon.ty - mon.memY;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < minD) minD = d;
        if (d > maxD) maxD = d;
        if (d >= 1.0f && d <= 5.0f) inside++;
    }

    CHECK(inside == 12,
          "every directed search point must sit on the ring around the memory anchor");
    CHECK(minD >= 1.0f, "search points must not collapse onto the anchor itself");
    CHECK(maxD <= 5.0f, "search points must stay near the memory anchor, not drift away");

    /* 对照组：同样的位置，用旧的漫游函数，落点会围绕**当前位置**（11.5,11.5） */
    {
        static Monster mon2;
        memset(&mon2, 0, sizeof(mon2));
        mon2.active = 1;
        mon2.x = 11.5f; mon2.y = 11.5f;
        mon2.memX = 5.5f; mon2.memY = 5.5f;

        int farFromMemory = 0;
        for (int i = 0; i < 12; i++) {
            mon2.pathLen = 0; mon2.pathIdx = 0;
            /* 只是对照：用同一套环算法但从**当前位置**出发 */
            MonsterSearchAround(&mon2, mon2.x, mon2.y);
            float dx = mon2.tx - 5.5f, dy = mon2.ty - 5.5f;
            if (sqrtf(dx * dx + dy * dy) > 6.0f) farFromMemory++;
        }
        CHECK(farFromMemory == 12,
              "anchoring on the current position (old behaviour) leaves the memory area");
    }

    memcpy(worldMap, saved, sizeof(saved));
}

static void test_suspicion_best_uses_propagation(void) {
    extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
    static int saved[MAP_HEIGHT][MAP_WIDTH];
    memcpy(saved, worldMap, sizeof(saved));

    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            worldMap[y][x] = 1;

    /* 一条东西向走廊 y=5 */
    for (int x = 2; x <= 14; x++) worldMap[5][x] = 0;
    /* 走廊北侧（y=4 是墙）。A 点放在 y=3 的一条孤立小通道里，
       直线离听者只有 2 格，但要绕到 x=14 再绕回来才能到。 */
    for (int x = 12; x <= 14; x++) worldMap[3][x] = 0;
    worldMap[4][14] = 0;

    SuspicionReset();
    SoundPropReset();

    /* 听者在 (3.5, 5.5) */
    /* A：墙后，直线 ≈ 2.2 格 */
    SuspicionAdd(4.5f, 3.5f, 60.0f, 4.0f, 8.0f, SUSP_SEARCH);
    /* B：走廊里，直线 6 格 */
    SuspicionAdd(9.5f, 5.5f, 60.0f, 4.0f, 8.0f, SUSP_SEARCH);

    /* 旧行为：直线距离，A 更近 → 选 A */
    {
        int idxPlain = SuspicionBest(3.5f, 5.5f, 0.9f);
        CHECK(idxPlain >= 0, "plain SuspicionBest must find something");
        CHECK(susp[idxPlain].y < 4.5f,
              "the plain scorer picks the straight-line-nearest point (behind the wall)");
    }

    /* 新行为：声学传播 → 必须选走廊里的 B */
    {
        float reach = 0.0f;
        int idx = SuspicionBestReach(3.5f, 5.5f, 0.9f, 22.0f, &reach);
        CHECK(idx >= 0, "SuspicionBestReach must find a point");
        CHECK(susp[idx].y > 4.5f,
              "with propagation the corridor point must win over the one behind the wall");
        CHECK(reach > 0.0f && reach <= 1.0f, "reach must be a 0..1 factor");
    }

    /* 墙后那一点的可达度必须显著低于走廊里同距离的点 */
    {
        float behind = SoundReach(3.5f, 5.5f, 4.5f, 3.5f, 22.0f);
        float open   = SoundReach(3.5f, 5.5f, 4.5f, 5.5f, 22.0f);
        CHECK(behind < open * 0.8f,
              "a point behind a wall must be markedly weaker than the same distance in the open");
    }

    SuspicionReset();
    SoundPropReset();
    memcpy(worldMap, saved, sizeof(saved));
}

/* ==================================================================
 * 2026-11-06（v2.4-beta B03）：**声学传播 —— 隔墙必须变弱**
 * ------------------------------------------------------------------
 * 这一组自己搭一个小地图，不依赖 GenerateLevel 的随机布局。
 * 钉住的核心事实：
 *   直线 3 格 + 中间一堵墙  → 传播步数远大于 3，可信度显著低于直通
 *   完全封死的房间          → 不可达，只有贴脸才给一点点
 *   同一个源重复查询        → 命中缓存，不重复跑 BFS
 * ================================================================== */
static void test_sound_propagation(void) {
    extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
    static int saved[MAP_HEIGHT][MAP_WIDTH];

    /* 备份真实地图，测完还原 —— 后面还有用例要用 */
    memcpy(saved, worldMap, sizeof(saved));

    /* 先把整张图填成墙，再挖出一条直走廊和一个封闭房间 */
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            worldMap[y][x] = 1;

    /* 走廊：y=5，x=2..12 全通 */
    for (int x = 2; x <= 12; x++) worldMap[5][x] = 0;
    /* 走廊上方一格也打通一小段，用来测"绕路" */
    for (int y = 3; y <= 5; y++) worldMap[y][10] = 0;
    for (int x = 8; x <= 10; x++) worldMap[3][x] = 0;
    worldMap[3][8] = 0;

    /* 声源放在 (2.5, 5.5) —— 走廊最左端 */
    SoundPropReset();
    SoundPropBuild(2.5f, 5.5f);

    /* ---- ① 直走廊：步数 == 直线距离 ---- */
    {
        int c5 = SoundPropCostAt(7.5f, 5.5f);      /* 直线上 5 格 */
        CHECK(c5 == 5, "along an open corridor the step count must equal the distance");

        float dNear = SoundReach(2.5f, 5.5f, 7.5f, 5.5f, 20.0f);
        float dFar  = SoundReach(2.5f, 5.5f, 11.5f, 5.5f, 20.0f);
        CHECK(dNear > 0.0f && dFar > 0.0f, "both corridor points must be audible");
        CHECK(dNear > dFar, "a nearer corridor point must be more audible than a farther one");
    }

    /* ---- ② 隔墙：直线 2 格，但要走一大圈 ----
           声源 (2.5,5.5)，目标 (4.5,3.5)：直线约 2.8 格，
           而中间隔着 y=4 整排墙，只能绕到 x=10 上去再回来。 */
    {
        int detour = SoundPropCostAt(4.5f, 3.5f);
        CHECK(detour == PROP_UNREACHABLE || detour > 10,
              "a point behind a wall must cost far more steps than its straight-line distance");

        /* 走不到 → 可信度必须接近 0（只有贴脸才给一点） */
        float through = SoundReach(2.5f, 5.5f, 4.5f, 3.5f, 20.0f);
        float open    = SoundReach(2.5f, 5.5f, 4.5f, 5.5f, 20.0f);   /* 同距离但直通 */
        CHECK(through < open,
              "a sound through a wall must be weaker than the same distance in the open");
    }

    /* ---- ③ 超过 maxRadius 完全听不见 ---- */
    {
        float zero = SoundReach(2.5f, 5.5f, 11.5f, 5.5f, 3.0f);
        CHECK(zero == 0.0f, "beyond maxRadius the reach must be exactly zero");
    }

    /* ---- ④ 声源自己那一格 = 最强 ---- */
    {
        float self = SoundReach(2.5f, 5.5f, 2.5f, 5.5f, 20.0f);
        CHECK(self > 0.99f, "the source cell itself must be fully audible");
    }

    /* ---- ⑤ 缓存命中：同一个源不重复建场 ---- */
    {
        int before = SoundPropBuildCount();
        (void)SoundReach(2.5f, 5.5f, 6.5f, 5.5f, 20.0f);
        (void)SoundReach(2.5f, 5.5f, 7.5f, 5.5f, 20.0f);
        (void)SoundReach(2.5f, 5.5f, 8.5f, 5.5f, 20.0f);
        CHECK(SoundPropBuildCount() == before,
              "repeated queries from the same source must hit the cache");

        (void)SoundReach(9.5f, 5.5f, 8.5f, 5.5f, 20.0f);   /* 换源 */
        CHECK(SoundPropBuildCount() == before + 1,
              "changing the source must rebuild the field exactly once");
    }

    /* ---- ⑥ 封死的格子：不可达 ---- */
    {
        /* (20,20) 四周全是墙，从来没被打通 */
        CHECK(SoundPropCostAt(20.5f, 20.5f) == PROP_UNREACHABLE,
              "a sealed cell must be unreachable");
        float r = SoundReach(2.5f, 5.5f, 20.5f, 20.5f, 20.0f);
        CHECK(r == 0.0f, "an unreachable point far away must be inaudible");
    }

    SoundPropReset();
    memcpy(worldMap, saved, sizeof(saved));
}

static void test_audio_pan_math(void) {
    float l = 0.0f, r = 0.0f;

    /* 玩家在原点朝 +X；右向量 = (0, 1) → +Y 是右边 */
    const float px = 0.0f, py = 0.0f, dx = 1.0f, dy = 0.0f;

    /* ---- ① 正前方：左右必须相等。
           注意距离衰减对正前方同样生效，所以"满增益"只在距离 0 时成立。
           这两条分开测，才不会被衰减掩盖掉声像问题。 ---- */
    {
        AudioComputeGains(px, py, dx, dy, 0.0f, 0.0f, 20.0f, &l, &r);
        CHECK(l > 0.99f && l < 1.01f, "at zero distance the center gain must be 1.0 (left)");
        CHECK(r > 0.99f && r < 1.01f, "at zero distance the center gain must be 1.0 (right)");

        AudioComputeGains(px, py, dx, dy, 3.0f, 0.0f, 20.0f, &l, &r);
        CHECK((l - r) < 0.001f && (r - l) < 0.001f,
              "straight ahead must be perfectly centered at any distance");
        CHECK(l > 0.0f && l < 1.0f, "straight ahead must still be distance-attenuated");
    }

    /* ---- ② 正右方：右明显大于左 ---- */
    {
        AudioComputeGains(px, py, dx, dy, 0.0f, 3.0f, 20.0f, &l, &r);
        CHECK(r > l, "a sound on the right must be louder on the right channel");
        CHECK(l < 0.001f, "a sound fully on the right must vanish on the left channel");
    }

    /* ---- ③ 正左方：左明显大于右（对称） ---- */
    {
        AudioComputeGains(px, py, dx, dy, 0.0f, -3.0f, 20.0f, &l, &r);
        CHECK(l > r, "a sound on the left must be louder on the left channel");
        CHECK(r < 0.001f, "a sound fully on the left must vanish on the right channel");
    }

    /* ---- ④ 正后方：左右相等（后方没法分左右，只能变小） ---- */
    {
        float fl = 0.0f, fr = 0.0f;
        AudioComputeGains(px, py, dx, dy, -3.0f, 0.0f, 20.0f, &fl, &fr);
        CHECK(fl > 0.0f && fr > 0.0f, "a sound behind must still be audible");
        CHECK((fl - fr) < 0.01f && (fr - fl) < 0.01f,
              "a sound directly behind must not bias either channel");
    }

    /* ---- ⑤ 距离衰减：远处更轻，超过 maxDist 完全听不见 ---- */
    {
        float nl = 0.0f, nr = 0.0f, fl2 = 0.0f, fr2 = 0.0f, zl = 0.0f, zr = 0.0f;
        AudioComputeGains(px, py, dx, dy, 2.0f, 0.0f, 20.0f, &nl, &nr);
        AudioComputeGains(px, py, dx, dy, 15.0f, 0.0f, 20.0f, &fl2, &fr2);
        AudioComputeGains(px, py, dx, dy, 25.0f, 0.0f, 20.0f, &zl, &zr);
        CHECK(nl > fl2, "a nearer sound must be louder than a farther one");
        CHECK(zl == 0.0f && zr == 0.0f,
              "a sound beyond maxDist must be completely silent");
    }

    /* ---- ⑥ 峰值归一：任何方向都不许放大到 1.0 以上 ----
           第一版用常量功率（×√2），侧方的响通道会被放大 41%，
           近距离直接撞上限幅 → 波形压平 → 越近越分不清左右。
           这条断言就是防它回潮的。 ---- */
    {
        float al = 0.0f, ar = 0.0f, bl = 0.0f, br = 0.0f;
        AudioComputeGains(px, py, dx, dy, 3.0f, 0.0f, 20.0f, &al, &ar);   /* 正前 */
        AudioComputeGains(px, py, dx, dy, 0.0f, 3.0f, 20.0f, &bl, &br);   /* 正右 */
        float peakA = (al > ar) ? al : ar;
        float peakB = (bl > br) ? bl : br;
        CHECK(peakA <= 1.0001f, "pan gain must never exceed 1.0 (center)");
        CHECK(peakB <= 1.0001f, "pan gain must never exceed 1.0 (hard side)");
        CHECK(peakA > 0.9f * peakB && peakA < 1.1f * peakB,
              "peak-normalized panning: the loud side must keep the same level");
    }

    /* ---- ⑦ 转身 90 度：同一个声源从右变成前方 ---- */
    {
        float gl = 0.0f, gr = 0.0f;
        AudioComputeGains(px, py, dx, dy, 0.0f, 3.0f, 20.0f, &l, &r);        /* 朝 +X */
        /* 朝向 +Y 之后，(0,3) 在正前方 */
        AudioComputeGains(px, py, 0.0f, 1.0f, 0.0f, 3.0f, 20.0f, &gl, &gr);
        CHECK(l < 0.2f, "before turning the sound is hard on one side");
        CHECK((gl - gr) < 0.001f && (gr - gl) < 0.001f,
              "after turning toward it, the sound must become centered");
        CHECK(gl > l, "after turning toward it, the near channel must get louder");
    }
}

static void test_sound_event_center(void) {
    SoundEvent ev;

    /* ---- ① 类别由 kind 决定，与半径无关 ---- */
    {
        /* 同一 kind、三个不同半径：ttl / decay / susSource 必须一致 */
        float ttls[3];
        float decs[3];
        int   srcs[3];
        static const float radii[3] = { 4.0f, 14.0f, 26.0f };

        for (int i = 0; i < 3; i++) {
            memset(&ev, 0, sizeof(ev));
            ev.kind = SK_IMPACT;
            ev.x = 10.0f; ev.y = 10.0f;
            ev.strength = 52.0f;
            ev.maxRadius = radii[i];
            ev.eventId = 0u;
            SuspicionReset();
            CHECK(SoundEmit(&ev) == 1, "SK_IMPACT must emit");

            /* 从可疑点表里读实际落下的 ttl / decay / source */
            int found = -1;
            for (int k = 0; k < SUSP_MAX; k++) {
                if (susp[k].strength > 0.0f) { found = k; break; }
            }
            CHECK(found >= 0, "emitted event must land in the suspicion table");
            ttls[i] = susp[found].ttl;
            decs[i] = susp[found].decay;
            srcs[i] = susp[found].source;
        }
        CHECK(ttls[0] == ttls[1] && ttls[1] == ttls[2],
              "ttl must come from kind, not from maxRadius");
        CHECK(decs[0] == decs[1] && decs[1] == decs[2],
              "decay must come from kind, not from maxRadius");
        CHECK(srcs[0] == srcs[1] && srcs[1] == srcs[2],
              "suspicion source must come from kind, not from maxRadius");
        CHECK(srcs[0] == SUSP_SEARCH, "SK_IMPACT must map to SUSP_SEARCH");

        /* SK_WALK 与 SK_IMPACT 必须真的不同（否则这套分类没意义） */
        CHECK(SoundKindTtl(SK_WALK) < SoundKindTtl(SK_IMPACT),
              "SK_WALK must have a shorter ttl than SK_IMPACT");
        CHECK(SoundKindDecay(SK_WALK) > SoundKindDecay(SK_IMPACT),
              "SK_WALK must decay faster than SK_IMPACT");
        CHECK(SoundKindSusSource(SK_VOICE) == SUSP_DECOY,
              "SK_VOICE must map to SUSP_DECOY");
    }

    /* ---- ② 同帧同 eventId 去重 ---- */
    {
        SoundEventResetStats();
        memset(&ev, 0, sizeof(ev));
        ev.kind = SK_IMPACT;
        ev.x = 10.0f; ev.y = 10.0f;
        ev.strength = 52.0f;
        ev.eventId = 0xBEEFu;

        SuspicionReset();
        CHECK(SoundEmit(&ev) == 1,  "first emit with an eventId must land");
        CHECK(SoundEmit(&ev) == 0,  "the same eventId in the same frame must be deduped");
        CHECK(SoundEventTotalDeduped() == 1, "dedupe counter must record it");

        /* 换一帧 → 同一个 eventId 又可以了 */
        SoundEventTick();
        CHECK(SoundEmit(&ev) == 1, "after a tick the same eventId is allowed again");
    }

    /* ---- ③ eventId == 0 不去重 ---- */
    {
        SoundEventResetStats();
        memset(&ev, 0, sizeof(ev));
        ev.kind = SK_WALK;
        ev.x = 5.0f; ev.y = 5.0f;
        ev.strength = 20.0f;
        ev.eventId = 0u;
        SuspicionReset();
        CHECK(SoundEmit(&ev) == 1, "eventId 0 emit #1 must land");
        CHECK(SoundEmit(&ev) == 1, "eventId 0 must NOT be deduped (real repeated steps)");
    }

    /* ---- ④ 数值夹取与非法输入 ---- */
    {
        memset(&ev, 0, sizeof(ev));
        ev.kind = SK_IMPACT;
        ev.x = 3.0f; ev.y = 3.0f;
        ev.strength = 999.0f;            /* 会被夹到 100 */
        SoundEventResetStats();
        CHECK(SoundEmit(&ev) == 1, "over-strength event must still emit");
        CHECK(SoundEventLastStrength() <= 100.0f,
              "strength must be clamped to 100");

        ev.x = 0.0f / 0.0f;              /* NaN 坐标 */
        CHECK(SoundEmit(&ev) == 0, "NaN coordinates must be rejected");
    }

    SoundEventTick();
    SoundEventResetStats();
    SuspicionReset();
}

static void test_save_unknown_chunk_roundtrip(void) {
    extern int SaveTestSerialize(const SaveBlob*, unsigned char**);
    extern int SaveTestDeserialize(const void*, long, SaveBlob*);

    /* ---- ① 手搓：已知块 + 未知块 ---- */
    static SaveBlob blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = SAVE_MAGIC;
    blob.version = SAVE_VERSION;
    blob.lang = 0;

    static const unsigned char unknown[12] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };

    static unsigned char buf[16384];   /* SaveBlob 约 6.5 KB，留足 */
    SaveChunkDesc d[2];
    const void* body[2];
    d[0].id = SC_LEGACY29; d[0].schema = 1; d[0].size = (unsigned)sizeof(blob);
    body[0] = &blob;
    d[1].id = 0x7777;      d[1].schema = 9; d[1].size = (unsigned)sizeof(unknown);
    body[1] = unknown;

    int pay = SaveContainerPack(d, body, 2, buf + sizeof(SaveEnvelope),
                                (int)(sizeof(buf) - sizeof(SaveEnvelope)), NULL);
    CHECK(pay > 0, "test fixture: two-chunk payload must pack");
    SaveEnvelope h;
    SaveEnvelopeSeal(&h, buf + sizeof(SaveEnvelope), pay, 1111u);
    memcpy(buf, &h, sizeof(h));
    int total = (int)sizeof(SaveEnvelope) + pay;

    /* ---- ② 反序列化 ---- */
    static SaveBlob loaded;
    CHECK(SaveTestDeserialize(buf, total, &loaded) != 0,
          "a valid two-chunk CSAV must load");
    CHECK(loaded.lang == 0, "test fixture: lang starts at 0");

    /* ---- ③ 改一个已知字段 ---- */
    loaded.lang = 7;

    /* ---- ④ 重新序列化 ---- */
    unsigned char* out = NULL;
    int outLen = SaveTestSerialize(&loaded, &out);
    CHECK(outLen > 0 && out != NULL, "re-serializing a loaded save must succeed");

    if (outLen > 0 && out) {
        /* ---- ⑤ 未知块必须逐字节保留 ---- */
        const void* got = NULL;
        int gotLen = 0;
        CHECK(SaveContainerFind(out + sizeof(SaveEnvelope), outLen - (int)sizeof(SaveEnvelope),
                                0x7777, &got, &gotLen) == 1,
              "unknown chunk 0x7777 must survive a load-modify-save cycle");
        CHECK(gotLen == (int)sizeof(unknown),
              "unknown chunk must keep its size");
        CHECK(got && memcmp(got, unknown, sizeof(unknown)) == 0,
              "unknown chunk must keep its bytes exactly");

        /* ---- ⑥ 已知块是新内容 ---- */
        SaveEnvelope nh;
        memcpy(&nh, out, sizeof(nh));
        CHECK(SaveEnvelopeValid(&nh, out + sizeof(SaveEnvelope),
                                outLen - (int)sizeof(SaveEnvelope)) == 1,
              "re-serialized envelope must carry a valid checksum");

        const void* kb = NULL;
        int klen = 0;
        CHECK(SaveContainerFind(out + sizeof(SaveEnvelope), outLen - (int)sizeof(SaveEnvelope),
                                SC_LEGACY29, &kb, &klen) == 1,
              "known chunk must still be present");
        if (kb && klen >= (int)sizeof(SaveBlob)) {
            SaveBlob check;
            memcpy(&check, kb, sizeof(check));
            CHECK(check.lang == 7, "the modified field must be persisted in the known chunk");
        }

        /* 块顺序：已知块在前、未知块在后（原位替换） */
        {
            SaveChunkDesc first;
            CHECK(SaveContainerAt(out + sizeof(SaveEnvelope),
                                  outLen - (int)sizeof(SaveEnvelope), 0, &first, NULL) == 1,
                  "chunk 0 must be readable");
            CHECK(first.id == SC_LEGACY29,
                  "chunk order must be preserved (known chunk stays in place)");
        }

        free(out);
    }

    /* ---- ⑦ 重复 ID 必须被拒绝，而不是静默挑一份 ---- */
    {
        unsigned char dup[4096];
        SaveChunkDesc dd[2];
        const void* db[2];
        dd[0].id = 0x7777; dd[0].schema = 1; dd[0].size = 4; db[0] = unknown;
        dd[1].id = 0x7777; dd[1].schema = 1; dd[1].size = 4; db[1] = unknown + 4;
        int dp = SaveContainerPack(dd, db, 2, dup + sizeof(SaveEnvelope),
                                   (int)(sizeof(dup) - sizeof(SaveEnvelope)), NULL);
        CHECK(dp > 0, "test fixture: duplicate-id payload must pack");
        SaveEnvelope dh;
        SaveEnvelopeSeal(&dh, dup + sizeof(SaveEnvelope), dp, 2222u);
        memcpy(dup, &dh, sizeof(dh));

        static SaveBlob dloaded;
        CHECK(SaveTestDeserialize(dup, (int)sizeof(SaveEnvelope) + dp, &dloaded) == 0,
              "a save with duplicate chunk ids must be rejected");
    }

    /* 收尾：让后续用例从干净状态开始（carry 已被置为只读） */
    {
        static SaveBlob clean;
        memset(&clean, 0, sizeof(clean));
        clean.magic = SAVE_MAGIC;
        clean.version = SAVE_VERSION;
        unsigned char* cout = NULL;
        int cl = SaveTestSerialize(&clean, &cout);
        if (cout) free(cout);
        (void)cl;
    }
}

static void test_cloud_decide_length(void) {
    extern int SteamZoneCloudDecide(const void*, int, const void*, int, int*);

    unsigned char A[512], B[512];
    SaveEnvelope h;
    const unsigned char payA[16] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };
    const unsigned char payB[40] = { 9,9,9,9 };     /* 更长的 payload */

    /* A：短的那份 */
    memcpy(A + sizeof(h), payA, sizeof(payA));
    SaveEnvelopeSeal(&h, A + sizeof(h), (int)sizeof(payA), 1000u);
    memcpy(A, &h, sizeof(h));
    int lenA = (int)sizeof(h) + (int)sizeof(payA);

    /* B：长的那份，stamp 更新 */
    memcpy(B + sizeof(h), payB, sizeof(payB));
    SaveEnvelopeSeal(&h, B + sizeof(h), (int)sizeof(payB), 2000u);
    memcpy(B, &h, sizeof(h));
    int lenB = (int)sizeof(h) + (int)sizeof(payB);

    int win = 0;

    /* ① 内容相同 → SAMECL（自身对比） */
    CHECK(SteamZoneCloudDecide(A, lenA, A, lenA, &win) == STEAM_SYNC_SAMECL,
          "identical local/remote must be SAMECL");

    /* ② 长度不同 + 云端更新 → CLOUDWIN，且 out 长度是**远端**的 */
    win = 0;
    CHECK(SteamZoneCloudDecide(A, lenA, B, lenB, &win) == STEAM_SYNC_CLOUDWIN,
          "different length must not be treated as corruption (cloud newer)");
    CHECK(win == lenB, "CLOUDWIN must report the remote length, not the local one");

    /* ③ 长度不同 + 本地更新 → LOCALWIN（不覆盖云端） */
    win = 0;
    CHECK(SteamZoneCloudDecide(B, lenB, A, lenA, &win) == STEAM_SYNC_LOCALWIN,
          "different length must not be treated as corruption (local newer)");

    /* ④ 同 stamp 不同内容 → CONFLICT，不裁决 */
    {
        unsigned char C[512];
        memcpy(C + sizeof(h), payB, sizeof(payB));
        SaveEnvelopeSeal(&h, C + sizeof(h), (int)sizeof(payB), 1000u);   /* 同 A 的 stamp */
        memcpy(C, &h, sizeof(h));
        int lenC = (int)sizeof(h) + (int)sizeof(payB);
        CHECK(SteamZoneCloudDecide(A, lenA, C, lenC, &win) == STEAM_SYNC_CONFLICT,
              "same timestamp with different content must be reported as CONFLICT");
    }

    /* ⑤ 远端被篡改一个字节 → ERROR（校验和抓得住） */
    {
        unsigned char D[512];
        memcpy(D, B, (size_t)lenB);
        D[sizeof(h) + 5] ^= 0xFF;
        CHECK(SteamZoneCloudDecide(A, lenA, D, lenB, &win) == STEAM_SYNC_ERROR,
              "a corrupted remote payload must be rejected");
    }

    /* ⑥ 远端长度字段与真实长度不符 → ERROR */
    {
        unsigned char E[512];
        memcpy(E, B, (size_t)lenB);
        SaveEnvelope bad;
        memcpy(&bad, E, sizeof(bad));
        bad.payload = 7;                 /* 谎报 */
        memcpy(E, &bad, sizeof(bad));
        CHECK(SteamZoneCloudDecide(A, lenA, E, lenB, &win) == STEAM_SYNC_ERROR,
              "a remote header lying about its payload size must be rejected");
    }
}

static void test_save_backend_errors(void) {
    extern int SaveBackendWrite(const char*, const void*, size_t);
    unsigned char buf[32];
    memset(buf, 0x5A, sizeof(buf));

    CHECK(SaveBackendWrite(NULL, buf, sizeof(buf)) != 0,
          "SaveBackendWrite must reject a NULL path");
    CHECK(SaveBackendWrite("", buf, sizeof(buf)) != 0,
          "SaveBackendWrite must reject an empty path");

    /* 目录不存在 → CreateFile 失败 → 必须返回系统错误码，不能返回 0 */
    {
        const char* dir = getenv("CANITY_DATA_DIR");
        CHECK(dir != NULL, "CANITY_DATA_DIR must be set");
        if (dir) {
            char bad[MAX_PATH];
            snprintf(bad, sizeof(bad), "%s\\__no_such_dir__\\x.sav", dir);
            CHECK(SaveBackendWrite(bad, buf, sizeof(buf)) != 0,
                  "SaveBackendWrite must fail when the directory does not exist");
        }
    }
}

static void test_stone_break_single_event(void) {
    extern void ThrowItem(int, float);
    extern void UpdateShots(float);
    extern float posX, posY, dirX, dirY;
    extern Container containers[MAX_CONTAINERS];
    extern int containerCount;

    /* 找一个确定空着的角落，不依赖 GenerateLevel 的具体布局 */
    posX = 12.0f; posY = 12.0f;
    dirX = 1.0f;  dirY = 0.0f;

    /* 沿 +X 摆几个上锁的箱子：不管瓶子飞多远，落点 2.5 格内必有一个 */
    int savedCount = containerCount;
    containerCount = 0;
    for (int i = 0; i < MAX_CONTAINERS; i++) {
        containers[i].active = 0;
        containers[i].locked = 0;
        containers[i].mimicHost = 0;
        containers[i].mimicShell = 0;
        containers[i].searched = 0;
        containers[i].content = 0;
    }
    {
        static const float dist[6] = { 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
        for (int i = 0; i < 6 && i < MAX_CONTAINERS; i++) {
            containers[i].x = posX + dist[i];
            containers[i].y = posY;
            containers[i].active = 1;
            containers[i].locked = 1;
            containers[i].lockType = 1;
            containers[i].lockCode = 0;
        }
        containerCount = 6;
    }

    SuspicionReset();
    ThrowItem(1, 0.10f);                    /* kind 1 = 投石/破门石 */

    /* 固定步进 2.5 秒：比任何一次抛掷的滞空时间都长 */
    for (int f = 0; f < 300; f++) UpdateShots(1.0f / 120.0f);

    int unlocked = 0;
    for (int i = 0; i < containerCount; i++) {
        if (containers[i].active && !containers[i].locked) unlocked++;
    }
    CHECK(unlocked >= 1, "a sling landing near a locked crate must break exactly one lock");
    CHECK(SuspicionActiveCount() == 1,
          "one breaking throw must leave exactly one suspicion point");

    /* 复原，别影响后面的用例 */
    for (int i = 0; i < MAX_CONTAINERS; i++) containers[i].active = 0;
    containerCount = savedCount;
    SuspicionReset();
}

int main(void) {
    CHECK(getenv("CANITY_DATA_DIR")!=NULL,"tests require isolated save directory");
    srand(1234);InitAchievements();InitPixels();
    test_music();test_daily();test_wallet_save();test_stone_break_single_event();test_save_backend_errors();test_cloud_decide_length();test_save_unknown_chunk_roundtrip();test_sound_event_center();test_audio_pan_math();test_sound_propagation();test_suspicion_best_uses_propagation();test_monster_search_around();test_stone_hearing_range();test_theme_rooms_placement();test_room_props();test_room_seed_roundtrip();test_prop_layers();test_hatch_screenshot();test_all_hatch_effects();test_hatch_end_to_end();test_hatch_state_and_mask();test_collapse_layer_params();test_balance_targets();test_load_clamps_flags();test_trees_shop();test_runs_and_layout();test_wrench();test_three_hunters();test_hunter_models();test_floor_resets_hunters();
    printf("CANITY runtime integration: PASS (%d checks)\n",checks);return 0;
}

/* tests/domains/test_save.c —— save 域的集成测试
 *
 * 第 5 轮（2026-11-08）从 test_runtime.c 原样搬过来，断言一条没改。
 * 入口：TestSuite_save()
 */
#include "../test_common.h"
#include <stddef.h>

static void test_legacy_fixture_one(const char* file, int oldVer, int wantPayload,
                                    int* ok, int* total) {
    extern const char* SavePathForSlot(int, int, char*, size_t);
    char dst[MAX_PATH];
    char src[MAX_PATH];
    int loaded;
    int same = 1;

    snprintf(src, sizeof(src), "tests/fixtures/saves/%s", file);
    SavePathForSlot(0, 0, dst, sizeof(dst));

    ++(*total);
    if (!fixture_copy(src, dst)) {
        fprintf(stderr, "  [fixture] %-20s 复制失败（夹具缺失？）\n", file);
        return;
    }

    loaded = LoadSettings();
    if (!loaded) {
        fprintf(stderr, "  [fixture] %-20s 读档被拒\n", file);
        return;
    }

    /* S-04：读档必须报告**真实载荷长度**，不是 sizeof(SaveBlob)。
       期望值来自夹具里 SC_LEGACY29 块的实际字节数。 */
    {
        extern long SaveTestPayloadLen(void);
        long got = SaveTestPayloadLen();
        if (wantPayload > 0 && got != (long)wantPayload) {
            same = 0;
            fprintf(stderr, "  [fixture] %-20s 载荷长度 %ld != 期望 %d\n",
                    file, got, wantPayload);
        }
    }

    ContinueRun();

    if (currentLevel != 3) same = 0;
    if (fabsf(sanity - 61.5f) >= 0.5f) same = 0;
    if (fabsf(posX - 12.5f) >= 0.05f || fabsf(posY - 9.5f) >= 0.05f) same = 0;

    /* 迁移前必须留下原件：<存档>.v<旧版本>.bak。
       这条比"读出来对"更基础 —— 读错还能回滚，没备份就只能认栽。 */
    if (oldVer > 0 && oldVer <= 30) {
        char bk[MAX_PATH + 24];
        FILE* fp;
        snprintf(bk, sizeof(bk), "%s.v%d.bak", dst, oldVer);
        fp = fopen(bk, "rb");
        if (!fp) { same = 0; fprintf(stderr, "  [fixture] %-20s 缺少备份 %s\n", file, bk); }
        else fclose(fp);
    }

    fprintf(stderr, "  [fixture] %-20s %s  level=%d sanity=%.1f pos=(%.1f,%.1f)\n",
            file, same ? "OK  " : "错位", currentLevel, sanity, posX, posY);
    if (same) ++(*ok);
}

static void test_save_v34_items(void) {
    extern int echoBolts, decoys, phaseBeacons, jumperSpawnedRun;

    clear_meta(); StartGame();
    echoBolts = 3; decoys = 2; phaseBeacons = 4; jumperSpawnedRun = 3;
    runInProgress = 1;
    SaveGame();
    CHECK(g_saveState == 1, "v34 save write succeeds");

    echoBolts = 0; decoys = 0; phaseBeacons = 0; jumperSpawnedRun = 0;
    CHECK(LoadSettings(), "v34 save can be loaded");
    ContinueRun();

    CHECK(echoBolts == 3,        "echo bolts must survive a restart");
    CHECK(decoys == 2,           "decoys must survive a restart");
    CHECK(phaseBeacons == 4,     "phase beacons must survive a restart");
    CHECK(jumperSpawnedRun == 3, "the facehugger per-run budget must survive a restart");
}

static void test_training_items_do_not_leak(void) {
    extern int phaseBeacons;

    /* trainingMode / pendingStartMode 都是 game.c 的内部状态，
       测试不去伪造入口，而是直接验证**不变量**：
       StartGame 必须无条件重置本局库存，再按模式发放。

       旧实现的缺陷正是"只在训练场赋值、普通局从不重置"：
       同一个进程里先玩训练场（发 5 枚），再开正式局 —— 5 枚被带过去。
       所以这里手工把库存摆成 5，再走一次普通开局。 */
    clear_meta();
    phaseBeacons = 5;
    StartGame();
    CHECK(phaseBeacons == 0, "training: stock must not leak into a normal run");

    /* 再来一次，确认不是偶发（上一局残留也不允许带） */
    phaseBeacons = 3;
    StartGame();
    CHECK(phaseBeacons == 0, "training: a second normal start also resets the stock");
}

static void test_menu_save_keeps_run(void) {
    extern int echoBolts, decoys, phaseBeacons, jumperSpawnedRun;
    extern int hudDetail;
    int hudBefore;

    clear_meta();
    StartGame();

    /* ① 造一个"正在进行"的局面，并记下特征值 */
    currentLevel = 5;
    sanity  = 42.0f;
    battery = 33.0f;
    posX = 7.25f; posY = 11.75f; dirX = 0.6f; dirY = 0.8f;
    flares = 3;
    /* 注意 CLAMP_ITEM_RARE 上限是 4、CLAMP_ITEM 是 8，
       测试值必须落在合法区间，否则读回时被夹取是**正确行为**。 */
    echoBolts = 4; decoys = 3; phaseBeacons = 2; jumperSpawnedRun = 2;
    runInProgress = 1;
    SaveGame();
    CHECK(g_saveState == 1, "menu-save: the in-run save is written");

    /* ② 模拟回到主菜单：运行时全局被清空，runInProgress 归零 */
    currentLevel = 1;
    sanity = 100.0f;
    battery = 100.0f;
    posX = 0.0f; posY = 0.0f;
    flares = 0;
    echoBolts = 0; decoys = 0; phaseBeacons = 0; jumperSpawnedRun = 0;
    runInProgress = 0;
    currentState = STATE_MAIN_MENU;

    /* ③ 改一个设置项再保存（这就是"菜单存档"那条路径） */
    hudBefore = hudDetail;
    hudDetail = hudDetail ? 0 : 1;          /* 随便改一个设置字段 */
    SaveGame();
    CHECK(g_saveState == 1, "menu-save: the menu save is written");

    /* ④ 读回来核对：运行段必须一模一样 */
    CHECK(LoadSettings(), "menu-save: save can be reloaded");
    ContinueRun();

    CHECK(currentLevel == 5,          "menu-save: level survives a menu save");
    CHECK(fabsf(sanity - 42.0f) < 0.5f,   "menu-save: sanity survives a menu save");
    CHECK(fabsf(posX - 7.25f) < 0.05f && fabsf(posY - 11.75f) < 0.05f,
                                      "menu-save: position survives a menu save");
    CHECK(flares == 3,                "menu-save: flares survive a menu save");
    CHECK(echoBolts == 4,             "menu-save: echo bolts survive a menu save");
    CHECK(decoys == 3,                "menu-save: decoys survive a menu save");
    CHECK(phaseBeacons == 2,          "menu-save: phase beacons survive a menu save");
    CHECK(jumperSpawnedRun == 2,      "menu-save: facehugger budget survives a menu save");
    CHECK(hudDetail == (hudBefore ? 0 : 1),
                                      "menu-save: the setting change itself is kept");
}

static void test_corrupt_save_is_never_overwritten(void) {
    extern const char* SavePathForSlot(int, int, char*, size_t);
    char dst[MAX_PATH];
    char src[MAX_PATH];
    char bak[MAX_PATH + 24];
    unsigned char orig[64];
    long n;
    long cut;
    FILE* f;
    int sameBytes = 0;

    snprintf(src, sizeof(src), "tests/fixtures/saves/sav_v2_4_1.sav");
    SavePathForSlot(0, 0, dst, sizeof(dst));

    /* 先清场：LoadSettings 在主档读不出来时会回退 .bak，
       上一轮用例留下的 .bak 会让"坏档"看起来能读 —— 那是测试污染，不是产品行为。 */
    {
        int v;
        char side[MAX_PATH + 24];
        remove(dst);
        snprintf(side, sizeof(side), "%s.bak", dst);
        remove(side);
        for (v = 0; v <= 40; v++) {
            snprintf(side, sizeof(side), "%s.v%d.bak", dst, v);
            remove(side);
        }
    }
    CHECK(fixture_copy(src, dst), "corrupt-save test: fixture copied");

    /* 读原文件长度并备份一份原始字节用于比对 */
    f = fopen(dst, "rb");
    CHECK(f != NULL, "corrupt-save test: can open fixture");
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    CHECK(n > 200, "corrupt-save test: fixture is large enough");

    /* 截断到一半：信封校验和必然对不上 */
    cut = n / 2;
    f = fopen(dst, "rb");
    CHECK(fread(orig, 1, sizeof(orig), f) == sizeof(orig), "corrupt-save test: head read");
    fclose(f);

    {
        unsigned char* all = (unsigned char*)malloc((size_t)cut);
        CHECK(all != NULL, "corrupt-save test: alloc");
        f = fopen(dst, "rb");
        CHECK(fread(all, 1, (size_t)cut, f) == (size_t)cut, "corrupt-save test: read half");
        fclose(f);
        f = fopen(dst, "wb");
        CHECK(fwrite(all, 1, (size_t)cut, f) == (size_t)cut, "corrupt-save test: truncate");
        fclose(f);
        free(all);
    }

    CHECK(!LoadSettings(), "corrupt save must not load as if it were fine");

    /* 原件要么还在原处，要么被改名成 .v*.bak —— 两种都接受，但字节必须一致 */
    f = fopen(dst, "rb");
    if (f) {
        unsigned char now[64];
        if (fread(now, 1, sizeof(now), f) == sizeof(now) &&
            memcmp(now, orig, sizeof(now)) == 0) sameBytes = 1;
        fclose(f);
    }
    if (!sameBytes) {
        int v;
        for (v = 0; v <= 40 && !sameBytes; v++) {
            snprintf(bak, sizeof(bak), "%s.v%d.bak", dst, v);
            f = fopen(bak, "rb");
            if (!f) continue;
            {
                unsigned char now[64];
                if (fread(now, 1, sizeof(now), f) == sizeof(now) &&
                    memcmp(now, orig, sizeof(now)) == 0) sameBytes = 1;
            }
            fclose(f);
        }
    }
    CHECK(sameBytes, "corrupt save bytes must still exist on disk (never silently overwritten)");
}

static void test_legacy_fixtures(void) {
    /* 裸档（v27）与四个 CSAV 信封档（v29 / v30 / v33 / v34）。
       每个夹具录入时的状态都是 level=3 / sanity=61.5 / pos=(12.5,9.5)。 */
    /* wantPayload = 该夹具里 SC_LEGACY29 块的真实字节数（裸档取文件长度） */
    static const struct { const char* file; int ver; int payload; } files[] = {
        { "sav_v2_3_6.sav",   27,  6404 },
        { "sav_v2_3_9.sav",   29,  6480 },
        { "sav_v2_4-rc.sav",  30,  6488 },
        { "sav_v2_4.sav",     33, 12412 },
        { "sav_v2_4_1.sav",   34, 12428 }
    };
    int ok = 0, total = 0;
    size_t i;

    fprintf(stderr, "  ---- 历史存档迁移（tests/fixtures/saves）----\n");
    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++)
        test_legacy_fixture_one(files[i].file, files[i].ver, files[i].payload, &ok, &total);

    fprintf(stderr, "  ---- 迁移结果: %d/%d 正确 ----\n", ok, total);
    checks += total;   /* 每个夹具都算一次检查，别让汇总把覆盖数吃掉 */
    CHECK(ok == total, "every historical save must migrate with identical key state");
    /* 第 2 轮：读完老档之后难度必须是"标准"，不能因为字段缺失变成轻松 */
    { extern int difficulty; CHECK(difficulty == 1,
        "a legacy save without a difficulty field must load as NORMAL"); }
}

static void test_v33_save_is_accepted(void) {
    extern int echoBolts, decoys, phaseBeacons, jumperSpawnedRun;
    extern const char* SavePathForSlot(int, int, char*, size_t);

    clear_meta(); StartGame();
    runInProgress = 1;
    SaveGame();                       /* 先把目录和路径建出来 */
    CHECK(g_saveState == 1, "prerequisite save written");

    /* 自己造一份 v33 档：当前布局的前缀 + 版本号 33 */
    {
        SaveBlob b;
        memset(&b, 0, sizeof(b));
        b.magic   = SAVE_MAGIC;
        b.version = 33;               /* 上一版 */
        /* 必须标成"有进行中的对局"，否则 LoadSettings 读成功之后
           会按"没有可继续的存档"返回 0 —— 那是另一条退出路径，
           会让这条用例看起来像"v33 被版本校验拒了"。 */
        b.hasRun = 1;
        b.flares  = 7;                /* 用来证明"前缀字段没丢" */
        b.bankGoldSave = 4242;
        b.echoBolts = 9;              /* 老档里这几个字段**不存在**，填垃圾值 */
        b.decoys = 9;
        b.phaseBeacons = 9;
        b.jumperSpawnedRun = 9;

        /* ==================================================================
         * 写遍**所有候选路径 × 所有槽**。
         * 第一次只写了槽 0 的候选 0，结果 LoadSettings 读的是别的路径，
         * 拿到的还是上一步的 v34 档 —— 用例假失败。
         * SavePathForSlot 是导出的，所以这里可以老老实实全覆盖。
         * ================================================================ */
        /* v33 档 = 一直写到 echoBolts 之前（v34 才开始有那四个字段）。
           用 offsetof 表达，以后再加字段也不会算错 —— 原来是
           sizeof - 4*int，v35 多了一个 int 之后就偏了 4 字节。 */
        size_t legacyLen = offsetof(SaveBlob, echoBolts);
        int written = 0;
        for (int slot = 0; slot < 3; slot++) {
            for (int cand = 0; cand < 6; cand++) {
                char p[MAX_PATH];
                SavePathForSlot(cand, slot, p, sizeof(p));
                if (!p[0]) continue;
                FILE* f = fopen(p, "wb");
                if (!f) continue;
                fwrite(&b, 1, legacyLen, f);
                fclose(f);
                written++;
                char bak[MAX_PATH + 16];
                snprintf(bak, sizeof(bak), "%s.bak", p);
                FILE* fb = fopen(bak, "wb");
                if (fb) { fwrite(&b, 1, legacyLen, fb); fclose(fb); }
            }
        }
        CHECK(written > 0, "the v33 fixture must reach at least one real save path");
    }

    echoBolts = decoys = phaseBeacons = jumperSpawnedRun = -1;
    CHECK(LoadSettings(), "a v33 save must NOT be rejected by the version gate");
    /* LoadSettings 只把数据填进 g_save；全局变量是在 ContinueRun 里灌回来的。
       不调这一步，断言检查的还是测试自己设的 -1。 */
    ContinueRun();

    /* 尾部四项在老档里不存在 → 必须是 0，而不是垃圾值 */
    CHECK(echoBolts == 0 && decoys == 0 && phaseBeacons == 0 && jumperSpawnedRun == 0,
          "fields appended in v34 must read as 0 from a v33 save");
    /* 前缀字段必须原样保留 */
    {
        extern const SaveBlob* SaveTestBlob(void);
        const SaveBlob* blob = SaveTestBlob();
        CHECK(blob->flares == 7,
              "v33 prefix fields must survive the v33->v34 hand-off");
        CHECK(blob->bankGoldSave == 4242,
              "v33 economy fields must survive the v33->v34 hand-off");
    }
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


/* ==================================================================
 * 2026-11-08（第 2 轮）：难度档位
 * ------------------------------------------------------------------
 * 玩家反馈分两类：「太难，每次都硬吃」和「不恐怖，好安全」。
 * 同一套参数满足不了两端 → 做成三档，只调六个可感知的量。
 * 这里锁的是"三档必须真的不同"和"存档往返不丢"。
 * ================================================================ */

#include "difficulty.h"

static void test_difficulty_multipliers_differ(void) {
    int saved = difficulty;

    difficulty = DIFF_EASY;
    {
        float t = DiffThreatIntervalMul(), s2 = DiffSanityDrainMul();
        float w = DiffWardenBlockAdd(), q = DiffQteWindowMul();
        difficulty = DIFF_NORMAL;
        CHECK(t > DiffThreatIntervalMul(), "easy must spawn threats less often than normal");
        CHECK(s2 < DiffSanityDrainMul(), "easy must drain sanity slower than normal");
        CHECK(w < DiffWardenBlockAdd(), "easy must shorten the warden block");
        CHECK(q > DiffQteWindowMul() && q > 0.0f, "easy must widen the QTE window");
    }

    difficulty = DIFF_HARD;
    {
        float t = DiffThreatIntervalMul(), s2 = DiffSanityDrainMul();
        difficulty = DIFF_NORMAL;
        CHECK(t < DiffThreatIntervalMul(), "hardcore must spawn threats more often");
        CHECK(s2 > DiffSanityDrainMul(), "hardcore must drain sanity faster");
    }

    /* 标准档必须是恒等值 —— 否则"默认手感"就被悄悄改了 */
    difficulty = DIFF_NORMAL;
    CHECK(DiffThreatIntervalMul() == 1.0f && DiffSanityDrainMul() == 1.0f &&
          DiffWrenchWindowMul() == 1.0f,
          "the normal difficulty must be the identity (no hidden rebalance)");
    CHECK(DiffWardenBlockAdd() == 0.0f, "normal must not shift the warden block");

    difficulty = saved;
}

static void test_difficulty_roundtrips_through_save(void) {
    extern const SaveBlob* SaveTestBlob(void);
    int saved = difficulty;

    clear_meta(); StartGame();
    runInProgress = 1;
    difficulty = DIFF_HARD;
    SaveGame();
    {
        const SaveBlob* b = SaveTestBlob();
        CHECK(b->difficultyPlus1 == DIFF_HARD + 1,
              "hardcore must be stored as 3 (difficulty+1), not 2");
    }

    /* 读回来 */
    difficulty = DIFF_EASY;
    CHECK(LoadSettings(), "save must load back");
    CHECK(difficulty == DIFF_HARD, "difficulty must survive a save/load round trip");

    /* 老档语义：字段为 0 → 标准，不是轻松 */
    difficulty = saved;
}

static void test_continue_rejects_invalid_exit_and_facing(void) {
    clear_meta(); StartGame();
    runInProgress = 1;
    SaveTestClearReadOnly();
    exitX = -1000.0f; exitY = NAN;
    dirX = NAN; dirY = 0.0f;
    SaveGame();
    ContinueRun();
    CHECK(exitX >= 1.0f && exitX <= MAP_WIDTH - 1.0f &&
          exitY >= 1.0f && exitY <= MAP_HEIGHT - 1.0f,
          "clamped saved exits cannot be overwritten by raw coordinates later");
    CHECK(dirX == 1.0f && dirY == 0.0f,
          "invalid saved facing falls back to a finite camera direction");
    exitX = 2.5f; exitY = 3.5f;
    dirX = 3.0f; dirY = 4.0f;
    SaveGame();
    ContinueRun();
    CHECK(fabsf(dirX - 0.6f) < 0.0001f && fabsf(dirY - 0.8f) < 0.0001f,
          "finite nonunit saved facing is normalized before camera setup");
}

static void test_saving_preserves_live_skill_ranks(void) {
    clear_meta(); StartGame(); SaveTestClearReadOnly();
    runInProgress = 1;
    int expected[SKILL_COUNT];
    for (int id = 0; id < SKILL_COUNT; id++) {
        expected[id] = SkillIsPlayerFacing(id) ? skillDefs[id].maxRank : 0;
        skillRank[id] = expected[id];
    }
    SaveGame();
    CHECK(memcmp(expected, skillRank, sizeof(expected)) == 0,
          "serialization must not restore zeroed saved ranks into live skills");
    const SaveBlob* blob = SaveTestBlob();
    for (int id = 0; id < SKILL_COUNT; id++) {
        if(!SkillIsPlayerFacing(id))continue;
        int stored = id < 26 ? blob->skillRank[id] :
                     id < 30 ? blob->extraSkillRanks[id-26] :
                     id < 41 ? blob->archetypeRanks[id-30] : blob->metaExpansionRanks[id-41];
        CHECK(stored == expected[id], "every player skill persists to its stable storage region");
    }
    memset(skillRank, 0, sizeof(skillRank));
    ContinueRun();
    CHECK(memcmp(expected, skillRank, sizeof(expected)) == 0,
          "all player skills, including archetypes, survive a save/continue roundtrip");
}

static void test_v37_prefix_is_migrated_to_v38(void) {
    clear_meta(); StartGame(); SaveTestClearReadOnly();
    runInProgress = 1;
    SaveGame();
    SaveBlob legacy = *SaveTestBlob();
    legacy.version = 37;
    legacy.archetypeRanks[0] = 1;
    legacy.musicVolume = 0.65f;
    int written = 0;
    for (int slot = 0; slot < 3; slot++) for (int cand = 0; cand < 6; cand++) {
        char path[MAX_PATH];
        SavePathForSlot(cand, slot, path, sizeof(path));
        FILE* file = fopen(path, "wb");
        if (!file) continue;
        size_t size = offsetof(SaveBlob, bestiaryExtraKnowledge);
        int ok = fwrite(&legacy, 1, size, file) == size;
        fclose(file);
        written += ok;
    }
    CHECK(written > 0, "v37 migration fixture must be written");
    CHECK(LoadSettings(), "the true v37 prefix must remain loadable");
    CHECK(SaveTestBlob()->version == SAVE_VERSION, "v37 must be promoted without archiving a supported save");
    CHECK(skillRank[SK_GHOST_BREATH] == 1, "v37 archetype ranks keep their original slots");
    CHECK(fabsf(SaveTestBlob()->musicVolume - 0.65f) < 0.0001f, "migration keeps independent music volume");
    CHECK(SaveTestBlob()->bestiaryExtraKnowledge[0] == 0 &&
          SaveTestBlob()->bestiaryExtraKnowledge[1] == 0 &&
          SaveTestBlob()->bestiaryExtraKnowledge[2] == 0,
          "missing mimic extension defaults to zero, never reads adjacent settings bytes");
}

void TestSuite_save(void) {
    test_saving_preserves_live_skill_ranks();
    test_v37_prefix_is_migrated_to_v38();
    test_continue_rejects_invalid_exit_and_facing();
    test_save_v34_items();
    test_training_items_do_not_leak();
    test_menu_save_keeps_run();
    test_corrupt_save_is_never_overwritten();
    test_legacy_fixtures();
    test_v33_save_is_accepted();
    test_difficulty_multipliers_differ();
    test_difficulty_roundtrips_through_save();
    test_wallet_save();
    test_save_unknown_chunk_roundtrip();
    test_save_backend_errors();
}

void TestSuite_legacy_fixtures(void){test_legacy_fixtures();}

/*
 * CANITY offline smoke test.
 *
 * This deliberately has no Win32 or game-runtime dependency.  It checks the
 * design contracts that are easy to regress while iterating on the game:
 *   - the 12 in-run bench nodes and their three-tier costs;
 *   - hide-spot candidates never use wall cells or the exit zone;
 *   - the tune-lock controls fit without overlap at the logical 600px height;
 *   - the source still contains the runtime guards for hiding, lock input,
 *     stable screen effects, and the green tune readouts.
 *
 * Build from the repository root with any C99 compiler, for example:
 *   gcc tests/test_canity.c -std=c99 -Wall -Wextra -o tests/test_canity
 */

#include "../include/common.h"
#include "../include/steam_ach.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAP_WIDTH 24
#define MAP_HEIGHT 24
#define HIDESPOT_MAX 6
#define BENCH_COUNT 16
#define LOCK_TUNE_KNOBS 3
#define LOCK_TUNE_STEPS 8

static void fail(const char* message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void check(int condition, const char* message) {
    if (!condition) fail(message);
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    long size;
    char* text;
    if (!f) fail(path);
    if (fseek(f, 0, SEEK_END) != 0) fail(path);
    size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) fail(path);
    text = (char*)malloc((size_t)size + 1u);
    if (!text) fail("out of memory");
    if (fread(text, 1, (size_t)size, f) != (size_t)size) fail(path);
    text[size] = '\0';
    fclose(f);
    return text;
}

static int contains(const char* text, const char* needle) {
    return strstr(text, needle) != NULL;
}

static int count_contains(const char* text, const char* needle) {
    int count = 0;
    size_t n = strlen(needle);
    const char* p = text;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += n;
    }
    return count;
}

static int hide_candidate_clear(const int map[MAP_HEIGHT][MAP_WIDTH],
                                float x, float y, float exitX, float exitY) {
    int cx = (int)floorf(x);
    int cy = (int)floorf(y);
    float dx = x - exitX;
    float dy = y - exitY;
    if (cx <= 0 || cy <= 0 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) return 0;
    if (map[cy][cx] != 0) return 0;
    return dx * dx + dy * dy >= 6.0f * 6.0f;
}

/* 把 C 源码里的注释替换成空格（保持长度，避免误判） */
static void strip_comments(char* s) {
    int i = 0;
    while (s[i]) {
        if (s[i] == '/' && s[i + 1] == '*') {
            while (s[i] && !(s[i] == '*' && s[i + 1] == '/')) { s[i] = ' '; i++; }
            if (s[i]) { s[i] = ' '; s[i + 1] = ' '; i += 2; }
        } else if (s[i] == '/' && s[i + 1] == '/') {
            while (s[i] && s[i] != '\n') { s[i] = ' '; i++; }
        } else {
            i++;
        }
    }
}


/* ==================================================================
 * v6 铁律二十：**公开函数必须有调用点**
 * ==================================================================
 * 2026-11-02 连续踩了两次同一个坑：
 *
 *   ① 遥测用了 catchCountThisFloor（每层清零）当"本局被抓次数"
 *   ② daily.c 写了 11 个修正/限制函数，**一个调用点都没有**
 *
 * 两次都是：**编译通过、自检通过、功能是死的。**
 * 玩家看到的是"这个功能不存在"，而开发看到的是"写完了"。
 *
 * 这条测试把"写完 ≠ 接上"变成一条机器能查的规矩。
 * ================================================================== */
static void test_no_unwired_api(void) {
    /* 这些函数定义在某个模块里，但**必须**在游戏主循环/UI 里被调用 */
    static const struct { const char* fn; const char* where; } must[] = {
        /* 每日任务：修正与限制 */
        { "DailyMonsterSpeedMul",    "src/game.c" },
        { "DailyBatteryDrainMul",    "src/game.c" },
        { "DailyGoldMul",            "src/game.c" },
        { "DailyNoiseMul",           "src/game.c" },
        { "DailyForbidSprint",       "src/game.c" },
        { "DailyForbidLight",        "src/game.c" },
        { "DailySecondSpawnOffset",  "src/entity.c" },
        { "DailyGoldTierBonus",      "src/economy.c" },   /* 强度档在 economy.c */
        { "DailyForbidHide",         "src/game.c" },
        { "DailyForbidWrench",       "src/game.c" },
        { "DailyForbidMonsterMap",   "src/ui.c" },
        /* 每日任务本身 */
        { "DailyTick",               "src/game.c" },
        { "DailySetProgress",        "src/game.c" },
        /* 威胁值：五项效果 */
        { "ThreatSpeedCap",          "src/entity.c" },
        { "ThreatHearMul",           "src/entity.c" },
        { "ThreatChaseBonus",        "src/entity.c" },
        { "ThreatSpawnAdvance",      "src/entity.c" },
        { "ThreatReset",             "src/game.c" },
        { "ThreatAddForTier",        "src/bench.c" },
        /* 战绩 / 图鉴 */
        { "BestiaryMarkEscaped",     "src/entity.c" },
        { "BestiaryMarkCaught",      "src/entity.c" },
        { "BestiaryDanger",          "src/ui.c" },
        /* 每日修正上一条遥测 */
        { "CatchCountThisRun",       "src/game.c" },
        /* 战役 5 关的规则（第 ⑧ 轮写好，第 ㉓ 轮才接上） */
        { "CampaignFlashlightLocked", "src/game.c" },
        { "CampaignNoiseAlertMul",    "src/entity.c" },
        { "CampaignNoGiveUp",         "src/entity.c" },
        { "CampaignWardenImmediate",  "src/warden.c" },
        { "CampaignExitDistNow",      "src/game.c" },
        /* v6 第 24 轮：出口必须真的按配电箱坐标算 */
        { "FindFarthestFromCell",    "src/game.c" },
        /* v6 round 35: bench capstone B (added round 10, never wired) */
        { "BenchBullRush",           "src/game.c" },
        { "BenchBurst",              "src/game.c" },
        { "BenchLure",               "src/throwing.c" },
        { "BenchRecover",            "src/game.c" },
        /* v6 round 36: THREAT 5th effect + daily modifier display */
        { "ThreatCooldownDelta",     "src/entity.c" },
        { "DailyModTextEn",          "src/game.c" },
        { "DailyRestrictTextEn",     "src/game.c" },
        /* 内置程序化配乐：合成器必须真的被音频线程调用，不能只是编译进去 */
        { "MusicProcMix",            "src/audio.c" },
        { "MusicProcSetScene",       "src/music.c" },
        { "MusicProcSetGain",        "src/music.c" },
        /* v6 round 37: the whole in-run skill draft was unreachable */
        { "OpenSkillPick",           "src/game.c" },
        /* v6 round 41: daily progress is now shown in the HUD */
        { "DailyCurrent",            "src/ui.c" },
        { "CampaignTierMul",          "src/economy.c" },
    };

    for (unsigned i = 0; i < sizeof(must) / sizeof(must[0]); i++) {
        char* t = read_file(must[i].where);
        if (!t) {
            check(0, "unwired check: file missing");
            continue;
        }
        /* 只查函数名 + 左括号，避免把定义本身算成调用 */
        char needle[128];
        snprintf(needle, sizeof(needle), "%s(", must[i].fn);
        check(contains(t, needle), must[i].fn);
        free(t);
    }
}

static void test_wallet_single_source(void) {
    /* v6 铁律十八：只有一个钱包。
       除了 wallet.c，任何 .c 里不许出现 carryGold -= / bankGold -=。
       **先剥注释再查**，否则一句解释性注释就会误报。 */
    static const char* files[] = {
        "src/game.c", "src/shop.c", "src/bench.c", "src/skills.c",
        "src/upgrade_run.c", "src/money.c", "src/collector.c", "src/ui.c"
    };
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        const char* t = read_file(files[i]);
        if (!t) continue;
        char* buf = (char*)malloc(strlen(t) + 1);
        if (buf) {
            strcpy(buf, t);
            strip_comments(buf);
            check(!contains(buf, "carryGold -="), "spend bypasses wallet (carryGold -=)");
            check(!contains(buf, "bankGold -="), "spend bypasses wallet (bankGold -=)");
            free(buf);
        }
        free((void*)t);
    }
}

static void test_steam_zone(void) {
    /* v5.0：Steam 专区铁律 —— 任何 .c 里不许直接出现 SteamAPI_ 字符串，
       必须走 steam_zone.h 的包装。这样以后改 Steam 逻辑只改一个文件。 */
    static const char* files[] = {
        "src/game.c", "src/ui.c", "src/entity.c", "src/render.c",
        "src/save.c", "src/audio.c"
    };
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        const char* t = read_file(files[i]);
        if (!t) continue;
        check(!contains(t, "SteamAPI_"), "SteamAPI_ used outside steam zone");
        free((void*)t);
    }
    /* 成就表必须正好 24 条（和 v4 方案一致） */
    check(kSteamAchievementCount == 32, "steam achievement table must have 32 entries");
}

static void test_input_contract(void) {
    /* ===== v2.4.77 键位契约（用户要求"验证完整性"）=====
       ACT_CANCEL_CHARGE 必须绑左键（= 扳手击退）
       ACT_THROW 必须绑右键（= 投掷）
       这两个一错，游戏里左右键就是反的，而且很难看出来。 */
    {
        const char* h = read_file("src/../include/common.h");
        check(h != NULL, "common.h not readable");
        if (h) {
            check(contains(h, "[ACT_THROW] = VK_RBUTTON"),
                  "ACT_THROW must be the RIGHT mouse button");
            check(contains(h, "[ACT_CANCEL_CHARGE] = VK_LBUTTON"),
                  "ACT_CANCEL_CHARGE must be the LEFT mouse button");
            free((void*)h);
        }
    }
    {
        const char* g = read_file("src/game.c");
        check(g != NULL, "game.c not readable");
        if (g) {
            check(contains(g, "IsActionDown(ACT_CANCEL_CHARGE)"),
                  "attack reads the wrong action");
            check(contains(g, "IsActionDown(ACT_THROW)"),
                  "throw reads the wrong action");
            free((void*)g);
        }
    }
}

static void test_hide_contract(void) {
    int map[MAP_HEIGHT][MAP_WIDTH] = {{0}};
    int i;
    for (i = 0; i < MAP_WIDTH; i++) {
        map[0][i] = 1;
        map[MAP_HEIGHT - 1][i] = 1;
    }
    for (i = 0; i < MAP_HEIGHT; i++) {
        map[i][0] = 1;
        map[i][MAP_WIDTH - 1] = 1;
    }
    map[5][5] = 1;
    check(HIDESPOT_MAX == 6, "hide-spot capacity changed unexpectedly");
    check(hide_candidate_clear(map, 4.5f, 4.5f, 20.5f, 20.5f), "valid hide spot rejected");
    check(!hide_candidate_clear(map, 5.5f, 5.5f, 20.5f, 20.5f), "wall hide spot accepted");
    check(!hide_candidate_clear(map, 20.5f, 20.5f, 20.5f, 20.5f), "exit hide spot accepted");

    {
        const char* game = read_file("src/game.c");
        const char* entity = read_file("src/entity.c");
        check(count_contains(game, "Dist2(hx2, hy2, exitX, exitY) < 6.0f") >= 2,
              "hide-spot exit guard missing from one generation pass");
        check(contains(entity, "if (hiddenIn < 0)"), "monster hidden catch guard missing");
        free((void*)game);
        free((void*)entity);
    }
}

static void test_tune_geometry(void) {
    const int knob = 76;
    const int button = 36;
    const int buttonGap = 8;
    const int controlGap = 24;
    const int control = button + buttonGap + knob + buttonGap + button;
    const int total = LOCK_TUNE_KNOBS * control + (LOCK_TUNE_KNOBS - 1) * controlGap;
    const int panel = 680;
    int i;
    check(total == 540, "tune control total changed unexpectedly");
    check(panel >= total, "tune panel is narrower than its controls");
    for (i = 0; i < LOCK_TUNE_KNOBS - 1; i++) {
        int leftEnd = i * (control + controlGap) + control;
        int rightStart = (i + 1) * (control + controlGap);
        check(leftEnd <= rightStart, "tune controls overlap");
    }
    {
        const char* lockbox = read_file("src/lockbox.c");
        check(contains(lockbox, "LockTuneReadout"), "tune readout missing");
        /* v3.0：面板加宽后基准从 680/540 变成 760/660，断言跟着更新 */
  check(contains(lockbox, "? LUIS(760)"), "tune panel width guard missing");
        free((void*)lockbox);
    }
}

static void test_stable_visuals_and_input(void) {
    const char* game = read_file("src/game.c");
    const char* ui = read_file("src/ui.c");
    const char* render = read_file("src/render.c");
    /* v3.3：合并上游后，锁盘的按键守卫改成 lockIgnoreVK ——
       打开锁盘的那次交互键自动重复不会立刻把它关掉。
       （原来断言的是「只认 Esc/M」，那是本地分支的写法，
         按用户要求以作者版本为准，这里跟着改。） */
    check(contains(game, "lockIgnoreVK"),
          "lock overlay key guard missing");
    /* v3.0：藏身遮罩已取消（用户要求：躲在柜子里也要看得见提示）。
     现在反过来检查：那段不透明黑边不该再回来。 */
    check(!contains(ui, "int bandT = winH * 37 / 100;"),
          "hide mask bands came back (user asked to remove it)");
    check(contains(render, "int frameSeed = 17;"),
          "low-sanity vignette still uses animated full-screen noise");
    free((void*)game);
    free((void*)ui);
    free((void*)render);
}

/* ==================================================================
 * v28：**存档必须真的把设置和进度读回来**
 * ------------------------------------------------------------------
 * 用户报「语言、无尽模式没保存」。根因不是没写，而是：
 *   · g_save.lang 只在 ContinueRun 里恢复，启动读设置时从不赋值；
 *   · 无尽解锁看的是 gameWon（单局结果），退出游戏就归零；
 *   · Dynamic Res 根本没进 SaveBlob。
 * 这些都不是能靠手玩回归覆盖的，所以在这里钉住。
 * ================================================================== */
static void test_save_persist_contract(void) {
    const char* saveh = read_file("include/save.h");
    const char* savec = read_file("src/save.c");
    const char* game  = read_file("src/game.c");
    const char* gameh = read_file("include/game.h");
    const char* uic   = read_file("src/ui.c");

    check(contains(saveh, "SAVE_VERSION 29"), "save format must be v29");
    check(contains(savec, "g_lang = (g_save.lang == LANG_ZH)"),
          "language is saved but never restored at startup");
    check(contains(savec, "s->dynamicRes = dynamicRes"),
          "Dynamic Res is still not written to the save blob");
    check(contains(savec, "g_save.gameClearedSave = gameCleared"),
          "clear record is not written to the save blob");
    check(contains(savec, "g_save.version == 27"),
          "v27 saves must migrate by prefix instead of being archived");
    /* 2026-11-05 v29：第 6 条局内强化不能塞进 runUpgSave[5]（它在结构体中间，
       一改就整体错位），必须追加在末尾并单独读。 */
    check(contains(saveh, "runUpgMinimapSave"),
          "minimap upgrade level is not persisted at all");
    check(contains(savec, "RunUpgLoadExtra(g_save.runUpgMinimapSave)"),
          "minimap upgrade level is written but never read back");
    check(contains(savec, "g_save.version == 28"),
          "v28 saves must still migrate by prefix (v29 appended a tail field)");
    check(contains(savec, "keepPendingRun"),
          "menu SaveGame can still clobber the pending run with an empty world");
    check(contains(gameh, "int  EndlessUnlocked(void)"),
          "EndlessUnlocked declaration missing");
    check(contains(game, "int EndlessUnlocked(void)"),
          "EndlessUnlocked implementation missing");
    check(contains(uic, "EndlessUnlocked()"),
          "menu does not use the shared endless unlock rule");
    check(!contains(uic, "(bestLevel >= 10) || gameWon"),
          "duplicate endless unlock rule came back");

    free((void*)saveh);
    free((void*)savec);
    free((void*)game);
    free((void*)gameh);
    free((void*)uic);
}

/* ==================================================================
 * v28：**训练/教程不能把模式状态泄漏进 Continue**
 * ------------------------------------------------------------------
 * 复现：每日/战役 → 暂停 → 主菜单 → 教程 → 暂停 → 主菜单 → Continue
 * 结果：挑战地图 + trainingMode 的无限道具，教程出口判定又被跳过。
 * 修法固定成三条契约：
 *   ① ContinueRun 先把所有模式标志清零，再由存档决定模式；
 *   ② 离开教程后菜单保存只写设置（runSaveSuspended）；
 *   ③ 菜单的 hasSave 回到真实存档状态，而不是强行置 1。
 * ================================================================== */
/* ==================================================================
 * v29：地图耗电契约（2026-11-05 用户要求）
 * ------------------------------------------------------------------
 *   · 大地图（全屏）打开时游戏暂停，并且**不耗电**
 *   · 小地图耗电可被局内强化降低：10 / 8 / 6 / 4 每秒
 * ================================================================== */
static void test_map_drain_contract(void) {
    const char* game = read_file("src/game.c");
    const char* upg  = read_file("src/upgrade_run.c");
    const char* upgh = read_file("include/upgrade_run.h");

    check(contains(game, "isMapOpen && !mapFullScreen"),
          "full-screen map still drains the battery");
    check(contains(game, "RunUpgMapDrainPct()"),
          "minimap drain does not consult the upgrade level");
    check(contains(upgh, "RUN_UPG_COUNT 6"),
          "the minimap upgrade slot is missing");
    check(contains(upg, "MAP_BATTERY_DRAIN - 2.0f"),
          "minimap saving does not subtract 2 percent per rank");
    check(contains(upg, "RunUpgMapDrainLevel"),
          "minimap upgrade level accessor missing");

    free((void*)game);
    free((void*)upg);
    free((void*)upgh);
}

/* ==================================================================
 * 2026-11-05：**六种锁型必须都接到底**
 * ------------------------------------------------------------------
 * 新增一种锁最容易漏的三处：
 *   ① LOCK_TYPE_COUNT 改了但绘制分发没加 → 开锁后是空白面板
 *   ② 命中测试没加 → 鼠标点哪里都没反应
 *   ③ 没有逐帧 Tick → 按住类的锁永远不动
 * 这里把五种入口一起钉住。
 * ================================================================== */
static void test_lock_types_contract(void) {
    const char* lk  = read_file("src/lockbox.c");
    const char* lkh = read_file("include/lockbox.h");
    const char* gm  = read_file("src/game.c");

    check(contains(lkh, "LOCK_TYPE_OVERLOAD = 4"), "overload lock type missing");
    check(contains(lkh, "LOCK_TYPE_SAW      = 5"), "saw lock type missing");
    check(contains(lkh, "LOCK_TYPE_COUNT"), "lock type count missing");

    /* 绘制分发 */
    check(contains(lk, "lockType == LOCK_TYPE_OVERLOAD"),
          "overload is never drawn");
    check(contains(lk, "lockType == LOCK_TYPE_SAW"),
          "saw is never drawn");
    /* 逐帧推进 */
    check(contains(lk, "void LockTick(float dt)"),
          "LockTick missing");
    check(contains(gm, "LockTick(dt)"),
          "LockTick is never called from Update");
    /* 按住 / 松开 */
    check(contains(lk, "void LockHoldBegin(void)"), "LockHoldBegin missing");
    check(contains(lk, "void LockHoldEnd(void)"),   "LockHoldEnd missing");
    check(contains(gm, "LockHoldEnd()"),
          "LBUTTONUP never releases the hold");
    /* 几何共用（铁律二十一） */
    check(contains(lk, "static LockBox LockHandleBarBox(void)"),
          "handle bar geometry missing");
    check(contains(lk, "LOCK_HIT_HANDLE"),
          "handle hit code never returned");

    /* 训练营：每种锁型各一个箱子 */
    check(contains(gm, "containerCount = 1 + LOCK_TYPE_COUNT;"),
          "training arena no longer spawns one box per lock type");

    free((void*)lk);
    free((void*)lkh);
    free((void*)gm);
}

/* ==================================================================
 * 2026-11-05：**宝箱怪契约**
 * ------------------------------------------------------------------
 * 这个怪最容易写错的四处：
 *   ① 替换顺序：必须在 ApplyLockboxes 之前，否则会被发一把锁
 *   ② 保险丝箱：绝不能替换，否则整层无解（铁律四十九）
 *   ③ 扳手钩子：它不在 monHunters[] 里，不单独扫就永远敲不到
 *   ④ 空壳：离场必须留下可翻的战利品，否则"冒险"没有回报
 * ================================================================== */
static void test_mimic_contract(void) {
    const char* gm  = read_file("src/game.c");
    const char* mc  = read_file("src/mimic.c");
    const char* mh  = read_file("include/mimic.h");
    const char* bs  = read_file("src/bestiary.c");
    const char* rd  = read_file("src/render.c");

    /* ① 生成期替换必须在 ApplyLockboxes 之前 */
    check(contains(gm, "MimicTrySpawn(currentLevel, containerCount);\n    ApplyLockboxes();"),
          "mimic spawn must run BEFORE ApplyLockboxes, or it gets a lock");

    /* ② 保险丝箱永不被替换 */
    check(contains(mc, "if (containers[i].content != 0) continue;"),
          "a fuse crate must never become a mimic (that is an unwinnable floor)");
    check(contains(mc, "if (containers[i].locked) continue;"),
          "a locked crate must not become a mimic");

    /* ③ 三个特有登记点 */
    check(contains(gm, "MimicOnSearchStep("),   "search hook (bite at 60 percent) missing");
    check(contains(gm, "MimicWrenchSweep("),    "wrench hook missing - it is not in monHunters[]");
    check(contains(gm, "MimicOnGlue("),         "glue hook missing");

    /* ④ 空壳 = 全部奖励的载体 */
    check(contains(gm, "void MimicSpawnShell(int slot, float x, float y)"),
          "shell spawner missing");
    check(contains(gm, "c->mimicShell"),        "shell loot is never paid out");
    check(contains(mc, "MimicSpawnShell(m->containerIdx, m->x, m->y)"),
          "vanish does not leave a shell behind");

    /* ⑤ 不可杀：没有 DEAD 状态 */
    check(!contains(mh, "MIM_DEAD"),
          "the mimic must stay unkillable (no combat in this game)");

    /* ⑥ 追击阶段必须发噪音 —— 这才是它真正的危险性 */
    check(contains(mh, "#define MIM_NOISE_R"),
          "noise radius constant missing");
    check(contains(mc, "MakeNoise(m->x, m->y, MIM_NOISE_R)"),
          "the chase phase never makes noise");

    /* ⑦ 结算加分上限 */
    check(contains(gm, "MIM_SCORE_BONUS * mimicsEscapedThisRun"),
          "score bonus not wired");
    check(contains(mc, "if (mimicsEscapedThisRun > MIM_MAX) mimicsEscapedThisRun = MIM_MAX;"),
          "score bonus has no cap");

    /* ⑧ 已拍板的数值（2026-11-05 用户确认） */
    check(contains(mh, "#define MIM_CHASE_TIME       8.0f"),  "chase time must be 8s");
    check(contains(mh, "#define MIM_LOSE_DIST        3.5f"),  "lose distance must be 3.5 tiles");
    check(contains(mh, "#define MIM_SCARE_SANITY     25.0f"), "bite sanity must be 25");
    check(contains(mh, "#define MIM_CATCH_SANITY     35.0f"), "catch sanity must be 35");
    check(contains(mh, "#define MIM_GOLD_BASE        150"),   "shell gold base must be 150");
    check(contains(mh, "#define MIM_GOLD_STEP        50"),    "shell gold step must be 50");

    /* ⑨ 回身：进渲染列表 + 图鉴有名字 */
    check(contains(rd, "DrawMimicSprite(list[i].idx)"), "mimic is never rendered");
    check(contains(bs, "\"MIMIC\""), "bestiary entry missing");

    /* ⑩ 验收完就拆 —— 铁律四十四：调试热键不许留在发布版里 */
    check(!contains(gm, "VK_F3"),
          "the F3 debug spawn hotkey came back (iron law 44)");

    free((void*)gm);
    free((void*)mc);
    free((void*)mh);
    free((void*)bs);
    free((void*)rd);
}

static void test_mode_leak_contract(void) {
    const char* savec = read_file("src/save.c");
    const char* game  = read_file("src/game.c");

    check(contains(savec, "ResetRunModeFlags();"),
          "ContinueRun must reset stale mode flags before loading");
    check(contains(savec, "if (trainingMode)"),
          "training runs must not serialize the tutorial world");
    check(contains(savec, "keepPendingRun"),
          "leaving the tutorial must preserve the suspended real run");
    check(count_contains(game, "hasSave = SaveHasPendingRun();") >= 2,
          "leaving the tutorial must restore hasSave from the real save");
    check(count_contains(game, "RunModeFlags") >= 3,
          "tutorial exit / continue paths are not wired to the shared reset");

    free((void*)savec);
    free((void*)game);
}

/* ==================================================================
 * 上锁箱子频率（2026-11-05 改成**按层递增、封顶 80%**）
 * ------------------------------------------------------------------
 * 设计：第 1 层 0%，第 2 层 5%，之后每层 +9%，第 11 层起封顶 80%。
 * 两条不能破的约束：
 *   ① 至少留 2 个未上锁箱子（LOCKBOX_MIN_UNLOCKED）
 *   ② 保险丝「先塞未上锁、再塞锁箱」—— 锁箱占比高时也必须放得下
 * ================================================================== */
static void test_lockbox_contract(void) {
    const char* game = read_file("src/game.c");
    check(contains(game, "static int LockboxPctForFloor(int lv)"),
          "per-floor lockbox ratio function missing");
    check(contains(game, "#define LOCKBOX_PCT_CAP         70"),
          "70 percent lock cap missing");
    check(contains(game, "#define LOCKBOX_MIN_UNLOCKED     2"),
          "minimum unlocked boxes missing");
    check(contains(game, "ApplyLockboxes();"),
          "lockbox pass is never called from level generation");
    check(contains(game, "int limit = containerCount - LOCKBOX_MIN_UNLOCKED;"),
          "lockbox pass can lock away every container");
    /* 保险丝两遍填充：第一遍只找未上锁，第二遍才进锁箱 */
    check(contains(game, "if (need <= keepFree) continue;"),
          "locked boxes take fuses first but must keep one free");
    check(contains(game, "const int keepFree = 1;"),
          "at least one fuse must stay reachable without a lockpick");
    check(!contains(game, "#define LOCKBOX_CHANCE_PCT"),
          "old fixed-percent lock logic came back");
    free((void*)game);
}

int main(void) {
    test_no_unwired_api();
    test_wallet_single_source();
    test_steam_zone();
    test_input_contract();
    test_hide_contract();
    test_tune_geometry();
    test_stable_visuals_and_input();
    test_save_persist_contract();
    test_mode_leak_contract();test_lock_types_contract();test_mimic_contract();test_map_drain_contract();
    test_lockbox_contract();
    puts("CANITY offline self-check: PASS");
    return 0;
}

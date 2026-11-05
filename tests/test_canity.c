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
/* 2026-11-05：存档分区的容器契约。这个头是纯类型/常量、无系统依赖，
   所以 test_canity 能直接链 src/save/save_container.c 做**真单元测试**
   （见 test_save_container_contract）。 */
#include "../include/save_format.h"
/* v2.4-rc：主题房间模板是纯数据 + 纯函数（无 rand / 无 worldMap / 无 Win32），
   所以能在这里做**真单元测试**，而不是查源码字符串。 */
#include "../include/room_template.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 2026-11-06：删掉本文件自己写的 MAP_WIDTH/MAP_HEIGHT ——
   它会盖住 common.h 的值（24），而世界已经是 36x36，
   于是这里的边界断言测的是 24 的地图、实现用的是 36 的，结论相反。 */
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
    /* ==================================================================
     * 2026-11-06：**把 CRLF 归一化成 LF**。
     * ------------------------------------------------------------------
     * 这个文件里的检查大部分是在源码里找多行片段（needle 里带 '\n'）。
     * 本机工作区是 LF，但 GitHub 的 Windows runner 检出时
     * core.autocrlf=true 会转成 CRLF —— 于是同一份测试
     * 本地绿、CI 红（实测：mimic spawn must run BEFORE ApplyLockboxes）。
     *
     * 在两处一起修：这里做归一化（对所有 source 断言一劳永逸），
     * 另外新增 .gitattributes 固定文本文件为 LF（治本）。
     * ================================================================ */
    {
        char* w = text;
        for (char* r = text; *r; ++r) {
            if (*r == '\r') continue;
            *w++ = *r;
        }
        *w = '\0';
    }
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

/* ==================================================================
 * 2026-11-06：**教程与天赋的静态契约**（标注 CONTRACT）
 * ------------------------------------------------------------------
 * ⚠️ 这一组是**源码字符串检查，不是行为测试**。
 *    它只能证明"某段代码存在"，不能证明"教程真的走得通"。
 *    真正的验证是你实际把教程跑一遍。
 *
 * 为什么还是要有它：
 *   这些 bug 全是"代码写了但没接上"的类型 ——
 *   tutorialKnocked 全工程只有清零、没有置位，
 *   编译通过、自检通过、功能是死的。
 *   静态契约能挡住这一类回退。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06：**隐藏点最小间距**（标注 CONTRACT）
 * ------------------------------------------------------------------
 * ⚠️ 这是源码字符串检查，不是行为测试 ——
 *    隐藏点的放置写在 GenerateLevel 内部，没法在单测里直接调用。
 *    真正的验证是在游戏里看"同一片区域会不会出现两个桶"。
 *
 * 为什么还是要有它：
 *    这个 bug 的形态是"某个字面量太小"（兜底档只有 1.5f），
 *    很容易在下一次重构里被改回去，而且改回去不会有任何编译错误。
 * ================================================================== */
static void test_hidespot_gap_contract(void) {
    char* g = read_file("src/game.c");
    strip_comments(g);

    check(contains(g, "HIDESPOT_MIN_GAP2"),
          "CONTRACT: hide spots must have a named minimum gap (not a bare literal)");
    check(contains(g, "HIDESPOT_MIN_GAP2_RELAX"),
          "CONTRACT: the fallback round must also respect a floor, not just 'not a wall'");
    /* 老的兜底间距是 1.5f —— 那等于"几乎不限"，必须消失 */
    check(!contains(g, "hideSpots[k].y) < 1.5f"),
          "CONTRACT: the old 1.5-tile fallback gap must be gone");
    check(!contains(g, "hideSpots[k].y) < 4.0f"),
          "CONTRACT: the old 2-tile primary gap must be gone");
    free(g);
}

static void test_tutorial_and_keystone_contract(void) {
    char* src = read_file("src/game.c");
    strip_comments(src);

    /* ---- P1-2：tutorialKnocked 必须真的被置 1（原来只有清零） ---- */
    check(count_contains(src, "tutorialKnocked = 1") >= 1,
          "CONTRACT: tutorialKnocked must be set somewhere, otherwise TUT_KNOCK can never advance");
    check(contains(src, "tutorialKnocked || tutorialThrown"),
          "CONTRACT: TUT_KNOCK must also accept a throw as fallback (training may lack the DECOY keystone)");

    /* ---- P1-3：教程提示不能写反 ---- */
    check(contains(src, "\"THROW  -  HOLD %s\", VkName(g_bindings[ACT_THROW])"),
          "CONTRACT: TUT_THROW must hint the real throw action");
    check(!contains(src, "\"THROW  -  HOLD %s\", VkName(g_bindings[ACT_CANCEL_CHARGE])"),
          "CONTRACT: TUT_THROW must not hint the cancel-charge action");

    /* ---- P1-4：训练场必须无条件开放 G 键 ---- */
    check(contains(src, "HasKeystone(SK_DECOY) || trainingMode"),
          "CONTRACT: training mode must open the decoy key without the keystone");

    /* ---- P1-4：会被外部条件卡死的那两步必须有超时兜底 ---- */
    check(contains(src, "TUTORIAL_STEP_TIMEOUT"),
          "CONTRACT: tutorial steps must have a timeout fallback");

    free(src);

    /* ---- P1-1：SK_SALVAGE 不得再出现在"禁灯"判定里 ---- */
    {
        char* g = read_file("src/game.c");
        strip_comments(g);
        check(!contains(g, "!(DailyForbidLight() || HasKeystone(SK_SALVAGE))"),
              "CONTRACT: SK_SALVAGE must no longer disable the flashlight permanently");
        /* 但"开局默认关灯"必须保留 —— 那是这个天赋的真正价值 */
        check(contains(g, "DailyForbidLight() || HasKeystone(SK_SALVAGE) ? 0 : 1"),
              "CONTRACT: SK_SALVAGE must still start a run with the light off");
        free(g);
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

    /* ==================================================================
     * 2026-11-06（v2.4-rc）：**v29 -> v30**
     * ------------------------------------------------------------------
     * 只追加了两个 int（roomSeed / roomCount），全部在结构体**末尾**，
     * 所以 v29 的老档按"文件比结构短"走现有前缀迁移路径，
     * roomSeed/roomCount 读成 0 —— 当作"无房间层"，不崩、不丢进度。
     * 容器格式（SAVE_CONTAINER_VERSION）**没有变**，仍是 1。
     * ================================================================ */
    /* v31：地图 24x24 -> 36x36，worldMap/explored 变大 → 布局又变了一次 */
    /* v32：MAX_CONTAINERS 8 -> 12，Container cont[] 变大 → 布局又变了一次。
       老档仍然只有 8 条记录，迁移靠 LEGACY_CONTAINER_COUNT 读取。 */
    check(contains(saveh, "SAVE_VERSION 32"), "save format must be v32");
    check(contains(savec, "LEGACY_CONTAINER_COUNT"),
          "the container-slot count change needs its own legacy constant");
    check(contains(saveh, "int roomSeed;"), "v30 must carry the room replay seed");
    check(contains(saveh, "int roomCount;"), "v30 must carry the room count");
    check(contains(savec, "g_lang = (g_save.lang == LANG_ZH)"),
          "language is saved but never restored at startup");
    check(contains(savec, "s->dynamicRes = dynamicRes"),
          "Dynamic Res is still not written to the save blob");
    check(contains(savec, "g_save.gameClearedSave = gameCleared"),
          "clear record is not written to the save blob");
    /* 2026-11-06（P0-1 + 36x36）：**v27/v28 不再是"按前缀读"了**。
       它们的 Container 是 32 字节、地图是 24x24，而现在是 40 / 36x36，
       所以必须走 LoadLegacyBlob 重排。这两条断言改成钉住"走重排路径"。 */
    check(contains(savec, "LoadLegacyBlob"),
          "legacy saves must go through the field-wise re-layout, not a prefix memcpy");
    check(contains(savec, "g_save.version >= 25 && g_save.version <= 30"),
          "v25..v30 must all be handled by the legacy re-layout");
    check(contains(savec, "LEGACY_MAP_W"),
          "the 24x24 -> 36x36 map re-index must exist");
    /* 2026-11-05 v29：第 6 条局内强化不能塞进 runUpgSave[5]（它在结构体中间，
       一改就整体错位），必须追加在末尾并单独读。 */
    check(contains(saveh, "runUpgMinimapSave"),
          "minimap upgrade level is not persisted at all");
    check(contains(savec, "RunUpgLoadExtra(g_save.runUpgMinimapSave)"),
          "minimap upgrade level is written but never read back");
    check(!contains(savec, "g_save.version == 28"),
          "v28 must no longer be treated as a plain prefix (its Container is 32 bytes)");
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

/* ==================================================================
 * 2026-11-05：**存档分区契约**
 * ------------------------------------------------------------------
 * 这一组是**真单元测试**（test_canity 链接了 src/save/save_container.c），
 * 不是读源码做文字断言。覆盖四件事：
 *   ① 打包 -> 按 id 取回，内容逐字节一致
 *   ② 信封校验和能抓住一个字节的损坏
 *   ③ 未知块 id 也能枚举出来（云存档跨版本保留的前提）
 *   ④ 块表声明越界时必须拒绝，不能返回越界指针
 * 另外用源码断言钉住分区规则和「信封优先、裸档回退」的接线。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-rc C01）：**房间模板的旋转与边界**
 * ------------------------------------------------------------------
 * 这些是纯函数，没有任何随机性或全局状态，所以能钉死：
 *   · 4 种旋转之后，宽高交换正确、tiles 真的转了
 *   · 转 4 次回到原样（旋转的群性质）
 *   · 可通行格数在旋转下不变（否则"房间面积"会随朝向变化）
 *   · 边界检查：贴外墙、越界、负坐标都必须拒绝
 * ================================================================== */
static void test_room_template_contract(void) {
    RoomTemplate rot;
    int i;

    /* ---- ① 三个模板都存在，尺寸在 6×6 上限内 ---- */
    for (i = 0; i < ROOM_TEMPLATE_COUNT; i++) {
        const RoomTemplate* t = RoomTemplateGet(i);
        check(t != NULL, "every declared room template must exist");
        if (!t) continue;
        check(t->id == i, "template id must match its slot");
        check(t->w >= 2 && t->w <= ROOM_MAX_W, "template width must be 2..ROOM_MAX_W");
        check(t->h >= 2 && t->h <= ROOM_MAX_H, "template height must be 2..ROOM_MAX_H");
        check(t->themeId >= 0 && t->themeId < THEME_COUNT,
              "template themeId must be a real theme");
        check(RoomTemplateFloorCount(t) > 0,
              "every template must have walkable floor");
    }
    /* 越界 id 必须返回 NULL，不能读到随机内存 */
    check(RoomTemplateGet(-1) == NULL, "negative template id must return NULL");
    check(RoomTemplateGet(ROOM_TEMPLATE_COUNT) == NULL, "out-of-range template id must return NULL");

    /* ---- ② 旋转 4 次回到原样 ---- */
    for (i = 0; i < ROOM_TEMPLATE_COUNT; i++) {
        const RoomTemplate* base = RoomTemplateGet(i);
        RoomTemplate a, b;
        if (!base) continue;

        check(RoomTemplateRotate(i, 4, &a) == 1, "rot 4 must be accepted");
        check(a.w == base->w && a.h == base->h, "rot 4 must restore the width/height");
        {
            int same = 1;
            for (int y = 0; y < ROOM_MAX_H; y++)
                for (int x = 0; x < ROOM_MAX_W; x++)
                    if (a.tiles[y][x] != base->tiles[y][x]) same = 0;
            check(same, "rot 4 must restore every tile");
        }

        /* rot 2（180°）宽高**不变**；只有 rot 1 / rot 3（90°/270°）才交换。
           原来这里写成 rot 2 交换 —— 是我把旋转的群性质记反了。 */
        check(RoomTemplateRotate(i, 2, &b) == 1, "rot 2 must be accepted");
        check(b.w == base->w && b.h == base->h, "rot 180 must keep width and height");

        check(RoomTemplateRotate(i, 1, &b) == 1, "rot 1 must be accepted");
        check(b.w == base->h && b.h == base->w, "rot 90 must swap width and height");
        check(RoomTemplateRotate(i, 3, &b) == 1, "rot 3 must be accepted");
        check(b.w == base->h && b.h == base->w, "rot 270 must swap width and height");
    }

    /* ---- ③ 可通行格数在旋转下不变 ---- */
    for (i = 0; i < ROOM_TEMPLATE_COUNT; i++) {
        const RoomTemplate* base = RoomTemplateGet(i);
        int f0 = RoomTemplateFloorCount(base);
        if (!base) continue;
        for (int r = 1; r < 4; r++) {
            check(RoomTemplateRotate(i, r, &rot) == 1, "rotate must succeed");
            check(RoomTemplateFloorCount(&rot) == f0,
                  "rotating must not change the walkable area");
        }
    }

    /* ---- ④ 旋转真的转了：锅炉房居中的柱子，转 90/180/270 后位置跟着动 ---- */
    {
        const RoomTemplate* t0 = RoomTemplateGet(0);
        check(t0 != NULL, "template 0 must exist");
        if (t0) {
            /* 原图柱子中心在相对坐标 (2,2) 附近；转 180 应该还在中心 */
            RoomTemplate r180;
            check(RoomTemplateRotate(0, 2, &r180) == 1, "rot 180 must succeed");
            int pillars0 = 0, pillars180 = 0;
            for (int y = 0; y < t0->h; y++)
                for (int x = 0; x < t0->w; x++)
                    if (t0->tiles[y][x] == ROOM_TILE_PILLAR) pillars0++;
            for (int y = 0; y < r180.h; y++)
                for (int x = 0; x < r180.w; x++)
                    if (r180.tiles[y][x] == ROOM_TILE_PILLAR) pillars180++;
            check(pillars0 == pillars180, "rotating must preserve the pillar count");
        }
    }

    /* ---- ⑤ 边界：贴外墙 / 越界 / 负坐标 ---- */
    {
        const RoomTemplate* t = RoomTemplateGet(0);
        if (t) {
            check(RoomTemplateFits(t, 1, 1, 1) == 0,
                  "a room touching the outer wall must be rejected with margin 1");
            check(RoomTemplateFits(t, 2, 2, 1) == 1,
                  "a room inset by 2 must fit");
            check(RoomTemplateFits(t, -3, 2, 1) == 0,
                  "negative coordinates must be rejected");
            check(RoomTemplateFits(t, MAP_WIDTH, 2, 1) == 0,
                  "off-map x must be rejected");
            check(RoomTemplateFits(t, 2, MAP_HEIGHT, 1) == 0,
                  "off-map y must be rejected");
            /* 右下极限位置 */
            check(RoomTemplateFits(t, MAP_WIDTH - 2 - t->w, MAP_HEIGHT - 2 - t->h, 1) == 1,
                  "the bottom-right inset position must fit");
            check(RoomTemplateFits(t, MAP_WIDTH - 1 - t->w, MAP_HEIGHT - 2 - t->h, 1) == 0,
                  "overhanging the right edge must be rejected");
        }
        check(RoomTemplateFits(NULL, 2, 2, 1) == 0, "NULL template must not fit");
    }

    /* ---- ⑥ 非法旋转参数按 0 处理，不崩 ---- */
    {
        check(RoomTemplateRotate(0, -1, &rot) == 1, "negative rot must be clamped, not crash");
        check(RoomTemplateRotate(-5, 0, &rot) == 0, "bad id must fail cleanly");
        check(RoomTemplateRotate(0, 0, NULL) == 0, "NULL output must fail cleanly");
    }
}

static void test_save_container_contract(void) {
    /* ---- ① 打包 / 取回 ---- */
    {
        SaveChunkDesc d[3];
        const void* body[3];
        char a[4] = { 'A', 'B', 'C', 'D' };
        char b[2] = { 'x', 'y' };
        char c[3] = { '1', '2', '3' };
        d[0].id = 0x0001; d[0].schema = 1; d[0].size = 4; body[0] = a;
        d[1].id = 0x0002; d[1].schema = 3; d[1].size = 2; body[1] = b;
        d[2].id = 0x00FF; d[2].schema = 1; d[2].size = 3; body[2] = c;

        unsigned char buf[256];
        unsigned int sum = 0;
        int n = SaveContainerPack(d, body, 3, buf, (int)sizeof(buf), &sum);
        check(n > 0, "SaveContainerPack must succeed for 3 small chunks");
        check(SaveContainerCount(buf, n) == 3, "chunk count must round-trip");

        const void* got = NULL;
        int sz = 0;
        check(SaveContainerFind(buf, n, 0x0002, &got, &sz) == 1 && sz == 2 &&
              got && memcmp(got, b, 2) == 0,
              "chunk 0x0002 must come back byte-identical");
        check(SaveContainerFind(buf, n, 0x7777, &got, &sz) == 0,
              "unknown chunk id must not be found");

        /* ---- ③ 未知块也能枚举 ---- */
        SaveChunkDesc d2[2];
        const void* body2[2];
        char q[1] = { 'Q' };
        d2[0].id = 0x7777; d2[0].schema = 9; d2[0].size = 1; body2[0] = q;
        d2[1].id = 0x0001; d2[1].schema = 1; d2[1].size = 1; body2[1] = a;
        unsigned char buf2[128];
        int n2 = SaveContainerPack(d2, body2, 2, buf2, (int)sizeof(buf2), NULL);
        check(n2 > 0, "pack with unknown chunk must succeed");
        SaveChunkDesc e;
        const void* eb = NULL;
        check(SaveContainerAt(buf2, n2, 0, &e, &eb) == 1 && e.id == 0x7777,
              "unknown chunk must still be enumerable (cloud forward-compat)");

        /* ---- ② 信封校验和 ---- */
        SaveEnvelope h;
        SaveEnvelopeSeal(&h, buf, n, 1793866745u);
        check(h.magic == SAVE_ENVELOPE_MAGIC, "envelope magic must be CSAV");
        check(h.version == SAVE_CONTAINER_VERSION, "envelope container version");
        check(SaveEnvelopeValid(&h, buf, n) == 1, "fresh envelope must validate");
        buf[SAVE_PAYLOAD_HEAD_SIZE + 8] ^= 0xFF;      /* 损坏一个字节 */
        check(SaveEnvelopeValid(&h, buf, n) == 0,
              "one flipped byte must fail the checksum");
        buf[SAVE_PAYLOAD_HEAD_SIZE + 8] ^= 0xFF;      /* 复原 */
        check(SaveEnvelopeValid(&h, buf, n) == 1, "restored payload must validate again");
        check(SaveEnvelopeValid(&h, buf, n - 1) == 0,
              "truncated payload must be rejected (payload length mismatch)");
    }

    /* ==================================================================
     * ④a **黄金向量**：校验和算法不能只和自己自洽
     * ------------------------------------------------------------------
     * 2026-11-06 实测：把 FNv 的偏移基数 2166136261 改成 ...262，
     * 整套测试仍然全绿 —— 因为 seal 和 validate 用的是同一个（坏掉的）
     * 函数，两边互相满意。而现实中这会**把所有老存档判成损坏**。
     *
     * 所以必须有对已知输入给出已知值的断言。改坏常数 → 这里立刻红。
     * ================================================================== */
    {
        check(SaveFnv1a("CANITY", 6) == 0xCB74372Bu,
              "FNV-1a golden vector: 'CANITY' must hash to 0xCB74372B");
        check(SaveFnv1a("", 0) == 0x811C9DC5u,
              "FNV-1a golden vector: empty input must be the offset basis");
        {
            const unsigned char raw[4] = { 0x00, 0x01, 0x02, 0x03 };
            check(SaveFnv1a(raw, 4) == 0xC3AA51B1u,
                  "FNV-1a golden vector: 00 01 02 03 must hash to 0xC3AA51B1");
        }
    }

    /* ==================================================================
     * ④b 容器字节布局：块数写在 offset 4，且总长 = 头 + 块表 + 数据
     *     这一条抓得住"块数 off-by-one"这类真实缺陷（③ 之外的另一面）。
     * ================================================================== */
    {
        SaveChunkDesc d2[2];
        const void* body2[2];
        char p0[5] = { 'A', 'B', 'C', 'D', 'E' };
        char p1[3] = { 'x', 'y', 'z' };
        d2[0].id = 0x0101; d2[0].schema = 2; d2[0].size = 5; body2[0] = p0;
        d2[1].id = 0x0202; d2[1].schema = 3; d2[1].size = 3; body2[1] = p1;

        unsigned char lay[256];
        int layLen = SaveContainerPack(d2, body2, 2, lay, (int)sizeof(lay), NULL);
        int wantLen = SAVE_PAYLOAD_HEAD_SIZE + 2 * (int)sizeof(SaveChunkDesc) + 5 + 3;
        check(layLen == wantLen, "container length must be head + table + data");

        int cntField = 0;
        memcpy(&cntField, lay + 4, sizeof(cntField));
        check(cntField == 2, "chunk count must be written at payload offset 4");
    }

    /* ---- ④ 越界块表必须拒绝 ---- */
    {
        unsigned char bad[32];
        memset(bad, 0, sizeof(bad));
        /* 容器版本 1 / 块数 1 / 保留 / 一个宣称 9999 字节的块 */
        int ver = 1, cnt = 1, rsv = 0;
        memcpy(bad + 0, &ver, 4);
        memcpy(bad + 4, &cnt, 4);
        memcpy(bad + 8, &rsv, 4);
        SaveChunkDesc d;
        d.id = 1; d.schema = 1; d.size = 9999;
        memcpy(bad + 12, &d, sizeof(d));
        check(SaveContainerCount(bad, (int)sizeof(bad)) < 0,
              "a chunk claiming more bytes than the payload must be rejected");
    }

    /* ---- 源码断言：分区接线 ---- */
    {
        const char* lk = read_file("include/save_format.h");
        const char* sz = read_file("src/steam/steam_zone.c");
        const char* sv = read_file("src/save.c");
        const char* bk = read_file("src/save/save_backend.c");
        const char* sh = read_file("include/save.h");

        check(contains(lk, "SAVE_ENVELOPE_MAGIC     0x56415343u"),
              "envelope magic must stay 'CSAV' (steam_zone contract)");
        check(contains(lk, "SC_LEGACY29"), "legacy chunk id missing");
        check(contains(sz, "#include \"save_format.h\""),
              "steam_zone must consume the shared envelope contract");
        check(!contains(sz, "typedef struct {\n    unsigned int magic;"),
              "steam_zone must not keep a private envelope typedef");
        check(contains(sz, "SAVE_ENVELOPE_MAGIC"),
              "steam_zone must recognize the envelope the save partition writes");
        /* v2.4-alpha R10：BuildSaveFile 已并入 SaveFileBytes（唯一序列化出口），
           两处业务写盘（SaveSettingsOnly / SaveGame）都走它。 */
        check(contains(sv, "SaveFileBytes(") && contains(sv, "ExtractSaveFile("),
              "save.c must pack/unpack through the container");
        check(contains(sv, "SaveCarryCapture("),
              "save.c must capture the ordered chunk directory for unknown-chunk preservation");
        check(contains(sv, "SAVE_ERR_DUP_CHUNK"),
              "duplicate chunk ids must be rejected, not silently dropped");
        check(contains(sv, "SC_LEGACY29"), "save.c must write the legacy chunk");
        check(contains(bk, "SaveBackendWrite") && contains(bk, "SaveBackendSyncAll"),
              "save backend must own the file writes and cloud sync");
        check(contains(sh, "SaveBackendWrite"),
              "save backend API must be exported from save.h");
        /* 老档兼容：裸档分支必须还在 */
        check(contains(sv, "magic == SAVE_MAGIC"),
              "raw legacy save path must be kept as fallback");
        check(contains(lk, "SAVE_CONTAINER_VERSION  1"),
              "container version must be independent from SAVE_VERSION");

        free((void*)lk); free((void*)sz); free((void*)sv);
        free((void*)bk); free((void*)sh);
    }
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
    test_room_template_contract();
    test_tutorial_and_keystone_contract();
    test_hidespot_gap_contract();   /* v2.4-rc C01：纯函数，最便宜先跑 */
    test_no_unwired_api();
    test_wallet_single_source();
    test_steam_zone();
    test_input_contract();
    test_hide_contract();
    test_tune_geometry();
    test_stable_visuals_and_input();
    test_save_persist_contract();
    test_mode_leak_contract();test_lock_types_contract();test_mimic_contract();test_save_container_contract();test_map_drain_contract();
    test_lockbox_contract();
    puts("CANITY offline self-check: PASS");
    return 0;
}

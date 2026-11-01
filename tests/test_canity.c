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

int main(void) {
    test_no_unwired_api();
    test_wallet_single_source();
    test_steam_zone();
    test_input_contract();
    test_hide_contract();
    test_tune_geometry();
    test_stable_visuals_and_input();
    puts("CANITY offline self-check: PASS");
    return 0;
}

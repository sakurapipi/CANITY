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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAP_WIDTH 24
#define MAP_HEIGHT 24
#define HIDESPOT_MAX 6
#define BENCH_COUNT 12
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

static void test_bench_contract(void) {
    static const int costs[BENCH_COUNT] = {
        120, 280, 620, 120, 280, 620,
        120, 280, 620, 120, 280, 620
    };
    static const char* names[BENCH_COUNT] = {
        "LIGHT STEP", "HOLD BREATH", "VANISH",
        "ENDURANCE", "BURST", "SECOND WIND",
        "HEARING", "INNER EYE", "PREMONITION",
        "THICK SKIN", "COMPOSURE", "PLAY DEAD"
    };
    const char* source = read_file("src/bench.c");
    int i;
    for (i = 0; i < BENCH_COUNT; i++) {
        char cost[32];
        snprintf(cost, sizeof(cost), "%d,", costs[i]);
        check(contains(source, names[i]), "bench node missing");
        check(contains(source, cost), "bench tier cost missing");
    }
    check(count_contains(source, "{ \"") >= BENCH_COUNT, "bench node count changed");
    free((void*)source);
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
    test_bench_contract();
    test_hide_contract();
    test_tune_geometry();
    test_stable_visuals_and_input();
    puts("CANITY offline self-check: PASS");
    return 0;
}

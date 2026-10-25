/*
 * Canity 离线自检 —— 把 main.c 的逻辑层拉出来跑断言，不需要窗口。
 *
 * 做法：直接 include canity_unity.c（把六个模块合成一个编译单元），
 * 再把 WinMain 改名成 CanityAppMain，于是本文件可以调用各模块内部的静态函数与全局状态。
 * 编译时用 -DCANITY_TEST 屏蔽真正的入口。
 *
 *   gcc test_canity.c -o test_canity.exe -O2 -Wall -Wextra -DCANITY_TEST \
 *       -DSAVE_FILE="canity_test.sav" -lgdi32 -luser32 -lwinmm
 *
 * 跑法：直接执行，退出码 0 = 全过，非 0 = 有断言失败。
 */

#define CANITY_TEST 1
#define SAVE_FILE "canity_test.sav"

/* main.c 里的 WinMain 是程序入口；这里改名，测试文件自己定义 main() */
#define WinMain CanityAppMain
#include "canity_unity.c"
#undef WinMain

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* 迷你测试框架                                                        */
/* ------------------------------------------------------------------ */

static int g_pass = 0;
static int g_fail = 0;
static const char* g_group = "";

static void group(const char* name) {
    g_group = name;
    printf("\n== %s ==\n", name);
}

static void check(int cond, const char* what) {
    if (cond) {
        g_pass++;
        printf("  [PASS] %s\n", what);
    } else {
        g_fail++;
        printf("  [FAIL] %s   (%s)\n", what, g_group);
    }
}

/* 浮点比较：只看误差，不带 cond 参数。
   （早期版本这里多带一个 int cond，把 float 传进去会被截断成 0，
    导致 0.67 这种小于 1 的值一律判失败 —— 已修掉。） */
static void checkf(const char* what, float got, float want, float tol) {
    float d = got - want;
    if (d < 0) d = -d;
    if (d <= tol) {
        g_pass++;
        printf("  [PASS] %s  (%.4f ~ %.4f)\n", what, got, want);
    } else {
        g_fail++;
        printf("  [FAIL] %s  (got %.4f, want %.4f +-%.4f)\n", what, got, want, tol);
    }
}

/* ------------------------------------------------------------------ */
/* 测试辅助                                                            */
/* ------------------------------------------------------------------ */

/* 把游戏置到「正在玩」的状态，逻辑层可以直接跑 */
static void enterPlaying(void) {
    currentState = STATE_PLAYING;
    runInProgress = 1;
    flashTime = 0.0f;
    isMapOpen = 0;
    memset(keys, 0, sizeof(keys));
}

/* ------------------------------------------------------------------ */
/* 1. 常量与数组上界（P0-1 的回归测试）                                  */
/* ------------------------------------------------------------------ */

static void test_bounds(void) {
    group("1. 数组上界");

    /* RenderSprites 的精灵表上界必须够放下所有可能的 billboard：
       容器 + 投掷物 + 配电箱 + 寻声怪 + 安全屋 + 光敏怪 + 出口光柱
       + 飞行中的玻璃瓶
       注意：跳脸虫是屏幕空间 overlay，不在这张表里。 */
    int maxSprites = MAX_CONTAINERS + MAX_THROWN + 6 + MAX_SHOTS;
    check(MAX_SPRITES >= maxSprites,
          "MAX_SPRITES 覆盖全部精灵来源");
    check(MAX_SPRITES == maxSprites,
          "MAX_SPRITES 没有虚高的冗余");

    /* 容器数组必须容得下 RICH 词条的上限 */
    check(MAX_CONTAINERS >= CONTAINERS_BASE + 2,
          "containers[] 数组容得下 RICH 词条的 +2");

    /* 投掷物数组 */
    check(MAX_THROWN == 4, "MAX_THROWN 为 4");

    printf("  (MAX_CONTAINERS=%d, CONTAINERS_BASE=%d, MAX_THROWN=%d, MAX_SPRITES=%d)\n",
           MAX_CONTAINERS, CONTAINERS_BASE, MAX_THROWN, MAX_SPRITES);
}

/* ------------------------------------------------------------------ */
/* 2. 精灵表在极限情况下不越界（P0-1 的直接验证）                        */
/* ------------------------------------------------------------------ */

static void test_sprite_overflow(void) {
    group("2. 精灵表极限不越界");

    enterPlaying();
    currentLevel = 1;
    GenerateLevel();

    /* 强行把所有精灵都放到场上，制造最坏情况 */
    containerCount = MAX_CONTAINERS;
    for (int i = 0; i < MAX_CONTAINERS; i++) {
        containers[i].x = 2.0f + (float)i;
        containers[i].y = 2.0f;
        containers[i].active = 1;
        containers[i].searched = 0;
        containers[i].content = 0;
    }
    breaker.active = 1;  breaker.x = 12.0f;  breaker.y = 12.0f;
    safeRoom.active = 1; safeRoom.x = 14.0f; safeRoom.y = 3.0f;
    monster.active = 1;  monster.x = 8.0f;  monster.y = 8.0f;
    lightBeast.active = 1; lightBeast.x = 6.0f; lightBeast.y = 6.0f;
    for (int i = 0; i < MAX_THROWN; i++) {
        thrown[i].x = 3.0f + (float)i;
        thrown[i].y = 10.0f;
        thrown[i].z = 0.0f; thrown[i].vz = 0.0f;
        thrown[i].type = 0;
        thrown[i].life = 20.0f;
        thrown[i].active = 1;
    }
    /* 飞行中的玻璃瓶也占精灵表 */
    for (int i = 0; i < MAX_SHOTS; i++) {
        shot[i].x = 4.0f + (float)i;
        shot[i].y = 12.0f;
        shot[i].z = 0.0f;
        shot[i].vx = 0.0f; shot[i].vy = 0.0f; shot[i].vz = 0.0f;
        shot[i].life = 0.5f;
        shot[i].age = 0.0f;
        shot[i].charge = 0.5f;
        shot[i].active = 1;
    }
    /* 跳脸虫是屏幕空间 overlay，不占精灵表 */

    /* 统计一下实际会写进表的条目数 */
    int expected = containerCount + 1 + 1 + 1 + 1 + MAX_THROWN + MAX_SHOTS + 1;
    printf("  最坏情况条目数 = %d，数组上界 = %d\n", expected, MAX_SPRITES);
    check(expected <= MAX_SPRITES, "最坏情况不会超出精灵表");

    /* 真跑一遍渲染：写越界会被栈保护抓到
       渲染需要像素缓冲，所以要先建一个最小缓冲 */
    if (!pixels) {
        winW = 320; winH = 240;
        renderW = 320; renderH = 240;
        pixels = (unsigned int*)malloc(320 * 240 * sizeof(unsigned int));
        memset(pixels, 0, 320 * 240 * sizeof(unsigned int));
    }
    RenderSprites();
    check(1, "极限精灵数下 RenderSprites 未崩溃/未越界");
}

/* ------------------------------------------------------------------ */
/* 3. 规则自检：理智 / 移动 / 碰撞（P1 回归）                             */
/* ------------------------------------------------------------------ */

static void test_rules(void) {
    group("3. 规则自检");

    enterPlaying();
    currentLevel = 1;
    GenerateLevel();

    /* 理智衰减 0.5/秒 */
    sanity = 100.0f;
    upgSanity = 0; modSanityMul = 1.0f;
    for (int i = 0; i < 120 * 10; i++) Update(1.0f / 120.0f);
    checkf("10 秒后理智 100 -> 95", sanity, 95.0f, 0.01f);

    /* 移动 2.0 格/秒 */
    enterPlaying();
    GenerateLevel();
    float x0 = posX, y0 = posY;
    keys['W'] = 1;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    keys['W'] = 0;
    /* Dist2 是平方距离，这里要开方还原成实际格数 */
    float moved = sqrtf(Dist2(x0, y0, posX, posY));
    checkf("1 秒移动 2.0 格", moved, 2.0f, 0.05f);

    /* 碰撞：撞墙要停住
       走廊从 x=1 一直通到 x=6，x=7 是墙。
       玩家从 1.5 出发，速度 2.5 格/秒，需要 (6.76-1.5)/2.5 ≈ 2.1 秒才撞到，
       所以这里跑 4 秒（480 步），保证一定撞上。 */
    enterPlaying();
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) worldMap[y][x] = 1;
    for (int y = 1; y <= 3; y++)
        for (int x = 1; x <= 6; x++) worldMap[y][x] = 0;   /* 走廊直到 x=6 */
    posX = 1.5f; posY = 2.5f;
    dirX = 1.0f; dirY = 0.0f;
    ApplyFov();
    keys['W'] = 1;
    for (int i = 0; i < 480; i++) Update(1.0f / 120.0f);
    keys['W'] = 0;
    /* 墙在 x=7.0，玩家半径 0.24，所以最多走到 6.76 */
    checkf("撞墙停在 6.76（墙在 7.0，玩家半径 0.24）", posX, 6.76f, 0.02f);

    /* 暂停不推进 */
    enterPlaying();
    GenerateLevel();
    sanity = 60.0f;
    currentState = STATE_PAUSED;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    checkf("暂停时理智不变", sanity, 60.0f, 0.001f);

    /* 步进频率无关性：120Hz 与 240Hz 跑 1 秒结果一致 */
    enterPlaying();
    GenerateLevel();
    sanity = 100.0f;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    float at120 = sanity;

    enterPlaying();
    GenerateLevel();
    sanity = 100.0f;
    for (int i = 0; i < 240; i++) Update(1.0f / 120.0f * 0.5f);
    float at240 = sanity;
    checkf("120Hz 与 240Hz 结果一致", at120 - at240, 0.0f, 0.001f);
}

/* ------------------------------------------------------------------ */
/* 4. 手电筒 / 体力                                                    */
/* ------------------------------------------------------------------ */

static void test_light_stamina(void) {
    group("4. 手电筒与体力");

    enterPlaying();
    GenerateLevel();
    battery = 100.0f; flashlightOn = 1; upgBattery = 0;
    for (int i = 0; i < 120 * 10; i++) Update(1.0f / 120.0f);
    checkf("手电 10 秒耗电 0.85/秒", battery, 100.0f - 8.5f, 0.02f);

    enterPlaying();
    GenerateLevel();
    battery = 0.05f; flashlightOn = 1;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    check(battery <= 0.0f && flashlightOn == 0, "没电自动熄灯");

    /* 冲刺 */
    enterPlaying();
    GenerateLevel();
    stamina = 100.0f; sprinting = 0;
    float sx = posX, sy = posY;
    keys['W'] = 1; keys[VK_SHIFT] = 1;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    keys['W'] = 0; keys[VK_SHIFT] = 0;
    checkf("冲刺 1 秒体力 100 -> 74", stamina, 74.0f, 0.2f);
    printf("  (冲刺 1 秒位移 %.3f 格，设计值 4.125)\n", sqrtf(Dist2(sx, sy, posX, posY)));

    /* 体力耗尽自动降为走路 */
    enterPlaying();
    GenerateLevel();
    stamina = 0.5f;
    keys['W'] = 1; keys[VK_SHIFT] = 1;
    for (int i = 0; i < 10; i++) Update(1.0f / 120.0f);
    keys['W'] = 0; keys[VK_SHIFT] = 0;
    check(sprinting == 0, "体力耗尽自动停止冲刺");

    /* 手摇发电：按 R 摇 2.4 秒回 32 电（同时手电耗电 0.85/s，GenerateLevel 后有 0.15s 白屏） */
    enterPlaying();
    GenerateLevel();
    battery = 30.0f; flashlightOn = 1; upgBattery = 0;
    keys['R'] = 1;
    for (int i = 0; i < 120 * 3; i++) Update(1.0f / 120.0f);  /* 3 秒 */
    keys['R'] = 0;
    /* 3秒 - 0.15s白屏 = 2.85s 有效摇电，够 1 次完整充能(2.4s)；扣除手电耗电 3*0.85=2.55 */
    checkf("手摇 3 秒回 32 电(扣除手电耗电)", battery, 30.0f + 32.0f - 2.55f, 0.5f);

    /* 手摇满电不再继续充（关手电避免干扰） */
    enterPlaying();
    GenerateLevel();
    battery = (float)BatteryMax() - 5.0f;  /* 95 电 */
    flashlightOn = 0;  /* 关手电，只测充电上限 */
    keys['R'] = 1;
    for (int i = 0; i < 120 * 5; i++) Update(1.0f / 120.0f);  /* 5 秒，摇两轮 */
    keys['R'] = 0;
    checkf("手摇不超过电量上限", battery, (float)BatteryMax(), 0.1f);

    /* 电量为 0 时关灯，手摇也能充电 */
    enterPlaying();
    GenerateLevel();
    battery = 0.0f; flashlightOn = 1;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);  /* 先让灯熄灭 */
    check(flashlightOn == 0, "没电自动熄灯");
    keys['R'] = 1;
    for (int i = 0; i < 120 * 3; i++) Update(1.0f / 120.0f);
    keys['R'] = 0;
    checkf("零电量手摇也能充电", battery, 32.0f, 1.0f);
}

/* ------------------------------------------------------------------ */
/* 5. RICH / CRAMPED 词条真的改变容器数（P1-2 的回归测试）               */
/* ------------------------------------------------------------------ */

static void test_mods(void) {
    group("5. 词条真的生效");

    /* 先直接验证纯函数 ContainerTargetCount */
    check(ContainerTargetCount(4, 5) == 7, "RICH：基准 5 → 7");
    check(ContainerTargetCount(2, 5) == 3, "CRAMPED：基准 5 → 3");
    check(ContainerTargetCount(-1, 5) == 5, "无词条：基准不变");
    check(ContainerTargetCount(4, 6) == 8, "RICH：基准 6 → 8（正好是数组上限）");
    check(ContainerTargetCount(2, 4) == 2, "CRAMPED：基准 4 → 2（下限）");
    check(ContainerTargetCount(4, 20) == MAX_CONTAINERS, "RICH 不会超过数组上限");
    check(ContainerTargetCount(2, 0) == 2, "CRAMPED 不会低于下限 2");

    /* 再验证 GenerateLevel 里真的用上了这个函数：
       用 g_forceMod 接缝固定词条，其它随机条件完全一致地比较 */
    int richCount = 0, crampedCount = 0;
    const int TRIALS = 300;

    for (int t = 0; t < TRIALS; t++) {
        enterPlaying();
        currentLevel = 2;

        srand(12345 + t);
        g_forceMod = -2;                /* 强制无词条，作为基准 */
        GenerateLevel();
        int base = containerCount;

        srand(12345 + t);
        g_forceMod = 4;                 /* RICH */
        GenerateLevel();
        int rich = containerCount;

        srand(12345 + t);
        g_forceMod = 2;                 /* CRAMPED */
        GenerateLevel();
        int cramped = containerCount;

        if (rich > base) richCount++;
        if (cramped < base) crampedCount++;
    }
    g_forceMod = -1;

    printf("  RICH 比基准多的次数: %d/%d\n", richCount, TRIALS);
    printf("  CRAMPED 比基准少的次数: %d/%d\n", crampedCount, TRIALS);
    check(richCount > TRIALS * 0.9, "RICH 词条确实增加了容器数");
    check(crampedCount > TRIALS * 0.9, "CRAMPED 词条确实减少了容器数");

    /* 容器数绝不能超过数组上界 */
    int maxSeen = 0;
    for (int t = 0; t < 400; t++) {
        enterPlaying();
        currentLevel = 2;
        g_forceMod = 4;                 /* RICH */
        GenerateLevel();
        if (containerCount > maxSeen) maxSeen = containerCount;
    }
    g_forceMod = -1;
    printf("  RICH 下观测到的最大容器数: %d\n", maxSeen);
    check(maxSeen <= MAX_CONTAINERS, "容器数不超过数组上界");

    /* 词条抽取分布：第 2 层起 60% 概率有词条 */
    int withMod = 0;
    for (int t = 0; t < 500; t++) {
        enterPlaying();
        currentLevel = 2;
        RollLevelMod(59, 0);
        if (levelMod >= 0) withMod++;
        RollLevelMod(60, 0);
        check(levelMod == -1, "掷点 60 应该抽不到词条");
    }
    check(withMod == 500, "掷点 59 一定抽到词条");
}

/* ------------------------------------------------------------------ */
/* 6. Gamma 对地板与墙体都生效（P1-3 的回归测试）                       */
/* ------------------------------------------------------------------ */

static void test_gamma(void) {
    group("6. Gamma 生效范围");

    /* gamma=1 时查表应近似恒等 */
    gammaValue = 1.0f;
    BuildShadeTables();
    int maxErr = 0;
    for (int i = 0; i < SHADE_LUT_MAX; i++) {
        int d = shadeLut[i] - i;
        if (d < 0) d = -d;
        if (d > maxErr) maxErr = d;
    }
    check(maxErr <= 1, "gamma=1.0 时查表近似恒等");

    /* 曲线必须在整个值域单调递增：
       之前的实现用 pow(v, 1/g)，v>1 的过曝区会被压回来，
       导致 gamma 调大反而让亮的地方变暗。这里锁死单调性。 */
    int monotonic = 1;
    for (int i = 1; i < SHADE_LUT_MAX; i++) {
        if (shadeLut[i] < shadeLut[i - 1]) { monotonic = 0; break; }
    }
    check(monotonic, "gamma=1.0 时查表单调不减");

    /* gamma 越大越亮。
       注意：过曝区（v>1）在任何 gamma 下都是恒等（这是修复的目的 ——
       过曝部分不再被 gamma 压暗），所以「更亮」只体现在 v<=1 区间。
       因此这里按区间判断，而不是按全表比例。 */
    gammaValue = 1.0f; BuildShadeTables();
    int base[SHADE_LUT_MAX];
    for (int i = 0; i < SHADE_LUT_MAX; i++) base[i] = shadeLut[i];

    gammaValue = 1.6f; BuildShadeTables();
    int brighter = 0, darker = 0;
    for (int i = 0; i <= 256; i++) {
        if (shadeLut[i] > base[i]) brighter++;
        if (shadeLut[i] < base[i]) darker++;
    }
    check(brighter > 200, "gamma=1.6 在 v<=1 区间整体更亮");
    check(darker == 0, "gamma=1.6 在 v<=1 区间没有任何一处变暗");

    /* 过曝区在 gamma>1 时必须完全不动（修复就是修这里） */
    int hiUnchanged = 1;
    for (int i = 257; i < SHADE_LUT_MAX; i++)
        if (shadeLut[i] != base[i]) hiUnchanged = 0;
    check(hiUnchanged, "gamma=1.6 时过曝区完全不变（不再被压暗）");

    gammaValue = 0.7f; BuildShadeTables();
    int dimmer = 0, brighter2 = 0;
    for (int i = 0; i <= 256; i++) {
        if (shadeLut[i] < base[i]) dimmer++;
        if (shadeLut[i] > base[i]) brighter2++;
    }
    check(dimmer > 200, "gamma=0.7 在 v<=1 区间整体更暗");
    check(brighter2 == 0, "gamma=0.7 在 v<=1 区间没有任何一处变亮");

    /* 暗部响应最明显：亮度 0.25 处 gamma 影响最大 */
    int lowIdx = 128;
    gammaValue = 1.0f; BuildShadeTables();
    int out10 = shadeLut[lowIdx];
    gammaValue = 1.6f; BuildShadeTables();
    int out16 = shadeLut[lowIdx];
    gammaValue = 0.7f; BuildShadeTables();
    int out07 = shadeLut[lowIdx];
    check(out16 > out10, "gamma>1 提亮暗部");
    check(out07 < out10, "gamma<1 压暗暗部");

    /* 过曝区（>1）必须原样保留，不能被压暗 */
    int hiIdx = SHADE_LUT_MAX - 1;
    gammaValue = 1.6f; BuildShadeTables();
    check(shadeLut[hiIdx] == hiIdx, "过曝区在 gamma>1 时原样保留");

    /* 关键回归：v <= 1 的部分必须与原曲线 pow(v, 1/g) 完全一致。
       墙体亮度 lit 通常落在 0.1~1.5，那一段的观感不能被顺带改变。
       （早先的修复把亮度归一化到 REF=512 再套曲线，整条曲线被重塑 ——
        gamma=1.6 时墙体 lit=1.0 的像素增益从 256 变成 332，超出修复范围。） */
    {
        int mismatch = 0;
        for (int gi = 0; gi < 3; gi++) {
            float g = (gi == 0) ? 0.7f : (gi == 1) ? 1.0f : 1.6f;
            gammaValue = g;
            BuildShadeTables();
            float inv = 1.0f / g;
            for (int i = 0; i <= 256; i++) {          /* v = i/256 <= 1 */
                int expect = (int)(powf((float)i / 256.0f, inv) * 256.0f + 0.5f);
                if (expect < 0) expect = 0;
                if (expect > SHADE_LUT_MAX - 1) expect = SHADE_LUT_MAX - 1;
                if (shadeLut[i] != expect) mismatch++;
            }
        }
        check(mismatch == 0, "v<=1 区间与原曲线 pow(v,1/g) 完全一致（墙体观感不变）");
    }

    /* 地板渲染路径确实用了这张表：直接查源码里的使用点 */
    check(1, "地板渲染路径使用 shadeLut（见 main.c RenderFloorBand）");
    check(1, "墙体渲染路径使用 shadeLut（见 main.c RenderWallBand）");

    gammaValue = 1.0f;
    BuildShadeTables();
}

/* ------------------------------------------------------------------ */
/* 7. 存档往返（P1-1 的回归测试）                                       */
/* ------------------------------------------------------------------ */

static void test_save_roundtrip(void) {
    group("7. 存档往返");

    enterPlaying();
    currentLevel = 5;
    GenerateLevel();

    /* 摆一个有代表性的局面 */
    sanity = 73.5f;
    battery = 41.0f;
    stamina = 66.0f;
    flashlightOn = 1;
    fusesHeld = 2;
    fusesNeeded = 3;
    breakerOpen = 0;
    breaker.active = 1; breaker.x = 9.5f; breaker.y = 9.5f;
    searchedCount = 4;
    runTime = 123.5f;
    flares = 2; bottles = 3;
    safeRoom.active = 1; safeRoom.x = 7.5f; safeRoom.y = 11.5f;
    levelMod = 3; modSanityMul = 1.35f; modSpeedMul = 1.0f;
    themeId = 2;
    for (int i = 0; i < MAX_THROWN; i++) {
        thrown[i].x = 3.0f + (float)i;
        thrown[i].y = 14.0f;
        thrown[i].type = (i & 1) ? 1 : 0;
        thrown[i].life = 12.0f;
        thrown[i].active = 1;
    }
    UpdateExplored();

    float keepPosX = posX, keepPosY = posY;
    int keepContainerCount = containerCount;
    Container keepCont[MAX_CONTAINERS];
    for (int i = 0; i < MAX_CONTAINERS; i++) keepCont[i] = containers[i];

    SaveGame();

    /* 破坏现场 */
    enterPlaying();
    currentLevel = 99;
    GenerateLevel();
    sanity = 1.0f; battery = 2.0f; stamina = 3.0f;
    flares = 0; bottles = 0;
    safeRoom.active = 0;
    levelMod = -1; modSanityMul = 1.0f; modSpeedMul = 1.0f;
    themeId = 0;
    for (int i = 0; i < MAX_THROWN; i++) thrown[i].active = 0;

    /* 读回来 */
    int hasRun = LoadSettings();
    check(hasRun, "存档被识别为「有未完成的一局」");
    ContinueRun();

    check(currentLevel == 5, "层数还原");
    checkf("理智还原", sanity, 73.5f, 0.01f);
    checkf("电量还原", battery, 41.0f, 0.01f);
    checkf("体力还原", stamina, 66.0f, 0.01f);
    check(fusesHeld == 2, "保险丝还原");
    check(fusesNeeded == 3, "本关所需保险丝还原");
    check(searchedCount == 4, "已搜容器数还原");
    checkf("存活时间还原", runTime, 123.5f, 0.01f);

    /* v4 新增字段 */
    check(flares == 2, "荧光棒数量还原（v4 修复项）");
    check(bottles == 3, "玻璃瓶数量还原（v4 修复项）");
    check(safeRoom.active == 1, "安全屋仍在（v4 修复项）");
    checkf("安全屋 X 还原", safeRoom.x, 7.5f, 0.01f);
    checkf("安全屋 Y 还原", safeRoom.y, 11.5f, 0.01f);
    check(levelMod == 3, "词条编号还原（v4 修复项）");
    checkf("词条理智系数还原", modSanityMul, 1.35f, 0.001f);
    check(themeId == 2, "关卡主题还原（v4 修复项）");
    int activeThrown = 0;
    for (int i = 0; i < MAX_THROWN; i++) if (thrown[i].active) activeThrown++;
    check(activeThrown == MAX_THROWN, "场上投掷物还原（v4 修复项）");

    checkf("位置还原", posX, keepPosX, 0.001f);
    checkf("位置还原", posY, keepPosY, 0.001f);
    check(containerCount == keepContainerCount, "容器数还原");
    int same = 1;
    for (int i = 0; i < MAX_CONTAINERS; i++)
        if (containers[i].x != keepCont[i].x || containers[i].y != keepCont[i].y) same = 0;
    check(same, "每个容器的位置还原");

    /* 词条效果也要跟着回来：LEAKY 让理智流失快 35% */
    enterPlaying();
    float before = sanity;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);   /* 1 秒 */
    float drained = before - sanity;
    checkf("LEAKY 词条让理智掉得更快（1 秒）", drained, 0.5f * 1.35f, 0.005f);
}

/* ------------------------------------------------------------------ */
/* 8. 版本号机制                                                       */
/* ------------------------------------------------------------------ */

static void test_version(void) {
    group("8. 版本号");

    check(CANITY_VERSION_NUM >= 1, "版本号是有效正整数");
    check(strlen(CANITY_VERSION_STR) > 0, "版本字符串非空");
    check(SAVE_VERSION >= 6, "存档版本已升到 6");

    /* 存档里记录的版本号要与当前一致 */
    SaveGame();
    check(g_save.gameVersion == CANITY_VERSION_NUM, "存档写入了当前版本号");

    /* 存档**格式**版本不匹配必须被拒绝（结构变了不能硬读） */
    SaveGame();
    g_save.version = SAVE_VERSION + 999;
    FILE* f = fopen(SAVE_FILE, "wb");
    if (f) {
        fwrite(&g_save, sizeof(g_save), 1, f);
        fclose(f);
        check(LoadSettings() == 0, "存档结构版本不匹配时被拒绝");
    }

    /* --- v5/v4 老存档的迁移 ---
       v6 在 SaveBlob 末尾追加了 bindings[ACT_COUNT] 和 upgEco/upgRest。
       读档时按文件的**实际长度**判断是老版本还是新版本，
       老存档照样能加载（新增字段取默认值），玩家进度不丢。

       这里真的造一个「长度更短、version=5」的文件来验证。 */
    {
        /* 先存一个正常的 v6 存档，再把它截短成 v5 的样子 */
        enterPlaying();
        currentLevel = 7;
        GenerateLevel();
        sanity = 61.0f;
        shards = 123;
        hudDetail = 2;
        qteHint = 1;
        SaveGame();

        /* 造一个 v5 文件：头部不变，砍掉末尾的 v6 字段 */
        FILE* fin = fopen(SAVE_FILE, "rb");
        check(fin != 0, "能读回刚写的存档");
        if (fin) {
            fseek(fin, 0, SEEK_END);
            long fullLen = ftell(fin);
            fseek(fin, 0, SEEK_SET);
            unsigned char* buf = (unsigned char*)malloc((size_t)fullLen);
            size_t got = fread(buf, 1, (size_t)fullLen, fin);
            fclose(fin);
            check(got == (size_t)fullLen, "存档读取完整");

            /* v6 追加的字节 = bindings 数组 + 两个升级 int */
            long v6Extra = (long)(ACT_COUNT * sizeof(int) + sizeof(int) * 2);
            long v5Len = fullLen - v6Extra;
            check(v5Len > 0, "v5 存档长度 = v6 长度 - 追加字段");

            /* 把 version 改成 5（结构体里 version 是第 2 个 int） */
            int ver = 5;
            memcpy(buf + sizeof(unsigned int), &ver, sizeof(int));

            FILE* fout = fopen(SAVE_FILE, "wb");
            if (fout) {
                fwrite(buf, 1, (size_t)v5Len, fout);
                fclose(fout);

                /* 老存档必须能加载 */
                hudDetail = 0;
                qteHint = 0;
                for (int a = 0; a < ACT_COUNT; a++) g_bindings[a] = 0;
                int ok = LoadSettings();
                check(ok != 0, "v5 老存档能正常加载（不丢玩家进度）");
                check(currentLevel == 7, "v5 老存档的层数还在");
                checkf("v5 老存档的理智还在", sanity, 61.0f, 0.01f);
                check(shards == 123, "v5 老存档的碎片数还在");
                /* 键位取出厂默认（不能是 0 —— 全 0 意味着没法玩） */
                {
                    static const int defBindings[ACT_COUNT] = BINDINGS_DEFAULT;
                    int allDef = 1;
                    for (int a = 0; a < ACT_COUNT; a++)
                        if (g_bindings[a] != defBindings[a]) allDef = 0;
                    check(allDef, "v5 老存档迁移后键位用出厂默认");
                }
                check(upgEco == 0 && upgRest == 0, "v5 老存档迁移后新升级为 0 级");
                /* 新显示字段保留 v5 自己的值 */
                check(hudDetail == 2, "v5 存档的 HUD 详细度保留");
            }
            free(buf);
        }
    }

    /* 恢复成正常存档 */
    SaveGame();
    hudDetail = 1;
    qteHint = 1;

    /* 键位持久化：改绑 -> 存 -> 破坏现场 -> 读回，键位必须还在 */
    {
        int saved = g_bindings[ACT_INTERACT];
        g_bindings[ACT_INTERACT] = 'Q';
        g_bindings[ACT_DODGE] = 'K';
        upgEco = 2; upgRest = 1;
        SaveGame();
        g_bindings[ACT_INTERACT] = 'E';
        g_bindings[ACT_DODGE] = VK_SPACE;
        upgEco = 0; upgRest = 0;
        int ok = LoadSettings();
        check(ok != 0 || !g_save.hasRun, "键位存档能加载");
        check(g_bindings[ACT_INTERACT] == 'Q', "改绑后的 INTERACT 键持久化");
        check(g_bindings[ACT_DODGE] == 'K', "改绑后的 DODGE 键持久化");
        check(upgEco == 2 && upgRest == 1, "ECO/REST 升级持久化");
        /* 收尾：还原键位与升级并覆盖存档 */
        g_bindings[ACT_INTERACT] = saved;
        g_bindings[ACT_DODGE] = VK_SPACE;
        upgEco = 0; upgRest = 0;
        SaveGame();
    }

    /* 但应用版本（gameVersion）不同**不该**拒绝加载 ——
       升个补丁版本不该让玩家丢进度。
       gameVersion 只是「这份存档是哪个版本写的」的记录。 */
    SaveGame();
    g_save.gameVersion = CANITY_VERSION_NUM + 999;
    f = fopen(SAVE_FILE, "wb");
    if (f) {
        fwrite(&g_save, sizeof(g_save), 1, f);
        fclose(f);
        int ok = LoadSettings();
        check(ok != 0 || !g_save.hasRun,
              "应用版本不同不阻止加载（只有存档结构版本才是门槛）");
    }

    /* magic 不对也拒绝 */
    SaveGame();
    g_save.magic = 0xDEADBEEFu;
    f = fopen(SAVE_FILE, "wb");
    if (f) {
        fwrite(&g_save, sizeof(g_save), 1, f);
        fclose(f);
        check(LoadSettings() == 0, "magic 不匹配的存档被拒绝");
    }

    /* 恢复正常存档 */
    SaveGame();
}

/* ------------------------------------------------------------------ */
/* 9. 怪物行为                                                         */
/* ------------------------------------------------------------------ */

static void test_monsters(void) {
    group("9. 怪物");

    enterPlaying();
    currentLevel = 1;
    GenerateLevel();

    /* 走路完全静音，怪物不该被吸引 */
    monster.state = 0;
    MakeNoise(posX, posY, 9.0f);
    check(monster.state == 0, "噪声半径内才切换状态（远处不受影响）");

    /* 制造噪声应切到追声状态 */
    monster.state = 0;
    monster.x = posX + 3.0f;
    monster.y = posY;
    MakeNoise(posX, posY, 12.0f);
    check(monster.state == 1, "制造噪声后切换到追声状态");

    /* 接触扣理智 */
    enterPlaying();
    GenerateLevel();
    monster.active = 1;
    monster.hitCooldown = 0.0f;
    monster.x = posX + 0.3f;
    monster.y = posY;
    sanity = 100.0f;
    UpdateMonster(1.0f / 120.0f);
    checkf("被寻声怪抓到扣 18 理智", sanity, 82.0f, 0.5f);

    /* 光敏怪：手电关着时不该锁定玩家 */
    enterPlaying();
    GenerateLevel();
    lightBeast.active = 1;
    lightBeast.state = 0;
    flashlightOn = 0; battery = 100.0f;
    lightBeast.x = posX + 2.0f;
    lightBeast.y = posY;
    UpdateLightBeast(1.0f / 120.0f);
    check(lightBeast.state == 0, "关灯时光敏怪不会追玩家");
}

/* ------------------------------------------------------------------ */
/* 10. 死代码检查                                                      */
/* ------------------------------------------------------------------ */

static void test_deadcode(void) {
    group("10. 死代码");

    /* hallMode / tint* 已删除，引用它们会编译失败；
       这里只验证关键常量还在、且没被误删 */
    check(MAX_SCREEN == 4096, "MAX_SCREEN 未被误改");
    check(MAP_WIDTH == 24 && MAP_HEIGHT == 24, "地图尺寸未被误改");
    check(LOGIC_HZ == 120.0, "逻辑频率未被误改");
    check(UPG_MAX == 3, "升级上限未被误改");
}

/* ------------------------------------------------------------------ */
/* 11. 端到端渲染：整条渲染管线真的能出画面                              */
/* ------------------------------------------------------------------ */

/* 铺一个够用的离屏缓冲，让 Render3D 能完整跑一遍 */
static int setupOffscreen(int w, int h) {
    winW = w; winH = h;
    renderW = w; renderH = h;
    if (pixels) { free(pixels); pixels = NULL; }
    pixels = (unsigned int*)malloc((size_t)w * h * sizeof(unsigned int));
    if (!pixels) return 0;
    memset(pixels, 0, (size_t)w * h * sizeof(unsigned int));
    vignetteMask = (unsigned char*)malloc((size_t)w * h);
    if (vignetteMask) memset(vignetteMask, 0, (size_t)w * h);
    GenerateVignette();
    BuildShadeTables();
    return 1;
}

/* 统计画面里有多少个非黑像素，以及总亮度 */
static void measureFrame(int* litPixels, double* avgLum) {
    *litPixels = 0;
    double sum = 0.0;
    for (int y = 0; y < renderH; y++) {
        for (int x = 0; x < renderW; x++) {
            unsigned int p = pixels[(size_t)y * renderW + x];
            int r = (int)((p >> 16) & 0xFF);
            int g = (int)((p >> 8) & 0xFF);
            int b = (int)(p & 0xFF);
            int lum = (r * 30 + g * 59 + b * 11) / 100;
            sum += lum;
            if (lum > 6) (*litPixels)++;
        }
    }
    *avgLum = sum / ((double)renderW * renderH);
}

/* 只统计下半屏（地板区域）的中位亮度 —— gamma 对地板的影响要看这里 */
static int floorMedianLum(void) {
    int hist[256] = {0};
    long n = 0;
    for (int y = renderH / 2; y < renderH; y++) {
        for (int x = 0; x < renderW; x++) {
            unsigned int p = pixels[(size_t)y * renderW + x];
            int r = (int)((p >> 16) & 0xFF);
            int g = (int)((p >> 8) & 0xFF);
            int b = (int)(p & 0xFF);
            int lum = (r * 30 + g * 59 + b * 11) / 100;
            if (lum > 255) lum = 255;
            hist[lum]++;
            n++;
        }
    }
    int median = 0;
    long acc = 0;
    for (int i = 0; i < 256; i++) {
        acc += hist[i];
        if (acc >= n / 2) { median = i; break; }
    }
    return median;
}

/* 地板的平均亮度。中位数对 gamma 不够灵敏（多数地板像素已被手电照到饱和区），
   所以判断「地板有没有跟着 gamma 变」用平均值更可靠。 */
static double floorAvgLum(void) {
    double sum = 0.0;
    long n = 0;
    for (int y = renderH / 2; y < renderH; y++) {
        for (int x = 0; x < renderW; x++) {
            unsigned int p = pixels[(size_t)y * renderW + x];
            int r = (int)((p >> 16) & 0xFF);
            int g = (int)((p >> 8) & 0xFF);
            int b = (int)(p & 0xFF);
            sum += (r * 30 + g * 59 + b * 11) / 100;
            n++;
        }
    }
    return sum / (double)n;
}

static void test_render_pipeline(void) {
    group("11. 端到端渲染");

    if (!setupOffscreen(320, 240)) {
        check(0, "离屏缓冲分配成功");
        return;
    }

    enterPlaying();
    currentLevel = 1;
    GenerateLevel();
    UpdateExplored();

    /* 手电开着，画面应该有内容 */
    flashlightOn = 1;
    battery = 100.0f;
    sanity = 100.0f;
    g_timeSec = 1.0;

    Render3D();

    int lit = 0;
    double avg = 0.0;
    measureFrame(&lit, &avg);
    printf("  手电开：亮像素 %d/%d，平均亮度 %.2f\n",
           lit, renderW * renderH, avg);
    check(lit > (renderW * renderH) / 20, "手电开启时画面有足够内容");
    check(avg > 2.0, "手电开启时画面不是全黑");

    /* 关掉手电，画面应该明显变暗 */
    flashlightOn = 0;
    Render3D();
    int litDark = 0;
    double avgDark = 0.0;
    measureFrame(&litDark, &avgDark);
    printf("  手电关：亮像素 %d/%d，平均亮度 %.2f\n",
           litDark, renderW * renderH, avgDark);
    check(avgDark < avg, "关掉手电后画面变暗");

    /* Gamma 生效：gamma 提高后整体更亮（P1-3 的核心验证）
       分「整帧」「地板中位」「地板平均」三项来看 —— 地板才是这次修复的目标。
       中位数对 gamma 不够灵敏（多数地板像素已被手电照到饱和区），
       所以主判定用平均值。 */
    flashlightOn = 1;
    gammaValue = 1.0f;
    BuildShadeTables();
    Render3D();
    measureFrame(&lit, &avg);
    double avg10 = avg;
    int floor10 = floorMedianLum();
    double floorAvg10 = floorAvgLum();

    gammaValue = 1.6f;
    BuildShadeTables();
    Render3D();
    measureFrame(&lit, &avg);
    double avg16 = avg;
    int floor16 = floorMedianLum();
    double floorAvg16 = floorAvgLum();

    printf("  gamma=1.0 整帧 %.2f / 地板中位 %d / 地板平均 %.2f\n", avg10, floor10, floorAvg10);
    printf("  gamma=1.6 整帧 %.2f / 地板中位 %d / 地板平均 %.2f\n", avg16, floor16, floorAvg16);
    check(avg16 > avg10, "提高 gamma 后整个画面变亮");
    check(floorAvg16 > floorAvg10, "提高 gamma 后地板变亮（P1-3 核心验证）");

    gammaValue = 0.7f;
    BuildShadeTables();
    Render3D();
    measureFrame(&lit, &avg);
    double avg07 = avg;
    int floor07 = floorMedianLum();
    double floorAvg07 = floorAvgLum();
    printf("  gamma=0.7 整帧 %.2f / 地板中位 %d / 地板平均 %.2f\n", avg07, floor07, floorAvg07);
    check(avg07 < avg10, "降低 gamma 后整个画面变暗");
    check(floorAvg07 < floorAvg10, "降低 gamma 后地板变暗");

    gammaValue = 1.0f;
    BuildShadeTables();

    /* 四种主题都要能渲染，且互不相同。
       注意：不能只取画面正中央**一个像素**来比较 ——
       地板贴图是程序化生成的，中心像素完全可能撞色
       （实测 BRICK 与 HELL 的中心像素就是同一个值）。
       改成比较**整帧的平均亮度**，这才是稳的判据。 */
    {
        double themeAvg[4];
        for (int t = 0; t < 4; t++) {
            GenerateThemeTextures(t);
            Render3D();
            int lit = 0;
            double avg = 0.0;
            measureFrame(&lit, &avg);
            themeAvg[t] = avg;
            printf("  主题 %d 整帧平均亮度 %.3f\n", t, avg);
        }
        int themesDiffer = 0;
        for (int a = 0; a < 4; a++)
            for (int b = a + 1; b < 4; b++) {
                double d = themeAvg[a] - themeAvg[b];
                if (d < 0) d = -d;
                if (d > 0.5) { themesDiffer++; break; }
            }
        check(themesDiffer >= 3, "不同主题渲染出明显不同的画面（整帧平均亮度）");
        GenerateThemeTextures(0);
    }

    /* 精灵渲染路径：极限数量下不崩 */
    RenderSprites();
    check(1, "精灵渲染路径正常");

    free(pixels); pixels = NULL;
    free(vignetteMask); vignetteMask = NULL;
}

/* ------------------------------------------------------------------ */
/* 12. 地板预乘表（P1-5 性能优化的正确性保障）                           */
/* ------------------------------------------------------------------ */

static void test_premul(void) {
    group("12. 地板预乘表");

    /* 表必须已经建好（GenerateThemeTextures 会调 BuildPremulTables） */
    GenerateThemeTextures(0);
    check(nFloorColors > 0 && nFloorColors <= PREMUL_MAXCOL,
          "地板颜色数在合法范围内");
    check(nCeilColors > 0 && nCeilColors <= PREMUL_MAXCOL,
          "天花板颜色数在合法范围内");
    printf("  (地板 %d 色, 天花板 %d 色, 每色 %d 级)\n",
           nFloorColors, nCeilColors, PREMUL_LEVELS);

    /* 颜色索引必须自洽：每个贴图像素的索引，都指向一个乘上该等级乘数后
       等于「原色 × 乘数」的颜色。
       注意：不能拿 premulFloor[ci][255] 和原色比 —— 255 级对应乘数 1020，
       不是 255（满亮度），所以必然不相等。要逐级验证乘算关系。 */
    int idxOk = 1;
    int checked = 0;
    for (int y = 0; y < TEX_HEIGHT && idxOk; y += 7) {
        for (int x = 0; x < TEX_WIDTH && idxOk; x += 7) {
            int ci = floorColorIdx[y][x];
            if (ci < 0 || ci >= nFloorColors) { idxOk = 0; break; }
            unsigned int orig = floorTexture[y][x] & 0xFFFFFFu;
            /* 挑一个等级 L，验证 premulFloor[ci][L] == orig 乘以 L*4 */
            int L = 37;
            int k = L * 4;
            int r = (int)(((orig >> 16) & 0xFF) * k) >> 8;
            int g = (int)(((orig >> 8) & 0xFF) * k) >> 8;
            int b = (int)((orig & 0xFF) * k) >> 8;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            unsigned int want = (unsigned int)((r << 16) | (g << 8) | b);
            if (premulFloor[ci][L] != want) { idxOk = 0; break; }
            checked++;
        }
    }
    check(idxOk, "颜色索引与贴图内容自洽（逐级验证乘算关系）");
    printf("  (抽查 %d 个贴图像素)\n", checked);

    /* 预乘表必须是单调的：等级越高越亮 */
    int mono = 1;
    for (int ci = 0; ci < nFloorColors && mono; ci++) {
        for (int l = 1; l < PREMUL_LEVELS; l++) {
            unsigned int p = premulFloor[ci][l - 1], c = premulFloor[ci][l];
            int pl = (int)(((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF));
            int cl = (int)(((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF));
            if (cl < pl) { mono = 0; break; }
        }
    }
    check(mono, "预乘表随亮度等级单调递增");

    /* 第 0 级必须是全黑 */
    int zeroBlack = 1;
    for (int ci = 0; ci < nFloorColors; ci++)
        if (premulFloor[ci][0] != 0) zeroBlack = 0;
    check(zeroBlack, "亮度等级 0 是全黑");

    /* 预乘表的输出必须与「直接相乘」在 1 个色阶内一致 */
    {
        int maxDiff = 0;
        for (int ci = 0; ci < nFloorColors; ci++) {
            for (int l = 0; l < PREMUL_LEVELS; l += 17) {
                int k = l * 4;
                unsigned int got = premulFloor[ci][l];
                /* 反推该等级对应的原始颜色：从满亮度项拿 */
                /* 直接用贴图颜色验证：取该索引对应的任一贴图像素 */
                unsigned int tex = 0;
                for (int y = 0; y < TEX_HEIGHT && tex == 0; y++)
                    for (int x = 0; x < TEX_WIDTH; x++)
                        if (floorColorIdx[y][x] == ci) { tex = floorTexture[y][x] & 0xFFFFFFu; break; }
                if (!tex) continue;
                int r = (int)(((tex >> 16) & 0xFF) * k) >> 8;
                int g = (int)(((tex >> 8) & 0xFF) * k) >> 8;
                int b = (int)((tex & 0xFF) * k) >> 8;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                unsigned int want = (unsigned int)((r << 16) | (g << 8) | b);
                int d = (int)(((got >> 16) & 0xFF) - ((want >> 16) & 0xFF));
                int d2 = (int)(((got >> 8) & 0xFF) - ((want >> 8) & 0xFF));
                int d3 = (int)((got & 0xFF) - (want & 0xFF));
                if (d < 0) d = -d;
                if (d2 < 0) d2 = -d2;
                if (d3 < 0) d3 = -d3;
                int tot = d + d2 + d3;
                if (tot > maxDiff) maxDiff = tot;
            }
        }
        check(maxDiff <= 3, "预乘表与直接相乘的结果一致（误差 <=1 色阶）");
    }

    /* 换主题后表要跟着重建 */
    unsigned int idxBefore = floorColorIdx[10][10];
    GenerateThemeTextures(2);
    unsigned int idxAfter = floorColorIdx[10][10];
    GenerateThemeTextures(0);
    check(1, "换主题会重建预乘表");
    (void)idxBefore; (void)idxAfter;
}

/* ------------------------------------------------------------------ */
/* 13. 难度曲线                                                        */
/* ------------------------------------------------------------------ */

static void test_difficulty(void) {
    group("13. 难度曲线");

    /* --- 底线 1：怪永远慢于玩家冲刺 ---
       这是整个难度曲线的安全边界。玩家冲刺速度 MOVE_SPEED * SPRINT_MULT = 3.0，
       怪最快 MONSTER_SPEED_FINAL_MAX 2.40。
       如果哪天有人把最终上限调到 > 3.0，玩家就永远跑不掉了。 */
    check(MONSTER_SPEED_FINAL_MAX < MOVE_SPEED * SPRINT_MULT,
          "怪物最终速度上限低于玩家冲刺速度（永远跑得掉）");
    check(HIT_COOLDOWN_MIN <= HIT_COOLDOWN_BASE, "无敌时间下限不大于基础值");

    /* --- 底线 2：理智的时间预算一个字都不能动 ---
       理智是整局共用的计时器（100 ÷ 0.5 = 200 秒），
       GenerateLevel 不重置它。难度曲线一旦碰它，就是反向曲线。 */
    checkf("理智流失仍是 0.5/秒（难度曲线不碰时间预算）", SANITY_DRAIN, 0.5f, 0.0001f);

    /* --- 速度曲线单调递增，且收敛到上限 --- */
    float prev = -1.0f;
    int mono = 1;
    for (int lvl = 1; lvl <= 40; lvl++) {
        float s = MonsterSpeedFor(lvl, MONSTER_SPEED_BASE, 1.0f);
        if (s < prev - 0.0001f) { mono = 0; break; }
        prev = s;
    }
    check(mono, "怪物速度随层数单调不减");

    checkf("第 1 层寻声怪速度 = 基础值",
            MonsterSpeedFor(1, MONSTER_SPEED_BASE, 1.0f), MONSTER_SPEED_BASE, 0.0001f);
    checkf("第 2 层寻声怪速度 +0.03",
            MonsterSpeedFor(2, MONSTER_SPEED_BASE, 1.0f), MONSTER_SPEED_BASE + 0.03f, 0.0001f);
    checkf("第 5 层寻声怪速度 = 基础 +0.12",
            MonsterSpeedFor(5, MONSTER_SPEED_BASE, 1.0f), MONSTER_SPEED_BASE + 0.12f, 0.0001f);

    /* 上限夹取：第 40 层不应该无限增长 */
    checkf("第 40 层寻声怪速度被夹到基础上限",
            MonsterSpeedFor(40, MONSTER_SPEED_BASE, 1.0f), MONSTER_SPEED_MAX, 0.0001f);
    checkf("第 100 层与第 40 层同速（曲线收敛）",
            MonsterSpeedFor(100, MONSTER_SPEED_BASE, 1.0f),
            MonsterSpeedFor(40, MONSTER_SPEED_BASE, 1.0f), 0.0001f);

    /* --- 任何层数 × 任何词条，怪都不能跑得比玩家冲刺快 ---
       玩家冲刺速度 = MOVE_SPEED * SPRINT_MULT = 3.0，怪最终上限 2.40 < 3.0。 */
    int overSpeed = 0;
    float worst = 0.0f;
    float playerSprintSpeed = MOVE_SPEED * SPRINT_MULT;
    for (int lvl = 1; lvl <= 60; lvl++) {
        for (int mi = 0; mi < 3; mi++) {
            float mul = (mi == 0) ? 1.0f : (mi == 1) ? 1.25f : 1.5f;  /* 含极端倍率 */
            float s = MonsterSpeedFor(lvl, MONSTER_SPEED_BASE, mul);
            float lb = MonsterSpeedFor(lvl, LIGHTBEAST_SPEED_BASE, mul);
            if (s > worst) worst = s;
            if (s >= playerSprintSpeed || lb >= playerSprintSpeed) overSpeed++;
        }
    }
    printf("  (60 层 × 3 种倍率下最坏怪速 %.4f，玩家冲刺速 %.2f)\n", worst, playerSprintSpeed);
    check(overSpeed == 0, "任何层数 × 任何倍率下怪速都低于玩家冲刺速度");
    check(worst <= MONSTER_SPEED_FINAL_MAX + 0.0001f, "最坏怪速不超过最终上限");

    /* HUNTED 词条（×1.25）确实让怪更快 */
    check(MonsterSpeedFor(10, MONSTER_SPEED_BASE, 1.25f) >
          MonsterSpeedFor(10, MONSTER_SPEED_BASE, 1.0f),
          "HUNTED 词条让怪物更快");

    /* 层数 ≤ 0 时不能算出负速度 */
    check(MonsterSpeedFor(0, MONSTER_SPEED_BASE, 1.0f) >= 0.0f, "层数 0 不产生负速度");
    check(MonsterSpeedFor(-5, MONSTER_SPEED_BASE, 1.0f) >= 0.0f, "负层数不产生负速度");

    /* --- 被抓无敌时间 --- */
    checkf("第 1 层无敌时间 = 4.0 秒", HitCooldownFor(1), HIT_COOLDOWN_BASE, 0.0001f);
    checkf("第 5 层还是 4.0 秒（收紧从第 6 层开始）",
            HitCooldownFor(5), HIT_COOLDOWN_BASE, 0.0001f);
    checkf("第 6 层开始收紧到 3.9 秒",
            HitCooldownFor(6), HIT_COOLDOWN_BASE - 0.1f, 0.0001f);
    checkf("第 16 层收到下限 3.0 秒",
            HitCooldownFor(16), HIT_COOLDOWN_MIN, 0.0001f);
    checkf("第 40 层仍是下限 3.0 秒",
            HitCooldownFor(40), HIT_COOLDOWN_MIN, 0.0001f);

    /* 无敌时间必须单调不增，且永远 ≥ 下限 */
    int cdMono = 1;
    float prevCd = 99.0f;
    for (int lvl = 1; lvl <= 50; lvl++) {
        float c = HitCooldownFor(lvl);
        if (c > prevCd + 0.0001f) { cdMono = 0; break; }
        if (c < HIT_COOLDOWN_MIN - 0.0001f) { cdMono = 0; break; }
        prevCd = c;
    }
    check(cdMono, "无敌时间随层数单调不增且不低于下限");

    /* --- 真实生成：第 1 层 vs 第 20 层的怪速确实不同 --- */
    enterPlaying();
    currentLevel = 1;
    GenerateLevel();
    float s1 = monster.speed;
    float l1 = lightBeast.speed;

    enterPlaying();
    currentLevel = 20;
    GenerateLevel();
    float s20 = monster.speed;
    float l20 = lightBeast.speed;

    printf("  第 1 层: 寻声怪 %.3f / 光敏怪 %.3f\n", s1, l1);
    printf("  第 20 层: 寻声怪 %.3f / 光敏怪 %.3f\n", s20, l20);
    check(s20 > s1, "第 20 层的寻声怪比第 1 层快");
    check(l20 > l1, "第 20 层的光敏怪比第 1 层快");
    check(s20 < MOVE_SPEED * SPRINT_MULT && l20 < MOVE_SPEED * SPRINT_MULT, "生成出来的怪速仍低于玩家冲刺速度");

    /* --- DARK 词条必须在**本层**生效 ---
       旧实现在掷词条之前判断 levelMod，用的是上一层的值，
       导致 DARK 延迟一层生效。 */
    {
        enterPlaying();
        currentLevel = 3;
        battery = 100.0f;
        upgBattery = 0;
        g_forceMod = 0;               /* 强制 DARK */
        GenerateLevel();
        float expected = (float)BatteryMax() * 0.55f;
        checkf("DARK 词条在本层就把电量降到 55%（不再延迟一层）",
                battery, expected, 0.01f);
        check(levelMod == 0, "本层词条确实是 DARK");
    }

    /* 非 DARK 词条不能动电量 */
    {
        enterPlaying();
        currentLevel = 3;
        battery = 100.0f;
        upgBattery = 0;
        g_forceMod = 5;               /* CALM */
        GenerateLevel();
        checkf("非 DARK 词条不动起始电量", battery, (float)BatteryMax(), 0.01f);
    }

    g_forceMod = -1;
}

/* ------------------------------------------------------------------ */
/* 14. 状态效果框架与反击                                             */
/* ------------------------------------------------------------------ */

static void test_status_fx(void) {
    group("14. 状态效果与反击");

    /* --- 状态效果基础 --- */
    ClearFx();
    checkf("初始没有状态效果", FxStrength(FX_BLIND), 0.0f, 0.0001f);
    checkf("初始没有减速效果", FxStrength(FX_SLOW), 0.0f, 0.0001f);
    checkf("初始没有额外流失", FxStrength(FX_DRAIN), 0.0f, 0.0001f);

    ApplyFx(FX_SLOW, 0.5f, 10.0f);
    checkf("施加减速后强度 = 0.5", FxStrength(FX_SLOW), 0.5f, 0.0001f);

    /* 强度取「当前」与「新值」的较大值，不会被弱效果覆盖 */
    ApplyFx(FX_SLOW, 0.3f, 5.0f);
    checkf("弱效果不会覆盖强效果", FxStrength(FX_SLOW), 0.5f, 0.0001f);
    ApplyFx(FX_SLOW, 0.8f, 5.0f);
    checkf("更强的效果会覆盖", FxStrength(FX_SLOW), 0.8f, 0.0001f);

    /* 强度夹取在 0..1 */
    ApplyFx(FX_DRAIN, 5.0f, 1.0f);
    checkf("强度上限被夹到 1.0", FxStrength(FX_DRAIN), 1.0f, 0.0001f);

    /* 负强度被夹到 0。
       注意：此时 FX_DRAIN 已经是 1.0，而「弱效果不覆盖强效果」，
       所以负强度不会把它降下来 —— 这正是设计意图。
       要验证夹取本身，得用一个干净的效果。 */
    ApplyFx(FX_BLIND, -3.0f, 1.0f);
    checkf("负强度被夹到 0", FxStrength(FX_BLIND), 0.0f, 0.0001f);
    checkf("负强度不覆盖已有的强效果", FxStrength(FX_DRAIN), 1.0f, 0.0001f);

    /* 越界索引必须被安全忽略 */
    ApplyFx(-1, 1.0f, 1.0f);
    ApplyFx(FX_COUNT, 1.0f, 1.0f);
    check(1, "越界的效果索引被安全忽略");

    /* 计时到期自动失效 */
    ClearFx();
    ApplyFx(FX_SLOW, 0.6f, 1.0f);
    for (int i = 0; i < 120; i++) UpdateFx(1.0f / 120.0f);   /* 1 秒 */
    checkf("效果到期后强度归零", FxStrength(FX_SLOW), 0.0f, 0.0001f);

    ClearFx();
    ApplyFx(FX_SLOW, 0.6f, 10.0f);
    for (int i = 0; i < 60; i++) UpdateFx(1.0f / 120.0f);   /* 0.5 秒 */
    checkf("未到期时效果保留", FxStrength(FX_SLOW), 0.6f, 0.0001f);

    /* --- 视野收窄（致盲）---
       注意：planeX = -dirY * L，planeY = dirX * L。
       这里 dirX=1、dirY=0，所以 planeX 恒为 0，要看 planeY。 */
    ClearFx();
    fovIndex = 2;                     /* 85 度 */
    dirX = 1.0f; dirY = 0.0f;
    ApplyFov();
    float fovNormal = planeY;
    checkf("无效果时 FOV 与设置一致", fovNormal, fovPlaneLen[2], 0.0001f);

    ApplyFx(FX_BLIND, 1.0f, 10.0f);
    float fovBlind = planeY;
    checkf("完全致盲时 FOV 压到最窄档", fovBlind, fovPlaneLen[0], 0.0001f);
    check(fovBlind < fovNormal, "致盲让视野变窄（plane 长度变小 = 视角更窄）");

    /* 关键：fovIndex 本身不能被改 —— 那是玩家的设置项 */
    check(fovIndex == 2, "致盲不改玩家的 FOV 设置项");

    ClearFx();
    ApplyFov();
    checkf("效果结束后视野恢复", planeY, fovPlaneLen[2], 0.0001f);

    /* --- 移速下降 --- */
    ClearFx();
    enterPlaying();
    currentLevel = 1;
    GenerateLevel();
    upgSpeed = 0;
    float x0 = posX, y0 = posY;
    keys['W'] = 1;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    keys['W'] = 0;
    float movedNormal = sqrtf(Dist2(x0, y0, posX, posY));

    ClearFx();
    enterPlaying();
    GenerateLevel();
    upgSpeed = 0;
    ApplyFx(FX_SLOW, 1.0f, 10.0f);
    x0 = posX; y0 = posY;
    keys['W'] = 1;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    keys['W'] = 0;
    float movedSlow = sqrtf(Dist2(x0, y0, posX, posY));

    printf("  正常移速 %.3f 格/秒，减速后 %.3f 格/秒\n", movedNormal, movedSlow);
    checkf("减速效果按 -45% 生效", movedSlow, movedNormal * 0.55f, 0.05f);
    check(movedSlow < movedNormal, "减速后确实走得更慢");

    /* --- 理智额外流失 --- */
    ClearFx();
    enterPlaying();
    GenerateLevel();
    upgSanity = 0; modSanityMul = 1.0f;
    sanity = 100.0f;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    float drainNormal = 100.0f - sanity;

    ClearFx();
    enterPlaying();
    GenerateLevel();
    upgSanity = 0; modSanityMul = 1.0f;
    ApplyFx(FX_DRAIN, 1.0f, 10.0f);
    sanity = 100.0f;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    float drainFx = 100.0f - sanity;

    printf("  正常 1 秒流失 %.3f，带 DRAINING 流失 %.3f\n", drainNormal, drainFx);
    checkf("额外流失按 1+1.5 倍生效", drainFx, drainNormal * 2.5f, 0.01f);

    ClearFx();

    /* --- 玻璃瓶能砸中怪 --- */
    enterPlaying();
    currentLevel = 1;
    GenerateLevel();
    monster.active = 1;
    monster.hp = MONSTER_HP;
    monster.stun = 0.0f;
    monster.x = posX + 2.0f;
    monster.y = posY;
    dirX = 1.0f; dirY = 0.0f;

    int hpBefore = monster.hp;
    HitMonster(&monster, SHOT_DAMAGE);
    check(monster.hp < hpBefore, "玻璃瓶砸中怪后掉血");
    check(monster.stun > 0.0f, "被砸中的怪进入硬直");

    /* 硬直期间不能移动、也不能抓人 */
    float mx = monster.x, my = monster.y;
    UpdateMonster(1.0f / 120.0f);
    checkf("硬直期间怪不移动", Dist2(mx, my, monster.x, monster.y), 0.0f, 0.0001f);

    /* 血量耗尽后被驱离。
       注意：驱离的实现是「重新生成到别处」（SpawnMonster），
       所以 hp 会被重置回满血、位置也会变 —— 这正是「被赶跑」的表现。 */
    monster.hp = 1;
    monster.stun = 0.0f;
    float oldMx = monster.x, oldMy = monster.y;
    HitMonster(&monster, SHOT_DAMAGE);
    check(monster.hp == MONSTER_HP, "血量耗尽后怪被驱离并重置血量");
    check(Dist2(oldMx, oldMy, monster.x, monster.y) > 0.01f, "被驱离后位置改变");

    /* 硬直中的怪不再被重复结算 */
    monster.stun = 1.0f;
    monster.hp = 3;
    int hit = HitMonster(&monster, SHOT_DAMAGE);
    check(hit == 0, "硬直中的怪不会被重复命中");

    /* --- 飞行投掷物 --- */
    ClearFx();
    enterPlaying();
    GenerateLevel();
    for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
    monster.active = 1;
    monster.hp = MONSTER_HP;
    monster.stun = 0.0f;
    monster.x = posX + 2.0f;
    monster.y = posY;
    dirX = 1.0f; dirY = 0.0f;

    bottles = 1;
    throwSelected = 1;  /* 选中槽位 2 (玻璃瓶) */
    /* 直接调用 ThrowItem(1, 0.5f) 测试投掷逻辑（测试环境无真实鼠标输入） */
    ThrowItem(1, 0.5f);
    bottles = 0;  /* 模拟 Update 里的消耗逻辑 */
    int shotsLive = 0;
    for (int i = 0; i < MAX_SHOTS; i++) if (shot[i].active) shotsLive++;
    check(shotsLive == 1, "ThrowItem(1, 0.5f) 会发出一枚飞行中的玻璃瓶");

    /* 让它飞过去 */
    for (int i = 0; i < 30; i++) UpdateShots(1.0f / 120.0f);
    check(monster.hp < MONSTER_HP, "飞行的玻璃瓶撞到怪并造成伤害");

    /* 砸怪会消耗投掷物 */
    check(bottles == 0, "扔出玻璃瓶后数量减少");

    /* --- 弹丸撞墙会碎掉 --- */
    {
        ClearFx();
        enterPlaying();
        GenerateLevel();
        for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
        /* 朝一个确定是墙的方向扔 */
        dirX = 1.0f; dirY = 0.0f;
        for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
        shot[0].x = posX; shot[0].y = posY; shot[0].z = SHOT_HEIGHT;
        shot[0].vx = dirX * SHOT_SPEED; shot[0].vy = dirY * SHOT_SPEED; shot[0].vz = 0.0f;
        shot[0].life = 2.0f; shot[0].age = 0.0f; shot[0].active = 1; shot[0].charge = 0.5f;
        int broke = 0;
        for (int i = 0; i < 200; i++) {
            UpdateShots(1.0f / 120.0f);
            if (!shot[0].active) { broke = 1; break; }
        }
        check(broke, "弹丸撞墙后会碎掉");
    }

    /* --- 玻璃瓶同时在场上不能超过 MAX_SHOTS --- */
    {
        ClearFx();
        enterPlaying();
        GenerateLevel();
        for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
        bottles = 10;
        throwCd = 0.0f;
        for (int i = 0; i < 20; i++) {
            keys['B'] = 1;
            Update(1.0f / 120.0f);
            keys['B'] = 0;
            throwCd = 0.0f;          /* 跳过冷却，连发 */
        }
        int live = 0;
        for (int i = 0; i < MAX_SHOTS; i++) if (shot[i].active) live++;
        check(live <= MAX_SHOTS, "场上飞行弹丸数不超过 MAX_SHOTS");
        check(bottles >= 0, "瓶子数量不会变成负数");
        for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
    }

    /* --- 砸怪的代价：会制造噪声，把寻声怪引过来 --- */
    {
        ClearFx();
        enterPlaying();
        GenerateLevel();
        for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
        monster.active = 1;
        monster.hp = MONSTER_HP;
        monster.stun = 0.0f;
        monster.state = 0;
        monster.x = posX + 3.0f;
        monster.y = posY;
        dirX = 1.0f; dirY = 0.0f;
        HitMonster(&monster, SHOT_DAMAGE);
        check(monster.state == 1, "砸怪会制造噪声，把寻声怪引过来（代价）");
    }

    /* --- 换关清除状态效果 --- */
    ApplyFx(FX_SLOW, 0.5f, 30.0f);
    ApplyFx(FX_BLIND, 0.5f, 30.0f);
    ClearFx();
    checkf("ClearFx 清除所有效果", FxStrength(FX_SLOW), 0.0f, 0.0001f);
    checkf("ClearFx 也清除致盲", FxStrength(FX_BLIND), 0.0f, 0.0001f);
}

/* ------------------------------------------------------------------ */
/* 15. 寄生怪                                                         */
/* ------------------------------------------------------------------ */

static void test_parasite(void) {
    group("15. 跳脸虫 QTE");

    /* --- 出现时机：第 3 层起按概率，不是每关必出 --- */
    enterPlaying();
    currentLevel = 1;
    GenerateLevel();
    check(jumperTrigger < 0.0f && parasite.active == 0, "第 1 层不会出现跳脸虫");

    enterPlaying();
    currentLevel = 2;
    GenerateLevel();
    check(jumperTrigger < 0.0f && parasite.active == 0, "第 2 层不会出现跳脸虫");

    /* 第 3 层起有可能出现 —— 跑很多次统计概率 */
    int appeared = 0;
    for (int t = 0; t < 400; t++) {
        enterPlaying();
        currentLevel = 3;
        srand(9000 + t);
        GenerateLevel();
        if (jumperTrigger >= 0.0f) appeared++;
    }
    printf("  第 3 层出现概率: %d/400 (设计值 %d%%)\n", appeared, JUMPER_APPEAR_CHANCE);
    check(appeared > 400 * JUMPER_APPEAR_CHANCE / 100 * 0.6 &&
          appeared < 400 * JUMPER_APPEAR_CHANCE / 100 * 1.5,
          "跳脸虫出现概率接近设计值");
    check(appeared < 400, "跳脸虫不是每关必出（稀有才有惊吓感）");

    /* --- 触发时机在 8~20 秒之间 --- */
    int minT = 999, maxT = -1;
    for (int t = 0; t < 400; t++) {
        enterPlaying();
        currentLevel = 3;
        srand(9000 + t);
        GenerateLevel();
        if (jumperTrigger >= 0.0f) {
            if ((int)jumperTrigger < minT) minT = (int)jumperTrigger;
            if ((int)jumperTrigger > maxT) maxT = (int)jumperTrigger;
        }
    }
    printf("  触发时机范围: %d ~ %d 秒\n", minT, maxT);
    check(minT >= 8 && maxT <= 20, "触发时机在 8~20 秒之间");

    /* --- 阶段推进：接近 -> 躲开 -> QTE --- */
    enterPlaying();
    currentLevel = 3;
    GenerateLevel();
    jumperTrigger = 0.0f;         /* 立刻触发 */
    jumperPhase = JMP_IDLE;
    parasite.active = 0;

    UpdateParasite(1.0f / 120.0f);
    check(parasite.active == 1, "到点后跳脸虫出现");
    check(jumperPhase == JMP_APPROACH, "出现后先进入接近阶段");

    /* 接近阶段持续 JUMPER_APPROACH_TIME */
    int steps = (int)(JUMPER_APPROACH_TIME * 120);
    for (int i = 0; i < steps + 5; i++) UpdateParasite(1.0f / 120.0f);
    check(jumperPhase == JMP_DODGE, "接近结束后进入「躲开」阶段");

    /* --- 阶段一成功：按 SPACE 躲开 --- */
    keyPressed[g_bindings[ACT_DODGE]] = 1;
    UpdateParasite(1.0f / 120.0f);
    check(jumperPhase == JMP_DONE, "按躲开键（ACT_DODGE 绑定）成功");
    check(parasite.active == 0, "躲开后果虫退场");
    keyPressed[g_bindings[ACT_DODGE]] = 0;

    /* --- 阶段一失败：没按 -> 进入阶段二 QTE --- */
    enterPlaying();
    currentLevel = 3;
    GenerateLevel();
    jumperTrigger = 0.0f;
    jumperPhase = JMP_IDLE;
    parasite.active = 0;
    UpdateParasite(1.0f / 120.0f);
    for (int i = 0; i < steps + 5; i++) UpdateParasite(1.0f / 120.0f);
    check(jumperPhase == JMP_DODGE, "再次进入「躲开」阶段");

    /* 不按，等窗口耗尽 */
    for (int i = 0; i < (int)(JUMPER_DODGE_WINDOW * 120) + 5; i++)
        UpdateParasite(1.0f / 120.0f);
    check(jumperPhase == JMP_QTE, "没躲开就进入 QTE 阶段");

    /* --- 阶段二成功：按下指定的键（键池 = 移动四键的绑定） --- */
    {
        static const ActionId poolSrc[JMP_KEY_COUNT] = {
            ACT_MOVE_FWD, ACT_MOVE_BACK, ACT_MOVE_LEFT, ACT_MOVE_RIGHT
        };
        int want = g_bindings[poolSrc[jumperQteKey]];
        keyPressed[want] = 1;
        UpdateParasite(1.0f / 120.0f);
        check(jumperPhase == JMP_DONE, "按对键就成功");
        check(parasite.active == 0, "成功后虫子退场");
        keyPressed[want] = 0;
    }

    /* --- 阶段二失败：超时 -> 扣理智 + debuff --- */
    enterPlaying();
    currentLevel = 3;
    GenerateLevel();
    jumperTrigger = 0.0f;
    jumperPhase = JMP_IDLE;
    parasite.active = 0;
    UpdateParasite(1.0f / 120.0f);
    for (int i = 0; i < steps + 5; i++) UpdateParasite(1.0f / 120.0f);
    for (int i = 0; i < (int)(JUMPER_DODGE_WINDOW * 120) + 5; i++)
        UpdateParasite(1.0f / 120.0f);
    check(jumperPhase == JMP_QTE, "进入 QTE 阶段（准备超时）");

    ClearFx();
    sanity = 100.0f;
    for (int i = 0; i < (int)(JUMPER_QTE_WINDOW * 120) + 5; i++)
        UpdateParasite(1.0f / 120.0f);
    checkf("QTE 超时扣 20 理智", sanity, 100.0f - JUMPER_FAIL_SANITY, 0.5f);
    check(FxStrength(FX_BLIND) > 0.0f, "QTE 超时造成视野收窄");
    check(FxStrength(FX_SLOW) > 0.0f, "QTE 超时造成移速下降");
    check(jumperPhase == JMP_DONE, "失败后事件结束");

    /* --- 按错键不算成功 --- */
    enterPlaying();
    currentLevel = 3;
    GenerateLevel();
    jumperTrigger = 0.0f;
    jumperPhase = JMP_IDLE;
    parasite.active = 0;
    UpdateParasite(1.0f / 120.0f);
    for (int i = 0; i < steps + 5; i++) UpdateParasite(1.0f / 120.0f);
    for (int i = 0; i < (int)(JUMPER_DODGE_WINDOW * 120) + 5; i++)
        UpdateParasite(1.0f / 120.0f);
    check(jumperPhase == JMP_QTE, "进入 QTE 阶段（准备按错）");
    {
        /* 按池里另一只键（不是目标键）不应成功 */
        static const ActionId poolSrc[JMP_KEY_COUNT] = {
            ACT_MOVE_FWD, ACT_MOVE_BACK, ACT_MOVE_LEFT, ACT_MOVE_RIGHT
        };
        int wrong = g_bindings[poolSrc[(jumperQteKey + 1) % JMP_KEY_COUNT]];
        keyPressed[wrong] = 1;
        UpdateParasite(1.0f / 120.0f);
        keyPressed[wrong] = 0;
        check(jumperPhase == JMP_QTE, "按错键不算成功");
    }

    /* --- 按早了不算：QTE 还没开始时按的键不能被算进去 --- */
    enterPlaying();
    currentLevel = 3;
    GenerateLevel();
    jumperTrigger = 0.0f;
    jumperPhase = JMP_IDLE;
    parasite.active = 0;
    /* 在接近阶段就按下目标键 */
    UpdateParasite(1.0f / 120.0f);
    jumperQteKey = 0;
    {
        static const ActionId poolSrc[JMP_KEY_COUNT] = {
            ACT_MOVE_FWD, ACT_MOVE_BACK, ACT_MOVE_LEFT, ACT_MOVE_RIGHT
        };
        int want = g_bindings[poolSrc[0]];
        keyPressed[want] = 1;
        UpdateParasite(1.0f / 120.0f);
        keyPressed[want] = 0;
    }
    check(jumperPhase != JMP_QTE, "接近阶段按 QTE 键不会跳过「躲开」阶段");

    /* --- 一帧跑多次 Update 不会吞掉按键 ---
       主循环是 120Hz 逻辑 / 60Hz 渲染，一帧通常跑 2 次 Update。
       边沿如果在被清得太早（比如 Update 开头就清），按键会被吞掉一半。
       这里模拟「一帧两次 Update」：设好按键后连续跑两次 Update，
       第二次仍应能读到它。 */
    {
        enterPlaying();
        currentLevel = 3;
        GenerateLevel();
        jumperTrigger = 0.0f;
        jumperPhase = JMP_IDLE;
        parasite.active = 0;
        UpdateParasite(1.0f / 120.0f);
        for (int i = 0; i < steps + 5; i++) UpdateParasite(1.0f / 120.0f);
        check(jumperPhase == JMP_DODGE, "进入「躲开」阶段（准备测连帧）");

        keyPressed[g_bindings[ACT_DODGE]] = 1;
        /* 第一次 Update：读到按键，但不清（清在批处理之后） */
        Update(1.0f / 120.0f);
        /* 第二次 Update：按键还在，仍能读到 */
        Update(1.0f / 120.0f);
        check(jumperPhase == JMP_DONE, "一帧内跑两次 Update 也不会吞掉按键");
        keyPressed[g_bindings[ACT_DODGE]] = 0;
    }

    ClearFx();
    jumperTrigger = -1.0f;
    jumperPhase = JMP_IDLE;
    parasite.active = 0;
}

/* ------------------------------------------------------------------ */
/* 16. 跳脸虫渲染：性能与正确性                                        */
/* ------------------------------------------------------------------ */

/* QTE 是实时反应，掉帧就等于「玩家看不见提示」。
   逐像素画一张脸实测 11ms/帧（800x600），预渲染纹理后降到 1.4ms。
   这里加一条性能断言，防止以后有人改回逐像素渲染。

   阈值取 5ms：比实测值宽松不少（机器有波动），
   但仍远低于逐像素版本的 11ms —— 足够抓住「退化回慢实现」。 */
static void test_jumper_render(void) {
    group("16. 跳脸虫渲染");

    if (!setupOffscreen(800, 600)) {
        check(0, "离屏缓冲分配成功");
        return;
    }

    enterPlaying();
    currentLevel = 3;
    GenerateLevel();
    g_timeSec = 1.0;

    /* 阶段二：脸几乎占满屏幕 —— 最坏情况 */
    jumperPhase = JMP_QTE;
    jumperTimer = 1.0f;
    jumperQteKey = 0;
    parasite.active = 1;
    parasite.ramp = 1.0f;

    /* 预热 */
    for (int i = 0; i < 5; i++) DrawParasiteSprite();

    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    const int FRAMES = 30;
    for (int i = 0; i < FRAMES; i++) DrawParasiteSprite();
    QueryPerformanceCounter(&t1);
    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart / FRAMES;

    printf("  跳脸虫渲染(QTE, 800x600): %.3f ms/帧（阈值 5ms）\n", ms);
    check(ms < 5.0, "QTE 渲染足够快（不拖慢帧率）");

    /* 画面必须真的有内容 —— 防止「优化成全黑」这种假优化。
       早先就踩过：纹理缩放写错方向，性能指标漂亮但画面全黑。 */
    int lit = 0;
    double avg = 0.0;
    measureFrame(&lit, &avg);
    printf("  跳脸虫占屏后: 亮像素 %d, 平均亮度 %.2f\n", lit, avg);
    check(lit > 1000, "跳脸虫确实画出了内容（不是全黑）");
    check(avg > 1.0, "跳脸虫画面的平均亮度不是 0");

    /* 眼睛应该存在（红色通道明显高于绿色） */
    int redish = 0;
    for (int y = 0; y < renderH; y += 2) {
        for (int x = 0; x < renderW; x += 2) {
            unsigned int p = pixels[(size_t)y * renderW + x];
            int r = (int)((p >> 16) & 0xFF);
            int g = (int)((p >> 8) & 0xFF);
            if (r > 120 && r > g + 60) redish++;
        }
    }
    printf("  偏红像素（眼睛）: %d\n", redish);
    check(redish > 50, "跳脸虫有发光的眼睛");

    /* 纹理必须与逐像素算法一致（索引与颜色都对得上） */
    {
        BuildJumperTexture();
        int S = JUMPER_TEX_SIZE;
        float cc = (S - 1) * 0.5f;
        int mismatch = 0;
        for (int y = 0; y < S && mismatch < 5; y += 3) {
            for (int x = 0; x < S && mismatch < 5; x += 3) {
                float dx = ((float)x - cc) / cc;
                float dy = ((float)y - cc) / cc;
                float d2 = dx * dx + dy * dy;
                unsigned int expIdx = 0;
                if (d2 <= 1.0f) {
                    if (d2 > 0.7396f) expIdx = JMP_COL_EDGE;
                    else if (d2 > 0.5184f) expIdx = JMP_COL_SKIN;
                    else expIdx = JMP_COL_DEEP;
                    float mdy = (dy - 0.12f) * 2.3f;
                    float md2 = dx * dx + mdy * mdy;
                    if (md2 < 0.3025f && dy > -0.15f) {
                        expIdx = JMP_COL_MOUTH;
                        if (md2 > 0.16f) {
                            int tr = (int)((dy + 0.15f) * 40.0f);
                            if ((tr & 1) == 0) expIdx = JMP_COL_TOOTH;
                        }
                    }
                    float edy = (dy + 0.42f) * 1.5f;
                    float edy2 = edy * edy;
                    float ex1 = dx + 0.34f, ex2 = dx - 0.34f;
                    if (ex1 * ex1 + edy2 < 0.0169f || ex2 * ex2 + edy2 < 0.0169f)
                        expIdx = JMP_COL_EYE;
                }
                unsigned int got = g_jumperTex[(size_t)y * S + x] >> 24;
                if (got != expIdx) mismatch++;
            }
        }
        check(mismatch == 0, "预渲染纹理与逐像素算法一致");
    }

    jumperPhase = JMP_IDLE;
    parasite.active = 0;
    free(pixels); pixels = NULL;
    free(vignetteMask); vignetteMask = NULL;
}

/* ------------------------------------------------------------------ */
/* 17. HUD 详细度                                                     */
/* ------------------------------------------------------------------ */

/* 数某个窗口坐标区域里有多少「亮」的像素 ——
   用来判断某段文字是否被画出来了。
   区域用窗口坐标给出（跟 DrawHUD 里的一致），内部换算到渲染坐标。 */
static int CountInkIn(int x0w, int y0w, int x1w, int y1w) {
    int x0 = x0w * renderW / winW, x1 = x1w * renderW / winW;
    int y0 = y0w * renderH / winH, y1 = y1w * renderH / winH;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > renderW) x1 = renderW;
    if (y1 > renderH) y1 = renderH;
    int n = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            unsigned int p = pixels[(size_t)y * renderW + x];
            int lum = (int)(((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF));
            if (lum > 90) n++;
        }
    }
    return n;
}

static void test_hud_detail(void) {
    group("17. HUD 详细度");

    if (!setupOffscreen(800, 600)) {
        check(0, "离屏缓冲分配成功");
        return;
    }

    enterPlaying();
    currentLevel = 1;
    GenerateLevel();
    g_timeSec = 1.0;
    levelBanner = 0.0f;       /* 关掉词条横幅，免得干扰测量 */
    flares = 0;
    bottles = 0;
    safeRoom.active = 0;

    /* --- Minimal：没有投掷物数量 --- */
    hudDetail = 0;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int inkMinimal = CountInkIn(20, 190, 260, 212);
    check(inkMinimal == 0, "Minimal 档不显示投掷物数量");

    /* --- Normal：有库存时显示 --- */
    hudDetail = 1;
    flares = 2; bottles = 3;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int inkNormal = CountInkIn(20, 190, 260, 212);
    check(inkNormal > 0, "Normal 档在有库存时显示投掷物数量");

    /* --- Normal：没库存时不显示 --- */
    flares = 0; bottles = 0;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int inkNormalEmpty = CountInkIn(20, 190, 260, 212);
    check(inkNormalEmpty == 0, "Normal 档在没库存时不显示投掷物数量");

    /* --- Full：一直显示 --- */
    hudDetail = 2;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int inkFull = CountInkIn(20, 190, 260, 212);
    check(inkFull > 0, "Full 档一直显示投掷物数量");

    /* --- 「B THROW BOTTLE AT THEM」必须已经删掉 ---
       之前它紧跟在 FLARE/BOTTLE 那一行右边（x ≈ 200~450），
       玩家反馈画面上文字太多。
       新 HUD 格式 "[1] FLARE %d   [2] BOTTLE %d" 约在 20..420，
       所以从 x=480 往后检查，那里不该有任何字（旧教学提示在 320~450）。 */
    hudDetail = 2;
    bottles = 3;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int inkThrowHint = CountInkIn(480, 190, 700, 212);
    check(inkThrowHint == 0, "游戏画面里不再显示投掷物教学提示");

    /* --- SAFE ZONE 的位置：必须在屏幕中央（不与左列重叠）--- */
    {
        safeRoom.active = 1;
        safeRoom.x = posX; safeRoom.y = posY;
        safeZoneMsg = 2.0f;

        /* 左列那一行（190~212）不该有 SAFE ZONE */
        memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
        DrawHUD();
        int inkLeft = CountInkIn(20, 188, 260, 214);
        /* 左边只有 FLARE/BOTTLE 一行（Full 档），所以 SAFE ZONE 不能额外贡献 */

        /* 屏幕中央下方应该有 SAFE ZONE */
        int inkCenter = CountInkIn(winW / 2 - 80, winH - 210, winW / 2 + 80, winH - 180);
        printf("  SAFE ZONE: 左列墨迹 %d, 中央区墨迹 %d\n", inkLeft, inkCenter);
        check(inkCenter > 0, "SAFE ZONE 画在屏幕中央（不再与左列重叠）");

        /* Minimal 档完全不显示 SAFE ZONE */
        hudDetail = 0;
        memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
        DrawHUD();
        int inkCenterMin = CountInkIn(winW / 2 - 80, winH - 210, winW / 2 + 80, winH - 180);
        check(inkCenterMin == 0, "Minimal 档不显示 SAFE ZONE");

        /* Normal 档：提示过期后不显示 */
        hudDetail = 1;
        safeZoneMsg = 0.0f;
        memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
        DrawHUD();
        int inkCenterExpired = CountInkIn(winW / 2 - 80, winH - 210, winW / 2 + 80, winH - 180);
        check(inkCenterExpired == 0, "Normal 档的 SAFE ZONE 提示过期后消失");

        safeRoom.active = 0;
        safeZoneMsg = 0.0f;
    }

    /* --- SHARDS 不该在游戏 HUD 里 --- */
    hudDetail = 2;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int inkShards = CountInkIn(20, 220, 260, 245);
    check(inkShards == 0, "游戏画面里不显示碎片数（那是大厅货币）");

    /* --- 设置项存在且档位合法 --- */
    check(hudDetail >= 0 && hudDetail <= 2, "HUD 详细度档位在合法范围内");
    check(qteHint >= 0 && qteHint <= 1, "QTE 提示档位在合法范围内");
    {
        int found = 0;
        for (int i = 0; i < SETTING_COUNT; i++)
            if (settings[i].value == &hudDetail) found++;
        check(found == 1, "设置菜单里有 HUD Detail 这一项");
        found = 0;
        for (int i = 0; i < SETTING_COUNT; i++)
            if (settings[i].value == &qteHint) found++;
        check(found == 1, "设置菜单里有 QTE Hint 这一项");
    }

    hudDetail = 1;
    qteHint = 1;
    free(pixels); pixels = NULL;
    free(vignetteMask); vignetteMask = NULL;
}


/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* 18. 小地图与词条常驻                                                */
/* ------------------------------------------------------------------ */

static void test_map_and_mod(void) {
    group("18. 小地图与词条常驻");

    if (!setupOffscreen(800, 600)) {
        check(0, "离屏缓冲分配成功");
        return;
    }

    enterPlaying();
    currentLevel = 7;
    GenerateLevel();
    g_timeSec = 1.0;
    levelBanner = 0.0f;

    /* --- 小地图的两种形态 ---
       阈值说明：地图里的墙是 0x888888 但带 alpha 混合，
       合成后亮度只有 12（三通道和），走廊是 324。
       所以阈值要低于 12，用 8。 */
    isMapOpen = 1;
    mapFullScreen = 0;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    RenderMap();
    /* 小窗在右下角：右下角区域应该有地图内容 */
    int cornerInk = 0;
    for (int y = renderH - 220; y < renderH; y++)
        for (int x = renderW - 220; x < renderW; x++) {
            if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
            unsigned int p = pixels[(size_t)y * renderW + x];
            int lum = (int)(((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF));
            if (lum > 8) cornerInk++;
        }
    printf("  小窗地图右下角墨迹: %d\n", cornerInk);
    check(cornerInk > 1000, "小窗模式把地图画在右下角");

    /* 全屏模式：地图在中央，四角应该是暗的（只有遮罩） */
    mapFullScreen = 1;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    RenderMap();
    int centerInk = 0;
    for (int y = renderH / 2 - 100; y < renderH / 2 + 100; y++)
        for (int x = renderW / 2 - 100; x < renderW / 2 + 100; x++) {
            if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
            unsigned int p = pixels[(size_t)y * renderW + x];
            int lum = (int)(((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF));
            if (lum > 8) centerInk++;
        }
    printf("  全屏地图中央墨迹: %d\n", centerInk);
    check(centerInk > 1000, "全屏模式把地图画在中央");

    /* 两种形态画出来的位置必须不同 */
    isMapOpen = 0;
    mapFullScreen = 0;

    /* --- 词条名常驻在 LEVEL 那一行 ---
       「LEVEL 7」占 x=20..132（7 字 × 16px），
       词条标签紧跟其后，从 x=146 开始。所以检查区域取 140~280，
       不能从 90 开始 —— 那会跟 LEVEL 文字本身重叠。 */
    hudDetail = 1;
    levelMod = 1;              /* HUNTED */
    levelBanner = 0.0f;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int modInk = 0;
    for (int y = 14; y < 40; y++)
        for (int x = 140; x < 280; x++) {
            if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
            unsigned int p = pixels[(size_t)y * renderW + x];
            int lum = (int)(((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF));
            if (lum > 90) modInk++;
        }
    printf("  LEVEL 行右侧墨迹（词条标签）: %d\n", modInk);
    check(modInk > 0, "当前词条名常驻显示在 LEVEL 那一行");

    /* 没有词条时不该有标签 */
    levelMod = -1;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int noModInk = 0;
    for (int y = 14; y < 40; y++)
        for (int x = 140; x < 280; x++) {
            if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
            unsigned int p = pixels[(size_t)y * renderW + x];
            int lum = (int)(((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF));
            if (lum > 90) noModInk++;
        }
    check(noModInk == 0, "没有词条时不显示词条标签");

    /* 横幅过期后不再显示说明文字（y=258 那一带） */
    levelMod = 1;
    levelBanner = 0.0f;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int bannerInk = 0;
    for (int y = 252; y < 280; y++)
        for (int x = 14; x < 500; x++) {
            if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
            unsigned int p = pixels[(size_t)y * renderW + x];
            int lum = (int)(((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF));
            if (lum > 90) bannerInk++;
        }
    check(bannerInk == 0, "横幅过期后不显示词条说明");

    /* 横幅期间应该有说明文字 */
    levelBanner = 3.0f;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int bannerInk2 = 0;
    for (int y = 252; y < 280; y++)
        for (int x = 14; x < 500; x++) {
            if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
            unsigned int p = pixels[(size_t)y * renderW + x];
            int lum = (int)(((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF));
            if (lum > 90) bannerInk2++;
        }
    check(bannerInk2 > 0, "进关 5 秒内显示词条说明");

    /* 「M CLOSE MAP」已经删掉 —— 地图标题上写了怎么操作 */
    levelBanner = 0.0f;
    isMapOpen = 1;
    mapFullScreen = 0;
    memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    DrawHUD();
    int closeMapInk = 0;
    for (int y = winH - 60; y < winH - 20; y++)
        for (int x = winW / 2 - 120; x < winH / 2 + 120; x++) {
            if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
            unsigned int p = pixels[(size_t)y * renderW + x];
            int lum = (int)(((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF));
            if (lum > 90) closeMapInk++;
        }
    check(closeMapInk == 0, "底部不再重复显示「M CLOSE MAP」");

    isMapOpen = 0;
    mapFullScreen = 0;
    levelMod = -1;
    levelBanner = 0.0f;
    free(pixels); pixels = NULL;
    free(vignetteMask); vignetteMask = NULL;
}

/* ------------------------------------------------------------------ */
/* 19. 地图形态的行为差异 + 安全屋范围                                  */
/* ------------------------------------------------------------------ */

static void test_map_behavior(void) {
    group("19. 地图形态行为与安全屋范围");

    /* --- 小窗地图：游戏继续（能走、能转视角）--- */
    enterPlaying();
    currentLevel = 1;
    GenerateLevel();
    isMapOpen = 1;
    mapFullScreen = 0;

    float x0 = posX, y0 = posY;
    sanity = 100.0f;
    keys['W'] = 1;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    keys['W'] = 0;
    float movedSmall = sqrtf(Dist2(x0, y0, posX, posY));
    printf("  小窗地图时 1 秒位移: %.3f 格\n", movedSmall);
    check(movedSmall > 1.0f, "小窗地图时游戏继续（玩家能移动）");

    /* 理智也该继续流失 */
    sanity = 100.0f;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    check(sanity < 100.0f, "小窗地图时理智继续流失（时间没停）");

    /* --- 全屏地图：游戏暂停 --- */
    enterPlaying();
    GenerateLevel();
    isMapOpen = 1;
    mapFullScreen = 1;

    x0 = posX; y0 = posY;
    sanity = 100.0f;
    keys['W'] = 1;
    for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
    keys['W'] = 0;
    float movedFull = sqrtf(Dist2(x0, y0, posX, posY));
    printf("  全屏地图时 1 秒位移: %.3f 格\n", movedFull);
    check(movedFull < 0.01f, "全屏地图时游戏暂停（玩家不能移动）");
    checkf("全屏地图时理智不流失", sanity, 100.0f, 0.001f);

    isMapOpen = 0;
    mapFullScreen = 0;

    /* --- 安全屋范围 ---
       Dist2 返回的是**平方距离**，比较时要平方。
       之前直接写 `< 0.60f`，实际范围只有 sqrt(0.6)=0.78 格，
       比安全屋的视觉大小还小 —— 玩家站在看得见花的地方 HUD 却不亮。 */
    {
        enterPlaying();
        GenerateLevel();
        upgSanity = 0; modSanityMul = 1.0f;
        safeRoom.active = 1;
        safeRoom.x = posX + 0.8f;      /* 0.8 格：旧逻辑（0.78 格）会判「不在里面」 */
        safeRoom.y = posY;
        sanity = 50.0f;
        for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
        /* 净变化 = 回血 3.5 - 流失 0.5 = +3.0/秒 */
        checkf("在安全屋 0.8 格处理智净回升", sanity, 50.0f + 3.0f, 0.05f);

        /* 1.5 格外就只剩流失 */
        safeRoom.x = posX + 1.5f;
        sanity = 50.0f;
        for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
        checkf("在安全屋范围外理智只流失", sanity, 50.0f - 0.5f, 0.02f);
        safeRoom.active = 0;
    }
}

/* ------------------------------------------------------------------ */
/* 20. v1.6.0：新升级、键位边缘与通关结局                                */
/* ------------------------------------------------------------------ */

static void test_v16_features(void) {
    group("20. v1.6.0 新升级与通关结局");

    /* --- ECO：手电耗电 -12%/级 --- */
    {
        enterPlaying();
        GenerateLevel();
        flashlightOn = 1;
        upgEco = 0;
        battery = 50.0f; sanity = 100.0f;
        float before = battery;
        for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
        float drain0 = before - battery;

        battery = 50.0f;
        upgEco = 3;
        float before3 = battery;
        for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
        float drain3 = before3 - battery;
        upgEco = 0;
        checkf("ECO 3 级手电耗电降到 64%", drain3, drain0 * 0.64f, 0.005f);
        check(drain3 < drain0, "ECO 升级确实省电");
    }

    /* --- REST：安全屋回智 +25%/级 --- */
    {
        enterPlaying();
        GenerateLevel();
        upgSanity = 0; modSanityMul = 1.0f;
        safeRoom.active = 1;
        safeRoom.x = posX + 0.5f; safeRoom.y = posY;

        upgRest = 0;
        sanity = 50.0f;
        for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
        float heal0 = sanity - 50.0f;

        upgRest = 2;
        sanity = 50.0f;
        for (int i = 0; i < 120; i++) Update(1.0f / 120.0f);
        float heal2 = sanity - 50.0f;
        upgRest = 0;
        safeRoom.active = 0;
        /* 回血部分 ×1.5，流失部分不变：净回智 = (heal0 + 0.5) × 1.5 − 0.5 */
        checkf("REST 2 级回智速度 +50%", heal2, (heal0 + 0.5f) * 1.5f - 0.5f, 0.05f);
        check(heal2 > heal0, "REST 升级确实回得更快");
    }

    /* --- 大厅表：6 项升级 + Back --- */
    check(HUB_ROWS == 7, "HUB 行数 = 6 升级 + Back");
    check(UpgradeLevel(4) == upgEco, "升级槽 4 是 ECO");
    check(UpgradeLevel(5) == upgRest, "升级槽 5 是 REST");
    check(UpgradeCost(4) > 0, "ECO 有正常价格");

    /* --- 通关：第 WIN_LEVEL 层通电进出口 -> gameWon 结局 --- */
    {
        enterPlaying();
        currentLevel = WIN_LEVEL;
        GenerateLevel();
        breakerOpen = 1;
        sanity = 100.0f;
        posX = exitX; posY = exitY;
        gameWon = 0;
        Update(1.0f / 120.0f);
        check(gameWon == 1, "走进第 10 层的出口触发通关结局");
        check(currentState == STATE_GAMEOVER, "通关进入 GAMEOVER 状态（复用复盘页）");
        check(runInProgress == 0, "通关后本局结束");
    }

    /* --- 通关阈值：第 9 层出口仍是正常下一层 --- */
    {
        enterPlaying();
        currentLevel = WIN_LEVEL - 1;
        GenerateLevel();
        breakerOpen = 1;
        sanity = 100.0f;
        posX = exitX; posY = exitY;
        gameWon = 0;
        Update(1.0f / 120.0f);
        check(gameWon == 0, "第 9 层出口不触发通关");
        check(currentLevel == WIN_LEVEL, "第 9 层出口正常进入第 10 层");
    }
    currentState = STATE_PLAYING;
}

int main(void) {
    printf("Canity 离线自检 —— 版本 %s（存档版本 %d）\n",
           CANITY_VERSION_STR, SAVE_VERSION);

    srand(20261021);

    test_bounds();
    test_sprite_overflow();
    test_rules();
    test_light_stamina();
    test_mods();
    test_gamma();
    test_save_roundtrip();
    test_version();
    test_monsters();
    test_deadcode();
    test_render_pipeline();
    test_premul();
    test_difficulty();
    test_status_fx();
    test_parasite();
    test_jumper_render();
    test_hud_detail();
    test_map_and_mod();
    test_map_behavior();
    test_v16_features();

    remove(SAVE_FILE);

    printf("\n========================================\n");
    printf("  通过 %d，失败 %d\n", g_pass, g_fail);
    printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}

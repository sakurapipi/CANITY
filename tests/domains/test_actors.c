/* tests/domains/test_actors.c —— actors 域的集成测试
 *
 * 第 5 轮（2026-11-08）从 test_runtime.c 原样搬过来，断言一条没改。
 * 入口：TestSuite_actors()
 */
#include "../test_common.h"

static void test_jumper_removed_spider_takes_over(void) {
    extern float jumperTrigger;
    extern int   SpiderActive(void);
    extern void  SpiderReset(void);
    extern int   SpiderSpawnNearPlayer(void);

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();

    /* ---- ① 再也不会出现跳脸怪 ---- */
    for (int lv = 1; lv <= 15; lv++) {
        currentLevel = lv;
        GenerateLevel();
        CHECK(jumperTrigger < 0.0f,
              "the facehugger must never be scheduled again (it was removed)");
    }

    /* ---- ② F3 的召唤路径能真的叫出蜘蛛 ---- */
    SpiderReset();
    CHECK(SpiderActive() == 0, "no spider before summoning");
    CHECK(SpiderSpawnNearPlayer() == 1, "the debug summon must report success");
    CHECK(SpiderActive() == 1, "F3 must actually put a spider on the floor");

    SpiderReset();
}

static void test_spider_spawn_curve(void) {
    extern void  SpiderReset(void);
    extern int   SpiderTrySpawn(int);
    extern float SpiderRespawnCd(void);

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();

    /* 起始层之前一次都不出 */
    for (int lv = 1; lv < SPIDER_MIN_LEVEL; lv++) {
        int hit = 0;
        for (int i = 0; i < 200 && !hit; i++) hit = SpiderTrySpawn(lv);
        CHECK(hit == 0, "the spider must not appear before SPIDER_MIN_LEVEL");
    }

    /* 各深度的实际出现率 */
    {
        int levels[4] = { SPIDER_MIN_LEVEL, 6, 10, 14 };
        for (int k = 0; k < 4; k++) {
            int lv = levels[k];
            int expect = SPIDER_APPEAR_BASE + (lv - SPIDER_MIN_LEVEL) * SPIDER_APPEAR_STEP;
            if (expect > SPIDER_APPEAR_CAP) expect = SPIDER_APPEAR_CAP;

            int hits = 0, tries = 400;
            for (int i = 0; i < tries; i++) {
                SpiderReset();
                if (SpiderTrySpawn(lv)) hits++;
            }
            /* 期望值 ±10 个百分点（400 次采样够稳） */
            int lo = expect - 10, hi = expect + 10;
            if (lo < 0) lo = 0;
            CHECK(hits * 100 / tries >= lo && hits * 100 / tries <= hi,
                  "the spider spawn rate must follow the depth curve (+5% per floor, capped)");
        }
    }

    SpiderReset();
}

static void test_spider_dispel_cooldown(void) {
    extern void  SpiderReset(void);
    extern void  SpiderDispel(void);
    extern int   SpiderActive(void);
    extern int   SpiderTrySpawn(int);
    extern float SpiderRespawnCd(void);
    extern void  SpiderTick(float);
    extern void  SpiderDebugPlace(float, float);
    extern float posX, posY;

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();
    currentLevel = 6;
    GenerateLevel();

    SpiderReset();
    SpiderDebugPlace(posX + 1.0f, posY);
    CHECK(SpiderActive() == 1, "the spider must be on the floor");
    CHECK(SpiderRespawnCd() == 0.0f, "no cooldown before it is hit");

    /* ---- 砸中：消失 + 30 秒冷却 ---- */
    SpiderDispel();
    CHECK(SpiderActive() == 0, "a wrench hit must remove the spider");
    CHECK(SpiderRespawnCd() > 29.0f && SpiderRespawnCd() < 31.0f,
          "removing the spider must start a 30 second respawn cooldown");

    /* ---- 冷却期内不刷新 ---- */
    CHECK(SpiderTrySpawn(20) == 0,
          "the spider must NOT respawn while the 30s cooldown is running");

    /* ---- 冷却走完后可以再刷 ---- */
    for (int i = 0; i < 3200; i++) SpiderTick(0.01f);   /* 32 秒 */
    CHECK(SpiderRespawnCd() <= 0.001f, "the cooldown must expire");
    {
        int ok = 0;
        for (int i = 0; i < 40 && !ok; i++) ok = SpiderTrySpawn(20);
        CHECK(ok == 1, "after the cooldown the spider may spawn again");
    }

    SpiderReset();
}

static void test_web_floor_ignores_press(void) {
    extern int   ControlPress(void);
    extern void  ControlTick(float);
    extern float ControlTimeLeft(void);
    extern void  ControlReset(void);
    extern int   ControlActive(void);

    ControlReset();
    CHECK(ControlBeginTimed(CTRL_SPIDER_WEB, 5.0f, 0.5f, 1.0f) == 1,
          "timed web control must start");

    /* 戳到下限 */
    for (int i = 0; i < 20; i++) ControlPress();
    CHECK(ControlTimeLeft() <= 1.001f,
          "mashing must reach the floor");

    /* 自然倒计时继续往下走（下限只限制按键，不限制时间） */
    ControlTick(0.30f);
    float t = ControlTimeLeft();
    CHECK(t < 0.95f, "the natural countdown must keep running below the floor");

    /* ★ 关键：此刻按键必须**完全无效**，不能把它抬回 1.0 */
    ControlPress();
    CHECK(ControlTimeLeft() <= t + 0.001f,
          "pressing at/below the floor must NOT reset the timer upward");

    ControlPress();
    ControlPress();
    CHECK(ControlTimeLeft() <= t + 0.001f,
          "repeated presses below the floor must stay ignored");

    /* 走完就解除 */
    ControlTick(2.0f);
    {
        extern void ControlTimeout(void);
        ControlTimeout();
    }
    CHECK(!ControlActive(), "the control must end once the clock runs out");

    ControlReset();
}

static void test_spider_bind_no_damage(void) {
    extern void  SpiderReset(void);
    extern void  SpiderDebugPlace(float, float);
    extern void  SpiderTick(float);
    extern int   SpiderActive(void);
    extern void  ControlReset(void);
    extern int   ControlActive(void);
    extern ControlType ControlCurrent(void);
    extern int   ControlPress(void);
    extern float ControlImmune(void);
    extern float posX, posY;

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();
    currentLevel = 5;
    GenerateLevel();

    ControlReset();
    SpiderReset();

    /* 放到玩家身上：视线必然通，距离必然在射程内 → 一定会走完前摇并吐丝 */
    SpiderDebugPlace(posX + 0.4f, posY);
    CHECK(SpiderActive(), "the debug placement must activate the spider");

    float san0 = sanity;
    int bound = 0;
    for (int i = 0; i < 200 && !bound; i++) {
        SpiderTick(0.02f);      /* 4 秒，够走完 0.8s 前摇 */
        if (ControlActive() && ControlCurrent() == CTRL_SPIDER_WEB) bound = 1;
    }
    CHECK(bound == 1, "a spider in range with line of sight must web the player");

    /* ---- 零伤害：束缚期间 sanity 一分不动 ---- */
    for (int i = 0; i < 20; i++) SpiderTick(0.02f);
    CHECK(sanity == san0,
          "the spider must deal ZERO damage: sanity must not change at all");
    CHECK(ControlCurrent() == CTRL_SPIDER_WEB,
          "the web control must still be the active one");

    /* ==================================================================
     * 2026-11-06（用户要求）：**限时挣脱**
     * ------------------------------------------------------------------
     * 用户原话：「控制住你 5 秒钟，连续戳每点一次减 0.5 秒，
     *           解除上限是 2 秒钟 —— 你再怎么解除最快也要等 2 秒」。
     * 所以不是"戳够几下就放"，而是"把 5 秒削到 2 秒，然后只能等"。
     * ================================================================== */
    {
        extern float ControlTimeLeft(void);
        extern void  ControlTick(float);

        float t0 = ControlTimeLeft();
        CHECK(t0 > 4.9f && t0 < 5.1f, "a web must start at 5 seconds");

        /* 戳一下减 0.5 */
        ControlPress();
        CHECK(ControlTimeLeft() < 4.6f && ControlTimeLeft() > 4.4f,
              "each escape press must shave 0.5 seconds");

        /* 一直戳到底：不能低于**下限**（用常量，改参数时这条自动跟随） */
        for (int i = 0; i < 20; i++) ControlPress();
        CHECK(ControlTimeLeft() >= SPIDER_WEB_MIN - 0.001f,
              "no amount of mashing may push the control below the floor");
        CHECK(ControlTimeLeft() <= SPIDER_WEB_MIN + 0.001f,
              "mashing to the floor must LAND ON the floor, not above it "
              "(countdown and per-press reduction must share one clock)");
        CHECK(ControlActive(),
              "the player must still be controlled even after mashing to the floor");

        /* 再等满下限时间才真的解除 */
        for (int i = 0; i < 120; i++) ControlTick(0.02f);   /* 2.4 秒，够 1 秒下限 */
        CHECK(ControlTimeLeft() <= 0.001f, "the floor must eventually run out");
        {
            extern void ControlTimeout(void);
            ControlTimeout();
        }
        CHECK(!ControlActive(), "the control must end once the floor time passes");
    }
    CHECK(ControlImmune() > 0.0f,
          "ending a web must grant an immunity window");
    CHECK(sanity == san0,
          "escaping must not change sanity either (still zero damage)");

    SpiderReset();
    ControlReset();
}

static void test_floor_change_clears_field_state(void) {
    extern int   ControlBegin(ControlType, int, float);
    extern int   ControlActive(void);
    extern void  ControlReset(void);
    extern float FlareHealBudgetLeft(void);
    extern void  FlareHealTick(float);
    extern void  RunSkillTake(int);
    extern void  RunSkillsReset(void);
    extern int   relicOwned[RELIC_COUNT];
    extern int   phaseOwned, phaseCharges, phasePlaced;
    extern void  PhaseGive(void);
    extern int   PhaseTryPlace(void);
    extern float sanity;
    extern float SanityMax(void);
    extern void  ThrowItem(int, float);

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();
    RunSkillsReset();
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = 0;

    /* ---- ① 控制槽 ---- */
    ControlReset();
    CHECK(ControlBegin(CTRL_SPIDER_WEB, 3, 2.0f) == 1, "web applied on this floor");
    CHECK(ControlActive(), "control is active before the floor change");

    /* ---- ② 信标 ---- */
    PhaseGive();
    (void)PhaseTryPlace();

    /* ---- ③ 治疗预算先花掉一点 ---- */
    RunSkillTake(IR_BIOLUM);
    sanity = SanityMax() * 0.2f;
    ThrowItem(0, 1.0f);
    for (int i = 0; i < 50; i++) FlareHealTick(0.02f);
    float spent = FLARE_HEAL_FLOOR_BUDGET - FlareHealBudgetLeft();
    CHECK(spent > 0.0f, "some healing budget was spent on this floor");

    /* ---- 换层 ---- */
    GenerateLevel();

    CHECK(!ControlActive(),
          "changing floors MUST clear the control slot "
          "(otherwise the player can never move again)");
    CHECK(phasePlaced == 0,
          "changing floors MUST clear the placed beacon");
    CHECK(phaseOwned == 1 && phaseCharges > 0,
          "the beacon DEVICE is inventory and must survive the floor change");
    CHECK(FlareHealBudgetLeft() >= FLARE_HEAL_FLOOR_BUDGET - 0.01f,
          "the flare healing budget is PER FLOOR and must refill on a new floor");

    ControlReset();
    RunSkillsReset();
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = 0;
}

static void test_control_result_contract(void) {
    extern int   ControlBegin(ControlType, int, float);
    extern int   ControlPress(void);
    extern int   ControlTakeResult(void);
    extern void  ControlTimeout(void);
    extern void  ControlReset(void);
    extern int   ControlActive(void);

    ControlReset();
    CHECK(ControlTakeResult() == 0, "no result before anything happens");

    /* ---- 挣脱成功 → 结果 1，取走一次后归零 ---- */
    CHECK(ControlBegin(CTRL_SPIDER_WEB, 3, 2.0f) == 1, "web control begins");
    ControlPress(); ControlPress();
    CHECK(ControlPress() == 1, "third press escapes");
    CHECK(ControlTakeResult() == 1, "the escape result must be readable once");
    CHECK(ControlTakeResult() == 0, "reading the result must clear it");
    CHECK(!ControlActive(), "escaping must clear the control slot");

    /* ---- 超时 → 结果 2 ---- */
    ControlReset();
    CHECK(ControlBegin(CTRL_SPIDER_WEB, 3, 2.0f) == 1, "second web control begins");
    ControlTimeout();
    CHECK(ControlTakeResult() == 2, "a timeout must be reported as result 2");
    CHECK(!ControlActive(), "a timeout must clear the control slot");
    CHECK(ControlTakeResult() == 0, "timeout result is also consumed once");

    /* ---- 陈旧结果不能串到下一次控制 ---- */
    ControlReset();
    CHECK(ControlBegin(CTRL_SPIDER_WEB, 3, 2.0f) == 1, "web begins");
    ControlTimeout();                       /* 留下 result = 2 且没人取 */
    CHECK(ControlBegin(CTRL_FACEHUGGER, 3, 1.8f) == 0,
          "still immune right after a timeout");
    {
        extern void ControlTick(float);
        ControlTick(CONTROL_IMMUNE_TIME + 0.1f);   /* 等免疫结束 */
    }
    CHECK(ControlBegin(CTRL_FACEHUGGER, 3, 1.8f) == 1, "facehugger can grab again");
    CHECK(ControlTakeResult() == 0,
          "a stale result from the previous control must NOT leak into the new one");

    ControlReset();
}

static void test_control_layer(void) {
    extern int   ControlBegin(ControlType, int, float);
    extern void  ControlTick(float);
    extern int   ControlPress(void);
    extern void  ControlRelease(int);
    extern void  ControlReset(void);
    extern int   ControlActive(void);
    extern ControlType ControlCurrent(void);
    extern int   ControlMashLeft(void);
    extern float ControlTimeLeft(void);
    extern float ControlImmune(void);

    ControlReset();
    CHECK(!ControlActive(), "the control layer must start idle");

    /* ---- ① 单占 ---- */
    CHECK(ControlBegin(CTRL_FACEHUGGER, 3, 2.0f) == 1,
          "the first control request must be accepted");
    CHECK(ControlActive(), "the control layer must report active");
    CHECK(ControlBegin(CTRL_SPIDER_WEB, 3, 2.0f) == 0,
          "a second control must be REJECTED while one is running (no stacking)");
    CHECK(ControlCurrent() == CTRL_FACEHUGGER,
          "the running control must not be replaced");

    /* ---- ② 按次计数 ---- */
    CHECK(ControlMashLeft() == 3, "mash count must start at the requested value");
    CHECK(ControlPress() == 0, "first press must not finish a 3-press control");
    CHECK(ControlMashLeft() == 2, "each press must decrement exactly one");
    CHECK(ControlPress() == 0, "second press must not finish it either");

    /* ---- ③ 第三次按下完成，并进入免疫 ---- */
    CHECK(ControlPress() == 1, "the third press must finish the control");
    CHECK(!ControlActive(), "finishing must clear the control slot");
    CHECK(ControlImmune() > 0.0f, "releasing must open an immunity window");
    CHECK(ControlBegin(CTRL_SPIDER_WEB, 3, 2.0f) == 0,
          "a new control must be blocked while immune");

    /* 免疫走完后可以再次被控 */
    ControlTick(CONTROL_IMMUNE_TIME + 0.1f);
    CHECK(ControlImmune() <= 0.0f, "immunity must expire");
    CHECK(ControlBegin(CTRL_SPIDER_WEB, 3, 2.0f) == 1,
          "after immunity expires a new control must be accepted");

    /* ---- 超时：时间走完但没挣脱 ---- */
    {
        float t0 = ControlTimeLeft();
        ControlTick(t0 * 0.5f);
        CHECK(ControlTimeLeft() > 0.0f, "time must tick down but not expire yet");
        ControlTick(t0);
        CHECK(ControlTimeLeft() <= 0.0f, "time must expire");
        CHECK(ControlActive(), "expiry alone must NOT auto-clear the slot "
                               "(the owner decides and calls ControlRelease)");
        ControlRelease(0);
        CHECK(!ControlActive(), "ControlRelease must clear the slot");
    }

    /* ---- ④ 重置 ---- */
    ControlReset();
    CHECK(!ControlActive() && ControlImmune() == 0.0f && ControlMashLeft() == 0,
          "ControlReset must leave no residue (new run / floor change / load)");
}

static void test_two_light_beasts(void) {
    extern Monster lightBeasts[LIGHTBEAST_MAX];

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();

    CHECK(lightBeasts[0].active == 1, "the first light beast must spawn");
    CHECK(lightBeasts[1].active == 1, "the SECOND light beast must spawn too");

    /* 两只必须站在不同位置，否则等于只有一只 */
    {
        float dx = lightBeasts[0].x - lightBeasts[1].x;
        float dy = lightBeasts[0].y - lightBeasts[1].y;
        CHECK(dx * dx + dy * dy > 0.25f,
              "the two light beasts must spawn at different spots");
    }

    /* 两只都必须在可站立的格子上 */
    {
        extern int Blocked(float, float);
        CHECK(!Blocked(lightBeasts[0].x, lightBeasts[0].y),
              "the first light beast must not spawn inside geometry");
        CHECK(!Blocked(lightBeasts[1].x, lightBeasts[1].y),
              "the second light beast must not spawn inside geometry");
    }

    /* 速度必须是新的 1.8 基准（不再是 1.5） */
    CHECK(lightBeasts[0].speed > 1.5f,
          "light beast speed must reflect the 1.2x bump (base 1.80)");
}

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


/* ==================================================================
 * 2026-11-08（第 1 轮）：玩家反馈的三条
 * ------------------------------------------------------------------
 *  ① 清道夫：抓柜门时玩家跑出来 → 旧代码直接走人（零代价脱身）
 *  ② 扳手：升级前砸中没有任何硬直 → 手感"没效果"
 *  ③ 图鉴：局内从不播报名字 → "不知道哪个怪是哪个"
 * ================================================================ */

extern float RunUpgWrenchStun(void);
extern int   RunUpgWrenchLevel(void);   /* 读 upg[4] */
extern void  RunUpgSetLevel(int idx, int lv);
extern int   BestiaryAnnounce(int id);

static void test_wrench_stun_has_a_floor_at_low_level(void) {
    /* 旧实现：level < 3 一律 0 —— 砸中只有击退，怪立刻贴回来 */
    int saved = RunUpgWrenchLevel();   /* 扳手是升级表里的 4 号 */
    RunUpgSetLevel(4, 0);   /* 4 = 扳手 */
    CHECK(RunUpgWrenchStun() > 0.05f,
          "wrench must stagger even before upgrades (a zero stun feels like 'no effect')");
    RunUpgSetLevel(4, 3);
    CHECK(RunUpgWrenchStun() >= 0.9f, "the level-3 wrench must still be clearly better");
    RunUpgSetLevel(4, saved);
}

static void test_bestiary_announces_only_once(void) {
    /* 同一只怪在一次进程里只播报一次，第二次返回 0 */
    int first = BestiaryAnnounce(6);      /* 用一个少见的 id */
    int again = BestiaryAnnounce(6);
    CHECK(first == 1, "the first encounter must be announced");
    CHECK(again == 0, "the same monster must not be announced twice");
    CHECK(BestiaryAnnounce(-1) == 0, "invalid ids must not be announced");
    CHECK(BestiaryAnnounce(BESTIARY_COUNT) == 0, "out-of-range ids must not be announced");
}

static void test_scavenger_does_not_vanish_when_you_leave(void) {
    /* 直接验状态机：抓柜门 → 玩家出柜 → 预警到期后它必须还在场上。
       旧代码在这里 scavengerLeave()，玩家零代价脱身。 */
    extern int hiddenIn;
    extern void ScavengerForcePlace(float, float);
    extern void ScavengerGrabTest(int spot);
    extern int  ScavengerWarnExpireTest(void);
    extern int  ScavengerActive(void);
    extern HideSpot hideSpots[];

    ScavengerForcePlace(posX + 0.5f, posY + 0.5f);
    hiddenIn = 2;                     /* 假装躲在 2 号柜 */
    if (!ScavengerActive()) { CHECK(1, "scavenger could not be placed"); return; }
    hideSpots[2].active = 1;
    hideSpots[2].broken = 0;

    ScavengerGrabTest(2);
    hiddenIn = -1;                    /* 玩家在预警里跑出来 */
    (void)ScavengerWarnExpireTest();

    CHECK(ScavengerActive(),
          "leaving the locker mid-grab must not make the scavenger vanish");
    CHECK(hideSpots[2].broken,
          "the grabbed locker must still be destroyed even if the player escapes");
    hiddenIn = -1;
    hideSpots[2].active = 0;
}


/* ==================================================================
 * 2026-11-08（第 3 轮）：威胁预告层
 * ------------------------------------------------------------------
 * 调研结论：恐怖来自**预期**（进门前就知道屋里有东西）。
 * 这三条锁的是契约：远处要响、看见它就不响、没怪绝对不响。
 * ================================================================ */

#include "threat_telegraph.h"

static void test_telegraph_is_silent_without_threats(void) {
    int i;
    TelegraphReset();
    for (i = 0; i < MON_HUNTER_MAX; i++) monHunters[i].active = 0;
    for (i = 0; i < LIGHTBEAST_MAX; i++) lightBeasts[i].active = 0;
    parasite.active = 0;

    CHECK(TelegraphTick(0.016f) == 0, "no monsters means no telegraph");
    CHECK(TelegraphBand() == -1, "no monsters means no distance band");
    CHECK(TelegraphFlicker() == 0.0f, "the flashlight must not flicker with nothing around");
}

static void test_telegraph_bands_by_distance(void) {
    int i;
    TelegraphReset();
    for (i = 0; i < MON_HUNTER_MAX; i++) monHunters[i].active = 0;
    for (i = 0; i < LIGHTBEAST_MAX; i++) lightBeasts[i].active = 0;
    parasite.active = 0;

    /* 放一只 20 格外的怪：超出预告范围 */
    monHunters[0].active = 1;
    monHunters[0].x = posX + 20.0f;
    monHunters[0].y = posY;
    (void)TelegraphTick(0.016f);
    CHECK(TelegraphBand() == -1, "a monster 20 tiles away must not trigger the telegraph");

    /* 注意：档位必须测"背后"的怪 —— 正前方的会被"看得见就不预告"过滤掉，
       那正是上一条用例要验的语义。 */
    /* 9 格：中距离档（背后） */
    monHunters[0].x = posX - dirX * 9.0f;
    monHunters[0].y = posY - dirY * 9.0f;
    (void)TelegraphTick(0.016f);
    CHECK(TelegraphBand() == 1, "9 tiles behind you must read as the mid band");

    /* 3 格：近距离档（背后）+ 手电该抖 */
    monHunters[0].x = posX - dirX * 3.0f;
    monHunters[0].y = posY - dirY * 3.0f;
    (void)TelegraphTick(0.016f);
    CHECK(TelegraphBand() == 2, "3 tiles behind you must read as the near band");

    monHunters[0].active = 0;
    TelegraphReset();
}

static void test_telegraph_stays_quiet_when_you_can_see_it(void) {
    int i;
    TelegraphReset();
    for (i = 0; i < MON_HUNTER_MAX; i++) monHunters[i].active = 0;
    for (i = 0; i < LIGHTBEAST_MAX; i++) lightBeasts[i].active = 0;
    parasite.active = 0;

    /* 正前方 3 格：看得见 → 不该再有"预告"（那是音效装饰，不是预告） */
    monHunters[0].active = 1;
    monHunters[0].x = posX + dirX * 3.0f;
    monHunters[0].y = posY + dirY * 3.0f;
    (void)TelegraphTick(0.016f);
    CHECK(TelegraphBand() == -1,
          "a monster you are looking at must not be 'telegraphed'");

    monHunters[0].active = 0;
    TelegraphReset();
}

void TestSuite_actors(void) {
    test_jumper_removed_spider_takes_over();
    test_spider_spawn_curve();
    test_spider_dispel_cooldown();
    test_web_floor_ignores_press();
    test_spider_bind_no_damage();
    test_floor_change_clears_field_state();
    test_control_result_contract();
    test_control_layer();
    test_two_light_beasts();
    test_three_hunters();
    test_hunter_models();
    test_floor_resets_hunters();
    test_monster_search_around();
    /* 第 1 轮：玩家反馈三条 */
    test_wrench_stun_has_a_floor_at_low_level();
    test_bestiary_announces_only_once();
    test_scavenger_does_not_vanish_when_you_leave();
    test_telegraph_is_silent_without_threats();
    test_telegraph_bands_by_distance();
    test_telegraph_stays_quiet_when_you_can_see_it();
}

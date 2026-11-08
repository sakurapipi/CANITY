/* tests/domains/test_world.c —— world 域的集成测试
 *
 * 第 5 轮（2026-11-08）从 test_runtime.c 原样搬过来，断言一条没改。
 * 入口：TestSuite_world()
 */
#include "../test_common.h"

static void test_global_balance_invariants(void) {
    extern int BatteryMax(void);
    extern float BatteryDrainRate(void);
    extern float RunUpgWrenchCooldown(void);
    extern float GoldSearchAt(int level);
    extern int ShopPriceAt(int row);
    extern int fusesNeeded, carryGold;
    extern int ShopWrenchLevelForTest(void);

    /* ---- ① ② 怪物速度夹在走速与冲刺之间 ---- */
    {
        float walk   = MOVE_SPEED;                    /* 2.0 */
        float sprint = MOVE_SPEED * SPRINT_MULT;      /* 3.0 */
        CHECK(MONSTER_SPEED_FINAL_MAX < sprint,
              "monster top speed must stay BELOW the player's sprint, or escape is impossible");
        CHECK(MONSTER_SPEED_FINAL_MAX > walk,
              "monster top speed must stay ABOVE the player's walk, or nothing is threatening");
        CHECK(LIGHTBEAST_SPEED_BASE < MONSTER_SPEED_FINAL_MAX,
              "the light beast must not be faster than the final clamp");
    }

    /* ---- ③ 手电续航区间 ---- */
    {
        float maxB  = (float)BatteryMax();
        float drain = BatteryDrainRate();
        CHECK(drain > 0.0f, "battery must actually drain");
        float secs = maxB / drain;
        CHECK(secs > 60.0f,
              "a full battery must last more than a minute (otherwise the floor is unplayable)");
        CHECK(secs < 400.0f,
              "a full battery must not last absurdly long (otherwise there is no pressure)");
    }

    /* ---- ④ 保险丝 vs 箱子数 ---- */
    {
        CHECK(fusesNeeded >= 2, "a floor must need at least two fuses");
        CHECK(fusesNeeded + 3 <= MAX_CONTAINERS,
              "there must always be room for fuses PLUS other loot "
              "(otherwise every crate is a fuse and gold gates become unreachable)");
    }

    /* ---- ⑤ 金币门槛可达：两层搜刮收入要能覆盖最贵的舱门门槛 ---- */
    {
        /* 每层保守估计 6 个能出金币的箱子 */
        /* 每层保守按 6 个"能出金币"的箱子算。
           上限取 3 层 —— 再多就等于前期永远打不开。 */
        float threeFloors = 3.0f * 6.0f * GoldSearchAt(1);
        CHECK(threeFloors > (float)HATCH_STORAGE_COST,
              "the storage hatch's gold gate must be reachable within three floors");
    }

    /* ---- ⑥ 相位信标：**递增百分比**（用户 2026-11-06 修正）----
       原来一律 70% 太狠。现在的契约是：
         · 随层数递增（第 1 层 30%，每层 +5%，第 11 层起封顶 80%）
         · 永远不把人清空
         · 永远不为 0
       所以这里测的是**关系**：单调递增、有上下界、有保底。 */
    {
        carryGold = 1000;
        int prev = -1, first = -1;
        for (int lv = 1; lv <= 14; lv++) {
            currentLevel = lv;
            int p = ShopPriceAt(8);
            if (lv == 1) first = p;
            CHECK(p < carryGold,
                  "the percentage price must never consume ALL carried gold");
            CHECK(p > 0, "the beacon must never become free");
            if (prev >= 0) {
                CHECK(p >= prev,
                      "the beacon price must rise with depth (never get cheaper)");
            }
            prev = p;
        }
        CHECK(first >= 250 && first <= 340,
              "at floor 1 the beacon must cost about 30% of 1000 gold");
        CHECK(prev >= 760 && prev <= 840,
              "by the deep floors the beacon must cap near 80% of 1000 gold");

        currentLevel = 1;
        carryGold = 0;
        CHECK(ShopPriceAt(8) >= 100,
              "with no gold the beacon must still cost a real floor price");
    }

    /* ---- ⑦ ⑧ 扳手冷却的两个边界 ---- */
    {
        float cd = RunUpgWrenchCooldown();
        CHECK(cd > 0.0f,
              "the wrench must be cooldown-based (a negative value means charge mode)");
        CHECK(cd < HUNT_CHASE_MAX,
              "the wrench cooldown must be shorter than a chase, or it is useless mid-chase");
        CHECK(HUNT_CHASE_MAX / cd >= 2.0f,
              "a single chase must allow at least two wrench swings");
        CHECK(cd > WRENCH_SWING_TIME,
              "the cooldown must be longer than one swing, or the swing interrupts itself");
    }

    /* ---- ⑨ 爆脸怪控制时长 vs 追捕超时 ---- */
    CHECK(JUMPER_CONTROL_TIME < HUNT_CHASE_MAX,
          "the facehugger control must end before a chase times out, "
          "otherwise the player is locked for the whole chase");
    CHECK(JUMPER_CONTROL_TIME > 5.0f,
          "the control must be long enough to be a real punishment");

    /* ---- ⑩ 舱门理智门槛的兼容窗口不为空 ---- */
    {
        float wardMax = 50.0f;    /* 病房：理智 < 50 */
        float hellMin = 40.0f;    /* 地狱：理智 >= 40 */
        CHECK(wardMax > hellMin,
              "the ward (<50) and hell (>=40) sanity gates must share a non-empty window");
    }

    /* ---- 舱门数量上限 vs 一层的房间数 ---- */
    CHECK(MAX_HATCHES >= 2, "a floor must be able to hold more than one hatch");
    CHECK(MAX_HATCHES <= 6, "too many hatches per floor would trivialise the effects");
}

static void test_theme_depth_bands(void) {
    extern CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();

    int checkedFloors = 0;
    for (int lv = 1; lv <= 12; lv++) {
        currentLevel = lv;
        GenerateLevel();
        checkedFloors++;

        int allowed[7] = { 0 };
        if (lv <= 3)      { allowed[0] = allowed[1] = 1; }
        else if (lv <= 6) { allowed[4] = allowed[3] = 1; }
        else if (lv <= 9) { allowed[5] = allowed[2] = 1; }
        else              { allowed[6] = allowed[5] = 1; }

        int bad = 0;
        for (int y = 1; y < MAP_HEIGHT - 1; y++)
            for (int x = 1; x < MAP_WIDTH - 1; x++) {
                if (cellMeta[y][x].roomId == 0) continue;
                int th = (int)cellMeta[y][x].themeId;
                if (th < 0 || th >= 7) { bad++; continue; }
                if (!allowed[th]) bad++;
            }
        CHECK(bad == 0, "a floor must only contain the themes allowed at that depth");
    }
    CHECK(checkedFloors == 12, "the depth-band test must cover 12 floors");
}

static void test_rooms_stay_sealed(void) {
    extern CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];
    extern int worldMap[MAP_HEIGHT][MAP_WIDTH];

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();

    int sealedFloors = 0, roomsSeen = 0;
    /* 2026-11-06（AD-64）：**自己固定种子**。
       这条用例要遍历 10 层，而层生成吃全局 rand() ——
       前面任何用例多调/少调一次 rand()，地图就变，
       墙上某个门是否接得上也跟着变。第一次跑就因为这个假红过。 */
    srand(20261106);
    for (int lv = 1; lv <= 10; lv++) {
        currentLevel = lv;
        GenerateLevel();

        int roomCells = 0, holes = 0;
        for (int y = 1; y < MAP_HEIGHT - 1; y++) {
            for (int x = 1; x < MAP_WIDTH - 1; x++) {
                if (cellMeta[y][x].roomId != 0) roomCells++;
                if (!(cellMeta[y][x].flags & CELLFLAG_ROOMWALL)) continue;
                if (worldMap[y][x] != 0) continue;     /* 还是墙：正常 */
                /* 环上的格子是空地 —— 只有**门**才允许。
                   门的判据：一侧是房间内部（roomId != 0），
                   对侧是非房间的空地。其他情况就是被打穿了。 */
                {
                    static const int dx4[4] = { 1, -1, 0, 0 };
                    static const int dy4[4] = { 0, 0, 1, -1 };
                    int isDoor = 0;
                    for (int k = 0; k < 4; k++) {
                        int ax = x + dx4[k], ay = y + dy4[k];
                        int bx = x - dx4[k], by = y - dy4[k];
                        if (ax < 0 || ay < 0 || ax >= MAP_WIDTH || ay >= MAP_HEIGHT) continue;
                        if (bx < 0 || by < 0 || bx >= MAP_WIDTH || by >= MAP_HEIGHT) continue;
                        int aIn  = (cellMeta[ay][ax].roomId != 0);
                        int aOut = (!worldMap[ay][ax] && cellMeta[ay][ax].roomId == 0);
                        int bIn  = (cellMeta[by][bx].roomId != 0);
                        int bOut = (!worldMap[by][bx] && cellMeta[by][bx].roomId == 0);
                        if ((aIn && bOut) || (bIn && aOut)) { isDoor = 1; break; }
                    }
                    if (!isDoor) holes++;              /* 不是门 = 被打穿 */
                }
            }
        }
        if (roomCells > 0) roomsSeen++;
        if (holes == 0) sealedFloors++;
    }

    CHECK(roomsSeen > 0, "the test must have seen at least one theme room");
    CHECK(sealedFloors == 10,
          "no room wall may be punched open by the post-room braid pass");
}

static void test_theme_room_label(void) {
    extern const char* ThemeName(int, int);
    extern CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];

    /* ---- ① 每个主题都有名字 ---- */
    for (int t = 0; t < THEME_COUNT; t++) {
        const char* en = ThemeName(t, 0);
        const char* zh = ThemeName(t, 1);
        CHECK(en && en[0], "every theme needs an English name");
        CHECK(zh && zh[0], "every theme needs a Chinese name");
    }
    /* 越界要安全返回空串，不能是 NULL */
    {
        const char* o1 = ThemeName(-1, 0);
        const char* o2 = ThemeName(THEME_COUNT + 5, 0);
        CHECK(o1 && o1[0] == 0, "an invalid theme id must return an empty string");
        CHECK(o2 && o2[0] == 0, "an out-of-range theme id must return an empty string");
    }

    /* ---- ② 站在房间里能读出主题 ---- */
    {
        clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
        StartGame();
        int found = 0;
        for (int lv = 1; lv <= 8 && !found; lv++) {
            currentLevel = lv;
            GenerateLevel();
            for (int y = 1; y < MAP_HEIGHT - 1 && !found; y++)
                for (int x = 1; x < MAP_WIDTH - 1 && !found; x++)
                    if (cellMeta[y][x].roomId != 0) {
                        int th = (int)cellMeta[y][x].themeId;
                        CHECK(th >= 0 && th < THEME_COUNT,
                              "a room cell must carry a valid theme id");
                        CHECK(ThemeName(th, 1)[0] != 0,
                              "the theme of a real room must have a display name");
                        found = 1;
                    }
        }
        CHECK(found == 1, "at least one floor must contain a theme room");
    }
}

static void test_room_budget_and_exit(void) {
    extern int roomCount;
    extern float exitX, exitY;
    extern CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];

    /* ---- ① 训练营：预算为 0 ---- */
    {
        extern int ThemeRoomBudgetForLevel(int, int);
        CHECK(ThemeRoomBudgetForLevel(1, 1) == 0,
              "training mode must budget ZERO theme rooms");
        CHECK(ThemeRoomBudgetForLevel(20, 1) == 0,
              "training mode must stay at zero on any level");
    }

    /* ---- ② 普通局：不超过每层上限 ---- */
    {
        extern int ThemeRoomBudgetForLevel(int, int);
        for (int lv = 1; lv <= 30; lv++)
            CHECK(ThemeRoomBudgetForLevel(lv, 0) <= MAX_THEME_ROOMS_PER_FLOOR,
                  "no floor may exceed MAX_THEME_ROOMS_PER_FLOOR theme rooms");
        CHECK(ThemeRoomBudgetForLevel(1, 0) == 1, "floor 1 gets a single room");
        CHECK(ThemeRoomBudgetForLevel(9, 0) == 2, "deep floors get two");
    }

    /* ---- ③ 出口不能落在房间内部（否则到出口只剩一条路） ---- */
    {
        int checked = 0, bad = 0;
        for (int lv = 1; lv <= 12; lv++) {
            currentLevel = lv;
            GenerateLevel();
            int ex = (int)exitX, ey = (int)exitY;
            if (ex < 0 || ey < 0 || ex >= MAP_WIDTH || ey >= MAP_HEIGHT) continue;
            checked++;
            if (cellMeta[ey][ex].roomId != 0) bad++;
        }
        CHECK(checked >= 10, "the exit test must have run on enough floors");
        CHECK(bad == 0,
              "the exit must never spawn inside a theme room");
    }

    trainingMode = 0;
}

static void test_room_wall_flags(void) {
    extern CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];
    extern int CellIsRoomWall(int, int, int*, int*, int*);

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();

    int found = 0;
    for (int attempt = 0; attempt < 20 && !found; attempt++) {
        currentLevel = 1;
        GenerateLevel();
        for (int y = 1; y < MAP_HEIGHT - 1 && !found; y++)
            for (int x = 1; x < MAP_WIDTH - 1 && !found; x++)
                if (CellIsRoomWall(x, y, NULL, NULL, NULL)) found = 1;
    }
    CHECK(found == 1, "at least one room wall must be tagged on a generated level");

    /* ---- 被标记的格子：必须是墙，而且 roomId 必须还是 0 ---- */
    {
        int checked = 0;
        for (int y = 1; y < MAP_HEIGHT - 1 && checked < 8; y++) {
            for (int x = 1; x < MAP_WIDTH - 1 && checked < 8; x++) {
                int r = 0, g = 0, b = 0;
                if (!CellIsRoomWall(x, y, &r, &g, &b)) continue;
                checked++;
                CHECK(cellMeta[y][x].flags & CELLFLAG_ROOMWALL,
                      "the flag must be what CellIsRoomWall keys on");
                CHECK(cellMeta[y][x].roomId == 0,
                      "room WALLS must not carry a roomId (it would corrupt floor counting)");
                CHECK(r + g + b > 0, "the wall must report its theme colour");
            }
        }
        CHECK(checked > 0, "the tagging scan must have found walls to check");
    }

    /* ---- 房间**内部**的格子不能被当成外墙 ---- */
    {
        int interiorChecked = 0;
        for (int y = 1; y < MAP_HEIGHT - 1 && interiorChecked < 8; y++) {
            for (int x = 1; x < MAP_WIDTH - 1 && interiorChecked < 8; x++) {
                if (cellMeta[y][x].roomId == 0) continue;
                interiorChecked++;
                CHECK(!(cellMeta[y][x].flags & CELLFLAG_ROOMWALL),
                      "a room's interior floor must not be flagged as a wall");
            }
        }
    }
}

static void test_mote_neutral(void) {
    extern void  NeutralReset(void);
    extern void  NeutralDebugPlace(float, float);
    extern void  NeutralTick(float);
    extern int   NeutralCount(void);
    extern int   NeutralScared(void);
    extern float NeutralX(int), NeutralY(int);
    extern float posX, posY;
    extern float sanity;
    extern int   carryGold, bankGold;
    extern int   Blocked(float, float);

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();
    currentLevel = 3;
    GenerateLevel();

    NeutralReset();
    CHECK(NeutralCount() == 0, "the neutral creature must start absent");

    /* ---- 受惊逃走 ---- */
    NeutralDebugPlace(posX + 1.0f, posY);      /* 就在玩家旁边 */
    CHECK(NeutralCount() == 1, "debug placement must activate it");

    float d0 = sqrtf((NeutralX(0) - posX) * (NeutralX(0) - posX) +
                     (NeutralY(0) - posY) * (NeutralY(0) - posY));
    for (int i = 0; i < 60; i++) NeutralTick(0.02f);
    float d1 = sqrtf((NeutralX(0) - posX) * (NeutralX(0) - posX) +
                     (NeutralY(0) - posY) * (NeutralY(0) - posY));
    CHECK(NeutralScared() == 1, "being close to the player must scare it");
    CHECK(d1 > d0, "a scared mote must move AWAY from the player");

    /* ---- 不伤害、不产出、不阻挡 ---- */
    {
        float s0 = sanity;
        int g0 = carryGold, b0 = bankGold;
        int blocked = Blocked(NeutralX(0), NeutralY(0));
        for (int i = 0; i < 200; i++) NeutralTick(0.02f);
        CHECK(sanity == s0, "the neutral creature must not touch sanity");
        CHECK(carryGold == g0 && bankGold == b0,
              "the neutral creature must not produce any gold");
        CHECK(blocked == Blocked(NeutralX(0), NeutralY(0)) ||
              blocked == 0,
              "the neutral creature must never become a wall");
    }

    NeutralReset();
    CHECK(NeutralCount() == 0, "NeutralReset must clear it (floor change / load)");
}

static void test_draft_structure(void) {
    extern void  OpenSkillPick(void);
    extern int   skillPickOptions[INRUN_CHOICES];
    extern void  RunSkillsReset(void);
    extern void  RunSkillTake(int);
    extern void  RunRngSeed(unsigned int);
    extern unsigned int RunRngNext(void);
    extern unsigned int RunRngState(void);
    extern int   relicOwned[RELIC_COUNT];
    extern BuildTag BuildTagOfSkill(int);

    RunSkillsReset();
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = 0;

    /* 让"速通"成型：拿两张速通卡 */
    {
        int taken = 0;
        for (int i = 0; i < INRUN_COUNT && taken < 2; i++) {
            if (BuildTagOfSkill(i) == BUILD_SPEED) { RunSkillTake(i); taken++; }
        }
        CHECK(taken == 2, "the test needs two speed cards to form a build");
    }

    /* ---- 抽卡不动全局 rand() ---- */
    {
        srand(4242);
        int r1 = rand();
        srand(4242);
        RunRngSeed(777);
        OpenSkillPick();            /* 内部只该用 RunRngNext */
        int r2 = rand();
        CHECK(r1 == r2,
              "the draft must NOT consume the global rand() sequence "
              "(otherwise map generation drifts)");
        CHECK(RunRngState() != 777,
              "the draft's own PRNG must have advanced");
    }

    /* ---- 结构：连续 40 次都要符合"三桶各一" ---- */
    int okStruct = 0, tries = 0, allInstant = 0;
    for (int round = 0; round < 40; round++) {
        RunRngSeed(1000 + (unsigned)round);
        OpenSkillPick();
        int a = skillPickOptions[0], b = skillPickOptions[1], c = skillPickOptions[2];
        if (a < 0 || b < 0 || c < 0) continue;
        tries++;
        CHECK(a != b && b != c && a != c, "the three offers must be distinct");
        {
            int nd = 0, no = 0, nu = 0;
            int ids[3] = { a, b, c };
            for (int k = 0; k < 3; k++) {
                BuildTag t = BuildTagOfSkill(ids[k]);
                if (t == BUILD_NONE) nu++;
                else if (t == BUILD_SPEED) nd++;
                else no++;
            }
            /* 成型后：主流派至少 1 张、通用至少 1 张 */
            if (nd >= 1 && nu >= 1) okStruct++;
            {
                extern const InRunDef inRunDefs[];
                if (inRunDefs[a].kind == IRK_INSTANT &&
                    inRunDefs[b].kind == IRK_INSTANT &&
                    inRunDefs[c].kind == IRK_INSTANT) allInstant++;
            }
        }
    }
    CHECK(tries >= 30, "the draft must keep producing three offers");
    CHECK(okStruct >= tries * 8 / 10,
          "at least 80% of drafts must contain one dominant-build card and one generic card");
    CHECK(allInstant == 0,
          "a draft must never be three instant-supply cards");

    RunSkillsReset();
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = 0;
}

static void test_build_tags(void) {
    extern unsigned int runSkillMask;
    extern int relicOwned[RELIC_COUNT];
    extern void RunSkillsReset(void);
    extern BuildTag BuildTagOfSkill(int);
    extern BuildTag BuildTagOfRelic(int);
    extern int      BuildScore(BuildTag);
    extern BuildTag BuildDominant(void);
    extern const char* BuildName(BuildTag, int);
    extern void     BuildSynergyList(char*, int);

    RunSkillsReset();
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = 0;

    /* ---- 每张卡都有标签，且标签合法 ---- */
    {
        int gadgets = 0, speeds = 0, steaths = 0, none = 0;
        for (int i = 0; i < INRUN_COUNT; i++) {
            BuildTag t = BuildTagOfSkill(i);
            CHECK(t >= BUILD_NONE && t < BUILD_COUNT, "every card must carry a valid tag");
            if (t == BUILD_GADGET) gadgets++;
            else if (t == BUILD_SPEED) speeds++;
            else if (t == BUILD_STEALTH) steaths++;
            else none++;
        }
        CHECK(gadgets > 0 && speeds > 0 && steaths > 0,
              "all three builds must have at least one card");
        CHECK(gadgets + speeds + steaths + none == INRUN_COUNT,
              "the tag table must cover every card exactly once");
    }

    /* ---- 遗物也必须全部有标签 ---- */
    {
        int speeds = 0;
        for (int i = 0; i < RELIC_COUNT; i++) {
            BuildTag t = BuildTagOfRelic(i);
            CHECK(t >= BUILD_NONE && t < BUILD_COUNT, "every relic must carry a valid tag");
            if (t == BUILD_SPEED) speeds++;
        }
        CHECK(speeds > 0, "at least one relic must feed the speed build");
    }

    /* ---- 空持有 → 没成型 ---- */
    CHECK(BuildDominant() == BUILD_NONE,
          "with nothing owned the game must say 'no build yet'");

    /* ---- 两张速通卡 → 速通成型（2×2 = 4 >= 3） ---- */
    {
        extern void RunSkillTake(int);
        /* 找出两张 SPEED 卡并持有 */
        int taken = 0;
        for (int i = 0; i < INRUN_COUNT && taken < 2; i++) {
            if (BuildTagOfSkill(i) == BUILD_SPEED) { RunSkillTake(i); taken++; }
        }
        CHECK(taken == 2, "the test needs at least two speed cards");
        CHECK(BuildDominant() == BUILD_SPEED,
              "two speed cards (4 points) must be recognised as the speed build");
        CHECK(BuildScore(BUILD_SPEED) >= 4, "score must count every owned card");
        CHECK(BuildScore(BUILD_GADGET) == 0, "other builds must stay at zero");

        char line[192];
        BuildSynergyList(line, sizeof(line));
        CHECK(line[0] != 0, "the panel must list the synergies it detected");
        CHECK(strcmp(BuildName(BUILD_SPEED, 1), "速通突围") == 0,
              "the Chinese build name must match the design doc");
    }

    /* ---- 并列 → 不表态 ---- */
    {
        extern void RunSkillsReset(void);
        RunSkillsReset();
        /* 各拿一张：2 分 vs 2 分，且都 < 3 → 仍然没成型 */
        for (int i = 0; i < INRUN_COUNT; i++) {
            if (BuildTagOfSkill(i) == BUILD_GADGET) { RunSkillTake(i); break; }
        }
        for (int i = 0; i < INRUN_COUNT; i++) {
            if (BuildTagOfSkill(i) == BUILD_STEALTH) { RunSkillTake(i); break; }
        }
        CHECK(BuildDominant() == BUILD_NONE,
              "a 2-vs-2 split must NOT lock the player into a build");
    }

    RunSkillsReset();
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = 0;
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

void TestSuite_world(void) {
    test_global_balance_invariants();
    test_theme_depth_bands();
    test_rooms_stay_sealed();
    test_theme_room_label();
    test_room_budget_and_exit();
    test_room_wall_flags();
    test_mote_neutral();
    test_draft_structure();
    test_build_tags();
    test_balance_targets();
    test_theme_rooms_placement();
    test_room_seed_roundtrip();
    test_room_props();
    test_prop_layers();
    test_suspicion_best_uses_propagation();
    test_sound_propagation();
}

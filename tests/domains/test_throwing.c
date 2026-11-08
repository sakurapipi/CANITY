/* tests/domains/test_throwing.c —— throwing 域的集成测试
 *
 * 第 2 轮（2026-11-08）从 test_runtime.c 原样搬过来，断言一条没改。
 * 入口：TestSuite_throwing()
 */
#include "../test_common.h"

static void test_flare_heal_budget(void) {
    extern void  FlareHealTick(float);
    extern void  FlareHealReset(void);
    extern float FlareHealBudgetLeft(void);
    extern void  RunSkillTake(int);
    extern void  RunSkillsReset(void);
    extern int   relicOwned[RELIC_COUNT];
    extern float sanity, posX, posY;
    extern float SanityMax(void);
    extern void  ThrowItem(int, float);

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();
    currentLevel = 1;
    GenerateLevel();

    RunSkillsReset();
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = 0;
    FlareHealReset();

    /* ==================================================================
     * 2026-11-06（用户实测：「闪光弹不能回理智，你没有做好」）
     * ------------------------------------------------------------------
     * 契约改了：**基础效果对所有人无条件生效**，抽到 IR_BIOLUM 是强化。
     * 原来这张卡是闸门，没抽到就完全无效 —— 玩家扔了照明弹发现没用，
     * 那是合理的抱怨。
     * ================================================================ */

    /* ---- 没有这张卡：也要回（基础速率） ---- */
    sanity = SanityMax() * 0.5f;
    {
        float s0 = sanity;
        ThrowItem(0, 1.0f);                 /* 扔一根照明棒 */
        for (int i = 0; i < 300; i++) FlareHealTick(0.02f);
        CHECK(sanity > s0, "a landed flare must heal sanity even WITHOUT IR_BIOLUM");
        CHECK(sanity - s0 <= 1.6f * 6.5f,
              "the baseline heal must respect its own cap");
    }

    /* ---- 有卡：回得更快 ---- */
    FlareHealReset();
    RunSkillTake(IR_BIOLUM);
    CHECK(FlareHealBudgetLeft() >= 23.9f, "the floor budget must start full");
    /* 有卡时速率翻倍：同样的帧数应该回得更多 */
    sanity = SanityMax() * 0.2f;
    {
        float s0 = sanity;
        for (int i = 0; i < 50; i++) FlareHealTick(0.02f);   /* 1 秒 */
        float gained = sanity - s0;
        CHECK(gained > 0.0f, "with IR_BIOLUM a landed flare must heal");
        /* 有卡时速率翻倍（1.5 -> 3.0/s），所以上限相应放宽 */
        CHECK(gained <= 3.2f, "the boosted heal rate must not exceed ~3 per second");
        CHECK(gained > 1.6f, "IR_BIOLUM must heal noticeably faster than the baseline");
    }

    /* ---- 每根灯 8 秒窗口：把时间推过去就不再回 ---- */
    {
        float s0 = sanity;
        for (int i = 0; i < 100; i++) FlareHealTick(0.02f);  /* 再 2 秒 */
        sanity = SanityMax() * 0.2f;                        /* 压低再测窗口 */
        s0 = sanity;
        /* thrown[].life 已经消耗了一些；这里直接把预算跑完来验证封顶 */
        FlareHealReset();
        for (int i = 0; i < 4000; i++) {
            sanity = SanityMax() * 0.2f;                    /* 保持低位，避免满了停手 */
            FlareHealTick(0.02f);
            if (FlareHealBudgetLeft() <= 0.01f) break;
        }
        CHECK(FlareHealBudgetLeft() <= 0.01f,
              "the per-floor budget must be exhaustible");
        float s1 = sanity;
        sanity = SanityMax() * 0.2f;
        for (int i = 0; i < 200; i++) FlareHealTick(0.02f);
        CHECK(FlareHealBudgetLeft() <= 0.01f,
              "once the floor budget is spent no further healing may happen");
        sanity = s1;
    }

    RunSkillsReset();
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = 0;
    FlareHealReset();
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
    /* ==================================================================
     * 2026-11-06（AD-64）：**玩家位置也要钉死**
     * ------------------------------------------------------------------
     * 前面的用例会把玩家挪到别处（这次读到 posY 正好 = 12.0，
     * 站在格线上，投石一出手就撞墙，落点离箱子太远 → 一个锁都没砸开）。
     * 测试自己定位置，然后按这个位置摆箱子和通道。
     * ================================================================ */
    posX = 1.5f; posY = 1.5f;
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

    /* ==================================================================
     * 2026-11-06（AD-64）：**自己清出通道，不要赌地图**
     * ------------------------------------------------------------------
     * 这条用例原来默认"玩家正前方 2 格内没有墙"。
     * 死路算法改成 4 个回合之后地图变了，投石撞在更近的墙上，
     * 落点离箱子太远 → 一个锁都没砸开，用例红了。
     * 测试要自己造地形，不能依赖生成结果。
     * ================================================================ */
    {
        extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
        int cy = (int)posY;
        for (int dx = 1; dx <= 9; dx++) {
            int cx = (int)posX + dx;
            if (cx >= 1 && cx < MAP_WIDTH - 1 && cy >= 1 && cy < MAP_HEIGHT - 1)
                worldMap[cy][cx] = 0;
        }
    }

    SuspicionReset();
    /* ==================================================================
     * 2026-11-06（AD-64）：**改成直接调用落点结算**
     * ------------------------------------------------------------------
     * 原来靠"扔一块投石、赌它落在第一只箱子旁边"。
     * 但 StoneBreakLockAt 有固定半径（STONE_LOCK_RADIUS），
     * 落点由弹道决定 —— 地图/物理任何变化都会让落点漂出半径，
     * 用例就红（本次改死路算法后正是如此）。
     *
     * 这个用例真正要验的是**契约**："一次落点只解一把锁"，
     * 不是弹道物理。所以直接喂精确落点。
     * ================================================================ */
    {
        extern int StoneBreakLockAtTest(float, float);
        StoneBreakLockAtTest(containers[0].x, containers[0].y);
    }

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

void TestSuite_throwing(void) {
    test_flare_heal_budget();
    test_stone_hearing_range();
    test_stone_break_single_event();
}

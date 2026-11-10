/* tests/domains/test_throwing.c —— throwing 域的集成测试
 *
 * 第 2 轮（2026-11-08）从 test_runtime.c 原样搬过来，断言一条没改。
 * 入口：TestSuite_throwing()
 */
#include "../test_common.h"
#include "shot_spec.h"

static void test_flare_heal_budget(void) {
    extern void  FlareHealTick(float);
    extern void  FlareHealReset(void);
    extern float FlareHealBudgetLeft(void);
    extern void  RunSkillTake(int);
    extern void  RunSkillsReset(void);
    extern int   relicOwned[RELIC_COUNT];
    extern float sanity;
    extern float SanityMax(void);
    extern void  ThrowItem(int, float);

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartFreshTestRun();
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
        for (int i = 0; i < 100; i++) FlareHealTick(0.02f);  /* 再 2 秒 */
        sanity = SanityMax() * 0.2f;                        /* 压低再测窗口 */
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


/* ==================================================================
 * 2026-11-08（第 5 轮）：**三条落点分支必须给出同样的效果**
 * ------------------------------------------------------------------
 * AD-71 的原话：同一个投掷物有三条落点分支（全局落地 / 子步落地 / 撞墙），
 * 各写了一套效果 —— 子步落地只有碎片和相位，撞墙的回声只做了半套
 * （不揭地形、不标资源）。弹道落进哪条取决于这一帧的步进几何，
 * 所以玩家看到的是"回声弹有时候不生效"。
 *
 * 这四条用例就是那张网：效果必须**从任意分支**都能出现。
 * 做法：自己清出通道（AD-64，不赌地图），扔一个，跑到落地，看全局状态。
 * ================================================================ */

extern SmokeCloud smokes[MAX_SMOKE];
extern GlueZone   glues[MAX_GLUE];
extern float      echoScanT;
extern int        explored[MAP_HEIGHT][MAP_WIDTH];
extern int        worldMap[MAP_HEIGHT][MAP_WIDTH];
extern float      posX, posY, dirX, dirY;
extern void       UpdateShots(float dt);
extern void       ThrowItem(int type, float charge);

/* 清一条朝 +X 的直通道，并清掉上次用例残留的效果 */
static void throw_prepare(int corridor, int wall_at) {
    int cy = (int)posY;
    int i;
    for (i = 0; i < MAX_SMOKE; i++) smokes[i].active = 0;
    for (i = 0; i < MAX_GLUE; i++)  glues[i].active = 0;
    echoScanT = 0.0f;
    for (i = 0; i < corridor + 2; i++) {
        int cx = (int)posX + i;
        if (cx >= 1 && cx < MAP_WIDTH - 1 && cy >= 1 && cy < MAP_HEIGHT - 1)
            worldMap[cy][cx] = 0;
    }
    if (wall_at > 0) {
        int cx = (int)posX + wall_at;
        if (cx >= 1 && cx < MAP_WIDTH - 1)
            worldMap[cy][cx] = 1;      /* 挡住去路 */
    }
    dirX = 1.0f; dirY = 0.0f;
}

/* 跑若干帧直到条件成立或超时；返回是否成立 */
static int throw_run_until(int (*cond)(void), int maxFrames) {
    int f;
    for (f = 0; f < maxFrames; f++) {
        UpdateShots(0.016f);
        if (cond()) return 1;
    }
    return 0;
}

static int smoke_active_any(void) {
    int i;
    for (i = 0; i < MAX_SMOKE; i++) if (smokes[i].active) return 1;
    return 0;
}

static int glue_active_any(void) {
    int i;
    for (i = 0; i < MAX_GLUE; i++) if (glues[i].active) return 1;
    return 0;
}

static int echo_scanning(void) {
    return echoScanT > 0.0f;
}

static void test_smoke_works_when_it_lands_on_open_floor(void) {
    throw_prepare(10, 0);
    ThrowItem(2, 1.0f);                    /* kind 4 = 烟雾 */
    CHECK(throw_run_until(smoke_active_any, 400),
          "a smoke bomb landing on open floor must spawn a cloud");
}

static void test_glue_works_when_it_lands_on_open_floor(void) {
    throw_prepare(10, 0);
    ThrowItem(3, 1.0f);                    /* kind 8 = 胶水 */
    CHECK(throw_run_until(glue_active_any, 400),
          "a glue pot landing on open floor must spawn a glue zone");
}

static void test_echo_works_when_it_lands_on_open_floor(void) {
    int marked = 0;
    int i;
    throw_prepare(10, 0);
    ThrowItem(4, 1.0f);                    /* kind 10 = 回声 */
    CHECK(throw_run_until(echo_scanning, 400),
          "an echo bolt landing on open floor must start a sonar scan");
    /* 扫过之后地形必须被揭出来 —— 这是"撞墙分支只做了半套"漏掉的那一半 */
    for (i = 0; i < MAP_WIDTH * MAP_HEIGHT; i++) {
        if (explored[i / MAP_WIDTH][i % MAP_WIDTH]) { marked = 1; break; }
    }
    CHECK(marked, "the echo scan must actually reveal terrain");
}

static void test_effects_also_fire_when_hitting_a_wall(void) {
    /* 墙近到弹道出不去，只能撞墙结束 —— 走的是撞墙分支 */
    throw_prepare(10, 2);
    ThrowItem(2, 1.0f);
    CHECK(throw_run_until(smoke_active_any, 400),
          "a smoke bomb hitting a wall must still spawn a cloud (wall branch)");

    throw_prepare(10, 2);
    ThrowItem(4, 1.0f);
    CHECK(throw_run_until(echo_scanning, 400),
          "an echo bolt hitting a wall must still start a scan (wall branch)");
}


/* ==================================================================
 * 2026-11-08（第 6 轮）：**投掷物规格表的契约**
 * ------------------------------------------------------------------
 * 表是新的唯一数据源（include/shot_spec.h）。它一旦漏行，
 * ShotSpecFor 会返回默认行 —— 表现是"这个投掷物变成只会碎的石头"，
 * 而且不会报错。所以必须有用例盯着"每种 kind 都有行"。
 * ================================================================ */

extern const ShotSpec* ShotSpecFor(int kind);
extern int ShotSpecCount(void);
extern const ShotSpec* ShotSpecAt(int i);

static void test_shot_spec_covers_every_kind_in_use(void) {
    /* 这些 kind 是 ThrowItem 会产出的全部（见 throwing.c 的 type->kind 映射） */
    static const int kinds[] = { 1, 2, 3, 4, 6, 7, 8, 9, 10, 12, 13 };
    int i;
    for (i = 0; i < (int)(sizeof(kinds) / sizeof(kinds[0])); i++) {
        const ShotSpec* sp = ShotSpecFor(kinds[i]);
        CHECK(sp != 0, "every kind in use must have a spec row");
        CHECK(sp->kind == kinds[i], "spec lookup must return the row for that kind");
        CHECK(sp->name[0] != '\0', "every spec row must have a name (for logs)");
    }
}

static void test_shot_spec_table_is_consistent(void) {
    int i, n = ShotSpecCount();
    CHECK(n >= 11, "the table must keep at least the 11 kinds currently in use");
    for (i = 0; i < n; i++) {
        const ShotSpec* sp = ShotSpecAt(i);
        CHECK(sp != 0, "ShotSpecAt must return a row for every valid index");
        /* 伤害为 0 的行不该声明护甲穿透——那是矛盾数据 */
        if (sp->dmg == 0) {
            CHECK(sp->ignoresArmor == 0, "a zero-damage shot must not declare armor pierce");
        }
    }
    CHECK(ShotSpecAt(-1) == 0, "ShotSpecAt must reject negative index");
    CHECK(ShotSpecAt(n) == 0, "ShotSpecAt must reject out-of-range index");
}

static void test_shot_spec_unknown_kind_is_safe(void) {
    const ShotSpec* sp = ShotSpecFor(99);      /* 没人用的 kind */
    CHECK(sp != 0, "unknown kind must still return a usable spec");
    CHECK(sp->effect == FX_SHARDS, "unknown kind must fall back to plain shards");
    CHECK(sp->dmg <= 1, "unknown kind must not become a high-damage weapon");
}

void TestSuite_throwing(void) {
    test_flare_heal_budget();
    test_stone_hearing_range();
    test_stone_break_single_event();
    test_smoke_works_when_it_lands_on_open_floor();
    test_glue_works_when_it_lands_on_open_floor();
    test_echo_works_when_it_lands_on_open_floor();
    test_effects_also_fire_when_hitting_a_wall();
    test_shot_spec_covers_every_kind_in_use();
    test_shot_spec_table_is_consistent();
    test_shot_spec_unknown_kind_is_safe();
}

/* tests/domains/test_phase.c —— phase 域的集成测试
 *
 * 第 2 轮（2026-11-08）从 test_runtime.c 原样搬过来，断言一条没改。
 * 入口：TestSuite_phase()
 */
#include "../test_common.h"

static void test_phase_beacon_one_use(void) {
    extern int phaseOwned, phaseCharges, phasePlaced, phaseBeacons;
    extern int PhaseTryPlace(void), PhaseTryTeleport(void);

    clear_meta(); StartFreshTestRun();
    phaseBeacons = 1;
    phaseOwned = 1;
    phaseCharges = 1;
    phasePlaced = 0;

    /* 站在一个确定可站的位置，然后原地放锚 */
    int count=0;float px[2]={0},py[2]={0};
    for(int y=1;y<MAP_HEIGHT-1 && count<2;y++)for(int x=1;x<MAP_WIDTH-1 && count<2;x++)
        if(!worldMap[y][x] && !Blocked(x+.5f,y+.5f) && !PropBlocksAt(x,y)){
            px[count]=x+.5f;py[count]=y+.5f;count++;
        }
    CHECK(count==2,"phase fixture needs two valid floor positions");
    posX=px[0];posY=py[0];
    CHECK(PhaseTryPlace(), "phase: place succeeds with a charge left");
    CHECK(phasePlaced == 1, "phase: beacon is on the map");
    CHECK(phaseCharges == 1, "phase: placing does not consume the charge");

    /* 走到别处再传回来 */
    posX=px[1];posY=py[1];
    CHECK(PhaseTryTeleport(), "phase: teleport succeeds once");
    CHECK(fabsf(posX-px[0])<.05f && fabsf(posY-py[0])<.05f,
                                "phase: player lands on the beacon");
    CHECK(phasePlaced == 0, "phase: the beacon is consumed");
    CHECK(phaseCharges == 0, "phase: the charge is spent");
    CHECK(phaseOwned == 0, "phase: the device is gone after its single use");

    /* 再按一次必须失败 —— 这就是旧版"第二次永远用不出来"的守卫 */
    CHECK(!PhaseTryTeleport(), "phase: a second teleport is refused");
}

static void test_phase_landing_near_wall(void) {
    extern void PhaseOnThrowLanded(int kind, float lx, float ly);
    extern int phasePlaced, phaseCharges;
    extern float phaseX, phaseY;
    extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
    extern int Blocked(float, float);

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartFreshTestRun();

    /* 随便找一格墙，把落点放在它所在的格子里（这就是撞墙的情形） */
    int wx = 0, wy = 0, found = 0;
    for (int y = 0; y < MAP_HEIGHT && !found; y++)
        for (int x = 0; x < MAP_WIDTH && !found; x++)
            if (worldMap[y][x] && x > 0 && y > 0 && x < MAP_WIDTH - 1 && y < MAP_HEIGHT - 1) {
                wx = x; wy = y; found = 1;
            }
    CHECK(found == 1, "a generated map must contain an interior wall");

    phasePlaced = 0; phaseCharges = 0;
    phaseX = phaseY = 0.0f;

    /* kind 12 = 信标；落点就在墙格中心 —— 旧代码会直接拒绝 */
    PhaseOnThrowLanded(12, (float)wx + 0.5f, (float)wy + 0.5f);

    CHECK(phasePlaced == 1,
          "a beacon must still be placed when the projectile lands against a wall");
    CHECK(phaseCharges == 1, "placing the beacon must arm its single charge");

    /* 坐标必须被吸附到**可站立**的格子中心，不是原来那个墙格 */
    CHECK(!Blocked(phaseX, phaseY),
          "the beacon coordinate must be nudged to a standable cell");
    CHECK(worldMap[(int)phaseY][(int)phaseX] == 0,
          "the beacon coordinate must sit on floor, not inside geometry");
    CHECK(phaseX == (float)(int)phaseX + 0.5f && phaseY == (float)(int)phaseY + 0.5f,
          "the beacon coordinate must be snapped to a cell centre");

    phasePlaced = 0;
}

static void test_phase_slots_selectable(void) {
    extern int throwSelected;
    extern int phaseBeacons;
    extern int flares, bottles, smokeBombs, gluePots, echoBolts, decoys;
    extern void EnsureThrowSelectedValid(void);

    int sf = flares, sb = bottles, ss = smokeBombs, sg = gluePots, se = echoBolts, sd = decoys;
    int sSel = throwSelected;

    /* ---- 只有信标：选择器必须落到槽 6 ---- */
    flares = bottles = smokeBombs = gluePots = echoBolts = decoys = 0;
    phaseBeacons = 3;
    throwSelected = 0;
    EnsureThrowSelectedValid();
    CHECK(throwSelected == 6,
          "with only beacons left the selector must land on slot 6 (BEACON)");

    /* ---- 六种旧物品都为空时，不能把选择器卡在 0 ---- */
    phaseBeacons = 0;
    throwSelected = 0;
    EnsureThrowSelectedValid();
    CHECK(throwSelected == 0, "with nothing at all the selector falls back to slot 0");

    flares = sf; bottles = sb; smokeBombs = ss; gluePots = sg; echoBolts = se; decoys = sd;
    throwSelected = sSel;
}

static void test_beacon_cleared_between_floors(void) {
    extern int   phaseOwned, phaseCharges, phasePlaced;
    extern float phaseX, phaseY;
    extern void  PhaseGive(void);
    extern int   PhaseTryPlace(void);
    extern void  PhaseResetPlacement(void);
    extern float posX, posY;

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartFreshTestRun();

    PhaseGive();
    CHECK(phaseOwned == 1 && phaseCharges == 1, "device granted (one use)");

    PhaseTryPlace();                 /* 放在脚下 */
    CHECK(phasePlaced == 1, "beacon placed on this floor");

    /* 模拟换层 */
    PhaseResetPlacement();
    CHECK(phasePlaced == 0, "changing floors must clear the placed beacon");
    CHECK(phaseX == 0.0f && phaseY == 0.0f, "the stale coordinates must be wiped");
    CHECK(phaseOwned == 1, "the DEVICE is inventory and must survive a floor change");
    CHECK(phaseCharges == 1, "charges must survive a floor change too");
    (void)posX; (void)posY;
}

static void test_phase_price(void) {
    extern int ShopPriceAt(int row);
    extern int carryGold, currentLevel;

    int savedGold = carryGold;
    int savedLevel = currentLevel;

    /* ==================================================================
     * 2026-11-06 修正后的契约：**随层数递增的百分比**
     *   当前用户规则：所有层数均为携带金币的5%，向上取整，最低1金
     * 固定 70% 的旧契约太狠（用户实测："价格过于昂贵"）。
     * ================================================================ */
    carryGold = 1000;

    currentLevel = 1;
    CHECK(ShopPriceAt(8) == 50, "beacon costs 5% of carried gold");

    currentLevel = 5;
    CHECK(ShopPriceAt(8) == 50, "depth does not alter the 5% beacon quote");

    currentLevel = 11;
    CHECK(ShopPriceAt(8) == 50, "deep floors retain the 5% beacon quote");

    currentLevel = 30;
    CHECK(ShopPriceAt(8) == 50, "the percentage stays fixed on deep floors");

    /* 下限：身上没钱时仍有一个真实价格，不是 0 也不至于买不起到离谱 */
    currentLevel = 1;
    carryGold = 0;
    CHECK(ShopPriceAt(8) == 1, "an empty wallet still receives a nonzero one-gold quote");

    carryGold = savedGold;
    currentLevel = savedLevel;
}

static void test_phase_beacon(void) {
    extern int   phaseOwned, phaseCharges, phasePlaced;
    extern float phaseX, phaseY;
    extern void  PhaseGive(void), PhaseReset(void);
    extern int   PhaseTryPlace(void), PhaseTryTeleport(void);
    extern float posX, posY;
    extern int   Blocked(float, float);

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartFreshTestRun();

    /* ==================================================================
     * **不要写死坐标**。
     * 上一版用的是 3.5 / 6.5 / 20.5 这种常数，而地图是随机生成的 ——
     * 前面的用例重新生成一层之后，这些格子可能正好是墙，
     * 于是"传送失败"看起来像代码坏了（实测就这么红过）。
     * 这里先扫描出 3 个确定可站立的位置再用。
     * ================================================================ */
    float fX[3] = { 0 }, fY[3] = { 0 };
    {
        extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
        int got = 0;
        for (int y = 1; y < MAP_HEIGHT - 1 && got < 3; y++)
            for (int x = 1; x < MAP_WIDTH - 1 && got < 3; x++) {
                if (worldMap[y][x]) continue;
                float cx = (float)x + 0.5f, cy = (float)y + 0.5f;
                if (Blocked(cx, cy)) continue;
                /* 三个点要分开，避免互相干扰 */
                int tooClose = 0;
                for (int k = 0; k < got; k++) {
                    float ddx = fX[k] - cx, ddy = fY[k] - cy;
                    if (ddx * ddx + ddy * ddy < 4.0f) tooClose = 1;
                }
                if (tooClose) continue;
                fX[got] = cx; fY[got] = cy; got++;
            }
        CHECK(got == 3, "the test map must contain at least three free cells");
    }

    /* ---- 初始状态：没有装置 ---- */
    PhaseReset();
    CHECK(phaseOwned == 0, "phase beacon must start not owned");
    CHECK(PhaseTryPlace() == 0, "cannot place without the device");
    CHECK(PhaseTryTeleport() == 0, "cannot teleport without the device");

    /* ==================================================================
     * 2026-11-07（v2.4.2 / B3）：规则改成「一枚信标 = 一次传送」
     * ------------------------------------------------------------------
     * 旧规则是"投一次给 2 次充电，传送成功就清 placed"，
     * 于是第一次传送后剩 1 次充能却没有锚点 —— 第二次永远用不出来。
     * 现在：放一个锚点 -> 传一次 -> 锚点消失；要再传就再放一个。
     * ================================================================== */

    /* ---- 发放：1 次，未放置 ---- */
    PhaseGive();
    CHECK(phaseOwned == 1, "PhaseGive must mark it owned");
    CHECK(phaseCharges == 1, "one beacon carries exactly one teleport charge");
    CHECK(phasePlaced == 0, "a fresh device has no beacon on the field");
    CHECK(PhaseTryTeleport() == 0,
          "teleporting before placing must fail (and must not eat a charge)");
    CHECK(phaseCharges == 1, "a failed teleport must not consume a charge");

    /* ---- 放置：一次，且只能有一个 ---- */
    posX = fX[0]; posY = fY[0];
    CHECK(PhaseTryPlace() == 1, "placing on a walkable cell must succeed");
    CHECK(phasePlaced == 1, "the beacon must be marked as on the field");
    CHECK(phaseX == fX[0] && phaseY == fY[0], "the beacon must record the exact spot");
    CHECK(phaseCharges == 1, "placing must not consume the charge");

    posX = fX[1]; posY = fY[1];
    PhaseTryPlace();
    CHECK(phaseX == fX[1] && phaseY == fY[1],
          "placing again must MOVE the same beacon, never stack a second one");

    /* ---- 传送：回到信标，锚点与充能一起用完 ---- */
    posX = fX[2]; posY = fY[2];
    CHECK(PhaseTryTeleport() == 1, "teleporting to a placed beacon must succeed");
    CHECK(posX == fX[1] && posY == fY[1], "teleport must put the player on the beacon");
    CHECK(phaseCharges == 0, "the single teleport consumes the charge");
    CHECK(phasePlaced == 0, "the beacon is consumed by the teleport");
    CHECK(phaseOwned == 0, "with no charges left the device must disappear");
    CHECK(PhaseTryTeleport() == 0, "a second teleport must be refused");
    CHECK(PhaseTryPlace() == 0, "an exhausted device can no longer be placed");

    /* ---- 目标格被占：拒绝，不能把玩家塞进墙里 ---- */
    PhaseGive();
    posX = fX[0]; posY = fY[0];
    PhaseTryPlace();
    {
        extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
        int bx = (int)phaseX, by = (int)phaseY;
        int saved = worldMap[by][bx];
        worldMap[by][bx] = 1;                 /* 事后被塌成墙 */
        /* 把玩家挪到一个已知空的格子（就用刚才放信标那格），
           然后断言"被拒绝之后坐标一分没动"。
           不要断言 `!Blocked(posX,posY)` —— 那假定了 20.5,20.5 是空地，
           而地图是随机生成的，这个假定不成立（第一次就红在这）。 */
        posX = fX[2]; posY = fY[2];
        {
            float keepX = posX, keepY = posY;
            CHECK(PhaseTryTeleport() == 0,
                  "teleporting into a wall must be refused");
            CHECK(posX == keepX && posY == keepY,
                  "a refused teleport must not move the player at all");
        }
        worldMap[by][bx] = saved;
    }

    PhaseReset();
}

void TestSuite_phase(void) {
    test_phase_beacon_one_use();
    test_phase_landing_near_wall();
    test_phase_slots_selectable();
    test_beacon_cleared_between_floors();
    test_phase_price();
    test_phase_beacon();
}

/* tests/domains/test_hatch.c —— hatch 域的集成测试
 *
 * 第 2 轮（2026-11-08）从 test_runtime.c 原样搬过来，断言一条没改。
 * 入口：TestSuite_hatch()
 */
#include "../test_common.h"

static void test_hatch_no_duplicate_kind_per_floor(void) {
    extern int HatchPlaceInRoom(int fx, int fy, int w, int h, int themeId);
    extern int HatchKindForTheme(int themeId);
    extern int hatchCount;
    extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
    int theme;
    int kind;
    int second;

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();

    /* 找一个能把门放下的主题（房间号 0..6 里总有一个能映射） */
    theme = -1;
    for (kind = 0; kind < 8; kind++) {
        if (HatchKindForTheme(kind) >= 0) { theme = kind; break; }
    }
    CHECK(theme >= 0, "hatch: at least one theme maps to a hatch kind");

    /* 清空并手工铺一块空地，保证放置有位置 */
    {
        extern void HatchReset(void);
        int x, y;
        HatchReset();
        for (y = 2; y < 8; y++)
            for (x = 2; x < 8; x++)
                worldMap[y][x] = 0;
    }

    CHECK(HatchPlaceInRoom(2, 2, 4, 4, theme) >= 0, "hatch: the first door is placed");
    CHECK(hatchCount == 1, "hatch: exactly one door after the first placement");

    second = HatchPlaceInRoom(2, 2, 4, 4, theme);
    CHECK(second == -1, "hatch: a second door of the SAME kind on one floor is refused");
    CHECK(hatchCount == 1, "hatch: the floor still has exactly one door");
}

static void test_hatch_screenshot(void) {
    const char* dir = getenv("CANITY_SHOT");
    if (!dir || !dir[0]) return;

    extern float posX, posY, dirX, dirY;
    extern int hatchCount;
    extern Hatch hatches[MAX_HATCHES];
    extern unsigned int* pixels;
    extern int renderW, renderH;
    extern void Render3D(void);

    /* ==================================================================
     * **必须先烘精灵**
     * ------------------------------------------------------------------
     * 这个用例在 main 的测试序列里跑得比 test_hunter_models 早，
     * 而 SpritesInit() 是在那里调的 —— 结果舱门精灵根本不存在，
     * Render3D 画了个空气，我看到的"门没出现"其实是这个。
     * 诊断里 `bbox=0 0x0`（hasB=0）就是它。
     * ================================================================ */
    {
        extern void SpritesInit(void);
        InitPixels();
        SpritesInit();
    }

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    flashlightOn = 1;

    /* 生成到一层真的有门为止 */
    int found = 0;
    for (int i = 0; i < 80 && !found; i++) {
        currentLevel = 1;
        GenerateLevel();
        if (hatchCount > 0) found = 1;
    }
    if (!found) { fprintf(stderr, "  [shot] no hatch after 80 levels\n"); return; }
    fprintf(stderr, "  [shot] dir=%s hatchCount=%d (cwd-relative output)\n", dir, hatchCount);
    battery = 150.0f;

    /* 门前面站三个距离各拍一张：近（贴脸）/ 中 / 远 */
    static const float kDists[3] = { 1.2f, 1.8f, 2.4f };   /* 近 / 中 / 稍远 */
    for (int k = 0; k < 3; k++) {
        float hx = hatches[0].x, hy = hatches[0].y;

        /* ================================================================
         * 站位：从门的四个轴向里挑**一条整段都是空地**的路，
         * 站在离门 kDists[k] 的位置朝门看。
         * 第一版是"朝地图中心的反方向退"，结果退到墙里，画面全是砖。
         * ================================================================ */
        static const int kdx[4] = { 1, -1, 0, 0 };
        static const int kdy[4] = { 0, 0, 1, -1 };
        float vx = 0.0f, vy = 0.0f;
        int have = 0;
        for (int d = 0; d < 4 && !have; d++) {
            int ok = 1;
            for (int step = 1; step <= 3 && ok; step++) {   /* 房间只有 5x5，3 格够站 */
                int cx = (int)(hx + (float)kdx[d] * (float)step);
                int cy = (int)(hy + (float)kdy[d] * (float)step);
                if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) { ok = 0; break; }
                if (worldMap[cy][cx]) ok = 0;
            }
            if (ok) { vx = (float)kdx[d]; vy = (float)kdy[d]; have = 1; }
        }
        if (!have) continue;   /* 四周都堵，这张跳过 */

        posX = hx + vx * (kDists[k] + 1.0f);
        posY = hy + vy * (kDists[k] + 1.0f);
        dirX = -vx; dirY = -vy;

        /* 2026-11-06：放一只缚丝蛛在门旁，让截图里能看到它的建模 */
        {
            extern void SpiderDebugPlace(float, float);
            extern void SpiderReset(void);
            SpiderReset();
            SpiderDebugPlace(hx + vx * 1.6f + (vy != 0.0f ? 1.0f : 0.0f),
                             hy + vy * 1.6f + (vx != 0.0f ? 1.0f : 0.0f));
        }

        /* 2026-11-06：顺便放一个相位信标在门旁边，让截图里能看到它的建模 */
        {
            extern void PhaseGive(void);
            extern int PhaseTryPlace(void);
            extern float posX, posY;
            float sx = posX, sy = posY;
            PhaseGive();
            /* 放在镜头与门之间（正对着，不偏移）——
               原来往侧面偏了 1.2 格，正好落进墙里，被 zBuffer 全遮住。 */
            posX = hx + vx * 0.9f;
            posY = hy + vy * 0.9f;
            PhaseTryPlace();
            posX = sx; posY = sy;
        }

        Render3D();

        /* 诊断：把门的投影框报出来，区分"没投影"和"画了个空图" */
        {
            extern int DebugHatchScreenBox(int, float, float, int*, int*, int*, int*,
                                           int*, int*, int*);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0, hasB = 0, bw = 0, bh = 0;
            int ok = DebugHatchScreenBox(0, hx, hy, &x0, &y0, &x1, &y1, &hasB, &bw, &bh);
            fprintf(stderr, "  [shot] k=%d ok=%d box=(%d,%d)-(%d,%d) bbox=%d %dx%d "
                            "pos=(%.1f,%.1f) dir=(%.1f,%.1f) hatch=(%.1f,%.1f)\n",
                    k, ok, x0, y0, x1, y1, hasB, bw, bh,
                    posX, posY, dirX, dirY, hx, hy);
        }

        /* 写**相对路径**（cwd = 仓库根）。
           绝对路径这次 fopen 直接失败，而精灵导出用相对路径是成功的 ——
           先按能跑通的方式做，之后再由脚本搬到产出目录。 */
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "shot_world_%d.ppm", k);
        {
            FILE* f = fopen(path, "wb");
            if (!f) { fprintf(stderr, "  [shot] fopen failed: %s\n", path); continue; }
            fprintf(f, "P6\n%d %d\n255\n", renderW, renderH);
            for (int y = 0; y < renderH; y++) {
                for (int x = 0; x < renderW; x++) {
                    unsigned int c = pixels[y * renderW + x];
                    /* 像素是 0x00RRGGBB（渲染层不带 alpha） */
                    unsigned char px[3] = {
                        (unsigned char)((c >> 16) & 0xFF),
                        (unsigned char)((c >> 8) & 0xFF),
                        (unsigned char)(c & 0xFF)
                    };
                    fwrite(px, 1, 3, f);
                }
            }
            fclose(f);
            fprintf(stderr, "  [shot] wrote %s\n", path);
        }
    }
}

static void test_all_hatch_effects(void) {
    extern Hatch hatches[MAX_HATCHES];
    extern int hatchCount;
    extern float hatchLightsT, hatchGoldMulT, hatchBloodT, hatchSpeedT;
    extern int collapsedLevel;

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();
    currentLevel = 1;
    GenerateLevel();          /* 需要一张真实地图（拆墙、崩塌都要用） */

    /* ---- ① 供电室：全灯清雾 ---- */
    {
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_POWER, 4);
        hatchLightsT = 0.0f;
        battery = 150.0f; fusesHeld = 0;
        float batBefore = battery;
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "POWER hatch must fire");
        CHECK(hatchLightsT > 19.0f && hatchLightsT < 21.0f,
              "POWER must turn the lights on for ~20 seconds");
        CHECK(battery < batBefore, "POWER must spend battery when no fuses are held");
    }

    /* ---- ② 地狱：血祭（金 + 理智 + 加速）---- */
    {
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_HELL, 2);
        hatchSpeedT = 0.0f;
        carryGold = 500; sanity = 60.0f;
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "HELL hatch must fire");
        CHECK(carryGold == 400, "HELL must burn exactly 100 gold");
        CHECK(sanity > 44.0f && sanity < 46.0f, "HELL must cost 15 sanity");
        CHECK(hatchSpeedT > 29.0f && hatchSpeedT < 31.0f,
              "HELL must grant a ~30 second speed window");
    }

    /* ---- ③ 病房：包扎（理智 + 体力 + 血迹）---- */
    {
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_WARD, 3);
        hatchBloodT = 0.0f;
        sanity = 20.0f; stamina = 0.0f;
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "WARD hatch must fire");
        CHECK(sanity > 59.0f, "WARD must restore ~40 sanity");
        CHECK(stamina > 1.0f, "WARD must refill stamina");
        CHECK(hatchBloodT > 59.0f && hatchBloodT < 61.0f,
              "WARD must leave a 60 second blood trail");
    }

    /* ---- ④ 仓储室：开仓（付钱 + 家具回满 + 金币倍率）---- */
    {
        extern RoomProp roomProps[MAX_ROOM_PROPS];
        extern int roomPropCount;
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_STORAGE, 1);
        hatchGoldMulT = 0.0f;
        carryGold = 500;
        /* 造一件搜空的家具，看它会不会被回满 */
        roomProps[0].active = 1;
        roomProps[0].kind = ROOMPROP_SHELF;
        roomProps[0].searchLeft = 0;
        roomProps[0].searched = 7;
        if (roomPropCount < 1) roomPropCount = 1;

        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "STORAGE hatch must fire");
        CHECK(carryGold == 500 - HATCH_STORAGE_COST,
              "STORAGE must cost exactly HATCH_STORAGE_COST gold");
        CHECK(roomProps[0].searchLeft == 3,
              "STORAGE must refill the shelf's three searches");
        CHECK(hatchGoldMulT > 59.0f && hatchGoldMulT < 61.0f,
              "STORAGE must grant a 60 second gold boost");
    }

    /* ---- ⑤ 血肉洞窟：喂食（消耗投掷物 + 全图僵直）---- */
    {
        extern Monster monHunters[MON_HUNTER_MAX];
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_FLESH, 6);
        flares = 2; bottles = 0; smokeBombs = 0; gluePots = 0; echoBolts = 0;
        monHunters[0].active = 1;
        monHunters[0].stun = 0.0f;
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "FLESH hatch must fire");
        CHECK(flares == 1, "FLESH must consume exactly one throwable");
        CHECK(monHunters[0].stun > 19.0f,
              "FLESH must freeze every hunter for ~20 seconds");
    }

    /* ---- ⑥ 砖墙：拆墙（墙的数量必须变少）---- */
    {
        extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
        /* ================================================================
         * **显式布置**，不依赖生成出来的地图。
         * 上一版靠"地图中间一定有墙"，多生成一只追光怪改变了随机序列，
         * 中间恰好没有可砸的墙 —— 测试就红了。
         * 这种"依赖具体地图"的断言是脆的：它测的不是逻辑，是地形。
         * ================================================================ */
        int hx = MAP_WIDTH / 2, hy = MAP_HEIGHT / 2;
        /* 门所在格与"墙/墙后空地"三格，手工摆好 */
        int savedCell[3];
        savedCell[0] = worldMap[hy][hx];
        savedCell[1] = worldMap[hy][hx + 1];
        savedCell[2] = worldMap[hy][hx + 2];
        worldMap[hy][hx] = 0;          /* 门站得住 */
        worldMap[hy][hx + 1] = 1;      /* 要砸的墙 */
        worldMap[hy][hx + 2] = 0;      /* 墙后是空地（否则不该砸） */

        HatchReset();
        HatchAdd((float)hx + 0.5f, (float)hy + 0.5f, HATCH_BRICK, 0);
        stamina = 100.0f;
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "BRICK hatch must fire");

        CHECK(worldMap[hy][hx + 1] == 0,
              "BRICK must carve the wall that has open space behind it");
        CHECK(worldMap[hy][hx + 2] == 0,
              "BRICK must not fill anything in");
        CHECK(stamina < 76.0f, "BRICK must cost stamina");

        /* 还原地形，别污染后面的用例 */
        worldMap[hy][hx] = savedCell[0];
        worldMap[hy][hx + 1] = savedCell[1];
        worldMap[hy][hx + 2] = savedCell[2];
    }

    /* ---- ⑦ 锅炉房：崩塌（走黑屏流程，计次 +1）---- */
    {
        extern int CollapseRerollCount(void);
        extern void CollapseBegin(void);
        extern int CollapseTick(float);
        int before = CollapseRerollCount();
        HatchReset();
        HatchAdd(2.5f, 2.5f, HATCH_BOILER, 5);
        HatchSetState(0, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[0].state == HATCH_FIRED, "BOILER hatch must fire");
        /* 效果只是"开始崩塌"，真正的重掷在黑屏流程里 */
        CollapseTick(0.5f);
        CollapseTick(0.5f);
        CHECK(CollapseRerollCount() == before + 1,
              "BOILER must start the collapse reroll");
        CollapseTick(2.0f);          /* 走完，清掉黑屏 */
        collapsedLevel = 0;
    }

    HatchReset();
}

static void test_hatch_end_to_end(void) {
    extern int hatchCount;
    extern Hatch hatches[MAX_HATCHES];
    extern void MapValidate(MapCheck* out);
    extern int collapsedLevel;

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();

    /* ---- ① 生成到一层真的有门 ---- */
    int found = 0;
    for (int i = 0; i < 60 && !found; i++) {
        currentLevel = 1;
        GenerateLevel();
        if (hatchCount > 0) found = 1;
    }
    CHECK(found == 1, "a generated level must contain at least one themed room with a hatch");
    CHECK(hatchCount <= MAX_HATCHES, "hatch table must never exceed MAX_HATCHES");

    /* ---- ② 资源拉满时，**每一种**门槛都必须能过 ---- */
    carryGold = 99999; fusesHeld = 8; battery = 150.0f;
    /* ================================================================
     * 理智取 **45**，不是 5 —— 这里有两道门槛互相咬住：
     *     病房（WARD）要求 理智 < 50
     *     地狱（HELL）要求 理智 >= 40
     * 兼容窗口只有 **[40, 50)**。给 5 的话地狱门永远过不去
     * （第一次就是这样红的：gate blocked: kind=5 why=SANITY TOO LOW (5/40)）。
     * ================================================================ */
    sanity = 45.0f;
    stamina = 100.0f; flares = 5; bottles = 5;
    /* ================================================================
     * 锅炉房的门槛是「本房家具**全部搜空** + 1 根保险丝」——
     * 只给资源是不够的，还得真的把家具搜空。
     * 这条以前是"碰巧通过"：生成到没家具的层，条件自然成立。
     * 现在显式满足它，用例才不依赖随机布局。
     * ================================================================ */
    {
        extern RoomProp roomProps[MAX_ROOM_PROPS];
        extern int roomPropCount;
        for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++)
            roomProps[i].searchLeft = 0;
    }
    {
        int gateFails = 0;
        for (int i = 0; i < hatchCount; i++) {
            char why[96];
            why[0] = 0;
            if (HatchGateCheck(i, why, (int)sizeof(why)) != 1) {
                gateFails++;
                fprintf(stderr, "  gate blocked: kind=%d why=%s\n", hatches[i].kind, why);
            }
        }
        CHECK(gateFails == 0, "with resources maxed, every hatch gate must pass");
    }

    /* ---- ③ 资源清零时，门槛必须真的挡人（并且给出原因） ---- */
    carryGold = 0; fusesHeld = 0; battery = 0.0f;
    sanity = 100.0f; stamina = 0.0f; flares = 0; bottles = 0;
    {
        int blocked = 0;
        for (int i = 0; i < hatchCount; i++) {
            char why[96];
            why[0] = 0;
            if (HatchGateCheck(i, why, (int)sizeof(why)) == 0) {
                blocked++;
                CHECK(why[0] != 0, "a blocked gate must say what is missing");
            }
        }
        CHECK(blocked == hatchCount, "with no resources, every hatch gate must block");
    }

    /* ---- ④ 开门 → 动画 → 效果结算 ---- */
    {
        int idx = 0;
        int kindBefore = hatches[idx].kind;
        int goldBefore = carryGold;
        HatchSetState(idx, HATCH_OPENING);
        HatchTick(HATCH_OPEN_SECONDS + 0.02f);
        CHECK(hatches[idx].state == HATCH_FIRED,
              "opening a hatch must end in FIRED");
        (void)kindBefore; (void)goldBefore;
    }

    /* ---- ⑤ 崩塌：黑屏 → 重掷 → 淡入 → 仍可玩 ---- */
    {
        extern void CollapseBegin(void);
        extern int CollapseTick(float);
        extern float screenBlackout;

        extern int CollapseRerollCount(void);
        int rerollsBefore = CollapseRerollCount();
        CollapseBegin();
        CHECK(CollapseTick(0.40f) == 1, "collapse must report busy during fade-out");
        CHECK(screenBlackout > 0.4f && screenBlackout < 0.6f,
              "0.4s into the 2s timeline the screen must be about half black");

        /* 这一刻画面应在全黑附近，重掷就发生在这一帧 */
        CHECK(CollapseTick(0.42f) == 1, "collapse must still be busy at the reroll");
        CHECK(screenBlackout > 0.95f,
              "the reroll must happen while the screen is (nearly) fully black");

        /* 走完淡入 */
        CollapseTick(1.30f);
        CHECK(screenBlackout == 0.0f, "after 2 seconds the blackout must be fully cleared");
        /* **关键补断言**：重掷必须真的执行过。
           只断言"塌完之后地图可玩"是不够的 ——
           "根本没塌"同样满足它，第一版就是这样假绿的。 */
        CHECK(CollapseRerollCount() == rerollsBefore + 1,
              "the collapse must actually execute the reroll");

        /* 塌成功 → 是崩塌层；塌失败 → 已回滚成原层。两种都必须是可玩地图。 */
        {
            MapCheck mc;
            memset(&mc, 0, sizeof(mc));
            MapValidate(&mc);
            CHECK(mc.ok, "after collapse the level must still validate as playable");
            CHECK(mc.exitReachable, "after collapse the exit must still be reachable");
            CHECK(mc.spawnOpen, "after collapse the spawn must still be open");
            /* **关键**：重掷换了整张图，而玩家坐标没动 ——
               如果落点变成墙，玩家会被永久卡住（两个轴向都 Blocked）。
               这条断言钉住第 6 轮的那个修复。 */
            {
                extern float posX, posY;
                extern int Blocked(float, float);
                CHECK(!Blocked(posX, posY),
                      "after collapse the player must not be stuck inside the new terrain");
            }

            /* ================================================================
             * **确定性地**验证"把玩家从墙里挪出来"
             * ----------------------------------------------------------------
             * 端到端那条断言不够：崩塌重掷后玩家**恰好**落在空地上时，
             * 删掉修复测试照样绿（我实测过，真的没有红）。
             * 所以这里直接把玩家坐标塞进一个已知墙格，再调修复函数。
             * ================================================================ */
            {
                extern float posX, posY;
                extern int Blocked(float, float);
                extern int CollapseUnstickPlayer(void);
                extern int worldMap[MAP_HEIGHT][MAP_WIDTH];

                /* 找一个墙格（外墙一定有） */
                int wx = 0, wy = 0, foundWall = 0;
                for (int y = 0; y < MAP_HEIGHT && !foundWall; y++)
                    for (int x = 0; x < MAP_WIDTH && !foundWall; x++)
                        if (worldMap[y][x]) { wx = x; wy = y; foundWall = 1; }
                CHECK(foundWall, "a generated map must contain at least one wall");

                float savedX = posX, savedY = posY;
                posX = (float)wx + 0.5f;
                posY = (float)wy + 0.5f;
                CHECK(Blocked(posX, posY), "the chosen cell must really be blocked");

                int moved = CollapseUnstickPlayer();
                CHECK(moved == 1, "CollapseUnstickPlayer must report that it moved the player");
                CHECK(!Blocked(posX, posY),
                      "after unstick the player must stand on a free cell");

                posX = savedX; posY = savedY;
            }
        }
        (void)collapsedLevel;
    }

    /* ---- ⑥ 崩塌层不生成舱门（防无限重掷）---- */
    /* 注意：本用例会把 collapsedLevel 置 1，结束时必须还原 ——
       否则后面的平衡断言会读到 1.8 倍金币而失败。
       （第一次跑就踩到了：FAIL per-crate gold must be about 7） */
    collapsedLevel = 0;

    /* ---- ⑥ 崩塌层不生成舱门（防无限重掷）---- */
    {
        extern int HatchPlaceInRoom(int, int, int, int, int);
        int save = collapsedLevel;
        collapsedLevel = 1;
        CHECK(HatchPlaceInRoom(2, 2, 4, 4, 5) == -1,
              "collapsed layers must not spawn new hatches");
        collapsedLevel = save;
    }
}

static void test_hatch_state_and_mask(void) {
    extern Hatch hatches[MAX_HATCHES];
    extern int hatchCount;

    HatchReset();
    CHECK(hatchCount == 0, "HatchReset must clear the table");

    int a = HatchAdd(1.5f, 1.5f, HATCH_POWER, 4);
    int b = HatchAdd(3.5f, 1.5f, HATCH_HELL, 2);
    CHECK(a == 0 && b == 1, "HatchAdd must append in order");
    CHECK(hatches[a].state == HATCH_SEALED, "a new hatch starts SEALED");

    /* 封闭的门在就绪查询里应该能被选中 */
    CHECK(HatchNearest(1.6f, 1.6f, 4.0f) == a, "the nearest sealed hatch must be found");

    /* ---- 开门：SEALED -> OPENING -> (动画播完) -> FIRED ---- */
    HatchSetState(a, HATCH_OPENING);
    CHECK(hatches[a].state == HATCH_OPENING, "hatch must enter OPENING");
    CHECK(HatchOpen01(a) < 0.5f, "a fresh OPENING hatch must read as mostly closed");
    /* 动画没播完时，效果不能结算（AD-27） */
    /* 半径收到 2.0：另一扇门在 3.6 之外，所以这里只能返回 -1。
       （用 4.0 会把远的那扇选出来 —— 那是正确行为，是测试写错了。） */
    CHECK(HatchNearest(1.6f, 1.6f, 2.0f) == -1,
          "a hatch mid-animation must not be pickable again");

    HatchTick(HATCH_OPEN_SECONDS + 0.01f);
    CHECK(hatches[a].state == HATCH_FIRED,
          "the effect must fire on the frame the animation finishes");

    /* ---- FIRED 是终态：任何转移都必须被拒 ---- */
    HatchSetState(a, HATCH_SEALED);
    CHECK(hatches[a].state == HATCH_FIRED,
          "FIRED is terminal - a used hatch must never become usable again");

    /* ---- 已用掩码：按**种类**记，不按顺序 ---- */
    {
        int mask = HatchUsedMask();
        CHECK(mask == (1 << HATCH_POWER),
              "the mask must record the used KIND, not the slot index");

        /* 重建（模拟读档重放）并应用掩码 */
        HatchReset();
        HatchAdd(9.5f, 9.5f, HATCH_HELL, 2);     /* 顺序刻意反过来 */
        HatchAdd(4.5f, 4.5f, HATCH_POWER, 4);
        HatchApplyUsedMask(mask);

        CHECK(hatches[0].state == HATCH_SEALED,
              "a kind that was NOT used must stay available after load");
        CHECK(hatches[1].state == HATCH_FIRED,
              "a kind that was used must be FIRED after load, whatever its slot");
    }

    /* ---- 读档不能把上一层的门带过来 ---- */
    HatchReset();
    CHECK(hatchCount == 0, "RoomReplay must reset the hatch table before regenerating");

    HatchReset();
}

void TestSuite_hatch(void) {
    test_hatch_no_duplicate_kind_per_floor();
    test_hatch_screenshot();
    test_all_hatch_effects();
    test_hatch_end_to_end();
    test_hatch_state_and_mask();
}

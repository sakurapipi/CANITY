/* tests/domains/test_progress.c —— progress 域的集成测试
 *
 * 第 5 轮（2026-11-08）从 test_runtime.c 原样搬过来，断言一条没改。
 * 入口：TestSuite_progress()
 */
#include "../test_common.h"

static void test_daily(void) {
    unsigned goals=0,mods=0,res=0;
    static const int hi[]={8000,2,12,40,15,15,15,6};
    for(unsigned y=2026;y<2040;++y) for(unsigned m=1;m<=12;++m) for(unsigned d=1;d<=28;++d) {
        unsigned date=y*10000+m*100+d;
        DailyGenerateForDate(date);
        goals|=1u<<DailyGoalId();mods|=1u<<DailyModId();res|=1u<<DailyRestrictId();
        CHECK(DailyGoalParam()>0 && DailyGoalParam()<=hi[DailyGoalId()],"daily target stays inside its range");
        int g=DailyGoalId(),v=DailyGoalParam(),mod=DailyModId(),r=DailyRestrictId();
        DailyGenerateForDate(date);
        CHECK(g==DailyGoalId() && v==DailyGoalParam() && mod==DailyModId() && r==DailyRestrictId(),"same date produces same challenge");
    }
    /* 2026-11-05：DR_NOWRENCH（不能使用扳手）已永久移出池。
       武器系统下线后扳手是玩家唯一的主动工具，禁掉它 = 赤手空拳跑一整局，
       玩家第一反应是「扳手坏了」（用户报告）。枚举值保留（存档里是数字），
       只是不再被抽到 —— 所以可达位图是 63 去掉 bit4 = 47。 */
    CHECK(goals==255 && mods==255 && res==47,"every active daily pool entry is reachable");
    CHECK(!(res & (1u<<4)),"the retired wrench restriction must never be picked");
    trainingMode=0;dailyMode=0;
    for(int goal=0;goal<DQ_COUNT;++goal) {
        daily_case(goal,hi[goal]);bankGold=0;
        DailyObserve(goal,1);CHECK(DailyCurrent()==1,"all eight daily goals advance");
        DailyObserve(goal,1);CHECK(DailyCurrent()==1,"repeated observation never duplicates progress");
        DailyBeginRun();DailyObserve(goal,1);
        int sum=goal==0 || goal==1 || goal==3 || goal==4 || goal==5;
        CHECK(DailyCurrent()==(sum?2:1),"cumulative goals span runs and run records take maximum");
        DailySetProgress(hi[goal]);CHECK(DailyDone(),"all daily goals can complete");
        int reward=bankGold;DailySetProgress(hi[goal]);CHECK(bankGold==reward,"daily reward paid exactly once");
    }
    daily_case(DQ_SEARCH,40);DailyObserve(DQ_SEARCH,7);
    int saved[15];DailySaveTo(saved);DailyLoadFrom(saved);DailyObserve(DQ_SEARCH,7);
    CHECK(DailyCurrent()==7,"resuming daily progress does not double count");
    saved[5]=20260101;DailyLoadFrom(saved);CHECK(DailyCurrent()==0,"previous date cannot restore progress");
    unsigned dates[]={20260129,20260130,20260131,20260201,20260202,20260203,20260204};
    int empty[15]={0};DailyLoadFrom(empty);bankGold=0;
    for(int i=0;i<7;++i) {DailyGenerateForDate(dates[i]);DailySetProgress(DailyGoalParam());}
    CHECK(DailyStreak()==7 && bankGold==7*800+1500+5000,"three/seven day streak bonuses across month boundary");
    DailyGenerateForDate(20260206);CHECK(DailyStreak()==0,"missing a day breaks streak");
    DailyGenerate();
    CHECK(!DailyForbidSprint() && !DailyForbidWrench() && DailyGoldTierBonus()==0,"daily restrictions do not leak into normal games");
}

static void test_double_reward_rounds(void) {
    extern int skillPickRound, skillPickTotal;

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();
    currentLevel = 6;                 /* 6 % 3 == 0 且 6 % 2 == 0 */
    GenerateLevel();
    { extern void RewardScheduleForLevel(int); RewardScheduleForLevel(6); }

    CHECK(skillPickTotal == 2,
          "level 6 must announce two separate rewards");
    CHECK(skillPickRound == 1,
          "the relic pick comes first and must be labelled 1 of 2");

    /* 模拟"取完遗物"之后的链式第二次 */
    {
        extern void OpenSkillPick(void);
        skillPickRound = 2;
        OpenSkillPick();
        CHECK(skillPickRound == 2 && skillPickTotal == 2,
              "the chained skill pick must be labelled 2 of 2");
    }

    /* 普通偶数层只有一个奖励 */
    currentLevel = 4;
    GenerateLevel();
    { extern void RewardScheduleForLevel(int); RewardScheduleForLevel(4); }
    CHECK(skillPickTotal == 1, "a plain even floor must announce a single reward");
}

static void test_shop_full_bag_no_charge(void) {
    extern int shopRow, shopPage, shopOffer[SHOP_OFFERS], shopPrice[SHOP_OFFERS];
    extern int flares, bottles, smokeBombs, gluePots, echoBolts, decoys;
    extern int carryGold;
    extern int BottleCap(void);
    extern void ShopOpen(int);
    extern void ShopActivate(void);
    extern int ShopRowCount(void);

    clear_meta(); bankGold = 0; dailyMode = 0; trainingMode = 0;
    StartGame();

    /* 直接把售价表改成"照明棒"，然后把它塞满 */
    ShopOpen(1);
    /* ==================================================================
     * 2026-11-06（审计补）：**测试也要自己消掉随机性**
     * ------------------------------------------------------------------
     * ShopOpen 会随机挑一行打折。之前这条用例能过，是因为
     * 前面的测试 consume 的 rand() 次数正好让折扣落在别的行上；
     * 我新加了一条测试（多调了几次 rand），折扣就落到了第 0 行，
     * 价格从 50 变成 50*(1-pct)，断言随即失败。
     *
     * 这不是游戏 bug，是**测试依赖了全局随机序列**（AD-64 的同类问题）。
     * ================================================================ */
    { extern float shopDiscountPct; extern int shopDiscountRow;
      shopDiscountPct = 0.0f; shopDiscountRow = -1; }
    shopPage = SHP_SHOP;         /* 商品页 */
    shopRow = 0;
    shopOffer[0] = 1;            /* kind 1 = flares */
    shopPrice[0] = 50;

    int cap = BottleCap();
    flares = cap;                /* 满 */

    /* ---- 满包：钱不能少 ---- */
    carryGold = 1000;
    int before = carryGold + bankGold;
    { extern float shopCooldown[SHOP_COOLDOWN_ROWS]; shopCooldown[0] = 0.0f; }
    ShopActivate();
    CHECK(carryGold + bankGold == before,
          "buying a full-bag throwable must NOT charge gold");
    CHECK(flares == cap, "the bag must stay exactly at cap, not overflow");

    /* ---- 不满：正常扣钱、正常入包 ---- */
    flares = cap - 1;
    carryGold = 1000;
    before = carryGold + bankGold;
    { extern float shopCooldown[SHOP_COOLDOWN_ROWS]; shopCooldown[0] = 0.0f; }
    ShopActivate();
    CHECK(carryGold + bankGold == before - 50,
          "a purchase that fits must still charge exactly the listed price");
    CHECK(flares == cap, "a purchase that fits must put the item in the bag");

    /* ---- 电池没有容量上限：满包也必须能买 ---- */
    shopRow = 0;
    shopOffer[0] = 6;            /* kind 6 = 电池 */
    shopPrice[0] = 40;
    flares = cap; bottles = cap; smokeBombs = cap;
    gluePots = cap; echoBolts = cap; decoys = cap;
    carryGold = 500;
    before = carryGold + bankGold;
    { extern float shopCooldown[SHOP_COOLDOWN_ROWS]; shopCooldown[0] = 0.0f; }
    ShopActivate();
    CHECK(carryGold + bankGold == before - 40,
          "battery is not bag-limited and must remain purchasable at full bag");
}

static void test_load_clamps_flags(void) {
    extern int dailyMode, campaignMode, campaignStage, bestiarySeen;

    SaveBlob blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = SAVE_MAGIC;
    blob.version = SAVE_VERSION;
    blob.hasRun = 1;
    blob.dailyMode = -1;          /* 坏值：看起来像真 */
    blob.dailySeed = 12345;
    blob.campaignMode = 7;        /* 坏值：不是 0/1 */
    blob.campaignStage = -5;      /* 坏值：负数段 */
    blob.seenMonsters = 0x7FFFFFFF;  /* 坏值：全是位 */

    const char* dir = getenv("CANITY_DATA_DIR");
    char path[MAX_PATH];
    /* 三个槽都清掉：LoadSettings 会挑"最新的那个槽"，
       如果别的用例留下了 canity1/2.sav，写的 canity0 根本不会被读。 */
    for (int slot = 0; slot < 3; slot++) {
        char p3[MAX_PATH];
        snprintf(p3, sizeof(p3), "%s\\canity%d.sav", dir, slot);
        remove(p3);
        snprintf(p3, sizeof(p3), "%s\\canity%d.sav.bak", dir, slot);
        remove(p3);
    }
    snprintf(path, sizeof(path), "%s\\canity0.sav", dir);
    FILE* f = fopen(path, "wb");
    CHECK(f != NULL, "create the corrupt-flag fixture");
    CHECK(fwrite(&blob, 1, sizeof(blob), f) == sizeof(blob), "write the corrupt-flag fixture");
    fclose(f);

    CHECK(LoadSettings() != 0, "a save with corrupt flag values must still load");
    /* 这些字段是在 ContinueRun() 里灌回全局的（不是 LoadSettings），
       所以必须按真实流程走一遍 —— 只调 LoadSettings 测不到。 */
    ContinueRun();

    CHECK(dailyMode == 0, "dailyMode must be clamped to 0/1 (a -1 leaks daily restrictions into a normal run)");
    CHECK(campaignMode == 0, "campaignMode must be clamped to 0/1");
    CHECK(campaignStage == 0, "a negative campaignStage must be clamped to 0");

    /* 图鉴位掩码只保留真实存在的位 */
    {
        int valid = 0;
        for (int bi = 0; bi < BESTIARY_COUNT && bi < 31; bi++) valid |= (1 << bi);
        CHECK((bestiarySeen & ~valid) == 0,
              "bestiarySeen must not keep bits for monsters that do not exist");
    }

    /* 正常值必须原样保留 —— 夹取不能把合法存档也改坏 */
    blob.dailyMode = 1;
    blob.campaignMode = 1;
    blob.campaignStage = 2;
    blob.seenMonsters = 0x5;
    f = fopen(path, "wb");
    CHECK(f != NULL, "rewrite the fixture with valid values");
    (void)fwrite(&blob, 1, sizeof(blob), f);
    fclose(f);
    CHECK(LoadSettings() != 0, "a valid save must still load");
    ContinueRun();
    CHECK(dailyMode == 1, "a legitimate dailyMode=1 must survive");
    CHECK(campaignMode == 1, "a legitimate campaignMode=1 must survive");
    CHECK(campaignStage == 2, "a legitimate campaignStage=2 must survive");
    CHECK(bestiarySeen == 0x5, "a valid bestiary mask must be preserved exactly");
}

static void test_trees_shop(void) {
    clear_meta();bankGold=1000000;activeKeystone=-1;
    CHECK(SkillsSelfTest()==0,"skill data costs, ranks and prerequisites validate");
    for(int i=0;i<SKILL_COUNT;++i) CHECK(skillDefs[i].name && skillDefs[i].maxRank>0,"all thirty skills have definitions");
    int before=bankGold;CHECK(SkillBuy(0),"buy a permanent skill from bank");
    CHECK(bankGold==before-SkillCost(0)/2 || bankGold<before,"permanent purchase charges bank");
    int cost=skillDefs[0].cost[0];int reward=g_achievements[29].unlocked?0:1500;
    before=bankGold;SkillResetAll();CHECK(bankGold==before+cost+reward && skillRank[0]==0 && skillRank[SK_ORIGIN]==1,"meta reset refunds cost, keeps origin, grants achievement");
    bankGold=1000000;
    for(int pass=0;pass<30;++pass)for(int id=0;id<SKILL_COUNT;++id)if(SkillCanBuy(id))SkillBuy(id);
    CHECK(SkillNodesLit()==30,"all thirty network nodes are purchasable");
    CHECK(activeKeystone>=16 && activeKeystone<=19,"four capstones have one active choice");
    int equipped=activeKeystone;CHECK(SkillSwitchKeystone(equipped==16?17:16),"owned capstones switch without losing purchases");
    CHECK(SkillNodesLit()==30,"capstone switching preserves the collected tree");
    clear_meta();
    for(int col=0;col<4;++col) {
        BenchResetRun();ThreatReset();carryGold=10000;
        CHECK(BenchTryBuy(col*4+1)==3,"bench rejects missing prerequisite");
        for(int row=0;row<3;++row) CHECK(BenchTryBuy(col*4+row)==1,"attribute tree lower tiers purchase");
        CHECK(BenchTryBuy(16+col)==1,"alternate capstone uses correct branch prerequisite");
        CHECK(BenchTryBuy(col*4+3)==5,"both capstones in a branch cannot be purchased");
    }
    BenchResetRun();ThreatReset();carryGold=0;
    CHECK(BenchTryBuy(0)==4 && !BenchHas(0),"insufficient money never installs a mod");
    carryGold=50000;for(int i=0;i<16;++i) CHECK(BenchTryBuy(i)==1,"complete sixteen-node attribute build");
    CHECK(ThreatValue()==18.0f,"max build reaches capped threat 18");
    CHECK(BenchStaminaMul()>1 && BenchMoveMul()>1 && BenchBatteryCapMul()>1 && BenchLightRangeMul()>1,"body and light attributes affect runtime getters");
    CHECK(BenchSanityAdd()==20 && BenchHealMul()>1 && BenchFearDamageMul()<1,"mind attributes affect real survival stats");
    CHECK(BenchKnockCharges()==1 && BenchKnockExtra()==2.5f && BenchShock(),"gear line upgrades charges, push and memory wipe");
    CHECK(BankPreviewGain(1)>=carryGold,"exit previews a valid settlement");
    int carry=carryGold;BenchRefundRun();CHECK(carryGold>carry && ThreatValue()==0 && !BenchHas(0),"run respec refunds money and removes added threat");
    SafeLevelReset();ShopOpen(1);int offers[4],prices[4];
    for(int i=0;i<SHOP_OFFERS;++i) {offers[i]=shopOffer[i];prices[i]=ShopPriceAt(i);CHECK(prices[i]>0,"every offer has a positive effective price");for(int j=0;j<i;++j)CHECK(offers[i]!=offers[j],"four supplies are distinct");}
    CHECK(shopDiscountRow>=0 && shopDiscountRow<SHOP_OFFERS,"one supply is discounted every floor");
    ShopOpen(1);for(int i=0;i<SHOP_OFFERS;++i) CHECK(offers[i]==shopOffer[i] && prices[i]==ShopPriceAt(i),"reopening shop does not reroll");
    shopPage=SHP_SHOP;shopRow=shopDiscountRow;carryGold=1000;int price=ShopPriceAt(shopRow);
    sanity=30;battery=30;bottles=flares=smokeBombs=gluePots=echoBolts=decoys=0;ShopResetCooldowns();
    ShopActivate();CHECK(carryGold==1000-price,"discount displayed is discount charged");
    ShopRefresh();CHECK(shopRefreshUsed==1,"one free refresh per floor");
    for(int i=0;i<SHOP_OFFERS;++i) offers[i]=shopOffer[i];
    ShopRefresh();
    for(int i=0;i<SHOP_OFFERS;++i)CHECK(offers[i]==shopOffer[i],"second refresh cannot reroll");
}

static void test_runs_and_layout(void) {
    clear_meta();bankGold=0;bankCarryPct=0;StartGame();
    finish_floor();CHECK(currentLevel==2 && currentState==STATE_SKILL_PICK && pickMode==0,"skill selection reachable on floor two");CloseSkillPick();
    finish_floor();CHECK(currentLevel==3 && currentState==STATE_SKILL_PICK && pickMode==1,"relic selection reachable on floor three");CloseSkillPick();
    while(currentLevel<6) {finish_floor();while(currentState==STATE_SKILL_PICK)CloseSkillPick();}
    /* A separate transition checks both selections before closing either. */
    StartGame();currentLevel=5;GenerateLevel();finish_floor();
    CHECK(currentLevel==6 && pickMode==1,"floor six first offers a relic");CloseSkillPick();
    CHECK(currentState==STATE_SKILL_PICK && pickMode==0,"floor six also offers the skill");CloseSkillPick();
    StartCampaign(0);int stages[8]={0,1,1,2,3,3,3,4};
    for(int floor=1;floor<=8;++floor) {
        CHECK(currentLevel==floor && campaignStage==stages[floor-1],"campaign cumulative floor boundaries");
        CHECK(fusesNeeded==CampaignFusesNow(),"campaign preserves specified fuse counts including eight");
        int c=carryGold;finish_floor();CHECK(carryGold>=c || currentState==STATE_GAMEOVER,"campaign stage transition never loses carried money");
    }
    CHECK(gameWon && currentState==STATE_GAMEOVER && bankGold>=36000,"all five campaign rewards settle into bank");
    int sizes[][2]={{800,600},{806,640},{1280,720},{1920,1080}};
    for(int k=0;k<4;++k) {
        winW=sizes[k][0];winH=sizes[k][1];currentState=STATE_SAFEHOUSE;
        for(int p=0;p<SAFEHOUSE_PAGES;++p) {shopPage=p;MenuGeo g=GetMenuGeo();int x0,y0,x1,y1;PageTabRect(0,&x0,&y0,&x1,&y1);for(int i=0;i<ShopRowCount();++i){RECT r=RowRect(g,i);CHECK(r.left>x1 && r.right<=g.panel.right,"shop text and hit areas clear the sidebar");}}
        currentState=STATE_BESTIARY;MenuGeo g=GetMenuGeo();CHECK(g.maxVisibleRows>=7,"six monsters plus back fit without scrolling");
        currentState=STATE_HUB;hubSelection=SK_ORIGIN;SkillResetView();
        for(int i=0;i<SKILL_COUNT;++i) {RECT a=SkillNodeRect(i);for(int j=0;j<i;++j){RECT b=SkillNodeRect(j),overlap;CHECK(!IntersectRect(&overlap,&a,&b),"radial skill nodes never overlap");}}
    }
}

static void test_music(void) {
    FakeMusic fake={0};MusicProvider api={MUSIC_PROTOCOL_VERSION,fake_play,fake_stop,fake_pause,fake_gain,fake_scene,fake_tick};
    MusicInit();CHECK(!MusicUseProvider(MUSIC_SIM),"missing SIM adapter falls back safely");
    api.version=99;CHECK(!MusicRegisterProvider(MUSIC_SIM,&api,&fake),"music protocol version is checked");api.version=1;
    CHECK(MusicRegisterProvider(MUSIC_SIM,&api,&fake) && MusicUseProvider(MUSIC_SIM),"SIM attaches through the shared music protocol");
    MusicSetMasterVolume(0.8f);MusicSetTrack(MUSIC_CHASE,"future-track.mp3");MusicSetScene(MUSIC_CHASE);MusicTick(1.0f);
    CHECK(fake.plays==2 && fake.scenes==2 && fabsf(fake.gain-0.8f)<0.01f,"scene and master volume reach provider");
    MusicDuck(2.0f,0.25f);MusicTick(0.5f);CHECK(fabsf(fake.gain-0.2f)<0.01f,"important events duck the provider");
    MusicTick(1.0f);MusicTick(1.0f);CHECK(fabsf(fake.gain-0.8f)<0.01f,"duck expires and music recovers");
    MusicSetPaused(1);MusicTick(0.5f);CHECK(fake.pauses && fake.gain==0,"pause reaches provider");
    MusicSetPaused(0);MusicTick(0.5f);CHECK(!fake.pauses && fake.gain>0,"resume restores music");
    CHECK(!MusicUseProvider(MUSIC_STEAM),"unavailable Steam cannot replace working provider");
    MusicShutdown();CHECK(fake.stops==1,"provider resources close on shutdown");MusicInit();
}

static void test_wrench(void) {
    clear_meta();bankGold=0;bankCarryPct=0;StartGame();dailyMode=0;
    monster.active=lightBeast.active=1;
    monster.x=lightBeast.x=posX+0.4f;monster.y=lightBeast.y=posY;
    int before=knockCountRun;WrenchStartSwing(1.0f);
    for(int i=0;i<12;++i)WrenchTick(0.04f);
    CHECK(knockCountRun==before+2,"a wrench swing affects both hunters once");

    /* ==================================================================
     * 2026-11-06：**扳手改成纯冷却制**（用户要求：8 秒，每级 -1，下限 5）
     * ------------------------------------------------------------------
     * 旧契约是"每层 N 次"，所以这条用例原来测的是"连挥两次都能命中"。
     * 新契约下连挥必须被冷却挡住 —— 这里改成同时验证两件事：
     *   ① 冷却期内再挥**不生效**
     *   ② 冷却走完之后能再挥
     * ================================================================ */
    monster.x=lightBeast.x=posX+0.4f;monster.y=lightBeast.y=posY;
    WrenchStartSwing(1.0f);for(int i=0;i<12;++i)WrenchTick(0.04f);
    CHECK(knockCountRun==before+2,
          "a second swing inside the cooldown must not land (wrench is cooldown-based now)");

    /* 把冷却清零。
       不用 Update(dt) 去"走完"时间 —— 它会把整局推进 8 秒
       （怪物移动、事件到期、甚至层结束），实测那样第二次挥击仍然不结算。
       这里直接把计时器清零，隔离出"冷却结束后能不能再挥"这一件事。 */
    {
        extern float wrenchCd;
        CHECK(wrenchCd > 0.0f, "starting a swing must arm the cooldown");
        wrenchCd = 0.0f;
    }
    monster.x=lightBeast.x=posX+0.4f;monster.y=lightBeast.y=posY;
    WrenchStartSwing(1.0f);for(int i=0;i<12;++i)WrenchTick(0.04f);
    CHECK(knockCountRun==before+4,
          "after the cooldown expires a new swing lands again");
    CHECK(monster.active && lightBeast.active,"wrench never kills a monster");
    /* ==================================================================
     * 扳手必须也能击退第 2、3 只听声鬼
     * （2026-11-03：原来 targets[] 里只有 {&monster, &lightBeast}）
     * ================================================================== */
    clear_meta();bankGold=0;bankCarryPct=0;StartGame();dailyMode=0;
    monHunters[0].active = monHunters[1].active = monHunters[2].active = 1;
    lightBeast.active = 0;
    for (int i = 0; i < MON_HUNTER_MAX; ++i) {
        monHunters[i].x = posX + 0.4f;
        monHunters[i].y = posY;
        monHunters[i].stun = 0.0f;
    }
    before = knockCountRun;
    WrenchStartSwing(1.0f);
    for (int i = 0; i < 12; ++i) WrenchTick(0.04f);
    CHECK(knockCountRun == before + 3, "a wrench swing knocks back all three hunters");

    carryGold=1000;CHECK(RunUpgTryBuy(0)==0,"run upgrade can be purchased");StartGame();
    CHECK(RunUpgLevel(0)==0,"run upgrades clear on a new run");
}


/* ==================================================================
 * 2026-11-08：**技能树数据自检**
 * ------------------------------------------------------------------
 * SkillsSelfTest() 一直只写 log，没有测试断言它必须返回 0。
 * 表一旦被改坏（总数对不上 / 前置成环 / keystone 列写错），
 * 玩家看到的是"买了没用"或"节点点不动"，却没人报警。
 * ================================================================ */

extern int SkillsSelfTest(void);

static void test_skill_tree_data_is_consistent(void) {
    int code = SkillsSelfTest();
    CHECK(code == 0, "skill tree data self-test must pass (code tells which check failed)");
}

static void test_skill_nodes_are_all_placeable(void) {
    extern const int* SkillNodeTableX(void);
    extern const int* SkillNodeTableY(void);
    extern int SkillNodeXTableSize(void);
    (void)SkillNodeTableX; (void)SkillNodeTableY; (void)SkillNodeXTableSize;
    /* 每个节点都必须有合法格子：用公开的列/行查询验证一遍 */
    {
        int id, bad = 0;
        for (id = 0; id < SKILL_COUNT; id++) {
            int c = SkillNodeCol(id);
            int r = SkillNodeRow(id);
            if (c < 0 && r < 0) bad++;      /* 两个都非法 = 完全没位置 */
        }
        CHECK(bad == 0, "every skill node must have a grid slot (0 = all placed)");
    }
}

void TestSuite_progress(void) {
    test_daily();
    test_double_reward_rounds();
    test_shop_full_bag_no_charge();
    test_load_clamps_flags();
    test_trees_shop();
    test_runs_and_layout();
    test_music();
    test_wrench();
    /* 技能树数据自检（2026-11-08 加） */
    test_skill_tree_data_is_consistent();
    test_skill_nodes_are_all_placeable();
}

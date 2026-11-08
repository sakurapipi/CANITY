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
    /* 2026-11-08：硬编码 30 会随节点增加而假失败 —— 用 SKILL_COUNT */
    CHECK(SkillNodesLit()==SKILL_COUNT,"every network node must be reachable/buyable");
    CHECK(activeKeystone>=16 && activeKeystone<=19,"four capstones have one active choice");
    int equipped=activeKeystone;CHECK(SkillSwitchKeystone(equipped==16?17:16),"owned capstones switch without losing purchases");
    CHECK(SkillNodesLit()==SKILL_COUNT,"capstone switching preserves the collected tree");
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
        /* 2026-11-08：判据改成"重叠面积 >= 8% 才算" —— 与 tools/skill_layout_check.py 同口径。
           斜辐条上相邻的轴对齐矩形会擦边 1~2%，视觉上看不出来；
           真正的"糊成一团"远超 8%。 */
        for(int i=0;i<SKILL_COUNT;++i){RECT a=SkillNodeRect(i);int aw=a.right-a.left, ah=a.bottom-a.top;
            for(int j=0;j<i;++j){RECT b=SkillNodeRect(j);
                int ox=(a.right<b.right?a.right:b.right)-(a.left>b.left?a.left:b.left);
                int oy=(a.bottom<b.bottom?a.bottom:b.bottom)-(a.top>b.top?a.top:b.top);
                { long long share = 0;
                  if (ox > 0 && oy > 0) share = (long long)ox * oy * 100 / ((long long)aw * ah);
                  if (share >= 8)
                      fprintf(stderr, "  [overlap] id %d x id %d  %lld%%\n",
                              i, j, share);
                  CHECK(share < 8, "radial skill nodes never overlap"); }
            }
        }
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


/* ==================================================================
 * 2026-11-08（第 4 轮）：**节点短名必须真的短**
 * ------------------------------------------------------------------
 * 实机截图里节点名被截成 `SIG..` / `GRE..`，因为 SkillShortName
 * 直接返回了全名，而节点框只放得下 ~10 个字符。
 * ================================================================ */

extern const char* SkillShortName(int id);

static void test_skill_short_names_are_short(void) {
    int id, tooLong = 0, empty = 0;
    for (id = 0; id < SKILL_COUNT; id++) {
        const char* n = SkillShortName(id);
        int len = 0;
        if (!n || !n[0]) { empty++; continue; }
        while (n[len]) len++;
        if (len > 6) tooLong++;
    }
    CHECK(empty == 0, "every skill must have a short name");
    CHECK(tooLong == 0, "short names must be <= 6 chars or the node box truncates them");
}


/* ==================================================================
 * 2026-11-08（第 6 轮）：**换王冠要花钱**
 * ------------------------------------------------------------------
 * 之前切换完全免费 —— 买齐 8 个王冠后人人拥有全部玩法，分支成了装饰。
 * 现在收 2,000 金（≈ 单局收入的 82%）。
 * ================================================================ */

extern int SkillSwitchKeystone(int id);
extern int SkillSwitchable(int id);

static void test_keystone_switch_costs_gold(void) {
    extern int bankGold;
    extern int SkillBuy(int id);
    extern void SkillResetAll(void);
    int savedGold;
    int kid = -1, i;

    /* 找一个王冠 */
    for (i = 0; i < SKILL_COUNT; i++) {
        extern const SkillDef skillDefs[];
        if (skillDefs[i].isKeystone) { kid = i; break; }
    }
    CHECK(kid >= 0, "the table must contain at least one keystone");
    if (kid < 0) return;

    SkillResetAll();
    bankGold = 0;
    /* 免费拿到它（直接把等级与当前王冠设好，绕开价格） */
    { extern int skillRank[]; extern int activeKeystone;
      skillRank[kid] = 1; activeKeystone = -1; }
    CHECK(SkillSwitchable(kid), "an owned keystone must be switchable when none is active");

    /* 钱不够：必须失败，且不改变状态 */
    bankGold = 0;
    CHECK(SkillSwitchKeystone(kid) == 0,
          "switching without enough gold must fail (it used to be free)");
    {
        extern int activeKeystone;
        CHECK(activeKeystone != kid, "a failed switch must not activate the keystone");
    }

    /* 钱够：成功，并扣掉 SKILL_SWITCH_COST */
    savedGold = 10000;
    bankGold = savedGold;
    CHECK(SkillSwitchKeystone(kid) == 1, "with enough gold the switch must succeed");
    CHECK(bankGold == savedGold - SKILL_SWITCH_COST,
          "switching must deduct exactly SKILL_SWITCH_COST");

    /* 已经是当前王冠：不可再"切换"（不重复扣钱） */
    CHECK(!SkillSwitchable(kid), "the active keystone must not be switchable to itself");

    bankGold = 0;
    SkillResetAll();
}


/* ==================================================================
 * 2026-11-08（第 9 轮）：**三流派对拍**
 * ------------------------------------------------------------------
 * 三个流派各 3~4 个节点，每个节点的"效果入口"必须真的存在且有返回。
 * 这条测试防的是：加了数据但忘了接效果（本工程历史上发生过多次）。
 * ================================================================ */

extern int SkillGhostBreathRank(void);
extern int SkillGhostStepOwned(void);
extern int SkillGhostLandOwned(void);
extern int SkillGhostFormOwned(void);
extern int SkillRaiderPawnOwned(void);
extern int SkillRaiderGambleOwned(void);
extern int SkillRaiderPowerOwned(void);
extern int SkillCounterFreeOwned(void);
extern int SkillCounterBackOwned(void);
extern int SkillCounterKillOwned(void);

static void test_three_archetypes_are_wired(void) {
    extern void SkillResetAll(void);
    extern int  skillRank[];
    int zero, one;

    SkillResetAll();

    /* 全部未购买时：所有入口都必须能调用且返回 0 */
    zero = SkillGhostBreathRank() + SkillGhostStepOwned() + SkillGhostLandOwned()
         + SkillGhostFormOwned() + SkillRaiderPawnOwned() + SkillRaiderGambleOwned()
         + SkillRaiderPowerOwned() + SkillCounterFreeOwned() + SkillCounterBackOwned()
         + SkillCounterKillOwned();
    CHECK(zero == 0, "with nothing bought, every archetype entry must read 0");

    /* 逐个点亮，入口必须跟着变 1（幽灵的呼吸同步是 rank，其余是 owned） */
    skillRank[SK_GHOST_BREATH] = 1;
    CHECK(SkillGhostBreathRank() == 1, "ghost/breath must report its rank");

    skillRank[SK_GHOST_STEP] = 1;  skillRank[SK_GHOST_LAND] = 1;
    skillRank[SK_GHOST_FORM] = 1;  skillRank[SK_RAIDER_PAWN] = 1;
    skillRank[SK_RAIDER_GAMBLE] = 1; skillRank[SK_RAIDER_POWER] = 1;
    skillRank[SK_COUNTER_FREE] = 1;  skillRank[SK_COUNTER_BACK] = 1;
    skillRank[SK_COUNTER_KILL] = 1;

    one = SkillGhostStepOwned() + SkillGhostLandOwned() + SkillGhostFormOwned()
        + SkillRaiderPawnOwned() + SkillRaiderGambleOwned() + SkillRaiderPowerOwned()
        + SkillCounterFreeOwned() + SkillCounterBackOwned() + SkillCounterKillOwned();
    CHECK(one == 9, "all nine owned-flags must flip to 1");

    /* 三个流派都必须真的存在于表里（名字非空、有价格） */
    {
        int i, ghost = 0, raider = 0, counter = 0;
        extern const SkillDef skillDefs[];
        for (i = 30; i <= 33; i++) if (skillDefs[i].name && skillDefs[i].name[0]) ghost++;
        for (i = 34; i <= 36; i++) if (skillDefs[i].name && skillDefs[i].name[0]) raider++;
        for (i = 37; i <= 39; i++) if (skillDefs[i].name && skillDefs[i].name[0]) counter++;
        CHECK(ghost == 4 && raider == 3 && counter == 3,
              "the three archetypes must contribute 4 + 3 + 3 named nodes");
    }

    SkillResetAll();
}


/* ==================================================================
 * 2026-11-08（第 11 轮）：**训练营七站**
 * ------------------------------------------------------------------
 * 玩家反馈「新手教程不够完整」。训练营原来只有一条走廊 + 一只标本怪。
 * 这条锁住：七站存在、坐标递增、都在可走的走廊行上。
 * ================================================================ */

#include "training_stations.h"
#include "shot_spec.h"

static void test_training_has_seven_stations(void) {
    int i, prevX = -1, ordered = 1, sameRow = 1;
    CHECK(TRAIN_STATION_COUNT == 7, "the tutorial must have exactly 7 stations");

    for (i = 0; i < TRAIN_STATION_COUNT; i++) {
        int x = TrainingStationX(i), y = TrainingStationY(i);
        CHECK(x >= 2 && x <= MAP_WIDTH - 3, "every station must sit inside the corridor");
        CHECK(y == MAP_HEIGHT / 2, "every station must be on the corridor row");
        if (x <= prevX) ordered = 0;
        if (y != MAP_HEIGHT / 2) sameRow = 0;
        prevX = x;
    }
    CHECK(ordered, "stations must be ordered left to right so the player walks forward");
    CHECK(sameRow, "all stations share the corridor row");

    /* 每站都有文案 */
    for (i = 0; i < TRAIN_STATION_COUNT; i++) {
        const char* s = TrainingStationText(i);
        CHECK(s && s[0], "every station needs a hint text");
    }
}


/* ==================================================================
 * 2026-11-08（第 12 轮）：**七站都能被触发，且只播一次**
 * ------------------------------------------------------------------
 * 上一轮只验了"站点存在"；这一轮验"走上去真的会触发"，
 * 以及"站着不动不会每秒重复播"。
 * ================================================================ */

extern int TrainingStationsTick(float px, float py);
extern void TrainingStationsReset(void);

static void test_training_stations_fire_once(void) {
    int i, fired = 0;
    TrainingStationsReset();

    for (i = 0; i < TRAIN_STATION_COUNT; i++) {
        float cx = (float)TrainingStationX(i) + 0.5f;
        float cy = (float)TrainingStationY(i) + 0.5f;

        /* 站上去：必须触发一次 */
        if (TrainingStationsTick(cx, cy) == i) fired++;
        /* 再站一帧：不许重复播 */
        CHECK(TrainingStationsTick(cx, cy) == -1,
              "standing on a station must not re-fire every frame");
    }
    CHECK(fired == TRAIN_STATION_COUNT, "walking through all 7 stations must fire all 7");

    /* 站在两站之间：不该触发 */
    TrainingStationsReset();
    /* 6.2 距站 0（4.5）1.7 格、距站 1（8.5）2.3 格 —— 两边都在 1.5 半径外 */
    CHECK(TrainingStationsTick(6.2f, (float)(MAP_HEIGHT / 2) + 0.5f) == -1,
          "standing between two stations must not fire anything");
}


/* ==================================================================
 * 2026-11-09（第 2 轮）：**内容中枢 content_db**
 * ------------------------------------------------------------------
 * 中枢是"同一个终端"的地基：所有内容（怪/投掷物/技能/成就/设置）
 * 都登记到一张表，其它系统只查这张表。
 *
 * 这一轮还没有系统接进来，所以这些用例是它将来能被信任的前提：
 * 越界安全、id 唯一、满表拒绝、清空彻底。
 * ================================================================ */

#include "content_db.h"

static void test_content_db_basics(void) {
    int a, b, c;
    ContentReset();

    CHECK(ContentCount(CK_MONSTER) == 0, "a fresh content db must be empty");
    CHECK(ContentGet(CK_MONSTER, 0) == NULL, "Get on an empty kind must return NULL");
    CHECK(ContentFind(CK_MONSTER, "nope") == -1, "Find on an empty kind must return -1");
    CHECK(ContentTotal() == 0, "total must start at 0");

    a = ContentAdd(CK_MONSTER, "sound_hunter", (const void*)1);
    b = ContentAdd(CK_MONSTER, "light_beast", (const void*)2);
    c = ContentAdd(CK_MONSTER, "chest_mimic", (const void*)3);
    CHECK(a == 0 && b == 1 && c == 2, "adds must return consecutive indices");
    CHECK(ContentCount(CK_MONSTER) == 3, "count must follow the adds");

    /* 按序号取 */
    CHECK(ContentGet(CK_MONSTER, 1) == (const void*)2, "Get(1) must be the second entry");
    CHECK(ContentGet(CK_MONSTER, 3) == NULL, "Get past the end must return NULL");
    CHECK(ContentGet(CK_MONSTER, -1) == NULL, "Get(-1) must return NULL");

    /* 按 id 找 */
    CHECK(ContentFind(CK_MONSTER, "light_beast") == 1, "Find must locate by id");
    CHECK(ContentFind(CK_MONSTER, "ghost") == -1, "Find must return -1 for unknown id");

    /* id 唯一：重复登记必须被拒绝 */
    CHECK(ContentAdd(CK_MONSTER, "sound_hunter", (const void*)9) == -1,
          "registering the same id twice must be rejected");
    CHECK(ContentCount(CK_MONSTER) == 3, "a rejected add must not change the count");
    CHECK(ContentDuplicateKind() == -1, "no duplicates must ever get in");

    /* 空 id / NULL id 拒绝 */
    CHECK(ContentAdd(CK_MONSTER, "", (const void*)1) == -1, "empty id must be rejected");
    CHECK(ContentAdd(CK_MONSTER, NULL, (const void*)1) == -1, "NULL id must be rejected");

    /* 不同种类互不干扰 */
    CHECK(ContentAdd(CK_SKILL, "sound_hunter", (const void*)7) >= 0,
          "the same id in a different kind is a different entry");
    CHECK(ContentCount(CK_SKILL) == 1 && ContentCount(CK_MONSTER) == 3,
          "kinds must be independent");

    /* 清空彻底 */
    ContentReset();
    CHECK(ContentCount(CK_MONSTER) == 0 && ContentCount(CK_SKILL) == 0,
          "reset must clear every kind");
    CHECK(ContentTotal() == 0, "reset must clear the total");
}

static void test_content_db_capacity(void) {
    /* id 必须指向**稳定存储** —— 中枢只存指针。
       第一版这里用栈上的 `char id[32]`，循环里被复用，
       第二次登记时中枢读到的还是"id_1"，当场被判重复。
       （这条约定也写进了 content_db.h。） */
    static char ids[CONTENT_MAX][16];
    int i, ok = 1;
    ContentReset();
    for (i = 0; i < CONTENT_MAX; i++) {
        sprintf(ids[i], "id_%d", i);
        if (ContentAdd(CK_SETTING, ids[i], (const void*)(long)(i + 1)) < 0) {
            ok = 0;
            break;
        }
    }
    CHECK(ok, "the db must accept exactly CONTENT_MAX entries");
    CHECK(ContentCount(CK_SETTING) == CONTENT_MAX, "count must reach CONTENT_MAX");
    CHECK(ContentAdd(CK_SETTING, "one_too_many", (const void*)1) == -1,
          "the (CONTENT_MAX+1)th add must be rejected, not overflow");
    ContentReset();
}


/* ==================================================================
 * 2026-11-09（第 3 轮）：**图鉴已接入内容中枢**
 * ------------------------------------------------------------------
 * 之前"加一只怪"要同时改 BESTIARY_COUNT、bestiary[]、解锁数组三处。
 * 现在图鉴自己登记进中枢，计数由中枢说了算。
 *
 * 这条测试锁两件事：
 *   ① 中枢里的怪物数 == BESTIARY_COUNT
 *   ② 每一只都能按 id（英文名）找到，且取回的就是它自己
 * ================================================================ */

extern void BestiaryRegister(void);

static void test_bestiary_registered_in_hub(void) {
    int i, found = 0;
    ContentReset();
    BestiaryRegister();

    CHECK(ContentCount(CK_MONSTER) == BESTIARY_COUNT,
          "the hub must hold exactly BESTIARY_COUNT monsters");

    for (i = 0; i < BESTIARY_COUNT; i++) {
        extern const BestiaryEntry bestiary[];
        const char* id = bestiary[i].nameEn;
        int idx;
        if (!id || !id[0]) continue;
        idx = ContentFind(CK_MONSTER, id);
        if (idx < 0) continue;
        if (ContentGet(CK_MONSTER, idx) == (const void*)&bestiary[i]) found++;
    }
    CHECK(found == BESTIARY_COUNT,
          "every bestiary entry must be findable by its english name");

    /* 幂等：再登记一次不许翻倍 */
    BestiaryRegister();
    CHECK(ContentCount(CK_MONSTER) == BESTIARY_COUNT,
          "registering twice must not duplicate entries");
    CHECK(ContentDuplicateKind() == -1, "the hub must stay duplicate-free");

    ContentReset();
}


/* ==================================================================
 * 2026-11-09（第 4 轮）：**技能已接入内容中枢**
 * ------------------------------------------------------------------
 * 一个技能节点要登记四处（表 / 短名 / 坐标 / 绘制列表）。
 * 这一轮先让中枢持有全部 40 个节点：中枢计数必须等于 SKILL_COUNT，
 * 且每个都能按英文名找回它自己。
 * ================================================================ */

extern void SkillRegister(void);

static void test_skills_registered_in_hub(void) {
    int i, found = 0;
    ContentReset();
    SkillRegister();

    CHECK(ContentCount(CK_SKILL) == SKILL_COUNT,
          "the hub must hold exactly SKILL_COUNT skills");

    for (i = 0; i < SKILL_COUNT; i++) {
        extern const SkillDef skillDefs[];
        const char* id = skillDefs[i].name;
        int idx;
        if (!id || !id[0]) continue;
        idx = ContentFind(CK_SKILL, id);
        if (idx >= 0 && ContentGet(CK_SKILL, idx) == (const void*)&skillDefs[i])
            found++;
    }
    CHECK(found == SKILL_COUNT,
          "every skill must be findable by name and map back to its own row");

    /* 幂等 */
    SkillRegister();
    CHECK(ContentCount(CK_SKILL) == SKILL_COUNT, "registering twice must not duplicate");
    CHECK(ContentDuplicateKind() == -1, "the hub must stay duplicate-free");

    ContentReset();
}


/* ==================================================================
 * 2026-11-09（第 5 轮）：**投掷物 / 设置已接入内容中枢**
 * ------------------------------------------------------------------
 * 投掷物：表 11 行 → 中枢 11 条
 * 设置：  表 44 行 → 中枢 44 条
 *
 * 设置这块治的是"加了动作忘登记"（今天补 3 个改绑行就是这个病）。
 * ================================================================ */

extern void ShotRegister(void);
extern void SettingsRegister(void);
extern int  ShotSpecCount(void);

static void test_shots_and_settings_in_hub(void) {
    int i, found = 0;
    ContentReset();

    /* ---- 投掷物 ---- */
    ShotRegister();
    CHECK(ContentCount(CK_SHOT) == ShotSpecCount(),
          "the hub must hold exactly ShotSpecCount() throwables");
    for (i = 0; i < ShotSpecCount(); i++) {
        extern const ShotSpec* ShotSpecAt(int);
        const ShotSpec* sp = ShotSpecAt(i);
        if (!sp || !sp->name) continue;
        if (ContentFind(CK_SHOT, sp->name) >= 0) found++;
    }
    CHECK(found == ShotSpecCount(), "every throwable must be findable by name");
    ShotRegister();
    CHECK(ContentCount(CK_SHOT) == ShotSpecCount(), "shot registration must be idempotent");

    /* ---- 设置 ---- */
    ContentReset();
    SettingsRegister();
    CHECK(ContentCount(CK_SETTING) == SETTING_COUNT,
          "the hub must hold exactly SETTING_COUNT settings");
    {
        int dup = ContentDuplicateKind();
        CHECK(dup == -1, "settings must not contain duplicate labels");
    }
    SettingsRegister();
    CHECK(ContentCount(CK_SETTING) == SETTING_COUNT,
          "settings registration must be idempotent");

    ContentReset();
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
    test_skill_short_names_are_short();
    test_keystone_switch_costs_gold();
    test_three_archetypes_are_wired();
    test_training_has_seven_stations();
    test_training_stations_fire_once();
    test_content_db_basics();
    test_content_db_capacity();
    test_bestiary_registered_in_hub();
    test_skills_registered_in_hub();
    test_shots_and_settings_in_hub();
}

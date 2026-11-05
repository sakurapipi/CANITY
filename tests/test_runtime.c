/* Integration checks link the real game modules; all saves are isolated. */
#include "common.h"
#include "game.h"
#include "entity.h"
#include "skills.h"
#include "bench.h"
#include "shop.h"
#include "daily.h"
#include "campaign.h"
#include "achievements.h"
#include "save.h"
#include "save_format.h"   /* R8: SaveEnvelope / SaveEnvelopeSeal */
#include "steam_zone.h"    /* R8: SteamZoneCloudDecide / STEAM_SYNC_* */
#include "sound_event.h"   /* B01: SoundEmit / SoundKind */
#include "wallet.h"
#include "threat.h"
#include "ui.h"
#include "render.h"
#include "sprites.h"
#include "music.h"
#include "bestiary.h"
#include "warden.h"
#include "state.h"
#include "upgrade_run.h"
#include <stddef.h>

extern int carryAtRunStart,exitGoDeeper,exitChoiceOpen;
extern int knockCountRun,throwHitCountRun,echoCellsTotalRun;
extern int lastRunGold,lastRunKept,lastRunCaught;
extern int shopRefreshFloor,shopRefreshUsed,contractKind;
extern void BankSettle(int survived);
extern int BankPreviewGain(int survived);
extern void CloseSkillPick(void);
extern void GenerateLevel(void);
extern void SkillResetView(void);
static int checks=0;
#define CHECK(c,m) do { ++checks; if(!(c)) { fprintf(stderr,"FAIL: %s (line %d)\n",m,__LINE__);exit(1); } } while(0)
static void clear_meta(void) { memset(skillRank,0,sizeof(int)*SKILL_COUNT);skillRank[SK_ORIGIN]=1; }
static void daily_case(int goal,int target) {
    int a[15]={0};a[0]=goal;a[1]=target;a[5]=(int)DailySeedToday();DailyLoadFrom(a);
}
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
static void test_wallet_save(void) {
    clear_meta();bankGold=1000;bankHistMax=1000;bankCarryPct=5;
    StartGame();CHECK(bankGold==750 && carryGold==250 && carryAtRunStart==250,"opening withdrawal stays in the player's pocket");
    carryGold=350;WalletSettle(1,100);CHECK(bankGold==1125,"settlement returns principal plus multiplied profit once");
    carryGold=101;WalletSettle(0,0);CHECK(bankGold==1160,"death returns exactly rounded 35 percent");
    CHECK(carryGold==0 && carryAtRunStart==0,"settlement cannot be paid a second time");
    bankGold=6543;bankHistMax=8000;bankCarryPct=3;carryGold=321;carryAtRunStart=200;
    knockCountRun=7;throwHitCountRun=8;echoCellsTotalRun=99;
    skillRank[29]=2;g_achievements[4].unlocked=1;g_achievements[4].progress=15;
    bestiarySeen=1;BestiaryMarkEscaped(0);BestiaryMarkEscaped(0);BestiaryMarkEscaped(0);
    runInProgress=1;SaveGame();CHECK(g_saveState==1,"save write succeeds");
    bankGold=0;carryGold=0;carryAtRunStart=0;skillRank[29]=0;g_achievements[4].unlocked=0;
    CHECK(LoadSettings(),"unfinished run can be loaded");
    CHECK(bankGold==6543 && bankCarryPct==3,"bank restores on startup before entering a run");
    CHECK(g_achievements[4].unlocked && skillRank[29]==2,"persistent achievements and new skills survive restart");
    InitAchievements();CHECK(g_achievements[4].unlocked,"opening achievements preserves unlocks");
    CHECK(BestiaryStage(0)==BST_COUNTER,"bestiary knowledge survives restart");
    ContinueRun();CHECK(carryGold==321 && carryAtRunStart==200,"saved carry and principal are distinct");
    CHECK(knockCountRun==7 && throwHitCountRun==8 && echoCellsTotalRun==99,"run counters survive resume");
    /* v25 uses the exact prefix of the new save, not a shifted struct. */
    SaveBlob old={0};old.magic=SAVE_MAGIC;old.version=25;old.bankGoldSave=4567;old.bankHistMaxSave=8000;old.skillRank[1]=1;
    const char* dir=getenv("CANITY_DATA_DIR");char path[MAX_PATH];snprintf(path,sizeof(path),"%s\\canity0.sav",dir);
    FILE* f=fopen(path,"wb");CHECK(f!=NULL,"create isolated v25 fixture");
    fwrite(&old,1,offsetof(SaveBlob,dailyQuestDate),f);fclose(f);
    LoadSettings();CHECK(bankGold==4567 && skillRank[1]==1,"v25 migration keeps bank and skill progress");
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
static void finish_floor(void) {
    currentState=STATE_PLAYING;flashTime=0.0f;exitChoiceOpen=0;exitGoDeeper=1;breakerOpen=1;
    posX=exitX;posY=exitY;sanity=SanityMax();monster.active=lightBeast.active=0;WardenReset();
    memset(keyPressed,0,sizeof(keyPressed));Update(0.008f);
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

typedef struct {int plays,stops,pauses,scenes,ticks;float gain;} FakeMusic;
static int fake_play(void* p,const char* track,int loop){(void)track;(void)loop;((FakeMusic*)p)->plays++;return 1;}
static void fake_stop(void* p){((FakeMusic*)p)->stops++;}
static void fake_pause(void* p,int pause){((FakeMusic*)p)->pauses=pause;}
static void fake_gain(void* p,float gain){((FakeMusic*)p)->gain=gain;}
static void fake_scene(void* p,MusicScene scene){(void)scene;((FakeMusic*)p)->scenes++;}
static void fake_tick(void* p,float dt){(void)dt;((FakeMusic*)p)->ticks++;}
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
    monster.x=lightBeast.x=posX+0.4f;monster.y=lightBeast.y=posY;
    WrenchStartSwing(1.0f);for(int i=0;i<12;++i)WrenchTick(0.04f);
    CHECK(knockCountRun==before+4,"later swings reset their hit masks");
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
 * 听声鬼三只：**第二、第三只必须真的会走**
 * ------------------------------------------------------------------
 * 2026-11-03 的回归：UpdateOneHunter 里那句
 *     MonPath((int)M->x, ...)
 * 调的是写死 &monster 的老封装，于是 2/3 号算出的路径全写进 1 号的
 * path[]，自己 pathLen 恒为 0 —— 召出来就站着不动。
 * 这个测试直接跑真实更新循环，位置必须变化。
 * ================================================================== */
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


/* ==================================================================
 * 听声鬼三只的**剪影必须能分开**（art_direction.h 铁律一 AD-01）
 * ------------------------------------------------------------------
 * 做法和规范一样：把主体当填黑，缩到 32×32 网格，
 * 只比较「有没有实体」的二维掩码，不看颜色。
 * 任意两只的掩码差异都必须足够大，否则玩家在黑暗里只能靠颜色认怪。
 * ================================================================== */
static void mask32(int sprId, unsigned char out[32][32]) {
    Sprite* sp = SpriteGet(sprId);
    memset(out, 0, 32 * 32);
    if (!sp || !sp->px || sp->w <= 0 || sp->h <= 0) return;
    for (int gy = 0; gy < 32; gy++) {
        for (int gx = 0; gx < 32; gx++) {
            int x0 = gx * sp->w / 32, x1 = (gx + 1) * sp->w / 32;
            int y0 = gy * sp->h / 32, y1 = (gy + 1) * sp->h / 32;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            int hit = 0;
            for (int y = y0; y < y1 && !hit; y++)
                for (int x = x0; x < x1; x++)
                    if ((sp->px[(size_t)y * sp->w + x] >> 24) != 0) { hit = 1; break; }
            out[gy][gx] = (unsigned char)hit;
        }
    }
}

static void test_hunter_models(void) {
    InitPixels();
    SpritesInit();               /* 烘焙精灵（纯 CPU，不需要窗口） */
    CHECK(SpritesReady(), "sprites baked");

    CHECK(SpriteGet(SPR_MON_HUNTER) != NULL, "hunter 1 sprite exists");
    CHECK(SpriteGet(SPR_MON_HUNTER2) != NULL, "hunter 2 sprite exists");
    CHECK(SpriteGet(SPR_MON_HUNTER3) != NULL, "hunter 3 sprite exists");

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


/* ==================================================================
 * 换层必须把第 2、3 只听声鬼清掉（2026-11-03 的 bug）
 * ------------------------------------------------------------------
 * GenerateLevel() 原来只重生第一只，第 2/3 只 active 会原样带进新层，
 * 玩家一进第二层就三只同时在场。
 * ================================================================== */
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

/* ==================================================================
 * 2026-11-06：**破门石一次撞击只结算一次逻辑噪音**
 * ------------------------------------------------------------------
 * 背景：`throwing.c` 里破锁会先 `MakeNoise(20)`，然后落地/撞墙分支
 * 又各来一次 `MakeNoise(15)` —— 同一块石头报两次。
 *
 * 实测修正（重要）：`suspicion.c` 的 `SuspicionAdd` 会把
 * **同源且 2 格内**的点合并（取更高强度/更长寿命），
 * 而 20 和 15 都 > 14 阈值，同属 `SUSP_SEARCH` ——
 * 所以这两次在当前实现里本来就会并成一个点，**玩家感觉不到差别**。
 *
 * 因此这个测试是**回归守卫**，不是"修复前失败"的证明：
 * 它钉住「一次破锁 = 一个可疑点」这条不变量，
 * 等 v2.4-beta 的 SoundEvent 把强度与类别分开之后，它会真正开始起作用。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06：**写盘失败的负路径**
 * ------------------------------------------------------------------
 * R6 改的是 SaveBackendWrite 的错误传播。这里验证它真的会返回非 0，
 * 而不是吞掉失败谎报成功 —— 存档层最危险的失败模式就是这一种。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-alpha R8）：**长度不同的合法存档不算损坏**
 * ------------------------------------------------------------------
 * 这是 N1 的正面证据。旧实现用本地长度去要求云端，
 * 只要两份 payload 长度不同就 STEAM_SYNC_ERROR ——
 * 而拆块（SC_SETTINGS / SC_WORLD）必然改变长度。
 *
 * 用 SteamZoneCloudDecide（纯函数）测，不需要 Steam 客户端。
 * 注意：这**不等于** Steam 真机通过 —— 传输层仍是 UNKNOWN。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-alpha R12）：**未知块经过真实读写流程仍然保留**
 * ------------------------------------------------------------------
 * 这是 v2.4-alpha 的核心验收（S3 / G02）。
 * 走的是 save.c 里**真实的**捕获 → 重建逻辑（SaveTestSerialize /
 * SaveTestDeserialize 只是把两个 static 原样导出），
 * 不是测试专用的另一套序列化。
 *
 * 步骤：
 *   ① 手搓一份合法 CSAV：SC_LEGACY29 + 未知块 0x7777
 *   ② 反序列化（应当捕获目录）
 *   ③ 改一个已知字段（lang）
 *   ④ 重新序列化
 *   ⑤ 解析新字节：0x7777 必须还在，且**逐字节相同**
 *   ⑥ 已知块的 lang 必须是新值
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（v2.4-beta B01）：**声音事件中心**
 * ------------------------------------------------------------------
 * 这一组钉两件事：
 *   ① 类别由 kind 决定，**不由半径决定** —— 这是 B01 存在的全部理由。
 *      同一个 kind 换不同 maxRadius，落到可疑点表的 source/ttl 必须不变。
 *   ② 同帧同 eventId 只结算一次（一次物理事件 = 一个逻辑事件），
 *      但 eventId == 0 时不去重（真实的连续脚步不能被合并）。
 * ================================================================== */
static void test_sound_event_center(void) {
    SoundEvent ev;

    /* ---- ① 类别由 kind 决定，与半径无关 ---- */
    {
        /* 同一 kind、三个不同半径：ttl / decay / susSource 必须一致 */
        float ttls[3];
        float decs[3];
        int   srcs[3];
        static const float radii[3] = { 4.0f, 14.0f, 26.0f };

        for (int i = 0; i < 3; i++) {
            memset(&ev, 0, sizeof(ev));
            ev.kind = SK_IMPACT;
            ev.x = 10.0f; ev.y = 10.0f;
            ev.strength = 52.0f;
            ev.maxRadius = radii[i];
            ev.eventId = 0u;
            SuspicionReset();
            CHECK(SoundEmit(&ev) == 1, "SK_IMPACT must emit");

            /* 从可疑点表里读实际落下的 ttl / decay / source */
            int found = -1;
            for (int k = 0; k < SUSP_MAX; k++) {
                if (susp[k].strength > 0.0f) { found = k; break; }
            }
            CHECK(found >= 0, "emitted event must land in the suspicion table");
            ttls[i] = susp[found].ttl;
            decs[i] = susp[found].decay;
            srcs[i] = susp[found].source;
        }
        CHECK(ttls[0] == ttls[1] && ttls[1] == ttls[2],
              "ttl must come from kind, not from maxRadius");
        CHECK(decs[0] == decs[1] && decs[1] == decs[2],
              "decay must come from kind, not from maxRadius");
        CHECK(srcs[0] == srcs[1] && srcs[1] == srcs[2],
              "suspicion source must come from kind, not from maxRadius");
        CHECK(srcs[0] == SUSP_SEARCH, "SK_IMPACT must map to SUSP_SEARCH");

        /* SK_WALK 与 SK_IMPACT 必须真的不同（否则这套分类没意义） */
        CHECK(SoundKindTtl(SK_WALK) < SoundKindTtl(SK_IMPACT),
              "SK_WALK must have a shorter ttl than SK_IMPACT");
        CHECK(SoundKindDecay(SK_WALK) > SoundKindDecay(SK_IMPACT),
              "SK_WALK must decay faster than SK_IMPACT");
        CHECK(SoundKindSusSource(SK_VOICE) == SUSP_DECOY,
              "SK_VOICE must map to SUSP_DECOY");
    }

    /* ---- ② 同帧同 eventId 去重 ---- */
    {
        SoundEventResetStats();
        memset(&ev, 0, sizeof(ev));
        ev.kind = SK_IMPACT;
        ev.x = 10.0f; ev.y = 10.0f;
        ev.strength = 52.0f;
        ev.eventId = 0xBEEFu;

        SuspicionReset();
        CHECK(SoundEmit(&ev) == 1,  "first emit with an eventId must land");
        CHECK(SoundEmit(&ev) == 0,  "the same eventId in the same frame must be deduped");
        CHECK(SoundEventTotalDeduped() == 1, "dedupe counter must record it");

        /* 换一帧 → 同一个 eventId 又可以了 */
        SoundEventTick();
        CHECK(SoundEmit(&ev) == 1, "after a tick the same eventId is allowed again");
    }

    /* ---- ③ eventId == 0 不去重 ---- */
    {
        SoundEventResetStats();
        memset(&ev, 0, sizeof(ev));
        ev.kind = SK_WALK;
        ev.x = 5.0f; ev.y = 5.0f;
        ev.strength = 20.0f;
        ev.eventId = 0u;
        SuspicionReset();
        CHECK(SoundEmit(&ev) == 1, "eventId 0 emit #1 must land");
        CHECK(SoundEmit(&ev) == 1, "eventId 0 must NOT be deduped (real repeated steps)");
    }

    /* ---- ④ 数值夹取与非法输入 ---- */
    {
        memset(&ev, 0, sizeof(ev));
        ev.kind = SK_IMPACT;
        ev.x = 3.0f; ev.y = 3.0f;
        ev.strength = 999.0f;            /* 会被夹到 100 */
        SoundEventResetStats();
        CHECK(SoundEmit(&ev) == 1, "over-strength event must still emit");
        CHECK(SoundEventLastStrength() <= 100.0f,
              "strength must be clamped to 100");

        ev.x = 0.0f / 0.0f;              /* NaN 坐标 */
        CHECK(SoundEmit(&ev) == 0, "NaN coordinates must be rejected");
    }

    SoundEventTick();
    SoundEventResetStats();
    SuspicionReset();
}

static void test_save_unknown_chunk_roundtrip(void) {
    extern int SaveTestSerialize(const SaveBlob*, unsigned char**);
    extern int SaveTestDeserialize(const void*, long, SaveBlob*);

    /* ---- ① 手搓：已知块 + 未知块 ---- */
    static SaveBlob blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = SAVE_MAGIC;
    blob.version = SAVE_VERSION;
    blob.lang = 0;

    static const unsigned char unknown[12] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };

    static unsigned char buf[16384];   /* SaveBlob 约 6.5 KB，留足 */
    SaveChunkDesc d[2];
    const void* body[2];
    d[0].id = SC_LEGACY29; d[0].schema = 1; d[0].size = (unsigned)sizeof(blob);
    body[0] = &blob;
    d[1].id = 0x7777;      d[1].schema = 9; d[1].size = (unsigned)sizeof(unknown);
    body[1] = unknown;

    int pay = SaveContainerPack(d, body, 2, buf + sizeof(SaveEnvelope),
                                (int)(sizeof(buf) - sizeof(SaveEnvelope)), NULL);
    CHECK(pay > 0, "test fixture: two-chunk payload must pack");
    SaveEnvelope h;
    SaveEnvelopeSeal(&h, buf + sizeof(SaveEnvelope), pay, 1111u);
    memcpy(buf, &h, sizeof(h));
    int total = (int)sizeof(SaveEnvelope) + pay;

    /* ---- ② 反序列化 ---- */
    static SaveBlob loaded;
    CHECK(SaveTestDeserialize(buf, total, &loaded) != 0,
          "a valid two-chunk CSAV must load");
    CHECK(loaded.lang == 0, "test fixture: lang starts at 0");

    /* ---- ③ 改一个已知字段 ---- */
    loaded.lang = 7;

    /* ---- ④ 重新序列化 ---- */
    unsigned char* out = NULL;
    int outLen = SaveTestSerialize(&loaded, &out);
    CHECK(outLen > 0 && out != NULL, "re-serializing a loaded save must succeed");

    if (outLen > 0 && out) {
        /* ---- ⑤ 未知块必须逐字节保留 ---- */
        const void* got = NULL;
        int gotLen = 0;
        CHECK(SaveContainerFind(out + sizeof(SaveEnvelope), outLen - (int)sizeof(SaveEnvelope),
                                0x7777, &got, &gotLen) == 1,
              "unknown chunk 0x7777 must survive a load-modify-save cycle");
        CHECK(gotLen == (int)sizeof(unknown),
              "unknown chunk must keep its size");
        CHECK(got && memcmp(got, unknown, sizeof(unknown)) == 0,
              "unknown chunk must keep its bytes exactly");

        /* ---- ⑥ 已知块是新内容 ---- */
        SaveEnvelope nh;
        memcpy(&nh, out, sizeof(nh));
        CHECK(SaveEnvelopeValid(&nh, out + sizeof(SaveEnvelope),
                                outLen - (int)sizeof(SaveEnvelope)) == 1,
              "re-serialized envelope must carry a valid checksum");

        const void* kb = NULL;
        int klen = 0;
        CHECK(SaveContainerFind(out + sizeof(SaveEnvelope), outLen - (int)sizeof(SaveEnvelope),
                                SC_LEGACY29, &kb, &klen) == 1,
              "known chunk must still be present");
        if (kb && klen >= (int)sizeof(SaveBlob)) {
            SaveBlob check;
            memcpy(&check, kb, sizeof(check));
            CHECK(check.lang == 7, "the modified field must be persisted in the known chunk");
        }

        /* 块顺序：已知块在前、未知块在后（原位替换） */
        {
            SaveChunkDesc first;
            CHECK(SaveContainerAt(out + sizeof(SaveEnvelope),
                                  outLen - (int)sizeof(SaveEnvelope), 0, &first, NULL) == 1,
                  "chunk 0 must be readable");
            CHECK(first.id == SC_LEGACY29,
                  "chunk order must be preserved (known chunk stays in place)");
        }

        free(out);
    }

    /* ---- ⑦ 重复 ID 必须被拒绝，而不是静默挑一份 ---- */
    {
        unsigned char dup[4096];
        SaveChunkDesc dd[2];
        const void* db[2];
        dd[0].id = 0x7777; dd[0].schema = 1; dd[0].size = 4; db[0] = unknown;
        dd[1].id = 0x7777; dd[1].schema = 1; dd[1].size = 4; db[1] = unknown + 4;
        int dp = SaveContainerPack(dd, db, 2, dup + sizeof(SaveEnvelope),
                                   (int)(sizeof(dup) - sizeof(SaveEnvelope)), NULL);
        CHECK(dp > 0, "test fixture: duplicate-id payload must pack");
        SaveEnvelope dh;
        SaveEnvelopeSeal(&dh, dup + sizeof(SaveEnvelope), dp, 2222u);
        memcpy(dup, &dh, sizeof(dh));

        static SaveBlob dloaded;
        CHECK(SaveTestDeserialize(dup, (int)sizeof(SaveEnvelope) + dp, &dloaded) == 0,
              "a save with duplicate chunk ids must be rejected");
    }

    /* 收尾：让后续用例从干净状态开始（carry 已被置为只读） */
    {
        static SaveBlob clean;
        memset(&clean, 0, sizeof(clean));
        clean.magic = SAVE_MAGIC;
        clean.version = SAVE_VERSION;
        unsigned char* cout = NULL;
        int cl = SaveTestSerialize(&clean, &cout);
        if (cout) free(cout);
        (void)cl;
    }
}

static void test_cloud_decide_length(void) {
    extern int SteamZoneCloudDecide(const void*, int, const void*, int, int*);

    unsigned char A[512], B[512];
    SaveEnvelope h;
    const unsigned char payA[16] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };
    const unsigned char payB[40] = { 9,9,9,9 };     /* 更长的 payload */

    /* A：短的那份 */
    memcpy(A + sizeof(h), payA, sizeof(payA));
    SaveEnvelopeSeal(&h, A + sizeof(h), (int)sizeof(payA), 1000u);
    memcpy(A, &h, sizeof(h));
    int lenA = (int)sizeof(h) + (int)sizeof(payA);

    /* B：长的那份，stamp 更新 */
    memcpy(B + sizeof(h), payB, sizeof(payB));
    SaveEnvelopeSeal(&h, B + sizeof(h), (int)sizeof(payB), 2000u);
    memcpy(B, &h, sizeof(h));
    int lenB = (int)sizeof(h) + (int)sizeof(payB);

    int win = 0;

    /* ① 内容相同 → SAMECL（自身对比） */
    CHECK(SteamZoneCloudDecide(A, lenA, A, lenA, &win) == STEAM_SYNC_SAMECL,
          "identical local/remote must be SAMECL");

    /* ② 长度不同 + 云端更新 → CLOUDWIN，且 out 长度是**远端**的 */
    win = 0;
    CHECK(SteamZoneCloudDecide(A, lenA, B, lenB, &win) == STEAM_SYNC_CLOUDWIN,
          "different length must not be treated as corruption (cloud newer)");
    CHECK(win == lenB, "CLOUDWIN must report the remote length, not the local one");

    /* ③ 长度不同 + 本地更新 → LOCALWIN（不覆盖云端） */
    win = 0;
    CHECK(SteamZoneCloudDecide(B, lenB, A, lenA, &win) == STEAM_SYNC_LOCALWIN,
          "different length must not be treated as corruption (local newer)");

    /* ④ 同 stamp 不同内容 → CONFLICT，不裁决 */
    {
        unsigned char C[512];
        memcpy(C + sizeof(h), payB, sizeof(payB));
        SaveEnvelopeSeal(&h, C + sizeof(h), (int)sizeof(payB), 1000u);   /* 同 A 的 stamp */
        memcpy(C, &h, sizeof(h));
        int lenC = (int)sizeof(h) + (int)sizeof(payB);
        CHECK(SteamZoneCloudDecide(A, lenA, C, lenC, &win) == STEAM_SYNC_CONFLICT,
              "same timestamp with different content must be reported as CONFLICT");
    }

    /* ⑤ 远端被篡改一个字节 → ERROR（校验和抓得住） */
    {
        unsigned char D[512];
        memcpy(D, B, (size_t)lenB);
        D[sizeof(h) + 5] ^= 0xFF;
        CHECK(SteamZoneCloudDecide(A, lenA, D, lenB, &win) == STEAM_SYNC_ERROR,
              "a corrupted remote payload must be rejected");
    }

    /* ⑥ 远端长度字段与真实长度不符 → ERROR */
    {
        unsigned char E[512];
        memcpy(E, B, (size_t)lenB);
        SaveEnvelope bad;
        memcpy(&bad, E, sizeof(bad));
        bad.payload = 7;                 /* 谎报 */
        memcpy(E, &bad, sizeof(bad));
        CHECK(SteamZoneCloudDecide(A, lenA, E, lenB, &win) == STEAM_SYNC_ERROR,
              "a remote header lying about its payload size must be rejected");
    }
}

static void test_save_backend_errors(void) {
    extern int SaveBackendWrite(const char*, const void*, size_t);
    unsigned char buf[32];
    memset(buf, 0x5A, sizeof(buf));

    CHECK(SaveBackendWrite(NULL, buf, sizeof(buf)) != 0,
          "SaveBackendWrite must reject a NULL path");
    CHECK(SaveBackendWrite("", buf, sizeof(buf)) != 0,
          "SaveBackendWrite must reject an empty path");

    /* 目录不存在 → CreateFile 失败 → 必须返回系统错误码，不能返回 0 */
    {
        const char* dir = getenv("CANITY_DATA_DIR");
        CHECK(dir != NULL, "CANITY_DATA_DIR must be set");
        if (dir) {
            char bad[MAX_PATH];
            snprintf(bad, sizeof(bad), "%s\\__no_such_dir__\\x.sav", dir);
            CHECK(SaveBackendWrite(bad, buf, sizeof(buf)) != 0,
                  "SaveBackendWrite must fail when the directory does not exist");
        }
    }
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

    SuspicionReset();
    ThrowItem(1, 0.10f);                    /* kind 1 = 投石/破门石 */

    /* 固定步进 2.5 秒：比任何一次抛掷的滞空时间都长 */
    for (int f = 0; f < 300; f++) UpdateShots(1.0f / 120.0f);

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

int main(void) {
    CHECK(getenv("CANITY_DATA_DIR")!=NULL,"tests require isolated save directory");
    srand(1234);InitAchievements();InitPixels();
    test_music();test_daily();test_wallet_save();test_stone_break_single_event();test_save_backend_errors();test_cloud_decide_length();test_save_unknown_chunk_roundtrip();test_sound_event_center();test_trees_shop();test_runs_and_layout();test_wrench();test_three_hunters();test_hunter_models();test_floor_resets_hunters();
    printf("CANITY runtime integration: PASS (%d checks)\n",checks);return 0;
}

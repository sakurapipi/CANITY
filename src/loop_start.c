/* Orchestration ownership split; stage ordering remains in the entry point. */
#include "common.h"
#include "run_economy.h"
#include "room_theme.h"
#include "lockbox.h"
#include "mimic.h"
#include "echo_marks.h"
#include "run_stats.h"
#include "threat.h"
#include "threat_telegraph.h"
#include "training_stations.h"
#include "map_validator.h"
#include "hatch.h"
#include "cell_meta.h"
#include "map_rooms.h"
#include "sound_event.h"
#include "state.h"
#include "economy.h"
#include "throwing.h"
#include "uptimers.h"
#include "warden.h"
#include "hints.h"
#include "loop_api.h"
#include "calibration.h"
#include "wrench.h"
#include "item_effects.h"
#include "player_tick.h"
#include "interaction.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "phase.h"
#include "collapse.h"
#include "core_rng.h"
#include "difficulty.h"
#include "detect_alert.h"
#include "control_effect.h"
#include "spider.h"
#include "plat.h"
#include "build_tags.h"
#include "neutral.h"
#include "suspicion.h"
#include "weapons.h"
#include "shop.h"
#include "bench.h"
#include "events.h"
#include "achievements.h"
#include "telemetry.h"
#include "daily.h"
#include "steam_zone.h"
#include "music.h"
#include "wallet.h"
#include "campaign.h"
#include "upgrade_run.h"
#include "relics.h"
#include "sprites.h"
#include "extras.h"
#include "gamepad.h"
#include "lang.h"
#include "entity.h"
#include "render.h"
#include "ui.h"
#include "audio.h"
#include "save.h"
#include "skills.h"
#include "content_db.h"
#include "bestiary.h"
#include "score.h"
#include "loop_private.h"

static int pendingStartMode = START_NORMAL;
static int pendingCampaignStage = 0;

void LoopQueueStart(int mode, int stage) {
    pendingStartMode = mode;
    pendingCampaignStage = stage;
}

static int replaceRunPending=0,replaceRunChoice=0,replaceRunApproved=0;
int LoopRunReplacementPending(void){return replaceRunPending;}
int LoopRunReplacementChoice(void){return replaceRunChoice;}
void LoopToggleRunReplacement(void){replaceRunChoice=!replaceRunChoice;uiDirty=1;}
void LoopResolveRunReplacement(int approve){
    if(!replaceRunPending)return;
    replaceRunPending=0;uiDirty=1;LoopClearInputEdges();resetInputState=1;
    if(approve){replaceRunApproved=1;StartGame();}
    else {pendingStartMode=START_NORMAL;pendingCampaignStage=0;}
}

static void ResetModeFlags(void) {
    RoomThemeSetMadness(0);
    isEndlessRun = 0;
    trainingMode = 0;
    guideMode = 0;
    dailyMode = 0;
    campaignMode = 0;
    campaignStage = 0;
    /* v28：教程状态是「训练模式」的一部分，模式清零时必须一起清。
       原来退出教程只把 trainingMode 置 0，tutorialStep/Done 会留到下一次
       进入对局；如果 trainingMode 又被别的路径带回来，就会在
       每日/战役地图上继续跑教程步骤。 */
    TutorialResetMode();
}

/* v28：**离开一局 / 读档前统一清理运行时模式状态**。
   必须在 SaveGame() 之后调用（先让存档记下 daily/campaign 模式），
   否则会把「退出时还在每日挑战里」这件事也清掉，Continue 就回不去了。 */
void ResetRunModeFlags(void) {
    LoopClearInputEdges();
    LoopCancelTransientInput();
    PlayerResetSprintGrace();
    ResetModeFlags();
    runInProgress = 0;
    exitChoiceOpen = 0;
    exitGoDeeper = 0;
    {  
      if (lockActive) CloseLockOverlay(); }
}

static void InitializeRunEconomy(void) {
    ThreatReset();BenchResetRun();RunUpgReset();counterKillUsed=0;
    if(trainingMode){
        carryGold=0;carryAtRunStart=0;throwSelected=0;runInProgress=1;runTime=0;
        RunEconomyReset();return;
    }
    /* v3.4 银行：按选择的档位从存款里取出本金 */
    {
        int p = bankCarryPct;
        if (p < 0) p = 0;
        if (p >= BANK_CARRY_STEPS) p = BANK_CARRY_STEPS - 1;
        double withdrawal=(double)WalletBank()*(p*BANK_STEP_PCT)/100.0+.5;
        int take=withdrawal>=INT_MAX?INT_MAX:(int)withdrawal;
        {   /* v6：取款走 Wallet（银行 → 携带） */
            take = WalletWithdraw(take);
        }
        carryGold = take;          /* 局内花的就是这笔 */
        carryAtRunStart = take;    /* v6：记录基线，用于算净赚 */
        /* v6：开局把今天的每日任务打出来 */
        {
            static char db[160];
            /* ==================================================================
             * 2026-11-06（用户实测）：**这行横幅只在每日挑战里才有意义**
             * ------------------------------------------------------------------
             * 用户原话：「每次进游戏上面都会有，就进个新手教程那里，
             *           每次都会有这行字。你看一下，如果这行字没有意义的话，
             *           就帮我把它删掉」。
             *
             * 根因：这个块写死在 StartGame 里，只判 !DailyDone()，
             * 没有判 dailyMode —— 于是普通局和训练营都会打出
             * "DAILY: 连续 3 层不掉理智 | 电池上限 -25% | ..."，
             * 而玩家玩的根本不是每日挑战。
             * ================================================================ */

            if (dailyMode && !DailyDone()) {
                /* v6 第 ㊱ 轮：**把修正和限制也说出来**
                   玩家之前只看到"收集 5000 金"，
                   完全不知道今天怪物更快、或者不能用扳手。 */
                const char* mod = DailyModTextEn();
                const char* res = DailyRestrictTextEn();
                if (mod && mod[0] && res && res[0])
                    snprintf(db, sizeof(db), "DAILY: %s | %s | %s",
                             DailyGoalTextEn(), mod, res);
                else if (mod && mod[0])
                    snprintf(db, sizeof(db), "DAILY: %s | %s", DailyGoalTextEn(), mod);
                else if (res && res[0])
                    snprintf(db, sizeof(db), "DAILY: %s | %s", DailyGoalTextEn(), res);
                else
                    snprintf(db, sizeof(db), "DAILY: %s", DailyGoalTextEn());
                pickupMsg = db;
                pickupMsgTimer = 4.5f;
            }
        }
        {  ThreatReset(); }   /* v6：每局威胁归零 */
        /* ==================================================================
         * v7 第 ㊿ 轮：**改造树每局清空**
         * ==================================================================
         * 这是第 ⑨ 轮改造树"跨局保留"留下的坑：
         *   · 节点买一次永久有（benchOwned 进存档）
         *   · 但它的代价 THREAT 每局被 ThreatReset 清零
         *   → 「付一次钱，永久白拿」，10 局买满 16 个节点之后
         *     每局开局满配、THREAT 恒 0，**局内构筑这个决策彻底消失**
         *   → 而这正是撑起 45 局的核心机制
         *
         * 而且节点是用**局内金币**（carryGold）买的，
         * 本来就该是"这一局的构筑"，不是永久升级。
         *
         * 现在每局清空：玩家每局用捡到的钱重新做 2~4 个选择，
         * 每次买都涨 THREAT —— 决策和代价同时回来。
         * ================================================================ */
        {  BenchResetRun(); }
        RunUpgReset();
    }
    throwSelected = 0;
    runInProgress = 1;
    runTime = 0.0f;RunEconomyReset();
}

static void InitializeRunScene(void) {
    /* v3.3：这两个不重置会导致——
         floorStartTime：第 1 层 floorTime = runTime - 0 之外的残留值，
                         可能出现负数 → 「90 秒内清完」白送
         lowSanitySeconds：上一局攒的秒数直接算进本局 → LAST BREATH 白送 */
    {
        ShopResetCooldowns();   /* v3.4：新局清空商店冷却 */
    }
    /* v3.4 CARTO 满级：开局直接把安全屋和配电箱所在格子标记为已探索，
       地图上能看见。原来只是「通关更快」，玩家感知不到。 */
    if (SkillCartoUnlock()) {
        int sx = (int)safeRoom.x, sy = (int)safeRoom.y;
        if (sx >= 0 && sx < MAP_WIDTH && sy >= 0 && sy < MAP_HEIGHT) explored[sy][sx] = 1;
        int bx = (int)breaker.x, by = (int)breaker.y;
        if (bx >= 0 && bx < MAP_WIDTH && by >= 0 && by < MAP_HEIGHT) explored[by][bx] = 1;
    }
    floorStartTime = 0.0f;
    lowSanitySeconds = 0.0f;
    fusesCollectedRun = 0;
    searchedCount = 0;
    sanity = 100.0f;
    battery = (float)BatteryMax();
    flashlightOn = DailyForbidLight() || HasKeystone(SK_SALVAGE) ? 0 : 1;
    stamina = StaminaMax();
    sprinting = 0;
    staminaIdle = 0.0f;
    healFlash = 0.0f;
    isMapOpen = 0;
    mapFullScreen = 0;
    isCharging = 0;
    mouseCharge = 0.0f;
    resetInputState = 1;
    /* v1.8：重置本局状态 */
    /* carryGold already contains the bank withdrawal. */
    bankStreak = 0;
    eventBonus = 0;
    levelEvent = -1;
    modsSeenMask = 0;
    modsSeenCount = 0;
    runSearched = 0;
    knifeCd = 0.0f;
    WeaponResetRun();               /* v2.0：武器冷却/蓄力 */
    RelicResetRun();                /* v2.0：遗物只在本局有效 */
    SafeLevelReset();               /* v2.2：每局重置安全屋的休息/充电标记 */
    shortcutUsed = 0;
    modSanityMul = modSpeedMul = modBatteryMul = modShardMul = 1.0f;   /* v2.0 修复 */
    modNoiseMul = 1.0f;
    ExtrasReset();                  /* v2.0 */
    pickMode = 0;
    weaponAtkCharge = 0.0f;
    slashActive = 0;
    slashTravel = 0.0f;
    slashFade = 0.0f;
    slashHitStop = 0.0f;
    decoySelfTimer = 0.0f;
    LoopResetDecoyCooldown();   /* 新局冷却清零 */
    leechHintTimer = 0.0f;
    ResetLeeches();
    RunSkillsReset();

    /* 重置成就追踪变量 */
    knockCountRun=throwHitCountRun=echoCellsTotalRun=0;
    /* 2026-11-06（用户实测）：**训练营里没有相位信标** ——
       那是个测试场，所有机制都该能试。这里直接发一个。 */
    {  mimicsEscapedThisRun = 0; }
    floorsNoSanityLoss=dailyEscapeCount=wardenCleanFloors=fullHideDone=0;
    doubleHuntSeconds=0.0f; LoopResetHuntAchievements(); RewardResetPending();
    totalGoldEarned = 0;
    bottlesKilled = 0;
    faceDodged = 0;
    crankTimeTotal = 0;
    fusesCollectedRun = 0;
    mapsFullyExplored = 0;
    tookDamage = 0;
    { contractKind=(bankRiskPlan>=0 && bankRiskPlan<4)?bankRiskPlan:1; shopContractUsed=1; }
    flashlightOffLevel = 0;
    sprintedThisLevel = 0;
    hideCount = 0;
    totalThrown = 0;
    sprintedAnyFloor = 0;
    runSpotted = 0; runLightUse = 0;   /* v3.4 新局清零 */
    InitAchievements();  /* 初始化成就定义（仅首次） */
    ClearFx();
    for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
    GenerateLevel();
    /* 生成关卡后清除过关白屏时间，避免新游戏开局 0.15s 逻辑不跑 */
    flashTime = 0.0f;
    UpdateExplored();
    if (trainingMode) TutorialBegin();
    ForceCursor(0);
    firstMouseMove = 1;
    uiDirty = 1;
}

void StartGame(void) {
    int replacing=SaveHasPendingRun() || (runInProgress && !trainingMode);
    if(pendingStartMode!=START_TRAINING && pendingStartMode!=START_GUIDE && replacing && !replaceRunApproved){
        replaceRunPending=1;replaceRunChoice=0;ForceCursor(1);uiDirty=1;return;
    }
    replaceRunApproved=0;
    CollectorResetRun();
    /* v6：**出口弹窗状态必须重置** —— 否则暂停→主菜单→新局会带着
       上一次的弹窗标志进来，开局即被冻结。 */
    exitChoiceOpen = 0;
    exitGoDeeper = 0;
    /* Per-run builds reset in InitializeRunEconomy, including practice. */
    playDeadUsed = 0;   /* v3.0 假死每局重置 */
    /* v3.0：这几个也必须在开局清零，否则上一局用过之后新局直接失效 */
    CalibResetRun();
    pawnCd=0;retaliateT=0;ghostStillT=0;
    PlayerResetSprintGrace();
    LoopClearInputEdges();
    LoopCancelTransientInput();

    int startMode = pendingStartMode;
    int startStage = pendingCampaignStage;
    pendingStartMode = START_NORMAL;
    pendingCampaignStage = 0;

    ResetModeFlags();   /* v2.2：先清模式标志 */
    {  SkillInitOnce(); }   /* v6：技能树中心永远点亮 */
    {  CatchCountResetRun(); }   /* v6：本局被捕计数清零 */
    if (startMode == START_ENDLESS) {
        isEndlessRun = 1;
    } else if (startMode == START_DAILY) {
        dailyMode = 1;
    } else if (startMode == START_CAMPAIGN) {
        campaignMode = 1;
        campaignStage = startStage;
    } else if (startMode == START_TRAINING) {
        trainingMode = 1;
        guideMode = 0;
    } else if (startMode == START_GUIDE) {
        trainingMode = 1;          /* 同一张训练地图 = 传送到引导场景 */
        guideMode = 1;
    }

    currentState = STATE_PLAYING;

    /* ==================================================================
     * 2026-11-06（用户实测）：**训练营里放不了信标**
     * ------------------------------------------------------------------
     * 原来这两行放在 StartGame 开头 —— 那一刻 trainingMode 还是 0
     * （ResetModeFlags 刚把它清掉，真正的赋值在下面 startMode 分支里）。
     * 于是 PhaseGive 永远不执行，训练营里根本没有装置，按 C 当然没反应。
     * ================================================================ */
    {  PhaseReset(); }
    /* ==================================================================
     * 2026-11-06（审计补）：**抽卡 PRNG 从来没有被播种**
     * ------------------------------------------------------------------
     * RunRngSeed 写了却没人调用，s_runRng 一直是 0 ——
     * RunRngNext 里的兜底常量会让**每一局的抽卡序列完全一样**：
     * 同一个种子之外，玩家第二局拿到的三张卡会和第一局相同。
     *
     * 在这里播种（rand() 之前，避免影响地图生成序列）。
     * ================================================================== */
    { 

      if (RunRngState() == 0) RunRngSeed((unsigned int)time(NULL) ^ 0x5bf03635u); }
    {  ControlReset(); }   /* 2026-11-06：控制层按局清零 */
    {  SpiderReset(); }      /* 2026-11-06：蜘蛛按局清零 */
    {  FlareHealReset(); } /* 2026-11-06：每局/每层重置治疗预算 */
    {  NeutralReset(); }       /* 2026-11-06：中立怪按局清零 */
    {  PhaseResetPlacement(); } /* 2026-11-06：信标换层失效 */
    /* ==================================================================
     * 2026-11-07（v2.4.2 / B3）：**训练营物资不许带进正常对局**
     * ------------------------------------------------------------------
     * 原来是 if (trainingMode && !guideMode) phaseBeacons = 5; —— 只有训练场赋值，
     * 普通新局**从不重置** phaseBeacons。玩家在同一个进程里先玩训练场、
     * 再开正式局，就会带着训练场的 5 枚信标进正式局。
     * 现在无条件先归零，再按模式发放。
     * ================================================================ */
    {

        phaseBeacons = 0;
        if (trainingMode && !guideMode) phaseBeacons = 5;
    }
    /* 2026-11-05：新局同样要重置鼠标基准 ——
       从菜单/商店直接开局时，lastMousePos 还是上一次的位置，
       第一帧会把它当成一次巨大的视角输入。 */
    firstMouseMove = 1;
    DailyTick(); DailyBeginRun();
    currentLevel = campaignMode ? CampaignStartFloor(campaignStage) : 1;
    gameWon = 0;
    /* v3.3：新六槽位的开局初始量
       光球 1（照明刚需）/ 投石 2（基础诱饵）/ 其余从箱子里找 */
    flares = 1;
    bottles = 2;
    smokeBombs = 0;
    gluePots = 0;
    echoBolts = 0;
    decoys = 0;
    /* 已删的三种清零，免得训练场（99 个）或上一局带进新一局 */
    bricks = 0;
    acids = 0;
    firePots = 0; mirrors = 0;
    InitializeRunEconomy();
    InitializeRunScene();
}

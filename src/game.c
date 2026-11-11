/*
 * Canity — update/transition orchestration and platform event routing.
 * Domain implementations and their private state live in loop_*.c.
 * Update keeps ordering/early-return decisions; stage APIs pass per-tick input
 * explicitly. Shared save-facing names retain their existing representation.
 */
#include "common.h"
#include "ui_menu.h"
#include "loop_private.h"
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
#include "core_tick.h"   /* 第 4 轮：每帧 tick 顺序守卫 */
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
#include "warden.h"
#include "campaign.h"
#include "lockbox.h"   /* v2.7 改造树 */
#include "mimic.h"     /* 2026-11-05 宝箱怪 */
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

/* ------------------------------------------------------------------
 * 2026-11-06：舱门效果与崩塌互相调用，需要前置声明。
 * 放在所有 include 之后、任何函数之前，避免"定义在使用之后"。
 * ------------------------------------------------------------------ */
void GenerateLevel(void);
void CollapseBegin(void);

/* Shared orchestration declarations are supplied by the narrow loop contracts. */
void SpawnGlue(float x, float y);
/* InitAchievements is public now (see game.h) */
int CatchCountThisRun(void);
float ExtractDepthMul(int level);
/* v3.4 银行结算（实现见下方 BankSettle） */
void BankSettle(int survived);

int winW = 800, winH = 600;          /* 窗口客户区尺寸（逻辑/显示分辨率） */
HWND g_hwnd = NULL;
GameState currentState = STATE_MAIN_MENU;
GameState prevState = STATE_MAIN_MENU;

int gameWon = 0;             /* 0=失败结局 1=通关结局（第 10 层逃出生天） */

int menuSelection = 0;
int uiDirty = 1;
/* FPS 统计 */
int currentFps = 0;
double lastRenderMs = 0.0;   /* 最近一帧（含呈现与 HUD）的耗时 */
double frameMsMax = 0.0;     /* 最近一秒内的最差帧，用来观察卡顿 */

/* 世界 */
float flashTime = 0.0f;      /* 切关白屏剩余秒数 */
float healFlash = 0.0f;      /* 回血白闪剩余秒数 */
int carryGold = 0;
int carryAtRunStart = 0;   /* v6：本局开局时的携带量（算净赚用） */

int hubSelection = 0;
/* 2026-11-08：weaponPickSel 已删 —— 选武器流程不存在了 */
/* 2026-11-06：两套相位投掷物（槽 6 / 7） */
int g_dmgIgnoreArmor = 0;
float smokeDim = 1.0f;      /* 烟幕内的光线衰减 */
int monsterMuted = 0;       /* v1.20b：只静怪物的叫声（训练场） */
/* ---- v1.17 训练场 ---- */
int trainingMode = 0;
int guideMode = 0;   /* 2026-11-08: 引导模式（B 键） */
int trainingTotal = 0;          /* 累计伤害 */
int trainingLast = 0;           /* 最近一次 */
int trainingCount = 0;          /* 命中次数 */
int trainingHist[6] = {0};      /* 最近 6 次 */
float trainingDpsTimer = 0.0f;
int trainingDps = 0;
/* Tutorial progression is owned by loop_tutorial.c. */
float echoScanT = 0.0f;        /* v3.3 回声弹剩余秒数 */
float echoScanX = 0.0f, echoScanY = 0.0f;
Thrown thrown[MAX_THROWN];
Shot shot[MAX_SHOTS];

/* v3.0：藏身点 */
float runTime = 0.0f;     /* 本局存活时间 */
float stepTimer = 0.0f;
/* v2.6：连续走路计时 + 走路噪声的节拍器（见 WALK_NOISE_*） */
float walkTimer = 0.0f;
float walkStepTimer = 0.0f;
float crankTimer = 0.0f;
float throwCd = 0.0f;   /* 手摇进度 0..CRANK_TIME */
float crankClick = 0.0f;
int cranking = 0;
int isCharging = 0;          /* 是否正在蓄力投掷 */
float mouseCharge = 0.0f;    /* 蓄力进度 0..1 */
int resetInputState = 0;     /* 请求重置输入边缘状态（跨局/菜单切换用） */
double g_timeSec = 0.0;    /* 主循环时钟，渲染用它做动画 */
float heartTimer = 0.0f;
int searchedCount = 0;    /* 搜索过的容器数 */
int hasSave = 0;          /* 是否有可继续的存档 */

int totalGoldEarned = 0;  /* 累计获得金币（用于成就） */
int bottlesKilled = 0;      /* 用瓶子击杀怪物数 */
int faceDodged = 0;         /* 躲开跳脸虫次数 */
int crankTimeTotal = 0;     /* 累计发电秒数 */
int fusesCollectedRun = 0;  /* 本局收集保险丝数 */
int mapsFullyExplored = 0;  /* 全探索地图数 */
int tookDamage = 0;         /* 本局是否受过伤 */
int flashlightOffLevel = 0; /* 关灯过关标记 */
int sprintedThisLevel = 0;  /* 本局是否冲刺过 */
/* v3.3 **新增逃生向计数器** ——
   击杀类成就全部失效后（NO_COMBAT 把结算分支编译掉了），
   这几条成就改成用真正能累加的逃生行为：
     hideCount    钻进藏身点的次数（躲=核心动作）
     totalThrown  投掷物品的总次数（引怪=核心动作）
   sprintedAnyFloor 跨层保留的冲刺标记（sprintedThisLevel 每层清零，
   导致 SILENT SHADOW 到第 5 层必然满足）。 */
int hideCount = 0;
int totalThrown = 0;
/* v7 第 ㊿ 轮：成就用的三个真实计数器 */
int knockCountRun = 0;       /* 整局扳手击退次数 */
int throwHitCountRun = 0;    /* 整局投掷命中次数 */
int echoCellsTotalRun = 0;   /* 整局回声弹揭示格数 */
/* v7 第 ㊿ 轮：每日任务用 */
int bountyRecovered = 0;   /* v7：从拾荒者手里抢回赃款的次数 */
int metaResetDone = 0;     /* v7：局外技能树重置过一次 */
int dailyEscapeCount = 0;    /* 当天累计撤离次数（每日目标 1） */
int lastRunGold=0, lastRunKept=0, lastRunCaught=0;
int wardenCleanFloors=0, fullHideDone=0;
float doubleHuntSeconds=0.0f;
int floorsNoSanityLoss = 0;  /* 连续不掉理智的层数（每日目标 7） */
int sprintedAnyFloor = 0;
const char* pickupMsg = NULL;    /* 拾取提示文本 */
float pickupMsgTimer = 0.0f;     /* 提示显示剩余时间 */
int bestLevel = 0;
int bestEndlessLevel = 0;   /* 无尽模式最高层数 */
int bestEndlessScore = 0;   /* 无尽模式最高分 */
int isEndlessRun = 0;       /* 当前是否为无尽模式 */
/* v28：跨局持久记录 —— 只要成功撤离/通关过一次就置 1。
   它取代了「用 gameWon 当解锁标志」的写法：gameWon 只描述**这一局**的结果，
   退出游戏就归零，于是重启后 Endless 又锁上了。 */
int gameCleared = 0;

/* 无尽模式解锁判定：只留这一份，菜单绘制和点击判定都调它。
   （以前 ui.c 和 game.c 各抄一份 `(bestLevel >= 10) || gameWon`。） */

int runInProgress = 0;    /* 当前是否有一局未结束 */

/* ---- v1.8 新状态 ---- */
int bestMainScore = 0;        /* 主线最高分（固定分，记录展示用） */
/* ===== v3.4 银行博弈 ===== */
int   bankGold      = 0;    /* 局外存款（不参与局内消费） */
int   bankHistMax   = 1;    /* 历史最高存款，决定倍数分母 */
int   bankCarryPct  = 10;   /* v6：档位 0..20 → 0% ~ 100%（每档 5%）。
                               默认 50%，不是 100% —— 100% 时倍数恒为 ×2、
                               BANK_LOW_CARRY 惩罚分支永远走不到，
                               「带多少」这个风险决策形同不存在。 */
/* v6 **出口二选一**：0 = 没弹窗，1 = 正在选（撤离 / 深入） */
int   exitChoiceOpen = 0;
int   exitGoDeeper   = 0;   /* v6：玩家在出口选了「深入」 */
float lastExtractDepthMul = 1.0f;   /* v6：上一次撤离用的深度倍数（结算页用） */
float exitChoiceT    = 0.0f;   /* 弹窗出现多久（用于淡入） */
/* v3.4 表现加成用的三个「本局」标志 */
int   runSpotted  = 0;   /* 本局有没有被发现过 */
int   runLightUse = 0;   /* 本局有没有开过手电 */
float lastPerfMul = 1.0f;  /* v3.4：上一次结算的表现加成倍数（结算页用） */   /* v3.4 CATCH BREAK：本局已用次数（上限 2） */
int bankStreak = 0;           /* 连续上缴层数（连缴加成用） */
int modsSeenMask = 0;         /* 本局见过的词条位掩码 */
int modsSeenCount = 0;        /* 本局见过的词条种类数 */
int runSearched = 0;          /* 本局翻箱总数（结算用） */
float knifeCd = 0.0f;         /* 绣刀剩余冷却 */
/* v2.9 剑气：位置 / 方向 / 已飞行距离 / 命中后的余辉 */
int   slashActive = 0;
float slashX = 0, slashY = 0;
float slashDX = 1, slashDY = 0;
float slashTravel = 0, slashFade = 0;
float slashHitStop = 0.0f;     /* v2.9 剑气命中顿帧（秒） */

/* ---- v2.1 屏幕特效 ---- */
float g_shakeOffX = 0.0f, g_shakeOffY = 0.0f;   /* 当前帧的相机抖动（世界单位） */
float hitFlash = 0.0f;      /* 受伤红闪 0..1 */
float muzzleFlash = 0.0f;   /* 开火闪光 0..1 */

float bobPhase = 0.0f;   /* v2.1：手持模型走路摆动相位 */
int   slashDmg = 999;
float slashMaxDist = 14.0f;   /* v2.0：法术吃武器射程/配件 */          /* 光束伤害：剑气固定秒杀，符印用武器表 */
float weaponAtkCharge = 0.0f;  /* 武器蓄力 0..1 */
/* ===== v7 扳手击退 =====
   次数制（0/1 级）与冷却制（2/3 级）共存：
     wrenchCharges 只在次数制下有意义，每层开始重置
     wrenchCd      只在冷却制下有意义 */
float gameTime = 0.0f;   /* v2.4.3: 全局秒表（出口脉冲等用） */
int   shopContractUsed = 0;   /* v7 B3: 本局是否已经签过协议 */
float playerMoveSpeedNow = 0.0f;

int   wrenchCharges  = 2;
float wrenchCd       = 0.0f;
int   wrenchLastUsed = 0;
int   playDeadUsed = 0;         /* v3.0 假死：本局用过了吗 */
/* v3.3：tutorialOff 已删除 ——
   旧的四行开局引导整块移除了（改用 Tutorial 模式的分步引导），
   这个变量既不写也不读，留着只是噪音。 */
float hideBlockTimer = 0.0f;    /* v3.0：刚解锁/刚翻完箱时短暂禁止进藏身点 */
int   ultUsed = 0;              /* v3.0：本局大招用过了吗 */
float ultTimer = 0.0f;          /* v3.0：大招持续秒数 */
int   ultKind = -1;             /* v3.0：正在生效的大招 id */
float decoySelfTimer = 0.0f;  /* 投石后的自身噪声加成计时 */
/* 2026-11-05：**G 键投石的冷却计时器**。
   原来没有这个东西 —— 技能描述写着 8 秒 cd，代码里却是无限制。
   文件级 static：只有下面那个按键分支会写它。 */
int hubPage = 1;   /* v6：Hub **只有一页**（技能树）。变量保留兼容旧判断，但恒为 1。 */
int levelEvent = -1;         /* 本层随机事件，-1 = 无 */
int eventBonus = 0;          /* 本局事件累计加分 */

/* 通过本层时给的分 */

int playerInSafe = 0;
int autoPaused = 0;
int lastHitDamage = 0;
float lastHitTimer = 0.0f;
int lastHitBackstab = 0;
int pixelBudget = PIXEL_BUDGET;   /* v1.9：运行时可变 */
int safehouseHoverRow = -1;       /* v3.0：鼠标悬停在安全屋第几行（-1=没有） */
int safehouseHoverTab = -1;       /* v3.0：悬停的页签 */
int forceFrame = 0;          /* v1.8：窗口变化后立刻出一帧，减少还原/缩放延迟 */          /* v1.8：因失焦自动暂停（回前台自动恢复） */         /* v1.8：玩家是否在安全屋里（怪物丢目标、不抓人） */
ScoreBreakdown lastScore;     /* 上一次结算的分项 */
/* 2026-11-06（v2.4.1 第 3 轮）：第 6 / 12 层**技能与遗物同时触发**，
   两次是独立选择。这两个计数只用于界面提示"第几次 / 共几次"，
   不参与任何逻辑判断 —— 用当前持有状态去推是不可能的。 */
void RewardScheduleForLevel(int level);   /* 2026-11-06 第 3 轮：前置声明 */
int bossLevel = 0;     /* v2.1：本层是否 Boss 层 */
/* 2026-11-06：bossMaxHp 删除（BOSS 已不存在，没有任何读者） */
int bossDefeated = 0;
/* 2026-11-05：**totalKills / killsWrench / killsBottle / floorKills 已删除** ——
   NO_COMBAT 之后本作没有任何击杀手段（ApplyDamage 只做噪音 + 硬直），
   这几个计数器一次都不会 +1。留着它们只会让人以为战斗系统还在。
   注：floorKills 原来还被 UNSEEN FLOOR 成就引用过，早在 v3.3 已换成 floorSpotted。 */
int shortcutUsed = 0;
int helpPage = 0;   /* v2.2：帮助页分页 */
float crankSecondsTotal = 0.0f;
int floorsNoLight = 0, floorsFlawless = 0, floorsPacifist = 0, floorsFast = 0, floorsQuiet = 0;
float lowSanitySeconds = 0.0f;
int floorTookDamage = 0;
/* v3.3 **新增 floorSpotted** ——
   UNSEEN FLOOR（原文案「Clear a floor without ever being spotted」）
   原来判的是 floorKills == 0，但 floorKills++ 在 NO_COMBAT 的
   #else 编译死分支里（怪物打不死，永远不会 +1），所以该成就首层必解。
   换成真正能反映「被发现」的计数器：怪物切到追捕（state=1）时 +1。 */
int floorSpotted = 0;
float floorStartTime = 0.0f;
int dailyMode = 0;
int dailySeed = 0;
int dailyClearedToday = 0;   /* v2.2：今天是否已通 */
int campaignMode = 0;
int campaignStage = 0;
/* StartGame() 统一清理模式标志；菜单入口先把要启动的模式排队，
   这样清理不会把 Endless / Daily / Campaign / Training 自己也清掉。 */


/* v2.0 战役：5 个手工场景，固定种子 + 各自的规则 */
/* Campaign labels use the campaign definition table. */
const char* campaignName(int stage) {

    if (stage < 0 || stage >= CAMPAIGN_STAGE_COUNT) return "?";
    return (g_lang == LANG_ZH) ? kCampaign[stage].nameZh : kCampaign[stage].nameEn;
}
float leechHintTimer = 0.0f;
int hoverKind = 0;        /* 0=无 1=容器 2=配电箱 3=家具（货架/木箱堆/控制台） */
int hoverIndex = -1;
float holdTimer = 0.0f;
int holdingNow = 0;
/* Gamma：0.7 ~ 1.6，1.0 为线性 */

/* 「本帧按下的键」—— QTE 需要边沿触发，不能用 keys[]（那是持续状态）。
   窗口过程收到 WM_KEYDOWN 时把键记在这里，逻辑层每帧消费一次后清空。
   这样「按早了」（QTE 还没开始时按）不会被误判成「按对了」。 */

/* 清除本帧的按键边沿。
   注意：**不能**在 Update() 开头清 —— 主循环一帧可能跑多次 Update
   （120Hz 逻辑 vs 60Hz 渲染，一帧通常跑 2 次），
   那样第一个 Update 就把按键清掉了，QTE 会吞掉一半的输入。
   所以边沿由主循环在一批逻辑更新**跑完之后**统一清。 */

/* v6：g_achievements 定义在 achievements.c */

/* 每个菜单状态的滚动偏移，持久化避免 GetMenuGeo 重新计算 */
int menuScrollOffsets[16] = {0};   /* 下标 = GameState，留足余量 */  /* 下标 = GameState，包含 STATE_ACHIEVEMENTS=7 */

/* 统一按键查询：键盘走 keys[]/keyPressed[]，鼠标走 GetAsyncKeyState */


/* v8: 给外部模块用的公开包装（原来 IsActionDown 是 static） */

/* v1.8：每帧轮询键盘。
   原来只靠 WM_KEYDOWN / WM_KILLFOCUS 维护 keys[]，
   最大化/切窗口时一旦没拿到 WM_SETFOCUS，keys 就永远清零 —— 表现就是"按键全失效"。
   这里改成：只要游戏在前台，每帧从 GetAsyncKeyState 直接读，边沿自己算。 */
/* v1.9：动态分辨率。
   每 2 秒看一次实测渲染耗时：
     平均 < 7ms  → 有富余，预算 ×1.12（画面更清晰）
     平均 > 12ms → 吃紧，预算 ×0.88（保住 60fps）
   只在变化超过 3% 时重建缓冲，避免抖动。 */
/* v1.14：手里不能是空的。
   如果当前槽位数量为 0，自动跳到另一个有货的槽位 ——
   玩家滚轮/按键切到空槽时不需要自己再切回来。 */
int itemHasStock(void) {
    /* v3.3 槽位：0 光球 / 1 投石 / 2 烟雾 / 3 胶水 / 4 回声 / 5 诱饵 */
    int c[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys, phaseBeacons };
    if (throwSelected < 0 || throwSelected >= ITEM_SLOT_COUNT) return 0;
    return c[throwSelected] > 0;
}

/* v2.6：键盘只有 3 个选槽键，但槽位有 5 个（砖/酸/烟都有）。
   原来 1/2/3 是写死的"第 0/1/2 号槽"，所以捡到酸之后键盘根本选不中它，
   只能靠滚轮 —— 玩家看到 HUD 上明明写着 ACID x2 却用不了。
   现在改成"第 N 个**有货的**槽"：HUD 只列有货的槽，第 3 行按 3 就对了。 */


void EnsureThrowSelectedValid(void) {
    /* v1.18：这里原来写死 2 个槽位 —— 滚到砖头/酸瓶时
       counts[throwSelected] 越界读到垃圾值，随即被拉回第 0/1 槽，
       表现就是"选不中砖头和酸瓶"。 */
    int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys, phaseBeacons };
    if (throwSelected < 0 || throwSelected >= ITEM_SLOT_COUNT) throwSelected = 0;
    if (counts[throwSelected] > 0) return;
    /* 2026-11-05：**就近取，不要永远拽回 0 号槽**。
       原来是从 0 开始找第一个有货的 —— 配合滚轮的空槽问题，
       造成"选中项永远回到最左边"的观感。
       现在从当前槽向两边交替找，落在最近的有货槽上。 */
    for (int d = 1; d < ITEM_SLOT_COUNT; d++) {
        int fwd = (throwSelected + d) % ITEM_SLOT_COUNT;
        if (counts[fwd] > 0) { throwSelected = fwd; uiDirty = 1; return; }
        int bwd = (throwSelected - d + ITEM_SLOT_COUNT * 2) % ITEM_SLOT_COUNT;
        if (counts[bwd] > 0) { throwSelected = bwd; uiDirty = 1; return; }
    }
}

/* v1.16 烟幕：落地生成，最多 MAX_SMOKE 朵 */

/* ================= v3.3 玩家诱饵「玩偶」 =================
   设计来源（调研结论）：硬核逃生游戏里的「假人」类道具只做两件事之一 ——
   要么当诱饵把怪引走，要么替你挨一下。两者同时做会太强，
   所以这里加了**时间和次数**两道闸门：
     · 每 3 秒才叫一次（不是持续噪音，怪有反应窗口）
     · 只挡 2 次抓（不是无敌）
     · 放下后要按 C 才激活（给你决定时机的余地）
   ======================================================= */

/* 怪物快要抓到玩家时调这个：有激活的玩偶在附近就替玩家挨一下 */

/* v3.3 胶水：落地生成一滩粘胶，半径 GLUE_RADIUS，持续 GLUE_LIFE。
   占满 MAX_GLUE 时**顶掉剩余时间最短的那个**（不是拒绝）——
   否则玩家在同一个门口连扔两瓶，第二瓶会静默消失。 */

/* 任意一点是否在某朵烟里（守门人/猎手/清道夫共用）。
   2026-11-03：原来只有 PlayerInSmoke()，怪物那边各自写死距离判断，
   结果烟雾只对第 1 只听声鬼 + 追光鬼生效，守门人和第 2/3 只完全免疫。 */

/* 线段是否穿过任意一朵烟（用于「视线被烟挡住」）。
   沿线段每 0.35 格采样一次，够密也不会太贵。 */

/* ==================================================================
 * **把所有还活着的猎手收集到一个列表里**
 * ------------------------------------------------------------------
 * 2026-11-03：听声鬼从 1 只扩到 3 只，但代码里有 5 处写死了
 *     Monster* targets[2] = { &monster, &lightBeast };
 * 每处都漏掉第 2、3 只 —— 扳手、烟雾、投掷物、WHISTLE、TIME STOP
 * 全都对它们无效。这不是一次失误，是「手写枚举」这种写法必然漏。
 *
 * 以后任何「对所有猎手做某事」的地方，一律调这两个函数，
 * 不要再自己写数组。新增怪只需要改 MonsterCollect 一处。
 * ================================================================ */
/* ==================================================================
 * 2026-11-05：宝箱怪离场后，在原地留下一个**空壳**（可以翻）
 * ------------------------------------------------------------------
 * 它生成时占着一个 container 槽并把它 active=0 掉，
 * 所以这里直接把那个槽重新激活就行 —— 不需要找新位置，
 * 也不会突破 MAX_CONTAINERS。
 * ================================================================ */

/* ==================== 实体 · 怪物 ==================== */
void MimicSpawnShell(int slot, float x, float y) {
    if (slot < 0 || slot >= MAX_CONTAINERS) return;
    containers[slot].x        = x;
    containers[slot].y        = y;
    containers[slot].active   = 1;
    containers[slot].searched = 0;
    containers[slot].locked   = 0;     /* 空壳永不上锁 */
    containers[slot].lockType = 0;
    containers[slot].lockCode = 0;
    containers[slot].content  = 0;
    containers[slot].mimicShell = 1;
    containers[slot].mimicHost  = 0;   /* 空壳不再是宝箱怪 */
    uiDirty = 1;
}

/* ==================== 实体 · 怪物 ==================== */
int MonsterCollect(Monster** out, int maxOut, int includeLightBeast) {
    int n = 0;
    for (int i = 0; i < MON_HUNTER_MAX && n < maxOut; i++)
        if (monHunters[i].active) out[n++] = &monHunters[i];
    if (includeLightBeast && lightBeast.active && n < maxOut)
        out[n++] = &lightBeast;
    return n;
}

/* 对所有猎手执行一次「瘫痪」——TIME STOP / STATIC FIELD 共用 */

/* ==================== 实体 · 怪物 ==================== */
void MonsterStunAll(float seconds) {
    Monster* list[MON_HUNTER_MAX + 1];
    int n = MonsterCollect(list, MON_HUNTER_MAX + 1, 1);
    for (int i = 0; i < n; i++) {
        list[i]->stun = seconds;
        list[i]->state = 0;
        list[i]->pathLen = 0;
        list[i]->pathIdx = 0;
    }
}

/* 玩家是否在某朵烟里 */

/* ==================== 玩家 · 移动/手电 ==================== */

void UpdateDynamicRes(double renderMs) {
    static double acc = 0.0;
    static int samples = 0;
    static double lastAdjust = -10.0;

    /* v2.9：**动态分辨率已禁用**。
       它每 2 秒按 ±12% 调 pixelBudget 并调 InitPixels()，
       而 renderW/renderH 一变，winW/winH 跟着变（SyncWindowSize），
       整个 UI 的尺度和间距全部重排 —— 玩家看到的就是「UI 在逐渐放大」。
       渲染尺寸必须在一局里保持稳定，宁可掉几帧也别让它漂。
       需要恢复时把 dynamicRes 设成 1 即可。 */
    if (1) return;              /* 强制关闭 */
    if (!dynamicRes) return;
    if (renderMs <= 0.0) return;
    acc += renderMs;
    samples++;
    if (samples < 8) return;

    double avg = acc / (double)samples;
    acc = 0.0;
    samples = 0;
    if (g_timeSec - lastAdjust < 2.0) return;

    long want = pixelBudget;
    if (avg < 7.0 && pixelBudget < PIXEL_BUDGET_MAX) {
        want = (long)(pixelBudget * 1.12);
        if (want > PIXEL_BUDGET_MAX) want = PIXEL_BUDGET_MAX;
    } else if (avg > 12.0 && pixelBudget > PIXEL_BUDGET_MIN) {
        want = (long)(pixelBudget * 0.88);
        if (want < PIXEL_BUDGET_MIN) want = PIXEL_BUDGET_MIN;
    }

    if (want != pixelBudget && (want > pixelBudget * 103 / 100 || want < pixelBudget * 97 / 100)) {
        pixelBudget = (int)want;
        InitPixels();
        ApplyFov();
        uiDirty = 1;
        lastAdjust = g_timeSec;
    }
}

/* ==================== 输入 ==================== */

POINT lastMousePos;
int firstMouseMove = 1;
int isMapOpen = 0;
POINT g_mousePos = {0, 0};
/* 小地图有两种形态：
     0 = 角落小窗（默认）—— 一边走一边看，不挡视野
     1 = 全屏 —— 停下来仔细规划路线
   按 M 切换形态，按 M 关闭。再按 M 会先切形态，再按一次才关闭。 */
int mapFullScreen = 0;
/* v2.5: 1 = 小地图是靠按住 TAB 开着的，松开要自动关 */

/* 该设置项是否是键位绑定（value 指向 g_bindings 的某一项）。
   是的话返回对应 ActionId，否则返回 -1。
   之前这里是一串 21 项的地址比较链，三处各抄一份，漏一个就出 bug。 */

/* VK 码 -> 显示名（改绑提示、QTE 提示共用）。
   在 optKeys 里找不到的（比如一些扩展键）返回 NULL，调用方自己兜底。 */

/* 玩家碰撞半径：贴墙时留出半个身位，避免半个身子钻进墙里 */

/* ==================================================================
 * 2026-11-06（用户实测「藏身柜传送 bug」/「相位传送 bug」）
 * ------------------------------------------------------------------
 * 玩家位置有两个地方会被**直接瞬移赋值**：
 *   · 相位传送  posX = phaseX
 *   · 躲藏身柜  posX = hideSpots[i].x
 * 一旦落点被判为阻塞（家具边界、半径擦墙、地图改动），
 * 人就卡死在原地 —— 表现就是"传过去了但动不了"。
 *
 * 统一走这个函数：落点阻塞就在周围 1~4 格里找一个能站的。
 * ================================================================== */

/* 弹丸的墙判定：只用**点**，不套玩家的碰撞半径。
   Blocked() 带 0.24 的半径，那是给玩家用的；弹丸如果也用它，
   出生点附近稍微贴墙就会立刻判定为「撞墙」—— 瓶子一扔就碎。
   这里改成取所在格子的中心来判断，边界行为稳定。 */

/* 词条的两个纯函数，单独拆出来是为了能离线自检直接调用：
   RollLevelMod 决定「这一层是什么词条」，ContainerTargetCount 决定
   「这个词条下要放几个容器」——都是不依赖世界状态的纯计算。 */

/* roll100: 0~99 的掷点（决定有没有抽到词条）；pick: 0~MOD_COUNT-1 的词条编号 */

/* 某个词条下，本层要放多少个容器。
   CRAMPED -2，RICH +2，其余不变。结果恒在 [2, MAX_CONTAINERS] 内。 */

/* 测试接缝：离线自检需要能指定「这一层用哪个词条」，
   否则 GenerateLevel 内部会自己重掷，测试没法比较两种词条的差异。
     -2 = 强制无词条
     -1 = 正常按随机流程掷词条
     0..5 = 强制使用该词条
   注意：无论走哪条分支，都必须消耗同样次数的 rand()，
   否则后续的地图/容器随机序列会错位，测试就没法做对照了。 */

/* v3.1 训练场改成新手教程：一条长廊 + 一个可翻的箱子 + 一个不会动的靶子。
   不再摆安全屋/配电箱/出口 —— 教程只教「怎么操作」，不要求真的逃出去。 */

/* v2.1：找离出生点最远的空格（BFS 洪泛）。
   原来出口硬编码在出生点正右方 2 格 —— 每层开局第一帧构图完全一样，
   而且整层探索的动机被削掉一半。 */
/* v6 第 ㉔ 轮：**从任意格出发**找最远格。
   原来只有"从出生点 (1,1)"一个版本，所以战役里写"出口更远"
   根本做不出来 —— 最远格就是最远格，再找一次还是它。
   现在第 3/5 关改成「从配电箱出发的最远格」：
   通电之后你必须横穿整张图。 */

/* Forward cursor visibility to the selected platform backend. */
void ForceCursor(int visible) {
    PlatShowCursor(visible);
}

/* ==================================================================
 * 2026-11-06（网页版 R2）：**鼠标视角旋转抽成平台无关函数**
 * ------------------------------------------------------------------
 * 原来这段逻辑长在 WM_MOUSEMOVE 里，网页端根本没有窗口消息。
 * 现在 Win32 的 WndProc 与网页后端都调这一个函数，行为完全一致。
 * ================================================================== */
void CanityMouseLook(int dx, int dyv) {
    if (dyv != 0) {
        /* v1.12：鼠标上下 = 抬头/低头（地平线偏移）。
           y-shearing 与这套墙渲染冲突（远距离黑边、缝隙、光球遮挡错乱），
           先关掉，等换成"真俯仰"（每列按 pitch 重新投影）再开。 */
        #define PITCH_ENABLED 0
        #if PITCH_ENABLED

        pitchRatio -= (float)dyv * 0.0030f * mouseSens;
        if (pitchRatio > 1.0f) pitchRatio = 1.0f;
        if (pitchRatio < -1.0f) pitchRatio = -1.0f;
        uiDirty = 1;
        #else
        {

            pitchRatio = 0.0f;
            horizon = 0;
        }
        #endif
    }
    if (dx != 0) {
        float rot = (float)(dx * MOUSE_BASE_SENS * mouseSens);
        float oldDirX = dirX;
        dirX = dirX * cosf(rot) - dirY * sinf(rot);
        dirY = oldDirX * sinf(rot) + dirY * cosf(rot);
        float oldPlaneX = planeX;
        planeX = planeX * cosf(rot) - planeY * sinf(rot);
        planeY = oldPlaneX * sinf(rot) + planeY * cosf(rot);
    }
}
void UpdateExplored(void) {
    int radius = 4, px = (int)posX, py = (int)posY;
    for (int y = py - radius; y <= py + radius; y++) {
        for (int x = px - radius; x <= px + radius; x++) {
            if (x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT &&
                (x - posX) * (x - posX) + (y - posY) * (y - posY) <= radius * radius)
                explored[y][x] = 1;
        }
    }

    /* MAP MASTER：检测本层是否全图探索完（仅走廊格子，墙不算） */
    static int lastCheckedLevel = 0;   /* 上一次检测的层数 */
    static int levelFullyExplored = 0; /* 本层已达成全探索标记，防重复计数 */
    if (currentLevel != lastCheckedLevel) {
        lastCheckedLevel = currentLevel;
        levelFullyExplored = 0;
    }
    if (!levelFullyExplored) {
        int allExplored = 1;
        for (int y = 1; y < MAP_HEIGHT - 1 && allExplored; y++) {
            for (int x = 1; x < MAP_WIDTH - 1; x++) {
                if (worldMap[y][x] == 0 && explored[y][x] == 0) {
                    allExplored = 0;
                    break;
                }
            }
        }
        if (allExplored) {
            mapsFullyExplored++;
            levelFullyExplored = 1;
        }
    }
}

/* 手电每秒耗电：ECO 升级每级 -12% */

/* 安全屋回智速度：REST 升级每级 +25% */

/* v1.8：理智流失随层数增长（原版是常量，导致经济净正、玩家不死） */

/* v1.8：瓶子携带上限（绣刀的代价是把它压到 1） */

/* v2.7：体力上限以前写死 100，局内技能 LONG WIND 的「+25 体力」白给。
   上限统一走这里，HUD 的分母也跟着用。 */
/* v2.0：理智上限（遗物 WAR PAINT 会削） */
/* v2.3：武器能量 —— 三把武器共用，**不会自然恢复**。
   一局的总输出因此有上限，投掷物（不吃能量）才有存在意义。 */
float energy = 120.0f;

/* v2.8：改造树是一个**可平移缩放的画布**（参考思维导图）。
   节点用世界坐标，画的时候统一变换 —— 这样窗口多小都不会崩，
   节点数量也能无限加。 */

/* ===== v3.4 金币产出公式（方案 v5 §4）=====
    v3.4 银行结算（方案 v5 §2.3）
   BANK += CARRY + 利润 × 倍数          （活着结算）
   BANK += CARRY × 0.5                  （死亡，本金损失一半 ——
                                          调研：100% 没收会 feel-bad，
                                          50~75% 既保留赌注又不让人觉得白玩）

   倍数 = 1 + min(1, CARRY / 历史最高)   → 1.0 ~ 2.0
   带 <20% 历史最高时利润 ×0.5（惩罚躺平：不能永远只带一点点） */

/* ==================== 进度 · 存档 ==================== */

int BankPreviewGain(int survived) {
    return WalletPreviewSettle(survived,EconomyRewardedProfit(survived,NULL));
}


/* ==================== 进度 · 存档 ==================== */
void BankSettle(int survived) {
    lastRunGold=carryGold; lastRunCaught=CatchCountThisRun();
    int profit = EconomyRewardedProfit(survived,&lastPerfMul);

    /* v6：结算统一走 WalletSettle */
    {
        int beforeBank = WalletBank();
        {  WalletSettle(survived, profit); }
        lastRunKept=WalletBank()-beforeBank;
        /* v6 第 ⑲ 轮：**实测遥测** —— 每局结束写一行，
           玩几局之后就能算真实金币/局、真实时长，
           再回头校准技能树价格（现在是推算值，不是实测值）。 */
        {

float ExtractDepthMul(int level);
            TelemetryRunEnd(currentLevel,
                            WalletBank() - beforeBank,
                            lastRunGold,
                            survived ? 1 : 0,
                            runTime,
                            CatchCountThisRun());
        }
    }
    SteamZoneSetStatInt("BEST_FLOOR",bestLevel);
    SteamZoneSetStatInt("BANK_GOLD",bankGold);
    SteamZoneStoreStats();
    SaveGame();
}

/* ===== v3.4 金币产出公式（方案 v5 §4）=====
   原来三个值都是固定常数：搜箱 12 / 配电箱 32 / 过关 50。
   问题是深层和浅层一样值钱 —— 玩家没有往下走的理由。

   现在全部随层数递增（线性项），累计是二次曲线：
     第 1 层   一层 102 金币
     第 12 层  一层 267
     第 24 层  一层 447
   配合银行倍数，深层一局的收益是浅层的 4~6 倍。 */
float EndlessDepthBonus(int level);
/* ==================================================================
 * 2026-11-06（A-1 单箱金币回调）
 * ------------------------------------------------------------------
 * 局内箱子从 6~8 涨到 8~12（+43%），而单箱金币没变
 * → 每层搜刮收入从 63 涨到 90，整体金币 +35~40%。
 *
 * 这里把单箱收益按同比例压回去：8+1.5L -> 6+1.1L
 *   第 1 层  9 -> 7   （×1.43 箱子 ≈ 10，净 +11%）
 *   第 8 层 20 -> 15  （净 +5%）
 *   第16 层 32 -> 24  （净 +6%）
 * 净通胀压到 +5~11%，几乎无感；价格表一个都没动。
 * ================================================================ */
float GoldSearchAt(int level)  {
    /* 2026-11-06：崩塌层金币 x1.8（用户要求"箱子金币概率变高"） */

    float mul = collapsedLevel ? 1.8f : 1.0f;
    return ( 6.0f + 1.1f * (float)level) * EndlessDepthBonus(level) * mul;
}

/* v6 **深度倍数** —— 撤离时按当前层算。
   第 1 层 1.00，第 24 层 2.00（上限 2.0）。
   越往下撤离越值钱，这是「贪」的诱惑。 */

/* v3.4 **无尽模式深度加成**（方案 v5 §4.3）——
   24 层之后每层额外 +15% 金币，线性叠加不封顶。
   这让无尽模式成为真正的刷钱途径，但风险同样递增。 */

/* ===== v6 **威胁分（ThreatScore）与金币招怪** =====
   算法三步：
     ① 归一化：每个数值 / 参考值   → 基准怪物每项都是 1.0
     ② 加权：  × 该项权重           → 体现哪个数值真致命
     ③ 求和：  Σ                    → 无量纲综合分，基准 = 1.0

   然后金币按 GOLD_THREAT_STEP 线性放大这个分数。
   为什么金币只放大**部分**数值（听觉全吃、速度吃 40%）：
     如果速度和听觉同时 +100%，玩家会觉得「钱一多就必死」，
     而调研强调「stakes amplify a game; they can't replace one」。
     让钱主要影响「被听见」，玩家还有「走慢一点」的应对空间。 */

/* 怪物当前的威胁分（不含金币） */

/* ==================== 实体 · 怪物 ==================== */
float MonsterThreatScore(void) {

    float sSpeed   = (THREAT_REF_SPEED  > 0.01f) ? Hunter0()->speed / THREAT_REF_SPEED : 1.0f;
    float sHear    = 1.0f;                       /* 听觉基数，由 MakeNoise 的半径体现 */
    float sPersist = 1.0f;                       /* 持久度，由 huntTimer 上限体现 */
    float sReact   = 1.0f;                       /* 反应速度 */
    return THREAT_W_SPEED   * sSpeed
         + THREAT_W_HEAR     * sHear
         + THREAT_W_PERSIST  * sPersist
         + THREAT_W_REACT    * sReact;
}

/* v2.3：武器能量上限 */

/* ---------------- 寻声怪 ---------------- */
/* v1.16：蓄满（>=0.85）额外 +1 伤害 */
int chargeBonus(float charge) {
    int b = (charge >= 0.85f) ? 1 : 0;
    if (b) b += SkillHeavyHand();    /* HEAVY HAND */
    return b;
}

/* v2.7：ThrowItem 是否真的生成了一个投掷物。
   以前池子满时它会静默不生成，而调用方已经先把数量扣掉了 —— 东西凭空消失。 */
int g_throwOk = 0;

/* ------------------------------------------------------------------ */
/* v2.0 武器攻击                                                        */
/* ------------------------------------------------------------------ */

/* v2.0 TYPE WORK：武器伤害类型。
   rank 0 = 按武器默认（近战钝 / 远程金币 / 法术斩）；
   rank 1/2/3 = 强制 斩 / 钝 / 金币 —— 让"改刃"变成石头剪刀布。 */
int WeaponDamageType(int id) {
    int r = SkillTypeWorkRank();
    if (r <= 0) {
        const WeaponDef* w = WeaponGet(id);
        if (w->kind == WPN_RANGED) return DT_SHARD;
        if (w->kind == WPN_SPELL)  return DT_SLASH;
        return DT_BLUNT;
    }
    if (r == 1) return DT_SLASH;
    if (r == 2) return DT_BLUNT;
    return DT_SHARD;
}

/* v2.0 RELOAD RHYTHM：每第 3 次命中无视护甲 */

/* v2.4.80：WeaponTryAttack 已删除 —— 左键改走 WrenchStrike()，
   这个函数（含里面的旧扳手击退、拾荒者判定）已经没有调用者，
   留着只会让人以为"还有另一条攻击路径"。 */

/* v2.1：对外统一的粒子接口。tint: 0 玻璃 / 1 血 / 2 尘 */

/* 生成玻璃金币：撞击点产生随机扩散的金币 */

/* ===== v2.4.62 **回声弹的永久标记** =====
   用户要求：「增加怪物图标在地图上，毕竟是永久透视，
   有怪物的时候在地图上就能看到，仅限那两三格。
   就是被扫描投掷物照出来之后，这一层可以永久透视那两三格。」

   所以：回声弹落地时，把落点 3 格内的所有怪物**记下来**，
   这一层之内它们都会显示在小地图上（红色骷髅点）。
   换层时清空。 */
/* ==================================================================
 * 2026-11-05：**回声弹改成「资源雷达」（方案 A）**
 * ------------------------------------------------------------------
 * 旧版：半径 3 格、只标怪物、永不消失 —— 玩家在 3 格内本来就能听到怪，
 *       等于零信息增量，纯占一个道具槽。
 *
 * 新版：半径 10 格、**标资源和威胁两种东西，各自有时效**。
 *   容器 30 秒（保险丝箱金色、宝箱怪红闪）  ← 玩家真正的问题
 *   活物  5 秒（红菱形，会跟着动）          ← 附带
 *   落地自带 20 格噪音                      ← 代价：敲门，不是偷看
 * ================================================================ */
/* 扫描半径读 common.h 的 ECHO_RADIUS（唯一权威来源） */
#define ECHO_NOISE_R    20.0f     /* 落地噪音半径 —— 这是它的代价 */
#define ECHO_LIFE_MON    5.0f     /* 活物标记活多久 */
#define ECHO_LIFE_CRATE 30.0f     /* 容器标记活多久 */

/* 标记类型：决定小地图上画什么颜色 */
enum {
    EMK_MON   = 0,   /* 活物：红菱形 */
    EMK_CRATE = 1,   /* 普通容器：白方块 */
    EMK_FUSE  = 2,   /* 保险丝箱：金方块 */
    EMK_MIMIC = 3    /* 宝箱怪：红色闪烁方块 */
};

float echoMarkX[ECHO_MARK_MAX];
float echoMarkY[ECHO_MARK_MAX];
int   echoMarkActive[ECHO_MARK_MAX];
float echoMarkT[ECHO_MARK_MAX];      /* 剩余寿命（秒） */
int   echoMarkKind[ECHO_MARK_MAX];   /* EMK_* */

/* ==================== 道具 · 投掷 ==================== */
void EchoMarksReset(void) {
    for (int i = 0; i < ECHO_MARK_MAX; i++) {
        echoMarkActive[i] = 0;
        echoMarkX[i] = echoMarkY[i] = 0.0f;
        echoMarkT[i] = 0.0f;
        echoMarkKind[i] = EMK_MON;
    }
}

/* 在 (sx,sy) 周围 rad 格内记录所有怪物 */

/* ==================== 道具 · 投掷 ==================== */
void EchoMarksScan(float sx, float sy, float rad) {

    /* 落点自带噪音 —— **这是它的代价**：敲门会让屋里的人知道。
       没有这一下，回声就是免费情报，"什么时候用"就不再是个决策。 */
    MakeNoise(sx, sy, ECHO_NOISE_R);

    float r2 = rad * rad;

    /* 往标记表里放一条：同类同位置就刷新寿命，否则找空槽 */
    /* （C89 风格：不用嵌套函数，直接内联展开成两段） */

    /* ---------------- ① 活物：5 秒 ---------------- */
    {
        float px[10]; float py[10]; int n = 0;
        if (Hunter0()->active)    { px[n] = Hunter0()->x;    py[n] = Hunter0()->y;    n++; }
        if (Hunter1()->active)   { px[n] = Hunter1()->x;   py[n] = Hunter1()->y;   n++; }
        if (Hunter2()->active)   { px[n] = Hunter2()->x;   py[n] = Hunter2()->y;   n++; }
        if (lightBeast.active) { px[n] = lightBeast.x; py[n] = lightBeast.y; n++; }
        if (!trainingMode) {
            if (CollectorActive()) { px[n] = CollectorX(); py[n] = CollectorY(); n++; }
            if (ScavengerActive()) { px[n] = ScavengerX(); py[n] = ScavengerY(); n++; }
            if (WardenActive())    { px[n] = WardenX();    py[n] = WardenY();    n++; }
        }
        for (int i = 0; i < n && i < 10; i++) {
            float dx = px[i] - sx, dy = py[i] - sy;
            if (dx * dx + dy * dy > r2) continue;
            int slot = -1, same = -1;
            for (int k2 = 0; k2 < ECHO_MARK_MAX; k2++) {
                if (echoMarkActive[k2] && echoMarkKind[k2] == EMK_MON) {
                    float ox = echoMarkX[k2] - px[i], oy = echoMarkY[k2] - py[i];
                    if (ox * ox + oy * oy < 1.0f) { same = k2; break; }
                }
                if (!echoMarkActive[k2] && slot < 0) slot = k2;
            }
            if (same >= 0) { echoMarkX[same] = px[i]; echoMarkY[same] = py[i];
                             echoMarkT[same] = ECHO_LIFE_MON; echoMarkKind[same] = EMK_MON; }
            else if (slot >= 0) { echoMarkActive[slot] = 1;
                             echoMarkX[slot] = px[i]; echoMarkY[slot] = py[i];
                             echoMarkT[slot] = ECHO_LIFE_MON; echoMarkKind[slot] = EMK_MON; }
        }
    }

    /* ---------------- ② 容器：30 秒（保险丝金、宝箱怪红闪）---------------- */
    for (int ci = 0; ci < containerCount && ci < MAX_CONTAINERS; ci++) {
        if (!containers[ci].active) continue;
        if (containers[ci].searched && !containers[ci].mimicHost) continue;   /* 翻过的不再标 */
        float dx = containers[ci].x - sx, dy = containers[ci].y - sy;
        if (dx * dx + dy * dy > r2) continue;

        int kind = EMK_CRATE;
        if (containers[ci].mimicHost)                    kind = EMK_MIMIC;
        else if (containers[ci].content == 1)            kind = EMK_FUSE;

        int slot = -1, same = -1;
        for (int k2 = 0; k2 < ECHO_MARK_MAX; k2++) {
            if (echoMarkActive[k2] && echoMarkKind[k2] != EMK_MON) {
                float ox = echoMarkX[k2] - containers[ci].x;
                float oy = echoMarkY[k2] - containers[ci].y;
                if (ox * ox + oy * oy < 1.0f) { same = k2; break; }
            }
            if (!echoMarkActive[k2] && slot < 0) slot = k2;
        }
        if (same >= 0) {
            echoMarkX[same] = containers[ci].x; echoMarkY[same] = containers[ci].y;
            echoMarkT[same] = ECHO_LIFE_CRATE; echoMarkKind[same] = kind;
        } else if (slot >= 0) {
            echoMarkActive[slot] = 1;
            echoMarkX[slot] = containers[ci].x; echoMarkY[slot] = containers[ci].y;
            echoMarkT[slot] = ECHO_LIFE_CRATE; echoMarkKind[slot] = kind;
        }
    }

    /* ---------------- ③ 宝箱怪：优先当 EMK_MIMIC 标（第 ② 段已覆盖）---------------- */
    for (int mi = 0; mi < mimicCount; mi++) {
        if (mimics[mi].state != MIM_DISGUISED && mimics[mi].state != MIM_SEEN) continue;
        float dx = mimics[mi].x - sx, dy = mimics[mi].y - sy;
        if (dx * dx + dy * dy > r2) continue;
        MimicOnEchoHit(mimics[mi].containerIdx);
    }
}

/* 每帧推进标记寿命 —— 信息有时效才是信息 */

/* ==================== 道具 · 投掷 ==================== */
void EchoMarksTick(float dt) {
    for (int i = 0; i < ECHO_MARK_MAX; i++) {
        if (!echoMarkActive[i]) continue;
        echoMarkT[i] -= dt;
        if (echoMarkT[i] <= 0.0f) {
            echoMarkActive[i] = 0;
            echoMarkT[i] = 0.0f;
        }
    }
}

/* ================================================================== */
/* v2.4.80 **左键 = 扳手击退**（从零重写，取代原来的 atkNow 块）        */
/* ================================================================== */
/*
 * 规则（只有这三条）：
 *   按住左键        → 蓄力（0 → 1，0.45 秒满）
 *   松手，蓄力 < 0.28 → **什么都不做**（阻尼，防误触）
 *   松手，蓄力 >= 0.28 → 击退：
 *                        · 消耗一次扳手（次数制）/ 进冷却（冷却制）
 *                        · 推走面前的怪（距离按蓄力）
 *                        · 打掉拾荒者的赃款（两次）
 *                        · 播两帧挥击动画
 *
 * 不含任何"普通敲击"—— 用户明确要求删掉那个动作。
 */
/* ==================================================================
 * v2.4.83 **多帧挥击 + 命中窗口**
 *
 * 用户要求：「动画多来几帧，然后在这几帧之内击退都生效。」
 * 意思是：不要把判定做成"松手瞬间一次性结算"，
 * 而是挥出去之后，**在挥击动作进行的过程中**判定命中。
 *
 *   帧 0  蓄力/起手        判定关
 *   帧 1  后摆最高点      判定关
 *   帧 2  挥到面前（接触）  **判定开**  ← 打中就在这一帧
 *   帧 3  收势            判定关
 *   帧 4  回位            判定关
 *
 * 这样"打中"发生在视觉上挥到的那一帧，手感才对得上。
 * ========================================================== */

/* 挥击进度 0..1（0 = 刚挥出，1 = 已收势） */

/* v8：每日“不能开手电”/ SALVAGE keystone 会禁止点灯。
   所有自动点灯路径（摇电、满电重亮、读档）都必须走这里，
   否则玩家可以靠摇电绕过每日限制。 */

/* ==================== 玩家 · 移动/手电 ==================== */


/* ------------------------------------------------------------------ */
/* 菜单动作                                                            */
/* ------------------------------------------------------------------ */

/* v2.0 每日挑战：日期当种子，武器/词条/地图都由它决定。
   同一天所有人拿到同一套随机数 —— 这才是"每日"的意义。 */
void StartDailyRun(void) {
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    if (!tm) return;
    dailySeed = (tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday;
    srand((unsigned)dailySeed);
    /* 2026-11-05：原来 `curWeapon = dailySeed % WPN_COUNT` ——
       每日局会随机抽到信号枪/符印，屏幕上卡出已下线武器的建模。
       本作只有扳手，钉死。 */
    curWeapon = WPN_MELEE;
    /* v29：不删旧存档 —— 见 ActivateMenuItem 里的说明。 */
    LoopQueueStart(START_DAILY, 0);
    StartGame();
    /* 强制机制：每日挑战理智流失 +25%，金币 +25%。
       v2.0 修复：原来直接乘在当前层的乘子上，下一层 GenerateLevel 就重置了，
       等于只有第 1 层生效。改成写进 dailyMode 的固定加成，在 GenerateLevel 里叠加。 */
}

/* v2.0：战役第 stage 关。固定种子保证每次进同一关看到的是同一张图。 */
void StartCampaign(int stage) {
    if (stage < 0) stage = 0;
    if (stage >= CAMPAIGN_STAGE_COUNT) stage = CAMPAIGN_STAGE_COUNT - 1;
    srand((unsigned)(9000 + stage * 733));
    /* 同上：战役不再按关卡号换武器 */
    curWeapon = WPN_MELEE;
    /* v29：不删旧存档 —— 见 ActivateMenuItem 里的说明。 */
    LoopQueueStart(START_CAMPAIGN, stage);
    StartGame();
}

/* v2.0 SHORTCUT：跳过当前层，直接生成下一层。
   不给金币、不给遗物 —— 换的是时间，不是资源。 */
void SkipOneLevel(void) {
    if (shortcutUsed) return;
    if (bossLevel) return;                     /* Boss 层不能跳 */
    shortcutUsed = 1;
    currentLevel++;
    GenerateLevel();
    /* Drafted skills last until the run ends. */
    pickupMsg = "SHORTCUT USED";
    pickupMsgTimer = 1.6f;
    uiDirty = 1;
}

/* v2.2 架构修正：UI 直接在**渲染空间**排版。
 *
 * 之前 UI 用窗口坐标，靠 winW/winH 换算到渲染缓冲；窗口一改大小
 * （或 DPI 缩放、最大化、全屏），两套坐标就开始打架：行高和字号脱节、
 * 鼠标命中错位、面板溢出。用户反复报的「UI 还是一大堆毛病」就是这个。
 *
 * 现在：
 *   · winW/winH  == renderW/renderH   —— 所有 UI 布局/字号/面板的唯一基准
 *   · clientW/H  == 真实客户区像素     —— 只用于鼠标坐标换算
 * 因为整帧画面（3D + UI）最后一起被拉伸到窗口，UI 自然跟着缩放，
 * 任何窗口尺寸下比例都恒定。 */
int clientW = 800, clientH = 600;

/* ===== v2.4 校准小游戏 ===== */

/* v2.6：校准难度随层数成长 —— 返回目标区缩放（1.0 = 第 1 层） */

void SyncWindowSize(void) {
    /* 2026-11-06（R4）：客户区尺寸由平台层给（网页端 = canvas CSS 尺寸） */
    int cw = 0, ch = 0;
    PlatWindowSize(&cw, &ch);
    if (cw > 0 && ch > 0) {
        clientW = cw;
        clientH = ch;
    }
    /* UI 空间 = 渲染空间 */
    if (winW != renderW || winH != renderH) {
        winW = renderW;
        winH = renderH;
        uiDirty = 1;
    }
}

/* v2.2：四种模式互斥。原来只有 New Run 清标记，无尽/训练不清，
   于是"先玩每日再进无尽"会把 +25% 带进无尽，
   "先玩战役死在半路再进无尽"会让无尽在第 1 层出口变成战役第 2 关。 */


/* 成就辅助：解锁并保存 */

/* ==================== 进度 · 存档 ==================== */


/* v6：改转发 */

/* v6：成就检查改成**表驱动** ——
   把游戏里的值打成快照，交给 achievements.c 按 kind 比对。
   加一个成就只需要在 kAchDefs[] 里加一行，这里永远不用改。 */
extern int CatchCountThisFloor(void);


/* ==================== 输入 ==================== */
void PauseGame(void) {
    /* v6：**出口弹窗状态必须重置** —— 否则暂停→主菜单→新局会带着
       上一次的弹窗标志进来，开局即被冻结。 */
    exitChoiceOpen = 0;
    exitGoDeeper = 0;
    /* v6 **暂停必须先关锁盘** ——
       失焦会自动暂停，但锁盘覆盖层还压着：
         · WM_KEYDOWN 的 if (lockActive) 在状态机之前 → 除 Esc/M 全吞
         · WM_LBUTTONDOWN 的 if (lockActive) 在 ClickMenu 之前 → 点击全给 LockClick
         · DrawLockOverlay 无条件帧末绘制 → 盖在暂停菜单上
       结果玩家解密码时 Alt+Tab 出去、回来看到锁盘盖着暂停菜单，
       鼠标点 Resume 全无效，只有连按两次 ESC 才能脱身。 */
    {  
      if (lockActive) CloseLockOverlay(); }
    /* prevState 记住"从哪来的"（正常就是 PLAYING）。
       手动暂停 vs 失焦自动暂停 用 autoPaused 区分，不要靠 prevState ——
       之前用 prevState 区分，结果拿过局内技能后手动暂停也会被自动恢复。 */
    prevState = currentState;
    currentState = STATE_PAUSED;
    menuSelection = 0;
    /* 两个标志必须一起清：只清 isMapOpen 会留下 mapFullScreen=1 的脏状态
       （全屏图开着按 ESC 就能复现），之后任何按 isMapOpen&&mapFullScreen
       判断的地方都会读到互相矛盾的组合。 */
    isMapOpen = 0;
    mapFullScreen = 0;
    LoopResetMapHold();
    ForceCursor(1);
    /* 2026-11-06（R4）：光标样式归平台层；ForceCursor(1) 已处理可见性 */
    uiDirty = 1;
}

void ApplyDisplayMode(void) {
#ifdef CANITY_WEB
    /* ==================================================================
     * 2026-11-06（用户实测：**退出对局后 UI 又错乱**）
     * ------------------------------------------------------------------
     * 这里原来把 winW/winH 设成 canvas 的 CSS 尺寸 —— 正是那个
     * "UI 空间必须等于渲染空间"的坑。任何走到 ApplyDisplayMode 的路径
     * （按 F11、恢复默认设置、切换全屏）都会把布局比例弄坏。
     *
     * 浏览器里没有"窗口模式/分辨率"这回事：canvas 由 CSS 铺满，
     * winW/winH 的唯一权威是 SyncWindowSize()。这里什么都不改。
     * ================================================================ */
    uiDirty = 1;
#else
    if (isFullscreen) {
        winW = GetSystemMetrics(SM_CXSCREEN);
        winH = GetSystemMetrics(SM_CYSCREEN);
    } else {
        if(resIndex<0 || resIndex>=RES_COUNT)resIndex=RES_AUTO;
        if(resIndex==RES_AUTO){
            RECT wa;SystemParametersInfo(SPI_GETWORKAREA,0,&wa,0);
            winW=wa.right-wa.left;winH=wa.bottom-wa.top;
        }else{winW=resolutions[resIndex][0];winH=resolutions[resIndex][1];}
    }
    InitPixels();

    if (!g_hwnd) return;

    /* v1.8：不再去掉 WS_MAXIMIZEBOX —— 这里是启动时真正生效的地方，
       原来把最大化按钮禁掉了（标题栏那个按钮是灰的）。 */
    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME;
    if (isFullscreen) {
        SetWindowLongPtr(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, 0, 0, winW, winH, SWP_FRAMECHANGED);
        /* v2.9：**窗口真的变成全屏尺寸之后**再算一次渲染分辨率。
           原来 InitPixels() 在 SetWindowPos 之前调用，那时候 GetClientRect
           还是旧的 800x600 → 全屏后渲染分辨率停在 800x600 不涨，
           既糊又让 HALFTONE 把 800x600 放大到 1080p（那一步特别慢）。 */
        InitPixels();
        SyncWindowSize();
        uiDirty = 1;
    } else {
        /* 保留 WS_VISIBLE：否则切分辨率后窗口会消失 */
        SetWindowLongPtr(g_hwnd, GWL_STYLE, style | WS_VISIBLE);

        /* v1.8：自动适配屏幕 ——
           ① 用**工作区**（扣掉任务栏）而不是整块屏幕
           ② 窗口比工作区大就等比缩到 88%，任何机器都放得下
           ③ 只改窗口尺寸，不改 winW/winH（内部逻辑分辨率不变，靠呈现拉伸） */
        RECT wa;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
        int availW = wa.right - wa.left;
        int availH = wa.bottom - wa.top;

        int cw2 = winW, ch2 = winH;
        int maxW = (int)(availW * 0.88f), maxH = (int)(availH * 0.88f);
        if (cw2 > maxW || ch2 > maxH) {
            float s = (float)maxW / (float)cw2;
            float s2 = (float)maxH / (float)ch2;
            if (s2 < s) s = s2;
            cw2 = (int)(cw2 * s);
            ch2 = (int)(ch2 * s);
            if (cw2 < 320) cw2 = 320;
            if (ch2 < 240) ch2 = 240;
        }

        RECT rect = {0, 0, cw2, ch2};
        AdjustWindowRect(&rect, style, FALSE);
        int w = rect.right - rect.left, h = rect.bottom - rect.top;
        int x = wa.left + (availW - w) / 2;
        int y = wa.top + (availH - h) / 2;
        if (x < wa.left) x = wa.left;
        if (y < wa.top) y = wa.top;
        SetWindowPos(g_hwnd, HWND_TOP, x, y, w, h, SWP_FRAMECHANGED);
    }
    firstMouseMove = 1;
    uiDirty = 1;
#endif
}

/* ==================== 输入 ==================== */
void ActivateMenuItem(void) {
    /* v3.4：Hub 页 0 的「携带档位」行 —— 回车循环 25/50/75/100% */
    /* v6：银行页回车 = 切换携带比例 */
    if (currentState == STATE_BANK) {

        bankCarryPct = (bankCarryPct + 1) % BANK_CARRY_STEPS;
        PlaySfx(SFX_POWER, 0.7f);
        uiDirty = 1;
        return;
    }
    if (currentState == STATE_MAIN_MENU) {
        int base = hasSave ? 1 : 0;
        int endlessUnlocked = EndlessUnlocked();
        int sel = MainMenuLegacySelection();
        if(sel<0)return;

        /* ===== v6 **菜单索引彻底重写** =====
           原来是一堆 base+N 和 MainMenuCount()-N 混着写，
           插了「Bank」一行之后全部错位：
             · 点 Bank 匹配不到任何分支 → 掉进最后的 else → **直接退出游戏**
             · Achievements / Bestiary 的条件重复
             · Help / Settings 各偏了一格
           现在改成**一张显式索引表**，行序和 MenuLabel() 的表严格一一对应：

             无存档                   有存档
             base+0  New Run          base+0  Continue
             base+1  Hub              base+1  New Run
             base+2  Bank             base+2  Hub
             base+3  Achievements     base+3  Bank
             base+4  Bestiary         base+4  Achievements
             base+5  Help             base+5  Bestiary
             base+6  Settings         base+6  Help
             base+7  Campaign         base+7  Settings
             base+8  Daily Run        base+8  Campaign
             base+9  Tutorial         base+9  Daily Run
             base+10 Quit             base+10 Tutorial
                                      base+11 Quit

           无尽解锁后「Endless」插在 New Run 后面，后面全部 +1，
           所以下面用 `int eo = endlessUnlocked ? 1 : 0;` 统一偏移。 */
        int eo = endlessUnlocked ? 1 : 0;
        int ixNew      = base;
        int ixHub      = base + 1 + eo;
        int ixBank     = base + 2 + eo;
        int ixAch      = base + 3 + eo;
        int ixBest     = base + 4 + eo;
        int ixHelp     = base + 5 + eo;
        int ixSettings = base + 6 + eo;
        int ixCampaign = base + 7 + eo;
        int ixDaily    = base + 8 + eo;
        int ixTutorial = base + 9 + eo;

        if (sel == 0 && hasSave) {
            ContinueRun();
        } else if (endlessUnlocked && sel == base + 1) {
            /* Endless 模式（v29：和战役/每日一样，不静默删旧档） */
            LoopQueueStart(START_ENDLESS, 0);
            StartGame();
        } else if (sel == ixNew) {
            /* Queue first. Never clear saves/mode flags before human confirmation. */
            LoopQueueStart(START_NORMAL,0);
            StartGame();
        } else if (sel == ixHub) {
            /* v2.8：HUB = 技能树，一进来光标就在第一个节点上 */
            currentState = STATE_HUB;
            hubPage = 1;
            /* v6：**光标要落在活着的节点上** ——
               SkillNodeIdAt(0,0) 是 LIGHT STEP，已退役（无矩形），
               进去之后选中框画不出来，玩家以为界面坏了。 */
            hubSelection = 1;   /* id 1 = QUIET LID（列 0 第一层，活着） */
            menuScrollOffsets[STATE_HUB] = 0;
            uiDirty = 1;
        } else if (sel == ixBank) {
            /* v6 **BANK 是独立状态**，不再寄居在 Hub 的第 3 页 */
            currentState = STATE_BANK;
            uiDirty = 1;
        } else if (sel == ixAch) {
            currentState = STATE_ACHIEVEMENTS;
            achSelection = 0;
            menuScrollOffsets[STATE_ACHIEVEMENTS] = 0;
            InitAchievements();
            uiDirty = 1;
        } else if (sel == ixBest) {
            /* v1.8：怪物图鉴 */
            currentState = STATE_BESTIARY;
            bestiarySel = 0;
            menuScrollOffsets[STATE_BESTIARY] = 0;
            uiDirty = 1;
        } else if (sel == ixHelp) {
            prevState = STATE_MAIN_MENU;
            currentState = STATE_HELP;
            uiDirty = 1;
        } else if (sel == ixSettings) {
            prevState = currentState;
            currentState = STATE_SETTINGS;
            settingsPage = 0;
            settingsSelection = 0;
            settingsPopupOpen = 0;
            rebindingAct = -1;
            rebindConflict = -1;
            uiDirty = 1;
        } else if (sel == ixCampaign) {
            /* v2.2：战役模式。
               v29：不再先 SaveGame() 再 DeleteSave() ——
               那两句等于「刚存一份、马上删掉」，正在进行的普通局会被静默丢弃。
               现在保留原存档，等战役第一次存档时自然覆盖；即使开局就崩溃，
               玩家也还能 Continue 回原来的进度。 */
            StartCampaign(0);
        } else if (sel == ixDaily) {
            /* v2.2：每日挑战（同上：不静默删档） */
            StartDailyRun();
        } else if (sel == ixTutorial) {
            /* v3.1：训练营改成新手教程 —— 不删档，不影响正式进度 */
            LoopQueueStart(START_TRAINING, 0);
            StartGame();
            uiDirty = 1;
        } else {
            SaveGame();
            PlatRequestQuit();
        }
    } else if (currentState == STATE_SAFEHOUSE) {
        ShopActivate();
        uiDirty = 1;
    /* 2026-11-08：选武器流程已删（武器改扳手后残留） */
    } else if (currentState == STATE_PAUSED) {
        if (menuSelection == 0) {
            currentState = STATE_PLAYING;
            resetInputState = 1;   /* 点 Resume 用的还是鼠标左键 —— 不复位会凭空蓄力 */
            ForceCursor(0);
            firstMouseMove = 1;
            uiDirty = 1;
        } else if (menuSelection == 1) {
            prevState = currentState;
            currentState = STATE_SETTINGS;
            settingsPage = 0;
            settingsSelection = 0;
            settingsPopupOpen = 0;
            rebindingAct = -1;
            rebindConflict = -1;
            uiDirty = 1;
        } else if (menuSelection == 2) {
            /* v28：训练/教程不是正式进度。
               原来这里无条件 SaveGame() + hasSave=1：
                 · SaveGame() 只写了设置（正确）
                 · 但 trainingMode **没有清**，hasSave 却被强行置 1
               于是回到菜单点 Continue，会把上一份每日/战役存档读出来，
               同时 trainingMode 还是 1 —— 挑战地图 + 训练模式无限道具，
               教程的出口判定又因为 trainingMode 被跳过，玩家卡死在里面。
               现在训练局直接丢弃，模式标志全部清掉；菜单里是否还有
               Continue 由真正的存档决定，不再强行伪造一个。 */
            if (trainingMode) {
                ResetRunModeFlags();
                hasSave = SaveHasPendingRun();
                currentState = STATE_MAIN_MENU;
                menuSelection = 0;
                ForceCursor(1);
                uiDirty = 1;
                return;
            }
            SaveGame();
            hasSave = SaveHasPendingRun();
            ForceCursor(1);resetInputState=1;
            /* 注意：这里**不能**清 dailyMode/campaignMode/runInProgress ——
               菜单里改设置还会调用 SaveGame，而 SaveGame 会按运行时状态
               重新序列化整份存档。清了模式标志，Continue 出来的每日/战役
               就会退化成普通局。 */
            currentState = STATE_MAIN_MENU;
            menuSelection = 0;
            uiDirty = 1;
        }
    } else if (currentState == STATE_GAMEOVER) {
        /* 结算已落盘：清掉本局模式标志，避免带进下一次开局/继续。 */
        ResetRunModeFlags();
        currentState = STATE_MAIN_MENU;
        menuSelection = 0;
        uiDirty = 1;
    }
}

/* ==================================================================
 * 2026-11-06（v2.4.1 第 3 轮）：**奖励调度抽成函数**
 * ------------------------------------------------------------------
 * 原来这段逻辑内联在 Update() 里（一个两千多行的函数），
 * 既没法单独测，也没法在别处复用。
 *
 * 规则：
 *   3 的倍数层 → 遗物
 *   偶数层     → 技能
 *   6 / 12 同时命中 → 遗物先、技能后，界面标 (1/2) 与 (2/2)
 * ================================================================== */

/* ==================== 输入 ==================== */
void ActivateHubRow(void) {
    /* v2.8：HUB 只剩技能树一页（基础属性升级已折进节点），节点式操作，
       Esc 回主菜单。原来那套"6 行升级 + Back 行"的分支整段删掉了。 */
    if (hubSelection < 0 || hubSelection >= SKILL_COUNT) return;
    int ok = 0;
    if (SkillCanBuy(hubSelection))            ok = SkillBuy(hubSelection);
    else if (SkillSwitchable(hubSelection))   ok = SkillSwitchKeystone(hubSelection);
    if (ok) {
        PlaySfx(SFX_POWER, 0.8f);
        SaveGame();
    } else {
        /* ============================================================
         * 修：**买不了的时候完全没反馈**
         * ============================================================
         * 原来失败就是静默 return，玩家按了回车以为键坏了。
         * 详情条虽然写着「金币不够 / 需要先点亮上面的节点」，
         * 但那是常驻文字，按下去的瞬间没有任何回应。
         * 现在给一句 1.2 秒的提示 —— 和改造树的行为对齐。
         * ============================================================ */

        const SkillDef* d = &skillDefs[hubSelection];
        if (skillRank[hubSelection] >= d->maxRank) {
            pickupMsg = Tr("ALREADY MAXED", "已满级");
        } else if (!(d->prereq < 0 || skillRank[d->prereq] > 0)) {
            pickupMsg = Tr("NEEDS THE NODE ABOVE", "需要先点亮上面的节点");
        } else {
            pickupMsg = Tr("NOT ENOUGH GOLD", "金币不够");
        }
        pickupMsgTimer = 1.2f;
    }
    uiDirty = 1;
}

/* ------------------------------------------------------------------ */
/* 窗口过程                                                            */
/* ------------------------------------------------------------------ */

/* v1.8：鼠标坐标换算。
   菜单几何（MakeGeo / RowRect）用的是**逻辑画布空间**（winW x winH，默认 800x600），
   而 WM_MOUSEMOVE 给的是**客户区像素**。窗口一放大，两者就对不上，
   表现就是"最大化后所有菜单的鼠标都点不中"。
   所有命中测试前统一走这个换算。 */
/* v6：技能树相机（实现在 ui.c） */
void SkillZoomAt(float factor, int mx, int my);
int  CanityKeyDown(int vk, int repeat);
void SkillResetView(void);
void SkillDragStart(int mx, int my);
void SkillDragMove(int mx, int my);
void SkillDragEnd(void);
int  SkillIsDragging(void);

/* ==================== 输入 ==================== */
void MouseToLogical(int* mx, int* my) {
    int cw = 0, ch = 0;
    PlatWindowSize(&cw, &ch);
    if (cw <= 0 || ch <= 0) return;
    *mx = *mx * winW / cw;
    *my = *my * winH / ch;
}

/* ==================================================================
 * 2026-11-06（用户实测：**主页鼠标漂移约三格**）
 * ------------------------------------------------------------------
 * 这个函数原来第一行自己做 MouseToLogical（客户区像素 -> 逻辑空间）。
 * 但网页端传进来的**已经是渲染空间坐标**（而且 winW==renderW 是硬约定，
 * 渲染空间就是逻辑空间），于是被换了两次 —— 悬停"银行"却落到"新游戏"。
 *
 * 现在契约写死：**入参是逻辑坐标**。
 *   · Win32：WndProc 负责先 MouseToLogical 再调它（客户区坐标 -> 逻辑）
 *   · 网页：JS 已经把 client 坐标按 letterbox 反算成渲染(=逻辑)坐标
 * ================================================================== */
void HitTestMenu(int mx, int my) {
    if(LoopRunReplacementPending())return;
    if(currentState==STATE_SKILL_PICK){int i=SkillPickAt(mx,my);if(i>=0){skillPickSel=i;uiDirty=1;}return;}
    if (currentState == STATE_GAMEOVER) return;

    /* v1.8：主菜单 / 暂停菜单也支持鼠标悬停（之前只有 HUB 和设置接了） */
    if (currentState == STATE_MAIN_MENU || currentState == STATE_PAUSED) {
        MenuGeo g = GetMenuGeo();
        int idx = -1;
        if (g.rowH > 0 && my >= g.firstY) idx = (my - g.firstY) / g.rowH;
        if (idx >= 0 && idx < g.count && menuSelection != idx) {
            menuSelection = idx;
            uiDirty = 1;
        }
        return;
    }

    if (currentState == STATE_HUB) {
        /* v6：Hub 只有技能树一页，直接命中节点 */
        {
            int id = SkillNodeHitTest(mx, my);
            if (id >= 0 && hubSelection != id) { hubSelection = id; uiDirty = 1; }
            return;
        }
        MenuGeo hg = GetMenuGeo();
        for (int i = 0; i < hg.count; i++) {
            RECT r = RowRect(hg, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                if (hubSelection != i) { hubSelection = i; uiDirty = 1; }
                return;
            }
        }
        return;
    }

    if (currentState == STATE_SETTINGS) {
        if (settingsPopupOpen) {
            if (settings[popupItem].kind == SET_SLIDER) return;   /* 滑块没有行可高亮 */
            MenuGeo p = GetPopupGeo();
            for (int i = 0; i < p.count; i++) {
                RECT r = RowRect(p, i);
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    if (popupSelection != i) { popupSelection = i; uiDirty = 1; }
                    return;
                }
            }
            return;
        }
        MenuGeo g = GetMenuGeo();
        for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                if (settingsSelection != i) { settingsSelection = i; uiDirty = 1; }
                return;
            }
        }
        return;
    }

    if (currentState == STATE_ACHIEVEMENTS) {
        MenuGeo g = GetMenuGeo();
        int scrollOff = menuScrollOffsets[STATE_ACHIEVEMENTS];
        int newHovered = -1;
        for (int i = scrollOff; i < g.count && i < scrollOff + g.maxVisibleRows; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                if (achSelection != i) { achSelection = i; uiDirty = 1; }
                newHovered = i;
                break;
            }
        }
        if (achHovered != newHovered) {
            achHovered = newHovered;
            uiDirty = 1;
        }
        return;
    }

    /* v2.1：图鉴页以前**没有接鼠标** —— 悬停走的是最后的兜底分支，
       改的是 menuSelection（跟这一页没关系），所以鼠标在这一页等于失效。
       注意别让它掉进最后那个兜底：那里会调 ActivateMenuItem()，
       用菜单那套下标去执行，在图鉴页上可能直接开始一局新游戏。 */
    if (currentState == STATE_BESTIARY) {
        MenuGeo g = GetMenuGeo();
        for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                if (bestiarySel != i) { bestiarySel = i; uiDirty = 1; }
                return;
            }
        }
        return;
    }

    MenuGeo g = GetMenuGeo();
    for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
        RECT r = RowRect(g, i);
        if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
            if (menuSelection != i) { menuSelection = i; uiDirty = 1; }
            return;
        }
    }
}


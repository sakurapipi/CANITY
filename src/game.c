/*
 * Canity — 游戏核心实现：世界 / 关卡 / 规则 / 输入 / 菜单状态机 / 窗口过程
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
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
#include "game.h"
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
#include "bestiary.h"
#include "score.h"

/* ------------------------------------------------------------------
 * 2026-11-06：舱门效果与崩塌互相调用，需要前置声明。
 * 放在所有 include 之后、任何函数之前，避免"定义在使用之后"。
 * ------------------------------------------------------------------ */
void GenerateLevel(void);
void CollapseBegin(void);

/* 文件内前置声明：这两个 static 辅助函数在被调用处之后定义
   （原来集中在 main.c 中部的前置声明块，拆分后只保留模块内部需要的）。 */
void ThrowItem(int type, float charge);
void SpawnShards(float x, float y, float vx, float vy, float charge);
/* v3.3：粘胶区域（实现放在 entity.c） */
void SpawnGlue(float x, float y);
/* InitAchievements is public now (see game.h) */
static void UnlockAchievement(int idx);
static void CheckAchievements(void);
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
int settingsSelection = 0;
int settingsPopupOpen = 0;
int popupSelection = 0;
int popupItem = -1;
int rebindingAct = -1;       /* >=0 = 正在等待按键改绑的动作（ActionId），-1 = 未在改绑 */
int rebindConflict = -1;     /* 改绑确认中：目标键被该动作占用（ActionId），-1 = 无冲突 */
int resetDefaultsFlag = 0;   /* Reset to Defaults 行的哨兵变量（见 settings 表） */
int uiDirty = 1;
int sliderDragging = 0;
/* 系统设置 */
int resIndex = 0;
/* v1.8：分辨率档位扩充（RES_COUNT / RES_AUTO 定义在 common.h） */
const int resolutions[RES_COUNT][2] = {
    {800, 600}, {1024, 768}, {1280, 720}, {1366, 768},
    {1600, 900}, {1920, 1080}, {2560, 1440}, {0, 0}
};
int isFullscreen = 0;
int showFps = 0;
int fpsCapIndex = 1;                                   /* 30 / 60 / 120 / 144 / 不限制 */
const int fpsCaps[5] = {30, 60, 120, 144, 0};
int renderScaleIndex = 0;                              /* 0..3 = 100/75/50/25%，固定值，运行时不自动改变 */
const int renderScales[4] = {100, 75, 50, 25};
float mouseSens = 1.0f;                                /* 0.2x - 3.0x，滑块调节 */
int vignetteOn = 1;                                    /* 低理智红边特效 */
int hudDetail = 1;                                     /* 0=Minimal 1=Normal 2=Full */
int qteHint = 1;                                       /* 0=Minimal（只留大键+倒计时）1=Full（带说明） */
int fovIndex = 2;                                      /* 66 / 75 / 85 / 95 / 105 度 */
/* FPS 统计 */
int currentFps = 0;
double lastRenderMs = 0.0;   /* 最近一帧（含呈现与 HUD）的耗时 */
double frameMsMax = 0.0;     /* 最近一秒内的最差帧，用来观察卡顿 */

/* 世界 */
int worldMap[MAP_HEIGHT][MAP_WIDTH];
int explored[MAP_HEIGHT][MAP_WIDTH];
int currentLevel = 1;
float exitX = 0.0f, exitY = 0.0f;
float flashTime = 0.0f;      /* 切关白屏剩余秒数 */
float healFlash = 0.0f;      /* 回血白闪剩余秒数 */
int levelMod = -1;
float modSanityMul = 1.0f;
float modSpeedMul = 1.0f;
float modBatteryMul = 1.0f;   /* v2.0 事件：手电耗电倍率 */
float modShardMul = 1.0f;     /* v2.0 事件：金币倍率 */
float modNoiseMul = 1.0f;     /* v2.0 事件：噪声倍率（死寂） */
float levelBanner = 0.0f;      /* 进关后显示词条名的秒数 */
float safeZoneMsg = 0.0f;      /* 进入安全屋后提示「SAFE ZONE」的剩余秒数 */
int carryGold = 0;
int carryAtRunStart = 0;   /* v6：本局开局时的携带量（算净赚用） */
int upgBattery = 0;      /* 电量上限 +20/级 */
int upgSpeed = 0;        /* 移速 +6%/级 */
int upgQuiet = 0;        /* 噪声半径 -15%/级 */
int upgSanity = 0;       /* 理智流失 -10%/级 */
int upgEco = 0;          /* 手电耗电 -12%/级 */
int upgRest = 0;         /* 安全屋回智 +25%/级 */

int hubSelection = 0;
int weaponPickSel = 0;          /* v2.0: 开局选武器光标 */
int flares = 0, bottles = 0;
int bricks = 0, acids = 0, smokeBombs = 0;   /* v1.16 新投掷物 */
int firePots = 0, gluePots = 0, mirrors = 0; /* v2.0 三个新投掷物（v3.3：火/镜已删，只剩 gluPots 在用） */
int echoBolts = 0;   /* 2026-11-05 回声弹 = 资源雷达：ECHO_RADIUS 内标容器 30 秒 / 活物 5 秒 */
int decoys = 0;      /* v3.3 玩家诱饵玩偶：C 键放置，每 DECOY_SHOUT_EVERY 秒发声 */
int g_dmgIgnoreArmor = 0;
float smokeDim = 1.0f;      /* 烟幕内的光线衰减 */
int monsterMuted = 0;       /* v1.20b：只静怪物的叫声（训练场） */
/* ---- v1.17 训练场 ---- */
int trainingMode = 0;
int trainingTotal = 0;          /* 累计伤害 */
int trainingLast = 0;           /* 最近一次 */
int trainingCount = 0;          /* 命中次数 */
int trainingHist[6] = {0};      /* 最近 6 次 */
float trainingDpsTimer = 0.0f;
int trainingDps = 0;
/* ---- v3.1 新手教程 ---- */
int tutorialStep = 0;
/* v3.3 **引导默认关闭** ——
   用户反馈「每次进训练营引导都在，很烦」。
   现在进 Tutorial 只显示一行「按 B 激活引导」，按 B 才展开分步引导。 */
int tutorialGuideOn = 0;
int tutorialDone = 0;
enum {
    TUT_MOVE = 0,
    TUT_LOOK,
    TUT_SPRINT,
    TUT_LIGHT,
    TUT_MAP,
    TUT_SEARCH,
    TUT_CRANK,
    TUT_THROW,
    TUT_KNOCK,
    TUT_BREAKER,
    TUT_EXIT,
    TUT_DONE
};
static float tutorialTimer = 0.0f;        /* 完成后的回菜单倒计时 */
static float tutorialActionTimer = 0.0f;  /* 持续动作计时（冲刺/摇电） */
/* ==================================================================
 * 2026-11-06（**P1-4 教程死锁的兜底**）
 * ------------------------------------------------------------------
 * 有几步的完成条件是**外部状态**决定的：
 *   TUT_SPRINT —— 每日限制禁跑时，sprinting 被强制清零，永远攒不满
 *   TUT_LIGHT  —— 禁灯（每日限制）时，玩家根本开不了手电
 * 任一条成立，教程就会停在那一步，而且玩家没有任何提示。
 *
 * 兜底：本步超过 TUTORIAL_STEP_TIMEOUT 秒就放行，并给一句说明。
 * 教程的目的是把操作教一遍，不是拦住玩家。
 * ================================================================== */
#define TUTORIAL_STEP_TIMEOUT 12.0f
static float tutorialStepTimer = 0.0f;
static float tutorialStartX = 0.0f, tutorialStartY = 0.0f;
static float tutorialStartDirX = 1.0f, tutorialStartDirY = 0.0f;
static int   tutorialLightOff = 0;        /* 手电关过再开 */
static int   tutorialThrown = 0;
static int   tutorialKnocked = 0;
static int   tutorialMapOpened = 0;       /* 地图至少打开过一次 */
SmokeCloud smokes[MAX_SMOKE];
GlueZone  glues[MAX_GLUE];     /* v3.3 粘胶区域 */
float echoScanT = 0.0f;        /* v3.3 回声弹剩余秒数 */
float echoScanX = 0.0f, echoScanY = 0.0f;
int throwSelected = 0;        /* 0=荧光棒, 1=玻璃瓶, 2=预留 */
Thrown thrown[MAX_THROWN];
Shot shot[MAX_SHOTS];
Shard shards_debris[MAX_SHOTS * SHARD_COUNT];
SafeRoom safeRoom;
Container containers[MAX_CONTAINERS];

/* v3.0：藏身点 */
HideSpot hideSpots[HIDESPOT_MAX];
int hideSpotCount = 0;
int hideRepairCount = 0;   /* v2.4.17：本局已修复次数 */
int hiddenIn = -1;          /* >=0 表示藏在第几个点里 */
int hideSpotNear = -1;      /* 当前离得最近、可以钻的点 */
int lockedThisFloor = 0;   /* v3.0：本层已上锁的箱子数 */
int containerCount = 0;
Breaker breaker;
int fusesHeld = 0;        /* 手上已有保险丝 */
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
int EndlessUnlocked(void) {
    return (bestLevel >= 10) || gameCleared || gameWon;
}
float volumeF = 0.8f;     /* 0..1，音效音量，0 = 静音 */
float musicVolumeF = 0.8f;/* 0..1，音乐音量，0 = 静音 */
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
int   catchBreakUsed = 0;
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
float shakeTime = 0.0f, shakeDur = 1.0f, shakeMag = 0.0f;
float hitFlash = 0.0f;      /* 受伤红闪 0..1 */
float muzzleFlash = 0.0f;   /* 开火闪光 0..1 */

void AddShake(float seconds, float mag) {
    if (seconds > shakeTime) { shakeTime = seconds; shakeDur = seconds; shakeMag = mag; }
    else if (mag > shakeMag) shakeMag = mag;
}
void AddHitFlash(float amount) {
    hitFlash += amount;
    if (hitFlash > 1.0f) hitFlash = 1.0f;
}
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
float secondWindTimer = 0.0f;   /* v3.0 二次呼吸：体力归零后的宽限秒数 */
float vanishTimer = 0.0f;       /* v3.0 消失：脱离视线累计秒数 */
int   playDeadUsed = 0;         /* v3.0 假死：本局用过了吗 */
/* v3.3：tutorialOff 已删除 ——
   旧的四行开局引导整块移除了（改用 Tutorial 模式的分步引导），
   这个变量既不写也不读，留着只是噪音。 */
float hideBlockTimer = 0.0f;    /* v3.0：刚解锁/刚翻完箱时短暂禁止进藏身点 */
int   ultUsed = 0;              /* v3.0：本局大招用过了吗 */
float ultTimer = 0.0f;          /* v3.0：大招持续秒数 */
int   ultKind = -1;             /* v3.0：正在生效的大招 id */
int   vanished = 0;             /* v3.0 消失：1 = 已隐身 */
float vanishHold = 0.0f;        /* v3.0 消失：本次隐身剩余秒数 */
float decoySelfTimer = 0.0f;  /* 投石后的自身噪声加成计时 */
/* 2026-11-05：**G 键投石的冷却计时器**。
   原来没有这个东西 —— 技能描述写着 8 秒 cd，代码里却是无限制。
   文件级 static：只有下面那个按键分支会写它。 */
static float s_decoyKeyCd = 0.0f;
int hubPage = 1;   /* v6：Hub **只有一页**（技能树）。变量保留兼容旧判断，但恒为 1。 */
int levelEvent = -1;         /* 本层随机事件，-1 = 无 */
int eventBonus = 0;          /* 本局事件累计加分 */

/* v6：事件名与分数全部搬到 events.c 的表里 */

/* 通过本层时给的分 */
int EventScore(int ev) {
    extern int EventScoreOf(int);
    return EventScoreOf(ev);   /* v6：查 events.c 的表 */
}

int playerInSafe = 0;
int autoPaused = 0;
int lastHitDamage = 0;
float lastHitTimer = 0.0f;
int lastHitBackstab = 0;
int pixelBudget = PIXEL_BUDGET;   /* v1.9：运行时可变 */
int safehouseHoverRow = -1;       /* v3.0：鼠标悬停在安全屋第几行（-1=没有） */
int safehouseHoverTab = -1;       /* v3.0：悬停的页签 */
int dynamicRes = 1;               /* v1.9：自动画质开关 */
int forceFrame = 0;          /* v1.8：窗口变化后立刻出一帧，减少还原/缩放延迟 */          /* v1.8：因失焦自动暂停（回前台自动恢复） */         /* v1.8：玩家是否在安全屋里（怪物丢目标、不抓人） */
ScoreBreakdown lastScore;     /* 上一次结算的分项 */
int skillPickOptions[INRUN_CHOICES] = {-1, -1, -1};
int skillPickSel = 0;
int pickMode = 0;
static int pendingSkillDraft=0;      /* v2.0：0 = 局内技能，1 = 遗物 */
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
int lastResortUsed = 0;   /* v2.2 LAST RESORT：本局是否已用掉免死 */    /* v2.0 SHORTCUT：本局是否已用过跳层 */
int dailyMode = 0;
int dailySeed = 0;
int dailyClearedToday = 0;   /* v2.2：今天是否已通 */
int campaignMode = 0;
int campaignStage = 0;
/* StartGame() 统一清理模式标志；菜单入口先把要启动的模式排队，
   这样清理不会把 Endless / Daily / Campaign / Training 自己也清掉。 */
enum { START_NORMAL = 0, START_ENDLESS, START_DAILY, START_CAMPAIGN, START_TRAINING };
static int pendingStartMode = START_NORMAL;
static int pendingCampaignStage = 0;
static void ApplyCampaignRules(int stage);

/* v2.0 战役：5 个手工场景，固定种子 + 各自的规则 */
/* v6：关名搬到 campaign.c 的 kCampaign[] */
const char* campaignName(int stage) {
    extern const CampaignDef kCampaign[];
    if (stage < 0 || stage >= CAMPAIGN_STAGE_COUNT) return "?";
    return (g_lang == LANG_ZH) ? kCampaign[stage].nameZh : kCampaign[stage].nameEn;
}
/* v2.0：燃烧瓶留下的火区（同时最多 3 处） */
static float fireX[3], fireY[3], fireT[3];
static float fireTick[3];
float leechHintTimer = 0.0f;
int fusesNeeded = 2;      /* 本关需要几根 */
int breakerOpen = 0;      /* 配电箱是否已通電 */
int hoverKind = 0;        /* 0=无 1=容器 2=配电箱 3=家具（货架/木箱堆/控制台） */
int hoverIndex = -1;
float holdTimer = 0.0f;
int holdingNow = 0;
/* Gamma：0.7 ~ 1.6，1.0 为线性 */
float gammaValue = 1.0f;
float gammaBuilt = -1.0f;
int keys[256] = {0};

/* 「本帧按下的键」—— QTE 需要边沿触发，不能用 keys[]（那是持续状态）。
   窗口过程收到 WM_KEYDOWN 时把键记在这里，逻辑层每帧消费一次后清空。
   这样「按早了」（QTE 还没开始时按）不会被误判成「按对了」。 */
int keyPressed[256] = {0};

/* 清除本帧的按键边沿。
   注意：**不能**在 Update() 开头清 —— 主循环一帧可能跑多次 Update
   （120Hz 逻辑 vs 60Hz 渲染，一帧通常跑 2 次），
   那样第一个 Update 就把按键清掉了，QTE 会吞掉一半的输入。
   所以边沿由主循环在一批逻辑更新**跑完之后**统一清。 */
void ClearKeyEdges(void) {
    for (int i = 0; i < 256; i++) keyPressed[i] = 0;
}

/* v6：g_achievements 定义在 achievements.c */

/* 每个菜单状态的滚动偏移，持久化避免 GetMenuGeo 重新计算 */
int menuScrollOffsets[16] = {0};   /* 下标 = GameState，留足余量 */  /* 下标 = GameState，包含 STATE_ACHIEVEMENTS=7 */

int g_bindings[ACT_COUNT] = BINDINGS_DEFAULT;

/* 设置菜单用的按键选项：名称与虚拟键码一一对应 */
const char* optKeys[] = {
    "None", "1","2","3","4","5","6","7","8","9","0",
    "A","B","C","D","E","F","G","H","I","J","K","L","M",
    "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
    "Space","LShift","RShift","LCtrl","RCtrl","LAlt","RAlt",
    "Up","Down","Left","Right","Enter","Esc","Tab",
    "LMB","RMB","MMB"
};
/* 对应的虚拟键码，与 optKeys 下标一一对应 */
const int optKeyVKs[] = {
    0, '1','2','3','4','5','6','7','8','9','0',
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    VK_SPACE, VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU,
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE, VK_TAB,
    VK_LBUTTON, VK_RBUTTON, VK_MBUTTON
};

/* 改绑提示框用的动作名（下标 = ActionId） */
const char* actionNames[ACT_COUNT] = {
    "MOVE FORWARD", "MOVE BACKWARD", "MOVE LEFT", "MOVE RIGHT",
    "SPRINT", "INTERACT", "CRANK BATTERY", "FLASHLIGHT", "TOGGLE MAP",
    "THROW", "CANCEL CHARGE", "SELECT FLARE", "SELECT BOTTLE", "SELECT SLOT 3",
    "PAUSE", "MENU UP", "MENU DOWN", "MENU LEFT", "MENU RIGHT",
    "MENU CONFIRM", "MENU CANCEL", "DODGE (QTE)",
    /* v2.7：这两项以前漏了，数组声明长度是 ACT_COUNT，缺的自动补 NULL，
       而 DrawRebindOverlay 会拿 actionNames[...] 去 strlen → 崩。
       今天它们还没进设置表所以摸不到，谁加一行改绑就立刻炸。 */
    "SWING KNIFE", "THROW DECOY"
};
/* 编译期守卫：表项数量必须与动作数一致，漏一个改绑框就会画到 NULL */
/* 注意：sizeof(array)/sizeof(elem) 拿到的是**声明长度**（缺省元素补 0），
   不是"写了几个初值"，所以下面这条守卫恒成立、拦不住漏项。
   真正的检查放在 SkillsSelfTest/启动自检里逐个判 NULL。 */
typedef char actionNamesSizeCheck[(ACT_COUNT == (int)(sizeof(actionNames) / sizeof(actionNames[0]))) ? 1 : -1];

/* 统一按键查询：键盘走 keys[]/keyPressed[]，鼠标走 GetAsyncKeyState */
static inline int IsMouseButton(int vk) {
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON;
}

static inline int IsActionDown(ActionId act) {
    int vk = g_bindings[act];
    if (vk == 0) return 0;
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    return keys[vk];
}
/* v8: 给外部模块用的公开包装（原来 IsActionDown 是 static） */
int ActionIsDown(int act) { return IsActionDown((ActionId)act); }


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
    int c[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys };
    if (throwSelected < 0 || throwSelected >= ITEM_SLOT_COUNT) return 0;
    return c[throwSelected] > 0;
}

/* v2.6：键盘只有 3 个选槽键，但槽位有 5 个（砖/酸/烟都有）。
   原来 1/2/3 是写死的"第 0/1/2 号槽"，所以捡到酸之后键盘根本选不中它，
   只能靠滚轮 —— 玩家看到 HUD 上明明写着 ACID x2 却用不了。
   现在改成"第 N 个**有货的**槽"：HUD 只列有货的槽，第 3 行按 3 就对了。 */
static void SelectVisibleSlot(int n) {
    int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys };
    int k = 0;
    for (int i = 0; i < ITEM_SLOT_COUNT; i++) {
        if (counts[i] <= 0) continue;
        if (k == n) {
            if (throwSelected != i) { throwSelected = i; uiDirty = 1; }
            return;
        }
        k++;
    }
}

void EnsureThrowSelectedValid(void) {
    /* v1.18：这里原来写死 2 个槽位 —— 滚到砖头/酸瓶时
       counts[throwSelected] 越界读到垃圾值，随即被拉回第 0/1 槽，
       表现就是"选不中砖头和酸瓶"。 */
    int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys };
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
void SpawnSmoke(float x, float y) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (smokes[i].active) continue;
        smokes[i].x = x; smokes[i].y = y;
        smokes[i].life = SMOKE_LIFE;
        smokes[i].active = 1;
        return;
    }
}


/* ================= v3.3 玩家诱饵「玩偶」 =================
   设计来源（调研结论）：硬核逃生游戏里的「假人」类道具只做两件事之一 ——
   要么当诱饵把怪引走，要么替你挨一下。两者同时做会太强，
   所以这里加了**时间和次数**两道闸门：
     · 每 3 秒才叫一次（不是持续噪音，怪有反应窗口）
     · 只挡 2 次抓（不是无敌）
     · 放下后要按 C 才激活（给你决定时机的余地）
   ======================================================= */
Decoy decoysArr[MAX_DECOY];

void PlaceDecoy(float x, float y) {
    int slot = -1;
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        /* 顶掉最老的那个 */
        float oldest = 1e9f; 
        for (int i = 0; i < MAX_DECOY; i++)
            if (decoysArr[i].life < oldest) { oldest = decoysArr[i].life; slot = i; }
    }
    if (slot < 0) return;
    decoysArr[slot].x = x;
    decoysArr[slot].y = y;
    decoysArr[slot].life = 10.0f;   /* v2.4.14：只活 10 秒 */
    decoysArr[slot].shoutT = DECOY_SHOUT_EVERY;
    decoysArr[slot].hp = DECOY_HP;
    decoysArr[slot].active = 1;
    decoysArr[slot].armed = 0;
}

void ArmDecoy(void) {
    /* 激活最近的一个未激活玩偶 */
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active || decoysArr[i].armed) continue;
        float d = Dist2(decoysArr[i].x, decoysArr[i].y, posX, posY);
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) {
        decoysArr[best].armed = 1;
        decoysArr[best].shoutT = 0.0f;   /* 激活立刻叫第一声 */
    }
}

/* 怪物快要抓到玩家时调这个：有激活的玩偶在附近就替玩家挨一下 */
int DecoyIntercept(void) {
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active || !decoysArr[i].armed) continue;
        if (decoysArr[i].hp <= 0) continue;
        /* 玩偶要在怪物附近（它替玩家吸引了怪） */
        if (Dist2(decoysArr[i].x, decoysArr[i].y, posX, posY) > 64.0f) continue;
        decoysArr[i].hp--;
        if (decoysArr[i].hp <= 0) {
            decoysArr[i].active = 0;
            pickupMsg = (g_lang == LANG_ZH) ? "玩偶散架了" : "DECOY BROKE";
        } else {
            pickupMsg = (g_lang == LANG_ZH) ? "玩偶替你挨了一下" : "DECOY TOOK THE HIT";
        }
        pickupMsgTimer = 1.6f;
        PlaySfx(SFX_GLASS, 0.8f);
        return 1;
    }
    return 0;
}

/* v3.3 胶水：落地生成一滩粘胶，半径 GLUE_RADIUS，持续 GLUE_LIFE。
   占满 MAX_GLUE 时**顶掉剩余时间最短的那个**（不是拒绝）——
   否则玩家在同一个门口连扔两瓶，第二瓶会静默消失。 */
void SpawnGlue(float x, float y) {
    int best = -1;
    float bestLife = 1e9f;
    for (int i = 0; i < MAX_GLUE; i++) {
        if (!glues[i].active) { best = i; bestLife = -1.0f; break; }
        if (glues[i].life < bestLife) { bestLife = glues[i].life; best = i; }
    }
    if (best < 0) return;
    glues[best].x = x;
    glues[best].y = y;
    glues[best].life = GLUE_LIFE;
    glues[best].active = 1;
    /* 2026-11-05：宝箱怪不是普通怪，胶水管线扫不到它 —— 单独通知一次。
       命中后它硬直 2 秒，玩家可以趁机搜空壳（铁律二十二 登记点）。 */
    MimicOnGlue(x, y);
}

/* 任意一点是否在某朵烟里（守门人/猎手/清道夫共用）。
   2026-11-03：原来只有 PlayerInSmoke()，怪物那边各自写死距离判断，
   结果烟雾只对第 1 只听声鬼 + 追光鬼生效，守门人和第 2/3 只完全免疫。 */
int PointInSmoke(float x, float y) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (!smokes[i].active) continue;
        float dx = x - smokes[i].x, dy = y - smokes[i].y;
        if (dx * dx + dy * dy < SMOKE_RADIUS * SMOKE_RADIUS) return 1;
    }
    return 0;
}

/* 线段是否穿过任意一朵烟（用于「视线被烟挡住」）。
   沿线段每 0.35 格采样一次，够密也不会太贵。 */
int SegmentInSmoke(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return PointInSmoke(x0, y0);
    dx /= d; dy /= d;
    for (float t = 0.0f; t <= d; t += 0.35f)
        if (PointInSmoke(x0 + dx * t, y0 + dy * t)) return 1;
    return PointInSmoke(x1, y1);
}

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

int MonsterCollect(Monster** out, int maxOut, int includeLightBeast) {
    int n = 0;
    for (int i = 0; i < MON_HUNTER_MAX && n < maxOut; i++)
        if (monHunters[i].active) out[n++] = &monHunters[i];
    if (includeLightBeast && lightBeast.active && n < maxOut)
        out[n++] = &lightBeast;
    return n;
}

/* 对所有猎手执行一次「瘫痪」——TIME STOP / STATIC FIELD 共用 */
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
int PlayerInSmoke(void) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (!smokes[i].active) continue;
        float dx = posX - smokes[i].x, dy = posY - smokes[i].y;
        if (dx * dx + dy * dy < SMOKE_RADIUS * SMOKE_RADIUS) return 1;
    }
    return 0;
}

void UpdateSmokes(float dt) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (!smokes[i].active) continue;
        smokes[i].life -= dt;
        if (smokes[i].life <= 0.0f) smokes[i].active = 0;
    }
    /* v3.4：记录本局有没有开过手电（表现加成用） */
    if (flashlightOn) runLightUse = 1;

    /* v6：出口弹窗的淡入计时 */
    if (exitChoiceOpen) exitChoiceT += dt;

    /* v3.4：商店每行的补货倒计时 */
    {
        extern void ShopTickCooldown(float dt);
        ShopTickCooldown(dt);
    }

    MoneyTick(dt);
    { extern void UpdateWarden(float); UpdateWarden(dt); }   /* v8 B1: 守门人 */   /* v8: 钱袋逻辑已搬到 src/money.c */

    /* v3.3：诱饵心跳 —— 每 DECOY_SHOUT_EVERY 秒叫一次，把怪引过去 */
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active) continue;
        decoysArr[i].life -= dt;
        if (decoysArr[i].life <= 0.0f) { decoysArr[i].active = 0; continue; }
        if (!decoysArr[i].armed) continue;
        decoysArr[i].shoutT -= dt;
        if (decoysArr[i].shoutT <= 0.0f) {
            decoysArr[i].shoutT = DECOY_SHOUT_EVERY;
            /* ===== v7 **玩偶重写** =====
               原来走 MakeNoise(半径20)，那个管道会直接改 monster.state，
               和玩家自己的脚步噪音抢同一个字段 —— 玩偶刚设好就被顶掉，
               所以「玩偶吸引不了听声怪」。

               现在直接往可疑点表写：strength 75（比冲刺的 45 高），
               decay 3/s、寿命 8 秒 —— 三个叫周期都能覆盖到。
               玩家自己的脚步只有 36 强度，抢不过它。 */
            /* ============================================================
             * 2026-11-06（v2.4-beta B05）：**诱饵走事件中心，并且有位置**
             * ------------------------------------------------------------
             * 原来这里直接 SuspicionAdd + 居中的 PlaySfx。
             * 结果：**你听不出自己的玩偶在左边还是右边** ——
             * 而"把怪引到那个方向"正是玩偶的全部意义。
             *
             * 现在：
             *   · 线索走 SoundEmit(SK_VOICE) —— 类别显式，参数在 kKindDefs 里
             *   · 声音走 AudioPlayWorld —— 从玩偶所在方向传过来
             * 参数保持原值（强度 75 / decay 3 / ttl 8），行为不变。
             * ============================================================ */
            {
                SoundEvent ev;
                memset(&ev, 0, sizeof(ev));
                ev.kind = SK_VOICE;
                ev.x = decoysArr[i].x;
                ev.y = decoysArr[i].y;
                ev.strength = 75.0f;
                ev.decay = 3.0f;
                ev.ttl = 8.0f;
                ev.eventId = 0u;              /* 每 1.5 秒一次是**独立**的叫声 */
                ev.sourceEntityId = -1;
                SoundEmit(&ev);
            }
            /* 保留一小圈物理噪音，让追光怪/环境音也有反应 */
            MakeNoise(decoysArr[i].x, decoysArr[i].y, 6.0f);
            { extern void AudioPlayWorld(int, float, float, float, float, int);
              AudioPlayWorld(SFX_DECOY, decoysArr[i].x, decoysArr[i].y, 0.70f, 22.0f, 3); }
        }
    }

    /* v3.3：粘胶区域 / 回声扫描的生命周期 */
    for (int i = 0; i < MAX_GLUE; i++) {
        if (!glues[i].active) continue;
        glues[i].life -= dt;
        if (glues[i].life <= 0.0f) { glues[i].active = 0; glues[i].life = 0.0f; }
    }
}

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

void PollInput(void) {
    static int prevDown[256];

    /* v1.8：窗口最小化时收不到任何键盘消息（F11 也就救不回来）。
       这里用 GetAsyncKeyState 直接读物理按键：最小化状态下按 F11 或 Enter
       就能把自己还原 —— 不注册全局热键，也不会在正常游戏时抢键。 */
    if (IsIconic(g_hwnd)) {
        static int prevF11 = 0, prevEnter = 0;
        int f11 = (GetAsyncKeyState(VK_F11) & 0x8000) ? 1 : 0;
        int ent = (GetAsyncKeyState(VK_RETURN) & 0x8000) ? 1 : 0;
        if ((f11 && !prevF11) || (ent && !prevEnter)) {
            ShowWindow(g_hwnd, SW_RESTORE);
            SetForegroundWindow(g_hwnd);
            SetFocus(g_hwnd);
            resetInputState = 1;
            firstMouseMove = 1;
            uiDirty = 1;
        }
        prevF11 = f11;
        prevEnter = ent;
        return;
    }

    if (GetForegroundWindow() != g_hwnd) {
        memset(keys, 0, sizeof(keys));
        memset(prevDown, 0, sizeof(prevDown));
        return;
    }
    for (int a = 0; a < ACT_COUNT; a++) {
        int vk = g_bindings[a];
        if (vk <= 0 || vk > 255) continue;
        if (vk == VK_LBUTTON || vk == VK_RBUTTON) continue;   /* 鼠标键由 Windows 消息驱动 */
        int down = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
        if (down && !prevDown[vk]) {
            keyPressed[vk] = 1;              /* 0→1 的边沿，等价于 WM_KEYDOWN 那次 */
            if (currentState == STATE_PLAYING) keyPressed[vk] = 1;
        }
        keys[vk] = down;
        prevDown[vk] = down;
    }

    /* v2.9：**不在游戏中就清空所有绑定键**。
       这是「面板里按 A/D 会漂移」的终极修法：
       之前只清 W/A/S/D 四个字面量，一旦按键绑到别的动作、
       或大小写不同（'a' vs 'A'）就漏掉，所以一直修不干净。
       现在直接遍历 g_bindings[]，凡是绑过的键全清。
       菜单导航走的是 WM_KEYDOWN 里的 wParam 比较，不依赖 keys[]。 */
    if (currentState != STATE_PLAYING) {
        for (int a = 0; a < ACT_COUNT; a++) {
            int vk = g_bindings[a];
            if (vk > 0 && vk < 256) keys[vk] = 0;
        }
        keys[VK_SHIFT] = 0;
        keys[VK_SPACE] = 0;
        keys[VK_LBUTTON] = keys[VK_RBUTTON] = 0;
    }

    /* v2.0：手柄（没有手柄时什么也不做） */
    GamepadPoll();
    /* 失焦自动暂停：回到前台就自动继续，不再依赖 WM_ACTIVATE 的时序 */
    if (autoPaused && currentState == STATE_PAUSED) {
        currentState = STATE_PLAYING;
        autoPaused = 0;
        resetInputState = 1;
        ForceCursor(0);
        firstMouseMove = 1;
        uiDirty = 1;
    }
    if (currentState != STATE_PAUSED) autoPaused = 0;
}

int IsActionPressed(ActionId act) {
    int vk = g_bindings[act];
    if (vk == 0) return 0;
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) {
        /* 鼠标边缘触发简化：这里只返回按下状态，Update 里用前一帧对比实现边缘 */
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    return keyPressed[vk];
}
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
static int mapHeld = 0;

/* 该设置项是否是键位绑定（value 指向 g_bindings 的某一项）。
   是的话返回对应 ActionId，否则返回 -1。
   之前这里是一串 21 项的地址比较链，三处各抄一份，漏一个就出 bug。 */
int BindActionOfItem(const SettingItem* it) {
    for (int a = 0; a < ACT_COUNT; a++)
        if (it->value == &g_bindings[a]) return a;
    return -1;
}

int IsKeyBindItem(const SettingItem* it) {
    return it->kind == SET_LIST && BindActionOfItem(it) >= 0;
}

/* VK 码 -> 显示名（改绑提示、QTE 提示共用）。
   在 optKeys 里找不到的（比如一些扩展键）返回 NULL，调用方自己兜底。 */
const char* VkName(int vk) {
    for (int k = 0; k < OPT_KEY_COUNT; k++)
        if (optKeyVKs[k] == vk) return optKeys[k];
    /* v1.8：功能键不在选项表里时给个可读名字 —— 返回 NULL 会让
       sprintf("%s") 打出 "(null)"（HELP 页的 SPRINT 就中过这个招）。 */
    switch (vk) {
    case VK_SHIFT:   return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU:    return "Alt";
    case VK_LBUTTON: return "LMB";
    case VK_RBUTTON: return "RMB";
    case VK_SPACE:   return "Space";
    case VK_RETURN:  return "Enter";
    case VK_ESCAPE:  return "Esc";
    case VK_TAB:     return "Tab";
    case 0:          return "None";
    default:         return "?";
    }
}

static const char* optRes[RES_COUNT] = {
    "800x600", "1024x768", "1280x720", "1366x768",
    "1600x900", "1920x1080", "2560x1440", "AUTO (FIT SCREEN)"
};
static const char* optFps[5] = {"30", "60", "120", "144", "Unlimited"};
static const char* optScale[4] = {"HIGH (100%)", "MEDIUM (75%)", "LOW (50%)", "MINIMAL (25%)"};
static const char* optFov[5] = {"66", "75", "85", "95", "105"};
static const char* optOnOff[2] = {"OFF", "ON"};
/* v6 第 ㊳ 轮：语言选项 —— 之前 g_lang 只能靠改存档改 */
static const char* optLang[2] = {"ENGLISH", "\xe4\xb8\xad\xe6\x96\x87"};
/* HUD 详细度：游戏画面上放多少字。
   Minimal = 只留必须实时知道的数值；Full = 全部显示。 */
static const char* optHud[3] = {"Minimal", "Normal", "Full"};
/* QTE 提示详细度：Full = 带英文说明，Minimal = 只留大按键和倒计时条 */
static const char* optQte[2] = {"Minimal", "Full"};
static const char* optSlot[SAVE_SLOTS] = {"SLOT 1", "SLOT 2", "SLOT 3"};
SettingItem settings[] = {
    /* ========== 分类 1: DISPLAY ========== */
    {"Resolution",      SET_LIST,   optRes,      RES_COUNT, &resIndex,          NULL,      0,    0,    0, 1},
    {"Fullscreen",      SET_LIST,   optOnOff,    2, &isFullscreen,      NULL,      0,    0,    0, 1},
    {"Frame Limit",     SET_LIST,   optFps,      5, &fpsCapIndex,       NULL,      0,    0,    0, 1},
    {"Graphics Quality", SET_LIST,  optScale,    4, &renderScaleIndex,  NULL,      0,    0,    0, 1},
    {"Dynamic Res",     SET_LIST,   optOnOff,   2, &dynamicRes,        NULL,      0,    0,    0, 1},
    {"Gamma",           SET_SLIDER, NULL,        0, NULL,               &gammaValue, 0.7f, 1.6f, 0.05f, 1},
    {"Field of View",   SET_LIST,   optFov,      5, &fovIndex,          NULL,      0,    0,    0, 1},
    {"Save Slot",       SET_LIST,   optSlot, SAVE_SLOTS, &g_saveSlot,  NULL,      0,    0,    0, 6},

    /* ========== 分类: KEY BINDINGS / MOVEMENT ========== */
    {"Move Forward",    SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MOVE_FWD],      NULL, 0, 0, 0, 3},
    {"Move Backward",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MOVE_BACK],     NULL, 0, 0, 0, 3},
    {"Move Left",       SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MOVE_LEFT],     NULL, 0, 0, 0, 3},
    {"Move Right",      SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MOVE_RIGHT],    NULL, 0, 0, 0, 3},
    {"Sprint",          SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_SPRINT],        NULL, 0, 0, 0, 3},

    /* ========== 分类: KEY BINDINGS / ACTIONS ========== */
    /* 2026-11-06：相位信标（用户要求 Q 传送 / C 放置） */
    {"Phase: Place",    SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_PHASE_PLACE],   NULL, 0, 0, 0, 4},
    {"Phase: Teleport", SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_PHASE_TELE],    NULL, 0, 0, 0, 4},
    {"Interact",        SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_INTERACT],      NULL, 0, 0, 0, 4},
    {"Crank Battery",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_CRANK],         NULL, 0, 0, 0, 4},
    {"Flashlight",      SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_FLASHLIGHT],    NULL, 0, 0, 0, 4},
    {"Toggle Map",      SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MAP],           NULL, 0, 0, 0, 4},
    {"Throw",           SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_THROW],         NULL, 0, 0, 0, 4},
    {"ATTACK (hold)",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_CANCEL_CHARGE], NULL, 0, 0, 0, 4},
    /* v2.2：这一行其实就是攻击键（原来叫 Cancel Charge，玩家根本不知道） */
    {"Select Flare",    SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_SELECT_1],      NULL, 0, 0, 0, 4},
    {"Select Bottle",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_SELECT_2],      NULL, 0, 0, 0, 4},
    {"Select Slot 3",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_SELECT_3],      NULL, 0, 0, 0, 4},

    /* ========== 分类: KEY BINDINGS / MENU KEYS ========== */
    {"Dodge (QTE)",     SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_DODGE],         NULL, 0, 0, 0, 5},
    {"Pause",           SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_PAUSE],         NULL, 0, 0, 0, 5},
    {"Menu Up",         SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_UP],       NULL, 0, 0, 0, 5},
    {"Menu Down",       SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_DOWN],     NULL, 0, 0, 0, 5},
    {"Menu Left",       SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_LEFT],     NULL, 0, 0, 0, 5},
    {"Menu Right",      SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_RIGHT],    NULL, 0, 0, 0, 5},
    {"Menu Confirm",    SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_CONFIRM],  NULL, 0, 0, 0, 5},
    {"Menu Cancel",     SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_CANCEL],   NULL, 0, 0, 0, 5},

    /* ========== 分类: GAMEPLAY ========== */
    {"Mouse Sens.",     SET_SLIDER, NULL,        0, NULL, &mouseSens, 0.2f, 3.0f, 0.1f, 6},
    {"SFX Volume",      SET_SLIDER, NULL,        0, NULL, &volumeF,  0.0f, 1.0f, 0.05f, 6},
    {"Music Volume",    SET_SLIDER, NULL,        0, NULL, &musicVolumeF, 0.0f, 1.0f, 0.05f, 6},
    /* v6 第 ㊳ 轮：**语言切换终于有入口了**
       审计发现 LangToggle() 全工程 0 调用点 ——
       游戏支持中英双语，但玩家在设置里找不到任何开关。
       这里直接绑 g_lang（0=EN，1=ZH），列表循环就能切。 */
    {"Language",        SET_LIST,   optLang,     2, &g_lang,             NULL,      0,    0,    0, 6},
    {"Sanity FX",       SET_LIST,   optOnOff,    2, &vignetteOn,        NULL,      0,    0,    0, 6},
    {"Show FPS",        SET_LIST,   optOnOff,    2, &showFps,           NULL,      0,    0,    0, 6},
    {"HUD Detail",      SET_LIST,   optHud,      3, &hudDetail,         NULL,      0,    0,    0, 6},
    {"QTE Hint",        SET_LIST,   optQte,      2, &qteHint,           NULL,      0,    0,    0, 6},
    /* Reset to Defaults：一次性动作行。value 指向 resetDefaultsFlag（没有真选项），
       ActivateSettingRow 里单独拦截执行。 */
    {"Reset to Defaults", SET_LIST, optOnOff,    2, &resetDefaultsFlag, NULL,      0,    0,    0, 6},
};
int settingsPage = 0;

/* 每页标题（下标 = settingsPage） */
const char* PageTitle(int page) {
    switch (page) {
        case SETPAGE_DISPLAY:  return "DISPLAY";
        case SETPAGE_BINDDIR:  return "KEY BINDINGS";
        case SETPAGE_MOVE:     return "MOVEMENT";
        case SETPAGE_ACT:      return "ACTIONS";
        case SETPAGE_MENUKEYS: return "MENU KEYS";
        case SETPAGE_GAME:     return "GAMEPLAY";
        default:               return "SETTINGS";
    }
}

/* 根目录 / Key Bindings 子目录的行文字，以及每行对应跳转的页 */
const char* rootCategoryNames[3] = {"Display", "Key Bindings", "Gameplay"};
static const int   rootCategoryPages[3] = {SETPAGE_DISPLAY, SETPAGE_BINDDIR, SETPAGE_GAME};
const char* bindCategoryNames[3] = {"Movement", "Actions", "Menu Keys"};
static const int   bindCategoryPages[3] = {SETPAGE_MOVE, SETPAGE_ACT, SETPAGE_MENUKEYS};

/* 当前页包含的设置项下标，返回个数。页 0/2 是目录，没有设置项。 */
int PageItems(int* out) {
    if (settingsPage == SETPAGE_ROOT || settingsPage == SETPAGE_BINDDIR) return 0;
    if (settingsPage < 1 || settingsPage > 6) return 0;
    int n = 0;
    for (int i = 0; i < SETTING_COUNT; i++)
        if (settings[i].group == settingsPage) out[n++] = i;
    return n;
}

int SettingsRowCount(void) {
    if (settingsPage == SETPAGE_ROOT || settingsPage == SETPAGE_BINDDIR) return 3 + 1;  /* 三个分类 + Back */
    int tmp[SETTING_COUNT];
    return PageItems(tmp) + 1;              /* 若干项 + Back */
}

/* 玩家碰撞半径：贴墙时留出半个身位，避免半个身子钻进墙里 */
#define PLAYER_RADIUS 0.24f

/* ==================================================================
 * 2026-11-06（用户要求）：**相位信标**
 * ------------------------------------------------------------------
 * 用户原话：「新增相位传送装置，按 Q 进行传送，按 C 进行放置，
 *           一个人身上上限只能有一个，使用次数为 2，价格极其昂贵为百分比价格。」
 *
 * 规则：
 *   · 身上最多 1 个
 *   · 总共 2 次使用（放 1 次 + 传 1 次算一次完整循环？）
 *     —— 这里按"**传送次数**"算 2 次：放置不消耗次数，
 *        每次传送消耗 1 次，用完装置消失。
 *   · 数量上限 1：场上同时只能存在 1 个信标，再按 C 只会重放（不叠加）。
 *
 * 为什么"放置不消耗次数"：
 *   如果放置也消耗，玩家在一个地方放错就得废掉整个装置，
 *   而这是最贵的一件道具 —— 惩罚过重。
 * ================================================================ */
int   phaseOwned   = 0;   /* 是否持有装置 */
int   phaseCharges = 0;   /* 剩余传送次数（上限 2） */
int   phasePlaced  = 0;   /* 场上是否已有信标 */
float phaseX = 0.0f, phaseY = 0.0f;

#define PHASE_MAX_CHARGES 2

void PhaseGive(void) {
    phaseOwned = 1;
    phaseCharges = PHASE_MAX_CHARGES;
    phasePlaced = 0;
}

void PhaseReset(void) {
    phaseOwned = 0;
    phaseCharges = 0;
    phasePlaced = 0;
    phaseX = phaseY = 0.0f;
}

int PhaseTryPlace(void) {
    if (!phaseOwned || phaseCharges <= 0) return 0;
    /* 场上只允许一个：重复按 C 就挪到脚下，不叠第二个 */
    phaseX = posX;
    phaseY = posY;
    phasePlaced = 1;
    return 1;
}

int PhaseTryTeleport(void) {
    if (!phaseOwned || phaseCharges <= 0 || !phasePlaced) return 0;
    /* 目标格如果已经不是空地（比如期间塌过），拒绝而不是把玩家塞进墙里 */
    if (Blocked(phaseX, phaseY)) return 0;
    posX = phaseX;
    posY = phaseY;
    phasePlaced = 0;
    phaseCharges--;
    if (phaseCharges <= 0) phaseOwned = 0;   /* 用完即消失 */
    return 1;
}

int Blocked(float x, float y) {
    int x0 = (int)floorf(x - PLAYER_RADIUS), x1 = (int)floorf(x + PLAYER_RADIUS);
    int y0 = (int)floorf(y - PLAYER_RADIUS), y1 = (int)floorf(y + PLAYER_RADIUS);
    if (x0 < 0 || y0 < 0 || x1 >= MAP_WIDTH || y1 >= MAP_HEIGHT) return 1;
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++) {
            if (worldMap[cy][cx]) return 1;
            /* ==================================================================
             * 2026-11-06（v2.4-rc P2）：**房间家具是实体**
             * ------------------------------------------------------------------
             * 在此之前全工程没有任何物件参与碰撞 —— 渲染层自己的注释就写着
             * 「箱子/配电箱没有碰撞，玩家可以站进模型里」。
             * 那正是"没有实体模型"的根源：不是画得不像，是它本来就不是实体。
             *
             * 这里让家具进入**唯一**的碰撞入口，玩家和所有 AI 都自动生效。
             * ================================================================== */
            /* ==================================================================
             * 2026-11-06（用户要求）：**取消伪实体阻挡**
             * ------------------------------------------------------------------
             * 用户实测：「伪实体堵路，堵住道路，导致出口进不去。
             *           我觉得还是不要用伪实体了。」
             *
             * 家具（货架/木箱堆/控制台）是**摆件**，不是墙。
             * 让它们参与碰撞会带来三个后果：
             *   ① 1 格宽的过道被从中间截断（房间生成时已经尽力避让，
             *      但模板尺寸固定，总有避不开的时候）
             *   ② AI 的 A* 把家具当墙绕，寻路变得又贵又难看
             *   ③ 最严重的：撤离点被堵死就等于这一层报废
             *
             * 所以它们全部改成**可穿透**。视觉上仍然有体量，
             * 但玩家和怪都能走过去 —— 和箱子/配电箱原本的行为一致。
             * ================================================================ */
            (void)cx; (void)cy;
        }
    return 0;
}

/* 弹丸的墙判定：只用**点**，不套玩家的碰撞半径。
   Blocked() 带 0.24 的半径，那是给玩家用的；弹丸如果也用它，
   出生点附近稍微贴墙就会立刻判定为「撞墙」—— 瓶子一扔就碎。
   这里改成取所在格子的中心来判断，边界行为稳定。 */
int ShotBlocked(float x, float y) {
    int cx = (int)floorf(x), cy = (int)floorf(y);
    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return 1;
    return worldMap[cy][cx] ? 1 : 0;
}
int SpotClear(float x, float y, float minD, int selfKind, int selfIdx) {
    float m2 = minD * minD;
    for (int i = 0; i < containerCount; i++) {
        if (selfKind == 1 && i == selfIdx) continue;
        if (containers[i].active && Dist2(x, y, containers[i].x, containers[i].y) < m2) return 0;
    }
    if (selfKind != 2 && breaker.active && Dist2(x, y, breaker.x, breaker.y) < m2) return 0;
    if (selfKind != 3 && safeRoom.active && Dist2(x, y, safeRoom.x, safeRoom.y) < m2) return 0;
    /* v3.3 **修复藏身点被箱子压住** ——
       原来这里只查容器/配电箱/安全屋，**完全没查藏身点**。
       而生成顺序是「先藏身点、后容器」，所以后放的箱子可以直接怼在垃圾桶上
       （用户截图：垃圾桶和木箱叠在同一格，只看见 HIDE 提示却找不到桶）。
       注意：藏身点自己是最后才写进 hideSpots[] 的，
       所以调用 SpotClear 时不会把自己也判进去。 */
    for (int i = 0; i < hideSpotCount; i++) {
        if (!hideSpots[i].active) continue;
        if (Dist2(x, y, hideSpots[i].x, hideSpots[i].y) < m2) return 0;
    }
    return 1;
}

/* 词条的两个纯函数，单独拆出来是为了能离线自检直接调用：
   RollLevelMod 决定「这一层是什么词条」，ContainerTargetCount 决定
   「这个词条下要放几个容器」——都是不依赖世界状态的纯计算。 */

/* roll100: 0~99 的掷点（决定有没有抽到词条）；pick: 0~MOD_COUNT-1 的词条编号 */
static void RollLevelMod(int roll100, int pick) {
    levelMod = -1;
    /* v2.0 修复：4 个乘子必须**全部**重置。
       原来只重置 sanity/speed，于是「涨水」事件抽到一次之后，
       之后每一层、甚至之后每一次 New Run 手电耗电都永久 ×2。 */
    modSanityMul = 1.0f;
    modSpeedMul = 1.0f;
    modBatteryMul = 1.0f;
    modShardMul = 1.0f;
    modNoiseMul = 1.0f;
    if (currentLevel >= 2 && roll100 < 60) {
        levelMod = pick % MOD_COUNT;
        if (levelMod == 1) modSpeedMul = 1.25f;       /* HUNTED */
        else if (levelMod == 3) modSanityMul = 1.35f; /* LEAKY */
        else if (levelMod == 5) modSanityMul = 0.80f; /* CALM */
    }
}

/* 某个词条下，本层要放多少个容器。
   CRAMPED -2，RICH +2，其余不变。结果恒在 [2, MAX_CONTAINERS] 内。 */
static int ContainerTargetCount(int mod, int base) {
    int n = base;
    if (mod == 2) n -= 2;       /* CRAMPED */
    else if (mod == 4) n += 2;  /* RICH */
    if (n > MAX_CONTAINERS) n = MAX_CONTAINERS;
    if (n < 2) n = 2;
    return n;
}

/* 测试接缝：离线自检需要能指定「这一层用哪个词条」，
   否则 GenerateLevel 内部会自己重掷，测试没法比较两种词条的差异。
     -2 = 强制无词条
     -1 = 正常按随机流程掷词条
     0..5 = 强制使用该词条
   注意：无论走哪条分支，都必须消耗同样次数的 rand()，
   否则后续的地图/容器随机序列会错位，测试就没法做对照了。 */
int g_forceMod = -1;

/* v3.1 训练场改成新手教程：一条长廊 + 一个可翻的箱子 + 一个不会动的靶子。
   不再摆安全屋/配电箱/出口 —— 教程只教「怎么操作」，不要求真的逃出去。 */
static void BuildTrainingArena(void) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) {
            worldMap[y][x] = 1;
            explored[y][x] = 1;          /* 长廊全图可见，方便看提示 */
        }

    /* 一条 3 格高、从 x=2 到 x=21 的直走廊，y 中心 12（作者原版，已恢复） */
    int y0 = MAP_HEIGHT / 2 - 1, y1 = MAP_HEIGHT / 2 + 1;
    for (int y = y0; y <= y1; y++)
        for (int x = 2; x <= MAP_WIDTH - 3; x++)
            worldMap[y][x] = 0;

    posX = 3.5f;
    posY = (float)(MAP_HEIGHT / 2) + 0.5f;
    dirX = 1.0f; dirY = 0.0f;

    /* 听声怪：标本模式冻结在原地看着，实战模式给正常参数让它真的追声 */
    monster.x = 18.5f;
    monster.y = (float)(MAP_HEIGHT / 2) + 0.5f;
    monster.active = 1;
    monster.state = 2;
    monster.stun = 0.0f;
    monster.speed = 0.0f;          /* 标本：完全不动 */

    monster.x = 18.5f;
    monster.y = (float)(MAP_HEIGHT / 2) + 0.5f;
    monster.active = 1;
    monster.state = 2;
    monster.stun = 0.0f;
    monster.speed = 0.0f;          /* 标靶：不动 */
    { monster2.active = 0; monster3.active = 0; }   /* 新局清掉第 2、3 只听声鬼 */

    /* 2026-11-06：追光怪现在是两只，复位必须两只都清 */
    lightBeasts[0].active = 0;
    lightBeasts[1].active = 0;
    parasite.active = 0;
    for (int i = 0; i < LEECH_MAX; i++) leeches[i].active = 0;

    /* 教程里没有藏身点；必须清掉上一局留下的 active 标记，
       否则走廊里会凭空冒出「按 E 躲藏」提示。 */
    hideSpotCount = 0;
    hideRepairCount = 0;
    hiddenIn = -1;
    hideSpotNear = -1;
    for (int i = 0; i < HIDESPOT_MAX; i++) hideSpots[i].active = 0;

    /* v3.2：四个上锁的箱子，四种锁型各一个。
       最近的一个（DIAL）里放本层唯一的一根保险丝；
       另外三个是补给/练习箱，可开可不开。
       这样教程既是「锁盘教学」，又是一个最小可通关关卡：
       撬开 DIAL 箱 → 拿 1 根保险丝 → 给配电箱通电 → 走到出口。 */
    containerCount = 4;
    memset(containers, 0, sizeof(containers));
    {
        static const float boxX[4] = { 6.5f, 9.0f, 11.5f, 14.0f };
        static const int   boxLock[4] = { LOCK_TYPE_DIAL, LOCK_TYPE_WIRE, LOCK_TYPE_EQ, LOCK_TYPE_TUNE };
        static const int   boxCode[4] = { 0x012, 0x003, 0x01B, 0x02D };
        static const int   boxContent[4] = { 1, 2, 3, 2 };   /* 1=保险丝，2=电池，3=医疗包 */
        for (int i = 0; i < 4; i++) {
            containers[i].x = boxX[i];
            containers[i].y = (float)(MAP_HEIGHT / 2) + 0.5f;
            containers[i].active = 1;
            containers[i].searched = 0;
            containers[i].locked = 1;
            containers[i].lockType = boxLock[i];
            containers[i].lockCode = boxCode[i];
            containers[i].content = boxContent[i];
        }
    }

    /* 最小可通关：1 根保险丝 → 配电箱 → 出口 */
    safeRoom.active = 0;
    breaker.x = 17.0f;
    breaker.y = (float)(MAP_HEIGHT / 2) + 0.5f;
    breaker.active = 1;
    breakerOpen = 0;
    fusesHeld = 0;
    fusesNeeded = 1;
    exitX = 21.5f;
    exitY = (float)(MAP_HEIGHT / 2) + 0.5f;

    /* 手电初始电量为 55%：这样「按住 R 摇电」真的有东西可充。 */
    battery = (float)BatteryMax() * 0.55f;

    /* ===== v3.0：训练场重做 =====
       用户要求：留 1 个（可搜容器）+ 2 个躲藏点 + 1 个解密装置。
       走廊是 x=2..21、y=11..13 三格高，全部铺在这条线上。 */

    /* ① 1 个可搜容器（带保险丝，方便测「捡→上电」链路） */
    containers[0].x = 5.5f;
    containers[0].y = (float)(MAP_HEIGHT / 2) + 0.5f;
    containers[0].active = 1;
    containers[0].searched = 0;
    containers[0].content = 1;          /* 1 = 保险丝 */
    containers[0].locked = 0;
    containers[0].lockType = 0;
    containers[0].lockCode = 0;
    containerCount = 1;

    /* ② 2 个躲藏点（衣柜 + 床底，正好覆盖两种高度） */
    hideSpotCount = 2;
    hideSpots[0].x = 9.5f;
    hideSpots[0].y = (float)(MAP_HEIGHT / 2) + 0.5f;
    hideSpots[0].kind = 0;              /* 衣柜 */
    hideSpots[0].active = 1;

    hideSpots[1].x = 12.0f;
    hideSpots[1].y = (float)(MAP_HEIGHT / 2) + 0.5f;
    hideSpots[1].kind = 1;              /* 床底 */
    hideSpots[1].active = 1;

    hiddenIn = -1;
    hideSpotNear = -1;

    /* ③ **4 个解密装置，四种锁型一次全摆出来** ——
       用户要求「把波形锁也加进去，多加一个箱子」。
       现在固定 4 个，每种锁型一个，进一次训练场就能把
       拨盘 / 接线 / 均衡器 / 调波器 四个 UI 全看一遍，不用反复进出。

       走廊 x=2..21 的分配：
         x=5.5   可搜容器
         x=7.5   安全区
         x=9.5   藏身点 1（衣柜）
         x=12.0  藏身点 2（床底）
         x=13.0 / 14.3 / 15.6 / 16.9 / 18.2 / 19.5   六个锁箱（每种锁型一个）
         x=20.5  固定靶
       注意调波器面板比别的锁宽（LUIS(760) vs LUIS(660)），
       正好用来验证宽面板在小窗口下的表现。 */
    {
        for (int k = 0; k < LOCK_TYPE_COUNT; k++) {
            int i = 1 + k;                       /* containers[1..LOCK_TYPE_COUNT] */
            /* 2026-11-05：锁型 4 → 6，间距从 2.0 收到 1.3。
               原来 2.0 × 6 会把最后一个推到 23.5 —— 跑出走廊(21)还压过靶子(20.5)。 */
            containers[i].x = 13.0f + (float)k * 1.3f;
            containers[i].y = (float)(MAP_HEIGHT / 2) + 0.5f;
            containers[i].active = 1;
            containers[i].searched = 0;
            containers[i].content = 0;
            containers[i].locked = 1;
            containers[i].lockType = k;          /* 0 拨盘 1 接线 2 均衡 3 调波 */
            containers[i].lockCode = 0x2A5;
        }
        containerCount = 1 + LOCK_TYPE_COUNT;    /* 1 个可搜 + 每种锁型各一个 */
    }

    ApplyFov();
}

/* ------------------------------------------------------------------ */
/* v3.1 新手教程：一步一步检测玩家真的做了动作                          */
/* ------------------------------------------------------------------ */

int TutorialStepTotal(void) { return TUT_DONE - TUT_MOVE; }
int TutorialStepNumber(void) {
    if (tutorialDone) return TutorialStepTotal();
    return tutorialStep + 1;
}

const char* TutorialObjective(void) {
    static char buf[160];
    switch (tutorialStep) {
    case TUT_MOVE:
        snprintf(buf, sizeof(buf), "MOVE  -  %s %s %s %s",
                 VkName(g_bindings[ACT_MOVE_FWD]), VkName(g_bindings[ACT_MOVE_LEFT]),
                 VkName(g_bindings[ACT_MOVE_BACK]), VkName(g_bindings[ACT_MOVE_RIGHT]));
        break;
    case TUT_LOOK:   return "LOOK  :  MOUSE";
    case TUT_SPRINT:
        snprintf(buf, sizeof(buf), "SPRINT  -  HOLD %s", VkName(g_bindings[ACT_SPRINT]));
        break;
    case TUT_LIGHT:
        snprintf(buf, sizeof(buf), "FLASHLIGHT  -  %s", VkName(g_bindings[ACT_FLASHLIGHT]));
        break;
    case TUT_MAP:
        snprintf(buf, sizeof(buf), "MAP  -  %s", VkName(g_bindings[ACT_MAP]));
        break;
    case TUT_SEARCH:
        snprintf(buf, sizeof(buf), "LOCK / SEARCH  -  %s", VkName(g_bindings[ACT_INTERACT]));
        break;
    case TUT_CRANK:
        snprintf(buf, sizeof(buf), "CRANK  -  HOLD %s", VkName(g_bindings[ACT_CRANK]));
        break;
    /* ==================================================================
     * 2026-11-06（**P1-3 教程按键提示写反**）
     * ------------------------------------------------------------------
     * 原来 TUT_THROW 提示 ACT_CANCEL_CHARGE（左键），
     * 而真正的投掷是 ACT_THROW（右键，见鼠标蓄力那段）；
     * TUT_KNOCK 提示 ACT_THROW（右键），
     * 而敲击/放噪音是 ACT_DECOY（G）。
     * 玩家照着提示按键永远推不动教程。
     * ================================================================ */
    case TUT_THROW:
        snprintf(buf, sizeof(buf), "THROW  -  HOLD %s", VkName(g_bindings[ACT_THROW]));
        break;
    case TUT_KNOCK:
        /* 两个键都写出来：G 是正解，右键也算（见下面的推进条件） */
        snprintf(buf, sizeof(buf), "MAKE NOISE  -  %s  /  %s",
                 VkName(g_bindings[ACT_DECOY]), VkName(g_bindings[ACT_THROW]));
        break;
    case TUT_BREAKER:
        snprintf(buf, sizeof(buf), "BREAKER  -  HOLD %s", VkName(g_bindings[ACT_INTERACT]));
        break;
    case TUT_EXIT:
        return "REACH THE EXIT";
    case TUT_DONE:
    default:
        return "TUTORIAL COMPLETE";
    }
    return buf;
}

const char* TutorialHint(void) {
    switch (tutorialStep) {
    case TUT_MOVE:   return "WALK A FEW STEPS DOWN THE HALL.";
    case TUT_LOOK:   return "MOVE THE MOUSE TO LOOK AROUND.";
    case TUT_SPRINT: return "HOLD SPRINT AND RUN FOR A MOMENT.";
    case TUT_LIGHT:  return "TURN THE FLASHLIGHT OFF, THEN BACK ON.";
    case TUT_MAP:    return "OPEN THE MAP, THEN CLOSE IT.";
    case TUT_SEARCH: return "SOLVE THE LOCK, THEN HOLD INTERACT.";
    case TUT_CRANK:  return "HOLD CRANK UNTIL THE BATTERY RISES.";
    case TUT_THROW:  return "HOLD THROW, AIM AT THE TARGET, RELEASE.";
    case TUT_KNOCK:  return "HOLD KNOCK, RELEASE TO MAKE NOISE.";
    case TUT_BREAKER:return "TAKE THE FUSE TO THE BREAKER.";
    case TUT_EXIT:   return "THE EXIT IS AT THE END OF THE HALL.";
    case TUT_DONE:
    default:         return "NICE WORK. RETURNING TO THE MAIN MENU...";
    }
}

void TutorialBegin(void) {
    tutorialStep = TUT_MOVE;
    tutorialDone = 0;
    tutorialTimer = 0.0f;
    tutorialActionTimer = 0.0f;
    tutorialStartX = posX;
    tutorialStartY = posY;
    tutorialStartDirX = dirX;
    tutorialStartDirY = dirY;
    tutorialLightOff = 0;
    tutorialThrown = 0;
    tutorialKnocked = 0;
    tutorialMapOpened = 0;
    uiDirty = 1;
    tutorialGuideOn = 0;   /* v3.3：进训练营默认不展开引导 */
}

static void TutorialAdvance(void) {
    if (tutorialStep >= TUT_DONE) return;
    tutorialStep++;
    tutorialTimer = 0.0f;
    tutorialActionTimer = 0.0f;
    tutorialStepTimer = 0.0f;   /* P1-4：换步就重置超时 */
    tutorialStartX = posX;
    tutorialStartY = posY;
    tutorialStartDirX = dirX;
    tutorialStartDirY = dirY;
    tutorialLightOff = 0;
    tutorialThrown = 0;
    tutorialKnocked = 0;
    tutorialMapOpened = 0;
    if (tutorialStep >= TUT_DONE) {
        tutorialStep = TUT_DONE;
        tutorialDone = 1;
        tutorialTimer = 0.0f;
    }
    uiDirty = 1;
}

static void TutorialUpdate(float dt) {
    if (!trainingMode) return;

    if (tutorialDone) {
        tutorialTimer += dt;
        if (tutorialTimer >= 3.5f) {
            /* 教程不写存档，也不影响正式进度，直接回主菜单。
               v28：必须把模式/教程状态全部清干净。否则训练场的 99 件道具
               和走廊地图会跟着 trainingMode 一起留到下一次 Continue，
               变成「挑战地图 + 无限道具」的混合关卡。 */
            ResetRunModeFlags();
            hasSave = SaveHasPendingRun();
            currentState = STATE_MAIN_MENU;
            menuSelection = 0;
            isMapOpen = 0;
            mapFullScreen = 0;
            ForceCursor(1);
            uiDirty = 1;
        }
        return;
    }

    /* P1-4：本步计时（用于"外部条件禁止这一步"的超时兜底） */
    tutorialStepTimer += dt;

    switch (tutorialStep) {
    case TUT_MOVE:
        /* 离开起点 1 格就算学会移动 */
        if (Dist2(posX, posY, tutorialStartX, tutorialStartY) >= 1.0f) TutorialAdvance();
        break;
    case TUT_LOOK:
        /* 视线方向转了约 10 度 */
        if (dirX * tutorialStartDirX + dirY * tutorialStartDirY < 0.985f) TutorialAdvance();
        break;
    case TUT_SPRINT:
        /* v6：每日限制 —— 不能冲刺 */
        { extern int DailyForbidSprint(void);
          if (DailyForbidSprint() && sprinting) sprinting = 0; }
        if (sprinting) tutorialActionTimer += dt;
        else tutorialActionTimer = 0.0f;
        if (tutorialActionTimer >= 0.5f) { TutorialAdvance(); break; }
        /* P1-4：禁跑时永远攒不满 —— 超时放行 */
        if (tutorialStepTimer >= TUTORIAL_STEP_TIMEOUT) {
            if (trainingMode) {
                extern const char* pickupMsg; extern float pickupMsgTimer;
                pickupMsg = (g_lang == LANG_ZH) ? "本局禁跑，跳过这一步" : "SPRINT IS DISABLED TODAY - SKIPPED";
                pickupMsgTimer = 2.0f;
            }
            TutorialAdvance();
        }
        break;
    case TUT_LIGHT:
        if (!flashlightOn) tutorialLightOff = 1;
        if (tutorialLightOff && flashlightOn) { TutorialAdvance(); break; }
        /* P1-4：禁灯时这一行永远走不到 —— 超时放行 */
        if (tutorialStepTimer >= TUTORIAL_STEP_TIMEOUT) {
            if (trainingMode) {
                extern const char* pickupMsg; extern float pickupMsgTimer;
                pickupMsg = (g_lang == LANG_ZH) ? "本局禁灯，跳过这一步" : "FLASHLIGHT IS DISABLED TODAY - SKIPPED";
                pickupMsgTimer = 2.0f;
            }
            TutorialAdvance();
        }
        break;
    case TUT_MAP:
        /* 地图打开时 Update 会提前 return，所以这里不能只看 isMapOpen；
           改由 WndProc 在真正打开地图时记一个 flag，玩家把地图关掉后再推进。 */
        if (tutorialMapOpened && !isMapOpen) TutorialAdvance();
        break;
    case TUT_SEARCH:
        /* 第一只箱子（DIAL 锁）里才有保险丝；拿到它就算这一步过了。 */
        if (fusesHeld > 0) TutorialAdvance();
        break;
    case TUT_CRANK:
        if (cranking || IsActionDown(ACT_CRANK)) tutorialActionTimer += dt;
        else tutorialActionTimer = 0.0f;
        if (tutorialActionTimer >= 0.5f) TutorialAdvance();
        break;
    case TUT_THROW:
        if (tutorialThrown) TutorialAdvance();
        break;
    case TUT_KNOCK:
        /* P1-2：tutorialKnocked 原来**从来没有被置 1**（只有清零），
           所以这一步必然卡死。现在两处都会置它（见下面），
           并且**投掷也算制造噪音** —— 训练场不一定授予 G 键需要的
           DECOY 天赋，不给兜底就会死锁（P1-4）。 */
        if (tutorialKnocked || tutorialThrown) TutorialAdvance();
        break;
    case TUT_BREAKER:
        if (breakerOpen) TutorialAdvance();
        break;
    case TUT_EXIT:
        if (breakerOpen &&
        Dist2(posX, posY, exitX, exitY) < EXIT_ENTER_R2 * EXIT_ENTER_R2) TutorialAdvance();
        break;
    default:
        break;
    }
}


/* v2.1：找离出生点最远的空格（BFS 洪泛）。
   原来出口硬编码在出生点正右方 2 格 —— 每层开局第一帧构图完全一样，
   而且整层探索的动机被削掉一半。 */
/* v6 第 ㉔ 轮：**从任意格出发**找最远格。
   原来只有"从出生点 (1,1)"一个版本，所以战役里写"出口更远"
   根本做不出来 —— 最远格就是最远格，再找一次还是它。
   现在第 3/5 关改成「从配电箱出发的最远格」：
   通电之后你必须横穿整张图。 */
static void FindFarthestFromCell(int sx, int sy, float* outX, float* outY) {
    static int dist[MAP_HEIGHT][MAP_WIDTH];
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) dist[y][x] = -1;

    int qx[MAP_WIDTH * MAP_HEIGHT], qy[MAP_WIDTH * MAP_HEIGHT];
    int head = 0, tail = 0;
    if (sx < 1 || sx >= MAP_WIDTH - 1 || sy < 1 || sy >= MAP_HEIGHT - 1) { sx = 1; sy = 1; }
    dist[sy][sx] = 0;
    qx[tail] = sx; qy[tail] = sy; tail++;

    const int dx4[4] = { 1, -1, 0, 0 };
    const int dy4[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        int cx = qx[head], cy = qy[head]; head++;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx4[k], ny = cy + dy4[k];
            if (nx < 1 || nx >= MAP_WIDTH - 1 || ny < 1 || ny >= MAP_HEIGHT - 1) continue;
            if (worldMap[ny][nx]) continue;
            if (dist[ny][nx] >= 0) continue;
            dist[ny][nx] = dist[cy][cx] + 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }

    int best = -1, bx = MAP_WIDTH - 3, by = MAP_HEIGHT - 3;
    for (int y = 1; y < MAP_HEIGHT - 1; y++) {
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (worldMap[y][x]) continue;
            if (dist[y][x] > best) { best = dist[y][x]; bx = x; by = y; }
        }
    }
    *outX = (float)bx + 0.5f;
    *outY = (float)by + 0.5f;
}

/* 旧接口：从出生点 (1,1) 出发 */
static void FindFarthestCell(float* outX, float* outY) {
    FindFarthestFromCell(1, 1, outX, outY);
}

/* ==================================================================
 * v28：**上锁箱子的出现频率**
 * ------------------------------------------------------------------
 * 原来只有「阶段1：保险丝箱」会掷锁：18% 概率、每层最多 2 个；
 * 阶段2 补出来的补给箱完全不参与，而保险丝箱本来就只有 2~6 个 ——
 * 结果是很多层一个锁箱都见不到，四种锁盘几乎玩不到。
 *
 * 现在改成**所有容器放完之后统一判定**：每个箱子 30% 概率上锁，
 * 每层最多 3 个。等容器数量确定后再判定，才能准确保证
 * 「未上锁的箱子仍然够放本层要求的保险丝」—— 保险丝不许进锁箱，
 * 锁太多会让整层无法通关。
 * ================================================================ */
/* ==================================================================
 * 2026-11-05 用户要求：**锁箱比例随层数递增，封顶 80%**
 * ------------------------------------------------------------------
 *   第 1 层   0%   —— 全部不上锁，先熟悉开箱
 *   第 2 层   5%
 *   之后每层 +9%，第 11 层起封顶 80%（8 箱 = 6 锁 2 不锁）
 *
 * 为什么用「比例」而不是「每箱掷骰」：
 *   掷骰在 8 个箱子上方差极大，同样 30% 有时出 0 个、有时出 4 个。
 *   改成先算「这一层该有几个锁箱」，再随机挑哪几个 —— 曲线才可控。
 *   小数部分用概率进位，所以第 2 层 40% 的机会出 1 个，不是永远 0。
 *
 * **硬约束（必须留着）**：至少 2 个箱子不上锁。
 *   保险丝只放未上锁的箱子（下面那段有说明），全锁 = 整层无解。
 * ================================================================ */
#define LOCKBOX_PCT_MIN_FLOOR    2      /* 第几层开始上锁 */
#define LOCKBOX_PCT_BASE         5      /* 第 2 层的比例 */
#define LOCKBOX_PCT_STEP         9      /* 之后每层 +9 */
#define LOCKBOX_PCT_CAP         70      /* 封顶（2026-11-05 用户：80 → 70，减轻开锁疲劳） */
#define LOCKBOX_MIN_UNLOCKED     2      /* 永远留这么多个不锁的 */

static int LockboxPctForFloor(int lv) {
    if (lv < LOCKBOX_PCT_MIN_FLOOR) return 0;
    int pct = LOCKBOX_PCT_BASE + (lv - LOCKBOX_PCT_MIN_FLOOR) * LOCKBOX_PCT_STEP;
    if (pct > LOCKBOX_PCT_CAP) pct = LOCKBOX_PCT_CAP;
    return pct;
}

static void ApplyLockboxes(void) {
    lockedThisFloor = 0;
    if (containerCount <= 0) return;

    int pct = LockboxPctForFloor(currentLevel);
    if (pct <= 0) return;

    /* 这一层该锁几个：整数部分 + 小数部分概率进位 */
    int scaled = containerCount * pct;          /* 分子 */
    int want   = scaled / 100;
    if ((rand() % 100) < (scaled % 100)) want++;

    /* 至少留 LOCKBOX_MIN_UNLOCKED 个不上锁 —— 保险丝要靠它们 */
    int limit = containerCount - LOCKBOX_MIN_UNLOCKED;
    if (want > limit) want = limit;
    if (want <= 0) return;

    int room = want;

    /* 打乱顺序，避免「永远从 0 号箱开始锁」的规律感。 */
    int order[MAX_CONTAINERS];
    for (int i = 0; i < containerCount; i++) order[i] = i;
    for (int i = containerCount - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }

    /* ==================================================================
     * 2026-11-05：**锁型发牌**
     * ------------------------------------------------------------------
     * 原来每箱独立 rand() % LOCK_TYPE_COUNT：
     *   · 同一层可能连出三个「接线」，也可能一整层见不到新锁；
     *   · 6 种锁能不能被玩到，靠运气。
     *
     * 现在按「一副牌」发：本层有 N 个锁箱就从洗好的 6 张里取前 N 张，
     * 所以**同层绝不重复**；牌每层重新洗，
     * 一层最多 6 个锁箱（MAX_CONTAINERS 8 - 2 个保底未上锁），
     * 所以一层里 6 种锁都可能出现，跨层也一定都能轮到。
     * ================================================================ */
    int typeDeck[LOCK_TYPE_COUNT];
    for (int i = 0; i < LOCK_TYPE_COUNT; i++) typeDeck[i] = i;
    for (int i = LOCK_TYPE_COUNT - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = typeDeck[i]; typeDeck[i] = typeDeck[j]; typeDeck[j] = t;
    }

    for (int n = 0; n < containerCount && lockedThisFloor < room; n++) {
        int idx = order[n];
        if (containers[idx].locked) continue;
        /* 数量已经在上面算好了（want），这里不再掷骰 —— 只决定锁哪几个 */
        containers[idx].lockType = typeDeck[lockedThisFloor % LOCK_TYPE_COUNT];
        containers[idx].lockCode = rand() & 0x7FF;
        lockedThisFloor++;
        containers[idx].locked = 1;
    }
}

/* ==================================================================
 * 2026-11-06：舱门放置（一个房间一扇）
 * ------------------------------------------------------------------
 * 是 game.c 的函数而不是 map_rooms.c 的，因为要写 hatch 表，
 * 而 hatch 表在 hatch.c；map_rooms 只负责"告诉我房间在哪"。
 * ================================================================ */
/* ==================================================================
 * 2026-11-06：舱门的**运行时状态**与效果
 * ------------------------------------------------------------------
 * 所有计时器都是"秒"，每帧递减。集中放这里，方便一眼看完。
 * ================================================================ */
float hatchLightsT   = 0.0f;   /* 供电室：全灯清雾剩余秒数 */
float hatchGoldMulT  = 0.0f;   /* 仓储室：金币 x1.5 剩余秒数 */
float hatchBloodT    = 0.0f;   /* 病房  ：血迹追踪剩余秒数 */
float hatchSpeedT    = 0.0f;   /* 地狱  ：移速 +25% 剩余秒数 */

/* A-9 2026-11-06：舱门的标题（提示行用） */
const char* HatchTitle(int idx) {
    if (idx < 0 || idx >= hatchCount) return "";
    switch (hatches[idx].kind) {
    case HATCH_BOILER:  return (g_lang == LANG_ZH) ? "总闸" : "MASTER VALVE";
    case HATCH_STORAGE: return (g_lang == LANG_ZH) ? "仓门" : "STORAGE LOCK";
    case HATCH_POWER:   return (g_lang == LANG_ZH) ? "总闸" : "MAIN BREAKER";
    case HATCH_WARD:    return (g_lang == LANG_ZH) ? "医疗台" : "MED BAY";
    case HATCH_BRICK:   return (g_lang == LANG_ZH) ? "破墙锤" : "SLEDGE";
    case HATCH_HELL:    return (g_lang == LANG_ZH) ? "祭坛" : "ALTAR";
    case HATCH_FLESH:   return (g_lang == LANG_ZH) ? "肉堆" : "FLESH PILE";
    default: return "";
    }
}

/* ==================================================================
 * 门槛判定 —— 用户要求：「特殊事件一定要有门槛」
 * ------------------------------------------------------------------
 * 返回值：1 = 可以开；0 = 不行，why 里写清**具体缺什么**。
 * 文案必须给数字（"你有 87 金"），不能只说"条件不足"。
 * ================================================================ */
int HatchGateCheck(int idx, char* why, int cap) {
    if (idx < 0 || idx >= hatchCount) return 0;
    const Hatch* h = &hatches[idx];
    if (why && cap > 0) why[0] = 0;

    switch (h->kind) {
    case HATCH_BOILER: {
        /* 门槛：本房家具全部搜空 + 手上 >=1 根保险丝 */
        int left = 0;
        for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++)
            if (roomProps[i].active && roomProps[i].searchLeft > 0) left++;
        if (left > 0) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "先把这间房搜空（还剩 %d 件）" : "SEARCH OUT THIS ROOM (%d LEFT)", left);
            return 0;
        }
        if (fusesHeld < 1) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "需要 1 根保险丝" : "NEED 1 FUSE");
            return 0;
        }
        return 1;
    }
    case HATCH_STORAGE: {
        if (carryGold < 150) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "买断整仓需要 150 金（你有 %d）" : "NEED 150 GOLD (YOU HAVE %d)", carryGold);
            return 0;
        }
        return 1;
    }
    case HATCH_POWER: {
        if (fusesHeld < 2 && battery < 60.0f) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "需要 2 根保险丝，或电量 >= 60（现在 %.0f）"
                                    : "NEED 2 FUSES OR 60%% BATTERY (NOW %.0f)", battery);
            return 0;
        }
        return 1;
    }
    case HATCH_WARD: {
        if (sanity >= 50.0f) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "理智还有 %.0f，用不上" : "SANITY %.0f - NOT HURT ENOUGH", sanity);
            return 0;
        }
        return 1;
    }
    case HATCH_BRICK: {
        /* 扳手是蓄力制、没有次数上限，所以门槛换成体力（挥大锤要力气） */
        if (stamina < 60.0f) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "体力不够（%.0f/60）" : "NOT ENOUGH STAMINA (%.0f/60)", stamina);
            return 0;
        }
        return 1;
    }
    case HATCH_HELL: {
        if (carryGold < 100) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "献祭需要 100 金（你有 %d）" : "NEED 100 GOLD (YOU HAVE %d)", carryGold);
            return 0;
        }
        if (sanity < 40.0f) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "理智太低（%.0f/40）" : "SANITY TOO LOW (%.0f/40)", sanity);
            return 0;
        }
        return 1;
    }
    case HATCH_FLESH: {
        int n = flares + bottles + smokeBombs + gluePots + echoBolts;
        if (n <= 0) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "需要 1 个投掷物" : "NEED 1 THROWABLE");
            return 0;
        }
        return 1;
    }
    default:
        return 0;
    }
}

/* ==================================================================
 * 效果结算 —— 由 HatchTick 在**开门动画播完的那一帧**调用（AD-27）
 * ================================================================ */
void HatchFireEffect(int idx) {
    if (idx < 0 || idx >= hatchCount) return;
    Hatch* h = &hatches[idx];

    switch (h->kind) {
    case HATCH_BOILER:
        /* 崩塌：本层重掷。真正的重掷在下一轮的黑屏流程里消费这个请求。 */
        /* 立刻进入崩塌流程（黑屏 -> 重掷 -> 淡入）。
           注意：效果是在**开门动画播完那一帧**才调到这里（AD-27）。 */
        MakeNoise(h->x, h->y, 30.0f);
        CollapseBegin();
        break;

    case HATCH_STORAGE:
        if (!WalletSpendRun(150)) return;
        {
            int refilled = 0;
            for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++) {
                if (!roomProps[i].active) continue;
                roomProps[i].searchLeft = (roomProps[i].kind == ROOMPROP_SHELF) ? 3 : 2;
                roomProps[i].searched = 0;
                refilled++;
            }
            for (int i = 0; i < containerCount && i < MAX_CONTAINERS; i++) {
                if (!containers[i].active) continue;
                containers[i].searched = 0;
                refilled++;
            }
            hatchGoldMulT = 60.0f;
            pickupMsg = (g_lang == LANG_ZH) ? "整仓买断了！60 秒内金币翻倍" : "WHOLE CRATE BOUGHT - 60s GOLD BOOST";
            pickupMsgTimer = 2.0f;
            (void)refilled;
        }
        MakeNoise(h->x, h->y, 12.0f);
        break;

    case HATCH_POWER:
        if (fusesHeld >= 2) fusesHeld -= 2; else battery -= 25.0f;
        if (battery < 0.0f) battery = 0.0f;
        hatchLightsT = 20.0f;
        pickupMsg = (g_lang == LANG_ZH) ? "全厂通电 —— 20 秒" : "POWER ON - 20 SECONDS";
        pickupMsgTimer = 2.0f;
        MakeNoise(h->x, h->y, 18.0f);
        break;

    case HATCH_WARD:
        sanity += 40.0f;
        if (sanity > SanityMax()) sanity = SanityMax();
        stamina = StaminaMax();
        hatchBloodT = 60.0f;      /* 留下血迹：60 秒内怪能追你 */
        pickupMsg = (g_lang == LANG_ZH) ? "包扎好了（但留了血迹）" : "PATCHED UP (BUT YOU BLED)";
        pickupMsgTimer = 2.0f;
        break;

    case HATCH_BRICK: {
        stamina -= 25.0f;
        if (stamina < 0.0f) stamina = 0.0f;
        /* 在门周围 8 格里挑一堵"背后是空地"的墙打穿 */
        int ex = (int)h->x, ey = (int)h->y, carved = 0;
        static const int dx8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
        static const int dy8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        for (int k = 0; k < 8 && !carved; k++) {
            int wx = ex + dx8[k], wy = ey + dy8[k];
            if (wx < 1 || wy < 1 || wx >= MAP_WIDTH - 1 || wy >= MAP_HEIGHT - 1) continue;
            if (!worldMap[wy][wx]) continue;                 /* 要墙 */
            int bx = wx + dx8[k], by = wy + dy8[k];
            if (bx < 1 || by < 1 || bx >= MAP_WIDTH - 1 || by >= MAP_HEIGHT - 1) continue;
            if (worldMap[by][bx]) continue;                  /* 墙后要空地 */
            worldMap[wy][wx] = 0;
            carved = 1;
        }
        pickupMsg = carved
            ? ((g_lang == LANG_ZH) ? "墙开了！" : "THE WALL GAVE WAY!")
            : ((g_lang == LANG_ZH) ? "这里砸不开" : "NOTHING GIVES HERE");
        pickupMsgTimer = 2.0f;
        MakeNoise(h->x, h->y, 30.0f);
        break;
    }

    case HATCH_HELL:
        if (!WalletSpendRun(100)) return;
        sanity -= 15.0f;
        if (sanity < 0.0f) sanity = 0.0f;
        hatchSpeedT = 30.0f;
        pickupMsg = (g_lang == LANG_ZH) ? "血祭生效 —— 30 秒" : "THE PACT HOLDS - 30 SECONDS";
        pickupMsgTimer = 2.0f;
        MakeNoise(h->x, h->y, 16.0f);
        break;

    case HATCH_FLESH: {
        /* 消耗一个投掷物 */
        if (flares > 0) flares--;
        else if (bottles > 0) bottles--;
        else if (smokeBombs > 0) smokeBombs--;
        else if (gluePots > 0) gluePots--;
        else if (echoBolts > 0) echoBolts--;
        /* 全图怪静止 20 秒 —— 复用现成的 stun 字段，不新增 AI 钩子 */
        for (int i = 0; i < MON_HUNTER_MAX; i++) {
            extern Monster monHunters[MON_HUNTER_MAX];
            if (monHunters[i].active) monHunters[i].stun = 20.0f;
        }
        {
            if (lightBeast.active) lightBeast.stun = 20.0f;
        }
        pickupMsg = (g_lang == LANG_ZH) ? "它开始进食了…20 秒" : "IT IS FEEDING... 20 SECONDS";
        pickupMsgTimer = 2.0f;
        break;
    }

    default:
        break;
    }
    PlaySfx(SFX_POWER, 0.7f);
}

/* ==================================================================
 * 2026-11-06：**崩塌**（锅炉房总闸）
 * ------------------------------------------------------------------
 * 用户定义：「黑屏 2 秒钟，然后出现到一个崩塌层。」
 *
 * 时间轴（总长 2.0 秒）：
 *   0.0 ~ 0.8   淡出（screenBlackout 0 -> 1）
 *   0.8         **全黑那一帧**执行重掷（玩家看不到突变）
 *   0.8 ~ 2.0   淡入（1 -> 0）
 *
 * 重掷失败（新层不可通关）→ **回滚到原层**，并提示"这里塌不动"。
 * 用户明确要求回滚，所以必须先把原层完整快照下来。
 * ================================================================ */
int   collapsedLevel   = 0;      /* 本层是否为崩塌层（影响生成参数） */
/* 2026-11-06：崩塌重掷期间置 1，用来区分"崩塌重掷"与"正常换层" */
static int s_collapseGen = 0;
float screenBlackout   = 0.0f;   /* 0 = 无，1 = 全黑 */
static float collapseT = 0.0f;   /* 崩塌计时 */
static int   collapseArmed = 0;  /* 1 = 崩塌流程进行中 */
static int   s_collapseRerolled = 0;  /* 本次崩塌是否已经执行过重掷 */
static int   s_collapseRerollCount = 0; /* 进程内累计重掷次数（测试用） */

/* 原层快照 —— 只在崩塌开始时写一次，失败时整块还原 */
typedef struct {
    int   worldMap[MAP_HEIGHT][MAP_WIDTH];
    int   explored[MAP_HEIGHT][MAP_WIDTH];
    CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];
    Container containers[MAX_CONTAINERS];
    int   containerCount;
    HideSpot hideSpots[HIDESPOT_MAX];
    int   hideSpotCount;
    RoomProp roomProps[MAX_ROOM_PROPS];
    int   roomPropCount;
    Hatch hatches[MAX_HATCHES];
    int   hatchCount;
    Breaker breaker;
    float exitX, exitY, posX, posY;
    int   fusesHeld, fusesNeeded, roomCount;
} CollapseSnapshot;

static CollapseSnapshot* s_collapseSnap = NULL;

static void CollapseSnapshotTake(void) {
    if (!s_collapseSnap) s_collapseSnap = (CollapseSnapshot*)malloc(sizeof(CollapseSnapshot));
    if (!s_collapseSnap) return;
    memcpy(s_collapseSnap->worldMap, worldMap, sizeof(worldMap));
    memcpy(s_collapseSnap->explored, explored, sizeof(explored));
    memcpy(s_collapseSnap->cellMeta, cellMeta, sizeof(cellMeta));
    memcpy(s_collapseSnap->containers, containers, sizeof(containers));
    memcpy(s_collapseSnap->hideSpots, hideSpots, sizeof(hideSpots));
    memcpy(s_collapseSnap->roomProps, roomProps, sizeof(roomProps));
    memcpy(s_collapseSnap->hatches, hatches, sizeof(hatches));
    s_collapseSnap->containerCount = containerCount;
    s_collapseSnap->hideSpotCount  = hideSpotCount;
    s_collapseSnap->roomPropCount  = roomPropCount;
    s_collapseSnap->hatchCount     = hatchCount;
    s_collapseSnap->breaker        = breaker;
    s_collapseSnap->exitX = exitX; s_collapseSnap->exitY = exitY;
    s_collapseSnap->posX  = posX;  s_collapseSnap->posY  = posY;
    s_collapseSnap->fusesHeld = fusesHeld;
    s_collapseSnap->fusesNeeded = fusesNeeded;
    s_collapseSnap->roomCount = roomCount;
}

static void CollapseSnapshotRestore(void) {
    if (!s_collapseSnap) return;
    memcpy(worldMap, s_collapseSnap->worldMap, sizeof(worldMap));
    memcpy(explored, s_collapseSnap->explored, sizeof(explored));
    memcpy(cellMeta, s_collapseSnap->cellMeta, sizeof(cellMeta));
    memcpy(containers, s_collapseSnap->containers, sizeof(containers));
    memcpy(hideSpots, s_collapseSnap->hideSpots, sizeof(hideSpots));
    memcpy(roomProps, s_collapseSnap->roomProps, sizeof(roomProps));
    memcpy(hatches, s_collapseSnap->hatches, sizeof(hatches));
    containerCount = s_collapseSnap->containerCount;
    hideSpotCount  = s_collapseSnap->hideSpotCount;
    roomPropCount  = s_collapseSnap->roomPropCount;
    hatchCount     = s_collapseSnap->hatchCount;
    breaker        = s_collapseSnap->breaker;
    exitX = s_collapseSnap->exitX; exitY = s_collapseSnap->exitY;
    posX  = s_collapseSnap->posX;  posY  = s_collapseSnap->posY;
    fusesHeld = s_collapseSnap->fusesHeld;
    fusesNeeded = s_collapseSnap->fusesNeeded;
    roomCount = s_collapseSnap->roomCount;
}

/* 崩塌层是否可玩 —— 用现成的校验器，不自己写一套判据 */
static int CollapseLevelPlayable(void) {
    MapCheck mc;
    memset(&mc, 0, sizeof(mc));
    MapValidate(&mc);
    if (!mc.ok) return 0;
    if (!mc.exitReachable) return 0;
    if (!mc.spawnOpen) return 0;
    if (mc.propsOutsideRoom > 0) return 0;
    return 1;
}

/* ==================================================================
 * 2026-11-06（第 6 轮）：**把玩家从墙里挪出来**
 * ------------------------------------------------------------------
 * 返回 1 = 挪过位置，0 = 原地就是空的。
 *
 * 为什么单独成函数：第一次我把它内联写在崩塌分支里，
 * 端到端用例**抓不到** —— 那个种子里玩家恰好落在空地，
 * 删掉修复测试照样绿（实测）。
 * 抽出来之后可以直接"把玩家塞进墙里"再断言。
 * ================================================================ */
int CollapseUnstickPlayer(void) {
    if (!Blocked(posX, posY)) return 0;
    for (int r = 1; r <= 12; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                /* 只走这一圈的边上，圈内已经找过 */
                if (dx > -r && dx < r && dy > -r && dy < r) continue;
                int cx = (int)posX + dx, cy = (int)posY + dy;
                if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) continue;
                if (worldMap[cy][cx]) continue;
                float nx = (float)cx + 0.5f, ny = (float)cy + 0.5f;
                if (Blocked(nx, ny)) continue;      /* 家具也算 */
                posX = nx; posY = ny;
                return 1;
            }
        }
    }
    return 0;
}

void CollapseBegin(void) {
    collapseArmed = 1;
    collapseT = 0.0f;
    screenBlackout = 0.0f;
    s_collapseRerolled = 0;
}

/* 每帧推进。返回 1 表示"正在崩塌，本帧应吞掉玩家输入"。 */
int CollapseTick(float dt) {
    if (!collapseArmed) return 0;

    collapseT += dt;

    if (collapseT < 0.8f) {
        /* 阶段 A：淡出 */
        screenBlackout = collapseT / 0.8f;
        if (screenBlackout > 1.0f) screenBlackout = 1.0f;
        return 1;
    }

    /* ==================================================================
     * 2026-11-06（**第 5 轮修**）：重掷触发条件原来是
     *     if (screenBlackout >= 1.0f && collapseT - dt < 0.8f)
     * 这个条件是**永远不会成立的**：
     *   淡出阶段只在 collapseT < 0.8 时跑，所以 blackout 最多到 0.98x；
     *   跨过 0.8 的那一帧用的还是**上一帧**的值（约 0.98），
     *   于是 `>= 1.0` 为假 → 重掷被跳过；
     *   下一帧 blackout 终于等于 1.0，但 `collapseT - dt < 0.8` 又为假。
     * 结果：玩家看到黑屏，然后**什么都没发生**。
     *
     * 实测这件事被端到端用例掩盖过 —— 用例只断言"塌完之后地图可玩"，
     * 而"根本没塌"同样满足那个断言。所以这里同时加了一个计数器，
     * 让测试能断言"重掷真的执行了"。
     * ================================================================ */
    if (!s_collapseRerolled && collapseT >= 0.8f) {
        s_collapseRerolled = 1;
        s_collapseRerollCount++;
        CollapseSnapshotTake();
        collapsedLevel = 1;
        s_collapseGen = 1;      /* 告诉 GenerateLevel：这一次是崩塌重掷 */
        GenerateLevel();
        if (!CollapseLevelPlayable()) {
            /* ---- 用户要求：失败就回到原来的层 ---- */
            CollapseSnapshotRestore();
            collapsedLevel = 0;
            pickupMsg = (g_lang == LANG_ZH) ? "这里塌不动" : "IT WON'T GIVE WAY";
            pickupMsgTimer = 2.2f;
        } else {
            /* ================================================================
             * 2026-11-06（第 6 轮）：**重掷后玩家可能被埋进新地形**
             * ----------------------------------------------------------------
             * GenerateLevel 换掉了整张图，但玩家坐标没动。
             * 原来那里是空地、现在是墙 —— 玩家会被卡在墙里：
             * Blocked() 对当前位置返回 1，两个轴向都动不了，
             * 除了重开没有任何出路。
             *
             * 现在：塌成功之后检查脚下，被埋就挪到最近的空地中心。
             * 顺着半径一圈圈找，找到的第一个空地就是最近的。
             * ================================================================ */
            CollapseUnstickPlayer();
        }
        /* 重掷会把舱门表清空并重置计时器 —— 这是期望行为，
           崩塌层不生成舱门（防止连锁刷）。 */
    }

    if (collapseT >= 2.0f) {
        collapseArmed = 0;
        screenBlackout = 0.0f;
        collapseT = 0.0f;
        return 0;
    }
    /* 阶段 B：淡入 */
    screenBlackout = 1.0f - (collapseT - 0.8f) / 1.2f;
    if (screenBlackout < 0.0f) screenBlackout = 0.0f;
    return 1;
}

int CollapseBusy(void) { return collapseArmed; }

/* 仅测试用：进程内累计执行过几次"重掷"。
   没有它就没法区分"塌了"和"黑屏完什么都没发生"，
   而那正是本轮抓到的 bug。 */
int CollapseRerollCount(void) { return s_collapseRerollCount; }

int HatchPlaceInRoom(int fx, int fy, int w, int h, int themeId) {
    /* 崩塌层**不生成舱门** —— 否则可以在崩塌层里再触发一次崩塌，
       无限重掷刷金币。这不是防玩家，是防机制本身。 */
    if (collapsedLevel) return -1;
    int kind = HatchKindForTheme(themeId);
    if (kind < 0) return -1;

    /* 第一优先：角落（同时在两条边上），不挡过道 */
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int cx = fx + x, cy = fy + y;
                if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) continue;
                if (worldMap[cy][cx]) continue;                 /* 要站得住 */
                int edgeX = (x == 0 || x == w - 1);
                int edgeY = (y == 0 || y == h - 1);
                int corner = edgeX && edgeY;
                int edge = edgeX || edgeY;
                if (pass == 0 && !corner) continue;             /* 先角落 */
                if (pass == 1 && (corner || !edge)) continue;   /* 再贴墙 */

                /* 别和家具重叠 */
                int clash = 0;
                for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++) {
                    if (!roomProps[i].active) continue;
                    if ((int)roomProps[i].x == cx && (int)roomProps[i].y == cy) { clash = 1; break; }
                }
                if (clash) continue;

                return HatchAdd((float)cx + 0.5f, (float)cy + 0.5f, kind, themeId);
            }
        }
    }
    return -1;
}

void GenerateLevel(void) {
    /* ==================================================================
     * **崩塌标记的生命周期**
     * ------------------------------------------------------------------
     * collapsedLevel 只能在"崩塌重掷"的那一次生成里为 1；
     * 正常换层必须清掉，否则从崩塌层往下走，后面每一层都还是崩塌层。
     * 这个 bug 不会编译报错，只会让玩家觉得"怎么一直在塌"。
     * ================================================================ */
    {
        int wasCollapse = s_collapseGen;
        s_collapseGen = 0;
        if (!wasCollapse) collapsedLevel = 0;
    }

    /* 2026-11-06：每层重建舱门表与所有舱门计时器 —— 舱门是"本层装置"，
       不跨层保留（跨层保留要动存档，见后续 SAVE_VERSION 33）。 */
    HatchReset();
    hatchLightsT = hatchGoldMulT = hatchBloodT = hatchSpeedT = 0.0f;
    BestiaryBeginFloor();
    SuspicionReset();   /* v7：新一层，清空可疑点 */
    {   /* v7：新一层，清空地上的钱袋 */
        extern void MoneyBagsResetForNewFloor(void);
        MoneyBagsResetForNewFloor();
    }
    { extern void HintsReset(void); HintsReset(); }   /* v7 B4: 一次性提示 */
    { extern void WardenReset(void); WardenReset(); }   /* v7 B1: 新一层清掉守门人 */
    {   /* v7：清道夫计时 / 连躲计数 / 重置（每层都清） */
        extern void ScavengerReset(void);
        extern float floorTime;
        extern int hideStreak;
        ScavengerReset(); floorTime = 0.0f; hideStreak = 0;
    { extern void EchoMarksReset(void); EchoMarksReset(); }   /* v2.4.62 换层清空回声标记 */
    { extern void FlankBuild(void), ResetSecondSpawn(void);
      FlankBuild(); ResetSecondSpawn();
      { extern void CatchCountResetFloor(void); CatchCountResetFloor(); }
    }   /* v2.4.94 封喉表 + 第二只计时 + 理智档位重置 */
    }
    {   /* v7：拾荒者按强度档决定是否出现 */
        extern void CollectorSpawnIfEligible(void);
        CollectorSpawnIfEligible();
    }
    {   /* v7：扳手次数每层补满 */
        extern int RunUpgWrenchCharges(void);
        extern int wrenchCharges, wrenchLastUsed;
        extern float wrenchCd;
        wrenchCharges = RunUpgWrenchCharges();
        wrenchLastUsed = 0;
        wrenchCd = 0.0f;
    }
    /* v2.0 修复：Boss 标记必须在**任何 SpawnMonster() 之前**设置。
       原来它写在函数中段（怪物已经刷出来之后），导致第 6/12/18 层的
       第一只怪是普通血量，被打死重生后才 ×8 —— Boss 整体错位一层。 */
    /* ===== v7 **BOSS 已删除** =====
       它原本只是「听声怪 x 血量」，没有任何独有机制 ——
       三个 Boss 层的差别只有 hp 倍率和速度，玩家分不出来。
       现在恒为 0：不再有 Boss 层，第 6/12/18 层就是普通层
       （怪物强度靠 v7 的 T0~T15 十六档强度档位来给）。
       保留 bossLevel 变量，免得 save / HUD / shop 的引用断掉。 */
    bossLevel = 0;
    bossDefeated = 0;
    monsterMuted = trainingMode ? 1 : 0;   /* 训练场里怪不叫 */
    if (trainingMode) {
        BuildTrainingArena();
        return;                       /* 训练场不生成迷宫、箱子、配电箱 */
    }

    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) { worldMap[y][x] = 1; explored[y][x] = 0; }

    /* DFS 递归回溯（数组栈） */
    int stackX[MAP_WIDTH * MAP_HEIGHT], stackY[MAP_WIDTH * MAP_HEIGHT], top = 0;
    int cx = 1, cy = 1;
    worldMap[cy][cx] = 0; stackX[top] = cx; stackY[top] = cy; top++;
    int dxs[] = {2, 0, -2, 0}, dys[] = {0, 2, 0, -2};

    while (top > 0) {
        cx = stackX[top - 1]; cy = stackY[top - 1];
        int validDirs[4], count = 0;
        for (int i = 0; i < 4; i++) {
            int nx = cx + dxs[i], ny = cy + dys[i];
            if (nx > 0 && nx < MAP_WIDTH - 1 && ny > 0 && ny < MAP_HEIGHT - 1 && worldMap[ny][nx] == 1)
                validDirs[count++] = i;
        }
        if (count > 0) {
            int dir = validDirs[rand() % count];
            int nx = cx + dxs[dir], ny = cy + dys[dir];
            worldMap[cy + dys[dir] / 2][cx + dxs[dir] / 2] = 0;
            worldMap[ny][nx] = 0;
            stackX[top] = nx; stackY[top] = ny; top++;
        } else top--;
    }

    /* 打破死胡同，生成环路 */
    for (int y = 1; y < MAP_HEIGHT - 1; y++) {
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (worldMap[y][x] == 1 &&
                ((worldMap[y][x - 1] == 0 && worldMap[y][x + 1] == 0) ||
                 (worldMap[y - 1][x] == 0 && worldMap[y + 1][x] == 0))) {
                /* ==================================================================
                 * 2026-11-06（第二轮实测修正）：45% -> 22%
                 * ------------------------------------------------------------------
                 * 用户反馈：「空旷场地太多了，迷宫不够。」
                 *
                 * 45% 是我上一轮为了"多路径"拍的，但 36x36 的偶数格其实很多，
                 * 45% 打通就把迷宫填成了大厅 —— 反而更难认路。
                 *
                 * 22% 仍然比最初的 30% 更低，但 36x36 的基础骨架更宽，
                 * 绝对环路数依然够"绕开守门人"。
                 * ================================================================ */
                /* 崩塌层把环路打得更碎（8%）：用户要的"有些路会被堵死" */
                if (rand() % 100 < (collapsedLevel ? 8 : 22)) worldMap[y][x] = 0;
            }
        }
    }

    /* ==================================================================
     * 2026-11-06（用户实测）：**死路太多**
     * ------------------------------------------------------------------
     * 用户原话：「目前这个算法导致地图内很多死路，导致游玩体验很差」。
     *
     * DFS 生成的迷宫天生多死路 —— 每棵树的分支末端都是死路。
     * 环路打通（上面那一步）只能随机消灭一部分，剩下的仍然很多。
     *
     * 这里加一个针对性回合：**逐个处理死路格**
     *   死路格 = 四邻里只有 1 个是空地的位置
     *   对每个死路格，往它的墙方向看 1~2 格，如果那边是空地，
     *   就把中间那堵墙打掉 —— 死路立刻变成环。
     *
     * 只在能接上时才打，接不上就保留（不会凭空挖出新的死路）。
     * 崩塌层跳过：那时候本来就是故意要"路更堵"。
     * ================================================================ */
    if (!collapsedLevel) {
        for (int pass = 0; pass < 2; pass++) {
            for (int y = 1; y < MAP_HEIGHT - 1; y++) {
                for (int x = 1; x < MAP_WIDTH - 1; x++) {
                    if (worldMap[y][x]) continue;

                    /* 数四邻开口 */
                    int openN = 0;
                    if (!worldMap[y][x - 1]) openN++;
                    if (!worldMap[y][x + 1]) openN++;
                    if (!worldMap[y - 1][x]) openN++;
                    if (!worldMap[y + 1][x]) openN++;
                    if (openN != 1) continue;          /* 只处理死路 */

                    /* 试四个方向：墙在 (x+dx,y+dy)，再往外是空地就打穿 */
                    static const int dxs[4] = { 1, -1, 0, 0 };
                    static const int dys[4] = { 0, 0, 1, -1 };
                    int done = 0;
                    for (int k = 0; k < 4 && !done; k++) {
                        int wx = x + dxs[k], wy = y + dys[k];
                        if (wx < 1 || wy < 1 || wx >= MAP_WIDTH - 1 || wy >= MAP_HEIGHT - 1) continue;
                        if (!worldMap[wy][wx]) continue;             /* 已经是通路 */
                        int bx = wx + dxs[k], by = wy + dys[k];
                        if (bx < 1 || by < 1 || bx >= MAP_WIDTH - 1 || by >= MAP_HEIGHT - 1) continue;
                        if (worldMap[by][bx]) continue;              /* 外面也得是空地 */
                        worldMap[wy][wx] = 0;                        /* 打通 → 死路变环 */
                        done = 1;
                    }
                }
            }
        }
    }

    /* 随机小房间（严格限制在内部格，避免打穿边界墙）
       2026-11-06：5 -> 3。每个 blob 是 2x2 的强制空地，
       36x36 上 5 个会把本来就减少的墙体再挖薄。 */
    for (int i = 0; i < 3; i++) {
        int rx = 2 + rand() % (MAP_WIDTH - 4);
        int ry = 2 + rand() % (MAP_HEIGHT - 4);
        for (int oy = 0; oy < 2; oy++)
            for (int ox = 0; ox < 2; ox++)
                worldMap[ry + oy][rx + ox] = 0;
    }

    /* ==================================================================
     * 2026-11-06（v2.4-rc C3）：**主题房间**
     * ------------------------------------------------------------------
     * 放在上面那 5 个 2x2 小房间**之后**：
     *   · 开关关闭时，上面那段代码一字未改 → 地图与 v2.4-beta **逐位相同**
     *   · 房间随后覆盖上去，所以房间里的柱子不会被 blob 打穿
     *
     * 放不下就跳过（GenerateThemeRooms 内部处理），
     * 绝不因为房间放不下让整层生成失败。
     * ================================================================ */
#if CANITY_ROOMS
    CellMetaReset();
    {
        extern int GenerateThemeRooms(int);
        extern int roomSeedUsed;
        int want = 1 + (rand() % 2);      /* 1~2 个 */
        /* v2.4-rc：记下这一阶段的种子。读档后用它重放，
           否则每层重新载入都会丢掉房间的色调、家具和碰撞。 */
        roomSeedUsed = (int)(rand() & 0x7FFFFFFF);
        srand((unsigned)roomSeedUsed);
        (void)GenerateThemeRooms(want);
    }
#else
    CellMetaReset();
#endif

    /* DARK 词条（手电起始电量低）的处理放在掷出词条之后 ——
       原来它写在掷词条之前，用的是**上一层**的 levelMod，
       导致「拿到 DARK 的这一层不生效，要等下一层才生效」。 */

    posX = 1.5f; posY = 1.5f;
    dirX = 1.0f; dirY = 0.0f;
    ApplyFov();

    /* 出生区只挖内部格 1..3，第 0 行/列必须保持是墙 */
    for (int y = 1; y <= 3; y++)
        for (int x = 1; x <= 3; x++) worldMap[y][x] = 0;

    /* 出口门：固定在出生点正前方（右侧），锁死等待配电箱通电 */
    /* 生成迷宫时保证出生点右侧有通路 */
    worldMap[1][2] = 0; worldMap[1][3] = 0;  /* 确保右侧有路 */
    FindFarthestCell(&exitX, &exitY);   /* v2.1：离出生点最远的空格 */

    /* ==================================================================
     * 2026-11-06（v2.4-rc）：**容器候选必须从出生点可达**
     * ------------------------------------------------------------------
     * 1000 种子洪泛发现：DFS 迷宫 + 5 个随机 2x2 空地会造出
     * 与出生点**不连通**的孤立空间，平均占 1.96% 的可通行格。
     *
     * 而下面这段候选列表原来是"所有空地" —— 于是保险丝/配电箱
     * 有概率被放进孤岛里，整层直接不可通关。
     * 这是**真正的死局来源**，比"出口选错"隐蔽得多。
     *
     * 现在先做一次从出生点出发的 BFS，只把可达格收进候选。
     * ================================================================ */
    static int reach[MAP_HEIGHT][MAP_WIDTH];
    {
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) reach[y][x] = 0;
        int rqx[MAP_WIDTH * MAP_HEIGHT], rqy[MAP_WIDTH * MAP_HEIGHT];
        int rh = 0, rt = 0;
        if (worldMap[1][1] == 0) { reach[1][1] = 1; rqx[rt] = 1; rqy[rt] = 1; rt++; }
        while (rh < rt) {
            int cx = rqx[rh], cy = rqy[rh]; rh++;
            static const int ddx[4] = { 1, -1, 0, 0 };
            static const int ddy[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; k++) {
                int nx = cx + ddx[k], ny = cy + ddy[k];
                if (nx < 1 || nx >= MAP_WIDTH - 1 || ny < 1 || ny >= MAP_HEIGHT - 1) continue;
                if (worldMap[ny][nx]) continue;
                { extern int PropBlocksAt(int, int); if (PropBlocksAt(nx, ny)) continue; }
                if (reach[ny][nx]) continue;
                reach[ny][nx] = 1;
                rqx[rt] = nx; rqy[rt] = ny; rt++;
            }
        }
    }

    /* 收集空地用于放置容器/配电箱/安全屋（排除出生区、出口周围、不可达区） */
    static int cells[MAP_WIDTH * MAP_HEIGHT][2];
    int emptyCount = 0;
    for (int y = 1; y < MAP_HEIGHT - 1; y++) {
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (worldMap[y][x] == 0 && reach[y][x] &&
                Dist2((float)x, (float)y, 1.5f, 1.5f) > 16.0f) {
                cells[emptyCount][0] = x;
                cells[emptyCount][1] = y;
                emptyCount++;
            }
        }
    }

    /* ==================================================================
     * 2026-11-06（v2.4-rc P-6）：**这里原来是无条件覆盖**
     * ------------------------------------------------------------------
     * 旧代码在这一行又跑了一次 FindFarthestCell，把上面刚算好的
     * 战役出口**直接盖掉** —— 那个分支等于从来没生效过。
     * 现在只在"不是战役 3/5 关"时兜底：那两关的出口挪到配电箱
     * 定稿之后再算，那时 breaker.x/y 才是本层的真实位置。
     * ================================================================ */
    {
        extern float CampaignExitDistNow(void);
        if (!(CampaignExitDistNow() > 1.01f)) {
            FindFarthestCell(&exitX, &exitY);
        }
    }
    /* ==================================================================
     * 2026-11-06（v2.4-rc / D）：**撤离点必须有两条以上通路**
     * ------------------------------------------------------------------
     * 现象：撤离点只有一个方向能进，守门人站在那条路上就完全堵死。
     * 做法：数四邻开口，少于 2 个就打通一堵"背后还有空地"的墙。
     * 只在可能时才动（找不到就放弃），不会把地图挖乱。
     * ================================================================ */
    {
        int ex = (int)exitX, ey = (int)exitY;
        if (ex >= 1 && ey >= 1 && ex < MAP_WIDTH - 1 && ey < MAP_HEIGHT - 1 && !worldMap[ey][ex]) {
            static const int dx4[4] = { 1, -1, 0, 0 };
            static const int dy4[4] = { 0, 0, 1, -1 };
            int openN = 0;
            for (int k = 0; k < 4; k++) {
                int nx = ex + dx4[k], ny = ey + dy4[k];
                if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
                if (!worldMap[ny][nx]) openN++;
            }
            if (openN < 2) {
                for (int k = 0; k < 4; k++) {
                    int wx = ex + dx4[k], wy = ey + dy4[k];
                    if (wx < 1 || wy < 1 || wx >= MAP_WIDTH - 1 || wy >= MAP_HEIGHT - 1) continue;
                    if (!worldMap[wy][wx]) continue;              /* 已是通路 */
                    int bx = wx + dx4[k], by = wy + dy4[k];
                    if (bx < 1 || by < 1 || bx >= MAP_WIDTH - 1 || by >= MAP_HEIGHT - 1) continue;
                    if (worldMap[by][bx]) continue;               /* 后面还得是空地 */
                    worldMap[wy][wx] = 0;                         /* 打通第二条路 */
                    break;
                }
            }
        }
    }

    breakerOpen = 0;  /* 出口门锁死，需通电开启 */

    /* 容器 / 保险丝 / 配电箱 */
    /* v3.0：每层生成 3~5 个藏身点 */
    hideSpotCount = 0;
    hiddenIn = -1;
    hideSpotNear = -1;
    for (int i = 0; i < HIDESPOT_MAX; i++) hideSpots[i].active = 0;

    if (lockActive) CloseLockOverlay();   /* v3.0：换层时别把锁盘带进新关 */
    for (int i = 0; i < MAX_CONTAINERS; i++) {
        containers[i].active = 0;
        containers[i].locked = 0;      /* v3.0：换层清锁 */
        containers[i].lockType = 0;
        containers[i].lockCode = 0;
        containers[i].searched = 0;
    }
    lockedThisFloor = 0;   /* v3.0：每层重新计数 */
    containerCount = 0;
    fusesHeld = 0;
    breakerOpen = 0;
    hoverKind = 0;
    hoverIndex = -1;
    holdTimer = 0.0f;
    breaker.active = 0;

    /* 随机词条：第 2 关起，60% 概率
       两条分支都消耗同样次数的 rand()，保证后续随机序列对齐。 */
    {
        int roll100 = rand() % 100;
        int pick = rand() % MOD_COUNT;
        if (g_forceMod == -2) {
            levelMod = -1;
            modSanityMul = 1.0f;
            modSpeedMul = 1.0f;
        } else if (g_forceMod >= 0) {
            levelMod = g_forceMod % MOD_COUNT;
            modSanityMul = 1.0f;
            modSpeedMul = 1.0f;
            if (levelMod == 1) modSpeedMul = 1.25f;
            else if (levelMod == 3) modSanityMul = 1.35f;
            else if (levelMod == 5) modSanityMul = 0.80f;
        } else {
            RollLevelMod(roll100, pick);
        }
    }
    /* 词条倍率已在上面重置并抽取；战役规则现在叠加到本层基础倍率上，
       这样首层和后续层都会生效，同时不会覆盖随机词条。 */
    /* ==================================================================
     * v6 第 ㉑ 轮：**每日任务的修正终于生效了**
     * ==================================================================
     * 第 ⑦ 轮写好 daily.c 的 6 个修正函数，但**一个调用点都没有** ——
     * 玩家看到的每日任务只有"目标"，没有"世界规则变了"。
     *
     * 这里在事件表之后叠一层，顺序：基础 → 层词条 → 事件 → 战役 → 每日
     * ================================================================ */
    if (dailyMode) {
        extern float DailyMonsterSpeedMul(void), DailyBatteryDrainMul(void);
        extern float DailyGoldMul(void), DailyNoiseMul(void);
        modSpeedMul   *= DailyMonsterSpeedMul();
        modBatteryMul *= DailyBatteryDrainMul();
        modShardMul   *= DailyGoldMul();
        modNoiseMul   *= DailyNoiseMul();
    }

    if (campaignMode) ApplyCampaignRules(campaignStage);
    levelBanner = 5.0f;

    /* DARK：手电起始电量只有 55%。必须放在掷词条之后，
       否则用的是上一层的 levelMod，词条会延迟一层生效。 */
    if (levelMod == 0) battery = (float)BatteryMax() * 0.55f;

    /* 本关需要的保险丝：2 根起，每 3 关 +1，最多 4（主线）；无尽模式封顶到 MAX_CONTAINERS */
    /* ==================================================================
     * v7 第 ㊿ 轮：**战役模式用 kCampaign[].fuses**
     * ==================================================================
     * 那个字段填好了（4/3/6/3·3·4/8）但全工程 0 个读取点，
     * 于是 5 关的保险丝全走普通曲线 —— 战役规模差异完全不成立。
     * ================================================================ */
    if (campaignMode) {
        extern int CampaignFusesNow(void);
        int cf = CampaignFusesNow();
        if (cf >= 2 && cf <= MAX_CONTAINERS) {
            fusesNeeded = cf;
            fusesNeeded -= (HasKeystone(SK_FUSE_LESS) ? 1 : 0) + RunFuseLess() + SkillOwned(SK_SHORTCUTJR);
            if (fusesNeeded < 2) fusesNeeded = 2;
        } else {
            fusesNeeded = 2 + currentLevel / 3;
            if (fusesNeeded > 6) fusesNeeded = 6;
        }
    } else if (isEndlessRun) {
        fusesNeeded = 2 + currentLevel / 3;   /* 见下方与箱子数联动的夹取 */
        if (fusesNeeded > MAX_CONTAINERS) fusesNeeded = MAX_CONTAINERS; /* 新增限制 */
    } else {
        /* v3.0 平衡：**不再封顶 4** ——
           原来第 6 层之后恒定 4 根，18 层零变化，玩家感觉不到难度推进。
           新曲线：每 3 层 +1，封顶 6；并且不许多于「箱子数 - 2」，
           给电池/药/投掷物留出货位（原来无尽第 18 层 8 根 = 全箱都是保险丝）。 */
        fusesNeeded = 2 + currentLevel / 3;
        if (fusesNeeded > 6) fusesNeeded = 6;
        fusesNeeded -= (HasKeystone(SK_FUSE_LESS) ? 1 : 0) + RunFuseLess() + SkillOwned(SK_SHORTCUTJR);
        if (fusesNeeded < 2) fusesNeeded = 2;
    }

    /* 容器两阶段生成：
       阶段1：必须先放够 fusesNeeded 个箱子（每个保险丝一个箱子）
       阶段2：继续填充到 contCap（词条 RICH/CRAMPED 影响这个上限） */
    int baseContainers = CONTAINERS_BASE - 2 + (rand() % 3);
    /* 无尽模式：保底容器数随层数增长，确保始终有足够箱子放保险丝 */
    if (isEndlessRun) {
        baseContainers += currentLevel / 5;  /* 每 5 层 +1 保底 */
    }
    /* ==================================================================
     * v7 第 ㊿ 轮：**保险丝曲线被容器数夹死**
     * ==================================================================
     * 曲线本来写的是 fusesNeeded = 2 + currentLevel/3（封顶 6），
     * 但箱子只有 3~5 个，紧接着这一行：
     *     if (!campaignMode && fusesNeeded > contCap - 2) fusesNeeded = contCap - 2;
     * 把曲线整个压回 2~3 根 —— **24 层没有任何爬升**。
     *
     * 审计原话：「第 2 层和第 20 层做的事一模一样」。
     *
     * 修法：反着来 —— 先让箱子数**跟着保险丝需求涨**（保险丝 + 2 个余量给
     * 电池/药/投掷物），再夹到 MAX_CONTAINERS。这样曲线才真的生效。
     * ================================================================ */
    /* ==================================================================
     * 2026-11-06（用户要求）：**地图变大，箱子也要变多**
     * ------------------------------------------------------------------
     * 36x36 的可走面积是 24x24 的两倍多，而箱子数一直是"保险丝 + 2"，
     * 于是搜刮密度被摊薄了一半 —— 走很久遇不到一个箱子。
     *
     * 这里在原来基础上按地图规模补一档：
     *   24x24 时代 +2 够用；现在按面积比给 +4（并夹到 MAX_CONTAINERS）。
     * ================================================================ */
    if (baseContainers < fusesNeeded + 4) baseContainers = fusesNeeded + 4;
    if (baseContainers > MAX_CONTAINERS) baseContainers = MAX_CONTAINERS;
    if (baseContainers < 5) baseContainers = 5;

    int contCap = (int)(ContainerTargetCount(levelMod, baseContainers) * DailyCrateCountMul() + 0.5f);

    /* ==================================================================
     * 2026-11-06（用户实测）：**箱子数量不足会让舱门永远打不开**
     * ------------------------------------------------------------------
     * 用户原话：「房间有时候不刷新足够的箱子，导致大箱子打开不了。」
     *
     * 成因是两层削减叠加：
     *   · CRAMPED 词条  -2
     *   · 每日挑战 DailyCrateCountMul() 可能 < 1
     * 于是 contCap 被压到只剩 fusesNeeded —— 那一层所有箱子都是保险丝，
     * 一个金币都搜不到，而仓储室的门槛是 150 金。
     *
     * 修法：给 contCap 一个下限 **fusesNeeded + 3**，
     * 保证至少有 3 个非保险丝箱子能出金币/道具。
     * 上限仍然是 MAX_CONTAINERS。
     * ================================================================ */
    if (contCap < fusesNeeded + 3) contCap = fusesNeeded + 3;
    if (contCap > MAX_CONTAINERS) contCap = MAX_CONTAINERS;
    if (contCap < fusesNeeded) contCap = fusesNeeded;

    /* v3.0 平衡：保险丝不许多于「箱子数 - 2」——
       否则整层所有箱子都是保险丝，电池/药/投掷物一件都搜不到。
       （上面的 baseContainers 已经保证了 fusesNeeded + 2，这里通常不再触发） */
    if (!campaignMode && fusesNeeded > contCap - 2) fusesNeeded = contCap - 2;
    if (fusesNeeded < 2) fusesNeeded = 2;

    int attempts = 0;
    /* 阶段1：保证 fusesNeeded 个箱子 */
    while (containerCount < fusesNeeded && containerCount < MAX_CONTAINERS && attempts < 512) {
        attempts++;
        int idx = rand() % emptyCount;
        float cx = cells[idx][0] + 0.5f, cy = cells[idx][1] + 0.5f;
        if (Dist2(cx, cy, exitX, exitY) < 9.0f) continue;
        if (!SpotClear(cx, cy, 1.6f, 1, -1)) continue;

        int ok = 1;
        for (int k = 0; k < containerCount; k++)
            if (Dist2(cx, cy, containers[k].x, containers[k].y) < 2.0f) { ok = 0; break; }
        if (!ok) continue;

        containers[containerCount].x = cx;
        containers[containerCount].y = cy;
        containers[containerCount].searched = 0;
        containers[containerCount].content = 0;  /* 先占位，后面再分发内容 */
        containers[containerCount].active = 1;
        containerCount++;
    }
    /* v3.0：藏身点 —— 两轮放置：
       第一轮按正常约束挑（避开出口/容器/配电箱/彼此）
       第二轮**放宽**（只要求不是墙），保证一定生得出来。
       之前只做第一轮，结果 SpotClear 把候选全拒了 → 一个都没有
       （用户实测 hideSpotCount=0）。 */
    hideSpotCount = 0;
    {
        /* ==================================================================
         * 2026-11-06（用户要求）：**隐藏点之间必须有最小间距**
         * ------------------------------------------------------------------
         * 「你要有一个隐藏点的距离设置，一定区域之内不能出现两个隐藏点，
         *   不然没什么用。」
         *
         * 原来第一轮只要求 2 格、兜底更是只有 1.2 格 ——
         * 同一段走廊里能出两三个桶，等于一段路给三次免死，
         * 而地图另一头一个都没有。
         *
         * 现在硬性要求：
         *   第一轮 7 格   （主约束）
         *   第二轮 5 格   （兜底放宽，但绝不低于它）
         * 宁可少放一个，也不要在同一片区域堆两个。
         * ================================================================ */
        const float HIDESPOT_MIN_GAP2      = 49.0f;   /* 7 格 */
        const float HIDESPOT_MIN_GAP2_RELAX = 25.0f;  /* 5 格（兜底下限） */
        /* ==================================================================
         * 2026-11-06（用户要求）：藏身点"稍微增加一点，随层数逐渐减少"
         * ------------------------------------------------------------------
         * 原来是 2~3 个、与层数无关。地图 36x36 之后一层要跑更久，
         * 2~3 个藏身点在长图里几乎遇不到。
         *
         * 现在：**第 1 层最多 6 个，越深越少，最低 3 个**。
         * 递减让后期的紧张感回升 —— 前期给你工具熟悉，后期收回。
         * 上限 6 等于 HIDESPOT_MAX，不触碰存档布局。
         * ================================================================ */
        int want = 6 - (currentLevel - 1) / 5;
        if (want > HIDESPOT_MAX) want = HIDESPOT_MAX;
        if (want < 3) want = 3;
        /* 抖 1 个，避免每层数量完全可预测 */
        want += (rand() % 2) ? 0 : -1;
        if (want < 3) want = 3;
        if (want > HIDESPOT_MAX) want = HIDESPOT_MAX;

        /* ---- 第一轮：严格 ---- */
        for (int tries = 0; tries < 300 && hideSpotCount < want && emptyCount > 0; tries++) {
            int idx = rand() % emptyCount;
            float hx2 = cells[idx][0] + 0.5f, hy2 = cells[idx][1] + 0.5f;
            if (Dist2(hx2, hy2, exitX, exitY) < 6.0f) continue;
            if (!SpotClear(hx2, hy2, 1.4f, 1, -1)) continue;
            int bad = 0;
            for (int k = 0; k < hideSpotCount; k++)
                if (Dist2(hx2, hy2, hideSpots[k].x, hideSpots[k].y) < HIDESPOT_MIN_GAP2) { bad = 1; break; }
            if (bad) continue;
            hideSpots[hideSpotCount].x = hx2;
            hideSpots[hideSpotCount].y = hy2;
            hideSpots[hideSpotCount].kind = rand() % 3;
            hideSpots[hideSpotCount].active = 1;
            hideSpotCount++;
        }

        /* ---- 第二轮：兜底，只要求脚下不是墙 ---- */
        for (int tries = 0; tries < 400 && hideSpotCount < want && emptyCount > 0; tries++) {
            int idx = rand() % emptyCount;
            float hx2 = cells[idx][0] + 0.5f, hy2 = cells[idx][1] + 0.5f;
            /* 兜底也不能贴着出口，否则藏身点会和离关目标重叠，
               造成玩家按 E 后看似藏好、实际却站在出口触发区里的错觉。 */
            if (Dist2(hx2, hy2, exitX, exitY) < 6.0f) continue;
            int bad = 0;
            for (int k = 0; k < hideSpotCount; k++)
                if (Dist2(hx2, hy2, hideSpots[k].x, hideSpots[k].y) < HIDESPOT_MIN_GAP2_RELAX) { bad = 1; break; }
            if (bad) continue;
            /* 兜底也不能压在箱子/配电箱上，否则垃圾桶会被道具盖住，
               玩家只看到 HIDE 提示却找不到桶。 */
            if (!SpotClear(hx2, hy2, 0.9f, 1, -1)) continue;
            hideSpots[hideSpotCount].x = hx2;
            hideSpots[hideSpotCount].y = hy2;
            hideSpots[hideSpotCount].kind = rand() % 3;
            hideSpots[hideSpotCount].active = 1;
            hideSpotCount++;
        }
    }


    /* 阶段2：继续填充到 contCap（受词条影响） */
    attempts = 0;
    /* v3.3：**必须防 emptyCount==0** —— 下面有 rand() % emptyCount，
       空地为 0 时是整数除零，x86 上直接 SIGFPE 崩溃（不是返回 0）。
       地图极端退化时（整层都是墙）会走到这里。 */
    while (containerCount < contCap && containerCount < MAX_CONTAINERS
           && emptyCount > 0 && attempts < 512) {
        attempts++;
        int idx = rand() % emptyCount;
        float cx = cells[idx][0] + 0.5f, cy = cells[idx][1] + 0.5f;
        if (Dist2(cx, cy, exitX, exitY) < 9.0f) continue;
        if (!SpotClear(cx, cy, 1.6f, 1, -1)) continue;

        int ok = 1;
        for (int k = 0; k < containerCount; k++)
            if (Dist2(cx, cy, containers[k].x, containers[k].y) < 2.0f) { ok = 0; break; }
        if (!ok) continue;

        containers[containerCount].x = cx;
        containers[containerCount].y = cy;
        containers[containerCount].searched = 0;
        containers[containerCount].content = 0;
        containers[containerCount].active = 1;
        containerCount++;
    }

    /* v3.3 **折中兜底** ——
       上面那轮要求容器离藏身点 1.6 格。加了这条检查之后，
       小地图里可能放不满 contCap，而**箱子少 = 保险丝少 = 整层卡关**，
       比视觉重叠严重得多。
       所以再补一轮宽松放置：只要求离藏身点 0.9 格（不压在同一格），
       离其它容器仍然 1.2 格。这样最坏情况是「挨着」而不是「叠在一起」。 */
    attempts = 0;
    while (containerCount < contCap && containerCount < MAX_CONTAINERS
           && emptyCount > 0 && attempts < 256) {
        attempts++;
        int idx = rand() % emptyCount;
        float cx = cells[idx][0] + 0.5f, cy = cells[idx][1] + 0.5f;
        if (Dist2(cx, cy, exitX, exitY) < 9.0f) continue;

        int ok = 1;
        for (int k = 0; k < containerCount; k++)
            if (Dist2(cx, cy, containers[k].x, containers[k].y) < 1.44f) { ok = 0; break; }
        if (!ok) continue;
        for (int k = 0; k < hideSpotCount; k++) {
            if (!hideSpots[k].active) continue;
            if (Dist2(cx, cy, hideSpots[k].x, hideSpots[k].y) < 0.81f) { ok = 0; break; }
        }
        if (!ok) continue;

        containers[containerCount].x = cx;
        containers[containerCount].y = cy;
        containers[containerCount].searched = 0;
        containers[containerCount].content = 0;
        containers[containerCount].active = 1;
        containerCount++;
    }

        /* v28：容器数量已经确定，统一判定这一层有哪些箱子上了锁。 */
        /* 2026-11-05：宝箱怪必须在 ApplyLockboxes **之前**替换掉箱子 ——
       否则那个箱子会先被发一把锁，变成"上锁的宝箱怪"（铁律四十九）。 */
    MimicReset();
    MimicTrySpawn(currentLevel, containerCount);
    ApplyLockboxes();

        /* 修复死局 Bug：优先保证能通关（放满 fusesNeeded）。
   只有在箱子极其充裕（多出 2 个以上）时，才留格子给其他物品。 */
        int need = fusesNeeded;
        if (need > containerCount) {
            need = containerCount; // 极端兜底，防止越界
        }
        /* ==================================================================
         * 2026-11-05：**保险丝改成「先未上锁、再锁箱」两遍填充**
         * ------------------------------------------------------------------
         * 原来是「保险丝绝对不许进锁箱」，而锁箱现在最高能占到 80%（8 箱锁 6）。
         * 那样未上锁的只剩 2 个，深层却需要 6 根保险丝 ——
         * 物理上放不下，整层直接无解（比"开不了锁"更严重的死锁）。
         *
         * 现在两遍：
         *   ① 先把保险丝塞进**未上锁**的箱子 —— 保证玩家不开锁也有保底路线；
         *   ② 还有剩的再塞进锁箱 —— 深层必须开锁才能凑齐，这就是锁箱的代价。
         * 因为 ① 只有 2 个空位，深层至少 2 根一定拿得到，不会一开局就卡死。
         * ================================================================ */
        {
            /* 打乱一遍箱子顺序，避免「保险丝永远在前几个箱子」的规律感 */
            int ord[MAX_CONTAINERS];
            for (int i = 0; i < containerCount; i++) ord[i] = i;
            for (int i = containerCount - 1; i > 0; i--) {
                int j = rand() % (i + 1);
                int t = ord[i]; ord[i] = ord[j]; ord[j] = t;
            }
            /* ============================================================
             * 2026-11-05 用户：「锁箱里必定有保险丝」+「至少 1 根不用开锁」
             * ------------------------------------------------------------
             * 三遍填：
             *   ① **锁箱优先**吃保险丝 —— 开锁=有明确回报，不是白费力气；
             *      但每留一根（keepFree）给未上锁的箱子
             *   ② 未上锁的箱子填剩下的
             *   ③ 万一未上锁的箱子不够，再回填锁箱
             *
             * 结果：深层每个锁箱都装着保险丝，而玩家**永远有 1 根**
             * 不开锁就能拿到，不会因为开锁不熟练直接卡死整层。
             * ============================================================ */
            const int keepFree = 1;
            for (int pass = 0; pass < 3 && need > 0; pass++) {
                for (int oi = 0; oi < containerCount && need > 0; oi++) {
                    int k = ord[oi];
                    if (containers[k].content != 0) continue;
                    if (pass == 0) {                       /* ① 锁箱，留 keepFree */
                        if (!containers[k].locked) continue;
                        if (need <= keepFree) continue;
                    } else if (pass == 1) {                /* ② 未上锁 */
                        if (containers[k].locked) continue;
                    } else {                               /* ③ 回填锁箱 */
                        if (!containers[k].locked) continue;
                    }
                    containers[k].content = 1;
                    need--;
                }
            }
        }
        for (int k = 0; k < containerCount; k++) {
            if (containers[k].content == 0) {
                int r = rand() % 100;
                /* v3.3 掉落表（投掷物重做后按新的 6 槽位重新分配）
                   content: 2 电池 / 3 药 / 4 光球 / 5 投石 /
                            6 烟雾 / 7 胶水 / 8 回声 / 9 诱饵 / 0 空
                   配比思路：投石最多（基础诱饵）、光球次之（照明刚需）、
                   烟雾和胶水是战术道具、回声和诱饵稀有。 */
                /* v3.4（方案 v5 §7）**箱子改成金币来源** ——
                   用户原话：「箱子太多了，我希望是在商店里面能买」。
                   所以箱子不再自动给道具：金币 35% / 空 25% / 道具 40%。
                   道具主要靠商店买，「找箱子」这件事的价值转移到金币上。 */
                if      (r < 35) containers[k].content = 10;  /* 金币     35% */
                else if (r < 50) containers[k].content = 2;   /* 电池     15% */
                else if (r < 62) containers[k].content = 3;   /* 药       12% */
                else if (r < 70) containers[k].content = 5;   /* 投石      8% */
                else if (r < 78) containers[k].content = 4;   /* 光球      8% */
                else if (r < 83) containers[k].content = 6;   /* 烟雾      5% */
                else if (r < 88) containers[k].content = 7;   /* 胶水      5% */
                else if (r < 92) containers[k].content = 8;   /* 回声      4% */
                else if (r < 95) containers[k].content = 9;   /* 诱饵      3% */
                else             containers[k].content = 0;   /* 空       25% */
            }
        }

    /* ==================================================================
     * 2026-11-06（v2.4-rc P-6）：**战役第 3/5 关的出口在这里才算**
     * ------------------------------------------------------------------
     * 旧版本在生成早期就读 breaker.x/y 求"离配电箱最远的格"，
     * 但配电箱要到本段之后才真正放置 —— 读到的是**上一层**的位置；
     * 之后还有一处无条件覆盖，于是这个设计从来没有生效过。
     *
     * 现在挪到配电箱定稿之后：breaker.x/y 是本层真实位置，
     * "通电之后出口在图纸另一头"这个语义才成立。
     * ================================================================ */
    {
        extern float CampaignExitDistNow(void);
        if (CampaignExitDistNow() > 1.01f && breaker.active) {
            FindFarthestFromCell((int)breaker.x, (int)breaker.y, &exitX, &exitY);
        }
    }


        {
        int nt = rand() % THEME_ENABLED;
        if (nt == themeId) nt = (nt + 1) % THEME_ENABLED;
        GenerateThemeTextures(nt);
    }
    SpawnMonster();
    /* ================================================================
     * 修：**换层后第 2、3 只听声鬼会立刻全部出现**
     * ----------------------------------------------------------------
     * GenerateLevel() 原来只重生第一只（SpawnMonster）、重生追光鬼、清偷电怪，
     * 唯独没碰 monHunters[1] / [2]。
     * 于是它们在上一层还是 active=1 的话，会**原样带到新的一层**：
     *   · 位置还是上一层的坐标（可能落进墙里）
     *   · 出场时间表 s_hunterSpawned 仍等于 3，调度器永远不会再刷
     * 玩家看到的就是「一进第二层，三只同时在场」。
     *
     * 现在换层时把它们清掉，并把时间表重置成「开局 1 只」：
     * 新的一层重新按 60 秒 / 120 秒出场。
     * ================================================================ */
    {
        extern Monster monHunters[MON_HUNTER_MAX];
        for (int hi = 1; hi < MON_HUNTER_MAX; hi++) {
            monHunters[hi].active = 0;
            monHunters[hi].stun = 0.0f;
            monHunters[hi].hitCooldown = 0.0f;
            monHunters[hi].pathLen = 0;
            monHunters[hi].pathIdx = 0;
            monHunters[hi].suspTarget = -1;
            monHunters[hi].huntTimer = 0.0f;
            monHunters[hi].stillTimer = 0.0f;
        }
        { extern void ResetSecondSpawn(void); ResetSecondSpawn(); }
    }
    /* 2026-11-06：用户要求追光怪 +1 只 */
    SpawnLightBeast();
    SpawnLightBeast2();
    /* v1.8 偷电怪：第 3 层起出现。主线每层 1 只；无尽每 5 层 +1，最多 3 只。 */
    ResetLeeches();
    if (currentLevel >= LEECH_MIN_LEVEL) {
        int n = 1;
        if (isEndlessRun) {
            n = 1 + (currentLevel - LEECH_MIN_LEVEL) / 5;
            if (n > LEECH_MAX) n = LEECH_MAX;
        }
        for (int i = 0; i < n; i++) SpawnLeech(i);
    }
    /* v1.8：随机事件（第 2 层起，按概率） */
    /* 先生成基础游荡怪，再应用 MIRROR 的额外刷怪；否则事件刚加的怪会被
       后面的 ExtrasSpawnForLevel() 清空，玩家只能看到速度/理智修正。 */
    ExtrasSpawnForLevel(currentLevel);
    levelEvent = -1;
    if (currentLevel >= 2 && (rand() % 100) < EVENT_CHANCE)
        {   /* v6：按权重抽（血月/空投更常见，幸运更少见） */
            extern int EventPickRandom(unsigned int*);
            unsigned int sd = (unsigned int)rand();
            levelEvent = EventPickRandom(&sd);
        }
    /* v2.0：每日挑战的固定加成，每层都生效 */
    if (dailyMode) {
        modSanityMul *= 1.25f;
        modShardMul *= 1.25f;
    }
    /* ===== v6：事件效果统一从表里取 =====
       加一个事件只需要在 events.c 的 kEvents[] 里加一行，
       这里永远不用改。 */
    {
        extern const EventEffect* EventEffectFor(int);
        const EventEffect* fx = EventEffectFor(levelEvent);
        modSpeedMul  *= fx->monsterSpeedMul;
        modNoiseMul  *= fx->noiseMul;
        modBatteryMul *= fx->batteryDrainMul;
        modSanityMul *= fx->sanityDrainMul;
        modShardMul  *= fx->goldMul;

        /* 电池上限按百分比压 */
        if (fx->batteryCapPct != 100 && fx->batteryCapPct > 0) {
            float cap = (float)BatteryMax() * (float)fx->batteryCapPct / 100.0f;
            if (battery > cap) battery = cap;
        }

        /* 额外游荡怪 */
        if (fx->extraMonsters > 0) {
            int added = 0;
            for (int i = 0; i < EXTRA_MAX && added < fx->extraMonsters; i++) {
                if (extras[i].active) continue;
                ExtrasSpawnOne(i);
                if (extras[i].active) added++;
            }
        }

        /* 空投：额外给投掷物 */
        if (levelEvent == EV_SUPPLY) {
            flares += 2;
            if (bottles < BottleCap()) bottles += 2;
        }
    }

    /* v1.8：图鉴解锁 —— 本层出现的怪记一笔 */
    for (int i = 0; i < EXTRA_MAX; i++) {
        if (extras[i].active) BestiaryMarkSeen(4 + extras[i].kind);
    }
    if (bossLevel) BestiaryMarkSeen(7);

    /* v2.0 CARTOGRAPHER：开局就把安全屋和配电箱的位置标在小地图上 */
    if (SkillCartoUnlock()) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                int ax = (int)safeRoom.x + dx, ay = (int)safeRoom.y + dy;
                if (safeRoom.active && ax > 0 && ax < MAP_WIDTH - 1 && ay > 0 && ay < MAP_HEIGHT - 1)
                    explored[ay][ax] = 1;
                ax = (int)breaker.x + dx; ay = (int)breaker.y + dy;
                if (ax > 0 && ax < MAP_WIDTH - 1 && ay > 0 && ay < MAP_HEIGHT - 1)
                    explored[ay][ax] = 1;
            }
        }
    }
    { extern void BestiaryNoteFloor(int,int);
      extern void BestiaryMarkSeen(int);
      BestiaryMarkSeen(0); BestiaryNoteFloor(0, currentLevel); }   /* 寻声鬼：每层都有 */
    if (lightBeast.active) { BestiaryMarkSeen(1); BestiaryNoteFloor(1, currentLevel); }           /* 追光鬼 */
    if (currentLevel >= LEECH_MIN_LEVEL) { BestiaryMarkSeen(2); BestiaryNoteFloor(2, currentLevel); }   /* 偷电怪 */
    if (jumperTrigger >= 0.0f) { BestiaryMarkSeen(3); BestiaryNoteFloor(3, currentLevel); }       /* 跳脸虫 */

    /* 记录本局见过的词条种类（算难度乘子用） */
    if (levelMod >= 0) {
        int bit = 1 << levelMod;
        if (!(modsSeenMask & bit)) { modsSeenMask |= bit; modsSeenCount++; }
    }
    /* 跳脸虫：第 3 层起按概率出现，不是每关必出 ——
       稀有才有惊吓感。触发时机也随机（进关 8~20 秒后），
       不然一进关就跳出来太突兀。 */
    /* ==================================================================
     * 2026-11-06（用户要求）：**一局总量受控**
     * ------------------------------------------------------------------
     * 用户原话：「刷新频率为一局只有上限 4 只，随着层数增加上限 4 只」。
     *
     * 旧逻辑是"每层 35% 概率出 1 只"，一局打 10 层可能遇 3~4 次，
     * 而且完全随机 —— 有的局一次没有，有的局连着来。
     *
     * 新逻辑：把**一局的总量**当预算来花。
     *   本局上限 = min(4, 1 + 层数/2)
     *     第 3 层 → 2 只，第 5 层 → 3 只，第 7 层起 → 4 只
     *   已经出过的数量达到上限 → 本层不再安排
     * ================================================================ */
    {
        int cap = 1 + currentLevel / 2;
        if (cap > JUMPER_MAX_PER_RUN) cap = JUMPER_MAX_PER_RUN;
        extern int jumperSpawnedRun;
        if (currentLevel >= JUMPER_MIN_LEVEL && jumperSpawnedRun < cap) {
            jumperTrigger = 8.0f + (float)(rand() % 13);
        } else {
            jumperTrigger = -1.0f;
        }
    }
    jumperPhase = JMP_IDLE;
    jumperTimer = 0.0f;
    parasite.active = 0;
    /* 2026-11-06：一局的总量预算与控制计时由 StartGame 负责清零 */
    { extern int jumperSpawnedRun; extern float jumperControlT;
      jumperSpawnedRun = 0; jumperControlT = 0.0f; }
    for (int i = 0; i < MAX_THROWN; i++) thrown[i].active = 0;

    /* ===== v6 **删除旧安全区** =====
       用户：「安全区是旧版本遗物，隐藏点才是主流机制」。
       核实：商店（T 键）、操作提示、校准小游戏三处的判定
       早就写成了 `inSafe || hiddenIn >= 0` —— 也就是说
       **藏在藏身点里已经能做安全区能做的全部事情**。
       安全区那半边是纯冗余，而且它的模型会顶掉配电箱的位置。
       现在 safeRoom.active 恒为 0，不再生成、不再绘制。 */
    safeRoom.active = 0;

    /* 配电箱：放在离出生点 15 格以上的远处，迫使玩家深入探索 */
    breaker.active = 0;
    for (int t = 0; t < 256; t++) {
        int idx = rand() % emptyCount;
        float bx = cells[idx][0] + 0.5f, by = cells[idx][1] + 0.5f;
        float dFromSpawn = Dist2(bx, by, 1.5f, 1.5f);
        if (dFromSpawn < 225.0f) continue;  /* 离出生点至少 15 格 (15^2=225) */
        if (!SpotClear(bx, by, 1.6f, 2, -1)) continue;
        breaker.x = bx;
        breaker.y = by;
        breaker.active = 1;
        break;
    }
    /* 强制兜底：如果随机找不到，遍历全图强行放一个 */
    if (!breaker.active) {
        float bestD2 = -1.0f;
        for (int y = 1; y < MAP_HEIGHT - 1; y++) {
            for (int x = 1; x < MAP_WIDTH - 1; x++) {
                if (worldMap[y][x]) continue;
                float bx = x + 0.5f, by = y + 0.5f;
                float dFromSpawn = Dist2(bx, by, 1.5f, 1.5f);
                if (dFromSpawn < 225.0f) continue;
                if (!SpotClear(bx, by, 1.2f, 2, -1)) continue;  /* 放宽到 1.2f */
                if (dFromSpawn > bestD2) {
                    bestD2 = dFromSpawn;
                    breaker.x = bx;
                    breaker.y = by;
                }
            }
        }
        if (bestD2 > 0.0f) breaker.active = 1;
    }
    /* ===== 最终兜底（v6 重写）=====
       原来这段有三个问题，正好对上玩家报的两个 bug：

       ① **安全屋会把配电箱顶掉** ——
          判重只写了「同一格」（cx == safeRoom.x），没有用半径。
          安全屋建模占 1.6 格，配电箱落在它隔壁一格也算「在安全屋里」，
          玩家看到的就是「安全区盖住了电闸，这层没有电闸」。

       ② **配电箱兜底被 %3 挡住** ——
          整段挂在 `currentLevel % 3 == 0` 里面。
          非 3 倍层只要前两轮随机都失败，就**永远没有配电箱**，
          而配电箱是通关必需品 → 那一层直接卡死。

       ③ 兜底没用 SpotClear，可以落在墙角/箱子里。

       现在安全区已删，只保留配电箱的两段兜底。 */


    /* ② 配电箱兜底（**无条件**，每层必须有） */
    if (!breaker.active) {
        for (int y = 1; y < MAP_HEIGHT - 1 && !breaker.active; y++) {
            for (int x = 1; x < MAP_WIDTH - 1 && !breaker.active; x++) {
                if (worldMap[y][x]) continue;
                float cx = x + 0.5f, cy = y + 0.5f;
                if (Dist2(cx, cy, 1.5f, 1.5f) < 25.0f) continue;
                if (!SpotClear(cx, cy, 1.2f, 2, -1)) continue;
                /* 离安全屋至少 4 格，绝不和它重叠 */
                if (safeRoom.active && Dist2(cx, cy, safeRoom.x, safeRoom.y) < 16.0f) continue;
                breaker.x = cx; breaker.y = cy; breaker.active = 1;
            }
        }
    }
    /* ③ 最后保险：全图任意空格（只在真的没别的地方时才走） */
    if (!breaker.active) {
        for (int y = 1; y < MAP_HEIGHT - 1 && !breaker.active; y++) {
            for (int x = 1; x < MAP_WIDTH - 1 && !breaker.active; x++) {
                if (worldMap[y][x]) continue;
                float cx = x + 0.5f, cy = y + 0.5f;
                if (safeRoom.active && Dist2(cx, cy, safeRoom.x, safeRoom.y) < 9.0f) continue;
                breaker.x = cx; breaker.y = cy; breaker.active = 1;
            }
        }
    }
}
/* ShowCursor 是引用计数的，跨状态切换很容易把引用数弄歪；
   这里用 GetCursorInfo 把光标状态直接拧到目标值 */
void ForceCursor(int visible) {
    CURSORINFO ci;
    ci.cbSize = sizeof(ci);
    for (int i = 0; i < 64; i++) {
        if (!GetCursorInfo(&ci)) break;
        int showing = (ci.flags & CURSOR_SHOWING) ? 1 : 0;
        if (showing == (visible ? 1 : 0)) break;
        ShowCursor(visible ? TRUE : FALSE);
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
int BatteryMax(void) {
    /* v2.2：SAVER 的 "cap -5" 代价现在真的生效 */
    /* ==================================================================
     * 2026-11-06（用户要求）：基础上限 100 -> 150
     * ------------------------------------------------------------------
     * 地图从 24x24 扩到 36x36 之后，一层要走的距离大约翻了一倍，
     * 原来的 100 电量撑不到图另一头 —— 手电变成"前一半能用"。
     * 同时保留所有加成/惩罚链（技能、遗物、每日、改造树）不变。
     * ================================================================ */
    int m = (int)((150 + SkillBatteryAdd() + RunBatteryCapDelta() + 30*relicOwned[3]) * DailyBatteryCapMul() * BenchBatteryCapMul());
    if (m < 30) m = 30;
    return m;
}

/* 手电每秒耗电：ECO 升级每级 -12% */
float BatteryDrainRate(void) { return BATTERY_DRAIN * BenchBatterySaver() * SkillEcoMul() * RelicBatteryMul() * modBatteryMul * BenchFlashDrainMul() * RunUpgFlashMul(); }   /* v2.7 省电 */

/* 安全屋回智速度：REST 升级每级 +25% */
float SafeHealRate(void) {
    return SAFE_HEAL_RATE * (1.0f + SkillRestAdd() + RelicHealAdd())*(HasKeystone(SK_LASTRESORT)?0.5f:1.0f);   /* REST + IRON LUNG */
}

/* v1.8：理智流失随层数增长（原版是常量，导致经济净正、玩家不死） */
float SanityDrainRate(void) {
    float base = isEndlessRun ? ENDLESS_SANITY_DRAIN : SANITY_DRAIN;
    int lv = currentLevel > 0 ? currentLevel - 1 : 0;
    float d = base + SANITY_DRAIN_STEP * (float)lv;
    if (d > SANITY_DRAIN_CAP) d = SANITY_DRAIN_CAP;
    return d;
}

/* v1.8：瓶子携带上限（绣刀的代价是把它压到 1） */
int BottleCap(void) {
    /* v3.4：DEEP POCKET（局内树 id 8）每种投掷物上限 +2 */
    extern int BenchDeepPocket(void);
    int cap = 4 + RunUpgCarryBonus() + (BenchDeepPocket() ? 2 : 0);
    if (cap < 1) cap = 1;
    /* v6 **补上 return** —— 这里原来漏了，非 void 函数落空是未定义行为。
       25 个调用点（商店 6 个货位、箱子掉落、技能判定）都拿它当上限，
       返回垃圾值会造成「钱扣了但东西没给」或「上限形同虚设」。 */
    return cap;
}

/* v2.7：体力上限以前写死 100，局内技能 LONG WIND 的「+25 体力」白给。
   上限统一走这里，HUD 的分母也跟着用。 */
/* v2.0：理智上限（遗物 WAR PAINT 会削） */
/* v2.3：武器能量 —— 三把武器共用，**不会自然恢复**。
   一局的总输出因此有上限，投掷物（不吃能量）才有存在意义。 */
float energy = 120.0f;

/* v2.8：改造树是一个**可平移缩放的画布**（参考思维导图）。
   节点用世界坐标，画的时候统一变换 —— 这样窗口多小都不会崩，
   节点数量也能无限加。 */
/* v2.9：treeVX/treeVY/treeVK 已搬到 bench.c（画布视口），这里只留鼠标位置 */


/* v2.4 校准小游戏状态 */
int   calibActive = 0;        /* 正在进行 */
float calibPos = 0.5f;
int calibUsedLevel = -1;   /* v3.0：本层已校准过的层数（每层限 1 次） */        /* 滑块位置 0..1 */
float calibDir = 1.0f;        /* 扫动方向 */
float calibZoneC = 0.5f;      /* 目标区中心 */
/* 2026-11-05：**本层已校准次数**。
   CALIB_PER_LEVEL 原来是 99（等于不限），于是躲着就能无限刷理智；
   现在用的是真计数。calibAttempts 这个死变量一并删掉。 */
int   calibUsesThisFloor = 0;
int   calibLevel = -1;        /* v2.4：这一层是否已经发过次数（防「出门再进来」刷满） */
float calibCooldown = 0.0f;
int   calibResult = 0;        /* 0 无 / 1 完美 / 2 普通 / 3 失手 */
float calibResultTimer = 0.0f;
/* v8：校准按键锁存与最短有效时间。
   按住空格时 Windows 会持续重发 WM_KEYDOWN，旧代码每收到一次就锁一次：
   刚在垃圾桶里按下“开始”，同一个按键的重复消息立刻把滑块锁在 0 附近，
   屏幕上直接蹦出「校准失手」。这两个状态保证一次按下只算一次、
   且至少让滑块跑够 0.12 秒才允许锁定。 */
static float calibElapsed = 0.0f;
static int   calibStartLatch = 0;

/* ===== v3.4 金币产出公式（方案 v5 §4）=====
    v3.4 银行结算（方案 v5 §2.3）
   BANK += CARRY + 利润 × 倍数          （活着结算）
   BANK += CARRY × 0.5                  （死亡，本金损失一半 ——
                                          调研：100% 没收会 feel-bad，
                                          50~75% 既保留赌注又不让人觉得白玩）

   倍数 = 1 + min(1, CARRY / 历史最高)   → 1.0 ~ 2.0
   带 <20% 历史最高时利润 ×0.5（惩罚躺平：不能永远只带一点点） */
int BankPreviewGain(int survived) {
    int profit=carryGold-carryAtRunStart;if(profit<0)profit=0;
    float pm=1.0f;
    if(survived) {if(!tookDamage)pm*=1.5f;if(!runSpotted)pm*=1.3f;if(!runLightUse)pm*=1.2f;
      profit=(int)(profit*pm*ExtractDepthMul(currentLevel)*ContractExitMul()*SkillExtractMul()+0.5f);}
    return WalletPreviewSettle(survived,profit);
}
void BankSettle(int survived) {
    lastRunGold=carryGold; lastRunCaught=CatchCountThisRun();
    int profit = carryGold - carryAtRunStart;   /* v6：本局净赚 = 现在 - 开局 */
    if (profit < 0) profit = 0;
    if (profit < 0) profit = 0;

    /* v3.4 **表现加成**（方案 v5 §4.4）—— 可叠加，最高 ×2.34
         没被抓过    ×1.5
         没被发现过  ×1.3
         全程关灯    ×1.2
       玩得好 = 拿得多，这是「深度收益」的另一半（另一半是层数）。 */
    if (survived && profit > 0) {
        float pm = 1.0f;
        extern int tookDamage, runSpotted, runLightUse;
        if (!tookDamage)   pm *= 1.5f;
        if (!runSpotted)   pm *= 1.3f;
        if (!runLightUse)  pm *= 1.2f;
        if (pm > 2.34f) pm = 2.34f;
        profit = (int)((float)profit * pm * ExtractDepthMul(currentLevel) * ContractExitMul() * SkillExtractMul() + 0.5f);
        lastPerfMul = pm;      /* 结算页显示用 */
    } else {
        lastPerfMul = 1.0f;
    }

    /* v6：结算统一走 WalletSettle */
    {
        extern int WalletBank(void);
        int beforeBank = WalletBank();
        { extern void WalletSettle(int, int); WalletSettle(survived, profit); }
        lastRunKept=WalletBank()-beforeBank;
        /* v6 第 ⑲ 轮：**实测遥测** —— 每局结束写一行，
           玩几局之后就能算真实金币/局、真实时长，
           再回头校准技能树价格（现在是推算值，不是实测值）。 */
        {
            extern void TelemetryRunEnd(int, int, int, int, float, int);
            extern int CatchCountThisRun(void);
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
    extern int collapsedLevel;
    float mul = collapsedLevel ? 1.8f : 1.0f;
    return ( 6.0f + 1.1f * (float)level) * EndlessDepthBonus(level) * mul;
}
float GoldBreakerAt(int level) { return (25.0f + 3.0f * (float)level) * EndlessDepthBonus(level); }
float GoldExitAt(int level)    { return (30.0f + 6.0f * (float)level) * EndlessDepthBonus(level); }

/* v6 **深度倍数** —— 撤离时按当前层算。
   第 1 层 1.00，第 24 层 2.00（上限 2.0）。
   越往下撤离越值钱，这是「贪」的诱惑。 */
float ExtractDepthMul(int level) {
    float m = 1.0f + 0.05f * (float)(level - 1);
    if (m > 2.0f) m = 2.0f;
    if (m < 1.0f) m = 1.0f;
    return m;
}

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
float MonsterThreatScore(void) {

    float sSpeed   = (THREAT_REF_SPEED  > 0.01f) ? monster.speed / THREAT_REF_SPEED : 1.0f;
    float sHear    = 1.0f;                       /* 听觉基数，由 MakeNoise 的半径体现 */
    float sPersist = 1.0f;                       /* 持久度，由 huntTimer 上限体现 */
    float sReact   = 1.0f;                       /* 反应速度 */
    return THREAT_W_SPEED   * sSpeed
         + THREAT_W_HEAR     * sHear
         + THREAT_W_PERSIST  * sPersist
         + THREAT_W_REACT    * sReact;
}

/* v8: 强度档位已搬到 src/economy.c（GoldTier / GoldTierMul / GoldHearMul / GoldSpeedMul） */


float EndlessDepthBonus(int level) {
    if (level <= 24) return 1.0f;
    float b = 1.0f + 0.15f * (float)(level - 24);
    return b;
}

float SanityMax(void) {
    float m = 100.0f + RelicMaxSanAdd()+BenchSanityAdd();
    if (m < 25.0f) m = 25.0f;
    return m;
}

float StaminaMax(void) {
    /* v3.0：改造树「耐力」+30 */
    float m = 100.0f + (float)RunStaminaAdd() + (float)BenchStaminaBonus();
    /* v6 第 ㉟ 轮：**爆发**（第 1 列第二顶点）体力上限 -30%，
       换冲刺速度 +25%（冲刺加成的调用点在下面的移速计算里）。
       第 ⑩ 轮加了节点没接 —— 玩家花钱买了个空节点。 */
    { extern int BenchBurst(void); if (BenchBurst()) m *= 0.70f; }
    return m*BenchStaminaMul();
}

/* v2.3：武器能量上限 */
int EnergyMax(void) { return BenchEnergyMax((int)ENERGY_MAX); }   /* v2.7：弹匣/电池库/嗜血 */

int UpgradeLevel(int kind) {
    switch (kind) {
        case 0: return upgBattery;
        case 1: return upgSpeed;
        case 2: return upgQuiet;
        case 3: return upgSanity;
        case 4: return upgEco;
        default: return upgRest;
    }
}


const char* UpgradeName(int kind) {
    switch (kind) {
        case 0: return "BATTERY +20";
        case 1: return "SPEED +6%";
        case 2: return "QUIETER -15%";
        case 3: return "CALM -10%";
        case 4: return "ECO -12% DRAIN";
        default: return "REST +25% HEAL";
    }
}

int UpgradeCost(int kind) {
    int lvl = UpgradeLevel(kind);
    if (lvl >= UPG_MAX) return -1;
    return 30 + lvl * 45;      /* 30 / 75 / 120 */
}



/* ---------------- 寻声怪 ---------------- */
/* v1.16：蓄满（>=0.85）额外 +1 伤害 */
int chargeBonus(float charge) {
    int b = (charge >= 0.85f) ? 1 : 0;
    if (b) b += SkillHeavyHand();    /* HEAVY HAND */
    return b;
}

/* v2.7：ThrowItem 是否真的生成了一个投掷物。
   以前池子满时它会静默不生成，而调用方已经先把数量扣掉了 —— 东西凭空消失。 */
static int g_shardTint = 0;   /* v2.1：下一批金币的颜色类型 */
int g_throwOk = 0;

/* v8: ThrowItem / UpdateShots 已搬到 src/throwing.c */

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
void SpawnParticles(float x, float y, int tint) {
    g_shardTint = tint;
    SpawnShards(x, y, 0.0f, 0.0f, 0.45f);
    g_shardTint = 0;
}

/* 生成玻璃金币：撞击点产生随机扩散的金币 */
void SpawnShards(float x, float y, float vx, float vy, float charge) {
    int count = 6 + (int)(charge * 6);  /* 6~12 个金币 */
    for (int i = 0; i < MAX_SHOTS * SHARD_COUNT && count > 0; i++) {
        if (shards_debris[i].active) continue;
        shards_debris[i].x = x;
        shards_debris[i].y = y;
        shards_debris[i].z = 0.1f;
        /* 随机扩散速度 */
        float ang = (float)rand() * 6.283f / RAND_MAX;
        float spd = 2.0f + (float)rand() * 4.0f / RAND_MAX;
        shards_debris[i].vx = cosf(ang) * spd + vx * 0.1f;
        shards_debris[i].vy = sinf(ang) * spd + vy * 0.1f;
        shards_debris[i].vz = 3.0f + (float)rand() * 4.0f / RAND_MAX;  /* 向上弹 */
        shards_debris[i].life = MAX_SHARD_LIFE;
        shards_debris[i].age = 0.0f;
        shards_debris[i].active = 1;
        shards_debris[i].tint = (unsigned char)g_shardTint;
        count--;
    }
}

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
#define ECHO_MARK_MAX   24
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

void EchoMarksReset(void) {
    for (int i = 0; i < ECHO_MARK_MAX; i++) {
        echoMarkActive[i] = 0;
        echoMarkX[i] = echoMarkY[i] = 0.0f;
        echoMarkT[i] = 0.0f;
        echoMarkKind[i] = EMK_MON;
    }
}

/* 在 (sx,sy) 周围 rad 格内记录所有怪物 */
void EchoMarksScan(float sx, float sy, float rad) {
    extern int   CollectorActive(void), ScavengerActive(void), WardenActive(void);
    extern float CollectorX(void), CollectorY(void);
    extern float ScavengerX(void), ScavengerY(void);
    extern float WardenX(void), WardenY(void);
    extern int   trainingMode;

    /* 落点自带噪音 —— **这是它的代价**：敲门会让屋里的人知道。
       没有这一下，回声就是免费情报，"什么时候用"就不再是个决策。 */
    MakeNoise(sx, sy, ECHO_NOISE_R);

    float r2 = rad * rad;

    /* 往标记表里放一条：同类同位置就刷新寿命，否则找空槽 */
    /* （C89 风格：不用嵌套函数，直接内联展开成两段） */

    /* ---------------- ① 活物：5 秒 ---------------- */
    {
        float px[10]; float py[10]; int n = 0;
        if (monster.active)    { px[n] = monster.x;    py[n] = monster.y;    n++; }
        if (monster2.active)   { px[n] = monster2.x;   py[n] = monster2.y;   n++; }
        if (monster3.active)   { px[n] = monster3.x;   py[n] = monster3.y;   n++; }
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
#define WRENCH_SWING_FRAMES  5
#define WRENCH_HIT_FRAME     2      /* 第几帧开始判定 */

float wrenchSwingT = 0.0f;      /* 剩余时间 */
float wrenchSwingPower = 1.0f;  /* 这次挥击的力度 */
int   wrenchSwingHit = 0;       /* 已经判定过了吗 */

/* 挥击进度 0..1（0 = 刚挥出，1 = 已收势） */
static float WrenchSwingProgress(void) {
    if (wrenchSwingT <= 0.0f) return 1.0f;
    return 1.0f - wrenchSwingT / WRENCH_SWING_TIME;
}

/* 当前第几帧（0..4） */
static int WrenchSwingFrame(void) {
    float p = WrenchSwingProgress();
    int f = (int)(p * (float)WRENCH_SWING_FRAMES);
    if (f < 0) f = 0;
    if (f >= WRENCH_SWING_FRAMES) f = WRENCH_SWING_FRAMES - 1;
    return f;
}

/* ---- 真正结算击退（在命中帧调用一次）---- */
static unsigned int wrenchHitMask=0;
static void WrenchApplyHit(float power) {
    extern float RunUpgWrenchRange(void);
    extern float RunUpgWrenchStun(void);

    /* ============================================================
     * 修：**扳手打不到第 2、3 只听声鬼**
     * ------------------------------------------------------------
     * 原来这里是写死的两只：{&monster, &lightBeast}。
     * 听声鬼扩到三只之后，第 2、3 只根本不在列表里 ——
     * 玩家砸到它们身上毫无反应，也击退不了。
     *
     * 位掩码：1=第1只 2=追光鬼 8=第2只 16=第3只
     * （4 留给清道夫，别动）
     * ============================================================ */
    /* 2026-11-06：追光怪 +1 只，击退目标从 4 个扩到 5 个 */
    Monster* targets[5] = { &monster, &lightBeast, &monster2, &monster3, &lightBeast2 };
    static const unsigned int tbits[4] = { 1u, 2u, 8u, 16u };
    for(int ti=0;ti<4;++ti) { Monster* target=targets[ti];
    if (target->active && !(wrenchHitMask&tbits[ti])) {
        float rng=RunUpgWrenchRange()+BenchKnockExtra();
        float dx = target->x - posX, dy = target->y - posY;
        float d2 = dx * dx + dy * dy;
        float reach = rng + 1.2f;
        if (d2 < reach * reach) {
            float d = sqrtf(d2);
            float ux = (d > 0.01f) ? dx / d : dirX;
            float uy = (d > 0.01f) ? dy / d : dirY;
            float push = rng * power;
            float nx = target->x + ux * push;
            float ny = target->y + uy * push;
            if (Blocked(nx, ny)) { nx = target->x + ux * push * 0.5f;
                                   ny = target->y + uy * push * 0.5f; }
            if (!Blocked(nx, ny)) { target->x = nx; target->y = ny; }
            wrenchHitMask|=tbits[ti];
            target->stun=RunUpgWrenchStun()*BenchKnockStunMul();
            knockCountRun++;
            if(BenchShock()) { SuspicionReset();target->stateTimer=0.0f;target->tx=target->x;target->ty=target->y; }
            target->state = 0;
            target->pathLen = 0;
            target->pathIdx = 0;
            target->repath = 0.0f;
            PlaySfx(SFX_POWER, 0.60f);
            AddShake(0.16f, 0.12f);
        }
    }

    }

    {
        extern int   CollectorActive(void);
        extern float CollectorX(void), CollectorY(void);
        extern void  CollectorHitByWrench(float, float);
        if (CollectorActive() && !(wrenchHitMask&4u)) {
            float cdx = CollectorX() - posX, cdy = CollectorY() - posY;
            if (cdx * cdx + cdy * cdy < 4.0f) { CollectorHitByWrench(posX, posY);wrenchHitMask|=4u;knockCountRun++; }
        }
    }
}

/* ---- 开始一次挥击（只扣资源，不判定）---- */
void WrenchStartSwing(float charge) {
    /* v6 每日限制：今天不能使用扳手 */
    { extern int DailyForbidWrench(void);
      if (DailyForbidWrench()) {
          extern const char* pickupMsg;
          extern float pickupMsgTimer;
          pickupMsg = g_lang == LANG_ZH ? "今天不能用扳手" : "TODAY: NO WRENCH";
          pickupMsgTimer = 1.6f;
          return;
      } }
    extern float RunUpgWrenchCooldown(void);
    extern int   wrenchCharges, wrenchLastUsed;
    extern float wrenchCd;

    float cdMax = RunUpgWrenchCooldown();

    if (cdMax < 0.0f) {
        if (wrenchLastUsed >= wrenchCharges) return;
        wrenchLastUsed++;
    } else {
        if (wrenchCd > 0.0f) return;
        wrenchCd = cdMax;
    }

    wrenchSwingT     = WRENCH_SWING_TIME;
    wrenchSwingPower = 0.70f + 0.30f * charge;
    wrenchSwingHit   = 0;
    wrenchHitMask = 0;
    PlaySfx(SFX_STEP, 0.50f);
}

/* ---- 每帧推进挥击动画，在命中帧结算 ---- */
void WrenchTick(float dt) {
    extern float wrenchSwingT;
    if (wrenchSwingT <= 0.0f) return;

    wrenchSwingT -= dt;
    if (wrenchSwingT < 0.0f) wrenchSwingT = 0.0f;

    if (WrenchSwingFrame() >= WRENCH_HIT_FRAME) {
        wrenchSwingHit = 1;
        WrenchApplyHit(wrenchSwingPower);
        /* 2026-11-05：宝箱怪**不在 monHunters[] 里**，MonsterCollect 永远扫不到。
           这里单独扫一遍：伪装态被敲 → 直接现形且**不挨咬**。 */
        { extern float RunUpgWrenchRange(void);
          MimicWrenchSweep(posX, posY, dirX >= 0.0f ? 0.0f : 3.14159265f,
                           RunUpgWrenchRange()); }
    }
}

float wrenchCharge = 0.0f;    /* 0..1 */
static int  wrenchPrev = 0;
static int  wrenchInit = 0;
/* ===== v2.5.00 **装填闩** =====
   用户报告：「每次进游戏老是误触消耗一次扳手次数。」

   根因：在主菜单用**鼠标左键**点"新游戏"，进入游戏时左键往往还按着。
   旧代码在 resetInputState 那一帧只是把 wrenchPrev 同步成"已按下"，
   但**接下来它仍然在累积蓄力**，等你松手就判定成一次挥击 ——
   于是每局开场都白扣一次。

   现在加一个闩：reset 之后必须**先松开过一次**，才允许开始蓄力。
   拿鼠标点进游戏 → 松开 → 之后才是真正的一挥。 */
static int  wrenchArmed = 1;

static void WrenchKnockUpdate(float dt) {
    extern int lockActive, isCharging;
    extern float wrenchCd;

    int down = (!lockActive) && (!isCharging) && IsActionDown(ACT_CANCEL_CHARGE);

    if (resetInputState || !wrenchInit) {
        wrenchPrev = down;
        wrenchCharge = 0.0f;
        wrenchArmed = 0;          /* 上闩：先松开才有效 */
        wrenchInit = 1;
    }

    /* 只要处于"没按住"，就把闩打开 */
    if (!down) wrenchArmed = 1;


    if (down && !wrenchPrev) {
        /* 只有上过膛才允许开始蓄力 */
        wrenchCharge = wrenchArmed ? (dt / 0.45f) : 0.0f;
        if (wrenchCharge > 1.0f) wrenchCharge = 1.0f;
    } else if (down) {
        if (wrenchArmed) {
            wrenchCharge += dt / 0.45f;
            if (wrenchCharge > 1.0f) wrenchCharge = 1.0f;
        }
    } else if (!down && wrenchPrev) {
        if (wrenchArmed && wrenchCharge >= WRENCH_CHARGE_DEADZONE) {
            WrenchStartSwing(wrenchCharge);
        }
        wrenchCharge = 0.0f;
    }

    wrenchPrev = down;
}

/* v8：每日“不能开手电”/ SALVAGE keystone 会禁止点灯。
   所有自动点灯路径（摇电、满电重亮、读档）都必须走这里，
   否则玩家可以靠摇电绕过每日限制。 */
static int FlashlightUseAllowed(void) {
    extern int DailyForbidLight(void);
    /* ==================================================================
     * 2026-11-06（**P1-1**）：**SK_SALVAGE 不再永久禁灯**
     * ------------------------------------------------------------------
     * 原来这里是 `!(DailyForbidLight() || HasKeystone(SK_SALVAGE))` ——
     * 于是买过「抢救」的玩家**永远**开不了手电，新开局、教程、
     * 每日挑战一起被禁，而且没有任何办法关掉。
     *
     * 用户拍板的方向：**默认摸黑，但允许主动开**。
     * 所以这里只剩下"每日限制"这一个真正的禁令；
     * SALVAGE 的价值改由「开局默认关灯 + 降噪 25%」承担（见 skills.c）。
     * ================================================================ */
    return !DailyForbidLight();
}

/* 2026-11-05：给 HUD 用 —— 玩家是否真的站在门口。
   安全区（1.4 格）比门口（1.05 格）大，所以「在安全区里但没进门」
   是一段真实存在的状态，提示要能区分这两件事。 */
int PlayerAtExit(void) {
    return (breakerOpen && Dist2(posX, posY, exitX, exitY) < EXIT_ENTER_R2 * EXIT_ENTER_R2);
}

void Update(float dt) {
    { extern float playerMoveSpeedNow; playerMoveSpeedNow = 0.0f; }   /* v8: 守门人判断「在动/冲刺」用 */
    /* v7：可疑点表每帧衰减 / 过期 */
    { extern float gameTime; gameTime += dt; }   /* v2.4.3 全局秒表 */
    SuspicionTick(dt);
    /* v2.4-beta B01：清掉"本帧已见的 eventId"去重表。
       放在 Update 的帧首，和 DailyTick 同一处 —— 两者都是"每帧一次"的簿记。 */
    { extern void SoundEventTick(void); SoundEventTick(); }
    /* v2.4-beta B02：把玩家位姿喂给音频层，世界音效才能算出左右声像。
       每帧一次；音频线程只读快照，不做 BFS、不分配内存。 */
    { extern void AudioUpdateListener(float, float, float, float);
      AudioUpdateListener(posX, posY, dirX, dirY); }
    DailyTick();   /* Calendar rollover also applies while in menus. */
    if(currentState==STATE_PLAYING && exitChoiceOpen && !keyPressed['1'] && !keyPressed['2']) return;
    if(currentState==STATE_PLAYING && !trainingMode) {
        if(monster.active && monster.state==2 && lightBeast.active && lightBeast.state==2) doubleHuntSeconds+=dt;
        else if(doubleHuntSeconds<30.0f) doubleHuntSeconds=0.0f;
        static int hidingHunt=0;
        if(hiddenIn>=0 && (monster.state==2 || lightBeast.state==2)) hidingHunt=1;
        if(hiddenIn<0) hidingHunt=0;
        if(hidingHunt && monster.state!=2 && lightBeast.state!=2) { fullHideDone=1; hidingHunt=0; }
    }
    {   /* v7 B4：第一次携带超过 8000 时提一句 */
        extern void HintOnce(int);
        if (carryGold >= 8000) HintOnce(4);
    }
    if(currentState==STATE_PLAYING && !exitChoiceOpen) { UpdateCollector(dt); }
    {   /* v7：清道夫 */
        extern float floorTime;
        extern void ScavengerTickTrigger(float), UpdateScavenger(float);
        if (currentState == STATE_PLAYING) floorTime += dt;
        if(currentState==STATE_PLAYING && !exitChoiceOpen) { ScavengerTickTrigger(dt); UpdateScavenger(dt); }
    }
    /* v7：扳手冷却递减 */
    { extern float wrenchCd; if (wrenchCd > 0.0f) { wrenchCd -= dt; if (wrenchCd < 0.0f) wrenchCd = 0.0f; } }

    CalibTick(dt);   /* v2.4：校准小游戏计时（冷却与滑块） */
    MimicTick(dt);   /* 2026-11-05 宝箱怪状态机 */
    { extern void EchoMarksTick(float); EchoMarksTick(dt); }   /* 回声标记寿命 */
    /* v2.1 屏幕特效推进：抖动 / 红闪 / 枪口闪 */
    if (hitFlash > 0.0f) { hitFlash -= dt * 3.2f; if (hitFlash < 0.0f) hitFlash = 0.0f; }
    if (muzzleFlash > 0.0f) { muzzleFlash -= dt * 9.0f; if (muzzleFlash < 0.0f) muzzleFlash = 0.0f; }
    if (shakeTime > 0.0f) {
        shakeTime -= dt;
        if (shakeTime <= 0.0f) { shakeTime = 0.0f; shakeMag = 0.0f; }
        float k = (shakeDur > 0.0f) ? (shakeTime / shakeDur) : 0.0f;
        float amp = shakeMag * k * k;              /* 尾巴快速收敛 */
        g_shakeOffX = ((float)(rand() % 2001) - 1000.0f) / 1000.0f * amp;
        g_shakeOffY = ((float)(rand() % 2001) - 1000.0f) / 1000.0f * amp * 0.8f;
    } else {
        g_shakeOffX = 0.0f;
        g_shakeOffY = 0.0f;
    }

    /* v2.9：剑气命中的顿帧 —— 整个逻辑冻结几十毫秒，打击感就出来了 */
    if (slashHitStop > 0.0f) {
        slashHitStop -= dt;
        if (slashHitStop < 0.0f) slashHitStop = 0.0f;
        return;
    }
    if (flashTime > 0.0f) flashTime -= dt;
    if (healFlash > 0.0f) healFlash -= dt;
    /* 全屏地图：游戏**暂停**（专心规划路线）。
       小窗地图：游戏**继续**（边看边玩，鼠标仍能转视角）。 */
    /* ===== v3.3：**看地图要耗电（10/秒）** =====
       用户原话：「开地图每秒钟点 10 点电量怎么不见了？
       我们需要这个来平衡强度啊，不然一直开着地图怎么玩。」

       这是**强度闸门**，不是惩罚：
       · 地图是唯一能规划路线的工具，不设成本就等于「一直开着」
       · 10/秒 意味着开 10 秒 = 一块电池的 1/3，想细看就要付电费
       · 藏身点里开着地图仍然划算（藏身自带 +4/秒 回电，净 -6/秒），
         所以「躲起来慢慢研究路线」依然是正确的解法
       · 电耗尽会自动关图并熄灯，不会被地图耗死

       （v2.4 的注保留：必须在「全屏地图暂停」判断之前 ——
        全屏看图时整个 Update 会提前 return，放后面就永远不扣电。） */
    /* 2026-11-05 用户要求：
       · **大地图不耗电**（全屏规划路线是免费的）
       · 小地图耗电，且可以被局内强化「小地图省电」降低（10 → 4 /秒） */
    if (isMapOpen && !mapFullScreen && currentState == STATE_PLAYING) {
        extern float RunUpgMapDrainPct(void);
        battery -= RunUpgMapDrainPct() * dt;
        if (battery <= 0.0f) {
            battery = 0.0f;
            isMapOpen = 0;
            mapFullScreen = 0;
            /* v8：战役第 1 关的规则是“手电关不掉”，地图耗电把灯关掉
               等于绕过了规则。这里保留开启状态（电量仍是 0，会自然变暗）。 */
            { extern int CampaignFlashlightLocked(void);
              flashlightOn = CampaignFlashlightLocked() ? 1 : 0; }
            ForceCursor(0);
            firstMouseMove = 1;
            uiDirty = 1;
        }
    }

    if (currentState != STATE_PLAYING || (isMapOpen && mapFullScreen)) return;
    if (flashTime > 0.0f) return;

    runTime += dt;
    if (levelBanner > 0.0f) levelBanner -= dt;

    /* 成就检查 */
    CheckAchievements();

    /* 状态效果计时。强度变化时重算 FOV（视野收窄要立刻生效） */
    if (UpdateFx(dt)) ApplyFov();

    /* v2.5：安全屋判定必须**在理智流失之前**算出来。
       原来这段在后面，于是安全屋里的基础流失照扣，而回智又被
       SAFE_HEAL_CAP(60) 挡住 —— 理智 70 走进安全屋会看到理智继续下降。 */
    int inSafe = (safeRoom.active &&
                  Dist2(posX, posY, safeRoom.x, safeRoom.y) < SAFE_ZONE_RADIUS * SAFE_ZONE_RADIUS);

    /* ===== v2.4.98 **出口也算安全区** =====
       用户报告的 bug：
       「我刚好到撤离点，同时清道夫刷新，我再撤一点没选继续/撤离，
         清道夫追上把我杀了，直接死亡。」

       根因：出口原来只是"按到 0.55 格内弹面板"，**不提供任何保护**。
       怪照追、照抓、照扣理智；理智一归零就直接判死。

       现在：
         · 距出口 1.4 格内 → 算安全区（怪不抓你）
         · 撤离选择面板打开期间 → **无条件安全** */
    {
        extern int exitChoiceOpen, breakerOpen;
        if (breakerOpen && Dist2(posX, posY, exitX, exitY) < 1.4f * 1.4f) inSafe = 1;
        if (exitChoiceOpen) inSafe = 1;
    }


    /* v3.0：**开锁盘时暂停理智流失** ——
       用户实测「开锁装置导致精神崩溃」：
       锁盘是模态层，玩家不能动也不能跑，理智却照扣。
       解一把弦线锁要 30~60 秒，按第 1 层 0.5/秒算是 15~30 点理智，
       到了第 10 层就是 30~60 点 —— 解谜变成自杀。
       现在把开锁当作和暂停同等的状态：理智冻结。 */
    /* 2026-11-05：过载 / 锯锁是**按住不放**的，需要逐帧推进。
       必须在下面的 if (lockActive) return 之前调用，否则永远不动。 */
    { extern void LockTick(float); LockTick(dt); }
    if (lockActive) return;

    /* v3.3 **藏身回电** ——
       用户要求：「在隐藏点的时候，手电自动充电」。
       躲在藏身点里、且手电关着时回电最快；
       开着手电也回（毕竟在充电），但慢一半 —— 这样「藏起来充电」
       和「开着灯继续看」之间有取舍。 */
    if (hiddenIn >= 0) {
        float regen = HIDE_BATTERY_REGEN * (flashlightOn ? 0.5f : 1.0f);
        battery += regen * dt;
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
    }

    /* 理智流失：随层数增长（v1.8），代谢 keystone 把它换成电量消耗 */
    float drainMul = 1.0f + FxStrength(FX_DRAIN) * 1.5f;
    float drain = SanityDrainRate() * SkillCalmMul() * modSanityMul * drainMul * RelicSanityMul() * BenchSanityDrainMul();
    /* v3.0：**被追捕者盯上时理智额外流失** ——
       这给「断开视线」一个具体理由，而藏身是最快的断开方式。
       藏着时不算被盯上（hiddenIn >= 0 直接跳过）。 */
        /* v3.0：冷血（局内 COLD BLOOD）—— 理智流失 -20% */
    drain *= ColdBloodMul();
    if (hiddenIn < 0 && monster.active && monster.state == 1) {
        float sdx = posX - monster.x, sdy = posY - monster.y;
        float sightNow = flashlightOn ? MONSTER_SIGHT : MONSTER_SIGHT_DARK;
        if (sdx * sdx + sdy * sdy < sightNow * sightNow) drain += SEEN_SANITY_EXTRA;
    }
    /* v3.0：改造树「镇定」—— 关灯时恐惧增长 -25% */
    if (!flashlightOn) drain *= BenchDarkFearMul();
    if (HasKeystone(SK_METAB)) {
        battery -= drain * META_COST * dt;
        if (battery <= 0.0f) {          /* 断电：理智开始双倍流失 */
            battery = 0.0f;
            sanity -= drain * META_STARVE_MUL * dt;
        }
    } else {
        float siphonSan = 1.0f;
        if (SkillPwrSiphonActive() && battery < (float)BatteryMax() * 0.25f)
            siphonSan = SkillPwrSiphonSanityMul();
        /* v2.2：LAST RESORT —— 理智归零时免死一次，回 15 点并给 4 秒。
           这个 keystone 之前是纯空函数（1200 金币 + 2 核心买了个寂寞）。 */
        if (!lastResortUsed && SkillLastResortOneshot()) {
            lastResortUsed = 1;
            sanity = 15.0f;
            pickupMsg = "LAST RESORT";
            pickupMsgTimer = 2.0f;
            uiDirty = 1;
            return;
        }
        /* v2.5：安全屋里不流失 —— 这是"安全"二字的承诺 */
        if (!inSafe) sanity -= drain * siphonSan * dt;
    }
    /* v3.3：躲进垃圾桶后缓慢回理智；净恢复约 1.5/秒 */
    if (hiddenIn >= 0) {
        sanity += HIDE_SANITY_REGEN * dt;
        if (sanity > SanityMax()) sanity = SanityMax();
    }
    lowSanitySeconds += (sanity <= 1.0f) ? dt : 0.0f;   /* v2.2：LAST BREATH */
    /* v2.4.98：撤离面板开着的时候不许判死 ——
       你已经在"要不要走"这一步了，此时被后面的怪撞死是 bug 不是难度。 */
    {
        extern int exitChoiceOpen;
        if (exitChoiceOpen && sanity < 1.0f) sanity = 1.0f;
    }
    if (sanity <= 0.0f) {
        sanity = 0.0f;
        runInProgress = 0;
        if (currentLevel > bestLevel) bestLevel = currentLevel;
        /* v2.0 SALVAGE：死亡时保留一半待上缴金币 */
    /* v6：这里原本是一加一减两个空操作（净效果 0），
       是合并货币时留下的。已删除。
       死亡时的保留比例统一由 WalletSettle 的 BANK_DEATH_REFUND 处理。 */
    gameWon = 0;
        BankSettle(0);          /* v3.4：死亡结算 —— 本金返还 50% */
        /* v6 **死亡后不允许 Continue 复活** ——
           原来 hasSave 死亡后仍是 1，回主菜单点 Continue 能把死前存档救回来，
           死亡被完全撤销（银行还多扣一次死亡结算）。 */
        hasSave = 0;
        bankStreak = 0;
        {
            ScoreInput si = { currentLevel, 0, isEndlessRun, runSearched,
                              bottlesKilled, runTime, modsSeenCount };
            ComputeScore(&si, &lastScore);
            if (isEndlessRun && lastScore.score > bestEndlessScore) bestEndlessScore = lastScore.score;
            if (!isEndlessRun && lastScore.score > bestMainScore) bestMainScore = lastScore.score;
        }
        currentState = STATE_GAMEOVER;
        menuSelection = 0;
        ForceCursor(1);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        uiDirty = 1;
        return;
    }

    /* 手电筒耗电：电量耗尽自动熄灭（ECO 升级降低耗电） */
    /* POWER SIPHON：低电涓流回电。必须放在 if(flashlightOn) 外面 ——
       原来被包在手电分支里，实际效果是「电量见底 → 手电自动熄灭 → 再也回不来」，
       偏偏这个技能就是为电量见底准备的。 */
    if (SkillPwrSiphonActive()) {
        float cap25 = (float)BatteryMax() * 0.25f;
        if (battery < cap25) {
            battery += (float)SkillPwrSiphonAdd() * dt;
            if (battery > cap25) battery = cap25;
        }
    }

    if (flashlightOn) {
        if(!CampaignFlashlightLocked()) battery -= BatteryDrainRate() * RunBatteryDrainMul() * SkillGhostStepDrainMul() * dt;
        if (battery <= 0.0f) { battery = 0.0f; flashlightOn = CampaignFlashlightLocked() ? 1 : 0; }
    }

    /* 先判断交互目标，决定这一帧能不能移动 */
    /* ===== v3.3 交互目标优先级重排 =====
       试玩实测卡关：「站在带锁箱子前时配电箱就在正前方，
       长按 E 永远命中箱子（反复弹锁盘），我始终没法给配电箱通电，
       也就无法通关第一层。」

       根因：锁盘是**模态层**，一旦打开就得先解完/退出来，
       而它和配电箱抢同一个 E —— 谁近谁赢，锁箱经常更近。

       现在的优先级（从高到低）：
         ① 配电箱   —— 直接推进关卡，必须永远能选到
         ② 未上锁的箱子 —— 翻一下就完事，无副作用
         ③ 上锁的箱子   —— 会把玩家拖进模态层，优先级最低
       这样「站在锁箱前也能给配电箱通电」，卡关消失。 */
    hoverKind = 0;
    hoverIndex = -1;

    if (!breakerOpen && breaker.active) {
        float d = Dist2(posX, posY, breaker.x, breaker.y);
        if (d < INTERACT_RANGE * INTERACT_RANGE) {
            hoverKind = 2;
            hoverIndex = -1;
        }
    }

    if (hoverKind == 0) {
        float bestD = INTERACT_RANGE * INTERACT_RANGE;
        int bestLocked = -1;      /* 锁箱候选，先记下来垫底 */
        float bestLockedD = 0.0f;
        for (int i = 0; i < containerCount; i++) {
            if (!containers[i].active || containers[i].searched) continue;
            float d = Dist2(posX, posY, containers[i].x, containers[i].y);
            if (d >= INTERACT_RANGE * INTERACT_RANGE) continue;
            if (containers[i].locked) {
                if (bestLocked < 0 || d < bestLockedD) { bestLocked = i; bestLockedD = d; }
            } else if (d < bestD) {
                bestD = d; hoverKind = 1; hoverIndex = i;
            }
        }
        /* 没有未上锁的箱子、但有锁箱 → 才轮到锁箱 */
        if (hoverKind == 0 && bestLocked >= 0) {
            hoverKind = 1;
            hoverIndex = bestLocked;
        }
    }

    /* ==================================================================
     * 2026-11-06：**家具也可以搜**
     * ------------------------------------------------------------------
     * 用户提议：「把它做成一个箱子，特殊箱子可以搜查两次，
     *           或者那个货架可以搜查三次。」
     *
     * 这也是这次"精灵过多导致掉帧"的补偿：每个家具只画 1 个精灵，
     * 但内容量由可搜查次数承担 ——
     *   货架 3 次 / 木箱堆 2 次 / 控制台 2 次
     *
     * 优先级：**永远排在容器之后**，家具不许抢箱子的 E。
     * ================================================================ */
    /* 2026-11-06：舱门（主题控制器）—— **排在最后**，不抢箱子和家具的 E */
    if (hoverKind == 0) {
        int hi = HatchNearest(posX, posY, INTERACT_RANGE * INTERACT_RANGE);
        if (hi >= 0 && hatches[hi].state == HATCH_SEALED) {
            hoverKind = 4;
            hoverIndex = hi;
        }
    }

    if (hoverKind == 0) {
        extern RoomProp roomProps[MAX_ROOM_PROPS];
        extern int roomPropCount;
        float bestD = INTERACT_RANGE * INTERACT_RANGE;
        for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++) {
            if (!roomProps[i].active || roomProps[i].searchLeft <= 0) continue;
            float d = Dist2(posX, posY, roomProps[i].x, roomProps[i].y);
            if (d >= INTERACT_RANGE * INTERACT_RANGE) continue;
            if (d < bestD) { bestD = d; hoverKind = 3; hoverIndex = i; }
        }
    }


    /* v3.1：藏身提示范围从 2.6 格收到 1.2 格。
       原来是一个直径 5 格的圆，站在房间中间就提示「按 E 躲藏」，
       玩家会以为整片空地都能藏；其实藏身点是一个具体位置。 */
    hideSpotNear = -1;

    /* v3.0：上锁的箱子**按一下 E 就弹锁盘**，不走长按翻箱；
       没锁的仍然长按搜索。 */
    if (hoverKind == 1 && hoverIndex >= 0 && hoverIndex < MAX_CONTAINERS &&
        containers[hoverIndex].active && !containers[hoverIndex].searched &&
        containers[hoverIndex].locked && !lockActive) {
        if (IsActionPressed(ACT_INTERACT)) {
            LockOpenFor(hoverIndex, containers[hoverIndex].lockType);
            keyPressed[g_bindings[ACT_INTERACT]] = 0;
            return;
        }
        holdingNow = 0;      /* 上锁时按住 E 不翻箱 */
    } else {
        /* v3.0：藏身点 —— 找最近的一个，按 E 进出 */
    {
        float bestHD = 1.2f * 1.2f;
        for (int i = 0; i < hideSpotCount; i++) {
            if (!hideSpots[i].active) continue;
            float dx = hideSpots[i].x - posX, dy = hideSpots[i].y - posY;
            float d = dx * dx + dy * dy;
            if (d >= bestHD) continue;
            /* 只提示“看得见的垃圾桶”：
               ① 必须在视线前方（背后的桶不提示，避免走在空地上突然冒出 HIDE）
               ② 玩家到桶之间不能隔墙
               ③ 距离 1.2 格以内 */
            if (dx * dirX + dy * dirY < 0.35f) continue;
            {
                int steps = (int)(sqrtf(d) * 10.0f) + 1;
                int blocked = 0;
                for (int s = 1; s < steps; s++) {
                    float t = (float)s / (float)steps;
                    int cx = (int)floorf(posX + dx * t);
                    int cy = (int)floorf(posY + dy * t);
                    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT ||
                        worldMap[cy][cx]) { blocked = 1; break; }
                }
                if (blocked) continue;
            }
            bestHD = d;
            hideSpotNear = i;
        }
        /* v3.0：**E 必须松开再按才算下一次** ——
           按住 0.5 秒时 WM_KEYDOWN 会自动重复，每次重复都置 keyPressed，
           于是「出来」刚执行完又被「进去」接上，净效果是 hiddenIn 恒为 0
           （验证报告 BUG2）。加一个锁存：只有键松开过才允许再次切换。 */
        {
            static int hideKeyLatch = 0;
            if (!IsActionDown(ACT_INTERACT)) hideKeyLatch = 0;
            if (IsActionPressed(ACT_INTERACT) && hideKeyLatch) {
                keyPressed[g_bindings[ACT_INTERACT]] = 0;
                /* 吞掉这次重复，不切换 */
                goto hide_done;
            }

        /* 在藏身点里再按 E → 出来 */
        if (hiddenIn >= 0) {
            if (IsActionPressed(ACT_INTERACT)) {
                hideKeyLatch = 1;
                hiddenIn = -1;
                pickupMsg = g_lang == LANG_ZH ? "出来了" : "YOU LEAVE THE COVER";
                pickupMsgTimer = 1.0f;
                keyPressed[g_bindings[ACT_INTERACT]] = 0;
                uiDirty = 1;
            }
        } else if (hideSpotNear >= 0 && hideSpots[hideSpotNear].broken) {
                    /* v6 每日限制：不能用藏身点 */
                    { extern int DailyForbidHide(void);
                      if (DailyForbidHide()) { pickupMsg = "TODAY: NO HIDE SPOTS"; pickupMsgTimer = 1.6f; hideKeyLatch = 1; } }
            /* ===== v2.4.17 **修复藏身点** =====
               清道夫撬坏的点按 E 花钱修。每修一次更贵。 */
            extern int hideRepairCount;
            float cost = HIDESPOT_REPAIR_BASE;
            for (int rc = 0; rc < hideRepairCount; rc++) cost *= HIDESPOT_REPAIR_STEP;
            if (cost > 99999.0f) cost = 99999.0f;

            if (IsActionPressed(ACT_INTERACT)) {
                hideKeyLatch = 1;
                keyPressed[g_bindings[ACT_INTERACT]] = 0;
                if (carryGold >= cost && hideRepairCount < HIDESPOT_REPAIR_MAX) {
                    { extern int WalletSpendRun(int); WalletSpendRun(cost); }
                    hideSpots[hideSpotNear].broken = 0;
                    hideRepairCount++;
                    pickupMsg = (g_lang == LANG_ZH) ? "藏身点修好了" : "HIDE SPOT REPAIRED";
                    pickupMsgTimer = 1.6f;
                    PlaySfx(SFX_GLASS, 0.6f);
                } else if (hideRepairCount >= HIDESPOT_REPAIR_MAX) {
                    pickupMsg = (g_lang == LANG_ZH) ? "修不了了（太破了）" : "TOO BROKEN TO REPAIR";
                    pickupMsgTimer = 1.6f;
                } else {
                    pickupMsg = (g_lang == LANG_ZH) ? "金币不够修这个" : "NOT ENOUGH GOLD";
                    pickupMsgTimer = 1.6f;
                }
                uiDirty = 1;
            }
        } else if (hideSpotNear >= 0 && hoverKind == 0 && hideBlockTimer <= 0.0f) {
            /* v3.0 修复：**附近有可交互的箱子时，藏身点不抢 E** ——
               原来藏身点无条件抢在容器前面消费按键
               （用户实测：箱子和躲藏点刷在一起时箱子开不了）。 */
            if (IsActionPressed(ACT_INTERACT)) {
                hideKeyLatch = 1;
                hiddenIn = hideSpotNear;
        { extern void HintOnce(int); HintOnce(1); }   /* v7 B4 */
        { extern int hideStreak; hideStreak++; }   /* v7：清道夫触发条件 */
                hideCount++;   /* v3.3：逃生向成就用 */
                posX = hideSpots[hiddenIn].x;
                posY = hideSpots[hiddenIn].y;

                /* v3.0：**当着他的面钻进去 = 他会来翻** ——
                   原来钻进柜子怪物 100% 立刻丢目标，太安全了。
                   如果它此刻正在追你（state==1）而且离得近（6 格内），
                   它会跑到柜子前徘徊一阵，而不是当场忘掉你。 */
                if (monster.active) {
                    float hdx = monster.x - posX, hdy = monster.y - posY;
                    int sawYou = (monster.state == 1) && (hdx * hdx + hdy * hdy < 36.0f);
                    monster.state = sawYou ? 2 : 0;      /* 2 = 原地搜寻 */
                    monster.tx = hideSpots[hiddenIn].x;
                    monster.ty = hideSpots[hiddenIn].y;
                    monster.stateTimer = sawYou ? 5.0f : 0.0f;
                    monster.repath = 0.0f;
                    if (sawYou) {
                        pickupMsg = g_lang == LANG_ZH ? "他看见你进来了" : "IT SAW YOU HIDE";
                        pickupMsgTimer = 1.8f;
                    }
                }
                pickupMsg = g_lang == LANG_ZH ? "藏好了  （再按 E 出来）" : "HIDDEN  (E TO LEAVE)";
                pickupMsgTimer = 1.6f;
                keyPressed[g_bindings[ACT_INTERACT]] = 0;
                uiDirty = 1;
            }
        }
        hide_done: ;
        }
    }

    holdingNow = (hoverKind != 0 && IsActionDown(ACT_INTERACT));
    }

    /* ===== v3.0：主动大招（Q 键） =====
       原来 runUltimate 只被存读档，**从来没有激活入口** ——
       三选一拿到的大招等于永远用不出来。现在按 Q 释放，一局一次。 */
    if (ultTimer > 0.0f) ultTimer -= dt;
    /* v3.3：**B 键开/关训练营的分步引导** ——
       进 Tutorial 时默认不展开，只提示「按 B 激活引导」。 */
    /* v3.3：**T 在不能开店的地方也要给一句反馈** ——
        v6 **出口二选一**
       1 = 撤离（立刻按 携带倍数 × 深度倍数 结算，本局结束）
       2 = 深入（继续下一层）
       ESC 不生效 —— 必须选一个。 */
    if (exitChoiceOpen && currentState == STATE_PLAYING) {
        if (keyPressed['1']) {
            keyPressed['1'] = 0;
            {
                extern float ExtractDepthMul(int level);
                float dm = ExtractDepthMul(currentLevel);
                /* v7 第 ㊿ 轮：**这里原来自己减自己**
                   profit = carryGold - carry，而 carry 就是 carryGold
                   → 恒 0 → 深度倍数一分不加 → 「越深撤离越值钱」不存在。 */
                int profit = carryGold - carryAtRunStart;
                if (profit < 0) profit = 0;
                int extra=0; /* Depth and contract are applied once by BankSettle. */
                carryGold += extra;
                exitChoiceOpen = 0;
                lastExtractDepthMul = dm;
                /* v6 **必须先清 runInProgress** —— BankSettle 内部会 SaveGame，
                   原来在它之后才清零，存出一份「钱已进银行、人还站在出口」
                   的僵尸档，下次启动会出现假的 Continue。 */
                runInProgress = 0;
                dailyEscapeCount++; DailyObserve(DQ_ESCAPE,dailyEscapeCount);
                gameWon=1; gameCleared=1; CheckAchievements();
                BankSettle(1); hasSave=0;
            }
            gameWon = 1;
            currentState = STATE_GAMEOVER;
            menuSelection = 0;
            ForceCursor(1);
            uiDirty = 1;
            return;
        }
        if (keyPressed['2']) {
            keyPressed['2'] = 0;
            exitChoiceOpen = 0;
            exitGoDeeper = 1;      /* v6：下一帧走「换下一层」那段 */
            uiDirty = 1;
            return;
        }
        return;   /* 弹窗期间冻结其它一切操作 */
    }


    /* v3.3：T 键在不能开店的地方也要给一句反馈 ——
       试玩实测：「T 在走廊里按没反应，也没有『此处不能开商店』的提示」。
       静默是最糟糕的反馈，玩家会以为按键坏了。 */
    /* v7 第 52 轮：**这条拦截删掉了**
       它用的是 playerInSafe —— 那个变量在下面才更新，
       所以玩家刚走进安全屋的那一帧，值还是 false，
       T 被这里吃掉并弹一句「商店只在安全屋」，
       而真正的开店代码在后面，永远收不到这个键。
       （这就是「按 T 什么都没发生」的直接原因。）

       而且现在商店是浮层，不再依赖安全屋状态机，
       所以干脆允许**任何时候**按 T 打开。 */






    if (currentState == STATE_PLAYING && trainingMode && keyPressed['B']) {
        keyPressed['B'] = 0;
        extern int tutorialGuideOn;
        tutorialGuideOn = !tutorialGuideOn;
        pickupMsg = tutorialGuideOn
            ? (g_lang == LANG_ZH ? "引导已展开" : "GUIDE ON")
            : (g_lang == LANG_ZH ? "引导已收起" : "GUIDE OFF");
        pickupMsgTimer = 1.2f;
        uiDirty = 1;
    }

    /* v3.3：**C 键激活诱饵**（右键负责放置，C 负责激活） */
    if (currentState == STATE_PLAYING && keyPressed['C']) {
        keyPressed['C'] = 0;
        int any = 0;
        for (int i = 0; i < MAX_DECOY; i++)
            if (decoysArr[i].active && !decoysArr[i].armed) { any = 1; break; }
        if (any) {
            ArmDecoy();
            /* ===== v2.4.63 **不要写死秒数** =====
               原来文案写死「每 3 秒」，后来把 DECOY_SHOUT_EVERY 改成 1.5 之后
               这句话就成了错的（用户实机看到的就是它）。
               现在直接从常量生成，改参数文案自动跟着变。 */
            {
                static char decoyMsg[64];
                snprintf(decoyMsg, sizeof(decoyMsg),
                         (g_lang == LANG_ZH) ? "诱饵已激活 - 每 %.1f 秒叫一次"
                                             : "DECOY ARMED - SHOUTS EVERY %.1fs",
                         (double)DECOY_SHOUT_EVERY);
                pickupMsg = decoyMsg;
            }
            pickupMsgTimer = 2.0f;
        }
    }

    if (runUltimate >= 0 && !ultUsed && currentState == STATE_PLAYING
        && IsActionPressed(ACT_SWING)) {

        Monster* targets[MON_HUNTER_MAX + 1];
        int nTargets = MonsterCollect(targets, MON_HUNTER_MAX + 1, 1);
        ultUsed = 1;
        ultKind = runUltimate;
        keyPressed[g_bindings[ACT_SWING]] = 0;
        uiDirty = 1;

        /* 按名字判断是哪一个（inRunDefs 的顺序：WHISTLE / SMOKE OUT / ... / BLACKOUT） */
        const char* nm = inRunDefs[runUltimate].name;
        if (strstr(nm, "BLACKOUT")) {
            ultTimer = ULT_BLACKOUT_TIME;
            pickupMsg = g_lang == LANG_ZH ? "全黑  10 秒" : "BLACKOUT  10s";
            PlaySfx(SFX_POWER, 1.0f);
        } else if (strstr(nm, "WHISTLE")) {
            ultTimer = ULT_WHISTLE_TIME;
            /* 把所有追捕者引到远处一个点 */
            float fx = posX, fy = posY;
            for (int k = 0; k < 12; k++) {
                fx = 2.0f + (float)(rand() % (MAP_WIDTH - 4));
                fy = 2.0f + (float)(rand() % (MAP_HEIGHT - 4));
                float ddx = fx - posX, ddy = fy - posY;
                if (ddx * ddx + ddy * ddy > 144.0f) break;   /* 至少 12 格远 */
            }
            for (int k = 0; k < nTargets; k++) {
                targets[k]->state = 1;
                targets[k]->tx = fx;
                targets[k]->ty = fy;
                targets[k]->repath = 0.0f;
                targets[k]->stateTimer = 0.0f;
            }
            pickupMsg = g_lang == LANG_ZH ? "哨声  它们被引走了" : "WHISTLE  THEY LEFT";
            PlaySfx(SFX_CRANK, 1.0f);
        } else {
            /* SMOKE OUT：立刻断开所有视线 */
            ultTimer = 0.01f;
            for (int k = 0; k < 2; k++) {
                if (!targets[k]->active) continue;
                targets[k]->state = 0;
                targets[k]->pathLen = 0;
                targets[k]->pathIdx = 0;
                targets[k]->sightHold = 0.0f;
            }
            pickupMsg = g_lang == LANG_ZH ? "烟幕  视线已断" : "SMOKE OUT";
            PlaySfx(SFX_GLASS, 0.9f);
        }
        pickupMsgTimer = 2.0f;
    }

    UpdateTimers(dt);   /* v8: 回声扫描 / 锁定窗口 / 周期自动存档（src/uptimers.c） */
    /* v3.0 改造树「消失」：脱离视线 3 秒后完全隐身。
       判据用「怪物是否在追捕状态」——它没在追（游荡/搜寻）就算脱离视线。 */
    if (BenchVanish()) {
        /* v3.0 修正：**隐身必须有持续时间，且"被发现"不能看 monster.state**。
           原来用 state==1 判发现，而隐身时 MakeNoise 直接 return，
           state 永远回不到 1 → vanished 自锁成永久隐身，追捕者整局停摆。
           现在改判「怪物离玩家多近」：5 格内算被发现，立刻破隐。 */
        float d2v = 1e9f;
        if (monster.active) {
            float dxv = monster.x - posX, dyv = monster.y - posY;
            d2v = dxv * dxv + dyv * dyv;
        }
        int seen = (d2v < 25.0f);        /* 5 格内 */

        if (vanished) {
            vanishHold -= dt;
            if (seen || vanishHold <= 0.0f) {
                vanished = 0;
                vanishHold = 0.0f;
                vanishTimer = 0.0f;      /* 破隐后要重新脱战 3 秒 */
                if (seen) {
                    pickupMsg = g_lang == LANG_ZH ? "隐身被打断" : "REVEALED";
                    pickupMsgTimer = 1.2f;
                }
                uiDirty = 1;
            }
        } else if (seen) {
            vanishTimer = 0.0f;
        } else {
            vanishTimer += dt;
            /* v3.0：SLIP AWAY 把这个时间从 3 秒降到 2 秒 */
            if (vanishTimer >= SlipAwaySeconds()) {
                vanished = 1;
                vanishHold = 6.0f;       /* 隐身 6 秒 */
                pickupMsg = g_lang == LANG_ZH ? "已隐身 6 秒" : "VANISHED 6s";
                pickupMsgTimer = 1.4f;
                uiDirty = 1;
            }
        }
    } else {
        vanishTimer = 0.0f;
        vanished = 0;
        vanishHold = 0.0f;
    }

    int wantSprint = IsActionDown(ACT_SPRINT);
    /* v3.0 改造树「二次呼吸」：体力归零后还能再冲 3 秒 */
    if (BenchSecondWind() || HasKeystone(SK_LASTRESORT)) {
        if (stamina <= 0.0f && secondWindTimer <= 0.0f && wantSprint && secondWindTimer > -99.0f) {
            /* v3.0：**体力回满就恢复一次** ——
               原来 static swUsed 从不重置，整局只能用一次
               （验证报告：第二次起 secondWindTimer 恒 0）。 */
            static int swUsed = 0;
            if (stamina > StaminaMax() * 0.5f) swUsed = 0;
            if (!swUsed) { secondWindTimer = TUNE_SECOND_WIND_SECONDS; swUsed = 1; }
        }
        if (stamina <= 0.0f && secondWindTimer > 0.0f) {
            wantSprint = 1;          /* 宽限期内允许冲刺 */
            secondWindTimer -= dt;
        }
        if (secondWindTimer < -98.0f) secondWindTimer = 0.0f;
    }
    int inF = IsActionDown(ACT_MOVE_FWD) - IsActionDown(ACT_MOVE_BACK);
    int inS = IsActionDown(ACT_MOVE_RIGHT) - IsActionDown(ACT_MOVE_LEFT);
    /* ==================================================================
     * 2026-11-06（用户要求）：**被爆脸怪抓住后 10 秒动不了**
     * 在读完输入之后直接清零，比在每个调用点加判断安全 ——
     * 后面还有冲刺/贴墙滑动等多条路径，漏一处就能溜。
     * ================================================================ */
    {
        extern float jumperControlT;
        if (jumperControlT > 0.0f) { inF = 0; inS = 0; }
    }
    /* v3.0：藏在柜子里不能动（但可以转头偷看） */
    if (hiddenIn >= 0) { inF = 0; inS = 0; }
    int moving = (inF != 0 || inS != 0);
    /* v2.1：走路相位，手持模型和准星都跟着动 */
    bobPhase += dt * (sprinting ? 11.0f : (moving ? 7.0f : 1.6f));
    if (bobPhase > 6283.0f) bobPhase -= 6283.0f;

    /* v3.0：二次呼吸宽限期内，体力为 0 也允许冲刺 */
    sprinting = (wantSprint && moving && (stamina > 1.0f || secondWindTimer > 0.0f));
    if (sprinting) {
        sprintedThisLevel = 1;  /* 标记本局冲刺过 */
        sprintedAnyFloor = 1;   /* v3.3：跨层不清零，SILENT SHADOW 用 */
        stamina -= STAMINA_DRAIN * BenchSprintCostMul() * RunUpgStaminaMul() * dt;   /* v3.0 体能强化 */
        if (stamina < 0.0f) stamina = 0.0f;
        staminaIdle = 0.5f;
    } else if (staminaIdle > 0.0f) {
        staminaIdle -= dt;
    } else if (stamina < StaminaMax()) {
        /* v3.0：藏在藏身点里体力回得更快（×3）—— 这是藏身点的主要收益 */
    stamina += STAMINA_REGEN * dt * ((hiddenIn >= 0) ? HIDE_STAMINA_REGEN_MUL : 1.0f);
        if (stamina > StaminaMax()) stamina = StaminaMax();
    }

    /* 带半径的碰撞：不再用「点」判定，避免贴墙时半个身子钻进墙里 */
    /* 安全屋：站在里面回理智 */
    playerInSafe = inSafe;   /* v2.5：inSafe 已在前面算过 */
    /* v2.5：站在安全区里按**空格**直接玩校准小游戏。
       原来必须先按 E 开面板、再在面板里按空格，多绕一步；
       用户要求"进安全区屏幕上出现一行字，按空格开始小游戏"。 */
    /* v3.0：**藏进藏身点也能校准** ——
       用户要求把校准系统移进藏身点，冷却 60 秒，主要奖励改成理智。
       这样柜子/床底从「单纯躲避」变成「喘息 + 回复」，才有存在感。 */
    if ((inSafe || hiddenIn >= 0) && currentState == STATE_PLAYING) {
        int spaceDown = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        if (keyPressed[VK_SPACE]) {
            keyPressed[VK_SPACE] = 0;   /* 吃掉这一次，别再触发闪避 */
            if (!calibStartLatch) {
                calibStartLatch = 1;
                if (calibActive) CalibLock();
                else             CalibStart();
            }
        }
        if (!spaceDown) calibStartLatch = 0;
    }

    /* v2.6：**进安全屋商店的热键改成 T**（用户要求）。
       原来用的是 ACT_INTERACT（默认 E），但 E 同时是「翻箱搜索」，
       站在安全屋里按 E 会让人搞不清是要搜箱子还是开店。
       E 保留给搜索，安全屋专用 T。 */
    /* v3.0：**藏身点里也能开商店** —— 用户要求「同时也可以打开商店面板」 */
    /* ==================================================================
     * v7 第 ㊿ 轮：**T 也走 GetAsyncKeyState 兜底**
     * ==================================================================
     * WM_KEYDOWN 只在窗口有焦点时才来。GDI 窗口被别的程序抢焦点时，
     * 玩家按 T 完全没反应 —— 而且这也让自动化测试无法验证这个功能。
     *
     * 这里自己做个 0→1 边沿检测，和 WM 那条互为兜底。
     * ================================================================ */
    {
        static int tPrev = 0;
        extern int shopOverlayOpen;
        int tNow = (GetAsyncKeyState('T') & 0x8000) ? 1 : 0;
        if (tNow && !tPrev && currentState == STATE_PLAYING && !shopOverlayOpen) {
            extern void ShopOpen(int);
            ShopOpen(currentLevel);
            currentState = STATE_SAFEHOUSE;
            shopPage = 0;
            shopRow = 0;
            ForceCursor(1);
            uiDirty = 1;
        }
        tPrev = tNow;
    }

    if (currentState == STATE_PLAYING && keyPressed['T']) {   /* v7 第 52 轮：随时可开 */
        keyPressed['T'] = 0;
        /* v7 第 51 轮：**开浮层，不切状态**
           STATE_SAFEHOUSE 那条路径一进就崩（已用 demo4 复现），
           而浮层不动 currentState，绕开了整条坏路径。 */
        {
            extern int shopOverlayOpen;
            extern void ShopOpen(int);
            ShopOpen(currentLevel);
            shopOverlayOpen = 1;
            ForceCursor(1);
            uiDirty = 1;
        }
        return;
        /* v2.4：次数改由 CalibTick 按层发放，这里不再重置 */
        shopPage = 0;
        shopRow = 0;
        ForceCursor(1);
        uiDirty = 1;
        return;
    }
    if (inSafe) {
        /* 进安全屋 = 脱离：怪物立刻丢失目标、回到游荡，也不再抓人 */
        if (monster.active) { monster.state = 0; monster.pathLen = 0; monster.pathIdx = 0; }
    }
    if (inSafe) {
        /* v2.0：安全屋不再是免费满血点 —— 回智封顶 SAFE_HEAL_CAP（60），
           再往上要靠药箱/技能/遗物。原来 3/s 回满 + 8 个安全屋，
           让理智根本不像计时器（审计实测一局净 +187）。 */
        if (sanity < SAFE_HEAL_CAP) {
            /* v6 第 ㉟ 轮：**回神**（第 3 列第二顶点）安全屋回复 +60% */
    {
        extern int BenchRecover(void);
        float rm=BenchHealMul();
        sanity += SafeHealRate() * RunSanityGainMul() * rm * dt;
    }
            if (sanity > SAFE_HEAL_CAP) sanity = SAFE_HEAL_CAP;
        }
        if (sanity > SanityMax()) sanity = SanityMax();
        /* 「SAFE ZONE」提示只闪一下：刚进来时给 2 秒，之后不常驻。
           （玩家在安全屋里本来就知道自己在那，不需要一直盯着这行字。） */
        if (safeZoneMsg <= 0.0f) safeZoneMsg = 2.0f;
    }
    if (safeZoneMsg > 0.0f) {
        safeZoneMsg -= dt;
        if (safeZoneMsg < 0.0f) safeZoneMsg = 0.0f;
    }

    /* ---- v2.9 绣刀：发射剑气（穿透、14 格射程），60 秒冷却，命中不再减半 ---- */
    UpdateExtras(dt);      /* v2.0：额外威胁 */

    /* v2.0：燃烧瓶火区 —— 每 0.6 秒烧一次站在里面的怪 */
    for (int f = 0; f < 3; f++) {
        if (fireT[f] <= 0.0f) continue;
        fireT[f] -= dt;
        fireTick[f] -= dt;
        if (fireTick[f] <= 0.0f) {
            fireTick[f] = 0.6f;
            float d2m = Dist2(fireX[f], fireY[f], monster.x, monster.y);
            if (monster.active && d2m < 1.6f) ApplyDamage(&monster, 6, DT_SHARD, fireX[f], fireY[f]);
            float d2l = Dist2(fireX[f], fireY[f], lightBeast.x, lightBeast.y);
            if (lightBeast.active && d2l < 1.6f) ApplyDamage(&lightBeast, 2, DT_SHARD, fireX[f], fireY[f]);
            SpawnParticles(fireX[f], fireY[f], 1);
        }
        if (fireT[f] < 0.0f) fireT[f] = 0.0f;
    }
    if (knifeCd > 0.0f) {
        knifeCd -= dt;
        if (knifeCd < 0.0f) knifeCd = 0.0f;
    }
    /* v2.9：RUST KNIFE 改成**发射一道剑气**向前飞，而不是原地挥砍。
       剑气一路穿透，碰到墙或飞满 SLASH_MAXDIST 才消失。 */
    /* v2.0：RUST KNIFE 已删除（剑气起手代码随之移除，只剩飞行中的剑气收尾） */
    if (slashActive) {
        float step = SLASH_SPEED * dt;
        slashX += slashDX * step;
        slashY += slashDY * step;
        slashTravel += step;

        /* 命中：剑气扫过的位置附近全部秒杀（穿透） */
        int hit = 0;
        if (monster.active && Dist2(slashX, slashY, monster.x, monster.y) < 0.55f) {
            ApplyDamage(&monster, slashDmg, DT_SLASH, posX, posY);
            hit = 1;
        }
        if (lightBeast.active && Dist2(slashX, slashY, lightBeast.x, lightBeast.y) < 0.55f) {
            ApplyDamage(&lightBeast, slashDmg, DT_SLASH, posX, posY);
            hit = 1;
        }
        for (int i = 0; i < LEECH_MAX; i++) {
            if (!leeches[i].active) continue;
            if (Dist2(slashX, slashY, leeches[i].x, leeches[i].y) < 0.55f) {
                leeches[i].active = 0;
                hit = 1;
            }
        }
        if (hit) {
            /* 2026-11-05：法术武器已下线，这一行只会给一个不存在的武器加经验 */

            slashFade = 0.18f;                 /* 命中留一点余辉 */
            slashHitStop = 0.05f;              /* 50ms 顿帧 */
            /* 在命中点炸一圈金币（复用玻璃金币池） */
            SpawnShards(slashX, slashY, slashDX * 3.0f, slashDY * 3.0f, 1.0f);
            MakeNoise(slashX, slashY, KNIFE_NOISE * 0.5f);
            PlaySfx(SFX_THROW, 0.85f);
        }

        /* 撞墙 / 射程到头：收招 */
        if (Blocked(slashX, slashY) || slashTravel > slashMaxDist) {
            slashActive = 0;
            slashFade = 0.22f;
        }
    }
    if (slashFade > 0.0f) {
        slashFade -= dt;
        if (slashFade < 0.0f) slashFade = 0.0f;
    }

    /* ---- v1.8 投石：正前方 3~6 格制造半径 14 的噪声 ---- */
    if (decoySelfTimer > 0.0f) {
        decoySelfTimer -= dt;
        if (decoySelfTimer < 0.0f) decoySelfTimer = 0.0f;
    }
    /* G 键冷却倒计时 */
    if (s_decoyKeyCd > 0.0f) {
        s_decoyKeyCd -= dt;
        if (s_decoyKeyCd < 0.0f) s_decoyKeyCd = 0.0f;
    }
    /* ==================================================================
     * 2026-11-06（**P1-4 教程在受限状态下死锁**）
     * ------------------------------------------------------------------
     * 原来这里要求 HasKeystone(SK_DECOY) —— 但训练场不授予任何天赋，
     * 所以教程第 9 步（TUT_KNOCK）教的 G 键**根本没有反应**，
     * 玩家照做也没用，教程卡死。
     *
     * 现在：**训练场里无条件开放 G 键**。
     * 教程的意义就是把操作教一遍，不能先要求玩家去商店买天赋。
     * ================================================================ */
    /* ==================================================================
     * 相位信标：C 放置 / Q 传送（2026-11-06 用户要求）
     * ------------------------------------------------------------------
     * 放在按骰子之前：它们和投掷物无关，不该被蓄力/投掷状态影响。
     * ================================================================ */
    if (IsActionPressed(ACT_PHASE_PLACE)) {
        keyPressed[g_bindings[ACT_PHASE_PLACE]] = 0;
        if (!phaseOwned || phaseCharges <= 0) {
            pickupMsg = (g_lang == LANG_ZH) ? "没有相位信标" : "NO PHASE BEACON";
            pickupMsgTimer = 1.4f;
        } else if (PhaseTryPlace()) {
            pickupMsg = (g_lang == LANG_ZH) ? "信标已放置" : "BEACON PLACED";
            pickupMsgTimer = 1.4f;
            PlaySfx(SFX_POWER, 0.55f);
            uiDirty = 1;
        }
    }
    if (IsActionPressed(ACT_PHASE_TELE)) {
        keyPressed[g_bindings[ACT_PHASE_TELE]] = 0;
        if (!phaseOwned || phaseCharges <= 0) {
            pickupMsg = (g_lang == LANG_ZH) ? "没有相位信标" : "NO PHASE BEACON";
            pickupMsgTimer = 1.4f;
        } else if (!phasePlaced) {
            pickupMsg = (g_lang == LANG_ZH) ? "先按 C 放一个信标" : "PLACE A BEACON FIRST (C)";
            pickupMsgTimer = 1.4f;
        } else if (PhaseTryTeleport()) {
            pickupMsg = (g_lang == LANG_ZH) ? "相位传送！" : "PHASE SHIFT!";
            pickupMsgTimer = 1.4f;
            PlaySfx(SFX_POWER, 0.9f);
            uiDirty = 1;
        } else {
            /* 目标格被墙占了（期间塌过 / 被改过地形） */
            pickupMsg = (g_lang == LANG_ZH) ? "信标位置已失效" : "BEACON OBSTRUCTED";
            pickupMsgTimer = 1.4f;
            phasePlaced = 0;
        }
    }

    if ((HasKeystone(SK_DECOY) || trainingMode) && IsActionPressed(ACT_DECOY)) {
        /* ==================================================================
         * 2026-11-05 修「DECOY 超模 / 可以无限使用」
         * ------------------------------------------------------------------
         * 技能树里这条的描述是：
         *     "G: throw a stone (8s cd), noise 14"
         * 但这里**从来没有冷却判定** —— 每按一次 G 就无条件放一发 14 格噪音，
         * 玩家可以一直按，把所有怪无限往一个点引。
         * 现在补上描述承诺的 8 秒；冷却中给明确提示，不做静默吞键。
         * ================================================================== */
        if (s_decoyKeyCd > 0.0f) {
            extern const char* pickupMsg;
            extern float pickupMsgTimer;
            extern int g_lang;
            pickupMsg = (g_lang == LANG_ZH) ? "投石冷却中" : "STONE ON COOLDOWN";
            pickupMsgTimer = 0.8f;
        } else {
            float px = posX, py = posY;
            for (float d = DECOY_MIN; d <= DECOY_MAX; d += 0.5f) {
                float tx = posX + dirX * d, ty = posY + dirY * d;
                if (Blocked(tx, ty)) break;
                px = tx; py = ty;
            }
            MakeNoise(px, py, DECOY_NOISE);
            decoySelfTimer = DECOY_SELF_TIME;
            s_decoyKeyCd = DECOY_KEY_COOLDOWN;
            PlaySfx(SFX_STEP, 0.7f);
            /* P1-2：教程「制造噪音」这一步的完成条件 */
            if (trainingMode) tutorialKnocked = 1;
        }
    }

    /* 手摇发电：站着摇，摇满一次回一截电；期间不能移动 */
    cranking = 0;
    if (IsActionDown(ACT_CRANK) && battery < (float)BatteryMax() - 0.1f && !holdingNow) {
        cranking = 1;
        crankTimer += dt;
        crankClick += dt;
        if (crankClick >= 0.30f) {
            crankClick = 0.0f;
            PlaySfx(SFX_CRANK, 0.45f);
            MakeNoise(posX, posY, 12.0f * RunCrankNoiseMul()*(SkillOwned(SK_APPRENTICE)?0.5f:1.0f));   /* v2.2：FAST CRANK 的 +15% 噪声 */
        }
        /* v3.4：SPARE CELL 满级时摇电时间 -30%（2.4 → 1.68 秒） */
        if (crankTimer >= 2.4f * RunCrankTimeMul() * SkillCrankTimeMul()) {
            crankTimer = 0.0f;
            int wasDead = (battery <= 0.5f);
            /* v3.4：SPARE CELL 满级 → 一次 +55（基础 32） */
            battery += SkillCrankBonus();
            crankTimeTotal += 3;  /* 2.4 秒算 3 秒，累计用于成就 */
            crankSecondsTotal += 3.0f;   /* v3.3：CRANK ADDICT 用的是这个 */
            if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
            /* v3.3 **充上电自动开灯** ——
               试玩实测：「电量归零灯自动熄灭，摇满后灯仍是 OFF，
               必须再单独按一次 F，而 HUD 从不提示 F 的存在」。
               你刚刚专门花 2.4 秒摇电，显然是想继续用灯。 */
            if (wasDead && !flashlightOn && FlashlightUseAllowed()) {
                flashlightOn = 1;
                pickupMsg = (g_lang == LANG_ZH) ? "手电已重新点亮" : "LIGHT BACK ON";
                pickupMsgTimer = 1.2f;
            }
            PlaySfx(SFX_POWER, 0.45f);
        }
    } else {
        /* v3.3 **早松手也要给部分电量** ——
           试玩实测：「按住 1.5 秒时屏幕上已经出现进度，松手后电量一点没加，
           也没有『松手太早』的提示」。
           原来 crankTimer 一松手就归零，不足 2.4 秒的部分全丢。
           现在按比例结算：攒了 1.5 秒就给 1.5/2.4 × 32 ≈ 20 点。
           ≥ 0.3 秒才给，避免「点一下就白送」。 */
        float full = 2.4f * RunCrankTimeMul() * SkillCrankTimeMul();
        if (crankTimer >= 0.3f && full > 0.01f) {
            int wasDeadPart = (battery <= 0.5f);   /* v6：早松手也判断是否没电 */
            float part = crankTimer / full;
            if (part > 1.0f) part = 1.0f;
            /* 2026-11-06（用户要求）：每次摇电的比例提高一点。
               上限涨到 150 之后，32 点显得太抠 —— 摇满一次只补 1/5。
               38 仍然需要摇两次多才能充满，节奏没变。 */
            battery += 38.0f * part;
            if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
            crankTimeTotal += (int)(3.0f * part);
            crankSecondsTotal += 3.0f * part;
            PlaySfx(SFX_POWER, 0.30f + 0.25f * part);
            /* v6 **早松手也要能开灯** ——
               v3.3 的「充上电自动开灯」只写在「摇满 2.4 秒」那条分支里。
               而这轮加了「早松手给部分电量」之后：
                 电量 0 → 松手早 → 加到 ~20 → battery 不再是 0
                 → 下次摇满时 wasDead = 0 → **灯永远开不回来**。
               所以这里补上同样的判断。 */
            if (wasDeadPart && !flashlightOn && battery > 1.0f && FlashlightUseAllowed()) {
                flashlightOn = 1;
                pickupMsg = (g_lang == LANG_ZH) ? "手电已重新点亮" : "LIGHT BACK ON";
                pickupMsgTimer = 1.2f;
            }
        }
        crankTimer = 0.0f;
        crankClick = 0.0f;
    }

    /* 移速：基础值 + 永久升级 - 状态效果的减速（最多 -45%） */
    float slowMul = 1.0f - FxStrength(FX_SLOW) * 0.45f;
    float darkAdd = flashlightOn ? 0.0f : RunDarkSpeedAdd();
    /* CRANK WALK：有这个技能时摇电也允许移动，只是移速打折 */
    /* v3.4：CRANK WALK（局内树 id 5）允许边走边摇，移速 -40% */
    float crankPenalty = SkillCrankWalkPenalty();
    if(SkillOwned(SK_APPRENTICE))crankPenalty*=0.5f;
    if (BenchCrankWalk() && crankPenalty <= 0.0f) crankPenalty = 0.40f;
    float crankMul = cranking ? (1.0f - crankPenalty) : 1.0f;
    if (cranking) sprinting = 0;            /* 边摇边冲刺太离谱 */
    /* v3.3：**玩家自己也会被胶水粘住** —— 这是胶水的代价，
       扔之前要看清自己站哪，否则等于把自己钉住。 */
    /* v3.3：玩家侧也加体积半径，踩到边就减速（否则「有时候不生效」）。
       减速量 -45% → -20%，只做轻微提醒，不至于逃跑时被自己钉住。 */
    float glueSelf = 1.0f;
    {
        const float gr = GLUE_RADIUS + GLUE_BODY_PAD;
        for (int gi = 0; gi < MAX_GLUE; gi++) {
            if (!glues[gi].active) continue;
            if (Dist2(posX, posY, glues[gi].x, glues[gi].y) < gr * gr) {
                glueSelf = GLUE_PLAYER_MUL;
                break;
            }
        }
    }
    /* v3.4：SLIDE（局内树 id 6）—— 没有蹲伏键，改成「冲刺时体力消耗减半 +
       起步瞬间速度 +20%」，手感上就是滑出去一下。 */
    /* v6 第 ㉟ 轮：**蛮冲**（改造树第 0 列的第二顶点）终于生效了。
   第 ⑩ 轮加了节点但 getter 没人调 —— 玩家花 1200 金买了一个空节点。 */
    float bullAdd = 0.0f;
    { extern int BenchBullRush(void); if (BenchBullRush()) bullAdd = 0.15f; }
    /* v6：**爆发** 冲刺时再 +25% */
    float burstAdd = 0.0f;
    { extern int BenchBurst(void); if (BenchBurst()) burstAdd = 0.25f; }
    /* ==================================================================
     * v7 第 60 轮：**RunSpeedMul() 全工程 0 调用点**
     * ==================================================================
     * 局内技能的卡片明写「LIGHT FOOT: speed -3%」「IRON SKIN: speed -4%」，
     * 而 RunSpeedMul() 就是这两个代价的实现 —— 但从来没人调它。
     * 于是这两条"代价"一分钱都没收，玩家只拿到了好处。
     *
     * 接在这里：它只乘玩家的移动速度，不影响冲刺倍率本身。
     * ================================================================ */
    float sp = MOVE_SPEED * (1.0f + SkillSpeedAdd() + darkAdd + bullAdd) * slowMul * RelicSpeedMul() * glueSelf * dt * BenchMoveMul()
             * RunSpeedMul()
             * (sprinting ? SPRINT_MULT * (BenchSlide() ? 1.20f : 1.0f) * (1.0f + burstAdd) : 1.0f)   /* v3.4 SLIDE +20% */ * crankMul * (hatchSpeedT > 0.0f ? 1.25f : 1.0f);
    if (sprinting && sp < 2.25f*dt) sp=2.25f*dt;
        { extern float playerMoveSpeedNow; playerMoveSpeedNow = dt>0.0f ? sp/dt : 0.0f; }   /* v8: 守门人据此判断在动/冲刺 */
    float perpX = -dirY, perpY = dirX;      /* 单位侧向，与 FOV 无关 */
    float dx = 0.0f, dy = 0.0f;
    /* v3.0：**锁盘打开时角色不能移动** ——
       用户实测「解锁的时候角色会乱动，导致这个功能失效」：
       移动条件里根本没检查 lockActive，锁盘开着 WASD 照样走，
       一走开箱子就失去目标，锁盘直接失效。 */
    if (lockActive) {
        dx = 0.0f; dy = 0.0f; sprinting = 0;
    }
    if (!lockActive && !holdingNow && (!cranking || crankPenalty > 0.0f)) {          /* 翻找/摇电时不接受移动输入 */
        if (inF > 0) { dx += dirX * sp;  dy += dirY * sp; }
        if (inF < 0) { dx -= dirX * sp;  dy -= dirY * sp; }
        if (inS > 0) { dx += perpX * sp; dy += perpY * sp; }
        if (inS < 0) { dx -= perpX * sp; dy -= perpY * sp; }
        if (inF != 0 && inS != 0) { dx *= 0.7071f; dy *= 0.7071f; }   /* 斜向不加速 */
    } else {
        sprinting = 0;
    }

    if (dx != 0.0f && !Blocked(posX + dx, posY)) posX += dx;
    if (dy != 0.0f && !Blocked(posX, posY + dy)) posY += dy;

    /* ==================================================================
     * 2026-11-06：**舱门推进 + 效果计时器**
     * ------------------------------------------------------------------
     * HatchTick 必须每帧跑：它负责推进开门动画，并在动画播完那一帧
     * 结算效果。漏了它门就永远卡在 OPENING（这类"更新循环漏写"
     * 在 art_direction.h 里被点名过）。
     * ================================================================ */
    HatchTick(dt);
    if (hatchLightsT > 0.0f) { hatchLightsT -= dt; if (hatchLightsT < 0.0f) hatchLightsT = 0.0f; }
    if (hatchGoldMulT > 0.0f) { hatchGoldMulT -= dt; if (hatchGoldMulT < 0.0f) hatchGoldMulT = 0.0f; }
    if (hatchBloodT > 0.0f)   { hatchBloodT -= dt;   if (hatchBloodT < 0.0f) hatchBloodT = 0.0f; }
    if (hatchSpeedT > 0.0f)   { hatchSpeedT -= dt;   if (hatchSpeedT < 0.0f) hatchSpeedT = 0.0f; }

    /* ==================================================================
     * 2026-11-06：**崩塌流程**
     * ------------------------------------------------------------------
     * 黑屏期间逻辑冻结、输入吞掉（AD-28：模态要有明确的时间与输入边界）。
     * 注意放在 UpdateExplored 与移动**之前** —— 放在后面的话，
     * 玩家在黑屏里仍然能走两步，重掷后位置就不可预测了。
     * ================================================================ */
    if (CollapseTick(dt)) return;

    UpdateExplored();

    /* ---------------- 交互：长按 E 搜索容器 / 给配电箱上电 ---------------- */
    if (holdingNow) {
        if (holdTimer == 0.0f && (hoverKind == 1 || hoverKind == 3 || hoverKind == 4)) {
            /* 开始翻找：发出声音，吸引怪物（压迫感来源）。
               v2.4-rc：家具（货架/木箱堆/控制台）也走这条 ——
               用户实测"搜这种大容器的时候搜索进度条失效、不发声"，
               因为第一版用的是**按一下**而不是**长按**。 */
            PlaySfx(SFX_SEARCH, 0.55f);  /* 专用翻箱声：金属刮擦+木板闷响 */
            MakeNoise(posX, posY, 10.0f * SkillSearchNoiseMul() * BenchSearchQuiet() * RunSearchNoiseMul2());   /* v2.2：FAST SEARCH 的 +10% */  /* 比走路大一点，比跑步小 */
        }
        /* 2026-11-05：宝箱怪在翻找进度 60% 时咬人。
           低于 45% 松手完全安全 —— 所以"起疑就松手"是有效解法。
           被咬的那一帧不推进 holdTimer，也不触发"翻完"结算。 */
        int mimicBit = 0;
        if (hoverKind == 1) {
            float prog = holdTimer / (SEARCH_TIME * RunSearchTimeMul());
            if (MimicOnSearchStep(hoverIndex, prog)) {
                holdTimer = 0.0f;
                hideBlockTimer = 0.4f;
                mimicBit = 1;
            }
        }
        holdTimer += dt;
        if (!mimicBit && holdTimer >= SEARCH_TIME * RunSearchTimeMul()) {
            holdTimer = 0.0f;
            /* v3.0：翻完箱也上 0.4 秒封锁 ——
               否则 E 还按着，同一帧/下一帧就会被旁边的藏身点吸进去。 */
            hideBlockTimer = 0.4f;
            if (hoverKind == 4) {
                /* ================================================================
                 * 舱门：门槛判定 → 开门动画
                 * 效果**不在这里结算** —— 由 HatchTick 在动画播完那一帧调
                 * HatchFireEffect（AD-27：动画与生效窗口同一时间轴）。
                 * ================================================================ */
                char why[96];
                if (HatchGateCheck(hoverIndex, why, (int)sizeof(why))) {
                    HatchSetState(hoverIndex, HATCH_OPENING);
                    pickupMsg = (g_lang == LANG_ZH) ? "舱门开启中…" : "HATCH CYCLING...";
                    pickupMsgTimer = 1.2f;
                } else {
                    pickupMsg = why[0] ? why : ((g_lang == LANG_ZH) ? "条件不满足" : "NOT YET");
                    pickupMsgTimer = 2.0f;
                }
                uiDirty = 1;
                hoverKind = 0;
            } else if (hoverKind == 3) {
                /* ================================================================
                 * 家具搜查结算（长按完成后）
                 * ----------------------------------------------------------------
                 * 用户要求：「收这种大箱子有概率收不出来东西，
                 *           不然的话奖励太丰厚了。」
                 * 所以每次有 **35% 空手** —— 大容器给得多，但也要赌。
                 * ================================================================ */
                extern RoomProp roomProps[MAX_ROOM_PROPS];
                if (hoverIndex >= 0 && hoverIndex < MAX_ROOM_PROPS && roomProps[hoverIndex].active) {
                    RoomProp* rp = &roomProps[hoverIndex];
                    int n = rp->searched;
                    rp->searched++;
                    if (rp->searchLeft > 0) rp->searchLeft--;

                    int empty = (rand() % 100) < 35;
                    if (empty) {
                        pickupMsg = (g_lang == LANG_ZH) ? "空的" : "NOTHING HERE";
                    } else if (n == 1) {
                        if (battery < (float)BatteryMax() - 1.0f) {
                            battery += 18.0f;
                            if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
                            pickupMsg = (g_lang == LANG_ZH) ? "翻到电池 +18" : "FOUND A BATTERY +18";
                        } else {
                            int g2 = 18 + (int)(rand() % 25);
                            carryGold += g2;
                            static char gb2[64];
                            snprintf(gb2, sizeof(gb2), (g_lang == LANG_ZH) ? "翻到 %d 金币" : "FOUND %d GOLD", g2);
                            pickupMsg = gb2;
                        }
                    } else if (n == 2 && fusesHeld < fusesNeeded) {
                        fusesHeld++;
                        pickupMsg = (g_lang == LANG_ZH) ? "翻到一根保险丝！" : "FOUND A FUSE!";
                    } else {
                        int g2 = 18 + (int)(rand() % 25);
                        carryGold += g2;
                        static char gb3[64];
                        snprintf(gb3, sizeof(gb3), (g_lang == LANG_ZH) ? "翻到 %d 金币" : "FOUND %d GOLD", g2);
                        pickupMsg = gb3;
                    }
                    pickupMsgTimer = 1.6f;
                    uiDirty = 1;
                    if (rp->searchLeft <= 0) hoverKind = 0;
                }
            } else if (hoverKind == 1) {
                Container* c = &containers[hoverIndex];
                c->searched = 1;
        { extern int hideStreak; hideStreak = 0; }   /* v7：搜过箱子，连躲计数清零 */
        SpawnParticles(c->x, c->y, 2);   /* v2.1：翻箱扬起灰尘 */
                searchedCount++;
                runSearched++;
        int shardGain = (int)(GoldSearchAt(currentLevel) * SkillScavengerShardMul() * SkillCrateGoldMul()
                              * RelicShardMul() * modShardMul + 0.5f);   /* v2.0：遗物+事件 */
                carryGold += shardGain;    /* v1.8：先记在待上缴里 */
                totalGoldEarned += shardGain;
                PlaySfx(SFX_PICKUP, 0.55f);
                MakeNoise(posX, posY, 8.0f * SkillSearchNoiseMul() * BenchSearchQuiet());
                if (c->content == 1) { fusesHeld++; fusesCollectedRun++; pickupMsg = "+FUSE"; }
                else if (c->content >= 4) {
                /* v1.16：纯随机产出，酸瓶与烟雾明显稀有 */
                /* v6 **删掉 BRICK / ACID 两行** ——
                   道具栏改成 6 格（光球/投石/烟雾/胶水/回声/诱饵）之后，
                   砖头与酸液早就没有槽位了，但箱子还在掉，
                   玩家会看到 “+ACID” 却什么都没有（用户报告）。
                   权重重新分配给现有 6 种。 */
                int roll = rand() % 100;
                if (roll < 22) { if (flares < BottleCap()) { flares++; pickupMsg = "+FLARE"; } }
                else if (roll < 44) {
                    /* v2.7：走 BottleCap()，否则 RUST KNIFE 的 cap=1 代价能被箱子刷开 */
                    if (bottles < BottleCap()) bottles++;
                    pickupMsg = "+BOTTLE";
                }
                else if (roll < 64) { if (smokeBombs < BottleCap()) { smokeBombs++; pickupMsg = "+SMOKE"; } }
                else if (roll < 78) { if (echoBolts < BottleCap()) { echoBolts++; pickupMsg = "+ECHO"; } }
                else if (roll < 90) { if (gluePots < BottleCap()) { gluePots++; pickupMsg = "+GLUE"; } }
                else                { if (decoys < BottleCap()) { decoys++; pickupMsg = "+DECOY"; } }
            }
                else if (c->content == 2) {
                    battery += BATTERY_PICKUP * BenchBatteryPickupMul();   /* v2.7 电池库 */
                    energy += ENERGY_FROM_BATTERY;   /* v2.3：电池顺带补武器能量 */
                    if (energy > ENERGY_MAX) energy = ENERGY_MAX;
                    if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
                    healFlash = 0.06f;
                    pickupMsg = "+BATTERY";
                } else if (c->content == 3) {
                    sanity += 25.0f * RunSanityGainMul();
                    if (sanity > SanityMax()) sanity = SanityMax();
                    healFlash = 0.15f;
                    pickupMsg = "+MEDKIT";
                }
                /* v3.3：新掉落表的四种投掷物 */
                else if (c->content == 6) { if (smokeBombs < BottleCap()) { smokeBombs++; pickupMsg = "+SMOKE"; } }
                else if (c->content == 7) { if (gluePots < BottleCap()) { gluePots++; pickupMsg = "+GLUE"; } }
                else if (c->content == 8) { if (echoBolts < BottleCap()) { echoBolts++; pickupMsg = "+ECHO"; } }
                else if (c->content == 9) { if (decoys < BottleCap()) { decoys++; pickupMsg = "+DECOY"; } }
                /* v3.4：箱子现在是主要金币来源（掉落表 35%） */
                else if (c->content == 10) {
                    int gld = (int)(GoldSearchAt(currentLevel) * SkillScavengerShardMul() * SkillCrateGoldMul()
                                    * RelicShardMul() * modShardMul * SkillGoldMul() + 0.5f);
                    carryGold += gld;
                    totalGoldEarned += gld;
                    {
                        static char gmsg[48];
                        snprintf(gmsg, sizeof(gmsg), "+%d GOLD", gld);
                        pickupMsg = gmsg;
                    }
                }
                /* SCAVENGER: every search also drops a random throwable */
        if (SkillScavenger()) {
            /* v3.3：6 选 1，对应新的六个槽位 */
            int r2 = rand() % 6;
            if (r2 == 0) { if (flares < BottleCap()) { flares++; pickupMsg = "+FLARE"; } }
            else if (r2 == 1) { if (bottles < BottleCap()) bottles++; pickupMsg = "+SLING"; }
            else if (r2 == 2) { if (smokeBombs < BottleCap()) { smokeBombs++; pickupMsg = "+SMOKE"; } }
            else if (r2 == 3) { if (gluePots < BottleCap()) { gluePots++; pickupMsg = "+GLUE"; } }
            else if (r2 == 4) { if (echoBolts < BottleCap()) { echoBolts++; pickupMsg = "+ECHO"; } }
            else { if (decoys < BottleCap()) { decoys++; pickupMsg = "+DECOY"; } }
        }
        /* ============================================================
         * 2026-11-05：**宝箱怪空壳的额外奖励**
         * ------------------------------------------------------------
         * 放在普通结算**之后**，这样它覆盖 pickupMsg，
         * 玩家看到的是"空壳里翻出 400 金币"而不是普通的 +GOLD 提示。
         *
         * 为什么不拦截普通结算：普通箱子那 8+1.5×层的金币量级很小，
         * 为了省这点钱去改一大段缩进，只会增加出错面（铁律四十九）。
         * ============================================================ */
        if (c->mimicShell) {
            int bigGold = MIM_GOLD_BASE + MIM_GOLD_STEP * currentLevel;
            bigGold = (int)((float)bigGold * RelicShardMul() * modShardMul * SkillGoldMul() + 0.5f);
            carryGold += bigGold;
            totalGoldEarned += bigGold;

            fusesHeld++;                 /* 必给一根保险丝 */
            fusesCollectedRun++;

            {   /* 再给一件道具（沿用箱子那套权重） */
                int r = rand() % 100;
                if      (r < 20) { if (bottles   < BottleCap()) bottles++;   }
                else if (r < 38) { if (flares    < BottleCap()) flares++;    }
                else if (r < 54) { if (smokeBombs< BottleCap()) smokeBombs++; }
                else if (r < 70) { if (gluePots  < BottleCap()) gluePots++;  }
                else if (r < 85) { if (echoBolts < BottleCap()) echoBolts++; }
                else             { if (decoys     < BottleCap()) decoys++;   }
            }

            PlaySfx(SFX_POWER, 1.0f);
            {
                static char shellMsg[112];
                snprintf(shellMsg, sizeof(shellMsg),
                         g_lang == LANG_ZH ? "空壳里翻出  +%d 金币  +1 保险丝  +1 道具"
                                           : "SHELL LOOT  +%d GOLD  +1 FUSE  +1 ITEM",
                         bigGold);
                pickupMsg = shellMsg;
            }
            pickupMsgTimer = 2.2f;
            uiDirty = 1;
        }
        if (pickupMsg) pickupMsgTimer = 2.0f;
            } else if (hoverKind == 2) {
                /* v3.0：**NO_COMBAT 下没有 BOSS 可打** ——
                   bossDefeated 只在 #else 分支赋值（怪不可杀），
                   这里永远为 0，于是第 6/12/18 层「BOSS STILL ALIVE」
                   把一局钉死（体验评审实测：bossLevel=1 时通电恒失败）。 */
                int bossBlocked = 0;
#if !NO_COMBAT
                bossBlocked = (bossLevel && !bossDefeated);
#endif
                if (bossBlocked) {
                    pickupMsg = "BOSS STILL ALIVE";
                    pickupMsgTimer = 1.5f;
                } else if (fusesHeld >= fusesNeeded) {
                    breakerOpen = 1;
                { extern void WardenSpawnIfPowered(void); WardenSpawnIfPowered(); }   /* v7 B1: 通电 -> 守门人出现 */
        SpawnParticles(breaker.x, breaker.y, 0);   /* v2.1：通电火花 */
                    fusesHeld -= fusesNeeded;
                    carryGold += (int)(GoldBreakerAt(currentLevel) * RelicShardMul() * modShardMul * SkillGoldMul() + 0.5f);   /* v2.0 */
                    totalGoldEarned += (int)GoldBreakerAt(currentLevel);
                    PlaySfx(SFX_POWER, 0.75f);
                }
            }
            hoverKind = 0;
            hoverIndex = -1;
        }
    } else {
        holdTimer = 0.0f;
    }

    /* ===== v2.4.80 **左键：扳手击退（从零重写）** =====
       用户要求：「左键直接帮我重构代码吧，一直就没对过。」
       旧的 atkNow 块里混着 5 个历史版本的注释、三套阈值、
       和投掷抢键的判断 —— 已经没人能读懂它到底该干什么。

       新实现见 WrenchKnockUpdate()（在 Update 之前定义）：
         按住 = 蓄力，松手 = 击退，低于死区 = 什么都不做。
         只有这一条路径，没有"敲"，没有和投掷抢键。 */
    WrenchKnockUpdate(dt);
    { extern void TickSecondSpawn(float); TickSecondSpawn(dt); }   /* v2.4.94 第二只 */
    WrenchTick(dt);

    /* 投掷：数字键选槽位，鼠标左键蓄力投掷 */
    if (throwCd > 0.0f) throwCd -= dt;

    /* 数字键 1/2/3 切换投掷物槽位。
       键盘的情况已经在 WM_KEYDOWN 里即时处理（见 WindowProc）——
       原来在这里用 if/else 轮询 keyPressed，同一帧先按 1 再按 2 只认 1，
       按住不放时系统的自动重复还会一直压住另一个键，表现就是「按了没反应」。
       这里只保留「把槽位改绑到鼠标键」的兜底：鼠标键不产生 WM_KEYDOWN。 */
    if (IsMouseButton(g_bindings[ACT_SELECT_1]) && IsActionPressed(ACT_SELECT_1)) SelectVisibleSlot(0);
    else if (IsMouseButton(g_bindings[ACT_SELECT_2]) && IsActionPressed(ACT_SELECT_2)) SelectVisibleSlot(1);
    else if (IsMouseButton(g_bindings[ACT_SELECT_3]) && IsActionPressed(ACT_SELECT_3)) SelectVisibleSlot(2);

    /* 鼠标左键：按住蓄力，松开投掷；右键取消蓄力。
       严格的「未按下 -> 按下」边缘检测：
       - prev 保存上一逻辑帧的真实按下状态；
       - 菜单期间 Update 顶部早退，prev 冻结 —— 菜单里点 Back 的那次
         左键按下对这里完全不可见；切回游戏时左键还物理按着，
         若不处理会被误判成新按下，开局就凭空蓄力、松手即投掷。
       - 所以「回到游戏」的每条路径（Resume / Esc / 失焦自动恢复 / 读档 /
         新开局）都必须置位 resetInputState：复位时把 prev 同步成当前
         物理状态 —— 已按着的键不算边沿，必须先松开再按才会蓄力。 */
    static int mouseLeftPrev = -1;
    static int mouseRightPrev = -1;
    if (resetInputState) {
        resetInputState = 0;
        mouseLeftPrev = IsActionDown(ACT_THROW);
        mouseRightPrev = IsActionDown(ACT_CANCEL_CHARGE);
        isCharging = 0;
        mouseCharge = 0.0f;
    }
    /* v2.4.77：**投掷 = 右键**（ACT_THROW = VK_RBUTTON） */
    int mouseLeftNow = (!lockActive) && IsActionDown(ACT_THROW);
    int mouseRightNow = (!lockActive) && IsActionDown(ACT_CANCEL_CHARGE);   /* 左键：现在只用于取消 */

    if (mouseLeftNow && !mouseLeftPrev && throwCd <= 0.0f && !holdingNow && !cranking) {
        /* 未按下 -> 按下：开始蓄力 */
        isCharging = 1;
        mouseCharge = 0.0f;
    }
    if (isCharging) {
        /* v2.4.83：**取消投掷改回右键**。
           原来这里读的是左键（鼠标左键），而左键现在归扳手击退 ——
           于是在箱子旁边按住左键时，投掷蓄力被这一行直接取消掉，
           看起来就是「扳手在箱子附近用不了」。 */
        if (mouseRightNow && !mouseRightPrev) {
            isCharging = 0;
            mouseCharge = 0.0f;
        } else if (mouseLeftNow) {
            /* 持续按住：累积蓄力（仅当选中道具有库存时） */
            int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys };
            int sel = throwSelected;
            if (sel < 0 || sel >= ITEM_SLOT_COUNT) sel = 0;
            if (counts[sel] > 0) {
                /* v3.4：STEADY HAND（局内树 id 9）满蓄力 0.5s → 0.32s */
                mouseCharge += dt / (TUNE_CHARGE_FULL_SECONDS * SkillHeavyHandChargeMul() * BenchSteadyHand());
                if (mouseCharge > 1.0f) mouseCharge = 1.0f;
            }
        } else {
            /* 松开左键：发射（v1.16：0..4 = 荧光棒/瓶/砖/酸/烟） */
            isCharging = 0;
            int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys };
            int sel = throwSelected;
            if (sel < 0 || sel >= ITEM_SLOT_COUNT) sel = 0;
            /* ===== v3.3 六槽位的右键行为 =====
               0 光球 / 1 投石 / 2 烟雾 / 3 胶水 / 4 回声  → 投掷
               5 诱饵                                     → **放在脚下**（不投）
               诱饵放下后要按 C 才激活（用户要求：C 是激活，右键才是放置）。 */
            if (sel == 5) {
                if (decoys > 0) {
                    PlaceDecoy(posX, posY);
                    decoys--;
                    totalThrown++;          /* 也算一次道具消耗 */
                    throwCd = 0.60f;
                    pickupMsg = (g_lang == LANG_ZH)
                        ? "诱饵已放下" : "DECOY PLACED";
                    pickupMsgTimer = 2.0f;
                    PlaySfx(SFX_PICKUP, 0.7f);
                }
                mouseCharge = 0.0f;
                mouseLeftPrev = mouseLeftNow;
                mouseRightPrev = mouseRightNow;
                return;
            }
            if (mouseCharge >= 0.1f && counts[sel] > 0) {
                /* v2.7：先投，真生成了才扣。原来先扣再投，
                   池子满时 ThrowItem 静默不生成，东西凭空消失。 */
                /* v3.4：TWIN THROW（局内树 id 11）一次扔两个 ——
                   按住 Shift 时生效，消耗 2 个。 */
                ThrowItem(sel, mouseCharge);
                if (BenchTwinThrow() && GetKeyState(VK_SHIFT) < 0 && counts[sel] >= 2) {
                    ThrowItem(sel, mouseCharge);
                    /* 第二个也扣掉：下面那一行只扣 1，这里补扣 */
                    extern int flares, bottles, smokeBombs, gluePots, echoBolts, decoys;
                    if (sel == 0 && flares > 0) flares--;
                    else if (sel == 1 && bottles > 0) bottles--;
                    else if (sel == 2 && smokeBombs > 0) smokeBombs--;
                    else if (sel == 3 && gluePots > 0) gluePots--;
                    else if (sel == 4 && echoBolts > 0) {
                echoBolts--;
                /* v7 第 65 轮：CARTOGRAPHER 成就的计数器**从来没被加过**
                   —— 掷一枚回声弹揭示 15 格（见 echoBolts 的定义）。 */
                /* 2026-11-05：原来写死 +15，而实际扫描半径是 3 格 ——
                   成就在统计一个不存在的数字。现在按**真实覆盖格数**近似。 */
                { extern int echoCellsTotalRun;
                  int cells = (int)(3.14159265f * ECHO_RADIUS * ECHO_RADIUS / 2.0f);
                  echoCellsTotalRun += cells; }
            }
                    pickupMsg = (g_lang == LANG_ZH) ? "双投！" : "TWIN THROW!";
                    pickupMsgTimer = 1.0f;
                }
                if (g_throwOk) {
                    if (sel == 0) flares--;
                    else if (sel == 1) bottles--;
                    else if (sel == 2) smokeBombs--;
                    else if (sel == 3) gluePots--;
                    else if (sel == 4) { echoBolts--; { extern int echoCellsTotalRun; echoCellsTotalRun += 15; } }
                    /* v3.4：SALVAGE（局内树 id 10）30% 概率不消耗 ——
                       比「落地后走过去捡回」实现简单得多，手感一样。 */
                    if (BenchSalvage() && (rand() % 100) < 30) {
                        if (sel == 0) flares++;
                        else if (sel == 1) bottles++;
                        else if (sel == 2) smokeBombs++;
                        else if (sel == 3) gluePots++;
                        else if (sel == 4) echoBolts++;
                        pickupMsg = (g_lang == LANG_ZH) ? "回收！" : "SALVAGED!";
                        pickupMsgTimer = 1.0f;
                    }
                    totalThrown++;   /* v3.3：逃生向成就用（整条链之后加） */
                    throwCd = 0.45f;
                    if (trainingMode) {
                        tutorialThrown = 1;
                        /* P1-2：投掷同样是"制造噪音"，让 TUT_KNOCK 也走得了 */
                        tutorialKnocked = 1;
                    }
                } else {
                    pickupMsg = "NO ROOM";
                    pickupMsgTimer = 1.0f;
                }
            }
            mouseCharge = 0.0f;
        }
    }
    mouseLeftPrev = mouseLeftNow;
    mouseRightPrev = mouseRightNow;

    /* v1.8 偷电怪提示音：电量低于 50% 时，若身边有偷电怪就给"滋滋"声。
       关灯的幽灵流也能听见 —— 否则被偷电完全无感（模拟里 438 秒被偷）。 */
    if (battery < LEECH_HINT_BATTERY) {
        int ld2 = LeechNearestDist2();
        if (ld2 >= 0 && ld2 < 9.0f) {          /* 3 格内 */
            leechHintTimer += dt;
            if (leechHintTimer > 1.6f) {
                leechHintTimer = 0.0f;
                PlaySfx(SFX_CRANK, 0.22f);
            }
        } else {
            leechHintTimer = 0.0f;
        }
    }

    /* 脚步：走路是完全静音的，只有冲刺才会响、才会惊动怪 */
    {
        /* 噪声倍率三件套：技能 / 局内技能 / 诱饵自身 */
        float ghost = flashlightOn ? 1.0f : SkillGhostStepNoiseMul();
        float noiseMul = SkillNoiseMul() * RunNoiseMul() * ghost
                       * (decoySelfTimer > 0.0f ? DECOY_SELF_MUL : 1.0f);

        if (moving && sprinting && !holdingNow && !cranking) {
            /* 冲刺：每 0.3 秒一次比较响的脚步 */
            stepTimer += dt;
            walkTimer = 0.0f;
            walkStepTimer = 0.0f;
            if (stepTimer >= 0.30f) {
                stepTimer = 0.0f;
                PlaySfx(SFX_STEP, 0.50f);
                /* v3.0：keystone SILENT RUN —— 冲刺完全不产生噪音 */
                if (!SilentRunOwned())
                    MakeNoise(posX, posY, 9.0f * noiseMul * BenchWalkNoiseMul()
                              * QuietSoleMul());   /* 轻步 + 静底 */
            }
        } else if (moving && !holdingNow && !cranking) {
            /* v2.6：走路本来完全无声，现在连续走 5 秒之后开始有细微噪声。
               停下 / 翻箱 / 摇电都会把计时清零 —— 也就是"走一段、停一下"仍然安静。 */
            stepTimer = 0.0f;
            walkTimer += dt;
            if (walkTimer >= WALK_NOISE_DELAY) {
                walkStepTimer += dt;
                if (walkStepTimer >= WALK_NOISE_INTERVAL) {
                    walkStepTimer = 0.0f;
                    PlaySfx(SFX_STEP, 0.28f);          /* 比冲刺轻 */
                    {
        /* v3.4 GHOST STEP 满级：关着手电时**脚步声完全消失**（开灯才恢复）。
           原来这节点是「黑暗脚步 -12% 耗电」，玩家感知不到。 */
        if (!(SkillGhostSilent() && !flashlightOn))
            MakeNoise(posX, posY, WALK_NOISE_RADIUS * noiseMul
                              * BenchWalkNoiseMul() * QuietSoleMul());;
    }   /* 轻步 + 静底 */
                }
            }
        } else {
            /* v2.0：停下不再立刻清零 —— 原来"走 4.9 秒停一帧"可以永久静音。
               现在按 3 倍速衰减，想刷掉噪声必须真的停下来站一会儿。 */
            stepTimer = 0.0f;
            walkTimer -= dt * 3.0f;
            if (walkTimer < 0.0f) walkTimer = 0.0f;
            walkStepTimer = 0.0f;
        }
    }
    if (sanity < 45.0f) {
        heartTimer += dt;
        float hb = 1.15f - (45.0f - sanity) / 45.0f * 0.65f;
        if (heartTimer >= hb) {
            heartTimer = 0.0f;
            PlaySfx(SFX_HEART, 0.30f + (45.0f - sanity) / 45.0f * 0.35f);
        }
    } else {
        heartTimer = 0.0f;
    }

    UpdateShots(dt);
    /* v1.17 训练场：无限道具、不掉理智、标本固定不动 */
    if (trainingMode) {
        /* v3.3：投掷物重做后训练场要给全六种，方便逐个试手感。
           对应槽位：光球 / 投石 / 烟雾 / 胶水 / 回声 / 诱饵 */
        flares = 99;
        bottles = 99;      /* 投石 */
        smokeBombs = 99;
        gluePots = 99;
        echoBolts = 99;
        decoys = 99;
        /* 已删的三种清零，免得老存档带进来 */
        bricks = 0; acids = 0; firePots = 0; mirrors = 0;
        sanity = 100.0f;
        /* 教程里手电电量会正常消耗，这样「按住 R 摇电」才有可见效果；
           只做上限夹取，不再每帧强行充满。 */
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        stamina = StaminaMax();
        monster.active = 1;
        monster.state = 2;              /* 原地不动，当标本 */
        monster.stun = 0.0f;
        if (trainingDpsTimer > 0.0f) trainingDpsTimer -= dt;
        else trainingDps = 0;
    }

    UpdateSmokes(dt);
    /* v1.18：站在烟里 → **只把光压暗**（用户要求，不再收窄视野） */
    /* v1.21: in smoke go nearly black (like the old 0.09 ambient build) */
    /* v2.1：0.10 等于把画面关掉。要的是「看不清」不是「看不见」。 */
    smokeDim = PlayerInSmoke() ? 0.45f : 1.0f;

    /* v2.2：烟幕原来**只压暗玩家自己**（怪完全不看烟），等于自伤道具。
       现在：烟里的怪丢失目标 —— 这才是"遮蔽视线"该有的效果。 */
    for (int si = 0; si < MAX_SMOKE; si++) {
        extern SmokeCloud smokes[MAX_SMOKE];
        if (!smokes[si].active) continue;
        if (monster.active && Dist2(monster.x, monster.y, smokes[si].x, smokes[si].y) < 2.6f) {
            monster.state = 0;
            monster.pathLen = 0;
        }
        if (lightBeast.active && Dist2(lightBeast.x, lightBeast.y, smokes[si].x, smokes[si].y) < 2.6f) {
            lightBeast.state = 0;
            lightBeast.pathLen = 0;
        }
        /* 2026-11-03：第 2、3 只听声鬼以前**完全不吃烟** ——
           烟只写了 monster 和 lightBeast，扩到三只之后这里没跟着改。 */
        for (int hi = 1; hi < MON_HUNTER_MAX; hi++) {
            extern Monster monHunters[MON_HUNTER_MAX];
            if (!monHunters[hi].active) continue;
            if (Dist2(monHunters[hi].x, monHunters[hi].y, smokes[si].x, smokes[si].y) < 2.6f) {
                monHunters[hi].state = 0;
                monHunters[hi].pathLen = 0;
            }
        }
    }

    EnsureThrowSelectedValid();   /* v1.14：手里不能是空的 */
    if (lastHitTimer > 0.0f) lastHitTimer -= dt;   /* v1.15：命中数字淡出 */
    UpdateMonster(dt);
    { extern void UpdateMonster2(float); UpdateMonster2(dt); }
    { extern void UpdateMonster3(float); UpdateMonster3(dt); }   /* v2.4.12 第二只 */
    { extern void FlankUpdate(float); FlankUpdate(dt); }   /* v2.4.95 包夹（必须在 AI 之后，否则目标被覆盖） */
    UpdateLightBeast(dt);
        { extern void UpdateLightBeast2(float); UpdateLightBeast2(dt); }   /* 第二只 */
    UpdateParasite(dt);
    UpdateLeech(dt);

    /* ==================================================================
     * 2026-11-05 修「荧光棒卡进墙里、建模消失」
     * ------------------------------------------------------------------
     * 原来的墙判定是**点**（ShotBlocked 只看中心那一格），而且出手后
     * 0.15 秒内 `justThrown` 完全跳过判定 ——
     *   玩家贴着墙扔 → 荧光棒先无碰撞飞 1.4 格（9.5 格/秒 × 0.15 秒）
     *   → 早已埋进墙里 → 下一帧判成"两个方向都被挡" → z=0 停在墙内
     *   → 渲染在墙背面 → 玩家看到的就是「东西没了」。
     *
     * 修法：
     *   ① 给荧光棒**实体半径**（中心 + 四个方向各推一个半径都能穿过才算空）
     *   ② 撞墙改成**反弹**（法向反射 + 切向摩擦），不再原地急停
     *   ③ 宽限期压到 0.08 秒；结束后如果还卡在墙里，朝玩家方向退出来
     * ================================================================== */
    #define THROWN_R        0.28f   /* 荧光棒的实体半径（格） */
    #define THROWN_BOUNCE   0.45f   /* 撞墙后保留的反弹速度 */
    #define THROWN_FRICTION 0.80f   /* 另一根轴的摩擦 */

    /* 带体积的墙判定：中心 + 四个方向各推一个半径，全都不是墙才算空 */
    #define THROWN_SOLID(x, y) ( \
        ShotBlocked((x), (y)) || \
        ShotBlocked((x) + THROWN_R, (y)) || \
        ShotBlocked((x) - THROWN_R, (y)) || \
        ShotBlocked((x), (y) + THROWN_R) || \
        ShotBlocked((x), (y) - THROWN_R) )

    /* 荧光棒/金币物理更新：飞行中受重力，落地转静止 */
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        if (thrown[i].z > 0.0f) {
            /* 飞行中：重力积分 */
            float nx = thrown[i].x + thrown[i].vx * dt;
            float ny = thrown[i].y + thrown[i].vy * dt;
            thrown[i].z += thrown[i].vz * dt;
            thrown[i].vz -= GRAVITY * dt;

            /* 撞墙预判：分离 X/Y 轴检测，允许滑动。
               v1.9：刚出手的前几帧跳过判定 —— 面朝墙扔的时候，
               nx/ny 立刻落进墙里，会被判成"两个方向都被挡"，
               于是速度清零、荧光棒垂直掉在脚下（玩家看到的就是"不飞"）。
               宽限期内允许它先离开手心。 */
            /* 宽限期 0.15 → 0.08 秒：够离开手心，又不至于钻进墙里 */
            int justThrown = (thrown[i].life > 24.92f);
            int blockedX = justThrown ? 0 : THROWN_SOLID(nx, thrown[i].y);
            int blockedY = justThrown ? 0 : THROWN_SOLID(thrown[i].x, ny);

            /* **反弹而不是急停**：法向速度取反并衰减，切向速度摩擦。
               这样贴着墙斜着扔会「擦墙滑一下再弹开」，
               直着砸墙会「弹回来落在脚边」，而不是原地消失。 */
            if (blockedX) {
                thrown[i].vx = -thrown[i].vx * THROWN_BOUNCE;
                thrown[i].vy *= THROWN_FRICTION;
                if (fabsf(thrown[i].vx) < 0.25f) thrown[i].vx = 0.0f;
            } else {
                thrown[i].x = nx;
            }
            if (blockedY) {
                thrown[i].vy = -thrown[i].vy * THROWN_BOUNCE;
                thrown[i].vx *= THROWN_FRICTION;
                if (fabsf(thrown[i].vy) < 0.25f) thrown[i].vy = 0.0f;
            } else {
                thrown[i].y = ny;
            }

            /* 宽限期结束后还卡在墙里（出生点贴墙）→ 朝玩家方向退出来。
               没有这一步，它仍然会停在墙内不再可见。 */
            if (!justThrown && THROWN_SOLID(thrown[i].x, thrown[i].y)) {
                float bx = thrown[i].x - dirX * 0.6f;
                float by = thrown[i].y - dirY * 0.6f;
                if (!THROWN_SOLID(bx, by)) { thrown[i].x = bx; thrown[i].y = by; }
                thrown[i].vx = 0.0f;
                thrown[i].vy = 0.0f;
            }
            /* v1.9：这里原来还有一次 vz -= GRAVITY*dt ——
               一帧减两次重力，抛物线直接塌成"砸脚面"。已删除。 */

            /* 落地 */
            if (thrown[i].z <= 0.0f) {
                thrown[i].z = 0.0f;
                thrown[i].vz = 0.0f;
                thrown[i].vx *= 0.3f;  /* 滑动摩擦 */
                thrown[i].vy *= 0.3f;
            }
        } else {
            /* 地面滑动衰减 + 滑动碰撞 */
            float nx = thrown[i].x + thrown[i].vx * dt;
            float ny = thrown[i].y + thrown[i].vy * dt;
            
            /* 地面滑行同样要用体积判定 + 轻微反弹，
               否则会「滑进墙脚」再消失 */
            int blockedX = THROWN_SOLID(nx, thrown[i].y);
            int blockedY = THROWN_SOLID(thrown[i].x, ny);

            if (!blockedX) thrown[i].x = nx;
            else thrown[i].vx = -thrown[i].vx * 0.35f;
            if (!blockedY) thrown[i].y = ny;
            else thrown[i].vy = -thrown[i].vy * 0.35f;
            
            thrown[i].vx *= 0.95f;
            thrown[i].vy *= 0.95f;
        }
        thrown[i].life -= dt;
        if (thrown[i].life <= 0.0f) thrown[i].active = 0;
    }

    /* 拾取提示计时 */
    if (pickupMsgTimer > 0.0f) {
        pickupMsgTimer -= dt;
        if (pickupMsgTimer <= 0.0f) pickupMsg = NULL;
    }

    /* 出口：通电后才能进下一关；第 WIN_LEVEL 层的出口 = 逃出生天。
       教程的出口只负责把教程标记为完成，不参与正常换层。 */
    /* v6：**走到出口先弹「撤离 / 深入」**，不再直接换层 */
    /* v6 **最终层不弹二选一** ——
       第 WIN_LEVEL 层的出口是「逃出生天」，必须走通关结算（含成就）。
       原来弹窗会把它截成「撤离」，导致通关画面/成就永远不出现。 */
    int isFinalFloor = campaignMode || (currentLevel >= WIN_LEVEL && !isEndlessRun);
    int atExit = (breakerOpen &&
                  Dist2(posX, posY, exitX, exitY) < EXIT_ENTER_R2 * EXIT_ENTER_R2);

    /* v6 **出口二选一：弹窗** ——
       只有「还没弹过、也不是刚选了深入」才弹。 */
    if (!trainingMode && !isFinalFloor && !exitChoiceOpen && !exitGoDeeper && atExit) {
        exitChoiceOpen = 1;
            { extern void HintOnce(int); HintOnce(3); }   /* v7 B4 */
        exitChoiceT = 0.0f;
        MusicNotify(MUSIC_EVENT_GATE);
        PlaySfx(SFX_GATE, 0.65f);
        uiDirty = 1;
    }
    /* v6 **前进条件** ——
       原来这里写的是 !exitChoiceOpen，而上面刚把它置 1，
       所以整段「换下一层」永远不执行（按 2 只会重复弹窗）。
       现在：最终层直接前进；非最终层必须玩家确认过「深入」。 */
    if (!trainingMode && atExit && (isFinalFloor || exitGoDeeper)) {
        exitGoDeeper = 0;
        /* NOCTURNAL：本层全程手电关着过关 */
        if (!flashlightOn) flashlightOffLevel = 1;

        MusicNotify(MUSIC_EVENT_GATE);
        PlaySfx(SFX_GATE, 0.65f);
        float completedTime = runTime-floorStartTime;
        if (completedTime<90.0f) floorsFast++;
        if (!floorSpotted) floorsPacifist++;
        if (!floorTookDamage) { floorsFlawless++; floorsNoSanityLoss++; }
        else floorsNoSanityLoss=0;
        if (!runLightUse) floorsNoLight++;
        if (WardenActive() && !floorTookDamage) wardenCleanFloors++; else wardenCleanFloors=0;
        DailyObserve(DQ_NOHIT,floorsNoSanityLoss);
        int evThisLevel = levelEvent;   /* v1.8：GenerateLevel 会重置 levelEvent，先存 */
        carryGold += (int)(GoldExitAt(currentLevel) * RelicShardMul() * modShardMul * SkillGoldMul() + 0.5f);
        /* v6 **探索奖励** —— 按本层容器数折算，补上之前缺失的 GOLD_NEWROOM 渠道。
           原来 GOLD_NEWROOM 只定义不发放，「首次进新房间 +5」这条收入是空的。 */
        carryGold += containerCount * GOLD_NEWROOM;   /* v2.0 */
        totalGoldEarned += (int)(GoldExitAt(currentLevel)*RelicShardMul()*modShardMul+0.5f)+containerCount*GOLD_NEWROOM;
        if (currentLevel > bestLevel) bestLevel = currentLevel;
        if (currentLevel == 1) UnlockAchievement(0);  /* FIRST STEPS */
        /* 战役模式：每段连续 3 层；段内换层不重置资源，段间才切规则/重开资源。 */
        if (campaignMode) {
            /* v7 第 ㊿ 轮：层数读 kCampaign[].layers，不再写死 3 */
            extern int CampaignLayersNow(void);
            int stageLayers = CampaignLayersNow();
            if (stageLayers < 1) stageLayers = 1;
            int stageFloor = currentLevel - CampaignStartFloor(campaignStage);
            if (stageFloor + 1 < stageLayers) {
                currentLevel++;
                energy += ENERGY_PER_FLOOR;                      /* v2.3：每层唯一补给 */
        /* v2.4：过关奖励立刻到账，给玩家一个明确的反馈 */
        pickupMsg = (g_lang == LANG_ZH) ? "过关奖励  +能量 +理智" : "LEVEL CLEAR  +ENERGY +SANITY";
        pickupMsgTimer = 2.2f;
                if (energy > ENERGY_MAX) energy = ENERGY_MAX;
                GenerateLevel();
                floorStartTime=runTime;
                ClearFx();
                for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
                flashTime = 0.15f;
                UpdateExplored();
                return;
            }
            carryGold += CampaignReward(campaignStage);
            if (campaignStage < CAMPAIGN_STAGE_COUNT - 1) {
                campaignStage++;
                currentLevel=CampaignStartFloor(campaignStage);
                srand((unsigned)(9000+campaignStage*733));
                GenerateLevel(); floorStartTime=runTime;
                flashTime=0.15f; UpdateExplored(); SaveGame();
                return;
            }
            gameWon = 1;
            gameCleared = 1;   /* v28：跨局记录，重启后 Endless 仍然解锁 */
            runInProgress = 0;
            /* v2.2：战役通关原来**没有 ComputeScore**，结算页显示上一局残留，
               也不给核心。现在补上。 */
            {
                ScoreInput si = { currentLevel, 1, 0, runSearched, bottlesKilled,
                                  runTime, modsSeenCount };
                ComputeScore(&si, &lastScore);
                lastScore.score += 5000;      /* 战役通关固定加成 */
            }
            dailyEscapeCount++; DailyObserve(DQ_ESCAPE,dailyEscapeCount);
            CheckAchievements(); BankSettle(1); hasSave=0;
            campaignMode = 0;
            currentState = STATE_GAMEOVER;
            menuSelection = 0;
            ForceCursor(1);
            uiDirty = 1;
            return;
        }
        /* v2.2：每日挑战通关 —— 今天第一次给 1 核心 */
        if (dailyMode && !dailyClearedToday) {
            dailyClearedToday = 1;
            carryGold += GOLD_PER_CORE;   /* v3.4 */
        }
        if (currentLevel >= WIN_LEVEL && !isEndlessRun) {
            /* 主线通关：走进第 10 层的出口就是结局，不再生成下一层 */
            gameWon = 1;
            gameCleared = 1;   /* v28：跨局记录，重启后 Endless 仍然解锁 */
            runInProgress = 0;
            /* Settle after all victory rewards and achievement checks. */
            currentState = STATE_GAMEOVER;
            menuSelection = 0;
            ForceCursor(1);
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            /* 通关时检查成就 */
            {
                /* v1.8：主线固定分 + 通关给 1 个核心 */
                ScoreInput si = { currentLevel, 1, 0, runSearched, bottlesKilled, runTime, modsSeenCount };
                ComputeScore(&si, &lastScore);
                if (lastScore.score > bestMainScore) bestMainScore = lastScore.score;
                carryGold += GOLD_PER_CORE;   /* v3.4 */
            }
            UnlockAchievement(1);  /* FIRST EXIT（原来 2 = FLOOR 5，解错了） */
            { extern int dailyEscapeCount; dailyEscapeCount++; }   /* v7：每日「撤离 N 次」 */
            if (sanity >= SanityMax()-0.01f) UnlockAchievement(14);  /* PERFECT ESCAPE */
            /* v2.8：属性面板删了，成就改成「技能树普通节点全满」 */
            if (SkillsAllMaxed()) {
                UnlockAchievement(25);  /* TREE MASTER（原来 15 = DISRUPTOR） */
            }
            /* v3.3 **修复 3 个永远解不开的通关成就** ——
               它们的判定写在 CheckAchievements 里（gameWon && ...），
               但 CheckAchievements 只在 STATE_PLAYING 执行，
               而本函数上面第 3485 行已经把 currentState 设成 GAMEOVER 了，
               所以这三条判定从来没有被求值过。
               在这里用同样的条件补一次。 */
            /* v7 第 ㊿ 轮：这三条原来用旧 40 条表的索引
               39 / 35 / 36 —— 新表只有 32 条，越界的被静默丢弃。
               39(TRUE ENDING) 和 35(BLOOD PRICE) 在新表里没有对应项，删掉；
               36(GHOST RUN) → 20(LIGHTLESS)，语义完全一致。 */
            if (!runLightUse)        UnlockAchievement(20);  /* LIGHTLESS */
            DailyObserve(DQ_ESCAPE,dailyEscapeCount);
            CheckAchievements(); BankSettle(1); hasSave=0;
            uiDirty = 1;
            return;
        }
        /* v2.1：**主线也回智**。原来只有无尽模式给，导致主线比无尽还穷，
           第 4~6 层必死 —— 一条 10 层的主线硬生生变成了不可能。
           无尽给 30（原 12 的 2.5 倍），主线给 18。 */
        {
            /* v3.0 平衡：**层清奖励随层涨** ——
       原来恒定 +18，而每层理智流失涨 0.012/秒，
       深层等于是净亏，第 24 层每层净掉 30+ 理智。 */
    /* v3.0 平衡修正：**奖励必须涨得比流失慢**。
       流失每层 +0.012/秒 = 每层 +0.72 点/分钟，
       上一版给 +2 点/层 → 净 +1.28/层，深层反而越玩越赚，难度曲线是反的。
       改成 +0.5 点/层：第 1 层净 -12、第 24 层净 -17，越深越紧。 */
    float gain = isEndlessRun ? (LEVEL_CLEAR_SANITY + 18.0f)
                              : (18.0f + (float)currentLevel * 0.5f);
            sanity += gain;
            if (sanity > SanityMax()) sanity = SanityMax();
        }
        if (isEndlessRun) {
            /* ---- v1.8：过关上缴（重生细胞式）---- */
            bankStreak++;
            /* v6：每日任务进度 —— 到达层数 / 箱子数 */
            { extern int DailyGoalId(void), DailyGoalParam(void);
              extern void DailySetProgress(int);
              extern int currentLevel;
              if (DailyGoalId() == 2) DailySetProgress(currentLevel);
            }
            /* Retain carried gold until the player chooses extraction. */
            /* 记分 + 里程碑核心 */
            {
                ScoreInput si = { currentLevel, 0, 1, runSearched, bottlesKilled, runTime, modsSeenCount };
                ComputeScore(&si, &lastScore);
                lastScore.score += eventBonus;
    /* 2026-11-05：宝箱怪 —— 每甩掉一只 +400，一局上限 2 只。
       和事件加分走同一条通道（结算页不单独列一行）。 */
    { extern int mimicsEscapedThisRun;
      lastScore.score += MIM_SCORE_BONUS * mimicsEscapedThisRun; }   /* v1.8：事件加分 */
                if (currentLevel > bestEndlessLevel) bestEndlessLevel = currentLevel;
                if (lastScore.score > bestEndlessScore) bestEndlessScore = lastScore.score;
            }
            if (currentLevel == 20 || currentLevel == 40 ||
                currentLevel == 60 || currentLevel == 80) carryGold += GOLD_PER_CORE;   /* v3.4 */
            /* v3.0：**每层 +35 能量**（数值审查 #1 的核心修复）——
               原来这行只写在战役分支里，主线/无尽从不补给，
               一局只有 19 刀的能量，第 2 层就开始「打不起」。 */
            energy += ENERGY_PER_FLOOR;
            if (energy > (float)EnergyMax()) energy = (float)EnergyMax();
            currentLevel++;
GenerateLevel();
        } else {
            /* v1.8：主线也一样上缴 */
            bankStreak++;
            /* v6：pending 已并入 carryGold，不再需要合并 */
            energy += ENERGY_PER_FLOOR;                        /* v3.0：主线每层补给 */
            if (energy > (float)EnergyMax()) energy = (float)EnergyMax();
            currentLevel++;
            GenerateLevel();
        }
        /* ==================================================================
         * v6 第 ㊲ 轮：**局内三选一技能终于会出现了**
         * ==================================================================
         * 全量审计发现 OpenSkillPick() **全工程 0 调用点** ——
         * 这个界面（STATE_SKILL_PICK）、选项生成、按键处理、
         * ui.c 的绘制，全都在，**但没有任何东西会打开它**。
         * 一个完整的 Roguelite 系统就这么躺在代码里没人用。
         *
         * 设计：每通关 2 层给一次三选一。
         * 插在这里（而不是某个分支里），主线 / 下潜两条路都会经过。
         * ================================================================ */
        /* ==================================================================
         * v7 第 59 轮：**12 条遗物一条都拿不到**
         * ==================================================================
         * `OpenRelicPick()` 全工程 0 调用点 —— 和上面 OpenSkillPick 一模一样的病：
         * 选项池、pickMode==1、STATE_SKILL_PICK 的绘制与按键、RelicTake()
         * 全都写好了，**但没有任何东西会打开它**。
         * 结果 8 个 Relic*Mul() 恒返 1.0，遗物系统整条不存在。
         *
         * 设计：每 3 层给一次遗物三选一（比技能稀有），
         *       和技能错开层级（技能在偶数层，遗物在 3 的倍数层）。
         * ================================================================ */
        if (currentLevel % 3 == 0) {
            pendingSkillDraft = (currentLevel % 2 == 0);
            extern void OpenRelicPick(void);
            OpenRelicPick();
        } else if (currentLevel % 2 == 0) {
            extern void OpenSkillPick(void);
            OpenSkillPick();
        }

    floorSpotted = 0;
    /* v3.3：换层时清空胶水，否则上一层扔的会残留到下一层 */
    for (int gi = 0; gi < MAX_GLUE; gi++) { glues[gi].active = 0; glues[gi].life = 0.0f; }
        floorTookDamage = 0;
        floorStartTime = runTime;
        CheckAchievements();

        /* v1.8：通过本层 → 事件加分进本局总分 */
        if (evThisLevel >= 0) eventBonus += EventScore(evThisLevel);

        /* v3.3：取消每层/每两层的三选一。玩家明确要求不再弹 CHOOSE ONE。 */

        /* 重置本层/本局的成就追踪变量 */
        sprintedThisLevel = 0;
        fusesCollectedRun = 0;
        flashlightOffLevel = 0;
        /* v3.3：mapsFullyExplored 不再每层清零 ——
       否则一个 24 层的主线最多只有 1，MAP MASTER(>=3) 永远解不开。 */
        ClearFx();                    /* 换关解除所有状态效果 */
        for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
        flashTime = 0.15f;
        UpdateExplored();
    }

    /* 新手教程：放在最后，保证所有交互/移动状态都已经更新完 */
    if (trainingMode) TutorialUpdate(dt);
}
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
    pendingStartMode = START_DAILY;
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
    pendingStartMode = START_CAMPAIGN;
    pendingCampaignStage = stage;
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
float CalibZoneScale(void) {
    float t = (float)(currentLevel - 1);
    if (t < 0.0f) t = 0.0f;
    float k = 1.0f - t * CALIB_ZONE_SHRINK;
    if (k < CALIB_ZONE_MIN) k = CALIB_ZONE_MIN;   /* 下限 */
    return k;
}

/* v2.6：区域缩到下限之后，多出来的难度转到滑块速度（1.0 = 正常） */
float CalibSpeedScale(void) {
    float t = (float)(currentLevel - 1);
    if (t < 0.0f) t = 0.0f;
    /* 区域开始触底的那一层：1 - t*shrink = MIN  →  t = (1-MIN)/shrink */
    float tFloor = (1.0f - CALIB_ZONE_MIN) / CALIB_ZONE_SHRINK;
    if (t <= tFloor) return 1.0f;
    float extra = (t - tFloor) * CALIB_SPEED_GROW;
    float s = 1.0f + extra;
    if (s > CALIB_SPEED_MAX) s = CALIB_SPEED_MAX;  /* 上限 */
    return s;
}

void CalibStart(void) {
    if (calibActive) return;
    /* v3.0：每层只能校准一次 */
    /* v3.0：**校准只能在藏身点里做** ——
       原来在安全屋里按空格，现在改成「藏进柜子/床底/桌下之后按空格」。
       理由：逃生玩法里藏身处本身就是「喘息点」，而校准是最贴合它的动作；
       安全屋改成纯粹的补给站。 */
    {
        extern int hiddenIn;
        if (hiddenIn < 0) {
            pickupMsg = g_lang == LANG_ZH ? "要躲在藏身点里才能校准"
                                          : "HIDE FIRST, THEN CALIBRATE";
            pickupMsgTimer = 1.4f;
            return;
        }
    }
    /* v3.0：**删掉「每层 1 次」限制** ——
       用户要求「冷却改成 30 秒」，但这里还有一条按层锁死的检查：
       同一层第二次按空格直接提示 ALREADY CALIBRATED THIS LEVEL，
       30 秒冷却根本没机会生效。
       现在只靠冷却控制节奏。 */
    /* (calibUsedLevel 检查已移除) */
    /* v2.6：**取消次数限制** —— 只靠 60 秒冷却控制节奏 */
    /* 2026-11-05：**每层限 CALIB_PER_LEVEL 次**。
       原来只有冷却、没有次数上限（CALIB_PER_LEVEL 是 99），
       躲在柜子里可以无限回理智。 */
    if (calibUsesThisFloor >= CALIB_PER_LEVEL) {
        pickupMsg = g_lang == LANG_ZH ? "本层校准已用完" : "NO CALIBRATION LEFT THIS FLOOR";
        pickupMsgTimer = 1.4f;
        return;
    }
    if (calibCooldown > 0.0f) {
        char* m = (char*)(g_lang == LANG_ZH ? "校准冷却中" : "CALIBRATION COOLING");
        pickupMsg = m;
        pickupMsgTimer = 1.2f;
        return;
    }
    calibActive = 1;
    calibPos = 0.0f;
    calibDir = 1.0f;
    calibElapsed = 0.0f;      /* v8：从 0 开始计时，锁定前至少要跑一小段 */
    /* 目标区随机，但不贴着两端（否则没得玩） */
    calibZoneC = 0.22f + (float)(rand() % 57) / 100.0f;   /* 0.22 ~ 0.78 */
    calibResult = 0;
    uiDirty = 1;
}

void CalibLock(void) {
    if (!calibActive) return;
    /* v8：刚按下开始就锁定的双击/自动重复，直接忽略 —— 否则滑块还停在
       0 附近，必然判成「失手」。0.12 秒足够挡住同一次按下的重复消息，
       又不会影响正常操作。 */
    if (calibElapsed < 0.12f) return;
    calibActive = 0;
    calibCooldown = CALIB_COOLDOWN;   /* v2.6：只上冷却 */
    calibUsedLevel = currentLevel;    /* v3.0：记下本层用过了 */
    calibUsesThisFloor++;             /* 2026-11-05：本层计数，配合 CALIB_PER_LEVEL */

    float d = calibPos - calibZoneC;
    if (d < 0.0f) d = -d;
    float zs = CalibZoneScale();          /* v2.6：目标区随层数缩小 */
    float perfW = CALIB_PERFECT_W * zs;
    float zoneW = CALIB_ZONE_W * zs;

    /* 2026-11-05 重新定价：**所有奖励随层数成长**，和箱子金币曲线对齐。
       完美：理智 12+2×层（封顶 34） / 金币 20+4×层 / 电量 +15
       良好：理智  6+1×层（封顶 18） / 金币 10+2×层 / 电量  +8
       （energy 属于已下线的武器系统，不再给） */
    int   lvNow   = currentLevel > 0 ? currentLevel : 1;
    float sanP    = CALIB_SANITY_BASE + CALIB_SANITY_STEP * (float)lvNow;
    float sanG    = CALIB_GOOD_SANITY_BASE + CALIB_GOOD_SANITY_STEP * (float)lvNow;
    if (sanP > CALIB_SANITY_MAX)      sanP = CALIB_SANITY_MAX;
    if (sanG > CALIB_GOOD_SANITY_MAX) sanG = CALIB_GOOD_SANITY_MAX;
    int   goldP   = GOLD_CALIB_PERFECT + GOLD_CALIB_PERFECT_STEP * lvNow;
    int   goldG   = GOLD_CALIB_GOOD    + GOLD_CALIB_GOOD_STEP    * lvNow;

    if (d <= perfW) {
        calibResult = 1;
        sanity += sanP;
        battery += CALIB_BATTERY_PERFECT;
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        if (sanity > SanityMax()) sanity = SanityMax();
        /* v3.4：完美校准额外给金币 —— 高技能动作应该比搜箱子值钱。
           **注意**：pickupMsg 是 `const char*`（指向字符串字面量），
           不能 sprintf 进去 —— 那是往只读段写，直接访问违例。
           必须走静态缓冲区（和 DrawSaveStatusBanner 那次踩的坑一模一样）。 */
        {
            static char calibMsg[96];
            int gld = (int)((float)goldP * RelicShardMul() * modShardMul * SkillGoldMul() + 0.5f);
            carryGold += gld;
            totalGoldEarned += gld;
            snprintf(calibMsg, sizeof(calibMsg),
                     g_lang == LANG_ZH ? "完美校准  +%d 理智  +%d 金币  +%d 电"
                                       : "PERFECT  +%d SANITY  +%d GOLD  +%d BAT",
                     (int)(sanP + 0.5f), gld, (int)CALIB_BATTERY_PERFECT);
            pickupMsg = calibMsg;
        }
        PlaySfx(SFX_POWER, 1.0f);
    } else if (d <= zoneW) {
        calibResult = 2;
        sanity  += sanG;
        battery += CALIB_BATTERY_GOOD;
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        /* v3.4：良好校准也给金币（少一半） */
        {
            int gld = (int)((float)goldG * RelicShardMul() * modShardMul * SkillGoldMul() + 0.5f);
            carryGold += gld;
            totalGoldEarned += gld;
            /* 文案原来写死 "+11 理智"，而实际给的是 CALIB_GOOD_SANITY(15) ——
               数字改了文案没改。现在统一用一个缓冲区。 */
            static char calibMsgG[96];
            snprintf(calibMsgG, sizeof(calibMsgG),
                     g_lang == LANG_ZH ? "校准成功  +%d 理智  +%d 金币  +%d 电"
                                       : "CALIBRATED  +%d SANITY  +%d GOLD  +%d BAT",
                     (int)(sanG + 0.5f), gld, (int)CALIB_BATTERY_GOOD);
            pickupMsg = calibMsgG;
        }
        if (sanity > SanityMax()) sanity = SanityMax();
        PlaySfx(SFX_POWER, 0.7f);
    } else {
        /* v2.9：**失手不给任何能量**（原来也是 0，这里写明防以后改坏） */
        calibResult = 3;
        pickupMsg = g_lang == LANG_ZH ? "校准失手" : "MISSED";
        PlaySfx(SFX_GLASS, 0.6f);
    }
    calibResultTimer = 1.6f;
    pickupMsgTimer = 1.6f;
    uiDirty = 1;
}

void CalibTick(float dt) {
    if (calibCooldown > 0.0f) {
        calibCooldown -= dt;
        if (calibCooldown < 0.0f) calibCooldown = 0.0f;
    }
    if (calibResultTimer > 0.0f) {
        calibResultTimer -= dt;
        if (calibResultTimer < 0.0f) calibResultTimer = 0.0f;
    }
    /* v2.4：次数**按层发放**。
       原来是在「进店」那一刻重置，于是出门再进来就又满 3 次 ——
       而安全屋会回理智，站够冷却就能无限刷能量和理智。 */
    /* v2.6：取消次数限制后，这里只需要保证「每层进来时不要残留上一次的滑块状态」 */
    if (calibLevel != currentLevel) {
        calibLevel = currentLevel;
        calibActive = 0;
        calibUsesThisFloor = 0;    /* 2026-11-05：换层重置「本层校准次数」 */
        uiDirty = 1;
    }

    if (!calibActive) return;
    /* v8：离开藏身点/换层后不能把上一次的滑块状态带进来 ——
       否则下次在垃圾桶里按空格会直接锁定一个残留位置，表现为「一按就失手」。 */
    if (hiddenIn < 0 && currentState == STATE_PLAYING) {
        calibActive = 0;
        calibElapsed = 0.0f;
        calibResult = 0;
        return;
    }
    calibElapsed += dt;
    /* 滑块每帧都在动，必须每帧重画界面（否则会留下残影） */
    uiDirty = 1;
    calibPos += calibDir * dt / (CALIB_SWEEP / CalibSpeedScale());   /* v2.6：深层更快 */
    if (calibPos >= 1.0f) { calibPos = 1.0f; calibDir = -1.0f; }
    if (calibPos <= 0.0f) { calibPos = 0.0f; calibDir = 1.0f; }
}

void SyncWindowSize(void) {
    if (!g_hwnd) return;
    RECT cr;
    if (!GetClientRect(g_hwnd, &cr)) return;
    if (cr.right > 0 && cr.bottom > 0) {
        clientW = (int)cr.right;
        clientH = (int)cr.bottom;
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
static void ResetModeFlags(void) {
    isEndlessRun = 0;
    trainingMode = 0;
    dailyMode = 0;
    campaignMode = 0;
    campaignStage = 0;
    /* v28：教程状态是「训练模式」的一部分，模式清零时必须一起清。
       原来退出教程只把 trainingMode 置 0，tutorialStep/Done 会留到下一次
       进入对局；如果 trainingMode 又被别的路径带回来，就会在
       每日/战役地图上继续跑教程步骤。 */
    tutorialStep = 0;
    tutorialDone = 0;
    tutorialGuideOn = 0;
    tutorialTimer = 0.0f;
    tutorialActionTimer = 0.0f;
}

/* v28：**离开一局 / 读档前统一清理运行时模式状态**。
   必须在 SaveGame() 之后调用（先让存档记下 daily/campaign 模式），
   否则会把「退出时还在每日挑战里」这件事也清掉，Continue 就回不去了。 */
void ResetRunModeFlags(void) {
    ResetModeFlags();
    runInProgress = 0;
    exitChoiceOpen = 0;
    exitGoDeeper = 0;
    { extern int lockActive; extern void CloseLockOverlay(void);
      if (lockActive) CloseLockOverlay(); }
}

static void ApplyCampaignRules(int stage) {
    /* v6：改成**表驱动** —— 规则细节全在 campaign.c 的 kCampaign[] 里，
       这里只把环境倍率乘进去。加一关只要加一行表。 */
    extern const CampaignDef kCampaign[];
    extern int campaignStage, campaignMode;
    (void)stage;
    if (!campaignMode) return;
    if (campaignStage < 0 || campaignStage >= CAMPAIGN_STAGE_COUNT) return;
    {
        const CampaignDef* d = &kCampaign[campaignStage];
        modSpeedMul   *= d->speedMul;
        modSanityMul  *= d->sanityMul;
        modBatteryMul *= d->batteryMul;
        modShardMul   *= d->goldMul;
    }
}

void StartGame(void) {
    /* v6：**出口弹窗状态必须重置** —— 否则暂停→主菜单→新局会带着
       上一次的弹窗标志进来，开局即被冻结。 */
    exitChoiceOpen = 0;
    exitGoDeeper = 0;
    /* v3.0：**改造树 / 强化都不重置** ——
       它们花的是 carryGold（跨局持久的元货币），重置等于把玩家买的东西吞掉：
       买一个 620 的星节点，下一局归零，金币永久损失。
       benchOwned / runUpg 都已进存档（benchOwnedSave / runUpgSave），
       本来就是元进度。每局该清的只有「假死」这类局内一次性状态。 */
    playDeadUsed = 0;   /* v3.0 假死每局重置 */
    /* v3.0：这几个也必须在开局清零，否则上一局用过之后新局直接失效 */
    calibUsedLevel = -1;
    calibActive = 0;
    calibElapsed = 0.0f;
    calibStartLatch = 0;
    lastResortUsed = 0;
    secondWindTimer = 0.0f;
    vanished = 0;
    vanishTimer = 0.0f;
    vanishHold = 0.0f;


    int startMode = pendingStartMode;
    int startStage = pendingCampaignStage;
    pendingStartMode = START_NORMAL;
    pendingCampaignStage = 0;

    ResetModeFlags();   /* v2.2：先清模式标志 */
    { extern void SkillInitOnce(void); SkillInitOnce(); }   /* v6：技能树中心永远点亮 */
    { extern void CatchCountResetRun(void); CatchCountResetRun(); }   /* v6：本局被捕计数清零 */
    if (startMode == START_ENDLESS) {
        isEndlessRun = 1;
    } else if (startMode == START_DAILY) {
        dailyMode = 1;
    } else if (startMode == START_CAMPAIGN) {
        campaignMode = 1;
        campaignStage = startStage;
    } else if (startMode == START_TRAINING) {
        trainingMode = 1;
    }

    currentState = STATE_PLAYING;
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
    /* v3.4 银行：按选择的档位从存款里取出本金 */
    {
        int p = bankCarryPct;
        if (p < 0) p = 0;
        if (p >= BANK_CARRY_STEPS) p = BANK_CARRY_STEPS - 1;
        int take = (int)((float)bankGold * (float)(p * BANK_STEP_PCT) / 100.0f + 0.5f);
        {   /* v6：取款走 Wallet（银行 → 携带） */
            extern int WalletWithdraw(int);
            take = WalletWithdraw(take);
        }
        carryGold = take;          /* 局内花的就是这笔 */
        carryAtRunStart = take;    /* v6：记录基线，用于算净赚 */
        /* v6：开局把今天的每日任务打出来 */
        {
            extern const char* DailyGoalTextEn(void);
            extern const char* DailyModTextEn(void);
            extern const char* DailyRestrictTextEn(void);
            extern int DailyDone(void);
            static char db[160];
            if (!DailyDone()) {
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
        { extern void ThreatReset(void); ThreatReset(); }   /* v6：每局威胁归零 */
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
        { extern void BenchResetRun(void); BenchResetRun(); }
        RunUpgReset();
    }
    throwSelected = 0;
    runInProgress = 1;
    runTime = 0.0f;
    /* v3.3：这两个不重置会导致——
         floorStartTime：第 1 层 floorTime = runTime - 0 之外的残留值，
                         可能出现负数 → 「90 秒内清完」白送
         lowSanitySeconds：上一局攒的秒数直接算进本局 → LAST BREATH 白送 */
    {
        extern void ShopResetCooldowns(void);
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
    lastResortUsed = 0;
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
    s_decoyKeyCd = 0.0f;   /* 新局冷却清零 */
    leechHintTimer = 0.0f;
    ResetLeeches();
    RunSkillsReset();

    /* 重置成就追踪变量 */
    knockCountRun=throwHitCountRun=echoCellsTotalRun=0;
    { extern void PhaseReset(void); PhaseReset(); }   /* 2026-11-06：相位信标按局重置 */
    { extern int mimicsEscapedThisRun; mimicsEscapedThisRun = 0; }
    floorsNoSanityLoss=dailyEscapeCount=wardenCleanFloors=fullHideDone=0;
    doubleHuntSeconds=0.0f; pendingSkillDraft=0;
    totalGoldEarned = 0;
    bottlesKilled = 0;
    faceDodged = 0;
    crankTimeTotal = 0;
    fusesCollectedRun = 0;
    mapsFullyExplored = 0;
    tookDamage = 0;
    { extern int contractKind,shopContractUsed; contractKind=1;shopContractUsed=0; }
    flashlightOffLevel = 0;
    sprintedThisLevel = 0;
    hideCount = 0;
    totalThrown = 0;
    sprintedAnyFloor = 0;
    catchBreakUsed = 0; runSpotted = 0; runLightUse = 0;   /* v3.4 新局清零 */
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

/* v6：InitAchievements 搬到 achievements.c（表驱动）*/

/* 成就辅助：解锁并保存 */
/* v6：UnlockAchievement 搬到 achievements.c（含 Steam 上报）*/
static void UnlockAchievement(int idx) { extern void AchUnlock(int); AchUnlock(idx); }

/* v6：改转发 */

/* v6：成就检查改成**表驱动** ——
   把游戏里的值打成快照，交给 achievements.c 按 kind 比对。
   加一个成就只需要在 kAchDefs[] 里加一行，这里永远不用改。 */
extern int CatchCountThisFloor(void);
static void CheckAchievements(void) {
    if(trainingMode) return;
    AchSnap s;
    memset(&s, 0, sizeof(s));
    s.level            = currentLevel;
    s.gameWon          = gameWon;
    s.totalGoldEarned  = totalGoldEarned;
    s.carryGold        = carryGold;
    { extern int bankGold; s.bankGold = bankGold; }
    s.totalThrown      = totalThrown;
    s.hideCount        = hideCount;
    /* v7 第 ㊿ 轮：**这三个原来写死 0**
       → DISRUPTOR(击退20次) / PITCHER(投掷命中10次) / CARTOGRAPHER(回声100格)
         这三个成就永远解不开。现在读真实计数器。 */
    { extern int knockCountRun, throwHitCountRun, echoCellsTotalRun;
      s.knockCount    = knockCountRun;
      s.throwHitCount = throwHitCountRun; }
    { extern int bestiarySeen; s.bestiarySeenMask = bestiarySeen; }
    { extern int SkillNodesLit(void); s.skillNodesLit = SkillNodesLit(); }
    { extern float ThreatValue(void); s.maxThreatThisRun = (int)ThreatValue(); }
    { extern int echoCellsTotalRun; s.echoCellsTotal = echoCellsTotalRun; }

    /* flags：特殊条件（0 未用）
       1 进入过游戏 / 2 存活10分 / 3 存活30分 / 4 抢回赃款3次
       5 零被抓撤离 / 6 完美逃脱 / 7 守门人三层 / 8~14 见 kAchDefs 注释 */
    s.flags[1] = !trainingMode;
    if (gameWon && CatchCountThisRun() == 0) s.flags[5] = 1;
    /* v7 第 ㊿ 轮：TEN MINUTES / HALF HOUR 的 flag 从来没人置位 */
    if (runTime >= 600.0f)  s.flags[2] = 1;
    if (runTime >= 1800.0f) s.flags[3] = 1;
    /* 撤离一次 → FIRST EXIT（独立 flag 15，不再和 FIRST BREATH 共用一个） */
    if (gameWon) s.flags[15] = 1;
    s.flags[6]=gameWon && sanity>=SanityMax()-0.01f;
    s.flags[7]=wardenCleanFloors>=3;
    s.flags[10]=gameWon && WardenActive() && WardenWatching();
    s.flags[11]=doubleHuntSeconds>=30.0f;
    s.flags[12]=currentLevel>=12 && carryGold>=300000;
    s.flags[14]=fullHideDone;
    /* 无光撤离 / 空手撤离 / 完美撤离 */
    if (gameWon && !runLightUse)        s.flags[9] = 1;
    if (gameWon && runSearched == 0)     s.flags[8] = 1;
    /* v7 第 56 轮：最后两个没有置位点的 flag */
    { extern int bountyRecovered;
      if (bountyRecovered >= 3) s.flags[4] = 1; }              /* CLAWBACK */
    { extern int metaResetDone;
      if (metaResetDone) s.flags[13] = 1; }                    /* META RESET */

        /* ==================================================================
     * v7 第 ㊿ 轮：**每日任务 8 个目标，原来只有 1 个会推进**
     * ==================================================================
     * 旧代码：DailySetProgress() 全项目只有一个调用点，
     *         条件是 `if (DailyGoalId() == 2)` 且包在 `if (isEndlessRun)` 里。
     *         → 抽到「开 40 个箱子」的玩家，HUD 永远显示 0/40。
     *
     * 现在每帧把当前局的状态映射到对应目标上。
     * DailySetProgress 是**取最大值**的，所以传累计值即可。
     * ================================================================ */
    {
        extern int DailyGoalId(void);
        extern void DailySetProgress(int);
        extern int totalGoldEarned, runSearched;
        extern int dailyEscapeCount, floorsNoSanityLoss;
        int v = 0;
        switch (DailyGoalId()) {
        case 0: v = totalGoldEarned;                        break;  /* 收集金币 */
        case 1: v = dailyEscapeCount;                       break;  /* 撤离次数 */
        case 2: v = currentLevel;                           break;  /* 到达层数 */
        case 3: v = runSearched;                            break;  /* 开箱数 */
        case 4: v = knockCountRun;                          break;  /* 击退 */
        case 5: v = throwHitCountRun;                       break;  /* 投掷命中 */
        case 6: v = (int)(runTime / 60.0f);                 break;  /* 存活分钟 */
        case 7: v = floorsNoSanityLoss;                     break;  /* 无伤层数 */
        default: break;
        }
        DailyObserve(DailyGoalId(),v);
    }

extern void AchCheck(const AchSnap*);
    AchCheck(&s);

    /* v7 第 ㊿ 轮：这里原来 `if (gameWon) s.flags[2] = 0;`
       写在 AchCheck() **之后**，是空操作；而且 flags[2] 现在是
       TEN MINUTES 的标记，清掉它就是错。已删除。 */
}

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
    { extern int lockActive; extern void CloseLockOverlay(void);
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
    mapHeld = 0;
    ForceCursor(1);
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    uiDirty = 1;
}

void ApplyDisplayMode(void) {
    if (isFullscreen) {
        winW = GetSystemMetrics(SM_CXSCREEN);
        winH = GetSystemMetrics(SM_CYSCREEN);
    } else {
        if (resIndex == RES_AUTO) {
            RECT wa;
            SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
            int aw = (int)((wa.right - wa.left) * 0.88f);
            int ah = (int)((wa.bottom - wa.top) * 0.88f);
            int pick = 0;
            for (int i = RES_AUTO - 1; i >= 0; i--) {
                if (resolutions[i][0] <= aw && resolutions[i][1] <= ah) { pick = i; break; }
            }
            resIndex = pick;    /* AUTO 存的是选出来的实际档位 */
        }
        winW = resolutions[resIndex][0];
        winH = resolutions[resIndex][1];
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
}

void ApplySetting(int item, int value) {
    if (item < 0 || item >= SETTING_COUNT) return;
    /* 滑块项的 value 是 NULL（它们走 fvalue），这里只处理列表型。
       以前没做判断，靠调用方保证只传列表项；现在显式拦下来。 */
    if (settings[item].kind != SET_LIST || !settings[item].value) return;
    if (value < 0 || value >= settings[item].count) return;
    /* 按键绑定项存的是 VK 码（从 optKeyVKs 映射），其它列表项存下标 */
    int act = BindActionOfItem(&settings[item]);
    if (act >= 0) {
        g_bindings[act] = optKeyVKs[value];
    } else {
        *settings[item].value = value;
    }
    if (settings[item].value == &resIndex || settings[item].value == &isFullscreen) ApplyDisplayMode();
    else if (settings[item].value == &renderScaleIndex) InitPixels();
    else if (settings[item].value == &fovIndex) ApplyFov();
    SaveGame();   /* 设置改完立刻落盘，不再等退出设置页 */
    uiDirty = 1;
}

void ActivateMenuItem(void) {
    /* v3.4：Hub 页 0 的「携带档位」行 —— 回车循环 25/50/75/100% */
    /* v6：银行页回车 = 切换携带比例 */
    if (currentState == STATE_BANK) {
        extern int bankCarryPct;
        bankCarryPct = (bankCarryPct + 1) % BANK_CARRY_STEPS;
        PlaySfx(SFX_POWER, 0.7f);
        uiDirty = 1;
        return;
    }
    if (currentState == STATE_MAIN_MENU) {
        int base = hasSave ? 1 : 0;
        int endlessUnlocked = EndlessUnlocked();
        int sel = menuSelection;

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
            pendingStartMode = START_ENDLESS;
            StartGame();
        } else if (sel == ixNew) {
            /* v2.7：普通 New Run 必须清掉两个模式标记 */
            isEndlessRun = 0;
            trainingMode = 0;
            dailyMode = 0;
            campaignMode = 0;
            DeleteSave();
            weaponPickSel = 0;
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
            pendingStartMode = START_TRAINING;
            StartGame();
            uiDirty = 1;
        } else {
            SaveGame();
            PostQuitMessage(0);
        }
    } else if (currentState == STATE_SAFEHOUSE) {
        ShopActivate();
        uiDirty = 1;
    } else if (currentState == STATE_WEAPON_PICK) {
        /* v2.0：确定初始武器 → 开一局 */
        if (weaponPickSel < 0) weaponPickSel = 0;
        if (weaponPickSel >= WPN_COUNT) weaponPickSel = WPN_COUNT - 1;
        curWeapon = weaponPickSel;
        StartGame();
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
            hasSave = 1;
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

/* ---- v1.8 局内技能三选一 ---- */
void OpenSkillPick(void) {
    int pool[INRUN_COUNT];
    int n = 0;
    for (int i = 0; i < INRUN_COUNT; i++)
        if (RunSkillPickable(i, currentLevel)) pool[n++] = i;
    if (n <= 0) return;
    for (int k = 0; k < INRUN_CHOICES; k++) {
        if (n <= 0) { skillPickOptions[k] = -1; continue; }
        int pick = rand() % n;
        skillPickOptions[k] = pool[pick];
        pool[pick] = pool[--n];
    }
    skillPickSel = 0;
    pickMode = 0;
    prevState = currentState;
    currentState = STATE_SKILL_PICK;
    ForceCursor(1);
    uiDirty = 1;
}

/* v2.0：遗物三选一（复用同一块界面） */
void OpenRelicPick(void) {
    int pool[RELIC_COUNT];
    int n = 0;
    for (int i = 0; i < RELIC_COUNT; i++) {
        if (relicOwned[i] >= 3) continue;      /* 同类最多 3 层 */
        pool[n++] = i;
    }
    if (n <= 0) return;
    for (int k = 0; k < RELIC_CHOICES; k++) {
        if (n <= 0) { skillPickOptions[k] = -1; continue; }
        int pick = rand() % n;
        skillPickOptions[k] = pool[pick];
        pool[pick] = pool[--n];
    }
    skillPickSel = 0;
    pickMode = 1;
    prevState = currentState;
    currentState = STATE_SKILL_PICK;
    ForceCursor(1);
    uiDirty = 1;
}

static void ApplyInstantSkill(int id) {
    switch (id) {
    case IR_BATTERYPACK:
        battery += 45.0f;
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        break;
    case IR_BOTTLE:
        if (bottles < BottleCap()) bottles++;
        break;
    case IR_FLARE: flares++; break;
    case IR_MEDKIT:
        sanity += 25.0f;
        if (sanity > SanityMax()) sanity = SanityMax();
        break;
    case IR_SHARDBAG: carryGold += 40; break;
    /* v2.0 新增 */
    case IR_FIELDREPAIR:
        battery += 35.0f;
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        sanity -= 5.0f; if (sanity < 0.0f) sanity = 0.0f;
        break;
    case IR_NERVESHOT:
        sanity += 15.0f; if (sanity > SanityMax()) sanity = SanityMax();
        battery -= 10.0f; if (battery < 0.0f) battery = 0.0f;
        break;
    case IR_PACKRAT:
        if (bottles < BottleCap()) bottles++;
        if (bottles < BottleCap()) bottles++;
        carryGold += 30;
        break;
    case IR_OILCAN:
        battery += 25.0f;
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        stamina += 20.0f;
        if (stamina > StaminaMax()) stamina = StaminaMax();
        break;
    case IR_LUCKYFUSE:
        fusesHeld++;
        break;
    case IR_TIMESTOP: {
        /* v2.0：TIME STOP 原来是空卡（24 张里唯一一张没 case 的） */
        MonsterStunAll(8.0f);
        break;
    }
    case IR_STATICFIELD: {
        /* 大招：瘫痪本层所有怪 6 秒 */
        MonsterStunAll(6.0f);
        for (int i = 0; i < LEECH_MAX; i++) if (leeches[i].active) leeches[i].repelTimer = 6.0f;
        sanity -= 8.0f;
        if (sanity < 0.0f) sanity = 0.0f;
        break;
    }
    case IR_MISDIRECT: {
        /* 大招：把最近的怪瞬移走 */
        if (monster.active) {
            int x = 1 + rand() % (MAP_WIDTH - 2), y = 1 + rand() % (MAP_HEIGHT - 2);
            monster.x = x + 0.5f; monster.y = y + 0.5f;
            monster.state = 0; monster.pathLen = 0; monster.pathIdx = 0;
        }
        decoySelfTimer = 3.0f;
        break;
    }
    default: break;
    }
}

void CloseSkillPick(void) {
    int id = skillPickOptions[skillPickSel];
    if (id >= 0) {
        if (pickMode == 1) {
            RelicTake(id);
            PlaySfx(SFX_POWER, 0.9f);
        } else {
            RunSkillTake(id);
            ApplyInstantSkill(id);
        }
    }
    int wasRelic=pickMode;
    pickMode = 0;
    for (int k = 0; k < INRUN_CHOICES; k++) skillPickOptions[k] = -1;
    currentState = prevState;
    if (wasRelic && pendingSkillDraft) { pendingSkillDraft=0; OpenSkillPick(); return; }
    ForceCursor(0);
    /* 鼠标点的卡：回到局内时左键还按着，不清就会立刻开始蓄力、
       松手朝面前的墙扔一个道具（还带 14 半径噪声把怪引来）。 */
    resetInputState = 1;
    uiDirty = 1;
}

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
        extern const char* pickupMsg;
        extern float pickupMsgTimer;
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

void ActivateSettingRow(void) {
    int rows = SettingsRowCount();
    if (settingsSelection < 0 || settingsSelection >= rows) return;

    /* 目录页（根目录 / Key Bindings 子目录）：进分类或返回 */
    if (settingsPage == SETPAGE_ROOT || settingsPage == SETPAGE_BINDDIR) {
        const int* pages = (settingsPage == SETPAGE_ROOT) ? rootCategoryPages : bindCategoryPages;
        if (settingsSelection < 3) {
            settingsPage = pages[settingsSelection];
            settingsSelection = 0;
            uiDirty = 1;
        } else {
            /* Back：根目录回上一状态，子目录回根目录 */
            if (settingsPage == SETPAGE_BINDDIR) {
                settingsPage = SETPAGE_ROOT;
            } else {
                currentState = prevState;
                SaveGame();
            }
            settingsPopupOpen = 0;
            uiDirty = 1;
        }
        return;
    }

    int idx[SETTING_COUNT];
    int n = PageItems(idx);
    if (settingsSelection >= n) {
        /* 键位页 / Display / Gameplay 的 Back：回到上级目录 */
        settingsPage = (settingsPage == SETPAGE_GAME || settingsPage == SETPAGE_DISPLAY)
                       ? SETPAGE_ROOT : SETPAGE_BINDDIR;
        settingsSelection = 0;
        settingsPopupOpen = 0;
        uiDirty = 1;
        return;
    }

    popupItem = idx[settingsSelection];
    /* Reset to Defaults：一键恢复出厂（键位 + 全部设置项）。
       用哨兵地址识别，不走弹窗。 */
    if (settings[popupItem].value == &resetDefaultsFlag) {
        static const int defBindings[ACT_COUNT] = BINDINGS_DEFAULT;
        for (int a = 0; a < ACT_COUNT; a++) g_bindings[a] = defBindings[a];
        resIndex = 0; isFullscreen = 0; fpsCapIndex = 1; renderScaleIndex = 0;
        fovIndex = 2; vignetteOn = 1; showFps = 0; hudDetail = 1; qteHint = 1;
        gammaValue = 1.0f; mouseSens = 1.0f; volumeF = 0.8f; musicVolumeF = 0.8f;
        ApplyDisplayMode();
        ApplyFov();
        PlaySfx(SFX_POWER, 0.5f);
        SaveGame();
        uiDirty = 1;
        return;
    }
    /* 键位项：进入「按任意键绑定」模式，不再弹 54 项的选项列表
       （那个列表本身也远超一屏，只能盲选）。 */
    if (IsKeyBindItem(&settings[popupItem])) {
        rebindingAct = BindActionOfItem(&settings[popupItem]);
        rebindConflict = -1;   /* 新的一次改绑，清掉上一次的冲突确认 */
        uiDirty = 1;
        return;
    }
    if (settings[popupItem].kind == SET_SLIDER) {
        popupSelection = 0;
    } else {
        popupSelection = *settings[popupItem].value;
        if (popupSelection >= settings[popupItem].count) popupSelection = 0;
    }
    settingsPopupOpen = 1;
    sliderDragging = 0;
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
void SkillResetView(void);
void SkillDragStart(int mx, int my);
void SkillDragMove(int mx, int my);
void SkillDragEnd(void);
int  SkillIsDragging(void);

static void MouseToLogical(int* mx, int* my) {
    if (!g_hwnd) return;
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
    if (cw <= 0 || ch <= 0) return;
    *mx = *mx * winW / cw;
    *my = *my * winH / ch;
}

static void HitTestMenu(int mx, int my) {
    MouseToLogical(&mx, &my);
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

static void ClickMenu(int mx, int my) {
    MouseToLogical(&mx, &my);
    if (currentState == STATE_HELP) { currentState = STATE_MAIN_MENU; uiDirty = 1; return; }

    /* v1.8：主菜单 / 暂停菜单鼠标点击直接执行。
       用"行带"算下标，矩形差几像素也点得中；没落在行上就选最近的一行，
       保证点击永远有反馈（之前玩家反馈"暂停页鼠标点了没反应"）。 */
    if (currentState == STATE_MAIN_MENU || currentState == STATE_PAUSED) {
        MenuGeo g = GetMenuGeo();
        int idx = -1;
        if (g.rowH > 0 && my >= g.firstY) idx = (my - g.firstY) / g.rowH;
        if (idx < 0 || idx >= g.count) {
            int best = 0, bestD = 1 << 30;
            for (int i = 0; i < g.count; i++) {
                RECT r = RowRect(g, i);
                int d = abs(my - (r.top + r.bottom) / 2);
                if (d < bestD) { bestD = d; best = i; }
            }
            idx = best;
        }
        if (idx >= 0 && idx < g.count) {
            menuSelection = idx;
            ActivateMenuItem();
            uiDirty = 1;
        }
        return;
    }

        /* v2.2 修复：安全屋 / 选武器原来没有命中分支，掉进通用兜底 ——
       点哪一行执行的都是"光标原来那一行"（买错东西、直接用掉跳层）。 */
    if (currentState == STATE_SAFEHOUSE || currentState == STATE_WEAPON_PICK) {
        MenuGeo g2 = GetMenuGeo();
        for (int i = 0; i < g2.count; i++) {
            RECT r = RowRect(g2, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                if (currentState == STATE_SAFEHOUSE) shopRow = i;
                else weaponPickSel = i;
                ActivateMenuItem();
                return;
            }
        }
        return;
    }

if (currentState == STATE_HUB) {
        {
            int id = SkillNodeHitTest(mx, my);
            if (id >= 0) { hubSelection = id; ActivateHubRow(); }
            return;
        }
        MenuGeo hg = GetMenuGeo();
        for (int i = 0; i < hg.count; i++) {
            RECT r = RowRect(hg, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                hubSelection = i;
                ActivateHubRow();
                return;
            }
        }
        return;
    }

    if (currentState == STATE_SETTINGS) {
        if (settingsPopupOpen) {
            MenuGeo p = GetPopupGeo();
            const SettingItem* it = &settings[popupItem];

            if (it->kind == SET_SLIDER) {
                RECT t = SliderTrackRect(p);
                if (my >= t.top - UIS(16) && my <= t.bottom + UIS(16)) {
                    *it->fvalue = SliderXToValue(p, mx, it->fmin, it->fmax, it->fstep);
                    sliderDragging = 1;
                    SetCapture(g_hwnd);          /* 拖出窗口也能继续拖 */
                    uiDirty = 1;
                    return;
                }
                if (mx < p.panel.left || mx > p.panel.right || my < p.panel.top || my > p.panel.bottom) {
                    settingsPopupOpen = 0;
                    sliderDragging = 0;
                    uiDirty = 1;
                }
                return;
            }

            for (int i = p.scrollOffset; i < p.count && i < p.scrollOffset + p.maxVisibleRows; i++) {
                RECT r = RowRect(p, i);
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    ApplySetting(popupItem, i);
                    settingsPopupOpen = 0;
                    uiDirty = 1;
                    return;
                }
            }
            /* 点击面板外关闭 */
            if (mx < p.panel.left || mx > p.panel.right || my < p.panel.top || my > p.panel.bottom) {
                settingsPopupOpen = 0;
                uiDirty = 1;
            }
            return;
        }
        MenuGeo g = GetMenuGeo();
        for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                settingsSelection = i;
                ActivateSettingRow();
                return;
            }
        }
        return;
    }

    if (currentState == STATE_ACHIEVEMENTS) {
        MenuGeo g = GetMenuGeo();
        for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                achSelection = i;
                if (i >= ACH_COUNT) {
                    /* 点 Back 直接退出，不是只把光标移过去 */
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    uiDirty = 1;
                }
                return;
            }
        }
        return;
    }

    if (currentState == STATE_BESTIARY) {
        MenuGeo g = GetMenuGeo();
        for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                bestiarySel = i;
                if (i >= BESTIARY_COUNT) {
                    /* 点 Back 直接退出 */
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                }
                uiDirty = 1;
                return;
            }
        }
        return;
    }

    if (currentState == STATE_GAMEOVER) { ActivateMenuItem(); return; }

    MenuGeo g = GetMenuGeo();
    for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
        RECT r = RowRect(g, i);
        if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
            menuSelection = i;
            ActivateMenuItem();
            return;
        }
    }
}
/* 地图形态变了之后刷新鼠标状态：**只有全屏图**才释放光标。
   小地图是角落叠层，鼠标继续锁在中心，边走边看。 */
static void MapCursorRefresh(void) {
    int wantCursor = (isMapOpen && mapFullScreen) ? 1 : 0;
    ForceCursor(wantCursor);
    if (wantCursor) SetCursor(LoadCursor(NULL, IDC_ARROW));
    firstMouseMove = 1;
    uiDirty = 1;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            SaveGame();
            PostQuitMessage(0);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            uiDirty = 1;
            return 0;
        }

        case WM_SIZE: {
            /* v2.2 重大修复（用户实测：窗口一大，UI 全是毛病）：
             *
             * 原来这里**只重建渲染缓冲，从不更新 winW/winH**。
             * 而 winW/winH 是"窗口客户区尺寸"，被用在四处关键地方：
             *   ① UIS() / TextHeightWin() / TextScale() —— 整个 UI 的布局与字号
             *   ② MouseToLogical()                        —— 鼠标坐标换算
             *   ③ GetMenuGeo() 的面板位置
             *   ④ 呈现路径的拉伸目标
             * 于是窗口拉到 1644x997 后，UI 仍按 800x600 排版：面板只占左半边，
             * 鼠标点击按错的坐标命中。用户看到的"一大就全是 bug"就是这个。
             *
             * 现在：客户区一变就同步 winW/winH，再重建缓冲、重算投影、重排 UI。 */
            RECT cr;
            if (GetClientRect(g_hwnd, &cr) && cr.right > 0 && cr.bottom > 0) {
                int nw = (int)cr.right, nh = (int)cr.bottom;
                if (nw != clientW || nh != clientH) {
                    clientW = nw;
                    clientH = nh;
                    InitPixels();
                    ApplyFov();
                    uiDirty = 1;
                }
            }
            return 0;
        }

        case WM_ACTIVATE:
            uiDirty = 1;
            if (LOWORD(wParam) == WA_INACTIVE) {
                memset(keys, 0, sizeof(keys));
                if (currentState == STATE_PLAYING) { autoPaused = 1; PauseGame(); }
            } else {
                /* 窗口重新获得焦点：若是因失焦自动暂停，则自动恢复 */
                if (currentState == STATE_PAUSED && autoPaused && prevState == STATE_PLAYING) {
                    currentState = STATE_PLAYING;
                    resetInputState = 1;   /* 鼠标在失焦期间的按下对游戏不可见，需重同步边沿 */
                    ForceCursor(0);
                    firstMouseMove = 1;
                    uiDirty = 1;
                }
            }
            return 0;

        case WM_KILLFOCUS:
            /* 按住 TAB 看图时切走窗口会收不到 KEYUP，这里兜底关掉 */
            if (mapHeld) { mapHeld = 0; isMapOpen = 0; mapFullScreen = 0; }
            memset(keys, 0, sizeof(keys));
            return 0;

        case WM_SETFOCUS:
            firstMouseMove = 1;
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                /* 游戏中（含小窗地图）隐藏光标 —— 鼠标是锁定的。
                   只有全屏地图才显示箭头光标。 */
                int mouseFree = (currentState != STATE_PLAYING) ||
                                (isMapOpen && mapFullScreen) || lockActive;
                if (!mouseFree) SetCursor(NULL);
                else SetCursor(LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_MOUSEMOVE: {
            /* v6：技能树拖拽中 */
            if (currentState == STATE_HUB && SkillIsDragging()) {
                int wx = (short)LOWORD(lParam), wy = (short)HIWORD(lParam);
                POINT pt = { wx, wy };
                ScreenToClient(g_hwnd, &pt);
                int mx2 = pt.x, my2 = pt.y;
                MouseToLogical(&mx2, &my2);
                SkillDragMove(mx2, my2);
                return 0;
            }
            /* v3.0：**锁盘打开时也必须更新鼠标位置**。
               原来 g_mousePos 只在 currentState != STATE_PLAYING 时才写，
               而锁盘是游戏中打开的 → 坐标永远是 {0,0}，
               LockHitTest 全部落空，玩家点哪里都没反应。
               这是「锁盘每一个效果都不行」的根因。 */
            if (lockActive) {
                int lmx = (short)LOWORD(lParam), lmy = (short)HIWORD(lParam);
                MouseToLogical(&lmx, &lmy);
                g_mousePos.x = lmx;
                g_mousePos.y = lmy;
                /* 锁盘打开时鼠标是 UI 指针，不能再回中/转视角 */
                uiDirty = 1;
                return 0;
            }

            /* v3.0：安全屋行悬停 —— 记录鼠标落在第几行，供绘制高亮 */
            if (currentState == STATE_SAFEHOUSE) {
                int hx = (short)LOWORD(lParam), hy = (short)HIWORD(lParam);
                MouseToLogical(&hx, &hy);
                MenuGeo hg = GetMenuGeo();
                int f = -1;
                for (int i = 0; i < hg.count; i++) {
                    RECT r = RowRect(hg, i);
                    if (hx >= r.left && hx <= r.right && hy >= r.top && hy <= r.bottom) { f = i; break; }
                }
                if (f != safehouseHoverRow) { safehouseHoverRow = f; uiDirty = 1; }
                /* v3.0：页签悬停也一起算（原来这个变量全工程无人赋值） */
                {
                    int ht = -1;
                    /* v6：**TREE(2) / UPGRADE(4) 两页已退役** ——
                       Tab 会跳过它们，鼠标也必须跳过，否则键盘进不去、
                       鼠标却点得进，行为不一致。 */
                    for (int t = 0; t < SAFEHOUSE_PAGES; t++) {
                        /* v6：**不再跳过任何页** —— 5 个页签全可用 */
                        int tx0, ty0, tx1, ty1;
                        PageTabRect(t, &tx0, &ty0, &tx1, &ty1);
                        if (hx >= tx0 && hx <= tx1 && hy >= ty0 && hy <= ty1) { ht = t; break; }
                    }
                    if (ht != safehouseHoverTab) { safehouseHoverTab = ht; uiDirty = 1; }
                }
            }

            /* v2.9：改造树画布拖拽（自绘光标已删除，这里只服务拖拽） */
            if (currentState == STATE_SAFEHOUSE && shopPage == SHP_TREE) {
                int rx = (short)LOWORD(lParam), ry = (short)HIWORD(lParam);
                MouseToLogical(&rx, &ry);
                BenchDragMove(rx, ry);
            }
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            /* v1.8：三选一面板支持鼠标悬停选中 */
            if (currentState == STATE_SKILL_PICK) {
                MenuGeo sg = GetMenuGeo();
                for (int i = 0; i < INRUN_CHOICES; i++) {
                    RECT rr = RowRect(sg, i);
                    if (my >= rr.top && my < rr.bottom) {
                        if (skillPickSel != i) { skillPickSel = i; uiDirty = 1; }
                        break;
                    }
                }
                return 0;
            }

            if (currentState != STATE_PLAYING) {
                if (currentState == STATE_SETTINGS && rebindingAct >= 0) return 0;
                if (settingsPopupOpen && sliderDragging && popupItem >= 0 &&
                    settings[popupItem].kind == SET_SLIDER) {
                    const SettingItem* it = &settings[popupItem];
                    MenuGeo p = GetPopupGeo();
                    int lmx = mx, lmy = my;
                    MouseToLogical(&lmx, &lmy);   /* 拖拽坐标也必须换到 UI 逻辑空间 */
                    float v = SliderXToValue(p, lmx, it->fmin, it->fmax, it->fstep);
                    if (v != *it->fvalue) { *it->fvalue = v; uiDirty = 1; }
                    return 0;
                }
                g_mousePos.x = mx;
                g_mousePos.y = my;
                HitTestMenu(mx, my);
                return 0;
            }
            /* 全屏地图：鼠标交给菜单（此时鼠标是释放的，可以点东西）。
               小窗地图：**不拦截** —— 鼠标仍然锁定在中心，
               玩家可以边看小地图边转视角、继续玩。 */
            if (isMapOpen && mapFullScreen) return 0;

            /* v1.8：中心点必须用**实时客户区**算。
               原来用 winW/winH（固定 800x600），最大化后真窗口是 1711x964，
               光标会被 SetCursorPos 拉到左上角，表现为"最大化了鼠标就用不了"。 */
            RECT cr;
            GetClientRect(hwnd, &cr);
            int cw2 = cr.right - cr.left, ch2 = cr.bottom - cr.top;
            /* 画面现在铺满整个客户区，光标就回到客户区中心 */
            POINT center = { cw2 / 2, ch2 / 2 };
            ClientToScreen(hwnd, &center);

            POINT currentPos;
            GetCursorPos(&currentPos);
            if (firstMouseMove) {
                firstMouseMove = 0;
                SetCursorPos(center.x, center.y);
                lastMousePos = center;
                return 0;
            }
            /* SetCursorPos 回到中心本身又会触发一次 WM_MOUSEMOVE，
               这种“回声”必须丢弃，否则消息队列被自己灌满，帧率会塌 */
            if (currentPos.x == center.x && currentPos.y == center.y) return 0;

            int dx = currentPos.x - lastMousePos.x;
            int dyv = currentPos.y - lastMousePos.y;
            if (dyv != 0) {
                /* v1.12：鼠标上下 = 抬头/低头（地平线偏移） */
                extern float pitchRatio;
                /* v1.12：y-shearing 与这套墙渲染冲突（远距离黑边、缝隙、光球遮挡错乱），
                   先关掉，等换成"真俯仰"（每列按 pitch 重新投影）再开。
                   要重新启用：把下面这行改成 1。 */
                #define PITCH_ENABLED 0   /* 真俯仰待重做：垂直/水平焦距需统一 */
                #if PITCH_ENABLED
                pitchRatio -= (float)dyv * 0.0030f * mouseSens;
                if (pitchRatio > 1.0f) pitchRatio = 1.0f;
                if (pitchRatio < -1.0f) pitchRatio = -1.0f;
                uiDirty = 1;
                #else
                pitchRatio = 0.0f;
                horizon = 0;
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
            SetCursorPos(center.x, center.y);
            lastMousePos = center;
            return 0;
        }

        case WM_MOUSEWHEEL: {
            /* v6：**技能树页用滚轮缩放**（和改造树同一套锚点逻辑） */
            if (currentState == STATE_HUB) {
                int wx = (short)LOWORD(lParam), wy = (short)HIWORD(lParam);
                POINT pt = { wx, wy };
                ScreenToClient(g_hwnd, &pt);
                int hx = pt.x, hy = pt.y;
                MouseToLogical(&hx, &hy);
                float hf = (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 1.12f : (1.0f / 1.12f);
                SkillZoomAt(hf, hx, hy);
                return 0;
            }
            /* v2.8：改造树页用滚轮缩放（以鼠标为锚点） */
            if (currentState == STATE_SAFEHOUSE && shopPage == SHP_TREE) {
                int wx = (short)LOWORD(lParam), wy = (short)HIWORD(lParam);
                POINT pt = { wx, wy };
                ScreenToClient(g_hwnd, &pt);
                int lx2 = pt.x, ly2 = pt.y;
                MouseToLogical(&lx2, &ly2);
                float f = (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 1.12f : (1.0f / 1.12f);
                BenchZoomAt(f, lx2, ly2);
                return 0;
            }
            /* v1.8：游玩中用滚轮切换投掷物（1/2/3 仍然可用） */
            if (currentState == STATE_PLAYING) {
                /* v1.14：滚轮阻尼。
                   ① 累加 delta，够 60 才切一格（原来一格 = 一次滚，太敏感）
                   ② 切换后 90ms 内不再响应（防止高精度滚轮连发）
                   ③ 顺手修 bug：原来模 3，但只有 ITEM_SLOT_COUNT 格 →
                      会选到不存在的槽位，HUD 数组越界。 */
                static int wheelAcc = 0;
                static double wheelCd = 0.0;
                int dz = GET_WHEEL_DELTA_WPARAM(wParam);
                wheelAcc += dz;
                if (wheelAcc >= 60 || wheelAcc <= -60) {
                    if (g_timeSec - wheelCd > 0.09) {
                        int dir = (wheelAcc > 0) ? +1 : -1;
                        /* ============================================================
                         * 2026-11-05 修 bug：**滚轮永久卡死**
                         * ------------------------------------------------------------
                         * 旧写法是 (sel + dir + 6) % 6 —— 会落在**空槽**上。
                         * 而 Update() 每帧都调 EnsureThrowSelectedValid()，
                         * 它发现当前槽空着，就把选中拽回"第一个有货的槽"。
                         *
                         * 于是：槽0 有货、槽1 空 → 滚轮 +1 到槽1 → 下一帧拽回槽0
                         *       → 再滚 +1 又到槽1 又拽回槽0 …… **永远出不去**。
                         * 玩家看到的就是"滚轮切换不出来"。
                         *
                         * 现在直接跳到**下一个有货的槽**，空槽整个跳过。
                         * ============================================================ */
                        int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs,
                                                        gluePots, echoBolts, decoys };
                        int moved = 0;
                        for (int step = 0; step < ITEM_SLOT_COUNT; step++) {
                            throwSelected = (throwSelected + dir + ITEM_SLOT_COUNT) % ITEM_SLOT_COUNT;
                            if (counts[throwSelected] > 0) { moved = 1; break; }
                        }
                        if (!moved) throwSelected = 0;   /* 一件都没有：回 0 号槽 */
                        wheelCd = g_timeSec;
                        uiDirty = 1;
                    }
                    wheelAcc = 0;
                }
                return 0;
            }

            /* 鼠标滚轮滚动菜单 */
            if (currentState != STATE_PLAYING && !settingsPopupOpen) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                MenuGeo g = GetMenuGeo();
                if (g.count > g.maxVisibleRows) {
                    int maxOffset = g.count - g.maxVisibleRows;
                    if (maxOffset < 0) maxOffset = 0;
                    if (delta > 0) {
                        /* 向上滚动 */
                        menuScrollOffsets[currentState] = (menuScrollOffsets[currentState] > 0) ? menuScrollOffsets[currentState] - 1 : 0;
                    } else if (delta < 0) {
                        /* 向下滚动 */
                        menuScrollOffsets[currentState] = (menuScrollOffsets[currentState] < maxOffset) ? menuScrollOffsets[currentState] + 1 : maxOffset;
                    }
                    uiDirty = 1;
                    InvalidateRect(g_hwnd, NULL, FALSE);  /* 强制重绘 */
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            /* v6：**技能树拖拽平移** —— 按下即开始拖，和改造树一致 */
            if (currentState == STATE_HUB) {
                int wx = (short)LOWORD(lParam), wy = (short)HIWORD(lParam);
                POINT pt = { wx, wy };
                ScreenToClient(g_hwnd, &pt);
                int dx = pt.x, dy = pt.y;
                MouseToLogical(&dx, &dy);
                SkillDragStart(dx, dy);
                return 0;
            }
            /* v3.0：锁盘打开时，鼠标全部归锁盘 */
            if (lockActive) {
                int lx = (short)LOWORD(lParam), ly = (short)HIWORD(lParam);
                MouseToLogical(&lx, &ly);
                LockClick(lx, ly);
                uiDirty = 1;
                return 0;
            }
            /* v2.8：安全屋页签 + 改造树节点点击 */
            if (currentState == STATE_SAFEHOUSE) {
                int mx0 = (short)LOWORD(lParam), my0 = (short)HIWORD(lParam);
                MouseToLogical(&mx0, &my0);
                /* 页签 */
                for (int t = 0; t < SAFEHOUSE_PAGES; t++) {
                        /* v6：**退役页（TREE/UPGRADE）鼠标也要跳过** ——
                           hover 循环跳过了、click 循环没跳，
                           结果键盘进不去、鼠标却点得进，行为自相矛盾。 */
                        /* v6：**不再跳过任何页** —— 5 个页签全可用 */
                    int x0, y0, x1, y1;
                    PageTabRect(t, &x0, &y0, &x1, &y1);
                    if (mx0 >= x0 && mx0 <= x1 && my0 >= y0 && my0 <= y1) {
                        shopPage = t;
                        shopRow = 0;
                        uiDirty = 1;
                        return 0;
                    }
                }
                /* 空白处按住 → 开始拖拽画布（v2.9，和思维导图一致） */
                if (shopPage == SHP_TREE) {
                    BenchDragStart(mx0, my0);
                }
                /* 树节点 */
                if (shopPage == SHP_TREE) {
                    for (int id = 0; id < BENCH_COUNT; id++) {
                        int cx = (int)(BenchNodeWorldX(id) * treeVK + treeVX);
                        int cy = (int)(BenchNodeWorldY(id) * treeVK + treeVY);
                        int hw = (int)(75.0f * treeVK), hh = (int)(27.0f * treeVK);
                        if (mx0 >= cx - hw && mx0 <= cx + hw && my0 >= cy - hh && my0 <= cy + hh) {
                            shopNodeSel = id;
                            uiDirty = 1;
                            return 0;
                        }
                    }
                }
            }
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);

            /* v2.2 修复：原来这里无条件 CloseSkillPick() ——
               点面板外、点边框、误点一下都算选定（一局 24 层要弹 23 次）。
               现在必须真的点在某张卡上。 */
            if (currentState == STATE_SKILL_PICK) {
                ClickMenu(mx, my);
                return 0;
            }
            if (currentState != STATE_PLAYING) {
                /* 改绑模式：点击也可以绑定为「鼠标左键」 */
                if (currentState == STATE_SETTINGS && rebindingAct >= 0) {
                    RebindConsumeKey(VK_LBUTTON);
                    return 0;
                }
                ClickMenu(mx, my);
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
            if (currentState == STATE_SETTINGS && rebindingAct >= 0) {
                RebindConsumeKey(VK_RBUTTON);
            }
            return 0;

        case WM_MBUTTONDOWN:
            if (currentState == STATE_SETTINGS && rebindingAct >= 0) {
                RebindConsumeKey(VK_MBUTTON);
            }
            return 0;

        case WM_LBUTTONUP:
            /* 2026-11-05：松开要通知锁盘 —— 过载的判定就发生在这一刻 */
            if (lockActive) {
                extern void LockHoldEnd(void);
                LockHoldEnd();
                uiDirty = 1;
                return 0;
            }
            if (currentState == STATE_HUB) {
                /* 修：没拖动就当成单击 —— 选中鼠标下的节点（见 ui.c 的注释） */
                extern int SkillDragWasClick(void);
                extern int SkillNodeHitTest(int, int);
                extern int hubSelection;
                int wasClick = SkillDragWasClick();
                SkillDragEnd();
                if (wasClick) {
                    int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
                    POINT pt = { mx, my };
                    ScreenToClient(g_hwnd, &pt);
                    int dx = pt.x, dy = pt.y;
                    MouseToLogical(&dx, &dy);
                    int id = SkillNodeHitTest(dx, dy);
                    if (id >= 0 && hubSelection != id) { hubSelection = id; uiDirty = 1; }
                }
                return 0;
            }
            BenchDragEnd();   /* v2.9：结束画布拖拽 */
            if (sliderDragging) {
                ReleaseCapture();
                SaveGame();   /* 拖动滑块松手就落盘 */
            }
            sliderDragging = 0;
            return 0;

        case WM_CAPTURECHANGED:
            sliderDragging = 0;
            return 0;

        case WM_KEYDOWN:
            /* v1.8：F11 或 Alt+Enter 切换最大化 / 还原（菜单里也能用） */
            /* v1.9：F9 导出当前帧的 UI 布局，给 tools/ui_layout_check.py 做重叠检查 */
            if (wParam == VK_F9) {
                DumpUILayout("ui_layout.txt");
                return 0;
            }
            /* ==================================================================
             * v7 第 63 轮：**技能树重置入口**
             * ==================================================================
             * SkillResetAll() 之前连实现都没有（skills.h 有原型、
             * skills.c 里被 git 冲掉了），META RESET 成就因此永远拿不到。
             *
             * 入口：Hub 里按 R，**两秒内再按一次**才真的重置。
             * 破坏性操作要二次确认，但不用弹窗（弹窗会打断键盘流）。
             * ================================================================ */
            if(currentState==STATE_SAFEHOUSE && shopPage==SHP_TREE && wParam=='R') {
                static DWORD lastReset=0; DWORD now=GetTickCount();
                if(lParam&(1L<<30)) return 0;
                if(lastReset && now-lastReset<2000) { BenchRefundRun();lastReset=0;pickupMsg=Tr("MODS RESET - GOLD REFUNDED","改造已重置，金币已退还"); }
                else {lastReset=now;pickupMsg=Tr("PRESS R AGAIN TO RESET MODS","再按 R 重置本局改造");}
                pickupMsgTimer=2.0f;uiDirty=1;return 0;
            }
            if(currentState==STATE_SAFEHOUSE && shopPage==SHP_SHOP && wParam=='R') { if(!(lParam&(1L<<30))) ShopRefresh(); return 0; }
            if (currentState == STATE_HUB && wParam == 'R') {
                if (lParam & (1L << 30)) return 0;
                static DWORD lastR = 0;
                DWORD now = GetTickCount();
                if (lastR != 0 && (now - lastR) < 2000) {
                    extern void SkillResetAll(void);
                    extern const char* pickupMsg;
                    extern float pickupMsgTimer;
                    SkillResetAll();
                    pickupMsg = Tr("SKILLS RESET - GOLD REFUNDED", "技能已重置，金币已退还");
                    pickupMsgTimer = 2.5f;
                    lastR = 0;
                    uiDirty = 1;
                } else {
                    extern const char* pickupMsg;
                    extern float pickupMsgTimer;
                    pickupMsg = Tr("PRESS R AGAIN TO RESET ALL SKILLS",
                                   "再按一次 R 重置全部技能");
                    pickupMsgTimer = 2.0f;
                    lastR = now;
                    uiDirty = 1;
                }
                return 0;
            }

            if (wParam == VK_F11 ||
                (wParam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))) {
                /* v1.24：F11 改成**真全屏切换**（无边框铺满整块屏），
                   原来只是最大化窗口 —— 还留着标题栏和边框，看起来像窗口化。 */
                if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
                isFullscreen = isFullscreen ? 0 : 1;
                ApplyDisplayMode();
                uiDirty = 1;
                return 0;
            }
            /* v3.0：**锁盘只认 Esc / M 关闭** ——
               原来这里是「任意键退出」，解题时误按一个键就把面板关掉，
               玩家得重新按 E 再从头对一遍（用户反馈「解密码的时候按键会出 bug」）。
               上游原本就是 M 关闭（原版截图底栏写的是 M: CLOSE）。 */
            if (lockActive) {
                /* v3.3 **真的只认 Esc / M** ——
                   注释一直这么写，代码却是「除打开键外任何键都关」，两回事。
                   试玩实测：「解开锁盘时按一次 W，面板立刻消失，
                   拼到一半的进度被丢掉」。玩家解谜时习惯性按 WASD，
                   这个键位必然误触。
                   现在白名单关闭：Esc / M / 打开锁盘的那个键（防止自动重复）。 */
                if ((int)wParam == lockIgnoreVK) return 0;      /* 打开它的那一下 */
                if ((int)wParam != VK_ESCAPE && (int)wParam != 'M') {
                    /* 其它键全部吞掉，不关面板、也不透传到游戏里 */
                    return 0;
                }
                CloseLockOverlay();
                return 0;
            }
            /* 小键盘数字（NumLock 开启时是 VK_NUMPAD0..9）统一折算成主键盘数字，
               这样 1/2/3 选投掷槽位、以及改绑数字键都不会因为按小键盘而失效。 */
            if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
                wParam = (WPARAM)('0' + (wParam - VK_NUMPAD0));
            /* 改绑模式：吞掉所有按键（Esc 取消），不进菜单导航 */
            if (currentState == STATE_SETTINGS && rebindingAct >= 0) {
                RebindConsumeKey((int)wParam);
                return 0;
            }
            /* v2.9：**只在游戏中才写 keys[]**。
               这是「面板里按 A/D 会漂移」的真正根因：
               按住 A/D 时 Windows 会以每秒几十次重发 WM_KEYDOWN（键盘自动重复），
               这里每次都把 keys['A'] 设回 1，而 PollInput 每帧清零一次 ——
               两条路径打架，结果就是玩家被推着走。
               菜单导航靠的是下面那些 wParam 比较，不需要 keys[]。 */
            if (wParam < 256) {
                if (currentState == STATE_PLAYING) {
                    keys[wParam] = 1;
                    keyPressed[wParam] = 1;   /* 边沿，供 QTE 消费 */
                }
            }

            /* ================================================================
             * v7 第 ㊿ 轮：**T 键在这里直接处理**
             * ================================================================
             * 原来 T 是在 Update 里读 keyPressed['T'] 的 —— 但
             * Update 里前面有若干 return / 提前分支，键很容易被跳过，
             * 而且玩家看到的症状就是「按 T 完全没反应」。
             *
             * 这里和「切投掷槽位」一样，在消息里立即处理，
             * 不依赖 Update 走到哪一行。
             * ================================================================ */
            if (currentState == STATE_PLAYING && wParam == 'T') {
                /* v7 第 ㊿ 轮：**改回切状态**
                   崩溃的真正原因是 ShopPriceAt() 无限递归（已修），
                   不是这条状态路径。而这条路径是进改造树 / 局内强化 /
                   休息 / 校准小游戏的**唯一入口**，不能没有。 */
                extern void ShopOpen(int);
                extern int currentLevel;
                ShopOpen(currentLevel);
                currentState = STATE_SAFEHOUSE;
                shopPage = 0;
                shopRow = 0;
                keyPressed['T'] = 0;   /* 否则 Update 里那条还会再开一次 */
                ForceCursor(1);
                uiDirty = 1;
                return 0;
            }

            /* 切投掷槽位：在消息里即时处理，后按的键立即生效。
               bit30 = 系统自动重复；按住不放时不重复覆盖，否则会压住另一个键。
               （绑到鼠标键的情况在 Update 里轮询兜底。） */
            if (currentState == STATE_PLAYING && !(lParam & 0x40000000)) {
                if (!IsMouseButton(g_bindings[ACT_SELECT_1]) &&
                    wParam == (WPARAM)g_bindings[ACT_SELECT_1]) SelectVisibleSlot(0);
                else if (!IsMouseButton(g_bindings[ACT_SELECT_2]) &&
                    wParam == (WPARAM)g_bindings[ACT_SELECT_2]) SelectVisibleSlot(1);
                else if (!IsMouseButton(g_bindings[ACT_SELECT_3]) &&
                    wParam == (WPARAM)g_bindings[ACT_SELECT_3]) SelectVisibleSlot(2);
            }

            /* 统一菜单导航按键判断 */
            /* v1.8：W/S 也能上下选菜单（很多玩家习惯 WASD，不想切手去按方向键） */
            /* v2.2：删掉 C 键开战役 —— 它会 DeleteSave()，按错就删档。
       战役/每日已进主菜单（第 7/8 行），那里先 SaveGame。 */
    /* v2.2：同上，删掉 D 键开每日。 */
    /* v2.0：安全屋是模态的，原来只能按 ESC / 选 LEAVE 出去，
       玩家一进去就像卡住。现在按任意移动键直接关门。 */
    /* v2.2 修复：上一版加了「W/A/S/D 直接关门」，但它放在菜单导航**之前**，
       把后面本该消费 W/S 的代码整段变成死代码 —— 想用 S 往下选，面板直接关，
       再按 E 进来光标又回到第 1 行，永远够不到第 2/3 行。
       现在只在玩家**同时按住方向无关的移动键组合**（A/D 横移）时才关门，
       W/S 留给导航；想退出请按 ESC 或选 LEAVE。 */
    /* v2.9：**真的删掉了**「A/D 关门」。
       上一版我以为删了，其实补丁打到了同文案的另一处，块还在 4345 行 ——
       结果按住 A/D 面板直接被关掉，state 从 11 跳回 1，玩家立刻被推着走。
       这就是「A/D 漂移」纠缠了这么多轮的真相。 */
    int menuUp    = (wParam == (WPARAM)g_bindings[ACT_MENU_UP])   || wParam == 'W';
            int menuDown  = (wParam == (WPARAM)g_bindings[ACT_MENU_DOWN]) || wParam == 'S';
            int menuLeft  = (wParam == (WPARAM)g_bindings[ACT_MENU_LEFT])  || wParam == 'A';   /* v2.8 */
            int menuRight = (wParam == (WPARAM)g_bindings[ACT_MENU_RIGHT]) || wParam == 'D';   /* v2.8 */
            /* v1.8：空格也能确认（很多人不习惯按回车）。
               游戏里空格仍是跳脸虫的躲避键，两者不冲突。 */
            /* ==================================================================
             * 修：**确认键没有挡「键盘自动重复」**
             * ==================================================================
             * 按住 Space / Enter 时 Windows 每秒重发几十次 WM_KEYDOWN。
             * menuUp/menuDown 需要重复（连按可以滚列表），
             * 但 menuConfirm 一旦重复就是灾难：
             *
             *   设置页按 Space → 第 1 次打开弹窗
             *                  → 第 2 次立刻确认并关掉弹窗
             *   玩家看到的就是「空格没反应」。
             *
             * 同文件里 R 键（6282）和投掷槽键（6389）都判断了 bit30，
             * 只有这个聚合量漏了。给 confirm / cancel 加上。
             * ================================================================ */
            int keyRepeat   = (lParam & (1L << 30)) ? 1 : 0;
            int menuConfirm = !keyRepeat &&
                              ((wParam == (WPARAM)g_bindings[ACT_MENU_CONFIRM]) || wParam == VK_SPACE);
            int menuCancel  = !keyRepeat && (wParam == (WPARAM)g_bindings[ACT_MENU_CANCEL]);

            if (currentState == STATE_MAIN_MENU) {
                /* v3.3 **数字键直接选中并确认** ——
                   左边那列 1 2 3 … 本来就是快捷键暗示，但从来没接。
                   试玩实测：「按 1 想选 New Run，完全没反应，
                   必须方向键+回车」。现在按数字 = 选中该行 + 立刻执行。 */
                {
                    int nk = -1;
                    if (wParam >= '1' && wParam <= '9')      nk = (int)wParam - '1';
                    else if (wParam == '0')                  nk = 9;
                    if (nk >= 0 && nk < MainMenuCount()) {
                        menuSelection = nk;
                        ActivateMenuItem();
                        return 0;
                    }
                }
                if (menuUp) { menuSelection = (menuSelection + MainMenuCount() - 1) % MainMenuCount(); uiDirty = 1; }
                if (menuDown) { menuSelection = (menuSelection + 1) % MainMenuCount(); uiDirty = 1; }
                MenuGeo g = GetMenuGeo();
                if (menuSelection < g.scrollOffset) menuScrollOffsets[STATE_MAIN_MENU] = menuSelection;
                else if (menuSelection >= g.scrollOffset + g.maxVisibleRows) menuScrollOffsets[STATE_MAIN_MENU] = menuSelection - g.maxVisibleRows + 1;
                if (menuConfirm) ActivateMenuItem();
            } else if (currentState == STATE_PAUSED) {
                if (menuUp) { menuSelection = (menuSelection + 2) % 3; uiDirty = 1; }
                if (menuDown) { menuSelection = (menuSelection + 1) % 3; uiDirty = 1; }
                if (menuConfirm) ActivateMenuItem();
                if (menuCancel) {
                    currentState = STATE_PLAYING;
                    resetInputState = 1;   /* Esc 回游戏：吞掉回焦路上可能的残留按下 */
                    ForceCursor(0);
                    firstMouseMove = 1;
                    uiDirty = 1;
                }
            } else if (currentState == STATE_SETTINGS) {
                if (settingsPopupOpen) {
                    const SettingItem* it = &settings[popupItem];
                    if (it->kind == SET_SLIDER) {
                        float v = *it->fvalue;
                        if (menuLeft) v -= it->fstep;
                        if (menuRight) v += it->fstep;
                        if (v < it->fmin) v = it->fmin;
                        if (v > it->fmax) v = it->fmax;
                        if (v != *it->fvalue) { *it->fvalue = v; uiDirty = 1; }
                        if (menuConfirm || menuCancel) {
                            settingsPopupOpen = 0;
                            sliderDragging = 0;
                            SaveGame();   /* 滑块值确认后立刻落盘 */
                            uiDirty = 1;
                        }
                    } else {
                        int n = it->count;
                        if (menuUp) { popupSelection = (popupSelection + n - 1) % n; uiDirty = 1; }
                        if (menuDown) { popupSelection = (popupSelection + 1) % n; uiDirty = 1; }
                        if (menuConfirm) { ApplySetting(popupItem, popupSelection); settingsPopupOpen = 0; }
                        if (menuCancel) { settingsPopupOpen = 0; uiDirty = 1; }
                    }
                } else {
                    int n = SettingsRowCount();
                    MenuGeo g = GetMenuGeo();
                    if (menuUp) {
                        settingsSelection = (settingsSelection + n - 1) % n;
                        if (settingsSelection < g.scrollOffset) menuScrollOffsets[STATE_SETTINGS] = settingsSelection;
                        uiDirty = 1;
                    }
                    if (menuDown) {
                        settingsSelection = (settingsSelection + 1) % n;
                        if (settingsSelection >= g.scrollOffset + g.maxVisibleRows) menuScrollOffsets[STATE_SETTINGS] = settingsSelection - g.maxVisibleRows + 1;
                        uiDirty = 1;
                    }
                    if (menuConfirm) ActivateSettingRow();
                    if (menuCancel) {
                        /* Esc 逐级上退：键位页 -> 子目录 -> 根目录 -> 退出 */
                        if (settingsPage == SETPAGE_MOVE || settingsPage == SETPAGE_ACT || settingsPage == SETPAGE_MENUKEYS) {
                            settingsPage = SETPAGE_BINDDIR;
                        } else if (settingsPage == SETPAGE_BINDDIR) {
                            settingsPage = SETPAGE_ROOT;
                        } else if (settingsPage == SETPAGE_DISPLAY || settingsPage == SETPAGE_GAME) {
                            settingsPage = SETPAGE_ROOT;
                        } else {
                            currentState = prevState;
                            SaveGame();
                        }
                        settingsSelection = 0;
                        uiDirty = 1;
                    }
                }
            } else if (currentState == STATE_GAMEOVER) {
                if (menuConfirm) ActivateMenuItem();
            } else if (currentState == STATE_HELP) {
                if (wParam == VK_TAB) { helpPage = 1 - helpPage; uiDirty = 1; }
                /* v6 **光标范围要和面板一致** ——
                   原来光标在 HELP_ROWS(23) 里滚，但每页只画 8 行，
                   移到第 9 行以后屏幕上没有任何高亮（玩家以为帮助页坏了），
                   helps[6..12]（瓶子 / 跳脸虫说明）永远看不到。
                   现在光标限制在单页 8 行，翻页由 Tab / 光标跨越自动跟随。 */
                {
                    int before = menuSelection;
                    if (menuUp)   menuSelection = (menuSelection + HELP_PAGE_ROWS - 1) % HELP_PAGE_ROWS;
                    if (menuDown) menuSelection = (menuSelection + 1) % HELP_PAGE_ROWS;
                    if (menuSelection != before) uiDirty = 1;
                    /* 在单页内滚到底再按下 → 翻页 */
                    if (menuDown && menuSelection == 0 && before == HELP_PAGE_ROWS - 1)
                        helpPage = (helpPage + 1) % 2;
                    if (menuUp && before == 0)
                        helpPage = (helpPage + 1) % 2;
                }
                MenuGeo hg = GetMenuGeo();
                if (menuSelection < hg.scrollOffset) menuScrollOffsets[STATE_HELP] = menuSelection;
                else if (menuSelection >= hg.scrollOffset + hg.maxVisibleRows) menuScrollOffsets[STATE_HELP] = menuSelection - hg.maxVisibleRows + 1;
                if (menuCancel || menuConfirm) { currentState = STATE_MAIN_MENU; uiDirty = 1; }
            } else if (currentState == STATE_SKILL_PICK) {
                /* v1.8：除了方向键，也接受 W/S（很多玩家习惯 WASD） */
                int up = menuUp || wParam == 'W';
                int dn = menuDown || wParam == 'S';
                if (up) { skillPickSel = (skillPickSel + INRUN_CHOICES - 1) % INRUN_CHOICES; uiDirty = 1; }
                if (dn) { skillPickSel = (skillPickSel + 1) % INRUN_CHOICES; uiDirty = 1; }
                if (menuConfirm) CloseSkillPick();
            } else if (currentState == STATE_SAFEHOUSE) {
                /* ==================================================================
                 * 2026-11-05 修三个「局内改造树」操作 bug
                 * ------------------------------------------------------------------
                 * ① **空格买不了东西**
                 *    这段「空格 = 校准」在按键处理的最前面，而且无条件 return ——
                 *    空格本来也算 menuConfirm（下面 ShopActivate 会处理），
                 *    却被这里整个吃掉了。改造树页按空格永远没反应。
                 *    修：树页不吃空格（让给 menuConfirm）；其它页保持原行为。
                 *
                 * ② **买完东西画面位置会变 / 左右移动像没动**
                 *    原来这里有一行**无条件**的 BenchFocusNode(shopNodeSel) ——
                 *    它是「把画布硬居中到选中节点」，每次按键都跑一遍：
                 *      · 按回车买东西 → 先居中再买 → 视图跳一下
                 *      · 按左右移动     → 改选中后立刻居中 → 节点永远在屏幕正中，
                 *                        看起来"没有左右跳跃"，只有背景在滑
                 *      · 鼠标拖过画布之后按任意键 → 画布被拽回原位
                 *    修：只在**真的从别的页切到树页**时居中一次。
                 * ================================================================== */
                if (wParam == VK_SPACE && !(lParam & 0x40000000) && shopPage != SHP_TREE) {
                    if (calibActive) CalibLock();
                    else             CalibStart();
                    return 0;
                }
                if (wParam == VK_TAB) ShopNextPage();
                /* 只在「切到树页」这一下居中，之后不再干扰玩家的视角 */
                {
                    static int s_lastShopPage = -1;
                    if (shopPage != s_lastShopPage) {
                        s_lastShopPage = shopPage;
                        if (shopPage == SHP_TREE) BenchFocusNode(shopNodeSel);
                    }
                }
                if (wParam == VK_HOME && currentState == STATE_HUB) SkillResetView();   /* v6：技能树复位 */
                if (wParam == VK_HOME && shopPage == SHP_TREE) BenchResetView();   /* v2.8 */
                if (shopPage == SHP_TREE) {
                    /* 2026-11-05：**移动不再强制居中**。
                       原来每次移动都 BenchFocusNode 把画布拉回使选中节点居中，
                       结果"节点永远在屏幕正中、只有背景在滑"，玩家感觉
                       「左右跳跃没反应」。现在只改选中，画布跟着玩家的
                       拖动/滚轮走；看不见时按 HOME 复位。 */
                    if (menuUp)    ShopTreeMove(0, -1);
                    if (menuDown)  ShopTreeMove(0, 1);
                    if (menuLeft)  ShopTreeMove(-1, 0);
                    if (menuRight) ShopTreeMove(1, 0);
                } else {
                    if (menuUp)      ShopMoveSel(-1);
                    if (menuDown)    ShopMoveSel(1);
                    /* v2.8：非树页时左右键切页（和顶部按钮一致，不用记 TAB） */
                    if (menuLeft)    ShopPrevPage();
                    if (menuRight)   ShopNextPage();
                }
                if (menuConfirm) ShopActivate();
                if (menuCancel) {
                    /* 2026-11-05：**ESC 和 LEAVE 行走同一条出口**。
                       原来这里各写一遍，ShopLeave 补了 firstMouseMove
                       而这里漏了 —— 于是 ESC 退出仍会视角翻转。
                       一份实现，两处调用，才不会再有这种半修。 */
                    extern void ShopLeave(void);
                    ShopLeave();
                }
            } else if (currentState == STATE_WEAPON_PICK) {
                if (menuUp)      { weaponPickSel = (weaponPickSel + WPN_COUNT - 1) % WPN_COUNT; uiDirty = 1; }
                if (menuDown)    { weaponPickSel = (weaponPickSel + 1) % WPN_COUNT; uiDirty = 1; }
                if (menuConfirm) { curWeapon = weaponPickSel; StartGame(); }
                if (menuCancel) {
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    ForceCursor(1);
                    uiDirty = 1;
                }
            } else if (currentState == STATE_HUB) {
                /* v6 **ESC 必须在所有页都生效** ——
                   原来 ESC 处理写在分支最末尾，而银行页用了 break 提前跳出，
                   结果在银行页按 ESC 完全没反应，退不出去。
                   现在把 ESC 提到最前面，任何页都先判它。 */
                if (menuCancel) {
                    SaveGame();
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    ForceCursor(1);
                    uiDirty = 1;
                    break;
                }

                /* v6：**Tab 在 Hub 的 3 页之间循环** ——
                   升级列表(0) → 技能树(1) → 银行(2) → 0
                   原来完全没有 Tab 处理，但屏幕右上角写着 TAB PAGE x/3，
                   玩家按 Tab 毫无反应。 */
                /* v6：Hub **只有一页**，Tab 不再翻页（原来会切到空的属性页） */
                /* v6：**HOME 复位** —— 原来这句写在 SAFEHOUSE 分支里，
                   条件恒假，技能树的复位键永远不生效。 */
                if (wParam == VK_HOME) { SkillResetView(); return 0; }
                if (wParam == VK_TAB) {
                    return 0;
                }
                /* v2.2 修复：原来这里只处理 ESC —— 方向键和回车一个都不响应，
                   而屏幕底部明写 ENTER BUY / ARROWS MOVE。纯键盘玩家
                   一分金币都花不出去，整条局外成长线等于锁死。 */
                {
                    int cur = hubSelection;
                    /* v6：**按边导航** —— 辐射布局里节点不在直线上，
                       col/row 加减会跳到视觉上很远的地方。
                       现在按方向 + 距离找最近的邻居。 */
                    int nid = -1;
                    extern int SkillNavNearest(int, int);
                    if (menuUp)    nid = SkillNavNearest(cur, 0);
                    if (menuDown)  nid = SkillNavNearest(cur, 1);
                    if (menuLeft)  nid = SkillNavNearest(cur, 2);
                    if (menuRight) nid = SkillNavNearest(cur, 3);
                    if (1) {
                        /* v6：**退役节点（maxRank 0）跳过** ——
                           原来会停在退役节点上，详情显示 [RETIRED]、
                           买不了，玩家以为卡死。 */
                        if (nid >= 0 && skillDefs[nid].maxRank > 0) {
                            hubSelection = nid;
                            { extern void SkillFocusNode(int id); SkillFocusNode(nid); }
                            uiDirty = 1;
                        }
                    }
                    if (menuConfirm) ActivateHubRow();
                }
            } else if (currentState == STATE_BANK) {
                /* v6 **独立银行页的输入** —— 全部符合玩家习惯：
                     ESC                     返回主菜单
                     Enter / Space           下一个携带比例
                     S / ↓ / →               下一个比例
                     W / ↑ / ←               上一个比例
                   鼠标点击在别处处理（点哪都算回车）。 */
                if (menuCancel) {
                    SaveGame();
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    ForceCursor(1);
                    uiDirty = 1;
                    break;
                }
                {
                    extern int bankCarryPct;
                    int moved3 = 0;
                    if (menuConfirm || menuRight) { bankCarryPct = (bankCarryPct + 1) % BANK_CARRY_STEPS; moved3 = 1; }
                    else if (menuUp || menuLeft)             { bankCarryPct -= 1; moved3 = 1; }
                    if (moved3) { PlaySfx(SFX_POWER, 0.6f); uiDirty = 1; }
                }
            } else if (currentState == STATE_BESTIARY) {
                int bn = BESTIARY_COUNT + 1;
                if (menuUp) { bestiarySel = (bestiarySel + bn - 1) % bn; uiDirty = 1; }
                if (menuDown) { bestiarySel = (bestiarySel + 1) % bn; uiDirty = 1; }
                MenuGeo bg = GetMenuGeo();
                if (bestiarySel < bg.scrollOffset) menuScrollOffsets[STATE_BESTIARY] = bestiarySel;
                else if (bestiarySel >= bg.scrollOffset + bg.maxVisibleRows)
                    menuScrollOffsets[STATE_BESTIARY] = bestiarySel - bg.maxVisibleRows + 1;
                if (menuConfirm || menuCancel) {
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    uiDirty = 1;
                }
            } else if (currentState == STATE_ACHIEVEMENTS) {
                /* v6：**分类筛选** —— 32 个平铺看不完，TAB / Q / E 切分类 */
                extern void AchCycleFilter(int);
                extern int AchVisibleCount(void);
                if (wParam == VK_TAB || wParam == (WPARAM)'E') { AchCycleFilter(1); menuScrollOffsets[STATE_ACHIEVEMENTS] = 0; uiDirty = 1; return 0; }
                if (wParam == (WPARAM)'Q') { AchCycleFilter(-1); menuScrollOffsets[STATE_ACHIEVEMENTS] = 0; uiDirty = 1; return 0; }
                int n = AchVisibleCount() + 1;   /* +1 = Back 行 */
                if (menuUp) { achSelection = (achSelection + n - 1) % n; uiDirty = 1; }
                if (menuDown) { achSelection = (achSelection + 1) % n; uiDirty = 1; }
                MenuGeo ag = GetMenuGeo();
                if (achSelection < ag.scrollOffset) menuScrollOffsets[STATE_ACHIEVEMENTS] = ag.scrollOffset;
                else if (achSelection >= ag.scrollOffset + ag.maxVisibleRows) menuScrollOffsets[STATE_ACHIEVEMENTS] = achSelection - ag.maxVisibleRows + 1;
                if (menuCancel || (menuConfirm && achSelection >= AchVisibleCount())) {
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    uiDirty = 1;
                }
            } else if (currentState == STATE_PLAYING) {

            /* ================================================================
             * v7 第 51 轮：**商店浮层接管按键**
             * ================================================================ */
            {
                extern int shopOverlayOpen, shopRow;
                extern void ShopActivate(void);
                extern int ShopRowCount(void);
                if (shopOverlayOpen) {
                    int n = ShopRowCount();
                    if (n < 1) n = 1;
                    if (wParam == VK_ESCAPE) {
                        shopOverlayOpen = 0;
                        ForceCursor(0);
                        resetInputState = 1;
                        uiDirty = 1;
                        return 0;
                    } else if (wParam == VK_UP) {
                        shopRow = (shopRow + n - 1) % n;
                        uiDirty = 1;
                        return 0;
                    } else if (wParam == VK_DOWN) {
                        shopRow = (shopRow + 1) % n;
                        uiDirty = 1;
                        return 0;
                    } else if (wParam == VK_RETURN || wParam == VK_SPACE) {
                        if (shopRow >= n - 1) {
                            shopOverlayOpen = 0;
                            ForceCursor(0);
                            resetInputState = 1;
                        } else {
                            ShopActivate();
                        }
                        uiDirty = 1;
                        return 0;
                    }
                    return 0;
                }
            }

                if (wParam == (WPARAM)g_bindings[ACT_PAUSE]) { autoPaused = 0; PauseGame(); }
                if (wParam == (WPARAM)g_bindings[ACT_MAP]) {
                    /* v2.4：M = 放大地图（全屏）。再按一次 M 关掉。
                       原来是 M 三态循环（小->全->关），一个键要按三次才回到游戏，不直观。 */
                    if (isMapOpen && mapFullScreen) {
                        isMapOpen = 0;
                        mapFullScreen = 0;
                    } else {
                        isMapOpen = 1;
                        mapFullScreen = 1;
                        if (trainingMode && tutorialStep == TUT_MAP) tutorialMapOpened = 1;
                    }
                    MapCursorRefresh();
                }
                if (wParam == VK_TAB) {
                    /* v2.5：TAB 改成按住看、松开关。
                       原来按一下开再按一下关，玩家很容易一直开着跑图，
                       而地图每秒 -10 电，一趟下来电就空了。
                       按住之后，"看一眼"的成本天然就是看了多久。 */
                    isMapOpen = 1;
                    mapFullScreen = 0;
                    mapHeld = 1;
                    if (trainingMode && tutorialStep == TUT_MAP) tutorialMapOpened = 1;
                    MapCursorRefresh();
                }
                if (wParam == (WPARAM)g_bindings[ACT_FLASHLIGHT]) {
                    /* ==========================================================
                     * 2026-11-05 修：**两个规则抢手电，而且优先级判反了**
                     * ----------------------------------------------------------
                     * 旧顺序：先判战役「手电关不掉」→ 直接 return，
                     *         每日的「今天不能开手电」永远轮不到。
                     *
                     * 后果（用户报告）：「战役模式左脑打右脑又能重新开那个手电筒」
                     *   —— 战役关按 F 会无条件把手电打开，
                     *      连每日 NOLIGHT 限制和 SALVAGE keystone 都被绕过。
                     *
                     * 正确优先级：**禁止 > 强制 > 自由**
                     *   ① 禁止（每日 NOLIGHT / SALVAGE）最高 ——
                     *      它关系到每日任务的公平性，任何模式都不能覆盖它。
                     *   ② 强制（战役「关不掉」）次之。
                     *   ③ 都允许时才自由切换。
                     * ========================================================== */
                    if (!FlashlightUseAllowed()) {
                        flashlightOn = 0; uiDirty = 1;
                        /* 2026-11-05：**提示必须说清是哪一个原因**。
                           原来两种情况都显示「今天不能开手电」，
                           于是买了 SALVAGE 的玩家会以为
                           「普通关卡被每日任务篡改了标志位」——
                           其实是他自己买的天赋永久禁灯。 */
                        { extern int DailyForbidLight(void);
                          /* P1-1：SALVAGE 不再走到这里（它已经不拦开灯了），
                             所以只剩"每日限制"这一种原因。 */
                          if (DailyForbidLight())
                              pickupMsg = g_lang == LANG_ZH ? "今天不能开手电（每日限制）"
                                                            : "NO FLASHLIGHT TODAY (DAILY RULE)";
                          else
                              pickupMsg = g_lang == LANG_ZH ? "现在不能开手电" : "CANNOT USE FLASHLIGHT"; }
                        pickupMsgTimer = 1.6f;
                        return 0;
                    }
                    { extern int CampaignFlashlightLocked(void);
                      if (CampaignFlashlightLocked()) {
                          flashlightOn = 1; uiDirty = 1;
                          pickupMsg = g_lang == LANG_ZH ? "这一关手电关不掉" : "FLASHLIGHT IS LOCKED ON";
                          pickupMsgTimer = 1.2f;
                          return 0;
                      } }
                    if (battery > 0.0f) flashlightOn = !flashlightOn;
                    uiDirty = 1;
                }
            }
            return 0;

        case WM_KEYUP:
            /* 锁盘打开键松开后，才允许它重新作为「任意键退出」 */
            if ((int)wParam == lockIgnoreVK) lockIgnoreVK = 0;
            /* 与 WM_KEYDOWN 保持同一套折算，否则小键盘数字的 keys[] 会卡在按下状态 */
            if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
                wParam = (WPARAM)('0' + (wParam - VK_NUMPAD0));
            /* v2.5：松开 TAB 立刻关掉小地图（按住才看） */
            if (wParam == VK_TAB && mapHeld) {
                mapHeld = 0;
                isMapOpen = 0;
                mapFullScreen = 0;
                MapCursorRefresh();
            }

            if (wParam < 256) keys[wParam] = 0;
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

/* 编译期守卫：表项数必须与 common.h 里的常量一致。
   常见坑：往 settings[] 里加了一项却忘了改 SETTING_COUNT，
   那样跨文件的 extern 数组尺寸会对不上。 */
typedef char settingsSizeCheck[(sizeof(settings) / sizeof(settings[0]) == SETTING_COUNT) ? 1 : -1];
typedef char optKeysSizeCheck[(sizeof(optKeys) / sizeof(optKeys[0]) == OPT_KEY_COUNT) ? 1 : -1];
typedef char optKeyVKsSizeCheck[(sizeof(optKeyVKs) / sizeof(optKeyVKs[0]) == OPT_KEY_COUNT) ? 1 : -1];

/*
 * Canity — 游戏核心实现：世界 / 关卡 / 规则 / 输入 / 菜单状态机 / 窗口过程
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "game.h"
#include "weapons.h"
#include "shop.h"
#include "bench.h"
#include "lockbox.h"   /* v2.7 改造树 */
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

/* 文件内前置声明：这两个 static 辅助函数在被调用处之后定义
   （原来集中在 main.c 中部的前置声明块，拆分后只保留模块内部需要的）。 */
static void ThrowItem(int type, float charge);
static void SpawnShards(float x, float y, float vx, float vy, float charge);
void WeaponTryAttack(float charge);   /* v2.0 武器攻击 */
/* InitAchievements is public now (see game.h) */
static void UnlockAchievement(int idx);
static void AddAchievementProgress(int idx, int amount);
static void CheckAchievements(void);


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
float modShardMul = 1.0f;     /* v2.0 事件：碎片倍率 */
float modNoiseMul = 1.0f;     /* v2.0 事件：噪声倍率（死寂） */
float levelBanner = 0.0f;      /* 进关后显示词条名的秒数 */
float safeZoneMsg = 0.0f;      /* 进入安全屋后提示「SAFE ZONE」的剩余秒数 */
int shards = 0;
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
int firePots = 0, gluePots = 0, mirrors = 0; /* v2.0 三个新投掷物 */
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
static float tutorialStartX = 0.0f, tutorialStartY = 0.0f;
static float tutorialStartDirX = 1.0f, tutorialStartDirY = 0.0f;
static int   tutorialLightOff = 0;        /* 手电关过再开 */
static int   tutorialThrown = 0;
static int   tutorialKnocked = 0;
static int   tutorialMapOpened = 0;       /* 地图至少打开过一次 */
SmokeCloud smokes[MAX_SMOKE];
int throwSelected = 0;        /* 0=荧光棒, 1=玻璃瓶, 2=预留 */
Thrown thrown[MAX_THROWN];
Shot shot[MAX_SHOTS];
Shard shards_debris[MAX_SHOTS * SHARD_COUNT];
SafeRoom safeRoom;
Container containers[MAX_CONTAINERS];

/* v3.0：藏身点 */
HideSpot hideSpots[HIDESPOT_MAX];
int hideSpotCount = 0;
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

int totalShardsEarned = 0;  /* 累计获得碎片（用于成就） */
int bottlesKilled = 0;      /* 用瓶子击杀怪物数 */
int faceDodged = 0;         /* 躲开跳脸虫次数 */
int crankTimeTotal = 0;     /* 累计发电秒数 */
int fusesCollectedRun = 0;  /* 本局收集保险丝数 */
int mapsFullyExplored = 0;  /* 全探索地图数 */
int tookDamage = 0;         /* 本局是否受过伤 */
int flashlightOffLevel = 0; /* 关灯过关标记 */
int sprintedThisLevel = 0;  /* 本局是否冲刺过 */
const char* pickupMsg = NULL;    /* 拾取提示文本 */
float pickupMsgTimer = 0.0f;     /* 提示显示剩余时间 */
int bestLevel = 0;
int bestEndlessLevel = 0;   /* 无尽模式最高层数 */
int bestEndlessScore = 0;   /* 无尽模式最高分 */
int isEndlessRun = 0;       /* 当前是否为无尽模式 */
float volumeF = 0.8f;     /* 0..1，0 = 静音 */
int runInProgress = 0;    /* 当前是否有一局未结束 */

/* ---- v1.8 新状态 ---- */
int bestMainScore = 0;        /* 主线最高分（固定分，记录展示用） */
int pendingShards = 0;        /* 本层已获得、尚未上缴的碎片 */
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
float secondWindTimer = 0.0f;   /* v3.0 二次呼吸：体力归零后的宽限秒数 */
float vanishTimer = 0.0f;       /* v3.0 消失：脱离视线累计秒数 */
int   playDeadUsed = 0;         /* v3.0 假死：本局用过了吗 */
int   tutorialOff = 0;          /* v3.0 开局引导：玩家按键关闭了 */
float hideBlockTimer = 0.0f;    /* v3.0：刚解锁/刚翻完箱时短暂禁止进藏身点 */
int   ultUsed = 0;              /* v3.0：本局大招用过了吗 */
float ultTimer = 0.0f;          /* v3.0：大招持续秒数 */
int   ultKind = -1;             /* v3.0：正在生效的大招 id */
int   vanished = 0;             /* v3.0 消失：1 = 已隐身 */
float vanishHold = 0.0f;        /* v3.0 消失：本次隐身剩余秒数 */
float decoySelfTimer = 0.0f;  /* 投石后的自身噪声加成计时 */
int hubPage = 1;              /* v2.8：属性面板已并入技能树，HUB 只有一页（保留变量给旧代码判断） */
int levelEvent = -1;         /* 本层随机事件，-1 = 无 */
int eventBonus = 0;          /* 本局事件累计加分 */

const char* const eventNames[EVENT_COUNT] = {
    "BLOOD MOON", "BLACKOUT", "DEAD SILENCE", "SUPPLY DROP",
    "FLOOD", "WHISPERS", "MIRROR"
};

/* 通过本层时给的分 */
int EventScore(int ev) {
    switch (ev) {
    case EV_BLOOD_MOON: return 1500;
    case EV_BLACKOUT:   return 1000;
    case EV_SILENCE:    return 800;
    case EV_SUPPLY:     return 600;
    case EV_FLOOD:      return 1200;    /* v2.0 */
    case EV_WHISPER:    return 1300;
    case EV_MIRROR:     return 1600;
    default:            return 0;
    }
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
int pickMode = 0;      /* v2.0：0 = 局内技能，1 = 遗物 */
int bossLevel = 0;     /* v2.1：本层是否 Boss 层 */
int bossMaxHp = 1;
int bossDefeated = 0;
int totalKills = 0;      /* v2.0：累计击杀，成就用 */
int shortcutUsed = 0;
int helpPage = 0;   /* v2.2：帮助页分页 */
/* v2.2：成就计数器（原来 15 个成就没有任何触发点） */
int killsWrench = 0, killsBottle = 0;
float crankSecondsTotal = 0.0f;
int floorsNoLight = 0, floorsFlawless = 0, floorsPacifist = 0, floorsFast = 0, floorsQuiet = 0;
float lowSanitySeconds = 0.0f;
int floorKills = 0, floorTookDamage = 0;
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
static const char* kCampaignName[5] = {
    "THE WARM-UP", "THE HUNT", "THE DROUGHT", "THE FLOOD", "THE LAST LIGHT"
};
static const char* kCampaignNameZh[5] = {
    "热身", "猎杀", "干旱", "洪水", "最后的灯"
};
const char* campaignName(int stage) {
    if (stage < 0 || stage >= 5) return "?";
    return Tr(kCampaignName[stage], kCampaignNameZh[stage]);
}
/* v2.0：燃烧瓶留下的火区（同时最多 3 处） */
static float fireX[3], fireY[3], fireT[3];
static float fireTick[3];
float leechHintTimer = 0.0f;
int fusesNeeded = 2;      /* 本关需要几根 */
int breakerOpen = 0;      /* 配电箱是否已通電 */
int hoverKind = 0;        /* 0=无 1=容器 2=配电箱 */
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

int achSelection = 0;
int achHovered = -1;     /* 鼠标悬停的成就索引，-1 = 无；用于显示 tooltip */
Achievement g_achievements[ACH_COUNT] = {0};

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
    int c[ITEM_SLOT_COUNT] = { flares, bottles, bricks, acids, smokeBombs, firePots, gluePots, mirrors };
    if (throwSelected < 0 || throwSelected >= ITEM_SLOT_COUNT) return 0;
    return c[throwSelected] > 0;
}

/* v2.6：键盘只有 3 个选槽键，但槽位有 5 个（砖/酸/烟都有）。
   原来 1/2/3 是写死的"第 0/1/2 号槽"，所以捡到酸之后键盘根本选不中它，
   只能靠滚轮 —— 玩家看到 HUD 上明明写着 ACID x2 却用不了。
   现在改成"第 N 个**有货的**槽"：HUD 只列有货的槽，第 3 行按 3 就对了。 */
static void SelectVisibleSlot(int n) {
    int counts[ITEM_SLOT_COUNT] = { flares, bottles, bricks, acids, smokeBombs, firePots, gluePots, mirrors };
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
    int counts[ITEM_SLOT_COUNT] = { flares, bottles, bricks, acids, smokeBombs, firePots, gluePots, mirrors };
    if (throwSelected < 0 || throwSelected >= ITEM_SLOT_COUNT) throwSelected = 0;
    if (counts[throwSelected] > 0) return;
    for (int i = 0; i < ITEM_SLOT_COUNT; i++) {
        if (counts[i] > 0) { throwSelected = i; uiDirty = 1; return; }
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
    {"Volume",          SET_SLIDER, NULL,        0, NULL, &volumeF,  0.0f, 1.0f, 0.05f, 6},
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

int Blocked(float x, float y) {
    int x0 = (int)floorf(x - PLAYER_RADIUS), x1 = (int)floorf(x + PLAYER_RADIUS);
    int y0 = (int)floorf(y - PLAYER_RADIUS), y1 = (int)floorf(y + PLAYER_RADIUS);
    if (x0 < 0 || y0 < 0 || x1 >= MAP_WIDTH || y1 >= MAP_HEIGHT) return 1;
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++)
            if (worldMap[cy][cx]) return 1;
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

    /* 一条 3 格高、从 x=2 到 x=21 的直走廊，y 中心 12 */
    int y0 = MAP_HEIGHT / 2 - 1, y1 = MAP_HEIGHT / 2 + 1;
    for (int y = y0; y <= y1; y++)
        for (int x = 2; x <= MAP_WIDTH - 3; x++)
            worldMap[y][x] = 0;

    posX = 3.5f;
    posY = (float)(MAP_HEIGHT / 2) + 0.5f;
    dirX = 1.0f; dirY = 0.0f;

    /* 靶子：走廊靠后，固定不动，只用来试投掷/敲击 */
    monster.x = 18.5f;
    monster.y = (float)(MAP_HEIGHT / 2) + 0.5f;
    monster.active = 1;
    monster.state = 2;
    monster.hp = 9999;
    monster.stun = 0.0f;
    monster.speed = 0.0f;

    lightBeast.active = 0;
    parasite.active = 0;
    for (int i = 0; i < LEECH_MAX; i++) leeches[i].active = 0;

    /* 教程里没有藏身点；必须清掉上一局留下的 active 标记，
       否则走廊里会凭空冒出「按 E 躲藏」提示。 */
    hideSpotCount = 0;
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
         x=13.5 / 15.5 / 17.5 / 19.5   四个锁箱
         x=20.5  固定靶
       注意调波器面板比别的锁宽（LUIS(760) vs LUIS(660)），
       正好用来验证宽面板在小窗口下的表现。 */
    {
        for (int k = 0; k < LOCK_TYPE_COUNT; k++) {
            int i = 1 + k;                       /* containers[1..4] */
            containers[i].x = 13.5f + (float)k * 2.0f;
            containers[i].y = (float)(MAP_HEIGHT / 2) + 0.5f;
            containers[i].active = 1;
            containers[i].searched = 0;
            containers[i].content = 0;
            containers[i].locked = 1;
            containers[i].lockType = k;          /* 0 拨盘 1 接线 2 均衡 3 调波 */
            containers[i].lockCode = 0x2A5;
        }
        containerCount = 1 + LOCK_TYPE_COUNT;    /* 1 个可搜 + 4 个上锁 */
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
    case TUT_LOOK:   return "LOOK  -  MOUSE";
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
    case TUT_THROW:
        snprintf(buf, sizeof(buf), "THROW  -  HOLD %s", VkName(g_bindings[ACT_THROW]));
        break;
    case TUT_KNOCK:
        snprintf(buf, sizeof(buf), "KNOCK  -  HOLD %s", VkName(g_bindings[ACT_CANCEL_CHARGE]));
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
}

static void TutorialAdvance(void) {
    if (tutorialStep >= TUT_DONE) return;
    tutorialStep++;
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
            /* 教程不写存档，也不影响正式进度，直接回主菜单。 */
            trainingMode = 0;
            currentState = STATE_MAIN_MENU;
            menuSelection = 0;
            isMapOpen = 0;
            mapFullScreen = 0;
            ForceCursor(1);
            uiDirty = 1;
        }
        return;
    }

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
        if (sprinting) tutorialActionTimer += dt;
        else tutorialActionTimer = 0.0f;
        if (tutorialActionTimer >= 0.5f) TutorialAdvance();
        break;
    case TUT_LIGHT:
        if (!flashlightOn) tutorialLightOff = 1;
        if (tutorialLightOff && flashlightOn) TutorialAdvance();
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
        if (tutorialKnocked) TutorialAdvance();
        break;
    case TUT_BREAKER:
        if (breakerOpen) TutorialAdvance();
        break;
    case TUT_EXIT:
        if (breakerOpen && Dist2(posX, posY, exitX, exitY) < 0.30f) TutorialAdvance();
        break;
    default:
        break;
    }
}


/* v2.1：找离出生点最远的空格（BFS 洪泛）。
   原来出口硬编码在出生点正右方 2 格 —— 每层开局第一帧构图完全一样，
   而且整层探索的动机被削掉一半。 */
static void FindFarthestCell(float* outX, float* outY) {
    static int dist[MAP_HEIGHT][MAP_WIDTH];
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) dist[y][x] = -1;

    int qx[MAP_WIDTH * MAP_HEIGHT], qy[MAP_WIDTH * MAP_HEIGHT];
    int head = 0, tail = 0;
    int sx = 1, sy = 1;
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

void GenerateLevel(void) {
    /* v2.0 修复：Boss 标记必须在**任何 SpawnMonster() 之前**设置。
       原来它写在函数中段（怪物已经刷出来之后），导致第 6/12/18 层的
       第一只怪是普通血量，被打死重生后才 ×8 —— Boss 整体错位一层。 */
    bossLevel = (currentLevel == 6 || currentLevel == 12 || currentLevel == 18) ? 1 : 0;
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
                if (rand() % 100 < 30) worldMap[y][x] = 0;
            }
        }
    }

    /* 随机小房间（严格限制在内部格，避免打穿边界墙） */
    for (int i = 0; i < 5; i++) {
        int rx = 2 + rand() % (MAP_WIDTH - 4);
        int ry = 2 + rand() % (MAP_HEIGHT - 4);
        for (int oy = 0; oy < 2; oy++)
            for (int ox = 0; ox < 2; ox++)
                worldMap[ry + oy][rx + ox] = 0;
    }

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

    /* 收集空地用于放置容器/配电箱/安全屋（排除出生区和出口周围） */
    static int cells[MAP_WIDTH * MAP_HEIGHT][2];
    int emptyCount = 0;
    for (int y = 1; y < MAP_HEIGHT - 1; y++) {
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (worldMap[y][x] == 0 && Dist2((float)x, (float)y, 1.5f, 1.5f) > 16.0f) {
                cells[emptyCount][0] = x;
                cells[emptyCount][1] = y;
                emptyCount++;
            }
        }
    }

    FindFarthestCell(&exitX, &exitY);   /* v2.1：同上 */
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
    if (campaignMode) ApplyCampaignRules(campaignStage);
    levelBanner = 5.0f;

    /* DARK：手电起始电量只有 55%。必须放在掷词条之后，
       否则用的是上一层的 levelMod，词条会延迟一层生效。 */
    if (levelMod == 0) battery = (float)BatteryMax() * 0.55f;

    /* 本关需要的保险丝：2 根起，每 3 关 +1，最多 4（主线）；无尽模式封顶到 MAX_CONTAINERS */
    if (isEndlessRun) {
        fusesNeeded = 2 + currentLevel / 3;   /* 见下方与箱子数联动的夹取 */
        if (fusesNeeded > MAX_CONTAINERS) fusesNeeded = MAX_CONTAINERS; /* 新增限制 */
    } else {
        /* v3.0 平衡：**不再封顶 4** ——
           原来第 6 层之后恒定 4 根，18 层零变化，玩家感觉不到难度推进。
           新曲线：每 3 层 +1，封顶 6；并且不许多于「箱子数 - 2」，
           给电池/药/投掷物留出货位（原来无尽第 18 层 8 根 = 全箱都是保险丝）。 */
        fusesNeeded = 2 + currentLevel / 3;
        if (fusesNeeded > 6) fusesNeeded = 6;
        fusesNeeded -= (HasKeystone(SK_FUSE_LESS) ? 1 : 0) + RunFuseLess();
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
    int contCap = ContainerTargetCount(levelMod, baseContainers);
    if (contCap < fusesNeeded) contCap = fusesNeeded;

    /* v3.0 平衡：保险丝不许多于「箱子数 - 2」——
       否则整层所有箱子都是保险丝，电池/药/投掷物一件都搜不到。 */
    if (fusesNeeded > contCap - 2) fusesNeeded = contCap - 2;
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
        /* v3.0：上锁箱子（移植自上游 v2.2.0）——
           第 2 层起 18% 概率，每层最多 2 个。 */
        containers[containerCount].locked = 0;
        containers[containerCount].lockType = 0;
        containers[containerCount].lockCode = 0;
        if (currentLevel >= 2 && lockedThisFloor < 2 && (rand() % 100) < 18) {
            lockedThisFloor++;
            containers[containerCount].locked = 1;
            containers[containerCount].lockType = rand() % LOCK_TYPE_COUNT;
            containers[containerCount].lockCode = rand() & 0x7FF;
        }
        containerCount++;
    }
    /* v3.0：藏身点 —— 两轮放置：
       第一轮按正常约束挑（避开出口/容器/配电箱/彼此）
       第二轮**放宽**（只要求不是墙），保证一定生得出来。
       之前只做第一轮，结果 SpotClear 把候选全拒了 → 一个都没有
       （用户实测 hideSpotCount=0）。 */
    hideSpotCount = 0;
    {
        int want = 2 + (rand() % 2);          /* v3.3：3~5 → 2~3，藏身点少一点 */

        /* ---- 第一轮：严格 ---- */
        for (int tries = 0; tries < 300 && hideSpotCount < want && emptyCount > 0; tries++) {
            int idx = rand() % emptyCount;
            float hx2 = cells[idx][0] + 0.5f, hy2 = cells[idx][1] + 0.5f;
            if (Dist2(hx2, hy2, exitX, exitY) < 6.0f) continue;
            if (!SpotClear(hx2, hy2, 1.4f, 1, -1)) continue;
            int bad = 0;
            for (int k = 0; k < hideSpotCount; k++)
                if (Dist2(hx2, hy2, hideSpots[k].x, hideSpots[k].y) < 4.0f) { bad = 1; break; }
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
                if (Dist2(hx2, hy2, hideSpots[k].x, hideSpots[k].y) < 1.5f) { bad = 1; break; }
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
    while (containerCount < contCap && containerCount < MAX_CONTAINERS && attempts < 512) {
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

        /* 修复死局 Bug：优先保证能通关（放满 fusesNeeded）。
   只有在箱子极其充裕（多出 2 个以上）时，才留格子给其他物品。 */
        int need = fusesNeeded;
        if (need > containerCount) {
            need = containerCount; // 极端兜底，防止越界
        }
        for (int guard = 0; guard < 64 && need > 0 && containerCount > 0; guard++) {
            int k = rand() % containerCount;
        /* v3.0：**保险丝不能落在上锁的箱子里** ——
           否则打不开锁就拿不到保险丝，整层死锁（玩法审查）。 */
        {
            int tries = 0;
            while (tries++ < 32 && containers[k].locked) {
                k = rand() % containerCount;
            }
        }

            if (containers[k].content == 0) {
                containers[k].content = 1;
                need--;
            }
        }
        for (int k = 0; k < containerCount; k++) {
            if (containers[k].content == 0) {
                int r = rand() % 100;
                /* 40% 电池 25% 药 15% 荧光棒 12% 玻璃瓶 8% 空 */
                if (r < 40) containers[k].content = 2;
                else if (r < 65) containers[k].content = 3;
                else if (r < 80) containers[k].content = 4;
                else if (r < 92) containers[k].content = 5;
                else containers[k].content = 0;
            }
        }

        {
        int nt = rand() % THEME_ENABLED;
        if (nt == themeId) nt = (nt + 1) % THEME_ENABLED;
        GenerateThemeTextures(nt);
    }
    SpawnMonster();
    SpawnLightBeast();
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
        levelEvent = rand() % EVENT_COUNT;
    /* v2.0：每日挑战的固定加成，每层都生效 */
    if (dailyMode) {
        modSanityMul *= 1.25f;
        modShardMul *= 1.25f;
    }
    if (levelEvent == EV_BLOOD_MOON) {
        modSpeedMul *= 1.15f;                 /* 怪更快 */
    } else if (levelEvent == EV_BLACKOUT) {
        float cap = (float)BatteryMax() * 0.5f;
        if (battery > cap) battery = cap;     /* 本层电量上限砍半 */
    } else if (levelEvent == EV_SUPPLY) {
        /* v2.2：补给空投给足量（原来最多 +1 荧光棒 +1 瓶，等于没有） */
        flares += 2;
        if (bottles < BottleCap()) bottles += 2;
        firePots++;
    } else if (levelEvent == EV_SILENCE) {
        /* v2.2：死寂从 0.5 加强到 0.3（0.5 在走路噪声 4.5 格上几乎感知不到） */
        modNoiseMul = 0.30f;
    } else if (levelEvent == EV_FLOOD) {
        /* v2.0 涨水：手电耗电翻倍，本层更逼你省电 */
        modBatteryMul = 2.0f;
    } else if (levelEvent == EV_WHISPER) {
        /* v2.0 低语：理智流失 +50%，碎片 +50% —— 拿命换钱 */
        modSanityMul *= 1.5f;
        modShardMul *= 1.5f;
    } else if (levelEvent == EV_MIRROR) {
        /* v2.2 修复：描述写"怪物成倍出现"，实际只 +10% 速度和理智 ——
           玩家完全感知不到。现在真的多放 3 只游荡怪（它们已经可杀）。 */
        modSpeedMul *= 1.10f;
        modSanityMul *= 1.10f;
        int added = 0;
        for (int i = 0; i < EXTRA_MAX && added < 3; i++) {
            if (extras[i].active) continue;
            ExtrasSpawnOne(i);
            if (extras[i].active) added++;
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
    BestiaryMarkSeen(0);                                  /* 寻声鬼：每层都有 */
    if (lightBeast.active) BestiaryMarkSeen(1);           /* 追光鬼 */
    if (currentLevel >= LEECH_MIN_LEVEL) BestiaryMarkSeen(2);   /* 偷电怪 */
    if (jumperTrigger >= 0.0f) BestiaryMarkSeen(3);       /* 跳脸虫 */

    /* 记录本局见过的词条种类（算难度乘子用） */
    if (levelMod >= 0) {
        int bit = 1 << levelMod;
        if (!(modsSeenMask & bit)) { modsSeenMask |= bit; modsSeenCount++; }
    }
    /* 跳脸虫：第 3 层起按概率出现，不是每关必出 ——
       稀有才有惊吓感。触发时机也随机（进关 8~20 秒后），
       不然一进关就跳出来太突兀。 */
    if (currentLevel >= JUMPER_MIN_LEVEL && rand() % 100 < JUMPER_APPEAR_CHANCE) {
        jumperTrigger = 8.0f + (float)(rand() % 13);   /* 8~20 秒 */
    } else {
        jumperTrigger = -1.0f;
    }
    jumperPhase = JMP_IDLE;
    jumperTimer = 0.0f;
    parasite.active = 0;
    for (int i = 0; i < MAX_THROWN; i++) thrown[i].active = 0;

    /* v2.0：安全屋改成每 3 层一个（第 3/6/9… 层），其余层不放 */
    safeRoom.active = 0;
    if (currentLevel % 3 != 0) {
        /* 本层没有安全屋，直接跳过下面的放置 */
    } else
    for (int t = 0; t < 256; t++) {
        int idx = rand() % emptyCount;
        float sx = cells[idx][0] + 0.5f, sy = cells[idx][1] + 0.5f;
        if (Dist2(sx, sy, 1.5f, 1.5f) < 25.0f) continue;
        if (Dist2(sx, sy, exitX, exitY) < 9.0f) continue;
        if (!SpotClear(sx, sy, 1.6f, 3, -1)) continue;
        safeRoom.x = sx;
        safeRoom.y = sy;
        safeRoom.active = 1;
        break;
    }
    /* 强制兜底：只有「本层该有安全屋」时才兜底。
       v2.1：原来这段在 else 之外 —— 非 3 倍层刚被清 0，兜底立刻又放一个，
       结果 10/10 层都有安全屋。 */
    if (currentLevel % 3 == 0 && !safeRoom.active) {
        float bestD2 = -1.0f;
        for (int y = 1; y < MAP_HEIGHT - 1; y++) {
            for (int x = 1; x < MAP_WIDTH - 1; x++) {
                if (worldMap[y][x]) continue;
                float sx = x + 0.5f, sy = y + 0.5f;
                if (Dist2(sx, sy, 1.5f, 1.5f) < 25.0f) continue;
                if (Dist2(sx, sy, exitX, exitY) < 9.0f) continue;
                if (!SpotClear(sx, sy, 1.2f, 3, -1)) continue;  /* 放宽到 1.2f */
                float d2 = Dist2(sx, sy, 1.5f, 1.5f);
                if (d2 > bestD2) {
                    bestD2 = d2;
                    safeRoom.x = sx;
                    safeRoom.y = sy;
                }
            }
        }
        if (bestD2 > 0.0f) safeRoom.active = 1;
    }

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
    /* 最终兜底：如果还失败（极端情况），直接找任意空地 */
    /* v2.2 修复：兜底必须也遵守"每 3 层"的规则。
                     原来这段无条件执行，而里面会把安全屋放上 ——
                     于是 %3 的判断被完全架空，24 层生成 24 个安全屋
                     （每层白送 +60 电 + 一次收费休息）。 */
    if ((currentLevel % 3 == 0) && (!safeRoom.active || !breaker.active)) {
        for (int y = 1; y < MAP_HEIGHT - 1 && (!safeRoom.active || !breaker.active); y++) {
            for (int x = 1; x < MAP_WIDTH - 1 && (!safeRoom.active || !breaker.active); x++) {
                if (worldMap[y][x]) continue;
                float cx = x + 0.5f, cy = y + 0.5f;
                int placedSafe = 0;  /* 标志位：本格刚给了安全屋，配电箱必须跳过这一格 */
                if (!safeRoom.active && Dist2(cx, cy, 1.5f, 1.5f) >= 25.0f && Dist2(cx, cy, exitX, exitY) >= 9.0f &&
                    !(breaker.active && cx == breaker.x && cy == breaker.y)) {
                    safeRoom.x = cx; safeRoom.y = cy; safeRoom.active = 1;
                    placedSafe = 1;
                }
                if (!placedSafe && !breaker.active && Dist2(cx, cy, 1.5f, 1.5f) >= 225.0f &&
                    !(safeRoom.active && cx == safeRoom.x && cy == safeRoom.y)) {
                    breaker.x = cx; breaker.y = cy; breaker.active = 1;
                }
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
    int m = 100 + SkillBatteryAdd() + RunBatteryCapDelta();
    if (m < 30) m = 30;
    return m;
}

/* 手电每秒耗电：ECO 升级每级 -12% */
float BatteryDrainRate(void) { return BATTERY_DRAIN * SkillEcoMul() * RelicBatteryMul() * modBatteryMul * BenchFlashDrainMul() * RunUpgFlashMul(); }   /* v2.7 省电 */

/* 安全屋回智速度：REST 升级每级 +25% */
float SafeHealRate(void) {
    return SAFE_HEAL_RATE * (1.0f + SkillRestAdd() + RelicHealAdd());   /* REST + IRON LUNG */
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
    int cap = 4 + RunUpgCarryBonus();   /* v3.0：强化第 0 条「携带量」+2/级 */
    if (cap < 4) cap = 4;
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
int   calibAttempts = CALIB_ATTEMPTS;
int   calibLevel = -1;        /* v2.4：这一层是否已经发过次数（防「出门再进来」刷满） */
float calibCooldown = 0.0f;
int   calibResult = 0;        /* 0 无 / 1 完美 / 2 普通 / 3 失手 */
float calibResultTimer = 0.0f;

float SanityMax(void) {
    float m = 100.0f + RelicMaxSanAdd();
    if (m < 25.0f) m = 25.0f;
    return m;
}

float StaminaMax(void) {
    /* v3.0：改造树「耐力」+30 */
    return 100.0f + (float)RunStaminaAdd() + (float)BenchStaminaBonus();
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

static void AddUpgrade(int kind) {
    switch (kind) {
        case 0: upgBattery++; break;
        case 1: upgSpeed++; break;
        case 2: upgQuiet++; break;
        case 3: upgSanity++; break;
        case 4: upgEco++; break;
        default: upgRest++; break;
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

static int BuyUpgrade(int kind) {
    int cost = UpgradeCost(kind);
    if (cost < 0 || shards < cost) return 0;
    shards -= cost;
    AddUpgrade(kind);
    PlaySfx(SFX_POWER, 0.8f);
    SaveGame();
    return 1;
}

/* ---------------- 寻声怪 ---------------- */
/* v1.16：蓄满（>=0.85）额外 +1 伤害 */
static int chargeBonus(float charge) {
    int b = (charge >= 0.85f) ? 1 : 0;
    if (b) b += SkillHeavyHand();    /* HEAVY HAND */
    return b;
}

/* v2.7：ThrowItem 是否真的生成了一个投掷物。
   以前池子满时它会静默不生成，而调用方已经先把数量扣掉了 —— 东西凭空消失。 */
static int g_shardTint = 0;   /* v2.1：下一批碎片的颜色类型 */
static int g_throwOk = 0;

static void ThrowItem(int type, float charge) {
    g_throwOk = 0;
    if (charge < 0.1f) charge = 0.1f;
    if (charge > 1.0f) charge = 1.0f;

    float speed = SHOT_SPEED + (SHOT_MAX_SPEED - SHOT_SPEED) * charge;
    float angle = 0.35f * charge;  /* 抛物线角度：蓄力越高越平直 */

    if (type == 1) {
        /* 玻璃瓶：抛物线飞行弹丸 */
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (shot[i].active) continue;
            shot[i].x = posX;
            shot[i].y = posY;
            shot[i].z = SHOT_HEIGHT;
            shot[i].vx = dirX * speed * cosf(angle);
            shot[i].vy = dirY * speed * cosf(angle);
            shot[i].vz = speed * sinf(angle);  /* 向上分量 */
            shot[i].life = 2.0f;
            shot[i].age = 0.0f;
            shot[i].charge = charge;
            shot[i].kind = 1;
            shot[i].dmg = 0;
            shot[i].pierce = 0;
            shot[i].mirrorRebounded = 0;
            shot[i].ignoreTarget = -1;
            shot[i].ignoreTargetTime = 0.0f;
            shot[i].active = 1; g_throwOk = 1;
            break;
        }
        PlaySfx(SFX_THROW, 0.6f);  /* 投掷专用音效：呼啸+出手闷响 */
        return;
    }

    /* ---- v1.16 砖头 / 酸瓶 / 烟雾弹：都走 shot 弹道 ---- */
    if (type >= 2) {
        int kind = type - 1;
        /* v2.0：2 砖 / 3 酸 / 4 烟 / 5 火 / 6 胶 / 7 镜 */
        kind = (type == 2) ? 2 : (type == 3) ? 3 :
               (type == 4) ? 4 : (type == 5) ? 7 : (type == 6) ? 8 : 9;
        /* 砖头更重、飞得低且慢；酸瓶更轻、抛物线更高，避免只是换一张贴图。 */
        float itemSpeed = speed;
        float itemAngle = angle;
        float itemHeight = 1.0f;
        if (type == 2) {
            itemSpeed *= 0.82f;
            itemAngle *= 0.65f;
            itemHeight = 0.90f;
        } else if (type == 3) {
            itemSpeed *= 1.08f;
            itemAngle = angle * 1.35f + 0.08f;
            itemHeight = 1.08f;
        }
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (shot[i].active) continue;
            shot[i].x = posX + dirX * 0.35f;
            shot[i].y = posY + dirY * 0.35f;
            shot[i].z = SHOT_HEIGHT * (type == 4 ? 0.9f : itemHeight);
            shot[i].vx = dirX * itemSpeed * cosf(itemAngle);
            shot[i].vy = dirY * itemSpeed * cosf(itemAngle);
            shot[i].vz = itemSpeed * sinf(itemAngle);
            shot[i].life = (type == 4) ? 1.6f : 2.0f;
            shot[i].age = 0.0f;
            shot[i].charge = charge;
            shot[i].kind = kind;
            shot[i].dmg = 0;
            shot[i].pierce = (kind == 9) ? 1 : 0;
            shot[i].mirrorRebounded = 0;
            shot[i].ignoreTarget = -1;
            shot[i].ignoreTargetTime = 0.0f;
            shot[i].active = 1; g_throwOk = 1;
            break;
        }
        PlaySfx(SFX_THROW, 0.6f);
        return;
    }

    /* 荧光棒：抛物线飞行，落地后转为静止发光体 */
    float flareSpeed = 6.5f + charge * 3.0f;   /* v1.11：6.5~9.5 格/秒（原来满蓄力太远） */
    /* v1.11：仰角从 0.4~0.7 压到 0.12~0.22 rad（7°~13°）。
       之前仰角太大，荧光棒升到 3~4 倍视高，而视角不能往上看 ——
       玩家看到的就是"扔出去先空两秒，第三秒球才从上面掉下来"。
       压平之后出手就能看到弧线。 */
    float flareAngle = 0.12f + charge * 0.10f;
    for (int i = 0; i < MAX_THROWN; i++) {
        if (thrown[i].active) continue;
        /* v1.10：出手点往前挪 0.45 格。
           原来与玩家同一位置 → 投影距离 ty≈0，被 ProjectSprite 的
           "太近就丢弃"挡掉，画面上就是"扔出去先消失一下再出现"。 */
        thrown[i].x = posX + dirX * 0.45f;
        thrown[i].y = posY + dirY * 0.45f;
        thrown[i].z = SHOT_HEIGHT * 0.45f;   /* v1.11：从腹部/胯部出手，不再从头顶 */
        thrown[i].vz = flareSpeed * sinf(flareAngle);
        thrown[i].vx = dirX * flareSpeed * cosf(flareAngle);
        thrown[i].vy = dirY * flareSpeed * cosf(flareAngle);
        thrown[i].type = 0;        /* 0=荧光棒 */
        thrown[i].life = 25.0f;    /* 落地后发光时长 */
        thrown[i].active = 1; g_throwOk = 1;
        break;
    }
    PlaySfx(SFX_THROW, 0.6f);  /* 投掷专用音效：呼啸+出手闷响 */
}

/* 推进所有飞行中的玻璃瓶，结算命中 + 抛物线 + 碎片 */
static void UpdateShots(float dt) {
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!shot[i].active) continue;
        shot[i].life -= dt;
        shot[i].age += dt;
        if (shot[i].ignoreTargetTime > 0.0f) {
            shot[i].ignoreTargetTime -= dt;
            if (shot[i].ignoreTargetTime <= 0.0f) {
                shot[i].ignoreTargetTime = 0.0f;
                shot[i].ignoreTarget = -1;
            }
        }

        /* v1.18：重力 + 落地。
           之前 shot 完全没有 z 积分 —— 砖头/酸瓶/烟雾弹都是
           从胸口直线飞出去、而且永远不落地（烟雾弹自然出不了烟）。 */
        shot[i].vz -= 16.0f * dt;
        shot[i].z += shot[i].vz * dt;
        if (shot[i].z <= 0.0f && shot[i].age > 0.02f) {
            shot[i].z = 0.0f;
            if (shot[i].kind == 4) SpawnSmoke(shot[i].x, shot[i].y);
            else SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
            /* v2.0：燃烧瓶落地留火区 */
            if (shot[i].kind == 7) {
                for (int f = 0; f < 3; f++) {
                    if (fireT[f] > 0.0f) continue;
                    fireX[f] = shot[i].x; fireY[f] = shot[i].y;
                    fireT[f] = 6.0f; fireTick[f] = 0.0f;
                    break;
                }
            }
            MakeNoise(shot[i].x, shot[i].y, (shot[i].kind == 2) ? 3.0f : 14.0f);
            shot[i].active = 0;
            continue;
        }

        /* 分段推进，避免高速穿过怪 */
        float stepLen = sqrtf(shot[i].vx*shot[i].vx + shot[i].vy*shot[i].vy + shot[i].vz*shot[i].vz) * dt;
        int steps = (int)(stepLen / 0.12f) + 1;
        float sdx = shot[i].vx * dt / (float)steps;
        float sdy = shot[i].vy * dt / (float)steps;
        float sdz = shot[i].vz * dt / (float)steps;

        for (int s = 0; s < steps && shot[i].active; s++) {
            shot[i].x += sdx;
            shot[i].y += sdy;
            shot[i].z += sdz;
            shot[i].vz -= GRAVITY * dt / (float)steps;  /* 重力 */

            /* 撞地面：碎裂生成碎片 */
            if (shot[i].z <= 0.0f) {
                shot[i].z = 0.0f;
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
                PlaySfx(SFX_GLASS, 0.7f);
                /* v3.0 逃生试炼：**扔东西制造噪音把追捕者引开**是核心手法，
                   落地的动静要够大，半径 14 → 22 格。 */
                MakeNoise(shot[i].x, shot[i].y, 15.0f);   /* v3.0 平衡：22 → 15 */
                shot[i].active = 0;
                break;
            }

            /* 撞墙：碎裂生成碎片 */
            if (shot[i].age > 0.02f && ShotBlocked(shot[i].x, shot[i].y)) {
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
                PlaySfx(SFX_GLASS, 0.7f);
                MakeNoise(shot[i].x, shot[i].y, 15.0f);   /* v3.0 平衡：22 → 15 */   /* v3.0：撞墙也要够响 */
                shot[i].active = 0;
                break;
            }

            /* 撞怪：按 kind 分派（1 玻璃瓶 / 2 砖头 / 3 酸瓶 / 4 烟雾弹） */
            float r = SHOT_RADIUS + 0.24f;
            {
                Monster* targets[2];
                int nTargets = 0;
                if (monster.active) targets[nTargets++] = &monster;
                if (lightBeast.active) targets[nTargets++] = &lightBeast;
                for (int t = 0; t < nTargets; t++) {
                    Monster* m = targets[t];
                    if (shot[i].ignoreTargetTime > 0.0f && shot[i].ignoreTarget == t) continue;
                    if (Dist2(shot[i].x, shot[i].y, m->x, m->y) >= r * r) continue;

                    int base = 1, type = DT_SHARD;
                    g_dmgIgnoreArmor = 0;
                    if (shot[i].kind == 2) {            /* 砖头：钝击 12 点，蓄满 15 */
                        base = 12 + chargeBonus(shot[i].charge) * 3;
                        type = DT_BLUNT;
                    } else if (shot[i].kind == 3) {     /* 酸瓶：碎片 10 点、无视护甲 */
                        base = 10 + chargeBonus(shot[i].charge) * 2;
                        type = DT_SHARD;
                        g_dmgIgnoreArmor = 1;
                    } else if (shot[i].kind == 4) {     /* 烟雾弹：不造成伤害 */
                        SpawnSmoke(shot[i].x, shot[i].y);
                        shot[i].active = 0;
                        break;
                    } else if (shot[i].kind == 8) {    /* v2.0 胶水瓶：定身 2 秒，无伤害 */
                        m->stun = 2.0f;
                        m->glued = 1;   /* v2.2：胶水期间受伤 +50% */
                        shot[i].active = 0;
                        SpawnShards(shot[i].x, shot[i].y, 0, 0, 0.3f);
                        PlaySfx(SFX_GLASS, 0.6f);
                        break;
                    } else if (shot[i].kind == 9) {    /* v2.0 镜片：反弹一次 */
                        if (shot[i].charge > 0.5f && !shot[i].mirrorRebounded) {
                            /* 不再严格 180°折返：那会把弹片立刻送回原目标/玩家。
                               加一点侧向偏转，并短暂忽略被反弹的目标。 */
                            float oldVx = shot[i].vx, oldVy = shot[i].vy;
                            float side = 0.22f;
                            float rvx = -oldVx - oldVy * side;
                            float rvy = -oldVy + oldVx * side;
                            float rlen = sqrtf(rvx * rvx + rvy * rvy);
                            float olen = sqrtf(oldVx * oldVx + oldVy * oldVy);
                            if (rlen > 0.001f && olen > 0.001f) {
                                shot[i].vx = rvx * olen / rlen * 0.8f;
                                shot[i].vy = rvy * olen / rlen * 0.8f;
                            } else {
                                shot[i].vx = -oldVx * 0.8f;
                                shot[i].vy = -oldVy * 0.8f;
                            }
                            shot[i].x += shot[i].vx * 0.08f;
                            shot[i].y += shot[i].vy * 0.08f;
                            shot[i].charge = 0.0f;
                            shot[i].mirrorRebounded = 1;
                            shot[i].ignoreTarget = t;
                            shot[i].ignoreTargetTime = 0.18f;
                            break;                     /* 保留飞行 */
                        }
                        base = 1; type = DT_SHARD;
                    } else if (shot[i].kind == 7) {    /* v2.0 燃烧瓶：直接命中 = 10 点火伤 */
                        base = 10; type = DT_SHARD;
                        g_dmgIgnoreArmor = 0;
                    } else if (shot[i].kind == 6) {     /* v2.0 信号枪弹 */
                        base = shot[i].dmg > 0 ? shot[i].dmg : 8;
                        type = WeaponDamageType(curWeapon);
                    } else {                            /* 玻璃瓶 */
                        base = (int)(SHOT_DAMAGE * RunThrowDmgMul() + 0.5f) + chargeBonus(shot[i].charge);   /* v2.2 SILENT BOTTLE */
                    }
                    SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
                    ApplyDamage(m, base, type, shot[i].x, shot[i].y);
                    if (shot[i].kind == 6) WeaponAddXp(WPN_RANGED, 1);   /* v2.1：远程也能练 */
                    g_dmgIgnoreArmor = 0;
                    if (shot[i].pierce) { shot[i].pierce = 0; continue; }   /* v2.0：穿透一次 */
                    shot[i].active = 0;
                    break;
                }
                if (!shot[i].active) break;
            }
        }
        if (shot[i].life <= 0.0f || (shot[i].z <= 0.0f && shot[i].age > 0.02f)) {
            if (shot[i].kind == 4) {
                SpawnSmoke(shot[i].x, shot[i].y);       /* 烟雾弹落地成云 */
            } else if (shot[i].z > 0.0f) {
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
            }
            shot[i].active = 0;
        }
    }

    /* 更新玻璃碎片 */
    for (int i = 0; i < MAX_SHOTS * SHARD_COUNT; i++) {
        if (!shards_debris[i].active) continue;
        shards_debris[i].age += dt;
        shards_debris[i].life -= dt;
        shards_debris[i].x += shards_debris[i].vx * dt;
        shards_debris[i].y += shards_debris[i].vy * dt;
        shards_debris[i].z += shards_debris[i].vz * dt;
        shards_debris[i].vz -= GRAVITY * dt;
        if (shards_debris[i].z <= 0.0f) {
            shards_debris[i].z = 0.0f;
            shards_debris[i].vz *= -0.3f;  /* 反弹衰减 */
            shards_debris[i].vx *= 0.7f;
            shards_debris[i].vy *= 0.7f;
        }
        if (shards_debris[i].life <= 0.0f) {
            shards_debris[i].active = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* v2.0 武器攻击                                                        */
/* ------------------------------------------------------------------ */

/* v2.0 TYPE WORK：武器伤害类型。
   rank 0 = 按武器默认（近战钝 / 远程碎片 / 法术斩）；
   rank 1/2/3 = 强制 斩 / 钝 / 碎片 —— 让"改刃"变成石头剪刀布。 */
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
static int g_hitCombo = 0;
static int WeaponHitTick(void) {
    g_hitCombo++;
    if (SkillRhythmActive(g_hitCombo)) return 1;
    return 0;
}

void WeaponTryAttack(float charge) {
#if NO_COMBAT
    /* v3.0 逃生试炼：**左键不再是攻击，是敲击**。
       朝前方敲一下（墙/管道/门），制造噪音把追捕者引过去 ——
       这是逃生类的经典操作：用声音控制敌人的位置。
       不消耗能量、不判定伤害、不产生子弹。 */
    MakeNoise(posX + dirX * 2.0f, posY + dirY * 2.0f, 11.0f);   /* v3.0 平衡：18 → 11 */
    PlaySfx(SFX_GLASS, 0.55f);
    if (trainingMode) tutorialKnocked = 1;
    pickupMsg = g_lang == LANG_ZH ? "敲击  制造噪音" : "KNOCK  NOISE";
    pickupMsgTimer = 0.8f;
    uiDirty = 1;
    (void)charge;
    return;
#endif

    if (weaponCd > 0.0f) return;
    const WeaponDef* w = WeaponGet(curWeapon);
    float cost = WeaponCostNow(curWeapon);
    if ((float)WeaponResourceAmount(w->resource) < cost) {
        pickupMsg = "NOT ENOUGH";
        pickupMsgTimer = 1.0f;
        PlaySfx(SFX_POWER, 0.25f);
        return;
    }

    WeaponSpend(w->resource, cost);
    /* v2.1：开火反馈 */
    muzzleFlash = 1.0f;
    AddShake(w->kind == WPN_MELEE ? 0.16f : 0.11f, w->kind == WPN_MELEE ? 0.10f : 0.055f);
    /* v2.0：体力是唯一会自然回复的资源，攻击后压住回复 0.7 秒，
       否则 20/s 的回复会把 18 点的消耗在两秒内吃掉，「烧体力」就没有意义了。 */
    /* v2.3：三把武器都烧能量，体力不再被攻击压住恢复 */
    weaponCd = WeaponCdNow(curWeapon);

    int dmg = WeaponDamageNow(curWeapon, charge);
    int silent = WeaponSilentNow(curWeapon);
    MakeNoise(posX, posY, w->noise * (silent ? 0.5f : 1.0f));   /* v3.0：轻步已移到脚步噪音处 */

    if (w->kind == WPN_MELEE) {
        /* 近战：沿视线扫 6 段，命中就停（除非有穿透） */
        int hits = 0;
        float range = WeaponRangeNow(curWeapon);
        for (int st = 1; st <= 6; st++) {
            float d = range * (float)st / 6.0f;
            float px = posX + dirX * d, py = posY + dirY * d;
            if (Blocked(px, py)) break;
            if (monster.active && Dist2(px, py, monster.x, monster.y) < 0.42f) {
                g_dmgIgnoreArmor = WeaponHitTick();
                ApplyDamage(&monster, dmg, WeaponDamageType(curWeapon), posX, posY);
                g_dmgIgnoreArmor = 0;
                hits++;
            }
            if (lightBeast.active && Dist2(px, py, lightBeast.x, lightBeast.y) < 0.42f) {
                g_dmgIgnoreArmor = WeaponHitTick();
                ApplyDamage(&lightBeast, dmg, WeaponDamageType(curWeapon), posX, posY);
                g_dmgIgnoreArmor = 0;
                hits++;
            }
            /* v2.2：游荡怪（影子/盲螨/回响）之前完全打不到 */
            if (DamageExtrasAt(px, py, 0.45f, dmg)) hits++;
            for (int i = 0; i < LEECH_MAX; i++) {
                if (!leeches[i].active) continue;
                if (Dist2(px, py, leeches[i].x, leeches[i].y) < 0.42f) {
                    leeches[i].active = 0;
                    hits++;
                }
            }
            if (hits > 0 && !WeaponPierceNow(curWeapon)) break;
        }
        if (hits > 0) {
            slashHitStop = 0.04f;
            AddShake(0.22f, 0.16f);
            SpawnShards(posX + dirX * 1.2f, posY + dirY * 1.2f, dirX * 2.0f, dirY * 2.0f, charge);
            WeaponAddXp(curWeapon, hits);
            PlaySfx(SFX_GLASS, 0.6f);
        } else {
            PlaySfx(SFX_STEP, 0.45f);
        }
        return;
    }

    if (w->kind == WPN_RANGED) {
        /* 远程：塞进 shot 池，kind 6 */
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (shot[i].active) continue;
            shot[i].x = posX + dirX * 0.35f;
            shot[i].y = posY + dirY * 0.35f;
            shot[i].z = SHOT_HEIGHT * 0.85f;
            shot[i].vx = dirX * w->speed;
            shot[i].vy = dirY * w->speed;
            shot[i].vz = 0.6f;
            shot[i].life = WeaponRangeNow(curWeapon) / (w->speed > 0.01f ? w->speed : 1.0f);
            if (shot[i].life < 0.10f) shot[i].life = 0.10f;
            shot[i].age = 0.0f;
            shot[i].charge = charge;
            shot[i].kind = 6;
            shot[i].dmg = dmg;
            shot[i].pierce = WeaponPierceNow(curWeapon);   /* v2.0：穿透配件对远程生效 */
            shot[i].mirrorRebounded = 0;
            shot[i].ignoreTarget = -1;
            shot[i].ignoreTargetTime = 0.0f;
            shot[i].active = 1;
            break;
        }
        PlaySfx(SFX_THROW, 0.8f);
        return;
    }

    /* 法术：走光束（和剑气同一套渲染），穿透 */
    slashActive = 1;
    slashX = posX + dirX * 0.4f;
    slashY = posY + dirY * 0.4f;
    slashDX = dirX;
    slashDY = dirY;
    slashTravel = 0.0f;
    slashFade = 0.0f;
    slashDmg = dmg;
    slashMaxDist = WeaponRangeNow(curWeapon);
    if (slashMaxDist < 4.0f) slashMaxDist = 4.0f;
    PlaySfx(SFX_CRANK, 0.7f);
}

/* v2.1：对外统一的粒子接口。tint: 0 玻璃 / 1 血 / 2 尘 */
void SpawnParticles(float x, float y, int tint) {
    g_shardTint = tint;
    SpawnShards(x, y, 0.0f, 0.0f, 0.45f);
    g_shardTint = 0;
}

/* 生成玻璃碎片：撞击点产生随机扩散的碎片 */
static void SpawnShards(float x, float y, float vx, float vy, float charge) {
    int count = 6 + (int)(charge * 6);  /* 6~12 个碎片 */
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
void Update(float dt) {
    CalibTick(dt);   /* v2.4：校准小游戏计时（冷却与滑块） */
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
    /* v2.4：地图耗电。**必须在"全屏地图暂停"判断之前** ——
       全屏看图时整个 Update 会提前 return，放后面就永远不扣电。 */
    if (isMapOpen && currentState == STATE_PLAYING) {
        battery -= MAP_BATTERY_DRAIN * dt;
        if (battery <= 0.0f) {
            battery = 0.0f;
            isMapOpen = 0;
            mapFullScreen = 0;
            flashlightOn = 0;          /* 电耗光了手电也没了 */
            ForceCursor(0);            /* 全屏图关掉要把鼠标收回中心 */
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

    /* v3.0：**开锁盘时暂停理智流失** ——
       用户实测「开锁装置导致精神崩溃」：
       锁盘是模态层，玩家不能动也不能跑，理智却照扣。
       解一把弦线锁要 30~60 秒，按第 1 层 0.5/秒算是 15~30 点理智，
       到了第 10 层就是 30~60 点 —— 解谜变成自杀。
       现在把开锁当作和暂停同等的状态：理智冻结。 */
    if (lockActive) return;

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
           这个 keystone 之前是纯空函数（1200 碎片 + 2 核心买了个寂寞）。 */
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
    if (sanity <= 0.0f) {
        sanity = 0.0f;
        runInProgress = 0;
        if (currentLevel > bestLevel) bestLevel = currentLevel;
        /* v2.0 SALVAGE：死亡时保留一半待上缴碎片 */
    if (SkillSalvageKeep() > 0 && pendingShards > 0) {
        int keep = pendingShards * SkillSalvageKeep() / 100;
        shards += keep;
        pendingShards -= keep;
    }
    gameWon = 0;
        pendingShards = 0;      /* v1.8：本层未上缴的碎片没收 */
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
        battery -= BatteryDrainRate() * RunBatteryDrainMul() * SkillGhostStepDrainMul() * dt;
        if (battery <= 0.0f) { battery = 0.0f; flashlightOn = 0; }
    }

    /* 先判断交互目标，决定这一帧能不能移动 */
    hoverKind = 0;
    hoverIndex = -1;
    float bestD = INTERACT_RANGE * INTERACT_RANGE;
    for (int i = 0; i < containerCount; i++) {
        if (!containers[i].active || containers[i].searched) continue;
        float d = Dist2(posX, posY, containers[i].x, containers[i].y);
        if (d < bestD) { bestD = d; hoverKind = 1; hoverIndex = i; }
    }
    if (!breakerOpen && breaker.active) {
        float d = Dist2(posX, posY, breaker.x, breaker.y);
        if (d < INTERACT_RANGE * INTERACT_RANGE) {
            /* 配电箱优先于容器 */
            hoverKind = 2;
            hoverIndex = -1;
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
        } else if (hideSpotNear >= 0 && hoverKind == 0 && hideBlockTimer <= 0.0f) {
            /* v3.0 修复：**附近有可交互的箱子时，藏身点不抢 E** ——
               原来藏身点无条件抢在容器前面消费按键
               （用户实测：箱子和躲藏点刷在一起时箱子开不了）。 */
            if (IsActionPressed(ACT_INTERACT)) {
                hideKeyLatch = 1;
                hiddenIn = hideSpotNear;
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
    if (runUltimate >= 0 && !ultUsed && currentState == STATE_PLAYING
        && IsActionPressed(ACT_SWING)) {

        extern Monster lightBeast;
        Monster* targets[2] = { &monster, &lightBeast };
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
            for (int k = 0; k < 2; k++) {
                if (!targets[k]->active) continue;
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

    /* v3.0：锁定窗口递减（见 hideBlockTimer 的用法） */
    if (hideBlockTimer > 0.0f) hideBlockTimer -= dt;

    /* v3.0：**周期自动存档** ——
       原来 SaveGame() 只在「解锁成就 / 换层 / 安全屋操作 / 退出」时调用，
       中途强退或崩溃等于整局白玩（验证报告 BUG1：保存后重启 benchOwned 变 0）。
       这里在游戏中/安全屋每 20 秒存一次，代价是一次 6KB 的写盘。 */
    /* v3.0：**暂停也要存** —— 游戏失焦会自动暂停（STATE_PAUSED），
           原来条件里没有它，于是切出去等多久都不会存盘。 */
    if ((currentState == STATE_PLAYING || currentState == STATE_PAUSED ||
         currentState == STATE_SAFEHOUSE) && !trainingMode) {
        static float autoSaveTimer = 0.0f;
        autoSaveTimer += dt;
        if (autoSaveTimer >= 20.0f) {
            autoSaveTimer = 0.0f;
            SaveGame();
        }
    }

    /* 冲刺与体力 */

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
    if (BenchSecondWind()) {
        if (stamina <= 0.0f && secondWindTimer <= 0.0f && wantSprint && secondWindTimer > -99.0f) {
            /* v3.0：**体力回满就恢复一次** ——
               原来 static swUsed 从不重置，整局只能用一次
               （验证报告：第二次起 secondWindTimer 恒 0）。 */
            static int swUsed = 0;
            if (stamina > StaminaMax() * 0.5f) swUsed = 0;
            if (!swUsed) { secondWindTimer = 3.0f; swUsed = 1; }
        }
        if (stamina <= 0.0f && secondWindTimer > 0.0f) {
            wantSprint = 1;          /* 宽限期内允许冲刺 */
            secondWindTimer -= dt;
        }
        if (secondWindTimer < -98.0f) secondWindTimer = 0.0f;
    }
    int inF = IsActionDown(ACT_MOVE_FWD) - IsActionDown(ACT_MOVE_BACK);
    int inS = IsActionDown(ACT_MOVE_RIGHT) - IsActionDown(ACT_MOVE_LEFT);
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
        stamina -= STAMINA_DRAIN * RunUpgStaminaMul() * dt;   /* v3.0 体能强化 */
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
        if (keyPressed[VK_SPACE]) {
            if (calibActive) CalibLock();
            else             CalibStart();
            keyPressed[VK_SPACE] = 0;   /* 吃掉这一次，别再触发闪避 */
        }
    }

    /* v2.6：**进安全屋商店的热键改成 T**（用户要求）。
       原来用的是 ACT_INTERACT（默认 E），但 E 同时是「翻箱搜索」，
       站在安全屋里按 E 会让人搞不清是要搜箱子还是开店。
       E 保留给搜索，安全屋专用 T。 */
    /* v3.0：**藏身点里也能开商店** —— 用户要求「同时也可以打开商店面板」 */
    if ((inSafe || hiddenIn >= 0) && currentState == STATE_PLAYING && keyPressed['T']) {
        keyPressed['T'] = 0;
        ShopOpen(currentLevel);
        currentState = STATE_SAFEHOUSE;
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
            sanity += SafeHealRate() * RunSanityGainMul() * dt;
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
            WeaponAddXp(WPN_SPELL, 1);         /* v2.1：法术也能练 */
            slashFade = 0.18f;                 /* 命中留一点余辉 */
            slashHitStop = 0.05f;              /* 50ms 顿帧 */
            /* 在命中点炸一圈碎片（复用玻璃碎片池） */
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
    if (HasKeystone(SK_DECOY) && IsActionPressed(ACT_DECOY)) {
        float px = posX, py = posY;
        for (float d = DECOY_MIN; d <= DECOY_MAX; d += 0.5f) {
            float tx = posX + dirX * d, ty = posY + dirY * d;
            if (Blocked(tx, ty)) break;
            px = tx; py = ty;
        }
        MakeNoise(px, py, DECOY_NOISE);
        decoySelfTimer = DECOY_SELF_TIME;
        PlaySfx(SFX_STEP, 0.7f);
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
            MakeNoise(posX, posY, 12.0f * RunCrankNoiseMul());   /* v2.2：FAST CRANK 的 +15% 噪声 */
        }
        if (crankTimer >= 2.4f * RunCrankTimeMul()) {
            crankTimer = 0.0f;
            battery += 32.0f;
            crankTimeTotal += 3;  /* 2.4 秒算 3 秒，累计用于成就 */
            if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
            PlaySfx(SFX_POWER, 0.45f);
        }
    } else {
        crankTimer = 0.0f;
        crankClick = 0.0f;
    }

    /* 移速：基础值 + 永久升级 - 状态效果的减速（最多 -45%） */
    float slowMul = 1.0f - FxStrength(FX_SLOW) * 0.45f;
    float darkAdd = flashlightOn ? 0.0f : RunDarkSpeedAdd();
    /* CRANK WALK：有这个技能时摇电也允许移动，只是移速打折 */
    float crankMul = cranking ? (1.0f - SkillCrankWalkPenalty()) : 1.0f;
    if (cranking) sprinting = 0;            /* 边摇边冲刺太离谱 */
    float sp = MOVE_SPEED * (1.0f + SkillSpeedAdd() + darkAdd) * slowMul * RelicSpeedMul() * dt
             * (sprinting ? SPRINT_MULT * BenchSprintMul() : 1.0f)   /* v3.0 爆发 */ * crankMul;
    float perpX = -dirY, perpY = dirX;      /* 单位侧向，与 FOV 无关 */
    float dx = 0.0f, dy = 0.0f;
    /* v3.0：**锁盘打开时角色不能移动** ——
       用户实测「解锁的时候角色会乱动，导致这个功能失效」：
       移动条件里根本没检查 lockActive，锁盘开着 WASD 照样走，
       一走开箱子就失去目标，锁盘直接失效。 */
    if (lockActive) {
        dx = 0.0f; dy = 0.0f; sprinting = 0;
    }
    if (!lockActive && !holdingNow && (!cranking || SkillCrankWalkPenalty() > 0.0f)) {          /* 翻找/摇电时不接受移动输入 */
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

    UpdateExplored();

    /* ---------------- 交互：长按 E 搜索容器 / 给配电箱上电 ---------------- */
    if (holdingNow) {
        if (holdTimer == 0.0f && hoverKind == 1) {
            /* 开始翻箱子：发出声音，吸引怪物（压迫感来源） */
            PlaySfx(SFX_SEARCH, 0.55f);  /* 专用翻箱声：金属刮擦+木板闷响 */
            MakeNoise(posX, posY, 10.0f * SkillSearchNoiseMul() * RunSearchNoiseMul2());   /* v2.2：FAST SEARCH 的 +10% */  /* 比走路大一点，比跑步小 */
        }
        holdTimer += dt;
        if (holdTimer >= SEARCH_TIME * RunSearchTimeMul()) {
            holdTimer = 0.0f;
            /* v3.0：翻完箱也上 0.4 秒封锁 ——
               否则 E 还按着，同一帧/下一帧就会被旁边的藏身点吸进去。 */
            hideBlockTimer = 0.4f;
            if (hoverKind == 1) {
                Container* c = &containers[hoverIndex];
                c->searched = 1;
        SpawnParticles(c->x, c->y, 2);   /* v2.1：翻箱扬起灰尘 */
                searchedCount++;
                runSearched++;
        int shardGain = (int)(SHARD_SEARCH * SkillScavengerShardMul()
                              * RelicShardMul() * modShardMul + 0.5f);   /* v2.0：遗物+事件 */
                pendingShards += shardGain;    /* v1.8：先记在待上缴里 */
                totalShardsEarned += shardGain;
                AddAchievementProgress(3, 3);  /* SHARD HUNTER */
                PlaySfx(SFX_PICKUP, 0.55f);
                MakeNoise(posX, posY, 8.0f * SkillSearchNoiseMul());
                if (c->content == 1) { fusesHeld++; fusesCollectedRun++; pickupMsg = "+FUSE"; }
                else if (c->content >= 4) {
                /* v1.16：纯随机产出，酸瓶与烟雾明显稀有 */
                int roll = rand() % 100;
                if (roll < 28) { flares++; pickupMsg = "+FLARE"; }
                else if (roll < 56) {
                    /* v2.7：走 BottleCap()，否则 RUST KNIFE 的 cap=1 代价能被箱子刷开 */
                    if (bottles < BottleCap()) bottles++;
                    pickupMsg = "+BOTTLE";
                }
                else if (roll < 74) { bricks++; pickupMsg = "+BRICK"; }
                else if (roll < 84) { acids++; pickupMsg = "+ACID"; }
                else if (roll < 90) { smokeBombs++; pickupMsg = "+SMOKE"; }
                /* v2.0：三个新投掷物终于有产出了（原来全项目没有任何 ++） */
                else if (roll < 94) { firePots++;  pickupMsg = "+FIREPOT"; }
                else if (roll < 97) { gluePots++;  pickupMsg = "+GLUE"; }
                else                { mirrors++;   pickupMsg = "+MIRROR"; }
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
                /* SCAVENGER: every search also drops a random throwable */
        if (SkillScavenger()) {
            int r2 = rand() % 8;
            if (r2 == 0) { flares++; pickupMsg = "+FLARE"; }
            else if (r2 == 1) { if (bottles < BottleCap()) bottles++; pickupMsg = "+BOTTLE"; }
            else if (r2 == 2) { bricks++; pickupMsg = "+BRICK"; }
            else if (r2 == 3) { acids++; pickupMsg = "+ACID"; }
            else if (r2 == 4) { smokeBombs++; pickupMsg = "+SMOKE"; }
            else if (r2 == 5) { firePots++; pickupMsg = "+FIREPOT"; }
            else if (r2 == 6) { gluePots++; pickupMsg = "+GLUE"; }
            else { mirrors++; pickupMsg = "+MIRROR"; }
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
        SpawnParticles(breaker.x, breaker.y, 0);   /* v2.1：通电火花 */
                    fusesHeld -= fusesNeeded;
                    pendingShards += (int)(SHARD_POWER * RelicShardMul() * modShardMul + 0.5f);   /* v2.0 */
                    totalShardsEarned += SHARD_POWER;
                    AddAchievementProgress(3, 8);  /* SHARD HUNTER */
                    PlaySfx(SFX_POWER, 0.75f);
                }
            }
            hoverKind = 0;
            hoverIndex = -1;
        }
    } else {
        holdTimer = 0.0f;
    }

    /* ---- v2.0 武器攻击 / v3.0 敲击（按住蓄力 0.45 秒）----
       三把武器各烧一条资源：扳手→体力、信号枪→电量、符印→理智。

       v3.1：**严格的「未按下 -> 按下」边沿检测**。
       以前只要这一帧按着就加蓄力，于是用鼠标点主菜单「新游戏」进入游戏时，
       左键还物理按着，下一帧就成了攻击蓄力，松手直接敲一下 ——
       玩家看到的就是「一进去就会敲」。现在：
         - 只有 atkPrev == 0 && atkNow == 1 才允许开始蓄力；
         - 刚进游戏（resetInputState）时把 atkPrev 同步成当前物理状态，
           已经按着的键不算新按下，必须先松开再按才会敲。 */
    {
        static int atkPrev = -1;
        /* 锁盘打开时左键属于 UI，不能进入武器蓄力 */
        int atkNow = (!lockActive) && IsActionDown(ACT_CANCEL_CHARGE);
        if (resetInputState) {
            atkPrev = atkNow;
            weaponAtkCharge = 0.0f;
        }
        if (atkPrev < 0) atkPrev = atkNow;
        int atkPressed = (atkNow && !atkPrev);
        if (weaponCd > 0.0f) {
            weaponCd -= dt;
            if (weaponCd < 0.0f) weaponCd = 0.0f;
        }
        if (atkPressed && !holdingNow && !cranking && !isCharging) {
            /* 未按下 -> 按下：开始蓄力（投掷蓄力中不抢键） */
            weaponAtkCharge = dt / 0.45f;
            if (weaponAtkCharge > 1.0f) weaponAtkCharge = 1.0f;
        } else if (atkNow && weaponAtkCharge > 0.0f &&
                   !holdingNow && !cranking && !isCharging) {
            /* 持续按住：累积蓄力 */
            weaponAtkCharge += dt / 0.45f;
            if (weaponAtkCharge > 1.0f) weaponAtkCharge = 1.0f;
        } else if (atkPrev && !atkNow && weaponAtkCharge > 0.10f) {
            /* 松开：释放攻击 / 敲击 */
            WeaponTryAttack(weaponAtkCharge);
            weaponAtkCharge = 0.0f;
        } else {
            weaponAtkCharge = 0.0f;
        }
        atkPrev = atkNow;
    }

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
    int mouseLeftNow = (!lockActive) && IsActionDown(ACT_THROW);
    int mouseRightNow = (!lockActive) && IsActionDown(ACT_CANCEL_CHARGE);

    if (mouseLeftNow && !mouseLeftPrev && throwCd <= 0.0f && !holdingNow && !cranking) {
        /* 未按下 -> 按下：开始蓄力 */
        isCharging = 1;
        mouseCharge = 0.0f;
    }
    if (isCharging) {
        if (mouseRightNow && !mouseRightPrev) {
            /* 右键取消蓄力 */
            isCharging = 0;
            mouseCharge = 0.0f;
        } else if (mouseLeftNow) {
            /* 持续按住：累积蓄力（仅当选中道具有库存时） */
            int counts[ITEM_SLOT_COUNT] = { flares, bottles, bricks, acids, smokeBombs, firePots, gluePots, mirrors };
            int sel = throwSelected;
            if (sel < 0 || sel >= ITEM_SLOT_COUNT) sel = 0;
            if (counts[sel] > 0) {
                mouseCharge += dt / (0.5f * SkillHeavyHandChargeMul());      /* 0.5 秒满蓄力 */
                if (mouseCharge > 1.0f) mouseCharge = 1.0f;
            }
        } else {
            /* 松开左键：发射（v1.16：0..4 = 荧光棒/瓶/砖/酸/烟） */
            isCharging = 0;
            int counts[ITEM_SLOT_COUNT] = { flares, bottles, bricks, acids, smokeBombs, firePots, gluePots, mirrors };
            int sel = throwSelected;
            if (sel < 0 || sel >= ITEM_SLOT_COUNT) sel = 0;
            if (mouseCharge >= 0.1f && counts[sel] > 0) {
                /* v2.7：先投，真生成了才扣。原来先扣再投，
                   池子满时 ThrowItem 静默不生成，东西凭空消失。 */
                ThrowItem(sel, mouseCharge);
                if (g_throwOk) {
                    if (sel == 0) flares--;
                    else if (sel == 1) bottles--;
                    else if (sel == 2) bricks--;
                    else if (sel == 3) acids--;
                    else if (sel == 4) smokeBombs--;
                    else if (sel == 5) firePots--;
                    else if (sel == 6) gluePots--;
                    else mirrors--;
                    throwCd = 0.45f;
                    if (trainingMode) tutorialThrown = 1;
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
                    MakeNoise(posX, posY, WALK_NOISE_RADIUS * noiseMul
                              * BenchWalkNoiseMul() * QuietSoleMul());   /* 轻步 + 静底 */
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
        flares = bottles = bricks = acids = smokeBombs = 99;
        firePots = gluePots = mirrors = 99;
        sanity = 100.0f;
        /* 教程里手电电量会正常消耗，这样「按住 R 摇电」才有可见效果；
           只做上限夹取，不再每帧强行充满。 */
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        stamina = StaminaMax();
        monster.active = 1;
        monster.state = 2;              /* 原地不动，当标本 */
        monster.hp = 9999;
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
    }

    EnsureThrowSelectedValid();   /* v1.14：手里不能是空的 */
    if (lastHitTimer > 0.0f) lastHitTimer -= dt;   /* v1.15：命中数字淡出 */
    UpdateMonster(dt);
    UpdateLightBeast(dt);
    UpdateParasite(dt);
    UpdateLeech(dt);

    /* 荧光棒/碎片物理更新：飞行中受重力，落地转静止 */
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
            int justThrown = (thrown[i].life > 24.85f);
            int blockedX = justThrown ? 0 : ShotBlocked(nx, thrown[i].y);
            int blockedY = justThrown ? 0 : ShotBlocked(thrown[i].x, ny);
            
            if (blockedX && blockedY) {
                /* 两个方向都被挡，完全停止 */
                thrown[i].vx = 0.0f;
                thrown[i].vy = 0.0f;
                thrown[i].vz = 0.0f;
                thrown[i].z = 0.0f;
            } else {
                if (!blockedX) thrown[i].x = nx;
                else thrown[i].vx = 0.0f;
                if (!blockedY) thrown[i].y = ny;
                else thrown[i].vy = 0.0f;
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
            
            int blockedX = ShotBlocked(nx, thrown[i].y);
            int blockedY = ShotBlocked(thrown[i].x, ny);
            
            if (!blockedX) thrown[i].x = nx;
            else thrown[i].vx = 0.0f;
            if (!blockedY) thrown[i].y = ny;
            else thrown[i].vy = 0.0f;
            
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
    if (!trainingMode && breakerOpen && Dist2(posX, posY, exitX, exitY) < 0.30f) {
        /* NOCTURNAL：本层全程手电关着过关 */
        if (!flashlightOn) flashlightOffLevel = 1;

        PlaySfx(SFX_GATE, 0.65f);
        int evThisLevel = levelEvent;   /* v1.8：GenerateLevel 会重置 levelEvent，先存 */
        pendingShards += (int)(SHARD_EXIT * RelicShardMul() * modShardMul + 0.5f);   /* v2.0 */
        totalShardsEarned += SHARD_EXIT;
        AddAchievementProgress(3, 15);  /* SHARD HUNTER */
        if (currentLevel > bestLevel) bestLevel = currentLevel;
        if (currentLevel == 1) UnlockAchievement(0);  /* FIRST STEPS */
        /* 战役模式：每段连续 3 层；段内换层不重置资源，段间才切规则/重开资源。 */
        if (campaignMode) {
            int stageFloor = (currentLevel - 1) % CAMPAIGN_LEVELS_PER_STAGE;
            if (stageFloor + 1 < CAMPAIGN_LEVELS_PER_STAGE) {
                currentLevel++;
                energy += ENERGY_PER_FLOOR;                      /* v2.3：每层唯一补给 */
        /* v2.4：过关奖励立刻到账，给玩家一个明确的反馈 */
        pickupMsg = (g_lang == LANG_ZH) ? "过关奖励  +能量 +理智" : "LEVEL CLEAR  +ENERGY +SANITY";
        pickupMsgTimer = 2.2f;
                if (energy > ENERGY_MAX) energy = ENERGY_MAX;
                GenerateLevel();
                RunSkillsReset();
                ClearFx();
                for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
                flashTime = 0.15f;
                UpdateExplored();
                return;
            }
            cores += 1;   /* 每完成一段给 1 核心 */
            if (campaignStage < CAMPAIGN_STAGE_COUNT - 1) {
                StartCampaign(campaignStage + 1);
                return;
            }
            gameWon = 1;
            runInProgress = 0;
            /* v2.2：战役通关原来**没有 ComputeScore**，结算页显示上一局残留，
               也不给核心。现在补上。 */
            {
                ScoreInput si = { currentLevel, 1, 0, runSearched, bottlesKilled,
                                  runTime, modsSeenCount };
                ComputeScore(&si, &lastScore);
                lastScore.score += 5000;      /* 战役通关固定加成 */
            }
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
            cores += 1;
        }
        if (currentLevel >= WIN_LEVEL && !isEndlessRun) {
            /* 主线通关：走进第 10 层的出口就是结局，不再生成下一层 */
            gameWon = 1;
            runInProgress = 0;
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
                cores += 1;
            }
            UnlockAchievement(2);  /* ESCAPE ARTIST */
            if (!tookDamage) UnlockAchievement(14);  /* PERFECT ESCAPE */
            /* v2.8：属性面板删了，成就改成「技能树普通节点全满」 */
            if (SkillsAllMaxed()) {
                UnlockAchievement(15);  /* TRUE SURVIVOR */
            }
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
            {
                int gain = pendingShards;
                if (bankStreak >= BANK_STREAK_AFTER) gain += (int)(gain * BANK_STREAK_BONUS);
                shards += gain;
                pendingShards = 0;
            }
            /* 记分 + 里程碑核心 */
            {
                ScoreInput si = { currentLevel, 0, 1, runSearched, bottlesKilled, runTime, modsSeenCount };
                ComputeScore(&si, &lastScore);
                lastScore.score += eventBonus;   /* v1.8：事件加分 */
                if (currentLevel > bestEndlessLevel) bestEndlessLevel = currentLevel;
                if (lastScore.score > bestEndlessScore) bestEndlessScore = lastScore.score;
            }
            if (currentLevel == 20 || currentLevel == 40 ||
                currentLevel == 60 || currentLevel == 80) cores += 1;
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
            shards += pendingShards;
            pendingShards = 0;
            energy += ENERGY_PER_FLOOR;                        /* v3.0：主线每层补给 */
            if (energy > (float)EnergyMax()) energy = (float)EnergyMax();
            currentLevel++;
            GenerateLevel();
        }
        /* v2.2：每层结算 —— 给"无伤/和平/速通/摸黑/安静"五个成就攒计数 */
        {
            float floorTime = runTime - floorStartTime;
            if (floorTime < 90.0f)  floorsFast++;
            if (floorKills == 0)    floorsPacifist++;
            if (floorTookDamage == 0) floorsFlawless++;
            if (!flashlightOn)      floorsNoLight++;
        }
        floorKills = 0;
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
        mapsFullyExplored = 0;
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
    curWeapon = dailySeed % WPN_COUNT;
    DeleteSave();
    pendingStartMode = START_DAILY;
    StartGame();
    /* 强制机制：每日挑战理智流失 +25%，碎片 +25%。
       v2.0 修复：原来直接乘在当前层的乘子上，下一层 GenerateLevel 就重置了，
       等于只有第 1 层生效。改成写进 dailyMode 的固定加成，在 GenerateLevel 里叠加。 */
}

/* v2.0：战役第 stage 关。固定种子保证每次进同一关看到的是同一张图。 */
void StartCampaign(int stage) {
    if (stage < 0) stage = 0;
    if (stage >= CAMPAIGN_STAGE_COUNT) stage = CAMPAIGN_STAGE_COUNT - 1;
    srand((unsigned)(9000 + stage * 733));
    curWeapon = stage % WPN_COUNT;
    DeleteSave();
    pendingStartMode = START_CAMPAIGN;
    pendingCampaignStage = stage;
    StartGame();
}

/* v2.0 SHORTCUT：跳过当前层，直接生成下一层。
   不给碎片、不给遗物 —— 换的是时间，不是资源。 */
void SkipOneLevel(void) {
    if (shortcutUsed) return;
    if (bossLevel) return;                     /* Boss 层不能跳 */
    shortcutUsed = 1;
    currentLevel++;
    GenerateLevel();
    RunSkillsReset();   /* 保持一致性：换层重置局内技能选择 */
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
    if (calibCooldown > 0.0f) {
        char* m = (char*)(g_lang == LANG_ZH ? "校准冷却中" : "CALIBRATION COOLING");
        pickupMsg = m;
        pickupMsgTimer = 1.2f;
        return;
    }
    calibActive = 1;
    calibPos = 0.0f;
    calibDir = 1.0f;
    /* 目标区随机，但不贴着两端（否则没得玩） */
    calibZoneC = 0.22f + (float)(rand() % 57) / 100.0f;   /* 0.22 ~ 0.78 */
    calibResult = 0;
    uiDirty = 1;
}

void CalibLock(void) {
    if (!calibActive) return;
    calibActive = 0;
    calibCooldown = CALIB_COOLDOWN;   /* v2.6：只上冷却 */
    calibUsedLevel = currentLevel;    /* v3.0：记下本层用过了 */

    float d = calibPos - calibZoneC;
    if (d < 0.0f) d = -d;
    float zs = CalibZoneScale();          /* v2.6：目标区随层数缩小 */
    float perfW = CALIB_PERFECT_W * zs;
    float zoneW = CALIB_ZONE_W * zs;

    if (d <= perfW) {
        /* v2.9：黄区（完美）= 能量上限的 70% */
        float gain = (float)EnergyMax() * CALIB_PERFECT_ENERGY_PCT;
        calibResult = 1;
        energy += gain;
        sanity += CALIB_PERFECT_SANITY;
        if (energy > (float)EnergyMax()) energy = (float)EnergyMax();
        if (sanity > SanityMax()) sanity = SanityMax();
        pickupMsg = g_lang == LANG_ZH ? "完美校准  +22 理智" : "PERFECT  +22 SANITY";
        PlaySfx(SFX_POWER, 1.0f);
    } else if (d <= zoneW) {
        /* v2.9：绿区（命中）= 能量上限的 40% */
        float gain = (float)EnergyMax() * CALIB_GOOD_ENERGY_PCT;
        calibResult = 2;
        energy += gain;
        sanity += CALIB_GOOD_SANITY;
        if (energy > (float)EnergyMax()) energy = (float)EnergyMax();
        if (sanity > SanityMax()) sanity = SanityMax();
        pickupMsg = g_lang == LANG_ZH ? "校准成功  +11 理智" : "CALIBRATED  +11 SANITY";
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
    if (currentState == STATE_SAFEHOUSE && calibLevel != currentLevel) {
        calibLevel = currentLevel;
        calibActive = 0;
        uiDirty = 1;
    }

    if (!calibActive) return;
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
}

static void ApplyCampaignRules(int stage) {
    /* 在 GenerateLevel() 完成本层基础词条后叠加，避免被随机倍率重置。 */
    switch (stage) {
    case 0: modSanityMul *= 0.55f; break;                       /* 热身：理智流失减半 */
    case 1: modSpeedMul *= 1.18f; break;                        /* 猎杀：怪更快 */
    case 2: modShardMul *= 0.5f; modBatteryMul *= 0.7f; break;   /* 干旱：没碎片，省电 */
    case 3: modBatteryMul *= 2.0f; modSanityMul *= 1.15f; break; /* 洪水：耗电翻倍 */
    default: modSpeedMul *= 1.30f; modSanityMul *= 1.25f; break;/* 终章：全上 */
    }
}

void StartGame(void) {
    /* v3.0：**改造树 / 强化都不重置** ——
       它们花的是 shards（跨局持久的元货币），重置等于把玩家买的东西吞掉：
       买一个 620 的星节点，下一局归零，碎片永久损失。
       benchOwned / runUpg 都已进存档（benchOwnedSave / runUpgSave），
       本来就是元进度。每局该清的只有「假死」这类局内一次性状态。 */
    playDeadUsed = 0;   /* v3.0 假死每局重置 */
    /* v3.0：这几个也必须在开局清零，否则上一局用过之后新局直接失效 */
    calibUsedLevel = -1;
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
    currentLevel = campaignMode ? campaignStage * CAMPAIGN_LEVELS_PER_STAGE + 1 : 1;
    gameWon = 0;
    flares = 1;
    bottles = 1;
    /* v2.9：三种新投掷物以前没在开局清零 —— 训练场（99 个）或上一局的
       砖/酸/烟会直接带进新一局。 */
    bricks = 0;
    acids = 0;
    smokeBombs = 0;
    firePots = 0; gluePots = 0; mirrors = 0;
    throwSelected = 0;
    runInProgress = 1;
    runTime = 0.0f;
    searchedCount = 0;
    sanity = 100.0f;
    battery = (float)BatteryMax();
    flashlightOn = 1;
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
    pendingShards = 0;
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
    leechHintTimer = 0.0f;
    ResetLeeches();
    RunSkillsReset();

    /* 重置成就追踪变量 */
    totalShardsEarned = 0;
    bottlesKilled = 0;
    faceDodged = 0;
    crankTimeTotal = 0;
    fusesCollectedRun = 0;
    mapsFullyExplored = 0;
    tookDamage = 0;
    flashlightOffLevel = 0;
    sprintedThisLevel = 0;
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

void InitAchievements(void) {
    /* 总是初始化静态字段（name/desc/target/id），存档只保存动态字段(unlocked/progress) */
    const char* names[ACH_COUNT] = {
        "FIRST STEPS", "SURVIVOR", "ESCAPE ARTIST", "SHARD HUNTER",
        "SILENT SHADOW", "BOTTLE MASTER", "FACE DODGER", "NOCTURNAL",
        "SPEED RUNNER", "DEEP DIVER", "FUSE FINDER", "GENERATOR GURU",
        "UPGRADE JUNKIE", "MAP MASTER", "PERFECT ESCAPE", "TRUE SURVIVOR",
        /* v2.0 新增 24 条：进程 / 技巧 / 收集 / 荒谬 / 隐藏 各 8 段位 */
        "DEEP DELVER",   "ABYSS WALKER", "CLOSE CALL",  "ALL FOUR ZONES",
        "FLAWLESS FLOOR","NO LIGHT RUN", "UNSEEN FLOOR","SPEED DEMON",
        "RELIC HOARDER", "FULL LOADOUT", "BESTIARY DONE", "SHARD TYCOON",
        "CRANK ADDICT",  "HANDS EMPTY",   "DECOY RAIN",  "NO RELICS",
        "LUCKY BREAK",   "OFF BY ONE",   "SILENT FLOOR", "BLOOD PRICE",
        "GHOST RUN",     "LAST BREATH",  "CENTURION",    "TRUE ENDING"
    };
    const char* descs[ACH_COUNT] = {
        "Complete level 1",
        "Reach level 5",
        "Beat the game (reach level 10 exit)",
        "Collect 500 shards total",
        "Reach level 5 without sprinting",
        "Lure a hunter with a thrown bottle 10 times",
        "Hide 5 times while a hunter is chasing you",
        "Complete a level with flashlight off",
        "Complete level 1 in under 2 minutes",
        "Reach level 10",
        "Collect 5 fuses in a single run",
        "Crank generator for 60 seconds total",
        "Max out all 6 upgrades",
        "Fully explore 3 maps in one run",
        "Beat the game without taking damage",
        "Beat the game on level 10 with all upgrades maxed",
        "Reach level 12", "Reach level 18", "Stand within 3 tiles of a hunting enemy and survive", "Clear all four zones in one run",
        "Clear a floor without taking damage", "Clear 3 floors with the light off",
        "Clear a floor without ever being spotted", "Clear a floor in under 90 seconds",
        "Hold 8 relics at once", "Hold three different throwables at once", "Unlock every bestiary entry",
        "Collect 3000 shards total", "Crank for 300 seconds total", "Escape a grab 25 times (play dead / vanish)",
        "Lure a hunter with thrown objects 25 times", "Reach level 12 without taking a relic",
        "Win a run with 1 sanity left", "Reach level 24", "Clear a floor with under 20 noise",
        "Win while below 25% sanity", "Win without the flashlight", "Survive at 1 sanity for 30s",
        "Hide in cover 100 times total", "Beat level 24 without a relic"
    };
    int targets[ACH_COUNT] = {
        1, 5, 10, 500, 5, 10, 5, 1, 120, 10, 5, 60, 18, 3, 1, 1
        , 12, 18, 1, 4, 1, 3, 1, 90, 8, 2, 6, 3000, 300, 25, 25, 12, 1, 24, 1, 1, 1, 30, 100, 1

    };
    for (int i = 0; i < ACH_COUNT; i++) {
        g_achievements[i].id = i + 1;
        strncpy(g_achievements[i].name, names[i], ACH_NAME_MAX - 1);
        g_achievements[i].name[ACH_NAME_MAX - 1] = '\0';
        strncpy(g_achievements[i].desc, descs[i], ACH_DESC_MAX - 1);
        g_achievements[i].desc[ACH_DESC_MAX - 1] = '\0';
        g_achievements[i].target = targets[i];
        /* unlocked 和 progress 由 LoadSettings 恢复，这里不覆盖 */
    }
}

/* 成就辅助：解锁并保存 */
static void UnlockAchievement(int idx) {
    if (idx < 0 || idx >= ACH_COUNT) return;
    if (g_achievements[idx].unlocked) return;
    g_achievements[idx].unlocked = 1;
    g_achievements[idx].progress = g_achievements[idx].target;
    /* v2.0 修复：原来每个成就白送 1 核心。40 个成就 = 40 核心，
       而满技能树只需要 9 个 —— 核心货币直接失去意义。
       现在成就只给碎片；核心只由「Boss 击杀 / 通关 / 里程碑楼层」产出。 */
    shards += 400;
    totalShardsEarned += 400;
    /* v2.2：解锁要有反馈 —— 原来只是静默加碎片 */
    pickupMsg = g_achievements[idx].name;
    pickupMsgTimer = 2.2f;
    PlaySfx(SFX_POWER, 0.9f);
    uiDirty = 1;
    AddAchievementProgress(3, 0);   /* 不递归，仅占位说明奖励已发 */
    SaveGame();  /* 立即持久化 */
}

static void AddAchievementProgress(int idx, int amount) {
    if (idx < 0 || idx >= ACH_COUNT) return;
    if (g_achievements[idx].unlocked) return;
    g_achievements[idx].progress += amount;
    if (g_achievements[idx].progress >= g_achievements[idx].target) {
        UnlockAchievement(idx);
    }
}

/* 每帧/事件时检查的成就 */
static void CheckAchievements(void) {
    /* 1: FIRST STEPS - 完成第 1 层（在过关时检查） */
    /* 2: SURVIVOR - 到达第 5 层 */
    if (currentLevel >= 5) UnlockAchievement(1);
    /* 3: ESCAPE ARTIST - 通关（在 gameWon 时检查） */
    /* 4: SHARD HUNTER - 累计 500 碎片（在获得碎片时累加，需要总计数） */
    if (totalShardsEarned >= 500) UnlockAchievement(3);
    /* 5: SILENT SHADOW - 第 5 层不冲刺 */
    if (currentLevel >= 5 && sprintedThisLevel == 0) UnlockAchievement(4);
    /* 6: BOTTLE MASTER - 用瓶子击杀 10 只怪 */
    if (bottlesKilled >= 10) UnlockAchievement(5);
    /* 7: FACE DODGER - 躲开 5 次跳脸虫 */
    if (faceDodged >= 5) UnlockAchievement(6);
    /* 8: NOCTURNAL - 手电关着过关 */
    if (flashlightOffLevel) UnlockAchievement(7);

    /* ---- v2.0：新加的 24 个成就接入触发点（原来只有 0-15 有解锁调用） ---- */
    if (currentLevel >= 12) UnlockAchievement(16);          /* DEEP DELVER */
    if (currentLevel >= 18) UnlockAchievement(17);          /* ABYSS WALKER */
    if (bossDefeated)       UnlockAchievement(18);          /* BOSS BREAKER */
    if (currentLevel >= 24) UnlockAchievement(19);          /* ALL FOUR ZONES */
    if (relicTotal >= 8)    UnlockAchievement(24);          /* RELIC HOARDER */
    if (WeaponAttachAt(curWeapon, 0) >= 0 && WeaponAttachAt(curWeapon, 1) >= 0)
        UnlockAchievement(25);                              /* FULL LOADOUT */
    if (totalShardsEarned >= 3000) UnlockAchievement(27);   /* SHARD TYCOON */
    if (BestiarySeenAll())  UnlockAchievement(26);          /* BESTIARY DONE */
    if (totalKills >= 100)  UnlockAchievement(38);          /* CENTURION */
    /* ---- v2.2：把长期解不开的成就接上（原来 15 个没有任何触发点） ---- */
    if (currentLevel >= 24) UnlockAchievement(33);              /* OFF BY ONE（与 19 同条件） */
    if (currentLevel >= 12 && relicTotal == 0) UnlockAchievement(31);   /* NO RELICS */
    if (gameWon && relicTotal == 0) UnlockAchievement(39);      /* TRUE ENDING */
    if (gameWon && sanity <= 25.0f) UnlockAchievement(35);      /* BLOOD PRICE */
    if (gameWon && !flashlightOn)   UnlockAchievement(36);      /* GHOST RUN */
    /* v3.0：**改成逃生向条件** ——
       原来 29/30 靠 killsWrench / killsBottle，而怪已经打不死，
       这两个计数器永远是 0，成就永远不可能解锁。
       改成用真正会累加的：搜索次数 / 投掷诱饵次数。 */
    if (runSearched >= 60) UnlockAchievement(29);               /* HANDS EMPTY */
    /* bottlesKilled 也只在「打死怪」时 ++，同样永远为 0。
       改用真正会累加的搜索次数（120 次 = 大约两局全翻）。 */
    if (runSearched >= 120) UnlockAchievement(30);              /* DECOY RAIN */
    if (crankSecondsTotal >= 300.0f) UnlockAchievement(28);     /* CRANK ADDICT */
    if (floorsNoLight >= 3) UnlockAchievement(21);              /* NO LIGHT RUN */
    if (floorsFlawless >= 1) UnlockAchievement(20);             /* FLAWLESS FLOOR */
    if (floorsPacifist >= 1) UnlockAchievement(22);             /* PACIFIST FLOOR */
    if (floorsFast >= 1) UnlockAchievement(23);                 /* SPEED DEMON */
    if (floorsQuiet >= 1) UnlockAchievement(34);                /* SILENT FLOOR */
    if (lowSanitySeconds >= 30.0f) UnlockAchievement(37);       /* LAST BREATH */
    /* 9: SPEED RUNNER - 第 1 关 2 分钟内过关 */
    if (currentLevel == 1 && runTime < 120.0f && breakerOpen) UnlockAchievement(8);
    /* 10: DEEP DIVER - 到达第 10 层 */
    if (currentLevel >= 10) UnlockAchievement(9);
    /* 11: FUSE FINDER - 单局集 5 保险丝（需计数） */
    if (fusesCollectedRun >= 5) UnlockAchievement(10);
    /* 12: GENERATOR GURU - 发电累计 60 秒（需累计） */
    if (crankTimeTotal >= 60) UnlockAchievement(11);
    /* 13: UPGRADE JUNKIE - 6 项升级全满级 */
    if (SkillsAllMaxed()) {      /* v2.8：属性面板删了，改成技能树全满 */
        UnlockAchievement(12);
    }
    /* 14: MAP MASTER - 单局全探索 3 张图 */
    if (mapsFullyExplored >= 3) UnlockAchievement(13);
    /* 15: PERFECT ESCAPE - 通关无伤（需标记） */
    /* 16: TRUE SURVIVOR - 通关且全满级（在 gameWon 时检查） */
}

void PauseGame(void) {
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
    if (currentState == STATE_MAIN_MENU) {
        int base = hasSave ? 1 : 0;
        int endlessUnlocked = (bestLevel >= 10) || gameWon;
        int sel = menuSelection;

        if (sel == 0 && hasSave) {
            ContinueRun();
        } else if (sel == base) {
            /* v2.7：普通 New Run 必须清掉两个模式标记。否则"先玩过一次
               Endless / Training"之后，之后每一局都会继承那个模式
               （无尽模式永不通关、训练场只有一条走廊），
               而且 isEndlessRun 还会被写进存档。 */
            isEndlessRun = 0;
            trainingMode = 0;
            dailyMode = 0;
            campaignMode = 0;
            DeleteSave();
            /* v3.0：**武器取消 → 跳过选武器页，直接开局** */
            weaponPickSel = 0;
            StartGame();
        } else if (endlessUnlocked && sel == base + 1) {
            /* Endless 模式 */
            DeleteSave();
            pendingStartMode = START_ENDLESS;
            StartGame();
        } else if ((endlessUnlocked && sel == base + 2) || (!endlessUnlocked && sel == base + 1)) {
            /* v2.8：HUB = 技能树，一进来光标就在第一个节点上 */
            currentState = STATE_HUB;
            hubPage = 1;
            hubSelection = SkillNodeIdAt(0, 0);
            menuScrollOffsets[STATE_HUB] = 0;
            uiDirty = 1;
        } else if ((endlessUnlocked && sel == base + 3) || (!endlessUnlocked && sel == base + 2)) {
            currentState = STATE_ACHIEVEMENTS;
            achSelection = 0;
            menuScrollOffsets[STATE_ACHIEVEMENTS] = 0;
            InitAchievements();
            uiDirty = 1;
        } else if ((endlessUnlocked && sel == base + 4) || (!endlessUnlocked && sel == base + 3)) {
            /* v1.8：怪物图鉴 */
            currentState = STATE_BESTIARY;
            bestiarySel = 0;
            menuScrollOffsets[STATE_BESTIARY] = 0;
            uiDirty = 1;
        } else if ((endlessUnlocked && sel == base + 5) || (!endlessUnlocked && sel == base + 4)) {
            prevState = STATE_MAIN_MENU;
            currentState = STATE_HELP;
            uiDirty = 1;
        } else if ((endlessUnlocked && sel == base + 6) || (!endlessUnlocked && sel == base + 5)) {
            prevState = currentState;
            currentState = STATE_SETTINGS;
            settingsPage = 0;
            settingsSelection = 0;
            settingsPopupOpen = 0;
            rebindingAct = -1;
            rebindConflict = -1;
            uiDirty = 1;
        } else if (sel == MainMenuCount() - 4) {
            /* v2.2：战役模式（原来只有藏在 C 键里的入口，按错还会删档） */
            SaveGame();
            StartCampaign(0);
        } else if (sel == MainMenuCount() - 3) {
            /* v2.2：每日挑战 */
            SaveGame();
            StartDailyRun();
        } else if (sel == MainMenuCount() - 2) {
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
            SaveGame();
            hasSave = 1;
            currentState = STATE_MAIN_MENU;
            menuSelection = 0;
            uiDirty = 1;
        }
    } else if (currentState == STATE_GAMEOVER) {
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
    case IR_SHARDBAG: pendingShards += 40; break;
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
        pendingShards += 30;
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
        Monster* list[2] = { &monster, &lightBeast };
        for (int i = 0; i < 2; i++) {
            if (!list[i]->active) continue;
            list[i]->stun = 8.0f;
            list[i]->state = 0;
            list[i]->pathLen = 0;
        }
        break;
    }
    case IR_STATICFIELD: {
        /* 大招：瘫痪本层所有怪 6 秒 */
        if (monster.active) monster.stun = 6.0f;
        if (lightBeast.active) lightBeast.stun = 6.0f;
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
    pickMode = 0;
    for (int k = 0; k < INRUN_CHOICES; k++) skillPickOptions[k] = -1;
    currentState = prevState;
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
        gammaValue = 1.0f; mouseSens = 1.0f; volumeF = 0.8f;
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
        /* v2 技能树：按节点方块命中，不能按行高算 —— 节点不是行 */
        if (hubPage == 1) {
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
        if (hubPage == 1) {
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
                    for (int t = 0; t < SAFEHOUSE_PAGES; t++) {
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
                        throwSelected = (throwSelected + dir + ITEM_SLOT_COUNT) % ITEM_SLOT_COUNT;
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
                /* 打开锁盘的那次交互键还按着时，Windows 会持续发 WM_KEYDOWN；
                   不能把第一次自动重复当成「任意键退出」，否则锁盘刚开就关。 */
                if ((int)wParam == lockIgnoreVK) return 0;
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
            int menuConfirm = (wParam == (WPARAM)g_bindings[ACT_MENU_CONFIRM]) || wParam == VK_SPACE;
            int menuCancel  = (wParam == (WPARAM)g_bindings[ACT_MENU_CANCEL]);

            if (currentState == STATE_MAIN_MENU) {
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
                if (menuUp) { menuSelection = (menuSelection + HELP_ROWS - 1) % HELP_ROWS; uiDirty = 1; }
                if (menuDown) { menuSelection = (menuSelection + 1) % HELP_ROWS; uiDirty = 1; }
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
                /* v2.4：空格 = 校准（开始 / 锁定） */
                if (wParam == VK_SPACE && !(lParam & 0x40000000)) {
                    if (calibActive) CalibLock();
                    else             CalibStart();
                    return 0;
                }
                if (wParam == VK_TAB) ShopNextPage();
                /* v2.9：切到树页时把选中节点居中（原来 treeVX=0，第一列半截在屏外） */
                if (shopPage == SHP_TREE) BenchFocusNode(shopNodeSel);
                if (wParam == VK_HOME && shopPage == SHP_TREE) BenchResetView();   /* v2.8 */
                if (shopPage == SHP_TREE) {
                    /* v2.8：树页四向移动 + 画布自动跟随 */
                    if (menuUp)    { ShopTreeMove(0, -1); BenchFocusNode(shopNodeSel); }
                    if (menuDown)  { ShopTreeMove(0, 1);  BenchFocusNode(shopNodeSel); }
                    if (menuLeft)  { ShopTreeMove(-1, 0); BenchFocusNode(shopNodeSel); }
                    if (menuRight) { ShopTreeMove(1, 0);  BenchFocusNode(shopNodeSel); }
                } else {
                    if (menuUp)      ShopMoveSel(-1);
                    if (menuDown)    ShopMoveSel(1);
                    /* v2.8：非树页时左右键切页（和顶部按钮一致，不用记 TAB） */
                    if (menuLeft)    ShopPrevPage();
                    if (menuRight)   ShopNextPage();
                }
                if (menuConfirm) ShopActivate();
                if (menuCancel) {
                    currentState = STATE_PLAYING;
                    resetInputState = 1;
                    ForceCursor(0);
                    uiDirty = 1;
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
                /* v2.2 修复：原来这里只处理 ESC —— 方向键和回车一个都不响应，
                   而屏幕底部明写 ENTER BUY / ARROWS MOVE。纯键盘玩家
                   一分碎片都花不出去，整条局外成长线等于锁死。 */
                if (hubPage == 1) {
                    int cur = hubSelection;
                    int col = SkillNodeCol(cur);
                    int row = SkillNodeRow(cur);
                    int moved = 0;
                    if (menuUp    && row > 0) { row--; moved = 1; }
                    if (menuDown  && row < SKILL_GRID_ROWS - 1) { row++; moved = 1; }
                    if (menuLeft  && col > 0) { col--; moved = 1; }
                    if (menuRight && col < SKILL_GRID_COLS - 1) { col++; moved = 1; }
                    if (moved) {
                        int nid = SkillNodeIdAt(col, row);
                        if (nid >= 0) { hubSelection = nid; uiDirty = 1; }
                    }
                    if (menuConfirm) ActivateHubRow();
                }
                if (menuCancel) {
                    SaveGame();
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    ForceCursor(1);
                    uiDirty = 1;
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
                int n = ACH_COUNT + 1;
                if (menuUp) { achSelection = (achSelection + n - 1) % n; uiDirty = 1; }
                if (menuDown) { achSelection = (achSelection + 1) % n; uiDirty = 1; }
                MenuGeo ag = GetMenuGeo();
                if (achSelection < ag.scrollOffset) menuScrollOffsets[STATE_ACHIEVEMENTS] = achSelection;
                else if (achSelection >= ag.scrollOffset + ag.maxVisibleRows) menuScrollOffsets[STATE_ACHIEVEMENTS] = achSelection - ag.maxVisibleRows + 1;
                /* 只有 Back 行（或者 Esc）才退出；成就条目本身按回车不做事 */
                if (menuCancel || (menuConfirm && achSelection >= ACH_COUNT)) {
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    uiDirty = 1;
                }
            } else if (currentState == STATE_PLAYING) {
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

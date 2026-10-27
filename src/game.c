/*
 * Canity — 游戏核心实现：世界 / 关卡 / 规则 / 输入 / 菜单状态机 / 窗口过程
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "game.h"
#include "entity.h"
#include "render.h"
#include "ui.h"
#include "audio.h"
#include "save.h"

/* 文件内前置声明：这两个 static 辅助函数在被调用处之后定义
   （原来集中在 main.c 中部的前置声明块，拆分后只保留模块内部需要的）。 */
static void ThrowItem(int type, float charge);
static void SpawnShards(float x, float y, float vx, float vy, float charge);
static void InitAchievements(void);
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
const int resolutions[5][2] = {{800, 600}, {1024, 768}, {1280, 720}, {1600, 900}, {1920, 1080}};
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
int flares = 0, bottles = 0;
int throwSelected = 0;        /* 0=荧光棒, 1=玻璃瓶, 2=预留 */
Thrown thrown[MAX_THROWN];
Shot shot[MAX_SHOTS];
Shard shards_debris[MAX_SHOTS * SHARD_COUNT];
SafeRoom safeRoom;
Container containers[MAX_CONTAINERS];
int containerCount = 0;
Breaker breaker;
int fusesHeld = 0;        /* 手上已有保险丝 */
float runTime = 0.0f;     /* 本局存活时间 */
float stepTimer = 0.0f;
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
int menuScrollOffsets[8] = {0};  /* 下标 = GameState，包含 STATE_ACHIEVEMENTS=7 */

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
    "MENU CONFIRM", "MENU CANCEL", "DODGE (QTE)"
};
/* 编译期守卫：表项数量必须与动作数一致，漏一个改绑框就会画到 NULL */
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
    return NULL;
}

static const char* optRes[5] = {"800 x 600", "1024 x 768", "1280 x 720", "1600 x 900", "1920 x 1080"};
static const char* optFps[5] = {"30", "60", "120", "144", "Unlimited"};
static const char* optScale[4] = {"100%", "75%", "50%", "25%"};
static const char* optFov[5] = {"66", "75", "85", "95", "105"};
static const char* optOnOff[2] = {"OFF", "ON"};
/* HUD 详细度：游戏画面上放多少字。
   Minimal = 只留必须实时知道的数值；Full = 全部显示。 */
static const char* optHud[3] = {"Minimal", "Normal", "Full"};
/* QTE 提示详细度：Full = 带英文说明，Minimal = 只留大按键和倒计时条 */
static const char* optQte[2] = {"Minimal", "Full"};
SettingItem settings[] = {
    /* ========== 分类 1: DISPLAY ========== */
    {"Resolution",      SET_LIST,   optRes,      5, &resIndex,          NULL,      0,    0,    0, 1},
    {"Fullscreen",      SET_LIST,   optOnOff,    2, &isFullscreen,      NULL,      0,    0,    0, 1},
    {"Frame Limit",     SET_LIST,   optFps,      5, &fpsCapIndex,       NULL,      0,    0,    0, 1},
    {"Render Scale",    SET_LIST,   optScale,    4, &renderScaleIndex,  NULL,      0,    0,    0, 1},
    {"Gamma",           SET_SLIDER, NULL,        0, NULL,               &gammaValue, 0.7f, 1.6f, 0.05f, 1},
    {"Field of View",   SET_LIST,   optFov,      5, &fovIndex,          NULL,      0,    0,    0, 1},

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
    {"Cancel Charge",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_CANCEL_CHARGE], NULL, 0, 0, 0, 4},
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
    modSanityMul = 1.0f;
    modSpeedMul = 1.0f;
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

void GenerateLevel(void) {
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
    exitX = 3.5f; exitY = 1.5f;  /* 出生点 (1.5,1.5) 正右方 2 格 */

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

    exitX = 3.5f; exitY = 1.5f;  /* 出口门固定在出生点右侧 2 格 */
    breakerOpen = 0;  /* 出口门锁死，需通电开启 */

    /* 容器 / 保险丝 / 配电箱 */
    for (int i = 0; i < MAX_CONTAINERS; i++) containers[i].active = 0;
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
    levelBanner = 5.0f;

    /* DARK：手电起始电量只有 55%。必须放在掷词条之后，
       否则用的是上一层的 levelMod，词条会延迟一层生效。 */
    if (levelMod == 0) battery = (float)BatteryMax() * 0.55f;

    /* 本关需要的保险丝：2 根起，每 3 关 +1，最多 4（主线）；无尽模式不封顶 */
    if (isEndlessRun) {
        fusesNeeded = 2 + currentLevel / 3;  /* 11层=5, 14层=6, 17层=7... */
    } else {
        fusesNeeded = 2 + currentLevel / 3;
        if (fusesNeeded > 4) fusesNeeded = 4;
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

    int attempts = 0;
    /* 阶段1：保证 fusesNeeded 个箱子 */
    while (containerCount < fusesNeeded && attempts < 512) {
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

    /* 阶段2：继续填充到 contCap（受词条影响） */
    attempts = 0;
    while (containerCount < contCap && attempts < 512) {
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
        int nt = rand() % 4;
        if (nt == themeId) nt = (nt + 1) % 4;
        GenerateThemeTextures(nt);
    }
    SpawnMonster();
    SpawnLightBeast();
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

    /* 安全屋：离出生点和出口都远一点 */
    safeRoom.active = 0;
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
    /* 强制兜底：如果随机找不到，遍历全图强行放一个 */
    if (!safeRoom.active) {
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
    if (!safeRoom.active || !breaker.active) {
        for (int y = 1; y < MAP_HEIGHT - 1 && (!safeRoom.active || !breaker.active); y++) {
            for (int x = 1; x < MAP_WIDTH - 1 && (!safeRoom.active || !breaker.active); x++) {
                if (worldMap[y][x]) continue;
                float cx = x + 0.5f, cy = y + 0.5f;
                if (!safeRoom.active && Dist2(cx, cy, 1.5f, 1.5f) >= 25.0f && Dist2(cx, cy, exitX, exitY) >= 9.0f) {
                    safeRoom.x = cx; safeRoom.y = cy; safeRoom.active = 1;
                }
                if (!breaker.active && Dist2(cx, cy, 1.5f, 1.5f) >= 225.0f) {
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
int BatteryMax(void) { return 100 + upgBattery * 20; }

/* 手电每秒耗电：ECO 升级每级 -12% */
float BatteryDrainRate(void) { return BATTERY_DRAIN * (1.0f - 0.12f * upgEco); }

/* 安全屋回智速度：REST 升级每级 +25% */
float SafeHealRate(void) { return 3.5f * (1.0f + 0.25f * upgRest); }

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
static void ThrowItem(int type, float charge) {
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
            shot[i].active = 1;
            break;
        }
        PlaySfx(SFX_THROW, 0.6f);  /* 投掷专用音效：呼啸+出手闷响 */
        return;
    }

    /* 荧光棒：抛物线飞行，落地后转为静止发光体 */
    float flareSpeed = 7.0f + charge * 5.0f;  /* 7~12 格/秒 */
    float flareAngle = 0.4f + charge * 0.3f;  /* 0.4~0.7 rad ≈ 23~40° */
    for (int i = 0; i < MAX_THROWN; i++) {
        if (thrown[i].active) continue;
        thrown[i].x = posX;
        thrown[i].y = posY;
        thrown[i].z = SHOT_HEIGHT;
        thrown[i].vz = flareSpeed * sinf(flareAngle);
        thrown[i].vx = dirX * flareSpeed * cosf(flareAngle);
        thrown[i].vy = dirY * flareSpeed * cosf(flareAngle);
        thrown[i].type = 0;        /* 0=荧光棒 */
        thrown[i].life = 25.0f;    /* 落地后发光时长 */
        thrown[i].active = 1;
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
                MakeNoise(shot[i].x, shot[i].y, 14.0f);
                shot[i].active = 0;
                break;
            }

            /* 撞墙：碎裂生成碎片 */
            if (shot[i].age > 0.02f && ShotBlocked(shot[i].x, shot[i].y)) {
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
                PlaySfx(SFX_GLASS, 0.7f);
                MakeNoise(shot[i].x, shot[i].y, 14.0f);
                shot[i].active = 0;
                break;
            }

            /* 撞怪 */
            float r = SHOT_RADIUS + 0.24f;
            if (monster.active && Dist2(shot[i].x, shot[i].y, monster.x, monster.y) < r * r) {
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
                HitMonster(&monster, SHOT_DAMAGE);
                shot[i].active = 0;
                break;
            }
            if (lightBeast.active && Dist2(shot[i].x, shot[i].y, lightBeast.x, lightBeast.y) < r * r) {
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
                HitMonster(&lightBeast, SHOT_DAMAGE);
                shot[i].active = 0;
                break;
            }
        }
        if (shot[i].life <= 0.0f) {
            /* 超时落地也生成碎片 */
            if (shot[i].z > 0.0f) {
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
        count--;
    }
}
void Update(float dt) {
    if (flashTime > 0.0f) flashTime -= dt;
    if (healFlash > 0.0f) healFlash -= dt;
    /* 全屏地图：游戏**暂停**（专心规划路线）。
       小窗地图：游戏**继续**（边看边玩，鼠标仍能转视角）。 */
    if (currentState != STATE_PLAYING || (isMapOpen && mapFullScreen)) return;
    if (flashTime > 0.0f) return;

    runTime += dt;
    if (levelBanner > 0.0f) levelBanner -= dt;

    /* 成就检查 */
    CheckAchievements();

    /* 状态效果计时。强度变化时重算 FOV（视野收窄要立刻生效） */
    if (UpdateFx(dt)) ApplyFov();

    /* 理智流失：基础值 + 状态效果的额外流失 */
    float baseDrain = isEndlessRun ? ENDLESS_SANITY_DRAIN : SANITY_DRAIN;
    float drainMul = 1.0f + FxStrength(FX_DRAIN) * 1.5f;
    sanity -= baseDrain * (1.0f - 0.10f * upgSanity) * modSanityMul * drainMul * dt;
    if (sanity <= 0.0f) {
        sanity = 0.0f;
        runInProgress = 0;
        if (currentLevel > bestLevel) bestLevel = currentLevel;
        gameWon = 0;
        currentState = STATE_GAMEOVER;
        menuSelection = 0;
        ForceCursor(1);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        uiDirty = 1;
        return;
    }

    /* 手电筒耗电：电量耗尽自动熄灭（ECO 升级降低耗电） */
    if (flashlightOn) {
        battery -= BatteryDrainRate() * dt;
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


    holdingNow = (hoverKind != 0 && IsActionDown(ACT_INTERACT));

    /* 冲刺与体力 */
    int wantSprint = IsActionDown(ACT_SPRINT);
    int inF = IsActionDown(ACT_MOVE_FWD) - IsActionDown(ACT_MOVE_BACK);
    int inS = IsActionDown(ACT_MOVE_RIGHT) - IsActionDown(ACT_MOVE_LEFT);
    int moving = (inF != 0 || inS != 0);

    sprinting = (wantSprint && moving && stamina > 1.0f);
    if (sprinting) {
        sprintedThisLevel = 1;  /* 标记本局冲刺过 */
        stamina -= STAMINA_DRAIN * dt;
        if (stamina < 0.0f) stamina = 0.0f;
        staminaIdle = 0.5f;
    } else if (staminaIdle > 0.0f) {
        staminaIdle -= dt;
    } else if (stamina < 100.0f) {
        stamina += STAMINA_REGEN * dt;
        if (stamina > 100.0f) stamina = 100.0f;
    }

    /* 带半径的碰撞：不再用「点」判定，避免贴墙时半个身子钻进墙里 */
    /* 安全屋：站在里面回理智 */
    int inSafe = (safeRoom.active && Dist2(posX, posY, safeRoom.x, safeRoom.y) < SAFE_ZONE_RADIUS * SAFE_ZONE_RADIUS);
    if (inSafe) {
        sanity += SafeHealRate() * dt;   /* REST 升级提高回智速度 */
        if (sanity > 100.0f) sanity = 100.0f;
        /* 「SAFE ZONE」提示只闪一下：刚进来时给 2 秒，之后不常驻。
           （玩家在安全屋里本来就知道自己在那，不需要一直盯着这行字。） */
        if (safeZoneMsg <= 0.0f) safeZoneMsg = 2.0f;
    }
    if (safeZoneMsg > 0.0f) {
        safeZoneMsg -= dt;
        if (safeZoneMsg < 0.0f) safeZoneMsg = 0.0f;
    }

    /* 手摇发电：站着摇，摇满一次回一截电；期间不能移动 */
    cranking = 0;
    if (IsActionDown(ACT_CRANK) && battery < 99.9f && !holdingNow) {
        cranking = 1;
        crankTimer += dt;
        crankClick += dt;
        if (crankClick >= 0.30f) {
            crankClick = 0.0f;
            PlaySfx(SFX_CRANK, 0.45f);
            MakeNoise(posX, posY, 12.0f);
        }
        if (crankTimer >= 2.4f) {
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
    float sp = MOVE_SPEED * (1.0f + 0.06f * upgSpeed) * slowMul * dt * (sprinting ? SPRINT_MULT : 1.0f);
    float perpX = -dirY, perpY = dirX;      /* 单位侧向，与 FOV 无关 */
    float dx = 0.0f, dy = 0.0f;
    if (!holdingNow && !cranking) {          /* 翻找/摇电时不接受移动输入 */
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
            MakeNoise(posX, posY, 10.0f);  /* 比走路大一点，比跑步小 */
        }
        holdTimer += dt;
        if (holdTimer >= SEARCH_TIME) {
            holdTimer = 0.0f;
            if (hoverKind == 1) {
                Container* c = &containers[hoverIndex];
                c->searched = 1;
                searchedCount++;
                shards += 3;
                totalShardsEarned += 3;
                AddAchievementProgress(3, 3);  /* SHARD HUNTER */
                PlaySfx(SFX_PICKUP, 0.55f);
                MakeNoise(posX, posY, 8.0f);
                if (c->content == 1) { fusesHeld++; fusesCollectedRun++; pickupMsg = "+FUSE"; }
                else if (c->content == 4) { flares++; pickupMsg = "+FLARE"; }
                else if (c->content == 5) { bottles++; pickupMsg = "+BOTTLE"; }
                else if (c->content == 2) {
                    battery += BATTERY_PICKUP;
                    if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
                    healFlash = 0.06f;
                    pickupMsg = "+BATTERY";
                } else if (c->content == 3) {
                    sanity += 25.0f;
                    if (sanity > 100.0f) sanity = 100.0f;
                    healFlash = 0.15f;
                    pickupMsg = "+MEDKIT";
                }
                if (pickupMsg) pickupMsgTimer = 2.0f;
            } else if (hoverKind == 2) {
                if (fusesHeld >= fusesNeeded) {
                    breakerOpen = 1;
                    fusesHeld -= fusesNeeded;
                    shards += 8;
                    totalShardsEarned += 8;
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

    /* 投掷：数字键选槽位，鼠标左键蓄力投掷 */
    if (throwCd > 0.0f) throwCd -= dt;

    /* 数字键 1/2/3 切换投掷物槽位。
       键盘的情况已经在 WM_KEYDOWN 里即时处理（见 WindowProc）——
       原来在这里用 if/else 轮询 keyPressed，同一帧先按 1 再按 2 只认 1，
       按住不放时系统的自动重复还会一直压住另一个键，表现就是「按了没反应」。
       这里只保留「把槽位改绑到鼠标键」的兜底：鼠标键不产生 WM_KEYDOWN。 */
    if (IsMouseButton(g_bindings[ACT_SELECT_1]) && IsActionPressed(ACT_SELECT_1)) throwSelected = 0;
    else if (IsMouseButton(g_bindings[ACT_SELECT_2]) && IsActionPressed(ACT_SELECT_2)) throwSelected = 1;
    else if (IsMouseButton(g_bindings[ACT_SELECT_3]) && IsActionPressed(ACT_SELECT_3)) throwSelected = 2;

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
    int mouseLeftNow = IsActionDown(ACT_THROW);
    int mouseRightNow = IsActionDown(ACT_CANCEL_CHARGE);

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
            int hasItem = (throwSelected == 1 && bottles > 0) || (throwSelected == 0 && flares > 0);
            if (hasItem) {
                mouseCharge += dt / 0.5f;  /* 0.5 秒满蓄力 */
                if (mouseCharge > 1.0f) mouseCharge = 1.0f;
            }
        } else {
            /* 松开左键：发射 */
            isCharging = 0;
            if (mouseCharge >= 0.1f && throwSelected == 1 && bottles > 0) {
                bottles--;
                ThrowItem(1, mouseCharge);  /* 玻璃瓶，传入蓄力倍率 */
                throwCd = 0.45f;
            } else if (mouseCharge >= 0.1f && throwSelected == 0 && flares > 0) {
                flares--;
                ThrowItem(0, mouseCharge);  /* 荧光棒（平抛） */
                throwCd = 0.45f;
            }
            mouseCharge = 0.0f;
        }
    }
    mouseLeftPrev = mouseLeftNow;
    mouseRightPrev = mouseRightNow;

    /* 脚步：走路是完全静音的，只有冲刺才会响、才会惊动怪 */
    if (moving && sprinting && !holdingNow && !cranking) {
        stepTimer += dt;
        if (stepTimer >= 0.30f) {
            stepTimer = 0.0f;
            PlaySfx(SFX_STEP, 0.50f);
            MakeNoise(posX, posY, 9.0f);
        }
    } else {
        stepTimer = 0.0f;
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
    UpdateMonster(dt);
    UpdateLightBeast(dt);
    UpdateParasite(dt);

    /* 荧光棒/碎片物理更新：飞行中受重力，落地转静止 */
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        if (thrown[i].z > 0.0f) {
            /* 飞行中：重力积分 */
            float nx = thrown[i].x + thrown[i].vx * dt;
            float ny = thrown[i].y + thrown[i].vy * dt;
            thrown[i].z += thrown[i].vz * dt;
            thrown[i].vz -= GRAVITY * dt;

            /* 撞墙预判：分离 X/Y 轴检测，允许滑动 */
            int blockedX = ShotBlocked(nx, thrown[i].y);
            int blockedY = ShotBlocked(thrown[i].x, ny);
            
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
            thrown[i].vz -= GRAVITY * dt;

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

    /* 出口：通电后才能进下一关；第 WIN_LEVEL 层的出口 = 逃出生天 */
    if (breakerOpen && Dist2(posX, posY, exitX, exitY) < 0.30f) {
        /* NOCTURNAL：本层全程手电关着过关 */
        if (!flashlightOn) flashlightOffLevel = 1;

        PlaySfx(SFX_GATE, 0.65f);
        shards += 15;
        totalShardsEarned += 15;
        AddAchievementProgress(3, 15);  /* SHARD HUNTER */
        if (currentLevel > bestLevel) bestLevel = currentLevel;
        if (currentLevel == 1) UnlockAchievement(0);  /* FIRST STEPS */
        if (currentLevel >= WIN_LEVEL && !isEndlessRun) {
            /* 主线通关：走进第 10 层的出口就是结局，不再生成下一层 */
            gameWon = 1;
            runInProgress = 0;
            currentState = STATE_GAMEOVER;
            menuSelection = 0;
            ForceCursor(1);
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            /* 通关时检查成就 */
            UnlockAchievement(2);  /* ESCAPE ARTIST */
            if (!tookDamage) UnlockAchievement(14);  /* PERFECT ESCAPE */
            if (upgBattery >= UPG_MAX && upgSpeed >= UPG_MAX && upgQuiet >= UPG_MAX &&
                upgSanity >= UPG_MAX && upgEco >= UPG_MAX && upgRest >= UPG_MAX) {
                UnlockAchievement(15);  /* TRUE SURVIVOR */
            }
            uiDirty = 1;
            return;
        }
        /* 无尽模式：过关回智 +30，继续下一层；主线模式：正常换关 */
        if (isEndlessRun) {
            sanity += 30.0f;
            if (sanity > 100.0f) sanity = 100.0f;
            /* 记录无尽模式最高层数与分数 */
            int score = currentLevel * 1000 + (int)(shards * 10) - (int)runTime;
            if (currentLevel > bestEndlessLevel) bestEndlessLevel = currentLevel;
            if (score > bestEndlessScore) bestEndlessScore = score;
            currentLevel++;
            GenerateLevel();
        } else {
            currentLevel++;
            GenerateLevel();
        }
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
}
/* ------------------------------------------------------------------ */
/* 菜单动作                                                            */
/* ------------------------------------------------------------------ */

void StartGame(void) {
    currentState = STATE_PLAYING;
    currentLevel = 1;
    gameWon = 0;
    flares = 1;
    bottles = 1;
    runInProgress = 1;
    runTime = 0.0f;
    searchedCount = 0;
    sanity = 100.0f;
    battery = (float)BatteryMax();
    flashlightOn = 1;
    stamina = 100.0f;
    sprinting = 0;
    staminaIdle = 0.0f;
    healFlash = 0.0f;
    isMapOpen = 0;
    isCharging = 0;
    mouseCharge = 0.0f;
    resetInputState = 1;
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
    ForceCursor(0);
    firstMouseMove = 1;
    uiDirty = 1;
}

static void InitAchievements(void) {
    /* 总是初始化静态字段（name/desc/target/id），存档只保存动态字段(unlocked/progress) */
    const char* names[ACH_COUNT] = {
        "FIRST STEPS", "SURVIVOR", "ESCAPE ARTIST", "SHARD HUNTER",
        "SILENT SHADOW", "BOTTLE MASTER", "FACE DODGER", "NOCTURNAL",
        "SPEED RUNNER", "DEEP DIVER", "FUSE FINDER", "GENERATOR GURU",
        "UPGRADE JUNKIE", "MAP MASTER", "PERFECT ESCAPE", "TRUE SURVIVOR"
    };
    const char* descs[ACH_COUNT] = {
        "Complete level 1",
        "Reach level 5",
        "Beat the game (reach level 10 exit)",
        "Collect 500 shards total",
        "Reach level 5 without sprinting",
        "Kill 10 monsters with bottles",
        "Dodge 5 face jumpers successfully",
        "Complete a level with flashlight off",
        "Complete level 1 in under 2 minutes",
        "Reach level 10",
        "Collect 5 fuses in a single run",
        "Crank generator for 60 seconds total",
        "Max out all 6 upgrades",
        "Fully explore 3 maps in one run",
        "Beat the game without taking damage",
        "Beat the game on level 10 with all upgrades maxed"
    };
    int targets[ACH_COUNT] = {
        1, 5, 10, 500, 5, 10, 5, 1, 120, 10, 5, 60, 18, 3, 1, 1
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
    /* 9: SPEED RUNNER - 第 1 关 2 分钟内过关 */
    if (currentLevel == 1 && runTime < 120.0f && breakerOpen) UnlockAchievement(8);
    /* 10: DEEP DIVER - 到达第 10 层 */
    if (currentLevel >= 10) UnlockAchievement(9);
    /* 11: FUSE FINDER - 单局集 5 保险丝（需计数） */
    if (fusesCollectedRun >= 5) UnlockAchievement(10);
    /* 12: GENERATOR GURU - 发电累计 60 秒（需累计） */
    if (crankTimeTotal >= 60) UnlockAchievement(11);
    /* 13: UPGRADE JUNKIE - 6 项升级全满级 */
    if (upgBattery >= UPG_MAX && upgSpeed >= UPG_MAX && upgQuiet >= UPG_MAX &&
        upgSanity >= UPG_MAX && upgEco >= UPG_MAX && upgRest >= UPG_MAX) {
        UnlockAchievement(12);
    }
    /* 14: MAP MASTER - 单局全探索 3 张图 */
    if (mapsFullyExplored >= 3) UnlockAchievement(13);
    /* 15: PERFECT ESCAPE - 通关无伤（需标记） */
    /* 16: TRUE SURVIVOR - 通关且全满级（在 gameWon 时检查） */
}

void PauseGame(void) {
    currentState = STATE_PAUSED;
    menuSelection = 0;
    isMapOpen = 0;
    ForceCursor(1);
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    uiDirty = 1;
}

void ApplyDisplayMode(void) {
    if (isFullscreen) {
        winW = GetSystemMetrics(SM_CXSCREEN);
        winH = GetSystemMetrics(SM_CYSCREEN);
    } else {
        winW = resolutions[resIndex][0];
        winH = resolutions[resIndex][1];
    }
    InitPixels();

    if (!g_hwnd) return;

    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    if (isFullscreen) {
        SetWindowLongPtr(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, 0, 0, winW, winH, SWP_FRAMECHANGED);
    } else {
        /* 保留 WS_VISIBLE：否则切分辨率后窗口会消失 */
        SetWindowLongPtr(g_hwnd, GWL_STYLE, style | WS_VISIBLE);
        RECT rect = {0, 0, winW, winH};
        AdjustWindowRect(&rect, style, FALSE);
        int w = rect.right - rect.left, h = rect.bottom - rect.top;
        int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
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
            DeleteSave();
            StartGame();
        } else if (endlessUnlocked && sel == base + 1) {
            /* Endless 模式 */
            isEndlessRun = 1;
            DeleteSave();
            StartGame();
        } else if ((endlessUnlocked && sel == base + 2) || (!endlessUnlocked && sel == base + 1)) {
            currentState = STATE_HUB;
            hubSelection = 0;
            uiDirty = 1;
        } else if ((endlessUnlocked && sel == base + 3) || (!endlessUnlocked && sel == base + 2)) {
            currentState = STATE_ACHIEVEMENTS;
            achSelection = 0;
            menuScrollOffsets[STATE_ACHIEVEMENTS] = 0;
            InitAchievements();
            uiDirty = 1;
        } else if ((endlessUnlocked && sel == base + 4) || (!endlessUnlocked && sel == base + 3)) {
            prevState = STATE_MAIN_MENU;
            currentState = STATE_HELP;
            uiDirty = 1;
        } else if ((endlessUnlocked && sel == base + 5) || (!endlessUnlocked && sel == base + 4)) {
            prevState = currentState;
            currentState = STATE_SETTINGS;
            settingsPage = 0;
            settingsSelection = 0;
            settingsPopupOpen = 0;
            rebindingAct = -1;
            rebindConflict = -1;
            uiDirty = 1;
        } else {
            SaveGame();
            PostQuitMessage(0);
        }
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

void ActivateHubRow(void) {
    if (hubSelection >= UPGRADE_COUNT) {
        SaveGame();
        currentState = STATE_MAIN_MENU;
        menuSelection = 0;
        ForceCursor(1);
        uiDirty = 1;
        return;
    }
    BuyUpgrade(hubSelection);
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

static void HitTestMenu(int mx, int my) {
    if (currentState == STATE_GAMEOVER) return;

    if (currentState == STATE_HUB) {
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
    if (currentState == STATE_HELP) { currentState = STATE_MAIN_MENU; uiDirty = 1; return; }

    if (currentState == STATE_HUB) {
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

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                memset(keys, 0, sizeof(keys));
                if (currentState == STATE_PLAYING) PauseGame();
            } else {
                /* 窗口重新获得焦点：若是因失焦自动暂停，则自动恢复 */
                if (currentState == STATE_PAUSED && prevState == STATE_PLAYING) {
                    currentState = STATE_PLAYING;
                    resetInputState = 1;   /* 鼠标在失焦期间的按下对游戏不可见，需重同步边沿 */
                    ForceCursor(0);
                    firstMouseMove = 1;
                    uiDirty = 1;
                }
            }
            return 0;

        case WM_KILLFOCUS:
            memset(keys, 0, sizeof(keys));
            return 0;

        case WM_SETFOCUS:
            firstMouseMove = 1;
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                /* 游戏中（含小窗地图）隐藏光标 —— 鼠标是锁定的。
                   只有全屏地图才显示箭头光标。 */
                int mouseFree = (currentState != STATE_PLAYING) || (isMapOpen && mapFullScreen);
                if (!mouseFree) SetCursor(NULL);
                else SetCursor(LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_MOUSEMOVE: {
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);

            if (currentState != STATE_PLAYING) {
                if (currentState == STATE_SETTINGS && rebindingAct >= 0) return 0;
                if (settingsPopupOpen && sliderDragging && popupItem >= 0 &&
                    settings[popupItem].kind == SET_SLIDER) {
                    const SettingItem* it = &settings[popupItem];
                    MenuGeo p = GetPopupGeo();
                    float v = SliderXToValue(p, mx, it->fmin, it->fmax, it->fstep);
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

            POINT center = {winW / 2, winH / 2};
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
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
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
            if (sliderDragging) ReleaseCapture();
            sliderDragging = 0;
            return 0;

        case WM_CAPTURECHANGED:
            sliderDragging = 0;
            return 0;

        case WM_KEYDOWN:
            /* 小键盘数字（NumLock 开启时是 VK_NUMPAD0..9）统一折算成主键盘数字，
               这样 1/2/3 选投掷槽位、以及改绑数字键都不会因为按小键盘而失效。 */
            if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
                wParam = (WPARAM)('0' + (wParam - VK_NUMPAD0));
            /* 改绑模式：吞掉所有按键（Esc 取消），不进菜单导航 */
            if (currentState == STATE_SETTINGS && rebindingAct >= 0) {
                RebindConsumeKey((int)wParam);
                return 0;
            }
            if (wParam < 256) {
                keys[wParam] = 1;
                /* 记下「刚按下」的边沿，供 QTE 消费。
                   只在游戏进行中记 —— 菜单里按的键不该触发 QTE。 */
                if (currentState == STATE_PLAYING) keyPressed[wParam] = 1;
            }

            /* 切投掷槽位：在消息里即时处理，后按的键立即生效。
               bit30 = 系统自动重复；按住不放时不重复覆盖，否则会压住另一个键。
               （绑到鼠标键的情况在 Update 里轮询兜底。） */
            if (currentState == STATE_PLAYING && !(lParam & 0x40000000)) {
                if (!IsMouseButton(g_bindings[ACT_SELECT_1]) &&
                    wParam == (WPARAM)g_bindings[ACT_SELECT_1]) throwSelected = 0;
                else if (!IsMouseButton(g_bindings[ACT_SELECT_2]) &&
                    wParam == (WPARAM)g_bindings[ACT_SELECT_2]) throwSelected = 1;
                else if (!IsMouseButton(g_bindings[ACT_SELECT_3]) &&
                    wParam == (WPARAM)g_bindings[ACT_SELECT_3]) throwSelected = 2;
            }

            /* 统一菜单导航按键判断 */
            int menuUp    = (wParam == (WPARAM)g_bindings[ACT_MENU_UP]);
            int menuDown  = (wParam == (WPARAM)g_bindings[ACT_MENU_DOWN]);
            int menuLeft  = (wParam == (WPARAM)g_bindings[ACT_MENU_LEFT]);
            int menuRight = (wParam == (WPARAM)g_bindings[ACT_MENU_RIGHT]);
            int menuConfirm = (wParam == (WPARAM)g_bindings[ACT_MENU_CONFIRM]);
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
                if (menuUp) { menuSelection = (menuSelection + 13 - 1) % 13; uiDirty = 1; }
                if (menuDown) { menuSelection = (menuSelection + 1) % 13; uiDirty = 1; }
                MenuGeo hg = GetMenuGeo();
                if (menuSelection < hg.scrollOffset) menuScrollOffsets[STATE_HELP] = menuSelection;
                else if (menuSelection >= hg.scrollOffset + hg.maxVisibleRows) menuScrollOffsets[STATE_HELP] = menuSelection - hg.maxVisibleRows + 1;
                if (menuCancel || menuConfirm) { currentState = STATE_MAIN_MENU; uiDirty = 1; }
            } else if (currentState == STATE_HUB) {
                int n = HUB_ROWS;
                if (menuUp) { hubSelection = (hubSelection + n - 1) % n; uiDirty = 1; }
                if (menuDown) { hubSelection = (hubSelection + 1) % n; uiDirty = 1; }
                MenuGeo hg = GetMenuGeo();
                if (hubSelection < hg.scrollOffset) menuScrollOffsets[STATE_HUB] = hubSelection;
                else if (hubSelection >= hg.scrollOffset + hg.maxVisibleRows) menuScrollOffsets[STATE_HUB] = hubSelection - hg.maxVisibleRows + 1;
                if (menuConfirm) ActivateHubRow();
                if (menuCancel) {
                    SaveGame();
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    ForceCursor(1);
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
                if (wParam == (WPARAM)g_bindings[ACT_PAUSE]) PauseGame();
                if (wParam == (WPARAM)g_bindings[ACT_MAP]) {
                    /* 三态循环：小窗 -> 全屏 -> 关闭 -> 小窗 ...
                       第一次按看到角落小窗（能边走边看），
                       再按一次变全屏（停下来规划路线），再按关闭。

                       鼠标行为跟着形态走：
                         小窗 —— 鼠标继续锁定，**视角能转、能继续玩**
                         全屏 —— 释放鼠标，**停下**专心看图 */
                    if (!isMapOpen) {
                        isMapOpen = 1;
                        mapFullScreen = 0;
                    } else if (!mapFullScreen) {
                        mapFullScreen = 1;
                    } else {
                        isMapOpen = 0;
                        mapFullScreen = 0;
                    }
                    /* 只有全屏才释放鼠标；小窗保持锁定，游戏继续 */
                    int wantCursor = (isMapOpen && mapFullScreen) ? 1 : 0;
                    ForceCursor(wantCursor);
                    if (wantCursor) SetCursor(LoadCursor(NULL, IDC_ARROW));
                    firstMouseMove = 1;
                    uiDirty = 1;
                }
                if (wParam == (WPARAM)g_bindings[ACT_FLASHLIGHT]) {
                    if (battery > 0.0f) flashlightOn = !flashlightOn;
                    uiDirty = 1;
                }
            }
            return 0;

        case WM_KEYUP:
            /* 与 WM_KEYDOWN 保持同一套折算，否则小键盘数字的 keys[] 会卡在按下状态 */
            if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
                wParam = (WPARAM)('0' + (wParam - VK_NUMPAD0));
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

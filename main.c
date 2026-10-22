/*
 * Canity — 纯 C + Win32 GDI 软件光栅化恐怖生存小引擎
 *
 * 架构要点：
 *   - 逻辑与渲染分离：固定 120Hz 逻辑步进（dt 驱动），渲染受帧数上限控制。
 *   - 内部分辨率渲染（Render Scale）：3D 与后处理都跑在 renderW×renderH，
 *     最后 StretchDIBits 放大到窗口；HUD / 菜单文字仍用 GDI 按窗口分辨率绘制。
 *   - 菜单/设置为整屏居中的半透明面板，选择项通过弹出式列表完成（不再用左右键）。
 *
 * 版本号：
 *   改这里就是改版本号 —— CANITY_VERSION_NUM 是整数（存进存档），
 *   CANITY_VERSION_STR 是显示用的字符串（窗口标题 / 主菜单 / 存档校验）。
 *   每次改动玩法或存档结构都要同时升 CANITY_VERSION_NUM 和 SAVE_VERSION，
 *   这样老存档会被识别为不兼容而安全丢弃，而不是加载出半截坏数据。
 */

#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- 版本号：改玩法或存档结构时必须同步升这两个 ---- */
#define CANITY_VERSION_NUM 8          /* 整数版本，进存档做记录 */
#define CANITY_VERSION_STR "1.5.1"    /* 显示版本，窗口标题 / 主菜单 */

#define MAP_WIDTH 24
#define MAP_HEIGHT 24
#define TEX_WIDTH 64
#define TEX_HEIGHT 64
#define MAX_SCREEN 4096       /* renderW/renderH 的安全上限 */
#define LOGIC_HZ 120.0        /* 逻辑步进频率，与渲染帧率无关 */
#define LOGIC_STEP (1.0 / LOGIC_HZ)
#define MOVE_SPEED 2.5f       /* 单位/秒 */
#define SANITY_DRAIN 0.5f     /* 理智/秒（满值约 200 秒） */
#define MOUSE_BASE_SENS 0.002 /* 弧度/像素（1.0x） */

/* 一层里最多能同时存在多少个可搜索容器。
   RICH 词条会额外 +2，所以数组本身要留出余量，
   否则容器数量一旦真的涨上去就会写越界。 */
#define MAX_CONTAINERS 8
#define CONTAINERS_BASE 6    /* 常规关卡的容器数上限 */
/* 场上最多同时存在几件投掷物（荧光棒 / 玻璃瓶） */
#define MAX_THROWN 4
/* 场上最多同时存在几枚飞行中的玻璃瓶 */
#define MAX_SHOTS 4

/* ------------------------------------------------------------------ */
/* 难度曲线                                                            */
/* ------------------------------------------------------------------ */
/* 原则：难度只从「怪物压力」上来，**不动理智的时间预算**。
   理智是整局共用的计时器（100 点 ÷ 0.5/秒 = 200 秒），GenerateLevel 里
   不会重置它。如果按层数放大理智流失，那不是难度曲线，是「越往后死得越早」
   的反向曲线 —— 所以 SANITY_DRAIN 保持常量，难度只改怪物。 */
#define MONSTER_SPEED_BASE 1.45f    /* 寻声怪基础速度 */
#define LIGHTBEAST_SPEED_BASE 1.50f /* 光敏怪基础速度 */
#define MONSTER_SPEED_STEP 0.03f    /* 每层 +0.03 */
#define MONSTER_SPEED_MAX 1.85f     /* 层数曲线的基础上限（第 14 层到顶）*/
#define MONSTER_SPEED_FINAL_MAX 2.40f /* 叠加 HUNTED 词条后的最终上限 */
/* 最终上限 2.40 < MOVE_SPEED 2.50：玩家永远走得比怪快。
   基础上限留 1.85 是为了让 HUNTED 的 ×1.25 在深层仍有意义
   （1.85 × 1.25 = 2.3125，不会顶到最终上限）。 */
#define HIT_COOLDOWN_BASE 4.0f      /* 被抓后的无敌时间，基础值 */
#define HIT_COOLDOWN_MIN 3.0f       /* 深层收紧到 3.0 秒 */
#define HIT_COOLDOWN_START_LEVEL 6  /* 从第 6 层开始收紧 */
/* 精灵排序表的上界：容器 + 配电箱 + 寻声怪 + 安全屋 + 光敏怪 + 投掷物 + 出口光柱
   + 飞行中的玻璃瓶
   注意：跳脸虫不在这个表里 —— 它是屏幕空间 overlay，由 RenderFrame 直接画。 */
#define MAX_SPRITES (MAX_CONTAINERS + MAX_THROWN + 6 + MAX_SHOTS)

/* 内部渲染的像素预算上限。
   窗口再大，内部渲染分辨率也不超过这个像素数 —— 这是全屏帧时间的保护阀。
   1080p 全屏实测：90 万 → 渲染 29ms + 呈现 11ms（只有 25fps）；
   28 万（约 640x360）→ 14ms，60fps 稳。
   注意：bench_canity.c 复刻了同一套分辨率计算，用的是这里的常量，
   不要在两处各写一份数字（上一轮就因为这里漏改，基准一直在测已废弃的配置）。 */
#define PIXEL_BUDGET 280000

/* ------------------------------------------------------------------ */
/* 全局状态                                                            */
/* ------------------------------------------------------------------ */

int winW = 800, winH = 600;          /* 窗口客户区尺寸（逻辑/显示分辨率） */
int renderW = 800, renderH = 600;    /* 内部渲染分辨率 */
HWND g_hwnd = NULL;

typedef enum { STATE_MAIN_MENU, STATE_PLAYING, STATE_PAUSED, STATE_SETTINGS, STATE_GAMEOVER, STATE_HUB, STATE_HELP } GameState;
GameState currentState = STATE_MAIN_MENU;
GameState prevState = STATE_MAIN_MENU;

int menuSelection = 0;
int settingsSelection = 0;
int settingsPopupOpen = 0;
int popupSelection = 0;
int popupItem = -1;
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
static const float fovPlaneLen[5] = {0.6494f, 0.7673f, 0.9163f, 1.0926f, 1.2972f};

/* FPS 统计 */
int currentFps = 0;
double lastRenderMs = 0.0;   /* 最近一帧（含呈现与 HUD）的耗时 */
double frameMsMax = 0.0;     /* 最近一秒内的最差帧，用来观察卡顿 */

/* 世界 */
int worldMap[MAP_HEIGHT][MAP_WIDTH];
int explored[MAP_HEIGHT][MAP_WIDTH];

float posX = 1.5f, posY = 1.5f;
float dirX = 1.0f, dirY = 0.0f;
float planeX = 0.0f, planeY = 0.66f;

int currentLevel = 1;
float exitX = 0.0f, exitY = 0.0f;
float flashTime = 0.0f;      /* 切关白屏剩余秒数 */
float healFlash = 0.0f;      /* 回血白闪剩余秒数 */

float sanity = 100.0f;

/* ------------------------------------------------------------------ */
/* 状态效果框架                                                        */
/* ------------------------------------------------------------------ */
/* ApplyFov 在下面会被用到（视野收窄要重算相机平面），
   它的定义在文件后部，这里先做前置声明。 */
void ApplyFov(void);

/* 玩家身上可以叠加若干「状态效果」（debuff）。每个效果有独立的强度与
   剩余时间，到期自动失效。强度统一是 0..1，1 = 最强。
   目前定义了三种效果，全部由寄生怪施加（见 v1.2.0）：
     FX_BLIND  视野收窄（FOV 从当前档位逐步压到最窄）
     FX_SLOW   移速下降
     FX_DRAIN  理智额外流失
   设计成「一组可叠加的全局状态」而不是把效果写死在某只怪里，
   以后加新怪只要往里塞一个效果就行，不用改渲染/逻辑主流程。 */
#define FX_COUNT 3
enum { FX_BLIND = 0, FX_SLOW, FX_DRAIN };

typedef struct {
    float strength;    /* 0..1，1 = 最强 */
    float remain;      /* 剩余秒数 */
} StatusFx;

static StatusFx g_fx[FX_COUNT];

/* 效果强度总览：给 HUD 用。返回 0 表示没有该效果。 */
static float FxStrength(int kind) {
    if (kind < 0 || kind >= FX_COUNT) return 0.0f;
    return g_fx[kind].strength;
}

/* 施加 / 刷新一个状态效果。
   新强度取「当前强度」与「新强度」的较大值 —— 寄生怪反复附着时
   不会把已有的强效果削弱掉。剩余时间取较大值。 */
void ApplyFx(int kind, float strength, float duration) {
    if (kind < 0 || kind >= FX_COUNT) return;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (g_fx[kind].strength < strength) g_fx[kind].strength = strength;
    if (g_fx[kind].remain < duration) g_fx[kind].remain = duration;
    /* 视野收窄要立刻生效 */
    if (kind == FX_BLIND) ApplyFov();
}

/* 推进所有效果的计时。到期归零。
   返回 1 表示某个效果的强度发生了变化 —— 调用方需要重算 FOV。
   用一个很小的 epsilon 判到期：浮点累加 1.0 秒后往往剩下 1e-7 这样的正数，
   直接判 `remain <= 0` 会让效果多活一帧。 */
static int UpdateFx(float dt) {
    int changed = 0;
    for (int i = 0; i < FX_COUNT; i++) {
        if (g_fx[i].remain <= 0.0f) continue;
        float before = g_fx[i].strength;
        g_fx[i].remain -= dt;
        if (g_fx[i].remain <= 0.0001f) {
            g_fx[i].remain = 0.0f;
            g_fx[i].strength = 0.0f;
        }
        if (g_fx[i].strength != before) changed = 1;
    }
    return changed;
}

/* 清除全部状态效果（换关、读档、死亡时调用） */
static void ClearFx(void) {
    for (int i = 0; i < FX_COUNT; i++) {
        g_fx[i].strength = 0.0f;
        g_fx[i].remain = 0.0f;
    }
}

/* ------------------------------------------------------------------ */
/* 怪物血量：现在玻璃瓶能砸怪，所以怪需要有血                            */
/* ------------------------------------------------------------------ */
/* 之前怪只有「碰到就扣玩家理智」一种交互，没有受击的概念。
   加上血量后，玻璃瓶成了唯一的武器 —— 被砸中的怪会掉血、
   短暂后退并进入硬直，砸够了就驱离。 */
#define MONSTER_HP 3         /* 寻声怪：3 瓶 */
#define LIGHTBEAST_HP 2      /* 光敏怪：2 瓶（它更难缠，脆一点更合理） */
#define MONSTER_STUN 1.2f    /* 被砸中后的硬直秒数 */
#define MONSTER_FLEE_DIST 3.0f /* 被驱离时后退的距离 */

/* 每关随机词条：给每层一点变化 */
#define MOD_COUNT 6
int levelMod = -1;
float modSanityMul = 1.0f;
float modSpeedMul = 1.0f;
float levelBanner = 0.0f;      /* 进关后显示词条名的秒数 */
float safeZoneMsg = 0.0f;      /* 进入安全屋后提示「SAFE ZONE」的剩余秒数 */
static const char* modNames[MOD_COUNT] = {"DARK", "HUNTED", "CRAMPED", "LEAKY", "RICH", "CALM"};
static const char* modDesc[MOD_COUNT] = {
    "FLASHLIGHT STARTS LOW",
    "THE THING MOVES FASTER",
    "FEWER CONTAINERS",
    "SANITY DRAINS FASTER",
    "MORE CONTAINERS",
    "SANITY DRAINS SLOWER"
};

/* 大厅：碎片货币 + 四项永久升级 */
int shards = 0;
int upgBattery = 0;      /* 电量上限 +20/级 */
int upgSpeed = 0;        /* 移速 +6%/级 */
int upgQuiet = 0;        /* 噪声半径 -15%/级 */
int upgSanity = 0;       /* 理智流失 -10%/级 */
#define UPG_MAX 3

int hubSelection = 0;
#define HUB_ROWS 5       /* 4 项升级 + Back */

/* 寻声怪：只听声音，走路它听不见，跑步/翻找/摇电会把它引过来 */
#define MON_MAXPATH 96
typedef struct {
    float x, y;
    int active;
    int state;                 /* 0=游荡 1=追声 2=原地搜寻 */
    float tx, ty;
    int path[MON_MAXPATH][2];
    int pathLen, pathIdx;
    float repath, stateTimer, wanderTimer, hitCooldown, growlTimer;
    float speed;
    int hp;                    /* 剩余血量，被玻璃瓶砸会掉 */
    float stun;                /* 硬直剩余秒数，期间不能移动 */
    /* 寄生怪专用（其它两只怪不用这几个字段） */
    int attached;              /* 1 = 已附着在玩家身上 */
    float ramp;                /* 附着深度 0..1，1 = 效果拉满 */
    float attachTimer;         /* 在范围内待了多久（累计），用于「缠上来」的进度 */
    float offX, offY;          /* 附着时趴在玩家背后的世界坐标偏移 */
} Monster;
Monster monster;
Monster lightBeast;      /* 只看光 */

/* ------------------------------------------------------------------ */
/* 跳脸虫（Face Jumper）—— QTE 怪                                      */
/* ------------------------------------------------------------------ */
/* 机制：它从远处**弹射**到玩家脸上，然后是一场两阶段的 QTE：
 *
 *   阶段一「躲开」：虫子跳到面前，屏幕上给出倒计时，
 *                    玩家必须在这段时间内按 SPACE 把它推开。
 *                    躲开了它就消失，本关不再出现。
 *
 *   阶段二「按键」：没躲开，它跿上脸，
 *                    屏幕上显示一个随机键（A / S / D / F），
 *                    玩家必须在倒计时内按下那个键。
 *
 *   两步都错 → 惩罚：扣理智 + 短时 debuff。
 *
 * 为什么是 QTE 而不是「贴上来施加 debuff」：
 *   QTE 把攻击变成**反应**，节奏感强，而且**每次都是不同的事件**，
 *   不像持续 debuff 那样会变成「常态化的背景惩罚」。
 *   跳脸虫也符合恐怖游戏的直觉 —— 它扑上来的那一瞬间才是可怕的地方。
 *
 * 它每关按概率出现（第 3 层起），不是每关必出 —— 稀有才有惊吓感。 */
#define JUMPER_APPEAR_CHANCE 35   /* 每关出现的概率（百分比） */
#define JUMPER_MIN_LEVEL 3        /* 第几层开始可能出现 */
#define JUMPER_APPROACH_TIME 1.1f /* 从远处弹到面前的秒数 */
#define JUMPER_DODGE_WINDOW 1.0f  /* 阶段一：躲开的时间窗 */
#define JUMPER_QTE_WINDOW 1.5f    /* 阶段二：按键的时间窗 */
#define JUMPER_FAIL_SANITY 20.0f  /* 失败的理智惩罚 */
#define JUMPER_FAIL_FX_TIME 3.0f  /* 失败的 debuff 持续时间 */
#define JUMPER_FAIL_FX_POWER 0.55f/* 失败的 debuff 强度 */

/* 阶段 */
enum { JMP_IDLE = 0, JMP_APPROACH, JMP_DODGE, JMP_QTE, JMP_DONE };

/* 阶段二要按的随机键：0=A 1=S 2=D 3=F */
#define JMP_KEY_COUNT 4

static Monster parasite;   /* 复用 Monster 结构体（只需要位置和几个状态字段） */

/* QTE 运行时状态 */
static int jumperPhase = JMP_IDLE;
static float jumperTimer = 0.0f;     /* 当前阶段的倒计时 */
static int jumperQteKey = 0;         /* 阶段二要按的键编号 */
static int jumperResolved = 0;       /* 本次事件是否已结算（防止重复结算） */
static float jumperTrigger = -1.0f;  /* 本关什么时候触发（秒）；<0 = 本关不出现 */

int flares = 0, bottles = 0;
typedef struct { float x, y; int type; float life; int active; } Thrown;  /* 0=荧光棒 */
Thrown thrown[MAX_THROWN];

/* ------------------------------------------------------------------ */
/* 飞行投掷物（玻璃瓶 = 唯一的武器）                                    */
/* ------------------------------------------------------------------ */
/* 荧光棒是「扔在地上持续发光」的静止物，玻璃瓶则是「飞出去砸东西」的弹丸。
   两者机制不同，所以分成两个系统：
     thrown[]  —— 静止物（荧光棒 / 瓶子碎片），只管显示与被光敏怪追踪
     shot[]    —— 飞行物（玻璃瓶），带速度，撞到怪就结算伤害
   之前玻璃瓶是「瞬间在落点碎裂」，没有飞行过程，也就无从「砸中怪」。 */
#define SHOT_SPEED 9.0f      /* 玻璃瓶飞行速度，单位/秒 */
#define SHOT_RADIUS 0.35f    /* 命中判定半径（怪也是半径 0.24 的量级） */
#define SHOT_DAMAGE 1        /* 一瓶砸掉几点血 */
typedef struct {
    float x, y;
    float vx, vy;
    int active;
    float life;      /* 最长飞行时间，防止卡在墙里 */
    float age;       /* 已经飞了多久：第一帧不判墙（出生点可能就贴着墙） */
} Shot;
static Shot shot[MAX_SHOTS];

/* 安全屋：每关一个，站在里面缓慢回理智 */
typedef struct { float x, y; int active; } SafeRoom;
SafeRoom safeRoom;
/* 安全屋的作用半径（单位：格）。
   注意 Dist2() 返回的是**平方距离**，所以比较时要平方。
   之前直接写 `Dist2(...) < 0.60f`，实际范围只有 sqrt(0.6)=0.78 格 ——
   比安全屋的视觉大小还小，玩家站在看得见花的地方 HUD 却不亮。
   现在统一用这个常量：1.0 格，平方后是 1.0f。 */
#define SAFE_ZONE_RADIUS 1.0f

/* 可搜索容器 / 保险丝 / 配电箱
   MAX_CONTAINERS 见文件顶部：数组按 RICH 词条的上限留足余量，
   常规关卡用 CONTAINERS_BASE 个。 */
typedef struct {
    float x, y;
    int active;
    int searched;
    int content;      /* 1 = 保险丝  2 = 电池  0 = 空 */
} Container;
Container containers[MAX_CONTAINERS];
int containerCount = 0;

typedef struct { float x, y; int active; } Breaker;
Breaker breaker;

int fusesHeld = 0;        /* 手上已有保险丝 */
float runTime = 0.0f;     /* 本局存活时间 */
float stepTimer = 0.0f;
float crankTimer = 0.0f;
float throwCd = 0.0f;   /* 手摇进度 0..CRANK_TIME */
float crankClick = 0.0f;
int cranking = 0;
double g_timeSec = 0.0;    /* 主循环时钟，渲染用它做动画 */
float heartTimer = 0.0f;
int searchedCount = 0;    /* 搜索过的容器数 */
int hasSave = 0;          /* 是否有可继续的存档 */
int bestLevel = 0;
float volumeF = 0.8f;     /* 0..1，0 = 静音 */
int runInProgress = 0;    /* 当前是否有一局未结束 */
int fusesNeeded = 2;      /* 本关需要几根 */
int breakerOpen = 0;      /* 配电箱是否已通電 */

/* 交互（长按 E） */
#define INTERACT_RANGE 1.25f
#define SEARCH_TIME 1.1f
int hoverKind = 0;        /* 0=无 1=容器 2=配电箱 */
int hoverIndex = -1;
float holdTimer = 0.0f;
int holdingNow = 0;

/* 手电筒 / 电池 / 体力 */
float battery = 100.0f;          /* 0..100 */
int flashlightOn = 1;
float stamina = 100.0f;          /* 0..100 */
int sprinting = 0;
float staminaIdle = 0.0f;        /* 停止冲刺后的恢复延迟 */

#define BATTERY_DRAIN 0.85f      /* 每秒耗电 */
#define BATTERY_PICKUP 45.0f     /* 电池补给量 */
#define STAMINA_DRAIN 26.0f      /* 冲刺每秒消耗 */
#define STAMINA_REGEN 20.0f      /* 恢复每秒 */
#define SPRINT_MULT 1.65f        /* 冲刺速度倍率 */

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
static void ClearKeyEdges(void) {
    for (int i = 0; i < 256; i++) keyPressed[i] = 0;
}

POINT lastMousePos;
int firstMouseMove = 1;
int isMapOpen = 0;
/* 小地图有两种形态：
     0 = 角落小窗（默认）—— 一边走一边看，不挡视野
     1 = 全屏 —— 停下来仔细规划路线
   按 M 切换形态，按 M 关闭。再按 M 会先切形态，再按一次才关闭。 */
int mapFullScreen = 0;

unsigned int* pixels = NULL;
unsigned char* vignetteMask = NULL;
BITMAPINFO bmi;

unsigned int texture[TEX_HEIGHT][TEX_WIDTH];
unsigned int floorTexture[TEX_HEIGHT][TEX_WIDTH];
unsigned int ceilTexture[TEX_HEIGHT][TEX_WIDTH];

float zBuffer[MAX_SCREEN];
float flashTab[MAX_SCREEN];
int ceilTab[MAX_SCREEN];
float floorGain[MAX_SCREEN];   /* 0.52 + cone*1.30，按列预计算 */

/* ------------------------------------------------------------------ */
/* 设置项模型                                                          */
/* ------------------------------------------------------------------ */

enum { SET_LIST = 0, SET_SLIDER = 1 };

typedef struct {
    const char* name;
    int kind;
    const char* const* options;   /* SET_LIST */
    int count;
    int* value;                   /* SET_LIST */
    float* fvalue;                /* SET_SLIDER */
    float fmin, fmax, fstep;
    int page;                     /* 1=VIDEO 2=INPUT 3=GAME */
} SettingItem;

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

static SettingItem settings[] = {
    {"Resolution",   SET_LIST,   optRes,   5, &resIndex,         NULL,      0,    0,    0, 1},
    {"Fullscreen",   SET_LIST,   optOnOff, 2, &isFullscreen,     NULL,      0,    0,    0, 1},
    {"Frame Limit",  SET_LIST,   optFps,   5, &fpsCapIndex,      NULL,      0,    0,    0, 1},
    {"Render Scale", SET_LIST,   optScale, 4, &renderScaleIndex, NULL,      0,    0,    0, 1},
    {"Gamma",        SET_SLIDER, NULL,     0, NULL,              &gammaValue, 0.7f, 1.6f, 0.05f, 1},
    {"Mouse Sens.",  SET_SLIDER, NULL,     0, NULL,              &mouseSens, 0.2f, 3.0f, 0.1f, 2},
    {"Field of View", SET_LIST,  optFov,   5, &fovIndex,         NULL,      0,    0,    0, 2},
    {"Volume",       SET_SLIDER, NULL,     0, NULL,              &volumeF,  0.0f, 1.0f, 0.05f, 3},
    {"Sanity FX",    SET_LIST,   optOnOff, 2, &vignetteOn,       NULL,      0,    0,    0, 3},
    {"Show FPS",     SET_LIST,   optOnOff, 2, &showFps,          NULL,      0,    0,    0, 3},
    {"HUD Detail",   SET_LIST,   optHud,   3, &hudDetail,        NULL,      0,    0,    0, 3},
    {"QTE Hint",     SET_LIST,   optQte,   2, &qteHint,          NULL,      0,    0,    0, 3},
};
#define SETTING_COUNT ((int)(sizeof(settings) / sizeof(settings[0])))

/* 设置菜单分页：0=根目录（三个分类 + Back），1..3=分类页 */
static int settingsPage = 0;
static const char* pageNames[3] = {"Video", "Input", "Game"};

/* 当前页包含的设置项下标，返回个数 */
static int PageItems(int* out) {
    if (settingsPage < 1 || settingsPage > 3) return 0;
    int n = 0;
    for (int i = 0; i < SETTING_COUNT; i++)
        if (settings[i].page == settingsPage) out[n++] = i;
    return n;
}

static int SettingsRowCount(void) {
    if (settingsPage < 1) return 3 + 1;     /* 三个分类 + Back */
    int tmp[SETTING_COUNT];
    return PageItems(tmp) + 1;              /* 若干项 + Back */
}

/* 前置声明：大厅 */
void ActivateHubRow(void);

void StartGame(void);
int BatteryMax(void);

/* 前置声明：怪物 */
void SpawnMonster(void);
void UpdateMonster(float dt);
void UpdateLightBeast(float dt);
void SpawnLightBeast(void);
void SpawnParasite(void);
static void ThrowItem(int type);
int HasLOS(float x0, float y0, float x1, float y1);
void MakeNoise(float x, float y, float radius);

/* 前置声明：音效 */
enum { SFX_STEP = 0, SFX_HEART, SFX_PICKUP, SFX_POWER, SFX_GATE, SFX_CRANK, SFX_SCREAM, SFX_GLASS, SFX_COUNT };
void PlaySfx(int id, float vol);
void InitAudio(void);
void ShutdownAudio(void);

/* 前置声明：菜单与存档互相引用 */
static int MainMenuCount(void);
static void SaveGame(void);
static void DeleteSave(void);
static void ContinueRun(void);

/* ------------------------------------------------------------------ */
/* 缓冲区 / 纹理 / 渐晕                                                 */
/* ------------------------------------------------------------------ */

void GenerateVignette(void) {
    free(vignetteMask);
    vignetteMask = (unsigned char*)malloc((size_t)renderW * renderH);
    int cx = renderW / 2, cy = renderH / 2;
    float maxDist = sqrtf((float)(cx * cx + cy * cy));
    if (maxDist < 1.0f) maxDist = 1.0f;
    for (int y = 0; y < renderH; y++) {
        for (int x = 0; x < renderW; x++) {
            int dx = x - cx, dy = y - cy;
            float ratio = sqrtf((float)(dx * dx + dy * dy)) / maxDist;
            if (ratio < 0.4f) ratio = 0.0f;
            else ratio = (ratio - 0.4f) / 0.6f;
            if (ratio > 1.0f) ratio = 1.0f;
            vignetteMask[y * renderW + x] = (unsigned char)(ratio * 255.0f);
        }
    }
}

/* 像素缓冲直接建在 DIB Section 上：GDI 走共享内存快路径，
   呈现时用 BitBlt 而不是每次从用户内存 StretchDIBits */
HDC g_memDC = NULL;
HBITMAP g_dib = NULL;
void* g_dibBits = NULL;
int g_useDIB = 0;

/* 按当前设置重新分配内部渲染缓冲 */
void InitPixels(void) {
    float s = renderScales[renderScaleIndex] / 100.0f;
    renderW = (int)(winW * s);
    renderH = (int)(winH * s);
    if (renderW < 160) renderW = 160;
    if (renderH < 120) renderH = 120;
    if (renderW > MAX_SCREEN) renderW = MAX_SCREEN;
    if (renderH > MAX_SCREEN) renderH = MAX_SCREEN;

    /* 像素预算：窗口再大，内部渲染也不超过这个量，保证高分辨率下的帧时间。
       原来 90 万，1080p 全屏实测「渲染 29ms + 呈现 11ms」只有 25fps，
       这就是全屏卡顿的根因。28 万（约 640x360）实测 14ms，60fps 稳。 */
    {
        const long budget = PIXEL_BUDGET;
        long area = (long)renderW * (long)renderH;
        if (area > budget) {
            float f = sqrtf((float)budget / (float)area);
            renderW = (int)(renderW * f);
            renderH = (int)(renderH * f);
            if (renderW < 160) renderW = 160;
            if (renderH < 120) renderH = 120;
        }
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = renderW;
    bmi.bmiHeader.biHeight = -renderH;   /* 负数 = 自上而下 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (g_memDC) { DeleteDC(g_memDC); g_memDC = NULL; }
    if (g_dib) { DeleteObject(g_dib); g_dib = NULL; }
    g_dibBits = NULL;

    g_memDC = CreateCompatibleDC(NULL);
    if (g_memDC) g_dib = CreateDIBSection(g_memDC, &bmi, DIB_RGB_COLORS, &g_dibBits, NULL, 0);
    if (g_memDC && g_dib && g_dibBits) {
        SelectObject(g_memDC, g_dib);
        pixels = (unsigned int*)g_dibBits;
        g_useDIB = 1;
    } else {
        if (g_memDC) { DeleteDC(g_memDC); g_memDC = NULL; }
        if (g_dib) { DeleteObject(g_dib); g_dib = NULL; }
        g_useDIB = 0;
        free(pixels);
        pixels = (unsigned int*)malloc((size_t)renderW * renderH * sizeof(unsigned int));
    }
    if (pixels) memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));

    GenerateVignette();
    uiDirty = 1;
}

static void BuildShadeTables(void);
void GenerateThemeTextures(int id);

static unsigned char noiseTab[256];
static int noiseReady = 0;

unsigned int floorBaseA = 0x00151515u, floorBaseB = 0x00202020u;
int themeId = 0;
static const char* themeNames[4] = {"BRICK", "PRISON", "HELL", "WARD"};

/* ---- 地板/天花板预乘表 ----
   地板是全屏最贵的部分（占 Render3D 的一半以上），每像素原本要做
   3 次「贴图通道 × 亮度」的乘法 + 3 次截断比较。

   关键观察：程序化生成的地板贴图**只有 4 种颜色**（复古化把颜色量化到 8 级，
   再加上 2x2 块对齐，同一主题内实际用到的颜色极少）。
   所以可以把「颜色 × 亮度」预先乘好，做成一张很小的表：

       premulFloor[颜色索引][亮度等级]  →  最终像素

   表大小 = 4 色 × 256 级 × 4 字节 = 4KB（地板）+ 4KB（天花板），
   完全落在 L1 缓存里。

   > 注意：HANDOFF §13.2 记录过「墙壁预乘成 512KB 查表反而更慢」——
   > 那是表太大导致 L2 未命中。这里表只有 4KB，结论正好相反。

   亮度等级怎么定：
   地板的 k8（shadeLut 校正后的亮度）实测只到 **250**（手电最亮处），
   所以等级用 8 级步长就够：level = k8 >> 3 -> 0..31，表只要 4*32*4 = 512 字节。
   但为了留余量（Gamma 调大时 shadeLut 输出范围不变，实际 k8 上限也不变），
   这里仍按 256 级建表（k8 >> 2，步长 4），精度足够且表才 4KB。 */
#define PREMUL_LEVELS 256          /* 亮度等级数 */
#define PREMUL_MAXCOL 16           /* 单张贴图最多用到的颜色数（实测 4） */
#define PREMUL_K8_SHIFT 2          /* k8 >> 2 -> 0..255 的等级索引 */

static unsigned int premulFloor[PREMUL_MAXCOL][PREMUL_LEVELS];
static unsigned int premulCeil[PREMUL_MAXCOL][PREMUL_LEVELS];
static unsigned char floorColorIdx[TEX_HEIGHT][TEX_WIDTH];  /* 贴图 -> 颜色索引 */
static unsigned char ceilColorIdx[TEX_HEIGHT][TEX_WIDTH];
static int nFloorColors = 0, nCeilColors = 0;

/* 把贴图重新索引成「颜色索引」，并建好预乘表。
   贴图本身保持不变（墙体、精灵等还在用），只是额外存一份索引。 */
static void BuildPremulTables(void) {
    unsigned int fcols[PREMUL_MAXCOL], ccols[PREMUL_MAXCOL];
    nFloorColors = 0;
    nCeilColors = 0;

    for (int y = 0; y < TEX_HEIGHT; y++) {
        for (int x = 0; x < TEX_WIDTH; x++) {
            unsigned int c = floorTexture[y][x] & 0xFFFFFFu;
            int fi = -1;
            for (int i = 0; i < nFloorColors; i++) if (fcols[i] == c) { fi = i; break; }
            if (fi < 0) {
                if (nFloorColors < PREMUL_MAXCOL) { fi = nFloorColors; fcols[nFloorColors++] = c; }
                else fi = PREMUL_MAXCOL - 1;      /* 颜色用超了，退回最后一个 */
            }
            floorColorIdx[y][x] = (unsigned char)fi;

            unsigned int cc = ceilTexture[y][x] & 0xFFFFFFu;
            int gi = -1;
            for (int i = 0; i < nCeilColors; i++) if (ccols[i] == cc) { gi = i; break; }
            if (gi < 0) {
                if (nCeilColors < PREMUL_MAXCOL) { gi = nCeilColors; ccols[nCeilColors++] = cc; }
                else gi = PREMUL_MAXCOL - 1;
            }
            ceilColorIdx[y][x] = (unsigned char)gi;
        }
    }

    /* 亮度等级 l -> k8 = l * 4（k8 上限 1023，256 级正好铺满） */
    for (int ci = 0; ci < nFloorColors; ci++) {
        int r = (int)((fcols[ci] >> 16) & 0xFF), g = (int)((fcols[ci] >> 8) & 0xFF), b = (int)(fcols[ci] & 0xFF);
        for (int l = 0; l < PREMUL_LEVELS; l++) {
            int k = l * 4;
            int rr = (r * k) >> 8, gg = (g * k) >> 8, bb = (b * k) >> 8;
            if (rr > 255) rr = 255;
            if (gg > 255) gg = 255;
            if (bb > 255) bb = 255;
            premulFloor[ci][l] = (unsigned int)((rr << 16) | (gg << 8) | bb);
        }
    }
    /* 天花板用的乘数 ceilTab[x] 是 0..255 量纲（(通道*ck)>>8），
       而表里统一用 k8 量纲（0..1023），所以等级 = ck（对应 k = ck*4）。 */
    for (int ci = 0; ci < nCeilColors; ci++) {
        int r = (int)((ccols[ci] >> 16) & 0xFF), g = (int)((ccols[ci] >> 8) & 0xFF), b = (int)(ccols[ci] & 0xFF);
        for (int l = 0; l < PREMUL_LEVELS; l++) {
            int k = l * 4;
            int rr = (r * k) >> 8, gg = (g * k) >> 8, bb = (b * k) >> 8;
            if (rr > 255) rr = 255;
            if (gg > 255) gg = 255;
            if (bb > 255) bb = 255;
            premulCeil[ci][l] = (unsigned int)((rr << 16) | (gg << 8) | bb);
        }
    }
}

void GenerateThemeTextures(int id) {
    themeId = id;
    if (id == 1) { floorBaseA = 0x0014161au; floorBaseB = 0x001c1f24u; }
    else if (id == 2) { floorBaseA = 0x001a0c0cu; floorBaseB = 0x00241010u; }
    else if (id == 3) { floorBaseA = 0x00181c18u; floorBaseB = 0x0020261fu; }
    else { floorBaseA = 0x00151515u; floorBaseB = 0x00202020u; }

    for (int y = 0; y < TEX_HEIGHT; y++) {
        for (int x = 0; x < TEX_WIDTH; x++) {
            /* 复古：2x2 像素块 + 8 级色阶 */
            int px = x & ~1, py = y & ~1;
            int r, g, b;
            switch (id) {
                case 1:   /* 监狱：冷灰蓝 + 铁栏 */
                    r = 34 + (px * py) % 12; g = 38 + (x * y) % 12; b = 44 + (px * py) % 14;
                    if (py % 12 == 0 || px % 24 == 0) { r = 22; g = 26; b = 32; }
                    if (px % 8 == 0) { r -= 10; g -= 10; b -= 10; }
                    break;
                case 2:   /* 地狱：暗红 + 裂纹 */
                    r = 52 + (px * py) % 24; g = 8 + (px * py) % 6; b = 6;
                    if (rand() % 22 == 0) { r = 8; g = 3; b = 3; }
                    if (rand() % 60 == 0) { r = 150; g = 40; b = 10; }
                    break;
                case 3:   /* 病房：浅瓷砖 + 污渍 */
                    r = 66 + (px * py) % 10; g = 70 + (x * y) % 10; b = 64 + (px * py) % 8;
                    if (py % 16 == 0 || px % 16 == 0) { r = 38; g = 42; b = 38; }
                    if (rand() % 45 == 0) { r = 30; g = 40; b = 30; }
                    break;
                default:  /* 砖墙 */
                    r = 40 + (px * py) % 20; g = 10 + (x * y) % 10; b = 10;
                    if (rand() % 50 == 0) { r = 150; g = 0; b = 0; }
                    if (py % 16 == 0 || (px + ((py / 16) % 2) * 16) % 32 == 0) { r = 10; g = 10; b = 10; }
                    break;
            }
            r = (r >> 5) << 5; g = (g >> 5) << 5; b = (b >> 5) << 5;
            if (r > 248) r = 248;
            if (g > 248) g = 248;
            if (b > 248) b = 248;
            texture[y][x] = (unsigned int)((r << 16) | (g << 8) | b);

            /* 地砖：32px 砖块 + 勾缝 + 污渍 */
            {
                int fr = (int)((floorBaseA >> 16) & 0xFF), fg = (int)((floorBaseA >> 8) & 0xFF), fb = (int)(floorBaseA & 0xFF);
                int dark = ((px / 32 + py / 32) % 2) ? 0 : -8;
                if ((px % 32) < 2 || (py % 32) < 2) { fr = 12; fg = 12; fb = 12; }   /* 勾缝 */
                else if (rand() % 70 == 0) { fr = fr * 6 / 10; fg = fg * 6 / 10; fb = fb * 6 / 10; }
                else { fr += dark; fg += dark; fb += dark; }
                if (fr < 0) fr = 0;
                if (fg < 0) fg = 0;
                if (fb < 0) fb = 0;
                floorTexture[y][x] = (unsigned int)((fr << 16) | (fg << 8) | fb);
            }
            /* 天花板：混凝土 + 每隔 32px 一道横梁 */
            {
                int cr = 16 + (px * py) % 8, cg = 16 + (px * py) % 8, cb = 18 + (px * py) % 8;
                if (py % 32 < 3) { cr = 9; cg = 9; cb = 10; }
                else if (rand() % 90 == 0) { cr = 26; cg = 24; cb = 22; }
                ceilTexture[y][x] = (unsigned int)((cr << 16) | (cg << 8) | cb);
            }
        }
    }
    if (!noiseReady) {
        for (int i = 0; i < 256; i++) noiseTab[i] = (unsigned char)(rand() % 40);
        noiseReady = 1;
    }
    BuildPremulTables();   /* 地板/天花板预乘表（贴图变了就要重建） */
    BuildShadeTables();
}


/* ------------------------------------------------------------------ */
/* 地图生成                                                            */
/* ------------------------------------------------------------------ */

static float Dist2(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

/* 玩家碰撞半径：贴墙时留出半个身位，避免半个身子钻进墙里 */
#define PLAYER_RADIUS 0.24f

static int Blocked(float x, float y) {
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
static int ShotBlocked(float x, float y) {
    int cx = (int)floorf(x), cy = (int)floorf(y);
    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return 1;
    return worldMap[cy][cx] ? 1 : 0;
}

/* 按 FOV 设置重建相机平面：长度 = tan(fov/2)，方向与朝向垂直
   状态效果 FX_BLIND 会把视野往最窄的一档压 —— 这是「致盲」的实现。
   不改 fovIndex 本身（那是玩家的设置项），而是算一个临时的有效 FOV，
   这样效果一结束，视野立刻回到玩家设的值。 */
void ApplyFov(void) {
    float L = fovPlaneLen[fovIndex];
    float blind = FxStrength(FX_BLIND);
    if (blind > 0.0f) {
        /* 与最窄档（66 度，fovPlaneLen[0]）线性插值 */
        float narrow = fovPlaneLen[0];
        L = narrow + (L - narrow) * (1.0f - blind);
    }
    planeX = -dirY * L;
    planeY = dirX * L;
}

/* 该位置是否离已有物件太近（防止箱子/配电箱/花/怪互相叠在一起） */
static int SpotClear(float x, float y, float minD, int selfKind, int selfIdx) {
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

/* ------------------------------------------------------------------ */
/* 难度曲线：全是纯函数，只吃层数，方便离线自检                        */
/* ------------------------------------------------------------------ */

/* 怪物速度随层数线性上升，但**永远慢于玩家走速 MOVE_SPEED**。
   玩家走 2.5，怪最快 2.40 —— 你永远跑得掉，但「跑掉」的代价越来越大：
   走路完全静音，而翻找箱子会发出半径 8 的噪声，怪跑得快意味着
   「搜箱子」在深层更危险。压迫感来自空间被压缩，不是来自时间被砍短。

   modMul 是 HUNTED 词条的倍率（1.25）。它叠在层数曲线之后，
   最后统一夹到 MONSTER_SPEED_FINAL_MAX —— 这条夹取保证了
   「即使在最深的一层 + HUNTED，怪也不会跑得比玩家快」。 */
static float MonsterSpeedFor(int level, float base, float modMul) {
    if (level < 1) level = 1;
    float s = base + MONSTER_SPEED_STEP * (float)(level - 1);
    if (s > MONSTER_SPEED_MAX) s = MONSTER_SPEED_MAX;
    s *= modMul;
    if (s > MONSTER_SPEED_FINAL_MAX) s = MONSTER_SPEED_FINAL_MAX;
    return s;
}

/* 被抓后的无敌时间：深层收紧，让连续被抓不再是无解连击。
   第 6 层开始，每深一层减 0.1 秒，第 15 层触到下限 3.0 秒。 */
static float HitCooldownFor(int level) {
    if (level < HIT_COOLDOWN_START_LEVEL) return HIT_COOLDOWN_BASE;
    float t = HIT_COOLDOWN_BASE - 0.1f * (float)(level - HIT_COOLDOWN_START_LEVEL + 1);
    if (t < HIT_COOLDOWN_MIN) t = HIT_COOLDOWN_MIN;
    return t;
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

    /* 出口与药箱候选：离出生点足够远 */
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

    exitX = posX; exitY = posY;
    if (emptyCount > 0) {
        int idx = rand() % emptyCount;
        exitX = cells[idx][0] + 0.5f;
        exitY = cells[idx][1] + 0.5f;
    }

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

    /* 本关需要的保险丝：2 根起，每 3 关 +1，最多 4 */
    fusesNeeded = 2 + currentLevel / 3;
    if (fusesNeeded > 4) fusesNeeded = 4;

    if (emptyCount > 10) {
        int attempts = 0;
        /* 容器：基准 4~6 个，离出生点和出口都远一点。
           CRAMPED 减 2，RICH 加 2 —— 词条真正改变容器数量，
           数组上界 MAX_CONTAINERS 已按 RICH 的最大值留好余量。 */
        int contCap = ContainerTargetCount(levelMod, CONTAINERS_BASE - 2 + (rand() % 3));
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

        /* 把保险丝塞进随机的容器里，剩下的给电池或空 */
        int need = fusesNeeded;
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

    /* 配电箱：放在离出口 2~5 格的空位 */
        for (int t = 0; t < 256; t++) {
            int idx = rand() % emptyCount;
            float bx = cells[idx][0] + 0.5f, by = cells[idx][1] + 0.5f;
            float d = Dist2(bx, by, exitX, exitY);
            if (d < 4.0f || d > 30.0f) continue;
            if (!SpotClear(bx, by, 1.6f, 2, -1)) continue;
            breaker.x = bx;
            breaker.y = by;
            breaker.active = 1;
            break;
        }
    }
}


/* ------------------------------------------------------------------ */
/* 像素工具                                                            */
/* ------------------------------------------------------------------ */

static inline void PutPixel(int x, int y, unsigned int color) {
    if (x >= 0 && x < renderW && y >= 0 && y < renderH) pixels[y * renderW + x] = color;
}

static inline void PutPixelBlend(int x, int y, unsigned int color, float alpha) {
    if (x < 0 || x >= renderW || y < 0 || y >= renderH) return;
    unsigned int dst = pixels[y * renderW + x];
    int r = (int)(((color >> 16) & 0xFF) * alpha + ((dst >> 16) & 0xFF) * (1.0f - alpha));
    int g = (int)(((color >> 8) & 0xFF) * alpha + ((dst >> 8) & 0xFF) * (1.0f - alpha));
    int b = (int)((color & 0xFF) * alpha + (dst & 0xFF) * (1.0f - alpha));
    pixels[y * renderW + x] = (unsigned int)((r << 16) | (g << 8) | b);
}

/* 在**渲染坐标**上画一行 1:1 的点阵字（小地图的标题用）。
   DrawTextWin 用的是窗口坐标，这里是另一套坐标系，所以单独写一个。 */
static void PutTextRaw(int x, int y, const char* t, unsigned int color);
static int TextWidthRaw(const char* t);
static int UIS(int v);
#define FONT_H 12
#define FONT_W 8

/* ShowCursor 是引用计数的，跨状态切换很容易把引用数弄歪；
   这里用 GetCursorInfo 把光标状态直接拧到目标值 */
static void ForceCursor(int visible) {
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
}

/* ------------------------------------------------------------------ */
/* 3D 渲染                                                             */
/* ------------------------------------------------------------------ */

/* 把世界坐标投影成屏幕矩形（billboard） */
typedef struct { int ok; float ty; int x0, x1, y0, y1; } SpriteBox;

static SpriteBox ProjectSprite(float wx, float wy, float lift, float halfH) {
    SpriteBox s;
    s.ok = 0;
    float dx = wx - posX, dy = wy - posY;
    float invDet = 1.0f / (planeX * dirY - dirX * planeY);
    float tx = invDet * (dirY * dx - dirX * dy);
    float ty = invDet * (-planeY * dx + planeX * dy);
    if (ty <= 0.08f) return s;

    float unit = renderH / ty;                  /* 一个世界单位多少像素 */
    int cx = (int)((renderW / 2.0f) * (1.0f + tx / ty));
    int half = (int)(unit * 0.28f);
    int cyFloor = renderH / 2 + (int)(unit * 0.5f);   /* 地面所在的屏幕 y */
    int hgt = (int)(unit * halfH);
    int yTop = cyFloor - hgt;

    s.ty = ty;
    s.x0 = cx - half; if (s.x0 < 0) s.x0 = 0;
    s.x1 = cx + half; if (s.x1 >= renderW) s.x1 = renderW - 1;
    s.y0 = yTop + (int)(unit * lift); if (s.y0 < 0) s.y0 = 0;
    s.y1 = cyFloor; if (s.y1 >= renderH) s.y1 = renderH - 1;
    s.ok = (s.x1 > s.x0 && s.y1 > s.y0);
    return s;
}

static inline void PutPixelAdd(int x, int y, unsigned int color, float a);

/* ---------------- 精灵：统一的投影 + 排序绘制 ---------------- */

static void DrawCrateSprite(int i) {
    Container* c = &containers[i];
    SpriteBox s = ProjectSprite(c->x, c->y, 0.0f, 0.40f);
    if (!s.ok) return;

    float lightFactor = (flashlightOn && battery > 0.0f) ? 1.0f : 0.35f;
    int spanX = s.x1 - s.x0, spanY = s.y1 - s.y0;
    int searched = c->searched;
    float dyRel = 32.0f / (float)spanY;

    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        int relX = (x - s.x0) * 32 / spanX;
        float fy = 0.0f;
        unsigned int* col = pixels + (size_t)s.y0 * renderW + x;
        int wrote = 0;
        for (int y = s.y0; y < s.y1; y++, fy += dyRel, col += renderW) {
            int relY = (int)fy;
            unsigned int color = 0;
            if (relX > 3 && relX < 29 && relY > 3 && relY < 31) {
                color = ((relX / 6) % 2) ? 0x00462f13 : 0x003d2910;
                if (relX == 6 || relX == 13 || relX == 20 || relX == 27) color = 0x00221607;
                if (relY == 6 || relY == 18 || relY == 30) color = 0x00221607;
                if ((relX < 8 || relX > 24) && (relY < 10 || relY > 25)) color = 0x005e5e5e;
                if ((relX < 8 || relX > 24) && (relY < 10 || relY > 25) && (relX == 4 || relX == 28 || relY == 4)) color = 0x00343434;
                {
                    int d1 = relX - relY, d2 = (31 - relX) - relY;
                    if (d1 > -2 && d1 < 2) color = 0x00301f0d;
                    if (d2 > -2 && d2 < 2) color = 0x00301f0d;
                }
                if (relY < 10) {
                    if (!searched) color = (relY < 6) ? 0x00543a1a : 0x004a3416;
                    else color = (relY < 8) ? 0x002a1a0a : 0x000c0803;
                }
                if (searched && relX > 8 && relX < 24 && relY > 9 && relY < 20) {
                    color = 0x000c0803;
                    if (relX > 13 && relX < 19 && relY > 11 && relY < 17) {
                        if (c->content == 1) color = 0x00e8c040;
                        else if (c->content == 2) color = 0x0040c060;
                        else if (c->content == 3) color = 0x00d04040;
                        else if (c->content == 4) color = 0x00ffb050;
                        else if (c->content == 5) color = 0x00a0d0ff;
                    }
                }
            } else if (relX > 1 && relX < 31 && relY > 1 && relY < 32) {
                color = 0x00180f06;
            }
            if (!color) continue;
            float sh = (1.0f - s.ty / 15.0f) * lightFactor;
            if (sh < 0.15f) sh = 0.15f;
            int r = (int)(((color >> 16) & 0xFF) * sh);
            int g = (int)(((color >> 8) & 0xFF) * sh);
            int b = (int)((color & 0xFF) * sh);
            *col = (unsigned int)((r << 16) | (g << 8) | b);
            wrote = 1;
        }
        if (wrote && s.ty < zBuffer[x]) zBuffer[x] = s.ty;   /* 让后面的精灵被它遮挡 */
    }
}

static void DrawBreakerSprite(void) {
    if (!breaker.active) return;
    SpriteBox s = ProjectSprite(breaker.x, breaker.y, 0.24f, 0.34f);
    if (!s.ok) return;

    float lightFactor = (flashlightOn && battery > 0.0f) ? 1.0f : 0.35f;
    int spanX = s.x1 - s.x0, spanY = s.y1 - s.y0;
    float dyRel = 32.0f / (float)spanY;

    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        int relX = (x - s.x0) * 32 / spanX;
        float fy = 0.0f;
        unsigned int* col2 = pixels + (size_t)s.y0 * renderW + x;
        int wrote = 0;
        for (int y = s.y0; y < s.y1; y++, fy += dyRel, col2 += renderW) {
            int relY = (int)fy;
            unsigned int color = 0;
            if (relX > 5 && relX < 27 && relY > 2 && relY < 30) {
                color = 0x00606668;
                if (relX > 8 && relX < 24 && relY > 5 && relY < 20) color = 0x00404a4e;
                if (relX > 12 && relX < 20 && relY > 8 && relY < 14)
                    color = breakerOpen ? 0x0030ff60 : 0xff2828;
            } else if (relX > 3 && relX < 29 && relY > 1 && relY < 31) {
                color = 0x00202426;
            }
            if (!color) continue;
            float sh = (1.0f - s.ty / 15.0f) * lightFactor;
            if (sh < 0.2f) sh = 0.2f;
            if (color == 0x0030ff60 || color == 0xff2828) sh = 1.0f;
            int r = (int)(((color >> 16) & 0xFF) * sh);
            int g = (int)(((color >> 8) & 0xFF) * sh);
            int b = (int)((color & 0xFF) * sh);
            *col2 = (unsigned int)((r << 16) | (g << 8) | b);
            wrote = 1;
        }
        if (wrote && s.ty < zBuffer[x]) zBuffer[x] = s.ty;
    }
}

static void DrawBeastSprite(Monster* m, unsigned int eyeColor) {
    if (!m->active) return;
    SpriteBox s = ProjectSprite(m->x, m->y, 0.0f, 0.86f);
    if (!s.ok) return;

    int spanX = s.x1 - s.x0, spanY = s.y1 - s.y0;
    float dyRel = 32.0f / (float)spanY;
    float light = (flashlightOn && battery > 0.0f) ? 1.0f : 0.55f;

    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        int relX = (x - s.x0) * 32 / spanX;
        float fy = 0.0f;
        unsigned int* col = pixels + (size_t)s.y0 * renderW + x;
        int wrote = 0;
        for (int y = s.y0; y < s.y1; y++, fy += dyRel, col += renderW) {
            int relY = (int)fy;
            unsigned int color = 0;
            int eye = 0;
            int headW = 4, bodyW;
            if (relY < 11) bodyW = headW + (relY / 4);
            else if (relY < 18) bodyW = 8;
            else bodyW = 6;
            int cx = 16;
            if (relY > 1 && relY < 31 && relX > cx - bodyW && relX < cx + bodyW) {
                color = 0x00141418;
                if (relX <= cx - bodyW + 1 || relX >= cx + bodyW - 1) color = 0x000a0a0c;
                if (relY > 4 && relY < 8) {
                    if ((relX > cx - 4 && relX < cx - 1) || (relX > cx + 1 && relX < cx + 4)) {
                        color = 0x00ff3020;
                        eye = 1;
                    }
                }
            }
            if (!color) continue;
            if (eye) {
                PutPixelAdd(x, y, eyeColor, 0.85f);
            } else {
                float sh = (1.0f - s.ty / 16.0f) * light;
                if (sh < 0.12f) sh = 0.12f;
                int r = (int)(((color >> 16) & 0xFF) * sh);
                int g = (int)(((color >> 8) & 0xFF) * sh);
                int b = (int)((color & 0xFF) * sh);
                /* 硬直期间身体发白 —— 玩家一眼看出「我砸中了它」。
                   这是砸中唯一可靠的视觉反馈（音效只是一瞬间）。 */
                if (m->stun > 0.0f) {
                    float k = m->stun / MONSTER_STUN;
                    if (k > 1.0f) k = 1.0f;
                    r = r + (int)((235 - r) * k);
                    g = g + (int)((245 - g) * k);
                    b = b + (int)((255 - b) * k);
                }
                *col = (unsigned int)((r << 16) | (g << 8) | b);
            }
            wrote = 1;
        }
        if (wrote && s.ty < zBuffer[x]) zBuffer[x] = s.ty;
    }
}

static void DrawMonsterSprite(void) { DrawBeastSprite(&monster, 0x00ff4020u); }
static void DrawLightBeastSprite(void) { DrawBeastSprite(&lightBeast, 0x00c8f0ffu); }

/* 跳脸虫的脸：预渲染成一张 128x128 的纹理，每帧只做采样 + 缩放。
   逐像素现画的话，800x600 实测 11ms/帧 —— QTE 是实时反应，
   掉帧就等于「玩家看不清提示」，所以必须预渲染。

   纹理格式：高字节 = 颜色索引（1..6），低 24 位 = 原始 RGB。
   每帧要按「逼近程度」整体调亮度，所以查表 g_jumperShade[色][亮度档]，
   避免逐像素做浮点乘加（脸几乎占满屏幕，这是热路径上的主要开销）。 */
#define JUMPER_TEX_SIZE 128
#define JMP_COL_EDGE  1      /* 外缘 */
#define JMP_COL_SKIN  2      /* 皮肤 */
#define JMP_COL_DEEP  3      /* 皮肤深色 */
#define JMP_COL_MOUTH 4      /* 口腔 */
#define JMP_COL_TOOTH 5      /* 牙 */
#define JMP_COL_EYE   6      /* 眼睛（单独处理亮度） */
#define JMP_SHADE_STEPS 64   /* 亮度档数 */

static unsigned int g_jumperTex[JUMPER_TEX_SIZE * JUMPER_TEX_SIZE];
static unsigned int g_jumperShade[8][JMP_SHADE_STEPS];
static int g_jumperTexReady = 0;

static void BuildJumperTexture(void) {
    if (g_jumperTexReady) return;
    int S = JUMPER_TEX_SIZE;
    float c = (S - 1) * 0.5f;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float dx = ((float)x - c) / c;
            float dy = ((float)y - c) / c;
            float d2 = dx * dx + dy * dy;
            unsigned int col = 0;
            unsigned int idx = 0;
            if (d2 <= 1.0f) {
                if (d2 > 0.7396f) { col = 0x00140c08; idx = JMP_COL_EDGE; }   /* 外缘 */
                else if (d2 > 0.5184f) { col = 0x002c1a12; idx = JMP_COL_SKIN; }
                else { col = 0x003c2418; idx = JMP_COL_DEEP; }

                /* 嘴：中央的大裂口 */
                float mdy = (dy - 0.12f) * 2.3f;
                float md2 = dx * dx + mdy * mdy;
                if (md2 < 0.3025f && dy > -0.15f) {
                    if (md2 < 0.1764f) { col = 0x00000000; idx = JMP_COL_MOUTH; }  /* 口腔全黑 */
                    else { col = 0x00201010; idx = JMP_COL_MOUTH; }
                    /* 牙：上下两排锯齿 */
                    if (md2 > 0.16f) {
                        int toothRow = (int)((dy + 0.15f) * 40.0f);
                        if ((toothRow & 1) == 0) { col = 0x00d8d0b0; idx = JMP_COL_TOOTH; }
                    }
                }

                /* 眼睛：两个发光的点 */
                float edy = (dy + 0.42f) * 1.5f;
                float edy2 = edy * edy;
                float ex1 = dx + 0.34f, ex2 = dx - 0.34f;
                float e1sq = ex1 * ex1 + edy2;
                float e2sq = ex2 * ex2 + edy2;
                if (e1sq < 0.0169f || e2sq < 0.0169f) { col = 0x00ff0000u; idx = JMP_COL_EYE; }
            }
            g_jumperTex[(size_t)y * S + x] = (idx << 24) | (col & 0x00FFFFFFu);
        }
    }

    /* 亮度查表：色 × 亮度档。
       亮度因子 = 0.45 + 0.55*t，t 在 0..1，分成 64 档。 */
    {
        static const unsigned int base[8] = {
            0,
            0x00140c08,   /* EDGE */
            0x002c1a12,   /* SKIN */
            0x003c2418,   /* DEEP */
            0x00000000,   /* MOUTH（黑） */
            0x00d8d0b0,   /* TOOTH */
            0x00ff0000,   /* EYE（用不到，这里只是占位） */
            0
        };
        for (int ci = 0; ci < 8; ci++) {
            for (int s = 0; s < JMP_SHADE_STEPS; s++) {
                float t = (float)s / (float)(JMP_SHADE_STEPS - 1);
                float k = 0.45f + 0.55f * t;
                unsigned int b = base[ci];
                int r = (int)(((b >> 16) & 0xFF) * k);
                int g = (int)(((b >> 8) & 0xFF) * k);
                int bl = (int)((b & 0xFF) * k);
                g_jumperShade[ci][s] = (unsigned int)((r << 16) | (g << 8) | bl);
            }
        }
    }
    g_jumperTexReady = 1;
}

/* 跳脸虫：它不是世界里的一个 billboard，而是**扑向镜头的东西**。
   所以用屏幕空间直接绘制：以画面中心为基准，尺寸随逼近程度放大，
   阶段二（跿上脸）时几乎占满整个屏幕 —— 这是它最吓人的瞬间。 */
static void DrawParasiteSprite(void) {
    if (!parasite.active) return;
    BuildJumperTexture();

    /* 逼近程度 0..1：ramp 在阶段一被用作进度，阶段二恒为 1 */
    float t = parasite.ramp;
    if (jumperPhase == JMP_DODGE || jumperPhase == JMP_QTE) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    /* 亮度查表的下标（每帧算一次，不是每像素） */
    int shadeIdx = (int)(t * (float)(JMP_SHADE_STEPS - 1) + 0.5f);
    if (shadeIdx < 0) shadeIdx = 0;
    if (shadeIdx >= JMP_SHADE_STEPS) shadeIdx = JMP_SHADE_STEPS - 1;
    int eyeG = 90 + (int)(165.0f * t);
    if (eyeG > 255) eyeG = 255;

    /* 尺寸：阶段一从小到大，阶段二几乎铺满屏幕 */
    float sizeF;
    if (jumperPhase == JMP_QTE) sizeF = 1.15f;
    else if (jumperPhase == JMP_DODGE) sizeF = 0.55f + 0.35f * t;
    else sizeF = 0.18f + 0.37f * t;

    int half = (int)((float)(renderH < renderW ? renderH : renderW) * 0.42f * sizeF);
    if (half < 4) half = 4;
    if (half * 2 >= renderH) half = renderH / 2 - 1;

    int cx = renderW / 2;
    int cy = renderH / 2;

    /* 抖动：逼近时疯狂抖动，制造不安感 */
    float jitterAmp = (jumperPhase == JMP_QTE) ? 7.0f : (jumperPhase == JMP_DODGE ? 4.0f : 1.5f);
    float jx = sinf((float)g_timeSec * 47.0f) * jitterAmp;
    float jy = sinf((float)g_timeSec * 39.0f) * jitterAmp;

    /* 呼吸：整体缓慢缩放，像在喘气 */
    float breathe = 1.0f + sinf((float)g_timeSec * 12.0f) * 0.05f * (0.4f + t);
    int h = (int)(half * breathe);
    if (h < 3) h = 3;

    /* ------------------------------------------------------------ */
    /* 关键：脸是**预渲染**的纹理，不是逐像素算出来的。            */
    /* 逐像素画一张脸要 15 次浮点乘加，800x600 下实测 11ms/帧 —— */
    /* QTE 是实时反应，掉帧就等于「玩家看不见提示」。              */
    /* 改成：纹理只画一次（64x64），每帧只是采样 + 缩放。          */
    /* ------------------------------------------------------------ */
    const unsigned int* src = g_jumperTex;
    int sw = JUMPER_TEX_SIZE;

    /* 纹理坐标范围：从左上角覆盖到右下角。
       注意是**双向缩放**：源纹理 128x128 要放大成 (2h+1) 见方，
       所以必须遍历**目标**像素、反查源纹理。
       早先写成「遍历源像素、每源像素只画 1 个目标点」，
       画面只剩 1/81 的内容 —— 性能指标很漂亮，画面全黑。
       **这就是「光看性能提升不够」的又一个例子。**

       性能：逐像素做 (dx*sw)/side 整数除法，578 见方就是 33 万次除法，
       实测 4.3ms —— 除法是瓶颈。改成：
         1) 定点数增量代替除法（sx16 每步加固定值，右移取整）
         2) 按圆形轮廓裁剪每行的 x 范围（圆外整段跳过） */
    int side = 2 * h + 1;
    int x0 = cx - h;
    int y0 = cy - h;
    /* 抖动：整块纹理偏移，不逐像素算 */
    x0 += (int)jx;
    y0 += (int)jy;

    /* 定点步长：每前进一个目标像素，源纹理坐标前进 (sw/side) */
    int step16 = (sw << 16) / side;
    if (step16 < 1) step16 = 1;

    /* 纹理空间的圆形轮廓参数 */
    float tc = (sw - 1) * 0.5f;
    float tr2 = tc * tc;

    for (int dy = 0; dy < side; dy++) {
        int py = y0 + dy;
        if (py < 0 || py >= renderH) continue;

        /* 反查源纹理行 */
        int sy = (dy * sw) / side;
        const unsigned int* srow = src + (size_t)sy * sw;

        /* 按圆裁剪这一行的 x 范围：
           源行相对圆心的竖直偏移 -> 允许的半宽 -> 换算成目标 x 范围 */
        float dyS = (float)sy - tc;
        float rem = tr2 - dyS * dyS;
        int dxLo = 0, dxHi = side - 1;
        if (rem > 0.0f) {
            float halfW = sqrtf(rem);
            float sxLo = tc - halfW, sxHi = tc + halfW;
            dxLo = (int)(sxLo * (float)side / (float)sw);
            dxHi = (int)(sxHi * (float)side / (float)sw);
            if (dxLo < 0) dxLo = 0;
            if (dxHi > side - 1) dxHi = side - 1;
        }

        unsigned int* row = pixels + (size_t)py * renderW;
        /* 定点源坐标：从 dxLo 对应的源位置开始 */
        int sx16 = (dxLo * sw / side) << 16;

        for (int dx = dxLo; dx <= dxHi; dx++) {
            int px = x0 + dx;
            if (px < 0 || px >= renderW) { sx16 += step16; continue; }
            unsigned int c = srow[sx16 >> 16];
            sx16 += step16;
            unsigned int ci = c >> 24;
            if (!ci) continue;                  /* 透明 */

            /* 眼睛要随逼近程度变亮：单独处理 */
            if (ci == JMP_COL_EYE) {
                row[px] = (unsigned int)(eyeG << 16);   /* 血红的眼睛 */
                continue;
            }
            /* 其余颜色走亮度查表 */
            row[px] = g_jumperShade[ci][shadeIdx];
        }
    }
}

/* 荧光棒：扔在地上持续发光。
   注意：玻璃瓶曾经也会进这个数组（type==1，画成碎片），
   现在瓶子改成了飞行弹丸（shot[]），这条路径已经不存在了。 */
static void DrawFlareSprite(int idx) {
    if (!thrown[idx].active) return;
    SpriteBox s = ProjectSprite(thrown[idx].x, thrown[idx].y, 0.0f, 0.30f);
    if (!s.ok) return;
    int spanX = s.x1 - s.x0;
    int spanY = s.y1 - s.y0;

    /* 荧光棒：三层光晕 + 慢速闪烁 */
    float flick = 0.82f + 0.18f * sinf((float)g_timeSec * 9.0f);
    float cx = spanX * 0.5f, cy = spanY * 0.55f;
    float rad = spanX * 0.95f;
    if (rad < 4.0f) rad = 4.0f;
    int R = (int)rad;
    for (int dy = -R; dy <= R; dy++) {
        int y = (int)(s.y0 + cy) + dy;
        if (y < 0 || y >= renderH) continue;
        for (int dx = -R; dx <= R; dx++) {
            int x = (int)(s.x0 + cx) + dx;
            if (x < 0 || x >= renderW) continue;
            if (s.ty >= zBuffer[x]) continue;
            float d2 = (float)(dx * dx + dy * dy) / (rad * rad);
            if (d2 >= 1.0f) continue;
            float a = 1.0f - d2;
            /* 核心最亮，外圈是暖橙色雾 */
            PutPixelAdd(x, y, 0x00ffd070u, a * a * a * 1.15f * flick);
            if (d2 < 0.16f) PutPixelAdd(x, y, 0x00fff8e0u, 0.55f * flick);
        }
    }
}

/* 飞行中的玻璃瓶：一小块冷色高光 + 一点拖影。
   它必须在画面上可见 —— 玩家要能判断「我这一瓶往哪飞」。 */
static void DrawShotSprite(int idx) {
    if (!shot[idx].active) return;
    SpriteBox s = ProjectSprite(shot[idx].x, shot[idx].y, 0.10f, 0.10f);
    if (!s.ok) return;

    int cx = (s.x0 + s.x1) / 2;
    int cy = (s.y0 + s.y1) / 2;
    float light = (flashlightOn && battery > 0.0f) ? 1.0f : 0.6f;

    /* 瓶身：一个 3x3 的亮色块 */
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
            if (s.ty >= zBuffer[x]) continue;
            PutPixelAdd(x, y, 0x00bfe8ffu, 0.85f * light);
        }
    }
    /* 中心更亮 */
    if (cx >= 0 && cx < renderW && cy >= 0 && cy < renderH && s.ty < zBuffer[cx])
        PutPixelAdd(cx, cy, 0x00ffffffu, 0.95f * light);
}

static void DrawFlowerSprite(void) {
    if (!safeRoom.active) return;
    SpriteBox s = ProjectSprite(safeRoom.x, safeRoom.y, 0.0f, 0.42f);
    if (!s.ok) return;
    int spanX = s.x1 - s.x0, spanY = s.y1 - s.y0;
    float dyRel = 32.0f / (float)spanY;
    float pulse = 0.75f + 0.25f * sinf((float)g_timeSec * 2.0f);
    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        int relX = (x - s.x0) * 32 / spanX;
        float fy = 0.0f;
        unsigned int* col = pixels + (size_t)s.y0 * renderW + x;
        int wrote = 0;
        for (int y = s.y0; y < s.y1; y++, fy += dyRel, col += renderW) {
            int relY = (int)fy;
            /* 茎 */
            if (relX > 15 && relX < 18 && relY > 14) { *col = 0x00305020; wrote = 1; continue; }
            /* 花瓣 */
            int dx = relX - 16, dy = relY - 11;
            int d2 = dx * dx + dy * dy;
            if (d2 < 30) {
                if (d2 < 8) PutPixelAdd(x, y, 0x00fff0a0u, 0.9f * pulse);
                else PutPixelAdd(x, y, 0x00e060a0u, 0.55f * pulse);
                wrote = 1;
            }
        }
        if (wrote && s.ty < zBuffer[x]) zBuffer[x] = s.ty;
    }
}

static void DrawExitBeamSprite(void) {
    SpriteBox s = ProjectSprite(exitX, exitY, 0.0f, 1.05f);
    if (!s.ok) return;
    int spanX = s.x1 - s.x0;
    int spanY = s.y1 - s.y0;
    if (spanX <= 0 || spanY <= 0) return;

    for (int x = s.x0; x < s.x1; x++) {
        if (s.ty >= zBuffer[x]) continue;
        float fx = (float)(x - s.x0) / (float)spanX;
        float edge = 1.0f - fabsf(fx - 0.5f) * 2.0f;
        if (edge <= 0.0f) continue;
        for (int y = s.y0; y < s.y1; y++) {
            float fy = (float)(y - s.y0) / (float)spanY;
            float a = edge * edge * (0.30f + 0.70f * fy);
            if (breakerOpen) PutPixelAdd(x, y, 0x0040FF70u, a * 0.50f);
            else if (edge > 0.80f) PutPixelAdd(x, y, 0x00C03030u, a * 0.28f);
        }
    }
}

/* 所有 billboard 按距离从远到近画，近的盖住远的（画家算法）
   数组上界 MAX_SPRITES = 容器 + 投掷物 + 配电箱 + 两只怪 + 安全屋 + 出口光柱，
   写满时会被下面的断言拦下来，不会像以前那样悄悄越界。 */
void RenderSprites(void) {
    struct { float d2; int kind, idx; } list[MAX_SPRITES];
    int n = 0;

    for (int i = 0; i < containerCount; i++) {
        if (!containers[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(containers[i].x, containers[i].y, posX, posY);
        list[n].kind = 0;
        list[n].idx = i;
        n++;
    }
    if (breaker.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(breaker.x, breaker.y, posX, posY);
            list[n].kind = 1;
            list[n].idx = 0;
            n++;
        }
    }
    if (monster.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(monster.x, monster.y, posX, posY);
            list[n].kind = 2;
            list[n].idx = 0;
            n++;
        }
    }
    if (safeRoom.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(safeRoom.x, safeRoom.y, posX, posY);
            list[n].kind = 7;
            list[n].idx = 0;
            n++;
        }
    }
    if (lightBeast.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(lightBeast.x, lightBeast.y, posX, posY);
            list[n].kind = 5;
            list[n].idx = 0;
            n++;
        }
    }
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(thrown[i].x, thrown[i].y, posX, posY);
        list[n].kind = 6;
        list[n].idx = i;
        n++;
    }
    /* 飞行中的玻璃瓶 */
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!shot[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(shot[i].x, shot[i].y, posX, posY);
        list[n].kind = 8;
        list[n].idx = i;
        n++;
    }
    /* 跳脸虫不在这个表里 —— 它是屏幕空间的 overlay，
       要盖住整个场景，由 RenderFrame 直接绘制。 */
    if (n < MAX_SPRITES) {
        list[n].d2 = Dist2(exitX, exitY, posX, posY);
        list[n].kind = 3;
        list[n].idx = 0;
        n++;
    }

    /* 插入排序：远的先画 */
    for (int i = 1; i < n; i++) {
        int j = i - 1;
        float d2 = list[i].d2;
        int kind = list[i].kind, idx = list[i].idx;
        while (j >= 0 && list[j].d2 < d2) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1].d2 = d2;
        list[j + 1].kind = kind;
        list[j + 1].idx = idx;
    }

    for (int i = 0; i < n; i++) {
        switch (list[i].kind) {
            case 0: DrawCrateSprite(list[i].idx); break;
            case 1: DrawBreakerSprite(); break;
            case 2: DrawMonsterSprite(); break;
            case 5: DrawLightBeastSprite(); break;
            case 6: DrawFlareSprite(list[i].idx); break;
            case 8: DrawShotSprite(list[i].idx); break;
            case 7: DrawFlowerSprite(); break;
            default: DrawExitBeamSprite(); break;
        }
    }
}


/* ------------------------------------------------------------------ */
/* 并行渲染：画面切成横带，主线程 + 工作线程一起画                       */
/* ------------------------------------------------------------------ */

#define MAX_PARTS 9   /* 1 主线程 + 最多 8 个工作线程 */

typedef struct {
    HANDLE hThread;
    HANDLE hStart;   /* 自动复位：主线程派活 */
    HANDLE hDone;    /* 自动复位：工作线程完工 */
    int y0, y1;      /* 地板 / 后处理的行范围 */
    int x0, x1;      /* 墙体的列范围 */
    int phase;       /* 1=地板 2=墙体 3=后处理 */
} Worker;

static Worker g_workers[MAX_PARTS];
static int g_workerCount = 0;   /* 不含主线程 */
static int g_parts = 1;
static volatile LONG g_stop = 0;

/* 亮度 → gamma 校正后的亮度，查表代替逐像素 powf。
   索引是「亮度 × 256」，值域 0..900（对应亮度 0..3.5）。
   地板与墙体都走这张表，gamma 滑块对整个画面生效。 */
#define SHADE_LUT_MAX 1024
static int shadeLut[SHADE_LUT_MAX];

static void RenderFloorBand(int y0, int y1);
static void RenderWallBand(int x0, int x1);
static void RenderPostBand(int y0, int y1);

static DWORD WINAPI WorkerMain(LPVOID param) {
    Worker* w = (Worker*)param;
    for (;;) {
        if (WaitForSingleObject(w->hStart, INFINITE) != WAIT_OBJECT_0) break;
        if (g_stop) break;
        if (w->phase == 1) RenderFloorBand(w->y0, w->y1);
        else if (w->phase == 2) RenderWallBand(w->x0, w->x1);
        else if (w->phase == 3) RenderPostBand(w->y0, w->y1);
        SetEvent(w->hDone);
    }
    return 0;
}

void InitWorkers(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int want = (int)si.dwNumberOfProcessors - 1;
    if (want > MAX_PARTS - 1) want = MAX_PARTS - 1;
    if (want < 0) want = 0;

    g_workerCount = 0;
    g_stop = 0;   /* 允许重复初始化（例如重启线程池） */
    for (int i = 0; i < want; i++) {
        Worker* w = &g_workers[i];
        w->hStart = CreateEvent(NULL, FALSE, FALSE, NULL);
        w->hDone = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!w->hStart || !w->hDone) break;
        w->phase = 0;
        w->hThread = CreateThread(NULL, 0, WorkerMain, w, 0, NULL);
        if (!w->hThread) break;
        g_workerCount++;
    }
    g_parts = g_workerCount + 1;
}

void ShutdownWorkers(void) {
    g_stop = 1;
    for (int i = 0; i < g_workerCount; i++) SetEvent(g_workers[i].hStart);
    for (int i = 0; i < g_workerCount; i++) {
        WaitForSingleObject(g_workers[i].hThread, 1000);
        CloseHandle(g_workers[i].hThread);
        CloseHandle(g_workers[i].hStart);
        CloseHandle(g_workers[i].hDone);
    }
    g_workerCount = 0;
    g_parts = 1;
}

static void SplitRange(int lo, int hi, int part, int parts, int* outLo, int* outHi) {
    int n = hi - lo;
    if (n < 0) n = 0;
    *outLo = lo + n * part / parts;
    *outHi = lo + n * (part + 1) / parts;
}

/* 派活并等所有分片画完；worker 数可以是 0（单线程退化） */
static void RunPhase(int phase) {
    int y0, y1;
    for (int i = 0; i < g_workerCount; i++) {
        if (phase == 1) { SplitRange(renderH / 2 + 1, renderH, i, g_parts, &g_workers[i].y0, &g_workers[i].y1); }
        else if (phase == 2) { SplitRange(0, renderW, i, g_parts, &g_workers[i].x0, &g_workers[i].x1); }
        else { SplitRange(0, renderH, i, g_parts, &g_workers[i].y0, &g_workers[i].y1); }
        g_workers[i].phase = phase;
        SetEvent(g_workers[i].hStart);
    }

    int p = g_workerCount;
    if (phase == 1) { SplitRange(renderH / 2 + 1, renderH, p, g_parts, &y0, &y1); RenderFloorBand(y0, y1); }
    else if (phase == 2) { int x0, x1; SplitRange(0, renderW, p, g_parts, &x0, &x1); RenderWallBand(x0, x1); }
    else { SplitRange(0, renderH, p, g_parts, &y0, &y1); RenderPostBand(y0, y1); }

    for (int i = 0; i < g_workerCount; i++) {
        /* 超时也要继续，宁可掉一帧也不能卡死 */
        WaitForSingleObject(g_workers[i].hDone, 300);
    }
}

/* 亮度 → gamma 校正后的亮度，查表代替逐像素 powf。
   索引是「亮度 × 256」，值域 0..1023（对应亮度 0..4.0）。

   曲线说明：直接用 pow(v, 1/g) 会踩一个坑 ——
   v > 1（过曝区，比如手电近处的地板）时，1/g < 1 会把值「压回来」，
   于是 gamma 调大反而让亮的地方变暗，和「数值越大越亮」的预期相反。

   修复方式：**只动 v > 1 那一段**，v <= 1 保持原曲线不变：
     - v <= 1：out = pow(v, 1/g) × 256      （与原实现完全一致）
     - v >  1：原样输出（过曝部分不再被压暗）
   这样既修好了过曝区被压暗的问题，又不会顺带改变墙体的既有观感 ——
   墙体亮度 lit 通常在 0.1~1.5 之间，那一段曲线必须原封不动。 */
#define SHADE_ONE 256      /* 亮度 1.0 对应的索引 */

static void BuildShadeTables(void) {
    float inv = 1.0f / gammaValue;
    for (int i = 0; i < SHADE_LUT_MAX; i++) {
        int o;
        if (i <= SHADE_ONE) {
            float v = (float)i / 256.0f;
            float g = powf(v, inv);
            o = (int)(g * 256.0f + 0.5f);
        } else {
            o = i;   /* 过曝区原样保留 */
        }
        if (o < 0) o = 0;
        if (o > SHADE_LUT_MAX - 1) o = SHADE_LUT_MAX - 1;
        shadeLut[i] = o;
    }
}

/* 地板：棋盘只有两种底色，亮度查表；出口发光只在出口所在格判断 */
static void RenderFloorBand(int y0, int y1) {
    int halfH = renderH / 2;
    float rayDirX0 = dirX - planeX, rayDirY0 = dirY - planeY;
    float rayDirX1 = dirX + planeX, rayDirY1 = dirY + planeY;
    int exitBX = (int)(exitX * 4.0f), exitBY = (int)(exitY * 4.0f);
    int safeBX = (int)(safeRoom.x * 4.0f), safeBY = (int)(safeRoom.y * 4.0f);

    for (int y = y0; y < y1; y++) {
        float rowDistance = (0.5f * renderH) / (float)(y - halfH);
        float stepX = rowDistance * (rayDirX1 - rayDirX0) / renderW;
        float stepY = rowDistance * (rayDirY1 - rayDirY0) / renderW;
        float fx = posX + rowDistance * rayDirX0;
        float fy = posY + rowDistance * rayDirY0;
        float shadow = 1.0f - rowDistance / 10.0f;
        if (shadow < 0.0f) shadow = 0.0f;
        unsigned int* row = pixels + (size_t)y * renderW;

        for (int x = 0; x < renderW; x++) {
            /* 地板与天花板都采样真纹理；亮度按列/行预乘 */
            int cX = (int)fx, cY = (int)fy;
            int tx2 = ((int)((fx - cX) * (float)TEX_WIDTH)) & (TEX_WIDTH - 1);
            int ty2 = ((int)((fy - cY) * (float)TEX_HEIGHT)) & (TEX_HEIGHT - 1);
            tx2 &= ~1; ty2 &= ~1;

            /* 亮度：shadow（行常量）× floorGain（列常量）→ k8 → gamma → 等级 */
            float bright = shadow * floorGain[x];
            int k8 = (int)(bright * 256.0f);
            if (k8 < 0) k8 = 0; else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
            k8 = shadeLut[k8];      /* gamma 校正：地板也走查表 */
            int lvl = k8 >> PREMUL_K8_SHIFT;   /* 0..255 */

            /* 查预乘表：省掉 3 次乘法 + 3 次截断 */
            unsigned int color = premulFloor[floorColorIdx[ty2][tx2]][lvl];

            /* 天花板：镜像行
               原版是 (通道 * ck) >> 8，ck 是 0..255 的乘数；
               表里用 k8 量纲（等级 l 对应乘数 l*4），所以等级 = ck >> 2。 */
            {
                int my2 = renderH - y - 1;
                if (my2 >= 0 && my2 < renderH) {
                    int cl = ceilTab[x] >> PREMUL_K8_SHIFT;
                    if (cl > PREMUL_LEVELS - 1) cl = PREMUL_LEVELS - 1;
                    pixels[(size_t)my2 * renderW + x] = premulCeil[ceilColorIdx[ty2][tx2]][cl];
                }
            }

            /* 安全屋：暖色花圃（只有附近才需要格子坐标） */
            if (safeRoom.active) {
                int bx = (int)(fx * 4.0f), by = (int)(fy * 4.0f);
                int sbx = bx - safeBX, sby = by - safeBY;
                if (sbx >= -2 && sbx <= 2 && sby >= -2 && sby <= 2) {
                    float d2s = (float)(sbx * sbx + sby * sby) / 8.0f;
                    float gs = 1.0f - d2s;
                    if (gs > 0.0f) {
                        int r = (int)(70 * gs), g = (int)(48 * gs), b = (int)(58 * gs);
                        color = (unsigned int)((r << 16) | (g << 8) | b);
                    }
                }
            }

            /* 出口：一个 1.5 格见方的光池，通电才是绿色 */
            {
                int bx = (int)(fx * 4.0f), by = (int)(fy * 4.0f);
                int dbx = bx - exitBX, dby = by - exitBY;
                if (dbx >= -3 && dbx <= 3 && dby >= -3 && dby <= 3) {
                    float d2 = (float)(dbx * dbx + dby * dby) / 9.0f;
                    float glow = 1.0f - d2;
                    if (glow > 0.0f) {
                        float k = (shadow * 0.45f + 0.60f) * glow;
                        if (breakerOpen) {
                            int r = (int)(40 * k), g = (int)(235 * k), b = (int)(105 * k);
                            if (r > 255) r = 255;
                            if (g > 255) g = 255;
                            if (b > 255) b = 255;
                            color = (unsigned int)((r << 16) | (g << 8) | b);
                        } else {
                            int r = (int)(120 * k), g = (int)(24 * k);
                            if (r > 255) r = 255;
                            color = (unsigned int)((r << 16) | (g << 8));
                        }
                    }
                }
            }
            row[x] = color;
            fx += stepX;
            fy += stepY;
        }
    }
}

/* 墙体：DDA 求交后整列查表，内层只有一次乘法和一次写 */
static void RenderWallBand(int x0, int x1) {
    int halfH = renderH / 2;
    for (int x = x0; x < x1; x++) {
        float cameraX = 2.0f * x / (float)renderW - 1.0f;
        float rayDirX = dirX + planeX * cameraX;
        float rayDirY = dirY + planeY * cameraX;

        int mapX = (int)posX, mapY = (int)posY;
        float deltaDistX = (rayDirX == 0) ? 1e30f : fabsf(1.0f / rayDirX);
        float deltaDistY = (rayDirY == 0) ? 1e30f : fabsf(1.0f / rayDirY);
        float sideDistX, sideDistY;
        int stepX, stepY, hit = 0, side = 0;

        if (rayDirX < 0) { stepX = -1; sideDistX = (posX - mapX) * deltaDistX; }
        else { stepX = 1; sideDistX = (mapX + 1.0f - posX) * deltaDistX; }
        if (rayDirY < 0) { stepY = -1; sideDistY = (posY - mapY) * deltaDistY; }
        else { stepY = 1; sideDistY = (mapY + 1.0f - posY) * deltaDistY; }

        while (hit == 0) {
            if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
            else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
            if (mapX < 0 || mapX >= MAP_WIDTH || mapY < 0 || mapY >= MAP_HEIGHT) break;
            if (worldMap[mapY][mapX] > 0) hit = 1;
        }

        float perpWallDist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
        if (perpWallDist < 0.1f) perpWallDist = 0.1f;
        zBuffer[x] = perpWallDist;

        int lineHeight = (int)(renderH / perpWallDist);
        int drawStart = -lineHeight / 2 + halfH;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + halfH;
        if (drawEnd > renderH) drawEnd = renderH;
        if (drawEnd <= drawStart) continue;

        float wallX = (side == 0) ? (posY + perpWallDist * rayDirY) : (posX + perpWallDist * rayDirX);
        wallX -= floorf(wallX);
        int texX = (int)(wallX * (float)TEX_WIDTH);
        if (texX < 0) texX = 0;
        if (texX > TEX_WIDTH - 1) texX = TEX_WIDTH - 1;
        if ((side == 0 && rayDirX > 0) || (side == 1 && rayDirY < 0)) texX = TEX_WIDTH - texX - 1;

        float step = (float)TEX_HEIGHT / lineHeight;
        float texPos = (drawStart - halfH + lineHeight / 2.0f) * step;

        /* 亮度 = 距离衰减 × 手电；手电中心再补一层白，墙才「亮得起来」 */
        float shadow = 1.0f - perpWallDist / 12.0f;
        if (shadow < 0.0f) shadow = 0.0f;
        if (side == 1) shadow *= 0.62f;
        float lit = shadow * flashTab[x];
        float flash = flashTab[x];
        float cone = flash > 0.45f ? (flash - 0.45f) : 0.0f;
        int addR = (int)(cone * shadow * 150.0f);
        int addG = (int)(cone * shadow * 128.0f);
        int addB = (int)(cone * shadow * 106.0f);
        int k8 = (int)(lit * 256.0f);
        if (k8 < 0) k8 = 0;
        else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
        k8 = shadeLut[k8];      /* gamma 校正，和地板共用同一张表 */

        const unsigned int* src = &texture[0][texX];
        unsigned int* dst = pixels + (size_t)drawStart * renderW + x;
        for (int y = drawStart; y < drawEnd; y++) {
            int texY = ((int)texPos) & (TEX_HEIGHT - 1);
            texPos += step;
            unsigned int t = src[(texY << 6)];
            int r = (int)(((t >> 16) & 0xFF) * k8) >> 8;
            int g = (int)(((t >> 8) & 0xFF) * k8) >> 8;
            int b = (int)((t & 0xFF) * k8) >> 8;
            r += addR; g += addG; b += addB;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            *dst = (unsigned int)((r << 16) | (g << 8) | b);
            dst += renderW;
        }
    }
}

/* 后处理：低理智红边 + 回血白闪，按行并行
   这一段只在理智 < 50 时才跑，但跑起来是全屏逐像素的。
   优化点：把「乘强度」和「除 300」从每像素挪到每帧一次（各 256 项的查表），
   顺便用递增代替乘 3 来定位噪点。 */
static void RenderPostBand(int y0, int y1) {
    int intensity = 0;
    if (vignetteOn && sanity < 50.0f) {
        intensity = (int)((50.0f - sanity) / 50.0f * 255.0f);
        if (intensity > 255) intensity = 255;
        if (intensity < 0) intensity = 0;
    }
    int heal = (healFlash > 0.0f);
    int frameSeed = (int)(g_timeSec * 61.0f) * 17;

    /* 每帧一次的查表：alpha = mask*intensity>>8，nz = (noise-20)*intensity/300 */
    unsigned char alphaLut[256];
    signed char noiseLut[256];
    if (intensity > 0) {
        for (int i = 0; i < 256; i++) {
            alphaLut[i] = (unsigned char)((i * intensity) >> 8);
            int nz = (int)noiseTab[i] - 20;
            noiseLut[i] = (signed char)(nz * intensity / 300);
        }
    }

    for (int y = y0; y < y1; y++) {
        unsigned int* row = pixels + (size_t)y * renderW;
        if (intensity > 0) {
            const unsigned char* mask = vignetteMask + (size_t)y * renderW;
            /* 噪点索引必须精确等于 (x*3 + y*5 + frameSeed) & 255。
               不能用「每列 +3」代替 —— x*3 超过 255 后要回绕，
               递增法在回绕点对不齐，噪点会整片错位。
               所以这里保留 x*3 的乘法，但把「乘强度/除 300」挪到查表。 */
            const int noiseBase = (y * 5 + frameSeed) & 255;
            for (int x = 0; x < renderW; x++) {
                int maskVal = mask[x];
                if (maskVal == 0) continue;
                int alpha = alphaLut[maskVal];
                if (alpha == 0) continue;
                unsigned int dst = row[x];
                int r = (int)((dst >> 16) & 0xFF), g = (int)((dst >> 8) & 0xFF), b = (int)(dst & 0xFF);
                r = r + (((255 - r) * alpha) >> 8);
                g = g - ((g * alpha) >> 8);
                b = b - ((b * alpha) >> 8);
                {
                    int nz = noiseLut[(x * 3 + noiseBase) & 255];
                    r += nz; g += nz; b += nz;
                    if (r < 0) r = 0; else if (r > 255) r = 255;
                    if (g < 0) g = 0; else if (g > 255) g = 255;
                    if (b < 0) b = 0; else if (b > 255) b = 255;
                }
                row[x] = (unsigned int)((r << 16) | (g << 8) | b);
            }
        }
        if (heal) {
            for (int x = 0; x < renderW; x++) {
                unsigned int dst = row[x];
                int r = (int)((dst >> 16) & 0xFF), g = (int)((dst >> 8) & 0xFF), b = (int)(dst & 0xFF);
                row[x] = (unsigned int)(((r + (255 - r) * 3 / 10) << 16) |
                                        ((g + (255 - g) * 3 / 10) << 8) |
                                        (b + (255 - b) * 3 / 10));
            }
        }
    }
}

void Render3D(void) {
    int halfH = renderH / 2;

    /* 只清上半屏（下半会被地板整片覆盖） */
    memset(pixels, 0, (size_t)(halfH + 1) * renderW * sizeof(unsigned int));

    /* 手电筒：开着是横向聚光，关掉/没电就整体压暗；电量低于 25% 会闪 */
    float flick = 1.0f;
    if (flashlightOn && battery > 0.0f && battery < 25.0f) {
        float t = (float)g_timeSec;
        float wobble = sinf(t * 7.5f) * sinf(t * 2.1f);      /* 慢速拍频，不会跟帧率打架 */
        flick = 0.78f + 0.22f * wobble;
        if (flick < 0.45f) flick = 0.45f;
    }
    int lightOn = (flashlightOn && battery > 0.0f);
    for (int x = 0; x < renderW; x++) {
        float f;
        if (lightOn) {
            /* 中心特别亮：核心 1.15 倍，边缘 0.55，用平方做成聚光斑 */
            float t = fabsf(x - renderW * 0.5f) / (renderW * 0.5f);
            if (t > 1.0f) t = 1.0f;
            float core = 1.0f - t * t * 0.75f;
            f = 0.40f + core * 0.75f;      /* 0.40 .. 1.15 */
            f *= flick;
        } else {
            f = 0.32f;   /* 手电熄灭时的环境光 */
        }
        flashTab[x] = f;
        float cone = f > 0.40f ? (f - 0.40f) / 0.75f : 0.0f;
        floorGain[x] = 0.52f + cone * 1.30f;
        ceilTab[x] = (int)((0.30f + cone * 0.75f) * 110.0f);
    }

    RunPhase(1);
    RunPhase(2);
    RenderSprites();
    if ((vignetteOn && sanity < 50.0f) || healFlash > 0.0f) RunPhase(3);
}

/* 加色混合：灯光用，越亮越接近白 */
static inline void PutPixelAdd(int x, int y, unsigned int color, float a) {
    if (x < 0 || x >= renderW || y < 0 || y >= renderH) return;
    unsigned int d = pixels[(size_t)y * renderW + x];
    int r = (int)((d >> 16) & 0xFF) + (int)(((color >> 16) & 0xFF) * a);
    int g = (int)((d >> 8) & 0xFF) + (int)(((color >> 8) & 0xFF) * a);
    int b = (int)(d & 0xFF) + (int)((color & 0xFF) * a);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    pixels[(size_t)y * renderW + x] = (unsigned int)((r << 16) | (g << 8) | b);
}

/* ---------------- 大厅与升级 ---------------- */

int BatteryMax(void) { return 100 + upgBattery * 20; }

static int UpgradeLevel(int kind) {
    switch (kind) {
        case 0: return upgBattery;
        case 1: return upgSpeed;
        case 2: return upgQuiet;
        default: return upgSanity;
    }
}

static void AddUpgrade(int kind) {
    switch (kind) {
        case 0: upgBattery++; break;
        case 1: upgSpeed++; break;
        case 2: upgQuiet++; break;
        default: upgSanity++; break;
    }
}

static const char* UpgradeName(int kind) {
    switch (kind) {
        case 0: return "BATTERY +20";
        case 1: return "SPEED +6%";
        case 2: return "QUIETER -15%";
        default: return "CALM -10%";
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

static int g_prev[MAP_HEIGHT][MAP_WIDTH];

/* 网格 BFS，填进 monster.path（不含起点） */
static int MonPathFor(Monster* m, int sx, int sy, int tx, int ty) {
    m->pathLen = 0;
    m->pathIdx = 0;
    if (sx < 1 || sy < 1 || sx >= MAP_WIDTH - 1 || sy >= MAP_HEIGHT - 1) return 0;
    if (tx < 1 || ty < 1 || tx >= MAP_WIDTH - 1 || ty >= MAP_HEIGHT - 1) return 0;
    if (worldMap[sy][sx] || worldMap[ty][tx]) return 0;
    if (sx == tx && sy == ty) return 0;

    static int queue[MAP_WIDTH * MAP_HEIGHT];
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) g_prev[y][x] = -1;

    int head = 0, tail = 0;
    queue[tail++] = sy * MAP_WIDTH + sx;
    g_prev[sy][sx] = sy * MAP_WIDTH + sx;

    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    while (head < tail) {
        int cur = queue[head++];
        int cx = cur % MAP_WIDTH, cy = cur / MAP_WIDTH;
        if (cx == tx && cy == ty) break;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
            if (worldMap[ny][nx]) continue;
            if (g_prev[ny][nx] != -1) continue;
            g_prev[ny][nx] = cur;
            queue[tail++] = ny * MAP_WIDTH + nx;
        }
    }
    if (g_prev[ty][tx] == -1) return 0;

    int tmp[MON_MAXPATH][2], n = 0;
    int cx = tx, cy = ty;
    while (!(cx == sx && cy == sy) && n < MON_MAXPATH) {
        tmp[n][0] = cx; tmp[n][1] = cy; n++;
        int p = g_prev[cy][cx];
        cx = p % MAP_WIDTH; cy = p / MAP_WIDTH;
    }
    m->pathLen = n;
    for (int i = 0; i < n; i++) {
        m->path[i][0] = tmp[n - 1 - i][0];
        m->path[i][1] = tmp[n - 1 - i][1];
    }
    return n;
}

static int MonPath(int sx, int sy, int tx, int ty) { return MonPathFor(&monster, sx, sy, tx, ty); }

/* 把怪放到离玩家足够远的空格 */
void SpawnMonster(void) {
    monster.active = 1;
    monster.state = 0;
    monster.speed = MonsterSpeedFor(currentLevel, MONSTER_SPEED_BASE, modSpeedMul);
    monster.pathLen = 0;
    monster.pathIdx = 0;
    monster.repath = 0.0f;
    monster.stateTimer = 0.0f;
    monster.wanderTimer = 1.5f;
    monster.hitCooldown = HitCooldownFor(currentLevel);
    monster.growlTimer = 3.0f;
    monster.hp = MONSTER_HP;
    monster.stun = 0.0f;
    for (int t = 0; t < 512; t++) {
        int x = 1 + rand() % (MAP_WIDTH - 2);
        int y = 1 + rand() % (MAP_HEIGHT - 2);
        if (worldMap[y][x]) continue;
        if (Dist2(x + 0.5f, y + 0.5f, posX, posY) < 100.0f) continue;
        if (!SpotClear(x + 0.5f, y + 0.5f, 1.4f, 4, -1)) continue;
        monster.x = x + 0.5f;
        monster.y = y + 0.5f;
        return;
    }
    monster.x = exitX;
    monster.y = exitY;
}

int HasLOS(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return 1;
    dx /= d; dy /= d;
    for (float t = 0.2f; t < d; t += 0.22f) {
        int cx = (int)(x0 + dx * t), cy = (int)(y0 + dy * t);
        if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return 0;
        if (worldMap[cy][cx]) return 0;
    }
    return 1;
}

/* 玩家制造声音：只有跑步、翻找、手摇会调用 */
void MakeNoise(float x, float y, float radius) {
    if (!monster.active) return;
    radius *= (1.0f - 0.15f * upgQuiet);
    if (Dist2(x, y, monster.x, monster.y) > radius * radius) return;
    monster.state = 1;
    monster.tx = x;
    monster.ty = y;
    monster.repath = 0.0f;
    monster.stateTimer = 0.0f;
}

void SpawnLightBeast(void) {
    /* 注意：不要整体拷贝 monster —— 那会把寻声怪的旧路径、旧命中冷却
       一并带过来，光敏怪会开局就沿着别人的路走、还带着别人的无敌时间。
       这里只借用它的类型，清干净状态再重置。 */
    memset(&lightBeast, 0, sizeof(lightBeast));
    lightBeast.active = 1;
    lightBeast.state = 0;
    lightBeast.speed = MonsterSpeedFor(currentLevel, LIGHTBEAST_SPEED_BASE, modSpeedMul);
    lightBeast.pathLen = 0;
    lightBeast.pathIdx = 0;
    lightBeast.repath = 0.0f;
    lightBeast.stateTimer = 0.0f;
    lightBeast.wanderTimer = 1.5f;
    lightBeast.hitCooldown = HitCooldownFor(currentLevel);
    lightBeast.growlTimer = 3.0f;
    lightBeast.hp = LIGHTBEAST_HP;
    lightBeast.stun = 0.0f;
    for (int t = 0; t < 512; t++) {
        int x = 1 + rand() % (MAP_WIDTH - 2);
        int y = 1 + rand() % (MAP_HEIGHT - 2);
        if (worldMap[y][x]) continue;
        if (Dist2(x + 0.5f, y + 0.5f, posX, posY) < 144.0f) continue;
        if (monster.active && Dist2(x + 0.5f, y + 0.5f, monster.x, monster.y) < 4.0f) continue;
        if (!SpotClear(x + 0.5f, y + 0.5f, 1.4f, 4, -1)) continue;
        lightBeast.x = x + 0.5f;
        lightBeast.y = y + 0.5f;
        return;
    }
    lightBeast.x = exitX;
    lightBeast.y = exitY;
}

void UpdateLightBeast(float dt) {
    if (!lightBeast.active) return;

    /* 硬直：被玻璃瓶砸中后短暂不能动 */
    if (lightBeast.stun > 0.0f) {
        lightBeast.stun -= dt;
        if (lightBeast.stun < 0.0f) lightBeast.stun = 0.0f;
        return;
    }

    /* 目标：看得见的玩家（手电开着）优先，其次是地上的荧光棒 */
    float tx = 0.0f, ty = 0.0f;
    int has = 0;
    if (flashlightOn && battery > 0.0f && Dist2(posX, posY, lightBeast.x, lightBeast.y) < 256.0f &&
        HasLOS(lightBeast.x, lightBeast.y, posX, posY)) {
        tx = posX; ty = posY; has = 1;
    } else {
        for (int i = 0; i < MAX_THROWN; i++) {
            if (!thrown[i].active || thrown[i].type != 0) continue;
            if (Dist2(thrown[i].x, thrown[i].y, lightBeast.x, lightBeast.y) < 256.0f &&
                HasLOS(lightBeast.x, lightBeast.y, thrown[i].x, thrown[i].y)) {
                tx = thrown[i].x; ty = thrown[i].y; has = 1; break;
            }
        }
    }

    if (has) {
        lightBeast.state = 1;
        lightBeast.tx = tx; lightBeast.ty = ty;
        lightBeast.repath -= dt;
        if (lightBeast.repath <= 0.0f) {
            lightBeast.repath = 0.35f;
            MonPathFor(&lightBeast, (int)lightBeast.x, (int)lightBeast.y, (int)tx, (int)ty);
        }
    } else if (lightBeast.state == 1) {
        lightBeast.state = 0;
        lightBeast.wanderTimer = 0.0f;
        lightBeast.pathLen = 0;
    } else {
        lightBeast.wanderTimer -= dt;
        if (lightBeast.wanderTimer <= 0.0f && lightBeast.pathLen == 0) {
            lightBeast.wanderTimer = 2.0f;
            int cx = (int)lightBeast.x, cy = (int)lightBeast.y;
            int nx = cx + (rand() % 9) - 4, ny = cy + (rand() % 9) - 4;
            if (nx > 0 && ny > 0 && nx < MAP_WIDTH - 1 && ny < MAP_HEIGHT - 1 && !worldMap[ny][nx])
                MonPathFor(&lightBeast, cx, cy, nx, ny);
        }
    }

    if (lightBeast.pathIdx < lightBeast.pathLen) {
        float px = lightBeast.path[lightBeast.pathIdx][0] + 0.5f;
        float py = lightBeast.path[lightBeast.pathIdx][1] + 0.5f;
        float dx = px - lightBeast.x, dy = py - lightBeast.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 0.12f) lightBeast.pathIdx++;
        else {
            float step = lightBeast.speed * dt;
            if (step > d) step = d;
            lightBeast.x += dx / d * step;
            lightBeast.y += dy / d * step;
        }
    }

    float pd2 = Dist2(lightBeast.x, lightBeast.y, posX, posY);
    if (lightBeast.hitCooldown <= 0.0f && pd2 < 0.55f) {
        sanity -= 14.0f;
        if (sanity < 0.0f) sanity = 0.0f;
        healFlash = 0.16f;
        PlaySfx(SFX_SCREAM, 1.0f);
        SpawnLightBeast();
    }
    if (lightBeast.hitCooldown > 0.0f) lightBeast.hitCooldown -= dt;
}

/* ------------------------------------------------------------------ */
/* 跳脸虫（Face Jumper）—— QTE 怪                                      */
/* ------------------------------------------------------------------ */

/* 失败惩罚：扣理智 + 短时 debuff */
static void JumperFail(void) {
    sanity -= JUMPER_FAIL_SANITY;
    if (sanity < 0.0f) sanity = 0.0f;
    ApplyFx(FX_BLIND, JUMPER_FAIL_FX_POWER, JUMPER_FAIL_FX_TIME);
    ApplyFx(FX_SLOW, JUMPER_FAIL_FX_POWER, JUMPER_FAIL_FX_TIME);
    healFlash = 0.25f;
    PlaySfx(SFX_SCREAM, 1.0f);
}

/* 事件结束（无论成功失败）—— 虫子退场，本关不再出现 */
static void JumperFinish(void) {
    jumperPhase = JMP_DONE;
    jumperTimer = 0.0f;
    jumperResolved = 1;
    parasite.active = 0;
}

/* 阶段一成功：躲开 */
static void JumperDodged(void) {
    JumperFinish();
    PlaySfx(SFX_PICKUP, 0.6f);
}

/* 阶段二成功：按对了键 */
static void JumperKeyHit(void) {
    JumperFinish();
    PlaySfx(SFX_POWER, 0.6f);
}

/* 生成跳脸虫：放在玩家视野前方一段距离，
   然后它会「弹」到玩家脸上。 */
void SpawnParasite(void) {
    memset(&parasite, 0, sizeof(parasite));
    parasite.active = 1;
    parasite.attached = 0;
    parasite.ramp = 0.0f;
    parasite.attachTimer = 0.0f;
    parasite.hp = 1;
    parasite.stun = 0.0f;

    /* 起点：玩家正前方 4~6 格，随机偏一点 */
    float d = 4.0f + (float)(rand() % 3);
    float ang = (float)(rand() % 7 - 3) * 0.12f;
    float ca = cosf(ang), sa = sinf(ang);
    parasite.x = posX + (dirX * ca - dirY * sa) * d;
    parasite.y = posY + (dirY * ca + dirX * sa) * d;
    if (ShotBlocked(parasite.x, parasite.y)) {
        parasite.x = posX + dirX * 3.0f;
        parasite.y = posY + dirY * 3.0f;
    }

    jumperPhase = JMP_APPROACH;
    jumperTimer = JUMPER_APPROACH_TIME;
    jumperResolved = 0;
    jumperQteKey = rand() % JMP_KEY_COUNT;
    PlaySfx(SFX_SCREAM, 0.35f);
}

void UpdateParasite(float dt) {
    /* 还没到触发时机 */
    if (jumperTrigger >= 0.0f && jumperPhase == JMP_IDLE) {
        jumperTrigger -= dt;
        if (jumperTrigger <= 0.0f) {
            jumperTrigger = -1.0f;
            SpawnParasite();
        }
    }
    if (!parasite.active) return;

    switch (jumperPhase) {
    case JMP_APPROACH: {
        /* 从远处弹到玩家脸上：位置插值，尺寸也会变大（更吓人） */
        jumperTimer -= dt;
        float t = 1.0f - jumperTimer / JUMPER_APPROACH_TIME;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        /* 起点就是生成时记下的位置；这里直接朝玩家脸逼近 */
        float tx = posX + dirX * 0.75f;
        float ty = posY + dirY * 0.75f;
        parasite.x += (tx - parasite.x) * (dt * 7.0f);
        parasite.y += (ty - parasite.y) * (dt * 7.0f);
        parasite.ramp = t;    /* 复用 ramp 记录「逼近程度」，供渲染放大 */

        if (jumperTimer <= 0.0f) {
            /* 到位了：进入阶段一「躲开」 */
            jumperPhase = JMP_DODGE;
            jumperTimer = JUMPER_DODGE_WINDOW;
            parasite.x = tx;
            parasite.y = ty;
            PlaySfx(SFX_SCREAM, 0.8f);
        }
        break;
    }

    case JMP_DODGE: {
        jumperTimer -= dt;
        /* 玩家按 SPACE = 躲开 */
        if (keyPressed[VK_SPACE]) {
            JumperDodged();
            return;
        }
        if (jumperTimer <= 0.0f) {
            /* 没躲开：跿上脸，进入阶段二 */
            jumperPhase = JMP_QTE;
            jumperTimer = JUMPER_QTE_WINDOW;
            PlaySfx(SFX_SCREAM, 1.0f);
        }
        break;
    }

    case JMP_QTE: {
        jumperTimer -= dt;
        /* 阶段二：按下指定的键 */
        static const int keyCodes[JMP_KEY_COUNT] = { 'A', 'S', 'D', 'F' };
        int want = keyCodes[jumperQteKey];
        if (keyPressed[want]) {
            JumperKeyHit();
            return;
        }
        if (jumperTimer <= 0.0f) {
            /* 超时：惩罚 */
            JumperFail();
            JumperFinish();
        }
        break;
    }

    default:
        break;
    }
}

static void MonsterWander(void) {
    int cx = (int)monster.x, cy = (int)monster.y;
    for (int t = 0; t < 24; t++) {
        int nx = cx + (rand() % 9) - 4;
        int ny = cy + (rand() % 9) - 4;
        if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
        if (worldMap[ny][nx]) continue;
        if (MonPath(cx, cy, nx, ny) > 0) {
            monster.tx = nx + 0.5f;
            monster.ty = ny + 0.5f;
            return;
        }
    }
}

void UpdateMonster(float dt) {
    if (!monster.active) return;
    if (monster.hitCooldown > 0.0f) monster.hitCooldown -= dt;
    if (monster.growlTimer > 0.0f) monster.growlTimer -= dt;

    /* 硬直：被玻璃瓶砸中后短暂不能动，也不能抓人 */
    if (monster.stun > 0.0f) {
        monster.stun -= dt;
        if (monster.stun < 0.0f) monster.stun = 0.0f;
        return;
    }

    if (monster.state == 1) {
        monster.repath -= dt;
        if (monster.repath <= 0.0f) {
            monster.repath = 0.45f;
            MonPath((int)monster.x, (int)monster.y, (int)monster.tx, (int)monster.ty);
        }
        if (Dist2(monster.x, monster.y, monster.tx, monster.ty) < 0.45f) {
            monster.state = 2;          /* 到了声源，原地搜一圈 */
            monster.stateTimer = 3.5f;
            monster.pathLen = 0;
            monster.pathIdx = 0;
        }
    } else if (monster.state == 2) {
        monster.stateTimer -= dt;
        if (monster.pathLen == 0 || monster.pathIdx >= monster.pathLen) {
            if (monster.stateTimer <= 0.0f) {
                monster.state = 0;
                monster.wanderTimer = 0.5f;
            } else {
                MonsterWander();
            }
        }
    } else {
        monster.wanderTimer -= dt;
        if (monster.wanderTimer <= 0.0f) {
            monster.wanderTimer = 2.5f + (float)(rand() % 25) / 10.0f;
            MonsterWander();
        }
    }

    /* 沿路径走 */
    if (monster.pathIdx < monster.pathLen) {
        float tx = monster.path[monster.pathIdx][0] + 0.5f;
        float ty = monster.path[monster.pathIdx][1] + 0.5f;
        float dx = tx - monster.x, dy = ty - monster.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 0.12f) {
            monster.pathIdx++;
        } else {
            float step = monster.speed * dt;
            if (step > d) step = d;
            monster.x += dx / d * step;
            monster.y += dy / d * step;
        }
    }

    /* 靠近时发出低吼（也是给玩家的听觉提示） */
    float pd2 = Dist2(monster.x, monster.y, posX, posY);
    if (pd2 < 36.0f && monster.growlTimer <= 0.0f) {
        monster.growlTimer = 2.6f;
        PlaySfx(SFX_SCREAM, pd2 < 9.0f ? 0.55f : 0.28f);
    }

    /* 抓到玩家：掉理智 + 怪被弹开（以后可以换成真正的地图事件） */
    if (monster.hitCooldown <= 0.0f && pd2 < 0.55f) {
        sanity -= 18.0f;
        if (sanity < 0.0f) sanity = 0.0f;
        healFlash = 0.18f;
        PlaySfx(SFX_SCREAM, 1.0f);
        SpawnMonster();
    }
}


/* 小地图（在内部渲染分辨率上绘制）
   两种形态：
     角落小窗（默认）：cellSize 固定 8 -> 24*8 = 192px 见方，
                       贴在右下角，不挡视野，可以边走边看。
     全屏：cellSize 按屏幕大小算，铺满中央，用来规划路线。
   之前只有全屏一种形态 —— 打开就得停下，关掉才看得见游戏，
   失去了「导航」的意义。 */
#define MAP_CORNER_CELL 8

void RenderMap(void) {
    int cellSize, mapW, mapH, mapStartX, mapStartY;
    if (mapFullScreen) {
        cellSize = (renderW < renderH ? renderW : renderH) / (MAP_WIDTH + 2);
        if (cellSize < 3) cellSize = 3;
        mapW = MAP_WIDTH * cellSize;
        mapH = MAP_HEIGHT * cellSize;
        mapStartX = (renderW - mapW) / 2;
        mapStartY = (renderH - mapH) / 2;
    } else {
        cellSize = MAP_CORNER_CELL;
        mapW = MAP_WIDTH * cellSize;
        mapH = MAP_HEIGHT * cellSize;
        /* 贴右下角，留出一点边距 */
        mapStartX = renderW - mapW - UIS(10);
        mapStartY = renderH - mapH - UIS(10);
        if (mapStartX < 0) mapStartX = 0;
        if (mapStartY < 0) mapStartY = 0;
    }

    if (mapFullScreen) {
        /* 全屏：整个画面压暗，突出地图 */
        int pad = cellSize;
        for (int y = mapStartY - pad; y < mapStartY + mapH + pad; y++)
            for (int x = mapStartX - pad; x < mapStartX + mapW + pad; x++)
                PutPixelBlend(x, y, 0x00000000, 0.72f);
    } else {
        /* 小窗：只给地图区域加个边框底板，不压暗整个画面 */
        int pad = 2;
        for (int y = mapStartY - pad; y < mapStartY + mapH + pad; y++) {
            if (y < 0 || y >= renderH) continue;
            for (int x = mapStartX - pad; x < mapStartX + mapW + pad; x++) {
                if (x < 0 || x >= renderW) continue;
                PutPixelBlend(x, y, 0x00000000, 0.82f);
            }
        }
    }

    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            unsigned int color = 0;
            float alpha = 0.8f;
            if (explored[y][x] == 0) { color = 0x00050505; alpha = 0.95f; }
            else {
                if (worldMap[y][x] == 1) color = 0x00888888;
                else { color = 0x00000000; alpha = 0.3f; }
                if ((int)exitX == x && (int)exitY == y) { color = 0x0000FF00; alpha = 1.0f; }
            }
            int bx = mapStartX + x * cellSize, by = mapStartY + y * cellSize;
            for (int dy = 0; dy < cellSize - 1; dy++)
                for (int dx = 0; dx < cellSize - 1; dx++)
                    PutPixelBlend(bx + dx, by + dy, color, alpha);
        }
    }

    /* 容器（未搜过 = 黄点闪烁）/ 配电箱（青点）/ 出口（绿或红）
       闪烁用主循环的连续时钟 g_timeSec，不用 GetTickCount()：
       后者的 15.6ms 台阶会让闪烁看起来像掉帧。 */
    for (int i = 0; i < containerCount; i++) {
        if (!containers[i].active || containers[i].searched) continue;
        if (!explored[(int)containers[i].y][(int)containers[i].x]) continue;
        if ((int)(g_timeSec * 4.5f) % 2) continue;
        int px = mapStartX + (int)containers[i].x * cellSize + cellSize / 2;
        int py = mapStartY + (int)containers[i].y * cellSize + cellSize / 2;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) PutPixel(px + dx, py + dy, 0x00E8C040);
    }
    if (breaker.active && explored[(int)breaker.y][(int)breaker.x]) {
        int px = mapStartX + (int)breaker.x * cellSize + cellSize / 2;
        int py = mapStartY + (int)breaker.y * cellSize + cellSize / 2;
        unsigned int c = breakerOpen ? 0x0030FF60 : 0x0030D0D0;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++)
                if (dx * dx + dy * dy <= 9) PutPixel(px + dx, py + dy, c);
    }

    int pmX = mapStartX + (int)(posX * cellSize), pmY = mapStartY + (int)(posY * cellSize);
    int r = cellSize / 3 + 1;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) PutPixel(pmX + dx, pmY + dy, 0x0000FF00);

    for (float t = 0; t < 1.5f; t += 0.1f) {
        int lx = mapStartX + (int)((posX + dirX * t) * cellSize);
        int ly = mapStartY + (int)((posY + dirY * t) * cellSize);
        PutPixel(lx, ly, 0x00FFFFFF);
    }

    /* 标题：告诉玩家当前是哪种形态，以及再按 M 会变成什么。
       小窗模式下标题画在地图上方；全屏模式画在地图上方居中。 */
    {
        const char* label = mapFullScreen ? "MAP   M: SMALL   M: CLOSE" : "MAP   M: FULL   M: CLOSE";
        int tw = TextWidthRaw(label);
        int tx = mapFullScreen ? (renderW - tw) / 2 : mapStartX;
        int ty = mapFullScreen ? (mapStartY - cellSize * 2 - FONT_H) : (mapStartY - FONT_H - 2);
        if (ty < 0) ty = 2;
        /* 底下垫一条深色条，浅色文字才看得清 */
        for (int y = ty - 2; y < ty + FONT_H + 2; y++) {
            if (y < 0 || y >= renderH) continue;
            for (int x = tx - 3; x < tx + tw + 3; x++) {
                if (x < 0 || x >= renderW) continue;
                PutPixelBlend(x, y, 0x00000000, 0.7f);
            }
        }
        PutTextRaw(tx, ty, label, 0x00B0C0D0u);
    }
}

/* ------------------------------------------------------------------ */
/* 逻辑更新（固定步长，dt = LOGIC_STEP）                                */
/* ------------------------------------------------------------------ */

static void ThrowItem(int type) {
    float px = posX, py = posY;
    for (int i = 0; i < 30; i++) {
        float nx = px + dirX * 0.22f, ny = py + dirY * 0.22f;
        if (Blocked(nx, ny)) break;
        px = nx; py = ny;
    }
    if (type == 1) {
        /* 玻璃瓶：现在是一个飞行弹丸，砸中怪会造成伤害。
           出生点必须用**玩家位置**，不能用上面算出来的 px/py ——
           那个 px/py 是「扔到墙上的落点」，最多能到 6.6 格外，
           瓶子一出生就飞过怪了。

           也不能简单地往前挪 0.3 格：贴墙站着时那 0.3 格会进墙格，
           弹丸一生成就被判定「撞墙」碎掉。
           所以从**玩家所在的格子**出发，让它自己飞出去。 */
        float sx = posX, sy = posY;
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (shot[i].active) continue;
            shot[i].x = sx; shot[i].y = sy;
            shot[i].vx = dirX * SHOT_SPEED;
            shot[i].vy = dirY * SHOT_SPEED;
            shot[i].life = 0.9f;
            shot[i].age = 0.0f;
            shot[i].active = 1;
            break;
        }
        PlaySfx(SFX_PICKUP, 0.5f);
        return;
    }
    for (int i = 0; i < MAX_THROWN; i++) {
        if (thrown[i].active) continue;
        thrown[i].x = px; thrown[i].y = py;
        thrown[i].type = 0; thrown[i].life = 25.0f; thrown[i].active = 1;
        break;
    }
    PlaySfx(SFX_PICKUP, 0.5f);
}

/* ------------------------------------------------------------------ */
/* 玻璃瓶命中怪物                                                       */
/* ------------------------------------------------------------------ */

/* 让被砸中的怪后退（沿「远离玩家」的方向），并进入硬直。
   后退距离要够明显 —— 玩家需要从反馈里知道「我打中了」。 */
static void KnockbackMonster(Monster* m) {
    float dx = m->x - posX, dy = m->y - posY;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) { dx = -dirX; dy = -dirY; d = 1.0f; }
    else { dx /= d; dy /= d; }

    /* 沿反方向找第一个能站住的位置 */
    for (int step = 3; step >= 1; step--) {
        float nx = m->x + dx * MONSTER_FLEE_DIST * (float)step / 3.0f;
        float ny = m->y + dy * MONSTER_FLEE_DIST * (float)step / 3.0f;
        if (!Blocked(nx, ny)) { m->x = nx; m->y = ny; break; }
    }
    m->stun = MONSTER_STUN;
    m->pathLen = 0;
    m->pathIdx = 0;
    m->state = 0;              /* 被打断，重新游荡 */
    m->wanderTimer = 0.6f;
}

/* 玻璃瓶命中一只怪。返回 1 表示命中。 */
static int HitMonster(Monster* m, int dmg) {
    if (!m->active) return 0;
    if (m->stun > 0.0f) return 0;    /* 已经在硬直里，不重复结算 */

    /* 跳脸虫不在这条路径上 —— 它是屏幕空间的 QTE 事件，
       不在世界里，也没有实体可以砸。 */

    m->hp -= dmg;
    m->hitCooldown = 0.0f;           /* 硬直期间不该抓人 */

    if (m->hp <= 0) {
        /* 血量耗尽：直接驱离（重新生成到别处），而不是留在原地 */
        if (m == &monster) SpawnMonster();
        else SpawnLightBeast();
    } else {
        KnockbackMonster(m);
    }
    PlaySfx(SFX_GLASS, 0.9f);
    MakeNoise(m->x, m->y, 9.0f);     /* 砸怪的代价：暴露自己的位置 */
    return 1;
}

/* 推进所有飞行中的玻璃瓶，结算命中 */
static void UpdateShots(float dt) {
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!shot[i].active) continue;
        shot[i].life -= dt;
        shot[i].age += dt;

        /* 分段推进，避免高速穿过怪。
           注意：vx/vy 存的是**速度**（格/秒），每步位移 = 速度 × dt / 段数。
           早先误把 vx 直接当每步位移用，弹丸一帧就飞出去 9 格 —— 整局都砸不到怪。 */
        float stepLen = SHOT_SPEED * dt;
        int steps = (int)(stepLen / 0.12f) + 1;
        float sdx = shot[i].vx * dt / (float)steps;
        float sdy = shot[i].vy * dt / (float)steps;

        for (int s = 0; s < steps && shot[i].active; s++) {
            shot[i].x += sdx;
            shot[i].y += sdy;

            /* 撞墙：碎掉。
               第一帧跳过 —— 出生点就在玩家脚下的格子里，
               贴墙站着时那一格可能已经是墙，判早了瓶子一扔就碎。 */
            if (shot[i].age > 0.02f && ShotBlocked(shot[i].x, shot[i].y)) {
                PlaySfx(SFX_GLASS, 0.7f);
                MakeNoise(shot[i].x, shot[i].y, 14.0f);
                shot[i].active = 0;
                break;
            }
            /* 撞怪 */
            float r = SHOT_RADIUS + 0.24f;
            if (monster.active && Dist2(shot[i].x, shot[i].y, monster.x, monster.y) < r * r) {
                HitMonster(&monster, SHOT_DAMAGE);
                shot[i].active = 0;
                break;
            }
            if (lightBeast.active && Dist2(shot[i].x, shot[i].y, lightBeast.x, lightBeast.y) < r * r) {
                HitMonster(&lightBeast, SHOT_DAMAGE);
                shot[i].active = 0;
                break;
            }
            /* 跳脸虫不在这里判定 —— 它是屏幕空间的 QTE 事件，
               不在世界里，瓶子砸不到它。 */
        }
        if (shot[i].life <= 0.0f) shot[i].active = 0;
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

    /* 状态效果计时。强度变化时重算 FOV（视野收窄要立刻生效） */
    if (UpdateFx(dt)) ApplyFov();

    /* 理智流失：基础值 + 状态效果的额外流失 */
    float drainMul = 1.0f + FxStrength(FX_DRAIN) * 1.5f;
    sanity -= SANITY_DRAIN * (1.0f - 0.10f * upgSanity) * modSanityMul * drainMul * dt;
    if (sanity <= 0.0f) {
        sanity = 0.0f;
        runInProgress = 0;
        if (currentLevel > bestLevel) bestLevel = currentLevel;
        currentState = STATE_GAMEOVER;
        menuSelection = 0;
        ForceCursor(1);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        uiDirty = 1;
        return;
    }

    /* 手电筒耗电：电量耗尽自动熄灭 */
    if (flashlightOn) {
        battery -= BATTERY_DRAIN * dt;
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


    holdingNow = (hoverKind != 0 && (keys['E'] || keys['e']));

    /* 冲刺与体力 */
    int wantSprint = keys[VK_SHIFT] || keys[VK_LSHIFT] || keys[VK_RSHIFT];
    int inF = (keys['W'] || keys['w']) - (keys['S'] || keys['s']);
    int inS = (keys['D'] || keys['d']) - (keys['A'] || keys['a']);
    int moving = (inF != 0 || inS != 0);

    sprinting = (wantSprint && moving && stamina > 1.0f);
    if (sprinting) {
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
        sanity += 3.5f * dt;
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
    if ((keys['R'] || keys['r']) && battery < 99.9f && !holdingNow) {
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
        holdTimer += dt;
        if (holdTimer >= SEARCH_TIME) {
            holdTimer = 0.0f;
            if (hoverKind == 1) {
                Container* c = &containers[hoverIndex];
                c->searched = 1;
                searchedCount++;
                shards += 5;
                PlaySfx(SFX_PICKUP, 0.55f);
                MakeNoise(posX, posY, 8.0f);
                if (c->content == 1) fusesHeld++;
                else if (c->content == 4) flares++;
                else if (c->content == 5) bottles++;
                else if (c->content == 2) {
                    battery += BATTERY_PICKUP;
                    if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
                    healFlash = 0.06f;
                } else if (c->content == 3) {
                    sanity += 25.0f;
                    if (sanity > 100.0f) sanity = 100.0f;
                    healFlash = 0.15f;
                }
            } else if (hoverKind == 2) {
                if (fusesHeld >= fusesNeeded) {
                    breakerOpen = 1;
                    fusesHeld -= fusesNeeded;
                    shards += 15;
                    PlaySfx(SFX_POWER, 0.75f);
                }
            }
            hoverKind = 0;
            hoverIndex = -1;
        }
    } else {
        holdTimer = 0.0f;
    }

    /* 投掷：G 荧光棒  B 玻璃瓶 */
    if (throwCd > 0.0f) throwCd -= dt;
    if (throwCd <= 0.0f && !holdingNow && !cranking) {
        if ((keys['G'] || keys['g']) && flares > 0) { flares--; ThrowItem(0); throwCd = 0.45f; }
        else if ((keys['B'] || keys['b']) && bottles > 0) { bottles--; ThrowItem(1); throwCd = 0.45f; }
    }

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
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        thrown[i].life -= dt;
        if (thrown[i].life <= 0.0f) thrown[i].active = 0;
    }

    /* 出口：通电后才能进下一关 */
    if (breakerOpen && Dist2(posX, posY, exitX, exitY) < 0.30f) {
        PlaySfx(SFX_GATE, 0.65f);
        shards += 30;
        if (currentLevel > bestLevel) bestLevel = currentLevel;
        currentLevel++;
        GenerateLevel();
        ClearFx();                    /* 换关解除所有状态效果 */
        for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
        flashTime = 0.15f;
        UpdateExplored();
    }
}

/* ------------------------------------------------------------------ */
/* UI：字体缓存 / 半透明面板 / 文字                                     */
/* ------------------------------------------------------------------ */

/* 8x12 点阵字库，ASCII 32..126（由 GDI 位图字体 1:1 采样生成）
   FONT_W / FONT_H 定义在文件前部（RenderMap 需要提前用）。 */
static const unsigned char fontData[95][FONT_H] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x20,0x20,0x20,0x20,0x20,0x20,0x00,0x20,0x00,0x00}, /* ! */
    {0x00,0x00,0x28,0x28,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x00,0x00,0x00,0x28,0x28,0xFC,0x28,0xFC,0x50,0x50,0x00,0x00}, /* # */
    {0x00,0x00,0x10,0x38,0x50,0x50,0x38,0x14,0x24,0x78,0x20,0x00}, /* $ */
    {0x00,0x00,0xE4,0xA8,0xF0,0x10,0x20,0x5C,0x54,0x9C,0x00,0x00}, /* % */
    {0x00,0x00,0x70,0x50,0x50,0x60,0xE8,0x98,0x88,0x7C,0x00,0x00}, /* & */
    {0x00,0x00,0x20,0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x00,0x00,0x08,0x10,0x10,0x20,0x20,0x20,0x20,0x10,0x10,0x08}, /* ( */
    {0x00,0x00,0x40,0x20,0x20,0x10,0x10,0x10,0x10,0x20,0x20,0x40}, /* ) */
    {0x00,0x00,0x10,0x54,0x38,0x54,0x10,0x00,0x00,0x00,0x00,0x00}, /* * */
    {0x00,0x00,0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x30,0x60}, /* , */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x70,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x00,0x00}, /* . */
    {0x00,0x00,0x04,0x08,0x08,0x10,0x10,0x10,0x20,0x20,0x40,0x00}, /* / */
    {0x00,0x00,0x00,0x70,0x88,0x98,0xA8,0xC8,0x88,0x70,0x00,0x00}, /* 0 */
    {0x00,0x00,0x00,0x30,0x50,0x10,0x10,0x10,0x10,0x7C,0x00,0x00}, /* 1 */
    {0x00,0x00,0x00,0x38,0x44,0x04,0x08,0x10,0x20,0x7C,0x00,0x00}, /* 2 */
    {0x00,0x00,0x00,0x70,0x08,0x08,0x30,0x08,0x08,0x70,0x00,0x00}, /* 3 */
    {0x00,0x00,0x00,0x18,0x28,0x48,0x88,0xFC,0x08,0x08,0x00,0x00}, /* 4 */
    {0x00,0x00,0x00,0x78,0x40,0x40,0x70,0x08,0x08,0x70,0x00,0x00}, /* 5 */
    {0x00,0x00,0x00,0x38,0x60,0x40,0x78,0x44,0x44,0x38,0x00,0x00}, /* 6 */
    {0x00,0x00,0x00,0x7C,0x04,0x08,0x08,0x10,0x10,0x20,0x00,0x00}, /* 7 */
    {0x00,0x00,0x00,0x38,0x44,0x64,0x38,0x4C,0x44,0x38,0x00,0x00}, /* 8 */
    {0x00,0x00,0x00,0x38,0x44,0x44,0x3C,0x04,0x08,0x70,0x00,0x00}, /* 9 */
    {0x00,0x00,0x00,0x00,0x30,0x30,0x00,0x00,0x30,0x30,0x00,0x00}, /* : */
    {0x00,0x00,0x00,0x00,0x30,0x30,0x00,0x00,0x30,0x30,0x30,0x60}, /* ; */
    {0x00,0x00,0x00,0x00,0x08,0x10,0x20,0x20,0x10,0x08,0x00,0x00}, /* < */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x7C,0x00,0x7C,0x00,0x00,0x00}, /* = */
    {0x00,0x00,0x00,0x00,0x40,0x20,0x10,0x10,0x20,0x40,0x00,0x00}, /* > */
    {0x00,0x00,0x30,0x18,0x08,0x08,0x30,0x20,0x00,0x20,0x00,0x00}, /* ? */
    {0x00,0x00,0x38,0x4C,0x44,0x94,0xB4,0xB4,0xB8,0x80,0x40,0x78}, /* @ */
    {0x00,0x00,0x00,0x10,0x28,0x28,0x28,0x7C,0x44,0x44,0x00,0x00}, /* A */
    {0x00,0x00,0x00,0x78,0x44,0x44,0x78,0x44,0x44,0x78,0x00,0x00}, /* B */
    {0x00,0x00,0x00,0x38,0x60,0x40,0x40,0x40,0x60,0x38,0x00,0x00}, /* C */
    {0x00,0x00,0x00,0x78,0x4C,0x44,0x44,0x44,0x4C,0x78,0x00,0x00}, /* D */
    {0x00,0x00,0x00,0x78,0x40,0x40,0x78,0x40,0x40,0x78,0x00,0x00}, /* E */
    {0x00,0x00,0x00,0x78,0x40,0x40,0x78,0x40,0x40,0x40,0x00,0x00}, /* F */
    {0x00,0x00,0x00,0x78,0x40,0x80,0x98,0x88,0xC8,0x78,0x00,0x00}, /* G */
    {0x00,0x00,0x00,0x48,0x48,0x48,0x78,0x48,0x48,0x48,0x00,0x00}, /* H */
    {0x00,0x00,0x00,0x7C,0x10,0x10,0x10,0x10,0x10,0x7C,0x00,0x00}, /* I */
    {0x00,0x00,0x00,0x78,0x08,0x08,0x08,0x08,0x08,0x70,0x00,0x00}, /* J */
    {0x00,0x00,0x00,0x48,0x50,0x50,0x60,0x50,0x50,0x48,0x00,0x00}, /* K */
    {0x00,0x00,0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x78,0x00,0x00}, /* L */
    {0x00,0x00,0x00,0x48,0x48,0xB4,0xB4,0x84,0x84,0x84,0x00,0x00}, /* M */
    {0x00,0x00,0x00,0x48,0x68,0x68,0x68,0x58,0x58,0x48,0x00,0x00}, /* N */
    {0x00,0x00,0x00,0x78,0xCC,0x84,0x84,0x84,0xCC,0x78,0x00,0x00}, /* O */
    {0x00,0x00,0x00,0x78,0x44,0x44,0x78,0x40,0x40,0x40,0x00,0x00}, /* P */
    {0x00,0x00,0x00,0x70,0xC8,0x88,0x88,0x88,0x98,0x70,0x20,0x18}, /* Q */
    {0x00,0x00,0x00,0x70,0x48,0x48,0x70,0x50,0x48,0x44,0x00,0x00}, /* R */
    {0x00,0x00,0x00,0x3C,0x40,0x40,0x38,0x04,0x04,0x78,0x00,0x00}, /* S */
    {0x00,0x00,0x00,0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x00}, /* T */
    {0x00,0x00,0x00,0x44,0x44,0x44,0x44,0x44,0x44,0x38,0x00,0x00}, /* U */
    {0x00,0x00,0x00,0x44,0x44,0x44,0x28,0x28,0x28,0x10,0x00,0x00}, /* V */
    {0x00,0x00,0x00,0x84,0x84,0x84,0xB4,0x78,0x58,0x48,0x00,0x00}, /* W */
    {0x00,0x00,0x00,0x84,0x48,0x30,0x30,0x30,0x48,0xCC,0x00,0x00}, /* X */
    {0x00,0x00,0x00,0x82,0x44,0x28,0x10,0x10,0x10,0x10,0x00,0x00}, /* Y */
    {0x00,0x00,0x00,0x7C,0x08,0x08,0x10,0x20,0x20,0x78,0x00,0x00}, /* Z */
    {0x00,0x00,0x38,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x38}, /* [ */
    {0x00,0x00,0x40,0x20,0x20,0x10,0x10,0x10,0x08,0x08,0x04,0x00}, /* \ */
    {0x00,0x00,0x70,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x70}, /* ] */
    {0x00,0x00,0x00,0x20,0x50,0x50,0x00,0x00,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFC}, /* _ */
    {0x00,0x60,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x00,0x00,0x38,0x04,0x3C,0x44,0x4C,0x3C,0x00,0x00}, /* a */
    {0x00,0x00,0x40,0x40,0x78,0x64,0x44,0x44,0x44,0x78,0x00,0x00}, /* b */
    {0x00,0x00,0x00,0x00,0x38,0x60,0x40,0x40,0x40,0x38,0x00,0x00}, /* c */
    {0x00,0x00,0x04,0x04,0x3C,0x44,0x44,0x44,0x4C,0x34,0x00,0x00}, /* d */
    {0x00,0x00,0x00,0x00,0x38,0x44,0x7C,0x40,0x60,0x3C,0x00,0x00}, /* e */
    {0x00,0x00,0x1C,0x20,0x20,0xF8,0x20,0x20,0x20,0x20,0x00,0x00}, /* f */
    {0x00,0x00,0x00,0x00,0x3E,0x44,0x44,0x78,0x40,0x7C,0x44,0x78}, /* g */
    {0x00,0x00,0x40,0x40,0x78,0x48,0x48,0x48,0x48,0x48,0x00,0x00}, /* h */
    {0x00,0x00,0x18,0x00,0x70,0x10,0x10,0x10,0x10,0x78,0x00,0x00}, /* i */
    {0x00,0x00,0x18,0x00,0x78,0x08,0x08,0x08,0x08,0x08,0x08,0x70}, /* j */
    {0x00,0x00,0x40,0x40,0x48,0x50,0x60,0x60,0x50,0x48,0x00,0x00}, /* k */
    {0x00,0x00,0x70,0x10,0x10,0x10,0x10,0x10,0x10,0x78,0x00,0x00}, /* l */
    {0x00,0x00,0x00,0x00,0x7C,0x54,0x54,0x54,0x54,0x54,0x00,0x00}, /* m */
    {0x00,0x00,0x00,0x00,0x78,0x48,0x48,0x48,0x48,0x48,0x00,0x00}, /* n */
    {0x00,0x00,0x00,0x00,0x38,0x44,0x44,0x44,0x44,0x38,0x00,0x00}, /* o */
    {0x00,0x00,0x00,0x00,0x78,0x64,0x44,0x44,0x44,0x78,0x40,0x40}, /* p */
    {0x00,0x00,0x00,0x00,0x3C,0x44,0x44,0x44,0x4C,0x34,0x04,0x04}, /* q */
    {0x00,0x00,0x00,0x00,0x78,0x48,0x40,0x40,0x40,0x40,0x00,0x00}, /* r */
    {0x00,0x00,0x00,0x00,0x38,0x40,0x60,0x18,0x08,0x70,0x00,0x00}, /* s */
    {0x00,0x00,0x20,0x20,0xF8,0x20,0x20,0x20,0x20,0x38,0x00,0x00}, /* t */
    {0x00,0x00,0x00,0x00,0x48,0x48,0x48,0x48,0x48,0x78,0x00,0x00}, /* u */
    {0x00,0x00,0x00,0x00,0x44,0x44,0x44,0x28,0x28,0x10,0x00,0x00}, /* v */
    {0x00,0x00,0x00,0x00,0x84,0xA8,0xA8,0xB8,0xD8,0x48,0x00,0x00}, /* w */
    {0x00,0x00,0x00,0x00,0xCC,0x48,0x30,0x30,0x48,0xCC,0x00,0x00}, /* x */
    {0x00,0x00,0x00,0x00,0x44,0x44,0x48,0x28,0x30,0x10,0x20,0xE0}, /* y */
    {0x00,0x00,0x00,0x00,0x78,0x10,0x10,0x20,0x20,0x78,0x00,0x00}, /* z */
    {0x00,0x00,0x30,0x20,0x20,0x20,0xC0,0x20,0x20,0x20,0x20,0x30}, /* { */
    {0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10}, /* | */
    {0x00,0x00,0x30,0x10,0x10,0x10,0x0C,0x10,0x10,0x10,0x10,0x30}, /* } */
    {0x00,0x00,0x00,0x00,0x00,0x00,0xE8,0xB8,0x00,0x00,0x00,0x00}, /* ~ */
};

/* 文字直接写进像素缓冲：和面板同一层，不存在“文字层丢失” */
static int TextScale(void) {
    int s = (int)(renderH * 2.0 / 600.0 + 0.5);
    if (s < 1) s = 1;
    if (s > 8) s = 8;
    return s;
}

static int TextAdvWin(void) {
    return FONT_W * TextScale() * winW / renderW;
}

static int TextWidthWin(const char* t) {
    return (int)strlen(t) * TextAdvWin();
}

static int TextHeightWin(void) {
    return FONT_H * TextScale() * winH / renderH;
}

/* xWin/yWin 用窗口坐标，内部换算到渲染坐标 */
static void DrawTextWin(int xWin, int yWin, const char* t, unsigned int color) {
    int s = TextScale();
    int px = xWin * renderW / winW;
    int py = yWin * renderH / winH;
    for (const char* p = t; *p; p++) {
        int ch = (unsigned char)*p;
        if (ch < 32 || ch > 126) ch = '?';
        const unsigned char* g = fontData[ch - 32];
        for (int r = 0; r < FONT_H; r++) {
            unsigned char bits = g[r];
            if (!bits) continue;
            for (int c = 0; c < FONT_W; c++) {
                if (!(bits & (1u << (FONT_W - 1 - c)))) continue;
                int bx = px + c * s, by = py + r * s;
                for (int dy = 0; dy < s; dy++) {
                    int y = by + dy;
                    if (y < 0 || y >= renderH) continue;
                    unsigned int* row = pixels + (size_t)y * renderW;
                    for (int dx = 0; dx < s; dx++) {
                        int x = bx + dx;
                        if (x >= 0 && x < renderW) row[x] = color;
                    }
                }
            }
        }
        px += FONT_W * s;
    }
}

static void DrawTextCenterWin(int cxWin, int yWin, const char* t, unsigned int color) {
    DrawTextWin(cxWin - TextWidthWin(t) / 2, yWin, t, color);
}

/* 在**渲染坐标**上画 1:1 的点阵字（小地图标题用）。
   与 DrawTextWin 的区别：那个用窗口坐标（会缩放），这个直接落在渲染缓冲上。 */
static void PutTextRaw(int x, int y, const char* t, unsigned int color) {
    for (const char* p = t; *p; p++) {
        int ch = (unsigned char)*p;
        if (ch < 32 || ch > 126) ch = '?';
        const unsigned char* g = fontData[ch - 32];
        for (int r = 0; r < FONT_H; r++) {
            unsigned char bits = g[r];
            if (!bits) continue;
            for (int c = 0; c < FONT_W; c++) {
                if (!(bits & (1u << (FONT_W - 1 - c)))) continue;
                PutPixel(x + c, y + r, color);
            }
        }
        x += FONT_W;
    }
}

/* 渲染坐标下的文字宽度（1:1 点阵） */
static int TextWidthRaw(const char* t) {
    return (int)strlen(t) * FONT_W;
}

/* 标题加粗：右移一格再画一遍 */
static void DrawTitleWin(const char* t, int yWin, unsigned int color) {
    DrawTextCenterWin(winW / 2, yWin - 2 * TextScale(), t, color);
    DrawTextCenterWin(winW / 2 + 1, yWin - 2 * TextScale(), t, color);
}

/* UI 尺寸随窗口高度缩放（以 600 为基准） */
static int UIS(int v) {
    int s = (int)(v * (winH / 600.0) + 0.5);
    return s < 10 ? 10 : s;
}

typedef struct {
    RECT panel;
    int titleY;
    int firstY;
    int rowH;
    int count;
    const char* title;
} MenuGeo;

static MenuGeo MakeGeo(const char* title, int count, int rowH, int pad, int width, int titlePx) {
    MenuGeo g;
    memset(&g, 0, sizeof(g));
    g.title = title;
    g.count = count;
    g.rowH = rowH;

    int titleH = titlePx;
    int innerH = titleH + UIS(24) + count * rowH;
    int panelH = pad * 2 + innerH;
    int panelW = width;
    if (panelW > winW - 60) panelW = winW - 60;
    if (panelW < 160) panelW = 160;

    g.panel.left = (winW - panelW) / 2;
    g.panel.right = g.panel.left + panelW;
    g.panel.top = (winH - panelH) / 2;
    g.panel.bottom = g.panel.top + panelH;
    g.titleY = g.panel.top + pad;
    g.firstY = g.titleY + titleH + UIS(24);
    return g;
}

static MenuGeo GetMenuGeo(void) {
    switch (currentState) {
        case STATE_MAIN_MENU: return MakeGeo("CANITY", MainMenuCount(), UIS(56), UIS(38), UIS(520), UIS(72));
        case STATE_HUB:       return MakeGeo("THE HUB", HUB_ROWS, UIS(50), UIS(34), UIS(700), UIS(52));
        case STATE_HELP:      return MakeGeo("CONTROLS", 15, UIS(29), UIS(22), UIS(1000), UIS(30));
        case STATE_PAUSED:    return MakeGeo("PAUSED", 3, UIS(56), UIS(38), UIS(520), UIS(56));
        case STATE_GAMEOVER:  return MakeGeo("YOU LOST YOUR MIND", 3, UIS(48), UIS(34), UIS(660), UIS(40));
        case STATE_SETTINGS:  return MakeGeo(settingsPage < 1 ? "SETTINGS" : pageNames[settingsPage - 1],
                                          SettingsRowCount(), UIS(44), UIS(30), UIS(680), UIS(44));
        default: break;
    }
    return MakeGeo("", 0, UIS(44), UIS(30), UIS(400), UIS(44));
}

static MenuGeo GetPopupGeo(void) {
    if (popupItem < 0 || popupItem >= SETTING_COUNT) return GetMenuGeo();
    const SettingItem* it = &settings[popupItem];
    if (it->kind == SET_SLIDER) {
        /* 滑块面板：标题 + 数值 + 轨道 */
        MenuGeo g;
        memset(&g, 0, sizeof(g));
        g.title = it->name;
        g.count = 0;
        int pad = UIS(26), titlePx = UIS(34);
        int contentH = UIS(140);
        int panelW = UIS(460);
        if (panelW > winW - 80) panelW = winW - 80;
        int panelH = pad * 2 + titlePx + UIS(24) + contentH;
        g.panel.left = (winW - panelW) / 2;
        g.panel.right = g.panel.left + panelW;
        g.panel.top = (winH - panelH) / 2;
        g.panel.bottom = g.panel.top + panelH;
        g.titleY = g.panel.top + pad;
        g.firstY = g.titleY + titlePx + UIS(24);
        g.rowH = contentH;
        return g;
    }
    return MakeGeo(it->name, it->count, UIS(42), UIS(26), UIS(460), UIS(34));
}

/* 滑块轨道（窗口坐标） */
static RECT SliderTrackRect(MenuGeo g) {
    RECT r;
    int pad = UIS(40);
    r.left = g.panel.left + pad;
    r.right = g.panel.right - pad;
    r.top = g.firstY + UIS(46);
    r.bottom = r.top + UIS(12);
    return r;
}

/* 滑块值 -> 窗口 x */
static int SliderValueToX(MenuGeo g, float v, float lo, float hi) {
    RECT t = SliderTrackRect(g);
    if (hi <= lo) return t.left;
    float f = (v - lo) / (hi - lo);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return t.left + (int)(f * (t.right - t.left));
}

/* 窗口 x -> 滑块值 */
static float SliderXToValue(MenuGeo g, int x, float lo, float hi, float step) {
    RECT t = SliderTrackRect(g);
    float f = (t.right == t.left) ? 0.0f : (float)(x - t.left) / (float)(t.right - t.left);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    float v = lo + f * (hi - lo);
    v = lo + floorf((v - lo) / step + 0.5f) * step;   /* 吸附到步长 */
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

static RECT RowRect(MenuGeo g, int i) {
    RECT r;
    r.left = g.panel.left + UIS(18);
    r.right = g.panel.right - UIS(18);
    r.top = g.firstY + i * g.rowH;
    r.bottom = r.top + g.rowH - UIS(8);
    return r;
}

static int MainMenuCount(void) {
    return hasSave ? 6 : 5;
}

static int MenuItemIndex(void) {
    if (currentState == STATE_SETTINGS) return settingsSelection;
    if (currentState == STATE_HUB) return hubSelection;
    return menuSelection;
}

static const char* MenuLabel(int i) {
    static char buf[128];
    switch (currentState) {
        case STATE_MAIN_MENU: {
            static const char* withSave[] = {"Continue", "New Run", "Hub", "Controls", "Settings", "Quit"};
            static const char* noSave[] = {"New Run", "Hub", "Controls", "Settings", "Quit"};
            return hasSave ? withSave[i] : noSave[i];
        }
        case STATE_PAUSED: {
            static const char* s[] = {"Resume", "Settings", "Main Menu"};
            return s[i];
        }
        case STATE_HELP: {
            /* 操作说明 + 玩法提示。
               游戏画面上的字要尽量少（见 HUD Detail 设置），
               所以「怎么用玻璃瓶」「跳脸虫怎么回事」这类一次性说明
               全部放在这一页。
               行数要控制在 14 行以内：面板高度 = 14×28 + 标题 + 内边距
               ≈ 500，在 600 高的窗口里才留得下边距。 */
            static const char* helps[] = {
                "WASD        MOVE",
                "SHIFT       RUN  (MAKES NOISE)",
                "E (HOLD)    SEARCH / USE",
                "R (HOLD)    CRANK BATTERY",
                "F           FLASHLIGHT",
                "G / B       THROW FLARE / BOTTLE",
                "M MAP   ESC PAUSE",
                "",
                "BOTTLE: THROW IT AT A MONSTER TO HIT IT.",
                "  3 BOTTLES DRIVE IT AWAY, BUT THE SOUND",
                "  GIVES AWAY YOUR POSITION.",
                "",
                "FACE JUMPER: PRESS SPACE TO DODGE IT.",
                "  IF IT LANDS ON YOUR FACE, PRESS THE KEY.",
                "  MISS BOTH AND YOU LOSE SANITY."
            };
            if (i >= 0 && i < 15) return helps[i];
            return "";
        }
        case STATE_HUB: {
            static char hb[96];
            if (i >= 4) return "Back";
            int cost = UpgradeCost(i);
            if (cost < 0) sprintf(hb, "%s   LV%d/%d   MAX", UpgradeName(i), UpgradeLevel(i), UPG_MAX);
            else sprintf(hb, "%s   LV%d/%d   %d SHARDS", UpgradeName(i), UpgradeLevel(i), UPG_MAX, cost);
            return hb;
        }
        case STATE_GAMEOVER: return "Return to Menu";
        case STATE_SETTINGS: {
            if (settingsPage < 1) {
                if (i < 3) return pageNames[i];
                return "Back";
            }
            int idx[SETTING_COUNT];
            int n = PageItems(idx);
            if (i >= n) return "Back";
            const SettingItem* it = &settings[idx[i]];
            if (it->kind == SET_SLIDER) {
                if (it->fvalue == &gammaValue) sprintf(buf, "%s: %.2f", it->name, *it->fvalue);
                else if (it->fvalue == &volumeF) sprintf(buf, "%s: %.0f%%", it->name, *it->fvalue * 100.0f);
                else sprintf(buf, "%s: %.1fx", it->name, *it->fvalue);
            } else {
                sprintf(buf, "%s: %s", it->name, it->options[*it->value]);
            }
            return buf;
        }
        default: return "";
    }
}

/* 窗口坐标 -> 渲染坐标的矩形填充（半透明） */
static void BlendRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha) {
    int rx0 = x0 * renderW / winW, rx1 = x1 * renderW / winW;
    int ry0 = y0 * renderH / winH, ry1 = y1 * renderH / winH;
    if (rx0 < 0) rx0 = 0;
    if (ry0 < 0) ry0 = 0;
    if (rx1 > renderW) rx1 = renderW;
    if (ry1 > renderH) ry1 = renderH;

    int r = (int)((color >> 16) & 0xFF), g = (int)((color >> 8) & 0xFF), b = (int)(color & 0xFF);
    int ia = (int)(alpha * 256.0f);
    int na = 256 - ia;

    for (int y = ry0; y < ry1; y++) {
        unsigned int* row = pixels + (size_t)y * renderW;
        for (int x = rx0; x < rx1; x++) {
            unsigned int d = row[x];
            int nr = (r * ia + ((int)((d >> 16) & 0xFF)) * na) >> 8;
            int ng = (g * ia + ((int)((d >> 8) & 0xFF)) * na) >> 8;
            int nb = (b * ia + ((int)(d & 0xFF)) * na) >> 8;
            row[x] = (unsigned int)((nr << 16) | (ng << 8) | nb);
        }
    }
}

static void FrameRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha, int thick) {
    BlendRectWin(x0, y0, x1, y0 + thick, color, alpha);
    BlendRectWin(x0, y1 - thick, x1, y1, color, alpha);
    BlendRectWin(x0, y0, x0 + thick, y1, color, alpha);
    BlendRectWin(x1 - thick, y0, x1, y1, color, alpha);
}

/* 主面板（不含弹窗）：弹窗必须画在列表文字之上，否则文字会压在弹窗上 */
void DrawMenuPanels(void) {
    MenuGeo g = GetMenuGeo();
    BlendRectWin(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, 0x000a1016, 0.94f);
    FrameRectWin(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, 0x00801818, 0.85f, UIS(2));

    /* 弹窗开着时不再高亮背后的列表行 */
    int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);
    int sel = MenuItemIndex();
    if (!popupOpen && sel >= 0 && sel < g.count) {
        RECT r = RowRect(g, sel);
        BlendRectWin(r.left, r.top, r.right, r.bottom, 0x00205020, 0.55f);
    }
}

/* 弹窗面板：不透明，盖住下面的列表文字 */
void DrawPopupPanel(void) {
    MenuGeo p = GetPopupGeo();
    const SettingItem* it = &settings[popupItem];

    BlendRectWin(p.panel.left, p.panel.top, p.panel.right, p.panel.bottom, 0x0004080c, 1.0f);
    FrameRectWin(p.panel.left, p.panel.top, p.panel.right, p.panel.bottom, 0x00b02020, 0.95f, UIS(2));

    if (it->kind == SET_SLIDER) {
        RECT t = SliderTrackRect(p);
        int bend = UIS(3);
        BlendRectWin(t.left - bend, t.top - bend, t.right + bend, t.bottom + bend, 0x00404040, 0.9f);
        BlendRectWin(t.left, t.top, t.right, t.bottom, 0x00141414, 0.95f);
        int kx = SliderValueToX(p, *it->fvalue, it->fmin, it->fmax);
        BlendRectWin(t.left, t.top, kx, t.bottom, 0x0030a060, 0.95f);
        int k = UIS(8);
        BlendRectWin(kx - k, t.top - k, kx + k, t.bottom + k, 0x00d8e8d8, 0.95f);
        FrameRectWin(kx - k, t.top - k, kx + k, t.bottom + k, 0x00ffffff, 0.55f, 1);
    } else if (popupSelection >= 0 && popupSelection < p.count) {
        RECT r = RowRect(p, popupSelection);
        BlendRectWin(r.left, r.top, r.right, r.bottom, 0x00306030, 0.65f);
    }
}

static void DrawTextInRow(const char* text, RECT row, unsigned int color) {
    int tw = TextWidthWin(text), th = TextHeightWin();
    int x = row.left + ((row.right - row.left) - tw) / 2;
    int y = row.top + ((row.bottom - row.top) - th) / 2;
    DrawTextWin(x, y, text, color);
}

/* 左对齐的一行：说明文字用。
   居中对齐遇到长句子会超出面板边缘（CONTROLS 页的说明就踩过这个坑）。 */
static void DrawTextInRowLeft(const char* text, RECT row, unsigned int color) {
    int th = TextHeightWin();
    int y = row.top + ((row.bottom - row.top) - th) / 2;
    DrawTextWin(row.left + UIS(8), y, text, color);
}

static void DrawMenuText(void) {
    MenuGeo g = GetMenuGeo();
    int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);

    unsigned int titleColor = (currentState == STATE_MAIN_MENU) ? 0x00BE1414u : 0x00D23C3Cu;
    if (currentState == STATE_MAIN_MENU) {
        char sb[48];
        sprintf(sb, "SHARDS %d", shards);
        DrawTextCenterWin(winW / 2, winH - UIS(56), sb, 0x0060D0FFu);
        char bb[48];
        sprintf(bb, "BEST LEVEL %d", bestLevel);
        DrawTextCenterWin(winW / 2, winH - UIS(30), bb, 0x00808080u);
        /* 版本号：右下角低调显示，方便对着存档确认是哪个版本 */
        DrawTextWin(winW - TextWidthWin(CANITY_VERSION_STR) - UIS(14),
                    winH - UIS(30), CANITY_VERSION_STR, 0x00585858u);
    }
    DrawTitleWin(g.title, g.titleY, titleColor);

    int sel = MenuItemIndex();
    for (int i = 0; i < g.count; i++) {
        unsigned int c;
        if (popupOpen) c = 0x00383E3Au;                       /* 弹窗后面的列表压暗 */
        else if (currentState == STATE_HUB && i < 4) {
            int cost = UpgradeCost(i);
            if (cost < 0) c = 0x00707070u;
            else if (shards >= cost) c = (i == sel) ? 0x008CFF8Cu : 0x00A0FFA0u;
            else c = (i == sel) ? 0x00FF8080u : 0x00B06060u;
        }
        else c = (i == sel) ? 0x008CFF8Cu : 0x009B9B9Bu;

        if (currentState == STATE_GAMEOVER && i > 0) {
            /* 死亡复盘：第 2、3 行放统计 */
            char line[96];
            int mins = (int)(runTime / 60.0f);
            int secs = (int)runTime - mins * 60;
            if (i == 1) sprintf(line, "LEVEL %d    TIME %d:%02d", currentLevel, mins, secs);
            else sprintf(line, "CRATES %d    BATTERY %d%%", searchedCount, (int)battery);
            DrawTextInRow(line, RowRect(g, i), (i == sel) ? 0x008CFF8Cu : 0x009B9B9Bu);
            continue;
        }

        /* CONTROLS 页的说明文字用左对齐 ——
           居中对齐遇到长句子会超出面板左右边缘。 */
        if (currentState == STATE_HELP) {
            DrawTextInRowLeft(MenuLabel(i), RowRect(g, i), c);
            continue;
        }
        DrawTextInRow(MenuLabel(i), RowRect(g, i), c);
    }
}

/* 弹窗里的文字：必须在弹窗面板之后画 */
void DrawPopupText(void) {
    if (currentState != STATE_SETTINGS || !settingsPopupOpen) return;
    MenuGeo p = GetPopupGeo();
    const SettingItem* it = &settings[popupItem];
    DrawTitleWin(p.title, p.titleY, 0x00E65A5Au);

    if (it->kind == SET_SLIDER) {
        char val[32];
        int isGamma = (it->fvalue == &gammaValue);
        if (isGamma) sprintf(val, "%.2f", *it->fvalue);
        else sprintf(val, "%.1fx", *it->fvalue);
        DrawTextCenterWin(winW / 2, p.firstY, val, 0x00A0FFA0u);

        RECT t = SliderTrackRect(p);
        char l[32], r[32];
        if (isGamma) {
            sprintf(l, "%.2f", it->fmin);
            sprintf(r, "%.2f", it->fmax);
        } else {
            sprintf(l, "%.1fx", it->fmin);
            sprintf(r, "%.1fx", it->fmax);
        }
        DrawTextWin(t.left, t.bottom + UIS(10), l, 0x008C8C8Cu);
        DrawTextWin(t.right - TextWidthWin(r), t.bottom + UIS(10), r, 0x008C8C8Cu);

        const char* hint = "DRAG OR A/D   ENTER OK";
        DrawTextCenterWin(winW / 2, p.panel.bottom - UIS(38), hint, 0x006E6E6Eu);
    } else {
        for (int i = 0; i < p.count; i++) {
            unsigned int c = (i == popupSelection) ? 0x00A0FFA0u : 0x00A0A0A0u;
            DrawTextInRow(settings[popupItem].options[i], RowRect(p, i), c);
        }
    }
}

/* 进度条：底色 + 填充 + 细边框，和文字一样画在像素层 */
static void DrawBar(int x, int y, int w, int h, float frac, unsigned int color) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    BlendRectWin(x, y, x + w, y + h, 0x00000000, 0.65f);
    int fw = (int)(w * frac);
    if (fw > 0) BlendRectWin(x, y, x + fw, y + h, color, 0.92f);
    FrameRectWin(x, y, x + w, y + h, 0x00909090, 0.55f, 1);
}

void DrawHUD(void) {
    char buf[96];

    /* ------------------------------------------------------------------ */
    /* HUD 详细度                                                        */
    /* ------------------------------------------------------------------ */
    /* 三档（设置里的 HUD Detail）：
         0 Minimal —— 只留必须实时知道的：数值、电量体力、状态效果
         1 Normal  —— 加：投掷物数量（有库存时才显示）、SAFE ZONE 短提示
         2 Full    —— 加：投掷物数量常驻、SAFE ZONE 常驻
       词条横幅在任何档位都只在进关后 5 秒出现（levelBanner 控制），
       因为它是「一次性信息」，常驻反而是噪音。 */

    sprintf(buf, "LEVEL %d", currentLevel);
    DrawTextWin(UIS(20), UIS(18), buf, 0x0000FF00u);

    /* 当前词条：常驻显示在 LEVEL 那一行的右边 —— **不新增一行**。
       词条是难度曲线的一部分（HUNTED 怪更快、LEAKY 理智掉更快），
       玩家看不见就不知道自己在被什么惩罚。
       用暗橙色区分于绿色的 LEVEL。
       详细说明（"THE THING MOVES FASTER"）仍然只在进关 5 秒的横幅里出现，
       常驻的是「提醒」，横幅是「解释」。 */
    if (levelMod >= 0 && levelMod < MOD_COUNT) {
        int lx = UIS(20) + TextWidthWin(buf) + UIS(14);
        sprintf(buf, "%s", modNames[levelMod]);
        DrawTextWin(lx, UIS(18), buf, 0x00C08040u);
    }

    sprintf(buf, "SANITY: %.0f%%", sanity);
    DrawTextWin(UIS(20), UIS(52), buf, (sanity < 30.0f) ? 0x00FF3C3Cu : 0x0000FFFFu);

    if (breakerOpen) sprintf(buf, "POWER ON");
    else sprintf(buf, "FUSES %d/%d", fusesHeld, fusesNeeded);
    DrawTextWin(UIS(20), UIS(86), buf, breakerOpen ? 0x0030FF60u : 0x00E8C040u);

    /* 状态效果：常驻显示在 FUSES 下面。
       玩家必须知道自己被怎么了 —— 否则「视野突然变窄」会被当成 bug。
       纵向排布（UIS 是设计分辨率，600 高）：
         86  FUSES
         116 状态效果 1   142 状态效果 2   168 状态效果 3
         196 FLARE / BOTTLE */
    {
        static const char* fxName[FX_COUNT] = { "BLINDED", "SLOWED", "DRAINING" };
        int fy = UIS(116);
        for (int i = 0; i < FX_COUNT; i++) {
            float s = FxStrength(i);
            if (s <= 0.0f) continue;
            sprintf(buf, "%s %d%%", fxName[i], (int)(s * 100.0f + 0.5f));
            DrawTextWin(UIS(20), fy, buf, 0x00FF6060u);
            fy += UIS(26);
        }
    }

    /* 投掷物数量。
       Minimal：不显示。
       Normal：只在身上有货时才显示（平时不占地方）。
       Full：一直显示。 */
    if (hudDetail >= 2 || (hudDetail >= 1 && (flares > 0 || bottles > 0))) {
        sprintf(buf, "FLARE %d   BOTTLE %d", flares, bottles);
        DrawTextWin(UIS(20), UIS(196), buf, 0x0090A0A0u);
    }

    /* ------------------------------------------------------------------ */
    /* 跳脸虫 QTE                                                        */
    /* ------------------------------------------------------------------ */
    /* 这是整个事件里最关键的一块 UI。玩家只有不到 2 秒的反应时间，
       所以提示必须：**巨大、居中、带明确的倒计时条**。
       任何含糊都会让玩家觉得是游戏 bug。 */
    if (parasite.active && jumperPhase != JMP_IDLE && jumperPhase != JMP_DONE) {
        float frac = 0.0f;
        const char* prompt = "";
        unsigned int col = 0x00FFE060u;

        if (jumperPhase == JMP_DODGE) {
            frac = jumperTimer / JUMPER_DODGE_WINDOW;
            prompt = "PRESS SPACE TO DODGE";
            col = 0x00FF8040u;
        } else if (jumperPhase == JMP_QTE) {
            frac = jumperTimer / JUMPER_QTE_WINDOW;
            static const char* qteKeys[JMP_KEY_COUNT] = { "A", "S", "D", "F" };
            prompt = qteKeys[jumperQteKey];
            col = 0x00FF3030u;
        }
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;

        /* 阶段二：把要按的键画得非常大，一眼就能看到 */
        if (jumperPhase == JMP_QTE) {
            /* 用点阵字放大绘制：把字符画成一个方块阵 */
            int keyW = TextWidthWin(prompt) * 3;
            int keyH = TextHeightWin() * 3;
            int kx = (winW - keyW) / 2;
            int ky = winH / 2 - keyH / 2;
            /* 底板 */
            BlendRectWin(kx - UIS(10), ky - UIS(6), kx + keyW + UIS(10), ky + keyH + UIS(6),
                         0x00202020, 0.75f);
            FrameRectWin(kx - UIS(10), ky - UIS(6), kx + keyW + UIS(10), ky + keyH + UIS(6),
                         col, 0.9f, 2);
            /* 把字放大 3 倍画 */
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    DrawTextWin(kx + (i) * (keyW / 3), ky + (j) * (keyH / 3), prompt, col);
                }
            }
            /* 英文说明只在 Full 档显示 */
            if (qteHint >= 1)
                DrawTextCenterWin(winW / 2, ky - UIS(30), "IT IS ON YOUR FACE", 0x00FF6060u);
        } else {
            /* 阶段一：Minimal 档只显示一个空格键图标，
               Full 档才显示英文说明。 */
            if (qteHint >= 1) {
                DrawTextCenterWin(winW / 2, winH / 2 - UIS(40), prompt, col);
            } else {
                int keyW = TextWidthWin("SPACE") * 2;
                int keyH = TextHeightWin() * 2;
                int kx = (winW - keyW) / 2;
                int ky = winH / 2 - keyH / 2;
                BlendRectWin(kx - UIS(10), ky - UIS(6), kx + keyW + UIS(10), ky + keyH + UIS(6),
                             0x00202020, 0.75f);
                FrameRectWin(kx - UIS(10), ky - UIS(6), kx + keyW + UIS(10), ky + keyH + UIS(6),
                             col, 0.9f, 2);
                for (int i = 0; i < 2; i++)
                    for (int j = 0; j < 2; j++)
                        DrawTextWin(kx + i * (keyW / 2), ky + j * (keyH / 2), "SPACE", col);
            }
        }

        /* 倒计时条：屏幕中央下方一条，越短越红 */
        int barW = UIS(240), barH = UIS(10);
        int bx = (winW - barW) / 2;
        int by = winH / 2 + UIS(56);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00202020, 0.8f);
        unsigned int barCol = col;
        if (frac < 0.35f) barCol = 0x00FF2020u;    /* 快没了 -> 变红 */
        BlendRectWin(bx, by, bx + (int)(barW * frac), by + barH, barCol, 0.95f);
        FrameRectWin(bx, by, bx + barW, by + barH, 0x00808080, 0.7f, 1);
    }

    /* 词条横幅：只在进关后 5 秒出现。
       词条**名**已经常驻在 LEVEL 那一行右边了，所以这里只重复「说明」——
       玩家刚进关时需要知道这一层的规则具体是什么。 */
    if (levelBanner > 0.0f) {
        int y = UIS(258);
        if (levelMod >= 0) {
            DrawTextWin(UIS(20), y, modDesc[levelMod], 0x00C08050u);
        }
        DrawTextWin(UIS(20), y + UIS(30), themeNames[themeId], 0x00B08040u);
    }

    if (showFps) {
        sprintf(buf, "FPS %d  %.1f/%.1fms  %dx%d", currentFps, lastRenderMs, frameMsMax, renderW, renderH);
        DrawTextWin(winW - UIS(20) - TextWidthWin(buf), UIS(18), buf, 0x00C8C8C8u);
    }

    /* 手电电量 / 体力：左下两条细条，标签按实际文字宽度排版 */
    int barW = UIS(180), barH = UIS(12);
    int labelX = UIS(20);
    int barX = labelX + TextWidthWin("LIGHT") + UIS(12);
    int y1 = winH - UIS(96), y2 = winH - UIS(74);

    DrawTextWin(labelX, y1 - UIS(3), "LIGHT", 0x00A0A0A0u);
    DrawBar(barX, y1, barW, barH, battery / 100.0f,
            (flashlightOn && battery > 0.0f) ? 0x00E8E060u : 0x00606040u);
    DrawTextWin(barX + barW + UIS(12), y1 - UIS(3), flashlightOn ? "ON" : "OFF",
                flashlightOn ? 0x00E8E060u : 0x00808080u);

    DrawTextWin(labelX, y2 - UIS(3), "RUN", 0x00A0A0A0u);
    DrawBar(barX, y2, barW, barH, stamina / 100.0f,
            (stamina < 25.0f) ? 0x00FF6A3Cu : 0x0040C060u);

    /* 交互提示 + 长按进度 */
    if (hoverKind != 0) {
        char pbuf[64];
        if (hoverKind == 1) sprintf(pbuf, "HOLD E  SEARCH");
        else if (fusesHeld >= fusesNeeded) sprintf(pbuf, "HOLD E  POWER ON");
        else sprintf(pbuf, "NEED %d MORE FUSE", fusesNeeded - fusesHeld);

        int tw = TextWidthWin(pbuf);
        int px = (winW - tw) / 2;
        int py = winH - UIS(150);
        DrawTextWin(px, py, pbuf, (hoverKind == 2 && fusesHeld < fusesNeeded) ? 0x00C05050u : 0x00E8E8E8u);

        int barW = UIS(240), barH = UIS(8);
        int bx = (winW - barW) / 2;
        int by = py + UIS(30);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00202020, 0.8f);
        if (holdingNow) {
            float f = holdTimer / SEARCH_TIME;
            if (f > 1.0f) f = 1.0f;
            BlendRectWin(bx, by, bx + (int)(barW * f), by + barH, 0x00C8E8C8, 0.95f);
        }
        FrameRectWin(bx, by, bx + barW, by + barH, 0x00808080, 0.7f, 1);
    }

    /* SAFE ZONE：画在**屏幕中央下方**，不占用左侧信息列。
       之前它画在 (20, 190)，跟 FLARE/BOTTLE（也是 196 附近）重叠，
       玩家反馈过字叠在一起。
       Normal 档是「闪一下就消失」：进入安全屋时 safeZoneMsg 置 2 秒，
       之后不再显示 —— 常驻在安全屋里其实没什么用（你知道自己在哪）。 */
    if (safeRoom.active && Dist2(posX, posY, safeRoom.x, safeRoom.y) < SAFE_ZONE_RADIUS * SAFE_ZONE_RADIUS) {
        if (hudDetail >= 2) {
            DrawTextCenterWin(winW / 2, winH - UIS(196), "SAFE ZONE", 0x0030D0D0u);
        } else if (hudDetail >= 1 && safeZoneMsg > 0.0f) {
            DrawTextCenterWin(winW / 2, winH - UIS(196), "SAFE ZONE", 0x0030D0D0u);
        }
    }

    if (cranking) {
        int barW = UIS(240), barH = UIS(8);
        int bx = (winW - barW) / 2;
        int by = winH - UIS(120);
        DrawTextCenterWin(winW / 2, by - UIS(34), "CRANKING", 0x00E8C040u);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00202020, 0.8f);
        float cc = crankTimer / 2.4f;
        if (cc > 1.0f) cc = 1.0f;
        BlendRectWin(bx, by, bx + (int)(barW * cc), by + barH, 0x00E8C040, 0.95f);
        FrameRectWin(bx, by, bx + barW, by + barH, 0x00808080, 0.7f, 1);
    }

    /* 碎片（SHARDS）是大厅货币，游戏过程中用不到 —— 不放进 HUD。
       「M CLOSE MAP」也删掉了：地图标题上已经写着
       「M: FULL  M: CLOSE」，再在底部挂一行是重复的。 */
}

/* ------------------------------------------------------------------ */
/* 一帧的呈现                                                          */
/* ------------------------------------------------------------------ */

void RenderFrame(void) {
    HDC hdc = GetDC(g_hwnd);
    if (!hdc) return;

    int showScene = (currentState == STATE_PLAYING) ||
                    (currentState == STATE_PAUSED) ||
                    (currentState == STATE_SETTINGS && prevState == STATE_PAUSED);

    if (showScene) {
        Render3D();
        if (currentState == STATE_PLAYING && isMapOpen) RenderMap();
        /* 跳脸虫：屏幕空间 overlay，盖在场景之上、HUD 之下 */
        if (currentState == STATE_PLAYING) DrawParasiteSprite();
        if (flashTime > 0.0f)
            memset(pixels, 0xFF, (size_t)renderW * renderH * sizeof(unsigned int));
    } else {
        memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    }

    /* 文字也在这一层，先合成再一次性呈现；弹窗必须在列表文字之后画 */
    if (currentState != STATE_PLAYING) {
        int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);
        DrawMenuPanels();
        DrawMenuText();
        if (popupOpen) {
            DrawPopupPanel();
            DrawPopupText();
        }
    } else if (isMapOpen && mapFullScreen) {
        /* 全屏地图：不画 HUD —— 整个屏幕就是地图，别在上面压一堆数值。
           小窗地图则照常画 HUD（它只是右下角一个叠加层）。 */
    } else {
        DrawHUD();
    }

    if (g_useDIB) {
        if (renderW == winW && renderH == winH) {
            BitBlt(hdc, 0, 0, winW, winH, g_memDC, 0, 0, SRCCOPY);
        } else {
            SetStretchBltMode(hdc, COLORONCOLOR);
            StretchBlt(hdc, 0, 0, winW, winH, g_memDC, 0, 0, renderW, renderH, SRCCOPY);
        }
    } else {
        StretchDIBits(hdc, 0, 0, winW, winH, 0, 0, renderW, renderH,
                      pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    ReleaseDC(g_hwnd, hdc);
}

/* ------------------------------------------------------------------ */
/* 菜单动作                                                            */
/* ------------------------------------------------------------------ */

void StartGame(void) {
    currentState = STATE_PLAYING;
    currentLevel = 1;
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
    flashTime = 0.0f;
    healFlash = 0.0f;
    isMapOpen = 0;
    ClearFx();
    for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
    GenerateLevel();
    UpdateExplored();
    ForceCursor(0);
    firstMouseMove = 1;
    uiDirty = 1;
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
    *settings[item].value = value;
    if (settings[item].value == &resIndex || settings[item].value == &isFullscreen) ApplyDisplayMode();
    else if (settings[item].value == &renderScaleIndex) InitPixels();
    else if (settings[item].value == &fovIndex) ApplyFov();
    uiDirty = 1;
}

void ActivateMenuItem(void) {
    if (currentState == STATE_MAIN_MENU) {
        int base = hasSave ? 1 : 0;
        int sel = menuSelection;

        if (sel == 0 && hasSave) {
            ContinueRun();
        } else if (sel == base) {
            DeleteSave();
            StartGame();
        } else if (sel == base + 1) {
            currentState = STATE_HUB;
            hubSelection = 0;
            uiDirty = 1;
        } else if (sel == base + 2) {
            prevState = STATE_MAIN_MENU;
            currentState = STATE_HELP;
            uiDirty = 1;
        } else if (sel == base + 3) {
            prevState = currentState;
            currentState = STATE_SETTINGS;
            settingsPage = 0;
            settingsSelection = 0;
            settingsPopupOpen = 0;
            uiDirty = 1;
        } else {
            SaveGame();
            PostQuitMessage(0);
        }
    } else if (currentState == STATE_PAUSED) {
        if (menuSelection == 0) {
            currentState = STATE_PLAYING;
            ForceCursor(0);
            firstMouseMove = 1;
            uiDirty = 1;
        } else if (menuSelection == 1) {
            prevState = currentState;
            currentState = STATE_SETTINGS;
            settingsPage = 0;
            settingsSelection = 0;
            settingsPopupOpen = 0;
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
    if (hubSelection >= 4) {
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

    if (settingsPage < 1) {
        /* 根目录：分类页 / Back */
        if (settingsSelection < 3) {
            settingsPage = settingsSelection + 1;
            settingsSelection = 0;
            uiDirty = 1;
        } else {
            currentState = prevState;
            settingsPopupOpen = 0;
            uiDirty = 1;
        }
        return;
    }

    int idx[SETTING_COUNT];
    int n = PageItems(idx);
    if (settingsSelection >= n) {
        /* 分类页的 Back：回到根目录 */
        settingsPage = 0;
        settingsSelection = 0;
        settingsPopupOpen = 0;
        uiDirty = 1;
        return;
    }

    popupItem = idx[settingsSelection];
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
        for (int i = 0; i < g.count; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                if (settingsSelection != i) { settingsSelection = i; uiDirty = 1; }
                return;
            }
        }
        return;
    }

    MenuGeo g = GetMenuGeo();
    for (int i = 0; i < g.count; i++) {
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

            for (int i = 0; i < p.count; i++) {
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
        for (int i = 0; i < g.count; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                settingsSelection = i;
                ActivateSettingRow();
                return;
            }
        }
        return;
    }

    if (currentState == STATE_GAMEOVER) { ActivateMenuItem(); return; }

    MenuGeo g = GetMenuGeo();
    for (int i = 0; i < g.count; i++) {
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
                if (settingsPopupOpen && sliderDragging && popupItem >= 0 &&
                    settings[popupItem].kind == SET_SLIDER) {
                    const SettingItem* it = &settings[popupItem];
                    MenuGeo p = GetPopupGeo();
                    float v = SliderXToValue(p, mx, it->fmin, it->fmax, it->fstep);
                    if (v != *it->fvalue) { *it->fvalue = v; uiDirty = 1; }
                    return 0;
                }
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

        case WM_LBUTTONDOWN: {
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            if (currentState != STATE_PLAYING) ClickMenu(mx, my);
            return 0;
        }

        case WM_LBUTTONUP:
            if (sliderDragging) ReleaseCapture();
            sliderDragging = 0;
            return 0;

        case WM_CAPTURECHANGED:
            sliderDragging = 0;
            return 0;

        case WM_KEYDOWN:
            if (wParam < 256) {
                keys[wParam] = 1;
                /* 记下「刚按下」的边沿，供 QTE 消费。
                   只在游戏进行中记 —— 菜单里按的键不该触发 QTE。 */
                if (currentState == STATE_PLAYING) keyPressed[wParam] = 1;
            }

            if (currentState == STATE_MAIN_MENU) {
                if (wParam == 'W' || wParam == VK_UP) { menuSelection = (menuSelection + MainMenuCount() - 1) % MainMenuCount(); uiDirty = 1; }
                if (wParam == 'S' || wParam == VK_DOWN) { menuSelection = (menuSelection + 1) % MainMenuCount(); uiDirty = 1; }
                if (wParam == VK_RETURN) ActivateMenuItem();
            } else if (currentState == STATE_PAUSED) {
                if (wParam == 'W' || wParam == VK_UP) { menuSelection = (menuSelection + 2) % 3; uiDirty = 1; }
                if (wParam == 'S' || wParam == VK_DOWN) { menuSelection = (menuSelection + 1) % 3; uiDirty = 1; }
                if (wParam == VK_RETURN) ActivateMenuItem();
                if (wParam == VK_ESCAPE) {
                    currentState = STATE_PLAYING;
                    ForceCursor(0);
                    firstMouseMove = 1;
                    uiDirty = 1;
                }
            } else if (currentState == STATE_SETTINGS) {
                if (settingsPopupOpen) {
                    const SettingItem* it = &settings[popupItem];
                    if (it->kind == SET_SLIDER) {
                        float v = *it->fvalue;
                        if (wParam == 'A' || wParam == VK_LEFT) v -= it->fstep;
                        if (wParam == 'D' || wParam == VK_RIGHT) v += it->fstep;
                        if (v < it->fmin) v = it->fmin;
                        if (v > it->fmax) v = it->fmax;
                        if (v != *it->fvalue) { *it->fvalue = v; uiDirty = 1; }
                        if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
                            settingsPopupOpen = 0;
                            sliderDragging = 0;
                            uiDirty = 1;
                        }
                    } else {
                        int n = it->count;
                        if (wParam == 'W' || wParam == VK_UP) { popupSelection = (popupSelection + n - 1) % n; uiDirty = 1; }
                        if (wParam == 'S' || wParam == VK_DOWN) { popupSelection = (popupSelection + 1) % n; uiDirty = 1; }
                        if (wParam == VK_RETURN) { ApplySetting(popupItem, popupSelection); settingsPopupOpen = 0; }
                        if (wParam == VK_ESCAPE) { settingsPopupOpen = 0; uiDirty = 1; }
                    }
                } else {
                    int n = SettingsRowCount();
                    if (wParam == 'W' || wParam == VK_UP) { settingsSelection = (settingsSelection + n - 1) % n; uiDirty = 1; }
                    if (wParam == 'S' || wParam == VK_DOWN) { settingsSelection = (settingsSelection + 1) % n; uiDirty = 1; }
                    if (wParam == VK_RETURN) ActivateSettingRow();
                    if (wParam == VK_ESCAPE) {
                        if (settingsPage > 0) { settingsPage = 0; settingsSelection = 0; }
                        else { currentState = prevState; SaveGame(); }
                        uiDirty = 1;
                    }
                }
            } else if (currentState == STATE_GAMEOVER) {
                if (wParam == VK_RETURN) ActivateMenuItem();
            } else if (currentState == STATE_HELP) {
                if (wParam == VK_ESCAPE || wParam == VK_RETURN) { currentState = STATE_MAIN_MENU; uiDirty = 1; }
            } else if (currentState == STATE_HUB) {
                int n = HUB_ROWS;
                if (wParam == 'W' || wParam == VK_UP) { hubSelection = (hubSelection + n - 1) % n; uiDirty = 1; }
                if (wParam == 'S' || wParam == VK_DOWN) { hubSelection = (hubSelection + 1) % n; uiDirty = 1; }
                if (wParam == VK_RETURN) ActivateHubRow();
                if (wParam == VK_ESCAPE) {
                    SaveGame();
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    ForceCursor(1);
                    uiDirty = 1;
                }
            } else if (currentState == STATE_PLAYING) {
                if (wParam == VK_ESCAPE) PauseGame();
                if (wParam == 'M' || wParam == 'm') {
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
                if (wParam == 'F' || wParam == 'f') {
                    if (battery > 0.0f) flashlightOn = !flashlightOn;
                    uiDirty = 1;
                }
            }
            return 0;

        case WM_KEYUP:
            if (wParam < 256) keys[wParam] = 0;
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/* 音效：程序化合成 + 软件混音（不依赖任何素材文件）                    */
/* ------------------------------------------------------------------ */

#define AUDIO_RATE 22050
#define AUDIO_BUFS 3
#define AUDIO_SAMPLES 1024
#define MAX_VOICES 8

static short g_sndStep[2205];      /* 0.10s 脚步 */
static short g_sndHeart[9922];     /* 0.45s 心跳 */
static short g_sndPickup[6615];    /* 0.30s 拾取 */
static short g_sndPower[19845];    /* 0.90s 通电 */
static short g_sndGate[15435];     /* 0.70s 过关 */
static short g_sndCrank[2647];     /* 0.12s 手摇 */
static short g_sndScream[13230];   /* 0.60s 尖叫 */
static short g_sndGlass[6615];     /* 0.30s 玻璃碎裂 */

typedef struct { const short* data; int len, pos; float vol; int active; } Voice;

static Voice g_voices[MAX_VOICES];
static HWAVEOUT g_wave = NULL;
static WAVEHDR g_hdr[AUDIO_BUFS];
static short g_pcm[AUDIO_BUFS][AUDIO_SAMPLES];
static HANDLE g_audioEvent = NULL;
static HANDLE g_audioThread = NULL;
static volatile LONG g_audioQuit = 0;
static CRITICAL_SECTION g_audioLock;
static int g_audioOn = 0;

static void AudioCallback(HWAVEOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    (void)h; (void)inst; (void)p1; (void)p2;
    if (msg == WOM_DONE && g_audioEvent) SetEvent(g_audioEvent);
}

static void MixInto(short* out) {
    int i;
    for (i = 0; i < AUDIO_SAMPLES; i++) out[i] = 0;

    EnterCriticalSection(&g_audioLock);
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &g_voices[v];
        if (!vo->active) continue;
        for (i = 0; i < AUDIO_SAMPLES; i++) {
            if (vo->pos >= vo->len) { vo->active = 0; break; }
            int sample = (int)(vo->data[vo->pos++] * vo->vol);
            int acc = out[i] + sample;
            if (acc > 32767) acc = 32767;
            else if (acc < -32768) acc = -32768;
            out[i] = (short)acc;
        }
    }
    LeaveCriticalSection(&g_audioLock);
}

static DWORD WINAPI AudioThreadMain(LPVOID param) {
    (void)param;
    while (!g_audioQuit) {
        WaitForSingleObject(g_audioEvent, 100);
        for (int i = 0; i < AUDIO_BUFS; i++) {
            if (g_hdr[i].dwFlags & WHDR_DONE) {
                MixInto(g_pcm[i]);
                g_hdr[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(g_wave, &g_hdr[i], sizeof(WAVEHDR));
            }
        }
    }
    return 0;
}

void PlaySfx(int id, float vol) {
    vol *= volumeF;
    if (!g_audioOn || vol <= 0.001f) return;
    const short* data = NULL;
    int len = 0;
    switch (id) {
        case SFX_STEP:   data = g_sndStep;  len = (int)(sizeof(g_sndStep) / sizeof(short)); break;
        case SFX_HEART:  data = g_sndHeart; len = (int)(sizeof(g_sndHeart) / sizeof(short)); break;
        case SFX_PICKUP: data = g_sndPickup;len = (int)(sizeof(g_sndPickup) / sizeof(short)); break;
        case SFX_POWER:  data = g_sndPower; len = (int)(sizeof(g_sndPower) / sizeof(short)); break;
        case SFX_GATE:   data = g_sndGate;  len = (int)(sizeof(g_sndGate) / sizeof(short)); break;
        case SFX_CRANK:  data = g_sndCrank; len = (int)(sizeof(g_sndCrank) / sizeof(short)); break;
        case SFX_SCREAM: data = g_sndScream;len = (int)(sizeof(g_sndScream) / sizeof(short)); break;
        case SFX_GLASS:  data = g_sndGlass; len = (int)(sizeof(g_sndGlass) / sizeof(short)); break;
        default: return;
    }
    EnterCriticalSection(&g_audioLock);
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++) if (!g_voices[i].active) { slot = i; break; }
    if (slot < 0) slot = 0;    /* 全忙就抢第一路 */
    g_voices[slot].data = data;
    g_voices[slot].len = len;
    g_voices[slot].pos = 0;
    g_voices[slot].vol = vol;
    g_voices[slot].active = 1;
    LeaveCriticalSection(&g_audioLock);
}

/* 合成：噪声、正弦、扫频，全部给 16bit 单声道 */
static void GenSounds(void) {
    unsigned int seed = 12345u;
    /* 脚步：低通噪声 + 快速衰减 */
    float lp = 0.0f;
    for (int i = 0; i < (int)(sizeof(g_sndStep) / sizeof(short)); i++) {
        seed = seed * 1664525u + 1013904223u;
        float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
        lp = lp * 0.72f + n * 0.28f;
        float t = (float)i / AUDIO_RATE;
        float env = expf(-t * 42.0f);
        float v = lp * env * 0.55f;
        g_sndStep[i] = (short)(v * 32000.0f);
    }
    /* 心跳：两声低频闷响 */
    int hl = (int)(sizeof(g_sndHeart) / sizeof(short));
    for (int i = 0; i < hl; i++) {
        float t = (float)i / AUDIO_RATE;
        float v = 0.0f;
        float th[2] = {0.0f, 0.17f};
        for (int k = 0; k < 2; k++) {
            float d = t - th[k];
            if (d < 0.0f || d > 0.22f) continue;
            float env = expf(-d * 16.0f);
            v += sinf(6.2831f * 52.0f * d) * env * (k == 0 ? 1.0f : 0.72f);
        }
        g_sndHeart[i] = (short)(v * 0.55f * 32000.0f);
    }
    /* 拾取：两声上行三角音 */
    int pl = (int)(sizeof(g_sndPickup) / sizeof(short));
    for (int i = 0; i < pl; i++) {
        float t = (float)i / AUDIO_RATE;
        float f = (t < 0.10f) ? 620.0f : 930.0f;
        float env = (t < 0.10f) ? (1.0f - t / 0.10f) : expf(-(t - 0.10f) * 22.0f);
        float v = sinf(6.2831f * f * t) * env * 0.22f;
        g_sndPickup[i] = (short)(v * 32000.0f);
    }
    /* 通电：低频嗡鸣上扫 + 开头一声咔哒 */
    int wl = (int)(sizeof(g_sndPower) / sizeof(short));
    for (int i = 0; i < wl; i++) {
        float t = (float)i / AUDIO_RATE;
        float f = 70.0f + 150.0f * (t / 0.9f);
        float env = expf(-t * 1.6f);
        float v = sinf(6.2831f * f * t) * env * 0.30f;
        if (t < 0.02f) v += ((rand() % 2000) - 1000) / 1000.0f * 0.35f;
        g_sndPower[i] = (short)(v * 32000.0f);
    }
    /* 手摇：两声棘轮咔哒 */
    {
        int cl = (int)(sizeof(g_sndCrank) / sizeof(short));
        for (int i = 0; i < cl; i++) {
            float t = (float)i / AUDIO_RATE;
            float v = 0.0f;
            float ticks[2] = {0.0f, 0.055f};
            for (int k = 0; k < 2; k++) {
                float d = t - ticks[k];
                if (d < 0.0f || d > 0.03f) continue;
                seed = seed * 1664525u + 1013904223u;
                float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
                v += n * expf(-d * 140.0f) * 0.5f;
            }
            g_sndCrank[i] = (short)(v * 32000.0f);
        }
    }
    /* 尖叫：锯齿感的上扫 + 颤抖 */
    {
        int sl = (int)(sizeof(g_sndScream) / sizeof(short));
        for (int i = 0; i < sl; i++) {
            float t = (float)i / AUDIO_RATE;
            float f = 420.0f + 260.0f * sinf(t * 3.5f) + 90.0f * sinf(t * 41.0f);
            float env = expf(-t * 3.2f);
            float v = sinf(6.2831f * f * t + 2.0f * sinf(t * 55.0f));
            g_sndScream[i] = (short)(v * env * 0.42f * 32000.0f);
        }
    }
    /* 玻璃碎裂：高通噪声爆发 + 几片细碎的高频余响 */
    {
        int bl = (int)(sizeof(g_sndGlass) / sizeof(short));
        float hp = 0.0f;
        for (int i = 0; i < bl; i++) {
            float t = (float)i / AUDIO_RATE;
            seed = seed * 1664525u + 1013904223u;
            float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
            hp = n - hp * 0.35f;                 /* 一阶高通：去掉低频，保留脆响 */
            float v = hp * expf(-t * 26.0f);
            /* 三片碎玻璃的高频点缀 */
            for (int k = 0; k < 3; k++) {
                float st = 0.045f + k * 0.038f;
                float d = t - st;
                if (d >= 0.0f && d < 0.012f) {
                    seed = seed * 1664525u + 1013904223u;
                    float sn = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
                    v += sn * expf(-d * 300.0f) * 0.55f;
                }
            }
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            g_sndGlass[i] = (short)(v * 0.55f * 32000.0f);
        }
    }
    /* 过关：噪声下扫的气流声 */
    int gl = (int)(sizeof(g_sndGate) / sizeof(short));
    lp = 0.0f;
    for (int i = 0; i < gl; i++) {
        seed = seed * 1664525u + 1013904223u;
        float n = ((float)((seed >> 16) & 0x7FFF) / 32767.0f) * 2.0f - 1.0f;
        float t = (float)i / AUDIO_RATE;
        float k = 0.15f + 0.6f * (t / 0.7f);
        lp = lp * (1.0f - k) + n * k;
        float env = sinf(3.1416f * (t / 0.7f));
        g_sndGate[i] = (short)(lp * env * 0.5f * 32000.0f);
    }
}

void InitAudio(void) {
    GenSounds();
    InitializeCriticalSection(&g_audioLock);
    g_audioEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_audioEvent) return;

    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = AUDIO_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = AUDIO_RATE * 2;

    if (waveOutOpen(&g_wave, WAVE_MAPPER, &fmt, (DWORD_PTR)AudioCallback, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        g_wave = NULL;
        return;
    }
    for (int i = 0; i < AUDIO_BUFS; i++) {
        memset(&g_hdr[i], 0, sizeof(WAVEHDR));
        g_hdr[i].lpData = (LPSTR)g_pcm[i];
        g_hdr[i].dwBufferLength = AUDIO_SAMPLES * sizeof(short);
        waveOutPrepareHeader(g_wave, &g_hdr[i], sizeof(WAVEHDR));
        MixInto(g_pcm[i]);
        waveOutWrite(g_wave, &g_hdr[i], sizeof(WAVEHDR));
    }
    g_audioThread = CreateThread(NULL, 0, AudioThreadMain, NULL, 0, NULL);
    g_audioOn = 1;
}

void ShutdownAudio(void) {
    if (!g_audioOn && !g_wave) return;
    g_audioOn = 0;
    InterlockedExchange(&g_audioQuit, 1);
    if (g_audioEvent) SetEvent(g_audioEvent);
    if (g_audioThread) { WaitForSingleObject(g_audioThread, 500); CloseHandle(g_audioThread); g_audioThread = NULL; }
    if (g_wave) {
        waveOutReset(g_wave);
        for (int i = 0; i < AUDIO_BUFS; i++) waveOutUnprepareHeader(g_wave, &g_hdr[i], sizeof(WAVEHDR));
        waveOutClose(g_wave);
        g_wave = NULL;
    }
    if (g_audioEvent) { CloseHandle(g_audioEvent); g_audioEvent = NULL; }
    DeleteCriticalSection(&g_audioLock);
}

/* ------------------------------------------------------------------ */
/* 存档与设置持久化                                                     */
/* ------------------------------------------------------------------ */

#define SAVE_MAGIC 0x43414E32u   /* "CAN2" */
/* 存档**格式**版本。v5 在末尾追加了 HUD 详细度 / QTE 提示两个字段。
   读档时会按文件的**实际长度**判断是老版本还是新版本：
   老存档照样能读（新增字段取默认值），不用丢玩家进度。
   —— 参见 LoadSettings 里的迁移逻辑。 */
#define SAVE_VERSION 5
/* 测试时可以把存档路径换掉，避免覆盖玩家真正的存档 */
#ifndef SAVE_FILE
#define SAVE_FILE "canity.sav"
#endif

typedef struct {
    unsigned int magic;
    int version;
    int gameVersion;      /* 写入时的 CANITY_VERSION_NUM，用来识别跨版本存档 */

    /* 设置 */
    int resIndex, fullscreen, fpsCap, scale, fov, vignette, showFps;
    float gamma, sens;

    /* 大厅进度 */
    int shards, upgBattery, upgSpeed, upgQuiet, upgSanity;
    int bestLevel;
    float volume;

    /* 进度 */
    int hasRun, level, fusesHeld, fusesNeeded, breakerOpen, breakerActive, searchedCount, nContainers;
    int flashlightOn;
    float sanity, battery, stamina, posX, posY, dirX, dirY, exitX, exitY, breakerX, breakerY, runTime;
    Container cont[MAX_CONTAINERS];
    int worldMap[MAP_HEIGHT][MAP_WIDTH];
    int explored[MAP_HEIGHT][MAP_WIDTH];

    /* v4 新增：以前这些状态没存，读档后会丢失 */
    int flares, bottles;                      /* 手上的投掷物数量 */
    int safeActive;                           /* 安全屋（花）是否存在 */
    float safeX, safeY;                       /* 安全屋位置 */
    int levelMod;                             /* 本层词条编号，-1 = 无 */
    float modSanityMul, modSpeedMul;          /* 词条修正系数 */
    int themeId;                              /* 关卡主题（墙/地贴图） */
    int nThrown;                              /* 场上投掷物个数 */
    Thrown thr[MAX_THROWN];                   /* 场上投掷物状态 */

    /* v5 新增：HUD 详细度与 QTE 提示档位。
       放在结构**末尾**是刻意的 —— 这样旧存档的字节完全对得上，
       读档时按文件长度就能判断是不是老版本（见 LoadSettings）。 */
    int hudDetail;
    int qteHint;
} SaveBlob;

static SaveBlob g_save;

static void SaveGame(void) {
    memset(&g_save, 0, sizeof(g_save));
    g_save.magic = SAVE_MAGIC;
    g_save.version = SAVE_VERSION;
    g_save.gameVersion = CANITY_VERSION_NUM;

    g_save.resIndex = resIndex;
    g_save.fullscreen = isFullscreen;
    g_save.fpsCap = fpsCapIndex;
    g_save.scale = renderScaleIndex;
    g_save.fov = fovIndex;
    g_save.vignette = vignetteOn;
    g_save.showFps = showFps;
    g_save.hudDetail = hudDetail;
    g_save.qteHint = qteHint;
    g_save.gamma = gammaValue;
    g_save.sens = mouseSens;
    g_save.shards = shards;
    g_save.upgBattery = upgBattery;
    g_save.upgSpeed = upgSpeed;
    g_save.upgQuiet = upgQuiet;
    g_save.upgSanity = upgSanity;
    g_save.bestLevel = bestLevel;
    g_save.volume = volumeF;

    g_save.hasRun = runInProgress;
    g_save.level = currentLevel;
    g_save.sanity = sanity;
    g_save.battery = battery;
    g_save.stamina = stamina;
    g_save.flashlightOn = flashlightOn;
    g_save.posX = posX; g_save.posY = posY;
    g_save.dirX = dirX; g_save.dirY = dirY;
    g_save.exitX = exitX; g_save.exitY = exitY;
    g_save.fusesHeld = fusesHeld;
    g_save.fusesNeeded = fusesNeeded;
    g_save.breakerOpen = breakerOpen;
    g_save.breakerActive = breaker.active;
    g_save.breakerX = breaker.x;
    g_save.breakerY = breaker.y;
    g_save.searchedCount = searchedCount;
    g_save.runTime = runTime;
    g_save.nContainers = containerCount;
    for (int i = 0; i < MAX_CONTAINERS; i++) g_save.cont[i] = containers[i];
    memcpy(g_save.worldMap, worldMap, sizeof(worldMap));
    memcpy(g_save.explored, explored, sizeof(explored));

    /* v4 新增字段 */
    g_save.flares = flares;
    g_save.bottles = bottles;
    g_save.safeActive = safeRoom.active;
    g_save.safeX = safeRoom.x;
    g_save.safeY = safeRoom.y;
    g_save.levelMod = levelMod;
    g_save.modSanityMul = modSanityMul;
    g_save.modSpeedMul = modSpeedMul;
    g_save.themeId = themeId;
    g_save.nThrown = 0;
    for (int i = 0; i < MAX_THROWN; i++) {
        g_save.thr[i] = thrown[i];
        if (thrown[i].active) g_save.nThrown++;
    }

    FILE* f = fopen(SAVE_FILE, "wb");
    if (!f) return;
    fwrite(&g_save, sizeof(g_save), 1, f);
    fclose(f);
}

/* 只读设置（启动时），返回是否还有可继续的进度 */
static int LoadSettings(void) {
    FILE* f = fopen(SAVE_FILE, "rb");
    if (!f) return 0;
    memset(&g_save, 0, sizeof(g_save));
    /* 按文件的**实际字节数**读，而不是 sizeof(SaveBlob) ——
       这样老存档（结构更短）也能读进来，新字段留在 0。 */
    size_t fileSize = 0;
    fseek(f, 0, SEEK_END);
    long fs = ftell(f);
    if (fs > 0) fileSize = (size_t)fs;
    fseek(f, 0, SEEK_SET);
    size_t want = (fileSize < sizeof(g_save)) ? fileSize : sizeof(g_save);
    size_t got = fread(&g_save, 1, want, f);
    fclose(f);
    if (!got || g_save.magic != SAVE_MAGIC) return 0;

    /* 存档**格式**版本：接受 v4（老结构）与 v5（当前结构）。
       v4 的存档按文件长度就能认出来 —— 因为新增字段在结构末尾，
       老文件的字节数正好少 sizeof(int) * 2。
       这里**不**做「版本不符就整份丢弃」：那两个新字段只是显示偏好，
       丢掉它们没有意义，而丢弃整份存档会让玩家进度全没。 */
    if (g_save.version != SAVE_VERSION && g_save.version != 4) return 0;
    int isOldV4 = (fileSize < sizeof(SaveBlob));

    /* 迁移：老存档没有这两个字段，给默认值 */
    if (isOldV4) {
        g_save.hudDetail = 1;    /* Normal */
        g_save.qteHint = 1;      /* Full */
    }
    /* 边界检查：存档可能被外部改坏，数值必须夹回合法范围 */
    if (g_save.hudDetail < 0 || g_save.hudDetail > 2) g_save.hudDetail = 1;
    if (g_save.qteHint < 0 || g_save.qteHint > 1) g_save.qteHint = 1;

    /* 注意这跟 gameVersion 是两回事 ——
       gameVersion 只是记录「这份存档是哪个应用版本写的」，
       升个补丁版本（比如 1.0.0 -> 1.0.1）不该让玩家丢进度，
       所以这里**不**拿它做加载门槛，只有 SAVE_VERSION 才是。 */

    resIndex = g_save.resIndex;
    isFullscreen = g_save.fullscreen;
    fpsCapIndex = g_save.fpsCap;
    renderScaleIndex = g_save.scale;
    fovIndex = g_save.fov;
    vignetteOn = g_save.vignette;
    showFps = g_save.showFps;
    hudDetail = g_save.hudDetail;
    qteHint = g_save.qteHint;
    gammaValue = g_save.gamma;
    mouseSens = g_save.sens;
    shards = g_save.shards;
    upgBattery = g_save.upgBattery;
    upgSpeed = g_save.upgSpeed;
    upgQuiet = g_save.upgQuiet;
    upgSanity = g_save.upgSanity;
    bestLevel = g_save.bestLevel;
    volumeF = g_save.volume;
    if (volumeF < 0.0f || volumeF > 1.0f) volumeF = 0.8f;
    if (shards < 0) shards = 0;
    if (upgBattery < 0 || upgBattery > UPG_MAX) upgBattery = 0;
    if (upgSpeed < 0 || upgSpeed > UPG_MAX) upgSpeed = 0;
    if (upgQuiet < 0 || upgQuiet > UPG_MAX) upgQuiet = 0;
    if (upgSanity < 0 || upgSanity > UPG_MAX) upgSanity = 0;

    if (resIndex < 0 || resIndex > 4) resIndex = 0;
    if (fpsCapIndex < 0 || fpsCapIndex > 4) fpsCapIndex = 1;
    if (renderScaleIndex < 0 || renderScaleIndex > 3) renderScaleIndex = 0;
    if (fovIndex < 0 || fovIndex > 4) fovIndex = 2;
    if (gammaValue < 0.7f || gammaValue > 1.6f) gammaValue = 1.0f;
    if (mouseSens < 0.2f || mouseSens > 3.0f) mouseSens = 1.0f;
    return g_save.hasRun;
}

/* 把存档里的进度灌回运行时 */
static void ContinueRun(void) {
    currentLevel = g_save.level;
    sanity = g_save.sanity;
    battery = g_save.battery;
    stamina = g_save.stamina;
    flashlightOn = g_save.flashlightOn;
    posX = g_save.posX; posY = g_save.posY;
    dirX = g_save.dirX; dirY = g_save.dirY;
    exitX = g_save.exitX; exitY = g_save.exitY;
    fusesHeld = g_save.fusesHeld;
    fusesNeeded = g_save.fusesNeeded;
    breakerOpen = g_save.breakerOpen;
    breaker.active = g_save.breakerActive;
    breaker.x = g_save.breakerX;
    breaker.y = g_save.breakerY;
    searchedCount = g_save.searchedCount;
    runTime = g_save.runTime;
    containerCount = g_save.nContainers;
    if (containerCount < 0 || containerCount > MAX_CONTAINERS) containerCount = 0;
    for (int i = 0; i < MAX_CONTAINERS; i++) containers[i] = g_save.cont[i];
    memcpy(worldMap, g_save.worldMap, sizeof(worldMap));
    memcpy(explored, g_save.explored, sizeof(explored));

    /* v4 新增：安全屋 / 投掷物 / 词条 / 主题 */
    flares = g_save.flares;
    bottles = g_save.bottles;
    if (flares < 0) flares = 0;
    if (bottles < 0) bottles = 0;
    safeRoom.active = g_save.safeActive;
    safeRoom.x = g_save.safeX;
    safeRoom.y = g_save.safeY;
    levelMod = g_save.levelMod;
    if (levelMod < -1 || levelMod >= MOD_COUNT) levelMod = -1;
    modSanityMul = g_save.modSanityMul;
    modSpeedMul = g_save.modSpeedMul;
    if (modSanityMul <= 0.0f) modSanityMul = 1.0f;
    if (modSpeedMul <= 0.0f) modSpeedMul = 1.0f;
    themeId = g_save.themeId;
    if (themeId < 0 || themeId > 3) themeId = 0;
    for (int i = 0; i < MAX_THROWN; i++) thrown[i] = g_save.thr[i];

    ApplyFov();
    GenerateThemeTextures(themeId);   /* 主题变了，墙面/地板贴图要跟着重建 */
    SpawnMonster();
    SpawnLightBeast();
    /* 读档不重掷跳脸虫：本关不会突然蹦出来。
       触发器留成 -1，等下一层进关时再掷。 */
    jumperTrigger = -1.0f;
    jumperPhase = JMP_IDLE;
    jumperTimer = 0.0f;
    parasite.active = 0;
    runInProgress = 1;
    currentState = STATE_PLAYING;
    isMapOpen = 0;
    flashTime = 0.0f;
    healFlash = 0.0f;
    ClearFx();
    for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
    ForceCursor(0);
    firstMouseMove = 1;
    uiDirty = 1;
}

static void DeleteSave(void) {
    remove(SAVE_FILE);
    g_save.hasRun = 0;
    hasSave = 0;
    runInProgress = 0;
}

/* ------------------------------------------------------------------ */
/* 入口：固定步长逻辑 + 可限帧渲染                                      */
/* ------------------------------------------------------------------ */

/* 正常构建时这里就是标准的程序入口。
   离线自检程序会用 `#define WinMain CanityAppMain` 把本函数改名，
   这样它就能 include 整个 main.c 并自己定义 main() —— 但那不影响本文件的独立性，
   单独编译 main.c 依然得到完整的游戏。 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    SetProcessDPIAware();
    srand((unsigned int)time(NULL));
    timeBeginPeriod(1);

    hasSave = LoadSettings();
    InitAudio();

    InitPixels();
    GenerateThemeTextures(0);   /* 启动用 BRICK 主题 */
    InitWorkers();

    const char CLASS_NAME[] = "CanityEngine";
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(0, CLASS_NAME, "Canity v" CANITY_VERSION_STR,
                            WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, 816, 639,
                            NULL, NULL, hInstance, NULL);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, nCmdShow);
    ForceCursor(1);
    ApplyDisplayMode();

    LARGE_INTEGER freq, now, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    double accum = 0.0;       /* 逻辑累加器 */
    double clock = 0.0;       /* 呈现排程时钟（秒，绝对；睡眠处会刷新） */
    double clockPrev = 0.0;   /* 上一次循环顶部的采样，仅用于算 dt */
    double nextRender = 0.0;  /* 下一帧的呈现时刻 */
    double fpsAcc = 0.0;
    int fpsFrames = 0;

    int running = 1;
    MSG msg;
    while (running) {
        int guard = 0;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (++guard > 256) break;   /* 防止任何消息风暴把渲染饿死 */
        }
        if (!running) break;

        QueryPerformanceCounter(&now);
        double absNow = (double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
        /* dt 必须基于「上一次循环顶部」的采样：睡眠/自旋里刷新的 clock 不能参与，
           否则一帧的真实时长会被算少，逻辑就变慢动作 */
        double dt = absNow - clockPrev;
        clockPrev = absNow;
        clock = absNow;
        g_timeSec = absNow;
        if (dt > 0.25) dt = 0.25;
        if (dt < 0.0) dt = 0.0;

        /* gamma 变动时重建查表（只在改动的那一帧做） */
        if (gammaValue != gammaBuilt) { BuildShadeTables(); gammaBuilt = gammaValue; }

        /* --- 逻辑：固定 120Hz 步进，与渲染帧率完全无关 --- */
        accum += dt;
        int steps = 0;
        while (accum >= LOGIC_STEP && steps < 16) {
            Update((float)LOGIC_STEP);
            accum -= LOGIC_STEP;
            steps++;
        }
        if (steps >= 16) accum = 0.0;
        /* 一批逻辑更新跑完后再清按键边沿。
           必须在批处理**之后**清 —— 一帧可能跑多次 Update，
           在 Update 里清会把按键吞掉。 */
        ClearKeyEdges();

        /* --- 渲染：按绝对时刻排程，帧数上限不会被渲染耗时吃掉 --- */
        int cap = fpsCaps[fpsCapIndex];
        double interval = (cap > 0) ? 1.0 / (double)cap : 0.0;

        int playing = (currentState == STATE_PLAYING);
        int doRender = playing ? (clock >= nextRender) : (uiDirty != 0);

        if (doRender) {
            /* 最小化时只清脏标记，避免 uiDirty 常挂导致忙等空转 */
            if (!IsIconic(g_hwnd)) {
                LARGE_INTEGER t0, t1;
                QueryPerformanceCounter(&t0);
                RenderFrame();
                QueryPerformanceCounter(&t1);
                lastRenderMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
                if (lastRenderMs > frameMsMax) frameMsMax = lastRenderMs;
                fpsFrames++;
            }
            uiDirty = 0;
            if (playing) {
                /* 从“上次计划的时刻”前进一个间隔，渲染耗时不叠加到周期里；
                   落后太多就重新对齐，避免追帧 */
                nextRender += interval;
                if (nextRender < clock) nextRender = clock + interval;
            }
        }

        fpsAcc += dt;
        if (fpsAcc >= 0.5) {
            currentFps = (int)(fpsFrames / fpsAcc + 0.5);
            fpsFrames = 0;
            fpsAcc = 0.0;
            frameMsMax = lastRenderMs;
        }

        /* --- 让出 CPU：先睡大头，最后 3ms 自旋，避免 Sleep 粒度吃掉帧预算 --- */
        if (playing) {
            if (interval > 0.0) {
                double remain = nextRender - clock;
                if (remain > 0.007) {
                    Sleep((DWORD)((remain - 0.004) * 1000.0));
                    QueryPerformanceCounter(&now);
                    clock = (double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
                }
                while (clock < nextRender) {
                    QueryPerformanceCounter(&now);
                    clock = (double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
                }
            } else {
                Sleep(0); /* 不限制帧数时也要让出时间片 */
            }
        } else {
            Sleep(uiDirty ? 0 : 4);
        }
    }

    if (currentState == STATE_PLAYING || currentState == STATE_PAUSED) SaveGame();
    if (g_memDC) DeleteDC(g_memDC);
    if (g_dib) DeleteObject(g_dib);
    if (!g_useDIB) free(pixels);
    free(vignetteMask);
    ShutdownAudio();
    ShutdownWorkers();
    timeEndPeriod(1);
    return 0;
}

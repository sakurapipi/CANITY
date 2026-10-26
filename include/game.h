/*
 * Canity — 游戏核心：世界 / 关卡 / 规则 / 输入 / 菜单状态机 / 窗口过程
 *
 * 所有全局状态在 game.c 里定义并赋初值，这里只做 extern + 原型。
 */
#ifndef CANITY_GAME_H
#define CANITY_GAME_H

#include "common.h"

/* ---- 窗口与状态机 ---- */
extern int winW, winH;              /* 窗口客户区尺寸（逻辑/显示分辨率） */
extern HWND g_hwnd;
extern GameState currentState, prevState;
extern int gameWon;                 /* 0=失败结局 1=通关结局（第 10 层逃出生天） */

/* ---- 菜单 ---- */
extern int menuSelection, settingsSelection, settingsPopupOpen;
extern int popupSelection, popupItem;
extern int rebindingAct;            /* >=0 = 正在等待按键改绑的动作，-1 = 未在改绑 */
extern int rebindConflict;          /* 改绑确认中：目标键被该动作占用，-1 = 无冲突 */
extern int resetDefaultsFlag;
extern int uiDirty;
extern int sliderDragging;

/* ---- 系统设置 ---- */
extern int resIndex;
extern const int resolutions[5][2];
extern int isFullscreen;
extern int showFps;
extern int fpsCapIndex;             /* 30 / 60 / 120 / 144 / 不限制 */
extern const int fpsCaps[5];
extern int renderScaleIndex;        /* 0..3 = 100/75/50/25% */
extern const int renderScales[4];
extern float mouseSens;             /* 0.2x - 3.0x */
extern int vignetteOn;              /* 低理智红边特效 */
extern int hudDetail;               /* 0=Minimal 1=Normal 2=Full */
extern int qteHint;                 /* 0=Minimal 1=Full */
extern int fovIndex;                /* 66 / 75 / 85 / 95 / 105 度 */
extern float gammaValue;
extern float gammaBuilt;

/* ---- FPS 统计 ---- */
extern int currentFps;
extern double lastRenderMs;         /* 最近一帧（含呈现与 HUD）的耗时 */
extern double frameMsMax;           /* 最近一秒内的最差帧，用来观察卡顿 */

/* ---- 世界 ---- */
extern int worldMap[MAP_HEIGHT][MAP_WIDTH];
extern int explored[MAP_HEIGHT][MAP_WIDTH];
extern int currentLevel;
extern float exitX, exitY;
extern float flashTime;             /* 切关白屏剩余秒数 */
extern float healFlash;             /* 回血白闪剩余秒数 */

/* ---- 关卡词条 ---- */
extern int levelMod;
extern float modSanityMul;
extern float modSpeedMul;
extern float levelBanner;           /* 进关后显示词条名的秒数 */
extern float safeZoneMsg;           /* 进入安全屋后提示「SAFE ZONE」的剩余秒数 */

/* ---- 局内资源 / 升级 ---- */
extern int shards;
extern int upgBattery;              /* 电量上限 +20/级 */
extern int upgSpeed;                /* 移速 +6%/级 */
extern int upgQuiet;                /* 噪声半径 -15%/级 */
extern int upgSanity;               /* 理智流失 -10%/级 */
extern int upgEco;                  /* 手电耗电 -12%/级 */
extern int upgRest;                 /* 安全屋回智 +25%/级 */
extern int hubSelection;

/* ---- 投掷物 ---- */
extern int flares, bottles;
extern int throwSelected;           /* 0=荧光棒, 1=玻璃瓶, 2=预留 */
extern Thrown thrown[MAX_THROWN];
extern Shot shot[MAX_SHOTS];
extern Shard shards_debris[MAX_SHOTS * SHARD_COUNT];

/* ---- 场景 ---- */
extern SafeRoom safeRoom;
extern Container containers[MAX_CONTAINERS];
extern int containerCount;
extern Breaker breaker;

/* ---- 运行状态 ---- */
extern int fusesHeld;
extern int fusesNeeded;
extern int breakerOpen;
extern float runTime;
extern float stepTimer;
extern float crankTimer;
extern float throwCd;
extern float crankClick;
extern int cranking;
extern int isCharging;              /* 是否正在蓄力投掷 */
extern float mouseCharge;           /* 蓄力进度 0..1 */
extern int resetInputState;         /* 请求重置输入边缘状态（跨局/菜单切换用） */
extern double g_timeSec;            /* 主循环时钟，渲染用它做动画 */
extern float heartTimer;
extern int searchedCount;
extern int hasSave;
extern const char* pickupMsg;
extern float pickupMsgTimer;
extern int bestLevel;
extern float volumeF;               /* 0..1，0 = 静音 */
extern int runInProgress;

/* ---- 交互 ---- */
extern int hoverKind;               /* 0=无 1=容器 2=配电箱 */
extern int hoverIndex;
extern float holdTimer;
extern int holdingNow;

/* ---- 输入 ---- */
extern int keys[256];
extern int keyPressed[256];
extern POINT lastMousePos;
extern int firstMouseMove;
extern int isMapOpen;
extern int mapFullScreen;
extern int g_bindings[ACT_COUNT];

/* ---- 设置模型 ---- */
extern int settingsPage;
extern int g_forceMod;
extern int menuScrollOffsets[8];   /* 每个菜单状态的滚动偏移 */
extern SettingItem settings[SETTING_COUNT];
extern const char* optKeys[OPT_KEY_COUNT];
extern const int optKeyVKs[OPT_KEY_COUNT];
extern const char* actionNames[ACT_COUNT];
extern const char* rootCategoryNames[3];
extern const char* bindCategoryNames[3];

/* ---- 主逻辑 ---- */
void Update(float dt);
void ClearKeyEdges(void);
void ApplyDisplayMode(void);
void ForceCursor(int visible);

/* ---- 输入查询（entity.c 的 QTE 也要用） ---- */
int IsActionPressed(ActionId act);

/* ---- 世界碰撞 ---- */
int Blocked(float x, float y);
int ShotBlocked(float x, float y);
int SpotClear(float x, float y, float minD, int selfKind, int selfIdx);

/* ---- 设置模型 ---- */
const char* PageTitle(int page);
int PageItems(int* out);
int SettingsRowCount(void);
const char* VkName(int vk);
int BindActionOfItem(const SettingItem* it);
int IsKeyBindItem(const SettingItem* it);

/* ---- 升级 ---- */
int BatteryMax(void);
int UpgradeLevel(int kind);
const char* UpgradeName(int kind);
int UpgradeCost(int kind);

/* ---- 菜单动作 ---- */
void StartGame(void);
void PauseGame(void);
void ApplySetting(int item, int value);
void ActivateMenuItem(void);
void ActivateHubRow(void);
void ActivateSettingRow(void);

/* ---- 成就追踪 ---- */
extern int totalShardsEarned;
extern int bottlesKilled;
extern int faceDodged;
extern int crankTimeTotal;
extern int fusesCollectedRun;
extern int mapsFullyExplored;
extern int tookDamage;
extern int flashlightOffLevel;
extern int sprintedThisLevel;

/* ---- 窗口过程 ---- */
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif /* CANITY_GAME_H */

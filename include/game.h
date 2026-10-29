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
extern int bestEndlessLevel;      /* 无尽模式最高层数 */
extern int bestEndlessScore;      /* 无尽模式最高分 */
extern float volumeF;               /* 0..1，0 = 静音 */
extern int runInProgress;
extern int isEndlessRun;            /* 当前是否为无尽模式 */

/* ---- 交互 ---- */
extern int hoverKind;               /* 0=无 1=容器 2=配电箱 */
extern int hoverIndex;
extern float holdTimer;
extern int holdingNow;

/* ---- 上锁容器（v1.8.0） ----
   contLocked/contLockType/contLockCode 与 containers[] 一一对应，
   独立于 Container 结构，因此不会改变 SaveBlob 的既有布局。
   contLockCode 是**谜题参数**，含义随锁型变：
     拨盘   = 3 个 4bit 数字拼成的密码
     接线   = 右端子的排列编号（0..5）
     均衡器 = 3 根推子的目标档位（每根 3bit 打包） */
extern int contLocked[MAX_CONTAINERS];
extern int contLockCode[MAX_CONTAINERS];
extern int contLockType[MAX_CONTAINERS];
extern int lockActive;                     /* 1 = 开锁覆盖层打开（游戏不暂停） */
extern int lockTarget;                     /* 目标容器下标，-1 = 无 */
extern int lockCursor;                     /* 拨盘锁：当前选中的拨盘 */
extern int lockDigits[LOCK_DIALS];         /* 拨盘锁：三个拨盘当前显示的值 */
extern int lockType;                       /* 当前打开的锁型（LOCK_TYPE_*） */

/* 接线锁的运行时状态 */
extern int lockWireSel;                    /* 选中的左端子，-1 = 无 */
extern int lockWireLink[LOCK_WIRES];       /* 左 i 连到的右端子，-1 = 未连 */
/* 均衡器锁的运行时状态 */
extern int lockEqVal[LOCK_EQ_BANDS];       /* 当前档位 */
/* 调波器锁的运行时状态（0=振幅 1=频率 2=相位） */
extern int lockTuneVal[LOCK_TUNE_KNOBS];   /* 当前档位 */

/* 纯函数：从打包的密码里取第 dial 位 / 比对对了几个位置 */
int LockDigit(int code, int dial);
int LockCorrectCount(int code, const int* digits);

/* 覆盖层进出。三种锁型都在“解开”时自动开锁 */
void OpenLockOverlay(int index);
void CloseLockOverlay(void);

/* 拨盘锁 */
void LockTurnDial(int dial, int delta);
int LockDialSolved(int dial);          /* 该拨盘是否已对（绿圈） */
void LockAdjustDigit(int dial, int delta);

/* 接线锁：左端子颜色 / 右端子颜色（由参数排列决定）/ 尝试连线 */
int LockWireColor(int leftIdx);
int LockWireRightColor(int rightIdx);
int LockWireConnect(int left, int right);   /* 1 = 配色正确已连上 */

/* 均衡器锁：目标档位 / 拨动 / 是否全部对上 */
int LockEqTarget(int band);
void LockEqAdjust(int band, int delta);
int LockEqSolved(void);

/* 调波器锁：目标档位 / 旋转 / 是否波形完全重合 */
int LockTuneTarget(int knob);
void LockTuneAdjust(int knob, int delta);
int LockTuneSolved(void);

/* ---- 输入 ---- */
extern int keys[256];
extern int keyPressed[256];
extern POINT lastMousePos;
extern int firstMouseMove;
extern POINT g_mousePos;       /* 窗口客户区坐标下的鼠标位置，用于 tooltip 等 */
extern int isMapOpen;
extern int g_bindings[ACT_COUNT];

/* ---- 设置模型 ---- */
extern int settingsPage;
extern int g_forceMod;
extern int menuScrollOffsets[9];   /* 每个菜单状态的滚动偏移（含 STATE_SKILLS） */
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

/* ---- 主动技能（v1.9.0） ----
   每局只能带一个技能，只受冷却限制。目前只有穿墙（SKILL_PHASE）。 */
extern int selectedSkill;              /* 大厅里选定的技能（SKILL_*） */
extern int runSkill;                   /* 这局实际带进来的技能 */
extern int skillLevels[SKILL_CAP];
extern float skillPhaseTimer;          /* 穿墙剩余秒数，0 = 没在穿 */
extern float skillSightTimer;          /* 透视剩余秒数 */
extern float skillCooldownTimer;       /* 冷却剩余秒数（三个技能共用） */
extern int phaseActive;                /* 1 = 正在穿墙（渲染走透视） */
extern int sightActive;                /* 1 = 正在透视（隔墙标出实体） */
void TryActivateSkill(void);
const char* SkillName(int skill);
float SkillCooldown(int skill);
float SkillDuration(int skill);
int SkillLevel(int skill);
int SkillUpgradeCost(int skill);     /* 返回残卷数量；-1 = 已满级 */
void BuySkillUpgrade(void);
extern int scrolls;                  /* 残卷：只用来升级技能 */
/* 技能页：选中某个技能（换技能会作废当前这局）/ 直接升级某个技能 */
void SkillSelect(int skill);
void SkillUpgradeAt(int skill);
void ActivateSkillRow(void);         /* 技能页当前行：换技能 / 升级 / 返回 */
extern int skillSelection;           /* 技能页当前高亮行 */
/* 大厅里换技能：当前这局作废（技能开局定死） */
void CycleSkill(void);
/* 玩家碰撞：穿墙期间忽略墙（地图边界仍然挡）。怪物用的是 Blocked()，不受影响。 */
int PlayerBlocked(float x, float y);

/* ---- 隐藏房间（v1.10.0） ---- */
extern int hiddenActive;                    /* 本层有没有 */
extern int hiddenOpened;
extern int hiddenX0, hiddenY0, hiddenX1, hiddenY1;   /* 内部地板范围（含） */
extern int hiddenDoorX, hiddenDoorY;
extern int hiddenCodeCount;
extern int hiddenCodesFound;
extern int hiddenCodeX[HIDDEN_MAX_CODES], hiddenCodeY[HIDDEN_MAX_CODES];
extern int hiddenCodeTaken[HIDDEN_MAX_CODES];
/* 穿墙 / 瞬移都不能进隐藏房间（用户要求）；返回 1 = 被拦 */
int HiddenBlocksPlayer(float x, float y);
/* 在暗门前按 E：密码齐了才开 */
void HiddenTryUnlock(void);

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
/* v14 新成就计数（每局重置） */
extern int locksOpened;
extern int hiddenOpenedCount;
extern int scrollsEarned;
extern float phaseSeconds;
extern int blinkCount;

/* ---- 窗口过程 ---- */
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif /* CANITY_GAME_H */

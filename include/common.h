/*
 * Canity — 公共契约头文件（唯一包含系统头的地方）
 *
 * 模块划分：
 *   common.h   系统头 / 全部常量 / 跨模块共享的类型 / 纯数学工具
 *   entity.h   玩家状态、状态效果、三种怪物 + 跳脸虫 QTE
 *   game.h     世界、关卡、规则、输入、菜单状态机、窗口过程
 *   render.h   像素缓冲、贴图与查表、多线程光栅、3D、精灵、地图
 *   ui.h       字体、文字、菜单/弹窗/HUD 绘制、最终呈现
 *   audio.h    waveOut 音频合成与混音
 *   save.h     SaveBlob 与存档读写
 *   main.c     只剩 WinMain：建窗 + 固定步长主循环
 *
 * 约定：
 *   - 除本文件外，任何 .c / .h 都不再直接包含系统头。
 *   - 全局变量只在某个 .c 里定义并赋初值，对应 .h 里只写 extern。
 *   - 本文件不定义任何变量对象。
 */
#ifndef CANITY_COMMON_H
#define CANITY_COMMON_H

#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- 版本号：改玩法或存档结构时必须同步升这两个 ---- */
#define CANITY_VERSION_NUM 17         /* 整数版本，进存档做记录 */
#define CANITY_VERSION_STR "1.6.6"    /* 显示版本，窗口标题 / 主菜单 */

/* ---- 游戏状态机（所有模块共用，定义放在公共头最前面） ---- */
typedef enum {
    STATE_MAIN_MENU = 0,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_SETTINGS,
    STATE_GAMEOVER,
    STATE_HUB,
    STATE_HELP,
    STATE_ACHIEVEMENTS
} GameState;

/* ------------------------------------------------------------------ */
/* 基础尺寸与逻辑步进                                                   */
/* ------------------------------------------------------------------ */
#define MAP_WIDTH 24
#define MAP_HEIGHT 24
#define TEX_WIDTH 64
#define TEX_HEIGHT 64
#define MAX_SCREEN 4096       /* renderW/renderH 的安全上限 */
#define LOGIC_HZ 120.0        /* 逻辑步进频率，与渲染帧率无关 */
#define LOGIC_STEP (1.0 / LOGIC_HZ)
#define MOVE_SPEED 2.0f       /* 单位/秒（基础走速） */
#define SPRINT_MULT 1.5f      /* 冲刺倍率 */
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

/* 到达这一层并进出口 = 通关 */
#define WIN_LEVEL 10

/* 字库尺寸（RenderMap 与 UI 共用） */
#define FONT_H 12
#define FONT_W 8

/* 设置表的项数。定义在 game.c，这里给出常量供 .h 里的 extern 使用；
   game.c 里有编译期断言保证它与实际表项数一致。 */
#define SETTING_COUNT 35
/* 按键选项表的项数（None + 数字 + 字母 + 功能键 + 鼠标键） */
#define OPT_KEY_COUNT 54

/* ------------------------------------------------------------------ */
/* 状态效果                                                            */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* 怪物                                                                */
/* ------------------------------------------------------------------ */
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

/* 怪物血量：玻璃瓶能砸怪，所以怪需要有血 */
#define MONSTER_HP 3            /* 寻声怪：3 瓶 */
#define LIGHTBEAST_HP 2         /* 光敏怪：2 瓶（它更难缠，脆一点更合理） */
#define MONSTER_STUN 1.2f       /* 被砸中后的硬直秒数 */
#define MONSTER_FLEE_DIST 3.0f  /* 被驱离时后退的距离 */

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

/* ------------------------------------------------------------------ */
/* 投掷物                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    float x, y;
    float z, vz;       /* 抛物线高度和垂直速度 */
    float vx, vy;      /* 水平速度 */
    int type;          /* 0=荧光棒(飞行中/落地发光) 1=玻璃碎片 */
    float life;
    int active;
} Thrown;

/* 飞行投掷物（玻璃瓶 = 唯一的武器）                                    */
/* 荧光棒是「扔在地上持续发光」的静止物，玻璃瓶则是「飞出去砸东西」的弹丸。
   两者机制不同，所以分成两个系统：
     thrown[]  —— 静止物（荧光棒 / 瓶子碎片），只管显示与被光敏怪追踪
     shot[]    —— 飞行物（玻璃瓶），带速度，撞到怪就结算伤害 */
#define SHOT_SPEED 9.0f      /* 玻璃瓶基础飞行速度，单位/秒 */
#define SHOT_MAX_SPEED 14.0f  /* 蓄力最大速度 */
#define SHOT_RADIUS 0.35f    /* 命中判定半径（怪也是半径 0.24 的量级） */
#define SHOT_DAMAGE 1        /* 一瓶砸掉几点血 */
#define GRAVITY 9.8f         /* 重力加速度，格/秒² */
#define SHOT_HEIGHT 1.6f     /* 手部高度（抛物线初始高度） */
#define MAX_SHARD_LIFE 2.5f  /* 玻璃碎片存活时间 */
#define SHARD_COUNT 10       /* 每次破碎产生的碎片数 */

typedef struct {
    float x, y;          /* 世界坐标（水平面） */
    float z;             /* 高度（抛物线） */
    float vx, vy, vz;    /* 速度向量 */
    int active;
    float life;          /* 最长飞行时间 */
    float age;           /* 已飞行时间 */
    float charge;        /* 蓄力倍率 0.1~1.0 */
} Shot;

/* 玻璃碎片：撞击时生成，带重力、随机扩散、逐渐变暗 */
typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life;
    float age;
    int active;
} Shard;

/* ------------------------------------------------------------------ */
/* 场景对象                                                            */
/* ------------------------------------------------------------------ */
/* 安全屋：每关一个，站在里面缓慢回理智 */
typedef struct { float x, y; int active; } SafeRoom;
/* 安全屋的作用半径（单位：格）。
   注意 Dist2() 返回的是**平方距离**，所以比较时要平方。 */
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

typedef struct { float x, y; int active; } Breaker;

/* 交互（长按 E） */
#define INTERACT_RANGE 1.25f
#define SEARCH_TIME 1.1f

/* 手电筒 / 电池 / 体力 */
#define BATTERY_DRAIN 0.85f      /* 每秒耗电 */
#define BATTERY_PICKUP 45.0f     /* 电池补给量 */
#define STAMINA_DRAIN 26.0f      /* 冲刺每秒消耗 */
#define STAMINA_REGEN 20.0f      /* 恢复每秒 */

/* 每关随机词条：给每层一点变化 */
#define MOD_COUNT 6

/* 升级项：Battery / Speed / Quiet / Calm / Eco / Rest */
#define UPGRADE_COUNT 6
#define UPG_MAX 3
/* 大厅菜单行数：6 项升级 + Back */
#define HUB_ROWS (UPGRADE_COUNT + 1)

/* ------------------------------------------------------------------ */
/* 成就系统                                                              */
/* ------------------------------------------------------------------ */
#define ACH_COUNT 16
#define ACH_NAME_MAX 32
#define ACH_DESC_MAX 64

typedef struct {
    int id;
    char name[ACH_NAME_MAX];
    char desc[ACH_DESC_MAX];
    int unlocked;      /* 0 = 未解锁，1 = 已解锁 */
    int progress;      /* 进度（用于部分成就） */
    int target;        /* 目标值 */
} Achievement;

extern Achievement g_achievements[ACH_COUNT];
extern int achSelection;
extern int achHovered;

/* ------------------------------------------------------------------ */
/* 全局按键绑定系统                                                      */
/* ------------------------------------------------------------------ */
typedef enum {
    ACT_MOVE_FWD = 0,      /* 前进 */
    ACT_MOVE_BACK,         /* 后退 */
    ACT_MOVE_LEFT,         /* 左移 */
    ACT_MOVE_RIGHT,        /* 右移 */
    ACT_SPRINT,            /* 冲刺 */
    ACT_INTERACT,          /* 交互/搜索 */
    ACT_CRANK,             /* 手摇发电 */
    ACT_FLASHLIGHT,        /* 手电筒开关 */
    ACT_MAP,               /* 切换地图 */
    ACT_THROW,             /* 投掷当前选中 */
    ACT_CANCEL_CHARGE,     /* 取消蓄力（右键） */
    ACT_SELECT_1,          /* 选槽位 1 */
    ACT_SELECT_2,          /* 选槽位 2 */
    ACT_SELECT_3,          /* 选槽位 3 */
    ACT_PAUSE,             /* 暂停/菜单 */
    ACT_MENU_UP,           /* 菜单向上 */
    ACT_MENU_DOWN,         /* 菜单向下 */
    ACT_MENU_LEFT,         /* 菜单向左/滑块- */
    ACT_MENU_RIGHT,        /* 菜单向右/滑块+ */
    ACT_MENU_CONFIRM,      /* 菜单确认 */
    ACT_MENU_CANCEL,       /* 菜单取消 */
    ACT_DODGE,             /* 跳脸虫 QTE：躲开（默认 SPACE，可改绑） */
    ACT_COUNT
} ActionId;

/* 每个动作绑定一个虚拟键码，0=无绑定。
   默认值集中在一个宏里 —— Settings 的 Reset to Defaults 和自检都要用它，
   别在两处各写一份。 */
#define BINDINGS_DEFAULT { \
    [ACT_MOVE_FWD] = 'W', [ACT_MOVE_BACK] = 'S', \
    [ACT_MOVE_LEFT] = 'A', [ACT_MOVE_RIGHT] = 'D', \
    [ACT_SPRINT] = VK_SHIFT, \
    [ACT_INTERACT] = 'E', [ACT_CRANK] = 'R', [ACT_FLASHLIGHT] = 'F', \
    [ACT_MAP] = 'M', \
    [ACT_THROW] = VK_LBUTTON, [ACT_CANCEL_CHARGE] = VK_RBUTTON, \
    [ACT_SELECT_1] = '1', [ACT_SELECT_2] = '2', [ACT_SELECT_3] = '3', \
    [ACT_PAUSE] = VK_ESCAPE, \
    [ACT_MENU_UP] = VK_UP, [ACT_MENU_DOWN] = VK_DOWN, \
    [ACT_MENU_LEFT] = VK_LEFT, [ACT_MENU_RIGHT] = VK_RIGHT, \
    [ACT_MENU_CONFIRM] = VK_RETURN, [ACT_MENU_CANCEL] = VK_ESCAPE, \
    [ACT_DODGE] = VK_SPACE, \
}

/* ------------------------------------------------------------------ */
/* 设置项与菜单几何                                                    */
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
    int group;                    /* 分类页：1=Display 2=Movement 3=Actions 4=Menu Keys 5=Gameplay */
} SettingItem;

/* 设置菜单分页（树状）：
   0 = 根目录：Display / Key Bindings / Gameplay / Back
   1 = Display 页
   2 = Key Bindings 子目录：Movement / Actions / Menu Keys / Back
   3..5 = Movement / Actions / Menu Keys 键位页
   6 = Gameplay 页 */
enum { SETPAGE_ROOT = 0, SETPAGE_DISPLAY = 1, SETPAGE_BINDDIR = 2,
       SETPAGE_MOVE = 3, SETPAGE_ACT = 4, SETPAGE_MENUKEYS = 5, SETPAGE_GAME = 6 };

typedef struct {
    RECT panel;
    int titleY;
    int firstY;
    int rowH;
    int count;
    const char* title;
    int scrollOffset;      /* 当前滚动偏移（行号） */
    int maxVisibleRows;    /* 面板内最多显示多少行 */
} MenuGeo;

/* ------------------------------------------------------------------ */
/* 纯数学工具（无状态，每个编译单元一份）                                */
/* ------------------------------------------------------------------ */
static inline float Dist2(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

#endif /* CANITY_COMMON_H */

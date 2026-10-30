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
#define CANITY_VERSION_NUM 24         /* 整数版本，进存档做记录 */
/* v3.0：**逃生试炼方向** —— 怪物不可击杀，玩家没有武器。
   置 1 = 攻击不再造成伤害，怪物永远打不死（只追不杀）。
   置 0 = 回到旧的战斗版本。 */
#define NO_COMBAT 1

#define CANITY_VERSION_STR "2.2.3"    /* 显示版本，窗口标题 / 主菜单 */

/* ---- 游戏状态机（所有模块共用，定义放在公共头最前面） ---- */
typedef enum {
    STATE_MAIN_MENU = 0,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_SETTINGS,
    STATE_GAMEOVER,
    STATE_HUB,
    STATE_HELP,
    STATE_ACHIEVEMENTS,
    STATE_SKILL_PICK,     /* v1.8：局内技能三选一 */
    STATE_BESTIARY,       /* v1.8：怪物图鉴 */
    STATE_WEAPON_PICK,    /* v2.0：开局选初始武器 */
    STATE_SAFEHOUSE,      /* v2.0：安全屋（商店/改装台/休息） */
    STATE_BANK            /* v6：**独立的银行页** —— 和 HUB（属性页）分开。
                             原来银行塞在 Hub 的第 3 页里，用户指出
                             「不然我单独设计一个 BANK 干嘛」。
                             加在枚举末尾，不动已有编号。 */
} GameState;

/* ------------------------------------------------------------------ */
/* 基础尺寸与逻辑步进                                                   */
/* ------------------------------------------------------------------ */
#define MAP_WIDTH 24
#define MAP_HEIGHT 24
#define TEX_WIDTH  128   /* v2.0：64→128，2x2 块从 25px 降到 12px */
#define TEX_HEIGHT 128
#define MAX_SCREEN 4096       /* renderW/renderH 的安全上限 */
#define LOGIC_HZ 120.0        /* 逻辑步进频率，与渲染帧率无关 */
#define LOGIC_STEP (1.0 / LOGIC_HZ)
#define MOVE_SPEED 2.0f       /* 单位/秒（基础走速） */
#define SPRINT_MULT 1.5f      /* 冲刺倍率 */
#define SANITY_DRAIN 0.5f     /* 理智/秒（基础值） */
#define HIDE_SANITY_REGEN 2.0f /* 躲在垃圾桶里每秒恢复的理智 */
/* v3.3 **藏身回电** ——
   躲进藏身点时手电自动充电。
   设计理由：藏身已经是「暂停被追」的动作，玩家在里面本来就在等，
   让它同时做件有用的事；而且这给了「电池快没了先找个柜子」一个具体解法。
   速度略低于安全屋充电（+60 一次），大约 20 秒回满一整块。 */
#define HIDE_BATTERY_REGEN  4.0f   /* 每秒回 4 点电量 */
#define ENDLESS_SANITY_DRAIN 0.35f  /* 无尽模式：理智流失更慢 */

/* ---- v1.8 理智经济（docs/终极方案_v2.md 第 1 节） ----
   原版每层净 +31（安全屋 +28 / 通关 +30 / 流失 -33），玩家不可能死。
   改成按层加成流失 + 削减回血，让深层真正吃紧。 */
#define SANITY_DRAIN_STEP 0.028f   /* v3.4（方案 v5 §5.3）：0.012 → 0.028。
                                     原来 STEP 和 CLEAR_REWARD 的曲线互相抵消 ——
                                     算出每层允许停留时间 37→39 秒，全程几乎不变，
                                     深层根本不比浅层难。改 0.028 后 25 → 17 秒。 */  /* v2.2：0.02→0.012（裸装原来第 11 层必死） */    /* v2.1：0.03→0.02，第 10 层 0.68 而非 0.77 */
#define SANITY_DRAIN_CAP  1.2f     /* 流失上限 */
#define SAFE_HEAL_RATE    1.2f     /* v2.0：3.0→1.2。审计实测三资源线里只有体力会卡，
                                      理智靠 8 个安全屋白送反而净 +187，恐怖感被抹平 */
#define SAFE_HEAL_CAP     60.0f    /* v2.0：安全屋回智封顶（不再是满血点） */
#define SAFE_REST_COST    25       /* v2.0：主动休息要花碎片 */
#define LEVEL_CLEAR_SANITY 12.0f   /* 过关回智（配合 STEP 上调，奖励同步下调） */   /* 过关回智（原 30） */
#define CATCH_SANITY_BASE 18.0f    /* 被抓扣智基础值 */
#define CATCH_SANITY_STEP 0.015f   /* 每层额外 +1.5% */
#define MOUSE_BASE_SENS 0.0035   /* v3.0：0.002→0.0035（玩家实测转身太慢） */ /* 弧度/像素（1.0x） */

/* 无尽模式：首通后解锁，独立存档、独立难度曲线 */
extern int isEndlessRun;
extern int bestEndlessLevel;
extern int bestEndlessScore;

/* 一层里最多能同时存在多少个可搜索容器。
   RICH 词条会额外 +2，所以数组本身要留出余量，
   否则容器数量一旦真的涨上去就会写越界。 */
#define MAX_CONTAINERS 8
#define CONTAINERS_BASE 5    /* v3.4：6 → 5。箱子太多 = 道具白送，
                                方案 v5 决定把箱子从「道具售货机」改成「金币来源」。 */
/* 场上最多同时存在几件投掷物（荧光棒 / 玻璃瓶） */
#define MAX_THROWN 4
/* 场上最多同时存在几枚飞行中的玻璃瓶 */
#define MAX_SHOTS 4

/* 无尽模式：每层最多怪物数（寻声怪+光敏怪+第3只怪） */

/* ------------------------------------------------------------------ */
/* 难度曲线                                                            */
/* ------------------------------------------------------------------ */
/* 原则：难度只从「怪物压力」上来，**不动理智的时间预算**。
   理智是整局共用的计时器（100 点 ÷ 0.5/秒 = 200 秒），GenerateLevel 里
   不会重置它。如果按层数放大理智流失，那不是难度曲线，是「越往后死得越早」
   的反向曲线 —— 所以 SANITY_DRAIN 保持常量，难度只改怪物。 */
/* v3.0 平衡：怪物必须**比走路快**（2.0）、**比冲刺慢**（3.0）。
   原来是 1.45，比走路慢 0.55/秒 —— 玩家慢悠悠走 10 秒，怪反而落后 5.5 格，
   追捕完全没有压迫感，逃生玩法立不住。
   现在走路会被逐渐追上（+0.15/秒），必须靠冲刺甩开（3.8 秒甩 2.5 格），
   然后找地方藏起来等体力回满 —— 这才是逃生的核心节奏。 */
/* v3.0：**寻声怪必须能"看见"玩家** ——
   原来它只对噪音有反应，玩家站在它面前 2 米它也毫无反应
   （玩家实测：怪贴脸站了十几秒，不打人、血没掉，只有 SANITY 在滴）。
   视野给 7 格 + 需要视线不被墙挡。手电关着时减半，逼玩家学会省电。 */
/* v3.0：**听声怪没有视觉** ——
   用户质疑：「有视觉为什么叫听声怪？」名字和机制确实矛盾。
   现在视觉完全关掉，它只靠 MakeNoise 传来的声音定位，
   这才是「听声」二字该有的样子。
   （追光怪 lightBeast 仍然有视觉，它靠的是手电的光。） */
#define MONSTER_SIGHT       0.0f
#define MONSTER_SIGHT_DARK  0.0f
/* v3.0 削弱：2.15 → 1.90。
   2.15 比玩家走路 2.0 还快，一旦被盯上只能靠 3.1 秒的冲刺，
   体力一空就是必死 —— 用户反馈「怪物强度过高」。
   1.90 低于走路，慢走能拉开但每 10 秒只赚 1 格，压力仍在。 */
#define MONSTER_SPEED_BASE 1.90f    /* 寻声怪基础速度 */
#define LIGHTBEAST_SPEED_BASE 1.50f /* 光敏怪基础速度 */
#define MONSTER_SPEED_STEP 0.03f    /* 每层 +0.03 */
#define MONSTER_SPEED_MAX 2.05f   /* v3.0 削弱：2.35 → 2.05（刚超过走路） */   /* v3.0 平衡：仍然 < 冲刺 3.0 */    /* v2.1：玩家 2.0，怪必须追不上但贴着 */
#define MONSTER_SPEED_FINAL_MAX 2.10f   /* v3.0 削弱 */  /* v2.1：叠 HUNTED 也不许超过玩家 */
/* 最终上限 2.40 < MOVE_SPEED 2.50：玩家永远走得比怪快。
   基础上限留 1.85 是为了让 HUNTED 的 ×1.25 在深层仍有意义
   （1.85 × 1.25 = 2.3125，不会顶到最终上限）。 */
#define HIT_COOLDOWN_BASE 4.0f      /* 被抓后的无敌时间，基础值 */
#define HIT_COOLDOWN_MIN 3.0f       /* 深层收紧到 3.0 秒 */
#define HIT_COOLDOWN_START_LEVEL 6  /* 从第 6 层开始收紧 */
/* 精灵排序表的上界：容器 + 配电箱 + 寻声怪 + 安全屋 + 光敏怪 + 投掷物 + 出口光柱
   + 飞行中的玻璃瓶
   注意：跳脸虫不在这个表里 —— 它是屏幕空间 overlay，由 RenderFrame 直接画。 */
#define MAX_SPRITES 48   /* v2.2: was 25, worst case 29 -> sprites were silently dropped */

/* 内部渲染的像素预算上限。
   窗口再大，内部渲染分辨率也不超过这个像素数 —— 这是全屏帧时间的保护阀。
   1080p 全屏实测：90 万 → 渲染 29ms + 呈现 11ms（只有 25fps）；
   28 万（约 640x360）→ 14ms，60fps 稳。
   注意：bench_canity.c 复刻了同一套分辨率计算，用的是这里的常量，
   不要在两处各写一份数字（上一轮就因为这里漏改，基准一直在测已废弃的配置）。 */
/* v2.9：28 万（≈705x396）在高分屏上糊得厉害 —— 用户实测「分辨率严重下降」。
   默认抬到 90 万（≈1265x712），自适应逻辑仍会在帧时间 >12ms 时自动往回收
   （PIXEL_BUDGET_MIN 12 万），所以慢机器不会卡死。 */
#define PIXEL_BUDGET 900000
/* v1.9：动态像素预算 —— 帧时间富裕就提分辨率，吃紧就降 */
#define PIXEL_BUDGET_MIN  120000
#define PIXEL_BUDGET_MAX  900000

/* 到达这一层并进出口 = 通关 */
#define WIN_LEVEL 24     /* v2.1：主线 10 → 24 层（4 个区段） */
#define CAMPAIGN_STAGE_COUNT 5
#define CAMPAIGN_LEVELS_PER_STAGE 3  /* 每段连续 3 层，再切换下一段规则 */

/* 字库尺寸（RenderMap 与 UI 共用） */
#define FONT_H 12
#define FONT_W 8

/* 设置表的项数。定义在 game.c，这里给出常量供 .h 里的 extern 使用；
   game.c 里有编译期断言保证它与实际表项数一致。 */
#define SETTING_COUNT 37   /* v3.1：界面固定英文，移除 Language 项（原 38） */
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
/* ---- v1.15 伤害系统 ----
   混合公式：
     ① 乘法段：抗性百分比削减（负值 = 弱点）
     ② 减法段：护甲固定减免
     ③ 保底 1 点，避免完全免疫卡关
   三种类型够用且好记。 */
#define DT_SLASH  0
#define DT_BLUNT  1
#define DT_SHARD  2
#define DT_COUNT  3

/* v1.16 烟幕云 */
/* v3.3 粘胶区域：和烟云同构，但作用的是移速而不是视线 */
#define MAX_GLUE 4
typedef struct {
    float x, y;
    float life;
    int   active;
} GlueZone;
extern GlueZone glues[MAX_GLUE];

/* v3.3 玩家诱饵「玩偶」
   放在自己脚下，C 键激活；激活后每 3 秒发一次强声，
   吸引范围内怪物（含会瞬移的那只）；能替玩家挨 2 次抓。
   hp 归零就散架。 */
#define MAX_DECOY 2
typedef struct {
    float x, y;
    float life;        /* 剩余存活秒数 */
    float shoutT;      /* 距下一次叫的秒数 */
    int   hp;          /* 还能挡几次 */
    int   active;
    int   armed;       /* 0=放下还没激活  1=已在叫 */
} Decoy;
extern Decoy decoysArr[MAX_DECOY];
void PlaceDecoy(float x, float y);
void ArmDecoy(void);
int  DecoyIntercept(void);       /* 若有激活的诱饵可以替玩家挨打，返回 1 并消耗一次 */


#define MAX_SMOKE 3
#define SMOKE_LIFE 5.0f
#define SMOKE_RADIUS 3.6f      /* v1.19：范围扩大（原来 2.2 太小） */

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
    float sightHold;   /* v3.0：连续看见玩家的秒数（察觉延迟） */
    float stillTimer;  /* v3.0：连续站着不动的秒数（超过就丢目标） */
    float huntTimer;   /* v3.0：本次追捕已持续秒数（超时放弃用） */
    float speed;
    int hp;                    /* 剩余血量 */
    /* ---- v1.15 伤害系统 ---- */
    int armor;                     /* 固定减伤 */
    signed char resist[DT_COUNT];  /* 百分比抗性；负数 = 弱点 */
    int glued;                     /* v2.2：被胶水定身，受伤 +50% */
    float stun;                /* 硬直剩余秒数，期间不能移动 */
    /* 寄生怪专用（其它两只怪不用这几个字段） */
    int attached;              /* 1 = 已附着在玩家身上 */
    float ramp;                /* 附着深度 0..1，1 = 效果拉满 */
    float attachTimer;         /* 在范围内待了多久（累计），用于「缠上来」的进度 */
    float offX, offY;          /* 附着时趴在玩家背后的世界坐标偏移 */
    /* ---- v3.0 朝向与走路相位（建模审查 #1/#3） ---- */
    float facing;              /* 怪自己面朝的角度（弧度），由移动方向更新 */
    float walkPhase;           /* 走路相位，按**位移积分**而不是全局时钟 */
    float prevX, prevY;        /* 上一帧位置，用来算位移 */
} Monster;

/* v1.16 烟幕云：落到地面后持续一段时间，双向遮蔽 */
typedef struct {
    float x, y;
    float life;
    int active;
} SmokeCloud;
extern SmokeCloud smokes[MAX_SMOKE];

/* 怪物血量：玻璃瓶能砸怪，所以怪需要有血 */
/* v2.1：血量搬到伤害量纲。原来恒 3 点，武器伤害 30-55 全部溢出，
   「伤害 / 熟练度 / 配件」整条成长链在数学上不存在。 */
#define MONSTER_HP      70      /* v2.0：基础 70（开局 2 击） */
#define MONSTER_HP_STEP 8       /* v2.2：12→8（第 24 层 346→254） */      /* v2.0：30→12。原来第 24 层 780 血，
                                   满蓄力 46 要打 17 下 = 20 秒，手枪一整条电都杀不死一只 */
#define LIGHTBEAST_HP_MUL 0.6f            /* 寻声怪：3 瓶 */
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
#define SHOT_DAMAGE 8        /* v2.0：1→8（原来一整局的投掷物伤害 < 一次近战挥击） */
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
    int kind;            /* 1=瓶 2=砖 3=酸 4=烟 6=信号枪弹（v2.0） */
    int dmg;             /* v2.0：武器子弹的伤害（其它 kind 用不到） */
    int pierce;          /* v2.0：1 = 命中后继续飞 */
    int mirrorRebounded; /* 镜片只反弹一次，避免高蓄力命中后立刻回撞原目标 */
    int ignoreTarget;    /* 镜片反弹后的短暂免碰撞目标：0=寻声鬼，1=追光鬼 */
    float ignoreTargetTime;
} Shot;

/* 玻璃碎片：撞击时生成，带重力、随机扩散、逐渐变暗 */
typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life;
    float age;
    int active;
    unsigned char tint;   /* v2.1：0 玻璃 1 血 2 尘 */
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
/* v3.0 逃生试炼：**藏身点**（衣柜 / 床底 / 桌下）。
   按 E 钻进去，追捕者会失去目标；钻着的时候不能动。
   没有藏身点，逃生类就会退化成「绕圈跑」，两分钟就腻。 */
#define HIDESPOT_MAX 6
typedef struct {
    float x, y;
    int active;
    int kind;         /* 0=衣柜 1=床底 2=桌下 */
} HideSpot;

typedef struct {
    float x, y;
    int active;
    int searched;
    int content;      /* 1 = 保险丝  2 = 电池  0 = 空 */
    /* v3.0：上锁箱子（移植自上游 v2.2.0） */
    int locked;       /* 1 = 上了锁，按 E 会弹锁盘而不是直接翻 */
    int lockType;     /* LOCK_TYPE_* */
    int lockCode;     /* 密码（按锁型解释） */
} Container;

typedef struct { float x, y; int active; } Breaker;

/* 交互（长按 E） */
#define INTERACT_RANGE 1.25f
#define SEARCH_TIME 1.1f

/* 手电筒 / 电池 / 体力 */
#define BATTERY_DRAIN 1.10f     /* v3.0 平衡：0.85→1.10，118 秒太长 */      /* 每秒耗电 */
/* v2.4：看地图要花电 —— 每开着一秒扣 10 点，电量见底自动关图。
   地图本来是"无解"的信息优势（一眼看到整层结构），现在标了价：
   开得越久，手电可用的时间越短。 */
#define MAP_BATTERY_DRAIN 10.0f
#define BATTERY_PICKUP 60.0f    /* v2.2：45→60（原来第 8 层就开始发黑） */     /* 电池补给量 */
#define STAMINA_DRAIN 32.0f      /* v3.0 平衡：26→32，冲刺变成"救命"而不是常态 */      /* 冲刺每秒消耗 */
#define STAMINA_REGEN 18.0f

/* v3.0：**藏身点的三条收益** ——
   用户反馈「隐藏点还是没什么用」。根因不是功能缺失，是游戏没有「必须躲」的压力：
   怪物 1.90 比走路 2.0 慢，走开就行。
   所以给藏身点三条实打实的收益，让「躲」成为主动选择：

   ① 藏着时体力回复 ×3（躲 2 秒回满，才能再冲刺）
   ② 被看见时理智额外流失（必须断开视线，而躲是最快的断开方式）
   ③ 隐藏自带断开视线 → ①②叠加

   没加怪物速度，避免又变成「追不上就必死」。 */
#define HIDE_STAMINA_REGEN_MUL 3.0f

/* v3.0：三个主动大招（局内三选一拿到，按 Q 释放，一局一次）
   BLACKOUT   全场追捕者失明 10 秒
   WHISTLE    把所有追捕者引向远处
   SMOKE OUT  立刻断开所有视线 */
#define ULT_BLACKOUT_TIME 10.0f
#define ULT_WHISTLE_TIME  8.0f
#define SEEN_SANITY_EXTRA     0.9f   /* 被盯上时每秒额外掉这么多理智 */      /* v3.0 平衡：24→18，藏起来回体力才有意义 */
/* v2.3 新机制：武器能量。
   三把武器原来各烧一条资源（体力/电量/理智），结果是「体力自己会回」+
   「电量能摇回来」+「符印只烧 2 点」，一层里可以无限挥 —— 投掷物就没人用了。
   现在统一烧这一条**不会自然恢复**的能量：一局的总输出有上限，
   想省就得用投掷物（投掷物不吃能量）。 */
#define ENERGY_MAX          120.0f   /* 上限 */
#define ENERGY_PER_FLOOR     35.0f   /* 每过一层补这么多 */
#define ENERGY_FROM_BATTERY  12.0f   /* 捡到电池顺带补一点 */
#define ENERGY_SAFE_REFILL   60.0f

/* v2.4 校准小游戏（参考第五人格的校准）：
   安全屋里按空格开一次，滑块左右扫，再按空格锁定。
   完美区命中给得多，普通区给得少，失手什么都没有。 */
#define CALIB_ATTEMPTS       2        /* v2.6：**已取消次数限制**，这个常量不再参与判定 */
#define CALIB_COOLDOWN      22.0f     /* v3.0：用户要求改成 30 秒 */
#define CALIB_PER_LEVEL      99      /* v3.0：藏身点里校准，不再按层限次，只靠 60 秒冷却 */        /* v3.0：**每层只能校准 1 次** ——
                                         原来无次数限制 + 安全屋不掉理智，
                                         站 3 分钟能刷 190 能量，能量经济直接失效
                                         （数值审查 #2）。 */
#define CALIB_SWEEP         1.25f     /* 单程扫完用多久（秒），越小越难 */
#define CALIB_ZONE_W        0.20f     /* 普通区半宽（0..1 的比例） */
#define CALIB_PERFECT_W     0.055f    /* 完美区半宽 */
/* v2.9：能量奖励改成**按能量上限的百分比**（用户要求）
   完美（黄区）70%、命中（绿区）40%、失手 0%。
   原来固定 +28/+14，在高上限 build 里收益反而缩水。 */
/* v3.0：**校准移进藏身点，主要奖励改成理智** ——
   武器取消后能量已经没用了（只剩手电/投掷的小储备），
   而逃生玩法里最缺的就是理智。
   平衡依据：
     · 理智流失第 1 层 0.5/秒、第 24 层 0.776/秒
     · 60 秒冷却 ≈ 一层流失 30~47 点
     · 完美 22 点 / 命中 11 点 —— 完美能抵掉大半层消耗，命中只抵 1/3
   这样「躲进柜子里校准」是值得的，但不足以完全替代找安全屋。 */
#define CALIB_PERFECT_ENERGY_PCT 0.00f   /* 不再给能量 */
#define CALIB_GOOD_ENERGY_PCT    0.00f
#define CALIB_PERFECT_SANITY 30.0f  /* v3.4：22 → 30。STEP 上调后必须加强回智，
                                       否则算出来第 3 层就死。 */
#define CALIB_GOOD_SANITY    15.0f  /* v3.4：11 → 15 */

/* v2.6 校准难度成长：
   · 每层把目标区缩 2.5%，缩到基准的 45% 就停（CALIB_ZONE_MIN）
   · 停住之后多出来的难度转到**滑块速度**上，最快 2.2 倍（CALIB_SPEED_MAX）
   两个都有硬上限，深层不会变成不可能。 */
#define CALIB_ZONE_SHRINK    0.045f   /* 每层缩小比例（第 13 层触底） */
#define CALIB_ZONE_MIN       0.45f    /* 区域最小 = 基准的 45% */
#define CALIB_SPEED_MAX      2.20f    /* 滑块最多快 2.2 倍 */
#define CALIB_SPEED_GROW     0.09f    /* 触底后每层再快 9% */   /* 安全屋充电补这么多 */     /* v2.2：20→24（扳手是伤害预算） */      /* 恢复每秒 */

/* 每关随机词条：给每层一点变化 */
#define MOD_COUNT 6



/* 道具槽位数量（滚轮列表长度）。加武器只改这一个数。 */
/* v3.3 **投掷物重做：8 → 6**
   老 8 种：光球/瓶/砖/酸/烟/火/胶/镜 —— 其中酸、燃烧瓶、镜片只有伤害，
   NO_COMBAT 之后完全是空壳，删掉。
   瓶 + 砖合并成「投石」（蓄力同时控制距离和噪音）。
   砖头腾出的槽位换成「回声弹」，另新增「玩家诱饵」。
   新 6 种：光球 / 投石 / 烟雾 / 胶水 / 回声 / 诱饵 */
#define ITEM_SLOT_COUNT 6

/* ---- v3.3 投掷物重做：粘胶区域 + 回声扫描 ----
   胶水：落地生成一滩，半径 2.5 格、持续 6 秒、区域内移速 -80%
   回声：落地后 5 秒内显示 15 格内所有怪物位置（静音、不发光） */
#define GLUE_MAX        4
#define GLUE_RADIUS     2.5f
#define GLUE_LIFE       6.0f
#define GLUE_MONSTER_MUL 0.20f   /* 怪物移速 x0.20 = -80% */
#define GLUE_PLAYER_MUL  0.80f   /* 玩家自己也会被粘，但只 -20% ——
                                    v3.3：原来 -45% 太重，逃跑时踩到自己的胶水
                                    等于自杀，用户要求降到 20% */
#define GLUE_BODY_PAD    0.35f   /* 怪物/玩家的体积半径：中心还没进圈、
                                    身体边缘蹭到就该减速，否则「有时候不生效」 */
#define ECHO_RADIUS     15.0f
#define ECHO_LIFE       5.0f
#define DECOY_SHOUT_EVERY 3.0f   /* 诱饵每 3 秒叫一次 */
#define DECOY_SHOUT_R    20.0f   /* 叫声半径（比投石最大噪音还大） */
#define DECOY_HP         2       /* 能挡两次伤害 */

/* 关卡主题（墙面/地板/天花板风格）数量 */
/* 7 种主题已经写好，但 4/5/6 目前会崩（贴图生成越界，未定位），
   先只放行前 4 种；等排查完把这里改回 7 即可全部启用。 */
#define THEME_COUNT 7

/* 分辨率档位（最后一档 AUTO = 自动挑放得下的最大档） */
#define RES_COUNT 8
#define RES_AUTO  7
#define THEME_ENABLED THEME_COUNT   /* 7 种已全部启用（崩溃点是 themeNames[4] 越界） */

/* ---- v1.8 随机事件（通过本层 → 本局结算加分） ---- */
#define EVENT_COUNT 7     /* v2.0：事件 4→7 */
#define EVENT_CHANCE 35          /* 每层触发概率（百分比），第 2 层起 */
enum {
    EV_BLOOD_MOON = 0,   /* 血月：怪更快，+1500 */
    EV_BLACKOUT,         /* 断电：本层电量上限减半，+1000 */
    EV_SILENCE,          /* 死寂：噪声减半，+800 */
    EV_SUPPLY,           /* 补给空投：多给瓶+棒，+600 */
    EV_FLOOD,            /* v2.0 涨水：耗电翻倍，+1200 */
    EV_WHISPER,          /* v2.0 低语：理智流失 +50%，但碎片 +50%，+1300 */
    EV_MIRROR            /* v2.0 镜像：怪物成倍出现，+1600 */
};
extern int levelEvent;       /* 本层事件，-1 = 无 */
extern int eventBonus;       /* 本局累计的事件加分 */
extern const char* const eventNames[EVENT_COUNT];

/* 升级项：Battery / Speed / Quiet / Calm / Eco / Rest */
#define UPGRADE_COUNT 6
#define UPG_MAX 3
/* 大厅菜单行数：6 项升级 + Back */
#define HUB_ROWS (UPGRADE_COUNT + 1)

/* ------------------------------------------------------------------ */
/* v1.8 技能系统                                                        */
/* ------------------------------------------------------------------ */
/* v2.0 技能树重造：4 列 × 5 行 = 20 节点
   列 = 静（噪声的时与地）· 筹（风险与经济）· 锋（伤害交付方式）· 途（整局形状）
   前 4 行每列 4 个（3 个普通 + 1 个分支 keystone），第 5 行 = 4 个跨支 keystone。
   设计红线：每个节点必须能一句话说出「玩法怎么变」，不许卖常驻百分比。 */
#define SKILL_COUNT 20
#define KEYSTONE_COUNT 8              /* 4 个分支 keystone + 4 个跨支 */
#define SKILL_POINTS_FOR_KEYSTONE 5   /* 分支投入门槛（每列上限 7 点） */
#define SKILL_TOTAL_COST 278500   /* v6：按现表重算 */        /* 满树碎片（自检会校验，按 20 节点新表算出来） */
#define SKILL_TOTAL_CORES 4   /* v6：按现表重算 */          /* 满树核心 */
#define SKILL_SWITCH_CORE_COST 3      /* 换 keystone 花费 */

/* 技能 id（顺序必须与 skills.c 的表一致；按列连续排列） */
enum {
    /* 静 —— 噪声的时与地 */
    SK_QUIET = 0, SK_SEARCHNOISE, SK_GHOSTSTEP, SK_DECOY,
    /* 筹 —— 风险与经济结构 */
    SK_BATTERY,   SK_CRANK,       SK_REST,      SK_METAB,
    /* 锋 —— 伤害交付方式 */
    SK_BREATH,    SK_STILL,       SK_SLIP,      SK_PLAYDEAD,   /* v3.0：列 2 改成藏匿系 */
    /* 途 —— 整局形状 */
    SK_CARTO,     SK_BREAKER,     SK_FUSE_LESS, SK_SHORTCUT,
    /* 第 5 行：跨支 keystone */
    SK_SILENTRUN, SK_WEATHERMAN,  SK_LASTRESORT, SK_SALVAGE
};

#define META_COST 3.0f          /* 代谢：1 理智 = 3.0 电量（2.0 已验证会崩） */
#define META_STARVE_MUL 4.0f    /* 断电时理智流失倍率 */

#define KNIFE_CD      60.0f     /* 绣刀冷却 */
#define KNIFE_NOISE   8.0f      /* 挥砍发声半径 */
#define SLASH_SPEED      18.0f  /* 剑气飞行速度（格/秒） */
/* v2.6：走路不再是完全无声。连续走超过 WALK_NOISE_DELAY 秒之后，
   每隔 WALK_NOISE_INTERVAL 秒发出一次 WALK_NOISE_RADIUS 的小噪声 ——
   比冲刺（9.0）明显小，但足够把寻声鬼引过来。停下就立刻恢复安静。 */
#define WALK_NOISE_DELAY     3.0f   /* v2.0：5→3。「走 5 秒停 1.7 秒」能 100% 静音，
                                        恐怖循环整个被破掉；3 秒 + 衰减才有真实压力 */
#define WALK_NOISE_INTERVAL  0.75f
#define WALK_NOISE_RADIUS    4.5f

#define DECOY_MIN 3.0f          /* 投石：最近 3 格 */
#define DECOY_MAX 6.0f          /* 最远 6 格 */
#define DECOY_NOISE 14.0f       /* 落点噪声半径 */
#define DECOY_SELF_TIME 2.0f    /* 扔完自身噪声加成持续 */
#define DECOY_SELF_MUL  1.25f

/* ---- 局内技能 ---- */
#define INRUN_COUNT 24    /* v2.0：16→24，池子够大才不重复 */
#define INRUN_PICK_EVERY 2      /* 每通关 2 层给一次三选一 */
#define INRUN_CHOICES 3
enum {
    IR_BATTERYPACK = 0, IR_BOTTLE, IR_FLARE, IR_MEDKIT, IR_SHARDBAG,
    IR_LIGHTFOOT, IR_FASTCRANK, IR_FASTSEARCH, IR_SAVER, IR_LONGWIND, IR_ARMOR,
    IR_SILENTBOTTLE, IR_DARKWALK, IR_LIGHTFUSE,
    IR_STATICFIELD, IR_MISDIRECT,
    /* v2.0 新增 8 张（16 -> 24） */
    IR_FIELDREPAIR, IR_NERVESHOT, IR_PACKRAT, IR_OILCAN, IR_LUCKYFUSE,
    IR_STEADYHAND, IR_SWIFTRELOAD, IR_TIMESTOP
};

/* ------------------------------------------------------------------ */
/* v1.8 偷电怪（Battery Leech）                                        */
/* ------------------------------------------------------------------ */
#define LEECH_MAX 3
#define LEECH_MIN_LEVEL 3        /* 第 3 层起出现 */
#define LEECH_SPEED_BASE 1.00f
#define LEECH_SPEED_GROW 0.012f  /* 每秒 +0.012 */
#define LEECH_SPEED_CAP  1.90f   /* 仍慢于玩家 2.0，但能咬住 */
#define LEECH_HUNTED_MUL 1.25f
#define LEECH_RANGE      3.5f    /* 3.5 格内开始偷电 */
#define LEECH_DRAIN_BASE 3.0f
#define LEECH_DRAIN_STEP 0.15f   /* 每层 +0.15/秒 */
#define LEECH_DRAIN_CAP  9.0f
#define LEECH_HINT_BATTERY 50.0f /* 电量低于这个值给"滋滋"声 */

typedef struct {
    float x, y;
    float age;        /* 存活秒数（算速度成长） */
    float repelTimer; /* 被照到后的逃逸计时 */
    int active;
    int draining;     /* 本帧是否正在抽电（画电光/HUD 警告用） */
    float litTimer;   /* 被手电连续照射的累计秒数；够久就逃走 */
} Leech;

extern Leech leeches[LEECH_MAX];

/* ------------------------------------------------------------------ */
/* v1.8 碎片"过关上缴"（重生细胞式）                                   */
/* ------------------------------------------------------------------ */
#define SHARD_SEARCH 12        /* v3.4：实际值改成 8 + 层数×1.5，见 GoldSearchAt() */         /* v2.0：7→12（一局满通 2160 → 3600 左右） */
#define SHARD_POWER  32        /* v3.4：实际值改成 25 + 层数×3，见 GoldBreakerAt() */         /* v2.0：20→32 */
#define SHARD_EXIT   50        /* v3.4：实际值改成 30 + 层数×6，见 GoldExitAt() */         /* v2.0：35→50 */

/* ===== v3.4 金币产出渠道（方案 v5 §4）=====
   原来金币只绑「搜箱」，那是重复劳动。
   现在把奖励绑到**需要技巧的动作**上：解谜 / 完美校准 / 甩脱追捕 / 探索 / 潜行。 */
/* v3.4 货币合并：1 核心 = 1000 金币（原来核心是稀有的，
       合并后按这个比率折算，保持顶级节点的「贵」感） */
#define GOLD_PER_CORE 1000

/* ===== v3.4（方案 v5 §2）银行博弈 =====
   局外有银行存款，进局前选「带多少进去」。
   带得多 → 局内能买的道具多 + 结算倍数高，但死了损失大。
   带得少 → 一切按比例缩水，且低于 20% 还要额外惩罚。 */
/* ===== v6 **威胁分（ThreatScore）算法** =====
   把怪物的每个数值先**归一化**（除以参考值，让基准 = 1.0），
   再按「对玩家威胁的真实贡献」加权求和。这是行业里算综合强度的标准做法：
     ① 归一化   stat / REF   （不同量纲拉到同一尺度）
     ② 加权     × w          （体现哪个数值真的更致命）
     ③ 求和     Σ            （得到无量纲的综合分）
   权重之和 = 1.0，所以「基准怪物」的威胁分正好是 1.0。 */
#define THREAT_REF_SPEED     1.90f   /* 寻声怪基础速度 */
#define THREAT_REF_HEAR      14.0f   /* 标准噪音感知半径 */
#define THREAT_REF_PERSIST   8.0f    /* 标准追捕持久秒数 */
#define THREAT_W_SPEED       0.30f   /* 速度：跑得快最难甩 */
#define THREAT_W_HEAR        0.40f   /* 听觉：这游戏怪物主要靠听，权重最高 */
#define THREAT_W_PERSIST     0.20f   /* 持久：追得久 = 压力大 */
#define THREAT_W_REACT       0.10f   /* 反应：重算路径越快越难骗 */

/* 金币招怪：每 5000 金币 +15% 威胁，上限 +100% */
#define GOLD_THREAT_PER      5000.0f
#define GOLD_THREAT_STEP     0.15f
#define GOLD_THREAT_MAXBONUS 1.00f

#define BANK_DEATH_REFUND   0.35f  /* v6：死亡保留 35%。
   调研原话：「partial salvage … keeps ~35% … 全损最大化张力，但也最大化怒退。
   这是赌场和健身房的区别 —— 两者都惩罚贪心，但只有一个让你下次还愿意来。」 */
#define BANK_LOW_CARRY      0.20f  /* 低于历史最高的 20% 算「躺平」 */
#define BANK_LOW_PENALTY    0.50f  /* 躺平惩罚：本局利润 ×0.5 */
#define BANK_MAX_MULT       2.00f  /* 倍数上限 */
/* v6 **滑动额选择**：0% ~ 100%，每档 5%，共 21 档。
   用户要「左右滑动选带多少」，所以档位必须足够密。
   百分比 = 档位 × 5。 */
#define BANK_CARRY_STEPS    21
#define BANK_STEP_PCT       5      /* 每档 5% */


#define GOLD_LOCK_BASE      40      /* 解开一个锁盘：40 + 层数×5 */
#define GOLD_LOCK_STEP      5
#define GOLD_CALIB_PERFECT  35      /* 校准完美 */
#define GOLD_CALIB_GOOD     15      /* 校准良好 */
#define GOLD_ESCAPE         30      /* 成功甩脱追捕（追捕者放弃） */
#define GOLD_NEWROOM        5       /* 首次进入新房间 */
#define GOLD_QUIET_BASE     20      /* 本层没触发警报：20 + 层数×2 */
#define GOLD_QUIET_STEP     2
#define BANK_STREAK_AFTER 3     /* 连续上缴几层开始给加成 */
#define BANK_STREAK_BONUS 0.10f /* 加成比例 */

/* ------------------------------------------------------------------ */
/* v1.8 结算分数                                                        */
/* ------------------------------------------------------------------ */
#define SCORE_MAINLINE 10000    /* 主线固定分 */
#define SCORE_BASE     10000
#define SCORE_W_DEPTH  40
#define SCORE_W_COLL   20
#define SCORE_W_KILL   15
#define SCORE_W_TIME   15
#define SCORE_W_DONE   10
#define SCORE_DEPTH_FULL 64.0f       /* log 归一的分母 */
#define SCORE_BOXES_PER_LEVEL 6.0f
#define SCORE_KILL_FULL 40.0f
#define SCORE_SECONDS_PER_LEVEL 55.0f
#define SCORE_DEPTH_MULT_CAP 3.0f

/* 存档里的键位槽固定 32 个：以后加动作不再改变存档布局 */
#define BINDING_SLOTS 32

/* HELP 页：前 10 行是按键说明书（按当前绑定动态生成），后面是玩法说明 */
#define HELP_KEY_ROWS 10
#define HELP_TEXT_ROWS 13
#define HELP_ROWS (HELP_KEY_ROWS + HELP_TEXT_ROWS)
#define HELP_PAGE_ROWS 8    /* v2.2：每页放 8 行；两页 = 16 行，800x600 正好装下 */
#define HELP_PAGES 2        /* 按键页 / 规则页 */

/* ------------------------------------------------------------------ */
/* 成就系统                                                              */
/* ------------------------------------------------------------------ */
#define ACH_COUNT 40     /* v2.0：成就 16 → 40 */
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
    ACT_SWING,             /* v1.8 绣刀挥砍（默认 Q） */
    ACT_DECOY,             /* v1.8 投石（默认 G） */
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
    [ACT_THROW] = VK_RBUTTON, [ACT_CANCEL_CHARGE] = VK_LBUTTON, \
    [ACT_SELECT_1] = '1', [ACT_SELECT_2] = '2', [ACT_SELECT_3] = '3', \
    [ACT_PAUSE] = VK_ESCAPE, \
    [ACT_MENU_UP] = VK_UP, [ACT_MENU_DOWN] = VK_DOWN, \
    [ACT_MENU_LEFT] = VK_LEFT, [ACT_MENU_RIGHT] = VK_RIGHT, \
    [ACT_MENU_CONFIRM] = VK_RETURN, [ACT_MENU_CANCEL] = VK_ESCAPE, \
    [ACT_DODGE] = VK_SPACE, \
    [ACT_SWING] = 'Q', [ACT_DECOY] = 'G', \
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

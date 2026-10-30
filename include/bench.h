/*
 * Canity — 局内改造树（v2.7）
 *
 * 安全屋按 T 打开。4 条分支 × 3 层 = 12 个节点，用**局内碎片**买，
 * **跨局保留**（元进度）。和局外 20 节点技能树互不相干。
 *
 * v3.3 修正：这里原来写「每局重置」，但实现和存盘都是跨局累积的
 * （benchOwnedSave 进 SaveBlob，BenchResetRun 全项目 0 调用点）——
 * 玩家花碎片买的东西不该一局就没了。注释跟着实现改，函数保留待用。
 *
 * 设计约束（见 docs 调研记录）：
 *   · 精确数字，不用抽象图标 —— "能量上限 +15" 而不是 "★★☆"
 *   · 每层都能看到"为什么买不了"：前置 / 碎片差多少
 *   · 连线表达状态：已解锁实线，未解锁虚线
 */
#ifndef CANITY_BENCH_H
#define CANITY_BENCH_H

#include "common.h"

#define BENCH_COLS 4
#define BENCH_ROWS 4   /* v3.4（方案 v5 §3.2）：3 → 4，12 节点扩到 16 */
#define BENCH_COUNT (BENCH_COLS * BENCH_ROWS)

/* 节点 id = col * BENCH_ROWS + row  (row 0 = 最便宜那层) */

typedef struct {
    const char* name;        /* 英文名 */
    const char* nameZh;      /* 中文名 */
    const char* desc;        /* 精确效果（英文） */
    const char* descZh;      /* 精确效果（中文） */
    int cost;                /* 碎片价格 */
    int col, row;            /* 网格位置（用来画） */
    int star;                /* 1 = ★ 节点（第三条） */
} BenchNode;

extern const BenchNode benchNodes[BENCH_COUNT];

/* 本局已购买位掩码（1 << id） */
extern unsigned int benchOwned;
extern int benchKills;      /* 嗜血：本局击杀计数 */

void BenchResetRun(void);                  /* 开局清空 */
int  BenchHas(int id);                     /* 是否已购买 */
int  BenchPrereqOk(int id);                /* 前置是否满足 */
int  BenchCanBuy(int id);                  /* 前置 + 碎片都够 */
int  BenchTryBuy(int id);                  /* 返回 0=成功，否则失败原因码 */

/* —— 效果查询（没买就是 0 / 1.0） —— */
int   BenchEnergyMax(int base);            /* 弹匣 / 电池库 / 嗜血 加成后的上限 */
float BenchDamageMul(void);                /* 穿甲：武器伤害倍率 */
float BenchSanityDrainMul(void);           /* 厚皮：理智流失倍率 */
float BenchFlashDrainMul(void);            /* 省电：手电耗电倍率 */
float BenchBatteryPickupMul(void);         /* 电池库：电池拾取倍率 */
float BenchLowSpeedAdd(void);              /* 稳心：低理智移速加成 */
int   BenchArmorIgnore(void);              /* 穿甲：无视护甲点数 */
int   BenchDarkSense(void);                /* 听风：关灯可视格数 */
int   BenchExecuteThreshold(void);         /* 处决：斩杀阈值百分比 */
int   BenchSecondWind(void);               /* 第二口气：还有没有 */
int   BenchMonsterSpeedMul(void);          /* 畸变：怪速倍率 */
float BenchDrainPenalty(void);             /* 畸变：理智流失倍率（惩罚） */
/* ---- v3.0 逃生试炼：新效果接口 ---- */
float BenchWalkNoiseMul(void);   /* 轻步：走路噪音 */
float BenchHideStealth(void);    /* 屏息：藏身被发现概率 */
int   BenchVanish(void);         /* 消失 */
int   BenchStaminaBonus(void);   /* 耐力：体力上限 */
float BenchSprintMul(void);      /* 爆发：冲刺速度 */
int   BenchSecondWind(void);     /* 二次呼吸 */
int   BenchHearing(void);        /* 听觉 */
int   BenchInnerEye(void);       /* 心眼 */
int   BenchPremonition(void);    /* 预知 */
float BenchFearDamageMul(void);  /* 厚皮：恐惧伤害 */
float BenchDarkFearMul(void);    /* 镇定：黑暗恐惧 */
int   BenchPlayDead(void);       /* 假死 */

int   BenchAlchemy(void);                  /* 旧接口，保留 */

/* ---- 画布视口（v2.9，照搬思维导图模型） ---- */
#define BENCH_NODE_WX  260.0f   /* v2.9：跟着框加宽 */      /* 节点世界间距（横向） */
#define BENCH_NODE_WY  130.0f      /* 节点世界间距（纵向） */
extern float treeVX, treeVY, treeVK;
extern int   treeDragging;

float BenchNodeWorldX(int id);
float BenchNodeWorldY(int id);
void  BenchFocusNode(int id);
void  BenchDragStart(int mx, int my);
void  BenchDragMove(int mx, int my);
void  BenchDragEnd(void);
void  BenchZoomAt(float factor, int mx, int my);
void  BenchResetView(void);
void  BenchPanBy(float dxScreen, float dyScreen);


/* ===== v3.4 新增的 10 个机制 accessor（方案 v5 §3.2）=====
   节点 id = col * BENCH_ROWS + row，BENCH_ROWS 现为 4。 */
float BenchSearchQuiet(void);    /* 1  QUIET LID    翻箱噪音倍率 */
int   BenchSilentThrow(void);    /* 2  SILENT THROW 投掷无声 */
int   BenchCrankWalk(void);      /* 5  CRANK WALK   边走边摇 */
int   BenchSlide(void);          /* 6  SLIDE        滑铲 */
int   BenchDeepPocket(void);     /* 8  DEEP POCKET  携带上限 +2 */
float BenchSteadyHand(void);     /* 9  STEADY HAND  蓄力时间倍率 */
int   BenchSalvage(void);        /* 10 SALVAGE      可捡回 */
int   BenchTwinThrow(void);      /* 11 TWIN THROW   双投 */
float BenchBatterySaver(void);   /* 13 SPARE CELL   耗电倍率 */
int   BenchCatchBreak(void);     /* 14 CATCH BREAK  挣脱 */

#endif

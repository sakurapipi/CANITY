/*
 * Canity — 宝箱怪 MIMIC
 *
 * 一个「看起来比普通箱子值钱」的箱子。惹它才有钱，绕开一分没有。
 *
 * 三条路（2026-11-05 用户拍板）：
 *   硬开        → 翻到 60% 被咬 -25，现形，追 8 秒
 *   回声箭      → 只做标记、不惊动，让你提前站好位置（奖励不打折）
 *   扳手敲      → 直接逼现形，**免掉那 25 点理智**
 *
 * 奖励（甩掉之后原地留下的空壳）：
 *   金币 150 + 50×层 / +1 保险丝 / +1 道具 / 结算 +400 分
 *
 * 它**不可杀**（本作怪物都不可杀）：扳手只能逼它现形和击退，打不死。
 *
 * 读 include/art_direction.h 的铁律：
 *   一 [AD-01]  伪装态剪影必须读作"箱子"，现形态剪影必须读作"怪"
 *   七 [AD-07]  用明暗解释厚度：现形态的背甲要和箱体同源，让玩家看出"就是那个箱子站起来了"
 *   九 [AD-09]  威胁与资源的可读性预算不同 —— 伪装态低调，现形态高对比
 *   四十 [AD-40] 多实例契约的三个特有登记点见文件末尾注释
 */
#ifndef CANITY_MIMIC_H
#define CANITY_MIMIC_H

#include "common.h"

/* ---- 状态 ---- */
enum {
    MIM_NONE = 0,
    MIM_DISGUISED,   /* 伪装成一个华丽箱子 */
    MIM_SEEN,        /* 被玩家识破（绕开，给 0 收益，永不再激怒） */
    MIM_ANGRY,       /* 现形追击 */
    MIM_GONE         /* 已离场（空壳已生成） */
};

/* ---- 每局上限：一局最多 2 只 ---- */
#define MIM_MAX              2

/* ---- 出现规则 ---- */
#define MIM_START_LEVEL      3       /* 第 3 层开始出现 */
#define MIM_PCT_EARLY        12      /* 第 3~5 层的概率 */
#define MIM_PCT_LATE         20      /* 第 6 层起的概率 */
#define MIM_LATE_LEVEL       6

/* ---- 触发 ---- */
#define MIM_REVEAL_PCT       0.60f   /* 翻找到这个进度会被咬 */
#define MIM_SAFE_PCT         0.45f   /* 低于这个进度松手，一次都不会被咬 */

/* ---- 追击 ---- */
#define MIM_CHASE_TIME       8.0f    /* 追 8 秒后钻回墙里 */
#define MIM_SPEED_MUL        0.55f   /* 相对玩家走路速 */
#define MIM_LOSE_DIST        3.5f    /* 拉开这么远算甩掉（相对速度 0.45×8=3.6，刚好够） */
#define MIM_CATCH_R          0.90f   /* 抓中判定半径 */

/* ---- 代价 ---- */
#define MIM_SCARE_SANITY     25.0f   /* 挨咬：必然发生、不吃抗性 */
#define MIM_CATCH_SANITY     35.0f   /* 被抓：和普通怪抓人一致 */

/* ---- 噪音（这是它真正的危险性） ---- */
#define MIM_NOISE_R          12.0f
#define MIM_NOISE_T          2.0f    /* 每 2 秒一次，8 秒共 4 次 */

/* ---- 扳手联动 ---- */
#define MIM_WRENCH_KNOCK     2.0f    /* 敲现形态：击退格数 */
#define MIM_WRENCH_STUN      1.0f    /* 敲现形态：硬直秒数 */

/* ---- 奖励 ---- */
#define MIM_GOLD_BASE        150
#define MIM_GOLD_STEP        50
#define MIM_SCORE_BONUS      400     /* 每只，一局上限 2 只 */

typedef struct {
    int   state;
    float x, y;
    float timer;          /* 当前状态已持续秒数 */
    float noiseT;         /* 距下一次噪音的秒数 */
    float breathPh;       /* 呼吸相位（三个破绽之一） */
    float stun;           /* 被扳手敲中后的硬直 */
    int   containerIdx;   /* 伪装时占着哪个箱子槽（-1 = 没有） */
    int   marked;         /* 被回声箭标记过（HUD 上显示红标） */
    int   seenTimer;      /* 玩家在附近观察了多久（识破用） */
} Mimic;

extern Mimic mimics[MIM_MAX];
extern int   mimicCount;
extern int   mimicsEscapedThisRun;   /* 结算加分用，上限 MIM_MAX */

/* ---- 生命周期 ---- */
void MimicReset(void);                          /* 换层 / 新局 */
void MimicTrySpawn(int level, int containerCount); /* GenerateLevel 里，必须在 ApplyLockboxes 之前调用 */
void MimicTick(float dt);                       /* Update 里 */

/* ---- 交互钩子 ---- */
int  MimicOnSearchStep(int containerIdx, float progress); /* 返回 1 = 被咬、翻找被打断 */
void MimicOnEchoHit(int containerIdx);          /* 回声箭：只标记，不惊动 */
int  MimicWrenchSweep(float px, float py, float facing, float range); /* 返回 1 = 敲到了 */
void MimicOnGlue(float x, float y);             /* 胶水落点 */

/* ---- 渲染 ---- */

#endif /* CANITY_MIMIC_H */

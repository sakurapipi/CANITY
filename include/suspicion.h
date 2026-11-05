/*
 * Canity — 可疑点系统（v7）
 * 见 src/suspicion.c 顶部说明。
 */
#ifndef CANITY_SUSPICION_H
#define CANITY_SUSPICION_H

#define SUSP_MAX 12

/* 来源类型 —— 加新怪时只需要关心它读哪种 source */
#define SUSP_WALK    0    /* 走路 / 跑步的被动噪音 */
#define SUSP_SEARCH  1    /* 翻箱 / 摇电 / 敲墙 */
#define SUSP_DECOY   2    /* 玩偶叫声（最强） */
#define SUSP_THROW   3    /* 投掷物落点（回声就用这条） */
#define SUSP_SEEN    4    /* 玩家被看见 */
#define SUSP_HIDE    5    /* 藏身点被用过 */
#define SUSP_GOLD    6    /* 地上的钱袋（v7） */

typedef struct {
    float x, y;
    float strength;   /* 0~100 */
    float decay;      /* 每秒衰减 */
    float ttl;        /* 剩余寿命（秒） */
    int   source;     /* SUSP_*，-1 = 空槽 */
} SuspicionPoint;

extern SuspicionPoint susp[SUSP_MAX];

void SuspicionReset(void);
void SuspicionAdd(float x, float y, float strength, float decay, float ttl, int source);
void SuspicionTick(float dt);
int  SuspicionBest(float fromX, float fromY, float distPenalty);

/* ==================================================================
 * 2026-11-06（v2.4-beta B03）：**带声学传播的挑点**
 * ------------------------------------------------------------------
 * 和 SuspicionBest 的区别只有一条：距离不用直线，用**沿可通行路径的步数**，
 * 并且把点的强度乘上从听者出发的声学可达度。
 *
 * 于是「隔着一堵墙的脚步声」对它的有效强度会掉到阈值以下 ——
 * 这就是"隔墙听不见"。原来是直线距离，墙完全不影响。
 *
 * outReach（可为 NULL）= 选中那一点的可达度 0..1，
 *   调用方用它把 strength 折算成"有效强度"再和阈值比较。
 * maxListen = 这次能听多远（格）；超过就完全听不见。
 * ================================================================ */
int  SuspicionBestReach(float fromX, float fromY, float distPenalty,
                        float maxListen, float* outReach);
void SuspicionConsume(int idx);
int  SuspicionClearNear(float x, float y, float radius);
int  SuspicionStrongest(void);
int  SuspicionActiveCount(void);

#endif /* CANITY_SUSPICION_H */

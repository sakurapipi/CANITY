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
void SuspicionConsume(int idx);
int  SuspicionClearNear(float x, float y, float radius);
int  SuspicionStrongest(void);
int  SuspicionActiveCount(void);

#endif /* CANITY_SUSPICION_H */

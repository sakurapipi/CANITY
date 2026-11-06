/*
 * Canity — 缚丝蛛（v2.4.1 第 5~6 轮）
 *
 * ==================================================================
 * 定位
 * ------------------------------------------------------------------
 * 它**不是**又一只追杀玩家的怪，而是改变行动节奏的控制型敌人：
 *   吐丝 → 限制移动 → 迫使玩家挣脱 → 为其它怪创造追击机会
 *
 * 硬约束：
 *   · 不造成任何伤害（不掉血、不掉 SAN、不置 tookDamage）
 *   · 不杀死玩家
 *   · 超时自动释放，绝不会把人永久困住
 *   · 与抱脸虫共用控制层，不会同时生效
 *   · 每层最多 1 只，不写入存档
 * ==================================================================
 */
#ifndef CANITY_SPIDER_H
#define CANITY_SPIDER_H

/* 状态（渲染与测试都要读） */
enum {
    SPIDER_IDLE = 0,
    SPIDER_PATROL,
    SPIDER_AIM,        /* 前摇：抬起前肢，有声音提示 */
    SPIDER_FIRE,       /* 吐丝瞬间 */
    SPIDER_COOLDOWN
};

#define SPIDER_MIN_LEVEL      4
#define SPIDER_APPEAR_CHANCE  30     /* 百分比 */
#define SPIDER_AIM_TIME       0.8f   /* 前摇（用户要求 0.7~1.0） */
#define SPIDER_RANGE          4.5f   /* 射程（格） */
#define SPIDER_COOLDOWN_TIME  8.0f
#define SPIDER_MOVE_SPEED     0.85f  /* 远慢于玩家 2.0 */
#define SPIDER_WEB_MASH       3
#define SPIDER_WEB_WINDOW     2.0f

void  SpiderReset(void);
int   SpiderTrySpawn(int level);    /* 返回 1 = 本层有蜘蛛 */
void  SpiderTick(float dt);
int   SpiderActive(void);
int   SpiderState(void);
float SpiderX(void);
float SpiderY(void);

#endif /* CANITY_SPIDER_H */

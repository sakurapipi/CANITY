#ifndef CANITY_EXTRAS_H
#define CANITY_EXTRAS_H

/* ------------------------------------------------------------------ */
/* v2.0 额外威胁（怪物 4 -> 7 种）                                      */
/*   为了不重写 AI 骨架，这里有三个"轻量怪"：                           */
/*     SHADE  影子    —— 只在关灯时存在，贴上来啃理智                    */
/*     SPIDER 盲螨    —— 跑得极快但血少，只对声音有反应                  */
/*     ECHO   回响    —— 不动，但会反复在你附近闪现                      */
/*   它们共用一套位置/计时器，渲染走同一张剪影（眼睛颜色区分）。          */
/* ------------------------------------------------------------------ */

#define EXTRA_MAX 6   /* v2.0：3→6，每层最多 6 只游荡怪 */

typedef struct {
    float x, y;
    float vx, vy;
    float tx, ty;     /* 最后一次听到噪音的位置（盲螨用） */
    float timer;      /* 行为计时器（闪现/冲刺冷却） */
    int   active;
    int   kind;       /* 0 SHADE / 1 SPIDER / 2 ECHO */
    int   cool;       /* 接触伤害后的短暂无敌 */
    int   hp;         /* v2.2：血量（原来没有 hp，三把武器都打不到它们） */
    int   hasTarget;  /* 1 = 有噪音目标；盲螨平时不知道玩家在哪 */
} Extra;

extern Extra extras[EXTRA_MAX];

void ExtrasReset(void);
void ExtrasSpawnForLevel(int level);
void ExtrasSpawnOne(int idx);
void UpdateExtras(float dt);

/* v2.2：对指定位置附近的游荡怪造成伤害，返回打中的只数 */
int  DamageExtrasAt(float x, float y, float radius, int dmg);

/* 供渲染使用 */
int  ExtrasCount(void);
const Extra* ExtraAt(int i);

#endif

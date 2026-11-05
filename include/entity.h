/*
 * Canity — 实体层（玩家状态 / 状态效果 / 怪物 AI / 跳脸虫 QTE）
 *
 * 只依赖 common.h。全局状态在 entity.c 定义并赋初值，这里只做 extern。
 */
#ifndef CANITY_ENTITY_H
#define CANITY_ENTITY_H

#include "common.h"

/* ---- 玩家（位置 / 朝向 / 相机平面） ---- */
extern float posX, posY;
extern float dirX, dirY;
extern float planeX, planeY;

/* ---- 玩家生存数值 ---- */
extern float sanity;
extern float battery;
extern int flashlightOn;
extern float stamina;
extern int sprinting;
extern float staminaIdle;

/* ---- 怪物 ----
   听声鬼现在是一个数组（monHunters[MON_HUNTER_MAX]），
   monster / monster2 / monster3 是它的别名，定义在 common.h。
   数组实体本身也在 common.h 里 extern 了，这里不再重复声明。 */
/* ==================================================================
 * 2026-11-06（用户要求）：**追光怪 +1 只**
 * ------------------------------------------------------------------
 * 照抄听声鬼的既有做法（`monHunters[]` + `#define monster monHunters[0]`）：
 *   实体变成数组，旧的单数名字保留成宏指向 [0]，
 *   于是原有 132 处引用**一行都不用改**。
 * 第二只通过 lightBeast2 / UpdateLightBeast2 访问。
 * ================================================================ */
#define LIGHTBEAST_MAX 2
extern Monster lightBeasts[LIGHTBEAST_MAX];
#define lightBeast  lightBeasts[0]
#define lightBeast2 lightBeasts[1]
void UpdateMonster2(float dt);
void UpdateMonster3(float dt);
extern Monster parasite;        /* 寄生怪 / 跳脸虫共用（只用位置和几个状态字段） */

/* ---- 跳脸虫 QTE 运行时状态 ---- */
extern int jumperPhase;
extern float jumperTimer;       /* 当前阶段的倒计时 */
extern int jumperQteKey;        /* 阶段二要按的键编号 */
extern int jumperResolved;
/* 2026-11-06：一局累计出过几只 / 失败后的控制计时 */
extern int   jumperSpawnedRun;
extern float jumperControlT;      /* 本次事件是否已结算（防止重复结算） */
extern float jumperTrigger;     /* 本关什么时候触发（秒）；<0 = 本关不出现 */

/* ---- v1.8 偷电怪 ---- */
void ResetLeeches(void);
void SpawnLeech(int index);
void UpdateLeech(float dt);
int  LeechNearestDist2(void);      /* 最近偷电怪的距离平方，-1 = 没有 */

/* ---- 状态效果 ---- */
float FxStrength(int kind);
void ApplyFx(int kind, float strength, float duration);
int UpdateFx(float dt);          /* 返回 1 = 某个效果强度变了，调用方要重算 FOV */
void ClearFx(void);

/* ---- 怪物 ---- */
void SpawnMonster(void);
void UpdateMonster(float dt);
void SpawnLightBeast(void);
void SpawnLightBeast2(void);   /* 第二只（2026-11-06） */
void UpdateLightBeast(float dt);
void UpdateLightBeast2(float dt);   /* 第二只（2026-11-06） */
void SpawnParasite(void);

/* ==================================================================
 * 2026-11-06（v2.4-beta B04）：**以记忆点为锚的定向搜查**
 * ------------------------------------------------------------------
 * 在 (cx,cy) 周围 1.5~4.4 格的环上挑一个走得通的点，写进 m->tx/ty。
 * 角度由 m->memAge 驱动，所以连续几次搜查会绕环分散开。
 * 环上都不通时退回 MonsterWanderFor。
 * ================================================================== */
void MonsterSearchAround(Monster* m, float cx, float cy);
void UpdateParasite(float dt);

/* ---- 感知 / 受击 ---- */
int HasLOS(float x0, float y0, float x1, float y1);
void MakeNoise(float x, float y, float radius);
extern float noiseDisplay;        /* v3.0：HUD 噪音条 0..1 */
extern float noiseDisplayTimer;
extern float noiseSrcX, noiseSrcY;
int ApplyDamage(Monster* m, int base, int type, float sx, float sy);


/* v2.4.94 封喉 / 包夹（flank.c） */
void  FlankBuild(void);
void  FlankReset(void);
void  FlankUpdate(float dt);
void  FlankOnCaught(void);
int   FlankChokeOnExitPath(float px, float py, float* gx, float* gy);
int   FlankBlocksPlayer(float nx, float ny);
int   FlankIsBlocking(void);
float FlankGoalX(void);
float FlankGoalY(void);
int   FlankStreak(void);

#endif /* CANITY_ENTITY_H */

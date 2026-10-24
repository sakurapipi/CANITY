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

/* ---- 怪物 ---- */
extern Monster monster;
extern Monster lightBeast;      /* 只看光 */
extern Monster parasite;        /* 寄生怪 / 跳脸虫共用（只用位置和几个状态字段） */

/* ---- 跳脸虫 QTE 运行时状态 ---- */
extern int jumperPhase;
extern float jumperTimer;       /* 当前阶段的倒计时 */
extern int jumperQteKey;        /* 阶段二要按的键编号 */
extern int jumperResolved;      /* 本次事件是否已结算（防止重复结算） */
extern float jumperTrigger;     /* 本关什么时候触发（秒）；<0 = 本关不出现 */

/* ---- 状态效果 ---- */
float FxStrength(int kind);
void ApplyFx(int kind, float strength, float duration);
int UpdateFx(float dt);          /* 返回 1 = 某个效果强度变了，调用方要重算 FOV */
void ClearFx(void);

/* ---- 怪物 ---- */
void SpawnMonster(void);
void UpdateMonster(float dt);
void SpawnLightBeast(void);
void UpdateLightBeast(float dt);
void SpawnParasite(void);
void UpdateParasite(float dt);

/* ---- 感知 / 受击 ---- */
int HasLOS(float x0, float y0, float x1, float y1);
void MakeNoise(float x, float y, float radius);
int HitMonster(Monster* m, int dmg);

#endif /* CANITY_ENTITY_H */

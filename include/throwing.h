#ifndef CANITY_THROWING_H
#define CANITY_THROWING_H
/* Canity — 投掷物系统（v8 重构第六刀 C） */
void ThrowItem(int type, float charge);   /* 发射一个投掷物 */
void UpdateShots(float dt);               /* 每帧飞行 + 落地分派 */
void EchoTryTrigger(float x, float y);    /* v2.3.6 回响统一入口（全场最多一个） */

/* Per-floor flare healing lifecycle. */
void FlareHealTick(float dt);

void FlareHealReset(void);

#endif

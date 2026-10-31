#ifndef CANITY_AI_COMMON_H
#define CANITY_AI_COMMON_H
/* Canity — AI 共用工具（v8 重构第三刀） */
int  AISpawnPointAround(float* ox, float* oy, float minD, float maxD);
void AIMoveToward(float* x, float* y, float tx, float ty, float sp, float dt);
#endif

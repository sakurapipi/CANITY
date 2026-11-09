#ifndef CANITY_HUSH_H
#define CANITY_HUSH_H
void HushReset(void);
void HushTick(float dt);
void HushActivate(void);
float HushNoiseMul(void);
void HushDraw(void);
unsigned HushRuntimeMask(void);
int HushRuntimeFloor(void);
void HushRestoreRuntime(int floor,unsigned mask);
#endif

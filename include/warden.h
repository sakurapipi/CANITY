#ifndef CANITY_WARDEN_H
#define CANITY_WARDEN_H
/* Canity — THE WARDEN 守门人（v7 B1） */
int WardenWatching(void);
/* 2026-11-03：锁定倒计时（看见玩家后 1.5 秒才开追），HUD 用 */
int   WardenSpotting(void);
float WardenSpotLeft(void);
float WardenSpotFrac(void);
int   WardenActive(void);
float WardenX(void);
float WardenY(void);
int   WardenBlocking(void);
void  WardenReset(void);
void  WardenSpawnIfPowered(void);
void  UpdateWarden(float dt);
#endif

#ifndef CANITY_WARDEN_H
#define CANITY_WARDEN_H
/* Canity — THE WARDEN 守门人（v7 B1） */
int WardenWatching(void);
int   WardenActive(void);
float WardenX(void);
float WardenY(void);
int   WardenBlocking(void);
void  WardenReset(void);
void  WardenSpawnIfPowered(void);
void  UpdateWarden(float dt);
#endif

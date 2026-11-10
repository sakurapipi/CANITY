#ifndef CANITY_MONSTER_RULES_H
#define CANITY_MONSTER_RULES_H
enum { SPAWN_HUNTER, SPAWN_LIGHT, SPAWN_SPIDER, SPAWN_COLLECTOR,
       SPAWN_MIMIC, SPAWN_SCAVENGER, SPAWN_WARDEN, SPAWN_KIND_COUNT };
int MonsterMinimumFloor(int kind);
int MonsterFloorChance(int kind,int level);
float HunterSpawnDelay(int level);
float MonsterMovementCap(void);
float HunterMoveSpeed(int index,int level,float modifier,float threat);
int MonsterChaseActive(void);
int MonsterEligible(int kind,int level);
#endif

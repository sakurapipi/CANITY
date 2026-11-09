#include "common.h"
#include "monster_rules.h"
#include "entity.h"
#include "world_api.h"
#include "loop_api.h"
#include "economy.h"
#include "threat.h"
int MonsterMinimumFloor(int kind){return kind>=0 && kind<SPAWN_KIND_COUNT?1:INT_MAX;}
int MonsterFloorChance(int kind,int level) {
    if(!MonsterEligible(kind,level))return 0;
    if(kind==SPAWN_COLLECTOR){int chance=14+level*7;return chance>90?90:chance;}
    if(kind==SPAWN_SCAVENGER)return 100; /* Trigger/time policy decides when. */
    static const int initial[SPAWN_KIND_COUNT]={100,65,32,65,25,100,100};
    int chance=initial[kind]+GoldTier()*2+(int)ThreatValue();return chance>100?100:chance;
}
float HunterSpawnDelay(int level) {
    if(level<1)level=1;
    return fmaxf(isEndlessRun?16.0f:24.0f,60.0f-(level-1)*2.0f-GoldTier()*1.3f-ThreatValue()*.8f);
}
float HunterMoveSpeed(int index,int level,float modifier,float threat) {
    if(level<1)level=1;
    if(!isfinite(modifier) || modifier<.1f)modifier=1;
    if(!isfinite(threat) || threat<0)threat=0;
    float base=1.30f+fminf(.35f,(level-1)*.035f)+fminf(.12f,threat*.006f);
    float cap=index==2?2.25f:1.85f;
    float speed=base*modifier*(index==2?1.26f:1.0f);
    /* Below unmodified walking speed (2.5), even with the HUNTED modifier. */
    return fminf(cap,speed);
}
int MonsterChaseActive(void) {
    for(int i=0;i<MON_HUNTER_MAX;i++)if(monHunters[i].active && monHunters[i].state==1)return 1;
    for(int i=0;i<LIGHTBEAST_MAX;i++)if(lightBeasts[i].active && lightBeasts[i].state==1)return 1;
    return 0;
}

int MonsterEligible(int kind,int level){
    if(kind<0 || kind>=SPAWN_KIND_COUNT || level<1)return 0;
    if(kind==SPAWN_HUNTER || kind==SPAWN_COLLECTOR || kind==SPAWN_SCAVENGER)return 1;
    if(level==1 && kind!=SPAWN_LIGHT)return 0;
    float strength=GoldTier()+ThreatValue()*.65f+fmaxf(0,level-1)*.85f;
    static const float threshold[SPAWN_KIND_COUNT]={0,1.4f,3,4,2.5f,6,7};
    return strength>=threshold[kind];
}

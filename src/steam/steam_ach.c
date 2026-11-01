#include <string.h>
#include "achievements.h"
#include "steam_ach.h"
#define ACH_DEF(en,zh,de,dz,cat,reward,hidden,kind,target,steam) {steam,en,zh,cat,reward,hidden},
const SteamAchDef kSteamAchievements[] = {
#include "achievement_defs.inc"
};
#undef ACH_DEF
const int kSteamAchievementCount=sizeof(kSteamAchievements)/sizeof(kSteamAchievements[0]);
typedef char steam_count_check[(sizeof(kSteamAchievements)/sizeof(kSteamAchievements[0])==ACH_COUNT)?1:-1];
int SteamAchievementIndex(const char* name) {
    if(!name) return -1;
    for(int i=0;i<kSteamAchievementCount;++i) if(strcmp(name,kSteamAchievements[i].apiName)==0) return i;
    return -1;
}

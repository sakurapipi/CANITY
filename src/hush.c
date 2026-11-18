#include "common.h"
#include "tutorial.h"
#include "hush.h"
#include "run_economy.h"
#include "protocol.h"
#include "entity.h"
#include "skills.h"
#include "world_api.h"
#include "loop_api.h"
#include "player_api.h"
#include "present_api.h"
#include "lang.h"
#include "audio.h"
#include "ui.h"
#include "ui_theme.h"
enum { SPARK_USED=1u, STAMINA_USED=2u, EXIT_MAPPED=4u, BREATHER_USED=8u, SUPPLY_USED=16u };
static float hiddenTime=0;
static int runtimeFloor=-1;static unsigned runtimeMask=0;
void HushReset(void) {hiddenTime=0;runtimeFloor=-1;runtimeMask=0;}
unsigned HushRuntimeMask(void){return runtimeMask;}
int HushRuntimeFloor(void){return runtimeFloor;}
void HushRestoreRuntime(int floor,unsigned mask){runtimeFloor=floor;runtimeMask=mask&31u;hiddenTime=0;}
static void RecoverEscapeSupply(void) {
    if (!SkillOwned(SK_DECOY) || (runtimeMask&SUPPLY_USED) || trainingMode || dailyMode || protocolRun.story) return;
    int floor,rhythmGold;
    unsigned paid;
    RunEconomyCapture(&floor,&paid,&rhythmGold);
    if (floor!=currentLevel || !(paid&((1u<<(MON_HUNTER_MAX+LIGHTBEAST_MAX))-1))) return;
    int stone=bottles<BottleCap(), light=SkillOwned(SK_HUSH_ECHO) && flares<BottleCap();
    float charge=SkillOwned(SK_ECHO_MAPPING)?fminf(8,(float)BatteryMax()-battery):0;
    if (!stone && !light && charge<=0) return;
    bottles+=stone;flares+=light;battery+=fmaxf(0,charge);
    runtimeMask|=SUPPLY_USED;
    pickupMsg=Tr("EMERGENCY SUPPLY RECOVERED","已获得应急补给");
    pickupMsgTimer=2;
    PlaySfx(SFX_PICKUP,.35f);
}
void HushTick(float dt) {
    if(!(dt>0) || ProtocolPeacefulStory())return;
    if(runtimeFloor!=currentLevel){runtimeFloor=currentLevel;runtimeMask=0;hiddenTime=0;}
    if(SkillOwned(SK_BACKUP_SPARK) && battery<=0 && !(runtimeMask&SPARK_USED)){battery=12;runtimeMask|=SPARK_USED;PlaySfx(SFX_POWER,.35f);}
    if(hiddenIn>=0)hiddenTime+=dt;else hiddenTime=0;
    if (hiddenIn>=0) RecoverEscapeSupply();
    int rest=SkillClampRank(SK_REST,skillRank[SK_REST]);
    if (rest>0 && hiddenTime>=3 && !(runtimeMask&BREATHER_USED) && sanity<SanityMax()-1) {
        sanity=fminf(SanityMax(),sanity+3+3*rest);
        runtimeMask|=BREATHER_USED;
        PlaySfx(SFX_PICKUP,.25f);
    }
    if(SkillOwned(SK_HIDE_RECOVERY) && hiddenTime>=3 && !(runtimeMask&STAMINA_USED)){stamina=fminf(StaminaMax(),stamina+20);runtimeMask|=STAMINA_USED;}
    if(SkillOwned(SK_EXIT_MEMORY) && breakerOpen && !(runtimeMask&EXIT_MAPPED)){
        for(int y=(int)exitY-2;y<=(int)exitY+2;y++)for(int x=(int)exitX-2;x<=(int)exitX+2;x++)if(x>0 && y>0 && x<MAP_WIDTH-1 && y<MAP_HEIGHT-1)explored[y][x]=1;
        runtimeMask|=EXIT_MAPPED;
    }
}

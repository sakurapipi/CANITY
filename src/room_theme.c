#include "common.h"
#include "room_theme.h"
#include "cell_meta.h"
#include "hatch.h"
#include "lockbox.h"
#include "core_rng.h"
#include "map_rooms.h"
#include "map_rooms.h"
#include "entity.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "collapse.h"
#include "wallet.h"
#include "audio.h"
#include "lang.h"
#include "plat.h"
RoomThemeState roomTheme={0};
static int eventKind=-1;static float eventTime=0;
int RoomThemeForFloor(int level) {if(level<1)level=1;return ((level-1)/2)%7;}
int RoomThemeTemplateForFloor(int level) {static const int templates[7]={4,1,5,3,2,0,6};return templates[RoomThemeForFloor(level)];}
const char* RoomThemeName(int theme,int chinese) {
    static const char* en[7]={"ECHO EXCHANGE","DEBT VAULT","LUMEN OVERLOAD","EQUAL TRANSPLANT","RETURN SWITCH","MADNESS COVENANT","BORROWED TIME"};
    static const char* zh[7]={"声源置换井","债务密库","过载灯塔","等价移植舱","折返换位门","疯癫契约","借时心室"};
    return theme>=0 && theme<7?(chinese?zh[theme]:en[theme]):"";
}
void RoomThemeBind(void) {
    memset(&roomTheme,0,sizeof(roomTheme));eventKind=-1;eventTime=0;
    if(hatchCount<=0)return;
    roomTheme.active=1;roomTheme.theme=hatches[0].kind;
    roomTheme.switchX=hatches[0].x;roomTheme.switchY=hatches[0].y;
    roomTheme.spent=hatches[0].state>=HATCH_OPEN;
}
int RoomThemeSwitchNear(float x,float y) {
    /* PLATFORM_NOOP: Retired prop switch lookup; the hatch state machine is the only room-event executor. */
    (void)x;(void)y;return 0;
}
void RoomThemeUseSwitch(void) {
    /* PLATFORM_NOOP: Retired prop switch activation; the hatch state machine owns gates, costs and single-use execution. */
}
const char* RoomThemeInstruction(void) {return hatchCount>0?HatchEffectLine(0):"";}
float RoomThemeNoiseMul(void){return (RoomThemeMadness()?1.35f:1)*(eventKind==1 && eventTime>0?1.25f:1);}
void RoomThemeTick(float dt){
    if(!(dt>0))return;
    if(eventTime>0){eventTime-=dt;if(eventTime<=0){eventTime=0;int expired=eventKind;eventKind=-1;if(expired==6)MakeNoise(roomTheme.switchX,roomTheme.switchY,32);}}
    if(!roomTheme.active || trainingMode)return;
    int x=(int)posX,y=(int)posY;roomTheme.inside=x>0 && y>0 && x<MAP_WIDTH-1 && y<MAP_HEIGHT-1 && cellMeta[y][x].roomId;
    roomTheme.spent=hatchCount>0 && hatches[0].state>=HATCH_OPEN;
}
static int runMadness=0;
void RoomThemeSetMadness(int enabled) {runMadness=!!enabled;}
int RoomThemeMadness(void) {return runMadness;}
float RoomThemeSanityRatio(float actual) {return runMadness?fminf(.12f,actual):actual;}
float RoomThemeLootMul(void){return (RoomThemeMadness()?1.25f:1)*(eventKind==1 && eventTime>0?1.35f:1);}

void RoomThemeStartEvent(int kind,float seconds){eventKind=kind;eventTime=seconds;}
void RoomThemeSaveEvent(int* kind,float* time){*kind=eventKind;*time=eventTime;}
void RoomThemeLoadEvent(int kind,float time){eventKind=kind>=0 && kind<7?kind:-1;eventTime=isfinite(time)?fmaxf(0,fminf(90,time)):0;}
void RoomThemeRedirectNoise(float* x,float* y){if(eventKind==0 && eventTime>0 && Dist2(*x,*y,posX,posY)<.16f){*x=roomTheme.switchX;*y=roomTheme.switchY;}}
void RoomThemePlaceVault(void){
    if(trainingMode)return;
    for(int y=2;y<MAP_HEIGHT-2;y++)for(int x=2;x<MAP_WIDTH-2;x++){
        if(!cellMeta[y][x].roomId || worldMap[y][x] || PropBlocksAt(x,y))continue;
        if(HatchNearest(x+.5f,y+.5f,1.5f)>=0)continue;
        int clash=0;for(int i=0;i<containerCount;i++)if(Dist2(x+.5f,y+.5f,containers[i].x,containers[i].y)<2)clash=1;
        if(clash)continue;
        int index=containerCount<MAX_CONTAINERS?containerCount:-1;
        if(index<0)for(int i=containerCount-1;i>=0;i--)if(containers[i].content!=1){index=i;break;}
        if(index<0 && containerCount>0)index=containerCount-1;
        if(index<0)return;
        Container* c=&containers[index];int fuse=index<containerCount && c->content==1;memset(c,0,sizeof(*c));c->x=x+.5f;c->y=y+.5f;c->active=1;c->locked=1;
        c->lockType=(cellMeta[y][x].themeId+currentLevel)%LOCK_TYPE_COUNT;c->lockCode=(int)(rng_world_next()&0xFFF);c->content=fuse?1:10;
        if(index==containerCount)containerCount++;
        return;
    }
}

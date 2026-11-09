#include "common.h"
#include "tutorial.h"
#include "hush.h"
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
static float remaining=0,cooldown=0,hiddenTime=0;
static int runtimeFloor=-1;static unsigned runtimeMask=0;
void HushReset(void) {remaining=cooldown=hiddenTime=0;runtimeFloor=-1;runtimeMask=0;}
unsigned HushRuntimeMask(void){return runtimeMask;}
int HushRuntimeFloor(void){return runtimeFloor;}
void HushRestoreRuntime(int floor,unsigned mask){runtimeFloor=floor;runtimeMask=mask&7u;remaining=cooldown=hiddenTime=0;}
void HushTick(float dt) {
    if(!(dt>0))return;
    remaining=fmaxf(0,remaining-dt);cooldown=fmaxf(0,cooldown-dt);
    if(runtimeFloor!=currentLevel){runtimeFloor=currentLevel;runtimeMask=0;hiddenTime=0;}
    if(SkillOwned(SK_BACKUP_SPARK) && battery<=0 && !(runtimeMask&1u)){battery=12;runtimeMask|=1u;PlaySfx(SFX_POWER,.35f);}
    if(hiddenIn>=0)hiddenTime+=dt;else hiddenTime=0;
    if(SkillOwned(SK_HIDE_RECOVERY) && hiddenTime>=3 && !(runtimeMask&2u)){stamina=fminf(StaminaMax(),stamina+20);runtimeMask|=2u;}
    if(SkillOwned(SK_EXIT_MEMORY) && breakerOpen && !(runtimeMask&4u)){
        for(int y=(int)exitY-2;y<=(int)exitY+2;y++)for(int x=(int)exitX-2;x<=(int)exitX+2;x++)if(x>0 && y>0 && x<MAP_WIDTH-1 && y<MAP_HEIGHT-1)explored[y][x]=1;
        runtimeMask|=4u;
    }
}
float HushNoiseMul(void) {return remaining>0?.15f:1;}
void HushActivate(void) {
    if(cooldown>0 || battery<8){pickupMsg=LangTr("HUSH PULSE: REQUIRES 8 BATTERY / 24s COOLDOWN","静息脉冲：需要8电量，冷却24秒");pickupMsgTimer=1.5f;return;}
    TutorialRecordAction(TUTOR_PULSE);
    battery-=8;remaining=SkillOwned(SK_HUSH_ECHO)?6:4;cooldown=24;
    if(SkillOwned(SK_ECHO_MAPPING))for(int y=(int)posY-3;y<=(int)posY+3;y++)for(int x=(int)posX-3;x<=(int)posX+3;x++)if(x>0 && y>0 && x<MAP_WIDTH-1 && y<MAP_HEIGHT-1)explored[y][x]=1;
    PlaySfx(SFX_ECHO,.6f);PlaySfx(SFX_HEART,.3f);
    static char message[128];snprintf(message,sizeof(message),LangTr("HUSH PULSE: %.0fs / FOOTSTEP NOISE -85%%","静息脉冲：%.0f秒内脚步噪声降低85%%"),remaining);pickupMsg=message;pickupMsgTimer=1.4f;
}
void HushDraw(void) {
    if(remaining<=0)return;
    int cx=winW-UIS(95),cy=winH-UIS(180);
    float t=(SkillOwned(SK_HUSH_ECHO)?6:4)-remaining;
    for(int ring=0;ring<3;ring++) {
        float phase=fmodf(t*1.4f+ring*.333f,1);
        float radius=UIS(12)+phase*UIS(45);
        for(int point=0;point<40;point++) {
            float angle=point*6.2831853f/40;
            int x=cx+(int)(cosf(angle)*radius),y=cy+(int)(sinf(angle)*radius*.55f);
            BlendRectWin(x,y,x+2,y+2,UI_ACCENT,(1-phase)*.65f);
        }
    }
    DrawTextSmallWin(cx-UIS(35),cy+UIS(35),LangTr("HUSH","静息脉冲"),UI_ACCENT);
}

#include "event_runtime.h"
/* Theme-room runtime owns identity, ambience and expiring effects.
 * Costs and control interactions live in domain/rooms/room_actions.c. */
#include "common.h"
#include "room_theme.h"
#include "room_archetype.h"
#include "cell_meta.h"
#include "hatch.h"
#include "lockbox.h"
#include "core_rng.h"
#include "map_rooms.h"
#include "entity.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "audio.h"
#include "campaign.h"
#include "daily.h"
RoomThemeState roomTheme={0};
static int eventKind=-1,runMadness=0;
static float eventTime=0,noteTime=0;
static char roomNote[384];
void RoomThemeNotify(const char* text){snprintf(roomNote,sizeof(roomNote),"%s",text?text:"");noteTime=3.5f;uiDirty=1;}
const char* RoomThemeNotification(void){return noteTime>0?roomNote:"";}
int RoomThemeGuideVisible(void) {
    return roomTheme.active && hatchCount>0 && (roomTheme.inside ||
        RoomThemeContains(posX,posY) ||
        (Dist2(posX,posY,roomTheme.landmarkX,roomTheme.landmarkY)<9.f &&
         HasLOS(posX,posY,roomTheme.landmarkX,roomTheme.landmarkY)));
}
int RoomThemeForFloor(int level) {if(level<1)level=1;return ((level-1)/2)%7;}
int RoomThemeMaterialForFloor(int level) {
    int material;
    if(campaignMode)material=CampaignThemeNow();
    else if(dailyMode)material=DailyVisualThemeNow();
    else {
        const RoomArchetype* identity=RoomArchetypeGet(((level>0?level:1)-1)%ROOM_ARCHETYPE_COUNT);
        material=identity?identity->materialId:5;
    }
    return material>=0 && material<THEME_COUNT?material:5;
}
int RoomThemeTemplateForFloor(int level) {static const int templates[7]={4,1,5,3,2,0,6};return templates[RoomThemeForFloor(level)];}
const char* RoomThemeName(int theme,int chinese) {
    static const char* en[7]={"RESONANCE WELL","DEBT VAULT","OVERLOAD BEACON","VITAL EXCHANGE","SWAP GATE","MADNESS COVENANT","BORROWED HEART"};
    static const char* zh[7]={"声源诱导井","债务密库","过载灯塔","生命转换舱","折返换位门","疯癫契约","借时心室"};
    return theme>=0 && theme<7?(chinese?zh[theme]:en[theme]):"";
}
void RoomThemeRebuildControls(void) {
    roomTheme.prepared=0;roomTheme.choice=0;roomTheme.target=-1;
    for(int i=0;i<roomPropCount;i++) {
        RoomProp* p=&roomProps[i];
        if(p->active && p->kind==ROOMPROP_LANDMARK_FIRST+HATCH_BRICK && p->searched>0 && p->searched<=MON_HUNTER_MAX)
            roomTheme.target=p->searched-1;
        if(!p->active || !RoomPropIsControl(p->kind))continue;
        int theme=(p->kind-ROOMPROP_CONTROL_FIRST)/2,side=(p->kind-ROOMPROP_CONTROL_FIRST)%2;
        if(theme!=roomTheme.theme || !p->searched)continue;
        roomTheme.prepared|=1u<<side;
        if(theme==HATCH_WARD)roomTheme.choice=side+1;
    }
}
int RoomThemeContains(float x,float y) {
    if (hatchCount<=0 || !isfinite(x) || !isfinite(y) || x<1 || y<1 || x>=MAP_WIDTH-1 || y>=MAP_HEIGHT-1) return 0;
    return cellMeta[(int)y][(int)x].roomId!=0;
}

/* Rebuild missing side controls in older room layouts. */
static void RestoreRoomControls(int theme) {
    for (int side=0;side<2;side++) {
        int kind=ROOMPROP_CONTROL_FIRST+theme*2+side,present=0;
        for (int i=0;i<roomPropCount;i++) if (roomProps[i].active && roomProps[i].kind==kind) present=1;
        if (present) continue;
        float targetX=roomTheme.landmarkX+(side?2:-2),targetY=roomTheme.landmarkY+(side?1:-1);
        float best=1e9f,bx=0,by=0;
        for (int y=1;y<MAP_HEIGHT-1;y++) for (int x=1;x<MAP_WIDTH-1;x++) {
            float wx=x+.5f,wy=y+.5f;
            if (!RoomThemeContains(wx,wy) || worldMap[y][x] || RoomPropAt(wx,wy)>=0) continue;
            float score=Dist2(wx,wy,targetX,targetY);
            if (score<best) {best=score;bx=wx;by=wy;}
        }
        if (best<1e9f && RoomPropAdd(bx,by,kind)) {
            RoomProp *p=&roomProps[roomPropCount-1];
            p->blocks=0;p->height=.48f;p->searchLeft=1;
        }
    }
}
void RoomThemeBind(void) {
    memset(&roomTheme,0,sizeof(roomTheme));eventKind=-1;eventTime=0;noteTime=0;roomNote[0]=0;
    roomTheme.target=-1;roomTheme.layoutVersion=ROOM_LAYOUT_LEGACY;
    if(hatchCount<=0)return;
    roomTheme.active=1;roomTheme.theme=hatches[0].kind;
    roomTheme.guideExpanded=roomTheme.theme!=HATCH_BOILER;
    roomTheme.switchX=hatches[0].x;roomTheme.switchY=hatches[0].y;
    roomTheme.landmarkX=roomTheme.switchX;roomTheme.landmarkY=roomTheme.switchY;
    roomTheme.spent=hatches[0].state==HATCH_FIRED;
    if(roomTheme.theme==HATCH_STORAGE && !RoomThemeIsCurrent(0)) {
        int slot=-1;
        for(int i=0;i<roomPropCount;i++)if(roomProps[i].active && roomProps[i].kind==4){slot=i;break;}
        if(slot>=0) {
            roomProps[slot].kind=ROOMPROP_LANDMARK_FIRST+HATCH_STORAGE;
            roomProps[slot].searched=0;roomProps[slot].searchLeft=0;roomProps[slot].height=.94f;
        }else (void)RoomPropAdd(hatches[0].x,hatches[0].y,ROOMPROP_LANDMARK_FIRST+HATCH_STORAGE);
    }
    for(int i=0;i<roomPropCount;i++)if(roomProps[i].active &&
       roomProps[i].kind==ROOMPROP_LANDMARK_FIRST+roomTheme.theme) {
        roomTheme.layoutVersion=RoomLayoutVersion(roomSeedUsed);
        if(roomTheme.layoutVersion==ROOM_LAYOUT_LEGACY)roomTheme.layoutVersion=ROOM_LAYOUT_COMPACT;
        roomTheme.variant=RoomLayoutVariant(roomSeedUsed);
        roomTheme.landmarkX=roomProps[i].x;roomTheme.landmarkY=roomProps[i].y;
    }
    if ((roomTheme.theme==HATCH_POWER ||
         RoomThemeContains(roomTheme.landmarkX,roomTheme.landmarkY)) && !RoomThemeIsCurrent(0)) {
        if (RoomPropAdd(roomTheme.landmarkX,roomTheme.landmarkY,ROOMPROP_LANDMARK_FIRST+roomTheme.theme)) {
            RoomProp *prop=&roomProps[roomPropCount-1];
            prop->blocks=0;prop->searchLeft=0;prop->height=.94f;
        }
    }
    if(RoomThemeIsCurrent(0)) {
        hatches[0].x=roomTheme.landmarkX;hatches[0].y=roomTheme.landmarkY;
        roomTheme.switchX=roomTheme.landmarkX;roomTheme.switchY=roomTheme.landmarkY;
    }
    if (!roomTheme.spent && RoomThemeIsCurrent(0)) RestoreRoomControls(roomTheme.theme);
    roomTheme.ambientClock=1.0f;
    RoomThemeRebuildControls();
}
const char* RoomThemeInstruction(void) {return hatchCount>0?HatchEffectLine(0):"";}
float RoomThemeNoiseMul(void){return (runMadness?1.35f:1)*(eventKind==1 && eventTime>0?1.25f:1);}
float RoomThemeLootMul(void){return runMadness?1.25f:1;}
void RoomThemeSetMadness(int enabled) {runMadness=!!enabled;}
int RoomThemeMadness(void) {return runMadness;}
float RoomThemeSanityRatio(float actual) {return runMadness?fminf(.12f,actual):actual;}
void RoomThemeStartEvent(int kind,float seconds) {
    eventKind=kind>=0 && kind<7?kind:-1;
    eventTime=isfinite(seconds)?fmaxf(0,fminf(90,seconds)):0;
    roomTheme.ambientClock=.15f;roomTheme.elapsed=0;
}
void RoomThemeSaveEvent(int* kind,float* time) {if(kind)*kind=eventKind;if(time)*time=eventTime;}
void RoomThemeLoadEvent(int kind,float time) {RoomThemeStartEvent(kind,time);}
int RoomThemeEventKind(void) {return eventKind;}
float RoomThemeEventRemaining(void) {return eventTime;}
/* Derived from the saved power-event clock: reload cannot restart the benefit. */
int RoomThemeMapRevealed(void) {return eventKind==HATCH_POWER && eventTime>0;}
float RoomThemeFreeMapRemaining(void) {return eventKind==HATCH_POWER ? fmaxf(0,eventTime-20.0f) : 0;}
float RoomThemeControlPulse(void) {return fminf(1,fmaxf(0,roomTheme.timer/.8f));}
int RoomThemeVisualStage(void) {
    if(roomTheme.spent || (hatchCount && hatches[0].state==HATCH_FIRED))
        return eventKind==roomTheme.theme && eventTime>0?1:2;
    const RoomArchetype* def=RoomArchetypeGet(roomTheme.theme);
    int prepared=def && roomTheme.prepared &&
                 (roomTheme.prepared&def->preparationMask)==def->preparationMask;
    return (hatchCount && hatches[0].state==HATCH_OPENING) || prepared?1:0;
}
void RoomThemeRedirectNoise(float* x,float* y) {
    if(x && y && eventKind==0 && eventTime>0 && Dist2(*x,*y,posX,posY)<.16f) {
        *x=roomTheme.landmarkX;*y=roomTheme.landmarkY;
    }
}
static void RoomThemeAmbience(float dt) {
    if(roomTheme.layoutVersion<ROOM_LAYOUT_COMPACT)return;
    float d=Dist2(posX,posY,roomTheme.landmarkX,roomTheme.landmarkY);
    /* The lure keeps working after the player has escaped its audible radius. */
    if(eventKind==HATCH_BOILER && eventTime>0 && d>196) {
        roomTheme.ambientClock-=dt;
        if(roomTheme.ambientClock<=0){MakeNoise(roomTheme.landmarkX,roomTheme.landmarkY,28);roomTheme.ambientClock=2.2f;}
        return;
    }
    if(d>196)return;
    roomTheme.ambientClock-=dt;
    if(roomTheme.ambientClock>0)return;
    const RoomArchetype* def=RoomArchetypeGet(roomTheme.theme);
    if(!def)return;
    if(eventKind==HATCH_FLESH && eventTime>5) {roomTheme.ambientClock=.5f;return;}
    int sound=def->ambientSoundId;
    if(roomTheme.theme==0 && eventTime<=0)sound=SFX_STEP;
    float volume=.17f,period=def->ambientPeriod;
    if(eventKind==HATCH_FLESH && eventTime>0){volume=.28f;period=.65f;}
    if(eventKind==HATCH_BOILER && eventTime>0) {
        sound=SFX_STEP;period=2.2f;volume=.25f;
        MakeNoise(roomTheme.landmarkX,roomTheme.landmarkY,28);
    }
    AudioPlayWorld(sound,roomTheme.landmarkX,roomTheme.landmarkY,volume,14,1);
    roomTheme.ambientClock=period;
}
void RoomThemeTick(float dt) {
    if(!(dt>0) || !isfinite(dt))return;
    roomTheme.timer=fmaxf(0,roomTheme.timer-dt);noteTime=fmaxf(0,noteTime-dt);
    if(eventTime>0) {
        eventTime=fmaxf(0,eventTime-dt);
        if(eventTime==0) {
            int expired=eventKind;eventKind=-1;
            if(expired==HATCH_FLESH) {
                MakeNoise(roomTheme.landmarkX,roomTheme.landmarkY,32);
                AudioPlayWorld(SFX_SCREAM,roomTheme.landmarkX,roomTheme.landmarkY,.55f,24,4);
            }
        }
    }
    if(eventKind==HATCH_FLESH && eventTime>0) {
        /* A loaded/newly spawned hunter must obey the same remaining stop window. */
        Monster* actors[MON_HUNTER_MAX+1];int count=MonsterCollect(actors,MON_HUNTER_MAX+1,1);
        for(int i=0;i<count;i++)actors[i]->stun=fmaxf(actors[i]->stun,eventTime);
    }
    if(!roomTheme.active || trainingMode)return;
    roomTheme.elapsed+=dt;
    int x=(int)posX,y=(int)posY;
    int inside=x>0 && y>0 && x<MAP_WIDTH-1 && y<MAP_HEIGHT-1 && cellMeta[y][x].roomId;
    if(inside && !roomTheme.inside)roomTheme.visits++;
    roomTheme.inside=inside;
    roomTheme.spent=hatchCount>0 && hatches[0].state==HATCH_FIRED;
    RoomThemeAmbience(dt);
}
void RoomThemePlaceVault(void){
    if(trainingMode)return;
    if(roomTheme.active && roomTheme.theme==HATCH_STORAGE && RoomThemeIsCurrent(0)){RoomThemePopulateVault();return;}
    if(roomTheme.layoutVersion>=ROOM_LAYOUT_COMPACT)for(int i=0;i<containerCount;i++) {
        Container* c=&containers[i];int x=(int)c->x,y=(int)c->y;
        if(c->active && !c->searched && x>0 && y>0 && x<MAP_WIDTH-1 && y<MAP_HEIGHT-1 && cellMeta[y][x].roomId) {
            c->locked=1;c->lockType=(roomTheme.theme+i)%LOCK_TYPE_COUNT;
        }
    }
    for(int y=2;y<MAP_HEIGHT-2;y++)for(int x=2;x<MAP_WIDTH-2;x++){
        if(!cellMeta[y][x].roomId || worldMap[y][x] || PropBlocksAt(x,y) || RoomPropAt(x+.5f,y+.5f)>=0)continue;
        if(HatchNearest(x+.5f,y+.5f,1.5f)>=0)continue;
        int clash=0;for(int i=0;i<containerCount;i++)if(Dist2(x+.5f,y+.5f,containers[i].x,containers[i].y)<2)clash=1;
        if(clash)continue;
        int index=containerCount<MAX_CONTAINERS-(levelEvent==EV_SUPPLY)?containerCount:-1;
        if(index<0)for(int i=containerCount-1;i>=0;i--)if(containers[i].content!=1 && !containers[i].searched){index=i;break;}
        if(index<0)return;
        Container* c=&containers[index];int fuse=index<containerCount && c->content==1;memset(c,0,sizeof(*c));c->x=x+.5f;c->y=y+.5f;c->active=1;c->locked=1;
        c->lockType=(cellMeta[y][x].themeId+currentLevel)%LOCK_TYPE_COUNT;c->lockCode=(int)(rng_world_next()&0xFFF);c->content=fuse?1:10;
        if(index==containerCount)containerCount++;
        return;
    }
}

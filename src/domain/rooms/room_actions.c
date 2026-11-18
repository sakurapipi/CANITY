#include "tactical_tools.h"
#include "skills.h"
#include "save.h"
/* Room controls prepare a choice; only the special hatch charges and commits it. */
#include "common.h"
#include "room_theme.h"
#include "room_archetype.h"
#include "map_rooms.h"
#include "hatch.h"
#include "entity.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "wallet.h"
#include "audio.h"
#include "lang.h"
int RoomThemeIsCurrent(int idx) {
    if(idx<0 || idx>=hatchCount)return 0;
    for(int i=0;i<roomPropCount;i++)if(roomProps[i].active &&
       roomProps[i].kind==ROOMPROP_LANDMARK_FIRST+hatches[idx].kind)return 1;
    return 0;
}
int RoomThemeGoldCost(int kind,int gold) {
    int percent=kind==HATCH_STORAGE?25:kind==HATCH_HELL?35:0;
    if(!percent)return 0;
    if(gold<0)gold=0;
    /* Divide before multiplying to avoid overflow at the wallet ceiling. */
    int cost=(gold/100)*percent+((gold%100)*percent+99)/100;
    int minimum=kind==HATCH_STORAGE?40:80;
    return cost<minimum?minimum:cost;
}
static int SwapCandidate(int index) {
    if(index<0 || index>=MON_HUNTER_MAX)return 0;
    Monster* actor=&monHunters[index];
    if(!actor->active || !isfinite(actor->x) || !isfinite(actor->y))return 0;
    float d=Dist2(posX,posY,actor->x,actor->y);
    if(d<36 || d>784 || Blocked(actor->x,actor->y) || Blocked(posX,posY))return 0;
    for(int j=0;j<MON_HUNTER_MAX;j++)if(j!=index && monHunters[j].active &&
       Dist2(actor->x,actor->y,monHunters[j].x,monHunters[j].y)<.64f)return 0;
    return 1;
}
int RoomThemeSwapTarget(void) {
    if(RoomThemeIsCurrent(0) && SwapCandidate(roomTheme.target))return roomTheme.target;
    int best=-1;float distance=35;
    for(int i=0;i<MON_HUNTER_MAX;i++)if(SwapCandidate(i)) {
        float d=Dist2(posX,posY,monHunters[i].x,monHunters[i].y);
        if(d>distance){best=i;distance=d;}
    }
    return best;
}
static void SetControl(int theme,int side,int value) {
    for(int i=0;i<roomPropCount;i++)if(roomProps[i].active &&
       roomProps[i].kind==ROOMPROP_CONTROL_FIRST+theme*2+side)roomProps[i].searched=value;
}
int RoomThemeInteractFixture(int index) {
    if(index<0 || index>=roomPropCount || !roomProps[index].active ||
       !RoomPropIsControl(roomProps[index].kind))return 0;
    RoomProp* control=&roomProps[index];
    int theme=(control->kind-ROOMPROP_CONTROL_FIRST)/2;
    int side=(control->kind-ROOMPROP_CONTROL_FIRST)%2;
    if(!roomTheme.active || theme!=roomTheme.theme || !RoomThemeIsCurrent(0))return 1;
    if((roomTheme.spent || hatches[0].state!=HATCH_SEALED)) {
        RoomThemeNotify(Tr("CIRCUIT ALREADY COMMITTED", "回路已启动，不能再修改"));return 1;
    }
    if(theme==HATCH_WARD) {
        SetControl(theme,0,0);SetControl(theme,1,0);control->searched=1;
    } else if(theme==HATCH_STORAGE || theme==HATCH_HELL) {
        SetControl(theme,0,side==0);SetControl(theme,1,0);
    } else control->searched=1;
    roomTheme.awaitingRelease=1;
    RoomThemeRebuildControls();roomTheme.timer=.8f;
    AudioPlayWorld(theme==HATCH_BOILER?SFX_ECHO:SFX_CRANK,control->x,control->y,.32f,9,2);
    RoomThemeNotify(RoomThemeFixtureLine(index));uiDirty=1;
    return 1;
}
static int Missing(char* reason,int cap,const char* text) {
    if(reason && cap>0)snprintf(reason,(size_t)cap,"%s",text);
    return 0;
}
int RoomThemeCanActivate(int idx,char* reason,int cap) {
    if(reason && cap>0)reason[0]=0;
    if(idx<0 || idx>=hatchCount || !hatches[idx].active)return 0;
    Hatch* hatch=&hatches[idx];
    if((idx==0 && roomTheme.spent) || hatch->state==HATCH_FIRED || hatch->state==HATCH_OPEN)
        return Missing(reason,cap,Tr("ALREADY USED", "本层已使用"));
    if(hiddenIn>=0)return Missing(reason,cap,Tr("LEAVE THE HIDE SPOT FIRST", "请先离开隐藏点"));
    int current=RoomThemeIsCurrent(idx),kind=hatch->kind;
    if(current) {
        RoomThemeRebuildControls();
        const RoomArchetype* def=RoomArchetypeGet(kind);
        if(!def)return 0;
        if(!(kind==HATCH_STORAGE && RoomLayoutVersion(roomSeedUsed)==ROOM_LAYOUT_LEGACY) && (roomTheme.prepared&def->preparationMask)!=def->preparationMask) {
            static const char* en[7]={"TUNE BOTH SOUND PIPES FIRST","AUTHORIZE THE PERCENTAGE INVESTMENT FIRST","CHARGE BOTH TOWERS FIRST","SELECT AN EXCHANGE","CALIBRATE BOTH SWAP RELAYS FIRST","CONFIRM THE IRREVERSIBLE COVENANT FIRST","WIND THE HEART AND PREPARE AN OFFERING FIRST"};
            static const char* zh[7]={"先校准两根声管","先在柜台确认比例风险投资","先给左右两座电塔充电","先选择转换方向","先校准左右两台换位装置","先确认本局不可撤销的契约","先为心脏上弦并准备投掷物"};
            return Missing(reason,cap,g_lang==LANG_ZH?zh[kind]:en[kind]);
        }
    }
    char need[144];need[0]=0;
    switch(kind) {
    case HATCH_BOILER:
        if(battery>=20)return 1;
        snprintf(need,sizeof(need),Tr("NEED 20 BATTERY; HAVE %.0f", "需要20电量，当前%.0f"),battery);break;
    case HATCH_STORAGE:case HATCH_HELL:
        if(kind==HATCH_STORAGE) {
            int eligible=0;
            for(int c=0;c<containerCount;c++)if(RoomThemeVaultChest(c) && !containers[c].searched)eligible++;
            if(!eligible)return Missing(reason,cap,Tr("NO UNOPENED VAULT CHESTS / NO FEE CHARGED", "没有可投资的未开密库箱，不会收费"));
        }
        if(kind==HATCH_HELL && RoomThemeMadness())return Missing(reason,cap,Tr("COVENANT ALREADY ACTIVE THIS RUN", "本局已签订契约"));
        {
            int cost=RoomThemeGoldCost(kind,carryGold);
            if(WalletCanPayRun(cost))return 1;
            snprintf(need,sizeof(need),Tr("NEED %d RUN GOLD; HAVE %d", "需要%d局内金币，当前%d"),cost,carryGold);break;
        }
    case HATCH_POWER:
        if(battery>=25)return 1;
        snprintf(need,sizeof(need),Tr("NEED 25 BATTERY; HAVE %.0f", "需要25电量，当前%.0f"),battery);break;
    case HATCH_WARD:
        if(current && !roomTheme.choice)return Missing(reason,cap,Tr("SELECT LEFT: BATTERY TO SANITY / RIGHT: SANITY TO BATTERY", "左侧选电量换理智，右侧选理智换电量"));
        if((!current || roomTheme.choice==1) && battery>=20 && sanity<SanityMax()-1)return 1;
        if((!current || roomTheme.choice==2) && sanity>=25 && battery<BatteryMax()-1)return 1;
        return Missing(reason,cap,Tr("SELECTED EXCHANGE NEEDS RESOURCES AND RECOVERY SPACE", "所选转换需要足够资源和恢复空间"));
    case HATCH_BRICK:
        if(stamina>=40 && RoomThemeSwapTarget()>=0)return 1;
        return Missing(reason,cap,Tr("NEED 40 STAMINA AND A SAFE TARGET 6-28 CELLS AWAY", "需要40体力和六至二十八格内的安全换位目标"));
    case HATCH_FLESH:
        if(flares+bottles+smokeBombs+gluePots+echoBolts+decoys>0)return 1;
        return Missing(reason,cap,Tr("NEED ONE THROWABLE; PAID ONLY ON ACTIVATION", "需要一个投掷物，启动时才会消耗"));
    default:return 0;
    }
    return Missing(reason,cap,need);
}
int RoomThemeActivate(int idx) {
    if(!RoomThemeCanActivate(idx,NULL,0) || hatches[idx].state!=HATCH_OPENING)return 0;
    Hatch* hatch=&hatches[idx];int current=RoomThemeIsCurrent(idx);
    switch(hatch->kind) {
    case HATCH_BOILER:
        battery-=20;RoomThemeStartEvent(0,45);MakeNoise(roomTheme.landmarkX,roomTheme.landmarkY,20);break;
    case HATCH_STORAGE: {
        int stake=RoomThemeGoldCost(hatch->kind,carryGold);
        if(!WalletCanPayRun(stake))return 0;
        if(!RoomThemeVaultPrepare(stake,RunRngNext()))return 0;
        if(!WalletSpendRun(stake))return 0;
        RoomThemeStartEvent(1,75);MakeNoise(hatch->x,hatch->y,18);break;
    }
    case HATCH_POWER:
        battery-=25;hatchLightsT=35;RoomThemeStartEvent(2,35);
        if(SkillOwned(SK_SHORTCUTJR) && SaveEvolutionCanPersist())ToolReceive(TOOL_PRISM);
        MakeNoise(hatch->x,hatch->y,32);break;
    case HATCH_WARD: {
        int direction=current?roomTheme.choice:
            ((sanity/SanityMax()<battery/fmaxf(1,BatteryMax()) && battery>=20)?1:2);
        if(!current && direction==2 && (sanity<25 || battery>=BatteryMax()-1))direction=1;
        if(direction==1){battery-=20;sanity=fminf(SanityMax(),sanity+35);}
        else {sanity-=20;battery=fminf((float)BatteryMax(),battery+50);}
        RoomThemeStartEvent(3,4);break;
    }
    case HATCH_BRICK: {
        int best=RoomThemeSwapTarget();if(best<0)return 0;
        Monster* actor=&monHunters[best];float oldX=posX,oldY=posY;
        posX=actor->x;posY=actor->y;actor->x=oldX;actor->y=oldY;
        actor->stun=fmaxf(actor->stun,5.0f);actor->pathLen=actor->pathIdx=0;actor->repath=0;
        stamina-=40;RoomThemeStartEvent(4,5);MakeNoise(oldX,oldY,22);break;
    }
    case HATCH_HELL:
        if(!WalletSpendRun(RoomThemeGoldCost(hatch->kind,carryGold)))return 0;
        RoomThemeSetMadness(1);MakeNoise(hatch->x,hatch->y,24);break;
    case HATCH_FLESH:
        if(flares>0)flares--;else if(bottles>0)bottles--;else if(smokeBombs>0)smokeBombs--;
        else if(gluePots>0)gluePots--;else if(echoBolts>0)echoBolts--;else decoys--;
        MonsterStunAll(20);RoomThemeStartEvent(6,20);break;
    default:return 0;
    }
    /* Commit the persisted terminal state before returning to any caller. */
    HatchSetState(idx,HATCH_FIRED);roomTheme.spent=1;roomTheme.timer=.8f;
    AudioPlayWorld(hatch->kind==HATCH_HELL?SFX_IMPACT:SFX_POWER,hatch->x,hatch->y,.55f,18,3);
    RoomThemeNotify(hatch->kind==HATCH_POWER?
        Tr("FLOOR REVEALED FOR 35s / MINIMAP FREE FOR 15s","全层已揭示35秒，小地图免耗电15秒"):
        Tr("ACTIVATED / ONCE PER FLOOR","装置已生效，本层不可重复使用"));
    uiDirty=1;
    return 1;
}

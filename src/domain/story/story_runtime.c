#include "common.h"
#include "protocol.h"
#include "story_layout.h"
#include "state.h"
#include "loop_api.h"
#include "tutorial.h"
#include "monster_rules.h"
#include "entity.h"
#include "world_api.h"
#include "player_api.h"
#include "present_api.h"
#include "cell_meta.h"
#include "map_rooms.h"
#include "room_theme.h"
#include "render.h"
#include "hatch.h"
#include "warden.h"
#include "audio.h"
#include "lang.h"
#include "wallet.h"
#include "save.h"
#include "ui.h"
#include "ui_theme.h"
#include "mimic.h"
#include "spider.h"
#include "neutral.h"

enum { RELAY=1, TERMINAL=4, EVIDENCE=16 };
typedef struct {
    const char *name[2], *event[2];
    int reward, low, high, cap;
    float gold;
} FloorDef;
static float film, messageTime;
static int filmKind;
static const char *message;


static const FloorDef missions[]={
    {{"B-17: The Repeated All-Clear","B-17：重复的解除警报"},{"MASON: I restored the B-17 line. Lumen, take the witness reel out of the archive. I'll keep the lamps on.","程默：B-17线路已恢复。林岑，去档案室取证词原带。我留在这边供电。"},65,0,1,5,1.f},
    {{"B-17: The Uncut Tape","B-17：未剪辑的录音"},{"LUMEN: The B-17 tape is outside. Ward, I heard the manual release instructions. Face the shutter on the second tone.","林岑：B-17录音已传到外面。沈砚，我听到了手动解锁方法。第二声警报响时，面向闸门。"},85,1,2,9,1.1f},
    {{"B-17: The Last Name","B-17：最后一个名字"},{"WARD: Mason gave us light. Lumen told us when to move. Everyone else is across. Mark B-17 complete as soon as I clear the gate.","沈砚：程默接通了灯，林岑报准了时机。其他人都过去了。我一过闸，就把B-17标成完成。"},110,2,3,15,1.2f}
};
int ProtocolPeacefulStory(void) {
    return protocolRun.story>=1 && protocolRun.story<=3 && (protocolRun.flags&PROTOCOL_MISSIONS);
}
void ProtocolClearStoryActors(void) {
    memset(monHunters,0,sizeof monHunters);
    memset(lightBeasts,0,sizeof lightBeasts);
    ResetSecondSpawn();
    for (int i=0;i<containerCount;i++) containers[i].mimicHost=0;
    MimicReset();
    ScavengerReset();
    WardenReset();
    CollectorReset();
    SpiderReset();
    NeutralReset();
}
static int LayoutFloor(void) {
    return protocolRun.story+((protocolRun.flags&PROTOCOL_EVAC_LAYOUT)?3:0);
}
int ProtocolDecoration(int index,StoryDecoration *out) {
    return ProtocolPeacefulStory() && StoryDecorationAt(LayoutFloor(),protocolRun.story,index,out);
}

static struct {
    float time,contact,x,y;
    int captured,warned;
} trial;

void ProtocolResetTrial(void) { memset(&trial,0,sizeof trial); }
static int MissionEvidence(void) { return (protocolRun.flags&RELAY)!=0; }
static const char *MissionEvidenceHint(void) {
    static const char *const text[][2]={
        {"Read the B-17 switch log","读取B-17切换记录"},
        {"Read the B-17 reel label","查看B-17录音盘标签"},
        {"Check the B-17 evacuation roster","检查B-17疏散名册"}
    };
    return LangTr(text[protocolRun.story-1][0],text[protocolRun.story-1][1]);
}
static int MissionSolved(void) { return (protocolRun.flags&PROTOCOL_MISSION_SOLVED)!=0; }
static const char *MissionInstruction(void) {
    const char *const steps[][2]={
        {"%s  Slow the moving relay carriage","%s  用过载防线减速移动滑台"},
        {"%s  Draw the tracking pickup to your double","%s  用记忆替身引开巡检拾音器"},
        {"%s  Brace at the shutter after the alarm","%s  警报响后在闸前举盾"}
    };
    static char line[128];
    int step=protocolRun.story-1;
    snprintf(line,sizeof line,LangTr(steps[step][0],steps[step][1]),VkName(g_bindings[ACT_DECOY]));
    return line;
}
static const FloorDef *Def(void) { return &missions[protocolRun.story-1]; }
const char *ProtocolFloorName(void) { return protocolRun.story ? LangTr(Def()->name[0],Def()->name[1]) : ""; }
static void Notify(const char *s) { message=s;messageTime=5;PlaySfx(SFX_POWER,.55f); }

static void SetBox(int i,Cell c,int fuse) {
    containers[i]=(Container){.x=c.x+.5f,.y=c.y+.5f,.active=1,.content=fuse};
}
void ProtocolBuildFloor(void) {
    StoryGuideInvalidate();
    const FloorDef *f=Def();
    int floor=protocolRun.story;
    CellMetaReset();RoomPropReset();HatchReset();RoomSetCount(0);
    roomSeedUsed=0;
    memset(explored,0,sizeof explored);
    for (int y=0;y<MAP_HEIGHT;y++) for (int x=0;x<MAP_WIDTH;x++) worldMap[y][x]=1;
    protocolRun.flags|=PROTOCOL_EVAC_LAYOUT;
    int layout=LayoutFloor();
    StoryLayOut(layout);
    const Cell *entry=StorySite(layout,SITE_ENTRY);
    posX=entry->x+.5f;posY=entry->y+.5f;
    dirX=1;dirY=0;
    pitchRatio=0;horizon=0;
    ApplyFov();
    const Cell *exit=StorySite(layout,SITE_EXIT);
    exitX=exit->x+.5f;exitY=exit->y+.5f;
    safeRoom.active=0;
    Cell refuge=*StorySite(layout,SITE_REFUGE);
    hideSpots[0]=(HideSpot){.x=refuge.x+.5f,.y=refuge.y+.5f,.active=1,.kind=floor%3};
    const Cell *returnBay=StorySite(layout,SITE_RETURN);
    hideSpots[1]=(HideSpot){.x=returnBay->x+.5f,.y=returnBay->y+.5f,.active=1,.kind=(floor+1)%3};
    hideSpotCount=2;hiddenIn=-1;hideSpotNear=-1;
    memset(containers,0,sizeof containers);
    static const int order[]={SITE_ENTRY,SITE_RETURN,SITE_RECORD,SITE_SERVICE,SITE_POWER,SITE_TERMINAL,SITE_REFUGE,SITE_EXIT};
    containerCount=3;
    for (int i=0;i<containerCount;i++) {
        Cell c=*StorySite(layout,order[i]);
        if (i==0) c.x+=2;
        else {c.x-=1;c.y+=1;}
        SetBox(i,c,i%2?2:0);
    }
    Cell power=*StorySite(layout,SITE_POWER);
    breaker=(Breaker){power.x+.5f,power.y+.5f,0};
    fusesHeld=0;fusesNeeded=0;breakerOpen=0;searchedCount=0;lockedThisFloor=0;
    levelMod=-1;levelEvent=-1;
    modSpeedMul=modSanityMul=modBatteryMul=modNoiseMul=1;
    modShardMul=f->gold;
    memset(thrown,0,sizeof thrown);
    memset(smokes,0,sizeof smokes);
    protocolRun.flags &= PROTOCOL_MISSIONS|PROTOCOL_EVAC_LAYOUT;
    static const char *const brief[][2]={
        {"MASON: Dispatch says B-17 is clear. On the floor plan that lamp is red. I'll pull the switching log.","程默：调度说B-17安全，面板上的指示灯却是红的。我去查切换记录。"},
        {"LUMEN: Mason got B-17 back on the line. The live warning is still on the uncut witness reel.","林岑：程默接回了B-17的线路。真正的警报还在那盘未剪辑的录音里。"},
        {"WARD: Lumen put the real warning on the radio. The crew are crossing now. I'll close the pressure line behind us.","沈砚：林岑把真正的警报发到了电台。其他人正在过闸，我留最后切断压力线路。"}
    };
    const char *const *intro=brief[floor-1];
    message=LangTr(intro[0],intro[1]);messageTime=8;
    holdTimer=0;holdingNow=0;hoverKind=0;hoverIndex=-1;
}

int ProtocolTier(int tier) {
    if (!protocolRun.story) return tier;
    const FloorDef *f=Def();
    return tier<f->low?f->low:tier>f->high?f->high:tier;
}
int ProtocolFloorMaterial(void) {
    return protocolRun.story?StoryMaterial(LayoutFloor()):RoomThemeMaterialForFloor(currentLevel);
}
float ProtocolMonsterThreat(float value) { return protocolRun.story?fminf(value,(float)Def()->cap):value; }
int ProtocolExitReady(void) {
    return !protocolRun.story || (MissionEvidence() && MissionSolved() && (protocolRun.flags&TERMINAL));
}

int ProtocolNearestDevice(void) {
    if (!protocolRun.story || hiddenIn>=0) return -1;
    int nearest=-1;
    float best=1.5f*1.5f;
    for (int id=0;id<PROTOCOL_DEVICE_COUNT;id++) {
        float x,y;
        if (id==PROTOCOL_TRACE || !ProtocolProp(id,&x,&y)) continue;
        float distance=Dist2(posX,posY,x,y);
        if (distance>=best || !HasLOS(posX,posY,x,y)) continue;
        nearest=id;
        best=distance;
    }
    return nearest;
}
const char *ProtocolDeviceHint(int id,int *ready) {
    *ready=id==PROTOCOL_RELAY || (MissionEvidence() && (id!=PROTOCOL_TERMINAL || MissionSolved()));
    if (id==PROTOCOL_RELAY) return LangTr("Read the local record","读取现场记录");
    if (!MissionEvidence()) return MissionEvidenceHint();
    if (id==PROTOCOL_EVIDENCE) {
        if (MissionSolved()) return LangTr("Station aligned","装置已就绪");
        return trial.time>0?MissionInstruction():LangTr("Start the station cycle","启动装置试运行");
    }
    return *ready?LangTr("Read the terminal and unlock evacuation","读取终端并开启撤离"):MissionInstruction();
}
static void FinishMissionTrial(float x,float y) {
    ProtocolResetTrial();
    protocolRun.flags|=PROTOCOL_MISSION_SOLVED;
    breakerOpen=1;
    SpawnParticles(x,y,0);
    Notify(LangTr("Station ready. Follow the terminal marker.","装置就绪。沿标记前往终端。"));
    SaveGame();
}

int ProtocolTrialVisual(float *x,float *y,float *signal) {
    if (!ProtocolPeacefulStory() || trial.time<=0) return 0;
    *x=trial.x;*y=trial.y;
    *signal=trial.contact>0?1.f:.57f+.23f*sinf(trial.time*12);
    return protocolRun.story;
}

static void TickMissionTrial(float dt) {
    if (!ProtocolPeacefulStory() || trial.time<=0 || MissionSolved()) return;
    float x,y;
    if (!ProtocolProp(PROTOCOL_EVIDENCE,&x,&y)) return;
    trial.time=fmaxf(0,trial.time-dt);
    if (protocolRun.story==1) {
        float rate=ProtocolFieldContains(trial.x,trial.y)?.55f:1.f;
        trial.contact=rate<1?trial.contact+dt:0;
        trial.x+=(trial.captured?-1:1)*1.5f*rate*dt;
        if (trial.x>=x+1.35f) {trial.x=x+1.35f;trial.captured=1;}
        if (trial.x<=x-1.35f) {trial.x=x-1.35f;trial.captured=0;}
        if (trial.contact>=.50f) { FinishMissionTrial(x,y);return; }
    } else if (protocolRun.story==2) {
        float mx,my;
        int hears=ProtocolAbilityMarker(&mx,&my)==2 && Dist2(mx,my,x,y)<9 &&
                  Dist2(mx,my,posX,posY)>.81f && HasLOS(trial.x,trial.y,mx,my);
        float goalX=x+(trial.captured?1.15f:-1.15f),goalY=y;
        if (hears) {goalX=mx;goalY=my;}
        float dx=goalX-trial.x,dy=goalY-trial.y;
        float distance=sqrtf(dx*dx+dy*dy);
        float fraction=distance>.01f?fminf(1,dt*1.85f/distance):0;
        float nx=trial.x+dx*fraction,ny=trial.y+dy*fraction;
        if (!Blocked(nx,ny)) {trial.x=nx;trial.y=ny;}
        if (!hears && distance<.15f) trial.captured=!trial.captured;
        trial.contact=hears && distance<.58f?trial.contact+dt:0;
        if (trial.contact>=.28f) { FinishMissionTrial(x,y);return; }
    } else {
        if (!trial.warned && trial.time<=6) {
            trial.warned=1;
            PlaySfx(SFX_POWER,.85f);
        }
        if (trial.time<=6 && trial.time>4 && hiddenIn<0 && Dist2(x,y,posX,posY)<=1.44f &&
            (x-posX)*dirX+(y-posY)*dirY>.28f && HasLOS(posX,posY,x,y)) {
            if (ProtocolAbilityRemaining()>0) {
                float loss=ProtocolContactLoss(6,6);
                if (loss<6) {
                    sanity=fmaxf(1,sanity-loss);
                    FinishMissionTrial(x,y);return;
                }
            }
        }
        if (trial.time<=4) trial.time=0;
    }
    if (trial.time<=0) {
        ProtocolResetTrial();
        Notify(LangTr("Cycle ended. Use the station to try again.","本轮结束。操作装置即可重试。"));
    }
}

static int InteractMission(int id) {
    if (id==PROTOCOL_RELAY) {
        static const char *const lines[][2]={
            {"B-17 SWITCH LOG: The drill recording replaced the live line during the pressure spike. Catch the relay carriage to restore it.", "B-17切换记录：压力飙升时，演习录音顶掉了实时信号。截停滑台，把线路切回去。"},
            {"B-17 REEL: The pressure operator recorded two alarm tones. Draw the pickup away so the receiver can copy both.", "B-17录音盘：操作员录下了两声警报。用替身引开巡检拾音器，让接收器完整转录。"},
            {"B-17 ROSTER: The gate stalls on the second tone. Use your shield at the shutter until the pressure falls.", "B-17疏散名册：第二声警报时闸门会卡住。在闸前举盾，撑到压力回落。"}
        };
        protocolRun.flags|=RELAY;
        const char *const *line=lines[protocolRun.story-1];
        Notify(LangTr(line[0],line[1]));
        SaveGame();
    } else if (id==PROTOCOL_TERMINAL) {
        if (!MissionEvidence() || !MissionSolved()) { Notify(ProtocolObjective());return 1; }
        protocolRun.flags|=TERMINAL;
        const FloorDef *f=Def();
        Notify(LangTr(f->event[0],f->event[1]));
        SaveGame();
    } else if (!MissionSolved()) {
        if (!MissionEvidence() || trial.time>0) { Notify(ProtocolObjective());return 1; }
        if (ProtocolAbilityRemaining()>0) {
            Notify(LangTr("Let your ability finish before starting the next cycle.",
                          "等当前能力结束，再启动下一轮。"));
            return 1;
        }
        protocolRun.cooldown=0;
        if (protocolRun.role==1) battery=fmaxf(battery,12);
        if (protocolRun.role==2) sanity=fmaxf(sanity,7);
        if (protocolRun.role==3) stamina=fmaxf(stamina,16);
        ProtocolResetTrial();
        ProtocolProp(PROTOCOL_EVIDENCE,&trial.x,&trial.y);
        if (protocolRun.story!=3) trial.x-=1.1f;
        trial.time=9;
        Notify(MissionInstruction());
        if (protocolRun.story==3) PlaySfx(SFX_CRANK,.65f);
    }
    return 1;
}
int ProtocolInteract(int id) {
    if (!ProtocolPeacefulStory() || id<0 || id!=ProtocolNearestDevice()) return 0;
    return InteractMission(id);
}
const char *ProtocolExitHint(void) {
    if (!protocolRun.story || Dist2(posX,posY,exitX,exitY)>2.5f*2.5f) return NULL;
    if (ProtocolPeacefulStory() && !ProtocolExitReady()) return ProtocolObjective();
    if (!breakerOpen) return LangTr("Exit sealed · Restore main power","出口封闭 · 恢复主电源");
    if (!ProtocolExitReady()) return ProtocolObjective();
    if (WardenBlocking()) return LangTr("Exit blocked · Wait for the Warden to leave","出口被封锁 · 等守门人离开");
    return LangTr("Exit unlocked","出口已解锁");
}
int ProtocolAdvance(void) {
    if (!ProtocolPeacefulStory() || !ProtocolExitReady()) return 0;
    const FloorDef *f=Def();
    if (protocolRun.story!=3) WalletAddRun(f->reward);
    ProtocolReleaseStoryNodes();
    if (protocolRun.story==3) return 2;
    currentLevel=++protocolRun.story;
    protocolRun.role=currentLevel;
    protocolRun.cooldown=0;
    sanity=SanityMax();battery=BatteryMax();stamina=StaminaMax();
    return 1;
}

int ProtocolStoryPayout(void) {
    if (!ProtocolPeacefulStory() || protocolRun.story!=3 || !ProtocolExitReady() ||
        (protocolRun.flags&PROTOCOL_STORY_PAID)) return 0;
    protocolRun.flags|=PROTOCOL_STORY_PAID;
    int bankBefore=WalletBank();
    int bonus=(protocolProfile.endings&PROTOCOL_STORY_COMPLETE)?0:500;
    int completion=missions[2].reward;
    int allowances=PROTOCOL_STORY_SUPPLIES+missions[0].reward+missions[1].reward;
    int earnings=WalletRun()-allowances;
    if (earnings<0) earnings=0;
    int limit=PROTOCOL_STORY_BANK_CAP-completion;
    if (earnings>limit) earnings=limit;
    WalletAddBank(completion+earnings+bonus);
    protocolProfile.endings|=PROTOCOL_STORY_COMPLETE;
    return WalletBank()-bankBefore;
}

const char *ProtocolObjective(void) {
    if (trainingMode) return guideMode?LangTr("Complete the current lesson","完成当前教学"):LangTr("Practice at your own pace","自由练习");
    if (ProtocolPeacefulStory()) {
        if (!MissionEvidence()) return MissionEvidenceHint();
        if (!MissionSolved()) {
            if (trial.time<=0) return LangTr("Go to the marked station and start its cycle","前往标记装置，启动试运行");
            if (protocolRun.story==3 && trial.time>6)
                return LangTr("Pressure building. Face the shutter and wait for the alarm.","压力正在上升。面向泄压闸，等待警报。");
            return MissionInstruction();
        }
        return ProtocolExitReady()?LangTr("Reach the evacuation gate","前往撤离闸"):LangTr("Read the marked terminal","读取已标记的终端");
    }
    return LangTr("Reach the powered exit","前往已供电的出口");
}

void ProtocolCinematicStart(void){filmKind=0;film=10;}
void ProtocolCinematicTransition(int ending){
    filmKind=ending?2:1;
    film=ending?20:8;
    uiDirty=1;
}
int ProtocolCinematicActive(void){return film>0;}
int ProtocolCinematicSkip(void){if(film<=0)return 0;film=0;return 1;}
void ProtocolCinematicTick(float dt){
    if (film>0) {film=fmaxf(0,film-dt);uiDirty=1;return;}
    messageTime=fmaxf(0,messageTime-dt);
    if (currentState==STATE_PLAYING) TickMissionTrial(dt);
    if (currentState==STATE_PLAYING && ProtocolPeacefulStory() &&
        (protocolRun.flags&PROTOCOL_EVAC_LAYOUT) && !(protocolRun.flags&EVIDENCE) && messageTime<=0) {
        const Cell *record=StorySite(LayoutFloor(),SITE_RECORD);
        if (Dist2(posX,posY,record->x+.5f,record->y+.5f)<4) {
            static const char *const details[][2]={
                {"The bypass key is still turned to DRILL. Someone wired the red pressure lamp directly to B-17.",
                 "旁路钥匙还拧在演习档。有人用一根单独的线，把红色压力灯接到了B-17。"},
                {"Two reel cases bear the same timestamp. The lighter case is labelled ALL-CLEAR and has no pressure-channel track.",
                 "两个录音盘盒的时间相同。写着解除警报的那盘，没有压力频道的音轨。"},
                {"Boot marks lead across the wet floor. At the far gate, someone left a lamp pointed back toward the shutter.",
                 "湿地上有一排向外的靴印。远端闸门旁，有人留了一盏朝向泄压闸的灯。"}
            };
            protocolRun.flags|=EVIDENCE;
            message=LangTr(details[protocolRun.story-1][0],details[protocolRun.story-1][1]);
            messageTime=7;
        }
    }
}
void ProtocolCinematicDraw(void) {
    if(film<=0)return;
    BlendRectWin(0,0,winW,winH,0x071319,1);
    int cx=winW/2,cy=winH*2/5;
    for(int depth=0;depth<8;depth++){
        int w=winW/(2+depth),h=winH/(2+depth);
        FrameRectWin(cx-w/2,cy-h/2,cx+w/2,cy+h/2,0x80b8b0,.7f-depth*.07f,2);
    }
    DrawTextCenterWin(cx,winH/10,"C A N I T Y",0xadd7cb);
    if (filmKind==2) {
        DrawTextCenterWin(cx,winH*7/10,LangTr("B-17 closed. The evacuation log had no blank lines.","B-17已关闭，疏散名册上没有空着的名字。"),0xf0eae1);
    } else if (filmKind==1) {
        char chapter[112];
        snprintf(chapter,sizeof chapter,"F%02d   %s",protocolRun.story,ProtocolFloorName());
        DrawTextCenterWin(cx,winH*7/10,chapter,0xf0eae1);
        DrawTextCenterWin(cx,winH*76/100,LangTr("The next chapter begins here.","已抵达下一章入口。"),0xa6bdbe);
    } else {
        DrawTextCenterWin(cx,winH*7/10,film>6?ProtocolRoleName(protocolRun.role):ProtocolFloorName(),0xf0eae1);
        DrawTextCenterWin(cx,winH*76/100,LangTr("Story prologue · Three chapters","剧情序章 · 共三章"),0xa6bdbe);
    }
#ifdef CANITY_WEB
    const char *skip=LangTr("Press Enter or tap to skip","按回车或轻触跳过");
#else
    const char *skip=LangTr("Press Enter to skip","按回车跳过");
#endif
    DrawTextCenterWin(cx,winH*84/100,skip,0xa6bdbe);
}
int ProtocolProp(int id,float *x,float *y) {
    if (!ProtocolPeacefulStory() || id<0 || id>=PROTOCOL_DEVICE_COUNT) return 0;
    if (id==PROTOCOL_TRACE) return ProtocolAbilityMarker(x,y);
    if (id!=PROTOCOL_TERMINAL && id!=PROTOCOL_EVIDENCE && id!=PROTOCOL_RELAY) return 0;
    int site=id==PROTOCOL_TERMINAL?SITE_TERMINAL:id==PROTOCOL_RELAY?SITE_SERVICE:SITE_POWER;
    const Cell *c=StorySite(LayoutFloor(),site);
    *x=c->x+.5f;*y=c->y+.5f;
    return 1+(id==PROTOCOL_TERMINAL?ProtocolExitReady():id==PROTOCOL_RELAY?MissionEvidence():MissionSolved());
}
int ProtocolTarget(float *x,float *y) {
    if (!ProtocolPeacefulStory()) return 0;
    if (ProtocolExitReady()) { *x=exitX;*y=exitY;return 1; }
    return ProtocolProp(!MissionEvidence()?PROTOCOL_RELAY:MissionSolved()?PROTOCOL_TERMINAL:PROTOCOL_EVIDENCE,x,y);
}
int ProtocolShowGuide(void) {
    return ProtocolPeacefulStory() && protocolProfile.guide!=2;
}
const char *ProtocolRadioLine(void) {
    return protocolRun.story && messageTime>0?message:NULL;
}

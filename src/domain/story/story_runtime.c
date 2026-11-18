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
#include "extras.h"
#include "neutral.h"

enum { RELAY=1, SHELTER=2, TERMINAL=4, CORE=8, EVIDENCE=16, F01_LAYOUT_V2=32, AUTHORED_LAYOUT=64 };
typedef struct {
    const char *name[2],*event[2];
    Cell terminal, breaker, shelter;
    int fuses, reward, unlock, low, high, cap;
    float gold;
} FloorDef;
static const FloorDef floors[] = {
    {{"Black Box Circuit","黑匣回路"},{"Rescue channel open. Subject 00, stage one complete.","救援频道已打开。00号，第一阶段完成。"}, {25,18},{25,8},{14,20},3,300,1,0,0,4,1.f},
    {{"Echo Archive: Records","回声档案·资料库"},{"Broadcast replay: a distress call from before the accident.","正在回放事故前的求救广播。"}, {10,27},{25,27},{17,18},3,0,0,1,1,7,1.15f},
    {{"Echo Archive: Memory","回声档案·记忆库"},{"Access granted. The voices match, but the identity is missing.","授权成功。声纹相同，身份记录缺失。"}, {25,8},{17,8},{10,18},3,500,2,1,1,7,1.15f},
    {{"Cold Containment","冷凝隔离"},{"Containment bypassed. The creatures were part of the experiment.","隔离已旁路。那些生物来自实验。"}, {25,27},{17,27},{10,18},4,700,3,1,2,10,1.2f},
    {{"Debt Vault: Reward","债务金库·奖励"},{"Ledger unlocked. The experiment used gold to shape decisions.","账本解锁。实验用金币诱导选择。"}, {17,8},{25,8},{14,18},4,0,0,2,3,14,1.35f},
    {{"Debt Vault: Penalty","债务金库·惩罚"},{"Casualty ledger recovered. Loss was an intended outcome.","已取回伤亡账册。损失是实验预设的结果。"}, {10,8},{25,27},{17,18},4,0,0,2,3,14,1.35f},
    {{"Debt Vault: Core","债务金库·核心"},{"Power logs recovered. We have been restarting the experiment.","已取回供电日志。我们一直在重启实验。"}, {17,27},{25,8},{14,18},4,900,0,2,3,14,1.35f},
    {{"Train Zero","归零列车"},{"Route committed. Follow the marked way out.","路线已锁定。沿标记前往撤离点。"}, {25,18},{17,8},{14,20},5,1400,0,3,3,18,1.35f}
};
static const Cell relay={11,18}, core={17,27}, alternate={32,27}, selector={22,22};
static const Cell firstRelay={11,15}, firstTerminal={27,21};
static const Cell authoredAlternate={31,28};
static float film, messageTime, stalled;
static int shelterGrantPending;
static char previousObjective[112];
static int filmKind;
static const char *message;


static const FloorDef missions[]={
    {{"B-17: The Repeated All-Clear","B-17：重复的解除警报"},{"MASON: I restored the B-17 line. Lumen, take the witness reel out of the archive. I'll keep the lamps on.","程默：B-17线路已恢复。林岑，去档案室取证词原带。我留在这边供电。"}, {0,0},{0,0},{0,0},0,65,1,0,1,5,1.f},
    {{"B-17: The Uncut Tape","B-17：未剪辑的录音"},{"LUMEN: The B-17 tape is outside. Ward, I heard the manual release instructions. Face the shutter on the second tone.","林岑：B-17录音已传到外面。沈砚，我听到了手动解锁方法。第二声警报响时，面向闸门。"}, {0,0},{0,0},{0,0},0,85,2,1,2,9,1.1f},
    {{"B-17: The Last Name","B-17：最后一个名字"},{"WARD: Mason gave us light. Lumen told us when to move. Everyone else is across. Mark B-17 complete as soon as I clear the gate.","沈砚：程默接通了灯，林岑报准了时机。其他人都过去了。我一过闸，就把B-17标成完成。"}, {0,0},{0,0},{0,0},0,110,3,2,3,15,1.2f}
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
    ExtrasReset();
}
static int LayoutFloor(void) {
    if (!ProtocolPeacefulStory()) return protocolRun.story;
    return protocolRun.story+((protocolRun.flags&PROTOCOL_EVAC_LAYOUT)?11:8);
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
static const FloorDef *Def(void) { return ProtocolPeacefulStory()?&missions[protocolRun.story-1]:&floors[protocolRun.story-1]; }
const char *ProtocolFloorName(void) { return protocolRun.story ? LangTr(Def()->name[0],Def()->name[1]) : ""; }
static void Notify(const char *s) { message=s;messageTime=5;PlaySfx(SFX_POWER,.55f); }

static void SetBox(int i,Cell c,int fuse) {
    containers[i]=(Container){.x=c.x+.5f,.y=c.y+.5f,.active=1,.content=fuse};
}
void ProtocolBuildFloor(void) {
    const FloorDef *f=Def();
    int floor=protocolRun.story;
    CellMetaReset();RoomPropReset();HatchReset();RoomSetCount(0);
    roomSeedUsed=0;
    memset(explored,0,sizeof explored);
    for (int y=0;y<MAP_HEIGHT;y++) for (int x=0;x<MAP_WIDTH;x++) worldMap[y][x]=1;
    if (ProtocolPeacefulStory()) protocolRun.flags|=PROTOCOL_EVAC_LAYOUT;
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
    containerCount=ProtocolPeacefulStory()?3:floor<4?7:8;
    for (int i=0;i<containerCount;i++) {
        Cell c=*StorySite(layout,order[i]);
        if (i==0) c.x+=2;
        else {c.x-=1;c.y+=1;}
        SetBox(i,c,i<f->fuses);
        if(i>=f->fuses && i%2) containers[i].content=2;
    }
    Cell power=*StorySite(layout,SITE_POWER);
    breaker=(Breaker){power.x+.5f,power.y+.5f,!ProtocolPeacefulStory()};
    fusesHeld=0;fusesNeeded=f->fuses;breakerOpen=0;searchedCount=0;lockedThisFloor=0;
    levelMod=-1;levelEvent=-1;
    modSpeedMul=modSanityMul=modBatteryMul=modNoiseMul=1;
    modShardMul=f->gold;
    memset(thrown,0,sizeof thrown);
    memset(smokes,0,sizeof smokes);
    protocolRun.flags=(protocolRun.flags&(PROTOCOL_MISSIONS|PROTOCOL_EVAC_LAYOUT))|AUTHORED_LAYOUT;
    static const char *const entryLines[][2]={
        {"A sign on the empty bed says 00. Someone left the recovery hatch open.",
         "空床上的牌子写着00。有人把苏醒舱的门留开了。"},
        {"The red light over the recording booth is on. The booth is empty.",
         "录音间上方的灯还亮着。里面却没有人。"},
        {"The labels on the file drawers have been replaced. The old names are still underneath.",
         "档案抽屉贴了新标签。旧名字还压在下面。"},
        {"One door has been opened from inside the isolation bay.",
         "有一道隔离门，是从里面打开的。"},
        {"The vault window is intact. The shredded papers are all receipts.",
         "金库的窗口完好无损。被撕碎的纸上全是领款记录。"},
        {"The observation glass faces the cells. From here, you can see both routes.",
         "观察窗正对牢房。从这里能看见两条通路。"},
        {"The standby bus is still lit. The main bus is waiting for your power.",
         "备用母线还亮着。主线路正在等你接通电源。"},
        {"The train and the service capsule run on separate lines. The signal box shows both.",
         "列车和检修撤离舱走不同线路。信号室能看到两边。"}
    };
    static const char *const brief[][2]={
        {"MASON: Dispatch says B-17 is clear. On the floor plan that lamp is red. I'll pull the switching log.","程默：调度说B-17安全，面板上的指示灯却是红的。我去查切换记录。"},
        {"LUMEN: Mason got B-17 back on the line. The live warning is still on the uncut witness reel.","林岑：程默接回了B-17的线路。真正的警报还在那盘未剪辑的录音里。"},
        {"WARD: Lumen put the real warning on the radio. The crew are crossing now. I'll close the pressure line behind us.","沈砚：林岑把真正的警报发到了电台。其他人正在过闸，我留最后切断压力线路。"}
    };
    const char *const *intro=ProtocolPeacefulStory()?brief[floor-1]:entryLines[floor-1];
    message=LangTr(intro[0],intro[1]);messageTime=8;
    stalled=0;previousObjective[0]=0;shelterGrantPending=0;
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
int ProtocolShelterPowered(void) { return (protocolRun.flags&RELAY)!=0; }
int ProtocolBreakerReady(void) {
    return ProtocolPeacefulStory() || protocolRun.story!=1 || (protocolRun.flags&RELAY);
}
int ProtocolExitReady(void) {
    if (!protocolRun.story) return 1;
    if (ProtocolPeacefulStory()) return MissionEvidence() && MissionSolved() && (protocolRun.flags&TERMINAL);
    return (protocolRun.flags&TERMINAL) && (protocolRun.story!=8 ||
           protocolRun.ending==1 || (protocolRun.ending==2 && (protocolRun.flags&CORE)));
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
static int DeviceReady(int id) {
    if (id==PROTOCOL_EVIDENCE) return 1;
    if (id==PROTOCOL_RELAY) return (protocolRun.flags&RELAY) || fusesHeld>0 || breakerOpen;
    if (id==PROTOCOL_CORE) return protocolRun.ending==2;
    if (id==PROTOCOL_ROUTE) return breakerOpen && protocolRun.ending!=1;
    return breakerOpen || protocolRun.story==2 || protocolRun.story==3;
}
const char *ProtocolDeviceHint(int id,int *ready) {
    if (ProtocolPeacefulStory()) {
        *ready=id==PROTOCOL_RELAY || (MissionEvidence() && (id!=PROTOCOL_TERMINAL || MissionSolved()));
        if (id==PROTOCOL_RELAY) return LangTr("Read the local record","读取现场记录");
        if (!MissionEvidence()) return MissionEvidenceHint();
        if (id==PROTOCOL_EVIDENCE) {
            if (MissionSolved()) return LangTr("Station aligned","装置已就绪");
            return trial.time>0?MissionInstruction():LangTr("Start the station cycle","启动装置试运行");
        }
        return *ready?LangTr("Read the terminal and unlock evacuation","读取终端并开启撤离"):MissionInstruction();
    }
    *ready=DeviceReady(id);
    switch (id) {
    case PROTOCOL_EVIDENCE:
        return protocolRun.flags&EVIDENCE?LangTr("Replay the station log","重播站点记录"):
               LangTr("Operate the local station","操作区域设备");
    case PROTOCOL_RELAY:
        if (!*ready) return LangTr("Capacitor · Find a fuse in the supply case","教学蓄电器 · 先从补给箱取一根保险丝");
        return protocolRun.flags&RELAY?LangTr("Review capacitor log","回放蓄电器记录"):LangTr("Charge the capacitor","接通教学蓄电器");
    case PROTOCOL_CORE:
        return *ready?LangTr("Disconnect the core","切断实验核心"):LangTr("Select reset at the route console first","先在路线控制台选择归零");
    case PROTOCOL_ROUTE:
        if (!breakerOpen) return LangTr("Restore main power first","先恢复主电源");
        if (protocolRun.ending==1) return LangTr("Departure route already locked","已锁定列车撤离路线");
        return LangTr("Select protocol reset","选择协议归零");
    default:
        if (!*ready) return LangTr("Terminal offline · Restore main power","终端离线 · 先接通主配电箱");
        if (protocolRun.story==1) return LangTr("Open the rescue channel","接通救援频道");
        if (protocolRun.story==8) return protocolRun.ending==2?LangTr("Review the reset route","查看归零路线"):LangTr("Select train departure","选择列车撤离");
        return LangTr("Read the floor terminal","读取本层终端");
    }
}
static void WakeFirstHunter(void) {
    if (Hunter0()->active) return;
    SpawnMonster();
    const Cell *bay=StorySite(1,SITE_RETURN);
    Hunter0()->x=(protocolRun.flags&AUTHORED_LAYOUT)?bay->x+.5f:24.5f;
    Hunter0()->y=(protocolRun.flags&AUTHORED_LAYOUT)?bay->y+.5f:26.5f;
    Hunter0()->pathLen=Hunter0()->pathIdx=0;
}
void ProtocolEnterShelter(void) {
    if (ProtocolPeacefulStory() || protocolRun.story!=1 || hiddenIn!=0 || !(protocolRun.flags&RELAY) || (protocolRun.flags&SHELTER)) return;
    protocolRun.flags|=SHELTER;
    WakeFirstHunter();
    shelterGrantPending=1;
    Notify(LangTr("POD LOG: 00 woke before the scheduled test. Outside: something is listening.",
        "休眠舱记录：00号在测试开始前醒来。门外，有东西在听。"));
}
static void ReadStation(void) {
    static const char *const record[8][2]={
        {"SERVICE LOG: Use the return gallery when the main corridor is occupied. Both routes reach the rescue radio.",
         "维修记录：主通路被堵时，走回流检修廊。两条路都能到救援电台。"},
        {"BROADCAST LOG: The rescue order was recorded before anyone was rescued. Listen to the booth opposite.",
         "广播记录：救援命令在救援开始前就录好了。声响已转接至对面的录音间。"},
        {"IDENTITY LOG: Eighteen subjects. One matching voiceprint. Check the storage aisle on the far side.",
         "身份记录：十八名受试者，声纹完全一致。对侧库房通路已标出。"},
        {"DECONTAMINATION LOG: The guard opened these doors from the wrong side. Charging the suit from reserve power.",
         "清洗记录：警卫是从被隔离的一侧打开门的。备用电源已为手电充电。"},
        {"VAULT LOG: Bonuses were paid for staying after the first alarm. An unclaimed emergency ration was left here.",
         "金库记录：第一次警报后仍然留下，才会发放奖金。这里留有一份无人领取的应急补给。"},
        {"OBSERVATION LOG: The cells faced a one-way window. We have marked the observation corridor on your map.",
         "观察记录：牢房正对一面单向窗。地图已标出观察通道。"},
        {"GRID LOG: Every restored breaker also powered the protocol. This panel lights the alternate bus route.",
         "电网记录：每次恢复电闸，也在为失序协议供电。备用母线通路已标出。"},
        {"PLATFORM LOG: Train departure leaves the facility running. Reset seals the core, then opens the service capsule.",
         "站台记录：乘列车离开，设施仍会运行。归零会关闭核心，再打开检修撤离舱。"}
    };
    int floor=protocolRun.story;
    if (!(protocolRun.flags&EVIDENCE)) {
        protocolRun.flags|=EVIDENCE;
        const Cell *from=StorySite(floor,SITE_SERVICE);
        if (floor==2) MakeNoise(from->x+.5f,from->y+.5f,19);
        if (floor==4 || floor==5)
            battery=fminf((float)BatteryMax(),battery+(floor==4?20:12));
        for(int y=from->y-3;y<=from->y+3;y++) for(int x=from->x-3;x<=from->x+3;x++)
            if(x>0 && y>0 && x<MAP_WIDTH-1 && y<MAP_HEIGHT-1) explored[y][x]=1;
    }
    Notify(LangTr(record[floor-1][0],record[floor-1][1]));
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
    if (id<0 || id!=ProtocolNearestDevice()) return 0;
    if (ProtocolPeacefulStory()) return InteractMission(id);
    int ready;
    const char *hint=ProtocolDeviceHint(id,&ready);
    if (!ready) {Notify(hint);return 1;}

    switch (id) {
    case PROTOCOL_EVIDENCE:
        ReadStation();
        break;
    case PROTOCOL_RELAY:
        if (!(protocolRun.flags&RELAY)) {
            protocolRun.flags|=RELAY;
            float x,y;
            if (ProtocolProp(PROTOCOL_RELAY,&x,&y)) SpawnParticles(x,y,0);
            WakeFirstHunter();
        }
        Notify(LangTr("RELAY LOG: Local circuit restored. Pod 00 is open. Main grid needs three fuses.",
            "蓄电器记录：局部回路已接通。00号休眠舱开启。主电网需要三根保险丝。"));
        break;
    case PROTOCOL_CORE:
        protocolRun.flags|=CORE;
        Cell destination=(protocolRun.flags&AUTHORED_LAYOUT)?authoredAlternate:alternate;
        exitX=destination.x+.5f;exitY=destination.y+.5f;
        Notify(LangTr("Core disconnected. Maintenance capsule unlocked.","核心已断开。维护撤离舱已解锁。"));
        break;
    case PROTOCOL_ROUTE:
        protocolRun.ending=2;
        protocolRun.flags|=TERMINAL;
        Notify(LangTr("Reset route selected. Disconnect the core.","已选择归零路线。前往核心断路终端。"));
        break;
    default: {
        int first=!(protocolRun.flags&TERMINAL);
        if (protocolRun.story==8 && !protocolRun.ending) protocolRun.ending=1;
        protocolRun.flags|=TERMINAL;
        const FloorDef *f=Def();
        if (protocolRun.story==2 && first) {
            float x,y;
            if (ProtocolProp(PROTOCOL_TERMINAL,&x,&y)) MakeNoise(x,y,18);
        }
        if (protocolRun.story==8 && protocolRun.ending==2 && !(protocolRun.flags&CORE))
            Notify(LangTr("Reset route selected. Disconnect the core.","已选择归零路线。前往核心断路终端。"));
        else if (protocolRun.story==1)
            Notify(LangTr("RADIO: This is not rescue. Subject 00, proceed to the next test. East exit unlocked.",
                         "无线电：这里不是救援频道。00号，请进入下一项测试。东侧出口已解锁。"));
        else Notify(LangTr(f->event[0],f->event[1]));
        break;
    }
    }
    SaveGame();
    return 1;
}
void ProtocolCommitShelter(void) {
    if (!shelterGrantPending) return;
    shelterGrantPending=0;
    SaveAwardTrainingGraduation();
    SaveGame();
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
    if (!ProtocolExitReady()) return 0;
    const FloorDef *f=Def();
    WalletAddRun(f->reward);
    if (f->unlock) protocolProfile.unlocked|=1u<<f->unlock;
    if (ProtocolPeacefulStory()) {
        ProtocolReleaseStoryNodes();
        if (protocolRun.story==3) return 2;
        currentLevel=++protocolRun.story;
        protocolRun.role=currentLevel;
        protocolRun.cooldown=0;
        sanity=SanityMax();battery=BatteryMax();stamina=StaminaMax();
        return 1;
    }
    if (protocolRun.story==8) {
        unsigned bit=1u<<(protocolRun.ending-1);
        if (!(protocolProfile.endings&bit)) {
            WalletAddBank(protocolRun.ending==1?300:500);
            protocolProfile.endings|=bit;
        }
        return 2;
    }
    currentLevel=++protocolRun.story;
    return 1;
}

int ProtocolStoryPayout(void) {
    int bankBefore=WalletBank();
    int bonus=(protocolProfile.endings&4u)?0:500;
    int earnings=WalletRun()-PROTOCOL_STORY_SUPPLIES;
    if (earnings<0) earnings=0;
    if (earnings>PROTOCOL_STORY_BANK_CAP) earnings=PROTOCOL_STORY_BANK_CAP;
    WalletAddBank(earnings+bonus);
    protocolProfile.endings|=4u;
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
    if (protocolRun.story==1 && !(protocolRun.flags&RELAY))
        return fusesHeld?LangTr("Use the maintenance relay","启动教学继电器"):LangTr("Search the first supply case","搜查第一个补给箱");
    if (!(protocolRun.flags&TERMINAL) && (protocolRun.story==2 || protocolRun.story==3))
        return protocolRun.story==2?LangTr("Replay the archive broadcast","回放档案广播"):LangTr("Authorize the memory terminal","授权记忆终端");
    if (!breakerOpen) {
        if (fusesHeld>=fusesNeeded) return LangTr("Restore the main breaker","恢复主配电箱供电");
        static char line[80];
        snprintf(line,sizeof line,LangTr("Find fuses  %d/%d","搜集保险丝  %d/%d"),fusesHeld,fusesNeeded);
        return line;
    }
    if (protocolRun.story && !(protocolRun.flags&TERMINAL))
        return protocolRun.story==8?LangTr("Choose the train terminal or reset console","选择列车终端或归零控制台"):protocolRun.story==1?LangTr("Open the rescue channel at the terminal","前往终端接通救援频道"):LangTr("Activate the floor terminal","启动本层终端");
    if (protocolRun.story==8 && protocolRun.ending==2 && !(protocolRun.flags&CORE))
        return LangTr("Disconnect the core","断开实验核心电路");
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
    if (trainingMode || currentState!=STATE_PLAYING || MonsterChaseActive()) return;
    const char *line=ProtocolObjective();
    if (strcmp(line,previousObjective)!=0) {
        snprintf(previousObjective,sizeof previousObjective,"%s",line);
        stalled=0;
    } else stalled+=dt;
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
        DrawTextCenterWin(cx,winH*7/10,ProtocolPeacefulStory()?LangTr("B-17 closed. The evacuation log had no blank lines.","B-17已关闭，疏散名册上没有空着的名字。"):protocolRun.ending==1?
            LangTr("Departure: the train leaves the signal behind.","离开设施：列车驶离信号范围。"):
            LangTr("Protocol Reset: the facility goes dark.","协议归零：设施断电，陷入沉寂。"),0xf0eae1);
    } else if (filmKind==1) {
        char chapter[112];
        snprintf(chapter,sizeof chapter,"F%02d   %s",protocolRun.story,ProtocolFloorName());
        DrawTextCenterWin(cx,winH*7/10,chapter,0xf0eae1);
        DrawTextCenterWin(cx,winH*76/100,LangTr("The next chapter begins here.","已抵达下一章入口。"),0xa6bdbe);
    } else if (ProtocolPeacefulStory()) {
        DrawTextCenterWin(cx,winH*7/10,film>6?ProtocolRoleName(protocolRun.role):ProtocolFloorName(),0xf0eae1);
        DrawTextCenterWin(cx,winH*76/100,LangTr("Story prologue · Three chapters","剧情序章 · 共三章"),0xa6bdbe);
    } else DrawTextCenterWin(cx,winH*7/10,film>7?ProtocolRoleName(protocolRun.role):
        film>4?LangTr("Can you hear me? You are underground.","听得见吗？你在地下。"):
        LangTr("Find the fuses. Get power to the exit.","寻找保险丝。恢复出口电力。"),0xf0eae1);
#ifdef CANITY_WEB
    const char *skip=LangTr("Press Enter or tap to skip","按回车或轻触跳过");
#else
    const char *skip=LangTr("Press Enter to skip","按回车跳过");
#endif
    DrawTextCenterWin(cx,winH*84/100,skip,0xa6bdbe);
}
int ProtocolProp(int id,float *x,float *y) {
    if (!protocolRun.story || id<0 || id>=PROTOCOL_DEVICE_COUNT) return 0;
    if (id==PROTOCOL_TRACE) return ProtocolAbilityMarker(x,y);
    Cell c;
    int floor=protocolRun.story;
    if (ProtocolPeacefulStory()) {
        if (id!=PROTOCOL_TERMINAL && id!=PROTOCOL_EVIDENCE && id!=PROTOCOL_RELAY) return 0;
        int site=id==PROTOCOL_TERMINAL?SITE_TERMINAL:id==PROTOCOL_RELAY?SITE_SERVICE:SITE_POWER;
        c=*StorySite(LayoutFloor(),site);
        *x=c.x+.5f;*y=c.y+.5f;
        return 1+(id==PROTOCOL_TERMINAL?ProtocolExitReady():id==PROTOCOL_RELAY?!!(protocolRun.flags&RELAY):MissionSolved());
    }
    int authored=!!(protocolRun.flags&AUTHORED_LAYOUT);
    int renovated=floor==1 && (protocolRun.flags&F01_LAYOUT_V2)!=0;
    if (id==PROTOCOL_TERMINAL) c=authored?*StorySite(floor,SITE_TERMINAL):renovated?firstTerminal:Def()->terminal;
    else if (id==PROTOCOL_RELAY && floor==1) c=authored?*StorySite(floor,SITE_SERVICE):renovated?firstRelay:relay;
    else if (id==PROTOCOL_CORE && floor==8) {
        c=authored?*StorySite(floor,SITE_RETURN):core;
        if (authored) c.x+=2;
    }
    else if (id==PROTOCOL_ROUTE && floor==8) c=authored?*StorySite(floor,SITE_RECORD):selector;
    else if (id==PROTOCOL_EVIDENCE && authored) {
        c=*StorySite(floor,floor==8?SITE_SERVICE:SITE_RECORD);
    } else return 0;
    *x=c.x+.5f;*y=c.y+.5f;
    int on=id==PROTOCOL_ROUTE?protocolRun.ending==2:(protocolRun.flags&(id==PROTOCOL_TERMINAL?TERMINAL:id==PROTOCOL_RELAY?RELAY:id==PROTOCOL_EVIDENCE?EVIDENCE:CORE))!=0;
    return 1+on;
}
int ProtocolTarget(float *x,float *y) {
    if (!protocolRun.story || MonsterChaseActive()) return 0;
    if (ProtocolPeacefulStory()) {
        if (ProtocolExitReady()) { *x=exitX;*y=exitY;return 1; }
        return ProtocolProp(!MissionEvidence()?PROTOCOL_RELAY:MissionSolved()?PROTOCOL_TERMINAL:PROTOCOL_EVIDENCE,x,y);
    }
    if (protocolRun.story==1 && !(protocolRun.flags&RELAY)) {
        if (fusesHeld) return ProtocolProp(1,x,y);
        *x=containers[0].x;*y=containers[0].y;return 1;
    }
    if (!(protocolRun.flags&TERMINAL) && (breakerOpen || protocolRun.story==2 || protocolRun.story==3))
        return ProtocolProp(0,x,y);
    if (protocolRun.story==8 && protocolRun.ending==2 && !(protocolRun.flags&CORE))
        return ProtocolProp(2,x,y);
    if (breakerOpen) { *x=exitX;*y=exitY;return 1; }
    if (fusesHeld>=fusesNeeded) { *x=breaker.x;*y=breaker.y;return 1; }
    if (protocolRun.story==1 && stalled>=35) {
        float best=1e6f;
        for(int i=0;i<containerCount;i++) {
            const Container *c=&containers[i];
            if (!c->active || c->searched || c->content!=1) continue;
            float d=Dist2(posX,posY,c->x,c->y);
            if (d<best) {best=d;*x=c->x;*y=c->y;}
        }
        return best<1e6f;
    }
    return 0;
}
int ProtocolShowGuide(void) {
    if (ProtocolPeacefulStory()) return protocolProfile.guide!=2;
    return protocolProfile.guide!=2 && ((protocolProfile.guide==0 && stalled>=90) ||
           (protocolRun.story==1 && !(protocolRun.flags&SHELTER)));
}
const char *ProtocolRadioLine(void) {
    return protocolRun.story && messageTime>0?message:NULL;
}

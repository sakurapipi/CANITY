#include "common.h"
#include "protocol.h"
#include "state.h"
#include "entity.h"
#include "player_api.h"
#include "world_api.h"
#include "tutorial.h"
#include "loop_api.h"
#include "present_api.h"
#include "skills.h"
#include "audio.h"
#include "lang.h"
#include "wallet.h"
#include "threat.h"
#include "save.h"
#include "sound_event.h"
#include "sound_propagation.h"
#include "monster_rules.h"
#include "control_effect.h"
#include "tactical_tools.h"
#include "warden.h"
#include "upgrade_run.h"

ProtocolRun protocolRun;
ProtocolProfile protocolProfile;
static float active, markerX, markerY, motionX, motionY, soundTimer;
static const void *affected[MON_HUNTER_MAX + LIGHTBEAST_MAX];
static float slowUntil[MON_HUNTER_MAX + LIGHTBEAST_MAX];
static float elapsed, traceDistance;
static int guarded, abilityHeld, once, trackedChase, guardFollowup, traceTurned, jammed;
static float activeAge, guardSprint, wrenchWindow, chaseQuiet, grace, traceTail;
static float guardFollowupTime, shieldImpact;
static float lostSight[MON_HUNTER_MAX];
static unsigned redirected;

static const char *const names[][2] = {
    {"00  The Survivor", "00  无名幸存者"}, {"01  Mason", "01  维修员 程默"},
    {"02  Lumen", "02  档案员 林岑"}, {"03  Ward", "03  隔离警卫 沈砚"}
};
static const char *const abilities[][2] = {
    {"Survival Instinct", "求生本能"}, {"Overload Barrier", "过载防线"},
    {"Memory Double", "记忆替身"}, {"Tactical Brace", "战术防御"}
};
static const int bases[][3] = {{100,150,100},{100,165,95},{110,150,95},{95,140,110}};

const char *ProtocolRoleName(int role) { return LangTr(names[role][0],names[role][1]); }
const char *ProtocolAbilityName(int role) { return LangTr(abilities[role][0],abilities[role][1]); }
int ProtocolRoleBase(int role,int resource) { return bases[role][resource]; }
int ProtocolBase(int resource) { return ProtocolRoleBase(protocolRun.role,resource); }
const char *ProtocolRoleBio(int role) {
    static const char *const bio[][2]={
        {"Subject 00 awoke ahead of schedule. The facility has a number for them, but no identity on file.",
         "00号提前苏醒。设施里有编号记录，却查不到这个人的身份。"},
        {"Mason kept the emergency grid alive. He traced the false rescue broadcast to the dispatch coil.",
         "程默负责维持应急电网。他沿着线路查到了假救援广播的调度线圈。"},
        {"Lumen found the same voice in recordings filed under different names.",
         "林岑在不同身份的档案里，听到了同一个人的声音。"},
        {"Ward held the spillway open while the others escaped.",
         "沈砚守着泄压通道，让其他人先撤离。"}
    };
    return LangTr(bio[role][0],bio[role][1]);
}
const char *ProtocolRoleAbility(int role) {
    static const char *const detail[][2]={
        {"Costs 4 sanity. Restores 20 stamina, then grants +15% sprint speed and 20% lower sprint drain for 5s. Cooldown 45s.",
         "消耗4点理智，恢复20点体力。随后5秒内冲刺速度增加15%，冲刺体力消耗降低20%。冷却45秒。"},
        {"Cranking yields 10% more charge. Spend 12 battery for a 5s field. The first entry slows each hunter to 60% speed for 2s. Cooldown 36s.",
         "手摇充电量增加10%。消耗12点电量，在前方展开5秒阻滞区。听声鬼和追光鬼首次进入时，移速降至60%，持续2秒。冷却36秒。"},
        {"Spend 6 sanity to send out a moving sound lure for 5s. It travels up to 6 tiles. Cooldown 40s.",
         "消耗6点理智，在前方放出持续5秒的移动声源，最远6格。冷却40秒。"},
        {"Spend 16 stamina. The first eligible contact hit within 3s can lose up to 40% of its damage. Cooldown 38s.",
         "消耗16点体力。3秒内首次符合条件的接触伤害最多减免40%。冷却38秒。"}
    };
    return LangTr(detail[role][0],detail[role][1]);
}
int ProtocolSelectRole(int role) {
    if (role < 0 || role > 3 || (role && !(protocolProfile.unlocked & (1u<<role)))) return 0;
    protocolProfile.selected = (unsigned)role;
    return 1;
}
void ProtocolFloorReset(void) {
    ProtocolResetTrial();
    active = soundTimer = elapsed = 0;
    guarded=abilityHeld=once=trackedChase=guardFollowup=traceTurned=jammed=0;
    activeAge=guardSprint=wrenchWindow=chaseQuiet=grace=traceTail=guardFollowupTime=shieldImpact=0;
    protocolRun.used = 0;
    memset(affected,0,sizeof affected);
    memset(slowUntil,0,sizeof slowUntil);
}
void ProtocolBegin(int story, int fixedRole) {
    memset(&protocolRun,0,sizeof protocolRun);
    protocolRun.story = story;
    protocolRun.role = story ? 1 : fixedRole ? 0 : (int)protocolProfile.selected;
    if (story) protocolRun.flags=PROTOCOL_MISSIONS;
    ProtocolFloorReset();
}

static int Own(int id) { return (protocolRun.nodes & (1u<<id)) != 0; }

static void AbilityNotice(const char *text) {
    pickupMsg=text;
    pickupMsgTimer=1.5f;
}
static int MarkerPlacement(float *x,float *y) {
    for (float d=1.5f;d>=.5f;d-=.25f) {
        *x=posX+dirX*d;*y=posY+dirY*d;
        if (!Blocked(*x,*y) && HasLOS(posX,posY,*x,*y)) return 1;
    }
    return 0;
}
static void EmitDouble(float strength) {
    SoundEvent sound={.kind=SK_VOICE,.x=markerX,.y=markerY,.strength=strength,
        .maxRadius=DECOY_SHOUT_R,.ttl=2,.sourceEntityId=-1};
    SoundEmit(&sound);
}
static int DoubleContact(void) {
    for (int i=0;i<MON_HUNTER_MAX;i++)
        if (monHunters[i].active && Dist2(markerX,markerY,monHunters[i].x,monHunters[i].y)<.18f) return 1;
    for (int i=0;i<LIGHTBEAST_MAX;i++)
        if (lightBeasts[i].active && Dist2(markerX,markerY,lightBeasts[i].x,lightBeasts[i].y)<.18f) return 1;
    return 0;
}
static void FadeDouble(void) {
    if (Own(6)) EmitDouble(65);
    traceTail=Own(8)?1.5f:Own(9)?2.f:0;
    if (Own(9)) { motionX=-dirY;motionY=dirX; }
}

void ProtocolActivate(void) {
    static const float baseCooldown[]={45,36,40,38};
    static const float baseCost[]={4,12,6,16};
    static const float cheapCost[]={4,9,4,12};
    int role=protocolRun.role;
    if (protocolRun.cooldown>0) return;
    if (hiddenIn>=0 || ControlActive()) {
        AbilityNotice(LangTr("Leave cover or break free before using the ability.","先离开藏身点或挣脱控制，再使用能力。"));
        return;
    }
    float cost=Own(1)?cheapCost[role]:baseCost[role];
    float *resource=role==0 || role==2?&sanity:role==1?&battery:&stamina;
    if (*resource<cost || ((role==0 || role==2) && *resource<cost+1)) {
        AbilityNotice(role==1?LangTr("Not enough battery","电量不足"):
                      role==3?LangTr("Not enough stamina","体力不足"):
                              LangTr("Not enough sanity","理智不足"));
        return;
    }
    float tx=posX,ty=posY;
    if ((role==1 || role==2) && !MarkerPlacement(&tx,&ty)) {
        AbilityNotice(LangTr("Aim at clear floor ahead","请瞄准前方可用地面"));
        return;
    }
    *resource-=cost;
    float cooldown=baseCooldown[role]-(Own(2)?role==0?7:role==3?6:0:0);
    int tuning=RunUpgLevel(RUN_UPG_ROLE);
    protocolRun.cooldown=cooldown-3.f*tuning;
    if (role==0) stamina=fminf(StaminaMax(),stamina+(Own(0)?30:20));
    markerX=tx;markerY=ty;motionX=dirX;motionY=dirY;
    soundTimer=traceDistance=activeAge=traceTail=grace=chaseQuiet=0;
    once=guarded=trackedChase=guardFollowup=traceTurned=jammed=0;
    memset(affected,0,sizeof affected);
    memset(slowUntil,0,sizeof slowUntil);
    memset(lostSight,0,sizeof lostSight);redirected=0;
    active=(role==0?5+Own(1):role==3?3+Own(0):5+2*Own(2))+.5f*tuning;
    if (role==2) active*=1.5f;
    protocolRun.used|=1u;
    TutorialRecordAction(TUTOR_PULSE);
    PlaySfx(role==3?SFX_IMPACT:role==2?SFX_ECHO:SFX_POWER,.45f);
    if (role==1 || role==2) SpawnParticles(tx,ty,0);
}
void ProtocolTickAbilityInput(void) {
    int down=ActionIsDown(ACT_DECOY), pressed=IsActionPressed(ACT_DECOY);
    if (pressed && !abilityHeld) ProtocolActivate();
    int binding=g_bindings[ACT_DECOY];
    if (pressed && binding>0 && binding<256) keyPressed[binding]=0;
    abilityHeld=down;
}

void ProtocolTick(float dt) {
    protocolRun.cooldown=fmaxf(0,protocolRun.cooldown-dt);
    guardSprint=fmaxf(0,guardSprint-dt);
    wrenchWindow=fmaxf(0,wrenchWindow-dt);
    guardFollowupTime=fmaxf(0,guardFollowupTime-dt);
    shieldImpact=fmaxf(0,shieldImpact-dt);
    elapsed+=dt;
    int role=protocolRun.role;
    if (active<=0) {
        if (role==2 && traceTail>0) {
            float before=traceTail;
            traceTail=fmaxf(0,traceTail-dt);
            if (Own(9)) {
                float nx=markerX+motionX*dt,ny=markerY+motionY*dt;
                if (!Blocked(nx,ny)) {markerX=nx;markerY=ny;}
                if ((int)ceilf(before*2)!=(int)ceilf(traceTail*2)) EmitDouble(55);
            }
        }
        return;
    }
    active=fmaxf(0,active-dt);activeAge+=dt;
    if (role==0 && Own(9)) {
        if (MonsterChaseActive()) {trackedChase=1;chaseQuiet=0;}
        else if (trackedChase && (chaseQuiet+=dt)>=1 && !(protocolRun.used&8u)) {
            sanity=fminf(SanityMax(),sanity+6);protocolRun.used|=8u;
        }
    }
    if (role==1 && active>0 && Own(9) && !jammed && WardenActive()) {
        float dx=WardenX()-markerX,dy=WardenY()-markerY;
        if (fabsf(dx*motionX+dy*motionY)<=.5f && fabsf(dx*motionY-dy*motionX)<=1.75f &&
            TacticalInterruptWarden(markerX,markerY,2.5f)>0) {
            jammed=1;
            PlaySfx(SFX_POWER,.65f);
        }
    }
    if (role!=2) return;
    if (active<=0 || DoubleContact()) {active=0;FadeDouble();return;}
    float step=fmaxf(0,fminf(dt*(Own(0)?1.6f:1.2f),(Own(0)?8:6)-traceDistance));
    float nx=markerX+motionX*step,ny=markerY+motionY*step;
    if (step>0 && Blocked(nx,ny) && Own(7) && !traceTurned) {
        float sideX=-motionY,sideY=motionX;
        int sign=Blocked(markerX+sideX*.5f,markerY+sideY*.5f)?-1:1;
        if (!Blocked(markerX+sideX*sign*.5f,markerY+sideY*sign*.5f)) {
            motionX=sideX*sign;motionY=sideY*sign;traceTurned=1;
            nx=markerX+motionX*step;ny=markerY+motionY*step;
        }
    }
    if (!Blocked(nx,ny)) {markerX=nx;markerY=ny;traceDistance+=step;}
    soundTimer-=dt;
    if (soundTimer<=0) {EmitDouble(Own(4)?75:Own(3)?65:55);soundTimer=Own(3)?.5f:.6f;}
}
float ProtocolAbilityAge(void) { return activeAge; }
float ProtocolShieldImpact(void) { return shieldImpact; }
float ProtocolAbilityRemaining(void) {
    return protocolRun.role==3 && guarded?0:active;
}
float ProtocolSprintBonus(void) { return protocolRun.role==0 && active>0?(Own(4)?.20f:.15f):0; }
float ProtocolSprintCap(void) { return protocolRun.role==0 && Own(7) && activeAge<=2?.68f:.53f; }
float ProtocolPursuitDrain(void) { return protocolRun.role==0 && active>0 && Own(3)?.7f:1; }
float ProtocolSprintDrain(void) {
    if (protocolRun.role==3 && guardSprint>0) return .8f;
    return protocolRun.role==0 && active>0?(Own(5)?.7f:.8f):1;
}
int ProtocolSprintGrace(float dt,int requested) {
    if (protocolRun.role!=0 || !Own(8) || active<=0 || secondWindTimer>0) return 0;
    if (requested && stamina<=0 && !once) {grace=1;once=1;}
    grace=fmaxf(0,grace-dt);
    return grace>0;
}
float ProtocolCrankMul(void) { return protocolRun.role==1?1.1f:1; }
float ProtocolWrenchStunBonus(void) {
    if (protocolRun.role!=3 || wrenchWindow<=0) return 0;
    wrenchWindow=0;
    if (Own(9)) guardSprint=fmaxf(guardSprint,1);
    return .3f;
}
float ProtocolContactLoss(float loss,float raw) {
    if (loss<=0) return loss;
    if (protocolRun.role==0 && active>0 && Own(6) && !once) {
        once=1;stamina=fminf(StaminaMax(),stamina+15);
    }
    if (protocolRun.role!=3) return loss;
    float rate=0,cap=.40f;
    if (active>0 && !guarded) {
        guarded=1;rate=Own(4)?.45f:.40f;cap=rate;
        shieldImpact=.28f;
        PlaySfx(SFX_IMPACT,.75f);
        SpawnParticles(posX+dirX*.4f,posY+dirY*.4f,0);
        if (Own(3)) stamina=fminf(StaminaMax(),stamina+8);
        if (Own(5)) guardSprint=Own(8)?4:2;
        if (Own(6)) wrenchWindow=8;
        guardFollowup=Own(7);guardFollowupTime=1;
    } else if (guardFollowup && guardFollowupTime>0) {
        guardFollowup=0;rate=.2f;cap=.2f;
    }
    return loss-fminf(loss*rate,fminf(raw*cap,fmaxf(0,loss-raw*.25f)));
}

void ProtocolAbilityEffect(char *out,int capacity) {
    switch (protocolRun.role) {
    case 0:
        snprintf(out,capacity,LangTr("Sprint +%d%% · Drain -%d%%","冲刺 +%d%% · 体耗减少%d%%"),
                 Own(4)?20:15,Own(5)?30:20);
        break;
    case 1:
        snprintf(out,capacity,LangTr("Hunter speed -%d%% for %.1fs","怪物移速降低%d%% · %.1f秒"),
                 Own(3)?50:40,Own(4)?2.8:2.0);
        break;
    case 2:
        snprintf(out,capacity,"%s",Own(5)?LangTr("Distracts with sound and light","以声音和光诱导"):LangTr("Moving sound lure","移动声源诱导"));
        break;
    default:
        snprintf(out,capacity,LangTr("Blocks up to %d%% of one hit","一次命中最多减免%d%%"),Own(4)?45:40);
        break;
    }
}

int ProtocolFieldContains(float x,float y) {
    if (protocolRun.role!=1 || active<=0 || !HasLOS(markerX,markerY,x,y)) return 0;
    float dx=x-markerX,dy=y-markerY;
    float forward=dx*motionX+dy*motionY;
    return forward>=-.5f && forward<=(Own(7)?1.1f:.5f) &&
           fabsf(dx*motionY-dy*motionX)<=(Own(0)?1.75f:1.25f);
}
float ProtocolHunterMul(const void *actor, int optical) {
    if (protocolRun.role != 1) return 1;
    (void)optical;
    const Monster *m = actor;
    int count = (int)(sizeof affected/sizeof affected[0]);
    int vacant = -1;
    for (int i=0;i<count;i++) {
        if (affected[i] == actor) return elapsed < slowUntil[i] ? (Own(3)?.5f:.6f) : 1;
        if (!affected[i] && vacant < 0) vacant = i;
    }
    if (vacant<0 || !ProtocolFieldContains(m->x,m->y)) return 1;
    affected[vacant] = actor;
    slowUntil[vacant]=elapsed+(Own(4)?2.8f:2)+(Own(6) && !once?.5f:0);
    if (!once) {
        once=1;
        if (Own(5)) battery=fminf((float)BatteryMax(),battery+3);
        if (Own(8)) active+=2;
    }
    return Own(3)?.5f:.6f;
}
int ProtocolAbilityMarker(float *x,float *y) {
    if (active<=0 || (protocolRun.role!=1 && protocolRun.role!=2)) return 0;
    *x=markerX;*y=markerY;
    return protocolRun.role;
}
int ProtocolHearingTarget(int index,float dt,int chasing,int seesPlayer,float x,float y,float *tx,float *ty) {
    if (protocolRun.role!=2 || active<=0 || (unsigned)index>=MON_HUNTER_MAX) return 0;
    if (!chasing || seesPlayer) {lostSight[index]=0;return 0;}
    lostSight[index]+=dt;
    unsigned bit=1u<<index;
    if (lostSight[index]<.8f || (redirected&bit) || Dist2(x,y,markerX,markerY)>DECOY_SHOUT_R*DECOY_SHOUT_R) return 0;
    SoundPropBuild(x,y);
    if (SoundPropCostAt(markerX,markerY)==PROP_UNREACHABLE) return 0;
    redirected|=bit;
    *tx=markerX;*ty=markerY;
    return 1;
}
int ProtocolOpticalTarget(float *x,float *y) {
    if (protocolRun.role!=2 || !Own(5) || (active<=0 && !(Own(8) && traceTail>0))) return 0;
    *x=markerX; *y=markerY;
    return 1;
}

static const int costs[] = {100,180,180,280,420,420,420,600,600,600};
static const float prices[] = {.4f,.6f,.6f,.8f,1,1,1,1.4f,1.4f,1.4f};
int ProtocolNodeCost(int id){return costs[id];}
float ProtocolNodeThreat(int id){return prices[id];}
int ProtocolNodeOwned(int id){return Own(id);}
static int ValidNext(unsigned mask,int id) {
    if (mask & (1u<<id)) return 0;
    if (id == 0) return 1;
    if (id <= 2) return (mask&1u) && !(mask&6u);
    if (id == 3) return (mask&6u)!=0;
    if (id <= 6) return (mask&8u) && !(mask&112u);
    return (mask & (1u<<(id-3))) != 0;
}
int ProtocolNodeAvailable(int id) {
    return (unsigned)id<PROTOCOL_NODE_COUNT && !ProtocolPeacefulStory() && !dailyMode && ValidNext(protocolRun.nodes,id) && WalletCanPayRun(costs[id]);
}
const char *ProtocolNodeStatus(int id) {
    if (Own(id)) return LangTr("Owned","已拥有");
    if ((id==1 || id==2) && (protocolRun.nodes&6u))
        return LangTr("Another S2 is selected","已选另一项二阶分支");
    if (id>=4 && id<=6 && (protocolRun.nodes&112u))
        return LangTr("Another S4 is selected","已选另一项四阶分支");
    if (!ValidNext(protocolRun.nodes,id)) return LangTr("Prerequisite missing","缺少前置");
    if (!WalletCanPayRun(costs[id])) return LangTr("Not enough run gold","局内金币不足");
    return LangTr("Available","可购买");
}
int ProtocolBuyNode(int id) {
    if (hiddenIn < 0 || !ProtocolNodeAvailable(id) || !WalletSpendRun(costs[id])) return 0;
    protocolRun.nodes |= 1u<<id;
    ThreatAdd(prices[id]);
    if (!trainingMode) SaveGame();
    return 1;
}
static int RefundNodes(void) {
    if (!protocolRun.nodes) return 0;
    int total=0;
    float pressure=0;
    for (int i=0;i<PROTOCOL_NODE_COUNT;i++) if (Own(i)) { total+=costs[i];pressure+=prices[i]; }
    protocolRun.nodes=0;
    ThreatSet(ThreatRaw()-pressure);
    WalletAddRun(total);
    return 1;
}

void ProtocolReleaseStoryNodes(void) { (void)RefundNodes(); }

int ProtocolRefund(void) {
    if (hiddenIn < 0 || active > 0 || (protocolRun.used & 1u) || !RefundNodes()) return 0;
    if (!trainingMode) SaveGame();
    return 1;
}

void ProtocolRestore(void) {
    unsigned restoreUsed=protocolRun.used;
    ProtocolFloorReset();
    protocolRun.used=restoreUsed&15u;
    protocolRun.role = (protocolRun.role >= 0 && protocolRun.role <= 3) ? protocolRun.role : 0;
    if (protocolRun.story<0 || protocolRun.story>8) protocolRun.story=0;
    protocolRun.flags &= PROTOCOL_STORY_FLAG_MASK;
    if (protocolRun.story<1 || protocolRun.story>3) protocolRun.flags &= ~(PROTOCOL_MISSIONS|PROTOCOL_MISSION_SOLVED|PROTOCOL_EVAC_LAYOUT);
    if (protocolRun.flags&PROTOCOL_MISSIONS) protocolRun.role=protocolRun.story;
    else if ((protocolRun.role && !(protocolProfile.unlocked & (1u<<protocolRun.role))) || dailyMode) protocolRun.role=0;
    protocolRun.cooldown=isfinite(protocolRun.cooldown)?fmaxf(0,fminf(45,protocolRun.cooldown)):0;
    unsigned mask=0;
    if (!dailyMode) for (int i=0;i<10;i++) {
        if (Own(i) && ValidNext(mask,i)) mask |= 1u<<i;
    }
    protocolRun.nodes=mask;
    if (protocolRun.ending<0 || protocolRun.ending>2) protocolRun.ending=0;
}

typedef struct {
    const char *name[2];
    const char *detail[2];
} RoleNodeText;

static const RoleNodeText roleNodes[4][PROTOCOL_NODE_COUNT]={
    {
        {{"Second Breath","应激唤醒"},{"Activation restores 30 stamina","发动时回复30体力"}},
        {{"Steady Pulse","稳住脉搏"},{"Instinct lasts 6s","求生本能持续6秒"}},
        {{"Quick Reset","快速复原"},{"Cooldown becomes 38s","冷却缩短至38秒"}},
        {{"Clear Head","临危冷静"},{"30% less extra sanity drain from nearby pursuit","近距离追击附加的理智损耗降低30%"}},
        {{"Breakaway","全力突围"},{"Sprint speed bonus increases to 20%","冲刺移速加成提高至20%"}},
        {{"Measured Stride","控制步幅"},{"Instinct lowers sprint drain by 30%","求生本能生效时，冲刺体耗降低30%"}},
        {{"Reflex","本能反应"},{"First contact during Instinct restores 15 stamina","本能生效期间，第一次接触伤害回复15体力"}},
        {{"Flying Start","爆发起步"},{"First 2s permit up to 0.68 extra tiles/s from Instinct","前2秒冲刺额外速度上限为每秒0.68格"}},
        {{"Last Stride","最后一步"},{"One 1s sprint grace at zero stamina","体力归零时可额外冲刺1秒，每次发动限一次"}},
        {{"Nerves of Steel","钢铁意志"},{"Lose pursuit during Instinct to recover 6 sanity once per floor","本能生效时甩开追击可恢复6理智，每层一次"}}
    },{
        {{"Wide Coil","宽幅线圈"},{"Field width increases to 3.5 tiles","阻滞区加宽至3.5格"}},
        {{"Efficient Coil","节能线圈"},{"Activation costs 9 battery","发动消耗降至9电量"}},
        {{"Charged Capacitor","蓄能延时"},{"Field lasts 7 seconds","阻滞区持续7秒"}},
        {{"High Inductance","高感应阻滞"},{"Hunters slow to 50% speed","进入区域的怪物移速降至50%"}},
        {{"Residual Field","残留电场"},{"Slow lasts 2.8 seconds","减速持续2.8秒"}},
        {{"Charge Recovery","电荷回收"},{"First slow refunds 3 battery","首次阻滞命中返还3电量"}},
        {{"First Contact","初次阻滞"},{"First target is slowed for 0.5s longer","第一个目标的减速多持续0.5秒"}},
        {{"Twin Coils","双段线圈"},{"The forward edge of the field extends by 0.6 tile","阻滞区向前延伸0.6格"}},
        {{"Reserve Power","备用电源"},{"First slow extends the field by 2s","首次阻滞命中后，区域延长2秒"}},
        {{"Signal Break","信号切断"},{"The field can interrupt the Warden once when in range","阻滞区可在范围内打断守门人一次"}}
    },{
        {{"Long Recording","延长录音"},{"The double travels at 1.6 tiles/s up to 8 tiles","替身移速提高至每秒1.6格，最远8格"}},
        {{"Compressed Memory","记忆压缩"},{"Activation costs 4 sanity","发动消耗降至4理智"}},
        {{"Extended Playback","延时回放"},{"The double lasts 10.5s before shop upgrades","强化后替身基础持续时间为10.5秒"}},
        {{"Clear Signal","清晰信号"},{"The double emits stronger, more frequent sounds","替身发声更频繁，传播强度更高"}},
        {{"Amplified Echo","回声放大"},{"Increases signal strength to 75","声源强度提高至75"}},
        {{"False Light","拟态灯光"},{"The double also draws light hunters in line of sight","替身也会诱导视线内的追光鬼"}},
        {{"Final Echo","末尾回声"},{"The double emits a pulse on fading","替身消散时发出一次脉冲"}},
        {{"Forked Memory","分岔记忆"},{"The double can turn once if a wall blocks the route","遇墙且侧路可走时，替身可转向一次"}},
        {{"Afterimage","余光残影"},{"Light lingers 1.5s after the double fades","替身消散后，拟态灯光再保留1.5秒"}},
        {{"Second Voice","第二个声音"},{"A secondary sound trace lasts up to 2s","消散后生成至多持续2秒的次级声源"}}
    },{
        {{"Solid Stance","稳固站姿"},{"Brace window lasts 4s","战术防御窗口延长至4秒"}},
        {{"Low Stance","省力架势"},{"Activation costs 12 stamina","发动消耗降至12体力"}},
        {{"Quick Brace","迅速架防"},{"Cooldown becomes 32s","冷却缩短至32秒"}},
        {{"Hold the Line","守住防线"},{"A successful block restores 8 stamina","成功防御恢复8体力"}},
        {{"Reinforced Brace","坚固防守"},{"Adds 5 points to the base block, up to 45%","首次格挡提高5个百分点，最多减免45%"}},
        {{"Retreat Window","撤退窗口"},{"After a block, sprint drain drops by 20% for 2s","成功防御后2秒内，冲刺体耗降低20%"}},
        {{"Counter Swing","反击架势"},{"After a block, the next wrench hit within 8s stuns for 0.3s longer","防御后8秒内，下次扳手命中的硬直延长0.3秒"}},
        {{"Second Block","连续防御"},{"After the first block, reduce one more hit within 1s by up to 20%","首次防御后1秒内，再减免至多20%的一次命中"}},
        {{"Tactical Withdrawal","战术撤退"},{"Reduced sprint drain lasts 4 seconds","防御后的冲刺减耗延长至4秒"}},
        {{"Break Contact","反击脱离"},{"A covered wrench hit grants 1s of reduced sprint drain","防御后命中扳手可获得1秒冲刺减耗"}}
    }
};
const char *ProtocolNodeShortName(int id) {
    static const char *const labels[4][PROTOCOL_NODE_COUNT][2]={
        {{"BREATH","唤醒"},{"PULSE","稳心"},{"RESET","复原"},{"FOCUS","冷静"},{"BURST","突围"},{"PACE","步幅"},{"REFLEX","反应"},{"START","起步"},{"LAST","末步"},{"NERVE","意志"}},
        {{"COIL","线圈"},{"ECO","节能"},{"CAP","延时"},{"INDUCT","高感"},{"FIELD","残场"},{"RETURN","回收"},{"CONTACT","阻滞"},{"TWIN","双段"},{"POWER","备电"},{"JAM","断信"}},
        {{"RECORD","录音"},{"PACK","压缩"},{"LOOP","回放"},{"SIGNAL","信号"},{"ECHO","回声"},{"LIGHT","拟光"},{"FADE","消散"},{"FORK","分岔"},{"AFTER","余光"},{"VOICE","余声"}},
        {{"STANCE","架势"},{"LOW","省力"},{"QUICK","迅防"},{"HOLD","坚守"},{"BRACE","防守"},{"STEP","撤步"},{"SWING","反击"},{"BLOCK","连防"},{"FALLBACK","撤退"},{"BREAK","脱离"}}
    };
    return LangTr(labels[protocolRun.role][id][0],labels[protocolRun.role][id][1]);
}
const char *ProtocolNodeName(int id) {
    const RoleNodeText *node=&roleNodes[protocolRun.role][id];
    return LangTr(node->name[0],node->name[1]);
}
const char *ProtocolNodeDetail(int id) {
    const RoleNodeText *node=&roleNodes[protocolRun.role][id];
    return LangTr(node->detail[0],node->detail[1]);
}

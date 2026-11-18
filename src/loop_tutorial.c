/* Tutorial progression and arena: private timers, event-driven input. */
#include "protocol.h"
#include "common.h"
#include "training_stations.h"
#include "tutorial.h"
#include "entity.h"
#include "state.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "save.h"
#include "daily.h"
#include "lang.h"
#include "render.h"
#include "lockbox.h"
#include "map_rooms.h"
#include "hatch.h"
#include "calibration.h"
#include "upgrade_run.h"
#include "phase.h"
#include "wallet.h"
#include "loop_private.h"

int tutorialStep = 0;
int tutorialGuideOn = 0;
int tutorialDone = 0;
#define TUTORIAL_STEP_TIMEOUT 12.0f
static float stepElapsed;
int TutorialNeedsHelp(void) { return !tutorialDone && stepElapsed >= TUTORIAL_STEP_TIMEOUT; }

static float tutorialTimer = 0.0f;        /* 完成后的回菜单倒计时 */
static float tutorialActionTimer = 0.0f;  /* 持续动作计时（冲刺/摇电） */
static float lessonX, lessonY, lessonBattery;
static int tutorialLightOff = 0;        /* 手电关过再开 */
static int tutorialThrown = 0;
static unsigned completedActions=0;
static int lessonSlot=0,rewardResult=0;
static int tutorialMapOpened = 0;       /* 地图至少打开过一次 */

int TutorialStepTotal(void) {return TUT_DONE;}

int TutorialStepNumber(void) {return tutorialDone?TUT_DONE:tutorialStep+1;}

void TutorialBegin(void) {
    tutorialStep = TUT_MOVE;
    stepElapsed = 0;
    completedActions=0;lessonSlot=throwSelected;rewardResult=0;
    tutorialDone = 0;
    tutorialTimer = 0.0f;
    tutorialActionTimer = 0.0f;
    lessonX = posX; lessonY = posY; lessonBattery = battery;
    tutorialLightOff = 0;
    tutorialThrown = 0;
    tutorialMapOpened = 0;
    uiDirty = 1;
    tutorialGuideOn = guideMode?1:0;
}


static void GuidedBuildFloor(void) {
    /* A dedicated 36x36 practice floor: central spine, north puzzle bays,
       south cover bay. No campaign enemies, loot transfers, or automatic narration. */
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++) {
        worldMap[y][x]=1;explored[y][x]=1;
        if(x>=2 && x<=33 && y>=17 && y<=19)worldMap[y][x]=0;
        if(x>=5 && x<=31 && y>=12 && y<=16 &&
           (y==16 || (x!=13 && x!=21)))worldMap[y][x]=0;
        if(y==20 && x>=18 && x<=21)worldMap[y][x]=0;
        if(y>=21 && y<=25 && x>=16 && x<=23 &&
           (x!=20 || y==21 || y==25))worldMap[y][x]=0;
    }
    posX=3.5f;posY=18.5f;dirX=1;dirY=0;planeX=0;planeY=.66f;
    roomPropCount=0;hatchCount=0;safeRoom.active=0;
    memset(containers,0,sizeof(containers));containerCount=1+LOCK_TYPE_COUNT;
    containers[0].x=12.5f;containers[0].y=18.5f;containers[0].active=1;containers[0].content=1;
    for(int type=0;type<LOCK_TYPE_COUNT;type++){
        Container* box=&containers[type+1];box->x=7.5f+type*4.0f;box->y=14.5f;
        box->active=1;box->locked=1;box->lockType=type;box->lockCode=0x2A5;box->content=0;
    }
    memset(hideSpots,0,sizeof(hideSpots));hideSpotCount=2;hiddenIn=hideSpotNear=-1;
    for(int i=0;i<2;i++){hideSpots[i].x=18.5f+i*3;hideSpots[i].y=22.5f;hideSpots[i].active=1;hideSpots[i].kind=i;}
    for(int i=0;i<MON_HUNTER_MAX;i++)monHunters[i].active=0;
    for(int i=0;i<LIGHTBEAST_MAX;i++)lightBeasts[i].active=0;
    for(int i=0;i<LEECH_MAX;i++)leeches[i].active=0;
    parasite.active=0;
    breaker.x=29.5f;breaker.y=18.5f;breaker.active=1;breakerOpen=0;fusesHeld=0;fusesNeeded=1;
    exitX=32.5f;exitY=18.5f;carryGold=1200;carryAtRunStart=0;
    bottles=flares=smokeBombs=gluePots=echoBolts=decoys=phaseBeacons=3;
    battery=BatteryMax()*.65f;stamina=StaminaMax();sanity=SanityMax();
    pickupMsgTimer=0;pickupMsg=NULL;tutorialGuideOn=1;
    ApplyFov();
}

static void OpenTrainingRoom(int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) worldMap[y][x] = 0;
}

void TrainingBuildArena(void) {
    if (guideMode) { GuidedBuildFloor(); return; }
    TrainingStationsReset();
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) worldMap[y][x] = explored[y][x] = 1;

    /* Central dispatch, four bays and two bypass corridors. */
    OpenTrainingRoom(14, 14, 21, 21);
    OpenTrainingRoom(3, 17, 32, 19);
    OpenTrainingRoom(17, 3, 19, 32);
    OpenTrainingRoom(4, 4, 15, 13);
    OpenTrainingRoom(21, 4, 31, 13);
    OpenTrainingRoom(4, 22, 15, 31);
    OpenTrainingRoom(21, 22, 31, 31);
    OpenTrainingRoom(7, 12, 9, 23);
    OpenTrainingRoom(26, 12, 28, 23);
    OpenTrainingRoom(14, 8, 22, 10);
    OpenTrainingRoom(14, 26, 22, 28);

    posX = 18.5f; posY = 18.5f;
    dirX = 1.0f; dirY = 0.0f; planeX = 0; planeY = .66f;
    for (int i = 0; i < MON_HUNTER_MAX; i++) monHunters[i].active = 0;
    Hunter0()->x = 28.5f; Hunter0()->y = 8.5f;
    Hunter0()->active = 1; Hunter0()->state = 2; Hunter0()->speed = 0;
    for (int i = 0; i < LIGHTBEAST_MAX; i++) lightBeasts[i].active = 0;
    for (int i = 0; i < LEECH_MAX; i++) leeches[i].active = 0;
    parasite.active = 0;
    roomPropCount = hatchCount = 0;
    safeRoom.active = 0;

    memset(containers, 0, sizeof(containers));
    containerCount = 1 + LOCK_TYPE_COUNT;
    containers[0] = (Container){.x=19.5f, .y=17.5f, .active=1, .content=1};
    for (int i = 0; i < LOCK_TYPE_COUNT; i++)
        containers[i+1] = (Container){.x=6.5f+(i%3)*3, .y=7.5f+(i/3)*3,
            .active=1, .locked=1, .lockType=i, .lockCode=0x2A5};

    memset(hideSpots, 0, sizeof(hideSpots));
    hideSpotCount = 2; hideRepairCount = 0; hiddenIn = hideSpotNear = -1;
    hideSpots[0].x = 7.5f; hideSpots[0].y = 26.5f; hideSpots[0].active = 1;
    hideSpots[1].x = 12.5f; hideSpots[1].y = 27.5f;
    hideSpots[1].kind = 1; hideSpots[1].active = 1;
    breaker.x = 25.5f; breaker.y = 26.5f; breaker.active = 1;
    fusesHeld = breakerOpen = 0; fusesNeeded = 1;
    exitX = 30.5f; exitY = 29.5f;
    carryGold = 1200; carryAtRunStart = 0;
    battery = BatteryMax() * .55f;
    ApplyFov();
}

const char* TutorialObjective(void) {
    static const char* en[TUT_DONE+1]={
        "01 MOVE","02 LOOK AROUND","03 SPRINT","04 FLASHLIGHT","05 MAP","06 SEARCH",
        "07 CRANK BATTERY","08 SELECT AN ITEM","09 THROW","10 PHASE RETURN","11 ARM A DECOY",
        "12 WRENCH","13 HIDE AND LEAVE","14 CHARACTER ABILITY","15 UNLOCK A CRATE","16 BUY A SUPPLY",
        "17 BUY A RUN UPGRADE","18 RHYTHM CONSOLE","19 POWER THE EXIT","20 EXTRACT","TRAINING COMPLETE"};
    static const char* zh[TUT_DONE+1]={
        "01 移动","02 转动视角","03 冲刺","04 手电开关","05 打开与关闭地图","06 搜查容器",
        "07 手摇充电","08 切换道具","09 蓄力投掷","10 相位返回","11 激活诱饵",
        "12 扳手挥击","13 躲藏与离开","14 角色能力","15 打开锁箱","16 购买补给",
        "17 购买局内强化","18 节律校准","19 接通配电箱","20 抵达出口","课程完成"};
    int step=tutorialStep>=0 && tutorialStep<=TUT_DONE?tutorialStep:TUT_MOVE;
    return LangTr(en[step],zh[step]);
}

const char* TutorialHint(void) {
    static char hint[240];
    switch(tutorialStep) {
    case TUT_MOVE: return LangTr("Walk to the diamond checkpoint. The distance tells you how far remains.","走到菱形路标处，数字表示还剩多远。");
    case TUT_LOOK:return LangTr("Turn until the marker is centered in your crosshair.","转动视角，将菱形路标对准屏幕中央。");
    case TUT_SPRINT:snprintf(hint,sizeof(hint),LangTr("Hold %s and sprint to the next marker.","按住 %s 冲刺到下一处路标，观察体力消耗。"),VkName(g_bindings[ACT_SPRINT]));return hint;
    case TUT_LIGHT:snprintf(hint,sizeof(hint),LangTr("Use %s to turn the flashlight off, then on.","按 %s 关闭手电，再打开。关灯能避开追光鬼。"),VkName(g_bindings[ACT_FLASHLIGHT]));return hint;
    case TUT_MAP:snprintf(hint,sizeof(hint),LangTr("Open the map with %s, then close it to continue.","使用 %s 打开地图，再关闭。锁箱在北侧，藏身点在南侧。"),VkName(g_bindings[ACT_MAP]));return hint;
    case TUT_SEARCH:snprintf(hint,sizeof(hint),LangTr("Hold %s at the marked supply box to collect its fuse.","到标记的补给箱前，按住 %s 取走保险丝。"),VkName(g_bindings[ACT_INTERACT]));return hint;
    case TUT_CRANK:snprintf(hint,sizeof(hint),LangTr("Hold %s until the battery bar rises. Cranking makes noise.","按住 %s 直到电量上升。手摇会制造声音。"),VkName(g_bindings[ACT_CRANK]));return hint;
    case TUT_SELECT:return LangTr("Select a DIFFERENT carried item using 1/2/3 or the wheel.","按1/2/3或滚动滚轮，切换为另一种持有的道具。");
    case TUT_THROW:snprintf(hint,sizeof(hint),LangTr("Select a stone. Hold %s, aim down the corridor, then release.","选择投石，按住 %s 蓄力，朝走廊瞄准后松开。左键可以取消投掷。"),VkName(g_bindings[ACT_THROW]));return hint;
    case TUT_BEACON:return LangTr("Select a PHASE BEACON, throw it to place an anchor, walk away, then Q to return.","滚轮选择相位信标，投出并等它落地。离开锚点后按Q返回。");
    case TUT_DECOY:return LangTr("Select and throw a DECOY, then press C to arm it.","滚轮选择诱饵玩偶并投出，等落地后按C激活；用来引开听声鬼。");
    case TUT_KNOCK:return LangTr("Get close to the marked dummy and hit it with a wrench swing.","靠近标记的训练靶，蓄力挥击并命中。");
    case TUT_HIDE:snprintf(hint,sizeof(hint),LangTr("At (18,22), use %s to enter a hiding place. Use it again to leave.","到（18,22）的藏身点，按 %s 进入；之后再按一次离开。"),VkName(g_bindings[ACT_INTERACT]));return hint;
    case TUT_PULSE:snprintf(hint,sizeof(hint),LangTr("Leave cover, then use %s to activate your character ability.","离开藏身点后，按 %s 使用角色能力。"),VkName(g_bindings[ACT_DECOY]));return hint;
    case TUT_LOCKS:return LangTr("Open any marked lockbox. The other locks are available for free practice.",
        "打开任意一个上锁的练习箱。其余锁具可留到自由训练场练习。");
    case TUT_SHOP:return LangTr("Enter a hiding place, then T opens supplies. Buy one item, Esc returns. Practice gold: 1200.","先进入南侧藏身点，再按T开商店。买一件商品后ESC返回。练习金币1200，不带出。");
    case TUT_UPGRADE:return LangTr("While hidden, T opens the shop. Switch to UPGRADES and buy one rank.","留在藏身点中，按T进入属性强化，买一级后返回。");
    case TUT_RHYTHM:return LangTr("T > REST > RHYTHM CONSOLE. QWER hits four lanes; complete one 40-second chart. Esc cancels.","留在藏身点：T → 修整 → 节律校准台。QWER击打音符，完成谱面。");
    case TUT_BREAKER:snprintf(hint,sizeof(hint),LangTr("Take the fuse to (29,18), hold %s to restore exit power.","带保险丝到（29,18）的配电箱前，按住 %s 接通出口。"),VkName(g_bindings[ACT_INTERACT]));return hint;
    case TUT_EXIT:return LangTr("Reach the powered exit to enter the practice arena.","走到已通电的出口，完成后进入自由训练场。");
    default:return rewardResult==1?LangTr("1500 STARTER GOLD DEPOSITED. Entering the arena.","1500启动金币已存入银行，即将进入训练场。"):rewardResult==2?LangTr("Course complete. Starter gold was already claimed on this profile.","课程完成。本存档已领取过启动资金，不重复发放。"):LangTr("Reward save failed. Keep this window open to retry; no reward has been confirmed.","奖励保存失败，将重试；尚未确认发放，请暂时保留此窗口。");
    }
}


static void TutorialAdvance(void) {
    if(tutorialStep>=TUT_DONE)return;
    tutorialStep++;tutorialTimer=tutorialActionTimer=stepElapsed=0;
    lessonX=posX;lessonY=posY;lessonBattery=battery;
    tutorialLightOff=tutorialThrown=tutorialMapOpened=0;lessonSlot=throwSelected;
    completedActions=0;
    if(tutorialStep==TUT_CRANK) {
        battery=fminf(battery,BatteryMax()*.55f);
        lessonBattery=battery;
    }
    if(tutorialStep==TUT_KNOCK) {
        Hunter0()->x=25.5f; Hunter0()->y=18.5f; Hunter0()->active=1;
        Hunter0()->state=2; Hunter0()->speed=0;
    }
    if(tutorialStep==TUT_HIDE) Hunter0()->active=0;
    if(tutorialStep==TUT_THROW && bottles<2)bottles=2;
    if(tutorialStep==TUT_BEACON && phaseBeacons<2)phaseBeacons=2;
    if(tutorialStep==TUT_DECOY && decoys<2)decoys=2;
    if(tutorialStep==TUT_RHYTHM)calibCooldown=0;
    if(tutorialStep==TUT_DONE){tutorialDone=1;rewardResult=SaveAwardTrainingGraduation();}
    uiDirty=1;
}

void TutorialUpdate(float dt) {
    if(!trainingMode || !guideMode || !(dt>0))return;
    if(tutorialDone) {
        if(!rewardResult){
            /* A failing disk must not receive 120 reward writes per second. */
            tutorialTimer+=dt;
            if(tutorialTimer>=2.0f){rewardResult=SaveAwardTrainingGraduation();tutorialTimer=0;}
            return;
        }
        if (IsActionPressed(ACT_MENU_CANCEL)) {
            ResetRunModeFlags();
            hasSave=SaveHasPendingRun();currentState=STATE_MAIN_MENU;menuSelection=0;
            ForceCursor(1);uiDirty=1;
            return;
        }
        if (!IsActionPressed(ACT_MENU_CONFIRM)) return;
        LoopQueueStart(START_TRAINING, 0);
        StartGame();
        return;
    }
    stepElapsed += dt;
    int done=0;
    switch(tutorialStep) {
    case TUT_MOVE:done=Dist2(posX,posY,5.5f,18.5f)<.55f*.55f;break;
    case TUT_LOOK: {
        float dx=5.5f-posX, dy=15.5f-posY;
        float length=sqrtf(dx*dx+dy*dy);
        done=length>.25f && (dx*dirX+dy*dirY)/length>.98f;
        break;
    }
    case TUT_SPRINT:
        if (sprinting && Dist2(posX,posY,lessonX,lessonY)>.000001f) tutorialActionTimer+=dt;
        lessonX=posX; lessonY=posY;
        done=tutorialActionTimer>=.35f && Dist2(posX,posY,10.5f,18.5f)<1.0f;
        break;
    case TUT_LIGHT:if(!flashlightOn)tutorialLightOff=1;done=tutorialLightOff && flashlightOn;break;
    case TUT_MAP:done=tutorialMapOpened && !isMapOpen;break;
    case TUT_SEARCH:done=containers[0].searched;break;
    case TUT_CRANK:done=battery>lessonBattery+1 && hiddenIn<0;break;
    case TUT_SELECT:done=throwSelected!=lessonSlot;break;
    case TUT_THROW:done=tutorialThrown;break;
    case TUT_BEACON:done=(completedActions&(1u<<TUTOR_PHASE))!=0;break;
    case TUT_DECOY:done=(completedActions&(1u<<TUTOR_DECOY))!=0;break;
    case TUT_KNOCK:done=(completedActions&(1u<<TUTOR_WRENCH))!=0;break;
    case TUT_HIDE:if(hiddenIn>=0)tutorialActionTimer=1;done=tutorialActionTimer>0 && hiddenIn<0;break;
    case TUT_PULSE:done=(completedActions&(1u<<TUTOR_PULSE))!=0;break;
    case TUT_LOCKS:for(int i=1;i<=LOCK_TYPE_COUNT;i++)if(!containers[i].locked)done=1;break;
    case TUT_SHOP:done=(completedActions&(1u<<TUTOR_SUPPLY))!=0;break;
    case TUT_UPGRADE:done=(completedActions&(1u<<TUTOR_UPGRADE))!=0;break;
    case TUT_RHYTHM:done=(completedActions&(1u<<TUTOR_RHYTHM))!=0;break;
    case TUT_BREAKER:done=breakerOpen;break;
    case TUT_EXIT:done=PlayerAtExit();break;
    default:break;
    }
    if(done)TutorialAdvance();
}

void TutorialResetMode(void) {
    tutorialStep = 0;
    tutorialDone = 0;
    tutorialGuideOn = 0;
    tutorialTimer = 0.0f;
    tutorialActionTimer = 0.0f;
}

void TutorialRecordThrow(void) { tutorialThrown = 1; }
void TutorialRecordMapOpened(void) {
    if (trainingMode && tutorialStep == TUT_MAP) tutorialMapOpened = 1;
}

void TutorialRecordAction(int action) {
    if(guideMode && action>=0 && action<7)completedActions|=1u<<action;
}
int TutorialGraduationReward(void) {return rewardResult;}
void TutorialTarget(float* x,float* y) {
    *x=posX;*y=posY;
    if(tutorialStep==TUT_MOVE){*x=5.5f;*y=18.5f;}
    else if(tutorialStep==TUT_LOOK){*x=5.5f;*y=15.5f;}
    else if(tutorialStep==TUT_SPRINT){*x=10.5f;*y=18.5f;}
    else if(tutorialStep==TUT_KNOCK){*x=Hunter0()->x;*y=Hunter0()->y;}
    else if((tutorialStep==TUT_SHOP || tutorialStep==TUT_UPGRADE || tutorialStep==TUT_RHYTHM) && hiddenIn<0){*x=hideSpots[0].x;*y=hideSpots[0].y;}
    else if(tutorialStep==TUT_SEARCH){*x=containers[0].x;*y=containers[0].y;}
    else if(tutorialStep==TUT_HIDE){*x=hideSpots[0].x;*y=hideSpots[0].y;}
    else if(tutorialStep==TUT_LOCKS){for(int i=1;i<=LOCK_TYPE_COUNT;i++)if(containers[i].locked){*x=containers[i].x;*y=containers[i].y;break;}}
    else if(tutorialStep==TUT_BREAKER){*x=breaker.x;*y=breaker.y;}
    else if(tutorialStep==TUT_EXIT){*x=exitX;*y=exitY;}
}

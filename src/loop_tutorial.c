/* Tutorial progression and arena: private timers, event-driven input. */
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

#define TUTORIAL_STEP_TIMEOUT 12.0f

int tutorialStep = 0;
int tutorialGuideOn = 0;
int tutorialDone = 0;
static float tutorialTimer = 0.0f;        /* 完成后的回菜单倒计时 */
static float tutorialActionTimer = 0.0f;  /* 持续动作计时（冲刺/摇电） */
static float tutorialStepTimer = 0.0f;
static float tutorialStartX = 0.0f, tutorialStartY = 0.0f;
static float tutorialStartDirX = 1.0f, tutorialStartDirY = 0.0f;
static int tutorialLightOff = 0;        /* 手电关过再开 */
static int tutorialThrown = 0;
static int tutorialKnocked = 0;
static unsigned completedActions=0;
static int lessonSlot=0,rewardResult=0;
static int tutorialMapOpened = 0;       /* 地图至少打开过一次 */

int TutorialStepTotal(void) {return TUT_DONE;}

int TutorialStepNumber(void) {return tutorialDone?TUT_DONE:tutorialStep+1;}

void TutorialBegin(void) {
    tutorialStep = TUT_MOVE;
    completedActions=0;lessonSlot=throwSelected;rewardResult=0;
    tutorialDone = 0;
    tutorialTimer = 0.0f;
    tutorialActionTimer = 0.0f;
    tutorialStartX = posX;
    tutorialStartY = posY;
    tutorialStartDirX = dirX;
    tutorialStartDirY = dirY;
    tutorialLightOff = 0;
    tutorialThrown = 0;
    tutorialKnocked = 0;
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
        if(x>=5 && x<=31 && y>=12 && y<=16)worldMap[y][x]=0;
        if(x>=16 && x<=22 && y>=20 && y<=25)worldMap[y][x]=0;
    }
    posX=3.5f;posY=18.5f;dirX=1;dirY=0;planeX=0;planeY=.66f;
    roomPropCount=0;hatchCount=0;safeRoom.active=0;
    memset(containers,0,sizeof(containers));containerCount=1+LOCK_TYPE_COUNT;
    containers[0].x=7.5f;containers[0].y=18.5f;containers[0].active=1;containers[0].content=1;
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

void TrainingBuildArena(void) {
    if(guideMode){GuidedBuildFloor();return;}
    /* 2026-11-08：引导模式强制展开分步教程，从第一步开始 */
    if (guideMode) {
        TutorialRestartGuide();
    }
    /* 2026-11-08（第 11 轮）：七个教学站点，位置见 training_stations.c */
    {  TrainingStationsReset(); }
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) {
            worldMap[y][x] = 1;
            explored[y][x] = 1;          /* 长廊全图可见，方便看提示 */
        }

    /* 一条 3 格高、从 x=2 到 x=21 的直走廊，y 中心 12（作者原版，已恢复） */
    int y0 = MAP_HEIGHT / 2 - 1, y1 = MAP_HEIGHT / 2 + 1;
    for (int y = y0; y <= y1; y++)
        for (int x = 2; x <= MAP_WIDTH - 3; x++)
            worldMap[y][x] = 0;

    posX = 3.5f;
    posY = (float)(MAP_HEIGHT / 2) + 0.5f;
    dirX = 1.0f; dirY = 0.0f;

    /* 听声怪：标本模式冻结在原地看着，实战模式给正常参数让它真的追声 */
    Hunter0()->x = 18.5f;
    Hunter0()->y = (float)(MAP_HEIGHT / 2) + 0.5f;
    Hunter0()->active = 1;
    Hunter0()->state = 2;
    Hunter0()->stun = 0.0f;
    Hunter0()->speed = 0.0f;          /* 标本：完全不动 */

    Hunter0()->x = 18.5f;
    Hunter0()->y = (float)(MAP_HEIGHT / 2) + 0.5f;
    Hunter0()->active = 1;
    Hunter0()->state = 2;
    Hunter0()->stun = 0.0f;
    Hunter0()->speed = 0.0f;          /* 标靶：不动 */
    { Hunter1()->active = 0; Hunter2()->active = 0; }   /* 新局清掉第 2、3 只听声鬼 */

    /* 2026-11-06：追光怪现在是两只，复位必须两只都清 */
    lightBeasts[0].active = 0;
    lightBeasts[1].active = 0;
    parasite.active = 0;
    for (int i = 0; i < LEECH_MAX; i++) leeches[i].active = 0;

    /* 教程里没有藏身点；必须清掉上一局留下的 active 标记，
       否则走廊里会凭空冒出「按 E 躲藏」提示。 */
    hideSpotCount = 0;
    hideRepairCount = 0;
    hiddenIn = -1;
    hideSpotNear = -1;
    for (int i = 0; i < HIDESPOT_MAX; i++) hideSpots[i].active = 0;

    /* v3.2：四个上锁的箱子，四种锁型各一个。
       最近的一个（DIAL）里放本层唯一的一根保险丝；
       另外三个是补给/练习箱，可开可不开。
       这样教程既是「锁盘教学」，又是一个最小可通关关卡：
       撬开 DIAL 箱 → 拿 1 根保险丝 → 给配电箱通电 → 走到出口。 */
    containerCount = 4;
    memset(containers, 0, sizeof(containers));
    {
        static const float boxX[4] = { 6.5f, 9.0f, 11.5f, 14.0f };
        static const int   boxLock[4] = { LOCK_TYPE_DIAL, LOCK_TYPE_WIRE, LOCK_TYPE_EQ, LOCK_TYPE_TUNE };
        static const int   boxCode[4] = { 0x012, 0x003, 0x01B, 0x02D };
        static const int   boxContent[4] = { 1, 2, 3, 2 };   /* 1=保险丝，2=电池，3=医疗包 */
        for (int i = 0; i < 4; i++) {
            containers[i].x = boxX[i];
            containers[i].y = (float)(MAP_HEIGHT / 2) + 0.5f;
            containers[i].active = 1;
            containers[i].searched = 0;
            containers[i].locked = 1;
            containers[i].lockType = boxLock[i];
            containers[i].lockCode = boxCode[i];
            containers[i].content = boxContent[i];
        }
    }

    /* 最小可通关：1 根保险丝 → 配电箱 → 出口 */
    safeRoom.active = 0;
    breaker.x = 17.0f;
    breaker.y = (float)(MAP_HEIGHT / 2) + 0.5f;
    breaker.active = 1;
    breakerOpen = 0;
    fusesHeld = 0;
    fusesNeeded = 1;
    exitX = 21.5f;
    exitY = (float)(MAP_HEIGHT / 2) + 0.5f;

    /* 手电初始电量为 55%：这样「按住 R 摇电」真的有东西可充。 */
    battery = (float)BatteryMax() * 0.55f;

    /* ===== v3.0：训练场重做 =====
       用户要求：留 1 个（可搜容器）+ 2 个躲藏点 + 1 个解密装置。
       走廊是 x=2..21、y=11..13 三格高，全部铺在这条线上。 */

    /* ① 1 个可搜容器（带保险丝，方便测「捡→上电」链路） */
    containers[0].x = 5.5f;
    containers[0].y = (float)(MAP_HEIGHT / 2) + 0.5f;
    containers[0].active = 1;
    containers[0].searched = 0;
    containers[0].content = 1;          /* 1 = 保险丝 */
    containers[0].locked = 0;
    containers[0].lockType = 0;
    containers[0].lockCode = 0;
    containerCount = 1;

    /* ② 2 个躲藏点（衣柜 + 床底，正好覆盖两种高度） */
    hideSpotCount = 2;
    hideSpots[0].x = 9.5f;
    hideSpots[0].y = (float)(MAP_HEIGHT / 2) + 0.5f;
    hideSpots[0].kind = 0;              /* 衣柜 */
    hideSpots[0].active = 1;

    hideSpots[1].x = 12.0f;
    hideSpots[1].y = (float)(MAP_HEIGHT / 2) + 0.5f;
    hideSpots[1].kind = 1;              /* 床底 */
    hideSpots[1].active = 1;

    hiddenIn = -1;
    hideSpotNear = -1;

    /* ③ **4 个解密装置，四种锁型一次全摆出来** ——
       用户要求「把波形锁也加进去，多加一个箱子」。
       现在固定 4 个，每种锁型一个，进一次训练场就能把
       拨盘 / 接线 / 均衡器 / 调波器 四个 UI 全看一遍，不用反复进出。

       走廊 x=2..21 的分配：
         x=5.5   可搜容器
         x=7.5   安全区
         x=9.5   藏身点 1（衣柜）
         x=12.0  藏身点 2（床底）
         x=13.0 / 14.3 / 15.6 / 16.9 / 18.2 / 19.5   六个锁箱（每种锁型一个）
         x=20.5  固定靶
       注意调波器面板比别的锁宽（LUIS(760) vs LUIS(660)），
       正好用来验证宽面板在小窗口下的表现。 */
    {
        for (int k = 0; k < LOCK_TYPE_COUNT; k++) {
            int i = 1 + k;                       /* containers[1..LOCK_TYPE_COUNT] */
            /* 2026-11-05：锁型 4 → 6，间距从 2.0 收到 1.3。
               原来 2.0 × 6 会把最后一个推到 23.5 —— 跑出走廊(21)还压过靶子(20.5)。 */
            containers[i].x = 13.0f + (float)k * 1.3f;
            containers[i].y = (float)(MAP_HEIGHT / 2) + 0.5f;
            containers[i].active = 1;
            containers[i].searched = 0;
            containers[i].content = 0;
            containers[i].locked = 1;
            containers[i].lockType = k;          /* 0 拨盘 1 接线 2 均衡 3 调波 */
            containers[i].lockCode = 0x2A5;
        }
        containerCount = 1 + LOCK_TYPE_COUNT;    /* 1 个可搜 + 每种锁型各一个 */
    }

    if(guideMode){
        /* Linear route: the search box supplies the fuse; one light orb is finite. */
        containerCount=1;containers[0].x=7.5f;containers[0].y=12.5f;
        containers[0].content=1;containers[0].locked=0;
        hideSpotCount=1;hideSpots[0].x=13.5f;hideSpots[0].y=12.5f;
        hideSpots[0].active=1;safeRoom.active=0;
        flares=1;bottles=2;smokeBombs=gluePots=echoBolts=decoys=phaseBeacons=0;
        for(int i=0;i<MON_HUNTER_MAX;i++)monHunters[i].active=0;
        tutorialGuideOn=1;
    }
    ApplyFov();
}

/* ------------------------------------------------------------------ */
/* v3.1 新手教程：一步一步检测玩家真的做了动作                          */
/* ------------------------------------------------------------------ */


const char* TutorialObjective(void) {
    static const char* en[TUT_DONE+1]={
        "01 MOVE","02 LOOK AROUND","03 SPRINT","04 FLASHLIGHT","05 MAP","06 SEARCH",
        "07 CRANK BATTERY","08 SELECT AN ITEM","09 THROW","10 PHASE RETURN","11 ARM A DECOY",
        "12 WRENCH","13 HIDE AND LEAVE","14 HUSH PULSE","15 SIX LOCKS","16 BUY A SUPPLY",
        "17 BUY A RUN UPGRADE","18 RHYTHM CONSOLE","19 POWER THE EXIT","20 EXTRACT","TRAINING COMPLETE"};
    static const char* zh[TUT_DONE+1]={
        "01 移动","02 转动视角","03 冲刺","04 手电开关","05 打开与关闭地图","06 搜查容器",
        "07 手摇充电","08 切换道具","09 蓄力投掷","10 相位返回","11 激活诱饵",
        "12 扳手挥击","13 躲藏与离开","14 静息脉冲","15 六种锁具","16 购买补给",
        "17 购买局内强化","18 节律校准","19 接通配电箱","20 抵达出口","课程完成"};
    int step=tutorialStep>=0 && tutorialStep<=TUT_DONE?tutorialStep:TUT_MOVE;
    return LangTr(en[step],zh[step]);
}

const char* TutorialHint(void) {
    static char hint[240];
    switch(tutorialStep) {
    case TUT_MOVE: return LangTr("Walk one tile using your movement keys. This floor is separate from your run.","使用移动键走出一格。这是独立引导层，不覆盖正式对局。");
    case TUT_LOOK:return LangTr("Move the mouse to turn your view.","移动鼠标转动视角，寻找走廊两侧的练习站。");
    case TUT_SPRINT:snprintf(hint,sizeof(hint),LangTr("Hold %s and move for half a second.","按住 %s 并移动半秒，观察体力消耗。"),VkName(g_bindings[ACT_SPRINT]));return hint;
    case TUT_LIGHT:snprintf(hint,sizeof(hint),LangTr("Use %s to turn the flashlight off, then on.","按 %s 关闭手电，再打开。关灯能避开追光鬼。"),VkName(g_bindings[ACT_FLASHLIGHT]));return hint;
    case TUT_MAP:snprintf(hint,sizeof(hint),LangTr("Open the map with %s, then close it to continue.","使用 %s 打开地图，再关闭。锁箱在北侧，藏身点在南侧。"),VkName(g_bindings[ACT_MAP]));return hint;
    case TUT_SEARCH:snprintf(hint,sizeof(hint),LangTr("Hold %s at the unlocked box (7,18) to collect its fuse.","到未上锁的箱子（7,18）前，按住 %s 取走保险丝。"),VkName(g_bindings[ACT_INTERACT]));return hint;
    case TUT_CRANK:snprintf(hint,sizeof(hint),LangTr("Hold %s for half a second to charge the battery.","按住 %s 半秒充电；充电会制造声音，实战时先确认安全。"),VkName(g_bindings[ACT_CRANK]));return hint;
    case TUT_SELECT:return LangTr("Select a DIFFERENT carried item using 1/2/3 or the wheel.","按1/2/3或滚动滚轮，切换为另一种持有的道具。");
    case TUT_THROW:snprintf(hint,sizeof(hint),LangTr("Select a stone. Hold %s, aim down the corridor, then release.","选择投石，按住 %s 蓄力，朝走廊瞄准后松开。左键可以取消投掷。"),VkName(g_bindings[ACT_THROW]));return hint;
    case TUT_BEACON:return LangTr("Select a PHASE BEACON, throw it to place an anchor, walk away, then Q to return.","滚轮选择相位信标，投出并等它落地。离开锚点后按Q返回。");
    case TUT_DECOY:return LangTr("Select and throw a DECOY, then press C to arm it.","滚轮选择诱饵玩偶并投出，等落地后按C激活；用来引开听声鬼。");
    case TUT_KNOCK:return LangTr("Hold and release the left mouse button to swing the wrench.","按住鼠标左键蓄力后松开，挥动扳手。准星反馈可区分命中与挥空。");
    case TUT_HIDE:snprintf(hint,sizeof(hint),LangTr("At (18,22), use %s to enter a hiding place. Use it again to leave.","到（18,22）的藏身点，按 %s 进入；之后再按一次离开。"),VkName(g_bindings[ACT_INTERACT]));return hint;
    case TUT_PULSE:snprintf(hint,sizeof(hint),LangTr("Leave cover, then use %s for Hush Pulse. The camp lends this ability.","先离开藏身点，再按 %s 释放静息脉冲。训练时临时开放，不改动永久技能。"),VkName(g_bindings[ACT_DECOY]));return hint;
    case TUT_LOCKS:{int solved=0;for(int i=1;i<=LOCK_TYPE_COUNT;i++)if(!containers[i].locked)solved++;
        snprintf(hint,sizeof(hint),LangTr("Unlock the six north-side boxes: dial, wire, equalizer, tuning, overload, saw. %d / 6 unlocked.","打开北侧六只锁箱：拨盘、接线、均衡、调波、过载、锯锁。已解开 %d / 6。"),solved);return hint;}
    case TUT_SHOP:return LangTr("Enter a hiding place, then T opens supplies. Buy one item, Esc returns. Practice gold: 1200.","先进入南侧藏身点，再按T开商店。买一件商品后ESC返回。练习金币1200，不带出。");
    case TUT_UPGRADE:return LangTr("While hidden, T opens the shop. Switch to UPGRADES and buy one rank.","留在藏身点中，按T进入属性强化，买一级后返回。");
    case TUT_RHYTHM:return LangTr("T > REST > RHYTHM CONSOLE. QWER hits four lanes; complete one 40-second chart. Esc cancels.","留在藏身点：T → 修整 → 节律校准台。QWER击打音符，完成谱面。");
    case TUT_BREAKER:snprintf(hint,sizeof(hint),LangTr("Take the fuse to (29,18), hold %s to restore exit power.","带保险丝到（29,18）的配电箱前，按住 %s 接通出口。"),VkName(g_bindings[ACT_INTERACT]));return hint;
    case TUT_EXIT:return LangTr("Reach the lit exit at (32,18). Your real run remains intact.","走到（32,18）的已通电出口。正式对局保持原样，完成后返回。");
    default:return rewardResult==1?LangTr("1500 STARTER GOLD DEPOSITED. Returning shortly.","1500启动金币已存入银行，即将返回。"):rewardResult==2?LangTr("Course complete. Starter gold was already claimed on this profile.","课程完成。本存档已领取过启动资金，不重复发放。"):LangTr("Reward save failed. Keep this window open to retry; no reward has been confirmed.","奖励保存失败，将重试；尚未确认发放，请暂时保留此窗口。");
    }
}


static void TutorialAdvance(void) {
    if(tutorialStep>=TUT_DONE)return;
    tutorialStep++;tutorialTimer=tutorialActionTimer=tutorialStepTimer=0;
    tutorialStartX=posX;tutorialStartY=posY;tutorialStartDirX=dirX;tutorialStartDirY=dirY;
    tutorialLightOff=tutorialThrown=tutorialKnocked=tutorialMapOpened=0;lessonSlot=throwSelected;
    if(tutorialStep==TUT_CRANK)battery=fminf(battery,BatteryMax()*.55f);
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
        tutorialTimer+=dt;
        if(tutorialTimer<5)return;
        int resume=SaveHasPendingRun();ResetRunModeFlags();
        if(resume){ContinueRun();return;}
        hasSave=SaveHasPendingRun();currentState=STATE_MAIN_MENU;menuSelection=0;
        isMapOpen=mapFullScreen=0;ForceCursor(1);uiDirty=1;return;
    }
    tutorialStepTimer+=dt;
    int done=0;
    switch(tutorialStep) {
    case TUT_MOVE:done=Dist2(posX,posY,tutorialStartX,tutorialStartY)>=1;break;
    case TUT_LOOK:done=dirX*tutorialStartDirX+dirY*tutorialStartDirY<.985f;break;
    case TUT_SPRINT:if(sprinting)tutorialActionTimer+=dt;else tutorialActionTimer=0;done=tutorialActionTimer>=.5f;break;
    case TUT_LIGHT:if(!flashlightOn)tutorialLightOff=1;done=tutorialLightOff && flashlightOn;break;
    case TUT_MAP:done=tutorialMapOpened && !isMapOpen;break;
    case TUT_SEARCH:done=containers[0].searched;break;
    case TUT_CRANK:if(cranking || ActionIsDown(ACT_CRANK))tutorialActionTimer+=dt;else tutorialActionTimer=0;done=tutorialActionTimer>=.5f;break;
    case TUT_SELECT:done=throwSelected!=lessonSlot;break;
    case TUT_THROW:done=tutorialThrown;break;
    case TUT_BEACON:done=(completedActions&(1u<<TUTOR_PHASE))!=0;break;
    case TUT_DECOY:done=(completedActions&(1u<<TUTOR_DECOY))!=0;break;
    case TUT_KNOCK:done=(completedActions&(1u<<TUTOR_WRENCH))!=0;break;
    case TUT_HIDE:if(hiddenIn>=0)tutorialActionTimer=1;done=tutorialActionTimer>0 && hiddenIn<0;break;
    case TUT_PULSE:done=(completedActions&(1u<<TUTOR_PULSE))!=0;break;
    case TUT_LOCKS:done=1;for(int i=1;i<=LOCK_TYPE_COUNT;i++)if(containers[i].locked)done=0;break;
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

void TutorialRestartGuide(void) {
    tutorialGuideOn = 1;
    tutorialStep = TUT_MOVE;
    tutorialStepTimer = 0.0f;
}

void TutorialRecordThrow(void) { tutorialThrown = 1; }
void TutorialRecordNoise(void) { tutorialKnocked = 1; }
void TutorialRecordMapOpened(void) {
    if (trainingMode && tutorialStep == TUT_MAP) tutorialMapOpened = 1;
}

void TutorialVisitStation(int station) {
    static const int kStationStep[7] = {
        TUT_MOVE, TUT_LIGHT, TUT_KNOCK, TUT_SEARCH,
        TUT_HIDE, TUT_BREAKER, TUT_EXIT
    };
    if (!guideMode && tutorialGuideOn && station >= 0 && station < 7 && tutorialStep < kStationStep[station]) {
        tutorialStep = kStationStep[station];
        tutorialStepTimer = 0.0f;
    }
}

void TutorialRecordAction(int action) {
    if(guideMode && action>=0 && action<7)completedActions|=1u<<action;
}
int TutorialGraduationReward(void) {return rewardResult;}
void TutorialTarget(float* x,float* y) {
    *x=posX;*y=posY;
    if(tutorialStep==TUT_SEARCH){*x=containers[0].x;*y=containers[0].y;}
    else if(tutorialStep==TUT_HIDE){*x=hideSpots[0].x;*y=hideSpots[0].y;}
    else if(tutorialStep==TUT_LOCKS){for(int i=1;i<=LOCK_TYPE_COUNT;i++)if(containers[i].locked){*x=containers[i].x;*y=containers[i].y;break;}}
    else if(tutorialStep==TUT_BREAKER){*x=breaker.x;*y=breaker.y;}
    else if(tutorialStep==TUT_EXIT){*x=exitX;*y=exitY;}
}

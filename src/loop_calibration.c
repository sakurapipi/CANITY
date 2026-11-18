/* Four-lane rhythm calibration. Rewards settle only after a completed chart. */
#include "common.h"
#include "tutorial.h"
#include "calibration.h"
#include "score_charts.h"
#include "entity.h"
#include "state.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "lang.h"
#include "audio.h"
#include "plat.h"
#include "control_effect.h"
#include "music.h"
#include "bench.h"
#include "run_economy.h"

int calibActive=0,calibResult=0;
float calibPos=.5f,calibZoneC=.5f,calibCooldown=0,calibResultTimer=0;
static const ScoreChart* chart;
static int stationSession=0;
static unsigned char judged[512];
static float elapsed=0,drainBudget=0,leadIn=0;
static double lastSpace=-10, outroUntil;
int CalibOutroActive(void) { return PlatNow() < outroUntil; }
static int waitingForStart=0;
static int errors=0,combo=0,pending=0,level=-1,startLatch=0;
static int hits=0,bestCombo=0;static float restored=0,charged=0;
static unsigned char laneLatch[4];
static float lanePressAt[4];   /* 本 lane 上一次真正被判定消费的时刻 */
static float laneMissAt[4];    /* 本 lane 最近一次漏音/被吃掉的音符时刻 */
/* 同一物理按键的重复 keydown（系统重复、触屏重复事件、平台重复边沿）
   在这么短时间内只允许判定一次。谱面最小音距 240ms，70ms 不会吞掉连打。 */
#define CALIB_LANE_DEBOUNCE 0.07f
/* Music/chart time stays real-time; only approach speed and density ramp. */
static float flash[4],hitSanity[4],hitBattery[4];
static int judgement[4];
static float HitWindow(void) {return fmaxf(.16f,.26f-(currentLevel-1)*.002f);}
float CalibZoneScale(void) {return HitWindow()/.26f;}
float CalibSpeedScale(void) {return 1.0f+fminf(.65f,fmaxf(0,elapsed)*.012f+fmaxf(0,(float)(currentLevel-1))*.008f);}
int CalibReadyState(void) {return waitingForStart?1:leadIn>0?2:0;}
float CalibLeadIn(void) {return leadIn;}
float CalibProgress(void) {return fminf(1,fmaxf(0,elapsed/40.0f));}
float CalibLaneFlash(int lane) {return lane>=0 && lane<4?flash[lane]:0;}
int CalibLaneJudgement(int lane) {return lane>=0 && lane<4?judgement[lane]:0;}
float CalibLastGain(int lane,int cell) {return lane>=0 && lane<4?(cell?hitBattery[lane]:hitSanity[lane]):0;}
int CalibNoteCount(void) {return calibActive && chart?chart->count:0;}
int CalibNoteLane(int note) {return chart && note>=0 && note<chart->count?chart->notes[note].lane:0;}
static int Enabled(int note) {
    /* Decide from the note's timestamp, never enable an already-passed note. */
    int stage=1+(chart->notes[note].ms>=14000 || currentLevel>=4)
               +(chart->notes[note].ms>=28000 || currentLevel>=8);
    return chart->notes[note].difficulty<=stage;
}
float CalibNoteOffset(int note) {
    if(waitingForStart || leadIn>0 || !chart || note<0 || note>=chart->count || judged[note] || !Enabled(note))return -999;
    return chart->notes[note].ms*.001f-elapsed;
}
int CalibErrors(void) {return errors;}
float CalibBatteryRestored(void){return charged;}
int CalibCombo(void) {return combo;}
int CalibPendingReward(void) {return pending;}
void CalibAccumulateDrain(float amount) {if(calibActive && amount>0 && isfinite(amount))drainBudget+=amount;}
static void Finish(int result) {
    /* 2026-11-16（用户报告的 bug）：小游戏失败**不许把玩家踢出藏身点**。
       原来这里 `hiddenIn=-1; resetInputState=1;` 会把玩家从柜子里弹出来，
       等于失败一次就暴露在怪物面前。现在只结束音游，藏身状态保持不变。 */
    MusicEndCalibration();waitingForStart=0;leadIn=0;outroUntil=PlatNow()+.3;
    calibActive=0;calibResult=result;calibResultTimer=3;calibCooldown=guideMode?3:30;
    static char message[160];
    if(result==1){
        TutorialRecordAction(TUTOR_RHYTHM);int gold=RunEconomyRhythmGold(hits,bestCombo);
        snprintf(message,sizeof(message),LangTr("COMPLETE: +%.1f SAN / +%.1f CELL / +%d GOLD","完成：理智+%.1f / 电量+%.1f / 金币+%d"),(double)restored,(double)charged,gold);PlaySfx(SFX_POWER,.55f);
    }else snprintf(message,sizeof(message),LangTr("%s / %.1f SANITY ALREADY RESTORED","%s / 已即时恢复%.1f理智"),result==3?LangTr("FAILED: TOO MANY MISSES","失误超限：小游戏结束"):LangTr("CANCELLED","已取消"),(double)restored);
    pickupMsg=message;stationSession=0;memset(keyPressed,0,sizeof(keyPressed));pickupMsgTimer=3;uiDirty=1;
}
void CalibStart(void) {
    if(calibActive || (!stationSession && hiddenIn<0 && !trainingMode) || calibCooldown>0)return;
    if(!MusicPrepareForCalibration()){pickupMsg=LangTr("CHART MUSIC NOT READY","谱面音乐尚未就绪");pickupMsgTimer=1.4f;return;}
    int scene=(int)MusicCurrentScene();if(scene<0 || scene>=6)scene=MUSIC_EXPLORE;
    chart=&scoreCharts[scene];
    if(chart->count>512 || chart->count<=0){MusicEndCalibration();return;}
    memset(flash,0,sizeof(flash));memset(judgement,0,sizeof(judgement));
    memset(judged,0,sizeof(judged));elapsed=-AudioOutputLatencySeconds()-.006f;errors=combo=pending=hits=bestCombo=0;restored=charged=0;drainBudget=0;
    for(int lane=0;lane<4;lane++){laneLatch[lane]=(unsigned char)PlatKeyDown("QWER"[lane]);lanePressAt[lane]=elapsed-10.0f;laneMissAt[lane]=elapsed-10.0f;}
    level=currentLevel;lastSpace=-10;startLatch=0;calibActive=1;calibResult=0;
    waitingForStart=1;leadIn=0;uiDirty=1;
}
void CalibLock(void) {
    if(!calibActive)return;
    double now=PlatNow();
    if(now-lastSpace<=.35)Finish(2);
    else lastSpace=now;
}
static void Hit(int lane) {
    int best=-1;float delta=10;
    for(int i=0;i<chart->count;i++) {
        if(judged[i] || !Enabled(i) || chart->notes[i].lane!=lane)continue;
        float d=fabsf(chart->notes[i].ms*.001f-elapsed);
        if(d<delta){delta=d;best=i;}
    }
    if(best<0 || delta>HitWindow()){
        /* 漏音已经在 CalibTick 扣过一次；玩家在同一个音符的窗口内补按，
           不应该再扣第二次。 */
        int alreadyMissed = (elapsed-laneMissAt[lane] <= HitWindow());
        if(!alreadyMissed){
            errors++;PlaySfx(SFX_CRANK,.25f);laneMissAt[lane]=elapsed;
        }
        combo=0;flash[lane]=.45f;judgement[lane]=-1;
        /* 一次早按只算一次失误：把这次想接、但还没到的音符吃掉，
           否则同一个音符过一会儿还会被“漏音”再记一次，玩家看到的是
           “按一下加两次失误”。只有明显是在打这个音符时才消费。 */
        if(best>=0 && delta<=HitWindow()*2.0f) judged[best]=1;
    }
    else {
        judged[best]=1;combo++;hits++;if(combo>bestCombo)bestCombo=combo;
        int perfect=delta<=HitWindow()*.35f;
        float chain=1+fminf(20,(float)(combo-1))*.04f;
        float gain=(perfect?1.0f:.8f)*chain+BenchRhythmHealBonus();
        flash[lane]=.45f;judgement[lane]=perfect?2:1;
        float before=sanity;sanity=fminf(SanityMax(),sanity+gain);hitSanity[lane]=sanity-before;restored+=hitSanity[lane];pending=(int)floorf(restored+.5f);
        float cellBefore=battery;battery=fminf((float)BatteryMax(),battery+(perfect?3.0f:2.2f)*chain);hitBattery[lane]=battery-cellBefore;charged+=hitBattery[lane];
        PlaySfx(perfect?SFX_POWER:SFX_PICKUP,perfect?.22f:.18f);
    }
    if(errors>5)Finish(3);
}
void CalibTick(float dt) {
    if(!(dt>0))return;
    calibCooldown=fmaxf(0,calibCooldown-dt);calibResultTimer=fmaxf(0,calibResultTimer-dt);
    for(int lane=0;lane<4;lane++)flash[lane]=fmaxf(0,flash[lane]-dt);
    if(!calibActive)return;
    if(currentLevel!=level || (stationSession?currentState!=STATE_SAFEHOUSE:(!trainingMode && hiddenIn<0))){Finish(2);return;}
    if(hiddenIn>=0){
        float regen = HIDE_BATTERY_REGEN * (flashlightOn ? 0.5f : 1.0f);
        battery=fminf((float)BatteryMax(),battery+regen*dt);
        sanity=fminf(SanityMax(),sanity+HIDE_SANITY_REGEN*dt);
    }

    float recoveryDt=fmaxf(0,dt-staminaIdle);
    staminaIdle=fmaxf(0,staminaIdle-dt);
    stamina=fminf(StaminaMax(),stamina+STAMINA_REGEN*recoveryDt*
        (hiddenIn>=0?HIDE_STAMINA_REGEN_MUL:1.f));
    if(waitingForStart){uiDirty=1;return;}
    if(leadIn>0){
        leadIn=fmaxf(0,leadIn-dt);
        if(leadIn==0){MusicSeekSeconds((float)chart->startSample/44100.0f);elapsed=-AudioOutputLatencySeconds()-.006f;}
        uiDirty=1;return;
    }
    elapsed+=dt;calibPos=elapsed/40.0f;
    /* 松键锁存：安全屋里 CalibHandleInput 不跑（它只处理 PLAYING），
       所以 latch 的复位必须放在每帧都执行的 CalibTick 里。 */
    for(int lane=0;lane<4;lane++)if(!PlatKeyDown("QWER"[lane]) && elapsed-lanePressAt[lane]>0.03f)laneLatch[lane]=0;
    for(int i=0;i<chart->count && calibActive;i++) {
        if(judged[i] || !Enabled(i))continue;
        if(chart->notes[i].ms*.001f<elapsed-HitWindow()){
            judged[i]=1;errors++;combo=0;flash[chart->notes[i].lane]=.45f;judgement[chart->notes[i].lane]=-1;laneMissAt[chart->notes[i].lane]=chart->notes[i].ms*.001f;
            if(errors>5)Finish(3);
        }
    }
    if(calibActive && elapsed>=40)Finish(1);
    uiDirty=1;
}
void CalibHandleInput(int inSafe) {
    (void)inSafe;
    if(currentState!=STATE_PLAYING || ControlActive())return;
    int down=PlatKeyDown(VK_SPACE);
    if(keyPressed[VK_SPACE] && (hiddenIn>=0 || trainingMode)) {
        keyPressed[VK_SPACE]=0;
        if(!startLatch){startLatch=1;if(calibActive)CalibLock();else CalibStart();}
    }
    if(!down)startLatch=0;
    if(calibActive)for(int lane=0;lane<4 && calibActive;lane++){
        static const int keys[4]={'Q','W','E','R'};
        int key=keys[lane];
        if(!PlatKeyDown(key) && elapsed-lanePressAt[lane]>0.03f)laneLatch[lane]=0;
        if(keyPressed[key]){
            /* 这里和 CalibInputKey 是同一份判定的两个入口：
               先消费边沿，再经过同一个 press latch，保证一次按键只判一次。 */
            keyPressed[key]=0;
            if(!laneLatch[lane] && elapsed-lanePressAt[lane]>=CALIB_LANE_DEBOUNCE){
                CalibInputKey(key);
            }
        }
    }
}
void CalibResetRun(void) {outroUntil=0;MusicEndCalibration();waitingForStart=0;leadIn=0;stationSession=0;calibActive=calibResult=0;elapsed=calibCooldown=calibResultTimer=0;startLatch=0;chart=NULL;}

void CalibStartAtStation(void) {
    if(calibActive || currentState!=STATE_SAFEHOUSE || calibCooldown>0)return;
    stationSession=1;CalibStart();if(!calibActive)stationSession=0;
}
void CalibInputKey(int key) {
    if(!calibActive)return;
    if(key==VK_ESCAPE){Finish(2);return;}
    if(key==VK_SPACE){CalibLock();return;}
    for(int lane=0;lane<4;lane++)if(key=="QWER"[lane]){
        if(waitingForStart){waitingForStart=0;leadIn=1.25f;laneLatch[lane]=1;lanePressAt[lane]=elapsed;keyPressed[key]=0;uiDirty=1;return;}
        if(leadIn>0)return;
        /* 同一次按住产生的重复 keydown（系统重复 / 平台重复边沿）不能
           每一个都算一次判定：laneLatch 未松键、或距离上次判定太近，
           都直接吞掉。原来这里无条件 Hit()，按一次会加好几次失误。 */
        if(laneLatch[lane] || elapsed-lanePressAt[lane]<CALIB_LANE_DEBOUNCE)return;
        laneLatch[lane]=1;lanePressAt[lane]=elapsed;
        keyPressed[key]=0;
        Hit(lane);
        return;
    }
}

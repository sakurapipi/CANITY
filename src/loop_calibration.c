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
static float elapsed=0,lastSpace=-10,drainBudget=0;
static int errors=0,combo=0,pending=0,level=-1,startLatch=0;
static int hits=0,bestCombo=0;static float restored=0,charged=0;
static unsigned char laneLatch[4];
static float lanePressAt[4];   /* 本 lane 上一次真正被判定消费的时刻 */
static float laneMissAt[4];    /* 本 lane 最近一次漏音/被吃掉的音符时刻 */
/* 同一物理按键的重复 keydown（系统重复、触屏重复事件、平台重复边沿）
   在这么短时间内只允许判定一次。谱面最小音距 240ms，70ms 不会吞掉连打。 */
#define CALIB_LANE_DEBOUNCE 0.07f
static float HitWindow(void) {return fmaxf(.085f,.18f-(currentLevel-1)*.006f);}
float CalibZoneScale(void) {return HitWindow()/.18f;}
float CalibSpeedScale(void) {return fminf(1.6f,1.0f+(currentLevel-1)*.025f);}
int CalibNoteCount(void) {return calibActive && chart?chart->count:0;}
int CalibNoteLane(int note) {return chart && note>=0 && note<chart->count?chart->notes[note].lane:0;}
static int Enabled(int note) {return chart->notes[note].difficulty<=1+(currentLevel>=4)+(currentLevel>=8);}
float CalibNoteOffset(int note) {
    if(!chart || note<0 || note>=chart->count || judged[note] || !Enabled(note))return -999;
    return chart->notes[note].ms*.001f-elapsed;
}
int CalibErrors(void) {return errors;}
float CalibBatteryRestored(void){return charged;}
int CalibCombo(void) {return combo;}
int CalibPendingReward(void) {return pending;}
void CalibAccumulateDrain(float amount) {if(calibActive && amount>0 && isfinite(amount))drainBudget+=amount;}
static void Finish(int result) {
    if(result==3 && hiddenIn>=0){hiddenIn=-1;resetInputState=1;holdTimer=0;keyPressed[g_bindings[ACT_INTERACT]]=0;}
    calibActive=0;calibResult=result;calibResultTimer=3;calibCooldown=guideMode?3:30;
    static char message[160];
    if(result==1){
        TutorialRecordAction(TUTOR_RHYTHM);int gold=RunEconomyRhythmGold(hits,bestCombo);
        snprintf(message,sizeof(message),LangTr("COMPLETE: +%.1f SAN / +%.1f CELL / +%d GOLD","完成：理智+%.1f / 电量+%.1f / 金币+%d"),(double)restored,(double)charged,gold);PlaySfx(SFX_POWER,.55f);
    }else snprintf(message,sizeof(message),LangTr("%s / %.1f SANITY ALREADY RESTORED","%s / 已即时恢复%.1f理智"),result==3?LangTr("FAILED: LEFT COVER","失误超限：已离开藏身点"):LangTr("CANCELLED","取消"),(double)restored);
    pickupMsg=message;stationSession=0;memset(keyPressed,0,sizeof(keyPressed));pickupMsgTimer=3;uiDirty=1;
}
void CalibStart(void) {
    if(calibActive || (!stationSession && hiddenIn<0 && !trainingMode) || calibCooldown>0)return;
    if(!MusicCanCalibrate()){pickupMsg=LangTr("CHART MUSIC NOT READY","谱面音乐尚未就绪");pickupMsgTimer=1.4f;return;}
    int scene=(int)MusicCurrentScene();if(scene<0 || scene>=6)scene=MUSIC_EXPLORE;
    chart=&scoreCharts[scene];
    if(chart->count>512 || chart->count<=0)return;
    memset(judged,0,sizeof(judged));elapsed=-AudioOutputLatencySeconds()-.006f;errors=combo=pending=hits=bestCombo=0;restored=charged=0;drainBudget=0;
    for(int lane=0;lane<4;lane++){laneLatch[lane]=(unsigned char)PlatKeyDown("QWER"[lane]);lanePressAt[lane]=elapsed-10.0f;laneMissAt[lane]=elapsed-10.0f;}
    level=currentLevel;lastSpace=-10;startLatch=0;calibActive=1;calibResult=0;
    MusicSeekSeconds((float)chart->startSample/44100.0f);uiDirty=1;
}
void CalibLock(void) {
    if(!calibActive)return;
    if(elapsed-lastSpace<=.35f)Finish(2);
    else lastSpace=elapsed;
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
        combo=0;
        /* 一次早按只算一次失误：把这次想接、但还没到的音符吃掉，
           否则同一个音符过一会儿还会被“漏音”再记一次，玩家看到的是
           “按一下加两次失误”。只有明显是在打这个音符时才消费。 */
        if(best>=0 && delta<=HitWindow()*2.0f) judged[best]=1;
    }
    else {
        judged[best]=1;combo++;hits++;if(combo>bestCombo)bestCombo=combo;
        float gain=.6f*(1+fminf(20,(float)(combo-1))*.05f)+BenchRhythmHealBonus();
        float before=sanity;sanity=fminf(SanityMax(),sanity+gain);restored+=sanity-before;pending=(int)floorf(restored+.5f);
        float cellBefore=battery;battery=fminf((float)BatteryMax(),battery+gain*1.25f);charged+=battery-cellBefore;
        PlaySfx(SFX_PICKUP,.18f);
    }
    if(errors>5)Finish(3);
}
void CalibTick(float dt) {
    if(!(dt>0))return;
    calibCooldown=fmaxf(0,calibCooldown-dt);calibResultTimer=fmaxf(0,calibResultTimer-dt);
    if(!calibActive)return;
    if(currentLevel!=level || (stationSession?currentState!=STATE_SAFEHOUSE:(!trainingMode && hiddenIn<0))){Finish(2);return;}
    if(hiddenIn>=0){
        if(!flashlightOn)battery=fminf((float)BatteryMax(),battery+HIDE_BATTERY_REGEN*dt);
        sanity=fminf(SanityMax(),sanity+HIDE_SANITY_REGEN*dt);
    }
    elapsed+=dt;calibPos=elapsed/40.0f;
    /* 松键锁存：安全屋里 CalibHandleInput 不跑（它只处理 PLAYING），
       所以 latch 的复位必须放在每帧都执行的 CalibTick 里。 */
    for(int lane=0;lane<4;lane++)if(!PlatKeyDown("QWER"[lane]) && elapsed-lanePressAt[lane]>0.03f)laneLatch[lane]=0;
    for(int i=0;i<chart->count && calibActive;i++) {
        if(judged[i] || !Enabled(i))continue;
        if(chart->notes[i].ms*.001f<elapsed-HitWindow()){
            judged[i]=1;errors++;combo=0;laneMissAt[chart->notes[i].lane]=chart->notes[i].ms*.001f;
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
                laneLatch[lane]=1;lanePressAt[lane]=elapsed;Hit(lane);
            }
        }
    }
}
void CalibResetRun(void) {stationSession=0;calibActive=calibResult=0;elapsed=calibCooldown=calibResultTimer=0;startLatch=0;chart=NULL;}

void CalibStartAtStation(void) {
    if(calibActive || currentState!=STATE_SAFEHOUSE || calibCooldown>0)return;
    stationSession=1;CalibStart();if(!calibActive)stationSession=0;
}
void CalibInputKey(int key) {
    if(!calibActive)return;
    if(key==VK_ESCAPE){Finish(2);return;}
    if(key==VK_SPACE){CalibLock();return;}
    for(int lane=0;lane<4;lane++)if(key=="QWER"[lane]){
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

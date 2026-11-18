/* Rhythm terminal: bounded lanes and local hit feedback, no camera shake. */
#include "ui_overlay.h"
#include "common.h"
#include "ui.h"
#include "ui_theme.h"
#include "lang.h"
#include "calibration.h"
#include "present_api.h"

static void CenterSmall(int x,int y,const char* text,unsigned color) {
    DrawTextSmallWin(x-DrawTextSmallWidth(text)/2,y,text,color);
}
void UiDrawCalibration(void) {
    int w=UIS(560),margin=UIS(12),th=TextHeightWin();
    if(w>winW-margin*2)w=winW-margin*2;
    int x=(winW-w)/2,y=winH*9/100,h=winH*78/100;
    int laneTop=y+th*3+UIS(18),target=y+h-th*3-UIS(16);
    if(target<laneTop+UIS(24))return;
    BlendRectWin(x,y,x+w,y+h,0x06101C,.98f);
    FrameRectWin(x,y,x+w,y+h,UI_ACCENT,.9f,2);
    BlendRectWin(x,y,x+w,y+UIS(4),0x223A50,1);
    BlendRectWin(x,y,x+(int)(w*CalibProgress()),y+UIS(4),UI_OK,1);
    DrawTextSmallWin(x+margin,y+UIS(10),Tr("RESONANCE / CALIBRATION","共振校准台"),UI_ACCENT);
    char text[128];
    snprintf(text,sizeof(text),Tr("COMBO %d   MISSES %d/6   SPEED %.2fx","连击 %d   失误 %d/6   流速 %.2f倍"),
             CalibCombo(),CalibErrors(),(double)CalibSpeedScale());
    UiTrimSmallText(text,w-margin*2);
    DrawTextSmallWin(x+margin,y+th+UIS(10),text,UI_INK);
    snprintf(text,sizeof(text),Tr("RECOVERED +%d SAN / +%.1f CELL","已恢复 理智+%d / 电量+%.1f"),
             CalibPendingReward(),(double)CalibBatteryRestored());
    UiTrimSmallText(text,w-margin*2);
    DrawTextSmallLiteralWin(x+margin,y+th*2+UIS(10),text,UI_OK);
    int inner=x+margin,laneW=(w-margin*2)/4;
    static const unsigned colors[4]={0x6AD9E8,0x9BAFF4,0xE6B17A,0x86DCAD};
    float horizon=2.2f/CalibSpeedScale();
    for(int lane=0;lane<4;lane++) {
        int lx=inner+lane*laneW,rx=lx+laneW-3,cx=(lx+rx)/2;
        float flash=CalibLaneFlash(lane)/.45f;
        int hit=CalibLaneJudgement(lane);
        unsigned accent=hit<0?0xE56760:colors[lane];
        BlendRectWin(lx,laneTop,rx,target+th+UIS(12),0x142536,1);
        if(flash>0)BlendRectWin(lx,laneTop,rx,target+th+UIS(12),accent,flash*.23f);
        FrameRectWin(lx,target-UIS(7),rx,target+UIS(7),accent,.7f,1);
        if(flash>0) {
            int spread=(int)((1-flash)*laneW*.28f);
            FrameRectWin(lx-spread/4,target-UIS(7)-spread/3,
                         rx+spread/4,target+UIS(7)+spread/3,accent,flash,2);
            const char* mark=hit<0?Tr("MISS","失误"):hit==2?Tr("PERFECT","精准"):Tr("GOOD","命中");
            CenterSmall(cx,target-th-UIS(12),mark,accent);
            if(hit>0) {
                char gain[64];
                snprintf(gain,sizeof(gain),"+%.1f / +%.1f",(double)CalibLastGain(lane,0),(double)CalibLastGain(lane,1));
                UiTrimSmallText(gain,laneW-UIS(8));
                CenterSmall(cx,target-th*2-UIS(12),gain,UI_OK);
            }
        }
        char key[2]={"QWER"[lane],0};
        CenterSmall(cx,target+UIS(12),key,colors[lane]);
    }
    if(CalibReadyState()) {
        char ready[128];
        if(CalibReadyState()==1)snprintf(ready,sizeof(ready),"%s",Tr("PRESS Q / W / E / R TO START","按Q / W / E / R任意键开始"));
        else snprintf(ready,sizeof(ready),Tr("READY  %.1f","准备  %.1f"),(double)CalibLeadIn());
        UiTrimSmallText(ready,w-margin*2);
        CenterSmall(x+w/2,laneTop+(target-laneTop)/2,ready,UI_ACCENT);
    }
    for(int i=0;i<CalibNoteCount();i++) {
        float offset=CalibNoteOffset(i);
        if(offset<-.12f || offset>horizon)continue;
        int lane=CalibNoteLane(i);
        int ny=target-(int)(offset*(target-laneTop)/horizon);
        if(ny<laneTop || ny>target+UIS(6))continue;
        int nx=inner+lane*laneW+UIS(5),rx=inner+(lane+1)*laneW-UIS(8);
        int noteTop=ny-UIS(4)>laneTop?ny-UIS(4):laneTop;
        BlendRectWin(nx,noteTop,rx,ny+UIS(4),colors[lane],1);
        if(ny>laneTop+UIS(2)) BlendRectWin(nx+UIS(3),ny-UIS(2),rx-UIS(3),ny,0xF4F6E9,.9f);
    }
    snprintf(text,sizeof(text),Tr("%.0f%% / 40s  |  PERFECT HITS RECOVER MORE","%.0f%% / 40秒  |  精准与连击提高恢复"),(double)(CalibProgress()*100));
    UiTrimSmallText(text,w-margin*2);
    DrawTextSmallWin(x+margin,y+h-th*2-UIS(8),text,UI_INK_DIM);
    DrawTextSmallWin(x+margin,y+h-th-UIS(5),Tr("Q W E R  |  ESC / DOUBLE SPACE: EXIT","Q W E R  |  ESC或双击空格退出"),UI_ACCENT);
}

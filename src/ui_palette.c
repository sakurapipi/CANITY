#include "common.h"
#include "ui_theme.h"
#include "ui.h"
#include "present_api.h"
#include "economy.h"
#include "state.h"
#include "threat.h"
#include "loop_api.h"
unsigned int UiThemeColor(int role) {
    int live=currentState==STATE_PLAYING || currentState==STATE_SAFEHOUSE || currentState==STATE_PAUSED || currentState==STATE_SKILL_PICK;
    float pressure=live?GoldTier()+ThreatValue()*.45f:BankPreviewTier()+bankRiskPlan*2;
    int tier=pressure>=13?3:pressure>=8?2:pressure>=4?1:0;
    static const unsigned int colors[4][7]={
        {0x79CDF2,0xF0F5FF,0x0B111B,0x172433,0x294D6C,0x4C718D,0x243F56},
        {0xA7C6FF,0xDCEAFF,0x11131F,0x22253A,0x414A73,0x6878A5,0x313959},
        {0xD84A32,0xF0D9CA,0x170C0B,0x2D1410,0x542116,0x8B3420,0x401B12},
        {0xE42A17,0xFFF0D9,0x160706,0x300C08,0x64190F,0xA02B18,0x491109}};
    return colors[tier][role>=0 && role<7?role:0];
}

void UiDecayFrame(void){
    if(((UiThemeColor(0)>>16)&255)<180)return;
    int gutter=UIS(12);
    for(int i=0;i<28;i++){
        int y=(i*83+11)%winH;
        int x=i%2?winW-gutter:0;
        BlendRectWin(x,y,x+gutter,y+UIS(2),0x4A0C07,.8f);
        BlendRectWin(x+gutter/2,y,x+gutter/2+2,y+UIS(16),0x9E2510,.55f);
    }
}


void UiCorrodeRect(int x0,int y0,int x1,int y1){
    if(x1-x0<20 || y1-y0<16 || ((UiThemeColor(0)>>16)&255)<180)return;
    unsigned seed=(unsigned)(x0*127+y0*331+x1*17);
    int margin=UIS(7),depth=UIS(13);if(depth>(y1-y0)/4)depth=(y1-y0)/4;
    for(int i=0;i<9;i++){
        seed=seed*1664525u+1013904223u;
        int x=x0+3+(int)(seed%(unsigned)(x1-x0-9));
        int len=3+(int)(seed%9);
        for(int row=0;row<depth;row++){
            int w=len-row/2;if(w<1)break;
            int right=x+w;if(right>x1)right=x1;
            BlendRectWin(x,y0+row,right,y0+row+1,row<2?0x060708:0x40130B,.9f);
        }
        int px=x,py=y1-1;
        for(int k=0;k<depth;k++){
            int nx=px+(k%3==0?1:-1);if(nx<x0+2 || nx>x1-2)nx=px;
            BlendRectWin(nx,py-k,nx+1,py-k+1,0x080B0D,.85f);px=nx;
        }
    }
    /* Side gutters carry the grime; keep all center text and button labels intact. */
    for(int i=0;i<5;i++){
        int y=y0+4+(i*41)%(y1-y0-8);
        BlendRectWin(x0+1,y,x0+margin,y+2,0x7B2716,.7f);
        BlendRectWin(x1-margin,y,x1-1,y+2,0x29120C,.85f);
    }
}


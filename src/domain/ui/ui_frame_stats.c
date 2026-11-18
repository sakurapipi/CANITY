#include "ui_frame_stats.h"
#include "common.h"
#include "ui.h"
#include "ui_text.h"
#include "render.h"
#include "present_api.h"

void UiDrawFrameStats(void) {
    char label[80];
    snprintf(label,sizeof label,"FPS %d  %.1f/%.1fms  %dx%d",
             currentFps,lastRenderMs,frameMsMax,renderW,renderH);
    int pad=UIS(5),width=DrawTextSmallWidth(label)+pad*2,height=FONT_H+pad*2;
    int x=winW-UIS(18)-width,y;
    if (!UiFindFreeBand(x,UIS(18),width,height,winH/2,&y)) return;
    BlendRectWin(x,y,x+width,y+height,0x101922,.84f);
    DrawTextSmallLiteralWin(x+pad,y+pad,label,0xdde5eb);
    UiTrack(x,y,width,height,"frame stats");
}

#include "event_runtime.h"
#include "skills.h"
/* One semantic marker per entity, cell-bounded glyphs and no blinking resources. */
#include "map_markers.h"
#include "common.h"
#include "render.h"
#include "map_rooms.h"
#include "room_theme.h"
#include "hatch.h"
#include "echo_marks.h"
#include "entity.h"
#include "world_api.h"
#include "player_api.h"
#include "protocol.h"
unsigned MapIconColor(int kind) {
    static const unsigned colors[MAP_ICON_COUNT]={
        0xFFE3A1,0xFFB65F,0xE1F0FF,0x8CBDFF,0xADFFF0,0xFF7864,
        0xE2BDFF,0x9985B0,0xFFFAD2,0xFF7465,0xFF9B8C,0xFFDA77,0x80F0FF,0xFFEFE0,0xABD1FF,0xFFE8A1,0xF4B265
    };
    return kind>=0 && kind<MAP_ICON_COUNT?colors[kind]:0xFFFFFF;
}
int MapIconPixel(int kind,int x,int y) {
    static const unsigned char glyphs[MAP_ICON_COUNT][5]={
        {31,17,31,17,31}, {14,4,31,4,14}, {4,8,31,2,4},
        {4,14,31,17,21}, {17,17,31,17,17}, {17,10,4,10,17},
        {4,14,27,14,4}, {4,10,17,10,4}, {4,6,31,6,4},
        {0,14,21,14,0}, {31,21,17,10,4},
        {5,6,31,12,20}, {17,10,21,10,17}, {31,21,17,21,31}, {31,17,31,4,14}, {30,17,29,21,31}, {31,17,21,17,31}
    };
    return kind>=0 && kind<MAP_ICON_COUNT && x>=0 && x<5 && y>=0 && y<5 &&
           (glyphs[kind][y]&(1u<<(4-x)));
}
static int Cell(float x,float y,int* cx,int* cy) {
    if(!isfinite(x) || !isfinite(y) || x<0 || y<0 || x>=MAP_WIDTH || y>=MAP_HEIGHT)return 0;
    *cx=(int)x;*cy=(int)y;return 1;
}
static void Mark(unsigned int grid[MAP_HEIGHT][MAP_WIDTH],float x,float y,int kind,int reveal) {
    int gridReveal=RoomThemeMapRevealed();
    int cx,cy;if(!Cell(x,y,&cx,&cy) || (!reveal && !gridReveal && !explored[cy][cx]))return;
    grid[cy][cx]|=1u<<kind;
}
static void Glyph(int x,int y,int size,int kind) {
    for(int dy=0;dy<size;dy++)for(int dx=0;dx<size;dx++) {
        /* Pixel-centre sampling keeps small masks symmetric. */
        int gx=(dx*5+2)/size,gy=(dy*5+2)/size;
        PutPixel(x+dx,y+dy,MapIconPixel(kind,gx,gy)?MapIconColor(kind):0x070D14);
    }
}
void DrawMapMarkers(int ox,int oy,int cellSize) {
    unsigned int grid[MAP_HEIGHT][MAP_WIDTH]={{0}};
    for(int i=0;i<containerCount;i++) {
        Container* c=&containers[i];
        if(!c->active || c->searched)continue;
        if(levelEvent==EV_SUPPLY && c->content==EVENT_CACHE_CONTENT) {
            int x=(int)c->x,y=(int)c->y;
            if((unsigned)x<MAP_WIDTH && (unsigned)y<MAP_HEIGHT && explored[y][x])
                Mark(grid,c->x,c->y,MAP_ICON_CACHE,0);
            continue;
        }
        int kind=c->content==1?MAP_ICON_FUSE:MAP_ICON_CRATE;
        /* Echo is an alternate observation of this same crate, not another icon. */
        for(int j=0;j<ECHO_MARK_MAX;j++)if(echoMarkActive[j] && echoMarkT[j]>0 && echoMarkKind[j]!=0 &&
            Dist2(c->x,c->y,echoMarkX[j],echoMarkY[j])<.1f) {
            kind=echoMarkKind[j]==3?MAP_ICON_MIMIC:echoMarkKind[j]==2?MAP_ICON_FUSE:MAP_ICON_CRATE;break;
        }
        Mark(grid,c->x,c->y,kind,0);
    }
    if(breaker.active)Mark(grid,breaker.x,breaker.y,MAP_ICON_BREAKER,0);
    if(safeRoom.active)Mark(grid,safeRoom.x,safeRoom.y,MAP_ICON_SAFE,0);
    int coverRank=SkillClampRank(SK_BREATH,skillRank[SK_BREATH]);
    float range=2+2*coverRank;
    for(int i=0;i<hideSpotCount;i++)if(hideSpots[i].active) {
        int reveal=coverRank>0 && Dist2(posX,posY,hideSpots[i].x,hideSpots[i].y)<=range*range;
        Mark(grid,hideSpots[i].x,hideSpots[i].y,hideSpots[i].broken?MAP_ICON_BROKEN:MAP_ICON_HIDE,reveal);
    }
    for(int i=0;i<hatchCount;i++)if(hatches[i].active) {
        int spent=hatches[i].state==HATCH_FIRED || hatches[i].state==HATCH_OPEN;
        Mark(grid,hatches[i].x,hatches[i].y,spent?MAP_ICON_SPENT:MAP_ICON_DEVICE,0);
    }
    int mission=(protocolRun.flags&PROTOCOL_MISSIONS)!=0;
    for (int id=0;id<PROTOCOL_DEVICE_COUNT;id++) {
        float x,y;
        if (id==PROTOCOL_TRACE || !ProtocolProp(id,&x,&y)) continue;
        int icon=id==PROTOCOL_TERMINAL?MAP_ICON_TERMINAL:MAP_ICON_DEVICE;
        if (mission && id==PROTOCOL_EVIDENCE)
            icon=MAP_ICON_COIL+protocolRun.story-1;
        float tx,ty;
        int show=mission && ProtocolTarget(&tx,&ty) && Dist2(tx,ty,x,y)<.01f;
        if (mission && id==PROTOCOL_RELAY) icon=MAP_ICON_RECORD;
        Mark(grid,x,y,icon,show);
    }
    Mark(grid,exitX,exitY,MAP_ICON_EXIT,mission && ProtocolExitReady());
    for(int j=0;j<ECHO_MARK_MAX;j++)if(echoMarkActive[j] && echoMarkT[j]>0) {
        if(echoMarkKind[j]==0)Mark(grid,echoMarkX[j],echoMarkY[j],MAP_ICON_THREAT,1);
        else for(int i=0;i<containerCount;i++)if(containers[i].active && !containers[i].searched &&
            Dist2(containers[i].x,containers[i].y,echoMarkX[j],echoMarkY[j])<.1f) {
            int kind=echoMarkKind[j]==3?MAP_ICON_MIMIC:echoMarkKind[j]==2?MAP_ICON_FUSE:MAP_ICON_CRATE;
            Mark(grid,echoMarkX[j],echoMarkY[j],kind,1);break;
        }
    }
    static const int priority[]={MAP_ICON_RECORD,MAP_ICON_COIL,MAP_ICON_RECEIVER,MAP_ICON_SHUTTER,MAP_ICON_TERMINAL,MAP_ICON_DEVICE,MAP_ICON_EXIT,MAP_ICON_BROKEN,MAP_ICON_THREAT,
        MAP_ICON_CACHE,MAP_ICON_MIMIC,MAP_ICON_HIDE,MAP_ICON_FUSE,MAP_ICON_BREAKER,MAP_ICON_SAFE,MAP_ICON_CRATE,MAP_ICON_SPENT};
    int size=cellSize>=7?cellSize-2:cellSize;if(size>7)size=7;if(size<1)size=1;
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++) {
        unsigned mask=grid[y][x];if(!mask)continue;
        int first=-1,second=-1,count=0;
        for(int i=0;i<MAP_ICON_COUNT;i++)if(mask&(1u<<priority[i])) {
            count++;
            if(first<0)first=priority[i];else if(second<0)second=priority[i];
        }
        int px=ox+x*cellSize,py=oy+y*cellSize;
        if(second>=0 && cellSize>=9) {
            int sub=(cellSize-2)/2;if(sub>5)sub=5;
            Glyph(px,py+(cellSize-sub)/2,sub,first);
            Glyph(px+cellSize-sub-1,py+(cellSize-sub)/2,sub,second);
            if(count>2)PutPixel(px+cellSize-1,py,0xFFFFFF);
        }else {
            Glyph(px+(cellSize-size)/2,py+(cellSize-size)/2,size,first);
            if(second>=0)PutPixel(px+cellSize-1,py,0xFFFFFF); /* Shared-cell badge. */
        }
    }
}

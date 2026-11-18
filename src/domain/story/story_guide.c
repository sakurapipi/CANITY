#include "common.h"
#include "protocol.h"
#include "story_visual.h"
#include "state.h"
#include "world_api.h"
#include "map_rooms.h"

#define ROUTE_CELLS (MAP_WIDTH*MAP_HEIGHT)
static const int dx[]={1,0,-1,0},dy[]={0,1,0,-1};
static short towards[ROUTE_CELLS];
static int cachedGoal=-1;

void StoryGuideInvalidate(void) { cachedGoal=-1; }

static int TileAt(float x,float y) {
    if (!isfinite(x) || !isfinite(y) || x<1 || y<1 ||
        x>=MAP_WIDTH-1 || y>=MAP_HEIGHT-1) return -1;
    return (int)y*MAP_WIDTH+(int)x;
}

static void BuildRoute(int goal) {
    short queue[ROUTE_CELLS];
    int head=0,tail=0;
    for (int i=0;i<ROUTE_CELLS;i++) towards[i]=-1;
    towards[goal]=(short)goal;
    queue[tail++]=(short)goal;
    while (head<tail) {
        int cell=queue[head++],x=cell%MAP_WIDTH,y=cell/MAP_WIDTH;
        for (int direction=0;direction<4;direction++) {
            int nx=x+dx[direction],ny=y+dy[direction];
            if (nx<=0 || ny<=0 || nx>=MAP_WIDTH-1 || ny>=MAP_HEIGHT-1) continue;
            int next=ny*MAP_WIDTH+nx;
            if (worldMap[ny][nx] || towards[next]>=0 || PropBlocksAt(nx,ny)) continue;
            towards[next]=(short)cell;
            queue[tail++]=(short)next;
        }
    }
    cachedGoal=goal;
}

int StoryGuidePrepare(unsigned char *tiles) {
    memset(tiles,0,ROUTE_CELLS);
    float x,y;
    if (!ProtocolShowGuide() || !ProtocolTarget(&x,&y)) return 0;
    int start=TileAt(posX,posY),goal=TileAt(x,y);
    if (start<0 || goal<0 || worldMap[goal/MAP_WIDTH][goal%MAP_WIDTH]) return 0;
    if (cachedGoal!=goal) BuildRoute(goal);
    if (towards[start]<0 || start==goal) return 0;

    int cell=start;
    for (int step=0;step<8 && cell!=goal;step++) {
        int next=towards[cell],delta=next-cell;
        int direction=delta==1?0:delta==MAP_WIDTH?1:delta==-1?2:3;
        tiles[cell]|=(unsigned char)(1u<<direction);
        tiles[next]|=(unsigned char)(1u<<((direction+2)%4));
        cell=next;
    }
    return 1;
}

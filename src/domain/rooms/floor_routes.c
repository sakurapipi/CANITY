/* Deterministic route repairs. Every carved chain joins existing floor cells. */
#include "common.h"
#include "floor_routes.h"
#include "cell_meta.h"
#include "world_api.h"
static const int routeDX[4]={1,-1,0,0},routeDY[4]={0,0,1,-1};
#define ROUTE_CELLS (MAP_WIDTH*MAP_HEIGHT)
static int Interior(int x,int y){return x>0 && y>0 && x<MAP_WIDTH-1 && y<MAP_HEIGHT-1;}
static int ProtectedWall(int x,int y){
    return cellMeta[y][x].roomId || (cellMeta[y][x].flags&CELLFLAG_ROOMWALL);
}
static int RouteFlood(unsigned char* reached,int* queue){
    memset(reached,0,ROUTE_CELLS);
    if(worldMap[1][1])return 0;
    int head=0,tail=1;queue[0]=MAP_WIDTH+1;reached[queue[0]]=1;
    while(head<tail){
        int p=queue[head++],x=p%MAP_WIDTH,y=p/MAP_WIDTH;
        for(int k=0;k<4;k++){
            int nx=x+routeDX[k],ny=y+routeDY[k],n=ny*MAP_WIDTH+nx;
            if(!Interior(nx,ny) || reached[n] || worldMap[ny][nx])continue;
            reached[n]=1;queue[tail++]=n;
        }
    }
    return tail;
}
int FloorRoutesConnect(int wallBudget){
    static unsigned char reached[ROUTE_CELLS];
    static int parent[ROUTE_CELLS],queue[ROUTE_CELLS];
    for(int merge=0;merge<32;merge++){
        int count=RouteFlood(reached,queue),floorCount=0;
        if(!count)return 0;
        for(int y=1;y<MAP_HEIGHT-1;y++)for(int x=1;x<MAP_WIDTH-1;x++)floorCount+=!worldMap[y][x];
        if(count==floorCount)return 1;
        for(int n=0;n<ROUTE_CELLS;n++)parent[n]=-1;
        int head=0,tail=0,target=-1;
        for(int n=0;n<ROUTE_CELLS;n++)if(reached[n]){parent[n]=-2;queue[tail++]=n;}
        while(head<tail && target<0){
            int p=queue[head++],x=p%MAP_WIDTH,y=p/MAP_WIDTH;
            for(int k=0;k<4;k++){
                int nx=x+routeDX[k],ny=y+routeDY[k],n=ny*MAP_WIDTH+nx;
                if(!Interior(nx,ny) || parent[n]!=-1)continue;
                if(worldMap[ny][nx] && ProtectedWall(nx,ny))continue;
                parent[n]=p;queue[tail++]=n;
                if(!worldMap[ny][nx] && !reached[n]){target=n;break;}
            }
        }
        if(target<0)return 0;
        int walls=0;
        for(int n=target;parent[n]>=0;n=parent[n])walls+=worldMap[n/MAP_WIDTH][n%MAP_WIDTH]!=0;
        if(walls>wallBudget)return 0;
        for(int n=target;parent[n]>=0;n=parent[n])worldMap[n/MAP_WIDTH][n%MAP_WIDTH]=0;
        wallBudget-=walls;
    }
    return 0;
}
static int Neighbors(int x,int y){
    return !worldMap[y][x-1]+!worldMap[y][x+1]+!worldMap[y-1][x]+!worldMap[y+1][x];
}
int FloorRoutesWithinDeadEndLimit(int targetPercent){
    int floors=0,ends=0;
    for(int y=1;y<MAP_HEIGHT-1;y++)for(int x=1;x<MAP_WIDTH-1;x++)if(!worldMap[y][x]){
        floors++;ends+=Neighbors(x,y)<=1;
    }
    int limit=floors*targetPercent/100;
    if(limit<2)limit=2;
    return floors>0 && ends<=limit;
}
/* Search straight and single-bend links, with a maximum of four wall cells. */
static int FindBraid(int x,int y,int *best) {
    int bestCount=5;
    for (int dir=0;dir<4;dir++) {
        int chain[4];
        for (int straight=1;straight<bestCount;straight++) {
            int nx=x+routeDX[dir]*straight,ny=y+routeDY[dir]*straight;
            if (!Interior(nx,ny)) break;
            if (!worldMap[ny][nx]) {
                if (straight>1) {
                    bestCount=straight-1;
                    memcpy(best,chain,(size_t)bestCount*sizeof *best);
                }
                break;
            }
            if (ProtectedWall(nx,ny)) break;
            chain[straight-1]=ny*MAP_WIDTH+nx;
            for (int turn=0;turn<4;turn++) {
                if ((turn<2)==(dir<2)) continue;
                for (int bend=1;straight+bend<=bestCount;bend++) {
                    int tx=nx+routeDX[turn]*bend,ty=ny+routeDY[turn]*bend;
                    if (!Interior(tx,ty)) break;
                    if (!worldMap[ty][tx]) {
                        /* Skip reconnecting to the dead end's original neighbor. */
                        if (abs(tx-x)+abs(ty-y)>1) {
                            bestCount=straight+bend-1;
                            memcpy(best,chain,(size_t)bestCount*sizeof *best);
                        }
                        break;
                    }
                    if (straight+bend>=bestCount || ProtectedWall(tx,ty)) break;
                    chain[straight+bend-1]=ty*MAP_WIDTH+tx;
                }
            }
        }
    }
    return bestCount<5?bestCount:0;
}
void FloorRoutesBraid(int targetPercent){
    if(targetPercent<0)targetPercent=0;
    if(targetPercent>10)targetPercent=10;
    for(int pass=0;pass<8;pass++){
        int floors=0,ends=0;
        for(int y=1;y<MAP_HEIGHT-1;y++)for(int x=1;x<MAP_WIDTH-1;x++)if(!worldMap[y][x]){
            floors++;ends+=Neighbors(x,y)==1;
        }
        if(ends*100<=floors*targetPercent || (targetPercent>0 && ends<=2))return;
        int changed=0;
        for(int y=1;y<MAP_HEIGHT-1;y++)for(int x=1;x<MAP_WIDTH-1;x++){
            if(worldMap[y][x] || Neighbors(x,y)!=1 || ProtectedWall(x,y))continue;
            int chain[4],length=FindBraid(x,y,chain);
            if (!length) continue;
            for (int i=0;i<length;i++) worldMap[chain[i]/MAP_WIDTH][chain[i]%MAP_WIDTH]=0;
            changed++;
        }
        if(!changed)return;
    }
}

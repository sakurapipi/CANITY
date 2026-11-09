#include "common.h"
#include "map_topology.h"
MapTopology MapTopologyMeasure(const int* cells,int width,int height) {
    MapTopology result={0};unsigned char seen[MAP_WIDTH*MAP_HEIGHT]={0};int queue[MAP_WIDTH*MAP_HEIGHT];int head=0,tail=0;
    if(!cells || width<3 || height<3 || width>MAP_WIDTH || height>MAP_HEIGHT)return result;
    int start=-1;
    for(int y=1;y<height-1;y++)for(int x=1;x<width-1;x++)if(!cells[y*width+x]){
        if(start<0)start=y*width+x;
        result.walkable++;
        int n=(!cells[y*width+x-1])+(!cells[y*width+x+1])+(!cells[(y-1)*width+x])+(!cells[(y+1)*width+x]);
        if(n<=1)result.deadEnds++;
    }
    if(start<0)return result;
    seen[start]=1;queue[tail++]=start;
    while(head<tail){int id=queue[head++];int x=id%width,y=id/width;
        int next[4]={id-1,id+1,id-width,id+width};
        for(int i=0;i<4;i++){int nx=next[i]%width,ny=next[i]/width;
            if(abs(nx-x)+abs(ny-y)!=1 || nx<1 || ny<1 || nx>=width-1 || ny>=height-1)continue;
            if(!cells[next[i]] && !seen[next[i]]){seen[next[i]]=1;queue[tail++]=next[i];}
        }
    }
    result.reachable=tail;result.deadEndLimit=result.walkable/6;if(result.deadEndLimit<8)result.deadEndLimit=8;
    return result;
}
int MapTopologyAccepts(MapTopology value) {return value.walkable>0 && value.walkable==value.reachable && value.deadEnds<=value.deadEndLimit;}

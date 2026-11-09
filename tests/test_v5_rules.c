/* Author-supplied regression cases. Run separately; not an acceptance report.
   gcc tests/test_v5_rules.c src/perception.c src/map_topology.c
       src/room_material.c src/monster_rules.c src/room_template.c
       -Iinclude -std=c99 -lm -o work/test_v5_rules.exe */
#include "common.h"
#include "perception.h"
#include "map_topology.h"
#include "room_material.h"
#include "room_template.h"
#include "monster_rules.h"
#include "entity.h"
/* Only the read-only chase query needs these engine-owned actor arrays. */
Monster monHunters[MON_HUNTER_MAX];
Monster lightBeasts[LIGHTBEAST_MAX];
#define EXPECT(condition) do { if(!(condition)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition);return 1;} } while(0)
int main(void) {
    for(int level=1;level<=100;level++){
        float slow=HunterMoveSpeed(0,level,1,18),fast=HunterMoveSpeed(2,level,1,18);
        EXPECT(fast>slow*1.15f);
        EXPECT(HunterMoveSpeed(2,level,2,18)<=2.25f);
        EXPECT(HunterSpawnDelay(level)>=22);
        if(level>1)EXPECT(HunterSpawnDelay(level)<=HunterSpawnDelay(level-1));
    }
    EXPECT(MonsterFloorChance(SPAWN_LIGHT,1)==0);
    EXPECT(MonsterFloorChance(SPAWN_MIMIC,2)>0);
    EXPECT(MonsterFloorChance(SPAWN_COLLECTOR,2)>=60);
    EXPECT(HunterMoveSpeed(0,1,1,0)<1.4f);
    PerceptionState clear=PerceptionEvaluate(.81f,123),low=PerceptionEvaluate(.19f,123);
    EXPECT(clear.grain==0 && clear.flicker==1 && clear.distortion==0);
    EXPECT(low.grain>clear.grain && low.grain<=PERCEPTION_GRAIN_MAX);
    EXPECT(low.distortion>0 && low.contamination>0);
    for(int i=0;i<1000;i++){
        PerceptionState p=PerceptionEvaluate(i/999.0f,i*.137);
        EXPECT(p.flicker>=.95f && p.flicker<=1);
        EXPECT(p.grain>=0 && p.grain<=PERCEPTION_GRAIN_MAX);
    }
    int grid[MAP_HEIGHT][MAP_WIDTH];
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)grid[y][x]=(x<1 || y<1 || x>=MAP_WIDTH-1 || y>=MAP_HEIGHT-1)?1:0;
    MapTopology topology=MapTopologyMeasure(&grid[0][0],MAP_WIDTH,MAP_HEIGHT);
    EXPECT(MapTopologyAccepts(topology));
    EXPECT(topology.walkable==(MAP_WIDTH-2)*(MAP_HEIGHT-2) && topology.reachable==topology.walkable);
    grid[30][30]=0;grid[29][30]=grid[31][30]=grid[30][29]=grid[30][31]=1;
    EXPECT(!MapTopologyAccepts(MapTopologyMeasure(&grid[0][0],MAP_WIDTH,MAP_HEIGHT)));
    unsigned signatures[7]={0};
    for(int theme=0;theme<7;theme++){
        for(int y=0;y<64;y++)for(int x=0;x<64;x++){
            unsigned sample=RoomMaterialSample(theme,x,y,0);
            EXPECT(sample==RoomMaterialSample(theme,x,y,0));
            signatures[theme]=signatures[theme]*33u+sample;
        }
        for(int other=0;other<theme;other++)EXPECT(signatures[theme]!=signatures[other]);
    }
    /* Every floor cell in every rotated room must connect to its first cell. */
    for(int id=0;id<ROOM_TEMPLATE_COUNT;id++)for(int rotation=0;rotation<4;rotation++){
        RoomTemplate room;EXPECT(RoomTemplateRotate(id,rotation,&room));
        int marked[ROOM_MAX_H][ROOM_MAX_W]={{0}},count=0,first=0;
        for(int y=0;y<room.h;y++)for(int x=0;x<room.w;x++)if(!room.tiles[y][x]){count++;if(!first){marked[y][x]=1;first=1;}}
        for(int pass=0;pass<36;pass++)for(int y=0;y<room.h;y++)for(int x=0;x<room.w;x++)if(marked[y][x]){
            if(x>0 && !room.tiles[y][x-1])marked[y][x-1]=1;
            if(y>0 && !room.tiles[y-1][x])marked[y-1][x]=1;
            if(x+1<room.w && !room.tiles[y][x+1])marked[y][x+1]=1;
            if(y+1<room.h && !room.tiles[y+1][x])marked[y+1][x]=1;
        }
        int reached=0;for(int y=0;y<room.h;y++)for(int x=0;x<room.w;x++)reached+=marked[y][x];
        EXPECT(count>0 && reached==count);
    }
    puts("V5 pure-rule cases passed");return 0;
}

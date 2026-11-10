/* Synthetic append-only layout contracts. NOT historical serializer outputs.
 * Build this authoring tool, then run with an output directory to write fixtures.
 * These files complement, rather than replace, the five authentic old-tag saves. */
#include "common.h"
#include "save.h"
#include <stddef.h>
int main(int argc,char** argv){
    if(argc!=2)return 2;
    static const struct {int version;size_t bytes;} cases[]={
        {35,offsetof(SaveBlob,difficultyPlus1)},
        {36,offsetof(SaveBlob,archetypeRanks)},
        {37,offsetof(SaveBlob,bestiaryExtraKnowledge)},
        {38,offsetof(SaveBlob,metaExpansionRanks)},
        {39,offsetof(SaveBlob,commonSkillRanks)},
        {40,offsetof(SaveBlob,trainingGraduated)},
        {41,offsetof(SaveBlob,serviceUsesV42)},
        {42,offsetof(SaveBlob,decorV43)},
        {43,offsetof(SaveBlob,collectorRunV44)},
        {44,offsetof(SaveBlob,moneyBagPositionsV45)},
        {45,sizeof(SaveBlob)}
    };
    SaveBlob* blob=calloc(1,sizeof(*blob));if(!blob)return 3;
    blob->magic=SAVE_MAGIC;blob->hasRun=1;blob->level=3;
    blob->posX=3.5f;blob->posY=3.5f;blob->dirX=1;blob->dirY=0;
    blob->sanity=61.5f;blob->battery=47;blob->resIndex=1;blob->scale=1;blob->musicVolume=.65f;
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)
        blob->worldMap[y][x]=(x==0 || y==0 || x==MAP_WIDTH-1 || y==MAP_HEIGHT-1);
    blob->exitX=MAP_WIDTH-2.5f;blob->exitY=MAP_HEIGHT-2.5f;blob->levelMod=-1;
    blob->difficultyPlus1=2;blob->archetypeRanks[0]=1;
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        char path[1024];snprintf(path,sizeof(path),"%s/synthetic_v%d.sav",argv[1],cases[i].version);
        blob->version=cases[i].version;
        FILE* f=fopen(path,"wb");if(!f){free(blob);return 4;}
        size_t written=fwrite(blob,1,cases[i].bytes,f);int closed=fclose(f);
        if(written!=cases[i].bytes || closed){free(blob);return 5;}
        printf("v%d %zu %s\n",cases[i].version,cases[i].bytes,path);
    }
    free(blob);return 0;
}

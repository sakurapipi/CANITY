/* Synthetic migration layout fixture, NOT a historical player save.
 * Compile against common.h/save.h from the commit recorded in
 * saves/layout-v31-v32.json, e.g. gcc -I<old-include> this.c -o gen.exe.
 * Run gen.exe output.sav. Historical layouts must not use current headers.
 */
#include "common.h"
#include "save.h"
int main(int argc,char**argv){if(argc!=2)return 2;SaveBlob* b=calloc(1,sizeof(*b));if(!b)return 3;b->magic=SAVE_MAGIC;b->version=SAVE_VERSION;b->hasRun=1;b->level=3;b->posX=3.5f;b->posY=3.5f;b->dirX=1;b->dirY=0;b->sanity=61.5f;b->battery=47;b->resIndex=1;b->scale=1;b->musicVolume=.65f;b->dynamicRes=0;b->gameClearedSave=1;b->volume=.8f;b->gamma=1;b->sens=1;b->fusesNeeded=3;b->nContainers=2;b->cont[0].x=8.5f;b->cont[0].y=8.5f;b->cont[1].x=10.5f;b->cont[1].y=8.5f;b->exitX=MAP_WIDTH-2.5f;b->exitY=MAP_HEIGHT-2.5f;for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)b->worldMap[y][x]=(x==0 || y==0 || x==MAP_WIDTH-1 || y==MAP_HEIGHT-1);FILE*f=fopen(argv[1],"wb");if(!f)return 4;size_t n=fwrite(b,1,sizeof(*b),f);fclose(f);free(b);return n==sizeof(SaveBlob)?0:5;}

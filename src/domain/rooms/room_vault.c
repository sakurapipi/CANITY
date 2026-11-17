/* Paid vault chest pool: no ordinary crate rolls, bonuses or re-roll on load.
 * Ledger uses the current landmark's existing saved integer fields only. */
#include "common.h"
#include "room_theme.h"
#include "map_rooms.h"
#include "hatch.h"
#include "cell_meta.h"
#include "world_api.h"
#include "player_api.h"
#include "loop_api.h"
#include "core_rng.h"
#include "lockbox.h"
#include "entity.h"

#define VAULT_CHEST_CONTENT 11
static RoomProp* Ledger(void) {
    if(!roomTheme.active || roomTheme.theme!=HATCH_STORAGE)return NULL;
    for(int i=0;i<roomPropCount;i++)if(roomProps[i].active &&
       roomProps[i].kind==ROOMPROP_LANDMARK_FIRST+HATCH_STORAGE)return &roomProps[i];
    return NULL;
}
int RoomThemeVaultChest(int index) {
    RoomProp* ledger=Ledger();
    if(!ledger || index<0 || index>=containerCount || trainingMode)return 0;
    Container* c=&containers[index];
    if(!c->active || c->mimicHost || c->mimicShell || c->content==1)return 0;
    if(c->content==VAULT_CHEST_CONTENT)return 1;
    /* Previously spent rooms from older saves keep their original loot rules. */
    if(roomTheme.spent || c->searched)return 0;
    int x=(int)c->x,y=(int)c->y;
    return x>=0 && y>=0 && x<MAP_WIDTH && y<MAP_HEIGHT && cellMeta[y][x].roomId!=0;
}
int RoomThemeVaultReturn(int stake,int roll) {
    if(stake<=0)return 0;
    if(roll<0)roll=0;
    if(roll>4)roll=4;
    long long amount=(long long)stake+(long long)stake*(roll-2)*10/100;
    return amount>INT_MAX?INT_MAX:(int)amount;
}
int RoomThemeVaultPrepare(int stake,unsigned roll) {
    RoomProp* ledger=Ledger();
    if(!ledger || roomTheme.spent || stake<=0)return 0;
    if(ledger->searched>0 && ledger->searchLeft>=80 && ledger->searchLeft<=120 && ledger->searchLeft%10==0)
        return ledger->searched==stake; /* An interrupted prepared ledger never rolls twice. */
    int count=0;
    for(int i=0;i<containerCount;i++)if(RoomThemeVaultChest(i) && !containers[i].searched)count++;
    if(!count)return 0;
    for(int i=0;i<containerCount;i++)if(RoomThemeVaultChest(i) && !containers[i].searched)
        containers[i].content=VAULT_CHEST_CONTENT;
    ledger->searched=stake;
    ledger->searchLeft=80+(int)(roll%5)*10; /* Secret until the chests are opened. */
    return 1;
}
int RoomThemeVaultReward(int index) {
    RoomProp* ledger=Ledger();
    if(!ledger || index<0 || index>=containerCount ||
       containers[index].content!=VAULT_CHEST_CONTENT || !roomTheme.spent ||
       ledger->searched<=0 || ledger->searchLeft<80 || ledger->searchLeft>120 ||
       ledger->searchLeft%10)return -1;
    int count=0,rank=0;
    for(int i=0;i<containerCount;i++)if(containers[i].content==VAULT_CHEST_CONTENT){
        if(i<index)rank++;
        count++;
    }
    if(!count)return -1;
    int total=RoomThemeVaultReturn(ledger->searched,(ledger->searchLeft-80)/10);
    /* Allocate rounding remainder once. Search order never changes total return. */
    return total/count+(rank<total%count);
}

int RoomThemeVaultResult(int* stake,int* total) {
    RoomProp* ledger=Ledger();
    if(!ledger || !roomTheme.spent || ledger->searched<=0 || ledger->searchLeft<80 || ledger->searchLeft>120 || ledger->searchLeft%10)return 0;
    int count=0;
    for(int i=0;i<containerCount;i++)if(containers[i].content==VAULT_CHEST_CONTENT){
        if(!containers[i].searched)return 0;
        count++;
    }
    if(!count)return 0;
    if(stake)*stake=ledger->searched;
    if(total)*total=RoomThemeVaultReturn(ledger->searched,(ledger->searchLeft-80)/10);
    return 1;
}

static int InRoom(float x,float y) {
    int cx=(int)x,cy=(int)y;
    return cx>0 && cy>0 && cx<MAP_WIDTH-1 && cy<MAP_HEIGHT-1 && cellMeta[cy][cx].roomId;
}
static int CratePositionFree(float x,float y) {
    if(worldMap[(int)y][(int)x] || PropBlocksAt((int)x,(int)y))return 0;
    if(Dist2(x,y,roomTheme.landmarkX,roomTheme.landmarkY)<2.25f)return 0;
    if(breaker.active && Dist2(x,y,breaker.x,breaker.y)<1.5f)return 0;
    if(Dist2(x,y,exitX,exitY)<2.25f)return 0;
    for(int i=0;i<roomPropCount;i++)if(roomProps[i].active && Dist2(x,y,roomProps[i].x,roomProps[i].y)<1.0f)return 0;
    for(int i=0;i<hideSpotCount;i++)if(hideSpots[i].active && Dist2(x,y,hideSpots[i].x,hideSpots[i].y)<1.0f)return 0;
    for(int i=0;i<containerCount;i++)if(containers[i].active && Dist2(x,y,containers[i].x,containers[i].y)<.9f)return 0;
    return 1;
}
void RoomThemePopulateVault(void) {
    int count=0;
    for(int i=0;i<containerCount;i++)if(containers[i].active && InRoom(containers[i].x,containers[i].y))count++;
    /* Move non-critical overflow out of the room instead of dropping progression. */
    for(int i=containerCount-1;i>=0 && count>10;i--) {
        Container* c=&containers[i];
        if(!c->active || !InRoom(c->x,c->y) || c->content==1 || c->mimicHost || c->mimicShell)continue;
        int moved=0;
        for(int y=2;y<MAP_HEIGHT-2 && !moved;y++)for(int x=2;x<MAP_WIDTH-2 && !moved;x++)
            if(!InRoom(x+.5f,y+.5f) && CratePositionFree(x+.5f,y+.5f)){c->x=x+.5f;c->y=y+.5f;moved=1;count--;}
    }
    int target=3+(int)(rng_world_next()%8);
    if(target<count)target=count;
    if(target>10)target=10;
    /* Slots stay capped at twelve. Fuse and mimic slots are never repurposed. */
    int spots[MAP_WIDTH*MAP_HEIGHT],n=0;
    for(int y=2;y<MAP_HEIGHT-2;y++)for(int x=2;x<MAP_WIDTH-2;x++)
        if(InRoom(x+.5f,y+.5f) && CratePositionFree(x+.5f,y+.5f))spots[n++]=y*MAP_WIDTH+x;
    for(int i=n-1;i>0;i--){int j=(int)(rng_world_next()%(unsigned)(i+1));int swap=spots[i];spots[i]=spots[j];spots[j]=swap;}
    for(int p=0;p<n && count<target;p++) {
        int slot=containerCount<MAX_CONTAINERS?containerCount:-1;
        if(slot<0)for(int i=containerCount-1;i>=0;i--) {
            Container* c=&containers[i];
            if(c->active && !InRoom(c->x,c->y) && c->content!=1 && !c->searched && !c->mimicHost && !c->mimicShell){slot=i;break;}
        }
        if(slot<0)break;
        int x=spots[p]%MAP_WIDTH,y=spots[p]/MAP_WIDTH;
        if(!CratePositionFree(x+.5f,y+.5f))continue;
        Container* c=&containers[slot];memset(c,0,sizeof(*c));
        c->x=x+.5f;c->y=y+.5f;c->active=1;c->content=VAULT_CHEST_CONTENT;
        c->locked=1;c->lockType=(int)(rng_world_next()%LOCK_TYPE_COUNT);c->lockCode=(int)(rng_world_next()&0xFFF);
        if(slot==containerCount)containerCount++;
        count++;
    }
    for(int i=0;i<containerCount;i++) {
        Container* c=&containers[i];
        if(!c->active || !InRoom(c->x,c->y) || c->searched || c->mimicHost || c->mimicShell)continue;
        c->locked=1;
        if(c->content!=1)c->content=VAULT_CHEST_CONTENT;
    }
}
int RoomThemeVaultBonus(int index) {
    RoomProp* ledger=Ledger();
    if(!ledger || ledger->searched<=0 || index<0 || index>=containerCount || containers[index].content!=VAULT_CHEST_CONTENT)return 0;
    int count=0,rank=0;
    for(int i=0;i<containerCount;i++)if(containers[i].content==VAULT_CHEST_CONTENT){if(i<index)rank++;count++;}
    if(!count)return 0;
    unsigned hash=(unsigned)roomSeedUsed^((unsigned)ledger->searched*2654435761u);
    hash^=hash>>16;hash*=2246822519u;hash^=hash>>13;
    int bits=0,first=(int)(hash%(unsigned)count);
    if(rank==first)bits|=1<<((hash>>8)%4); /* One modest item per complete pool. */
    if(count>1 && (hash>>16)%100<35 && rank==(first+1)%count)bits|=1<<((hash>>22)%4);
    if((hash>>6)%100<6 && rank==(int)((hash>>12)%(unsigned)count))bits|=16;
    return bits;
}

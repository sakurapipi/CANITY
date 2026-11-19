#include "event_runtime.h"
#include "events.h"
#include "audio.h"
#include "cell_meta.h"
#include "entity.h"
#include "item_effects.h"
#include "lang.h"
#include "loop_api.h"
#include "map_rooms.h"
#include "player_api.h"
#include "protocol.h"
#include "state.h"
#include "wallet.h"
#include "world_api.h"

static struct {
    int floor, last, gold, signals, cache, cue;
    uint32_t seed, runSeed, flags;
    float time;
    uint64_t paid;
} runtime;
static unsigned char wet[MAP_HEIGHT][MAP_WIDTH];
static int sites[2], siteCount;
static float batteryLost;
static unsigned generationOffset;

static uint32_t Mix(uint32_t x) {
    x ^= x >> 16; x *= UINT32_C(0x7feb352d);
    x ^= x >> 15; x *= UINT32_C(0x846ca68b);
    return x ^ (x >> 16);
}

void EventResetRun(void) {
    memset(&runtime,0,sizeof runtime);
    runtime.last=runtime.cache=-1;
    levelEvent=-1;
    eventBonus=0;
    generationOffset=0;
    batteryLost=0;
    siteCount=0;
    memset(wet,0,sizeof wet);
}

void EventPlanFloor(int rebuild,int tier,unsigned floorSeed) {
    generationOffset=rebuild?generationOffset+1:0;
    runtime.cache=-1;
    siteCount=0;
    memset(wet,0,sizeof wet);
    if (rebuild && runtime.floor==currentLevel) return;
    int previous=runtime.floor==currentLevel-1?levelEvent:-1;
    runtime.last=previous;
    runtime.floor=currentLevel;
    runtime.flags=0;
    runtime.time=0;
    runtime.gold=runtime.signals=runtime.cue=0;
    runtime.paid=0;
    levelEvent=-1;
    if (!runtime.runSeed) runtime.runSeed=Mix(floorSeed ^ UINT32_C(0x3148a7f2)) | 1u;
    runtime.seed=Mix(runtime.runSeed ^ (uint32_t)currentLevel*UINT32_C(0x9e3779b9) ^ (isEndlessRun?2u:1u));
    if (trainingMode || dailyMode || campaignMode || protocolRun.story || currentLevel<2 ||
        runtime.seed%100u>=EVENT_CHANCE) return;
    int weights[EVENT_COUNT],sum=0;
    for (int id=0;id<EVENT_COUNT;id++) {
        const EventDef *d=EventDefinition(id);
        int allowed=id!=previous && currentLevel>=d->unlock &&
            !(id==EV_BLACKOUT && levelMod==0) &&
            !(id==EV_BLOOD_MOON && tier>=2 && levelMod==1) &&
            !(id==EV_WHISPER && (levelMod==0 || (tier>=2 && levelMod==1)));
        weights[id]=allowed?d->weight:0;
        sum+=weights[id];
    }
    if (!sum) return;
    int roll=(int)(Mix(runtime.seed ^ UINT32_C(0xeb35b487))%(unsigned)sum);
    for (int id=0;id<EVENT_COUNT;id++) {
        roll-=weights[id];
        if (roll<0) { levelEvent=id; return; }
    }
}

/* Auxiliary cells never alter the map grid. Reachability uses player collision. */
static void PopulateSites(int *cacheCell) {
    enum { WIDTH=MAP_WIDTH, COUNT=MAP_WIDTH*MAP_HEIGHT };
    unsigned char visited[COUNT]={0};
    int queue[COUNT],n=0,read=0;
    static const int dx[]={0,0,1,-1},dy[]={1,-1,0,0};
    int sx=(int)posX,sy=(int)posY;
    if (sx<1 || sy<1 || sx>=MAP_WIDTH-1 || sy>=MAP_HEIGHT-1) return;
    int start=sy*WIDTH+sx;
    visited[start]=1;queue[n++]=start;
    uint32_t best=UINT32_MAX;
    while(read<n) {
        int cell=queue[read++],x=cell%WIDTH,y=cell/WIDTH;
        float cx=x+.5f,cy=y+.5f;
        for(int k=0;k<4;k++) {
            int nx=x+dx[k],ny=y+dy[k],next=ny*WIDTH+nx;
            if(nx<1 || ny<1 || nx>=MAP_WIDTH-1 || ny>=MAP_HEIGHT-1 ||
               visited[next] || Blocked(nx+.5f,ny+.5f)) continue;
            visited[next]=1;queue[n++]=next;
        }
        if (Dist2(cx,cy,posX,posY)<16 || Dist2(cx,cy,exitX,exitY)<4 ||
            !SpotClear(cx,cy,1.1f,0,-1)) continue;
        uint32_t rank=Mix(runtime.seed ^ (uint32_t)cell);
        if(rank<best) {best=rank;*cacheCell=cell;}
    }
}

static int WetCellAllowed(int x,int y) {
    return x>=1 && x<MAP_WIDTH-1 && y>=1 && y<MAP_HEIGHT-1 && !worldMap[y][x] &&
           !(x<5 && y<5) && !(abs(x-3)<=2 && abs(y-18)<=2) &&
           Dist2(x+.5f,y+.5f,exitX,exitY)>=4;
}

static int WetAllowed(int x,int y,int axis) {
    if(!WetCellAllowed(x,y) || wet[y][x])return 0;
    return axis==0 ? worldMap[y-1][x] && worldMap[y+1][x] :
                     worldMap[y][x-1] && worldMap[y][x+1];
}

/* A short floor can use connected patches when it runs out of straight corridors. */
static int PlaceFloodBands(void) {
    enum { MIN_WET_CELLS=6 };
    int wetCount=0;
    memset(wet,0,sizeof wet);
    int bands=2+(int)(runtime.seed%3u);
    for(int band=0;band<bands;band++) {
        uint32_t best=UINT32_MAX;
        int startX=0,startY=0,axis=0,length=0;
        for(int y=1;y<MAP_HEIGHT-1;y++)for(int x=1;x<MAP_WIDTH-1;x++) {
            for(int d=0;d<2;d++) {
                if(!WetAllowed(x,y,d))continue;
                int n=1;
                while(n<6 && WetAllowed(x+(d==0?n:0),y+(d==1?n:0),d))n++;
                if(n<3)continue;
                uint32_t rank=Mix(runtime.seed ^ (uint32_t)(2*(y*MAP_WIDTH+x)+d));
                if(rank<best){best=rank;startX=x;startY=y;axis=d;length=n;}
            }
        }
        if(!length)break;
        for(int i=0;i<length;i++)wet[startY+(axis==1?i:0)][startX+(axis==0?i:0)]=1;
        wetCount+=length;
    }
    while(wetCount<MIN_WET_CELLS) {
        uint32_t best=UINT32_MAX;
        int bx=0,by=0,nx=0,ny=0;
        for(int y=1;y<MAP_HEIGHT-1;y++)for(int x=1;x<MAP_WIDTH-1;x++) {
            if(!WetCellAllowed(x,y) || wet[y][x])continue;
            for(int axis=0;axis<2;axis++) {
                int tx=x+(axis==0),ty=y+(axis==1);
                if(!WetCellAllowed(tx,ty) || wet[ty][tx])continue;
                uint32_t rank=Mix(runtime.seed ^ (uint32_t)(2*(y*MAP_WIDTH+x)+axis));
                if(rank<best){best=rank;bx=x;by=y;nx=tx;ny=ty;}
            }
        }
        if(!bx)break;
        wet[by][bx]=wet[ny][nx]=1;
        wetCount+=2;
    }
    return wetCount>=MIN_WET_CELLS;
}

void EventPlaceFloor(void) {
    int cell=-1;
    siteCount=0;
    if(levelEvent==EV_FLOOD && !PlaceFloodBands() && !(runtime.flags&ACTIVATED)) {
        levelEvent=-1;
        memset(wet,0,sizeof wet);
    }
    if (levelEvent==EV_SUPPLY && !(runtime.flags&CACHE_CLAIMED) && (!(runtime.flags&ACTIVATED) || generationOffset) && containerCount<MAX_CONTAINERS) {
        if (runtime.cache<0) {
            PopulateSites(&cell);
            if (cell>=0) {
                runtime.cache=cell;
                containers[containerCount++]=(Container){.x=cell%MAP_WIDTH+.5f,.y=cell/MAP_WIDTH+.5f,
                                                         .active=1,.content=EVENT_CACHE_CONTENT};
            }
        }
    }
    if(levelEvent==EV_WHISPER || levelEvent==EV_MIRROR) {
        uint32_t best[2]={UINT32_MAX,UINT32_MAX};
        for(int y=2;y<MAP_HEIGHT-2;y++) for(int x=2;x<MAP_WIDTH-2;x++) {
            if(worldMap[y][x] || PropBlocksAt(x,y)) continue;
            int i=y*MAP_WIDTH+x;
            uint32_t rank=Mix(runtime.seed ^ (uint32_t)i);
            if(rank<best[0]) {
                best[1]=best[0];sites[1]=sites[0];best[0]=rank;sites[0]=i;
            } else if(rank<best[1]) {best[1]=rank;sites[1]=i;}
        }
        siteCount=(best[0]!=UINT32_MAX)+(best[1]!=UINT32_MAX);
    }
}

void EventActivateFloor(void) {
    if(levelEvent<0 || (runtime.flags&ACTIVATED))return;
    runtime.flags|=ACTIVATED;
    batteryLost=0;
    if(levelEvent==EV_BLACKOUT) {
        float before=battery;
        battery=floorf(fmaxf(0,battery)*.5f);
        batteryLost=before-battery;
    }
    static const int sound[EVENT_COUNT]={
        SFX_HEARTBEAT,SFX_CRANK,SFX_BREATH,SFX_ECHO,SFX_DRIP,
        SFX_BREATH,SFX_GLASS_CRACK,SFX_POWER,SFX_STEAM,SFX_HUM
    };
    PlaySfx(sound[levelEvent],.34f);
}

float EventElapsed(void){return runtime.time;}
int EventBloodMoon(void){return levelEvent==EV_BLOOD_MOON;}
int EventEntranceProtected(void){return levelEvent>=0 && runtime.time<3;}
int EventWetCell(int x,int y){return (unsigned)x<MAP_WIDTH && (unsigned)y<MAP_HEIGHT && wet[y][x];}
static float GridPhase(void){return fmodf(runtime.time,20);}
static int GridLive(void){return levelEvent==EV_GRID_SURGE && GridPhase()>=8 && GridPhase()<14;}
float EventCrankScale(void){return GridLive()?1.25f:1;}
float EventNoiseScale(NoiseKind kind,float x,float y) {
    if(levelEvent==EV_SILENCE) {
        if(kind==NOISE_WALK)return .60f;
        if(kind==NOISE_SPRINT || kind==NOISE_SEARCH || kind==NOISE_CRANK)return 1.20f;
    }
    if(kind==NOISE_CRANK && GridLive())return 1.35f;
    if(kind==NOISE_SPRINT && levelEvent==EV_FLOOD && EventWetCell((int)x,(int)y))return 1.50f;
    return 1;
}

int EventResidue(int index,float *x,float *y) {
    if ((levelEvent!=EV_WHISPER && levelEvent!=EV_MIRROR) || (unsigned)index>=(unsigned)siteCount ||
        fmodf(runtime.time,15)<7 || fmodf(runtime.time,15)>=10) return 0;
    int cell=sites[index];
    *x=cell%MAP_WIDTH+.5f;*y=cell/MAP_WIDTH+.5f;
    return 1;
}

float EventSanityScale(void) {
    if(levelEvent!=EV_WHISPER)return 1;
    float x,y;
    return EventResidue(0,&x,&y) && Dist2(x,y,posX,posY)<36?1.40f:1.25f;
}

int EventCacheAt(float *x,float *y) {
    if(levelEvent!=EV_SUPPLY || runtime.cache<0 || (runtime.flags&CACHE_CLAIMED))return 0;
    *x=runtime.cache%MAP_WIDTH+.5f;*y=runtime.cache/MAP_WIDTH+.5f;
    return 1;
}

int EventClaimCache(void) {
    if(levelEvent!=EV_SUPPLY || (runtime.flags&CACHE_CLAIMED))return 0;
    if (smokeBombs<BottleCap()) smokeBombs++;
    else if (battery<(float)BatteryMax()-.1f) battery=fminf(BatteryMax(),battery+25);
    else if (flares<BottleCap()) flares++;
    else if (bottles<BottleCap()) bottles++;
    else return 0;
    runtime.flags|=CACHE_CLAIMED;
    MakeNoise(posX,posY,20);
    PlaySfx(SFX_PICKUP,.7f);
    return 1;
}

void EventTick(float dt) {
    if(levelEvent<0 || !isfinite(dt) || dt<=0)return;
    float previous=runtime.time;
    runtime.time=fminf(runtime.time+dt,86400);
    if(levelEvent==EV_GRID_SURGE && fmodf(previous,20)<6 && GridPhase()>=6 && GridPhase()<8) PlaySfx(SFX_POWER,.34f);
    if(levelEvent==EV_SUPPLY && (int)(previous/7)!=(int)(runtime.time/7)) {
        float x,y;if(EventCacheAt(&x,&y))AudioPlayWorld(SFX_ECHO,x,y,.24f,9,1);
    }
    if(levelEvent==EV_WHISPER && fmodf(previous,15)<7) {
        float x,y;if(EventResidue(0,&x,&y))AudioPlayWorld(SFX_BREATH,x,y,.32f,13,1);
    }
    if(levelEvent==EV_FLOOD && (int)(previous*2)!=(int)(runtime.time*2) &&
        sprinting && EventWetCell((int)posX,(int)posY))
        AudioPlayWorld(SFX_DRIP,posX,posY,.34f,5,1);
    if(levelEvent!=EV_GOLD_RESONANCE || runtime.signals>=3)return;
    const float due=fmaxf(25.0f*(runtime.signals+1),runtime.cue+25.0f);
    if(runtime.time>=due-2 && WalletRun()>=350 && hiddenIn<0 && !(runtime.flags&GOLD_WARNED)) {
        runtime.flags|=GOLD_WARNED;
        runtime.cue=(int)ceilf(runtime.time)+2;
        PlaySfx(SFX_HUM,.35f);
    }
    if((runtime.flags&GOLD_WARNED) && runtime.time>=runtime.cue) {
        runtime.flags&=~GOLD_WARNED;
        runtime.cue=(int)runtime.time;
        runtime.signals++;
        if(hiddenIn<0 && WalletRun()>=350)MakeNoise(posX,posY,9);
    }
}

const char *EventPhaseLine(void) {
    static char line[112];
    if(levelEvent==EV_BLACKOUT && runtime.time<2.5f && batteryLost>0) {
        snprintf(line,sizeof line,Tr("ENTRY POWER LOSS: %.0f","入层电量减少 %.0f"),batteryLost);
        return line;
    }
    if(levelEvent==EV_GRID_SURGE && GridLive()) {
        snprintf(line,sizeof line,Tr("SURGE %ds: CHARGE +25%% / NOISE +35%%","增压剩%d秒：回电 +25%% / 噪声 +35%%"),
                 (int)ceilf(14-GridPhase()));
        return line;
    }
    if(levelEvent==EV_GRID_SURGE && GridPhase()>=6 && GridPhase()<8)return Tr("SURGE IN 2 SECONDS","增压即将开始");
    if(levelEvent==EV_GOLD_RESONANCE && (runtime.flags&GOLD_WARNED)) {
        snprintf(line,sizeof line,Tr("GOLD SIGNAL %d / 3: HIDE NOW","金币信号 %d / 3：立即藏身"),runtime.signals+1);
        return line;
    }
    if(levelEvent==EV_WHISPER) {
        float x,y;
        if(EventResidue(0,&x,&y) && Dist2(x,y,posX,posY)<169) {
            float side=(x-posX)*(-dirY)+(y-posY)*dirX;
            float front=(x-posX)*dirX+(y-posY)*dirY;
            return fabsf(front)>fabsf(side)?(front>0?Tr("WHISPER AHEAD","前方传来低语"):Tr("WHISPER BEHIND","后方传来低语")):
                side>0?Tr("WHISPER RIGHT","右侧传来低语"):Tr("WHISPER LEFT","左侧传来低语");
        }
    }
    return NULL;
}

int EventGoldCredit(EventGoldSource source,int amount,int slot) {
    const EventDef *d=EventDefinition(levelEvent);
    if (!d || !d->goldPct || amount<=0 || (unsigned)source>EVENT_EXIT) return 0;
    int tx=source*MAX_CONTAINERS+slot;
    if ((unsigned)slot>=MAX_CONTAINERS || (generationOffset && source<=EVENT_MIMIC)) return 0;
    uint64_t bit=UINT64_C(1)<<tx;
    if(runtime.paid&bit)return 0;
    runtime.paid|=bit;
    int available=EventGoldCap(runtime.floor)-runtime.gold;
    int64_t raw=((int64_t)amount*d->goldPct+50)/100;
    int bonus=raw>available?available:(int)raw;
    runtime.gold+=bonus;
    return bonus;
}

void EventClearFloor(void) {
    const EventDef *event=EventDefinition(levelEvent);
    if(!event || (runtime.flags&CLEARED))return;
    runtime.flags|=CLEARED;
    eventBonus+=event->score;
}

static void Put(unsigned char *out,int index,uint32_t value) {
    for(int i=0;i<4;i++)out[index*4+i]=(unsigned char)(value>>(i*8));
}
static uint32_t Get(const unsigned char *in,int index) {
    uint32_t n=0;for(int i=0;i<4;i++)n|=(uint32_t)in[4*index+i]<<(i*8);
    return n;
}
int EventWireEncode(unsigned char *out) {
    if(runtime.floor!=currentLevel || runtime.floor<=0 || runtime.floor>999)return 0;
    Put(out,0,1);Put(out,1,(uint32_t)runtime.floor);Put(out,2,(uint32_t)(levelEvent+1));
    Put(out,3,(uint32_t)(runtime.last+1));Put(out,4,runtime.seed);Put(out,5,runtime.runSeed);
    Put(out,6,runtime.flags);Put(out,7,(uint32_t)(runtime.time*1000));Put(out,8,(uint32_t)runtime.gold);
    Put(out,9,(uint32_t)eventBonus);Put(out,10,(uint32_t)runtime.paid);Put(out,11,(uint32_t)(runtime.paid>>32));
    Put(out,12,(uint32_t)(runtime.cache+1));
    Put(out,13,(uint32_t)runtime.signals | ((uint32_t)runtime.cue<<2) | ((uint32_t)(generationOffset!=0)<<31u));
    return 1;
}

int EventWireRestore(const void *body,int size,int floor) {
    const unsigned char *p=body;
    EventResetRun();
    batteryLost=0;
    if(!EventWireValid(body,size) || Get(p,1)!=(uint32_t)floor)return 0;
    runtime.floor=floor;levelEvent=(int)Get(p,2)-1;runtime.last=(int)Get(p,3)-1;
    runtime.seed=Get(p,4);runtime.runSeed=Get(p,5);runtime.flags=Get(p,6);
    runtime.time=Get(p,7)/1000.0f;runtime.gold=(int)Get(p,8);eventBonus=(int)Get(p,9);
    runtime.paid=((uint64_t)Get(p,11)<<32)|Get(p,10);runtime.cache=(int)Get(p,12)-1;
    runtime.signals=(int)(Get(p,13)&3);runtime.cue=(int)((Get(p,13)&0x7fffffffu)>>2);
    generationOffset=Get(p,13)>>31;
    return 1;
}

#include "common.h"
#include "event_wire.h"

static uint32_t Word(const unsigned char *p,int index) {
    uint32_t result=0;
    for(int i=0;i<4;i++)result|=(uint32_t)p[4*index+i]<<(8*i);
    return result;
}

int EventGoldCap(int floor) { return 40+10*(floor<12?floor:12); }

int EventWireFloor(const void *body,int size) {
    const unsigned char *p=body;
    if(!p || size!=EVENT_WIRE_BYTES || Word(p,0)!=1)return 0;
    uint32_t floor=Word(p,1),id=Word(p,2),last=Word(p,3),flags=Word(p,6);
    uint32_t time=Word(p,7),gold=Word(p,8),score=Word(p,9),cache=Word(p,12);
    uint32_t signal=Word(p,13),count=signal&3,cue=(signal&0x7fffffffu)>>2;
    uint64_t paid=(uint64_t)Word(p,11)<<32|Word(p,10);
    uint64_t allowed=((UINT64_C(1)<<(3*MAX_CONTAINERS))-1)|
                     (UINT64_C(1)<<(3*MAX_CONTAINERS))|(UINT64_C(1)<<(4*MAX_CONTAINERS));
    if(floor==0 || floor>999 || id>EVENT_COUNT || last>EVENT_COUNT || (id && id==last) ||
       flags>15 || time>86400000u || score>2000000 || gold>(unsigned)EventGoldCap((int)floor) ||
       (paid&~allowed) || (gold>0 && !paid) || cache>MAP_WIDTH*MAP_HEIGHT || cue>86402)
        return 0;
    if(!id && (flags || time || gold || paid || cache || cue || count))return 0;
    if((cache || (flags&CACHE_CLAIMED)) && id!=EV_SUPPLY+1)return 0;
    if((cue || count || (flags&GOLD_WARNED)) && id!=EV_GOLD_RESONANCE+1)return 0;
    if((flags&GOLD_WARNED) && (count>=3 || !cue || cue<time/1000u || cue>time/1000u+3))return 0;
    if(!(flags&GOLD_WARNED) && cue>time/1000u)return 0;
    if(cache) {
        unsigned cell=cache-1,x=cell%MAP_WIDTH,y=cell/MAP_WIDTH;
        if(!x || x>=MAP_WIDTH-1 || !y || y>=MAP_HEIGHT-1)return 0;
    }
    return (int)floor;
}
int EventWireValid(const void *body,int size) { return EventWireFloor(body,size)>0; }

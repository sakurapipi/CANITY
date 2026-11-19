#ifndef CANITY_EVENT_WIRE_H
#define CANITY_EVENT_WIRE_H
enum {
    EVENT_WIRE_BYTES=56,
    ACTIVATED=1, CLEARED=2, CACHE_CLAIMED=4, GOLD_WARNED=8
};
int EventGoldCap(int floor);
int EventWireFloor(const void *body,int size);
int EventWireValid(const void *body,int size);
#endif

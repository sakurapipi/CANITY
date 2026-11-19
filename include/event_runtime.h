#ifndef CANITY_EVENT_RUNTIME_H
#define CANITY_EVENT_RUNTIME_H
#include "common.h"

enum { EVENT_CACHE_CONTENT = 11, EVENT_WIRE_BYTES = 56 };
typedef enum { EVENT_SEARCH, EVENT_CRATE, EVENT_MIMIC, EVENT_BREAKER, EVENT_EXIT } EventGoldSource;

void EventResetRun(void);
void EventPlanFloor(int rebuild, int tier, unsigned floorSeed);
void EventPlaceFloor(void);
void EventActivateFloor(void);
void EventTick(float dt);
void EventClearFloor(void);
int EventGoldCredit(EventGoldSource source, int amount, int slot);
int EventClaimCache(void);
int EventWetCell(int x, int y);
float EventNoiseScale(int walking, int sprinting, float x, float y);
float EventCrankScale(void);
float EventCrankNoiseScale(void);
float EventSanityScale(void);
int EventBloodMoon(void);
int EventEntranceProtected(void);
float EventElapsed(void);
int EventResidue(int index, float *x, float *y);
int EventCacheAt(float *x, float *y);
const char *EventPhaseLine(void);
int EventWireEncode(unsigned char *out);
int EventWireValid(const void *body, int size);
int EventWireRestore(const void *body, int size, int floor);
#endif

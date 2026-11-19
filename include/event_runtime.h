#ifndef CANITY_EVENT_RUNTIME_H
#define CANITY_EVENT_RUNTIME_H
#include "common.h"
#include "entity.h"
#include "event_wire.h"

enum { EVENT_CACHE_CONTENT = 11 };
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
float EventNoiseScale(NoiseKind kind, float x, float y);
float EventCrankScale(void);
float EventSanityScale(void);
int EventBloodMoon(void);
int EventEntranceProtected(void);
float EventElapsed(void);
int EventResidue(int index, float *x, float *y);
int EventCacheAt(float *x, float *y);
const char *EventPhaseLine(void);
int EventWireEncode(unsigned char *out);
int EventWireRestore(const void *body, int size, int floor);
#endif

#ifndef CANITY_EVOLUTION_SAVE_H
#define CANITY_EVOLUTION_SAVE_H
#include "monster_evolution.h"
#include "tactical_tools.h"
#define EVOLUTION_WIRE_BYTES 792
#define TACTICAL_WIRE_BYTES 348
typedef struct {
    int floor, tier;
    float plan;
    EvolutionState state;
} EvolutionSnapshot;
int EvolutionWireEncode(const EvolutionSnapshot *value, void *bytes, int cap);
int EvolutionWireDecode(EvolutionSnapshot *value, const void *bytes, int length);
int TacticalWireEncode(const TacticalState *state, const Shot *shots, void *bytes, int cap);
int TacticalWireDecode(TacticalState *state, Shot *shots, const void *bytes, int length);
int EvolutionChunkValid(unsigned short id, unsigned short schema, const void *bytes, int length);
#endif

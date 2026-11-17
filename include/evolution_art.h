#ifndef CANITY_EVOLUTION_ART_H
#define CANITY_EVOLUTION_ART_H
#include "common.h"
void DrawEvolutionPortrait(int bestiary, int variant, int tier, RECT box, int identified);
const char *EvolutionGuideText(int bestiary, int tier);
void SpritesSetEvolutionTier(int tier);
#endif

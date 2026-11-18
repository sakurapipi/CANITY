#ifndef CANITY_RENDER_STORY_H
#define CANITY_RENDER_STORY_H
#include "render.h"
#include "story_visual.h"

enum { STORY_DRAW_ENTRY=24, STORY_DRAW_COUNT=STORY_DECOR_PER_CHAPTER };
int PrepareStoryFloorTrail(unsigned char *tiles);
int CollectStoryVisuals(SpriteDrawEntry *entries,int n,StoryDecoration *visuals);
void DrawStoryVisual(const StoryDecoration *visual);
#endif

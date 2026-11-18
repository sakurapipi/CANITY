#ifndef CANITY_STORY_LAYOUT_H
#define CANITY_STORY_LAYOUT_H
#include "story_visual.h"

typedef struct { int x,y; } Cell;
enum { SITE_ENTRY, SITE_TERMINAL, SITE_POWER, SITE_REFUGE,
       SITE_SERVICE, SITE_RETURN, SITE_RECORD, SITE_EXIT, SITE_COUNT };
const Cell *StorySite(int floor,int site);
void StoryLayOut(int floor);
int StoryMaterial(int floor);
int StoryDecorationAt(int floor,int chapter,int index,StoryDecoration *out);

#endif

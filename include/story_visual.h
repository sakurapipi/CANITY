#ifndef CANITY_STORY_VISUAL_H
#define CANITY_STORY_VISUAL_H

enum { STORY_DECOR_PER_CHAPTER=4, STORY_DECOR_CHAPTERS=3 };
typedef struct { float x,y,height; int model; } StoryDecoration;

int StoryGuidePrepare(unsigned char *tiles);
void StoryGuideInvalidate(void);
int ProtocolDecoration(int index,StoryDecoration *out);
#endif

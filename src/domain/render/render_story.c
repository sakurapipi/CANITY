#include "common.h"
#include "render_story.h"
#include "protocol.h"
#include "sprites.h"
#include "world_api.h"
#include "state.h"
#include "loop_api.h"
#include "present_api.h"
#include "calibration.h"
#include "lockbox.h"
#include "tactical_tools.h"

int PrepareStoryFloorTrail(unsigned char *tiles) {
    return protocolRun.story && currentState==STATE_PLAYING && !ProtocolCinematicActive() &&
        !isMapOpen && !calibActive && !lockActive && hiddenIn<0 && !exitChoiceOpen &&
        !TacticalPanelOpen() && StoryGuidePrepare(tiles);
}

int CollectStoryVisuals(SpriteDrawEntry *entries,int n,StoryDecoration *visuals) {
    if (!ProtocolPeacefulStory()) return n;
    int count=0;
    for (int i=0;i<STORY_DECOR_PER_CHAPTER && n<MAX_SPRITES;i++) {
        StoryDecoration visual;
        if (!ProtocolDecoration(i,&visual)) continue;
        visuals[count]=visual;
        entries[n++]=(SpriteDrawEntry){Dist2(posX,posY,visual.x,visual.y),STORY_DRAW_ENTRY,count++};
    }
    return n;
}

void DrawStoryVisual(const StoryDecoration *visual) {
    DrawBakedPropArt(SPR_STORY_DECOR_FIRST+visual->model,visual->x,visual->y,0,visual->height,1.38f);
}

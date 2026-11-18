#include "common.h"
#include "render.h"
#include "sprites.h"
#include "entity.h"
#include "player_api.h"

void DrawDecoyModel(int idx) {
    if ((unsigned)idx>=MAX_DECOY || !decoysArr[idx].active) return;
    const Decoy *d=&decoysArr[idx];
    if (!HasLOS(posX,posY,d->x,d->y)) return;
    float accent=d->armed?fmaxf(0,d->shoutT/DECOY_SHOUT_EVERY-.72f):0;
    DrawBakedPropArt(d->armed?SPR_DECOY_ARMED:SPR_DECOY_FOLDED,d->x,d->y,0,
                     d->armed?.66f:.46f,1+accent*.55f);
}

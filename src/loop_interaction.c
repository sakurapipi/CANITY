/* Target selection only; input edges are consumed by the caller. */
#include "common.h"
#include "tutorial.h"
#include "interaction.h"
#include "protocol.h"
#include "entity.h"
#include "state.h"
#include "world_api.h"
#include "present_api.h"
#include "hatch.h"
#include "map_rooms.h"
#include "room_theme.h"

/* Devices cannot be operated through the enclosing room wall. */
static int RoomDeviceVisible(float x,float y) {
    float dx=x-posX,dy=y-posY;
    int steps=(int)(sqrtf(dx*dx+dy*dy)*12)+1;
    if(steps>64)return 0;
    for(int i=1;i<=steps;i++) {
        int cx=(int)floorf(posX+dx*i/steps),cy=(int)floorf(posY+dy*i/steps);
        if(cx<0 || cy<0 || cx>=MAP_WIDTH || cy>=MAP_HEIGHT || worldMap[cy][cx])return 0;
    }
    return 1;
}

void InteractionRefreshTargets(void) {
    hoverKind = 0;
    hoverIndex = -1;
    if (protocolRun.story) {
        int device=ProtocolNearestDevice();
        if (device>=0) {hoverKind=INTERACT_STORY_DEVICE;hoverIndex=device;return;}
    }
    /* The saved hatch controller is co-located with the central mechanism. */
    float deviceDistance=INTERACT_RANGE*INTERACT_RANGE;
    for(int i=0;i<hatchCount;i++)if(RoomThemeIsCurrent(i) && hatches[i].state==HATCH_SEALED && RoomDeviceVisible(hatches[i].x,hatches[i].y)) {
        float d=Dist2(posX,posY,hatches[i].x,hatches[i].y);
        if(d<deviceDistance){deviceDistance=d;hoverKind=4;hoverIndex=i;}
    }
    for(int i=0;i<roomPropCount;i++)if(roomProps[i].active && RoomPropIsControl(roomProps[i].kind) &&
        (roomProps[i].kind-ROOMPROP_CONTROL_FIRST)/2!=HATCH_BRICK && hatchCount &&
        hatches[0].state==HATCH_SEALED && RoomDeviceVisible(roomProps[i].x,roomProps[i].y)) {
        float d=Dist2(posX,posY,roomProps[i].x,roomProps[i].y);
        if(d<deviceDistance){deviceDistance=d;hoverKind=3;hoverIndex=i;}
    }

    if (hoverKind==0 && !breakerOpen && breaker.active && (!guideMode || tutorialStep>=TUT_BREAKER)) {
        float d = Dist2(posX, posY, breaker.x, breaker.y);
        if (d < INTERACT_RANGE * INTERACT_RANGE) {
            hoverKind = 2;
            hoverIndex = -1;
        }
    }

    if (hoverKind == 0) {
        float bestD = INTERACT_RANGE * INTERACT_RANGE;
        int bestLocked = -1;      /* 锁箱候选，先记下来垫底 */
        float bestLockedD = 0.0f;
        for (int i = 0; i < containerCount; i++) {
            if (!containers[i].active || containers[i].searched) continue;
            float d = Dist2(posX, posY, containers[i].x, containers[i].y);
            if (d >= INTERACT_RANGE * INTERACT_RANGE) continue;
            if (containers[i].locked) {
                if (bestLocked < 0 || d < bestLockedD) { bestLocked = i; bestLockedD = d; }
            } else if (d < bestD) {
                bestD = d; hoverKind = 1; hoverIndex = i;
            }
        }
        /* 没有未上锁的箱子、但有锁箱 → 才轮到锁箱 */
        if (hoverKind == 0 && bestLocked >= 0) {
            hoverKind = 1;
            hoverIndex = bestLocked;
        }
    }

    /* ==================================================================
     * 2026-11-06：**家具也可以搜**
     * ------------------------------------------------------------------
     * 用户提议：「把它做成一个箱子，特殊箱子可以搜查两次，
     *           或者那个货架可以搜查三次。」
     *
     * 这也是这次"精灵过多导致掉帧"的补偿：每个家具只画 1 个精灵，
     * 但内容量由可搜查次数承担 ——
     *   货架 3 次 / 木箱堆 2 次 / 控制台 2 次
     *
     * 优先级：**永远排在容器之后**，家具不许抢箱子的 E。
     * ================================================================ */
    /* 2026-11-06：舱门（主题控制器）—— **排在最后**，不抢箱子和家具的 E */
    if (hoverKind == 0) {
        int hi = HatchNearest(posX, posY, INTERACT_RANGE * INTERACT_RANGE);
        if (hi >= 0 && hatches[hi].state == HATCH_SEALED &&
            (!RoomThemeIsCurrent(hi) || RoomDeviceVisible(hatches[hi].x,hatches[hi].y))) {
            hoverKind = 4;
            hoverIndex = hi;
        }
    }

    if (hoverKind == 0) {
        
        
        float bestD = INTERACT_RANGE * INTERACT_RANGE;
        for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++) {
            if (!roomProps[i].active || roomProps[i].searchLeft <= 0 || RoomPropIsLandmark(roomProps[i].kind)) continue;
            if(RoomPropIsControl(roomProps[i].kind) && (roomProps[i].kind-ROOMPROP_CONTROL_FIRST)/2==HATCH_BRICK)continue;
            if(RoomPropIsControl(roomProps[i].kind) &&
               (!hatchCount || hatches[0].state!=HATCH_SEALED || !RoomDeviceVisible(roomProps[i].x,roomProps[i].y)))continue;
            float d = Dist2(posX, posY, roomProps[i].x, roomProps[i].y);
            if (d >= INTERACT_RANGE * INTERACT_RANGE) continue;
            if (d < bestD) { bestD = d; hoverKind = 3; hoverIndex = i; }
        }
    }


}

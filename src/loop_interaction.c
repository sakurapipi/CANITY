/* Target selection only; input edges are consumed by the caller. */
#include "common.h"
#include "tutorial.h"
#include "interaction.h"
#include "entity.h"
#include "state.h"
#include "world_api.h"
#include "present_api.h"
#include "hatch.h"
#include "map_rooms.h"
#include "room_theme.h"

void InteractionRefreshTargets(void) {
    hoverKind = 0;
    hoverIndex = -1;
    /* Special rooms use the existing hatch target (kind 4), never a prop target. */

    if (!breakerOpen && breaker.active && (!guideMode || tutorialStep>=TUT_BREAKER)) {
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
        if (hi >= 0 && hatches[hi].state == HATCH_SEALED) {
            hoverKind = 4;
            hoverIndex = hi;
        }
    }

    if (hoverKind == 0) {
        
        
        float bestD = INTERACT_RANGE * INTERACT_RANGE;
        for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++) {
            if (!roomProps[i].active || roomProps[i].searchLeft <= 0) continue;
            float d = Dist2(posX, posY, roomProps[i].x, roomProps[i].y);
            if (d >= INTERACT_RANGE * INTERACT_RANGE) continue;
            if (d < bestD) { bestD = d; hoverKind = 3; hoverIndex = i; }
        }
    }


}

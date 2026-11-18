#include "tactical_tools.h"
/* 2026-11-16：从 render.c 拆出的**精灵绘制队列收集**（域内解耦，行为不变）。 */
#include "protocol.h"
#include "threat_telegraph.h"
#include "perception.h"
#include "room_material.h"
#include "collapse.h"
#include "echo_marks.h"
#include "item_effects.h"
#include "upgrade_run.h"
#include "warden.h"
#include "wrench.h"
#include "control_effect.h"
#include "cell_meta.h"
#include "suspicion.h"
#include "art.h"
#include "bench.h"
#include "weapons.h"
#include "extras.h"
#include "loop_api.h"
#include "present_api.h"
#include "common.h"
#include "render.h"
#include "ui.h"
#include "sprites.h"
#include "entity.h"
#include "mimic.h"
#include "map_rooms.h"
#include "room_theme.h"
#include "hatch.h"
#include "spider.h"
#include "phase.h"
#include "player_api.h"
#include "world_api.h"
#include "lang.h"
#include "state.h"
#include "plat.h"



int CollectSpriteScenery(SpriteDrawEntry* list, int n) {
    for (int i = 0; i < containerCount; i++) {
        if (!containers[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(containers[i].x, containers[i].y, posX, posY);
        list[n].kind = 0;
        list[n].idx = i;
        n++;
    }
    /* v3.3：藏身点必须真的进渲染列表。
       原来这里没有插入，case 12 却去画 hideSpots[idx]，
       而 kind=12 又被 Extras 占用 → 垃圾桶画在错误的位置/根本不画，
       玩家只看到随机出现的 HIDE 提示。 */
    for (int i = 0; i < hideSpotCount; i++) {
        if (!hideSpots[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(hideSpots[i].x, hideSpots[i].y, posX, posY);
        list[n].kind = 12;
        list[n].idx = i;
        n++;
    }

    /* 2026-11-05：宝箱怪也走渲染列表 —— 这样它和箱子/怪共享同一套
       深度排序与光照，不会出现"穿墙显示"或"永远在最上面"。 */
    for (int i = 0; i < mimicCount; i++) {
        if (mimics[i].state == MIM_NONE || mimics[i].state == MIM_GONE) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(mimics[i].x, mimics[i].y, posX, posY);
        list[n].kind = 15;
        list[n].idx = i;
        n++;
    }
    /* v2.4-rc：房间家具也走同一个渲染列表，共享深度排序与光照 */
    {


        for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++) {
            if (!roomProps[i].active) continue;
            if(RoomPropIsControl(roomProps[i].kind) && (roomProps[i].kind-ROOMPROP_CONTROL_FIRST)/2==HATCH_BRICK)continue;
            if (n >= MAX_SPRITES) break;
            list[n].d2 = Dist2(roomProps[i].x, roomProps[i].y, posX, posY);
            list[n].kind = 16;
            list[n].idx = i;
            n++;
        }
    }
    /* 2026-11-06：舱门也走同一个渲染列表（共享深度排序与光照） */
    {
        for (int i = 0; i < hatchCount && i < MAX_HATCHES; i++) {
            if (!hatches[i].active || RoomThemeIsCurrent(i)) continue;
            if (n >= MAX_SPRITES) break;
            list[n].d2 = Dist2(hatches[i].x, hatches[i].y, posX, posY);
            list[n].kind = 17;
            list[n].idx = i;
            n++;
        }
    }
    return n;
}

int CollectSpriteActorsAndMarkers(SpriteDrawEntry* list, int n) {
    for (int i=0;i<PROTOCOL_DEVICE_COUNT && n<MAX_SPRITES;i++) {
        float x,y;
        int state=i==3?ProtocolAbilityMarker(&x,&y):ProtocolProp(i,&x,&y);
        if (!state) continue;
        list[n].d2=Dist2(x,y,posX,posY);list[n].kind=20;list[n].idx=i;n++;
    }
    float impactX,impactY;
    if (n<MAX_SPRITES && TacticalJammerEffect(&impactX,&impactY)>0) {
        list[n].d2=Dist2(impactX,impactY,posX,posY);list[n].kind=21;list[n].idx=0;n++;
    }
    if (breaker.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(breaker.x, breaker.y, posX, posY);
            list[n].kind = 1;
            list[n].idx = 0;
            n++;
        }
    }
    if (Hunter0()->active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(Hunter0()->x, Hunter0()->y, posX, posY);
            list[n].kind = 2;
            list[n].idx = 0;
            n++;
        }
    }
    /* v2.4.12：第二只听声怪也进渲染列表 */
    {

        if (Hunter1()->active && n < MAX_SPRITES) {
            list[n].d2 = Dist2(Hunter1()->x, Hunter1()->y, posX, posY);
            list[n].kind = 13;
            list[n].idx = 0;
            n++;
        }
        /* 第三只（120 秒后出现） */
        if (Hunter2()->active && n < MAX_SPRITES) {
            list[n].d2 = Dist2(Hunter2()->x, Hunter2()->y, posX, posY);
            list[n].kind = 14;
            list[n].idx = 0;
            n++;
        }
    }
    if (safeRoom.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(safeRoom.x, safeRoom.y, posX, posY);
            list[n].kind = 7;
            list[n].idx = 0;
            n++;
        }
    }
    if (lightBeast.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(lightBeast.x, lightBeast.y, posX, posY);
            list[n].kind = 5;
            list[n].idx = 0;
            n++;
        }
    }

    /* ==================================================================
     * 2026-11-06（用户要求）：**缚丝蛛的世界模型**
     * ------------------------------------------------------------------
     * 用户原话：「蜘蛛怪的建模什么的全部都没有」。
     * 它之前只有逻辑、没有入渲染列表 —— 玩家完全看不见。
     * ================================================================ */
    {


        if (SpiderActive()) {
            if (n < MAX_SPRITES) {
                float sx = SpiderX(), sy = SpiderY();
                list[n].d2 = Dist2(sx, sy, posX, posY);
                list[n].kind = 19;
                list[n].idx = 0;
                n++;
            }
        }
    }

    /* 2026-11-06：**相位信标的世界模型**（用户要求） */
    {


        if (phasePlaced) {
            if (n < MAX_SPRITES) {
                list[n].d2 = Dist2(phaseX, phaseY, posX, posY);
                list[n].kind = 18;
                list[n].idx = 0;
                n++;
            }
        }
    }

    /* 2026-11-06：第二只追光怪 */
    if (lightBeast2.active) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(lightBeast2.x, lightBeast2.y, posX, posY);
            list[n].kind = 5;          /* 同一种精灵 */
            list[n].idx = 1;           /* idx=1 = 第二只（case 5 里按 idx 分流） */
            n++;
        }
    }    /* v1.8 偷电怪 */
    return n;
}

int CollectSpriteEffectsAndExit(SpriteDrawEntry* list, int n) {
    for (int i = 0; i < LEECH_MAX; i++) {
        if (!leeches[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(leeches[i].x, leeches[i].y, posX, posY);
        list[n].kind = 9;
        list[n].idx = i;
        n++;
    }
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(thrown[i].x, thrown[i].y, posX, posY);
        list[n].kind = 6;
        list[n].idx = i;
        n++;
    }
    /* 飞行中的玻璃瓶 */
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!shot[i].active) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(shot[i].x, shot[i].y, posX, posY);
        list[n].kind = 8;
        list[n].idx = i;
        n++;
    }
    /* v2.0 额外威胁（影子/盲螨/回响） */
    for (int i = 0; i < ExtrasCount(); i++) {
        const Extra* e = ExtraAt(i);
        if (!e || !e->active) continue;
        /* 影子在开灯时不渲染 */
        if (e->kind == 0 && flashlightOn && battery > 0.0f) continue;
        if (n >= MAX_SPRITES) break;
        list[n].d2 = Dist2(e->x, e->y, posX, posY);
        list[n].kind = 11;
        list[n].idx = i;
        n++;
    }

    /* v2.9 剑气 */
    if (slashActive) {
        if (n < MAX_SPRITES) {
            list[n].d2 = Dist2(slashX, slashY, posX, posY);
            list[n].kind = 10;
            list[n].idx = 0;
            n++;
        }
    }
    /* 跳脸虫不在这个表里 —— 它是屏幕空间的 overlay，
       要盖住整个场景，由 RenderFrame 直接绘制。 */
    if (n < MAX_SPRITES) {
        list[n].d2 = Dist2(exitX, exitY, posX, posY);
        list[n].kind = 3;
        list[n].idx = 0;
        n++;
    }

    return n;
}


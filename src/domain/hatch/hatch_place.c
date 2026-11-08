/* src/domain/hatch/hatch_place.c —— 舱门：标题 / 门槛 / 效果 / 放置
 *
 * 第 3 轮（2026-11-08）从 game.c 原样搬过来，逻辑一行没改。
 * 运行时状态机仍在 src/hatch.c（HatchTick 等）；本文件只管放置与文案。
 * 卡片：docs/modules/hatch.md
 */
#include "common.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "entity.h"
#include "lang.h"
#include "hatch.h"
#include "map_rooms.h"    /* roomProps / roomPropCount */
#include "wallet.h"       /* WalletSpendRun */
#include "cell_meta.h"
#include "audio.h"         /* PlaySfx / SFX_POWER */
#include "collapse.h"

float hatchLightsT   = 0.0f;   /* 供电室：全灯清雾剩余秒数 */
float hatchGoldMulT  = 0.0f;   /* 仓储室：金币 x1.5 剩余秒数 */
float hatchBloodT    = 0.0f;   /* 病房  ：血迹追踪剩余秒数 */
float hatchSpeedT    = 0.0f;   /* 地狱  ：移速 +25% 剩余秒数 */

/* A-9 2026-11-06：舱门的标题（提示行用） */
const char* HatchTitle(int idx) {
    if (idx < 0 || idx >= hatchCount) return "";
    switch (hatches[idx].kind) {
    case HATCH_BOILER:  return (g_lang == LANG_ZH) ? "总闸" : "MASTER VALVE";
    case HATCH_STORAGE: return (g_lang == LANG_ZH) ? "仓门" : "STORAGE LOCK";
    case HATCH_POWER:   return (g_lang == LANG_ZH) ? "总闸" : "MAIN BREAKER";
    case HATCH_WARD:    return (g_lang == LANG_ZH) ? "医疗台" : "MED BAY";
    case HATCH_BRICK:   return (g_lang == LANG_ZH) ? "破墙锤" : "SLEDGE";
    case HATCH_HELL:    return (g_lang == LANG_ZH) ? "祭坛" : "ALTAR";
    case HATCH_FLESH:   return (g_lang == LANG_ZH) ? "肉堆" : "FLESH PILE";
    default: return "";
    }
}

/* ==================================================================
 * 门槛判定 —— 用户要求：「特殊事件一定要有门槛」
 * ------------------------------------------------------------------
 * 返回值：1 = 可以开；0 = 不行，why 里写清**具体缺什么**。
 * 文案必须给数字（"你有 87 金"），不能只说"条件不足"。
 * ================================================================ */
/* ==================================================================
 * 2026-11-06（用户要求）：**说清楚拉下去会发生什么**
 * ------------------------------------------------------------------
 * 用户原话：「里面那个主题房间的控制闸也不是特别明显，
 *           就是不知道拉了有什么用，感觉效果都差不多，
 *           而且没什么特点」。
 *
 * 实测七种效果**完全不一样**（塌方重掷 / 买断整仓 / 通电 / 医疗 /
 * 破墙 / 血祭加速 / 全场静止），问题是玩家看不到。
 * 这里给每种闸门一句**后果**描述，悬停时显示在标题下面一行。
 * ================================================================ */
const char* HatchEffectLine(int idx) {
    if (idx < 0 || idx >= hatchCount) return "";
    int zh = (g_lang == LANG_ZH);
    switch (hatches[idx].kind) {
    case HATCH_BOILER:  return zh ? "拉下 -> 塌方：本层地图重掷" : "PULL -> COLLAPSE: REROLL FLOOR";
    case HATCH_STORAGE: return zh ? "拉下 -> 买断整仓：家具重填 + 60 秒金币翻倍" : "PULL -> BUY OUT: REFILL + 60s GOLD x2";
    case HATCH_POWER:   return zh ? "拉下 -> 全厂通电 20 秒" : "PULL -> POWER ON FOR 20s";
    case HATCH_WARD:    return zh ? "拉下 -> 回满理智与体力，但留下 60 秒血迹" : "PULL -> HEAL, BUT LEAVE A 60s BLOOD TRAIL";
    case HATCH_BRICK:   return zh ? "拉下 -> 砸穿一堵墙（消耗 25 体力）" : "PULL -> SMASH A WALL (-25 STAMINA)";
    case HATCH_HELL:    return zh ? "拉下 -> 血祭：30 秒加速，代价是 15 理智" : "PULL -> PACT: 30s SPEED, -15 SANITY";
    case HATCH_FLESH:   return zh ? "拉下 -> 全场敌人静止 20 秒（消耗 1 个投掷物）" : "PULL -> ALL ENEMIES FROZEN 20s (-1 THROWABLE)";
    default: return "";
    }
}

int HatchGateCheck(int idx, char* why, int cap) {
    if (idx < 0 || idx >= hatchCount) return 0;
    const Hatch* h = &hatches[idx];
    if (why && cap > 0) why[0] = 0;

    switch (h->kind) {
    case HATCH_BOILER: {
        /* 门槛：本房家具全部搜空 + 手上 >=1 根保险丝 */
        int left = 0;
        for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++)
            if (roomProps[i].active && roomProps[i].searchLeft > 0) left++;
        if (left > 0) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "先把这间房搜空（还剩 %d 件）" : "SEARCH OUT THIS ROOM (%d LEFT)", left);
            return 0;
        }
        if (fusesHeld < 1) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "需要 1 根保险丝" : "NEED 1 FUSE");
            return 0;
        }
        return 1;
    }
    case HATCH_STORAGE: {
        if (carryGold < HATCH_STORAGE_COST) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "买断整仓需要 %d 金（你有 %d）" : "NEED %d GOLD (YOU HAVE %d)",
                HATCH_STORAGE_COST, carryGold);
            return 0;
        }
        return 1;
    }
    case HATCH_POWER: {
        if (fusesHeld < 2 && battery < 60.0f) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "需要 2 根保险丝，或电量 >= 60（现在 %.0f）"
                                    : "NEED 2 FUSES OR 60%% BATTERY (NOW %.0f)", battery);
            return 0;
        }
        return 1;
    }
    case HATCH_WARD: {
        if (sanity >= 50.0f) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "理智还有 %.0f，用不上" : "SANITY %.0f - NOT HURT ENOUGH", sanity);
            return 0;
        }
        return 1;
    }
    case HATCH_BRICK: {
        /* 扳手是蓄力制、没有次数上限，所以门槛换成体力（挥大锤要力气） */
        if (stamina < 60.0f) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "体力不够（%.0f/60）" : "NOT ENOUGH STAMINA (%.0f/60)", stamina);
            return 0;
        }
        return 1;
    }
    case HATCH_HELL: {
        if (carryGold < 100) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "献祭需要 100 金（你有 %d）" : "NEED 100 GOLD (YOU HAVE %d)", carryGold);
            return 0;
        }
        if (sanity < 40.0f) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "理智太低（%.0f/40）" : "SANITY TOO LOW (%.0f/40)", sanity);
            return 0;
        }
        return 1;
    }
    case HATCH_FLESH: {
        int n = flares + bottles + smokeBombs + gluePots + echoBolts;
        if (n <= 0) {
            if (why) snprintf(why, (size_t)cap,
                (g_lang == LANG_ZH) ? "需要 1 个投掷物" : "NEED 1 THROWABLE");
            return 0;
        }
        return 1;
    }
    default:
        return 0;
    }
}

/* ==================================================================
 * 效果结算 —— 由 HatchTick 在**开门动画播完的那一帧**调用（AD-27）
 * ================================================================ */
void HatchFireEffect(int idx) {
    if (idx < 0 || idx >= hatchCount) return;
    Hatch* h = &hatches[idx];

    switch (h->kind) {
    case HATCH_BOILER:
        /* 崩塌：本层重掷。真正的重掷在下一轮的黑屏流程里消费这个请求。 */
        /* 立刻进入崩塌流程（黑屏 -> 重掷 -> 淡入）。
           注意：效果是在**开门动画播完那一帧**才调到这里（AD-27）。 */
        MakeNoise(h->x, h->y, 30.0f);
        CollapseBegin();
        break;

    case HATCH_STORAGE:
        if (!WalletSpendRun(HATCH_STORAGE_COST)) return;
        {
            int refilled = 0;
            for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++) {
                if (!roomProps[i].active) continue;
                roomProps[i].searchLeft = (roomProps[i].kind == ROOMPROP_SHELF) ? 3 : 2;
                roomProps[i].searched = 0;
                refilled++;
            }
            for (int i = 0; i < containerCount && i < MAX_CONTAINERS; i++) {
                if (!containers[i].active) continue;
                containers[i].searched = 0;
                refilled++;
            }
            hatchGoldMulT = 60.0f;
            pickupMsg = (g_lang == LANG_ZH) ? "整仓买断了！60 秒内金币翻倍" : "WHOLE CRATE BOUGHT - 60s GOLD BOOST";
            pickupMsgTimer = 2.0f;
            (void)refilled;
        }
        MakeNoise(h->x, h->y, 12.0f);
        break;

    case HATCH_POWER:
        if (fusesHeld >= 2) fusesHeld -= 2; else battery -= 25.0f;
        if (battery < 0.0f) battery = 0.0f;
        hatchLightsT = 20.0f;
        pickupMsg = (g_lang == LANG_ZH) ? "全厂通电 —— 20 秒" : "POWER ON - 20 SECONDS";
        pickupMsgTimer = 2.0f;
        MakeNoise(h->x, h->y, 18.0f);
        break;

    case HATCH_WARD:
        sanity += 40.0f;
        if (sanity > SanityMax()) sanity = SanityMax();
        stamina = StaminaMax();
        hatchBloodT = 60.0f;      /* 留下血迹：60 秒内怪能追你 */
        pickupMsg = (g_lang == LANG_ZH) ? "包扎好了（但留了血迹）" : "PATCHED UP (BUT YOU BLED)";
        pickupMsgTimer = 2.0f;
        break;

    case HATCH_BRICK: {
        stamina -= 25.0f;
        if (stamina < 0.0f) stamina = 0.0f;
        /* 在门周围 8 格里挑一堵"背后是空地"的墙打穿 */
        int ex = (int)h->x, ey = (int)h->y, carved = 0;
        static const int dx8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
        static const int dy8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        for (int k = 0; k < 8 && !carved; k++) {
            int wx = ex + dx8[k], wy = ey + dy8[k];
            if (wx < 1 || wy < 1 || wx >= MAP_WIDTH - 1 || wy >= MAP_HEIGHT - 1) continue;
            if (!worldMap[wy][wx]) continue;                 /* 要墙 */
            int bx = wx + dx8[k], by = wy + dy8[k];
            if (bx < 1 || by < 1 || bx >= MAP_WIDTH - 1 || by >= MAP_HEIGHT - 1) continue;
            if (worldMap[by][bx]) continue;                  /* 墙后要空地 */
            worldMap[wy][wx] = 0;
            carved = 1;
        }
        pickupMsg = carved
            ? ((g_lang == LANG_ZH) ? "墙开了！" : "THE WALL GAVE WAY!")
            : ((g_lang == LANG_ZH) ? "这里砸不开" : "NOTHING GIVES HERE");
        pickupMsgTimer = 2.0f;
        MakeNoise(h->x, h->y, 30.0f);
        break;
    }

    case HATCH_HELL:
        if (!WalletSpendRun(100)) return;
        sanity -= 15.0f;
        if (sanity < 0.0f) sanity = 0.0f;
        hatchSpeedT = 30.0f;
        pickupMsg = (g_lang == LANG_ZH) ? "血祭生效 —— 30 秒" : "THE PACT HOLDS - 30 SECONDS";
        pickupMsgTimer = 2.0f;
        MakeNoise(h->x, h->y, 16.0f);
        break;

    case HATCH_FLESH: {
        /* 消耗一个投掷物 */
        if (flares > 0) flares--;
        else if (bottles > 0) bottles--;
        else if (smokeBombs > 0) smokeBombs--;
        else if (gluePots > 0) gluePots--;
        else if (echoBolts > 0) echoBolts--;
        /* 全图怪静止 20 秒 —— 复用现成的 stun 字段，不新增 AI 钩子 */
        for (int i = 0; i < MON_HUNTER_MAX; i++) {

            if (monHunters[i].active) monHunters[i].stun = 20.0f;
        }
        {
            if (lightBeast.active) lightBeast.stun = 20.0f;
        }
        pickupMsg = (g_lang == LANG_ZH) ? "它开始进食了…20 秒" : "IT IS FEEDING... 20 SECONDS";
        pickupMsgTimer = 2.0f;
        break;
    }

    default:
        break;
    }
    PlaySfx(SFX_POWER, 0.7f);
}

int HatchPlaceInRoom(int fx, int fy, int w, int h, int themeId) {
    /* 崩塌层**不生成舱门** —— 否则可以在崩塌层里再触发一次崩塌，
       无限重掷刷金币。这不是防玩家，是防机制本身。 */
    int i;
    if (collapsedLevel) return -1;
    int kind = HatchKindForTheme(themeId);
    if (kind < 0) return -1;

    /* ==================================================================
     * 2026-11-07（v2.4.2 / B3）：**同层同类舱门只放一扇**
     * ------------------------------------------------------------------
     * hatchUsedMask 是按 HatchKind 存的（一扇门一位）。同层出现两扇同类型
     * 舱门时，「只用过其中一扇」这个事实没法表达 —— 读档会把两扇都标成已用。
     *
     * 实测频率（tools/hatch_stat.c，链接真实生成路径，300 种子）：
     *   · 真实关卡路径：每层 1 扇，同类重复 0%
     *   · 强制两层房间：只有 1.7% 的层能放下两扇，其中 60% 是同类
     *   => 约 1% 的层会踩中。频率不高，但一旦踩中就是**静默错判**。
     *
     * 为什么选择"跳过"而不是"给舱门分配实例 ID"：
     *   实例 ID 要求房间放置可确定重放（读档时按同一种子重建顺序），
     *   那是 v35 的结构性改造（RoomInstance[]）。在本期用最小代价
     *   先消除歧义：同层已有一扇同类门时，这间房就不再放第二扇。
     *   代价是约 1% 的层少一扇门，且那扇门本来就和已有的重复。
     * ================================================================ */
    for (i = 0; i < hatchCount; i++) {
        if (hatches[i].kind == kind) return -1;   /* 同类已存在，不叠第二扇 */
    }

    /* 第一优先：角落（同时在两条边上），不挡过道 */
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int cx = fx + x, cy = fy + y;
                if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) continue;
                if (worldMap[cy][cx]) continue;                 /* 要站得住 */
                int edgeX = (x == 0 || x == w - 1);
                int edgeY = (y == 0 || y == h - 1);
                int corner = edgeX && edgeY;
                int edge = edgeX || edgeY;
                if (pass == 0 && !corner) continue;             /* 先角落 */
                if (pass == 1 && (corner || !edge)) continue;   /* 再贴墙 */

                /* 别和家具重叠 */
                int clash = 0;
                for (int i = 0; i < roomPropCount && i < MAX_ROOM_PROPS; i++) {
                    if (!roomProps[i].active) continue;
                    if ((int)roomProps[i].x == cx && (int)roomProps[i].y == cy) { clash = 1; break; }
                }
                if (clash) continue;

                return HatchAdd((float)cx + 0.5f, (float)cy + 0.5f, kind, themeId);
            }
        }
    }
    return -1;
}

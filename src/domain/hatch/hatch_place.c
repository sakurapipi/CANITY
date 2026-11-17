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
#include "room_theme.h"

float hatchLightsT   = 0.0f;   /* 供电室：全灯清雾剩余秒数 */
float hatchGoldMulT  = 0.0f;   /* 仓储室：金币 x1.5 剩余秒数 */
float hatchBloodT    = 0.0f;   /* 病房  ：血迹追踪剩余秒数 */
float hatchSpeedT    = 0.0f;   /* 地狱  ：移速 +25% 剩余秒数 */

/* A-9 2026-11-06：舱门的标题（提示行用） */
const char* HatchTitle(int idx) {
    if(idx<0 || idx>=hatchCount)return "";
    return RoomThemeName(hatches[idx].kind,g_lang==LANG_ZH);
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
const char* HatchEffectLine(int idx){return RoomThemeEffectLine(idx);}
int HatchGateCheck(int idx,char* why,int cap){return RoomThemeCanActivate(idx,why,cap);}
int HatchFireEffect(int idx){return RoomThemeActivate(idx);}

int HatchPlaceInRoom(int fx, int fy, int w, int h, int themeId) {
    /* 崩塌层**不生成舱门** —— 否则可以在崩塌层里再触发一次崩塌，
       无限重掷刷金币。这不是防玩家，是防机制本身。 */
    int i;
    if (collapsedLevel || hatchCount>0) return -1;
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

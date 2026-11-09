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
const char* HatchEffectLine(int idx){
    static const char* en[7]={"ECHO EXCHANGE: 20 battery; footsteps originate here for 45s.","DEBT WINDOW: 100 gold; loot +35%, noise +25% for 75s.","LUMEN OVERLOAD: 25 battery; reveal routes for 35s; a loud beacon draws hunters.","EQUAL TRANSPLANT: trade battery for sanity, or sanity for battery.","RETURN SWITCH: 40 stamina; swap with a nearby hunter and stagger it.","MADNESS COVENANT: 100 gold; run-long pollution, loot +25%, noise +35%.","BORROWED TIME: one throwable; hunters freeze 20s, then the room screams."};
    static const char* zh[7]={"声源置换：消耗20电量，45秒内脚步声从此处传出。","债务窗口：消耗100金币，75秒内搜刮+35%、噪音+25%。","光源过载：消耗25电量，35秒揭示路线，巨响引来听声鬼。","等价移植：电量较多时换理智，理智较多时换电量。","折返换位：消耗40体力，与附近听声鬼换位并使其硬直。","疯癫契约：消耗100金币，本局精神污染、搜刮+25%、噪音+35%。","借来的时间：消耗1投掷物，猎手静止20秒，随后房间发出尖啸。"};
    if(idx<0 || idx>=hatchCount)return "";
    int kind=hatches[idx].kind;return kind>=0 && kind<7?(g_lang==LANG_ZH?zh[kind]:en[kind]):"";
}

int HatchGateCheck(int idx,char* why,int cap){
    if(idx<0 || idx>=hatchCount)return 0;
    int kind=hatches[idx].kind,ok=0;const char* message="";
    switch(kind){
    case HATCH_BOILER:ok=battery>=20;message=g_lang==LANG_ZH?"需要20电量":"NEED 20 BATTERY";break;
    case HATCH_STORAGE:case HATCH_HELL:ok=WalletCanPayRun(100);message=g_lang==LANG_ZH?"需要100局内金币":"NEED 100 RUN GOLD";break;
    case HATCH_POWER:ok=battery>=25;message=g_lang==LANG_ZH?"需要25电量":"NEED 25 BATTERY";break;
    case HATCH_WARD:ok=(sanity<SanityMax()-1 && battery>=20)||(battery<BatteryMax()-1 && sanity>=25);message=g_lang==LANG_ZH?"需要可交换的资源及恢复空间":"NEED SPARE SANITY OR BATTERY";break;
    case HATCH_BRICK:ok=stamina>=40;message=g_lang==LANG_ZH?"需要40体力及附近可换位的听声鬼":"NEED 40 STAMINA AND A NEARBY HUNTER";
        if(ok){ok=0;for(int i=0;i<MON_HUNTER_MAX;i++)if(monHunters[i].active && Dist2(posX,posY,monHunters[i].x,monHunters[i].y)>=4 && Dist2(posX,posY,monHunters[i].x,monHunters[i].y)<=100 && !Blocked(monHunters[i].x,monHunters[i].y))ok=1;}break;
    case HATCH_FLESH:ok=flares+bottles+smokeBombs+gluePots+echoBolts>0;message=g_lang==LANG_ZH?"需要1个投掷物":"NEED ONE THROWABLE";break;
    }
    if(!ok && why && cap>0)snprintf(why,(size_t)cap,"%s",message);
    return ok;
}

/* ==================================================================
 * 效果结算 —— 由 HatchTick 在**开门动画播完的那一帧**调用（AD-27）
 * ================================================================ */
void HatchFireEffect(int idx){
    if(!HatchGateCheck(idx,NULL,0))return;
    Hatch* h=&hatches[idx];
    switch(h->kind){
    case HATCH_BOILER:battery-=20;RoomThemeStartEvent(0,45);MakeNoise(h->x,h->y,20);break;
    case HATCH_STORAGE:if(!WalletSpendRun(100))return;RoomThemeStartEvent(1,75);MakeNoise(h->x,h->y,18);break;
    case HATCH_POWER:
        battery-=25;hatchLightsT=35;RoomThemeStartEvent(2,35);
        for(int y=1;y<MAP_HEIGHT-1;y++)for(int x=1;x<MAP_WIDTH-1;x++)if(!worldMap[y][x])explored[y][x]=1;
        MakeNoise(h->x,h->y,32);break;
    case HATCH_WARD:
        if(sanity/SanityMax()<battery/fmaxf(1,BatteryMax()) && battery>=20){battery-=20;sanity=fminf(SanityMax(),sanity+35);}
        else if(sanity>=25 && battery<BatteryMax()-1){sanity-=20;battery=fminf((float)BatteryMax(),battery+50);}
        else {battery-=20;sanity=fminf(SanityMax(),sanity+35);}break;
    case HATCH_BRICK:{
        int best=-1;float distance=101;
        for(int i=0;i<MON_HUNTER_MAX;i++){float d=Dist2(posX,posY,monHunters[i].x,monHunters[i].y);if(monHunters[i].active && d>=4 && d<distance && !Blocked(monHunters[i].x,monHunters[i].y)){best=i;distance=d;}}
        if(best<0)return;
        Monster* m=&monHunters[best];float x=posX,y=posY;posX=m->x;posY=m->y;m->x=x;m->y=y;m->stun=2.5f;m->pathLen=m->pathIdx=0;m->repath=0;stamina-=40;MakeNoise(x,y,22);break;
    }
    case HATCH_HELL:if(!WalletSpendRun(100))return;RoomThemeSetMadness(1);MakeNoise(h->x,h->y,24);break;
    case HATCH_FLESH:
        if(flares>0)flares--;else if(bottles>0)bottles--;else if(smokeBombs>0)smokeBombs--;else if(gluePots>0)gluePots--;else echoBolts--;
        MonsterStunAll(20);RoomThemeStartEvent(6,20);break;
    }
    PlaySfx(SFX_POWER,.7f);pickupMsg=g_lang==LANG_ZH?"主题回路已激活":"ROOM CIRCUIT ACTIVE";pickupMsgTimer=1.5f;
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

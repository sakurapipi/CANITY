/* Bilingual descriptions are shared by HUD prompts and the actual interaction. */
#include "common.h"
#include "room_theme.h"
#include "room_archetype.h"
#include "map_rooms.h"
#include "hatch.h"
#include "entity.h"
#include "lang.h"
#include "player_api.h"
#include "world_api.h"
#include "loop_api.h"
#include "run_economy.h"
#include "present_api.h"
#include "skills.h"
static const char* roomControlEn[7][2]={
        {"TUNE THE TRANSMITTER; COST PAID AT THE CENTRAL DEVICE", "ALIGN THE RETURN PIPE; THEN ACTIVATE THE CENTRAL DEVICE"},
        {"AUTHORIZE: 25% CARRIED GOLD (MIN 40) / VAULT POOL 80-120% RETURN / NOISE +25%", "CANCEL AUTHORIZATION; NO GOLD HAS BEEN PAID"},
        {"CHARGE THE LEFT TOWER", "CHARGE THE RIGHT TOWER; THEN USE THE CENTRAL DEVICE"},
        {"SELECT: 20 BATTERY -> UP TO 35 SANITY", "SELECT: 20 SANITY -> UP TO 50 BATTERY (25 SAN REQUIRED)"},
        {"CALIBRATE LEFT SWAP RELAY", "CALIBRATE RIGHT SWAP RELAY"},
        {"ARM IRREVERSIBLE COVENANT: 35% CARRIED GOLD (MIN 80), LOOT +25%, NOISE +35%", "CANCEL COVENANT; NO COST UNTIL THE CENTRAL DEVICE OPENS"},
        {"WIND THE MECHANICAL HEART", "ARM THE TRAY; THE CENTER CONSUMES ONE TOOL"}
    };
static const char* roomControlZh[7][2]={
        {"校准发声管，消耗由中央装置统一结算", "校准回声管，完成后可启动中央装置"},
        {"确认风险：携带金币25%（至少40），独立奖池回款80%至120%，噪音+25%", "取消确认，目前没有扣除金币"},
        {"给左侧电塔充电", "给右侧电塔充电，完成后启动中央装置"},
        {"选择：20电量换最多35理智", "选择：20理智换最多50电量，需保有25理智"},
        {"校准左侧换位装置", "校准右侧换位装置"},
        {"确认不可撤销的契约：携带金币35%（至少80），收益+25%、噪音+35%", "取消确认，中央装置打开前不会扣费"},
        {"为机械心脏上弦", "开启献祭托盘，中央装置启动时扣除一个道具"}
    };

static const char* roomEffectEn[7]={
        "20 BATTERY / 45s: SOUND DECOY ATTRACTS HUNTERS WITHIN 28 CELLS, EVEN AFTER YOU LEAVE.",
        "25% CARRIED GOLD (MIN 40): VAULT CHESTS RETURN 80-120% OF STAKE IN TOTAL. 40% LOSS / 20% EVEN / 40% PROFIT.",
        "25 BATTERY / REVEAL ROUTES 35s / FIRST 15s: MINIMAP USES NO BATTERY. THE TOWER IS LOUD.",
        "CHOOSE ONE SIDE POD, THEN ACTIVATE THE CENTER TO EXCHANGE RESOURCES.",
        "40 STAMINA: SWAP WITH THE SELECTED SAFE HUNTER; STAGGER 5s / 6-28 CELLS / ONCE PER FLOOR. NO CAMERA ROTATION.",
        "IRREVERSIBLE THIS RUN: 35% CARRIED GOLD (MIN 80) / POLLUTION / LOOT +25% / NOISE +35%.",
        "1 THROWABLE: SOUND AND LIGHT HUNTERS STOP FOR 20s; THE HEART THEN SCREAMS AND DRAWS ATTENTION."
    };
static const char* roomEffectZh[7]={
        "20电量：井口持续45秒诱导二十八格内猎手；离开后仍有效，替你制造逃离窗口。",
        "携带金币25%（至少40）：独立宝箱总回款为本金80%至120%；四成亏损、两成回本、四成盈利。",
        "25电量：35秒揭示路线，前15秒小地图免耗电；电塔巨响会暴露位置。",
        "在两侧装置二选一，再启动中央装置完成资源转换。",
        "40体力：与安全目标交换位置，使其硬直5秒，六至二十八格，每层一次，不改变视角。",
        "本局不可撤销：携带金币35%（至少80），持续精神污染、搜刮+25%、噪音+35%。",
        "一个投掷物：听声鬼和追光鬼停滞20秒，结束时心脏尖啸引来注意。"
    };

const char* RoomThemeGuideEffect(int theme){
    return theme>=0 && theme<7?(g_lang==LANG_ZH?roomEffectZh[theme]:roomEffectEn[theme]):"";
}
const char* RoomThemeGuideControl(int theme,int side){
    if(theme<0 || theme>=7 || side<0 || side>1)return "";
    return g_lang==LANG_ZH?roomControlZh[theme][side]:roomControlEn[theme][side];
}
const char* RoomThemeFixtureLine(int index) {
    if(index<0 || index>=roomPropCount || !RoomPropIsControl(roomProps[index].kind))return "";
    int theme=(roomProps[index].kind-ROOMPROP_CONTROL_FIRST)/2;
    int side=(roomProps[index].kind-ROOMPROP_CONTROL_FIRST)%2;
    if(theme<0 || theme>=7)return "";
    static char status[384];
    const char* action=g_lang==LANG_ZH?roomControlZh[theme][side]:roomControlEn[theme][side];
    if(roomTheme.spent)return Tr("SPENT / NO SECOND ACTIVATION", "已结算，不可再次启动");
    if(roomProps[index].searched) {
        snprintf(status,sizeof(status),Tr("READY / %s / ACTIVATE THE CENTRAL DEVICE TO COMMIT", "已校准 / %s / 请在中央装置启动生效"),action);
        return status;
    }
    return action;
}
const char* RoomThemeEffectLine(int index) {
    if(index<0 || index>=hatchCount)return "";
    int kind=hatches[index].kind;
    if(kind<0 || kind>=7)return "";
    static char dynamic[320];
    if(kind==HATCH_POWER && roomTheme.spent && RoomThemeFreeMapRemaining()>0) {
        snprintf(dynamic,sizeof(dynamic),Tr("GRID LINK / MINIMAP FREE %.1fs / FLASHLIGHT STILL DRAINS", "电网链路 / 小地图免耗电剩余%.1f秒 / 手电正常耗电"),(double)RoomThemeFreeMapRemaining());
        return dynamic;
    }
    if (kind==HATCH_POWER && roomTheme.spent && RoomThemeMapRevealed()) {
        snprintf(dynamic,sizeof dynamic,Tr("FULL MAP REVEALED %.1fs / NORMAL BATTERY DRAIN",
                                           "全图揭示剩余%.1f秒 / 小地图正常耗电"),
                 (double)RoomThemeEventRemaining());
        return dynamic;
    }
    if (kind==HATCH_FLESH && roomTheme.spent && RoomThemeEventKind()==HATCH_FLESH) {
        snprintf(dynamic,sizeof dynamic,Tr("HUNTERS STALLED %.1fs / SCREAM ON EXPIRY",
                                           "猎手停滞剩余%.1f秒 / 结束时会发出尖啸"),
                 (double)RoomThemeEventRemaining());
        return dynamic;
    }
    if (kind==HATCH_HELL && RoomThemeMadness())
        return Tr("COVENANT ACTIVE FOR THE RUN / LOOT +25% / NOISE +35%",
                  "本局契约已生效 / 搜刮+25% / 噪音+35%");
    if(kind==HATCH_STORAGE && roomTheme.spent) {
        int stake=0,total=0;
        if(RoomThemeVaultResult(&stake,&total)) {
            snprintf(dynamic,sizeof(dynamic),Tr("SETTLED / STAKE %d / RETURN %d / NET %+d", "已结算 / 本金%d / 总回款%d / 净盈亏%+d"),stake,total,total-stake);
            return dynamic;
        }
        int left=0;
        for(int i=0;i<containerCount;i++)if(RoomThemeVaultChest(i) && !containers[i].searched)left++;
        snprintf(dynamic,sizeof(dynamic),Tr("INVESTMENT COMMITTED / %d VAULT CHESTS LEFT / NO ORDINARY BONUSES", "投资已锁定 / 剩余%d个密库箱 / 不叠加普通开箱奖励"),left);
        return dynamic;
    }
    if(hatches[index].state==HATCH_FIRED || roomTheme.spent)
        return Tr("SPENT / ONCE PER FLOOR / EFFECT ALREADY COMMITTED", "已使用 / 每层仅一次 / 效果已结算");
    if(kind==HATCH_STORAGE || kind==HATCH_HELL) {
        snprintf(dynamic,sizeof(dynamic),Tr("COST NOW %d GOLD (%d%%, MIN %d) / %s", "当前费用%d金币（%d%%，至少%d） / %s"),
                 RoomThemeGoldCost(kind,carryGold),kind==HATCH_STORAGE?25:35,
                 kind==HATCH_STORAGE?40:80,kind==HATCH_STORAGE?Tr("VAULT POOL: TOTAL 80-120% / NOISE +25%","独立奖池总回款80%至120% / 噪音+25%"):Tr("RUN-LONG LOOT +25% / NOISE +35%","本局收益+25% / 噪音+35%"));
        return dynamic;
    }
    if(kind==HATCH_WARD && RoomThemeIsCurrent(index) && roomTheme.choice) {
        return roomTheme.choice==1?Tr("SELECTED: -20 BATTERY / UP TO +35 SANITY", "已选择：消耗20电量，恢复最多35理智"):
                                  Tr("SELECTED: -20 SANITY / UP TO +50 BATTERY", "已选择：消耗20理智，恢复最多50电量");
    }
    if(kind==HATCH_BRICK) {
        int target=RoomThemeSwapTarget();
        if(target>=0) {
            float angle=atan2f(monHunters[target].y-posY,monHunters[target].x-posX)-atan2f(dirY,dirX);
            while(angle>3.141593f)angle-=6.283186f;
            while(angle< -3.141593f)angle+=6.283186f;
            const char* direction=fabsf(angle)<.40f?Tr("AHEAD","前方"):(angle<0?Tr("LEFT","左侧"):Tr("RIGHT","右侧"));
            snprintf(dynamic,sizeof(dynamic),Tr("TARGET %d / %s / %.1f CELLS / 40 STAMINA / SWAP DANGER", "目标%d / %s / %.1f格 / 40体力 / 注意换位风险"),
                     target+1,direction,sqrtf(Dist2(posX,posY,monHunters[target].x,monHunters[target].y)));
            return dynamic;
        }
    }
    return RoomThemeGuideEffect(kind);
}

void RoomThemeGuideStep(int step,const char **label,float *progress,int *alternate) {
    static const char *const en[7][2]={
        {"Tune transmitter","Tune return pipe"},{"Authorize stake","Cancel authorization"},
        {"Charge left tower","Charge right tower"},{"Battery to sanity","Sanity to battery"},
        {"Calibrate left relay","Calibrate right relay"},{"Arm the covenant","Cancel authorization"},
        {"Wind the heart","Arm the offering tray"}
    };
    static const char *const zh[7][2]={
        {"校准发声管","校准回声管"},{"确认投资","取消确认"},
        {"左塔充电","右塔充电"},{"电量换理智","理智换电量"},
        {"校准左侧","校准右侧"},{"确认契约","取消确认"},
        {"为心脏上弦","开启献祭托盘"}
    };
    int theme=roomTheme.theme;
    *alternate=0;*progress=0;*label="";
    if ((unsigned)theme>=7 || (unsigned)step>=3 || !hatchCount) return;
    if (step==2) {
        *label=Tr("Start central device","启动中央装置");
        *progress=roomTheme.spent?1:hatches[0].state==HATCH_OPENING?.70f+.29f*HatchOpen01(0):0;
        if(!*progress && holdingNow && hoverKind==4) *progress=fminf(.68f,holdTimer/(SEARCH_TIME*RunSearchTimeMul()*BenchSearchTimeMul())*.68f);
        return;
    }
    *label=(g_lang==LANG_ZH?zh:en)[theme][step];
    if ((theme==HATCH_WARD && roomTheme.choice && roomTheme.choice!=step+1) ||
        (step==1 && (theme==HATCH_STORAGE || theme==HATCH_HELL))) *alternate=1;
    *progress=((roomTheme.prepared>>step)&1u)?1:0;
    if (roomTheme.spent && !*alternate && theme!=HATCH_WARD) *progress=1;
    if (!*progress && holdingNow && hoverKind==3 && hoverIndex>=0 && hoverIndex<roomPropCount &&
        roomProps[hoverIndex].kind==ROOMPROP_CONTROL_FIRST+theme*2+step)
        *progress=fminf(.98f,holdTimer/(SEARCH_TIME*RunSearchTimeMul()*BenchSearchTimeMul()));
}

const char *RoomThemeNextStep(void) {
    if (roomTheme.spent) return Tr("Activated","已启动");
    if (roomTheme.theme==HATCH_WARD && !roomTheme.choice)
        return Tr("Choose either side pod","任选一侧转换舱");
    if (hatches[0].state==HATCH_OPENING) return Tr("Starting the center","中央装置启动中");
    for (int step=0;step<3;step++) {
        const char *label;
        float progress;
        int alternate;
        RoomThemeGuideStep(step,&label,&progress,&alternate);
        if (!alternate && progress<1.f) return label;
    }
    return Tr("Start central device","启动中央装置");
}

/* src/loop_player.c —— 玩家数值（2026-11-09，从 game.c 拆出）
 *
 * 依赖的 modBatteryMul 等乘法器已声明在 player_api.h。
 */
#include "common.h"
#include "entity.h"
#include "state.h"
#include "player_api.h"
#include "bench.h"
#include "world_api.h"
#include "daily.h"
#include "campaign.h"
#include "skills.h"
#include "relics.h"
#include "upgrade_run.h"

int FlashlightUseAllowed(void) {
    /* ==================================================================
     * 2026-11-06（**P1-1**）：**SK_SALVAGE 不再永久禁灯**
     * ------------------------------------------------------------------
     * 原来这里是 `!(DailyForbidLight() || HasKeystone(SK_SALVAGE))` ——
     * 于是买过「抢救」的玩家**永远**开不了手电，新开局、教程、
     * 每日挑战一起被禁，而且没有任何办法关掉。
     *
     * 用户拍板的方向：**默认摸黑，但允许主动开**。
     * 所以这里只剩下"每日限制"这一个真正的禁令；
     * SALVAGE 的价值改由「开局默认关灯 + 降噪 25%」承担（见 skills.c）。
     * ================================================================ */
    return !DailyForbidLight();
}

float BatteryDrainRate(void) {
    float eventMul=isfinite(modBatteryMul) && modBatteryMul>0?modBatteryMul:1;
    float rate=BATTERY_DRAIN*SkillEcoMul()*RelicBatteryMul()*eventMul*BenchFlashDrainMul()*RunUpgFlashMul();
    return isfinite(rate)?fmaxf(BATTERY_DRAIN*.05f,rate):BATTERY_DRAIN;
}   /* v2.7 省电 */

int EnergyMax(void) { return (int)ENERGY_MAX; }

float SanityMax(void) {
    float m = 100.0f + RelicMaxSanAdd()+BenchSanityAdd();
    if (m < 25.0f) m = 25.0f;
    return m;
}

float SafeHealRate(void) {
    return SAFE_HEAL_RATE * (1.0f + SkillRestAdd() + RelicHealAdd())*(HasKeystone(SK_LASTRESORT)?0.5f:1.0f) * BenchHealMul();   /* REST + IRON LUNG */
}


int BatteryMax(void) {
    /* v2.2：SAVER 的 "cap -5" 代价现在真的生效 */
    /* ==================================================================
     * 2026-11-06（用户要求）：基础上限 100 -> 150
     * ------------------------------------------------------------------
     * 地图从 24x24 扩到 36x36 之后，一层要走的距离大约翻了一倍，
     * 原来的 100 电量撑不到图另一头 —— 手电变成"前一半能用"。
     * 同时保留所有加成/惩罚链（技能、遗物、每日、改造树）不变。
     * ================================================================ */
    int m = (int)((150 + SkillBatteryAdd() + RunBatteryCapDelta() + 30*relicOwned[3]) * DailyBatteryCapMul() * BenchBatteryCapMul());
    if (m < 30) m = 30;
    return m;
}

float SanityDrainRate(void) {
    float base = isEndlessRun ? ENDLESS_SANITY_DRAIN : SANITY_DRAIN;
    int lv = currentLevel > 0 ? currentLevel - 1 : 0;
    float d = base + SANITY_DRAIN_STEP * (float)lv;
    if (d > SANITY_DRAIN_CAP) d = SANITY_DRAIN_CAP;
    return d;
}

int BottleCap(void) {
    int cap = 4 + RunUpgCarryBonus();
    if (cap < 1) cap = 1;
    /* v6 **补上 return** —— 这里原来漏了，非 void 函数落空是未定义行为。
       25 个调用点（商店 6 个货位、箱子掉落、技能判定）都拿它当上限，
       返回垃圾值会造成「钱扣了但东西没给」或「上限形同虚设」。 */
    return cap;
}

float StaminaMax(void) {
    float m = 100.0f + (float)RunStaminaAdd();
    return m*BenchStaminaMul();
}

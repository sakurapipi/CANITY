/* src/loop_player.c —— 玩家数值（2026-11-09，从 game.c 拆出）
 *
 * 依赖的 modBatteryMul 等乘法器已声明在 player_api.h。
 */
#include "common.h"
#include "entity.h"
#include "state.h"
#include "player_api.h"
#include "bench.h"
#include "skills.h"
#include "relics.h"
#include "upgrade_run.h"

float BatteryDrainRate(void) { return BATTERY_DRAIN * BenchBatterySaver() * SkillEcoMul() * RelicBatteryMul() * modBatteryMul * BenchFlashDrainMul() * RunUpgFlashMul(); }   /* v2.7 省电 */

int EnergyMax(void) { return BenchEnergyMax((int)ENERGY_MAX); }   /* v2.7：弹匣/电池库/嗜血 */

float SanityMax(void) {
    float m = 100.0f + RelicMaxSanAdd()+BenchSanityAdd();
    if (m < 25.0f) m = 25.0f;
    return m;
}

float SafeHealRate(void) {
    return SAFE_HEAL_RATE * (1.0f + SkillRestAdd() + RelicHealAdd())*(HasKeystone(SK_LASTRESORT)?0.5f:1.0f);   /* REST + IRON LUNG */
}

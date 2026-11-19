#include "common.h"
#include "throwing.h"
#include "player_api.h"
#include "state.h"
#include "skills.h"
#include "world_api.h"

/* ==================================================================
 * 2026-11-06（v2.4.1 第 10 轮）：**生物荧光**
 * ------------------------------------------------------------------
 * 持有 IR_BIOLUM 时，落到地上的照明棒会缓慢恢复附近玩家的理智。
 *
 * 三个限制缺一不可（否则道具流可以无限续航）：
 *   ① 单根只在**前 8 秒**有效 —— 灯亮 25 秒，但治疗时间更短
 *   ② 单根最多回 12 点
 *   ③ 每层总预算 24 点
 *
 * 用 "elapsed = FLARE_LIFE - life" 判断有效窗口，
 * 不需要给每根灯加状态字段（thrown[] 是存档结构，不能随便扩）。
 * 多根重叠时**只按最近的一根**结算，不叠加速率。
 * ================================================================== */
static float s_floorFlareHealed = 0.0f;

void FlareHealReset(void) { s_floorFlareHealed = 0.0f; }
float FlareHealBudgetLeft(void) { return FLARE_HEAL_FLOOR_BUDGET - s_floorFlareHealed; }
void FlareHealRestoreBudget(float remaining) {
    s_floorFlareHealed = FLARE_HEAL_FLOOR_BUDGET - fminf(FLARE_HEAL_FLOOR_BUDGET, fmaxf(0, remaining));
}

void FlareHealTick(float dt) {

    /* ==================================================================
     * 2026-11-06（用户实测：「闪光弹不能回理智，你没有做好」）
     * ------------------------------------------------------------------
     * 原来这一行是 `if (!RunSkillOwned(IR_BIOLUM)) return;` ——
     * 也就是说**必须先抽到那张卡**才生效。玩家扔了照明弹发现不回理智，
     * 完全合理：他根本没有那张卡。
     *
     * 现在：**基础效果对所有人生效**，抽到 IR_BIOLUM 则是强化
     * （回得更快、单根上限更高）。这样"照明弹能回理智"是稳定预期，
     * 卡牌负责把它变成值得专门去构筑的方向。
     * ================================================================ */
    int boosted = RunSkillOwned(IR_BIOLUM);
    if (sanity >= SanityMax()) return;
    if (s_floorFlareHealed >= FLARE_HEAL_FLOOR_BUDGET) return;

    /* 找最近的一根"仍在有效治疗窗口内"的落地照明棒 */
    float windowLeft=0;
    float bestD2 = FLARE_HEAL_RADIUS * FLARE_HEAL_RADIUS;
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        if (thrown[i].type != 0 || thrown[i].z > 0) continue;             /* 0 = 照明棒 */
        float elapsed = FLARE_LIFE_SECONDS - thrown[i].life;
        if (elapsed < 0.0f) continue;
        if (elapsed >= FLARE_HEAL_SECONDS) continue;   /* 只在前 8 秒有效 */
        float d2 = Dist2(thrown[i].x, thrown[i].y, posX, posY);
        if (d2 > bestD2) continue;
        bestD2 = d2;
        windowLeft=FLARE_HEAL_SECONDS-elapsed;
    }
    if (windowLeft<=0 || dt<=0) return;

    /* 单根上限靠"窗口 + 速率"自然成立：1.5/s × 8s = 12，
       正好等于 FLARE_HEAL_CAP。这里再用总量封一次顶，防止帧率异常。 */
    float rate = FLARE_HEAL_RATE * (boosted ? 2.0f : 1.0f);
    float cap  = FLARE_HEAL_CAP  * (boosted ? 2.0f : 1.0f);
    float want = rate * fminf(dt,windowLeft);
    if (want > cap) want = cap;
    float room = FLARE_HEAL_FLOOR_BUDGET - s_floorFlareHealed;
    if (want > room) want = room;
    sanity += want;
    if (sanity > SanityMax()) sanity = SanityMax();
    s_floorFlareHealed += want;
}

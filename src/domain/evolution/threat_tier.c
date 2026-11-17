#include "loop_api.h"
/* A confirmed plan is frozen before wallet withdrawal; floor entry is the
 * only live difficulty commit. Preview is never an AI input. */
#include "common.h"
#include "threat_tier.h"
#include "economy.h"
#include "threat.h"
#include "player_api.h"
#include "lang.h"
static int activeTier = 0, activeFloor = 0;
static float confirmedPlan = 0;
int ThreatTierFromPressure(float p) {
    if (!isfinite(p) || p < 0)
        return 0;
    return p >= 13 ? 3 : p >= 8 ? 2 : p >= 4 ? 1 : 0;
}
static float PlannedPressure(void) {
    static const float floor[4] = {0, 4, 8, 13};
    int plan = bankRiskPlan >= 0 && bankRiskPlan < 4 ? bankRiskPlan : 1;
    return fmaxf((float)BankPreviewTier(), floor[plan]);
}
int PreviewThreatTier(void) { return ThreatTierFromPressure(PlannedPressure()); }
int CurrentRunThreatTier(void) { return trainingMode ? 0 : activeTier; }
int MonsterTierAtLeast(int required) { return CurrentRunThreatTier() >= required; }
void ThreatTierConfirmPlan(void) {
    confirmedPlan = PlannedPressure();
    activeFloor = 0;
    activeTier = 0;
}
void CommitThreatTierForFloor(int floor) {
    activeFloor = floor;
    activeTier =
        trainingMode
            ? 0
            : ThreatTierFromPressure(fmaxf(GoldTier() + ThreatValue() * .45f, confirmedPlan));
}
void ThreatTierRestore(int floor, int tier, float plan) {
    activeFloor = floor > 0 ? floor : 0;
    activeTier = tier >= 0 && tier < 4 ? tier : 0;
    confirmedPlan = isfinite(plan) ? fmaxf(0, fminf(64, plan)) : 0;
}
int ThreatTierFloor(void) { return activeFloor; }
float ThreatTierCommittedPlan(void) { return confirmedPlan; }
const char *ThreatTierLabel(int tier) {
    static const char *en[] = {"I / BASE", "II / ALTERED", "III / ENHANCED", "IV / ULTIMATE"};
    static const char *zh[] = {"一档 / 基础", "二档 / 异化", "三档 / 强化", "四档 / 终极"};
    if (tier < 0 || tier > 3)
        tier = 0;
    return Tr(en[tier], zh[tier]);
}
unsigned ThreatTierColor(int tier) {
    static const unsigned col[] = {0x79CDF2, 0xA99AFF, 0xF0C45A, 0xF12B1C};
    return col[tier >= 0 && tier < 4 ? tier : 0];
}

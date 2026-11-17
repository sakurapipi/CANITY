#ifndef CANITY_THREAT_TIER_H
#define CANITY_THREAT_TIER_H
int ThreatTierFromPressure(float pressure);
int PreviewThreatTier(void);
int CurrentRunThreatTier(void);
int MonsterTierAtLeast(int required);
void ThreatTierConfirmPlan(void);
void CommitThreatTierForFloor(int level);
void ThreatTierRestore(int floor, int tier, float plan);
int ThreatTierFloor(void);
float ThreatTierCommittedPlan(void);
const char *ThreatTierLabel(int tier);
unsigned ThreatTierColor(int tier);
#endif

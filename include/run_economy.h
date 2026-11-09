#ifndef CANITY_RUN_ECONOMY_H
#define CANITY_RUN_ECONOMY_H
void RunEconomyReset(void);
void RunEconomyTick(float dt);
int RunEconomyRhythmGold(int hits,int bestCombo);
void RunEconomyCapture(int* floor,unsigned* mask,int* rhythmGold);
void RunEconomyRestore(int floor,unsigned mask,int rhythmGold);
float BenchCrateGoldMul(void);
float BenchExitGoldMul(void);
float BenchEscapeGoldMul(void);
void RunEconomyFloorGold(void);
float BenchRestPriceMul(void);
float BenchRhythmHealBonus(void);
float BenchSearchTimeMul(void);
float BenchCrankTimeMul(void);
float BenchSanityDrainMul(void);
#endif

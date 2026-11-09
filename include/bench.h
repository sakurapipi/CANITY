/* Current 28-node run attribute tree. Purchases persist within a saved run.
 * ResetRun is used by new-run/refund paths, not normal floor transitions.
 * Legacy mechanism nodes were retired; do not reintroduce neutral-value hooks.
 */
#ifndef CANITY_BENCH_H
#define CANITY_BENCH_H

#include "common.h"

#define BENCH_COLS 4
#define BENCH_ROWS 4
#define BENCH_COUNT 40

/* Saved bit positions: never reorder when changing UI layout or descriptions. */
typedef enum {
    BENCH_ENDURANCE = 0,
    BENCH_LIGHT_PACK = 1,
    BENCH_ECONOMY = 2,
    BENCH_BULL = 3,
    BENCH_BIG_CELL = 4,
    BENCH_SAVER = 5,
    BENCH_BRIGHT = 6,
    BENCH_EVERLIGHT = 7,
    BENCH_COMPOSURE = 8,
    BENCH_THICK_SKIN = 9,
    BENCH_RECOVER = 10,
    BENCH_IRON_MIND = 11,
    BENCH_SPARE_TOOL = 12,
    BENCH_LONG_HANDLE = 13,
    BENCH_HEAVY_HEAD = 14,
    BENCH_SHOCK = 15,
    BENCH_SWIFT = 16,
    BENCH_SEARCHLIGHT = 17,
    BENCH_DESPERATE = 18,
    BENCH_TEMPERED = 19,
    BENCH_SOFT_SOLES = 20, BENCH_TOOL_COOLING = 21,
    BENCH_FIELD_REST = 22, BENCH_RESERVE_LIGHT = 23,
    BENCH_BALANCED_LOAD=24, BENCH_CLEAR_MIND, BENCH_BREATH_CONTROL, BENCH_WIDE_BEAM,
    BENCH_CALM_RESERVE=28, BENCH_SCAVENGE, BENCH_RHYTHM_FOCUS, BENCH_TRIAGE, BENCH_SECOND_CELL, BENCH_LIGHT_TOUCH, BENCH_RUNNER, BENCH_STEADY_HANDS, BENCH_CRANK_TORQUE, BENCH_POCKET_CHANGE, BENCH_OPENING_BONUS, BENCH_DEEP_DIVIDEND,
} BenchNodeId;

typedef struct {
    const char* name;
    const char* nameZh;
    const char* desc;
    const char* descZh;
    int cost;
    int col, row;
    int star;
} BenchNode;

extern const BenchNode benchNodes[];
extern uint64_t benchOwned;

void BenchResetRun(void);
void BenchRefundRun(void);
int BenchHas(int id);
int BenchPrereqOk(int id);
int BenchCanBuy(int id);
/* 1 success; 2 owned; 3 invalid/missing prerequisite; 4 funds; 5 other capstone. */
int BenchTryBuy(int id);
float BenchThreatCostOf(int id);
int BenchPrereqId(int id);
int BenchArchetype(int id);
const char* BenchShortName(int id);
int BenchNavNearest(int from, int dx, int dy);
int BenchNodeAt(int x, int y);
void BenchBuyButtonRect(int* x0,int* y0,int* x1,int* y1);
int BenchBuyButtonAt(int x,int y);
void BenchNodeBoxSize(int* width, int* height);

float BenchMoveMul(void);
float BenchStaminaMul(void);
float BenchSprintCostMul(void);
float BenchBatteryCapMul(void);
float BenchFlashDrainMul(void);
float BenchLightRangeMul(void);
float BenchSanityAdd(void);
float BenchFearDamageMul(void);
float BenchHealMul(void); /* RECOVER (10): safe-room recovery x1.50. */
float BenchWalkNoiseMul(void);
float BenchWrenchCooldownMul(void);
float BenchKnockExtra(void);
float BenchKnockStunMul(void);
int BenchKnockCharges(void);
int BenchShock(void);

/* Persistent skill-tree sensory effects; independent of bench purchases. */
int BenchHearing(void);
int BenchInnerEye(void);
int BenchPremonition(void);

#define BENCH_NODE_WX 260.0f
#define BENCH_NODE_WY 130.0f
extern float treeVX, treeVY, treeVK;
extern int treeDragging;
float BenchNodeWorldX(int id);
float BenchNodeWorldY(int id);
void BenchFocusNode(int id);
void BenchDragStart(int mx, int my);
void BenchDragMove(int mx, int my);
void BenchDragEnd(void);
void BenchZoomAt(float factor, int mx, int my);
void BenchResetView(void);
void BenchPanBy(float dxScreen, float dyScreen);

#endif

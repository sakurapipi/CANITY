#ifndef CANITY_TACTICAL_TOOLS_H
#define CANITY_TACTICAL_TOOLS_H
#include "common.h"
#define TOOL_FIRST_ITEM 7
#define TOOL_COUNT 7
#define TOOL_ACOUSTIC_RADIUS 3.7f
#define TOOL_ACOUSTIC_SECONDS 10.0f
#define TOOL_PRISM_BLIND_SECONDS 3.0f
#define TOOL_SHOT_FIRST 32
#define TOOL_SHOP_FIRST 32
int TacticalInterruptWarden(float x,float y,float radius);

enum { TOOL_ACOUSTIC, TOOL_PRISM, TOOL_SOLVENT, TOOL_TETHER, TOOL_REVEAL, TOOL_TRACE, TOOL_JAMMER };
typedef struct {
    const char *en;
    const char *zh;
    const char *detailEn;
    const char *detailZh;
    int price, cap;
} ToolDef;
typedef struct {
    float x, y, ttl;
    int kind;
} ToolZone;
typedef struct {
    int inventory[TOOL_COUNT], quick[4], key[4], assistEscape;
    ToolZone acoustic[2], solvent;
    float dust[12], prismFlicker;
} TacticalState;
extern TacticalState tactical;
const ToolDef *ToolDefinition(int tool);
const char *ToolDetail(int tool);
int InventoryCount(int item);
int InventoryAt(int position);
int InventoryPosition(int item);
int InventoryReorder(int item,int destination);
void InventoryOrderSave(unsigned char *out);
void InventoryOrderRestore(const void *bytes,int length);
int InventoryCycle(int item,int step);
void InventoryCounts(int *out);
const char *InventoryName(int item);
int ToolCapacity(int tool);
int ToolReceive(int tool);
int ToolCanOffer(int tool);
int TacticalTryUse(int item, float charge);
void TacticalImpact(int kind, float x, float y);
void TacticalTick(float dt);
float TacticalJammerEffect(float *x,float *y);
void TacticalResetJammer(void);
void TacticalResetRun(void);
void TacticalResetFloor(void);
float TacticalFootstepMul(float x, float y, float hush);
float TacticalNoiseMul(float x, float y);
int TacticalEmergencySolvent(void);
int CollectorMagneticPull(int apply);
int TacticalPanelOpen(void);
int TacticalInputKey(int key, int repeat);
int TacticalInputClick(int x, int y);
void TacticalInputWheel(int steps);
void TacticalDrawPanel(void);
void TacticalDrawWorld(void);
#endif

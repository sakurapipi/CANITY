#ifndef CANITY_ROOM_THEME_H
#define CANITY_ROOM_THEME_H
typedef struct {
    int theme,active,spent,inside,visits,previousLight;
    float switchX,switchY,timer,lastX,lastY;
    unsigned visited;
    int layoutVersion,variant,choice,target,awaitingRelease;
    unsigned prepared;
    float landmarkX,landmarkY,ambientClock,elapsed;
} RoomThemeState;
extern RoomThemeState roomTheme;
int RoomThemeForFloor(int level);
int RoomThemeMaterialForFloor(int level);
int RoomThemeTemplateForFloor(int level);
const char* RoomThemeName(int theme,int chinese);
void RoomThemeBind(void);
void RoomThemeNotify(const char* text);
const char* RoomThemeNotification(void);
void RoomThemeTick(float dt);
const char* RoomThemeInstruction(void);
float RoomThemeNoiseMul(void);
void RoomThemeSetMadness(int enabled);
int RoomThemeMadness(void);
float RoomThemeSanityRatio(float actual);
float RoomThemeLootMul(void);
void RoomThemePlaceVault(void);
void RoomThemeStartEvent(int kind,float seconds);
void RoomThemeRedirectNoise(float* x,float* y);
void RoomThemeSaveEvent(int* kind,float* time);
void RoomThemeLoadEvent(int kind,float time);
/* New room controls use the same hold interaction on keyboard, mouse and touch. */
int RoomThemeInteractFixture(int propIndex);
const char* RoomThemeFixtureLine(int propIndex);
const char* RoomThemeGuideEffect(int theme);
const char* RoomThemeGuideControl(int theme,int side);
const char* RoomThemeEffectLine(int hatchIndex);
int RoomThemeIsCurrent(int hatchIndex);
int RoomThemeCanActivate(int hatchIndex,char* reason,int capacity);
int RoomThemeActivate(int hatchIndex);
int RoomThemeSwapTarget(void);
int RoomThemeSelectCentralTarget(int hatchIndex);
void RoomThemePopulateVault(void);
int RoomThemeVaultBonus(int index);
int RoomThemeVaultChest(int index);
int RoomThemeVaultResult(int* stake,int* total);
int RoomThemeVaultReward(int index); /* -1 until investment is committed */
int RoomThemeVaultReturn(int stake,int roll);
int RoomThemeVaultPrepare(int stake,unsigned roll);
/* Price is ceil(percent * carried gold), with a low-balance admission floor. */
int RoomThemeGoldCost(int kind,int gold);
float RoomThemeControlPulse(void);
float RoomThemeEventRemaining(void);
float RoomThemeFreeMapRemaining(void);
int RoomThemeEventKind(void);
int RoomThemeVisualStage(void);
void RoomThemeRebuildControls(void);
#endif

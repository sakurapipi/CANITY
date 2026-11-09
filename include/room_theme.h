#ifndef CANITY_ROOM_THEME_H
#define CANITY_ROOM_THEME_H
typedef struct { int theme,active,spent,inside,visits,previousLight; float switchX,switchY,timer,lastX,lastY; unsigned visited; } RoomThemeState;
extern RoomThemeState roomTheme;
int RoomThemeForFloor(int level);
int RoomThemeTemplateForFloor(int level);
const char* RoomThemeName(int theme,int chinese);
void RoomThemeBind(void);
void RoomThemeTick(float dt);
int RoomThemeSwitchNear(float x,float y);
void RoomThemeUseSwitch(void);
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
#endif

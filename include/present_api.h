/* include/present_api.h —— 呈现/界面（2026-11-09，第 10 轮）
 *
 * 从 game.h 按职责抽出。**game.h 未改动**，两边并存。
 */
#ifndef CANITY_PRESENT_API_H
#define CANITY_PRESENT_API_H

#include "common.h"

/* 窗口逻辑尺寸（呈现层） */
extern int winW, winH;

extern HWND g_hwnd;
int CanityKeyDown(int vk, int repeat);
void CanityMouseMove(int mx, int my);
void CanityMouseClick(int mx, int my);
void CanityMouseWheel(int delta, int mx, int my);
void CanityPauseToggle(void);
void CanityToggleFlashlight(void);
void CanityMenuAction(void);
extern int menuSelection, settingsSelection, settingsPopupOpen;
extern int popupSelection, popupItem;
extern int rebindingAct;
extern int rebindConflict;
extern int resetDefaultsFlag;
extern int sliderDragging;
extern int resIndex;
extern const int resolutions[RES_COUNT][2];
extern int isFullscreen;
extern int showFps;
extern int fpsCapIndex;
extern const int fpsCaps[5];
extern int renderScaleIndex;
extern int dynamicRes;
extern const int renderScales[4];
extern float mouseSens;
extern int vignetteOn;
extern int hudDetail;
extern int fovIndex;
extern float gammaValue;
extern float gammaBuilt;
extern int currentFps;
extern double lastRenderMs;
extern double frameMsMax;
extern int hubSelection;
extern float mouseCharge;
extern int resetInputState;
extern double g_timeSec;
extern float volumeF;
extern float musicVolumeF;
extern int hoverKind;
extern int hoverIndex;
extern int keys[256];
extern int keyPressed[256];
extern POINT lastMousePos;
extern int firstMouseMove;
extern POINT g_mousePos;
extern int isMapOpen;
extern int mapFullScreen;
extern int settingsPage;
extern int menuScrollOffsets[16];
extern SettingItem settings[SETTING_COUNT];
extern const char* optKeys[OPT_KEY_COUNT];
extern const int optKeyVKs[OPT_KEY_COUNT];
extern const char* rootCategoryNames[3];
extern const char* bindCategoryNames[3];
void ClearKeyEdges(void);
void ApplyDisplayMode(void);
void ForceCursor(int visible);
int IsActionPressed(ActionId act);
void SettingsRegister(void);
const char* PageTitle(int page);
int PageItems(int* out);
int SettingsRowCount(void);
const char* VkName(int vk);
int BindActionOfItem(const SettingItem* it);
int IsKeyBindItem(const SettingItem* it);
extern float g_shakeOffX, g_shakeOffY;
extern float bobPhase;
void AddShake(float seconds, float mag);
extern int hubPage;
void UiLayoutBegin(void);
void DumpUILayout(const char* path);
extern int forceFrame;
void PollInput(void);
extern float treeVX, treeVY, treeVK;
void SyncWindowSize(void);
extern int clientW, clientH;
void ApplySetting(int item, int value);
void ActivateMenuItem(void);
void ActivateHubRow(void);
void ActivateSettingRow(void);
extern int mapsFullyExplored;
extern int flashlightOffLevel;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern int helpPage;

extern int uiDirty;
extern const char* pickupMsg;
extern float pickupMsgTimer;

int itemHasStock(void);
extern int weaponPickSel;
extern int playerInSafe;

/* 粒子（原 game.c，已拆出） */
void SpawnParticles(float x, float y, int tint);
void SpawnShards(float x, float y, float vx, float vy, float charge);
/* v3.3：粘胶区域（实现放在 entity.c） */;

extern int g_shardTint;

#endif

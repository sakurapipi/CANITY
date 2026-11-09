/*
 * Canity — Steam 专区的网页后端（全部空实现）
 * ---------------------------------------------------------------------
 * 2026-11-06：网页构建排除 src/steam/steam_zone.c（Steamworks + Win32
 * 文件时间），这个文件补上 steam_zone.h 的全部公开接口。
 *
 * 语义：网页版没有 Steamworks。云同步/成就一律安全空操作 ——
 * 本地存档照常工作（localStorage），只是不参与云端对比。
 */
#include "common.h"
#include "steam_zone.h"


int  SteamZoneInit(void) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ return 0; }
void SteamZoneUploadSave(const char* path) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)path; }
void SteamZoneShutdown(void) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ }
void SteamZoneTick(float dt) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)dt; }
int  SteamZoneReady(void) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ return 0; }
int  SteamZoneUnlockAchievement(const char* n) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)n; return 0; }
int  SteamZoneHasAchievement(const char* n) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)n; return 0; }
int  SteamZoneCloudUpload(const char* n, const void* d, int b) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)n; (void)d; (void)b; return 0; }
int  SteamZoneCloudDownload(const char* n, void* b, int s) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)n; (void)b; (void)s; return -1; }
int  SteamZoneCloudGetSize(const char* n) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)n; return -1; }
int  SteamZoneCloudExists(const char* n) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)n; return 0; }
void SteamZoneMusicPause(int p) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)p; }
int  SteamZoneMusicPlaying(void) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ return 0; }
void SteamZoneOpenOverlay(const char* d) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)d; }
int  SteamZoneCloudDecide(const void* l, int ll, const void* r, int rl, int* pick) {
    (void)l; (void)ll; (void)r; (void)rl; if (pick) *pick = 0; return 0;
}
int  SteamZoneCloudSync(const char* n, void* lb, int* bs) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)n; (void)lb; (void)bs; return 0; }
void SteamZoneSetStatInt(const char* n, int v) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)n; (void)v; }
void SteamZoneStoreStats(void) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ }
void SteamZoneSetRichPresence(const char* k, const char* v) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)k; (void)v; }
void SteamZoneMusicDuck(float s) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)s; }
void SteamZoneMusicDuckTick(float dt) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)dt; }
int  SteamZoneUnlockAchievementIndex(int i) { /* PLATFORM_NOOP: Steamworks is unavailable in the browser; this is the unsupported-platform contract. */ (void)i; return 0; }
const char* SteamZoneStatusText(void) { return "WEB"; }


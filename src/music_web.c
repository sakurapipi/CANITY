/*
 * Canity — 配乐分区的网页后端（静音）
 * ---------------------------------------------------------------------
 * 2026-11-06：网页构建排除 music.c（MCI）与 music_proc.c（Interlocked*），
 * 这个文件补上 music.h 的全部公开接口。
 *
 * 分区规则：music 符号只住在 music 分区里，平台层不越界。
 */
#include "common.h"
#include "music.h"


void MusicInit(void) { }
void MusicShutdown(void) { }
void MusicLoadGameTracks(void) { }
int  MusicRegisterProvider(MusicProviderId id, const MusicProvider* api, void* ctx) {
    (void)id; (void)api; (void)ctx; return 0;
}
int  MusicUseProvider(MusicProviderId id) { (void)id; return 0; }
int  MusicSetTrack(MusicScene scene, const char* utf8Path) {
    (void)scene; (void)utf8Path; return 0;
}
void MusicSetScene(MusicScene scene) { (void)scene; }
void MusicSetMasterVolume(float v) { (void)v; }
void MusicSetPaused(int paused) { (void)paused; }
void MusicDuck(float seconds, float gain) { (void)seconds; (void)gain; }
void MusicNotify(MusicEvent event) { (void)event; }
void MusicTick(float dt) { (void)dt; }
float MusicEffectiveGain(void) { return 0.0f; }


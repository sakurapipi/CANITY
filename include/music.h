#ifndef CANITY_MUSIC_H
#define CANITY_MUSIC_H

/* Music protocol v1. Game/audio/platform modules depend only on this file.
   All calls run on the main thread. Callbacks must not block or call back
   into Music*. A provider owns its decoder/resources; the music director
   owns scene selection, master gain, ducking and pause state. UTF-8 paths. */
#define MUSIC_PROTOCOL_VERSION 1
typedef enum { MUSIC_LOCAL=0, MUSIC_STEAM, MUSIC_SIM, MUSIC_PROVIDER_COUNT } MusicProviderId;
typedef enum { MUSIC_MENU=0, MUSIC_EXPLORE, MUSIC_CHASE, MUSIC_SAFE, MUSIC_VICTORY, MUSIC_DEFEAT, MUSIC_SCENE_COUNT } MusicScene;
typedef enum { MUSIC_EVENT_GATE=0, MUSIC_EVENT_CAUGHT, MUSIC_EVENT_ACHIEVEMENT } MusicEvent;
typedef struct {
    int version;
    int (*play)(void* context,const char* track,int loop);
    void (*stop)(void* context);
    void (*pause)(void* context,int paused);
    void (*gain)(void* context,float linearGain);
    void (*scene)(void* context,MusicScene scene);
    void (*tick)(void* context,float dt);
} MusicProvider;

void MusicInit(void);
void MusicLoadGameTracks(void);
void MusicShutdown(void);
int MusicRegisterProvider(MusicProviderId id,const MusicProvider* api,void* context);
int MusicUseProvider(MusicProviderId id);
int MusicSetTrack(MusicScene scene,const char* utf8Path);
void MusicSetScene(MusicScene scene);
void MusicSetMasterVolume(float volume);
void MusicSetPaused(int paused);
void MusicDuck(float seconds,float gain);
void MusicNotify(MusicEvent event);
void MusicTick(float dt);
float MusicEffectiveGain(void);
MusicScene MusicCurrentScene(void);
MusicProviderId MusicCurrentProvider(void);
const char* MusicLastError(void);
#endif

/* Independent music director. Three providers: local MCI files, the built-in
   procedural score (music_proc.c) and Steam. Missing track files fall back to
   the built-in score; SIM plugs into the same v1 provider protocol. */
#include "common.h"
#include "room_theme.h"
#include "music.h"
#include "music_proc.h"
#include "music_pcm.h"
#include "player_api.h"
#include "entity.h"
#include "audio.h"
#include "steam_zone.h"

typedef struct { MusicProvider api;void* context;int present; } ProviderSlot;
static ProviderSlot providers[MUSIC_PROVIDER_COUNT];
static MusicProviderId current=MUSIC_LOCAL;
static MusicScene currentScene=MUSIC_MENU;
static char tracks[MUSIC_SCENE_COUNT][MAX_PATH*3];
static char loadedPcmTracks[MUSIC_SCENE_COUNT][MAX_PATH*3];
static float master=0.5f,duckGain=1.0f,duckTime=0.0f,effective=0.5f,lastGain=-1.0f;
static int ready=0,paused=0,localOpen=0,steamPausedByUs=0;
static char errorText[128];
static float clamp01(float v) { if(!(v>=0.0f)) return 0.0f;return v>1.0f?1.0f:v; }
static ProviderSlot* active(void) { return &providers[current]; }
static void localStop(void* context) {
    (void)context;if(localOpen) mciSendStringW(L"close canity_music",NULL,0,NULL);localOpen=0;
    MusicPcmSelect(-1);MusicProcSetScene(MUSIC_PROC_SILENT);
}
static int localPlay(void* context,const char* track,int loop) {
    (void)context;localStop(NULL);
    if(track && track[0] && MusicPcmLoad((int)currentScene,track) && MusicPcmSelect((int)currentScene)) {
        strcpy(loadedPcmTracks[currentScene],track);errorText[0]=0;return 1;
    }
    if(!track || !track[0]) {MusicProcSetScene((int)currentScene);return 1;}
    wchar_t path[MAX_PATH*3],command[MAX_PATH*3+80];
    if(strchr(track,'"') || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,track,-1,path,(int)(sizeof(path)/sizeof(path[0])))) {
        snprintf(errorText,sizeof(errorText),"Invalid UTF-8 music path");return 0;
    }
    _snwprintf(command,sizeof(command)/sizeof(command[0]),L"open \"%ls\" type mpegvideo alias canity_music",path);
    MCIERROR code=mciSendStringW(command,NULL,0,NULL);
    if(code) {snprintf(errorText,sizeof(errorText),"Cannot open music (MCI %lu); built-in fallback",(unsigned long)code);MusicProcSetScene((int)currentScene);return 1;}
    localOpen=1;
    code=mciSendStringW(loop?L"play canity_music repeat":L"play canity_music",NULL,0,NULL);
    if(code) {localStop(NULL);snprintf(errorText,sizeof(errorText),"Cannot play music (MCI %lu); built-in fallback",(unsigned long)code);MusicProcSetScene((int)currentScene);return 1;}
    errorText[0]=0;return 1;
}
static void localPause(void* context,int value) {
    (void)context;if(localOpen) mciSendStringW(value?L"pause canity_music":L"resume canity_music",NULL,0,NULL);
}
static void localGain(void* context,float gain) {
    (void)context;MusicPcmGain(gain);MusicProcSetGain(gain);
    if(!localOpen) return;
    wchar_t command[80];
    _snwprintf(command,80,L"setaudio canity_music volume to %d",(int)(clamp01(gain)*1000.0f));
    mciSendStringW(command,NULL,0,NULL);
}
/* Steam's player has transport controls, not per-game PCM mixing. Never
   start music the user was not already playing. Restore only our own pause. */
static int steamPlay(void* context,const char* track,int loop) {(void)context;(void)track;(void)loop;return SteamZoneReady();}
static void steamPause(void* context,int value) {
    (void)context;
    if(value && !steamPausedByUs && SteamZoneMusicPlaying()) {SteamZoneMusicPause(1);steamPausedByUs=1;}
    if(!value && steamPausedByUs) {SteamZoneMusicPause(0);steamPausedByUs=0;}
}
static void steamStop(void* context) {steamPause(context,0);}
/* 内置配乐：真正的声音在音频线程里生成。这里只转发场景和音量；
   暂停由 MusicTick 把增益降到 0 完成，所以不给 pause 回调。 */
static int builtinPlay(void* context,const char* track,int loop) { /* FIXED_RESULT: Procedural provider accepts play; scene/gain callbacks control synthesis, not a track file. */
    (void)context;(void)track;(void)loop;return 1;
}
static void builtinStop(void* context) {(void)context;MusicProcSetScene(MUSIC_PROC_SILENT);}
static void builtinGain(void* context,float gain) {(void)context;MusicProcSetGain(gain);MusicPcmGain(gain);}
static void builtinScene(void* context,MusicScene scene) {(void)context;MusicProcSetScene((int)scene);}
int MusicRegisterProvider(MusicProviderId id,const MusicProvider* api,void* context) {
    if(id<0 || id>=MUSIC_PROVIDER_COUNT || !api || api->version!=MUSIC_PROTOCOL_VERSION || !api->play || !api->stop) return 0;
    if(ready && current==id) active()->api.stop(active()->context);
    providers[id].api=*api;providers[id].context=context;providers[id].present=1;return 1;
}
void MusicInit(void) {
    if(ready) return;
    memset(providers,0,sizeof(providers));memset(tracks,0,sizeof(tracks));
    MusicProvider local={1,localPlay,localStop,localPause,localGain,NULL,NULL};
    MusicProvider steam={1,steamPlay,steamStop,steamPause,NULL,NULL,NULL};
    MusicProvider builtin={1,builtinPlay,builtinStop,NULL,builtinGain,builtinScene,NULL};
    MusicRegisterProvider(MUSIC_LOCAL,&local,NULL);MusicRegisterProvider(MUSIC_STEAM,&steam,NULL);
    MusicRegisterProvider(MUSIC_BUILTIN,&builtin,NULL);
    current=MUSIC_LOCAL;currentScene=MUSIC_MENU;ready=1;paused=0;duckTime=0;duckGain=1;lastGain=-1;errorText[0]=0;
}
void MusicShutdown(void) {if(!ready)return;active()->api.stop(active()->context);MusicPcmShutdown();ready=0;}
int MusicUseProvider(MusicProviderId id) {
    if(!ready) MusicInit();
    if(id<0 || id>=MUSIC_PROVIDER_COUNT || !providers[id].present || (id==MUSIC_STEAM && !SteamZoneReady())) return 0;
    active()->api.stop(active()->context);current=id;lastGain=-1;
    ProviderSlot* p=active();int ok=p->api.play(p->context,tracks[currentScene],1);
    if(p->api.scene)p->api.scene(p->context,currentScene);
    if(p->api.pause)p->api.pause(p->context,paused);
    return ok;
}
int MusicSetTrack(MusicScene scene,const char* path) {
    if(!ready)MusicInit();
    if(scene<0 || scene>=MUSIC_SCENE_COUNT || !path || strlen(path)>=sizeof(tracks[0])) return 0;
    strcpy(tracks[scene],path);
    /* Decode only the active scene, not the complete album at startup. */
    if(scene==currentScene) {lastGain=-1;return active()->api.play(active()->context,path,1);}
    return 1;
}
void MusicSetScene(MusicScene scene) {
    if(!ready)MusicInit();
    if(scene<0 || scene>=MUSIC_SCENE_COUNT || scene==currentScene)return;
    currentScene=scene;ProviderSlot* p=active();lastGain=-1;
    p->api.play(p->context,tracks[scene],1);if(p->api.scene)p->api.scene(p->context,scene);
    if(p->api.pause)p->api.pause(p->context,paused);
}
void MusicSetMasterVolume(float v) {master=clamp01(v);}
void MusicSetPaused(int value) {
    value=!!value;if(value==paused)return;paused=value;
    /* Gain reaches zero before pausing file transport; resume precedes fade-in. */
    if(!value && ready && active()->api.pause)active()->api.pause(active()->context,0);
}
void MusicDuck(float seconds,float gain) {
    if(!(seconds>0.0f))return;
    gain=clamp01(gain);
    if(duckTime<=0 || gain<duckGain)duckGain=gain;
    if(seconds>duckTime)duckTime=seconds;
}
void MusicNotify(MusicEvent event) {
    if(event==MUSIC_EVENT_GATE)MusicDuck(2.0f,0.25f);
    else if(event==MUSIC_EVENT_CAUGHT)MusicDuck(1.5f,0.15f);
    else if(event==MUSIC_EVENT_ACHIEVEMENT)MusicDuck(1.0f,0.5f);
    if(current!=MUSIC_STEAM)SteamZoneMusicDuck(2.0f);
}
void MusicTick(float dt) {
    if(!ready || !(dt>=0.0f))return;
    MusicPcmMood(RoomThemeSanityRatio(sanity/fmaxf(1.0f,SanityMax())));
    if(dt>1.0f)dt=1.0f;
    if(duckTime>0.0f) {duckTime-=dt;if(duckTime<=0.0f){duckTime=0;duckGain=1;}}
    float target=master*duckGain;if(paused)target=0;
    float step=dt*4.0f;if(effective<target)effective=fminf(target,effective+step);else effective=fmaxf(target,effective-step);
    ProviderSlot* p=active();
    if(p->api.gain && (fabsf(effective-lastGain)>0.005f || (lastGain!=target && effective==target))) {p->api.gain(p->context,effective);lastGain=effective;}
    if(current==MUSIC_STEAM)steamPause(NULL,paused || duckTime>0.0f);
    else if(paused && effective==0 && p->api.pause)p->api.pause(p->context,1);
    if(p->api.tick)p->api.tick(p->context,dt);
}
float MusicEffectiveGain(void) {return effective;}
void MusicSeekSeconds(float seconds) {
    if(!(seconds>=0))return;
    MusicPcmSeek((int)(seconds*44100.0f));
    if(localOpen) {wchar_t command[96];_snwprintf(command,96,L"play canity_music from %d repeat",(int)(seconds*1000));mciSendStringW(command,NULL,0,NULL);}
}
MusicScene MusicCurrentScene(void) {return currentScene;}
MusicProviderId MusicCurrentProvider(void) {return current;}
const char* MusicLastError(void) {return errorText;}

void MusicLoadGameTracks(void) {
    static const wchar_t* names[]={L"menu",L"explore",L"chase",L"safe",L"victory",L"defeat"};
    wchar_t base[MAX_PATH*3];DWORD n=GetModuleFileNameW(NULL,base,(DWORD)(sizeof(base)/sizeof(base[0])));
    if(!n || n>=sizeof(base)/sizeof(base[0])){MusicUseProvider(MUSIC_BUILTIN);return;}
    wchar_t* slash=wcsrchr(base,L'\\');if(!slash){MusicUseProvider(MUSIC_BUILTIN);return;}slash[1]=0;
    const wchar_t* bundled=_wgetenv(L"CANITY_AUDIO_DIR");
    int found=0;
    for(int i=0;i<MUSIC_SCENE_COUNT;++i) {
        wchar_t path[MAX_PATH*3+80];char utf8[MAX_PATH*3];
        _snwprintf(path,sizeof(path)/sizeof(path[0]),L"%lsmusic\\%ls.mp3",base,names[i]);
        if(bundled && *bundled)
            _snwprintf(path,sizeof(path)/sizeof(path[0]),L"%ls\\%ls.mp3",bundled,names[i]);
        if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)
            _snwprintf(path,sizeof(path)/sizeof(path[0]),L"%lsmusic\\%ls.wav",base,names[i]);
        if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)continue;
        if(WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,sizeof(utf8),NULL,NULL)) {
            if(MusicSetTrack((MusicScene)i,utf8))found++;
        }
    }
    /* 没有 music 文件夹时用内置程序化配乐；CANITY_MUSIC=builtin/files 可强制选择。 */
    const char* mode=getenv("CANITY_MUSIC");
    if(mode && !strcmp(mode,"builtin")) {MusicUseProvider(MUSIC_BUILTIN);return;}
    MusicUseProvider(found?MUSIC_LOCAL:MUSIC_BUILTIN);
}

int MusicCanCalibrate(void) {return current==MUSIC_LOCAL && AudioOutputLatencySeconds()>0 && !strcmp(tracks[currentScene],loadedPcmTracks[currentScene]) && MusicPcmAvailable((int)currentScene);}

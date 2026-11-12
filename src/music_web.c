/* Browser music director: decoded file loops + synthesized per-scene fallback. */
#include "common.h"
#include "room_theme.h"
#include "music.h"
#include "entity.h"
#include "player_api.h"
static MusicScene webScene=MUSIC_MENU;
static MusicProviderId webProvider=MUSIC_LOCAL;
static float webMaster=.5f,webGain=0,webDuck=1,webDuckTime=0;
static int webPaused=0;
static MusicProvider webCustom[MUSIC_PROVIDER_COUNT];
static void* webContexts[MUSIC_PROVIDER_COUNT];
static char webTracks[MUSIC_SCENE_COUNT][MAX_PATH*3];
static int WebCustomActive(void){return webProvider>=0 && webProvider<MUSIC_PROVIDER_COUNT && webCustom[webProvider].play!=NULL;}
EM_JS(void,js_music_init,(void),{
    if(Module._musicBus || !Module._actx)return;
    var ctx=Module._actx;
    var filter=ctx.createBiquadFilter();filter.type='lowpass';filter.frequency.value=18000;
    var gain=ctx.createGain();gain.gain.value=0;filter.connect(gain);gain.connect(ctx.destination);
    Module._musicBus={filter:filter,gain:gain,source:null,buffers:{},loading:{},scene:0,serial:0,builtin:false,offset:0,paths:{},startedAt:0,paused:false};
    var bus=Module._musicBus;
    var nbuffer=ctx.createBuffer(1,ctx.sampleRate*2,ctx.sampleRate),ndata=nbuffer.getChannelData(0),seed=77123;
    var hbuffer=ctx.createBuffer(1,ctx.sampleRate*2,ctx.sampleRate),hdata=hbuffer.getChannelData(0);
    for(var i=0;i<ndata.length;i++){
        seed=(Math.imul(seed,1664525)+1013904223)|0;ndata[i]=((seed>>>8)/8388608-1);
        var t=(i/ctx.sampleRate)%1;hdata[i]=Math.sin(2*Math.PI*56*t)*Math.exp(-t*24)+.6*Math.sin(2*Math.PI*63*(t-.23))*Math.exp(-Math.abs(t-.23)*28);
    }
    bus.noise=ctx.createBufferSource();bus.noise.buffer=nbuffer;bus.noise.loop=true;
    bus.noiseGain=ctx.createGain();bus.noiseGain.gain.value=0;bus.noise.connect(bus.noiseGain);bus.noiseGain.connect(filter);bus.noise.start();
    bus.heart=ctx.createBufferSource();bus.heart.buffer=hbuffer;bus.heart.loop=true;
    bus.heartGain=ctx.createGain();bus.heartGain.gain.value=0;bus.heart.connect(bus.heartGain);bus.heartGain.connect(filter);bus.heart.start();
    Module._musicPlay=function(scene,offset){
        var m=Module._musicBus;if(!m)return;
        var old=m.source;if(old){try{old.stop();}catch(e){} old.disconnect();m.source=null;}
        m.scene=scene;m.offset=offset||0;m.serial++;
        var generation=m.serial;
        function start(buffer){
            if(generation!==m.serial)return;
            var node=ctx.createBufferSource();node.buffer=buffer;node.loop=true;
            node.connect(m.filter);node.start(0,m.offset%buffer.duration);m.source=node;m.startedAt=ctx.currentTime;
        }
        function fallback(){
            if(generation!==m.serial)return;
            var frames=ctx.sampleRate*16,buffer=ctx.createBuffer(2,frames,ctx.sampleRate);
            var base=[36,32,39,30,40,28][scene];
            for(var c=0;c<2;c++){
                var data=buffer.getChannelData(c),phase=c*.2;
                for(var i=0;i<frames;i++){
                    var t=i/ctx.sampleRate;
                    var drone=Math.sin(2*Math.PI*Math.round(base*16)*t/16+phase)*.12;
                    drone+=Math.sin(2*Math.PI*Math.round(base*2.1189*16)*t/16)*.025;
                    var beat=scene===2? .5:1.0,age=t%beat;
                    var pulse=Math.sin(2*Math.PI*52*age)*Math.exp(-age*18)*.06;
                    data[i]=drone+pulse;
                }
            }start(buffer);
        }
        var names=['menu','explore','chase','safe','victory','defeat'];
        if(m.builtin){fallback();return;}
        if(m.buffers[scene]){start(m.buffers[scene]);return;}
        var path=m.paths[scene]||(globalThis.CANITY_MUSIC_TRACKS && globalThis.CANITY_MUSIC_TRACKS[scene])||'music/'+names[scene]+'.mp3';
        if(!m.loading[scene]){
            function decode(url){return fetch(url).then(function(r){if(!r.ok)throw new Error('music unavailable');return r.arrayBuffer();}).then(function(b){return ctx.decodeAudioData(b);});}
            m.loading[scene]=decode(path).catch(function(e){
                if(!m.paths[scene] && path.endsWith('.mp3'))return decode('music/'+names[scene]+'.wav');
                throw e;
            }).then(function(buffer){
                /* Keep the active scene; avoid retaining the whole decoded album. */
                m.buffers={};if(m.scene===scene)m.buffers[scene]=buffer;
                delete m.loading[scene];return buffer;
            }).catch(function(e){delete m.loading[scene];throw e;});
        }
        m.loading[scene].then(start).catch(fallback);
    };
    Module._musicPlay(0,0);
});
EM_JS(void,js_music_scene,(int scene),{if(Module._musicPlay)Module._musicPlay(scene,0);});
EM_JS(void,js_music_mode,(int builtin),{if(Module._musicBus){Module._musicBus.builtin=!!builtin;Module._musicPlay(Module._musicBus.scene,0);}});
EM_JS(void,js_music_seek,(float seconds),{if(Module._musicBus)Module._musicPlay(Module._musicBus.scene,seconds);});
EM_JS(void,js_music_tick,(float gain,float ratio,int paused),{
    var m=Module._musicBus;if(!m)return;var t=Module._actx.currentTime;
    m.gain.gain.setTargetAtTime(gain,t,.08);
    if(paused && gain<.0002 && !m.paused){
        if(m.source){m.offset=(m.offset+Math.max(0,t-m.startedAt))%m.source.buffer.duration;try{m.source.stop();}catch(e){}m.source.disconnect();m.source=null;}
        m.paused=true;
    }else if(!paused && m.paused){m.paused=false;Module._musicPlay(m.scene,m.offset);}
    var stress=ratio>.8?0:Math.max(0,Math.min(1,(.8-ratio)/.8));
    m.filter.frequency.setTargetAtTime(18000-16500*stress,t,.3);
    m.noiseGain.gain.setTargetAtTime(stress*.018,t,.25);
    m.heartGain.gain.setTargetAtTime(stress*.075,t,.25);
    m.heart.playbackRate.setTargetAtTime(.8+stress*1.1,t,.2);
    if(!m.dissonance){
        m.dissonance=Module._actx.createGain();m.dissonance.gain.value=0;m.dissonance.connect(m.filter);
        m.drones=[55,58,83].map(function(f){var o=Module._actx.createOscillator();o.type='sine';o.frequency.value=f;o.connect(m.dissonance);o.start();return o;});
    }
    m.dissonance.gain.setTargetAtTime(Math.max(0,(.5-ratio)/.5)*.035,t,.3);
    if(m.source)m.source.detune.setTargetAtTime(stress*2.5*Math.sin(t*2.3),t,.1);
});
EM_JS(void,js_music_stop,(void),{var m=Module._musicBus;if(m){m.serial++;if(m.source){try{m.source.stop();}catch(e){}m.source.disconnect();}if(m.drones)m.drones.forEach(function(o){o.stop();o.disconnect();});if(m.dissonance)m.dissonance.disconnect();m.noise.stop();m.heart.stop();m.noise.disconnect();m.heart.disconnect();m.noiseGain.disconnect();m.heartGain.disconnect();m.filter.disconnect();m.gain.disconnect();Module._musicBus=null;}});
void MusicInit(void){js_music_init();}
void MusicLoadGameTracks(void){const char* mode=getenv("CANITY_MUSIC");MusicUseProvider(mode && !strcmp(mode,"builtin")?MUSIC_BUILTIN:MUSIC_LOCAL);}
void MusicShutdown(void){if(WebCustomActive())webCustom[webProvider].stop(webContexts[webProvider]);js_music_stop();}
int MusicRegisterProvider(MusicProviderId id,const MusicProvider* api,void* context){if(id<0 || id>=MUSIC_PROVIDER_COUNT || !api || api->version!=1 || !api->play || !api->stop)return 0;webCustom[id]=*api;webContexts[id]=context;return 1;}
int MusicUseProvider(MusicProviderId id){
    if(id<0 || id>=MUSIC_PROVIDER_COUNT || (id!=MUSIC_LOCAL && id!=MUSIC_BUILTIN && !webCustom[id].play))return 0;
    if(WebCustomActive())webCustom[webProvider].stop(webContexts[webProvider]);
    webProvider=id;
    if(WebCustomActive()){
        js_music_tick(0,1,1);
        MusicProvider* p=&webCustom[id];
        if(!p->play(webContexts[id],webTracks[webScene],1)){webProvider=MUSIC_BUILTIN;js_music_mode(1);return 0;}
        if(p->scene)p->scene(webContexts[id],webScene);
        return 1;
    }
    js_music_mode(id==MUSIC_BUILTIN);return 1;
}
EM_JS(int,js_music_track,(int scene,const char* path),{var m=Module._musicBus;if(!m)return 0;m.paths[scene]=UTF8ToString(path);delete m.buffers[scene];delete m.loading[scene];if(m.scene===scene)Module._musicPlay(scene,0);return 1;});
int MusicSetTrack(MusicScene scene,const char* path){if(scene<0 || scene>=MUSIC_SCENE_COUNT || !path || !path[0] || strlen(path)>=sizeof(webTracks[0]))return 0;strcpy(webTracks[scene],path);if(WebCustomActive() && scene==webScene)return webCustom[webProvider].play(webContexts[webProvider],path,1);return js_music_track(scene,path);}
void MusicSetScene(MusicScene scene){if(scene<0 || scene>=MUSIC_SCENE_COUNT || scene==webScene)return;webScene=scene;if(WebCustomActive()){MusicProvider* p=&webCustom[webProvider];p->play(webContexts[webProvider],webTracks[scene],1);if(p->scene)p->scene(webContexts[webProvider],scene);}else js_music_scene(scene);}
void MusicSetMasterVolume(float volume){webMaster=isfinite(volume)?fmaxf(0,fminf(1,volume)):0;}
void MusicSetPaused(int paused){webPaused=!!paused;if(!paused && WebCustomActive() && webCustom[webProvider].pause)webCustom[webProvider].pause(webContexts[webProvider],0);}
void MusicDuck(float seconds,float gain){if(seconds<=0)return;if(webDuckTime<=0 || gain<webDuck)webDuck=fmaxf(0,gain);if(seconds>webDuckTime)webDuckTime=seconds;}
void MusicNotify(MusicEvent event){MusicDuck(event==MUSIC_EVENT_GATE?2:1.5f,event==MUSIC_EVENT_CAUGHT?.15f:.35f);}
void MusicTick(float dt){js_music_init();if(!(dt>=0))return;webDuckTime=fmaxf(0,webDuckTime-dt);if(webDuckTime<=0)webDuck=1;float target=webPaused?0:webMaster*webDuck;webGain+=(target-webGain)*fminf(1,dt*4);if(WebCustomActive()){MusicProvider* p=&webCustom[webProvider];if(p->gain)p->gain(webContexts[webProvider],webGain);if(p->tick)p->tick(webContexts[webProvider],dt);if(webPaused && webGain<.0002f && p->pause)p->pause(webContexts[webProvider],1);}else js_music_tick(webGain,RoomThemeSanityRatio(sanity/fmaxf(1,SanityMax())),webPaused);}
void MusicSeekSeconds(float seconds){if(seconds>=0 && !WebCustomActive())js_music_seek(seconds);}
float MusicEffectiveGain(void){return webGain;}
MusicScene MusicCurrentScene(void){return webScene;}
MusicProviderId MusicCurrentProvider(void){return webProvider;}
const char* MusicLastError(void){return "";}

EM_JS(int,js_music_calibratable,(void),{var m=Module._musicBus;return m && !m.builtin && m.buffers[m.scene]?1:0;});
int MusicCanCalibrate(void){return !WebCustomActive() && js_music_calibratable();}
int MusicPrepareForCalibration(void){js_music_init();return MusicCanCalibrate();}

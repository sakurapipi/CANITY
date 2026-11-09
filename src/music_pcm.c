#include "common.h"
#ifndef CANITY_WEB
#include "music_pcm.h"
#include "music.h"
#include "music_proc.h"

typedef struct { short* samples; int frames; } PcmTrack;
static PcmTrack pcmTracks[MUSIC_SCENE_COUNT];
static volatile LONG requestedScene=-1, requestedGain=0, requestedMood=1000;
static volatile LONG requestedSeek=-1;
static int playingScene=-1;
static double cursor=0.0, heartbeat=0.0, detunePhase=0.0;
static int seekPending=-1;static float seekFade=1;
static float busGain=0.0f, filterL=0.0f, filterR=0.0f, moodSmooth=1.0f;
static uint32_t noiseState=179531u;
static unsigned pcmU16(const unsigned char* p) { return p[0]|((unsigned)p[1]<<8); }
static unsigned pcmU32(const unsigned char* p) { return pcmU16(p)|(pcmU16(p+2)<<16); }

int MusicPcmLoad(int scene,const char* path) {
    if(scene<0 || scene>=MUSIC_SCENE_COUNT || !path || pcmTracks[scene].samples) return 0;
    wchar_t wide[MAX_PATH*3];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,MAX_PATH*3)) return 0;
    FILE* file=_wfopen(wide,L"rb"); if(!file)return 0;
    unsigned char head[12],fmt[16];
    int valid=0; long dataAt=0; unsigned bytes=0;
    if(fread(head,1,12,file)!=12 || memcmp(head,"RIFF",4) || memcmp(head+8,"WAVE",4)) {fclose(file);return 0;}
    while(fread(head,1,8,file)==8) {
        unsigned size=pcmU32(head+4); if(size>44100u*4u*180u)break;
        if(!memcmp(head,"fmt ",4) && size>=16) {
            if(fread(fmt,1,16,file)!=16)break;
            valid=pcmU16(fmt)==1 && pcmU16(fmt+2)==2 && pcmU32(fmt+4)==44100 && pcmU16(fmt+12)==4 && pcmU16(fmt+14)==16;
            if(fseek(file,(long)(size-16+(size&1)),SEEK_CUR))break;
        } else if(!memcmp(head,"data",4)) { dataAt=ftell(file); bytes=size;break; }
        else if(fseek(file,(long)(size+(size&1)),SEEK_CUR))break;
    }
    if(!valid || !dataAt || bytes<8 || bytes%4) {fclose(file);return 0;}
    unsigned char* raw=(unsigned char*)malloc(bytes);
    if(!raw) {fclose(file);return 0;}
    if(fread(raw,1,bytes,file)!=bytes){free(raw);fclose(file);return 0;} fclose(file);
    /* Pairwise downsample to the existing 22050-Hz mixer, retaining stereo. */
    int frames=(int)(bytes/8);
    short* samples=(short*)malloc((size_t)frames*2*sizeof(short));
    if(!samples){free(raw);return 0;}
    for(int i=0;i<frames;i++)for(int c=0;c<2;c++) {
        int a=(int16_t)pcmU16(raw+(i*2)*4+c*2);
        int b=(int16_t)pcmU16(raw+(i*2+1)*4+c*2);
        samples[i*2+c]=(short)((a+b)/2);
    }
    free(raw);pcmTracks[scene].samples=samples;pcmTracks[scene].frames=frames;return 1;
}

int MusicPcmSelect(int scene) {
    int available=scene>=0 && scene<MUSIC_SCENE_COUNT && pcmTracks[scene].samples;
    InterlockedExchange(&requestedScene,available?scene:-1);return available;
}
void MusicPcmGain(float gain) {if(!(gain>=0))gain=0;if(gain>1)gain=1;InterlockedExchange(&requestedGain,(LONG)(gain*1000));}
void MusicPcmMood(float ratio) {if(!(ratio>=0))ratio=0;if(ratio>1)ratio=1;InterlockedExchange(&requestedMood,(LONG)(ratio*1000));}
void MusicPcmSeek(int sample44100) {if(sample44100>=0)InterlockedExchange(&requestedSeek,sample44100/2);}
void MusicPcmMix(float* left,float* right,int count) {
    int target=(int)InterlockedCompareExchange(&requestedScene,0,0);
    int seek=(int)InterlockedExchange(&requestedSeek,-1);
    float gain=(float)InterlockedCompareExchange(&requestedGain,0,0)*.001f;
    float mood=(float)InterlockedCompareExchange(&requestedMood,0,0)*.001f;
    if(seek>=0)seekPending=seek;
    for(int i=0;i<count;i++) {
        float desired=playingScene==target?gain:0.0f;
        busGain+=(desired-busGain)*.002f;
        if(playingScene!=target && busGain<.0001f) {
            playingScene=target;cursor=0;filterL=filterR=0;busGain=0;
            if(seek>=0 && playingScene>=0)cursor=seek%pcmTracks[playingScene].frames;
        }
        if(playingScene<0 || (gain<=0 && busGain<.0001f))continue;
        if(seekPending>=0){
            seekFade-=1.0f/128;
            if(seekFade<=0){cursor=seekPending%pcmTracks[playingScene].frames;seekPending=-1;seekFade=0;}
        }else if(seekFade<1){seekFade+=1.0f/128;if(seekFade>1)seekFade=1;}
        PcmTrack* p=&pcmTracks[playingScene];
        if(!p->samples || p->frames<2)continue;
        moodSmooth+=(mood-moodSmooth)*.00008f;
        float stress=moodSmooth>.8f?0:(.8f-moodSmooth)/.8f;
        int a=(int)cursor,b=(a+1)%p->frames;float fraction=(float)(cursor-a);
        float l=(p->samples[a*2]*(1-fraction)+p->samples[b*2]*fraction)/32768.0f;
        float r=(p->samples[a*2+1]*(1-fraction)+p->samples[b*2+1]*fraction)/32768.0f;
        left[i]+=l*busGain*seekFade;right[i]+=r*busGain*seekFade;
        detunePhase+=6.2831853*.37/MUSIC_PROC_RATE;
        if(detunePhase>6.2831853)detunePhase-=6.2831853;
        cursor+=1.0+stress*.0012*sin(detunePhase);
        while(cursor>=p->frames)cursor-=p->frames;
    }
}
int MusicPcmAvailable(int scene){return scene>=0 && scene<MUSIC_SCENE_COUNT && pcmTracks[scene].samples!=NULL;}
void MusicBusProcess(float* left,float* right,int count) {
    float mood=(float)InterlockedCompareExchange(&requestedMood,0,0)*.001f;
    float gain=(float)InterlockedCompareExchange(&requestedGain,0,0)*.001f;
    static float smooth=1,lpL=0,lpR=0;static double phase=0;
    for(int i=0;i<count;i++){
        smooth+=(mood-smooth)*.00008f;
        float stress=smooth>.8f?0:(.8f-smooth)/.8f;
        float crisis=fmaxf(0,(.5f-smooth)/.5f);
        float coefficient=1-expf(-6.2831853f*(14000-13100*stress)/MUSIC_PROC_RATE);
        lpL+=(left[i]-lpL)*coefficient;lpR+=(right[i]-lpR)*coefficient;
        noiseState=noiseState*1664525u+1013904223u;
        heartbeat+=(.8+stress*1.1)/MUSIC_PROC_RATE;if(heartbeat>=1)heartbeat-=1;
        phase+=1.0/MUSIC_PROC_RATE;if(phase>120)phase-=120;
        float noise=((float)(noiseState>>8)/8388608.0f-1)*stress*.018f;
        float thump=stress*.075f*expf(-(float)heartbeat*25)*sinf((float)heartbeat*270);
        float drone=(float)(sin(phase*6.2831853*55)+.55*sin(phase*6.2831853*58)+.35*sin(phase*6.2831853*83))*crisis*.042f;
        float tremor=1-stress*.22f*(.5f+.5f*sinf((float)phase*4.1f));
        left[i]=lpL*tremor+(noise+thump+drone)*gain;
        right[i]=lpR*tremor+(-noise*.65f+thump+drone*.82f)*gain;
    }
}
void MusicPcmShutdown(void) {
    InterlockedExchange(&requestedScene,-1);MusicPcmGain(0);
    /* Immutable score buffers live until process exit. ShutdownAudio has a
       bounded join; a late worker must never observe freed sample memory. */
}
#endif

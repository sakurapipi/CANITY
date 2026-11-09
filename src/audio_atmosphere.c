#include "common.h"
#include "audio.h"
/* Startup/offline synthesis only. The render callback consumes immutable PCM. */
void AudioBuildAtmosphere(short* output,int frames,int kind,int rate) {
    if(!output || frames<1 || rate<1)return;
    unsigned seed=19273u+(unsigned)kind*7919u;
    float low=0,previous=0;
    for(int i=0;i<frames;i++) {
        float t=(float)i/rate,pi=3.14159265f,v=0;
        seed=seed*1664525u+1013904223u;
        float noise=(float)(seed>>8)/8388608.0f-1;
        low+=(noise-low)*.08f;
        float high=noise-previous;previous=noise;
        switch(kind) {
        case 0:v=sinf(2*pi*(33*t+3*(1-expf(-t*18))))*expf(-t*6);break;
        case 1:v=(sinf(2*pi*62*t)*expf(-t*24)+sinf(2*pi*56*(t-.22f))*expf(-fabsf(t-.22f)*28)*.7f)*.7f;break;
        case 2:v=(high*.24f+sinf(2*pi*(1700*t+500*t*t))*.13f)*expf(-t*3);break;
        case 3:v=sinf(2*pi*(1600*t+80*(1-expf(-t*12))))*expf(-t*20);break;
        case 4:v=low*.9f*sinf(pi*fminf(1,t/.7f))*expf(-t*1.4f);break;
        case 5:v=(sinf(2*pi*50*t)*.55f+sinf(2*pi*100*t)*.2f+low*.1f)*(.8f+.2f*sinf(2*pi*7*t));break;
        case 6:v=low*sinf(2*pi*13*t)*expf(-t*2)*1.2f;break;
        case 7:v=(high*.18f+sinf(2*pi*320*t)*.1f)*expf(-t*7)*(.5f+.5f*sinf(2*pi*29*t));break;
        case 8:v=(sinf(2*pi*2131*t)+sinf(2*pi*2877*t)+high*.6f)*expf(-t*10)*.2f;break;
        case 9:v=(sinf(2*pi*(410*t+90*t*t))*.4f+high*.1f)*expf(-t*2);break;
        case 10:v=low*(.2f+.8f*sinf(pi*fminf(1,t)))*.9f;break;
        }
        float end=(float)(frames-i)/rate;
        float envelope=fminf(1,t/.004f)*fminf(1,end/.04f);
        v=tanhf(v*envelope)*.75f;
        output[i]=(short)(v*32767);
    }
}

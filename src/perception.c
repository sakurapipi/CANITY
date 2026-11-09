#include "common.h"
#include "perception.h"
PerceptionState perception={1,0,0,.12f,1,0,0,0};
static double lastTime=-1;
static float Smooth(float x) {x=fmaxf(0,fminf(1,x));return x*x*(3-2*x);}
PerceptionState PerceptionEvaluate(float ratio,double seconds) {
    PerceptionState p={0};
    if(!isfinite(ratio))ratio=1;
    p.ratio=fmaxf(0,fminf(1,ratio));
    p.stress=Smooth((SANITY_CLEAR_THRESHOLD-p.ratio)/SANITY_CLEAR_THRESHOLD);
    p.grain=PERCEPTION_GRAIN_MAX*p.stress;
    p.vignette=.12f+p.stress*.25f;p.flicker=1;
    p.distortion=Smooth((SANITY_UNEASE_THRESHOLD-p.ratio)/SANITY_UNEASE_THRESHOLD)*.018f;
    p.contamination=Smooth((SANITY_COLLAPSE_THRESHOLD-p.ratio)/SANITY_COLLAPSE_THRESHOLD);
    unsigned segment=(unsigned)(seconds/3.7);
    unsigned hash=segment*747796405u+2891336453u;hash^=hash>>16;
    float phase=(float)(seconds-segment*3.7);
    float at=.5f+(hash%2500)*.001f;
    if(p.stress>0 && hash%100<(unsigned)(12+p.stress*55) && phase>at && phase<at+.19f)
        p.flicker=1-(.015f+p.stress*.035f)*sinf((phase-at)*16.5347f);
    p.seed=(unsigned)(seconds*12.0)*277803737u;
    return p;
}
void PerceptionUpdate(float ratio,double seconds) {
    PerceptionState target=PerceptionEvaluate(ratio,seconds);
    float dt=lastTime<0?1:(float)(seconds-lastTime);lastTime=seconds;
    if(dt<0)dt=0;
    if(dt>1)dt=1;
    float k=1-expf(-dt*3);
    perception.ratio=target.ratio;
    perception.stress+=(target.stress-perception.stress)*k;
    perception.grain+=(target.grain-perception.grain)*k;
    perception.vignette+=(target.vignette-perception.vignette)*k;
    perception.distortion+=(target.distortion-perception.distortion)*k;
    perception.contamination+=(target.contamination-perception.contamination)*k;
    perception.flicker=target.flicker;perception.seed=target.seed;
    if(ratio>SANITY_CLEAR_THRESHOLD){perception.grain=0;perception.distortion=0;perception.contamination=0;}
}

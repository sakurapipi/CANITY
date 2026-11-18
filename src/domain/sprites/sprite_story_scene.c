#include "common.h"
#include "sprite_bake.h"
#include "story_visual.h"

static void Panel(float x,float y,float w,float h,unsigned top,unsigned bottom) {
    const float shape[]={x,y+8,x+8,y,x+w,y,x+w,y+h-8,x+w-8,y+h,x,y+h};
    cvPolyGrad(shape,6,top,bottom,1);
}
static void LampStrip(float x,float y,int n,unsigned color) {
    for (int i=0;i<n;i++) {
        cvGlow(x+i*15,y,12,color,.38f);
        cvLine(x+i*15,y,x+i*15+8,y,4,color,1);
    }
}
static void BakeSwitchingWorks(int fixture) {
    const unsigned copper=0xbe8043;
    switch (fixture) {
    case 0: /* Knife switch panel with visible ceramic posts. */
        Panel(47,26,162,202,0x626463,0x232821);
        for (int i=0;i<3;i++) {
            float x=79+i*48;
            cvEllipse(x,75,13,12,0xc8bd9a,1);
            cvEllipse(x,166,13,12,0xc8bd9a,1);
            cvLine(x,164,x+7,82,9,copper,1);
        }
        LampStrip(71,199,7,0xffbe67);break;
    case 1: /* Wound copper bus drums. */
        for (int i=0;i<2;i++) {
            float x=86+i*84;
            cvEllipse(x,151,43,58,0x292a28,1);
            for (int r=0;r<5;r++) cvEllipse(x,151,37-r*5,49-r*7,r%2?0x4d3023:copper,1);
            cvEllipse(x,151,12,19,0x23232b,1);
        }
        break;
    case 2: /* Angled maintenance drawing board. */
        Panel(55,50,144,123,0x8e623f,0x382e2a);
        Panel(66,60,122,91,0x3d4f3f,0x26362a);
        cvLine(87,120,100,90,5,0x9fd4aa,1);
        cvLine(100,90,155,90,5,0x9fd4aa,1);
        cvLine(155,90,171,78,5,0x9fd4aa,1);
        cvLine(72,172,61,231,9,0x4c4338,1);
        cvLine(187,172,195,231,9,0x4c4338,1);break;
    default: /* Cylindrical bus isolator. */
        Panel(101,35,55,190,0x564f42,0x242521);
        for (int i=0;i<7;i++) cvEllipse(128,59+i*23,43,9,i%2?0x7b6c52:0xba8653,1);
        cvLine(70,230,186,230,11,0x3e3730,1);break;
    }
}
static void BakeArchive(int fixture) {
    const unsigned iris=0xc09bff;
    switch (fixture) {
    case 0: /* Index kiosk with a floating purple catalog pane. */
        Panel(104,126,48,105,0x4d375d,0x171322);
        Panel(40,37,176,110,0x69508b,0x272038);
        Panel(54,49,148,83,0x332252,0x18142d);
        for (int i=0;i<5;i++) cvLine(67,66+i*12,187-(i%3)*22,66+i*12,3,iris,.83f);
        break;
    case 1: /* Tall rack of recorded witness cartridges. */
        Panel(49,17,158,218,0x57456f,0x201828);
        for (int i=0;i<5;i++) {
            Panel(63,30+i*37,130,29,0x675087,0x2f2443);
            LampStrip(74,47+i*37,7,i%2?iris:0x9761df);
        }
        break;
    case 2: /* Two independent reel heads on a low reader. */
        Panel(32,114,192,108,0x56446c,0x23182f);
        for(int i=0;i<2;i++) {
            float x=81+i*94;
            cvEllipse(x,143,33,40,0x211a33,1);
            cvEllipse(x,143,23,28,iris,.85f);
            cvEllipse(x,143,8,10,0x37244c,1);
        }
        cvLine(84,192,174,192,4,iris,.86f);break;
    default: /* Glass archive-core cabinet. */
        Panel(73,26,110,203,0x5a527b,0x1d1731);
        for (int i=0;i<5;i++) {
            const float z=65+i*29;
            cvEllipse(128,z,36,10,0x9c79ed,.8f);
            cvGlow(128,z,20,0x9056e5,.42f);
        }
        cvLine(124,40,124,202,5,0xdab7ff,.56f);break;
    }
}
static void BakeIsolation(int fixture) {
    const unsigned amber=0xf4bf40;
    switch (fixture) {
    case 0: /* Trestle barricade, stencilled safety chevrons. */
        Panel(29,93,198,81,amber,0x997022);
        for (int i=0;i<5;i++) cvLine(47+i*34,157,72+i*34,106,10,0x282722,1);
        cvLine(71,168,52,231,11,0x575244,1);
        cvLine(185,168,206,231,11,0x575244,1);break;
    case 1: /* Manual override with oversized round handwheel. */
        Panel(82,111,91,119,0x8a733a,0x342b21);
        cvEllipse(128,116,57,52,amber,1);
        cvEllipse(128,116,46,41,0x272426,1);
        cvLine(83,116,173,116,9,amber,1);
        cvLine(128,74,128,158,9,amber,1);
        break;
    case 2: /* Tall isolation warning beacon with guard cage. */
        Panel(94,78,68,157,0x76633b,0x2d271c);
        cvGlow(128,55,68,amber,.52f);
        cvEllipse(128,55,34,31,0xfadd79,1);
        cvLine(91,82,91,30,6,0x4d432e,1);
        cvLine(165,82,165,30,6,0x4d432e,1);
        cvLine(91,30,165,30,6,0x4d432e,1);
        break;
    default: /* Evacuation bollard with backlit arrow and base shoes. */
        Panel(71,25,115,204,0x8f794a,0x363027);
        Panel(84,47,89,98,0x3d3728,0x27261e);
        cvLine(129,127,129,66,8,amber,1);
        cvLine(104,91,129,66,8,amber,1);
        cvLine(129,66,154,91,8,amber,1);
        LampStrip(91,171,5,amber);
        cvLine(63,233,193,233,10,0x62553b,1);break;
    }
}

void BakeStorySceneArt(void) {
    for (int chapter=0;chapter<STORY_DECOR_CHAPTERS;chapter++) {
        for (int fixture=0;fixture<STORY_DECOR_PER_CHAPTER;fixture++) {
            cvClear();
            if (chapter==0) BakeSwitchingWorks(fixture);
            else if (chapter==1) BakeArchive(fixture);
            else BakeIsolation(fixture);
            cvFinish(SpriteBakeTarget(SPR_STORY_DECOR_FIRST+chapter*STORY_DECOR_PER_CHAPTER+fixture));
        }
    }
}

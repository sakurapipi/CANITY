#include "common.h"
#include "sprite_bake.h"

static void Rect(float x0,float y0,float x1,float y1,unsigned color) {
    float vertices[]={x0,y0,x1,y0,x1,y1,x0,y1};
    cvPoly(vertices,4,color,1);
}

static void Face(float x,float y,float w,float h,unsigned top,unsigned bottom) {
    float bevel[]={x,y+8,x+8,y,x+w-8,y,x+w,y+8,x+w,y+h,x,y+h};
    cvPolyGrad(bevel,6,top,bottom,1);
    cvLine(x+7,y+3,x+w-8,y+3,2,0x9eacaa,.65f);
}

static void Lamp(float x,float y,float size,unsigned color,int on) {
    if (on) cvGlow(x,y,size*3,color,.45f);
    cvEllipse(x,y,size,size,color,1);
    cvLine(x-size*.4f,y-size*.4f,x+size*.4f,y-size*.4f,2,0xe9f2e6,.55f);
}

static void Capacitor(int on,unsigned color) {
    Face(31,104,194,130,0x69747a,0x18272f);
    Rect(53,128,193,198,0x111e24);
    Face(45,100,152,30,0x525f5e,0x213239);
    for(int i=0;i<3;i++){
        float x=76+i*49;
        Face(x-16,133,32,58,0x2d4249,0x152025);
        Rect(x-10,142,x+10,181,0x172e3a);
        for (int j=0;j<4;j++) Rect(x-9,145+j*9,x+9,149+j*9,on?0x40857d:0x59432b);
        Lamp(x,116,4,color,on);
    }
    cvLine(54,209,202,209,3,0xc69447,1);
    for(int i=0;i<8;i++) cvLine(62+i*18,218,70+i*18,211,3,0xc69447,1);
    Rect(43,231,78,244,0x131618);
    Rect(177,231,211,244,0x131618);
}

static void Pod(int on,unsigned color) {
    Face(41,25,174,213,0x606e75,0x14252b);
    Face(55,40,147,181,0x374f56,0x233744);
    float glass[]={75,71,96,51,164,51,187,73,187,181,167,207,94,207,75,180};
    cvPolyGrad(glass,8,on?0x396e73:0x263747,on?0x172c33:0x111a24,1);
    cvLine(81,77,98,58,3,0x91b5bb,.85f);
    cvLine(82,86,82,174,3,0x79a4af,.65f);
    cvEllipse(130,100,21,20,0x122332,.85f);
    cvLine(130,129,130,177,37,0x152937,.75f);
    Face(174,135,28,44,0x718080,0x29367c);
    Lamp(187,148,5,color,on);
    Rect(184,160,190,170,0x9eada4);
    for(int i=0;i<5;i++) Rect(94+i*16,218,102+i*16,224,0x839797);
    Rect(31,231,220,244,0x101c24);
}

static void Radio(int on,unsigned color) {
    Face(53,194,150,48,0x58676a,0x161f22);
    Face(83,120,90,85,0x536573,0x202f35);
    Face(49,63,159,94,0x728189,0x27383a);
    Rect(64,79,156,132,0x0d171a);
    Rect(70,85,150,126,on?0x224b49:0x282c35);
    for (int i=0;i<6;i++) {
        float y=91+i*5;
        cvLine(78,y,143-(i%3)*12,y,2,on?0x8bd2af:0x4a5359,.95f);
    }
    for(int i=0;i<6;i++) Rect(168,84+i*5,192,86+i*5,0x171c21);
    Lamp(180,125,5,color,on);
    cvLine(175,62,194,20,4,0x748088,1);
    cvEllipse(194,20,4,4,0x9ca4a0,1);
    cvLine(57,144,197,144,3,0x9a704b,1);
}

static void Core(int on,unsigned color) {
    Face(65,27,126,210,0x53606a,0x202930);
    Rect(79,48,177,165,0x141e27);
    cvEllipse(128,105,40,39,0x697980,1);
    cvEllipse(128,105,32,31,0x233637,1);
    Lamp(128,105,17,color,on);
    cvLine(89,192,165,192,5,0x9a7c51,1);
    cvLine(110,203,on?147:111,on?176:168,9,0xcad4ca,1);
    Rect(48,229,208,244,0x141c23);
}

static void Route(int on,unsigned color) {
    Face(65,140,124,98,0x637175,0x1d2b33);
    Face(45,88,166,78,0x4d5e61,0x263443);
    Rect(61,104,194,150,0x142123);
    cvLine(78,137,121,115,4,0x72abc5,1);
    cvLine(121,115,173,115,4,0x72abc5,1);
    cvLine(126,137,173,137,4,0xc5a461,1);
    Lamp(176,137,5,color,on);
    Lamp(176,115,5,0x67aaca,!on);
    Rect(80,172,176,181,0x18282c);
}

static void Hatch(int open,unsigned color) {
    Face(38,22,180,219,0x718189,0x203039);
    Rect(57,48,199,233,0x10191d);
    Face(61,54,open?45:135,176,0x526f76,0x28363c);
    cvLine(62,124,open?103:195,124,4,0x25313a,1);
    cvLine(62,183,open?103:195,183,4,0x25313a,1);
    cvLine(open?110:176,59,open?110:176,224,5,0xb19461,.8f);
    Lamp(128,35,5,color,open);
    Rect(43,232,213,244,0x253039);
}

static void BakeBarrier(void) {
    cvClear();
    float mesh[]={36,105,220,105,236,230,20,230};
    cvPolyGrad(mesh,4,0x449ba6,0x183756,.35f);
    for (int i=0;i<7;i++) {
        float x=43+i*28;
        cvLine(x,109,x-11,223,2,0x71cad4,.6f);
    }
    for (int j=0;j<5;j++) cvLine(34,111+j*25,222,111+j*25,2,0x5fa6e7,.4f);
    for (int i=0;i<2;i++) {
        float x=i?207:19;
        Face(x,96,30,145,0x687a85,0x151c27);
        Rect(x+5,133,x+22,219,0x244456);
        cvLine(x+13,135,x+13,215,3,0x89e8ee,1);
        Lamp(x+14,111,5,0x7ddff1,1);
    }
    cvLine(30,230,221,230,5,0x84d4ee,.9f);
    cvFinish(SpriteBakeTarget(SPR_ABILITY_FIELD));
}

static void BakeDouble(void) {
    cvClear();
    cvGlow(127,111,67,0x755996,.38f);
    cvEllipse(126,52,22,29,0x8db2c6,.78f);
    float shoulders[]={83,96,109,78,144,78,176,94,188,171,164,179,154,126,
        151,171,163,225,141,238,126,183,111,238,89,226,102,167,101,127,91,178,68,170};
    cvPolyGrad(shoulders,20,0x8ca8bb,0x434c81,.85f);
    cvLine(107,81,91,95,3,0xcaddf7,.85f);
    cvLine(151,86,174,102,3,0x9bebdd,.7f);
    cvLine(111,51,140,51,3,0xe1e8e4,.8f);
    for (int i=0;i<8;i++) {
        float y=90+i*14;
        cvLine(92+(i%3)*10,y,159+(i%2)*10,y,2,0x98aaeb,.45f);
    }
    cvFinish(SpriteBakeTarget(SPR_ABILITY_DOUBLE));
}

static void DrawGlove(void) {
    const float arm[]={55,259,75,214,101,178,128,169,158,189,155,226,143,259};
    cvPolyGrad(arm,7,0x4a5c66,0x191f28,1);
    cvLine(67,243,152,242,5,0x384858,1);
    cvEllipse(127,187,28,27,0x526773,1);
    cvLine(102,183,153,176,6,0x7b908e,.7f);
}
static void BakeRoleHand(int id) {
    cvClear();
    DrawGlove();
    switch (id) {
    case SPR_ABILITY_EMITTER:
        Face(94,91,88,103,0x5b737a,0x233341);
        Rect(105,123,171,168,0x102634);
        for(int j=0;j<3;j++) Lamp(119+j*20,139,5,0x80e0ec,1);
        cvLine(106,91,96,64,7,0x7bacbf,1);
        cvLine(164,92,174,64,7,0x7bacbf,1);
        cvGlow(135,79,34,0x65d7ea,.55f);
        cvLine(97,64,134,78,3,0xa4ffff,.85f);
        cvLine(134,78,174,64,3,0xa4ffff,.85f);
        break;
    case SPR_ABILITY_SHIELD: {
        const float rim[]={32,47,70,23,180,21,219,45,222,148,187,205,
                           117,228,55,191,29,141};
        const float armor[]={41,53,76,34,174,32,207,54,208,143,178,193,
                             119,213,65,181,40,138};
        cvPolyGrad(rim,9,0x71808b,0x1a232e,1);
        cvPolyGrad(armor,9,0x4b616c,0x263642,1);
        Rect(63,69,184,109,0x12232e);
        Rect(68,75,179,103,0x315563);
        cvLine(76,83,171,78,3,0xa5d5d1,.85f);
        for(int j=0;j<4;j++) {
            cvLine(69,129+j*12,178,129+j*12,5,0x243743,1);
            cvLine(72,128+j*12,176,128+j*12,2,0x6e8687,.65f);
        }
        Lamp(183,119,5,0xd4ad6c,1);
        Rect(92,163,145,183,0x131f28);
        break;
    }
    case SPR_ABILITY_PROJECTOR:
        Face(96,122,71,64,0x706385,0x2f243c);
        cvEllipse(132,125,30,17,0x514673,1);
        Lamp(132,117,13,0xbb9df0,1);
        cvLine(132,108,111,76,2,0xbbaafa,.6f);
        cvLine(132,108,152,76,2,0xbbaafa,.6f);
        break;
    default:
        Face(106,145,63,46,0x4c666c,0x2a333a);
        Rect(114,155,161,177,0x122b2f);
        for(int i=0;i<5;i++) Rect(119+i*7,161,123+i*7,171,0x7bc4a1);
        break;
    }
    cvFinish(SpriteBakeTarget(id));
}

static void BakeMissionStation(int sprite,int on) {
    cvClear();
    unsigned status=on?0x9DFFE0:0xEEA768;
    if (sprite==SPR_MISSION_COIL) {
        Face(36,203,183,38,0x69635d,0x262a30);
        for (int i=0;i<2;i++) {
            float x=65+126*i;
            cvLine(x,194,x,64,19,0x6f7579,1);
            cvEllipse(x,61,23,12,0x9c9180,1);
            for (int j=0;j<5;j++) cvLine(x-16,82+19*j,x+16,82+19*j,5,0xD08952,1);
        }
        cvLine(65,55,116,22,8,0x545b63,1);
        cvLine(192,55,140,22,8,0x545b63,1);
        Lamp(128,29,10,status,on);
        cvLine(92,115,164,115,3,status,.65f);
    } else if (sprite==SPR_MISSION_RECEIVER) {
        Face(100,164,56,77,0x657a89,0x212833);
        for (int i=0;i<2;i++) {
            float x=71+114*i;
            cvLine(x,203,x,90,11,0x476070,1);
            cvEllipse(x,96,32,47,0x253346,1);
            cvEllipse(x,94,23,36,0x57687a,1);
            cvEllipse(x,94,14,26,0x10232f,1);
            Lamp(x,94,7,status,on);
        }
        cvLine(99,149,118,137,3,status,.8f);
        cvLine(118,137,138,155,3,status,.8f);
        cvLine(138,155,157,145,3,status,.8f);
    } else if (sprite==SPR_MISSION_SHUTTER) {
        Face(22,44,212,198,0x65717e,0x242b34);
        Rect(38,62,218,227,0x151c23);
        for (int i=0;i<4;i++) {
            int y=66+i*34;
            Rect(45,y,211,y+27,i%2?0x584a3c:0x766147);
            cvLine(46,y,208,y,4,0xA89A79,1);
        }
        Lamp(128,53,7,status,on);
        cvLine(97,128,128,107,7,status,1);
        cvLine(128,107,159,128,7,status,1);
        cvLine(97,154,128,133,7,status,1);
        cvLine(128,133,159,154,7,status,1);
    }
    cvFinish(SpriteBakeTarget(sprite+on));
}

static void BakeChapterProp(int base,int chapter,int on) {
    static const unsigned skin[][3]={
        {0x9E7750,0x443127,0xF7BA77},
        {0x728C92,0x273941,0xA9DDD8},
        {0x828663,0x383C2C,0xDAD986}
    };
    const unsigned *p=skin[chapter];
    unsigned lit=p[2],dark=p[1],metal=p[0],signal=on?lit:0x776450;
    cvClear();
    switch (base) {
    case SPR_MISSION_TERMINAL:
        if (chapter==0) {
            Face(47,56,161,177,metal,dark);
            Rect(64,77,189,157,dark);
            for(int i=0;i<4;i++) Lamp(83+i*28,109,6,signal,on);
            cvLine(78,185,173,183,7,lit,.65f);
            Rect(44,225,212,240,dark);
        } else if (chapter==1) {
            Face(89,110,76,124,metal,dark);
            Face(28,76,200,80,metal,dark);
            Rect(44,88,212,139,dark);
            for(int i=0;i<14;i++) {
                int h=3+(i*17)%23;
                cvLine(56+i*10,114-h/2,56+i*10,114+h/2,3,signal,.9f);
            }
            Rect(49,226,207,240,dark);
        } else {
            Face(44,144,170,95,metal,dark);
            Face(64,65,128,93,metal,dark);
            Rect(79,80,177,141,dark);
            for(int i=0;i<3;i++) cvLine(92,94+i*14,162,94+i*14,4,signal,.8f);
            Lamp(170,184,8,signal,on);
        }
        break;
    case SPR_MISSION_CASE:
        if (chapter==0) {
            Face(23,148,210,88,metal,dark);
            cvLine(30,174,225,174,6,dark,1);
            Rect(107,180,149,196,signal);
        } else if (chapter==1) {
            Face(37,135,182,101,metal,dark);
            for(int i=0;i<3;i++){
                cvLine(50,165+i*21,207,165+i*21,3,dark,1);
                Rect(113,152+i*21,143,158+i*21,signal);
            }
        } else {
            Face(44,152,168,84,metal,dark);
            cvLine(65,161,65,232,8,dark,1);
            cvLine(191,161,191,232,8,dark,1);
            Rect(103,177,153,188,signal);
        }
        if(on) Rect(38,144,218,151,0x131b20);
        break;
    case SPR_MISSION_COVER:
        if (chapter==0) {
            Face(33,59,190,171,metal,dark);
            Rect(50,80,206,227,dark);
            for(int i=0;i<5;i++) cvLine(55,87+i*10,201,87+i*10,3,lit,.38f);
            cvLine(160,150,160,188,5,metal,1);
        } else if (chapter==1) {
            Face(49,37,158,198,metal,dark);
            Rect(65,61,191,218,dark);
            Rect(75,75,181,152,0x385355);
            cvLine(82,80,82,145,3,lit,.55f);
            cvLine(173,164,173,193,5,lit,.7f);
        } else {
            Face(29,104,198,134,metal,dark);
            Rect(47,125,209,221,dark);
            for(int i=0;i<4;i++) cvLine(60,142+i*18,194,142+i*18,4,metal,1);
            Rect(100,112,156,120,lit);
        }
        break;
    case SPR_MISSION_EXIT:
        Face(23,18,210,222,metal,dark);
        Rect(41,38,215,233,0x131a1d);
        if(chapter==0) {
            for(int i=0;i<(on?2:8);i++) Rect(52,62+i*20,204,78+i*20,metal);
            cvLine(46,45,46,230,5,lit,.6f);
            cvLine(210,45,210,230,5,lit,.6f);
        } else if(chapter==1) {
            Rect(49,50,on?83:206,229,metal);
            cvEllipse(on?68:128,108,on?12:32,25,dark,1);
            cvLine(on?74:114,163,on?82:141,163,4,lit,.75f);
        } else {
            Rect(50,58,84,232,metal);
            Rect(on?177:85,58,206,232,metal);
            for(int i=0;i<6;i++) cvLine(53,68+i*28,80,87+i*28,3,lit,.55f);
        }
        Lamp(128,32,7,signal,on);
        break;
    default:
        if(chapter==0){
            Face(54,103,149,129,metal,dark);
            Rect(71,124,184,207,dark);
            Lamp(128,165,25,signal,on);
            cvLine(123,146,140,178,5,metal,1);
        } else if(chapter==1) {
            Face(36,142,184,90,metal,dark);
            cvEllipse(85,146,32,32,dark,1);
            cvEllipse(170,146,32,32,dark,1);
            Lamp(85,146,12,signal,on);
            Lamp(170,146,12,signal,on);
            cvLine(85,165,170,166,4,lit,.65f);
        } else {
            Face(50,78,156,157,metal,dark);
            Rect(67,100,189,206,dark);
            for(int i=0;i<3;i++) {
                cvLine(81,126+i*23,160,126+i*23,4,metal,1);
                Lamp(172,126+i*23,4,signal,on);
            }
        }
        break;
    }
    cvFinish(SpriteBakeTarget(base+chapter*2+on));
}

static void BakeJammerBurst(void) {
    cvClear();
    cvGlow(128,132,96,0x6255d5,.34f);
    for (int i=0;i<8;i++) {
        float a=i*6.2831853f/8,c=cosf(a),s=sinf(a);
        float bx=128+c*44,by=128+s*44;
        cvLine(bx,by,128+c*69-s*13,128+s*69+c*13,5,0x82e6ff,.9f);
        cvLine(128+c*69-s*13,128+s*69+c*13,128+c*109,128+s*109,3,0xc7b0ff,.8f);
    }
    cvEllipse(128,128,12,12,0xe3efff,.9f);
    cvFinish(SpriteBakeTarget(SPR_JAMMER_BURST));
}

void BakeProtocolProps(void) {
    for (int sprite=SPR_MISSION_COIL;sprite<=SPR_MISSION_SHUTTER;sprite+=2)
        for (int on=0;on<2;on++) BakeMissionStation(sprite,on);
    const int chapterProps[]={SPR_MISSION_TERMINAL,SPR_MISSION_CASE,SPR_MISSION_COVER,SPR_MISSION_EXIT,SPR_MISSION_RECORD};
    for (unsigned i=0;i<sizeof chapterProps/sizeof chapterProps[0];i++)
        for(int chapter=0;chapter<3;chapter++)
            for(int on=0;on<2;on++) BakeChapterProp(chapterProps[i],chapter,on);
    BakeJammerBurst();
    for (int kind=STORY_CAPACITOR;kind<=STORY_HATCH;kind++) {
        for (int on=0;on<2;on++) {
            unsigned color=on?0x67e0c2:0xbe8350;
            cvClear();
            cvEllipse(128,236,100,11,0x071013,.85f);
            switch(kind) {
            case STORY_CAPACITOR: Capacitor(on,color);break;
            case STORY_POD: Pod(on,color);break;
            case STORY_RADIO: Radio(on,color);break;
            case STORY_CORE: Core(on,color);break;
            case STORY_ROUTE: Route(on,color);break;
            case STORY_HATCH: Hatch(on,color);break;
            }
            cvFinish(SpriteBakeTarget(SPR_STORY_PROP_BASE+kind*2+on));
        }
    }
    BakeBarrier();
    BakeDouble();
    for (int id=SPR_ABILITY_EMITTER;id<=SPR_ABILITY_WRIST;id++) BakeRoleHand(id);
}

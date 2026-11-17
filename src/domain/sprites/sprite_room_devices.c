/* Industrial room landmarks. Three baked states per identity, not seven recolors.
 * Authoring remains on the 256px logical canvas; no camera/postprocess transforms. */
#include "sprite_bake.h"
#include "common.h"
#include "art.h"
static const unsigned roomAccent[7]={0xD59C52,0x7EB1D1,0x7CDEED,0x93CDB7,0xCEDDEB,0xB93228,0x9C3A38};
static void DeviceRivet(float x,float y) {
    cvEllipse(x,y,3,3,0x101416,1);cvEllipse(x-1,y-1,1,1,0xA7ADA6,.8f);
}
static void DeviceBase(void) {
    cvEllipse(128,244,78,9,0x030609,.65f);
    cvPolyGrad((float[]){47,236,62,218,193,218,212,236},4,0x566367,0x171F25,1);
    for(int x=65;x<200;x+=26)DeviceRivet((float)x,230);
}
static void ResonanceWell(int state,unsigned accent) {
    for(int side=-1;side<=1;side+=2) {
        float x=128+side*76;
        cvLine(x,219,x,75,13,0x16232B,1);cvLine(x-2,216,x-2,78,5,0x738083,1);
        cvEllipse(x,72,22,11,0x697575,1);cvEllipse(x,71,15,7,0x080E15,1);
        for(int y=99;y<209;y+=25)cvLine(x-9,(float)y,x+9,(float)y,4,0xA59167,.75f);
    }
    cvEllipse(128,202,64,29,0x8B8F80,1);cvEllipse(128,199,54,23,0x070D13,1);
    for(int i=0;i<5;i++)cvEllipse(128,198-i*2,43-i*7,17-i*2,0x172735,.8f);
    cvEllipse(128,189,17,5,state==1?accent:0x040709,1);
    for(int i=0;i<9;i++)cvLine(75+i*13,212,79+i*12,222,3,0xA1A38C,.8f);
    if(state==1)cvGlow(128,190,28,accent,.30f);
    if(state==2)cvLine(106,182,152,202,4,0x0B1016,.85f);
}
static void DebtVault(int state,unsigned accent) {
    cvPolyGrad((float[]){51,221,51,45,202,45,207,221},4,0x697579,0x1C2832,1);
    for(int row=0;row<4;row++)for(int column=0;column<3;column++) {
        float x=59+column*46,y=76+row*33,open=(state==1 && (row+column)%2)?8:0;
        cvPolyGrad((float[]){x,y,x,y-24,x+39,y-24,x+39+open,y+open},4,0x4D626B,0x14212B,1);
        cvLine(x+12,y-12,x+27,y-12,3,accent,.85f);
        DeviceRivet(x+4,y-19);
    }
    cvPolyGrad((float[]){96,55,96,23,161,23,161,55},4,0x7E8276,0x23343D,1);
    for(int digit=0;digit<4;digit++) {
        cvPoly((float[]){103+digit*13,31,112+digit*13,31,112+digit*13,45,103+digit*13,45},4,0x0B1318,1);
        cvLine(105+digit*13,35,110+digit*13,35,1,state==1?accent:0x5A6F73,1);
    }
    if(state==2)cvLine(68,43,188,216,2,0x1B2832,.7f);
}
static void OverloadBeacon(int state,unsigned accent) {
    cvLine(128,219,128,33,19,0x233844,1);cvLine(124,216,124,35,6,0xA3BABD,.9f);
    for(int i=0;i<3;i++) {
        float y=64+i*52;
        cvEllipse(128,y,35,9,0x7F6A4F,1);cvEllipse(128,y-2,27,6,0x162C39,1);
        cvLine(128,y-16,128,y+9,9,state==1?accent:0x4C6D78,1);
        if(state==1)cvGlow(128,y-5,18,accent,.2f);
    }
    cvLine(76,219,80,163,9,0x4B5C66,1);cvLine(181,220,175,159,9,0x52666B,1);
    for(int i=0;i<4;i++){cvEllipse(80,174+i*10,12,4,0xA0B2AF,1);cvEllipse(175,172+i*10,12,4,0x9DAFAE,1);}
    if(state==2){cvLine(114,83,133,113,5,0x0B131B,1);cvLine(130,116,113,139,3,0x161E25,1);}
}
static void TransplantPods(int state,unsigned accent) {
    for(int side=-1;side<=1;side+=2) {
        float x=128+side*49;
        cvPolyGrad((float[]){x-29,219,x-29,57,x-20,42,x+22,42,x+29,61,x+29,219},6,0xA1B7AE,0x283F43,1);
        cvPoly((float[]){x-20,70,x+20,70,x+20,199,x-20,199},4,0x0A202D,1);
        unsigned fluid=side<0?0x4794C1:accent;
        float top=state==1?(side<0?129:91):state==2?165:111;
        cvPolyGrad((float[]){x-17,top,x+17,top,x+17,194,x-17,194},4,fluid,0x142E34,.86f);
        cvLine(x-14,78,x-14,180,2,0xC5D8CC,.38f);
        for(int y=90;y<191;y+=20)cvLine(x+14,(float)y,x+20,(float)y,1,0xA2B9AF,.8f);
    }
    cvLine(99,59,155,59,9,0x526B6B,1);cvLine(128,58,128,143,6,0x8B9E89,1);
    cvEllipse(128,147,12,12,state==1?accent:0x394B4B,1);
}
static void ReturnFrames(int state,unsigned accent) {
    cvPolyGrad((float[]){53,220,53,46,97,29,100,218,86,218,83,56,69,61,69,220},8,0x9BAFBA,0x253441,1);
    cvPolyGrad((float[]){151,219,155,79,192,52,204,60,200,218,185,218,187,83,170,91,167,219},8,0x718D9D,0x192833,1);
    cvLine(79,181,180,201,3,accent,state==1?.9f:.35f);
    cvLine(81,130,181,153,2,accent,state==1?.65f:.2f);
    cvPoly((float[]){103,213,135,204,128,197,155,204,135,217,137,209},6,accent,.9f);
    if(state==1){cvGlow(84,91,12,accent,.25f);cvGlow(188,126,10,accent,.25f);}
    if(state==2)cvLine(60,51,84,97,4,0x111F2B,.85f);
}
static void CovenantPress(int state,unsigned accent) {
    cvLine(66,221,66,58,18,0x3E4948,1);cvLine(190,221,190,58,18,0x2C3B3E,1);
    cvPolyGrad((float[]){46,57,51,27,205,27,212,57},4,0x7F7963,0x2F3436,1);
    cvLine(128,56,128,state==2?160:112,27,0x576063,1);
    float y=state==2?172:127;
    cvPolyGrad((float[]){83,y,91,y-23,165,y-23,175,y},4,0x7E8170,0x1B272E,1);
    cvEllipse(128,205,42,11,0x121C23,1);cvEllipse(128,202,28,6,accent,state==2?.95f:.4f);
    for(int i=0;i<5;i++)cvLine(84+i*21,221,90+i*18,188-i%2*17,3,0x74302D,.8f);
    if(state==2){cvLine(95,182,152,221,5,accent,.8f);cvLine(151,181,116,220,3,accent,.75f);}
}
static void BorrowedHeart(int state,unsigned accent) {
    cvLine(63,220,63,45,13,0x455354,1);cvLine(193,220,193,45,13,0x34474A,1);
    cvLine(63,44,193,44,13,0x71817A,1);
    for(int side=-1;side<=1;side+=2)for(int rib=0;rib<5;rib++) {
        float y=81+rib*25;
        cvLine(128+side*66,y,128+side*43,y+12,5,0x89947D,1);
        cvLine(128+side*43,y+12,128+side*31,y+9,4,0x4A5D5C,1);
    }
    cvEllipse(116,121,29,44,state==2?0x452B2E:0x852E32,1);
    cvEllipse(143,114,27,37,state==2?0x4D3030:0xA2433D,1);
    cvPolyGrad((float[]){92,131,164,127,132,190},3,accent,0x321F29,1);
    cvLine(119,81,112,48,10,0x6C3833,1);cvLine(143,79,159,47,9,0x643334,1);
    cvEllipse(130,134,15,15,0x19232A,1);cvLine(130,134,130,123,2,0xD3B890,1);
    cvLine(130,134,141,138,2,0xBEA580,1);
    if(state!=1){cvLine(130,187,130,214,2,0xA39D7F,1);cvEllipse(130,215,11,6,0x82735B,1);}
}
static int motionPhase=0; /* Startup baking only; never shared with the render thread. */
static void RunningMechanism(int theme,unsigned accent) {
    float shift=(float)(motionPhase-1)*5;
    switch(theme) {
    case 0:cvEllipse(128,194,25+shift,8+shift*.25f,accent,.65f);break;
    case 1:cvLine(145,35,145+shift,42,2,0xE1DBC0,1);break;
    case 2:cvEllipse(128,71+shift*3,5,5,0xD0F2D6,.9f);break;
    case 3:
        for(int i=0;i<3;i++){cvEllipse(89,157-i*18-shift,2,3,0xB0DFD1,.9f);cvEllipse(167,135-i*15+shift,2,3,0xBDD9B1,.9f);}break;
    case 4:cvLine(91+shift,181,104+shift,163,5,0xC4D6D1,1);break;
    case 5:cvLine(102,125+shift,157,125+shift,7,0x8F8A72,1);break;
    case 6:cvLine(130,187,130+shift*2,214,3,0xC1AD84,1);cvEllipse(130+shift*2,215,10,5,accent,1);break;
    }
}
void BakeRoomLandmark(int id,int theme,int state) {
    SpriteBakeCanvas()->scale=2;cvClear();DeviceBase();
    unsigned accent=roomAccent[theme];
    switch(theme) {
    case 0:ResonanceWell(state,accent);break;
    case 1:DebtVault(state,accent);break;
    case 2:OverloadBeacon(state,accent);break;
    case 3:TransplantPods(state,accent);break;
    case 4:ReturnFrames(state,accent);break;
    case 5:CovenantPress(state,accent);break;
    case 6:BorrowedHeart(state,accent);break;
    }
    if(state==1)RunningMechanism(theme,accent);
    cvFinish(SpriteBakeTarget(id));SpriteBakeCanvas()->scale=1;
}
void BakeRoomPortal(int id,int theme) {
    cvClear();unsigned accent=roomAccent[theme];
    float left=theme==4?37:43,right=theme==4?211:213;
    cvLine(left,246,left,27,14,0x1B2A33,1);cvLine(right,246,right,theme==4?62:27,14,0x283B43,1);
    cvLine(left,27,right,theme==4?62:27,14,0x6E7D7C,1);
    for(int i=0;i<6;i++) {
        float y=55+i*30;
        if(theme==0 || theme==6) {cvEllipse(left,y,12,5,0x89958B,1);cvEllipse(right,y,12,5,0x5D7779,1);}
        else {cvLine(left-5,y,left+5,y,4,accent,.8f);cvLine(right-5,y,right+5,y,4,accent,.8f);}
    }
    if(theme==1)for(int x=67;x<200;x+=21)cvLine((float)x,24,(float)x,37,5,0x9EA9A1,1);
    if(theme==2){cvEllipse(128,24,18,8,accent,.8f);cvGlow(128,26,14,accent,.18f);}
    if(theme==3){cvLine(51,73,51,156,4,0x6AC9DE,.8f);cvLine(205,73,205,156,4,0xA0D9A6,.8f);}
    if(theme==5)for(int i=0;i<9;i++)cvLine(45+i*19,33,51+i*18,51+i%3*8,3,0x642D28,.8f);
    cvFinish(SpriteBakeTarget(id));
}
void BakeRoomControlState(int id,int theme,int side,int state) {
    cvClear();unsigned accent=roomAccent[theme];
    cvLine(106,242,110,137,17,0x31454D,1);cvLine(152,242,147,137,11,0x21343E,1);
    cvPolyGrad((float[]){63,139,75,88,183,88,197,139},4,0x788D8E,0x283E48,1);
    cvPoly((float[]){88,124,93,98,163,98,172,124},4,0x0A1B26,1);
    if(theme==3) {
        unsigned fluid=side?0x9DD4AB:0x72C9E1;
        cvLine(106,111,151,111,5,fluid,.9f);
        cvLine(side?107:149,111,side?117:139,104,3,fluid,.9f);
    }else if(theme==5 || theme==1) {
        cvLine(101,106,155,117,3,side?0xD47A69:accent,.9f);
        cvLine(101,117,155,106,3,side?0xD47A69:accent,.9f);
    }else {
        cvEllipse(129,111,15,8,accent,.9f);cvLine(129,111,side?141:119,104,3,0xD7DEC7,1);
    }
    if(state) {
        /* Latched lever and a live meter, rather than brightness alone. */
        cvLine(109,123,109,state==1?91:94,5,0xBBD4B3,1);
        cvEllipse(109,state==1?89:92,5,3,accent,1);
        cvLine(142,118,142,101,2,accent,1);
        cvLine(142,109,state==1?153:149,state==1?102:99,2,0xE0DFB7,1);
        cvEllipse(177,99,4,3,state==1?0xA5DFB4:accent,1);
    }
    DeviceRivet(78,129);DeviceRivet(181,129);
    cvFinish(SpriteBakeTarget(id));
}

void BakeRoomControl(int id,int theme,int side) {BakeRoomControlState(id,theme,side,0);}

void BakeRoomLandmarkMotion(int id,int theme,int phase) {
    motionPhase=phase;BakeRoomLandmark(id,theme,1);motionPhase=0;
}

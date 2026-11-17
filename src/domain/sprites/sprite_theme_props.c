/* Seven material families; shared gameplay identity, distinct silhouettes. */
#include "sprite_bake.h"
#include "common.h"
static const unsigned body[7]={0x817365,0x71858B,0x77372D,0xA1B4A0,0x568DA6,0x977344,0x824B42};
static const unsigned lamp[7]={0xC8B58D,0xA6C1CF,0xD26C4F,0xA8DFC0,0x8AE3E8,0xDFB56A,0xD9947D};
static void Rivets(float x,float y,int n) {
    for(int i=0;i<n;i++){cvEllipse(x+i*20,y,2,2,0x101719,1);cvEllipse(x+i*20-1,y-1,1,1,0xB3B8A4,.7f);}
}
static void HideShell(int theme) {
    unsigned c=body[theme],a=lamp[theme];
    cvEllipse(128,238,68,9,0x05090B,.7f);
    switch(theme) {
    case 0: /* Masonry alcove and a heavy timber shutter. */
        cvEllipse(128,54,60,35,c,1);
        cvPolyGrad((float[]){66,58,190,58,190,233,66,233},4,c,0x302A26,1);
        cvEllipse(128,62,43,24,0x202526,1);
        cvPoly((float[]){83,62,173,62,173,224,83,224},4,0x3C3B32,1);
        for(int x=94;x<170;x+=18)cvLine(x,70,x,223,2,0x171E20,.8f);
        cvLine(85,103,171,103,5,0x72674B,1);cvLine(85,195,171,195,5,0x72674B,1);break;
    case 1: /* Steel shelter with a barred inspection slit. */
        cvPolyGrad((float[]){61,232,61,40,190,40,199,232},4,c,0x28353A,1);
        cvPoly((float[]){75,53,180,53,180,219,75,219},4,0x35444B,1);
        cvPoly((float[]){87,72,166,72,166,107,87,107},4,0x071219,1);
        for(int x=95;x<165;x+=14)cvLine(x,72,x,107,3,0x8C9896,1);
        Rivets(76,44,6);break;
    case 2: /* Red confessional, pitched canopy and carved lattice. */
        cvPoly((float[]){55,71,128,20,202,71},3,0x4D2420,1);
        cvPolyGrad((float[]){66,71,190,71,190,233,66,233},4,c,0x28191B,1);
        cvPoly((float[]){86,83,168,83,168,226,86,226},4,0x281A20,1);
        for(int i=0;i<5;i++){cvLine(90+i*15,88,90+i*15,129,2,0xA47C5A,.8f);}
        cvLine(79,76,79,228,6,0xA78463,1);cvLine(177,76,177,228,6,0xA78463,1);break;
    case 3: /* Hospital privacy capsule, curtain instead of a cupboard door. */
        cvLine(60,235,60,42,8,0x718F8B,1);cvLine(197,235,197,42,8,0x718F8B,1);
        cvLine(60,42,197,42,9,0xBED0BB,1);
        cvPolyGrad((float[]){67,50,190,50,190,225,67,225},4,c,0x435D59,1);
        for(int i=0;i<7;i++)cvLine(74+i*17,52,74+i*17,221,5,i%2?0x566E61:0xBCCBBB,.65f);
        cvLine(130,82,130,112,5,0x799F91,1);cvLine(116,97,144,97,5,0x799F91,1);break;
    case 4: /* Frosted chamber: angled shoulders, round pressure window. */
        cvPolyGrad((float[]){62,227,62,64,86,35,170,35,196,64,196,227},6,c,0x1F4052,1);
        cvEllipse(128,102,39,43,0xBEDADB,1);cvEllipse(128,102,30,34,0x244451,1);
        for(int i=0;i<4;i++)cvLine(100+i*15,76,94+i*14,123,2,0x7CBBCE,.6f);
        cvPoly((float[]){80,156,177,156,177,212,80,212},4,0x2A4D5E,1);break;
    case 5: /* Industrial boiler refuge with side pipes and inspection wheel. */
        cvEllipse(128,62,58,29,c,1);
        cvPolyGrad((float[]){70,61,186,61,186,221,70,221},4,c,0x403728,1);
        cvEllipse(128,219,58,13,0x574B36,1);
        cvLine(51,218,51,74,12,0x5C5946,1);cvLine(207,218,207,98,12,0x5C5946,1);
        cvEllipse(128,135,32,40,0x26312F,1);cvEllipse(128,135,19,23,0x5A6B5C,1);
        cvLine(110,135,146,135,5,0x9D9C75,1);cvLine(128,114,128,156,5,0x9D9C75,1);break;
    case 6: /* Ribbed organic shelter, visibly open seam. */
        cvEllipse(128,135,64,99,c,1);cvEllipse(128,143,36,76,0x201E22,1);
        for(int i=0;i<6;i++) {
            float y=64+i*28;
            cvLine(77,y,111,y+19,8,0xB39375,1);cvLine(179,y,145,y+19,8,0xB39375,1);
        }
        cvLine(128,68,123,208,4,0x572A28,.85f);break;
    }
    cvEllipse(170,164,5,8,0x090F14,1);cvEllipse(168,161,2,3,a,.85f);
}
static void SupplyCase(int theme,int state) {
    unsigned c=body[theme],a=lamp[theme];
    float top=state==1?119:146;
    if(theme==3) { /* Medical transport case on short legs. */
        cvLine(73,213,67,238,6,0x82958B,1);cvLine(183,213,191,238,6,0x82958B,1);
    }
    cvPolyGrad((float[]){53,153,196,153,205,226,49,226},4,c,0x202A2C,1);
    cvPoly((float[]){61,157,190,157,190,214,61,214},4,0x131D23,1);
    cvPolyGrad((float[]){53,top,184,top-20,201,top+13,67,top+32},4,c,0x314349,1);
    if(state!=1)cvPolyGrad((float[]){62,168,190,154,190,218,62,226},4,c,0x2A302F,1);
    switch(theme) {
    case 0:for(int x=77;x<187;x+=23)cvLine(x,174,x,219,3,0x4A4034,1);break;
    case 1:for(int x=80;x<185;x+=23)cvLine(x,175,x,215,5,0xACB8AC,.8f);Rivets(72,164,6);break;
    case 2:cvLine(89,187,159,201,5,0xB78452,1);cvLine(158,181,89,208,5,0xB78452,1);break;
    case 3:cvLine(126,177,126,209,6,0xBCD5B9,1);cvLine(111,193,141,193,6,0xBCD5B9,1);break;
    case 4:cvLine(76,208,180,195,4,a,.85f);cvEllipse(128,187,15,9,0x80B8C8,.6f);break;
    case 5:cvLine(81,166,81,220,7,0xC2A36A,1);cvLine(171,159,171,215,7,0xC2A36A,1);break;
    case 6:for(int i=0;i<5;i++)cvLine(75+i*24,178,87+i*22,216,6,0xB69077,1);break;
    }
    if(state==2){cvEllipse(128,177,8,10,0xBCAC79,1);cvPoly((float[]){117,177,139,177,139,195,117,195},4,0x8F7651,1);}
    if(state==1)cvEllipse(129,152,42,12,0x060C13,1);
}
static void BreakerCabinet(int theme,int on) {
    unsigned c=body[theme],a=lamp[theme];
    if(theme==6)cvEllipse(129,138,74,98,0x624138,1);
    cvPolyGrad((float[]){66,227,66,48,189,48,201,227},4,c,0x283236,1);
    cvPoly((float[]){79,65,177,65,177,207,79,207},4,0x101E27,1);
    if(theme==0 || theme==2) {
        for(int x=87;x<175;x+=22)cvLine(x,68,x,201,4,theme==2?0x8A5844:0x6F6553,.8f);
    } else if(theme==3 || theme==4) {
        cvPoly((float[]){89,77,169,77,169,118,89,118},4,theme==3?0x334E45:0x274E66,1);
        cvLine(97,100,157,100,3,a,.8f);
    } else if(theme==5) {
        cvLine(53,214,53,96,10,0x8D7A4E,1);cvLine(209,212,209,72,10,0x8D7A4E,1);
    } else if(theme==6) {
        for(int i=0;i<5;i++)cvLine(70,79+i*27,97,90+i*25,7,0xB39F7E,1);
    }
    for(int i=0;i<3;i++) {
        cvPoly((float[]){102+i*21,129,115+i*21,129,115+i*21,186,102+i*21,186},4,0x718E89,1);
        cvLine(108+i*21,on?145:174,115+i*21,on?134:183,5,on?a:0x8B6860,1);
    }
    cvEllipse(128,90,8,5,on?0x90D8A8:0xA04A37,1);Rivets(76,55,6);
}
void BakeThemeProp(int id,int material,int kind,int state) {
    cvClear();
    if(kind==0)HideShell(material);
    else if(kind==1)SupplyCase(material,state);
    else BreakerCabinet(material,state);
    cvFinish(SpriteBakeTarget(id));
}

#include "common.h"
#include "room_material.h"
/* Shared floor/ceiling/wall materials, baked once. Coarse, static detail avoids shimmer. */
unsigned RoomMaterialSample(int theme,int x,int y,int surface){
    static const int palette[7][3][3]={
        {{92,87,78},{70,65,55},{35,37,37}},
        {{69,86,91},{43,57,61},{24,35,39}},
        {{105,42,29},{59,26,23},{35,18,20}},
        {{125,139,122},{65,84,72},{39,51,45}},
        {{67,121,150},{31,68,89},{23,44,61}},
        {{123,89,49},{71,52,30},{42,34,27}},
        {{115,57,41},{68,37,29},{42,25,24}}
    };
    if(theme<0 || theme>=7)theme=0;
    if(surface<0 || surface>2)surface=0;
    x&=63;y&=63;
    unsigned hash=(unsigned)((x/2)*1973+(y/2)*9277+theme*26699+surface*31847);
    hash^=hash>>7;
    int noise=(int)(hash%7)-3,edge=0,mark=0,stain=0;
    if(surface==0){
        switch(theme){
        case 0:edge=(y%16<2 || (x+(y/16)*16)%32<2);mark=0;break;
        case 1:edge=(x%32<2 || y%16<2);mark=(x%32>6 && x%32<14 && y%16>10 && y%16<13);break;
        case 2:edge=(x%32<3 || y%32<3);stain=((x*3+y*5)%61<8 && y>24);mark=(y<7 && x%16<6);break;
        case 3:edge=(x%16<2 || y%16<2);mark=(y>27 && y<31);stain=(hash%31<2);break;
        case 4:edge=(x%16<3);mark=(y%16<3 && x%16>4 && x%16<13);break;
        case 5:edge=(x%32<3);mark=(y%16<3 && x%32<8);stain=(hash%19<2);break;
        case 6:edge=((x*x/16+y)%22<3);stain=((x/3+y/5)%13<3);mark=(y>52 && (x+y)%17<4);break;
        }
    }else if(surface==1){
        switch(theme){
        case 0:edge=(y%32<2 || (x+(y/32)*16)%32<2);mark=0;break;
        case 1:edge=(x%16<2 || y%32<2);mark=(x>27 && x<36);break;
        case 2:edge=(x%32<3 || y%16<2);stain=(hash%17<3);mark=(y%32<5 && (x+y)%16<8);break;
        case 3:edge=(x%16<2 || y%16<2);mark=(x%32>27 && y%32>27);break;
        case 4:edge=((x+y)%32<3);mark=(x%32>27 || y%32>27);break;
        case 5:edge=(x%16<2 || y%16<2);mark=((x/4+y/4)%2==0 && y>25 && y<35);break;
        case 6:edge=((x*x/24+y)%24<3);stain=((x/4-y/8+64)%11<3);break;
        }
    }else{
        edge=(theme==6)?((x*x/16+y)%24<4):(x%32<5 || y%32<3);
        mark=(theme==4 && x>28 && x<35) || (theme==3 && y>27 && y<34);
        stain=(theme==2 || theme==5) && hash%23<3;
    }
    int r=palette[theme][surface][0]+noise,g=palette[theme][surface][1]+noise,b=palette[theme][surface][2]+noise;
    if(edge){r=r*2/3;g=g*2/3;b=b*2/3;}
    if(stain){r=r*4/5;g=g*3/5;b=b*3/5;}
    if(mark){
        if(theme==2 || theme==6){r+=23;g+=3;b+=1;}
        else if(theme==4){r+=8;g+=21;b+=29;}
        else {r+=17;g+=18;b+=10;}
    }
    if(r<0)r=0;
    if(g<0)g=0;
    if(b<0)b=0;
    if(r>255)r=255;
    if(g>255)g=255;
    if(b>255)b=255;
    return (unsigned)(((r>>3)<<3)<<16|((g>>3)<<3)<<8|((b>>3)<<3));
}

unsigned StoryMaterialSample(int chapter,int x,int y,int surface) {
    static const unsigned palette[3][3]={
        {0x76634d,0x524433,0x302a24},
        {0x594474,0x3a2b54,0x2c213e},
        {0x78602f,0x54472b,0x383023}
    };
    if ((unsigned)(chapter-1)>=3 || (unsigned)surface>=3) return 0;
    unsigned color=palette[chapter-1][surface];
    x&=63;y&=63;
    int joint,detail;
    if (chapter==1) {
        joint=surface==0?x%32<2:(x%32<2 || y%32<2);
        detail=surface==1?(y==24 || y==26) && x%16<10:x%32==4 && y%32==4;
        if (detail) return 0xa77c4e;
    } else if (chapter==2) {
        joint=surface==0?x%32<2:(x%32<2 || y%32<2);
        if (surface==0 && x>=8 && x<=56) {
            if ((y>=16 && y<=18) || (y>=36 && y<=38 && x<42)) return 0x916bcb;
            if (y>=24 && y<=31 && x%12<6) return 0x684f92;
        }
        if (surface==1 && ((x==10 && y%32<25) || (y==22 && x%16<10))) return 0x805eb9;
        detail=x%32==4 && y%32==4;
    } else {
        joint=x%32<2 || y%32<2;
        int hazard=surface==0?(y>=39 && y<49):surface==1?(x>=4 && x<12):0;
        if (hazard) return (x+y)%16<8?0xcaa034:0x292622;
        if (surface==0 && y>=7 && y<11 && x>=8 && x<=56) return 0x9e7426;
        detail=x%32==4 && y%32==4;
    }
    int delta=joint?-17:detail?12:(int)(((unsigned)x*29u+(unsigned)y*17u)%5u)-2;
    int r=(int)((color>>16)&255)+delta,g=(int)((color>>8)&255)+delta,b=(int)(color&255)+delta;
    return (unsigned)(r<<16|g<<8|b);
}

#include "common.h"
#include "room_material.h"
/* Bake-time material functions; not called from the frame rasterizer. */
unsigned RoomMaterialSample(int theme,int x,int y,int surface) {
    unsigned hash=(unsigned)(x*1973+y*9277+theme*26699+surface*31847);hash^=hash>>7;
    int noise=(int)(hash%9),r=18+noise,g=27+noise,b=39+noise,mark=0;
    if(surface==1){
        switch(theme){
        case 0:mark=(x%16<2 || y%16<2 || ((x+y)%29<2));break;
        case 1:mark=(x%12<2 || y%32<2);break;
        case 2:mark=(y%10<3);break;
        case 3:mark=(x%24<2 || y%24<2);break;
        case 4:mark=((x-y+128)%31<2);break;
        case 5:mark=((x%16<2 && y%32<20) || (y%16<2 && x%32<20));break;
        case 6:mark=((x*x+y*y)%67<5);break;
        }
        r=10+noise;g=19+noise;b=27+noise;if(mark){r+=7;g+=15;b+=20;}
    }else if(surface==2){r=7+noise/2;g=11+noise/2;b=18+noise/2;}
    else {
        switch(theme){
        case 0:mark=((x*x+3*y*y)%211<12 || x%32==0);r=16+noise;g=29+noise;b=42+noise;break;
        case 1:mark=(x%16<3 || y%12<2);r=23+noise;g=27+noise;b=38+noise;break;
        case 2:mark=(y%8<3 || (x+y)%43<2);r=16+noise;g=33+noise;b=36+noise;break;
        case 3:mark=(x%16<2 || y%16<2 || ((x*3+y)%47<2));r=29+noise;g=40+noise;b=45+noise;break;
        case 4:mark=((x+y)%23<2 || (x-y+128)%37<2);r=20+noise;g=35+noise;b=48+noise;break;
        case 5:mark=(x%24<3 || y%24<3 || (x%24==12 && y%24<12));r=26+noise;g=22+noise;b=44+noise;break;
        case 6:mark=((x*x+(y%16)*(y%16))%79<8);r=31+noise;g=19+noise;b=37+noise;break;
        }
        if(mark){r/=2;g/=2;b/=2;}
        if((hash%251)==0){r+=25;g+=32;b+=35;}
    }
    static const int palette[7][3][3]={
        {{83,61,48},{54,45,32},{29,26,23}},
        {{72,78,77},{42,47,44},{24,27,26}},
        {{91,35,29},{55,26,23},{34,16,18}},
        {{104,115,109},{57,66,62},{36,42,41}},
        {{65,106,130},{35,67,86},{23,40,57}},
        {{99,65,36},{59,45,30},{33,28,24}},
        {{103,46,66},{57,32,43},{32,20,29}}};
    if(theme<0 || theme>=7)theme=0;
    int side=surface>=0 && surface<=2?surface:0;
    int shade=noise+(mark?-12:0);
    r=palette[theme][side][0]+shade;g=palette[theme][side][1]+shade;b=palette[theme][side][2]+shade;
    if(theme==4 && mark){r+=18;g+=22;b+=26;}
    if(r<0)r=0;
    if(g<0)g=0;
    if(b<0)b=0;
    /* Eight-level quantization matches the existing premultiplied lookup table. */
    r=(r>>3)<<3;g=(g>>3)<<3;b=(b>>3)<<3;
    return (unsigned)(r<<16|g<<8|b);
}

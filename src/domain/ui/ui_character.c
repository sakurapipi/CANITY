#include "common.h"
#include "ui.h"

void UiDrawCharacterPortrait(int role,int cx,int cy,int pixel) {
    static const char *const portraits[4][19]={
        {
            "................",".....oooooo.....","....oggGGggo....","...ogggGGgggo...",
            "...ogffSSffgo...","...ogfFSSFfgo...","...ogfssssfgo...","....ogsfsggo....",
            ".....oGgGo......","....ooGGGoo.....","...ogooGGggo....","..oggggGGgggo...",
            ".ogGGgggggGGgo..",".ogGggGGgggGgo..",".ogGggGagggGgo..",".ogggggaggGGgo..",
            ".oggggggggGGgo..",".ogggggggggggo..","..ooooooooooo..."
        },{
            "................","......oooo......","....ooAAaaoo....","...oAAAaaaaoo...",
            "...oaaaaaaaao...","..oooooooooooo..","....ofSGGSfo....","....ofsFFsfo....",
            "....ofssssfo....",".....ofsso......","...oooooAoooo...",
            "..ogGAogAogGgo..",".ogGGaogAogGggo.",".oggGaogAogGggo.",
            ".ogGGAoAAoggggo.",".ogGGaoAgogGggo.",".oggGaoooogGGgo.",
            ".oggggggggGGggo.","..oooooooooooo.."
        },{
            "................",".....oooooo.....","....ogGGgGgo....","...ogGGggGGgo...",
            "...ogGfSsfggo...","...ogfooofggo...","...ogfPPSfggo...","...ogfssssggo...",
            "....ogsfsGgo....",".....offfo......","...ooPAAPoo.....","..opPPAAPPppo...",
            ".opPPpAAppPppo..",".opPppAAppPppo..",".oppPPpppAPppo..",".oppPAPpppPppo..",
            ".opppAPppppppo..",".oppppPppppppo..","..ooooooooooo..."
        },{
            "................",".....oooooo.....","....ogGAAgo.....","...ogGAAAAgo....",
            "...ogGggGGgo....","...oYYYYYYo.....","...oyYYYYyo.....","...ooooooo......",
            "....ogGGgo......","..oooggggooo....",".ogGOGgggOGgoo..",".ogGOGGGGOGGgo..",
            ".ogGggYYggGGgo..",".oggggYYggGGgo..",".ogGGggggGGGgo..",".ogGGgggGGGGgo..",
            ".ogGGgggGGGggo..",".ogggggggggggo..","..ooooooooooo..."
        }
    };
    static const char keys[]="ogGfsFSAaPpYyO";
    static const unsigned palette[4][14]={
        {0x0b1017,0x324652,0x6c9098,0x745b57,0xb18a6e,0xe2bd9a,0x2e4d60,0x9acaba,0x5b8e85,0x9d8eb3,0x665d86,0xaaa86c,0x646649,0x5f6a71},
        {0x0c141a,0x37525e,0x79a2b1,0x835c46,0xb1805e,0xe1b48a,0x353139,0xe6b86f,0xb58a51,0xa58ab0,0x504166,0xecba7a,0x7e5f42,0x28333a},
        {0x111019,0x322c45,0x635173,0x866567,0xbf9280,0xe8c0a1,0xbeb7d4,0x9d88ae,0x8a78a4,0xc5a8d8,0x53476e,0xb0aec3,0x636477,0x333342},
        {0x111316,0x293b41,0x71848a,0x72605d,0xad8c72,0xd1aa88,0x395359,0x8d7860,0x625c50,0x77607f,0x544665,0xe6b465,0x9f7448,0x536068}
    };
    if (role<0 || role>=4 || pixel<1) return;
    int left=cx-8*pixel,top=cy-9*pixel;
    for (int y=0;y<19;y++) {
        const char *row=portraits[role][y];
        for (int x=0;row[x];x++) {
            const char *shade=strchr(keys,row[x]);
            if (!shade) continue;
            int px=left+x*pixel,py=top+y*pixel;
            BlendRectWin(px,py,px+pixel,py+pixel,palette[role][shade-keys],1);
        }
    }
}

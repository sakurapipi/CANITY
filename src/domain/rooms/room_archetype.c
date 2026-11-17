/* Immutable room identities and pure geometry; legacy templates are untouched. */
#include "common.h"
#include "room_archetype.h"
#include "audio.h"
#include "hatch.h"
static const RoomArchetype archetypes[ROOM_ARCHETYPE_COUNT] = {
    {0,5,32,SFX_ECHO,0,HATCH_BOILER,3,3.8f},
    {1,1,33,SFX_CRANK,1,HATCH_STORAGE,1,4.6f},
    {2,4,34,SFX_HUM,2,HATCH_POWER,3,3.2f},
    {3,3,35,SFX_DRIP,3,HATCH_WARD,0,4.2f},
    {4,0,36,SFX_GLASS,4,HATCH_BRICK,0,5.0f},
    {5,2,37,SFX_METAL_SCRAPE,5,HATCH_HELL,1,5.4f},
    {6,6,38,SFX_HEARTBEAT,6,HATCH_FLESH,3,1.7f}
};
const RoomArchetype* RoomArchetypeGet(int theme) {
    return theme>=0 && theme<ROOM_ARCHETYPE_COUNT ? &archetypes[theme] : NULL;
}
/* Nonnegative seeds retain legacy geometry. The first negative range is compact v2;
   a disjoint negative range tags spacious v3 without adding any SaveBlob bytes. */
int RoomLayoutVersion(int seed) {
    if(seed>=0)return ROOM_LAYOUT_LEGACY;
    return seed< -1073741824?ROOM_LAYOUT_CURRENT:ROOM_LAYOUT_COMPACT;
}
int RoomLayoutSeed(unsigned entropy) { return -(int)(entropy & 0x1FFFFFFFu)-1073741825; }
int RoomLayoutVariant(int seed) {
    unsigned value=seed<0 ? (unsigned)(-(seed+1)) : (unsigned)seed;
    value ^= value>>11; value *= 0x45d9f3bu; return (int)((value>>7)&1u);
}
int RoomThemeForMaterial(int material) {
    for(int theme=0;theme<ROOM_ARCHETYPE_COUNT;theme++)if(archetypes[theme].materialId==material)return theme;
    return -1;
}
int RoomPropIsLandmark(int kind) { return kind>=32 && kind<32+ROOM_ARCHETYPE_COUNT; }
int RoomPropIsControl(int kind) { return kind>=ROOMPROP_CONTROL_FIRST && kind<ROOMPROP_CONTROL_END; }
/* Each perimeter remains walkable and accepts either pair of entrance tunnels.
   # cells are actual geometry; furniture never secretly changes collision. */
static const char* const layouts[7][2][6] = {
    {{".....","..#..",".....","..#..",".....",NULL},
     {"......","..##..","......","..##..","......",NULL}},
    {{"......",".#..#.",".#..#.",".#..#.",".#..#.","......"},
     {".....",".#.#.",".#.#.",".....",".#.#.","....."}},
    {{".....",".#.#.",".....",".#.#.",".....",NULL},
     {"......","..#...","......","...#..","......",NULL}},
    {{"......",".#..#.","......",".#..#.","......",NULL},
     {".....",".#.#.",".....",".....",".#.#.","....."}},
    {{"......",".##...","......","...##.","......",NULL},
     {".....","...#.",".#...","...#.",".....",NULL}},
    {{".....",".#.#.",".....",".#.#.",".....",NULL},
     {"......",".#..#.","......","..##..","......",NULL}},
    {{".....","..#..",".....",".#.#.",".....",NULL},
     {"......",".#..#.","......","..#...","......",NULL}}
};
/* Layout v3: a single larger chamber with a continuous perimeter and cross routes. */
static const char* const spacious[7][2][8]={
    {{".......","..###..",".#...#.",".......",".#...#.","..###..",".......",NULL},
     {"........","..####..",".#....#.","........",".#....#.","..####..","........",NULL}},
    {{"........",".##..##.",".##..##.","........",".##..##.",".##..##.","........",NULL},
     {".......",".#...#.",".#...#.",".......",".#...#.",".#...#.",".......","......."}},
    {{".......",".#...#.",".......",".......",".......",".#...#.",".......",NULL},
     {"........","..#..#..","........","........","........","..#..#..","........",NULL}},
    {{"........",".##..##.","........","........","........",".##..##.","........",NULL},
     {".......",".#...#.",".#...#.",".......",".......",".#...#.",".#...#.","......."}},
    {{".......",".##....",".......","....##.",".......",".##....",".......","......."},
     {"........","....##..","........",".##.....","........","....##..","........",NULL}},
    {{".......",".#...#.",".......",".......",".......",".#...#.",".......",NULL},
     {"........",".##..##.","........","........","........","..#..#..","........",NULL}},
    {{".......","..#.#..",".......",".#...#.",".......","..#.#..",".......","......."},
     {"........",".#....#.","..#..#..","........","..#..#..",".#....#.","........",NULL}}
};
int RoomTemplateBuildVersion(int theme,int variant,int rotation,int version,RoomTemplate* out) {
    const RoomArchetype* def=RoomArchetypeGet(theme);
    if(!def || !out || variant<0 || variant>=2 || rotation<0 || rotation>3)return 0;
    memset(out,0,sizeof(*out)); out->id=32+theme*2+variant;
    out->themeId=def->materialId;out->featureId=ROOM_FEATURE_NONE;
    if(version!=ROOM_LAYOUT_COMPACT && version!=ROOM_LAYOUT_CURRENT)return 0;
    const char* const* rows=version==ROOM_LAYOUT_COMPACT?layouts[theme][variant]:spacious[theme][variant];
    int maxRows=version==ROOM_LAYOUT_COMPACT?6:8;
    out->w=(int)strlen(rows[0]);
    while(out->h<maxRows && rows[out->h]) {
        for(int x=0;x<out->w;x++)out->tiles[out->h][x]=rows[out->h][x]=='#'?ROOM_TILE_PILLAR:ROOM_TILE_FLOOR;
        out->h++;
    }
    for(int r=0;r<rotation;r++) {
        RoomTemplate previous=*out;
        memset(out->tiles,0,sizeof(out->tiles));out->w=previous.h;out->h=previous.w;
        for(int y=0;y<previous.h;y++)for(int x=0;x<previous.w;x++)
            out->tiles[x][previous.h-1-y]=previous.tiles[y][x];
    }
    return 1;
}

int RoomTemplateBuildCurrent(int theme,int variant,int rotation,RoomTemplate* out) {
    return RoomTemplateBuildVersion(theme,variant,rotation,ROOM_LAYOUT_CURRENT,out);
}

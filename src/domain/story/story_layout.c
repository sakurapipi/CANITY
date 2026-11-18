#include "common.h"
#include "cell_meta.h"
#include "world_api.h"
#include "map_rooms.h"
#include "story_layout.h"

typedef struct { unsigned char a,b,verticalFirst; } Route;
typedef struct {
    Cell site[SITE_COUNT];
    unsigned char width[SITE_COUNT], height[SITE_COUNT];
    unsigned char material, accent, acoustics;
    Route path[12];
} Layout;

/* Each site is a usable room. Links are service corridors, with shortcuts
   and loops around key stations so pursuit does not demand a single route. */
static const Layout levels[]={
    { /* Recovery, power distribution and rescue radio. */
     {{5,20},{26,21},{26,8},{13,21},{10,12},{20,27},{18,8},{31,16}},
     {2,2,2,2,2,3,3,2},{2,2,2,2,2,2,2,2},3,5,ACOUSTIC_HARD,
     {{0,3,0},{0,4,1},{4,6,1},{4,3,0},{6,2,0},{2,7,0},{2,1,1},
      {3,5,1},{5,1,0},{3,6,0},{1,7,0},{5,7,0}}},
    { /* Broadcast switch, acoustic booths and equipment aisle. */
     {{5,27},{11,22},{26,10},{8,10},{15,14},{25,25},{18,6},{31,19}},
     {2,2,2,2,3,2,2,2},{2,2,2,2,2,3,2,2},5,4,ACOUSTIC_METAL,
     {{0,1,0},{0,3,1},{1,4,0},{1,5,1},{3,4,0},{3,6,1},{4,6,1},
      {4,2,0},{6,2,0},{2,7,0},{5,7,0},{4,5,0}}},
    { /* Identity archives around a dark, inaccessible records stack. */
     {{6,18},{25,8},{25,26},{10,7},{14,13},{14,25},{24,17},{31,18}},
     {2,3,2,2,2,2,2,2},{2,2,2,2,2,3,3,2},4,3,ACOUSTIC_DEFAULT,
     {{0,3,1},{0,5,0},{0,4,0},{3,1,1},{4,3,0},{4,6,0},{1,6,1},
      {6,2,1},{5,2,0},{2,7,0},{1,7,0},{5,4,1}}},
    { /* Decontamination, separate patient bays and a guard station. */
     {{6,28},{27,21},{19,7},{9,16},{13,6},{19,26},{27,10},{31,27}},
     {2,3,2,2,2,2,3,2},{2,2,2,2,2,2,3,2},4,1,ACOUSTIC_HARD,
     {{0,3,1},{0,5,0},{3,4,1},{4,2,0},{3,2,0},{2,6,0},{2,5,1},
      {6,1,1},{3,1,0},{5,1,0},{1,7,0},{5,7,0}}},
    { /* Reward vault. Teller windows and counter access form two circuits. */
     {{5,19},{18,17},{28,11},{9,26},{12,10},{23,26},{22,8},{31,22}},
     {2,4,2,2,3,3,2,2},{2,3,2,2,2,2,2,2},5,0,ACOUSTIC_METAL,
     {{0,4,1},{0,3,0},{4,6,0},{4,1,1},{3,1,0},{3,5,0},{1,5,1},
      {1,6,1},{6,2,0},{2,7,0},{5,7,0},{1,2,0}}},
    { /* Detention. The observation corridor bypasses the narrow cell block. */
     {{5,7},{24,26},{11,24},{8,16},{16,8},{18,17},{27,11},{31,23}},
     {2,2,2,2,3,2,2,2},{2,2,3,2,2,3,2,2},1,2,ACOUSTIC_HARD,
     {{0,3,1},{0,4,0},{3,2,1},{3,5,0},{4,5,1},{4,6,0},{6,5,0},
      {2,1,0},{5,1,1},{6,7,0},{1,7,0},{5,7,0}}},
    { /* Generator banks, a ring bus, service cable route and core logs. */
     {{5,24},{23,18},{26,7},{11,13},{15,7},{18,26},{27,26},{31,14}},
     {2,3,2,2,2,3,2,2},{2,2,2,2,3,2,2,2},2,6,ACOUSTIC_METAL,
     {{0,3,1},{0,5,0},{3,4,1},{3,1,0},{4,2,0},{4,5,1},{2,7,0},
      {2,1,1},{1,5,1},{1,6,0},{5,6,0},{6,7,0}}},
    { /* Signal box above the service route, two platforms and the reset core. */
     {{5,18},{24,10},{14,7},{9,25},{14,16},{18,26},{24,22},{31,14}},
     {2,2,2,3,2,3,2,2},{2,2,2,2,2,2,2,2},5,6,ACOUSTIC_METAL,
     {{0,4,0},{0,3,1},{3,5,0},{4,2,1},{4,5,0},{2,1,0},{4,1,0},
      {1,6,1},{5,6,0},{1,7,0},{6,7,0},{3,4,0}}},
    { /* Switching works: a return duct goes around the live bus. */
     {{5,22},{26,12},{16,12},{9,29},{8,12},{23,28},{19,19},{31,22}},
     {2,2,2,2,2,3,2,2},{2,2,2,2,2,1,2,2},5,2,ACOUSTIC_METAL,
     {{0,3,0},{0,4,1},{0,6,0},{4,2,0},{4,6,0},{2,6,1},{2,1,0},
      {6,5,0},{3,5,0},{5,7,0},{1,7,1},{6,1,0}}},
    { /* Witness archive: parallel listening galleries join at the recording suites. */
     {{5,17},{27,10},{19,26},{10,8},{10,25},{24,17},{16,8},{31,24}},
     {1,2,2,2,2,2,3,2},{2,1,2,1,2,1,2,2},3,4,ACOUSTIC_DEFAULT,
     {{0,3,1},{0,4,1},{0,6,0},{3,6,0},{4,2,0},{6,5,1},{6,1,0},
      {2,5,0},{5,1,0},{5,7,0},{2,7,0},{1,7,0}}},
    { /* Emergency spillway: two evacuation routes along the isolation shutters. */
     {{6,26},{26,21},{20,12},{8,10},{11,20},{21,29},{15,7},{31,12}},
     {2,2,2,2,2,3,2,2},{1,2,2,2,2,1,2,2},1,5,ACOUSTIC_HARD,
     {{0,4,1},{0,5,1},{4,3,0},{4,2,0},{3,6,1},{6,2,0},{2,1,0},
      {6,7,0},{1,7,1},{5,1,0},{5,2,1},{2,7,0}}},
    { /* B-17 switching yard: dispatch and a return duct skirt the main bus. */
     {{5,24},{29,11},{20,16},{10,29},{9,15},{22,26},{20,7},{31,20}},
     {2,2,3,2,2,3,3,2},{2,2,2,1,2,2,2,2},5,2,ACOUSTIC_METAL,
     {{0,4,1},{0,3,1},{3,5,0},{4,2,0},{4,6,1},{6,2,0},{6,1,0},
      {2,5,1},{2,1,0},{5,7,0},{1,7,1},{2,7,0}}},
    { /* B-17 witness archive: separate booths flank the receiver and merge at the uplink. */
     {{5,10},{29,25},{20,20},{9,15},{9,25},{23,29},{17,9},{31,11}},
     {2,2,3,2,2,2,3,2},{2,2,2,2,2,1,2,2},3,4,ACOUSTIC_DEFAULT,
     {{0,3,1},{0,6,0},{3,4,1},{3,2,0},{4,2,0},{4,5,0},{6,2,1},
      {6,7,0},{2,5,1},{2,1,0},{5,1,0},{1,7,0}}},
    { /* B-17 spillway: two shelter lanes meet at the pressure shutter. */
     {{5,22},{27,12},{22,20},{9,28},{10,13},{23,28},{17,9},{31,17}},
     {2,2,3,2,2,3,2,2},{2,2,2,1,2,1,2,2},1,5,ACOUSTIC_HARD,
     {{0,4,1},{0,3,0},{4,6,1},{4,2,0},{3,5,0},{5,2,0},{6,2,1},
      {6,1,0},{2,1,1},{2,7,0},{1,7,0},{5,7,0}}}
};

const Cell *StorySite(int floor,int site) { return &levels[floor-1].site[site]; }
int StoryMaterial(int floor) { return levels[floor-1].material; }
int StoryDecorationAt(int floor,int chapter,int index,StoryDecoration *out) {
    static const struct { int site,dx,dy; float height; } mounts[3][STORY_DECOR_PER_CHAPTER]={
        {{SITE_ENTRY,1,-1,.85f},{SITE_POWER,-2,1,.65f},{SITE_RECORD,1,-1,.92f},{SITE_RETURN,1,1,.78f}},
        {{SITE_ENTRY,1,-1,.92f},{SITE_RECORD,1,1,.97f},{SITE_POWER,2,-1,.76f},{SITE_RETURN,1,-1,.71f}},
        {{SITE_ENTRY,1,-1,.88f},{SITE_SERVICE,-1,-1,.62f},{SITE_POWER,2,-1,.98f},{SITE_EXIT,1,1,.95f}}
    };
    if ((unsigned)(chapter-1)>=3 || (unsigned)index>=STORY_DECOR_PER_CHAPTER) return 0;
    const Layout *layout=&levels[floor-1];
    const int site=mounts[chapter-1][index].site;
    int x=layout->site[site].x+mounts[chapter-1][index].dx;
    int y=layout->site[site].y+mounts[chapter-1][index].dy;
    if (worldMap[y][x] || PropBlocksAt(x,y)) return 0;
    *out=(StoryDecoration){x+.5f,y+.5f,mounts[chapter-1][index].height,
                           (chapter-1)*STORY_DECOR_PER_CHAPTER+index};
    return 1;
}

static void Open(int x0,int y0,int x1,int y1) {
    if(x0>x1){int q=x0;x0=x1;x1=q;}
    if(y0>y1){int q=y0;y0=y1;y1=q;}
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) worldMap[y][x]=0;
}
static void Furnish(const Layout *l,int site,int kind) {
    const Cell *c=&l->site[site];
    float x=c->x+l->width[site]-.5f,y=c->y-l->height[site]+.5f;
    if (RoomPropAdd(x,y,kind)) {
        roomProps[roomPropCount-1].blocks=0;
        roomProps[roomPropCount-1].searchLeft=0;
    }
}
void StoryLayOut(int floor) {
    const Layout *l=&levels[floor-1];
    for (int i=0;i<12;i++) {
        Route r=l->path[i];
        Cell a=l->site[r.a],b=l->site[r.b];
        if (r.verticalFirst) {Open(a.x,a.y,a.x,b.y);Open(a.x,b.y,b.x,b.y);}
        else {Open(a.x,a.y,b.x,a.y);Open(b.x,a.y,b.x,b.y);}
    }
    for (int i=0;i<SITE_COUNT;i++) {
        Cell c=l->site[i];
        int w=l->width[i],h=l->height[i];
        Open(c.x-w,c.y-h,c.x+w,c.y+h);
        int accent=i==SITE_RECORD || i==SITE_POWER || i==SITE_TERMINAL;
        int theme=accent && floor<=8?l->accent:l->material;
        unsigned flags=i==SITE_POWER?CELLFLAG_POWER:0;
        CellMetaFillRoom(c.x-w,c.y-h,2*w+1,2*h+1,i+1,theme,l->acoustics,flags);
        for (int y=c.y-h-1;y<=c.y+h+1;y++) for (int x=c.x-w-1;x<=c.x+w+1;x++)
            if (worldMap[y][x]) CellMetaFillRoom(x,y,1,1,0,theme,l->acoustics,CELLFLAG_ROOMWALL);
    }
    if(floor==8) {
        Open(24,22,31,22);Open(31,22,31,29);Open(29,27,33,29);
        Furnish(l,SITE_RETURN,ROOMPROP_CONSOLE);
    }
    if (floor>=12) return; /* Role-specific scenery is drawn from story fixtures. */
    if (floor>8) {
        static const int fixtures[3][3]={
            {ROOMPROP_CONSOLE,ROOMPROP_THEME_FIRST,ROOMPROP_CRATES},
            {ROOMPROP_SHELF,ROOMPROP_CONSOLE,ROOMPROP_SHELF},
            {ROOMPROP_CRATES,ROOMPROP_THEME_FIRST+2,ROOMPROP_CONSOLE},
        };
        const int *f=fixtures[(floor-9)%3];
        Furnish(l,SITE_ENTRY,f[0]);
        Furnish(l,SITE_RECORD,f[1]);
        Furnish(l,SITE_TERMINAL,f[2]);
        return;
    }
    Furnish(l,SITE_SERVICE,floor==2 || floor==3 || floor==5?ROOMPROP_SHELF:ROOMPROP_CONSOLE);
    Furnish(l,SITE_RECORD,floor==2 || floor==3?ROOMPROP_SHELF:ROOMPROP_CONSOLE);
}

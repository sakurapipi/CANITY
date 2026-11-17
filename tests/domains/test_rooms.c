/* Authored for RC2. No build/test execution is implied by these fixtures. */
#include "../test_common.h"
#include "room_archetype.h"
#include "room_theme.h"
#include "floor_routes.h"
#include "core_rng.h"

static void layout_paths(void) {
    for(int version=ROOM_LAYOUT_COMPACT;version<=ROOM_LAYOUT_CURRENT;version++)
    for(int theme=0;theme<7;theme++)for(int variant=0;variant<2;variant++)for(int rot=0;rot<4;rot++) {
        RoomTemplate t;
        CHECK(RoomTemplateBuildVersion(theme,variant,rot,version,&t),"both saved compact and spacious layouts exist in every rotation");
        CHECK(t.w<=ROOM_MAX_W && t.h<=ROOM_MAX_H && t.w>=4 && t.h>=4,"spacious layouts stay within the versioned room envelope");
        int seen[ROOM_MAX_H][ROOM_MAX_W]={{0}},qx[ROOM_MAX_W*ROOM_MAX_H],qy[ROOM_MAX_W*ROOM_MAX_H],head=0,tail=1;
        qx[0]=qy[0]=0;seen[0][0]=1;
        while(head<tail) {
            int x=qx[head],y=qy[head++];
            const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
            for(int n=0;n<4;n++) {
                int nx=x+dx[n],ny=y+dy[n];
                if(nx<0 || ny<0 || nx>=t.w || ny>=t.h || seen[ny][nx] || t.tiles[ny][nx]!=ROOM_TILE_FLOOR)continue;
                seen[ny][nx]=1;qx[tail]=nx;qy[tail++]=ny;
            }
        }
        CHECK(tail==RoomTemplateFloorCount(&t),"no unreachable pocket is hidden behind the landmark geometry");
        CHECK(seen[0][t.w/2] && seen[t.h-1][t.w/2] && seen[t.h/2][0] && seen[t.h/2][t.w-1],
              "both entrance orientations connect to the same interior");
    }
    CHECK(RoomLayoutVersion(-12345)==ROOM_LAYOUT_COMPACT,"compact RC2 seeds retain their geometry version");
    CHECK(RoomLayoutVersion(12345)==ROOM_LAYOUT_LEGACY,"positive old room seeds never select new geometry");
    CHECK(RoomLayoutSeed(0)<0 && RoomLayoutVersion(RoomLayoutSeed(0))==ROOM_LAYOUT_CURRENT,
          "a new layout seed has a nonzero version tag even for zero entropy");
    CHECK(RoomLayoutVariant(RoomLayoutSeed(456))==RoomLayoutVariant(RoomLayoutSeed(456)),
          "variant identity is stable for saved seeds");
}
static void room_scene(int theme) {
    const RoomArchetype* def=RoomArchetypeGet(theme);
    HatchReset();RoomPropReset();CellMetaReset();hiddenIn=-1;trainingMode=0;
    roomSeedUsed=RoomLayoutSeed(2468);sanity=50;battery=150;stamina=100;carryGold=500;
    flares=2;bottles=smokeBombs=gluePots=echoBolts=decoys=0;
    posX=3.5f;posY=3.5f;
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)
        worldMap[y][x]=(x==0 || y==0 || x==MAP_WIDTH-1 || y==MAP_HEIGHT-1);
    memset(monHunters,0,sizeof(monHunters));
    monHunters[0].active=1;monHunters[0].x=18.5f;monHunters[0].y=3.5f;
    HatchAdd(5.5f,5.5f,theme,def->materialId);
    RoomPropAdd(8.5f,8.5f,ROOMPROP_LANDMARK_FIRST+theme);
    RoomPropAdd(7.5f,8.5f,ROOMPROP_CONTROL_FIRST+theme*2);
    RoomPropAdd(9.5f,8.5f,ROOMPROP_CONTROL_FIRST+theme*2+1);
    containerCount=3;memset(containers,0,sizeof(containers));
    for(int c=0;c<3;c++){containers[c].active=1;containers[c].x=10.5f+c;containers[c].y=10.5f;containers[c].content=10;cellMeta[10][10+c].roomId=1;}
    RoomThemeSetMadness(0);RoomThemeBind();
}
static void prepared_commit(void) {
    for(int theme=0;theme<7;theme++) {
        room_scene(theme);
        const RoomArchetype* def=RoomArchetypeGet(theme);
        if(def->preparationMask)CHECK(!HatchGateCheck(0,NULL,0),"new rooms cannot bypass their preparation controls");
        int gold=carryGold;float charge=battery;
        if(theme==HATCH_BRICK)CHECK(RoomThemeSelectCentralTarget(0),"central frame owns target selection");
        else CHECK(RoomThemeInteractFixture(1),"left control is an action, never a search reward");
        if(def->preparationMask==3)CHECK(RoomThemeInteractFixture(2),"second preparation control is connected");
        CHECK(carryGold==gold && battery==charge,"preparation does not charge early or grant free supplies");
        CHECK(HatchGateCheck(0,NULL,0),"prepared room with sufficient resources can activate");
        HatchSetState(0,HATCH_OPENING);HatchTick(HATCH_OPEN_SECONDS+.01f);
        CHECK(hatches[0].state==HATCH_FIRED,"one completed animation commits one room effect");
        gold=carryGold;charge=battery;
        CHECK(!HatchFireEffect(0),"a spent room cannot commit a second time through the direct API");
        CHECK(carryGold==gold && battery==charge,"a rejected duplicate commit has no resource side effects");
    }
}
static void choices_and_failures(void) {
    room_scene(HATCH_WARD);
    RoomThemeInteractFixture(1);RoomThemeInteractFixture(2);RoomThemeRebuildControls();
    CHECK(roomTheme.choice==2 && roomProps[1].searched==0,"transplant direction is exclusive and stored in control records");
    RoomThemeBind();CHECK(roomTheme.choice==2,"snapshot rebind preserves the player's explicit exchange direction");
    room_scene(HATCH_STORAGE);RoomThemeInteractFixture(1);RoomThemeInteractFixture(2);
    CHECK(!HatchGateCheck(0,NULL,0) && carryGold==500,"cancelling investment removes authorization without a fee");
    room_scene(HATCH_POWER);RoomThemeInteractFixture(1);RoomThemeInteractFixture(2);
    battery=25;HatchSetState(0,HATCH_OPENING);battery=0;HatchTick(HATCH_OPEN_SECONDS+.01f);
    CHECK(hatches[0].state==HATCH_SEALED && battery==0,"resource loss during animation leaves an unpaid room retryable");
    room_scene(HATCH_BRICK);RoomThemeSelectCentralTarget(0);
    worldMap[3][18]=1;
    CHECK(!HatchGateCheck(0,NULL,0),"target becoming embedded in a wall cancels the swap before charging");
}
static void route_repair_cases(void){
    int previous[MAP_HEIGHT][MAP_WIDTH];CellMeta previousMeta[MAP_HEIGHT][MAP_WIDTH];
    int previousRooms=roomCount;memcpy(previous,worldMap,sizeof(previous));memcpy(previousMeta,cellMeta,sizeof(previousMeta));
    CellMetaReset();
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)worldMap[y][x]=1;
    for(int x=2;x<=8;x++)worldMap[4][x]=worldMap[8][x]=0;
    for(int y=4;y<=8;y++)worldMap[y][2]=0;
    FloorRoutesBraid(0);
    CHECK(!worldMap[5][8] && !worldMap[6][8] && !worldMap[7][8],"long braid commits every intermediate wall, not half a corridor");
    CHECK(FloorRoutesWithinDeadEndLimit(3),"joined U-shaped corridor has no excessive dead ends");
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)worldMap[y][x]=1;
    for(int x=2;x<=8;x++)worldMap[4][x]=worldMap[8][x]=0;
    for(int y=4;y<=8;y++)worldMap[y][2]=0;
    cellMeta[6][8].flags=CELLFLAG_ROOMWALL;FloorRoutesBraid(0);
    CHECK(worldMap[6][8],"dead-end repair cannot puncture a protected room wall");
    CellMetaReset();
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)worldMap[y][x]=1;
    for(int x=1;x<=5;x++)worldMap[1][x]=0;
    worldMap[1][12]=0;
    CHECK(FloorRoutesConnect(6),"disconnected exterior corridor is joined within its wall budget");
    for(int x=6;x<12;x++)CHECK(!worldMap[1][x],"reconnection has no missing middle segment");
    memcpy(worldMap,previous,sizeof(previous));memcpy(cellMeta,previousMeta,sizeof(previousMeta));roomCount=previousRooms;
}
static void vault_population_budget(void) {
    for(unsigned seed=1;seed<=16;seed++) {
        room_scene(HATCH_STORAGE);rng_world_seed(seed);hideSpotCount=0;breaker.active=0;
        for(int y=4;y<=12;y++)for(int x=4;x<=12;x++)cellMeta[y][x].roomId=1;
        containerCount=MAX_CONTAINERS;memset(containers,0,sizeof(containers));
        for(int i=0;i<containerCount;i++){containers[i].active=1;containers[i].x=2.5f+i;containers[i].y=20.5f;containers[i].content=i<2?1:10;}
        RoomThemePopulateVault();
        int count=0,fuses=0;
        for(int i=0;i<containerCount;i++){
            Container* c=&containers[i];fuses+=c->content==1;
            if(cellMeta[(int)c->y][(int)c->x].roomId){count++;CHECK(c->locked,"every vault receipt requires a puzzle lock");}
        }
        CHECK(count>=3 && count<=10,"vault population stays within three to ten crates");
        CHECK(containerCount==MAX_CONTAINERS && fuses==2,"vault placement preserves twelve slots and mandatory fuses");
    }
}
void TestSuite_rooms(void) {
    layout_paths();route_repair_cases();prepared_commit();choices_and_failures();vault_population_budget();
    RoomThemeSetMadness(0);RoomThemeLoadEvent(-1,0);HatchReset();RoomPropReset();RoomThemeBind();
}

#include "../test_common.h"
static void test_training_supplies_do_not_leak_into_normal_run(void);
#include "loop_api.h"
#include "loop_private.h"
#include "plat.h"
#include "run_rewards.h"
#include "phase.h"
#include "calibration.h"
#include "present_api.h"

static unsigned checksum(void){
    unsigned h=2166136261u;for(int i=0;i<renderW*renderH;i++)h=(h^pixels[i])*16777619u;return h;
}
static void image(const char* path){
    FILE* f=fopen(path,"wb");CHECK(f!=NULL,"can write native render evidence");
    fprintf(f,"P6\n%d %d\n255\n",renderW,renderH);
    for(int i=0;i<renderW*renderH;i++){unsigned c=pixels[i];unsigned char p[3]={(unsigned char)(c>>16),(unsigned char)(c>>8),(unsigned char)c};fwrite(p,1,3,f);}fclose(f);
}
void TestSuite_delivery(void){
    test_training_supplies_do_not_leak_into_normal_run();
    winW=800;winH=600;renderScaleIndex=0;InitPixels();SpritesInit();CHECK(SpritesReady(),"all sprite assets initialize");
    int ids[]={SPR_MON_HUNTER,SPR_MON_HUNTER2,SPR_MON_HUNTER3,SPR_MON_LIGHT,SPR_MON_LEECH,SPR_MON_SHADE,SPR_MON_SPIDER,SPR_MON_WARDEN_CHASE,SPR_MON_MIMIC,SPR_MON_PARASITE,SPR_PROP_SPIDER,SPR_PROP_CRATE_MIMIC};
    for(unsigned n=0;n<sizeof(ids)/sizeof(ids[0]);n++){
        Sprite* sp=SpriteGet(ids[n]);CHECK(sp && sp->w==512 && sp->h==512,"creature really bakes to 512px");
        CHECK(sp->bx1>sp->bx0 && sp->by1>sp->by0,"creature has a valid visible silhouette");
    }
    CHECK(memcmp(SpriteGet(SPR_MON_LEECH_W)->px,SpriteGet(SPR_MON_WARDEN_CHASE)->px,512*512*sizeof(unsigned))!=0,"collector walk and Warden chase remain separate");
    for(int i=0;i<renderW;i++)zBuffer[i]=100;
    SpritePose pose={0,0,.6f,0};memset(pixels,0,renderW*renderH*sizeof(*pixels));
    DrawSpriteCreature(SPR_MON_HUNTER,200,60,400,500,1,1,1,2,0,0,&pose);unsigned first=checksum();
    const char* dir=getenv("CANITY_EVIDENCE_DIR");char path[1024];if(dir){snprintf(path,sizeof(path),"%s/hunter_native.ppm",dir);image(path);}
    pose.time=1;memset(pixels,0,renderW*renderH*sizeof(*pixels));DrawSpriteCreature(SPR_MON_HUNTER,200,60,400,500,1,1,1,2,0,0,&pose);
    CHECK(checksum()!=first,"idle animation changes actual rendered pixels");
    clear_meta();DeleteSave();LoopQueueStart(START_NORMAL,0);StartGame();
    currentState=STATE_MAIN_MENU;CanityMouseClick(1,1);CHECK(currentState==STATE_MAIN_MENU,"blank main-menu click does not start a run");
    currentState=STATE_PLAYING;RewardResetPending();RunSkillsReset();OpenSkillPick();CHECK(currentState==STATE_SKILL_PICK,"draft opens");
    MenuGeo g=GetMenuGeo();RECT card=RowRect(g,0);CanityMouseClick((card.left+card.right)/2,(card.top+card.bottom)/2);
    CHECK(currentState==STATE_PLAYING,"mouse card click really submits choice");
    for(int stage=0;stage<CAMPAIGN_STAGE_COUNT;stage++){
        ResetRunModeFlags();DeleteSave();LoopQueueStart(START_CAMPAIGN,stage);StartGame();
        CHECK(currentState==STATE_PLAYING && campaignMode,"campaign chapter starts");
        CHECK(containerCount>=fusesNeeded && containerCount<=12,"campaign has enough fuse containers and respects 12 slots");
        CHECK(breaker.active,"campaign breaker exists");
        CHECK(!worldMap[(int)posY][(int)posX],"campaign spawn is walkable");
    }
    flashlightOn=1;battery=100;g_timeSec=1;Render3D();unsigned long long bright=0,dark=0;
    for(int i=0;i<renderW*renderH;i++)bright+=(pixels[i]&255)+((pixels[i]>>8)&255)+((pixels[i]>>16)&255);
    if(dir){snprintf(path,sizeof(path),"%s/torch_on.ppm",dir);image(path);}
    flashlightOn=0;Render3D();for(int i=0;i<renderW*renderH;i++)dark+=(pixels[i]&255)+((pixels[i]>>8)&255)+((pixels[i]>>16)&255);
    if(dir){snprintf(path,sizeof(path),"%s/torch_off.ppm",dir);image(path);}
    printf("LIGHT LUMINANCE: on=%llu off=%llu\n",bright,dark);CHECK(dark<bright*7/10,"torch-off scene is measurably darker");
    for(int id=0;id<SKILL_COUNT;id++)skillRank[id]=SkillIsPlayerFacing(id)?skillDefs[id].maxRank:0;
    SaveGame();int expected[SKILL_COUNT];memcpy(expected,skillRank,sizeof(expected));
    memset(skillRank,0,sizeof(expected));ContinueRun();
    CHECK(!memcmp(expected,skillRank,sizeof(expected)),"all current player skills survive save/continue");
    SaveGame();currentState=STATE_MAIN_MENU;LoopQueueStart(START_DAILY,0);StartGame();CHECK(LoopRunReplacementPending(),"existing progress prompts before replacement");
    LoopResolveRunReplacement(0);CHECK(currentState==STATE_MAIN_MENU && SaveHasPendingRun(),"cancel preserves progress");
}

static void test_training_supplies_do_not_leak_into_normal_run(void){
    LoopQueueStart(START_TRAINING,0);StartFreshTestRun();
    Update(.02f);
    CHECK(trainingMode && flares==99,"free training intentionally provides practice supplies");
    LoopQueueStart(START_NORMAL,0);StartFreshTestRun();
    CHECK(!trainingMode && !guideMode && !dailyMode && !campaignMode,"normal new run clears every practice/mode flag");
    CHECK(flares<99 && bottles<99 && smokeBombs<99,"practice supplies do not become normal-run inventory");
}

void TestSuite_existing_save(void){
    const char* source=getenv("CANITY_EXISTING_SAVE");
    if(!source || !source[0]){
        source="tests/fixtures/saves/synthetic_v44.sav";
        puts("EXISTING SAVE INPUT: synthetic v44 contract fixture, not a private player save");
    }
    char target[MAX_PATH];SavePathForSlot(0,0,target,sizeof(target));
    CHECK(fixture_copy(source,target),"user-save probe copies into isolated test data");
    int pending=LoadSettings();
    CHECK(SaveTestBlob()->magic==SAVE_MAGIC && SaveTestBlob()->version==SAVE_VERSION,"existing user save migrates to the current format");
    if(pending){ContinueRun();CHECK(currentState==STATE_PLAYING,"existing progress resumes");CHECK(posX>0 && posY>0 && posX<MAP_WIDTH && posY<MAP_HEIGHT,"existing player position remains bounded");}
    printf("EXISTING SAVE: pending=%d level=%d version=%d\n",pending,currentLevel,SaveTestBlob()->version);
}

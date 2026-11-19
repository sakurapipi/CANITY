#include "../test_common.h"
#include "event_runtime.h"
#include "events.h"
#include "monster_rules.h"
#include "threat_tier.h"
#include "economy.h"
#include "monster_evolution.h"

static void PutWord(unsigned char *bytes,int index,unsigned value) {
    for(int i=0;i<4;i++)bytes[4*index+i]=(unsigned char)(value>>(i*8));
}

static void SetTestFloorEvent(int id) {
    unsigned char bytes[EVENT_WIRE_BYTES]={0};
    PutWord(bytes,0,1);
    PutWord(bytes,1,(unsigned)currentLevel);
    PutWord(bytes,2,(unsigned)(id+1));
    PutWord(bytes,4,1234);
    PutWord(bytes,5,4321);
    CHECK(EventWireRestore(bytes,sizeof bytes,currentLevel),"event fixture is valid");
}

static void CheckNoise(NoiseKind kind,float radius,float expected,int expectedKind) {
    levelEvent=-1;
    SoundEventResetStats();
    MakeActionNoise(posX,posY,radius,kind);
    float normal=SoundEventLastStrength();
    levelEvent=EV_SILENCE;
    SoundEventResetStats();
    MakeActionNoise(posX,posY,radius,kind);
    CHECK(normal>0 && fabsf(SoundEventLastStrength()/normal-expected*1.08f)<.001f,
          "diegetic sound modifier and hearing gain apply exactly once");
    CHECK(SoundEventLastKind()==expectedKind,"radius does not determine sound category");
}

static void CheckHunterHearing(void) {
    static void (*const update[])(float)={UpdateMonster,UpdateMonster2,UpdateMonster3};
    posX=5.5f;posY=5.5f;playerInSafe=1;hiddenIn=-1;
    levelEvent=-1;modNoiseMul=1;
    for(int bend=0;bend<=1;bend++) {
        for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)worldMap[y][x]=1;
        for(int x=5;x<=19;x++)worldMap[5][x]=worldMap[7][x]=0;
        worldMap[6][5]=worldMap[6][19]=0;
        for(int tier=0;tier<4;tier++)for(int h=0;h<MON_HUNTER_MAX;h++) {
            ThreatTierRestore(currentLevel,tier,0);
            EvolutionResetFloor();
            for(int distance=2;distance<=14;distance+=2) {
                int heard[2];
                for(int high=0;high<2;high++) {
                    carryGold=high?90000:0;
                    Monster *m=&monHunters[h];
                    memset(m,0,sizeof *m);m->active=1;m->x=posX+distance;m->y=posY+2*bend;
                    m->suspTarget=-1;
                    SuspicionReset();
                    MakeActionNoise(posX,posY,16,NOISE_SEARCH);
                    CHECK(m->state==0,"a sound publishes a clue without directly assigning a hunter state");
                    float effective=fminf(100,16*2.6f*EconomyHearMul());
                    CHECK(fabsf(SoundEventLastStrength()-effective)<.001f,"shared hearing gets one economy multiplier");
                    float reach=SoundReach(m->x,m->y,posX,posY,EvolutionHearingRange(22));
                    update[h](.01f);
                    heard[high]=m->suspTarget>=0;
                    CHECK(heard[high]==(effective*reach>15/HunterHearMulAt(h)),
                          "actual hunter update uses topology and the individual hearing threshold");
                }
                CHECK(heard[1]>=heard[0],"wealth makes the same sound at least as audible for each hunter");
            }
        }
    }
    playerInSafe=0;
}

static void CheckGeneratedEventFloors(void) {
    static const int levels[]={2,4,8,10};
    for(unsigned seed=1;seed<=1000;seed++) {
        RunRngSeed(seed);
        EventResetRun();
        currentLevel=levels[(seed-1)%4];
        GenerateLevel();
        MapCheck map;
        MapValidate(&map);
        CHECK(map.spawnOpen && map.exitReachable,"generated event floor retains a usable route");
        SetTestFloorEvent(EV_FLOOD);
        EventPlaceFloor();
        int count=0;
        for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)
            if(EventWetCell(x,y)) {
                CHECK(!worldMap[y][x],"flood cannot add or replace walls");
                count++;
            }
        CHECK(levelEvent==EV_FLOOD && count>=6,"generated floor supports a meaningful flooded section");
    }
}

void TestSuite_events(void) {
    clear_meta();
    StartFreshTestRun();
    currentLevel=5;
    for(int sprint=0;sprint<=1;sprint++) {
        sprinting=sprint;
        for(int radius=5;radius<=17;radius+=6) {
            CheckNoise(NOISE_WALK,(float)radius,.60f,SK_WALK);
            CheckNoise(NOISE_SPRINT,(float)radius,1.20f,SK_WALK);
            CheckNoise(NOISE_SEARCH,(float)radius,1.20f,SK_SEARCH);
            CheckNoise(NOISE_CRANK,(float)radius,1.20f,SK_SEARCH);
            CheckNoise(NOISE_WORLD,(float)radius,1,SK_SEARCH);
        }
    }
    sprinting=0;

    for(int role=0;role<4;role++)for(int tier=0;tier<4;tier++)for(int id=0;id<EVENT_COUNT;id++){
        protocolRun.role=role;
        ThreatTierRestore(currentLevel,tier,0);
        SetTestFloorEvent(id);
        unsigned char bytes[EVENT_WIRE_BYTES];
        EventActivateFloor();
        CHECK(EventWireEncode(bytes) && EventWireRestore(bytes,sizeof bytes,currentLevel) &&
              levelEvent==id,"every event round-trips for each role and threat tier");
        CHECK(ProtocolBase(1)>0 && MonsterMovementCap()>0,"event does not replace base role or movement rules");
    }
    SetTestFloorEvent(EV_BLACKOUT);
    battery=120;
    int cap=BatteryMax();
    EventActivateFloor();EventActivateFloor();
    CHECK(battery==60 && BatteryMax()==cap,"blackout drains only once and leaves battery capacity intact");

    SetTestFloorEvent(EV_GRID_SURGE);
    EventTick(6);CHECK(EventPhaseLine()!=NULL && EventCrankScale()==1,"grid warns before boosting");
    EventTick(2);CHECK(EventCrankScale()==1.25f && EventNoiseScale(NOISE_CRANK,posX,posY)==1.35f,"grid boost starts at eight seconds");
    EventTick(6);CHECK(EventCrankScale()==1 && EventNoiseScale(NOISE_CRANK,posX,posY)==1,"grid boost ends at fourteen seconds");
    EventTick(6);CHECK(EventCrankScale()==1,"grid cycle repeats without extending the boost");

    SetTestFloorEvent(EV_SUPPLY);
    flares=bottles=smokeBombs=BottleCap();battery=(float)BatteryMax();
    CHECK(!EventClaimCache(),"full inventory leaves the supply crate unclaimed");
    smokeBombs--;
    CHECK(EventClaimCache() && smokeBombs==BottleCap() && !EventClaimCache(),"supply fits available stock and cannot be claimed twice");

    SetTestFloorEvent(EV_GOLD_RESONANCE);
    CHECK(EventGoldCredit(EVENT_SEARCH,20,2)==4 && !EventGoldCredit(EVENT_SEARCH,20,2),
          "event bonuses are one per source and slot");
    CHECK(EventGoldCredit(EVENT_BREAKER,10000,0)==EventGoldCap(currentLevel)-4 &&
          !EventGoldCredit(EVENT_EXIT,10000,0),"all sources share the floor gold cap");
    unsigned char ledger[EVENT_WIRE_BYTES];
    CHECK(EventWireEncode(ledger) && EventWireRestore(ledger,sizeof ledger,currentLevel) &&
          !EventGoldCredit(EVENT_SEARCH,20,2),"ledger still prevents repeat payouts after restore");
    PutWord(ledger,11,0x80000000u);
    CHECK(!EventWireValid(ledger,sizeof ledger),"invalid ledger bit is rejected");

    SetTestFloorEvent(EV_FLOOD);
    EventPlaceFloor();
    for(int y=1;y<MAP_HEIGHT-1;y++)for(int x=1;x<MAP_WIDTH-1;x++)if(EventWetCell(x,y)){
        CHECK(!worldMap[y][x] && (EventWetCell(x+1,y) || EventWetCell(x-1,y) ||
              EventWetCell(x,y+1) || EventWetCell(x,y-1)),"every wet tile joins an open corridor band");
        CHECK(EventNoiseScale(NOISE_SPRINT,x+.5f,y+.5f)==1.50f &&
              EventNoiseScale(NOISE_WALK,x+.5f,y+.5f)==1,"only sprinting makes extra water noise");
    }
    int wetCount=0;
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)wetCount+=EventWetCell(x,y)!=0;
    CHECK(levelEvent==EV_FLOOD && wetCount>=6,"a normal generated flood offers at least six wet tiles");
    for(unsigned seed=1;seed<=1000;seed++) {
        int prior=-1;
        EventResetRun();
        for(int floor=1;floor<=8;floor++){
            currentLevel=floor;
            EventPlanFloor(0,(int)(seed%4),seed);
            const EventDef *d=EventDefinition(levelEvent);
            CHECK(!d || (floor>=d->unlock && levelEvent!=prior),"seeded floor selection honors level gates and no-repeat");
            prior=levelEvent;
        }
    }
    CheckGeneratedEventFloors();
    CheckHunterHearing();
}

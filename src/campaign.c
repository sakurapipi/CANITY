/*
 * Canity — 战役 5 关（v6）
 *
 * 每关只改一条规则。加/改关卡 = 改这一张表。
 */
#include "common.h"
#include "campaign.h"
#include "entity.h"
#include "world_api.h"
#include "loop_api.h"
#include "cell_meta.h"
#include "map_rooms.h"
#include "hatch.h"
#include "room_theme.h"

const CampaignDef kCampaign[] = {
    {"BLACKBOX RELAY","黑匣回路","Restore three relay fuses through a paired loop; light is a resource, not a permanent lock.","穿过双环回路收集三根保险丝，规划照明与返回路径。",0,1,0,1,1,0,1,3,1,.85f,1,1},
    {"ECHO ARCHIVE","回声档案","Two archive floors with long sound corridors. Search alcoves, then cross the central spine.","两层档案回廊；声音传播更远，利用侧室搜查和主轴突围。",0,1.65f,0,1,1,0,2,3,1,1,1,1.15f},
    {"COLD QUARANTINE","冷凝隔离","Ladder routes offer two approaches. Power wakes the Warden; the exit is a separate flank.","梯形隔离通道可绕行；接电唤醒守门人，出口在另一侧。",0,1,1,1.5f,1,0,1,4,1,1.05f,1.15f,1.2f},
    {"DEBT VAULTS","债务金库","Three vault floors: locked caches pay more, but carried gold increases awareness faster.","三层金库分区；解锁密箱获得收益，携带金币以1.6倍计入感知压力。",0,1.15f,0,1,1.6f,0,3,4,1,1.1f,1,1.35f},
    {"ZERO SIGNAL","归零列车","Parallel escape lanes cross sealed compartments. Power triggers a final pursuit; preserve resources.","平行撤离线穿过封闭车厢；接电触发最后追捕，保留资源突围。",0,1.35f,1,1.3f,1.3f,0,1,5,1.06f,1.2f,1.15f,1.5f}
};

typedef char kCampaign_count_check[(sizeof(kCampaign)/sizeof(kCampaign[0]) == CAMPAIGN_STAGE_COUNT) ? 1 : -1];

/* ---- 查询 ---- */

static const CampaignDef* cur(void) {
    if (!campaignMode) return NULL;
    if (campaignStage < 0 || campaignStage >= CAMPAIGN_STAGE_COUNT) return NULL;
    return &kCampaign[campaignStage];
}

const char* CampaignName(int stage) {
    if (stage < 0 || stage >= CAMPAIGN_STAGE_COUNT) return "?";
    return kCampaign[stage].nameEn;
}
const char* CampaignRule(int stage) {
    if (stage < 0 || stage >= CAMPAIGN_STAGE_COUNT) return "";
    return kCampaign[stage].ruleEn;
}

/* v7 第 62 轮：中文规则访问器（原来只有 En） */
const char* CampaignRuleZh(int stage) {
    if (stage < 0 || stage >= CAMPAIGN_STAGE_COUNT) return "";
    return kCampaign[stage].ruleZh;
}

int CampaignFlashlightLocked(void) {
    const CampaignDef* d = cur();
    return d ? d->flashlightLocked : 0;
}
float CampaignNoiseAlertMul(void) {
    const CampaignDef* d = cur();
    return d ? d->noiseAlertMul : 1.0f;
}
int CampaignWardenOnPower(void) {
    const CampaignDef* d = cur();
    return d ? d->wardenOnPower : 0;
}
float CampaignExitDistMul(void) {
    const CampaignDef* d = cur();
    return d ? d->exitDistMul : 1.0f;
}
float CampaignTierMul(void) {
    const CampaignDef* d = cur();
    return d ? d->tierMul : 1.0f;
}
int CampaignNoGiveUp(void) {
    const CampaignDef* d = cur();
    return d ? d->noGiveUp : 0;
}
float CampaignSpeedMulNow(void) {
    const CampaignDef* d = cur();
    return d ? d->speedMul : 1.0f;
}
float CampaignSanityMulNow(void) {
    const CampaignDef* d = cur();
    return d ? d->sanityMul : 1.0f;
}
float CampaignBatteryMulNow(void) {
    const CampaignDef* d = cur();
    return d ? d->batteryMul : 1.0f;
}
float CampaignGoldMulNow(void) {
    const CampaignDef* d = cur();
    return d ? d->goldMul : 1.0f;
}

/* ==================================================================
 * v6 第 ㉓ 轮：把最后两条战役规则接上
 * ==================================================================
 * 第 ⑧ 轮写好了 wardenOnPower / exitDistMul 两个字段，
 * 但**没有任何系统读它们** —— 第 3 关和第 5 关的规则是空的。
 * ================================================================== */

/* 第 3 关「守门人之城」：通电立刻刷守门人（不等防守计时） */
int CampaignWardenImmediate(void) {
    const CampaignDef* d = cur();
    return d ? d->wardenOnPower : 0;
}

/* 第 3/5 关：出口距离倍率（>1 = 更远） */
float CampaignExitDistNow(void) {
    const CampaignDef* d = cur();
    return d ? d->exitDistMul : 1.0f;
}

/* ==================================================================
 * v7 第 ㊿ 轮：**layers / fuses 两个字段填好了但全工程 0 个读取点**
 * ==================================================================
 * 设计文档说每关规模各异（1/2/1/3/1 层，保险丝 4/3/6/3·3·4/8），
 * 而实际层数由写死的 CAMPAIGN_LEVELS_PER_STAGE = 3 决定
 * → **5 关全是 3 层**，玩家感受到的是"还是那三层，怪快一点"。
 *
 * 文档自己的关卡原则是「好关卡不是更大更难，而是有一条新规则」。
 * 现在把这两个字段接出来。
 * ================================================================ */
int CampaignLayersNow(void) {
    if (campaignStage < 0 || campaignStage >= CAMPAIGN_STAGE_COUNT) return 3;
    {
        int n = kCampaign[campaignStage].layers;
        return (n > 0 && n <= 8) ? n : 3;
    }
}

int CampaignFusesNow(void) {
    if (campaignStage < 0 || campaignStage >= CAMPAIGN_STAGE_COUNT) return 0;
    {
        int n = kCampaign[campaignStage].fuses;
        return (n > 0 && n <= 8) ? n : 0;
    }
}

int CampaignStartFloor(int stage) {
    int floor = 1;
    for (int i = 0; i < stage && i < CAMPAIGN_STAGE_COUNT; ++i) floor += kCampaign[i].layers;
    return floor;
}
int CampaignReward(int stage) {
    static const int rewards[] = {300, 500, 700, 900, 1400};
    return stage >= 0 && stage < CAMPAIGN_STAGE_COUNT ? rewards[stage] : 0;
}

static void CampaignCarve(int x0,int y0,int x1,int y1){
    if(x0>x1){int t=x0;x0=x1;x1=t;}
    if(y0>y1){int t=y0;y0=y1;y1=t;}
    for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++)if(x>0 && y>0 && x<MAP_WIDTH-1 && y<MAP_HEIGHT-1)worldMap[y][x]=0;
}
int CampaignThemeNow(void){static const int themes[5]={5,1,4,2,6};return campaignMode && campaignStage>=0 && campaignStage<5?themes[campaignStage]:0;}
void CampaignBuildFloor(void){
    if(!campaignMode)return;
    CellMetaReset();RoomPropReset();HatchReset();roomSeedUsed=0;
    for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)worldMap[y][x]=1;
    CampaignCarve(1,1,3,3);CampaignCarve(3,3,3,18);
    CampaignCarve(2,17,33,19);CampaignCarve(17,3,19,32);
    if(campaignStage==0){
        CampaignCarve(5,7,30,9);CampaignCarve(5,26,30,28);CampaignCarve(5,7,7,28);CampaignCarve(28,7,30,28);
    }else if(campaignStage==1){
        for(int y=5;y<=29;y+=6){CampaignCarve(3,y,31,y+1);CampaignCarve(y%12==5?3:29,y, y%12==5?5:31,y+5);}
    }else if(campaignStage==2){
        CampaignCarve(7,4,9,31);CampaignCarve(26,4,28,31);for(int y=6;y<=30;y+=6)CampaignCarve(7,y,28,y+1);
    }else if(campaignStage==3){
        for(int y=4;y<=27;y+=8)for(int x=4;x<=27;x+=8){CampaignCarve(x,y,x+4,y+4);CampaignCarve(x+2,y+2,18,y+2);CampaignCarve(x+2,y+2,x+2,18);}
        for(int y=6;y<=30;y+=8)CampaignCarve(6,y,30,y);
    }else{
        CampaignCarve(4,6,31,7);CampaignCarve(4,28,31,29);CampaignCarve(4,6,5,29);CampaignCarve(30,6,31,29);
        for(int x=8;x<=26;x+=6){CampaignCarve(x,7,x+1,28);CampaignCarve(x-2,11,x+3,14);CampaignCarve(x-2,23,x+3,26);}
    }
    /* These maintenance bays connect to the common spine in every chapter. */
    CampaignCarve(9,14,14,20);CampaignCarve(22,17,27,23);
    CellMetaFillRoom(9,14,6,7,1,CampaignThemeNow(),ACOUSTIC_METAL,0);
    CellMetaFillRoom(22,17,6,7,1,CampaignThemeNow(),ACOUSTIC_HARD,0);
    static const int fixtures[5][2]={{3,5},{4,4},{7,5},{8,5},{9,9}};
    int stage=campaignStage>=0 && campaignStage<CAMPAIGN_STAGE_COUNT?campaignStage:0;
    RoomPropAdd(10.5f,14.5f,fixtures[stage][0]);RoomPropAdd(26.5f,22.5f,fixtures[stage][1]);
    HatchAdd(13.5f,14.5f,HatchKindForTheme(CampaignThemeNow()),CampaignThemeNow());RoomThemeBind();RoomSetCount(1);
    posX=3.5f;posY=18.5f;exitX=32.5f;exitY=18.5f;dirX=1;dirY=0;planeX=0;planeY=.66f;
}

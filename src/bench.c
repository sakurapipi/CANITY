/*
 * Canity — 当前 20 节点属性改造树；节点编号兼容既有存档。
 */
#include "common.h"
#include "run_economy.h"
#include "skills.h"
#include "bench.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "entity.h"   /* sanity 在这里声明 */
#include "lang.h"
#include "ui.h"
#include "save.h"
#include "wallet.h"
#include "threat.h"

/* 节点 id = col * BENCH_ROWS + row */
const BenchNode benchNodes[] = {
    {"ENDURANCE","耐力","Stamina cap +15%","体力上限 +15%",90,0,0,0},
    {"LIGHT PACK","轻装","Move speed +6%","移速 +6%",180,0,1,0},
    {"ECONOMY","省力","Sprint cost -25%","冲刺消耗 -25%",320,0,2,0},
    {"BULL","蛮牛","Stamina +30%, sprint cost -15%","体力再 +30%，冲刺消耗再 -15%",550,0,3,1},
    {"BIG CELL","大电池","Battery cap +20%","电量上限 +20%",90,1,0,0},
    {"SAVER","省电","Battery drain -20%","手电耗电 -20%",180,1,1,0},
    {"BRIGHT","强光","Beam distance +25%","照射距离 +25%",320,1,2,0},
    {"EVERLIGHT","长明","Battery drain another -35%","耗电再 -35%",550,1,3,1},
    {"COMPOSURE","镇定","Sanity cap +20","理智上限 +20",90,2,0,0},
    {"THICK SKIN","厚皮","Grab sanity cost -20%","被抓扣理智 -20%",180,2,1,0},
    {"RECOVER","回神","Safe room healing +50%","安全屋回复 +50%",320,2,2,0},
    {"IRON MIND","铁石","Grab cost another -30%","被抓扣理智再 -30%",550,2,3,1},
    {"SPARE TOOL","备用扳手","Wrench charges +1 per floor","每层扳手次数 +1",90,3,0,0},
    {"LONG HANDLE","长柄","Knockback distance +1","击退距离 +1 格",180,3,1,0},
    {"HEAVY HEAD","重锤","Knockback +1.5, stun +50%","击退再 +1.5 格，硬直 +50%",320,3,2,0},
    {"SHOCK","震荡","Hits erase the hunters memory","击退命中清空怪物记忆与可疑点",550,3,3,1},
    {"SWIFT","疾风","Move +12%, stamina cap -15%","移速再 +12%，体力上限 -15%",550,0,3,1},
    {"SEARCHLIGHT","探照","Beam +40%, battery drain +20%","照射距离再 +40%，耗电 +20%",550,1,3,1},
    {"DESPERATE","绝境","Low sanity: move +10%, noise -20%","理智低于30时移速 +10%，噪音 -20%",550,2,3,1},
    {"TEMPERED","回震","Stun another +100%, no damage","硬直再 +100%，不造成伤害",550,3,3,1},
    {"SOFT SOLES","软底鞋","Walk noise -10%","行走噪音 -10%",120,1,0,0},
    {"TOOL COOLING","工具散热","Wrench cooldown -12%","扳手冷却时间 -12%",240,3,1,0},
    {"FIELD REST","野外休整","Safe-room recovery +20%","安全区域恢复效果 +20%",180,2,1,0},
    {"RESERVE LIGHT","应急照明","Below 30 sanity: flashlight drain -15%","理智低于30时手电耗电 -15%",320,2,2,0},

    {"BALANCED LOAD","平衡负重","Move speed +3%","移速 +3%",140,0,1,0},
    {"CLEAR MIND","澄心","Sanity cap +5","理智上限 +5",140,2,1,0},
    {"BREATH CONTROL","调息","Sprint cost -10%","冲刺耗体 -10%",180,0,2,0},
    {"WIDE BEAM","广照","Light range +10%","照明距离 +10%",180,1,2,0},
    {"CALM RESERVE","定心储备","Passive sanity drain -8%","自然理智衰减 -8%",220,0,1,0},
    {"SCAVENGE","拾荒诀窍","Crate gold +15%","搜查箱子金币 +15%",210,1,1,0},
    {"RHYTHM FOCUS","节拍专注","Rhythm hits restore +0.2 sanity","每次校准命中额外恢复0.2理智",280,2,1,0},
    {"TRIAGE","急救统筹","Recovery service base prices -15%","恢复服务基础价格 -15%",250,0,1,0},
    {"SECOND CELL","并联电芯","Battery capacity +15%","电量上限额外 +15%",210,1,2,0},
    {"LIGHT TOUCH","柔光驱动","Flashlight consumption -10%","手电耗电额外 -10%",250,2,2,0},
    {"RUNNER","长跑呼吸","Sprint consumption -10%","冲刺耗体额外 -10%",240,0,2,0},
    {"STEADY HANDS","稳手搜查","Search duration -12%","搜查时间 -12%",220,1,2,0},
    {"CRANK TORQUE","省时摇柄","Crank duration -15%","手摇充电时间 -15%",230,2,3,0},
    {"POCKET CHANGE","节拍佣金","Rhythm completion gold +20%","校准通关金币 +20%",260,0,3,0},
    {"OPENING BONUS","脱身报酬","Verified escape gold +15%","严格确认脱身后金币 +15%",300,1,3,0},
    {"DEEP DIVIDEND","深层分红","Floor completion gold +15%","楼层通关金币 +15%",340,2,3,0},

};

typedef char benchNodes_count_check[(sizeof(benchNodes)/sizeof(benchNodes[0])==BENCH_COUNT) ? 1 : -1];


uint64_t benchOwned = 0;


void BenchResetRun(void) {
    benchOwned = 0u;
}

int BenchHas(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 0;
    return (benchOwned & (UINT64_C(1) << id)) ? 1 : 0;
}

int BenchPrereqOk(int id) {
    if(id<0 || id>=BENCH_COUNT)return 0;
    int pre=BenchPrereqId(id);
    return pre<0 || BenchHas(pre);
}


/* v6：顶点二选一 —— A(id 3/7/11/15) 与 B(id 16/17/18/19) 互斥 */
static int capstonePartner(int id) {
    if (id == 3 || id == 7 || id == 11 || id == 15) return 16+(id-3)/4;
    if (id >= 16 && id <= 19) return (id-16)*4+3;
    return -1;
}

/* v7：给 UI 用 —— 这个节点买下去会涨多少 THREAT。
   算法必须和 BenchTryBuy 里的一致（ThreatAddForTier(row+1, star)）。 */
float BenchThreatCostOf(int id) {
    if (id < 0 || id >= BENCH_COUNT || BenchHas(id)) return 0.0f;
    return ThreatCostForTier(benchNodes[id].row + 1, benchNodes[id].star);
}


int BenchCanBuy(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 0;
    if (BenchHas(id)) return 0;
    if (!BenchPrereqOk(id)) return 0;
    {
        int partner = capstonePartner(id);
        if (partner >= 0 && BenchHas(partner)) return 0;   /* 对面已选，锁死 */
    }
    {  return WalletCanPayRun(benchNodes[id].cost); }
}

/* ==================================================================
 * v7 第 ㊿ 轮：**返回值语义原来和调用方完全反了**
 * ==================================================================
 * 这里成功返回 1、失败返回 0；
 * 而 shop.c 把这个函数当"0 = 成功、2/3/4 = 各种失败"来解读。
 *
 * 后果：**买失败时屏幕弹「已安装」并播放成功音效**，
 *       买成功时一个字都没有。
 *       这不是缺提示，是"假成功回应"——玩家会以为买了，
 *       然后发现钱没扣、节点没亮，接着怀疑游戏坏了。
 *
 * 现在统一成原因码（和 RunUpgTryBuy 一致）：
 *   1 = 成功 / 2 = 已拥有 / 3 = 需要先买上一层
 *   4 = 金币不够 / 5 = 已被对面的顶点锁死
 * ================================================================ */
int BenchTryBuy(int id) {
    if (id < 0 || id >= BENCH_COUNT)   return 3;
    if (BenchHas(id))                  return 2;
    if (!BenchPrereqOk(id))            return 3;
    {
        int partner = capstonePartner(id);
        if (partner >= 0 && BenchHas(partner)) return 5;
    }
    { 
      if (!WalletCanPayRun(benchNodes[id].cost)) return 4; }
    {  if (!WalletSpendRun(benchNodes[id].cost)) return 4; }
    float addedThreat = BenchThreatCostOf(id);
    benchOwned |= (UINT64_C(1) << id);
    /* v6：买得越多，怪越凶 —— 局内强度必须有代价 */
    { 
      ThreatAdd(addedThreat); }
    {  SaveGame(); }
    return 1;
}

/* Current attribute-tree effects. Retired mechanism nodes have no public hooks.
 * These three sensory queries intentionally read the separate persistent skill tree. */
float BenchWalkNoiseMul(void) { return (BenchHas(BENCH_DESPERATE)&&sanity<30.0f?.8f:1.0f)*(BenchHas(BENCH_SOFT_SOLES)?.9f:1.0f); }
float BenchFearDamageMul(void) { return (BenchHas(BENCH_THICK_SKIN) ? 0.80f : 1.0f) * (BenchHas(BENCH_IRON_MIND) ? 0.70f : 1.0f); }
float BenchFlashDrainMul(void) { return (BenchHas(BENCH_SAVER) ? 0.80f : 1.0f) * (BenchHas(BENCH_EVERLIGHT) ? 0.65f : 1.0f) * (BenchHas(BENCH_SEARCHLIGHT) ? 1.20f : 1.0f)*(BenchHas(BENCH_RESERVE_LIGHT)&&sanity<30.0f?.85f:1.0f)*(BenchHas(BENCH_LIGHT_TOUCH)?.9f:1.0f); }
int BenchHearing(void) { return skillRank[8] >= 3; }
int BenchPremonition(void) { return skillRank[2] >= 3; }
int BenchInnerEye(void) { return skillRank[9] >= 3; }

/* ==================== 画布视口（v2.9，照搬思维导图模型） ====================
 *   screen = world * k + (vx, vy)
 *   drag:   vx += 屏幕位移（不除 k）
 *   wheel:  以鼠标为锚点缩放
 * ======================================================================== */

float treeVX = 0.0f, treeVY = 0.0f, treeVK = 1.0f;

int   treeDragging = 0;
float treeDragMouseX = 0.0f, treeDragMouseY = 0.0f;
float treeDragVX = 0.0f, treeDragVY = 0.0f;

#define TREE_K_MIN 0.18f
#define TREE_K_MAX 2.60f

static const float kBenchPosition[BENCH_COUNT][2]={
    {4.07032f,2.35000f},
    {6.40817f,2.55838f},
    {8.84755f,2.12859f},
    {11.25185f,1.04206f},
    {0.00000f,-4.70000f},
    {-1.95653f,-6.61679f},
    {-3.40353f,-8.43955f},
    {-5.20975f,-10.02739f},
    {-4.07032f,2.35000f},
    {-5.41971f,4.27045f},
    {-6.26719f,6.59790f},
    {-6.52837f,9.22336f},
    {2.58036f,-8.72650f},
    {3.20418f,-10.83620f},
    {2.57169f,-13.25279f},
    {1.50228f,-15.62796f},
    {10.49454f,4.18981f},
    {-3.20418f,-10.83620f},
    {-8.87576f,6.99363f},
    {4.45182f,-15.05561f},
    {1.95653f,-6.61679f},
    {6.22404f,-11.97962f},
    {-8.45136f,3.37410f},
    {-1.29177f,-13.43806f},
    {3.98635f,5.63196f},
    {-6.87060f,0.63630f},
    {5.25736f,7.42766f},
    {-1.50228f,-15.62796f},
    {-9.06122f,0.83918f},
    {7.14773f,5.63204f},
    {-11.25185f,1.04206f},
    {-10.49454f,4.18981f},
    {-0.87075f,-9.05824f},
    {-1.08126f,-11.24815f},
    {6.52837f,9.22336f},
    {-3.82800f,-12.94590f},
    {-4.45182f,-15.05561f},
    {8.87576f,6.99363f},
    {-12.53773f,5.00553f},
    {10.60378f,8.35523f},
};
float BenchNodeWorldX(int id) {
    return id >= 0 && id < BENCH_COUNT ? kBenchPosition[id][0] * BENCH_NODE_WX : 0.0f;
}


float BenchNodeWorldY(int id) {
    return id >= 0 && id < BENCH_COUNT ? kBenchPosition[id][1] * BENCH_NODE_WY : 0.0f;
}


void BenchFocusNode(int id) {
    float cx = (float)winW * 0.64f;
    float cy = (float)winH * 0.43f;
    float x = BenchNodeWorldX(id) * treeVK + treeVX;
    float y = BenchNodeWorldY(id) * treeVK + treeVY;
    if (x < winW * .33f || x > winW * .93f || y < winH * .20f || y > winH * .68f) {
        treeVX = cx - BenchNodeWorldX(id) * treeVK;
        treeVY = cy - BenchNodeWorldY(id) * treeVK;
    }
    uiDirty = 1;
}


void BenchDragStart(int mx, int my) {
    treeDragging = 1;
    treeDragMouseX = (float)mx; treeDragMouseY = (float)my;
    treeDragVX = treeVX; treeDragVY = treeVY;
}
void BenchDragMove(int mx, int my) {
    if (!treeDragging) return;
    treeVX = treeDragVX + ((float)mx - treeDragMouseX);
    treeVY = treeDragVY + ((float)my - treeDragMouseY);
    uiDirty = 1;
}
void BenchDragEnd(void) { treeDragging = 0; }

void BenchZoomAt(float factor, int mx, int my) {
    float k0 = treeVK, k2 = k0 * factor;
    if (k2 < TREE_K_MIN) k2 = TREE_K_MIN;
    if (k2 > TREE_K_MAX) k2 = TREE_K_MAX;
    if (k2 == k0) { uiDirty = 1; return; }
    treeVX = (float)mx - ((float)mx - treeVX) * (k2 / k0);
    treeVY = (float)my - ((float)my - treeVY) * (k2 / k0);
    treeVK = k2;
    uiDirty = 1;
}

void BenchResetView(void) {
    float minX=0,maxX=0,minY=0,maxY=0;
    for(int i=0;i<BENCH_COUNT;i++){float x=BenchNodeWorldX(i),y=BenchNodeWorldY(i);minX=fminf(minX,x);maxX=fmaxf(maxX,x);minY=fminf(minY,y);maxY=fmaxf(maxY,y);}
    treeVK=fminf(winW*.60f/(maxX-minX+200),winH*.52f/(maxY-minY+130));
    if(treeVK<TREE_K_MIN)treeVK=TREE_K_MIN;
    treeVX=winW*.64f-(minX+maxX)*.5f*treeVK;treeVY=winH*.42f-(minY+maxY)*.5f*treeVK;uiDirty=1;
}


void BenchPanBy(float dxScreen, float dyScreen) {
    treeVX += dxScreen; treeVY += dyScreen;
    uiDirty = 1;
}

float BenchMoveMul(void) { return (BenchHas(BENCH_BALANCED_LOAD)?1.03f:1.0f)*(BenchHas(BENCH_LIGHT_PACK)?1.06f:1.0f)*(BenchHas(BENCH_SWIFT)?1.12f:1.0f)*(BenchHas(BENCH_DESPERATE)&&sanity<30.0f?1.1f:1.0f); }
float BenchStaminaMul(void) { return (BenchHas(BENCH_ENDURANCE)?1.15f:1.0f)*(BenchHas(BENCH_BULL)?1.30f:1.0f)*(BenchHas(BENCH_SWIFT)?0.85f:1.0f); }
float BenchSprintCostMul(void) { return (BenchHas(BENCH_BREATH_CONTROL)?.9f:1.0f)*(BenchHas(BENCH_ECONOMY)?0.75f:1.0f)*(BenchHas(BENCH_BULL)?0.85f:1.0f)*(BenchHas(BENCH_RUNNER)?.9f:1.0f); }
float BenchBatteryCapMul(void) { return (BenchHas(BENCH_BIG_CELL)?1.20f:1.0f)*(BenchHas(BENCH_SECOND_CELL)?1.15f:1.0f); }
float BenchLightRangeMul(void) { return (BenchHas(BENCH_WIDE_BEAM)?1.1f:1.0f)*(BenchHas(BENCH_BRIGHT)?1.25f:1.0f)*(BenchHas(BENCH_SEARCHLIGHT)?1.40f:1.0f); }
float BenchSanityAdd(void) { return (BenchHas(BENCH_COMPOSURE)?20.0f:0.0f)+(BenchHas(BENCH_CLEAR_MIND)?5.0f:0.0f); }
float BenchHealMul(void) { return (BenchHas(BENCH_RECOVER)?1.50f:1.0f)*(BenchHas(BENCH_FIELD_REST)?1.20f:1.0f); }
float BenchKnockExtra(void) { return (BenchHas(BENCH_LONG_HANDLE)?1.0f:0.0f)+(BenchHas(BENCH_HEAVY_HEAD)?1.5f:0.0f); }
float BenchKnockStunMul(void) { return (BenchHas(BENCH_HEAVY_HEAD)?1.5f:1.0f)*(BenchHas(BENCH_TEMPERED)?2.0f:1.0f); }
int BenchKnockCharges(void) { return BenchHas(BENCH_SPARE_TOOL)?1:0; }
int BenchShock(void) { return BenchHas(BENCH_SHOCK); }
void BenchRefundRun(void) {
    int refund=0; for(int i=0;i<BENCH_COUNT;++i) if(BenchHas(i)) refund+=benchNodes[i].cost;
    BenchResetRun(); ThreatReset(); WalletAddRun(refund); SaveGame();
}

int BenchPrereqId(int id) {
    static const signed char parents[BENCH_COUNT]={-1,0,1,2,-1,4,5,6,-1,8,9,10,20,12,13,14,2,6,10,14,4,13,9,33,0,8,24,23,25,1,28,22,5,32,26,17,35,29,31,37};
    return id>=0 && id<BENCH_COUNT?parents[id]:-1;
}
int BenchArchetype(int id) {
    static const unsigned char groups[BENCH_COUNT]={1,1,1,1,0,0,0,0,2,2,2,2,0,0,0,0,1,0,2,0,0,0,2,0,1,2,1,0,2,1,2,2,0,0,1,0,0,1,2,1};
    return id>=0 && id<BENCH_COUNT?groups[id]:-1;
}
void BenchNodeBoxSize(int* width, int* height) {
    int w = (int)((BENCH_NODE_WX - 46.0f) * treeVK);
    int h = (int)((BENCH_NODE_WY - 52.0f) * treeVK);
    float separation=1e9f;
    for(int i=0;i<BENCH_COUNT;i++)for(int j=i+1;j<BENCH_COUNT;j++){
        float dx=fabsf(BenchNodeWorldX(i)-BenchNodeWorldX(j))*treeVK;
        float dy=fabsf(BenchNodeWorldY(i)-BenchNodeWorldY(j))*treeVK;
        if(dx>w+4 || dy>h+4)continue;
        float span=fmaxf(dx,dy);if(span<separation)separation=span;
    }
    if(separation<1e8f){int cap=(int)(separation*.72f);if(w>cap)w=cap;if(h>cap)h=cap;}
    if(w<8)w=8;
    if(h<6)h=6;
    if (width) *width = w;
    if (height) *height = h;
}
int BenchNodeAt(int x, int y) {
    if (x < winW * .30f || x > winW - UIS(20) || y < UIS(70) || y > winH - UIS(178)) return -1;
    int w, h; BenchNodeBoxSize(&w, &h);
    for (int id = 0; id < BENCH_COUNT; id++) {
        int cx = (int)(BenchNodeWorldX(id) * treeVK + treeVX);
        int cy = (int)(BenchNodeWorldY(id) * treeVK + treeVY);
        if (cx-w/2 < winW*.30f || cx+w/2 > winW-UIS(20) || cy-h/2 < UIS(70) || cy+h/2 > winH-UIS(178)) continue;
        if (x >= cx-w/2 && x <= cx+w/2 && y >= cy-h/2 && y <= cy+h/2) return id;
    }
    return -1;
}
int BenchNavNearest(int from, int dx, int dy) {
    if (from < 0 || from >= BENCH_COUNT) return 0;
    if (!dx && !dy) return from;
    float bestScore = 1e30f; int best = from;
    for(int pass=0;pass<2;pass++){
    for (int id = 0; id < BENCH_COUNT; id++) {
        if(!pass && BenchPrereqId(id)!=from && BenchPrereqId(from)!=id)continue;
        if (id == from) continue;
        float x = BenchNodeWorldX(id) - BenchNodeWorldX(from);
        float y = BenchNodeWorldY(id) - BenchNodeWorldY(from);
        float forward = x*dx + y*dy;
        if (forward <= 1.0f) continue;
        float side = x*dy - y*dx;
        float score = forward + fabsf(side)*2.5f;
        if (score < bestScore) { bestScore = score; best = id; }
    }
    if(best!=from)return best;
    }
    return from;
}

void BenchBuyButtonRect(int* x0,int* y0,int* x1,int* y1) {
    *x1=winW-UIS(28); *x0=*x1-UIS(108);
    *y1=winH-UIS(25); *y0=*y1-UIS(36);
}
int BenchBuyButtonAt(int x,int y) {
    int x0,y0,x1,y1; BenchBuyButtonRect(&x0,&y0,&x1,&y1);
    return x>=x0 && x<=x1 && y>=y0 && y<=y1;
}

float BenchWrenchCooldownMul(void) { return BenchHas(BENCH_TOOL_COOLING)?.88f:1.0f; }

const char* BenchShortName(int id) {
    return id>=0 && id<BENCH_COUNT?(g_lang==LANG_ZH?benchNodes[id].nameZh:benchNodes[id].name):"?";
}

float BenchCrateGoldMul(void){return BenchHas(BENCH_SCAVENGE)?1.15f:1;}
float BenchExitGoldMul(void){return BenchHas(BENCH_DEEP_DIVIDEND)?1.15f:1;}
float BenchEscapeGoldMul(void){return BenchHas(BENCH_OPENING_BONUS)?1.15f:1;}
float BenchRestPriceMul(void){return BenchHas(BENCH_TRIAGE)?.85f:1;}
float BenchRhythmHealBonus(void){return BenchHas(BENCH_RHYTHM_FOCUS)?.2f:0;}
float BenchSearchTimeMul(void){return BenchHas(BENCH_STEADY_HANDS)?.88f:1;}
float BenchCrankTimeMul(void){return BenchHas(BENCH_CRANK_TORQUE)?.85f:1;}
float BenchSanityDrainMul(void){return BenchHas(BENCH_CALM_RESERVE)?.92f:1;}

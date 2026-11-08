/*
 * Canity — 局内改造树实现（v2.7）
 */
#include "common.h"
#include "bench.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "entity.h"   /* sanity 在这里声明 */
#include "lang.h"
#include "save.h"
#include "wallet.h"
#include "threat.h"

/* 节点 id = col * BENCH_ROWS + row */
const BenchNode benchNodes[] = {
    {"ENDURANCE","耐力","Stamina cap +15%","体力上限 +15%",350,0,0,0},
    {"LIGHT PACK","轻装","Move speed +6%","移速 +6%",700,0,1,0},
    {"ECONOMY","省力","Sprint cost -25%","冲刺消耗 -25%",1200,0,2,0},
    {"BULL","蛮牛","Stamina +30%, sprint cost -15%","体力再 +30%，冲刺消耗再 -15%",2000,0,3,1},
    {"BIG CELL","大电池","Battery cap +20%","电量上限 +20%",350,1,0,0},
    {"SAVER","省电","Battery drain -20%","手电耗电 -20%",700,1,1,0},
    {"BRIGHT","强光","Beam distance +25%","照射距离 +25%",1200,1,2,0},
    {"EVERLIGHT","长明","Battery drain another -35%","耗电再 -35%",2000,1,3,1},
    {"COMPOSURE","镇定","Sanity cap +20","理智上限 +20",350,2,0,0},
    {"THICK SKIN","厚皮","Grab sanity cost -20%","被抓扣理智 -20%",700,2,1,0},
    {"RECOVER","回神","Safe room healing +50%","安全屋回复 +50%",1200,2,2,0},
    {"IRON MIND","铁石","Grab cost another -30%","被抓扣理智再 -30%",2000,2,3,1},
    {"SPARE TOOL","备用扳手","Wrench charges +1 per floor","每层扳手次数 +1",350,3,0,0},
    {"LONG HANDLE","长柄","Knockback distance +1","击退距离 +1 格",700,3,1,0},
    {"HEAVY HEAD","重锤","Knockback +1.5, stun +50%","击退再 +1.5 格，硬直 +50%",1200,3,2,0},
    {"SHOCK","震荡","Hits erase the hunters memory","击退命中清空怪物记忆与可疑点",2000,3,3,1},
    {"SWIFT","疾风","Move +12%, stamina cap -15%","移速再 +12%，体力上限 -15%",2000,0,3,1},
    {"SEARCHLIGHT","探照","Beam +40%, battery drain +20%","照射距离再 +40%，耗电 +20%",2000,1,3,1},
    {"DESPERATE","绝境","Low sanity: move +10%, noise -20%","理智低于30时移速 +10%，噪音 -20%",2000,2,3,1},
    {"TEMPERED","回震","Stun another +100%, no damage","硬直再 +100%，不造成伤害",2000,3,3,1},
};

typedef char benchNodes_count_check[(sizeof(benchNodes)/sizeof(benchNodes[0])==BENCH_COUNT) ? 1 : -1];


unsigned int benchOwned = 0u;


void BenchResetRun(void) {
    benchOwned = 0u;
}

int BenchHas(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 0;
    return (benchOwned & (1u << id)) ? 1 : 0;
}

int BenchPrereqOk(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 0;
    int row = benchNodes[id].row;
    if (row == 0) return 1;                       /* 第一层无条件 */
    if(id>=16) return BenchHas((id-16)*4+2);
    return BenchHas(id - 1);                      /* 同列上一层 */
}

/* v6：顶点二选一 —— A(id 3/7/11/15) 与 B(id 16/17/18/19) 互斥 */
static int capstonePartner(int id) {
    if (id == 3 || id == 7 || id == 11 || id == 15) return 16+(id-3)/4;
    if (id >= 16 && id <= 19) return (id-16)*4+3;
    return -1;
}

/* v7：给 UI 用 —— 这个节点买下去会涨多少 THREAT。
   算法必须和 BenchTryBuy 里的一致（ThreatAddForTier(row+1, star)）。 */
int BenchThreatCostOf(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 0;
    /* 和 ThreatAddForTier 的公式保持一致 */
    if (benchNodes[id].star) return 3;                 /* THREAT_W_CAPSTONE */
    switch (benchNodes[id].row + 1) {
    case 1:  return 1;
    case 2:  return 2;
    case 3:  return 3;
    default: return 1;
    }
}

int BenchCanBuy(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 0;
    if (BenchHas(id)) return 0;
    if (!BenchPrereqOk(id)) return 0;
    {
        int partner = capstonePartner(id);
        if (partner >= 0 && BenchHas(partner)) return 0;   /* 对面已选，锁死 */
    }
    { extern int WalletCanPayRun(int); return WalletCanPayRun(benchNodes[id].cost); }
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
    { extern int WalletCanPayRun(int);
      if (!WalletCanPayRun(benchNodes[id].cost)) return 4; }
    { extern int WalletSpendRun(int); if (!WalletSpendRun(benchNodes[id].cost)) return 4; }
    benchOwned |= (1u << id);
    /* v6：买得越多，怪越凶 —— 局内强度必须有代价 */
    { extern void ThreatAddForTier(int, int);
      ThreatAddForTier(benchNodes[id].row + 1, benchNodes[id].star); }
    { extern void SaveGame(void); SaveGame(); }
    return 1;
}

/* ==================== 效果查询（v3.0 逃生向） ==================== */

/* 潜行 */
/* ===== v3.4：**id 全部重映射**（BENCH_ROWS 3 → 4 导致错位）=====
   节点 id = col * BENCH_ROWS + row。行数一改，id 就全变了。
   下面每个 accessor 后面的注释写明它现在指向哪个节点，便于维护。 */

/* ---- 列 0：潜行 ---- */
float BenchWalkNoiseMul(void)  { return BenchHas(18) && sanity<30.0f ? 0.8f : 1.0f; }   /* 0 LIGHT STEP  噪音 -35% */
float BenchSearchQuiet(void)   { return 1.0f; }   /* 1 QUIET LID   翻箱 -50% */
int   BenchSilentThrow(void)   { return 0; }          /* 2 SILENT THROW 投掷无声 */
int   BenchVanish(void)        { return 0; }                   /* 3 VANISH      隐身 */

/* ---- 列 1：体能 ---- */
int   BenchStaminaBonus(void)  { return 0; }         /* 4 ENDURANCE   +30 */
int   BenchCrankWalk(void)     { return 0; }          /* 5 CRANK WALK  边走边摇 */
int   BenchSlide(void)         { return 0; }          /* 6 SLIDE       滑铲 */
int   BenchSecondWind(void)    { return 0; }                   /* 7 SECOND WIND 二次呼吸 */

/* ---- 列 2：投掷 ---- */
int   BenchDeepPocket(void)    { return 0; }          /* 8 DEEP POCKET 上限 +2 */
float BenchSteadyHand(void)    { return 1.0f; }   /* 9 STEADY HAND 蓄力更快 */
int   BenchSalvage(void)       { return 0; }          /* 10 SALVAGE    可捡回 */
int   BenchTwinThrow(void)     { return 0; }          /* 11 TWIN THROW 双投 */

/* ---- 列 3：生存 ---- */
float BenchFearDamageMul(void) { return (BenchHas(9)?0.80f:1.0f)*(BenchHas(11)?0.70f:1.0f); }   /* 12 THICK SKIN 被抓 -20% 理智 */
float BenchBatterySaver(void)  { return 1.0f; }   /* 13 SPARE CELL 耗电 -25% */
int   BenchCatchBreak(void)    { return 0; }          /* 14 CATCH BREAK 空格挣脱 */
int   BenchPlayDead(void)      { return 0; }                  /* 15 PLAY DEAD   假死 */

/* v6：四个第二顶点的效果（id 16..19，与 3/7/11/15 互斥） */
int   BenchBullRush(void)   { return 0; }   /* 16 蛮冲：移速+噪音 */
int   BenchBurst(void)      { return 0; }   /* 17 爆发：冲刺+体力- */
int   BenchLure(void)       { return 0; }   /* 18 诱饵：投掷噪音x3 */
int   BenchRecover(void)    { return 0; }   /* 19 回神：安全屋+60% */

/* ===== v3.4 退役节点的兼容桩 =====
   HOLD BREATH / BURST / HEARING / INNER EYE / PREMONITION / COMPOSURE
   这 6 个节点已经从局内树移除（方案 v5：感知类与回声弹重叠，
   数值类感知不到）。调用点还在，这里给「无加成」的默认值。 */
float BenchHideStealth(void)   { return 1.0f; }   /* HOLD BREATH 已并入局外树 */
/* 2026-11-08（第 13 轮）：BenchSprintMul 是常量桩（全工程零调用），已删 */   /* BURST 已退役 */
/* v6 **三个退役节点的功能重新挂到活着的节点上** ——
   原来它们恒返回 0，而 render.c / ui.c 里三整段代码
   （听风落点标记 / 预知红边 / 心眼距离读数）看起来是活的、实际永远不执行。
   现在改挂：
     听风落点  ← 列 2「屏息」BREATH 满级（听着更专注）
     预知红边  ← 列 0「鬼步」GHOST STEP 满级（对危险更敏感）
     心眼距离  ← 列 2「静面」STILL FACE 满级 */
int BenchHearing(void)     { extern int skillRank[]; return skillRank[8] >= 3; }   /* BREATH 满级 */
int BenchPremonition(void) { extern int skillRank[]; return skillRank[2] >= 3; }  /* GHOST STEP 满级 */
int BenchInnerEye(void)    { extern int skillRank[]; return skillRank[9] >= 3; }  /* STILL FACE 满级 */
float BenchDarkFearMul(void)   { return 1.0f; }   /* COMPOSURE 已退役 */

/* ---- 兼容旧接口（改造树重做后这些名字换了，留空实现避免链接错误） ---- */
int   BenchEnergyMax(int base)      { return base; }
/* 2026-11-08（第 13 轮）：BenchDamageMul 是常量桩（全工程零调用），已删 */
float BenchSanityDrainMul(void)     { return 1.0f; }
float BenchFlashDrainMul(void) { return (BenchHas(5)?0.80f:1.0f)*(BenchHas(7)?0.65f:1.0f)*(BenchHas(17)?1.20f:1.0f); }
float BenchBatteryPickupMul(void)   { return 1.0f; }
/* 2026-11-08（第 13 轮）：BenchLowSpeedAdd 是常量桩（全工程零调用），已删 */
/* 2026-11-08（第 13 轮）：BenchArmorIgnore 是常量桩（全工程零调用），已删 */
/* 2026-11-08（第 13 轮）：BenchDarkSense 是常量桩（全工程零调用），已删 */
/* 2026-11-08（第 13 轮）：BenchExecuteThreshold 是常量桩（全工程零调用），已删 */
/* 2026-11-08（第 13 轮）：BenchMonsterSpeedMul 是常量桩（全工程零调用），已删 */
/* 2026-11-08（第 13 轮）：BenchDrainPenalty 是常量桩（全工程零调用），已删 */
/* 2026-11-08（第 13 轮）：BenchAlchemy 是常量桩（全工程零调用），已删 */


/* ==================== 画布视口（v2.9，照搬思维导图模型） ====================
 *   screen = world * k + (vx, vy)
 *   drag:   vx += 屏幕位移（不除 k）
 *   wheel:  以鼠标为锚点缩放
 * ======================================================================== */

float treeVX = 0.0f, treeVY = 0.0f, treeVK = 1.0f;

int   treeDragging = 0;
float treeDragMouseX = 0.0f, treeDragMouseY = 0.0f;
float treeDragVX = 0.0f, treeDragVY = 0.0f;

#define TREE_K_MIN 0.40f
#define TREE_K_MAX 2.60f

float BenchNodeWorldX(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 0.0f;
    return 40.0f + (float)benchNodes[id].col * BENCH_NODE_WX;
}

float BenchNodeWorldY(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 0.0f;
    return 40.0f + (float)(id>=16 ? 4 : benchNodes[id].row) * BENCH_NODE_WY;
}

void BenchFocusNode(int id) {
    float cx = (float)winW * 0.5f;
    float cy = (float)winH * 0.46f;
    treeVX = cx - BenchNodeWorldX(id) * treeVK;
    treeVY = cy - BenchNodeWorldY(id) * treeVK;
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
    float left=(float)winW*0.32f, right=(float)winW*0.96f;
    float top=(float)winH*0.18f, bottom=(float)winH*0.72f;
    treeVK=(right-left)/(4.0f*BENCH_NODE_WX);
    float ky=(bottom-top)/(5.0f*BENCH_NODE_WY); if(ky<treeVK) treeVK=ky;
    if(treeVK<0.40f) treeVK=0.40f;
    treeVX=left+BENCH_NODE_WX*treeVK*0.5f-40.0f*treeVK;
    treeVY=top+BENCH_NODE_WY*treeVK*0.5f-40.0f*treeVK;
    uiDirty = 1;
}

void BenchPanBy(float dxScreen, float dyScreen) {
    treeVX += dxScreen; treeVY += dyScreen;
    uiDirty = 1;
}

float BenchMoveMul(void) { return (BenchHas(1)?1.06f:1.0f)*(BenchHas(16)?1.12f:1.0f)*(BenchHas(18)&&sanity<30.0f?1.1f:1.0f); }
float BenchStaminaMul(void) { return (BenchHas(0)?1.15f:1.0f)*(BenchHas(3)?1.30f:1.0f)*(BenchHas(16)?0.85f:1.0f); }
float BenchSprintCostMul(void) { return (BenchHas(2)?0.75f:1.0f)*(BenchHas(3)?0.85f:1.0f); }
float BenchBatteryCapMul(void) { return BenchHas(4)?1.20f:1.0f; }
float BenchLightRangeMul(void) { return (BenchHas(6)?1.25f:1.0f)*(BenchHas(17)?1.40f:1.0f); }
float BenchSanityAdd(void) { return BenchHas(8)?20.0f:0.0f; }
float BenchHealMul(void) { return BenchHas(10)?1.50f:1.0f; }
float BenchKnockExtra(void) { return (BenchHas(13)?1.0f:0.0f)+(BenchHas(14)?1.5f:0.0f); }
float BenchKnockStunMul(void) { return (BenchHas(14)?1.5f:1.0f)*(BenchHas(19)?2.0f:1.0f); }
int BenchKnockCharges(void) { return BenchHas(12)?1:0; }
int BenchShock(void) { return BenchHas(15); }
void BenchRefundRun(void) {
    int refund=0; for(int i=0;i<BENCH_COUNT;++i) if(BenchHas(i)) refund+=benchNodes[i].cost;
    BenchResetRun(); ThreatReset(); WalletAddRun(refund); SaveGame();
}

/*
 * Canity — 局内改造树实现（v2.7）
 */
#include "common.h"
#include "bench.h"
#include "game.h"
#include "entity.h"   /* sanity 在这里声明 */
#include "lang.h"

/* 节点 id = col * BENCH_ROWS + row */
const BenchNode benchNodes[BENCH_COUNT] = {
    /* ===== v3.4（方案 v5 §3.2）**12 → 16 节点，4 列 × 4 层** =====
       价格统一四档：150 / 350 / 700 / 1200
       8 个纯数值（第 1~2 层）+ 8 个机制（第 3~4 层）
       新增第 3 列 THROWING —— 投掷物第一次和成长系统挂钩。 */

    /* ---- 列 0：潜行 STEALTH ---- */
    { "LIGHT STEP",  "轻步",   "Walking noise -35%",             "走路噪音 -35%",
      150, 0, 0, 0 },
    { "QUIET LID",   "静盖",   "Search noise -50%, search 40% faster", "翻箱噪音 -50%，翻箱快 40%",
      350, 0, 1, 0 },
    { "SILENT THROW","静投",   "Thrown items make NO noise",     "投掷物落地不再产生噪音",
      700, 0, 2, 0 },
    { "VANISH",      "消失",   "3s out of sight = invisible until you move", "脱离视线 3 秒隐身，直到你移动",
      1200, 0, 3, 1 },

    /* ---- 列 1：体能 MOBILITY ---- */
    { "ENDURANCE",   "耐力",   "Stamina max +30",                "体力上限 +30",
      150, 1, 0, 0 },
    { "CRANK WALK",  "边走边摇","Crank while walking",           "边走边摇电",
      350, 1, 1, 0 },
    { "SLIDE",       "滑铲",   "Sprint + crouch = slide, free",  "冲刺中按蹲滑铲，不耗体力",
      700, 1, 2, 0 },
    { "SECOND WIND", "二次呼吸","3s of sprint after stamina hits 0", "体力归零后还能再冲 3 秒",
      1200, 1, 3, 1 },

    /* ---- 列 2：投掷 THROWING（全新）---- */
    { "DEEP POCKET", "深口袋", "Throwable capacity +2 each",     "每种投掷物上限 +2",
      150, 2, 0, 0 },
    { "STEADY HAND", "稳手",   "Full charge 0.5s -> 0.32s",      "满蓄力 0.5 秒 → 0.32 秒",
      350, 2, 1, 0 },
    { "SALVAGE",     "回收",   "Pick thrown items back up (5/run)", "投掷物落地可捡回（每局 5 次）",
      700, 2, 2, 0 },
    { "TWIN THROW",  "双投",   "Shift+RMB throws two at once",   "Shift+右键一次扔两个",
      1200, 2, 3, 1 },

    /* ---- 列 3：生存 SURVIVAL ---- */
    { "THICK SKIN",  "厚皮",   "Grab costs -20% sanity",         "被抓扣理智 -20%",
      150, 3, 0, 0 },
    { "SPARE CELL",  "备用电池","Flashlight drain -25%",          "手电耗电 -25%",
      350, 3, 1, 0 },
    { "CATCH BREAK", "挣断",   "Press SPACE to break a grab (2/run)", "被抓时按空格挣脱（每局 2 次）",
      700, 3, 2, 0 },
    { "PLAY DEAD",   "假死",   "Once per run: escape + confuse",  "每局一次：被抓自动逃脱，怪物迷失",
      1200, 3, 3, 1 },
};


unsigned int benchOwned = 0u;
int benchKills = 0;          /* 嗜血：本局击杀计数 */

void BenchResetRun(void) {
    benchOwned = 0u;
    benchKills = 0;
}

int BenchHas(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 0;
    return (benchOwned & (1u << id)) ? 1 : 0;
}

int BenchPrereqOk(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 0;
    int row = benchNodes[id].row;
    if (row == 0) return 1;                       /* 第一层无条件 */
    return BenchHas(id - 1);                      /* 同列上一层 */
}

int BenchCanBuy(int id) {
    if (!BenchPrereqOk(id)) return 0;
    if (BenchHas(id)) return 0;
    return shards >= benchNodes[id].cost;
}

int BenchTryBuy(int id) {
    if (id < 0 || id >= BENCH_COUNT) return 1;
    if (BenchHas(id)) return 2;                   /* 已拥有 */
    if (!BenchPrereqOk(id)) return 3;             /* 前置没买 */
    if (shards < benchNodes[id].cost) return 4;   /* 碎片不够 */
    shards -= benchNodes[id].cost;
    benchOwned |= (1u << id);
    return 0;
}

/* ==================== 效果查询（v3.0 逃生向） ==================== */

/* 潜行 */
/* ===== v3.4：**id 全部重映射**（BENCH_ROWS 3 → 4 导致错位）=====
   节点 id = col * BENCH_ROWS + row。行数一改，id 就全变了。
   下面每个 accessor 后面的注释写明它现在指向哪个节点，便于维护。 */

/* ---- 列 0：潜行 ---- */
float BenchWalkNoiseMul(void)  { return BenchHas(0)  ? 0.65f : 1.0f; }   /* 0 LIGHT STEP  噪音 -35% */
float BenchSearchQuiet(void)   { return BenchHas(1)  ? 0.50f : 1.0f; }   /* 1 QUIET LID   翻箱 -50% */
int   BenchSilentThrow(void)   { return BenchHas(2)  ? 1 : 0; }          /* 2 SILENT THROW 投掷无声 */
int   BenchVanish(void)        { return BenchHas(3); }                   /* 3 VANISH      隐身 */

/* ---- 列 1：体能 ---- */
int   BenchStaminaBonus(void)  { return BenchHas(4)  ? 30 : 0; }         /* 4 ENDURANCE   +30 */
int   BenchCrankWalk(void)     { return BenchHas(5)  ? 1 : 0; }          /* 5 CRANK WALK  边走边摇 */
int   BenchSlide(void)         { return BenchHas(6)  ? 1 : 0; }          /* 6 SLIDE       滑铲 */
int   BenchSecondWind(void)    { return BenchHas(7); }                   /* 7 SECOND WIND 二次呼吸 */

/* ---- 列 2：投掷 ---- */
int   BenchDeepPocket(void)    { return BenchHas(8)  ? 1 : 0; }          /* 8 DEEP POCKET 上限 +2 */
float BenchSteadyHand(void)    { return BenchHas(9)  ? 0.64f : 1.0f; }   /* 9 STEADY HAND 蓄力更快 */
int   BenchSalvage(void)       { return BenchHas(10) ? 1 : 0; }          /* 10 SALVAGE    可捡回 */
int   BenchTwinThrow(void)     { return BenchHas(11) ? 1 : 0; }          /* 11 TWIN THROW 双投 */

/* ---- 列 3：生存 ---- */
float BenchFearDamageMul(void) { return BenchHas(12) ? 0.80f : 1.0f; }   /* 12 THICK SKIN 被抓 -20% 理智 */
float BenchBatterySaver(void)  { return BenchHas(13) ? 0.75f : 1.0f; }   /* 13 SPARE CELL 耗电 -25% */
int   BenchCatchBreak(void)    { return BenchHas(14) ? 1 : 0; }          /* 14 CATCH BREAK 空格挣脱 */
int   BenchPlayDead(void)      { return BenchHas(15); }                  /* 15 PLAY DEAD   假死 */

/* ===== v3.4 退役节点的兼容桩 =====
   HOLD BREATH / BURST / HEARING / INNER EYE / PREMONITION / COMPOSURE
   这 6 个节点已经从局内树移除（方案 v5：感知类与回声弹重叠，
   数值类感知不到）。调用点还在，这里给「无加成」的默认值。 */
float BenchHideStealth(void)   { return 1.0f; }   /* HOLD BREATH 已并入局外树 */
float BenchSprintMul(void)     { return 1.0f; }   /* BURST 已退役 */
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
float BenchDamageMul(void)          { return 1.0f; }
float BenchSanityDrainMul(void)     { return 1.0f; }
float BenchFlashDrainMul(void)      { return 1.0f; }
float BenchBatteryPickupMul(void)   { return 1.0f; }
float BenchLowSpeedAdd(void)        { return 0.0f; }
int   BenchArmorIgnore(void)        { return 0; }
int   BenchDarkSense(void)          { return 0; }
int   BenchExecuteThreshold(void)   { return 0; }
int   BenchMonsterSpeedMul(void)    { return 1.0f; }
float BenchDrainPenalty(void)       { return 1.0f; }
int   BenchAlchemy(void)            { return 0; }


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
    return 40.0f + (float)benchNodes[id].row * BENCH_NODE_WY;
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
    treeVX = 0.0f; treeVY = 0.0f; treeVK = 1.0f;
    uiDirty = 1;
}

void BenchPanBy(float dxScreen, float dyScreen) {
    treeVX += dxScreen; treeVY += dyScreen;
    uiDirty = 1;
}

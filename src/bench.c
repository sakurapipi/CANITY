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
    /* ===== v3.0 逃生试炼重做：12 个节点全部围绕「不被发现」和「逃脱」 =====
       没有一条提升伤害 —— 玩家在这个版本里打不死任何东西。 */

    /* ---- 列 0：潜行 STEALTH ---- */
    { "LIGHT STEP",   "轻步",   "Walking noise -30%",                "走路噪音 -30%",
      120, 0, 0, 0 },
    { "HOLD BREATH",  "屏息",   "Harder to spot while hidden",       "藏身时更难被发现",
      280, 0, 1, 0 },
    { "VANISH",       "消失",   "3s out of sight = fully invisible", "脱离视线 3 秒后完全隐身",
      620, 0, 2, 1 },

    /* ---- 列 1：体能 MOBILITY ---- */
    { "ENDURANCE",    "耐力",   "Stamina max +30",                   "体力上限 +30",
      120, 1, 0, 0 },
    { "BURST",        "爆发",   "Sprint speed +12%",                 "冲刺速度 +12%",
      280, 1, 1, 0 },
    { "SECOND WIND",  "二次呼吸", "3s of sprint after stamina hits 0", "体力归零后还能再冲 3 秒",
      620, 1, 2, 1 },

    /* ---- 列 2：感知 SENSE ---- */
    { "HEARING",      "听觉",   "See noise sources within 20 tiles", "能看到 20 格内的噪音源",
      120, 2, 0, 0 },
    { "INNER EYE",    "心眼",   "Hear monster distance while hidden", "藏身时听到怪物脚步距离",
      280, 2, 1, 0 },
    { "PREMONITION",  "预知",   "Screen edges turn red within 8 tiles", "8 格内屏幕边缘发红",
      620, 2, 2, 1 },

    /* ---- 列 3：生存 SURVIVAL ---- */
    { "THICK SKIN",   "厚皮",   "Fear damage -15%",                  "恐惧伤害 -15%",
      120, 3, 0, 0 },
    { "COMPOSURE",    "镇定",   "Dark fear growth -25%",             "黑暗中恐惧增长 -25%",
      280, 3, 1, 0 },
    { "PLAY DEAD",    "假死",   "Once per run: escape a grab",       "每局一次：被抓住时逃脱",
      620, 3, 2, 1 },
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
float BenchWalkNoiseMul(void)  { return BenchHas(0) ? 0.70f : 1.0f; }   /* 轻步 -30% */
float BenchHideStealth(void)   { return BenchHas(1) ? 0.55f : 1.0f; }   /* 屏息：被发现概率 ×0.55 */
int   BenchVanish(void)        { return BenchHas(2); }                  /* 消失 */

/* 体能 */
int   BenchStaminaBonus(void)  { return BenchHas(3) ? 30 : 0; }         /* 耐力 +30 */
float BenchSprintMul(void)     { return BenchHas(4) ? 1.12f : 1.0f; }   /* 爆发 +12% */
int   BenchSecondWind(void)    { return BenchHas(5); }                  /* 二次呼吸 */

/* 感知 */
int   BenchHearing(void)       { return BenchHas(6) ? 1 : 0; }          /* 听觉 */
int   BenchInnerEye(void)      { return BenchHas(7) ? 1 : 0; }          /* 心眼 */
int   BenchPremonition(void)   { return BenchHas(8) ? 1 : 0; }          /* 预知 */

/* 生存 */
float BenchFearDamageMul(void) { return BenchHas(9)  ? 0.85f : 1.0f; }  /* 厚皮 -15% */
float BenchDarkFearMul(void)   { return BenchHas(10) ? 0.75f : 1.0f; }  /* 镇定 -25% */
int   BenchPlayDead(void)      { return BenchHas(11); }                 /* 假死 */

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

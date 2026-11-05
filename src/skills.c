/*
 * Canity — 技能系统实现（数据表 + 纯逻辑）
 *
 * 数值来源：docs/终极方案_v2.md（冻结版）
 *   普通节点 100 / 500 金币（两级）
 *   分支 keystone 2000 金币 + 1 核心
 *   跨支 keystone 1400 金币 + 2 核心
 *   9×600 + 3×2000 + 3×1400 = 15600 金币，9 核心 —— 约 34 局主线
 */
#include "common.h"
#include "game.h"
#include "skills.h"
#include "upgrade_run.h"
#include "save.h"
#include "wallet.h"

/* ------------------------------------------------------------------ */
/* 局外表                                                            */
/* ------------------------------------------------------------------ */
/* 分支顺序必须与 SK_* 枚举一致（common.h）
 * v2：前置改成**同列链式** —— 上一行没点亮就买不了下一行。
 *     这样技能树里画出来的竖线才有真实含义（不是你随便点，是真的有路）。 */
const SkillDef skillDefs[] = {
    /* ---- 列 0：静（噪声的时与地） ---- */
    {"LIGHT STEP",   "Walking noise reduced 35% per rank",                          0, 3, {1000,2000,3000},       0, -1, 0},   /* v3.0 退役：纯属性，感知不到 */
    {"QUIET LID",    "Searching makes NO noise at max rank", 0, 3, {1000,2000,3000}, 0, 0, 0},
    {"GHOST STEP",   "Silent footsteps while light is off", 0, 3, {1000,2000,3000}, 0, SK_SEARCHNOISE, 0},
    {"DECOY",        "G: throw a stone (8s cd), noise 14", 0, 1, {3500,0,0},  1, SK_GHOSTSTEP, 0},
    /* ---- 列 1：筹（风险与经济） ---- */
    {"SIPHON",       "Crates yield +25% gold per rank",                          1, 3, {1000,2000,3000},       0, -1, 0},   /* v3.0 退役 */
    {"CRANK WALK",   "Crank while walking (move -40%, noise x2)", 1, 3, {1000,2000,3000}, 0, 4, 0},
    {"REST",         "Safe house rest restores +30 sanity",                          1, 3, {1000,2000,3000},       0, SK_CRANK, 0},   /* v3.0 退役 */
    {"METABOLISM",   "Sanity loss burns battery (1:3)",    1, 1, {3500,0,0}, 0, SK_CRANK, 0},
    /* ---- 列 2：隐（藏匿与脱身） ----
       v3.0：整列重做。原来 4 个节点全是伤害（HEAVY HAND / STAGGER /
       TYPE WORK / RELOAD RHYTHM），无战斗后完全无效。
       换成逃生玩法的核心：怎么躲、怎么脱身。 */
    {"BREATH",       "Hunters reach the locker but never open it", 2, 3, {1000,2000,3000}, 0, -1, 0},
    {"STILL FACE",   "Standing still makes you invisible to sound",                          2, 3, {1000,2000,3000},       0, SK_BREATH, 0},   /* v3.0 退役：和 HOLD BREATH 重叠 */
    {"SLIP AWAY",    "Vanish after 2s out of sight (was 3s)", 2, 3, {1000,2000,3000}, 0, SK_BREATH, 0},
    {"PLAY DEAD",    "Once per run: escape a grab",        2, 1, {3500,0,0},  1, SK_SLIP, 0},
    /* ---- 列 3：途（整局形状） ---- */
    {"CARTO", "Reveal safe house + breaker at start", 3, 2, {1500,3000,0}, 0, -1, 0},
    {"BREAKER", "Locked crates open 40% faster",                        3, 3, {1000,2000,3000},       0, SK_CARTO, 0},   /* v3.0 退役 */
    {"FUSE SMITH",   "Fuse need -1 (min 2)",               3, 3, {1000,2000,3000}, 0, SK_CARTO, 0},
    {"SHORTCUT",     "Skip one non-boss level per run",    3, 1, {3500,0,0}, 0, SK_FUSE_LESS, 0},
    /* ---- 第 5 行：跨支 keystone（全局只持有一个） ---- */
    {"SILENT RUN",   "Sprint makes no noise (stamina -30%)", 0, 1, {3000,0,0},  2, 3, 1},
    {"GREED",   "Gold x1.5, gold threat tier +2", 1, 1, {3000,0,0},       0, 7, 1},   /* v3.0 退役 */
    {"SECOND WIND",  "Sprint 3s after empty stamina, healing -50%", 2, 1, {3000,0,0},       0, 11, 1},   /* v3.0 退役：PLAY DEAD 覆盖 */
    {"LIGHTLESS",      "No flashlight; quieter movement (-25%)", 3, 1, {3000,0,0},       0, 15, 1},   /* v3.0 退役 */

    /* ==================================================================
     * v7 第 58 轮：**补回被 git checkout 冲掉的 6 个节点**
     * ==================================================================
     * common.h 的枚举一直有 26 个（含 SK_ORIGIN），SKILL_COUNT = 26，
     * 但 git 回滚把表恢复成只剩 20 条 —— 后 6 条全是零初始化：
     *     skillDefs[20..25].name == NULL
     * 节点循环里 sprintf(nm, "%s", SkillShortName(id)) 会拿到 NULL，
     * 直接访问 0 地址 -> **切到 Hub 必崩**。
     *
     * 这 6 个函数我是按枚举名和分支表重建的，数值取和同层节点一致。
     * ================================================================ */
    {"SILENT DEAL",   "Crates are silent; crate gold +50%",        5, 1, {2500,0,0},    0, SK_BATTERY,    0},
    {"GOLDEN ESCAPE", "Escape bonus +50%",                        5, 1, {2500,0,0},    0, SK_BATTERY,    0},
    {"APPRENTICE",    "Cranking noise -50%, walking penalty halved",    5, 1, {2500,0,0},    0, SK_BREATH,    0},
    {"SHORTCUT JR",   "Fuse need -1 (minimum 2)",5, 1, {2500,0,0},    0, SK_CARTO,     0},
    {"NIGHT WALKER",  "Light on drains 25% slower",               5, 1, {2500,0,0},    0, SK_GHOSTSTEP, 0},
    {"ORIGIN",        "The center. Light this to unlock the net.",5, 1, {0,0,0},       0, -1,           0},
    {"GREEDY HAND", "Gold +20% per rank, THREAT +1 per rank", 4, 3, {1000,1500,2250},0,-1,0},
    {"GOLD SCENT", "Noise -10% per rank while carrying gold", 4, 3, {1000,1500,2250},0,SK_GREEDHAND,0},
    {"GAMBLER", "10% per rank chance for double crate gold", 4, 3, {1000,1500,2250},0,SK_GOLDSCENT,0},
    {"DESPERATE", "Below 30 sanity: move +4% per rank", 4, 3, {1000,1500,2250},0,SK_GAMBLER,0},
};

typedef char skillDefs_count_check[(sizeof(skillDefs)/sizeof(skillDefs[0]) == SKILL_COUNT) ? 1 : -1];

/* ==================================================================
 * v7 第 67 轮：编译期断言，防止技能表再次少一半
 * ==================================================================
 * 上一轮表里只有 20 条而 SKILL_COUNT = 26，后 6 条零初始化、
 * name == NULL，sprintf("%s", NULL) 在运行时崩。
 * C 不会为此报错 —— 但这一行会。
 * ================================================================== */
typedef char SKILL_DEF_COUNT_CHECK[(
    sizeof(skillDefs) / sizeof(skillDefs[0]) == (unsigned)SKILL_COUNT) ? 1 : -1];



/* ---- 技能树网格：4 列 × 5 行 ----
   id 规则：列 c 行 r（r<4）→ c*4 + r；第 5 行 → 16 + c */
/* v6：**和新布局 kSkillPos 严格对齐**
     id 0..15 → 列 c 第 r 行（c = id/4, r = id%4）
     id 16    → SILENT RUN，横跨四列（用列 4 表示，不占网格格）
     id 17    → WEATHERMAN，列 0 第 5 行
     id 18    → LAST RES，  列 1 第 5 行
     id 19    → SALVAGE，   列 2 第 5 行
   原来 16..19 一律当成「第 5 行列 0..3」，导致方向键导航跳错节点。 */
static const unsigned char kNodeCol[SKILL_COUNT] = {
    0,0,0,0,  1,1,1,1,  2,2,2,2,  3,3,3,3,  4, 0,1,2,  4,4,4,4,4,  4, 4,4,4,4
};
static const unsigned char kNodeRow[SKILL_COUNT] = {
    0,1,2,3,  0,1,2,3,  0,1,2,3,  0,1,2,3,  0, 4,4,4,  1,2,3,4,0,  4, 0,1,2,3
};

int SkillNodeCol(int id) {
    if (id < 0 || id >= SKILL_COUNT) return -1;
    return (int)kNodeCol[id];
}

int SkillNodeRow(int id) {
    if (id < 0 || id >= SKILL_COUNT) return -1;
    return (int)kNodeRow[id];
}

int SkillNodeIdAt(int col, int row) {
    if (col < 0 || col >= SKILL_GRID_COLS) return -1;
    if (row < 0 || row >= SKILL_GRID_ROWS) return -1;
    /* v6：第 5 行（row 4）只有列 0/1/2 有节点 → 17 / 18 / 19；列 3 空 */
    if (row == 4) {
        if (col == 0) return 17;        /* WEATHERMAN */
        if (col == 1) return 18;        /* LAST RES   */
        if (col == 2) return 19;        /* SALVAGE    */
        return -1;                      /* 列 3 第 5 行没有节点 */
    }
    return col * 4 + row;               /* 列内：0..15 */
}


int skillRank[SKILL_COUNT] = {0};
int cores = 0;   /* v3.4：**已废弃** —— 货币合并成一个「金币」。
                    变量保留只为存档兼容（永远 0），实际花费走 bankGold。 */
int activeKeystone = -1;

/* ------------------------------------------------------------------ */
/* 查询                                                               */
/* ------------------------------------------------------------------ */
int SkillCost(int id) {
    if (id < 0 || id >= SKILL_COUNT) return -1;
    const SkillDef* d = &skillDefs[id];
    int r = skillRank[id];
    if (r >= d->maxRank) return -1;
    int c = d->cost[r];
    return c > 0 ? c : -1;
}

int SkillBranchPoints(int branch) {
    int n = 0;
    for (int i = 0; i < SKILL_COUNT; i++) {
        const SkillDef* d = &skillDefs[i];
        if (d->branch != branch) continue;
        if (d->isKeystone) continue;      /* keystone 本身不计入 "投入点数" */
        n += skillRank[i];
    }
    return n;
}

int SkillKeystoneUnlocked(int id) {
    if (id < 0 || id >= SKILL_COUNT) return 0;
    const SkillDef* d = &skillDefs[id];
    if (!d->isKeystone) return 1;
    return SkillBranchPoints(d->branch) >= SKILL_POINTS_FOR_KEYSTONE;
}

int SkillOwned(int id) {
    if (id < 0 || id >= SKILL_COUNT) return 0;
    return skillRank[id] > 0;
}

const char* SkillName(int id) {
    if (id < 0 || id >= SKILL_COUNT) return "?";
    return skillDefs[id].name;
}

int SkillCanBuy(int id) {
    if (id < 0 || id >= SKILL_COUNT) return 0;
    const SkillDef* d = &skillDefs[id];
    int cost = SkillCost(id);
    if (cost < 0) return 0;                                  /* 已满 */
    /* v6 **价格要合起来校验** —— 原来「金币够」和「核心折金够」分别判，
       2500 金币买 DECOY(2000 + 1核×1000) 会扣成 -500，
       HUD 显示负数，之后所有购买被拒。 */
    int total = cost + (d->coreCost > 0 ? d->coreCost * GOLD_PER_CORE : 0);
    /* v6 **局外购买扣银行** —— 技能树只能从主菜单进，那时 bankGold 恒为 0
       （BankSettle 末尾清零、只有开局才填），所以原来 20 个节点全都
       NOT ENOUGH GOLD，整条局外成长线是装饰。用 bankGold 当钱包。 */
    { extern int bankGold; if (bankGold < total) return 0; }
    if(id>=20 && id<=24) for(int e=0;e<SkillEdgeCount(id);++e)
        if(!SkillOwned(SkillEdgeNeighbor(id,e))) return 0;
    if (d->prereq >= 0 && skillRank[d->prereq] <= 0) return 0; /* 前置未点 */
    if (d->isKeystone) {
        if (!SkillKeystoneUnlocked(id)) return 0;            /* 分支点数不足 */

    }
    return 1;
}

int SkillBuy(int id) {
    if (!SkillCanBuy(id)) return 0;
    const SkillDef* d = &skillDefs[id];
    int cost = SkillCost(id);
    /* v6：一次性扣总价（从银行扣，见 SkillCanBuy） */
    { extern int bankGold;
      { extern int WalletSpendBank(int);
        if (!WalletSpendBank(cost + (d->coreCost > 0 ? d->coreCost * GOLD_PER_CORE : 0))) return 0; }   /* v7：走钱包，不许直扣 */
      if (bankGold < 0) bankGold = 0; }
    skillRank[id]++;
    if (d->isKeystone) activeKeystone = id;
    return 1;
}

/* 换 keystone：把当前那个卸下来（不退金币），花 3 核心，再照原价买新的。
 * 之前没有这条路 —— 玩家第一次选了 keystone 就永远换不掉，设计上是死的。 */
extern int bankGold;   /* v7：货币统一后技能树花银行金币 */

int SkillSwitchable(int id) {
    return id>=0 && id<SKILL_COUNT && skillDefs[id].isKeystone && SkillOwned(id) && activeKeystone!=id;
}
int SkillSwitchKeystone(int id) {
    if(!SkillSwitchable(id))return 0;
    activeKeystone=id;return 1;
}

/* ------------------------------------------------------------------ */
/* 局外效果换算                                                        */
/* ------------------------------------------------------------------ */
/* ==================================================================
 * v7 第 53 轮：**HasKeystone 恒为 0 —— 8 个节点买了没效果**
 * ==================================================================
 * activeKeystone 只在 isKeystone == 1 时赋值，
 * 而取消「全局只能持有一个 keystone」后整张表 isKeystone 全是 0。
 * → SK_SILENTRUN / SK_SALVAGE / SK_SHORTCUT / SK_LASTRESORT
 *   SK_PLAYDEAD / SK_METAB / SK_DECOY / SK_FUSE_LESS 全部失效，
 *   按卡片价累加约 66,500 金。
 * 现在改成「买过就算」。
 * ================================================================ */
int HasKeystone(int id) { return SkillOwned(id) && (!skillDefs[id].isKeystone || activeKeystone==id); }

/* v3.4 **机制化** —— QUIET LID（静盖）：
   原来 3 级共 -75% 噪音，玩家感知不到。
   现在：1~2 级给 -40%/-70%，**满 3 级直接完全无声**（走路零噪音）。
   从「数值」变成「质变」—— 满级之后你可以贴着怪走。 */
float SkillNoiseMul(void) {
    int r = skillRank[SK_QUIET];
    extern int carryGold;
    float m=1.0f-0.20f*(float)r;
    if(HasKeystone(SK_SALVAGE))m*=0.75f;
    if(carryGold>0) m*=1.0f-0.10f*skillRank[SK_GOLDSCENT];
    return m;   /* 1 级 -40%，2 级 -80% */
}
/* v3.4 **机制化** —— GHOST STEP（鬼步）：
   原来 -30%/级，现在**满级完全无声**。
   「翻箱子不出声」是个真机制：可以从怪背后翻。 */
float SkillSearchNoiseMul(void) {
    int r = skillRank[SK_SEARCHNOISE];
    if (r >= 3) return 0.0f;          /* 满级：翻箱零噪音 */
    return SkillOwned(SK_SILENTDEAL) ? 0.0f : 1.0f - 0.30f*(float)r;
}
float SkillRestAdd(void) {
    return 0.25f * (float)skillRank[SK_REST];
}
float SkillStunAdd(void) {
    return 0.20f * (float)skillRank[SK_STILL];
}

/* ---- v2.8：Hub 的 6 项基础升级折进技能树 ------------------------------
   每个普通节点 3 级，每级同时给「机制」和一项「基础属性」，
   这样属性上限和原来 3 级基础升级完全一致，而玩家只需要看一套界面。 */

/* ==================================================================
 * 2026-11-06（用户要求"技能树的数值也做点调整"）
 * ------------------------------------------------------------------
 * 前提下限从 100 提到 150，所以"每级 +20"的相对价值被稀释了
 * （3 级从 +60% 掉到 +40%）。
 * → 每级 +20 改成 +25：满级 +75/150 = +50%，和原来 +60/100 的
 *   手感接近，但不会让上限虚高。
 * ================================================================ */
int SkillBatteryAdd(void) {                 /* SIPHON    +25/级（原 +20） */
    return 25 * skillRank[SK_BATTERY];
}
float SkillSpeedAdd(void) {
    /* 2026-11-06（用户要求"技能树做点数值微调，机制不要改"）：
       QUITE STEP +2%/级 -> +3%/级；深层还 +4%（濒死加成保持不变）。
       地图变大之后移动距离变长，+2% 几乎感觉不到。 */
    extern float sanity;
    return 0.03f*(float)skillRank[SK_QUIET]+(sanity<30.0f ? 0.04f*skillRank[SK_DESPERATE] : 0.0f);
}
float SkillQuietMul(void) {
    /* 2026-11-06：翻箱噪音 -15%/级 -> -18%/级。
       地图变大后搜刮次数变多，噪音惩罚的绝对量也变大了。 */
    return 1.0f - 0.18f * (float)skillRank[SK_SEARCHNOISE];
}

/* ===== v3.4 新增的三个机制查询（方案 v5 §4.2 局外树）===== */

/* SPARE CELL 备用电池：满级时摇电一次 +55，且摇电时间 -30%
   原来只是「电量上限 +20」，玩家根本感觉不到。 */
int SkillSpareCellMaxed(void) {
    return (skillRank[SK_BATTERY] >= 3) ? 1 : 0;
}
float SkillCrankBonus(void) {
    /* 摇电一次给多少电量：基础 38（2026-11-06：32 -> 38，上限涨到 150 后
       32 只补 1/5，太抠），满级 62。 */
    return SkillSpareCellMaxed() ? 62.0f : 38.0f;
}
float SkillCrankTimeMul(void) {
    /* 摇电所需时间倍率：满级 -30% */
    return SkillSpareCellMaxed() ? 0.70f : 1.0f;
}

/* GHOST STEP 满级：关着手电时脚步声完全消失。
   原来 -12% 耗电，玩家感知不到。 */
int SkillGhostSilent(void) {
    return (skillRank[SK_GHOSTSTEP] >= 3) ? 1 : 0;
}

/* CARTO 制图：满级开局直接标出安全屋 + 配电箱 */
int SkillCartoMaxed(void) {
    return (skillRank[SK_CARTO] >= 2) ? 1 : 0;
}
float SkillEcoMul(void) {
    /* 2026-11-06：省电 -10%/级 -> -12%/级。
       手电上限涨到 150 之后，每 1% 的绝对值也变大了，
       这里只补一点点，避免夜行者那条 -25% 叠加后过于强势。 */
    return (1.0f-0.12f*(float)skillRank[SK_GHOSTSTEP])*(SkillOwned(SK_NIGHTWALKER)?0.75f:1.0f);
}
float SkillCalmMul(void) {                  /* REST       -10%/级 */
    return 1.0f - 0.10f * (float)skillRank[SK_REST];
}

/* 「全满级」成就：keystone 不参与（全局只能持有一个） */
int SkillsAllMaxed(void) {
    for (int i = 0; i < SKILL_COUNT; i++) {
        if (skillDefs[i].isKeystone) continue;
        if (skillRank[i] < skillDefs[i].maxRank) return 0;
    }
    return 1;
}

/* ---- 机制型技能（v2.5 起替换掉原来的纯数值节点） ---- */

/* POWER SIPHON：电量低于 25% 时自动涓流回电，代价是理智流失加快 */
int SkillPwrSiphonAdd(void) {
    if (skillRank[SK_BATTERY] <= 0) return 0;
    return 1 + skillRank[SK_BATTERY];          /* 2 / 3 点每秒 */
}
int SkillPwrSiphonActive(void) {
    return skillRank[SK_BATTERY] > 0;
}
float SkillPwrSiphonSanityMul(void) {
    return skillRank[SK_BATTERY] > 0 ? 1.20f : 1.0f;
}

/* CRANK WALK：可以边移动边摇电；移速惩罚随等级减轻（-20% / -12%） */
float SkillCrankWalkPenalty(void) {
    int r = skillRank[SK_CRANK];
    if (r <= 0) return 0.0f;                    /* 0 = 没这个技能，摇电时不能动 */
    if (r >= 3) return 0.06f;                   /* 3 级几乎不减速 */
    return (r >= 2) ? 0.12f : 0.20f;
}

/* GHOST STEP：关灯时脚步噪声减少 / 完全消失，代价是耗电更快 */
float SkillGhostStepNoiseMul(void) {
    int r = skillRank[SK_GHOSTSTEP];
    if (r <= 0) return 1.0f;
    return (r >= 2) ? 0.0f : 0.5f;              /* 1 级减半，2 级完全无声 */
}
float SkillGhostStepDrainMul(void) {
    return skillRank[SK_GHOSTSTEP] > 0 ? 1.15f : 1.0f;
}

/* HEAVY HAND：满蓄力（>=0.85）额外伤害，代价是蓄力更慢 */
int SkillHeavyHand(void) {
    return skillRank[SK_BREATH];
}
float SkillHeavyHandChargeMul(void) {
    return skillRank[SK_BREATH] > 0 ? 1.40f : 1.0f;
}

/* SCAVENGER：翻箱额外掉一件投掷物，代价是金币收入变少 */
int SkillScavenger(void) {
    return skillRank[SK_SLIP] > 0 ? 1 : 0;
}
float SkillScavengerShardMul(void) {
    int r = skillRank[SK_SLIP];
    if (r <= 0) return 1.0f;
    if (r >= 3) return 1.0f;                    /* 满级没有金币惩罚了 */
    return (r >= 2) ? 0.90f : 0.75f;
}

/* ------------------------------------------------------------------ */
/* 局内技能                                                            */
/* ------------------------------------------------------------------ */
const InRunDef inRunDefs[INRUN_COUNT] = {
    /* 即时（5） */
    {"SPARE CELL", "+45 battery now", IRK_INSTANT, 0, "备用电池", "立即 +45 电量"},
    {"GLASS SUPPLY", "+1 bottle", IRK_INSTANT, 0, "玻璃补给", "立即 +1 瓶子"},
    {"FLARE SUPPLY", "+1 flare", IRK_INSTANT, 0, "照明补给", "立即 +1 照明弹"},
    {"MED KIT", "+25 sanity now", IRK_INSTANT, 0, "急救包", "立即 +25 理智"},
    {"GOLD BAG", "+40 bankGold", IRK_INSTANT, 0, "金币袋", "立即 +40 金币"},
    /* 被动（7，均带轻代价） */
    {"LIGHT FOOT", "Noise -20%, speed -3%", IRK_PASSIVE, 0, "轻步", "噪声 -20%，移速 -3%"},
    {"FAST CRANK", "Crank -25% time, noise +15%", IRK_PASSIVE, 0, "快摇", "摇电时间 -25%，噪声 +15%"},
    {"FAST SEARCH", "Search -25% time, noise +10%", IRK_PASSIVE, 0, "快手", "翻找时间 -25%，噪声 +10%"},
    {"SAVER", "Drain -15%, cap -5", IRK_PASSIVE, 0, "节能", "耗电 -15%，电量上限 -5"},
    {"LONG WIND", "Stamina +25, heal -10%", IRK_PASSIVE, 0, "长气", "体力 +25，治疗 -10%"},
    {"THICK HIDE", "Grab sanity loss -35%", IRK_PASSIVE, 1, "厚皮", "被抓扣理智 -35%"},
    /* 规则（3，稀有） */
    {"SILENT BOTTLE", "Silent breaks, dmg -10%", IRK_RULE, 1, "静音瓶", "瓶子不发声，伤害 -10%"},
    {"DARK WALK", "Off-light +12%, drain +10%", IRK_RULE, 1, "暗行", "关灯移速 +12%，流失 +10%"},
    {"LIGHT FUSES", "Fuse need -1, gold -10%", IRK_RULE, 1, "省保险丝", "所需保险丝 -1，金币 -10%"},
    /* 大招（2，一局一个，第 5 层后进池） */
    {"WHISTLE", "Lure every hunter far away", IRK_ULTIMATE, 1, "哨声", "把所有追捕者引向远处"},
    {"SMOKE OUT", "Break line of sight instantly", IRK_ULTIMATE, 1, "烟幕", "立刻断开所有视线"},

    /* ---- v2.0 新增 8 张（16 -> 24） ---- */
    {"FIELD REPAIR", "+35 battery, -5 sanity", IRK_INSTANT, 0, "现场维修", "+35 电量，-5 理智"},
    {"NERVE SHOT", "+15 sanity, -10 battery", IRK_INSTANT, 0, "镇定剂", "+15 理智，-10 电量"},
    {"PACK RAT", "+2 bottles +30 bankGold", IRK_INSTANT, 0, "囤积者", "+2 瓶子，+30 金币"},
    {"OIL CAN", "+25 battery, +20 stamina", IRK_INSTANT, 0, "油壶", "+25 电量，+20 体力"},
    {"LUCKY FUSE", "+1 fuse held", IRK_INSTANT, 0, "幸运保险丝", "+1 持有保险丝"},
    {"QUIET SOLE", "Walk noise -25%", IRK_PASSIVE, 0, "静底", "走路噪音 -25%"},
    {"COLD BLOOD", "Sanity drain -20%", IRK_PASSIVE, 0, "冷血", "理智流失 -20%"},
    {"BLACKOUT", "All hunters blind for 10s", IRK_ULTIMATE, 1, "全黑", "全场追捕者失明 10 秒"},
};

unsigned int runSkillMask = 0u;
int runSkillCount = 0;
int runUltimate = -1;

void RunSkillsReset(void) {
    runSkillMask = 0u;
    runSkillCount = 0;
    runUltimate = -1;
}

int RunSkillOwned(int id) {
    if (id < 0 || id >= INRUN_COUNT) return 0;
    return (runSkillMask >> id) & 1u;
}

int RunSkillPickable(int id, int level) {
    if (id < 0 || id >= INRUN_COUNT) return 0;
    if (RunSkillOwned(id)) return 0;                        /* 一局每个只能拿一次 */
    const InRunDef* d = &inRunDefs[id];
    if (d->rarity == 1 && level < 5) return 0;              /* 稀有第 5 层后才出 */
    if (d->kind == IRK_ULTIMATE) {
        if (level < 5) return 0;                            /* 大招第 5 层后 */
        if (runUltimate >= 0) return 0;                     /* 一局只能拿一个 */
    }
    return 1;
}

void RunSkillTake(int id) {
    if (id < 0 || id >= INRUN_COUNT) return;
    if (RunSkillOwned(id)) return;
    runSkillMask |= (1u << id);
    runSkillCount++;
    if (inRunDefs[id].kind == IRK_ULTIMATE) runUltimate = id;
}

/* ---- 效果倍率 ---- */
float RunNoiseMul(void) {
    return RunSkillOwned(IR_LIGHTFOOT) ? 0.80f : 1.0f;
}
float RunCrankTimeMul(void) {
    return RunSkillOwned(IR_FASTCRANK) ? 0.75f : 1.0f;
}
float RunSearchTimeMul(void) {
    return RunSkillOwned(IR_FASTSEARCH) ? 0.75f : 1.0f;
}
float RunBatteryDrainMul(void) {
    float m = 1.0f;
    if (RunSkillOwned(IR_SAVER)) m *= 0.85f;
    if (RunSkillOwned(IR_DARKWALK)) m *= 1.10f;
    return m;
}
/* v2.0：新被动 */

/* v2.2：局内技能的**代价**（描述里写了但从来没实现）。
   原来 LIGHT FOOT / IRON SKIN / FAST CRANK / FAST SEARCH / SAVER /
   SILENT BOTTLE 的负面效果全是废纸，等于纯白给。 */
float RunSpeedMul(void) {
    float m = 1.0f;
    if (RunSkillOwned(IR_LIGHTFOOT)) m *= 0.97f;   /* "speed -3%" */
    if (RunSkillOwned(IR_ARMOR))     m *= 0.96f;   /* "speed -4%" */
    return m;
}
float RunCrankNoiseMul(void) {
    return RunSkillOwned(IR_FASTCRANK) ? 1.15f : 1.0f;   /* "noise +15%" */
}
float RunSearchNoiseMul2(void) {
    return RunSkillOwned(IR_FASTSEARCH) ? 1.10f : 1.0f;  /* "noise +10%" */
}
int RunBatteryCapDelta(void) {
    return RunSkillOwned(IR_SAVER) ? -5 : 0;             /* "cap -5" */
}
float RunThrowDmgMul(void) {
    return RunSkillOwned(IR_SILENTBOTTLE) ? 0.90f : 1.0f; /* "dmg -10%" */
}

float RunDamageMul(void) {
    return RunSkillOwned(IR_STEADYHAND) ? 1.20f : 1.0f;
}
float RunCooldownMul(void) {
    return RunSkillOwned(IR_SWIFTRELOAD) ? 0.75f : 1.0f;
}

int RunStaminaAdd(void) {
    return RunSkillOwned(IR_LONGWIND) ? 25 : 0;
}
float RunDamageTakenMul(void) {
    /* v3.0：再乘上局内「角色抗性」强化（每级 -10%） */
    return (RunSkillOwned(IR_ARMOR) ? 0.70f : 1.0f) * RunUpgResistMul();
}
float RunSanityGainMul(void) {
    return RunSkillOwned(IR_LONGWIND) ? 0.90f : 1.0f;
}
int RunBottleSilent(void) {
    return RunSkillOwned(IR_SILENTBOTTLE) ? 1 : 0;
}
float RunDarkSpeedAdd(void) {
    return RunSkillOwned(IR_DARKWALK) ? 0.12f : 0.0f;
}
int RunFuseLess(void) {
    return RunSkillOwned(IR_LIGHTFUSE) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* 自检                                                                */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* v2.0 新节点效果                                                     */
/* ------------------------------------------------------------------ */

/* EXECUTIONER：目标 <25% 血时伤害 +80% */
float SkillExecuteMul(int hp, int maxHp) {
    if (!HasKeystone(SK_SILENTRUN)) return 1.0f;
    if (maxHp <= 0) return 1.0f;
    if (hp * 4 >= maxHp) return 1.0f;          /* 还不到 1/4 血 */
    return 1.8f;
}

/* SALVAGE：死亡时保留 50% 待上缴金币 */
int SkillSalvageKeep(void) {
    return 0;
}

/* CARTOGRAPHER：开局揭示安全屋 / 配电箱 */
int SkillCartoUnlock(void) {
    return skillRank[SK_CARTO] > 0 ? 1 : 0;
}

/* SHORTCUT：每局可以跳过一个非 Boss 层 */
int SkillShortcutAvailable(void) {
    return HasKeystone(SK_SHORTCUT) ? 1 : 0;
}

/* LAST RESORT：理智归零时免死一次 */
int SkillLastResortOneshot(void) {
    return 0;
}

/* TYPE WORK：武器伤害类型（0 = 不启用，1..3 = 砍/钝/金币） */
int SkillTypeWorkRank(void) {
    return skillRank[SK_SLIP];
}

/* RELOAD RHYTHM：每第 3 次命中无视护甲 */
int SkillRhythmActive(int hitCounter) {
    if (!HasKeystone(SK_PLAYDEAD)) return 0;
    return (hitCounter % 3) == 0 ? 1 : 0;
}

int SkillsSelfTest(void) {
    int fail = 0;

    /* 1) 总成本必须等于 SKILL_TOTAL_COST（4 列 20 节点，方案冻结值） */
    int total = 0; int coreTotal = 0;
    for (int i = 0; i < SKILL_COUNT; i++) {
        coreTotal += skillDefs[i].coreCost;
        for (int r = 0; r < skillDefs[i].maxRank; r++) total += skillDefs[i].cost[r];
    }
    if (total != SKILL_TOTAL_COST) fail |= 1;
    if (coreTotal != SKILL_TOTAL_CORES) fail |= 2;

    /* 2) 前置 id 必须合法且成环检测（简单：不能自己依赖自己） */
    for (int i = 0; i < SKILL_COUNT; i++) {
        int p = skillDefs[i].prereq;
        if (p >= SKILL_COUNT || p == i) fail |= 4;
    }

    /* 3) v6：**取消「全局只能持有一个 keystone」后，isKeystone 必须全为 0**。
       原来断言「数量 = 8」，而数据表把「上一节点 id」误写进这一列（值 1..14），
       累加出 81，一旦接线必然失败 —— 这也是它一直没被接线的原因之一。 */
    for (int i = 0; i < SKILL_COUNT; i++) {
        if (skillDefs[i].isKeystone != 0 && skillDefs[i].isKeystone != 1) fail |= 8;
    }

    /* 4) 局内技能：普通 >= 8、稀有 >= 4、大招 2 */
    int rare = 0, ult = 0;
    for (int i = 0; i < INRUN_COUNT; i++) {
        if (inRunDefs[i].rarity == 1) rare++;
        if (inRunDefs[i].kind == IRK_ULTIMATE) ult++;
    }
    if (rare < 4) fail |= 16;
    if (ult != 3) fail |= 32;

    return fail;
}

/* ==================== v3.0 逃生向效果（统一入口） ====================
 * 两个来源会叠加：
 *   ① HUB 技能树（skillRank[]，跨局持久）
 *   ② 局内三选一（runSkillMask，每局重置）
 * 这里把它们 OR 起来，调用方只问一次。
 * ================================================================== */

/* 屏息：藏身时被发现概率（越小越难被发现） */
float HideStealthMul(void) {
    float m = 1.0f;
    if (SkillOwned(SK_BREATH)) m *= 0.55f;      /* 技能树 HOLD BREATH */
    return m;
}

/* 静面：追捕者在藏身点旁放弃速度（越大放弃越快） */
float StillFaceMul(void) {
    if (SkillOwned(SK_STILL)) return 2.0f;      /* 放弃时间减半 */
    return 1.0f;
}

/* 脱身：脱离视线多久后隐身（技能树 SLIP AWAY 从 3 秒降到 2 秒） */
float SlipAwaySeconds(void) {
    return SkillOwned(SK_SLIP) ? 2.0f : 3.0f;
}

/* 假死：技能树或改造树，任一拥有即可 */
int PlayDeadAvailable(void) {
    extern float BenchPlayDead(void);
    if (SkillOwned(SK_PLAYDEAD)) return 1;
    if (BenchPlayDead() > 0.5f) return 1;
    return 0;
}

/* 静音冲刺（keystone SILENT RUN）：冲刺不再产生噪音 */
int SilentRunOwned(void) {
    return HasKeystone(SK_SILENTRUN);
}

/* 静底（局内 QUIET SOLE）：走路噪音 -25% */
float QuietSoleMul(void) {
    return RunSkillOwned(IR_STEADYHAND) ? 0.75f : 1.0f;
}

/* 冷血（局内 COLD BLOOD）：理智流失 -20% */
float ColdBloodMul(void) {
    return RunSkillOwned(IR_SWIFTRELOAD) ? 0.80f : 1.0f;
}

/* 厚皮（局内 THICK HIDE）：被抓扣理智 -35% */
float ThickHideMul(void) {
    return RunSkillOwned(IR_ARMOR) ? 0.65f : 1.0f;
}


/* ==================================================================
 * v7 第 53 轮：**补回 8 个技能树函数**
 * ==================================================================
 * 这几个函数在 git checkout 时被回滚掉了（教训：这个项目有大量
 * 未提交改动，绝对不能用 `git checkout -- <file>` 回滚）。
 *
 * 按调用点的语义重写：
 *   SkillNodeX/Y     归一化坐标（-1..1），UI 乘 SK_WX/SK_WY 得像素
 *   SkillEdgeCount   这个节点有几条出边
 *   SkillEdgeNeighbor 第 i 条出边连到谁
 *   SkillNavNearest  方向键导航（0=上 1=下 2=左 3=右）
 *   SkillShortName   小地图/HUD 用的缩写
 *   SkillNodesLit    已点亮的节点数
 *   SkillInitOnce    一次性初始化
 * ================================================================ */

/* --- 归一化坐标：按分支角度 + 行半径排成放射网 --- */
/* ==================================================================
 * v7 第 57 轮：**归一化坐标必须是 0..1，不是 -1..1**
 * ==================================================================
 * 契约在 ui.c 的 SkillWorldPos：
 *     *wx = SkillNodeX(id) * SK_WX * 6.0f;
 * 注释写明「把 0..1 映射到世界坐标」，而且给了间距要求：
 *     归一化间距 0.10 → 世界 0.60·SK_WX，而节点宽 0.55·SK_WX
 *     → 间距 0.10 才不重叠
 *
 * 我上一版返回 -1..1，半径也只有 0.19 递进，
 * 结果节点飞出画布 / 重叠 / 画线时坐标爆炸 → 切 Hub 直接崩。
 *
 * 这一版严格按契约：
 *   中心 (0.5, 0.5)，5 条枝按 72° 分布
 *   行半径 0.12 / 0.22 / 0.32 / 0.42 / 0.50（间距 0.10）
 *   联动节点（branch 4）半径 0.30，靠中
 * ================================================================ */
static void SkillNodeNorm(int id, float* nx, float* ny) {
    *nx = 0.5f; *ny = 0.5f;
    if (id < 0 || id >= SKILL_COUNT) return;
    if (id == SK_ORIGIN) return;              /* 中心 */

    int branch, row;
    if(id<16) { branch=id/4; row=id%4; }
    else if(id>=26) { branch=4; row=id-26; }
    else if(id>=20 && id<=24) {
        float angle=-1.5707963f+6.2831853f*((float)(id-20)+0.5f)/5.0f;
        *nx=0.5f+cosf(angle)*0.37f; *ny=0.5f+sinf(angle)*0.37f; return;
    } else { branch=id-16; row=4; }
    float angle=-1.5707963f+6.2831853f*(float)branch/5.0f;
    float radius=0.18f+0.17f*(float)row;
    *nx=0.5f+cosf(angle)*radius; *ny=0.5f+sinf(angle)*radius;

}

float SkillNodeX(int id) {
    float nx, ny;
    SkillNodeNorm(id, &nx, &ny);
    return nx;
}



float SkillNodeY(int id) { float nx,ny; SkillNodeNorm(id,&nx,&ny); return ny; }

/* --- 邻接：同枝上一行 → 本节点；本节点 → 同枝下一行 --- */
/* 连线：每个节点连自己的前置（没有前置就连中心） */
int SkillEdgeCount(int id) {
    if (id < 0 || id >= SKILL_COUNT || id==SK_ORIGIN) return 0;
    return id>=20 && id<=24 ? 2 : 1;
}

int SkillEdgeNeighbor(int id, int i) {
    static const int cross[5][2]={{1,4},{6,8},{9,12},{13,27},{2,26}};
    if(id>=20 && id<=24 && i>=0 && i<2) return cross[id-20][i];
    if (id < 0 || id >= SKILL_COUNT) return SK_ORIGIN;
    if (id == SK_ORIGIN)         return SK_ORIGIN;
    {
        int pre = skillDefs[id].prereq;
        if (pre >= 0 && pre < SKILL_COUNT && pre != id) return pre;
    }
    return SK_ORIGIN;
}

/* --- 方向键导航：在候选里挑方向最接近、距离最近的 --- */
int SkillNavNearest(int from, int dir) {
    if (from < 0 || from >= SKILL_COUNT) return from;
    {
        float fx = SkillNodeX(from), fy = SkillNodeY(from);
        float dx = 0.0f, dy = 0.0f;
        int best = -1;
        float bestD = 1e9f, bestDot = -1e9f;
        if (dir == 0) dy = -1.0f;
        else if (dir == 1) dy = 1.0f;
        else if (dir == 2) dx = -1.0f;
        else dx = 1.0f;

        for (int i = 0; i < SKILL_COUNT; i++) {
            if (i == from) continue;
            float vx = SkillNodeX(i) - fx, vy = SkillNodeY(i) - fy;
            float len = sqrtf(vx * vx + vy * vy);
            if (len < 0.01f) continue;
            float dot = (vx * dx + vy * dy) / len;
            if (dot < 0.30f) continue;             /* 不是这个方向 */
            if (dot > bestDot + 0.001f || (dot > bestDot - 0.05f && len < bestD)) {
                bestDot = dot; bestD = len; best = i;
            }
        }
        return (best >= 0) ? best : from;
    }
}

const char* SkillShortName(int id) {
    if (id < 0 || id >= SKILL_COUNT) return "?";
    return skillDefs[id].name;
}

int SkillNodesLit(void) {
    int n = 0;
    for (int i = 0; i < SKILL_COUNT; i++) if (skillRank[i] > 0) n++;
    return n;
}

void SkillInitOnce(void) { skillRank[SK_ORIGIN]=1; }



/* ==================================================================
 * v7 第 63 轮：**SkillResetAll 只剩声明，实现被 git checkout 冲掉了**
 * ==================================================================
 * skills.h 里有原型，skills.c 里没有函数体 —— 谁调谁链接失败，
 * 所以「重置技能树」这件事根本不存在，META RESET 成就也永远拿不到。
 *
 * 语义：**全额退还**。玩家可以免费重洗，用它去试别的构筑。
 * 这是 Meta 系统的标准做法（重洗不该收费，收费只会让人不敢试）。
 * ================================================================ */
void SkillResetAll(void) {
    extern void WalletAddBank(int);
    int refund = 0;
    for (int i = 0; i < SKILL_COUNT; i++) {
        if (i == SK_ORIGIN) { skillRank[i] = 1; continue; }
        for (int r = 0; r < skillRank[i] && r < 3; r++) refund += skillDefs[i].cost[r];
        if (skillRank[i] > 0) refund += skillDefs[i].coreCost * GOLD_PER_CORE * skillRank[i];
        skillRank[i] = 0;
    }
    activeKeystone = -1;
    if (refund > 0) WalletAddBank(refund);
    { extern int metaResetDone; metaResetDone = 1; }
    { extern void AchUnlock(int); AchUnlock(29); }
    SaveGame();
}

float SkillGoldMul(void) {
    float m=1.0f+0.20f*skillRank[SK_GREEDHAND];
    if(SkillOwned(SK_SILENTDEAL)) m*=1.50f;
    if(HasKeystone(SK_WEATHERMAN)) m*=1.50f;
    return m;
}
float SkillCrateGoldMul(void) {
    float m=SkillGoldMul();
    if((rand()%100)<10*skillRank[SK_GAMBLER]) m*=2.0f;
    return m;
}
float SkillExtractMul(void) { return SkillOwned(SK_GOLDENESC) ? 1.5f : 1.0f; }

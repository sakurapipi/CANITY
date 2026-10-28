/*
 * Canity — 技能系统实现（数据表 + 纯逻辑）
 *
 * 数值来源：docs/终极方案_v2.md（冻结版）
 *   普通节点 100 / 500 碎片（两级）
 *   分支 keystone 2000 碎片 + 1 核心
 *   跨支 keystone 1400 碎片 + 2 核心
 *   9×600 + 3×2000 + 3×1400 = 15600 碎片，9 核心 —— 约 34 局主线
 */
#include "common.h"
#include "game.h"
#include "skills.h"

/* ------------------------------------------------------------------ */
/* 局外表                                                            */
/* ------------------------------------------------------------------ */
/* 分支顺序必须与 SK_* 枚举一致（common.h）
 * v2：前置改成**同列链式** —— 上一行没点亮就买不了下一行。
 *     这样技能树里画出来的竖线才有真实含义（不是你随便点，是真的有路）。 */
const SkillDef skillDefs[SKILL_COUNT] = {
    /* ---- 列 0：静（噪声的时与地） ---- */
    {"LIGHT STEP",   "Sprint noise -25%, speed +6%",      0, 3, {100, 500, 900}, 0, -1, 0},
    {"QUIET LID",    "Search noise -30%, noise -15%",     0, 3, {100, 500, 900}, 0, SK_QUIET, 0},
    {"GHOST STEP",   "Dark steps silent, drain -12%",     0, 3, {100, 500, 900}, 0, SK_SEARCHNOISE, 0},
    {"DECOY",        "G: throw a stone (8s cd), noise 14", 0, 1, {2000, 0},  1, SK_GHOSTSTEP, 1},
    /* ---- 列 1：筹（风险与经济） ---- */
    {"SIPHON",       "Low-bat regen, battery +20",         1, 3, {100, 500, 900}, 0, -1, 0},
    {"CRANK WALK",   "Crank while walking, move -20%",     1, 3, {100, 500, 900}, 0, SK_BATTERY, 0},
    {"REST",         "Safe heal +25%, sanity drain -10%",  1, 3, {100, 500, 900}, 0, SK_CRANK, 0},
    {"METABOLISM",   "Sanity loss burns battery (1:3)",    1, 1, {2000, 0},  1, SK_REST, 1},
    /* ---- 列 2：锋（伤害交付方式） ---- */
    {"HEAVY HAND",   "Weapon damage +8%/rank",             2, 3, {250, 900, 0}, 0, -1, 0},
    {"STAGGER",      "Hits stun +0.2s per rank",           2, 3, {250, 900, 0}, 0, SK_DMG, 0},
    {"TYPE WORK",    "Weapon deals your chosen damage type", 2, 3, {250, 900, 0}, 0, SK_STUN, 0},
    {"RELOAD RHYTHM","3rd hit ignores armor",              2, 1, {1800, 0},  1, SK_TYPEWORK, 1},
    /* ---- 列 3：途（整局形状） ---- */
    {"CARTOGRAPHER", "Reveal safe house + breaker at start", 3, 2, {250, 900}, 0, -1, 0},
    {"BREAKER HAND", "Power-on faster, monsters slowed 20s", 3, 2, {250, 900}, 0, SK_CARTO, 0},
    {"FUSE SMITH",   "Fuse need -1 (min 2)",               3, 3, {200, 450, 900}, 0, SK_BREAKER, 0},
    {"SHORTCUT",     "Skip one non-boss level per run",    3, 1, {1800, 0},  1, SK_FUSE_LESS, 1},
    /* ---- 第 5 行：跨支 keystone（全局只持有一个） ---- */
    {"EXECUTIONER",  "<25% HP targets take +80% damage",   4, 1, {1200, 0},  2, -1, 1},
    {"WEATHERMAN",   "Preview next modifier, reroll 150",  4, 1, {1200, 0},  2, -1, 1},
    {"LAST RESORT",  "Sanity 0 saves you once (4s)",       4, 1, {1200, 0},  2, -1, 1},
    {"SALVAGE",      "Keep 50% pending shards on death",   4, 1, {1200, 0},  2, -1, 1},
};

/* ---- 技能树网格：4 列 × 5 行 ----
   id 规则：列 c 行 r（r<4）→ c*4 + r；第 5 行 → 16 + c */
static const unsigned char kNodeCol[SKILL_COUNT] = {
    0,0,0,0,  1,1,1,1,  2,2,2,2,  3,3,3,3,  0,1,2,3
};
static const unsigned char kNodeRow[SKILL_COUNT] = {
    0,1,2,3,  0,1,2,3,  0,1,2,3,  0,1,2,3,  4,4,4,4
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
    if (row == 4) return 16 + col;      /* 跨支 keystone：16..19 */
    return col * 4 + row;               /* 列内：0..15 */
}


int skillRank[SKILL_COUNT] = {0};
int cores = 0;
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
    if (d->branch == 3) {
        /* 跨支：任一分支已经买了 keystone */
        for (int i = 0; i < SKILL_COUNT; i++)
            if (skillDefs[i].isKeystone && skillDefs[i].branch != 3 && skillRank[i] > 0)
                return 1;
        return 0;
    }
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
    if (shards < cost) return 0;                             /* 碎片不够 */
    if (d->coreCost > 0 && cores < d->coreCost) return 0;    /* 核心不够 */
    if (d->prereq >= 0 && skillRank[d->prereq] <= 0) return 0; /* 前置未点 */
    if (d->isKeystone) {
        if (!SkillKeystoneUnlocked(id)) return 0;            /* 分支点数不足 */
        if (activeKeystone >= 0 && activeKeystone != id) {
            /* 已经持有别的 keystone：必须显式切换（花 3 核心），这里不算可买 */
            return 0;
        }
    }
    return 1;
}

int SkillBuy(int id) {
    if (!SkillCanBuy(id)) return 0;
    const SkillDef* d = &skillDefs[id];
    int cost = SkillCost(id);
    shards -= cost;
    if (d->coreCost > 0) cores -= d->coreCost;
    skillRank[id]++;
    if (d->isKeystone) activeKeystone = id;
    return 1;
}

/* 换 keystone：把当前那个卸下来（不退碎片），花 3 核心，再照原价买新的。
 * 之前没有这条路 —— 玩家第一次选了 keystone 就永远换不掉，设计上是死的。 */
int SkillSwitchable(int id) {
    if (id < 0 || id >= SKILL_COUNT) return 0;
    const SkillDef* d = &skillDefs[id];
    if (!d->isKeystone) return 0;
    if (skillRank[id] > 0) return 0;              /* 已经拥有这个，不用换 */
    if (activeKeystone < 0) return 0;             /* 没有可卸的，走正常购买 */
    if (!SkillKeystoneUnlocked(id)) return 0;     /* 门槛没到 */
    if (cores < SKILL_SWITCH_CORE_COST) return 0; /* 换不起 */
    if (shards < d->cost[0]) return 0;
    if (d->coreCost > 0 && cores < d->coreCost + SKILL_SWITCH_CORE_COST) return 0;
    return 1;
}

int SkillSwitchKeystone(int id) {
    if (!SkillSwitchable(id)) return 0;
    cores -= SKILL_SWITCH_CORE_COST;
    if (activeKeystone >= 0) {
        skillRank[activeKeystone] = 0;            /* 卸下旧的，碎片不退 */
        activeKeystone = -1;
    }
    return SkillBuy(id);                          /* 再走一遍正常购买 */
}

/* ------------------------------------------------------------------ */
/* 局外效果换算                                                        */
/* ------------------------------------------------------------------ */
int HasKeystone(int id) { return activeKeystone == id; }

float SkillNoiseMul(void) {
    return 1.0f - 0.25f * (float)skillRank[SK_QUIET];
}
float SkillSearchNoiseMul(void) {
    return 1.0f - 0.30f * (float)skillRank[SK_SEARCHNOISE];
}
float SkillRestAdd(void) {
    return 0.25f * (float)skillRank[SK_REST];
}
float SkillStunAdd(void) {
    return 0.20f * (float)skillRank[SK_STUN];
}

/* ---- v2.8：Hub 的 6 项基础升级折进技能树 ------------------------------
   每个普通节点 3 级，每级同时给「机制」和一项「基础属性」，
   这样属性上限和原来 3 级基础升级完全一致，而玩家只需要看一套界面。 */

int SkillBatteryAdd(void) {                 /* SIPHON    +20/级 */
    return 20 * skillRank[SK_BATTERY];
}
float SkillSpeedAdd(void) {                 /* LIGHT STEP +6%/级 */
    return 0.06f * (float)skillRank[SK_QUIET];
}
float SkillQuietMul(void) {                 /* QUIET LID  -15%/级 */
    return 1.0f - 0.15f * (float)skillRank[SK_SEARCHNOISE];
}
float SkillEcoMul(void) {                   /* GHOST STEP -12%/级 */
    return 1.0f - 0.12f * (float)skillRank[SK_GHOSTSTEP];
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
    return skillRank[SK_DMG];
}
float SkillHeavyHandChargeMul(void) {
    return skillRank[SK_DMG] > 0 ? 1.40f : 1.0f;
}

/* SCAVENGER：翻箱额外掉一件投掷物，代价是碎片收入变少 */
int SkillScavenger(void) {
    return skillRank[SK_TYPEWORK] > 0 ? 1 : 0;
}
float SkillScavengerShardMul(void) {
    int r = skillRank[SK_TYPEWORK];
    if (r <= 0) return 1.0f;
    if (r >= 3) return 1.0f;                    /* 满级没有碎片惩罚了 */
    return (r >= 2) ? 0.90f : 0.75f;
}

/* ------------------------------------------------------------------ */
/* 局内技能                                                            */
/* ------------------------------------------------------------------ */
const InRunDef inRunDefs[INRUN_COUNT] = {
    /* 即时（5） */
    {"SPARE CELL",    "+45 battery now",              IRK_INSTANT, 0},
    {"GLASS SUPPLY",  "+1 bottle",                    IRK_INSTANT, 0},
    {"FLARE SUPPLY",  "+1 flare",                     IRK_INSTANT, 0},
    {"MED KIT",       "+25 sanity now",               IRK_INSTANT, 0},
    {"SHARD BAG",     "+40 shards",                   IRK_INSTANT, 0},
    /* 被动（7，均带轻代价） */
    {"LIGHT FOOT",    "Noise -20%, speed -3%",        IRK_PASSIVE, 0},
    {"FAST CRANK",    "Crank -25% time, noise +15%",  IRK_PASSIVE, 0},
    {"FAST SEARCH",   "Search -25% time, noise +10%", IRK_PASSIVE, 0},
    {"SAVER",         "Drain -15%, cap -5",           IRK_PASSIVE, 0},
    {"LONG WIND",     "Stamina +25, heal -10%",       IRK_PASSIVE, 0},
    {"IRON SKIN",     "Damage -30%, speed -4%",       IRK_PASSIVE, 1},
    /* 规则（3，稀有） */
    {"SILENT BOTTLE", "Silent breaks, dmg -10%",      IRK_RULE, 1},
    {"DARK WALK",     "Off-light +12%, drain +10%",   IRK_RULE, 1},
    {"LIGHT FUSES",   "Fuse need -1, shards -10%",    IRK_RULE, 1},
    /* 大招（2，一局一个，第 5 层后进池） */
    {"STATIC FIELD",  "Stun all monsters 6s",         IRK_ULTIMATE, 1},
    {"MISDIRECT",     "Teleport nearest monster away", IRK_ULTIMATE, 1},

    /* ---- v2.0 新增 8 张（16 -> 24） ---- */
    {"FIELD REPAIR",  "+35 battery, -5 sanity",        IRK_INSTANT, 0},
    {"NERVE SHOT",    "+15 sanity, -10 battery",       IRK_INSTANT, 0},
    {"PACK RAT",      "+2 bottles +30 shards",         IRK_INSTANT, 0},
    {"OIL CAN",       "+25 battery, +20 stamina",      IRK_INSTANT, 0},
    {"LUCKY FUSE",    "+1 fuse held",                  IRK_INSTANT, 0},
    {"STEADY HAND",   "Weapon damage +20%",            IRK_PASSIVE, 0},
    {"SWIFT RELOAD",  "Weapon cooldown -25%",          IRK_PASSIVE, 0},
    {"TIME STOP",     "Freeze monsters 8s",            IRK_ULTIMATE, 1},
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
    return RunSkillOwned(IR_ARMOR) ? 0.70f : 1.0f;
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
    if (!HasKeystone(SK_EXECUTE)) return 1.0f;
    if (maxHp <= 0) return 1.0f;
    if (hp * 4 >= maxHp) return 1.0f;          /* 还不到 1/4 血 */
    return 1.8f;
}

/* SALVAGE：死亡时保留 50% 待上缴碎片 */
int SkillSalvageKeep(void) {
    return HasKeystone(SK_SALVAGE) ? 50 : 0;
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
    return HasKeystone(SK_LASTRESORT) ? 1 : 0;
}

/* TYPE WORK：武器伤害类型（0 = 不启用，1..3 = 砍/钝/碎片） */
int SkillTypeWorkRank(void) {
    return skillRank[SK_TYPEWORK];
}

/* RELOAD RHYTHM：每第 3 次命中无视护甲 */
int SkillRhythmActive(int hitCounter) {
    if (!HasKeystone(SK_RHYTHM)) return 0;
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

    /* 3) keystone 数量必须是 8（4 分支 + 4 跨支） */
    int ks = 0;
    for (int i = 0; i < SKILL_COUNT; i++) ks += skillDefs[i].isKeystone;
    if (ks != KEYSTONE_COUNT) fail |= 8;

    /* 4) 局内技能：普通 >= 8、稀有 >= 4、大招 2 */
    int rare = 0, ult = 0;
    for (int i = 0; i < INRUN_COUNT; i++) {
        if (inRunDefs[i].rarity == 1) rare++;
        if (inRunDefs[i].kind == IRK_ULTIMATE) ult++;
    }
    if (rare < 4) fail |= 16;
    if (ult != 2) fail |= 32;

    return fail;
}

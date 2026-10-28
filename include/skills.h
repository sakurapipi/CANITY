/*
 * Canity — 技能系统（局外技能树 + 局内技能池）
 *
 * 设计依据：docs/终极方案_v2.md
 *   · 局外 15 项：9 个两级节点 + 3 个分支 keystone + 3 个跨支 keystone
 *   · keystone 全局只能持有 1 个，切换花 3 核心
 *   · 解锁条件：对应分支累计投入 >= 6 点
 *   · 局内 16 项：每通关 2 层三选一，大招第 5 层后进池且一局只能拿一个
 *
 * 这个文件只放"数据 + 查询函数"，不碰 UI、不碰渲染，方便自检与调参。
 */
#ifndef CANITY_SKILLS_H
#define CANITY_SKILLS_H

#include "common.h"

/* ------------------------------------------------------------------ */
/* 局外技能树                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    const char* name;      /* 显示名（英文，和游戏其它 UI 一致） */
    const char* desc;      /* 一行说明 */
    int branch;            /* 0=A 潜行 1=B 续航 2=C 反击 3=跨支 */
    int maxRank;           /* 3 = 普通节点，1 = keystone */
    int cost[3];           /* 每级花费碎片；keystone 只用 cost[0] */
    int coreCost;          /* 购买需要多少核心（0 = 不需要） */
    int prereq;            /* 前置技能 id，-1 = 无 */
    int isKeystone;        /* 1 = keystone（全局只能持有 1 个） */
} SkillDef;

extern const SkillDef skillDefs[SKILL_COUNT];
extern int skillRank[SKILL_COUNT];
extern int cores;              /* 核心货币 */
extern int activeKeystone;     /* 当前持有的 keystone id，-1 = 无 */

/* 技能树网格坐标（v2 节点式 UI 用）
 *   3 列 × 5 行：行 0..3 = 分支内层（第 4 行是分支 keystone），行 4 = 跨支 keystone
 *   这是一个**填满的矩形**，所以四向导航永远不会落到空位。
 */
#define SKILL_GRID_COLS 4   /* v2.0：3→4 列 */
#define SKILL_GRID_ROWS 5
int SkillNodeCol(int id);                    /* -1 = 非法 id */
int SkillNodeRow(int id);
int SkillNodeIdAt(int col, int row);         /* 越界返回 -1 */

/* 查询 */
int SkillCost(int id);                       /* 下一级花费，-1 = 已满 */
int SkillBranchPoints(int branch);           /* 该分支已投入点数 */
int SkillKeystoneUnlocked(int id);           /* 分支点数是否够 */
int SkillCanBuy(int id);                     /* 是否可以买（点数/前置/核心/keystone 上限） */
int SkillBuy(int id);                        /* 执行购买，返回 1 = 成功 */
int SkillSwitchKeystone(int id);             /* 换 keystone：卸下旧的 + 花 3 核心 + 买新的 */
int SkillSwitchable(int id);                 /* 是否处于"可以花核心换过去"的状态 */
int SkillOwned(int id);                      /* 是否已拥有（>=1 级） */
const char* SkillName(int id);

/* 局外效果查询：把 rank 换算成运行时倍率/加成 */
float SkillNoiseMul(void);        /* 冲刺脚步噪声倍率 */
float SkillSearchNoiseMul(void);  /* 翻箱噪声倍率 */
float SkillRestAdd(void);
/* v2.8：原本 Hub 的 6 项基础升级折进技能树节点，每级给属性 */
int   SkillBatteryAdd(void);    /* SIPHON      +20 电量上限/级 */
float SkillSpeedAdd(void);      /* LIGHT STEP  +6%  移速/级 */
float SkillQuietMul(void);      /* QUIET LID   -15% 噪声/级 */
float SkillEcoMul(void);        /* GHOST STEP  -12% 手电耗电/级 */
float SkillCalmMul(void);       /* REST        -10% 理智流失/级 */
int   SkillsAllMaxed(void);     /* 所有普通节点满级（成就用） */         /* 安全屋回智加成 */
float SkillStunAdd(void);         /* 硬直加成 */

/* ---- 机制型技能（v2.5：替换掉原来的纯数值样板节点） ---- */
int   SkillPwrSiphonAdd(void);        /* 低电涓流：每秒回电，0 = 没这技能 */
int   SkillPwrSiphonActive(void);
float SkillPwrSiphonSanityMul(void);  /* 低电期间理智流失倍率 */
float SkillCrankWalkPenalty(void);    /* 边摇边走时的移速惩罚，0 = 不能边摇边走 */
float SkillGhostStepNoiseMul(void);   /* 关灯时脚步噪声倍率（0 = 完全无声） */
float SkillGhostStepDrainMul(void);   /* 该技能带来的额外耗电倍率 */
int   SkillHeavyHand(void);           /* 满蓄力投掷的额外伤害 */
float SkillHeavyHandChargeMul(void);  /* 蓄力变慢倍率 */
int   SkillScavenger(void);           /* 翻箱额外掉投掷物 */
float SkillScavengerShardMul(void);   /* 碎片收入倍率 */

int   HasKeystone(int id);

/* v2.0 新节点效果 */
float SkillExecuteMul(int hp, int maxHp);
int   SkillSalvageKeep(void);
int   SkillCartoUnlock(void);
int   SkillShortcutAvailable(void);
int   SkillLastResortOneshot(void);
int   SkillTypeWorkRank(void);
int   SkillRhythmActive(int hitCounter);            /* 是否持有某个 keystone */

/* ------------------------------------------------------------------ */
/* 局内技能                                                            */
/* ------------------------------------------------------------------ */
enum {
    IRK_INSTANT = 0,   /* 即时补给 */
    IRK_PASSIVE,       /* 被动 */
    IRK_RULE,          /* 规则（稀有） */
    IRK_ULTIMATE       /* 大招（一局一个） */
};

typedef struct {
    const char* name;
    const char* desc;
    int kind;          /* IRK_* */
    int rarity;        /* 0=普通 1=稀有 */
} InRunDef;

extern const InRunDef inRunDefs[INRUN_COUNT];
extern unsigned int runSkillMask;   /* 已获得的局内技能位掩码 */
extern int runSkillCount;
extern int runUltimate;             /* 已拿的大招 id，-1 = 无 */

void RunSkillsReset(void);
int  RunSkillOwned(int id);
int  RunSkillPickable(int id, int level);   /* 该层是否允许出现在候选里 */
void RunSkillTake(int id);

/* 局内技能的效果倍率（没拿就是 1.0 / 0） */
float RunNoiseMul(void);
float RunDamageMul(void);      /* v2.0 STEADY HAND */
float RunCooldownMul(void);   /* v2.0 SWIFT RELOAD */
float RunCrankTimeMul(void);
float RunSearchTimeMul(void);
float RunBatteryDrainMul(void);
int   RunStaminaAdd(void);
float RunDamageTakenMul(void);
float RunSanityGainMul(void);
int   RunBottleSilent(void);
float RunDarkSpeedAdd(void);
int   RunFuseLess(void);

/* 自检：返回 0 = 通过 */
int SkillsSelfTest(void);

#endif /* CANITY_SKILLS_H */

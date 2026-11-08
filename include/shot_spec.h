/* include/shot_spec.h —— 投掷物规格表（数据驱动）
 *
 * 为什么
 *   加一种投掷物，原来要改三处 switch（落地 / 撞墙 / 撞怪）+ 噪音 + 存档夹取。
 *   漏一处就出现"有时候生效"（AD-71 的原型）。
 *   现在把**数值**收进一张表，分派只读表；加一种普通投掷物 = 加一行。
 *
 * 边界（诚实说明）
 *   effect 是枚举而不是函数指针：C 里函数指针表在这个规模下收益小于可读性损失。
 *   所以"加一种全新效果"仍然要加一个 enum + 一个 case；
 *   但"加一种同样是烟雾/胶水/伤害的投掷物"确实只加一行。
 */
#ifndef CANITY_SHOT_SPEC_H
#define CANITY_SHOT_SPEC_H

typedef enum {
    FX_SHARDS = 0,     /* 只碎（默认） */
    FX_SMOKE,          /* 生成烟云 */
    FX_GLUE,           /* 生成胶水区 */
    FX_ECHO_SCAN,      /* 声呐扫描（揭地形 + 标资源） */
    FX_PHASE           /* 交给 phase 域处理 */
} ShotEffect;

typedef struct {
    int         kind;        /* shot[].kind */
    const char* name;        /* 日志/测试用 */
    ShotEffect  effect;      /* 落点效果（落地与撞墙共用同一份） */
    float       noise;       /* 落地噪音半径；0 = 静音 */
    int         noiseScaled; /* 1 = 噪音随蓄力增加（投石） */
    int         dmg;         /* 撞怪基础伤害（0 = 不造成伤害） */
    int         dmgType;     /* DT_* 常量 */
    int         ignoresArmor;
    int         rare;        /* 1 = 库存上限 4，0 = 8 */
    int         pierce;      /* 1 = 穿过怪物继续飞（投石是工具不是武器） */
} ShotSpec;

/* 按 kind 取规格；未知 kind 返回默认行（碎片，静音，不穿透）。 */
const ShotSpec* ShotSpecFor(int kind);

/* 表里有多少行（测试用） */
/* 2026-11-09：登记进内容中枢（幂等） */
void ShotRegister(void);

int ShotSpecCount(void);

/* 第 i 行（测试用；越界返回 0） */
const ShotSpec* ShotSpecAt(int i);

#endif /* CANITY_SHOT_SPEC_H */

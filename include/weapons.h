#ifndef CANITY_WEAPONS_H
#define CANITY_WEAPONS_H

#include "common.h"

/* ------------------------------------------------------------------ */
/* 初始武器（v2.0）                                                    */
/*   三把武器各自烧一条生存资源 —— 这是 v2.0 的骨架：                    */
/*     CRANKWRENCH 近战 → 体力                                         */
/*     FLAREPISTOL 远程 → 电量                                         */
/*     SIGIL       法术 → 理智                                         */
/*   名字用 key，正式版由 lang.c 翻译。                                 */
/* ------------------------------------------------------------------ */

typedef enum {
    WPN_MELEE = 0,      /* CRANKWRENCH 摇把扳手 */
    WPN_RANGED,         /* FLAREPISTOL 信号枪 */
    WPN_SPELL,          /* SIGIL 符印 */
    WPN_COUNT
} WeaponId;

/* 资源类型 */
typedef enum {
    WRES_STAMINA = 0,      /* 注意：不能叫 RES_*，common.h 里 RES_COUNT 是分辨率档位数 */
    WRES_BATTERY,
    WRES_SANITY,
    WRES_ENERGY,      /* v2.3：三把武器统一烧这一条 */
    WRES_COUNT
} WeaponResource;

#define ATTACH_SLOTS 2      /* 每把武器 2 个配件槽：0=核心 1=词条 */

typedef struct {
    const char* nameKey;    /* 本地化 key */
    const char* dispName;   /* 英文显示名 */
    const char* nameZh;     /* 中文名 */
    const char* descKey;
    int   kind;             /* WeaponId（= 表内下标，冗余但好读） */
    int   resource;         /* WeaponResource */
    float cost;             /* 每次攻击消耗 */
    int   damage;           /* 基础伤害 */
    float range;            /* 近战/法术射程（格）；远程用 0 */
    float cooldown;         /* 攻击间隔（秒） */
    float noise;            /* 攻击噪声半径 */
    float speed;            /* 投射物/光束速度（格/秒） */
    int   pierce;           /* 1 = 穿透 */
} WeaponDef;

extern const WeaponDef weaponDefs[WPN_COUNT];

/* 每把武器的运行时状态（存档要存） */
extern int   curWeapon;                         /* 当前使用的武器 */
extern int   weaponRank[WPN_COUNT];             /* 熟练度 0..3 */
extern int   weaponXp[WPN_COUNT];               /* 熟练度经验（击杀/使用累计） */
extern int   weaponAttach[WPN_COUNT][ATTACH_SLOTS];  /* 配件 id，-1 = 空 */
extern float weaponCd;                          /* 攻击冷却剩余 */

/* ------------------------------------------------------------------ */
/* 配件（武器改装）                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    const char* nameKey;    /* 英文显示名 */
    const char* nameZh;     /* 中文名 */
    const char* descKey;
    int slot;               /* 0 = 核心（定位），1 = 词条（行为微调） */
    int rarity;             /* 0 普通 / 1 稀有 / 2 传说 */
    float dmgMul;           /* 伤害倍率（1.0 = 不变） */
    float costMul;          /* 消耗倍率 */
    float rangeAdd;         /* 射程加成（格） */
    float cdMul;            /* 冷却倍率 */
    int   pierce;           /* 加穿透 */
    int   knockback;        /* 加击退 */
    int   silent;           /* 攻击噪声减半 */
} AttachDef;

#define ATTACH_COUNT 10

extern const AttachDef attachDefs[ATTACH_COUNT];

/* 查询 */
const WeaponDef* WeaponGet(int id);
const AttachDef* AttachGet(int id);
int   WeaponResourceAmount(int res);      /* 当前资源量 */
void  WeaponSpend(int res, float amount); /* 扣资源（内部 clamp 到 0） */
float WeaponCostNow(int id);              /* 算上配件后的实际消耗 */
int   WeaponDamageNow(int id, float charge);
int WeaponDamageType(int id);   /* v2.0 TYPE WORK */
float WeaponCdNow(int id);
float WeaponRangeNow(int id);
int   WeaponPierceNow(int id);
int   WeaponKnockNow(int id);
int   WeaponSilentNow(int id);

/* 配件操作 */
int   WeaponAttachAdd(int weapon, int attachId);   /* 返回槽位号，-1 = 满了 */
int   WeaponAttachRemove(int weapon, int slot);    /* 返回被拆下的 id */
int   WeaponAttachAt(int weapon, int slot);
void  WeaponAttachClear(int weapon);

/* 熟练度 */
void  WeaponAddXp(int id, int amount);
int   WeaponRankOf(int id);

/* 重置（开新局） */
void  WeaponResetRun(void);

#endif

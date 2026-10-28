#include "weapons.h"
#include "game.h"
#include "entity.h"
#include "skills.h"   /* posX/battery/stamina/sanity 在这里 */

/* ------------------------------------------------------------------ */
/* 武器数据表                                                          */
/* ------------------------------------------------------------------ */

const WeaponDef weaponDefs[WPN_COUNT] = {
    /* nameKey        descKey         kind         res          cost  dmg  range  cd    noise speed pierce */
    { "WPN_WRENCH", "CRANKWRENCH",  "摇把扳手", "WPN_WRENCH_D",  WPN_MELEE,  WRES_STAMINA, 12.0f, 36, 2.4f, 0.55f, 9.0f,  0.0f, 0 },
    { "WPN_FLARE",  "FLARE PISTOL", "信号枪",   "WPN_FLARE_D",   WPN_RANGED, WRES_BATTERY,  4.0f, 32, 0.0f, 0.85f, 12.0f, 15.0f, 0 },
    { "WPN_SIGIL",  "SIGIL",        "符印",     "WPN_SIGIL_D",   WPN_SPELL,  WRES_SANITY,   2.0f, 44, 12.0f, 1.05f, 4.0f,  22.0f, 1 },
};

/* ------------------------------------------------------------------ */
/* 配件表（10 个：5 核心 + 5 词条）                                     */
/* ------------------------------------------------------------------ */

const AttachDef attachDefs[ATTACH_COUNT] = {
    /* 核心（决定定位） */
    { "HEAVY",  "重击", "ATT_HEAVY_D",  0, 0, 1.35f, 1.25f, 0.0f, 1.15f, 0, 0, 0 },
    { "QUICK",  "速击", "ATT_QUICK_D",  0, 0, 0.80f, 0.85f, 0.0f, 0.65f, 0, 0, 0 },
    { "LONG",   "加长", "ATT_LONG_D",   0, 1, 1.00f, 1.10f, 1.5f, 1.00f, 0, 0, 0 },
    { "THRIFT", "节俭", "ATT_THRIFT_D", 0, 1, 0.85f, 0.55f, 0.0f, 1.00f, 0, 0, 0 },
    { "RUNE",   "铭文", "ATT_RUNE_D",   0, 2, 1.50f, 1.40f, 1.0f, 1.30f, 1, 0, 0 },

    /* 词条（微调行为） */
    { "PIERCE", "穿透", "ATT_PIERCE_D", 1, 1, 1.00f, 1.00f, 1.0f, 1.00f, 1, 0, 0 },
    { "KNOCK",  "击退", "ATT_KNOCK_D",  1, 1, 1.00f, 1.00f, 0.0f, 1.00f, 0, 1, 0 },
    { "SILENT", "静音", "ATT_SILENT_D", 1, 1, 0.95f, 1.00f, 0.0f, 1.00f, 0, 0, 1 },
    { "CHAIN",  "连锁", "ATT_CHAIN_D",  1, 2, 1.10f, 1.15f, 0.0f, 1.00f, 1, 0, 0 },
    { "VAMP",   "吸血", "ATT_VAMP_D",   1, 2, 0.90f, 0.80f, 0.0f, 0.95f, 0, 0, 0 },
};

/* ------------------------------------------------------------------ */
/* 运行时状态                                                          */
/* ------------------------------------------------------------------ */

int   curWeapon = WPN_MELEE;
int   weaponRank[WPN_COUNT] = {0, 0, 0};
int   weaponXp[WPN_COUNT] = {0, 0, 0};
int   weaponAttach[WPN_COUNT][ATTACH_SLOTS] = {
    {-1, -1}, {-1, -1}, {-1, -1}
};
float weaponCd = 0.0f;

/* ------------------------------------------------------------------ */
/* 查询                                                                */
/* ------------------------------------------------------------------ */

const WeaponDef* WeaponGet(int id) {
    if (id < 0 || id >= WPN_COUNT) return &weaponDefs[0];
    return &weaponDefs[id];
}

const AttachDef* AttachGet(int id) {
    if (id < 0 || id >= ATTACH_COUNT) return NULL;
    return &attachDefs[id];
}

int WeaponAttachAt(int weapon, int slot) {
    if (weapon < 0 || weapon >= WPN_COUNT) return -1;
    if (slot < 0 || slot >= ATTACH_SLOTS) return -1;
    return weaponAttach[weapon][slot];
}

/* 当前资源量：0=体力 1=电量 2=理智 */
int WeaponResourceAmount(int res) {
    switch (res) {
    case WRES_STAMINA: return (int)stamina;
    case WRES_BATTERY: return (int)battery;
    case WRES_SANITY:  return (int)sanity;
    default:          return 0;
    }
}

void WeaponSpend(int res, float amount) {
    if (amount <= 0.0f) return;
    switch (res) {
    case WRES_STAMINA:
        stamina -= amount;
        if (stamina < 0.0f) stamina = 0.0f;
        break;
    case WRES_BATTERY:
        battery -= amount;
        if (battery < 0.0f) battery = 0.0f;
        if (battery <= 0.0f) flashlightOn = 0;
        break;
    case WRES_SANITY:
        sanity -= amount;
        if (sanity < 0.0f) sanity = 0.0f;
        break;
    default: break;
    }
}

/* 配件带来的乘算（同一把武器的两个槽都算） */
static void attach_mul(int id, float* dmg, float* cost, float* range, float* cd,
                       int* pierce, int* knock, int* silent) {
    float d = 1.0f, c = 1.0f, r = 0.0f, k = 1.0f;
    int pi = 0, kn = 0, si = 0;
    for (int s = 0; s < ATTACH_SLOTS; s++) {
        int a = WeaponAttachAt(id, s);
        if (a < 0) continue;
        const AttachDef* ad = &attachDefs[a];
        d *= ad->dmgMul;
        c *= ad->costMul;
        r += ad->rangeAdd;
        k *= ad->cdMul;
        if (ad->pierce) pi = 1;
        if (ad->knockback) kn = 1;
        if (ad->silent) si = 1;
    }
    *dmg = d; *cost = c; *range = r; *cd = k;
    *pierce = pi; *knock = kn; *silent = si;
}

float WeaponCostNow(int id) {
    const WeaponDef* w = WeaponGet(id);
    float d, c, r, k; int p, kn, si;
    attach_mul(id, &d, &c, &r, &k, &p, &kn, &si);
    return w->cost * c;
}

int WeaponDamageNow(int id, float charge) {
    const WeaponDef* w = WeaponGet(id);
    float d, c, r, k; int p, kn, si;
    attach_mul(id, &d, &c, &r, &k, &p, &kn, &si);
    float base = (float)w->damage * (0.7f + 0.6f * charge);   /* 蓄力 70% ~ 130% */
    /* 熟练度：每级 +8% 伤害 */
    base *= (1.0f + 0.08f * (float)WeaponRankOf(id));
    base *= RunDamageMul();          /* v2.0 STEADY HAND */
    base *= (1.0f + 0.08f * (float)SkillHeavyHand());   /* v2.0：HEAVY HAND 现在对武器生效 */
    int v = (int)(base * d + 0.5f);
    if (v < 1) v = 1;
    return v;
}

float WeaponCdNow(int id) {
    const WeaponDef* w = WeaponGet(id);
    float d, c, r, k; int p, kn, si;
    attach_mul(id, &d, &c, &r, &k, &p, &kn, &si);
    float v = w->cooldown * k * RunCooldownMul();   /* v2.0 SWIFT RELOAD */
    if (v < 0.15f) v = 0.15f;
    return v;
}

float WeaponRangeNow(int id) {
    const WeaponDef* w = WeaponGet(id);
    float d, c, r, k; int p, kn, si;
    attach_mul(id, &d, &c, &r, &k, &p, &kn, &si);
    return w->range + r;
}

int WeaponPierceNow(int id) {
    float d, c, r, k; int p, kn, si;
    attach_mul(id, &d, &c, &r, &k, &p, &kn, &si);
    return p || WeaponGet(id)->pierce;
}

int WeaponKnockNow(int id) {
    float d, c, r, k; int p, kn, si;
    attach_mul(id, &d, &c, &r, &k, &p, &kn, &si);
    return kn;
}

int WeaponSilentNow(int id) {
    float d, c, r, k; int p, kn, si;
    attach_mul(id, &d, &c, &r, &k, &p, &kn, &si);
    return si;
}

/* ------------------------------------------------------------------ */
/* 配件操作                                                            */
/* ------------------------------------------------------------------ */

int WeaponAttachAdd(int weapon, int attachId) {
    if (weapon < 0 || weapon >= WPN_COUNT) return -1;
    if (attachId < 0 || attachId >= ATTACH_COUNT) return -1;
    int want = attachDefs[attachId].slot;
    /* 先找对应槽位：核心进 0，词条进 1 */
    int slot = (want == 0) ? 0 : 1;
    weaponAttach[weapon][slot] = attachId;
    return slot;
}

int WeaponAttachRemove(int weapon, int slot) {
    if (weapon < 0 || weapon >= WPN_COUNT) return -1;
    if (slot < 0 || slot >= ATTACH_SLOTS) return -1;
    int old = weaponAttach[weapon][slot];
    weaponAttach[weapon][slot] = -1;
    return old;
}

void WeaponAttachClear(int weapon) {
    if (weapon < 0 || weapon >= WPN_COUNT) return;
    for (int s = 0; s < ATTACH_SLOTS; s++) weaponAttach[weapon][s] = -1;
}

/* ------------------------------------------------------------------ */
/* 熟练度：每 12 点经验升 1 级，最高 3 级                               */
/* ------------------------------------------------------------------ */

int WeaponRankOf(int id) {
    if (id < 0 || id >= WPN_COUNT) return 0;
    return weaponRank[id];
}

void WeaponAddXp(int id, int amount) {
    if (id < 0 || id >= WPN_COUNT || amount <= 0) return;
    if (weaponRank[id] >= 3) return;
    weaponXp[id] += amount;
    while (weaponRank[id] < 3 && weaponXp[id] >= 12) {
        weaponXp[id] -= 12;
        weaponRank[id]++;
    }
}

/* ------------------------------------------------------------------ */
/* 开新局重置（熟练度保留，配件保留 —— 那是局外成长）                    */
/* ------------------------------------------------------------------ */

void WeaponResetRun(void) {
    weaponCd = 0.0f;
}

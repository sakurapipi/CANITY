#include "relics.h"

/* ------------------------------------------------------------------ */
/* 20 个遗物。每个必带一条负面，正面/负面都乘算。                        */
/* 设计原则：单件收益 10~30%，负面 8~25%，叠 3 层后效果显著。            */
/* ------------------------------------------------------------------ */
const RelicDef relicDefs[RELIC_COUNT] = {
    /*  name            中文       描述（英）                      描述（中）                    稀有  伤害  移速  理智  耗电  噪声  碎片  回智  上限 */
    { "CRACKED LENS", "裂镜",      "+25% damage, +15% sanity drain",  "伤害 +25%，理智流失 +15%",   0, 1.25f, 1.00f, 1.15f, 1.00f, 1.00f, 1.00f, 0.0f, 0 },
    { "STEEL NERVE",  "铁胆",      "-20% sanity drain, -10% speed",   "理智流失 -20%，移速 -10%",   0, 1.00f, 0.90f, 0.80f, 1.00f, 1.00f, 1.00f, 0.0f, 0 },
    { "LIGHT FOOT",   "轻足",      "+12% speed, +20% noise",          "移速 +12%，噪声 +20%",       0, 1.00f, 1.12f, 1.00f, 1.00f, 1.20f, 1.00f, 0.0f, 0 },
    { "MUFFLED BOOTS","静音靴",    "-25% noise, -8% speed",           "噪声 -25%，移速 -8%",        0, 1.00f, 0.92f, 1.00f, 1.00f, 0.75f, 1.00f, 0.0f, 0 },
    { "SPARE CELL",   "备用电池",  "+30 max battery, -15% speed",     "电量上限 +30，移速 -15%",    0, 1.00f, 0.85f, 1.00f, 1.00f, 1.00f, 1.00f, 0.0f, 0 },
    { "RUSTED GEAR",  "锈齿轮",    "+20% damage, +15% battery drain", "伤害 +20%，手电耗电 +15%",   0, 1.20f, 1.00f, 1.00f, 1.15f, 1.00f, 1.00f, 0.0f, 0 },
    { "SHARD MAGNET", "碎片磁石",  "+30% shards, -10% damage",        "碎片 +30%，伤害 -10%",       0, 0.90f, 1.00f, 1.00f, 1.00f, 1.00f, 1.30f, 0.0f, 0 },
    { "GLASS HEART",  "玻璃心",    "+15% speed, +20% sanity drain",   "移速 +15%，理智流失 +20%",   0, 1.00f, 1.15f, 1.20f, 1.00f, 1.00f, 1.00f, 0.0f, 0 },
    { "IRON LUNG",    "铁肺",      "+50% safe-room heal, -15% damage", "安全屋回智 +50%，伤害 -15%", 0, 0.85f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.5f, 0 },
    { "QUIET MIND",   "静心",      "-30% sanity drain, -20% shards",  "理智流失 -30%，碎片 -20%",   0, 1.00f, 1.00f, 0.70f, 1.00f, 1.00f, 0.80f, 0.0f, 0 },
    { "HUNTER'S EYE", "猎眼",      "+18% damage, +10% noise",         "伤害 +18%，噪声 +10%",       0, 1.18f, 1.00f, 1.00f, 1.00f, 1.10f, 1.00f, 0.0f, 0 },
    { "THICK SOLE",   "厚鞋底",    "-18% noise, -6% speed",           "噪声 -18%，移速 -6%",        0, 1.00f, 0.94f, 1.00f, 1.00f, 0.82f, 1.00f, 0.0f, 0 },
    { "COPPER WIRE",  "铜线",      "-20% battery drain, -10% shards", "手电耗电 -20%，碎片 -10%",   0, 1.00f, 1.00f, 1.00f, 0.80f, 1.00f, 0.90f, 0.0f, 0 },
    { "WAR PAINT",    "战纹",      "+30% damage, -20% max sanity",    "伤害 +30%，理智上限 -20",    1, 1.30f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.0f, -20 },
    { "SILENT STEP",  "无声步",    "-35% noise, -12% damage",         "噪声 -35%，伤害 -12%",       1, 0.88f, 1.00f, 1.00f, 1.00f, 0.65f, 1.00f, 0.0f, 0 },
    { "BRASS LUNG",   "黄铜肺",    "-30% sanity drain, -10% speed",   "理智流失 -30%，移速 -10%",   1, 1.00f, 0.90f, 0.70f, 1.00f, 1.00f, 1.00f, 0.0f, 0 },
    { "SCOUT BOOTS",  "斥候靴",    "+18% speed, +15% noise",          "移速 +18%，噪声 +15%",       1, 1.00f, 1.18f, 1.00f, 1.00f, 1.15f, 1.00f, 0.0f, 0 },
    { "BLOOD DEBT",   "血债",      "+40% damage, +30% sanity drain",  "伤害 +40%，理智流失 +30%",   2, 1.40f, 1.00f, 1.30f, 1.00f, 1.00f, 1.00f, 0.0f, 0 },
    { "GHOST SHELL",  "幽壳",      "-45% noise, -20% shards",         "噪声 -45%，碎片 -20%",       2, 1.00f, 1.00f, 1.00f, 1.00f, 0.55f, 0.80f, 0.0f, 0 },
    { "LAST LIGHT",   "残火",      "-40% battery drain, +20% sanity drain", "手电耗电 -40%，理智流失 +20%", 2, 1.00f, 1.00f, 1.20f, 0.60f, 1.00f, 1.00f, 0.0f, 0 },
};

int relicOwned[RELIC_COUNT];
int relicTotal = 0;

void RelicResetRun(void) {
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = 0;
    relicTotal = 0;
}

void RelicTake(int id) {
    if (id < 0 || id >= RELIC_COUNT) return;
    relicOwned[id]++;
    relicTotal++;
}

/* 聚合：每件独立乘算（同类叠 3 件就是 3 次方） */
float RelicDamageMul(void) {
    float m = 1.0f;
    for (int i = 0; i < RELIC_COUNT; i++)
        for (int k = 0; k < relicOwned[i]; k++) m *= relicDefs[i].dmgMul;
    if (m < 0.25f) m = 0.25f;
    return m;
}
float RelicSpeedMul(void) {
    float m = 1.0f;
    for (int i = 0; i < RELIC_COUNT; i++)
        for (int k = 0; k < relicOwned[i]; k++) m *= relicDefs[i].speedMul;
    if (m < 0.5f) m = 0.5f;
    return m;
}
float RelicSanityMul(void) {
    float m = 1.0f;
    for (int i = 0; i < RELIC_COUNT; i++)
        for (int k = 0; k < relicOwned[i]; k++) m *= relicDefs[i].sanityMul;
    if (m < 0.2f) m = 0.2f;
    return m;
}
float RelicBatteryMul(void) {
    float m = 1.0f;
    for (int i = 0; i < RELIC_COUNT; i++)
        for (int k = 0; k < relicOwned[i]; k++) m *= relicDefs[i].batteryMul;
    if (m < 0.2f) m = 0.2f;
    return m;
}
float RelicNoiseMul(void) {
    float m = 1.0f;
    for (int i = 0; i < RELIC_COUNT; i++)
        for (int k = 0; k < relicOwned[i]; k++) m *= relicDefs[i].noiseMul;
    if (m < 0.15f) m = 0.15f;
    return m;
}
float RelicShardMul(void) {
    float m = 1.0f;
    for (int i = 0; i < RELIC_COUNT; i++)
        for (int k = 0; k < relicOwned[i]; k++) m *= relicDefs[i].shardMul;
    if (m < 0.2f) m = 0.2f;
    return m;
}
float RelicHealAdd(void) {
    float a = 0.0f;
    for (int i = 0; i < RELIC_COUNT; i++)
        a += relicDefs[i].healAdd * (float)relicOwned[i];
    return a;
}
float RelicMaxSanAdd(void) {
    float a = 0.0f;
    for (int i = 0; i < RELIC_COUNT; i++)
        a += relicDefs[i].maxSanAdd * (float)relicOwned[i];
    return a;
}

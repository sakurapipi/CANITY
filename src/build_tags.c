/*
 * Canity — 流派标签实现（v2.4.1 第 7 轮）
 * 头部注释见 include/build_tags.h。
 */
#include "common.h"
#include "build_tags.h"
#include "skills.h"
#include "relics.h"

extern unsigned int runSkillMask;
extern int relicOwned[RELIC_COUNT];

/* ==================================================================
 * 卡牌标签表（按 IR_* 枚举顺序，24 项）
 * ------------------------------------------------------------------
 * 这是全工程**唯一**一处给卡牌定流派的地方 —— 加卡时只改这里。
 * BUILD_NONE = 通用卡，任何流派都能拿，不计分。
 * ================================================================== */
static const BuildTag kSkillTag[INRUN_COUNT] = {
    /* 0  IR_BATTERYPACK */ BUILD_NONE,
    /* 1  IR_BOTTLE      */ BUILD_GADGET,
    /* 2  IR_FLARE       */ BUILD_GADGET,
    /* 3  IR_MEDKIT      */ BUILD_NONE,
    /* 4  IR_SHARDBAG    */ BUILD_GADGET,
    /* 5  IR_LIGHTFOOT   */ BUILD_SPEED,
    /* 6  IR_FASTCRANK   */ BUILD_SPEED,
    /* 7  IR_FASTSEARCH  */ BUILD_SPEED,
    /* 8  IR_SAVER       */ BUILD_STEALTH,
    /* 9  IR_LONGWIND    */ BUILD_SPEED,
    /* 10 IR_ARMOR       */ BUILD_STEALTH,
    /* 11 IR_SILENTBOTTLE*/ BUILD_STEALTH,
    /* 12 IR_DARKWALK    */ BUILD_STEALTH,
    /* 13 IR_LIGHTFUSE   */ BUILD_STEALTH,
    /* 14 IR_STATICFIELD */ BUILD_NONE,
    /* 15 IR_MISDIRECT    */ BUILD_GADGET,
    /* 16 IR_FIELDREPAIR */ BUILD_GADGET,
    /* 17 IR_NERVESHOT   */ BUILD_NONE,
    /* 18 IR_PACKRAT     */ BUILD_GADGET,
    /* 19 IR_OILCAN      */ BUILD_NONE,
    /* 20 IR_LUCKYFUSE   */ BUILD_NONE,
    /* 21 IR_STEADYHAND  */ BUILD_NONE,
    /* 22 IR_SWIFTRELOAD */ BUILD_SPEED,
    /* 23 IR_TIMESTOP    */ BUILD_STEALTH,
    /* 24 IR_BIOLUM      */ BUILD_GADGET
};

/* ==================================================================
 * 遗物标签表（12 件）
 * ------------------------------------------------------------------
 * 三件纯加速的（轻足 / 玻璃心 / 斥候靴）归速通；
 * 其余以降噪、理智、电量为主，归潜行。
 * 道具流目前没有专属遗物 —— 它的分主要来自卡牌。
 * ================================================================== */
static const BuildTag kRelicTag[RELIC_COUNT] = {
    BUILD_STEALTH,   /* 0  铁胆：理智 -20% */
    BUILD_SPEED,     /* 1  轻足：移速 +12% */
    BUILD_STEALTH,   /* 2  静音靴：噪声 -25% */
    BUILD_STEALTH,   /* 3  备用电池：电量上限 +30 */
    BUILD_SPEED,     /* 4  玻璃心：移速 +15% */
    BUILD_STEALTH,   /* 5  静心：理智 -30% */
    BUILD_STEALTH,   /* 6  厚鞋底：噪声 -18% */
    BUILD_STEALTH,   /* 7  铜线：耗电 -20% */
    BUILD_STEALTH,   /* 8  黄铜肺：理智 -30% */
    BUILD_SPEED,     /* 9  斥候靴：移速 +18% */
    BUILD_STEALTH,   /* 10 幽壳：噪声 -45% */
    BUILD_STEALTH    /* 11 残火：耗电 -40% */
};

/* 加权：卡 2 分、遗物 2 分。通用卡不计分。 */
#define BUILD_WEIGHT_CARD   2
#define BUILD_WEIGHT_RELIC  2
#define BUILD_DOMINANT_MIN  3   /* 至少 3 分才算"成型" */

BuildTag BuildTagOfSkill(int irId) {
    if (irId < 0 || irId >= INRUN_COUNT) return BUILD_NONE;
    return kSkillTag[irId];
}

BuildTag BuildTagOfRelic(int relicId) {
    if (relicId < 0 || relicId >= RELIC_COUNT) return BUILD_NONE;
    return kRelicTag[relicId];
}

int BuildScore(BuildTag t) {
    if (t <= BUILD_NONE || t >= BUILD_COUNT) return 0;
    int score = 0;
    for (int i = 0; i < INRUN_COUNT; i++) {
        if (!((runSkillMask >> i) & 1u)) continue;
        if (kSkillTag[i] == t) score += BUILD_WEIGHT_CARD;
    }
    for (int i = 0; i < RELIC_COUNT; i++) {
        if (relicOwned[i] <= 0) continue;
        if (kRelicTag[i] == t) score += BUILD_WEIGHT_RELIC;
    }
    return score;
}

BuildTag BuildDominant(void) {
    int best = 0, bestScore = 0, tie = 0;
    for (int t = BUILD_GADGET; t < BUILD_COUNT; t++) {
        int s = BuildScore((BuildTag)t);
        if (s > bestScore) { bestScore = s; best = t; tie = 0; }
        else if (s == bestScore && s > 0) { tie = 1; }
    }
    /* 没成型或者并列 → 不表态。宁可说"还没有倾向"，
       也不要因为一分之差把玩家锁进某个流派。 */
    if (bestScore < BUILD_DOMINANT_MIN) return BUILD_NONE;
    if (tie) return BUILD_NONE;
    return (BuildTag)best;
}

const char* BuildName(BuildTag t, int zh) {
    switch (t) {
    case BUILD_GADGET:  return zh ? "道具工匠" : "TINKERER";
    case BUILD_SPEED:   return zh ? "速通突围" : "RUNNER";
    case BUILD_STEALTH: return zh ? "潜行生存" : "GHOST";
    default:            return zh ? "尚未成型" : "UNFORMED";
    }
}

int BuildSynergyCount(void) {
    BuildTag d = BuildDominant();
    if (d == BUILD_NONE) return 0;
    return BuildScore(d) / BUILD_WEIGHT_CARD;
}

void BuildSynergyList(char* out, int cap) {
    if (!out || cap <= 0) return;
    BuildTag d = BuildDominant();
    if (d == BUILD_NONE) {
        snprintf(out, (size_t)cap, "%s", "");
        return;
    }
    /* 只列"已持有且属于该流派"的前三项，够面板一行用 */
    char buf[192];
    int n = 0;
    buf[0] = 0;
    for (int i = 0; i < INRUN_COUNT && n < 3; i++) {
        if (!((runSkillMask >> i) & 1u)) continue;
        if (kSkillTag[i] != d) continue;
        const char* nm = inRunDefs[i].name;
        if (!nm) continue;
        if (n) strncat(buf, " + ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, nm, sizeof(buf) - strlen(buf) - 1);
        n++;
    }
    snprintf(out, (size_t)cap, "%s", buf);
}

void BuildReset(void) {
    /* 标签是静态表，没有运行时状态需要清。
       保留这个函数是为了让"新局 / 换层 / 读档 / 崩塌"四条路径
       有一个统一的调用点，将来加了状态不用改调用方。 */
}

/* ==================================================================
 * 2026-11-06（第 8 轮）：这张卡的效果**是否已经实现**
 * ------------------------------------------------------------------
 * 抽卡会排除未实现的卡 —— 出了拿不到东西的卡比不出更糟。
 * v2.4.1 里 INRUN_COUNT 仍是 24，全部已实现；等第 10 轮加新卡时，
 * 就在这里把"只加了定义、效果还没做"的卡返回 0。
 * ================================================================== */
int BuildCardImplemented(int irId) {
    if (irId < 0 || irId >= INRUN_COUNT) return 0;
    return 1;
}

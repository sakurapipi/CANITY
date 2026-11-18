#include "progression.h"
#include "skills.h"
#include "save.h"
#include "entity.h"
#include "common.h"
#include "tactical_tools.h"
#include "player_api.h"
#include "state.h"
#include "world_api.h"
#include "item_effects.h"
#include "monster_rules.h"
#include "threat_tier.h"
#include "spider.h"
#include "mimic.h"
#include "warden.h"
#include "lang.h"
TacticalState tactical = {.quick = {0, 1, 2, 6}};
static const ToolDef definitions[TOOL_COUNT] = {
    {"DAMPENER", "吸声胶囊",
     "Ground / 10s / radius 3 / footsteps -80%, other new sounds -70%; walls block field.",
     "地面10秒，半径3格，脚步降噪80%、其他新噪音降70%；隔墙无效，旧声源仍保留。", 45, 2},
    {"LIGHT BREAKER", "断光棱镜",
     "Forward mirror shard / hit: blind light beasts 3s / radius 1.5 / 10s resistance.",
     "向前投出镜面冰刺，命中致盲追光怪3秒并取消蓄力；半径1.5格，目标10秒抗性。", 75, 2},
    {"WEB SOLVENT", "解丝溶剂",
     "Clear nearby webs; selected or bound: press throw while webbed (-1.5s, floor 1s).",
     "清除附近蛛网；选中或绑定后，受控时按投掷减时1.5秒，保留1秒底线。", 60, 2},
    {"MAGNETIC TETHER", "磁索钩弹",
     "Direct / stolen loot within 3 / once: split 25% into a real bag.",
     "直用，3格内赃款目标；每次盗窃一次，分离25%为真实钱袋。", 65, 2},
    {"REVEAL DUST", "显形粉尘",
     "One nearby crate / 8s visible dust; does not pacify or reward mimics.",
     "附近一个箱体附尘8秒；不会安全化宝箱怪，也不送奖励。", 45, 3},
    {"TRACE ERASER", "抹迹粉", "Direct / old hide mark within 1.8 / no effect on witnessed hiding.",
     "直用，清除1.8格内旧藏点痕迹；不取消目击或拉柜预警。", 55, 2},
    {"CIRCUIT JAMMER", "断路脉冲弹",
     "Lantern disruption / 1s / cancel windup; never cancel a blockade.",
     "提灯失调1秒，取消脉冲前摇；不结束堵门、不改变供电。", 90, 2}};
const ToolDef *ToolDefinition(int tool) {
    return tool >= 0 && tool < TOOL_COUNT ? &definitions[tool] : NULL;
}
const char *ToolDetail(int tool) {
    const ToolDef *def = ToolDefinition(tool);
    if (!def)
        return "";
    static char lines[TOOL_COUNT][360];
    char *text = lines[tool];
    switch (tool) {
    case TOOL_ACOUSTIC:
        snprintf(
            text, 360,
            Tr("Radius %.1f / %.0fs / footsteps -80%% / new noise -70%%. Walls block the field.",
               "半径%.1f格 / 持续%.0f秒 / 脚步降噪80%% / 新噪声降70%%；不穿墙。"),
            (double)ProgressionToolValue(PG_DAMP_RADIUS),
            (double)ProgressionToolValue(PG_DAMP_TIME));
        break;
    case TOOL_PRISM:
        snprintf(text, 360,
                 Tr("Forward mirror shard: blind %.1fs; guide investigation %.1fs; resistance 10s.",
                    "镜面冰刺：致盲%.1f秒；命中点诱导%.1f秒；抗性10秒。"),
                 (double)ProgressionToolValue(PG_BLIND_TIME),
                 .6 * SkillClampRank(SK_BREAKER, skillRank[SK_BREAKER]));
        break;
    case TOOL_SOLVENT:
        snprintf(text, 360,
                 Tr("Clear local webs; emergency throw removes %.1fs, keeping the escape floor. %s",
                    "清除附近蛛网；受控时按投掷减时%.1f秒，保留原底线。%s"),
                 (double)ProgressionToolValue(PG_SOLVENT_TIME),
                 SkillOwned(SK_NIGHTWALKER)
                     ? Tr("Inventory access unlocked.", "已解锁袖中使用，无需选中。")
                     : Tr("Select or bind first.", "须先选中或绑定。"));
        break;
    case TOOL_TETHER:
        snprintf(text, 360,
                 Tr("Reach %.1f / split 25%% of real stolen loot once per theft; no new money.",
                    "射程%.1f格；每次盗窃最多分离25%%真实赃款，不凭空造钱。"),
                 (double)ProgressionToolValue(PG_TETHER_RANGE));
        break;
    case TOOL_REVEAL:
        snprintf(text, 360,
                 Tr("One crate: dust remains %.0fs; no pacification or free reward.",
                    "一个箱体附尘%.0f秒；不安全化宝箱怪，不额外产出奖励。"),
                 (double)ProgressionToolValue(PG_DUST_TIME));
        break;
    case TOOL_TRACE:
        snprintf(
            text, 360,
            Tr("Erase one old hide trace within %.1f cells; witnessed hiding remains dangerous.",
               "清除%.1f格内一处旧藏点痕迹；不取消目击或拉柜预警。"),
            (double)ProgressionToolValue(PG_TRACE_RANGE));
        break;
    default:
        snprintf(text, 360,
                 Tr("Lantern disruption %.1fs / cancel windup / blockade and power remain.",
                    "提灯失调%.1f秒，打断前摇；不结束堵门，不改变供电。"),
                 (double)ProgressionToolValue(PG_JAM_TIME));
        break;
    }
    return text;
}
int InventoryCount(int item) {
    switch (item) {
    case 0:
        return flares;
    case 1:
        return bottles;
    case 2:
        return smokeBombs;
    case 3:
        return gluePots;
    case 4:
        return echoBolts;
    case 5:
        return decoys;
    case 6:
        return phaseBeacons;
    default:
        return item >= 7 && item < 14 ? tactical.inventory[item - 7] : 0;
    }
}
void InventoryCounts(int *out) {
    for (int i = 0; i < ITEM_SLOT_COUNT; i++)
        out[i] = InventoryCount(i);
}
const char *InventoryName(int item) {
    static const char *en[] = {"FLARE", "SLING", "SMOKE", "GLUE", "ECHO", "DECOY", "BEACON"};
    static const char *zh[] = {"光球", "投石", "烟雾", "黏胶", "回声", "诱饵", "相位信标"};
    if (item >= 0 && item < 7)
        return Tr(en[item], zh[item]);
    const ToolDef *t = ToolDefinition(item - 7);
    return t ? Tr(t->en, t->zh) : "?";
}
int ToolCapacity(int tool) {
    const ToolDef *t = ToolDefinition(tool);
    return t ? (BottleCap() < t->cap ? BottleCap() : t->cap) : 0;
}
int ToolReceive(int tool) {
    if (tool < 0 || tool >= TOOL_COUNT || tactical.inventory[tool] >= ToolCapacity(tool))
        return 0;
    tactical.inventory[tool]++;
    return 1;
}
int ToolCanOffer(int tool) {
    if (!SaveEvolutionCanPersist())
        return 0;
    switch (tool) {
    case TOOL_ACOUSTIC:
        return monHunters[0].active || monHunters[1].active || monHunters[2].active ||
               MonsterEligible(SPAWN_HUNTER, currentLevel);
    case TOOL_PRISM:
        return lightBeasts[0].active || lightBeasts[1].active ||
               MonsterEligible(SPAWN_LIGHT, currentLevel);
    case TOOL_SOLVENT:
        return SpiderActive() ||
               MonsterEligible(SPAWN_SPIDER, currentLevel);
    case TOOL_TETHER:
        return CollectorActive() ||
               MonsterEligible(SPAWN_COLLECTOR, currentLevel);
    case TOOL_REVEAL:
        for (int i = 0; i < MIM_MAX; i++)
            if (mimics[i].state == MIM_DISGUISED)
                return 1;
        return MonsterEligible(SPAWN_MIMIC, currentLevel);
    case TOOL_TRACE:
        return MonsterTierAtLeast(1) &&
               (ScavengerActive() || MonsterEligible(SPAWN_SCAVENGER, currentLevel));
    case TOOL_JAMMER:
        return WardenActive() ||
               MonsterEligible(SPAWN_WARDEN, currentLevel);
    default:
        return 0;
    }
}
void TacticalResetRun(void) {
    TacticalResetJammer();
    memset(&tactical, 0, sizeof(tactical));
    tactical.quick[0] = 0;
    tactical.quick[1] = 1;
    tactical.quick[2] = 2;
    tactical.quick[3] = 6;
    static const int defaultKeys[] = {'J', 'U', 'O', 'L'};
    for (int slot = 0; slot < 4; slot++) {
        int key = defaultKeys[slot];
        for (int action = 0; action < ACT_COUNT; action++)
            if (g_bindings[action] == key)
                key = 0;
        tactical.key[slot] = key;
    }
}
void TacticalResetFloor(void) {
    TacticalResetJammer();
    memset(tactical.acoustic, 0, sizeof(tactical.acoustic));
    memset(tactical.dust, 0, sizeof(tactical.dust));
    memset(&tactical.solvent, 0, sizeof(tactical.solvent));
    tactical.prismFlicker = 0;
}

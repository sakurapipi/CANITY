#include <stdio.h>
#include <string.h>
#include "lang.h"

int g_lang = LANG_EN;

const char* LangName(int id) {
    return (id == LANG_ZH) ? "中文" : "ENGLISH";
}

void LangToggle(void) {
    g_lang = (g_lang + 1) % LANG_COUNT;
}

/* ------------------------------------------------------------------ */
/* 绘制层翻译表                                                         */
/*   在 DrawTextWin / TextWidthWin 里统一替换，调用点一行都不用改。      */
/*   找不到精确条目时按前缀做模式翻译（动态字符串如 "SHARDS 123"）。      */
/* ------------------------------------------------------------------ */
typedef struct { const char* en; const char* zh; } TrEntry;

static const TrEntry kTr[] = {
    /* 标题 */
    { "SETTINGS", "设置" }, { "DISPLAY", "显示" }, { "KEY BINDINGS", "按键绑定" },
    { "GAMEPLAY", "玩法" }, { "HELP", "帮助" }, { "BESTIARY", "图鉴" },
    { "ACHIEVEMENTS", "成就" }, { "SKILL TREE", "技能树" }, { "THE HUB", "大厅" },
    { "PAUSED", "已暂停" }, { "CHOOSE ONE", "三选一" }, { "CHOOSE WEAPON", "选择初始武器" },
    { "BACK TO MENU", "返回主菜单" }, { "Return to Menu", "返回主菜单" },
    { "YOU LOST YOUR MIND", "你失去了理智" }, { "YOU ESCAPED", "你逃出去了" },
    { "RAYCASTER HORROR", "射线投射恐怖游戏" },

    /* 菜单项 */
    { "Back", "返回" }, { "Quit", "退出" }, { "Resume", "继续游戏" },
    { "Main Menu", "主菜单" }, { "Continue", "继续" }, { "New Run", "新游戏" },
    { "Endless", "无尽" }, { "Hub", "大厅" }, { "Training", "训练营" },
    { "Settings", "设置" }, { "Bestiary", "图鉴" }, { "Help", "帮助" },
    { "Achievements", "成就" },

    /* HUD */
    { "LIGHT", "手电" }, { "RUN", "冲刺" }, { "DRAIN", "抽取中" },
    { "CRANKING", "摇电中" }, { "CHARGING THROW", "蓄力投掷" },
    { "SAFE ZONE", "安全区" }, { "POWER ON", "已供电" }, { "PENDING", "待上缴" },
    { "SNEAK!", "偷袭!" }, { "MAP -10/s", "地图 -10/s" }, { "MAP", "地图" },
    { "BLINDED", "致盲" }, { "SLOWED", "减速" }, { "DRAINING", "抽取" },
    { "ACCESS DENIED", "拒绝访问" }, { "SPECIMEN DMG", "木桩伤害" },

    /* 技能树 */
    { "MAXED", "已满级" }, { "MAX", "满" }, { "NOT ENOUGH SHARDS", "碎片不够" },
    { "NOT ENOUGH CORES", "核心不够" }, { "BUY THE NODE ABOVE", "先买上面的节点" },
    { "NEEDS A BRANCH ULT", "需要分支终极" }, { "NEEDS 6 PTS IN BRANCH", "需要分支 6 点" },
    { "ENTER BUY   ARROWS MOVE   ESC BACK", "回车购买   方向键移动   ESC 返回" },
    { "ENTER-SWAP 3 CORES", "回车切换（3 核心）" },
    { "PRESS AGAIN TO REPLACE", "再按一次替换" }, { "MAX 3 RANKS", "每节点 3 级" },
    { "ANOTHER ULT IS ACTIVE", "已有其它终极生效" },
    { "STEALTH", "潜行" }, { "STAMINA", "续航" }, { "COMBAT", "反击" }, { "CROSS", "跨支" },

    /* 通用开关 */
    { "OFF", "关" }, { "ON", "开" }, { "PRESS ANY KEY", "按任意键" },
    { "REBIND", "改绑" }, { "ESC TO CANCEL", "ESC 取消" },
    { "UNKNOWN", "未知" }, { "???", "???" },
    { "UNKNOWN. ENCOUNTER IT IN A RUN TO UNLOCK.", "未知。在游戏里遇到它就会解锁。" },

    /* 投掷物 */
    { "FLARE", "荧光棒" }, { "BOTTLE", "瓶子" }, { "BRICK", "砖头" },
    { "ACID", "酸瓶" }, { "SMOKE", "烟雾弹" },
    { "STAMINA", "体力" }, { "BATTERY", "电量" }, { "SANITY", "理智" },

    /* 主题 */
    { "BRICK", "砖墙" }, { "PRISON", "监狱" }, { "HELL", "地狱" },
    { "WARD", "病栋" }, { "ICE VAULT", "冰窖" }, { "RUST WORKS", "锈厂" },
    { "FLESH CAVE", "血肉洞穴" },
    { "DARK", "黑暗" }, { "HUNTED", "被猎" }, { "CRAMPED", "狭窄" },
    { "LEAKY", "漏水" }, { "RICH", "富饶" }, { "CALM", "平静" },

    /* 帮助 */
    { "SEARCH CONTAINERS FOR FUSES, THEN POWER THE BREAKER.", "翻箱子找保险丝，然后给配电箱供电。" },
    { "BOTTLE: THROW IT AT A MONSTER TO HIT IT.", "瓶子：砸向怪物造成伤害。" },
    { "RUNNING AND SEARCHING MAKE NOISE.", "跑步和翻箱都会发出声音。" },
    { "WALKING IS QUIET, BUT 5S OF IT STARTS TO WHISPER.", "走路是安静的，但连续走 5 秒就会开始有声音。" },
    { "FACE JUMPER: PRESS SPACE TO DODGE IT.", "跳脸虫：按空格闪避。" },
    { "IT IS ON YOUR FACE", "它扑到脸上了" },
    { "IF IT LANDS ON YOUR FACE, PRESS THE KEY.", "如果扑到脸上，按提示的键。" },
    { "MISS BOTH AND YOU LOSE SANITY.", "两次都错过就掉理智。" },
    { "THE THING MOVES FASTER", "它变快了" },
    { "120 SHARDS", "120 碎片" },

    /* 技能树节点 */
    { "LIGHT STEP", "轻步" }, { "QUIET LID", "静盖" }, { "GHOST STEP", "幽步" },
    { "DECOY", "诱饵" }, { "SIPHON", "涓流" }, { "CRANK WALK", "摇走" },
    { "REST", "休息" }, { "METABOLISM", "代谢" }, { "HEAVY HAND", "重手" },
    { "STAGGER", "硬直" }, { "SCAVENGER", "拾荒" }, { "RUST KNIFE", "锈刃" },
    { "LESS FUSES", "省丝" }, { "BOTTLE BACK", "回瓶" }, { "REROLL", "重掷" },
    { "Sprint noise -25%, speed +6%", "冲刺噪声 -25%，移速 +6%" },
    { "Search noise -30%, noise -15%", "翻箱噪声 -30%，全局噪声 -15%" },
    { "Dark steps quiet, drain -12%", "黑暗中脚步变轻，耗电 -12%" },
    { "G: throw a stone, noise r=14", "G：扔石头，噪声半径 14" },
    { "Low-bat regen, battery +20", "低电涓流回电，电量上限 +20" },
    { "Crank while walking, move -20%", "可边移动边摇电，移速 -20%" },
    { "Heal +25%, sanity drain -10%", "安全屋回智 +25%，理智流失 -10%" },
    { "Sanity loss burns battery (1:3)", "理智流失转为耗电（1:3）" },
    { "Full charge +1 dmg, slow charge", "满蓄力 +1 伤害，蓄力变慢" },
    { "Bottle stun +0.2s per rank", "瓶子硬直 +0.2 秒/级" },
    { "Search also drops loot, shards -25%", "翻箱额外掉道具，碎片 -25%" },
    { "Q: sword wave, 60s cd", "Q：发射剑气，冷却 60 秒" },
    { "Fuse need -1 (min 2)", "保险丝需求 -1（最低 2）" },
    { "40% chance to return on hit", "命中后 40% 概率返还" },
    { "Reroll first negative modifier", "重掷前一个负面词条" },
    { "A  STEALTH", "A  潜行" }, { "B  STAMINA", "B  续航" }, { "C  COMBAT", "C  反击" },

    /* ---- v2.0 补完：设置项 / 成就名 ---- */
    { "Resolution", "分辨率" },
    { "Fullscreen", "全屏" },
    { "Frame Limit", "帧率上限" },
    { "Render Scale", "渲染比例" },
    { "Dynamic Res", "动态分辨率" },
    { "Gamma", "亮度" },
    { "Field of View", "视野" },
    { "Language", "语言" },
    { "Save Slot", "存档槽" },
    { "Move Forward", "前进" },
    { "Move Backward", "后退" },
    { "Move Left", "左移" },
    { "Move Right", "右移" },
    { "Sprint", "冲刺" },
    { "Interact", "交互" },
    { "Crank Battery", "手摇发电" },
    { "Flashlight", "手电" },
    { "Toggle Map", "地图" },
    { "Throw", "投掷" },
    { "Cancel Charge", "取消蓄力" },
    { "Select Flare", "选荧光棒" },
    { "Select Bottle", "选瓶子" },
    { "Select Slot 3", "选第三槽" },
    { "Dodge (QTE)", "闪避" },
    { "Pause", "暂停" },
    { "Menu Up", "菜单上" },
    { "Menu Down", "菜单下" },
    { "Menu Left", "菜单左" },
    { "Menu Right", "菜单右" },
    { "Menu Confirm", "确认" },
    { "Menu Cancel", "取消" },
    { "HUD Detail", "HUD 详细度" },
    { "QTE Hint", "QTE 提示" },
    { "Reset to Defaults", "恢复默认" },
    { "FIRST STEPS", "第一步" },
    { "UNSEEN FLOOR", "无人察觉" },
    { "CLOSE CALL", "擦肩而过" },
    { "DECOY RAIN", "诱饵如雨" },
    { "HANDS EMPTY", "空手而归" },
    { "SURVIVOR", "幸存者" },
    { "ESCAPE ARTIST", "逃生专家" },
    { "SHARD HUNTER", "碎片猎手" },
    { "SILENT SHADOW", "无声之影" },
    { "BOTTLE MASTER", "瓶子大师" },
    { "FACE DODGER", "闪避高手" },
    { "NOCTURNAL", "夜行" },
    { "SPEED RUNNER", "速通者" },
    { "DEEP DIVER", "深潜者" },
    { "FUSE FINDER", "寻丝者" },
    { "GENERATOR GURU", "发电专家" },
    { "UPGRADE JUNKIE", "升级狂" },
    { "MAP MASTER", "地图大师" },
    { "PERFECT ESCAPE", "完美逃生" },
    { "TRUE SURVIVOR", "真·幸存者" },
    { "ABYSS WALKER", "深渊行者" },
    { "BOSS BREAKER", "屠魔者" },
    { "ALL FOUR ZONES", "踏遍四区" },
    { "FLAWLESS FLOOR", "无瑕一层" },
    { "NO LIGHT RUN", "摸黑前行" },
    { "PACIFIST FLOOR", "和平主义者" },
    { "SPEED DEMON", "疾风" },
    { "RELIC HOARDER", "遗物囤积者" },
    { "FULL LOADOUT", "满配" },
    { "BESTIARY DONE", "图鉴全开" },
    { "SHARD TYCOON", "碎片大亨" },
    { "CRANK ADDICT", "摇电成瘾" },
    { "KNIFE ONLY", "只用扳手" },
    { "BOTTLE RAIN", "瓶雨" },
    { "NO RELICS", "不用遗物" },
    { "LUCKY BREAK", "险胜" },
    { "OFF BY ONE", "只差一层" },
    { "SILENT FLOOR", "静默一层" },
    { "BLOOD PRICE", "血的代价" },
    { "GHOST RUN", "幽灵之旅" },
    { "LAST BREATH", "最后一息" },
    { "CENTURION", "百人斩" },
    { "TRUE ENDING", "真结局" },

    /* ---- v2.0 补完：图鉴 ---- */
    { "SOUND HUNTER", "寻声鬼" },
    { "HUNTS NOISE. WALKS TO THE LAST SOUND IT HEARD.", "循声而来。会移动到它最后听到声音的位置。" },
    { "SPRINTING / SEARCHING / CRANKING MAKE NOISE. WALKING IS QUIET - BUT AFTER 5S IT WHISPERS.", "跑步 / 翻箱 / 摇电都会发声。走路是安静的 —— 但连续走 5 秒就会开始有声音。" },
    { "LIGHT BEAST", "追光鬼" },
    { "CHASES YOUR FLASHLIGHT. FASTER THAN THE SOUND HUNTER.", "追着你的手电跑，比寻声鬼更快。" },
    { "KILL THE LIGHT. IT LOSES YOU AND WANDERS OFF.", "关灯它就跟丢了，会自己游荡走。" },
    { "BATTERY LEECH", "偷电怪" },
    { "SMALL, SILENT, ONLY VISIBLE WHEN LIT. SIPS BATTERY WITHIN 3.5 TILES.", "又小又安静，只有被照到才看得见。3.5 格内会偷电。" },
    { "SHINE ON IT FOR 0.6s AND IT FLEES. WATCH THE DRAIN WARNING.", "照它 0.6 秒它就跑。注意耗电警告。" },
    { "FACE JUMPER", "跳脸虫" },
    { "LEAPS AT YOUR FACE. TWO WINDOWS: DODGE, THEN PRESS THE SHOWN KEY.", "直接扑到脸上。两次判定：先闪避，再按提示的键。" },
    { "SPACE TO DODGE. MISS BOTH AND YOU LOSE SANITY.", "空格闪避。两次都错过就掉理智。" },
    { "NOISE", "声音" },
    { "LIGHT", "光" },
    { "DRAIN", "抽取" },
    { "QTE", "快速反应" },
    { "SHADE", "影子" },
    { "SPIDER", "盲螨" },
    { "ECHO", "回响" },
    { "BOSS", "首领" },
    { "DARK", "黑暗" },
    { "SPEED", "速度" },
    { "TELEPORT", "闪现" },
    { "ALL", "全部" },
    { "ONLY EXISTS WHILE YOUR LIGHT IS OFF. SLOW, BUT IT NEVER STOPS.", "只在你关灯时存在。很慢，但永不停下。" },
    { "TURN THE LIGHT ON AND IT VANISHES. THEY CAN BE KILLED NOW.", "开灯它就消失。现在它也能被打死了。" },
    { "BLIND AND FAST. DASHES TOWARD THE LAST SOUND, THEN WAITS.", "又瞎又快。朝最后的声音冲刺，然后等待。" },
    { "LOW HP. ONE GOOD HIT ENDS IT. LISTEN FOR THE SKITTER.", "血很少，一击就死。听那阵窸窣。" },
    { "IT DOES NOT WALK. IT BLINKS TO A SPOT NEAR YOU EVERY FEW SECONDS.", "它不走。每隔几秒闪到你附近。" },
    { "STAY MOVING. ITS CONTACT HURTS MORE THAN THE OTHERS.", "保持移动。它碰到你比其它怪更疼。" },
    { "THE ONE THAT WAS WAITING. EIGHT TIMES THE BODY, NO MERCY.", "一直在等的那个。八倍的身躯，不留情面。" },
    { "KILL IT TO POWER THE BREAKER. THERE IS NO WAY AROUND IT.", "杀了它才能通电。没有绕过去的办法。" },

    /* ---- v2.2 补完：成就描述 + 技能树描述 ---- */
    { "Complete level 1", "通过第 1 层" },
    { "Reach level 5", "到达第 5 层" },
    { "Beat the game (reach level 10 exit)", "通关（走进最终出口）" },
    { "Collect 500 shards total", "累计收集 500 碎片" },
    { "Reach level 5 without sprinting", "不冲刺到达第 5 层" },
    { "Kill 10 monsters with bottles", "用瓶子击杀 10 只怪" },
    { "Dodge 5 face jumpers successfully", "成功躲开 5 次跳脸虫" },
    { "Complete a level with flashlight off", "关灯通过一层" },
    { "Complete level 1 in under 2 minutes", "2 分钟内通过第 1 层" },
    { "Reach level 10", "到达第 10 层" },
    { "Collect 5 fuses in a single run", "单局收集 5 根保险丝" },
    { "Crank generator for 60 seconds total", "累计摇电 60 秒" },
    { "Max out all 6 upgrades", "把 6 项基础升级点满" },
    { "Fully explore 3 maps in one run", "单局完整探索 3 张图" },
    { "Beat the game without taking damage", "无伤通关" },
    { "Beat the game on level 10 with all upgrades maxed", "满级满强化通关" },
    { "Reach level 12", "到达第 12 层" },
    { "Reach level 18", "到达第 18 层" },
    { "Kill a boss", "击杀一只首领" },
    { "Clear all four zones in one run", "单局踏遍四个区段" },
    { "Clear a floor without taking damage", "无伤通过一层" },
    { "Clear 3 floors with the light off", "关灯通过 3 层" },
    { "Clear a floor without killing anything", "一层里不杀任何东西" },
    { "Clear a floor in under 90 seconds", "90 秒内通过一层" },
    { "Hold 8 relics at once", "同时持有 8 个遗物" },
    { "Fill both weapon slots", "两个武器槽都装上配件" },
    { "Unlock every bestiary entry", "解锁全部图鉴" },
    { "Collect 3000 shards total", "累计收集 3000 碎片" },
    { "Crank for 300 seconds total", "累计摇电 300 秒" },
    { "Kill 25 monsters with the wrench", "用扳手击杀 25 只怪" },
    { "Kill 25 monsters with bottles", "用瓶子击杀 25 只怪" },
    { "Reach level 12 without taking a relic", "不拿遗物到达第 12 层" },
    { "Win a run with 1 sanity left", "只剩 1 点理智时通关" },
    { "Reach level 24", "到达第 24 层" },
    { "Clear a floor with under 20 noise", "噪声低于 20 通过一层" },
    { "Win while below 25% sanity", "理智低于 25% 时通关" },
    { "Win without the flashlight", "不开手电通关" },
    { "Survive at 1 sanity for 30s", "在 1 点理智下撑住 30 秒" },
    { "Kill 100 monsters total", "累计击杀 100 只怪" },
    { "Beat level 24 without a relic", "不拿遗物打到第 24 层" },
    { "Sprint noise -25%, speed +6%", "冲刺噪声 -25%，移速 +6%" },
    { "Search noise -30%, noise -15%", "翻箱噪声 -30%，总体噪声 -15%" },
    { "Dark steps quiet, drain -12%", "关灯时脚步无声，耗电 -12%" },
    { "G: throw a stone (8s cd), noise 14", "G：扔石头引开（8 秒冷却），噪声 14" },
    { "Low-bat regen, battery +20", "低电自动回复，电量上限 +20" },
    { "Crank while walking, move -20%", "可以边走边摇电，移速 -20%" },
    { "Safe heal +25%, sanity drain -10%", "安全屋回智 +25%，理智流失 -10%" },
    { "Sanity loss burns battery (1:3)", "理智流失转成耗电（1:3）" },
    { "Weapon damage +8%/rank", "武器伤害 +8%/级" },
    { "Hits stun +0.2s per rank", "命中硬直 +0.2 秒/级" },
    { "Weapon deals your chosen damage type", "武器按你选的伤害类型结算" },
    { "3rd hit ignores armor", "每第 3 次命中无视护甲" },
    { "Reveal safe house + breaker at start", "开局标出安全屋和配电箱" },
    { "Power-on faster, monsters slowed 20s", "通电更快，通电后怪减速 20 秒" },
    { "Fuse need -1 (min 2)", "保险丝需求 -1（最少 2）" },
    { "Skip one non-boss level per run", "每局可跳过一层（首领层除外）" },
    { "<25% HP targets take +80% damage", "目标血量 <25% 时伤害 +80%" },
    { "Preview next modifier, reroll 150", "预览下一层词条，150 碎片重掷" },
    { "Sanity 0 saves you once (4s)", "理智归零时免死一次" },
    { "Keep 50% pending shards on death", "死亡时保留 50% 待上缴碎片" },
    { "MAX 3 RANKS", "最多 3 级" },
    { "Lv 0/3", "0/3 级" },
    { "SILENCE", "沉寂" },
    { "ECONOMY", "节流" },
    { "DELIVERY", "投放" },
    { "SHAPE", "塑形" },
    { "ENERGY", "能量" },
    { "DRAINED", "枯竭" },
    { "FIREPOT", "燃烧瓶" },
    { "GLUE", "胶瓶" },
    { "MIRROR", "镜面弹" },
    { "HOLD E SEARCH", "长按 E 翻找" },
    { "NEED 1 MORE FUSE", "还差 1 根保险丝" },
    { "PRESS SPACE TO DODGE IT", "按空格闪避" },
    { "FUSE SMITH", "保险丝匠" },
    { "SHORTCUT", "捷径" },
    { "EXECUTIONER", "处刑人" },
    { "WEATHERMAN", "观测者" },
    { "LAST RESORT", "绝境反击" },
    { "SALVAGE", "打捞" },
    { "CARTOGRAPHER", "制图师" },
    { "BREAKER HAND", "配电手" },
    { "TYPE WORK", "熟能生巧" },
    { "RELOAD RHYTHM", "换弹节奏" },
    { "LOW", "低" },
    { "MEDIUM", "中" },
    { "HIGH", "高" },
    { "ULTRA", "极高" },
    { "FULL", "全屏" },
    { "WINDOWED", "窗口" },
    { "ENABLED", "启用" },
    { "DISABLED", "禁用" },
    { "NORMAL", "正常" },
    { "SMALL", "小" },
    { "LARGE", "大" },
    { "Dark steps silent, drain -12%", "暗步无声，流失 -12%" },
};

#define KTR_N ((int)(sizeof(kTr) / sizeof(kTr[0])))

const char* Transl(const char* en) {
    if (!en) return en;
    if (g_lang != LANG_ZH) return en;

    for (int i = 0; i < KTR_N; i++) {
        if (strcmp(en, kTr[i].en) == 0) return kTr[i].zh;
    }

    /* 动态字符串：按前缀造 */
    static char buf[160];
    if (!strncmp(en, "SHARDS ", 7))  { snprintf(buf, sizeof(buf), "碎片 %s", en + 7); return buf; }
    if (!strncmp(en, "CORES ", 6))   { snprintf(buf, sizeof(buf), "核心 %s", en + 6); return buf; }
    if (!strncmp(en, "BEST LEVEL ", 11)) { sprintf(buf, "最高层 %s", en + 11); return buf; }
    if (!strncmp(en, "PENDING ", 8)) { sprintf(buf, "待上缴 %s", en + 8); return buf; }
    if (!strncmp(en, "LEVEL ", 6))   { sprintf(buf, "第 %s 层", en + 6); return buf; }
    if (!strncmp(en, "TIME ", 5))    { sprintf(buf, "时间 %s", en + 5); return buf; }
    if (!strncmp(en, "FUSES ", 6))   { sprintf(buf, "保险丝 %s", en + 6); return buf; }
    if (!strncmp(en, "SCORE ", 6))   { sprintf(buf, "得分 %s", en + 6); return buf; }
    if (!strncmp(en, "RUN FAILED", 10)) return "本局失败";
    if (!strncmp(en, "KNIFE", 5))    { sprintf(buf, "锈刃 %s", en + 5); return buf; }
    {   /* "100 SHARDS" -> "100 碎片" */
        int n = 0; char tail[24];
        if (sscanf(en, "%d SHARDS%23s", &n, tail) == 1) { sprintf(buf, "%d 碎片", n); return buf; }
        if (sscanf(en, "%d CORES%23s", &n, tail) == 1)  { sprintf(buf, "%d 核心", n); return buf; }
    }

    return en;
}

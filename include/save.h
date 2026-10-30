/*
 * Canity — 存档层：SaveBlob 布局 + 读写 / 迁移 / 继续游戏
 *
 * 存档格式版本与结构定义集中在这里。
 */
#ifndef CANITY_SAVE_H
#define CANITY_SAVE_H

#include "common.h"
#include "relics.h"
#include "game.h"

#define SAVE_MAGIC 0x43414E32u   /* "CAN2" */
/* 存档**格式**版本。v5 在末尾追加了 HUD 详细度 / QTE 提示两个字段。
   v6 又在末尾追加：键位绑定表（ACT_COUNT 个 int）+ ECO/REST 两个新升级。
   读档时会按文件的**实际长度**判断是老版本还是新版本：
   老存档照样能读（新增字段取默认值），不用丢玩家进度。
   —— 参见 LoadSettings 里的迁移逻辑。 */
#define SAVE_VERSION 21  /* v17：藏身点进存档（HideSpot[6] + count + hiddenIn） */
#define SAVE_SLOTS   3   /* v2.0：3 个存档槽 */
/* 测试时可以把存档路径换掉，避免覆盖玩家真正的存档 */
#ifndef SAVE_FILE
#define SAVE_FILE "canity.sav"
#endif

/* 存档结果（UI 要显示）：0 = 还没试过，1 = 写成功，2 = 一个位置都写不进去。
   失败时 g_saveError 是 exe 目录那次的 Win32 错误码。 */
extern int  g_saveState;
extern int  g_saveError;
extern int  g_saveSlot;      /* v2.0：当前存档槽 0..2 */
extern char g_savePath[MAX_PATH];

typedef struct {
    unsigned int magic;
    int version;
    int gameVersion;      /* 写入时的 CANITY_VERSION_NUM，用来识别跨版本存档 */

    /* 设置 */
    int resIndex, fullscreen, fpsCap, scale, fov, vignette, showFps;
    float gamma, sens;

    /* 大厅进度 */
    int shards, upgBattery, upgSpeed, upgQuiet, upgSanity;
    int bestLevel;
    float volume;
    /* 进度 */
    int hasRun, level, fusesHeld, fusesNeeded, breakerOpen, breakerActive, searchedCount, nContainers;
    int flashlightOn;
    float sanity, battery, stamina, posX, posY, dirX, dirY, exitX, exitY, breakerX, breakerY, runTime;
    Container cont[MAX_CONTAINERS];
    int worldMap[MAP_HEIGHT][MAP_WIDTH];
    int explored[MAP_HEIGHT][MAP_WIDTH];

    /* v4 新增：以前这些状态没存，读档后会丢失 */
    int flares, bottles;                      /* 手上的投掷物数量 */
    int safeActive;                           /* 安全屋（花）是否存在 */
    float safeX, safeY;                       /* 安全屋位置 */
    int levelMod;                             /* 本层词条编号，-1 = 无 */
    float modSanityMul, modSpeedMul;          /* 词条修正系数 */
    int themeId;                              /* 关卡主题（墙/地贴图） */
    int nThrown;                              /* 场上投掷物个数 */
    Thrown thr[MAX_THROWN];                   /* 场上投掷物状态 */

    /* v5 新增：HUD 详细度与 QTE 提示档位。
       放在结构**末尾**是刻意的 —— 这样旧存档的字节完全对得上，
       读档时按文件长度就能判断是不是老版本（见 LoadSettings）。 */
    int hudDetail;
    int qteHint;

    /* v6 新增：键位绑定表 + 两个新升级（ECO/REST）。同样放末尾，
       v5 存档按文件长度识别后取默认键位与 0 级升级。 */
    int bindings[BINDING_SLOTS];   /* v8：固定 32 槽，加动作不再改布局 */
    int upgEco, upgRest;

    /* v7 新增：成就系统。放在结构末尾以保持兼容性。 */
    int achUnlocked[ACH_COUNT];
    int achProgress[ACH_COUNT];

    /* v8 新增：无尽模式进度。放在结构末尾以保持兼容性。 */
    int bestEndlessLevel;
    int bestEndlessScore;
    int isEndlessRunSaved;

    /* 应用 1.8.0 新增：技能树 / 核心 / keystone。
       测试版不做向后兼容：旧存档被 SAVE_VERSION 校验挡掉。 */
    int skillRank[SKILL_COUNT];
    int cores;
    int activeKeystone;
    int bestMainScore;
    int seenMonsters;      /* v1.8：怪物图鉴解锁位掩码 */
    /* ---- v9：以前没存的运行时进度 ---- */
    int bricks, acids, smokeBombs;
    int pendingShards;
    unsigned int runSkillMask;
    int lang;              /* v2.0：界面语言 */
    int saveSlot;          /* v2.0：存档槽 0..2 */
    /* ---- v11：武器局外成长 ---- */
    int weaponRank[3];
    int weaponXp[3];
    /* ---- v13：v2.0 新系统 ---- */
    int   relicOwned[RELIC_COUNT];
    int   relicTotal;
    int   bossLevel, bossMaxHp, bossDefeated;
    int   dailyMode, dailySeed;
    int   campaignMode, campaignStage;
    int   firePots, gluePots, mirrors;
    int   bankStreak;
    float modBatteryMul, modShardMul, modNoiseMul;
    int weaponAttach[3][2];
    int attachOwned[10];
    int curWeapon;

    int runUltimate;

    /* ---- v2.3：武器能量（追加在末尾，老档读到 0，载入时补满） ---- */
    float energy;
    /* v3.0：局内改造树 / 强化必须存盘 ——
       原来重启后这两个全归零，但买它们的碎片已经从 shards 扣掉了，
       等于每重启一次就吞掉玩家最多 5720 碎片（数值审查 #4）。 */
    unsigned int benchOwnedSave;   /* 改造树位掩码 */
    int runUpgSave[4];             /* 强化 4 条线的等级 */
    /* v17：藏身点必须存盘 —— 否则读档后整层一个都没有（玩法审查 #3） */
    int hideSpotSaveCount;
    HideSpot hideSpotSave[6];
    int hiddenInSave;
    /* v18：安全屋每层限一次的三个标记必须存盘 ——
       原来它们是 shop.c 的文件级 static，读档后全回 -1，
       于是「在 6 层充过电 → 触发任一自动存档 → 退出重进」
       就能在同一层再免费充一次（审计报告商店 #5）。 */
    int safeHealLvSave;
    int safeChargeLvSave;
    int safeStaminaLvSave;
    /* v3.3：跨局累计的成就计数器必须存盘 ——
       原来 unlocked 是永久的，但这些计数器不进存档，
       关一次游戏进度就归零，成就永远解不开。 */
    int achFloorsNoLight, achFloorsFlawless, achFloorsPacifist, achFloorsFast;
    int achMapsExplored, achFusesRun, achLocksOpened;
    int achHideCount, achTotalThrown;
    float achCrankSecondsTotal, achLowSanitySeconds;
    /* v3.4 银行博弈：存款 / 历史最高 / 携带档位 必须存盘，
       否则关游戏就把银行存款丢了。 */
    int bankGoldSave;
    int bankHistMaxSave;
    int bankCarryPctSave;
    /* v6：**本局本金必须存盘** —— 它是 BankSettle 的利润基准：
         profit = shards - carryGold；死亡返还 35% 也按它算；
         金币招怪（GoldThreatMul）也读它。
       不存的话读档后 carryGold = 0 → 利润算成全额、死亡返还变 0、
       带钱招怪整条机制消失。 */
    int carryGoldSave;
} SaveBlob;

/* 写当前进度（游戏中 / 退出时） */
void SaveGame(void);
/* 启动时只读设置；返回是否还有可继续的进度 */
int LoadSettings(void);
/* 把存档里的进度灌回运行时 */
void ContinueRun(void);
void DeleteSave(void);

#endif /* CANITY_SAVE_H */

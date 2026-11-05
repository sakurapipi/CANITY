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
/* v28: 末尾追加 Dynamic Res + 通关记录；v25/v26/v27 都按前缀迁移。
   v27 及更早的存档没有这两个字段，读档时按文件长度取默认值。 */
#define SAVE_VERSION 32   /* v8: 末尾追加音乐音量；继续兼容 v24/v25/v26/v27 */
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

/* ==================================================================
 * 2026-11-06（v2.4-alpha R9）：**存档序列化错误码**
 * 负数，和 SaveBackendWrite 返回的 Win32 正数错误码区分开。
 * ================================================================ */
#define SAVE_ERR_CAPACITY   (-1)   /* 容量不足（含块数超限） */
#define SAVE_ERR_DUP_CHUNK  (-2)   /* 块表出现重复 ID → 拒绝改写 */
#define SAVE_ERR_PAYLOAD    (-3)   /* 信封/块表/校验和损坏 */


/* ---- 存档分区后端（src/save/save_backend.c）----
   全工程**唯一**允许碰 canity*.sav 的地方。 */
int   SaveBackendWrite(const char* path, const void* data, size_t n); /* 0 = 成功 */
long  SaveBackendRead(const char* path, void* buf, size_t cap);       /* 字节数 / -1 */
void  SaveBackendUpload(const char* path);                            /* 交给 Steam 专区 */
void  SaveBackendSyncAll(void);                                       /* 云 <-> 本地 */

/* 按「候选路径 i + 存档槽 slot」算路径（save.c 实现） */
const char* SavePathForSlot(int i, int slot, char* buf, size_t n);

typedef struct {
    unsigned int magic;
    int version;
    int gameVersion;      /* 写入时的 CANITY_VERSION_NUM，用来识别跨版本存档 */

    /* 设置 */
    int resIndex, fullscreen, fpsCap, scale, fov, vignette, showFps;
    float gamma, sens;

    /* 大厅进度 */
    int carryGold, upgBattery, upgSpeed, upgQuiet, upgSanity;
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
    int skillRank[26];
    int cores;
    int activeKeystone;
    int bestMainScore;
    int seenMonsters;      /* v1.8：怪物图鉴解锁位掩码 */
    /* ---- v9：以前没存的运行时进度 ---- */
    int bricks, acids, smokeBombs;
    unsigned int runSkillMask;
    int lang;              /* v2.0：界面语言 */
    int saveSlot;          /* v2.0：存档槽 0..2 */
    /* ---- v11：武器局外成长 ---- */
    int weaponRank[3];
    int weaponXp[3];
    /* ---- v13：v2.0 新系统 ---- */
    int   relicOwned[RELIC_COUNT];
    int   relicTotal;
    /* v8: bossLevel / bossMaxHp / bossDefeated 已从存档移除（BOSS 已删） */
    int   dailyMode, dailySeed;
    /* v7 第 ㊿ 轮：**每日任务的进度必须存盘**
       原来 s_daily 整个结构不进存档，开机首帧发现 !s_ready 就重新生成
       → 今天做了一半的进度、连击天数、完成标记全部归零
       → 「连续 7 天 +5,000」在正常开机关机的节奏下不可能达成。 */
    int   dailyGoalId, dailyGoalParam, dailyProgress, dailyDone, dailyStreak;
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
       原来重启后这两个全归零，但买它们的金币已经从 carryGold 扣掉了，
       等于每重启一次就吞掉玩家最多 5720 金币（数值审查 #4）。 */
    unsigned int benchOwnedSave;   /* 改造树位掩码 */
    int runUpgSave[5];   /* v7：4 -> 5（新增 WRENCH）。save.h 不包含 upgrade_run.h，这里写死 */             /* 强化 4 条线的等级 */
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
         profit = carryGold - carryGold；死亡返还 35% 也按它算；
         金币招怪（GoldThreatMul）也读它。
       不存的话读档后 carryGold = 0 → 利润算成全额、死亡返还变 0、
       带钱招怪整条机制消失。 */
    int carryGoldSave;
    unsigned int dailyQuestDate, dailyLastCompletedDate;
    int dailySamples[8];
    int runPrincipalSave;
    int knockRunSave, throwHitRunSave, echoRunSave, dailyEscapeRunSave;
    int cleanFloorsSave, wardenFloorsSave;
    float doubleHuntSave;
    int fullHideSave;
    int lastRunGoldSave, lastRunKeptSave, lastRunCaughtSave;
    int extraSkillRanks[4];
    int shopRefreshFloorSave, shopRefreshUsedSave;
    int contractKindSave, contractLockedSave;
    float threatSave;
    int bestiaryKnowledge[18];
    /* v27：音乐音量独立于音效音量。追加在末尾，旧档读不到时回退到音效音量。 */
    float musicVolume;
    /* v28：这两个设置/进度原来根本没有进存档。
       · dynamicRes —— 设置页里的 Dynamic Res，重启后总是回到默认 ON；
       · gameClearedSave —— 「通关过一局」的持久记录。无尽模式的解锁条件是
         `bestLevel >= 10 || gameWon`，而 main 模式在第 8 层就结束、
         bestLevel 永远到不了 10，所以唯一解锁途径 gameWon 只是运行时变量，
         重启后 Endless 从菜单里消失。 */
    int dynamicRes;
    int gameClearedSave;
    /* v29：局内强化第 6 条（小地图省电）。
       **不能直接放大 runUpgSave[5] → [6]** —— 它在结构体中间，
       一改就把后面所有字段整体挪 4 字节，v28 的旧档会全部错位。
       所以新字段只能追加在末尾，读档时单独调用 RunUpgLoadExtra()。 */
    int runUpgMinimapSave;
    /* ==================================================================
     * 2026-11-06（v2.4-rc）：**主题房间的重放种子**
     * ------------------------------------------------------------------
     * 房间是"刻进 worldMap"的，而 worldMap 本来就存在档里，
     * 所以房间的**墙**不需要额外保存。
     * 但 cellMeta（色调/声学）和 roomProps（家具 + 碰撞）是运行时数据 ——
     * 读档后必须重建，否则每一层重新载入都会变回"没有房间"。
     *
     * 做法：生成时把房间阶段的随机种子记下来，
     * 读档后用同一个种子在已恢复的 worldMap 上**重放一次**房间放置。
     * 房间放置是确定性纯函数（只吃 worldMap + 种子），所以结果一致。
     *
     * 老档（v29）里这两个字段读成 0 → 当作"无房间层"，不崩、不丢进度。
     * ================================================================== */
    int roomSeed;      /* 房间阶段的随机种子；0 = 这层没有房间 */
    int roomCount;     /* 本层房间数 */
} SaveBlob;

/* ==================================================================
 * 2026-11-06（v2.4-alpha R12）：**仅测试用的序列化钩子**
 * ------------------------------------------------------------------
 * ExtSaveFile / SaveFileBytes 在 save.c 里是 static。
 * 要验证「未知块经过真实读写流程仍然保留」，必须能针对内存字节
 * 走一遍真实的捕获 → 重建逻辑，而不是再造一套测试专用的序列化。
 * 所以只把这两个 static 原样导出，行为完全一致。
 *
 * 生产代码不应调用它们 —— 正式写盘走 SaveGame / SaveSettingsOnly。
 * ================================================================ */
int SaveTestSerialize(const SaveBlob* blob, unsigned char** out);   /* >0 长度，<0 SAVE_ERR_* */
int SaveTestDeserialize(const void* buf, long n, SaveBlob* out);
void SaveTestClearReadOnly(void);   /* 仅测试用：复位只读标志 */
const SaveBlob* SaveTestBlob(void);  /* 仅测试用：最近一次载入的 blob */    /* 1 成功，0 失败（看 g_saveError） */


/* 写当前进度（游戏中 / 退出时） */
void SaveGame(void);
/* 启动时只读设置；返回是否还有可继续的进度 */
int LoadSettings(void);
/* 把存档里的进度灌回运行时 */
void ContinueRun(void);
/* 存档里是否还有一局可继续（训练局退出后恢复菜单用） */
int SaveHasPendingRun(void);
void DeleteSave(void);

#endif /* CANITY_SAVE_H */

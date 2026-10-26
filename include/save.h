/*
 * Canity — 存档层：SaveBlob 布局 + 读写 / 迁移 / 继续游戏
 *
 * 存档格式版本与结构定义集中在这里。
 */
#ifndef CANITY_SAVE_H
#define CANITY_SAVE_H

#include "common.h"

#define SAVE_MAGIC 0x43414E32u   /* "CAN2" */
/* 存档**格式**版本。v5 在末尾追加了 HUD 详细度 / QTE 提示两个字段。
   v6 又在末尾追加：键位绑定表（ACT_COUNT 个 int）+ ECO/REST 两个新升级。
   读档时会按文件的**实际长度**判断是老版本还是新版本：
   老存档照样能读（新增字段取默认值），不用丢玩家进度。
   —— 参见 LoadSettings 里的迁移逻辑。 */
#define SAVE_VERSION 7
/* 测试时可以把存档路径换掉，避免覆盖玩家真正的存档 */
#ifndef SAVE_FILE
#define SAVE_FILE "canity.sav"
#endif

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
    int bindings[ACT_COUNT];
    int upgEco, upgRest;

    /* v7 新增：成就系统。放在结构末尾以保持兼容性。 */
    int achUnlocked[ACH_COUNT];
    int achProgress[ACH_COUNT];
} SaveBlob;

/* 写当前进度（游戏中 / 退出时） */
void SaveGame(void);
/* 启动时只读设置；返回是否还有可继续的进度 */
int LoadSettings(void);
/* 把存档里的进度灌回运行时 */
void ContinueRun(void);
void DeleteSave(void);

#endif /* CANITY_SAVE_H */

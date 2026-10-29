/*
 * Canity — 存档层：SaveBlob 布局 + 读写 / 迁移 / 继续游戏
 *
 * 存档格式版本与结构定义集中在这里。
 */
#ifndef CANITY_SAVE_H
#define CANITY_SAVE_H

#include "common.h"

#define SAVE_MAGIC 0x43414E32u   /* "CAN2" */
/* 存档**格式**版本。
   v5 在末尾追加了 HUD 详细度 / QTE 提示两个字段。
   v6 又在末尾追加：键位绑定表（ACT_COUNT 个 int）+ ECO/REST 两个新升级。
   v7 追加成就数组；v8 追加无尽模式进度；v9 追加上锁容器状态；
   v10 追加锁型；v11 追加新动作绑定 + 技能选择；v12 追加隐藏房间；
   v13 追加残卷（技能升级唯一货币）；v14 追加 8 项新成就；
   v15 预留容量：成就最多 64 项、技能最多 16 个。
   读档时会按文件的**实际长度**判断是老版本还是新版本：
   老存档照样能读（新增字段取默认值），不用丢玩家进度。
   —— 参见 LoadSettings 里的迁移逻辑。 */
#define SAVE_VERSION 15
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
       v5 存档按文件长度识别后取默认键位与 0 级升级。
       注意 bindings 是 **BINDINGS_BASE 固定长度**，不是 ACT_COUNT ——
       以后加动作时 ACT_COUNT 会变，但这个数组绝不能跟着变，
       否则它后面的 upgEco/成就/无尽/锁字段全部错位。 */
    int bindings[BINDINGS_BASE];
    int upgEco, upgRest;

    /* v7 新增：成就系统。放在结构末尾以保持兼容性。 */
    int achUnlocked[ACH_COUNT];
    int achProgress[ACH_COUNT];

    /* v8 新增：无尽模式进度。放在结构末尾以保持兼容性。 */
    int bestEndlessLevel;
    int bestEndlessScore;
    int isEndlessRunSaved;

    /* v9 新增：上锁容器（v1.8.0）。
       **必须**用平行数组，不能往 Container 里加字段 ——
       Container 嵌在结构中间，加字段会让 worldMap / 成就 / 无尽进度
       整体后移，老存档全部错位。 */
    int contLocked[MAX_CONTAINERS];
    int contLockCode[MAX_CONTAINERS];

    /* v10 新增：锁型（拨盘 / 接线 / 均衡器 / 调波器）。v9 老档默认全当拨盘。 */
    int contLockType[MAX_CONTAINERS];

    /* v11 新增：第 BINDINGS_BASE 个之后的新动作绑定（目前只有技能）。
       放在整个结构末尾，以后再加动作只需要扩这个数组，老存档不受影响。 */
    int extraBindings[BINDINGS_EXTRA];

    /* v11 新增：技能。
       selectedSkill = 大厅里选中的技能；runSkill = 这局实际带的技能。
       两者不一致（中途换过技能）时这局不允许续 —— 见 LoadSettings。 */
    int selectedSkill;
    int runSkill;
    int skillLevels[SKILL_SLOTS];   /* 预留升级槽，固定容量避免以后再改结构 */

    /* v12 新增：隐藏房间（可选，一层最多一个）。
       worldMap 里已经存了暗门的状态（2 = 未开），这里存的是房间几何与收集进度。 */
    int hiddenActive, hiddenOpened;
    int hiddenX0, hiddenY0, hiddenX1, hiddenY1;
    int hiddenDoorX, hiddenDoorY;
    int hiddenCodeCount, hiddenCodesFound;
    int hiddenCodeX[HIDDEN_MAX_CODES], hiddenCodeY[HIDDEN_MAX_CODES];
    int hiddenCodeTaken[HIDDEN_MAX_CODES];

    /* v13 新增：残卷。技能升级只能花这个，不能花碎片。 */
    int scrolls;

    /* v14 新增：第 17~24 项成就。老的前 16 项仍在 achUnlocked/achProgress 里，
       这两个数组长度永远不变，所以新成就不动既有布局。 */
    int achUnlocked2[ACH_EXTRA];
    int achProgress2[ACH_EXTRA];

    /* v15：一次性容量预留。
       索引从 ACH_BASE2(24) / SKILL_BASE2(6) 开始，长度**固定**；
       以后加成就只改 ACH_TOTAL、加技能只改 SKILL_COUNT，
       不再需要动存档结构或升 SAVE_VERSION。 */
    int achUnlockedX[ACH_RESERVE];
    int achProgressX[ACH_RESERVE];
    int skillLevelsX[SKILL_RESERVE];
} SaveBlob;

/* 写当前进度（游戏中 / 退出时） */
void SaveGame(void);
/* 启动时只读设置；返回是否还有可继续的进度 */
int LoadSettings(void);
/* 把存档里的进度灌回运行时 */
void ContinueRun(void);
void DeleteSave(void);

#endif /* CANITY_SAVE_H */

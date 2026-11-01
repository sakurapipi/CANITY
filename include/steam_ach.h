/* ==================================================================
 *  STEAM 专区 —— 成就 ID 表
 * ==================================================================
 *
 *  为什么单独一个文件
 *  ──────────────────
 *  成就的 API Name 必须**和 Steamworks 后台完全一致**。
 *  字符串散落在代码里 → 后台改个名字，代码里就静默失效。
 *
 *  所以：**所有成就 ID 只在这里定义一次**，
 *  代码里一律用 SACH_xxx 常量，绝不写裸字符串。
 *
 *  后台配置时照着这张表建，一一对应。
 *  命名规范：ACH_<类>_<两位序号>_<短名>
 *            SURVIVE / ECON / SKILL / SECRET
 * ================================================================== */
#ifndef CANITY_STEAM_ACH_H
#define CANITY_STEAM_ACH_H

/* ---- 生存 6（奖励 500 金）---- */
#define SACH_SURVIVE_01  "ACH_SURVIVE_01_FIRST_EXIT"    /* 第一次成功撤离 */
#define SACH_SURVIVE_02  "ACH_SURVIVE_02_FLOOR_5"       /* 到达第 5 层 */
#define SACH_SURVIVE_03  "ACH_SURVIVE_03_FLOOR_10"      /* 到达第 10 层 */
#define SACH_SURVIVE_04  "ACH_SURVIVE_04_FLOOR_15"      /* 到达第 15 层 */
#define SACH_SURVIVE_05  "ACH_SURVIVE_05_TEN_MIN"       /* 单局存活 10 分钟 */
#define SACH_SURVIVE_06  "ACH_SURVIVE_06_25_MIN"        /* 单局存活 25 分钟 */

/* ---- 经济 6（奖励 800 金）---- */
#define SACH_ECON_01     "ACH_ECON_01_CARRY_20K"        /* 携带 20 000 金 */
#define SACH_ECON_02     "ACH_ECON_02_CARRY_100K"       /* 携带 100 000 金 */
#define SACH_ECON_03     "ACH_ECON_03_CARRY_500K"       /* 携带 500 000 金 */
#define SACH_ECON_04     "ACH_ECON_04_BOUNTY_X3"        /* 抢回赃款 3 次 */
#define SACH_ECON_05     "ACH_ECON_05_BANK_100K"        /* 累计存入 100 000 */
#define SACH_ECON_06     "ACH_ECON_06_CLEAN_ESCAPE"     /* 零被抓撤离 */

/* ---- 技巧 6（奖励 1 200 金）---- */
#define SACH_SKILL_01    "ACH_SKILL_01_PERFECT"         /* 完美逃脱 */
#define SACH_SKILL_02    "ACH_SKILL_02_KNOCK_10"        /* 扳手击退 10 次 */
#define SACH_SKILL_03    "ACH_SKILL_03_BOTTLE_5"        /* 瓶子打断 5 次 */
#define SACH_SKILL_04    "ACH_SKILL_04_WARDEN_3F"       /* 三层不被守门人抓 */
#define SACH_SKILL_05    "ACH_SKILL_05_ECHO_50"         /* 回声揭示 50 格 */
#define SACH_SKILL_06    "ACH_SKILL_06_NO_CONTAINER"    /* 不开箱撤离 */

/* ---- 隐藏 6（奖励 2 000 金，未解锁显示 ???）---- */
#define SACH_SECRET_01   "ACH_SECRET_01_UNDER_WARDEN"   /* 守门人眼皮底下撤离 */
#define SACH_SECRET_02   "ACH_SECRET_02_DOUBLE_HUNT"    /* 双鬼追 30 秒后逃脱 */
#define SACH_SECRET_03   "ACH_SECRET_03_300K_FLOOR12"   /* 30 万金到第 12 层 */
#define SACH_SECRET_04   "ACH_SECRET_04_META_RESET"     /* 局外重置一次 */
#define SACH_SECRET_05   "ACH_SECRET_05_BESTIARY_6"     /* 6 种怪全见 */
#define SACH_SECRET_06   "ACH_SECRET_06_HIDE_FULL"      /* 藏身点躲过整次追捕 */

/* ---- 表 ---- */
typedef struct {
    const char* apiName;
    const char* nameEn;
    const char* nameZh;
    int         category;   /* 0 生存 1 经济 2 技巧 3 隐藏 */
    int         reward;     /* 金币 */
    int         hidden;     /* 1 = 未解锁显示 ??? */
} SteamAchDef;

extern const SteamAchDef kSteamAchievements[];
extern const int         kSteamAchievementCount;

/* 按 API Name 查下标，找不到返回 -1 */
int SteamAchievementIndex(const char* apiName);

#endif /* CANITY_STEAM_ACH_H */

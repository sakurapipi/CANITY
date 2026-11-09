/*
 * Canity — 流派标签（v2.4.1 第 7~9 轮）
 *
 * ==================================================================
 * 为什么需要标签层
 * ------------------------------------------------------------------
 * 游戏已经有 24 张局内技能卡、12 件遗物、6 条局内强化，
 * 但奖励之间**没有方向**：玩家可能拿到移速 + 回电 + 降噪，
 * 却说不出自己在玩什么流派。
 *
 * 这一层只做两件事：
 *   ① 给每张卡/每件遗物打一个流派标签
 *   ② 从"已持有"反推当前倾向（动态识别，不是开局选职业）
 *
 * 刻意**不改** InRunDef 的结构 —— 那是存档与 UI 都依赖的定长数据。
 * 标签放在独立的静态表里，加卡时只改一处。
 * ==================================================================
 */
#ifndef CANITY_BUILD_TAGS_H
#define CANITY_BUILD_TAGS_H

typedef enum {
    BUILD_NONE = 0,
    BUILD_GADGET,     /* 道具工匠：靠投掷物创造机会 */
    BUILD_SPEED,      /* 速通突围：靠机动性与效率 */
    BUILD_STEALTH,    /* 潜行生存：靠降噪、理智管理与藏身 */
    BUILD_COUNT
} BuildTag;

/* 单张卡的标签。-1 / BUILD_NONE = 通用卡。 */
BuildTag BuildTagOfSkill(int irId);
BuildTag BuildTagOfRelic(int relicId);

/* 当前倾向分：卡 ×2 + 遗物 ×2（通用卡不计分） */
int BuildScore(BuildTag t);

/* 最高分且 >= 3 且唯一 → 该流派；否则 BUILD_NONE（还没成型） */
BuildTag BuildDominant(void);

const char* BuildName(BuildTag t, int zh);

/* 面板用：已形成的联动条数 + 一行摘要（写进 out，最多 cap 字节） */
int  BuildSynergyCount(void);
void BuildSynergyList(char* out, int cap);

void BuildReset(void);


/* Whether a draft card has an implemented gameplay effect. */
int BuildCardImplemented(int irId);

#endif /* CANITY_BUILD_TAGS_H */

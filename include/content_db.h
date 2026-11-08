/* include/content_db.h —— CANITY 内容中枢（2026-11-09）
 *
 * ## 为什么要有这个文件
 *
 * 同一个内容的"事实"在这个项目里经常散在 2~4 处：
 *
 *     技能：skillDefs[] + kShortNames[] + kNodeNX/NY[] + kSkillList[]
 *     图鉴：bestiary[] + bestiaryKnowledge[]
 *     设置：settings[] + g_bindings[]
 *
 * 全靠人同步 → 漏一处就出现「加了 10 个节点，界面一个都不画」。
 *
 * ## 中枢的做法
 *
 * 所有内容登记到一张表里，其它系统**只查这张表**：
 *
 *     ContentAdd(CK_MONSTER, "sound_hunter", &bestiary[0]);
 *
 * 然后图鉴、成就、UI 都走 ContentCount / ContentGet / ContentFind。
 *
 * ## 三条规矩
 *
 * 1. **不许绕过中枢自己再存一份**（否则又在制造第二真相）
 * 2. id 必须唯一（重复登记返回 -1，并且 ContentDuplicateKind() 会报出来）
 * 3. 登记的是**指针**，内容本体仍然归各系统所有 —— 中枢只做索引
 * 4. **id 必须指向稳定存储**（字符串常量，或活到程序结束的缓冲）。
 *    中枢不复制 id —— 传栈上的临时缓冲进来，第二次登记会被误判重复。
 *
 * ## 第 2 轮的范围
 *
 * 本轮只搭骨架 + 测试，**没有任何系统接进来**（零风险）。
 * 第 3 轮开始逐个通电。
 */
#ifndef CANITY_CONTENT_DB_H
#define CANITY_CONTENT_DB_H

/* 内容种类。加新种类：在这里加一个，CONTENT_MAX 自动跟上。 */
typedef enum {
    CK_MONSTER = 0,      /* 怪物（图鉴） */
    CK_SHOT,             /* 投掷物 */
    CK_SKILL,            /* 技能节点 */
    CK_ACHIEVEMENT,      /* 成就 */
    CK_SETTING,          /* 设置项 */
    CK_COUNT
} ContentKind;

/* 每一类最多登记多少条 */
#define CONTENT_MAX 128

/* 清空整张表（测试与新局用） */
void ContentReset(void);

/* 登记一条。成功返回它的序号（>=0），失败返回 -1：
 *   - id 为空
 *   - id 重复
 *   - 该种类已满（CONTENT_MAX） */
int ContentAdd(ContentKind kind, const char* id, const void* ptr);

/* 某一类有多少条 */
int ContentCount(ContentKind kind);

/* 按序号取（越界返回 NULL） */
const void* ContentGet(ContentKind kind, int index);

/* 按序号取 id（越界返回 NULL） */
const char* ContentIdAt(ContentKind kind, int index);

/* 按 id 找序号（找不到返回 -1） */
int ContentFind(ContentKind kind, const char* id);

/* 自检：有没有重复 id 漏进来。返回第一个出问题的种类，全干净返回 -1。 */
int ContentDuplicateKind(void);

/* 自检：登记总条数（所有种类相加） */
int ContentTotal(void);

#endif

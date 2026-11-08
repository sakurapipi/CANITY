/* include/core_tick.h —— 每帧更新的**声明式顺序表**
 *
 * 为什么要有
 *   `Update()`（src/game.c:3768 起）是一条约 2,000 行的函数，里面散着
 *   十几个域的 tick 调用。想知道"投掷物在哪一步更新"只能通读整段 ——
 *   这正是 v2 方案里说的"改一次要读 2 万行"。
 *
 *   现在把顺序**声明**在这里，并且用运行期断言守着它：
 *   每个域 tick 之前调一次 TickMark(该域)，如果实际调用顺序和这里的
 *   枚举顺序不一致，TickOrderViolations() 就会 >0，测试会红。
 *
 * 注意
 *   这**不改变任何执行顺序**（只是加了一次序号比较），所以对玩法零影响。
 *   以后要把某个 tick 挪位置，先改这里的枚举，测试会告诉你哪里还没跟上。
 */
#ifndef CANITY_CORE_TICK_H
#define CANITY_CORE_TICK_H

/* 声明顺序 = 期望执行顺序。新增域 tick 请插到正确位置，不要加在末尾图省事。 */
typedef enum {
    /* 顺序取自 game.c 的 Update()，**不是理想顺序而是实际顺序**：
     * 第一次写这枚举时我按"崩塌应该最早"排，结果真帧用例立刻报违规 ——
     * 实际是 HatchTick 在 CollapseTick 之前。声明必须反映现实，
     * 否则这张表就是第二份真相。 */
    TICK_HATCH = 0,      /* 舱门开门动画 + 效果结算 */
    TICK_COLLAPSE,       /* 崩塌流程：黑屏期间冻结后续一切 */
    TICK_THROWING,       /* 投掷物飞行与落点 */
    TICK_SMOKES,         /* 烟雾衰减（含商店冷却/钱袋/守门人） */
    TICK_COUNT
} TickId;

/* 名字（日志/测试用） */
const char* TickName(TickId id);

/* 每帧开头调一次：复位序号，允许从头再来一遍。 */
void TickFrameBegin(void);

/* 每个域 tick 之前调一次。顺序不对会记一次违规（不会中止游戏）。 */
void TickMark(TickId id);

/* 本帧至今累计的顺序违规次数；TickFrameBegin 会清零。 */
int  TickOrderViolations(void);

/* 上一次违规的说明（"smokes 在 throwing 之前"），没有则返回空串。 */
const char* TickLastViolation(void);

#endif /* CANITY_CORE_TICK_H */

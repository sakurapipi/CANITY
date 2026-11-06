/*
 * Canity — 控制效果层（v2.4.1 第 4 轮）
 *
 * ==================================================================
 * 为什么需要这一层
 * ------------------------------------------------------------------
 * 抱脸虫与缚丝蛛都要"抓住玩家 → 连按挣脱键 → 成功/超时"。
 * 如果各写一套，就会出现：
 *   · 两套各自监听空格，同时生效 → 按一下算两次
 *   · 两套各自计时 → 暂停时其中一套还在走
 *   · 谁先谁后没有仲裁 → 玩家被连续控制到死
 *
 * 所以这里只放**一个**控制槽、**一个**计时器、**一条**按键边沿，
 * 所有控制类型都走同一套规则。
 *
 * 硬约束（都能被测试钉住）：
 *   ① 同一时刻只允许一个控制，后来者在已有控制时被拒绝（不排队）
 *   ② 释放后有一段免疫窗口，期间同类控制起不来
 *   ③ 挣脱必须**按次**计数 —— 按住键不会因为键盘重复而自动完成
 *   ④ 计时只在 ControlTick 被调用时推进；暂停/失焦不调用 → 自然停表
 *   ⑤ ControlReset 之后状态完全干净（新局 / 换层 / 读档 / 崩塌重掷）
 * ==================================================================
 */
#ifndef CANITY_CONTROL_EFFECT_H
#define CANITY_CONTROL_EFFECT_H

typedef enum {
    CTRL_NONE = 0,
    CTRL_FACEHUGGER,      /* 爆脸怪：贴脸后连戳 */
    CTRL_SPIDER_WEB       /* 缚丝蛛：蛛网束缚 */
} ControlType;

#define CONTROL_IMMUNE_TIME 3.0f   /* 释放后的免疫窗口（秒） */

/* 开始一次控制。返回 1 = 成功接管；0 = 已有其它控制 / 免疫中。 */
int ControlBegin(ControlType t, int mashNeeded, float window);

/* 每帧推进。**只在真正游玩时调用** —— 暂停、设置界面、失焦都不该调。 */
void ControlTick(float dt);

/* 按下一次挣脱键。返回 1 = 本次按下正好完成挣脱（调用方据此结算成功）。 */
int ControlPress(void);

void  ControlRelease(int escaped);   /* 唯一出口：清槽 + 开免疫窗口 */
void  ControlReset(void);            /* 新局 / 换层 / 读档 / 崩塌 */

/* ==================================================================
 * 2026-11-06（审计补）：**谁都可以按空格挣脱，结果由持有者取走**
 * ------------------------------------------------------------------
 * 原来只有抱脸虫的 JMP_MASH 自己读空格。蜘蛛的蛛网虽然也走
 * ControlBegin，却**没有任何代码读 ControlPress** ——
 * 于是蛛网一旦生效就永远占着控制槽：玩家既没被限制移动，
 * 也永远挣脱不掉，后面抱脸虫再也抓不到人。
 *
 * 现在把"按键 + 超时"统一放到 Update 里的一个处理点，
 * 持有者通过 ControlTakeResult() 取一次结果：
 *   0 = 没有新结果，1 = 挣脱成功，2 = 超时失败（读走即清零）
 * ================================================================ */
int   ControlTakeResult(void);

int         ControlActive(void);
ControlType ControlCurrent(void);
int         ControlMashLeft(void);
float       ControlTimeLeft(void);
float       ControlImmune(void);

#endif /* CANITY_CONTROL_EFFECT_H */

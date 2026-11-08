/* src/core/tick.c —— 每帧 tick 的顺序守卫
 *
 * 设计取舍（第 4 轮，2026-11-08）
 * ----------------------------------------------------------------
 * 一开始想把 Update() 里的 tick 抽成"函数指针表 + 循环调用"。
 * 检查后发现不可行：这些 tick 之间夹着大量**非 tick 的逻辑**
 * （输入采样、交互判定、经济结算……），抽成表就必然改变相对顺序，
 * 那是行为改动，不是在整理结构。
 *
 * 所以改成现在这样：
 *   · 顺序**声明**在 core_tick.h 的枚举里（唯一事实来源）
 *   · 每个 tick 调用前打一个序号标记
 *   · 顺序不单调就记违规
 * 效果：想查顺序看头文件；有人挪了调用位置，第一次跑帧就报出来。
 * 运行期代价：每帧 4 次整数比较，可以忽略。
 */
#include "core_tick.h"

static int s_lastMark = -1;      /* 本帧上一个标记的 TickId */
static int s_violations = 0;
static char s_lastMsg[96] = "";

/* 名称与 id 一一对应；id 的先后由 core_tick.h 的枚举决定。 */
const char* TickName(TickId id) {
    switch (id) {
    case TICK_COLLAPSE: return "collapse";
    case TICK_HATCH:    return "hatch";
    case TICK_THROWING: return "throwing";
    case TICK_SMOKES:   return "smokes";
    default:            return "?";
    }
}

void TickFrameBegin(void) {
    s_lastMark = -1;
    s_violations = 0;
    s_lastMsg[0] = '\0';
}

void TickMark(TickId id) {
    if (s_lastMark >= 0 && (int)id <= s_lastMark) {
        /* 顺序不对：要么倒着调了，要么重复调了 */
        s_violations++;
        {
            const char* a = TickName((TickId)s_lastMark);
            const char* b = TickName(id);
            int i = 0;
            while (a[i] && i < 40) { s_lastMsg[i] = a[i]; i++; }
            s_lastMsg[i++] = ' ';
            s_lastMsg[i++] = '>';
            s_lastMsg[i++] = ' ';
            {
                int k = 0;
                while (b[k] && i < 90) { s_lastMsg[i++] = b[k++]; }
            }
            s_lastMsg[i] = '\0';
        }
    }
    s_lastMark = (int)id;
}

int TickOrderViolations(void) {
    return s_violations;
}

const char* TickLastViolation(void) {
    return s_lastMsg;
}

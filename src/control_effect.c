/*
 * Canity — 控制效果层实现（v2.4.1 第 4 轮）
 * 头部注释见 include/control_effect.h。
 */
#include "common.h"
#include "control_effect.h"

static ControlType s_type      = CTRL_NONE;
static int         s_mashLeft  = 0;
static int         s_mashNeed  = 0;
static float       s_timeLeft  = 0.0f;
static float       s_immune    = 0.0f;
static int         s_result    = 0;   /* 0 无 / 1 挣脱 / 2 超时 */
static float       s_pressReduce = 0.0f;  /* >0 = 限时模式：每次按键削减多少秒 */
static float       s_minTime     = 0.0f;  /* 限时模式的时间下限 */

/* Live control state, maintained by begin/press/tick/release; not fallback stubs. */
int ControlActive(void) { return s_type != CTRL_NONE; }
ControlType ControlCurrent(void) { return s_type; }
int   ControlMashLeft(void) { return s_mashLeft; }
float ControlTimeLeft(void) { return s_timeLeft; }
float ControlImmune(void)   { return s_immune; }

int ControlBegin(ControlType t, int mashNeeded, float window) {
    if (t == CTRL_NONE) return 0;
    /* ① 单占：已有控制时，后来者直接拒绝 —— 不排队、不叠加。
       排队会让玩家被连续控住，那是"无法操作的连续控制"，
       正是方案里明确要避免的。 */
    if (s_type != CTRL_NONE) return 0;
    /* ② 免疫窗口 */
    if (s_immune > 0.0f) return 0;
    if (mashNeeded < 1) mashNeeded = 1;
    if (window < 0.1f) window = 0.1f;
    s_type     = t;
    s_mashNeed = mashNeeded;
    s_mashLeft = mashNeeded;
    s_timeLeft = window;
    /* 2026-11-06 审计补：**清掉上一次的结算结果**。
       蜘蛛的蛛网从来不读 ControlTakeResult（它不需要），
       于是"挣脱/超时"的结果会一直挂着；
       等下一次抱脸虫进入 JMP_MASH 时，第一帧就会读到这个陈旧结果，
       表现为"刚贴脸就自动挣脱"或"刚贴脸就直接判失败"。 */
    s_result   = 0;
    s_pressReduce = 0.0f;      /* 默认走按次模式 */
    s_minTime     = 0.0f;
    return 1;
}

/* 限时模式下开始一次控制（蜘蛛网用这个） */
int ControlBeginTimed(ControlType t, float window, float pressReduce, float minTime) {
    if (t == CTRL_NONE) return 0;
    if (s_type != CTRL_NONE) return 0;
    if (s_immune > 0.0f) return 0;
    if (window < 0.1f) window = 0.1f;
    if (pressReduce < 0.0f) pressReduce = 0.0f;
    if (minTime < 0.0f) minTime = 0.0f;
    if (minTime > window) minTime = window;
    s_type        = t;
    s_mashNeed    = 0;
    s_mashLeft    = 0;
    s_timeLeft    = window;
    s_result      = 0;
    s_pressReduce = pressReduce;
    s_minTime     = minTime;
    return 1;
}

void ControlTick(float dt) {
    /* 免疫窗口在任何状态下都要走完，否则"释放后免疫"会永远不结束 */
    if (s_immune > 0.0f) {
        s_immune -= dt;
        if (s_immune < 0.0f) s_immune = 0.0f;
    }
    if (s_type == CTRL_NONE) return;
    s_timeLeft -= dt;
    if (s_timeLeft < 0.0f) s_timeLeft = 0.0f;
}

int ControlPress(void) {
    if (s_type == CTRL_NONE) return 0;

    /* 限时模式：每按一次削掉 pressReduce 秒，但**不低于 minTime** */
    if (s_pressReduce > 0.0f) {
        /* ==================================================================
         * 2026-11-06（用户实测 BUG）：**到下限之后按键必须无效**
         * ------------------------------------------------------------------
         * 用户原话：「倒计时剩 0.4 / 0.5 的时候，我再戳，它又变回一秒钟了，
         *           再戳又变回一秒钟……到一秒钟的时候，直接无视我的空格键，
         *           不然它每次都会重置」。
         *
         * 根因：自然倒计时（ControlTick）会照常把时间减到下限**以下**，
         * 而这里按键后又把结果钳回 minTime —— 于是每次按都把它**抬回 1.0**，
         * 表现就是"越戳越回到 1 秒"。
         *
         * 现在：已经在下限（或更低）时，按键**完全无效**，
         * 让自然倒计时自己走到 0 再解除。
         * ================================================================ */
        if (s_timeLeft <= s_minTime + 0.001f) return 0;   /* 下限处：无视按键 */

        s_timeLeft -= s_pressReduce;
        if (s_timeLeft < s_minTime) s_timeLeft = s_minTime;
        if (s_timeLeft <= 0.0f) {
            ControlRelease(1);
            s_result = 1;
            return 1;
        }
        return 0;
    }

    /* ③ 按次计数。调用方用的是 IsActionPressed（边沿），
       所以按住不放不会因为键盘重复而自动完成 —— 这是本层的契约，
       实现上只负责"减一"，不读硬件。 */
    s_mashLeft--;
    if (s_mashLeft <= 0) {
        ControlRelease(1);
        s_result = 1;      /* 挣脱成功，等持有者取走 */
        return 1;
    }
    return 0;
}

void ControlRelease(int escaped) {
    (void)escaped;
    s_type     = CTRL_NONE;
    s_mashLeft = 0;
    s_mashNeed = 0;
    s_timeLeft = 0.0f;
    /* ④ 释放后免疫：成功与超时都给，避免"刚挣脱又被同一只抓住" */
    s_immune   = CONTROL_IMMUNE_TIME;
}

void ControlReset(void) {
    s_result   = 0;
    s_pressReduce = 0.0f;
    s_minTime     = 0.0f;
    s_type     = CTRL_NONE;
    s_mashLeft = 0;
    s_mashNeed = 0;
    s_timeLeft = 0.0f;
    s_immune   = 0.0f;
}


/* 取得一次结算结果（读走即清零）。0 = 无，1 = 挣脱，2 = 超时。 */
int ControlTakeResult(void) {
    int r = s_result;
    s_result = 0;
    return r;
}

/* 超时：由 Update 里的统一处理点调用 */
void ControlTimeout(void) {
    if (s_type == CTRL_NONE) return;
    ControlRelease(0);
    s_result = 2;
}

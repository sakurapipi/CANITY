/* src/content/content_monsters.c —— 怪物静态数据（2026-11-09，第 4 轮）
 *
 * ## 这个文件是什么
 *
 * 这是 `content/` 层的第一块内容：**只放数据，不放逻辑**。
 *
 * 之前这张表住在 src/bestiary.c 里 —— 那个文件既管数据、又管
 * "遇见/逃脱/被抓"的进度和三阶揭示的渲染判断，两件事混在一起。
 *
 * 现在：
 *   content_monsters.c  数据（这张表）
 *   bestiary.c          逻辑（揭示阶段 / 进度 / 查询）
 *
 * ## 加一只怪要改哪里
 *
 *   1. 这里加一行（名字 / 标签 / 三个函数指针 / 文案）
 *   2. BESTIARY_COUNT +1（include/bestiary.h）
 *   3. P3 里写它的 AI（感知方式 + 状态机）—— 这步没法自动化
 *
 * 第 2 步将来由 tools/content_gen.py 自动改。
 */
#include "common.h"
#include "bestiary.h"
#include "entity.h"
#include "mimic.h"

/* 取值器：表里的函数指针指向它们（数据的一部分） */
static float bstSpeedHunter(void) { return monster.speed; }
static float bstHearHunter(void)  { return 8.0f; }
static float bstChaseHunter(void) { return 14.0f; }
static float bstSpeedLight(void) { return lightBeast.speed; }
static float bstHearLight(void)  { return 0.0f; }
static float bstChaseLight(void) { return 10.0f; }
static float bstSpeedJumper(void) { return 0.0f; }
static float bstHearJumper(void)  { return 3.0f; }
static float bstChaseJumper(void) { return 0.0f; }
static float bstSpeedCollector(void) { extern float CollectorSpeedNow(void); return CollectorSpeedNow(); }
static float bstHearCollector(void)  { return 0.0f; }
static float bstChaseCollector(void) { return 10.0f; }
static float bstSpeedScavenger(void) { extern float ScavengerSpeedNow(void); return ScavengerSpeedNow(); }
static float bstHearScavenger(void)  { return 0.0f; }
static float bstChaseScavenger(void) { return 25.0f; }
static float bstSpeedWarden(void) { extern float WardenChaseSpeedNow(void); return WardenChaseSpeedNow(); }
static float bstHearWarden(void)  { return 4.0f; }
static float bstChaseWarden(void) { return 5.0f; }
static float bstSpeedMimic(void) { return MIM_SPEED_MUL; }static float bstHearMimic(void)  { return 0.0f; }static float bstChaseMimic(void) { return MIM_CHASE_TIME; }


const BestiaryEntry bestiary[BESTIARY_COUNT] = {
    {
        "SOUND HUNTER", "听声鬼", "NOISE", "声音", 0,
        bstSpeedHunter, bstHearHunter, bstChaseHunter,
        "It is blind. It walks to the last sound it heard.",
        "它看不见。它走向最后听到的声音。",
        "Walk quietly, or throw something far away and let it go there.",
        "走轻一点；或者把东西扔远，让它过去。"
    },
    {
        "LIGHT BEAST", "追光鬼", "LIGHT", "光", 0,
        bstSpeedLight, bstHearLight, bstChaseLight,
        "It only sees your flashlight. It is fast.",
        "它只看得见你的手电。很快。",
        "Kill the light and it loses you. So does hiding.",
        "关掉手电它就丢了目标。躲起来也行。"
    },
    {
        "FACE JUMPER", "面相鬼", "QTE", "突袭", 0,
        bstSpeedJumper, bstHearJumper, bstChaseJumper,
        "It waits next to cover and springs when you are close.",
        "它躲在掩体旁边，你靠近就扑出来。",
        "Back off the moment you hear it. Break free when grabbed.",
        "听到它的动静立刻后退。被抓时按提示挣脱。"
    },
    {
        "THE COLLECTOR", "拾荒者", "GOLD", "金币", 0,
        bstSpeedCollector, bstHearCollector, bstChaseCollector,
        "It smells money. Slow, but it never stops.",
        "它闻得到钱。慢，但不停。",
        "Drop gold and it loses interest. Or strike it to take the bounty back.",
        "丢钱它就不追了；或者打它把赃款抢回来。"
    },
    {
        "THE SCAVENGER", "清道夫", "TIME", "时间", 0,
        bstSpeedScavenger, bstHearScavenger, bstChaseScavenger,
        "Stand still too long, or hide twice, and it comes for you.",
        "你站着不动太久，或者躲两次，它就来找你。",
        "It is slow. Keep moving, and never use the same locker twice.",
        "它慢。保持移动，别连续用同一个柜子。"
    },
    {
        "THE WARDEN", "守门人", "ROAD", "道路", 0,
        bstSpeedWarden, bstHearWarden, bstChaseWarden,
        "Once the breaker is on, something patrols the way out.",
        "配电箱通电之后，有东西在出口那条路上巡逻。",
        "It only sees motion. Stand still and it walks past you.",
        "它只对移动有反应。站着不动它会走过去。"
    },
    {
        "MIMIC", "宝箱怪", "TRAP", "陷阱", 0,
        bstSpeedMimic, bstHearMimic, bstChaseMimic,
        "It looks like a crate. It is not. You cannot kill it.",
        "它长得像箱子。它不是。打不死。",
        "Echo reveals it. Wrench flushes it out. Hiding loses it.",
        "回声箭能照出来；扳手能敲出原形；躲起来能甩掉。"
    },
};

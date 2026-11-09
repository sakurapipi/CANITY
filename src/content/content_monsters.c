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
#include "state.h"
#include "warden.h"
#include "bestiary.h"
#include "sprites.h"
#include "entity.h"
#include "mimic.h"
#include "spider.h"

/* 取值器：表里的函数指针指向它们（数据的一部分） */
static float bstSpeedHunter(void) { return Hunter0()->speed; }
static float bstHearHunter(void)  { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 8.0f; }
static float bstChaseHunter(void) { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 14.0f; }
static float bstSpeedLight(void) { return lightBeast.speed; }
static float bstHearLight(void)  { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 0.0f; }
static float bstChaseLight(void) { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 10.0f; }
static float bstSpeedJumper(void) { return SPIDER_MOVE_SPEED; }
static float bstHearJumper(void)  { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 0.0f; }
static float bstChaseJumper(void) { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 0.0f; }
static float bstSpeedCollector(void) {  return CollectorSpeedNow(); }
static float bstHearCollector(void)  { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 0.0f; }
static float bstChaseCollector(void) { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 10.0f; }
static float bstSpeedScavenger(void) {  return ScavengerSpeedNow(); }
static float bstHearScavenger(void)  { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 0.0f; }
static float bstChaseScavenger(void) { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 25.0f; }
static float bstSpeedWarden(void) {  return WardenChaseSpeedNow(); }
static float bstHearWarden(void)  { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 4.0f; }
static float bstChaseWarden(void) { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 5.0f; }
static float bstSpeedMimic(void) { return MIM_SPEED_MUL; }static float bstHearMimic(void)  { /* FIXED_RESULT: Static bestiary catalog value; not an inactive runtime actor. */ return 0.0f; }static float bstChaseMimic(void) { return MIM_CHASE_TIME; }


const BestiaryEntry bestiary[BESTIARY_COUNT] = {
    {
        "SOUND HUNTER", "听声鬼", "NOISE", "声音", SPR_MON_HUNTER,
        bstSpeedHunter, bstHearHunter, bstChaseHunter,
        "It is blind. It walks to the last sound it heard.",
        "它看不见。它走向最后听到的声音。",
        "Walk quietly, or throw something far away and let it go there.",
        "走轻一点；或者把东西扔远，让它过去。"
    },
    {
        "LIGHT BEAST", "追光鬼", "LIGHT", "光", SPR_MON_LIGHT,
        bstSpeedLight, bstHearLight, bstChaseLight,
        "It only sees your flashlight. It is fast.",
        "它只看得见你的手电。很快。",
        "Kill the light and it loses you. So does hiding.",
        "关掉手电它就丢了目标。躲起来也行。"
    },
    {
        "THREAD SPIDER", "缚丝蛛", "WEB", "蛛网", SPR_PROP_SPIDER,
        bstSpeedJumper, bstHearJumper, bstChaseJumper,
        "Raises its forelegs before firing a restraining web. The web causes no direct damage.",
        "抬起前肢后吐出束缚网；蛛网不会直接造成伤害。",
        "Break line of sight during the wind-up. A wrench strike banishes it for 30 seconds.",
        "前摇时绕过墙角；扳手击中后驱散三十秒。"
    },
    {
        "THE COLLECTOR", "拾荒者", "GOLD", "金币", SPR_MON_LEECH,
        bstSpeedCollector, bstHearCollector, bstChaseCollector,
        "Higher floors attract it more often. At most three appearances per run.",
        "越深层越容易出现，每局最多出现三次。",
        "Avoid contact: each appearance steals 20%, 30%, then 40%. Strike it to reclaim the bounty.",
        "接触偷走当前金币20%、30%、40%；扳手击中可夺回赃款。"
    },
    {
        "THE SCAVENGER", "清道夫", "TIME", "时间", SPR_MON_SHADE,
        bstSpeedScavenger, bstHearScavenger, bstChaseScavenger,
        "Three consecutive locker entries or 180 seconds without an encounter summon it.",
        "连续进出藏身点三次，或等待180秒，它就会来找你。",
        "After it leaves, a 35-second cooldown gives you a chance to move on.",
        "它离开后有35秒冷却；利用空档前进，别反复躲藏。"
    },
    {
        "THE WARDEN", "守门人", "ROAD", "道路", SPR_MON_SPIDER,
        bstSpeedWarden, bstHearWarden, bstChaseWarden,
        "Once the breaker is on, something patrols the way out.",
        "配电箱通电之后，有东西在出口那条路上巡逻。",
        "It only sees motion. Stand still and it walks past you.",
        "它只对移动有反应。站着不动它会走过去。"
    },
    {
        "MIMIC", "宝箱怪", "TRAP", "陷阱", SPR_MON_MIMIC,
        bstSpeedMimic, bstHearMimic, bstChaseMimic,
        "It looks like a crate. It is not. You cannot kill it.",
        "它长得像箱子。它不是。打不死。",
        "Echo reveals it. Wrench flushes it out. Hiding loses it.",
        "回声箭能照出来；扳手能敲出原形；躲起来能甩掉。"
    },
};

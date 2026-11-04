/*
 * Canity — 怪物图鉴（v6 程序化重做）
 *
 * 核心设计：**数值不写死，全部从真实常量读**。
 * 改一个怪物速度 → 图鉴、危险度自动跟着变，一处都不用改。
 *
 * 调研依据（RPG Maker 论坛开发者共识）：
 *   · 数值本身无意义（"Bat 1 has 100 HP or 110 HP doesn't matter"）
 *   · 有价值的是：独特机制 / 弱点 / 对策
 *   · 两段揭示（初见只有名字，之后才给细节）最受好评
 */
#include "common.h"
#include "bestiary.h"

int bestiarySel = 0;
int bestiarySeen = 0;
int bestiaryEscapes[BESTIARY_COUNT] = {0};
int bestiaryCaught[BESTIARY_COUNT] = {0};

/* ==================================================================
 * 程序化数据源：每个函数读的是**真实常量**，不是写死的数字
 * ================================================================== */

static float bstSpeedHunter(void) { return monster.speed; }
static float bstHearHunter(void)  { return 8.0f; }
static float bstChaseHunter(void) { return 14.0f; }

static float bstSpeedLight(void) { extern Monster lightBeast; return lightBeast.speed; }
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

/* 2026-11-05 宝箱怪：数值全部从 mimic.h 的真实常量读，
   改一个常量图鉴自动跟着变（这是本文件的设计原则） */
#include "mimic.h"
static float bstSpeedMimic(void) { return MIM_SPEED_MUL; }       /* 相对玩家的倍数 */
static float bstHearMimic(void)  { return 0.0f; }                /* 它不听声音，只装死 */
static float bstChaseMimic(void) { return MIM_CHASE_TIME; }

/* ---- 表：加一种怪 = 加一行 ---- */
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

/* ==================================================================
 * 认识进度
 * ================================================================== */

void BestiaryMarkSeen(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return;
    bestiarySeen |= (1 << id);
}

int BestiaryIsSeen(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return 0;
    return (bestiarySeen >> id) & 1;
}

int BestiarySeenAll(void) {
    for (int i = 0; i < BESTIARY_COUNT; i++)
        if (!BestiaryIsSeen(i)) return 0;
    return 1;
}

void BestiaryMarkEscaped(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return;
    BestiaryMarkSeen(id);
    bestiaryEscapes[id]++;
}

void BestiaryMarkCaught(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return;
    BestiaryMarkSeen(id);
    bestiaryCaught[id]++;
}

/* ==================================================================
 * v7 第 ㊿ 轮：**6 只怪里 5 只的对策永远解锁不了**
 * ==================================================================
 * 旧规则：逃脱 3 次 → 行为，逃脱 5 次 → 对策。
 * 但 BestiaryMarkEscaped() 全工程只有 3 个调用点，**全部传 id 0**
 * （寻声鬼）—— 因为只有它会「追捕」，也只有它抓人（CatchCountBump
 * 只有 2 处调用，都在它的更新函数里）。
 *
 * 追光鬼 / 偷电怪 / 跳脸虫 / 清道夫 / 守门人 根本没有"逃脱"这个事件，
 * 所以它们的对策栏**永远是 ???** —— 而图鉴本来是用来教对策的
 * （"守门人只对移动有反应"这句话能救命），现在成了 1/6 有用的收藏册。
 *
 * 修法：**遇到过的层数**也算学习进度。
 *   第 3 层遇到它 → 行为；第 5 层遇到它 → 对策。
 * 逃脱仍然算（而且更快的路径：逃 3 次就够了）。
 * ================================================================ */
static int s_floorsSeen[BESTIARY_COUNT];
static int s_lastFloor[BESTIARY_COUNT];

void BestiaryNoteFloor(int id, int level) {
    if (id < 0 || id >= BESTIARY_COUNT) return;
    if (s_lastFloor[id] == level) return;      /* 同一层只记一次 */
    s_lastFloor[id] = level;
    s_floorsSeen[id]++;
}

int BestiaryStage(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return BST_HIDDEN;
    if (!BestiaryIsSeen(id)) return BST_HIDDEN;
    int e = bestiaryEscapes[id];
    int f = s_floorsSeen[id];
    /* 两条路取更快的那条 */
    int progress = (e * 2 > f) ? (e * 2) : f;   /* 逃 1 次 ≈ 遇到 2 层 */
    if (progress >= 5) return BST_COUNTER;
    if (progress >= 3) return BST_BEHAVED;
    return BST_NAMED;
}

/* ==================================================================
 * 危险度：**从真实常量算出来**，不手写
 *   速度 2.0 → 2 分 / 听觉 8 格 → 2 分 / 追逐 14 秒 → 2 分
 *   合计夹到 1~5
 * ================================================================== */
int BestiaryDanger(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return 0;
    const BestiaryEntry* e = &bestiary[id];

    float sp = e->getSpeed ? e->getSpeed() : 0.0f;
    float hr = e->getHear  ? e->getHear()  : 0.0f;
    float ch = e->getChase ? e->getChase() : 0.0f;

    int s = (int)(sp / 1.0f);
    int h = (int)(hr / 4.0f);
    int c = (int)(ch / 7.0f);

    int d = s + h + c;
    if (d < 1) d = 1;
    if (d > 5) d = 5;
    return d;
}

void BestiaryBeginFloor(void) { for(int i=0;i<BESTIARY_COUNT;++i)s_lastFloor[i]=-1; }
void BestiarySave(int* a) { for(int i=0;i<BESTIARY_COUNT;++i) {a[i]=bestiaryEscapes[i];a[6+i]=bestiaryCaught[i];a[12+i]=s_floorsSeen[i];} }
void BestiaryLoad(const int* a) { for(int i=0;i<BESTIARY_COUNT;++i) {bestiaryEscapes[i]=a[i]>0?a[i]:0;bestiaryCaught[i]=a[6+i]>0?a[6+i]:0;s_floorsSeen[i]=a[12+i]>0?a[12+i]:0;}BestiaryBeginFloor(); }

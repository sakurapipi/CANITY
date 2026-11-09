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
#include "content_db.h"
#include "entity.h"

int bestiarySel = 0;
int bestiarySeen = 0;
int bestiaryEscapes[BESTIARY_COUNT] = {0};
int bestiaryCaught[BESTIARY_COUNT] = {0};

/* ==================================================================
 * 程序化数据源：每个函数读的是**真实常量**，不是写死的数字
 * ================================================================== */


/* 2026-11-06：追光怪变成数组（lightBeasts[]），旧名字是宏指向 [0]。
   这里原来在函数体里写了 `extern Monster lightBeast;`，
   宏展开后变成 `extern Monster lightBeasts[0];` —— 非法声明。
   改成直接用 entity.h 里的声明即可。 */





/* 2026-11-05 宝箱怪：数值全部从 mimic.h 的真实常量读，
   改一个常量图鉴自动跟着变（这是本文件的设计原则） */
#include "mimic.h"
       /* 相对玩家的倍数 */
                /* 它不听声音，只装死 */

/* ---- 表：加一种怪 = 加一行 ---- */
/* Bestiary queries consume the shared content_monsters table via bestiary.h. */


/* ==================================================================
 * 认识进度
 * ================================================================== */

/* ==================================================================
 * 2026-11-09（第 3 轮）：**图鉴登记进内容中枢**
 * ------------------------------------------------------------------
 * 用英文名当 id（它是字符串常量，地址稳定 —— 中枢只存指针，见
 * content_db.h 第 4 条规矩）。
 *
 * 幂等：ContentAdd 会拒绝重复 id，所以多调几次不会重复计数。
 * ================================================================ */
void BestiaryRegister(void) {
    int i;
    for (i = 0; i < BESTIARY_COUNT; i++) {
        if (bestiary[i].nameEn && bestiary[i].nameEn[0])
            (void)ContentAdd(CK_MONSTER, bestiary[i].nameEn, &bestiary[i]);
    }
}

void BestiaryMarkSeen(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return;
    bestiarySeen |= (1 << id);
}

int BestiaryIsSeen(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return 0;
    return (bestiarySeen >> id) & 1;
}

/* Discovery is silent; this module owns knowledge, not narration. */




int BestiaryIsActive(int id) {
    return id >= 0 && id < BESTIARY_COUNT;
}
int BestiaryActiveCount(void) {
    int n=0;for(int i=0;i<BESTIARY_COUNT;i++)n+=BestiaryIsActive(i);return n;
}
int BestiaryCountSeen(int seenMask) {
    int count = 0;
    for (int id = 0; id < BESTIARY_COUNT; id++)
        if (BestiaryIsActive(id) && ((unsigned)seenMask & (1u << id))) count++;
    return count;
}
int BestiarySeenAll(void) {
    for (int id = 0; id < BESTIARY_COUNT; id++)
        if (BestiaryIsActive(id) && !BestiaryIsSeen(id)) return 0;
    return 1;
}

void BestiaryMarkEscaped(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return;
    BestiaryMarkSeen(id);
    if (bestiaryEscapes[id] < INT_MAX) bestiaryEscapes[id]++;
}

void BestiaryMarkCaught(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return;
    BestiaryMarkSeen(id);
    if (bestiaryCaught[id] < INT_MAX) bestiaryCaught[id]++;
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
    if (s_floorsSeen[id] < INT_MAX) s_floorsSeen[id]++;
}

int BestiaryFloorsSeen(int id) {
    return id >= 0 && id < BESTIARY_COUNT ? s_floorsSeen[id] : 0;
}
int BestiaryStage(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return BST_HIDDEN;
    if (!BestiaryIsSeen(id)) return BST_HIDDEN;
    int e = bestiaryEscapes[id];
    int f = s_floorsSeen[id];
    /* 两条路取更快的那条 */
    int doubledEscapes = e >= 3 ? 6 : e * 2;
    int progress = doubledEscapes > f ? doubledEscapes : f;   /* 逃 1 次 ≈ 遇到 2 层 */
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

void BestiaryBeginFloor(void) {
    BestiaryRegister();   /* 第 3 轮：保证图鉴已登记进中枢 */ for(int i=0;i<BESTIARY_COUNT;++i)s_lastFloor[i]=-1; }



typedef char bestiary_storage_capacity_check[(BESTIARY_COUNT == BESTIARY_LEGACY_COUNT + 1) ? 1 : -1];

void BestiarySave(int* a) {
    if (!a) return;
    for (int i = 0; i < BESTIARY_LEGACY_COUNT; i++) {
        a[i] = bestiaryEscapes[i];
        a[BESTIARY_LEGACY_COUNT+i] = bestiaryCaught[i];
        a[2*BESTIARY_LEGACY_COUNT+i] = s_floorsSeen[i];
    }
}
void BestiaryLoad(const int* a) {
    memset(bestiaryEscapes, 0, sizeof(bestiaryEscapes));
    memset(bestiaryCaught, 0, sizeof(bestiaryCaught));
    memset(s_floorsSeen, 0, sizeof(s_floorsSeen));
    if (a) for (int i = 0; i < BESTIARY_LEGACY_COUNT; i++) {
        bestiaryEscapes[i] = a[i] > 0 ? a[i] : 0;
        bestiaryCaught[i] = a[BESTIARY_LEGACY_COUNT+i] > 0 ? a[BESTIARY_LEGACY_COUNT+i] : 0;
        s_floorsSeen[i] = a[2*BESTIARY_LEGACY_COUNT+i] > 0 ? a[2*BESTIARY_LEGACY_COUNT+i] : 0;
    }
    BestiaryBeginFloor();
}
void BestiarySaveExtra(int* a) {
    if (!a) return;
    a[0] = bestiaryEscapes[BST_ID_MIMIC];
    a[1] = bestiaryCaught[BST_ID_MIMIC];
    a[2] = s_floorsSeen[BST_ID_MIMIC];
}
void BestiaryLoadExtra(const int* a) {
    bestiaryEscapes[BST_ID_MIMIC] = a && a[0] > 0 ? a[0] : 0;
    bestiaryCaught[BST_ID_MIMIC] = a && a[1] > 0 ? a[1] : 0;
    s_floorsSeen[BST_ID_MIMIC] = a && a[2] > 0 ? a[2] : 0;
}

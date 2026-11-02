/*
 * Canity — 可疑点系统（v7 新模块）
 *
 * 为什么需要它
 * ─────────────
 * 旧做法是 MakeNoise() 直接改 monster.state / tx / ty。
 * 问题：**所有噪音源抢同一个字段，谁后写谁赢**。
 *      · 玩家边走边搜，噪音不断刷新目标 → 玩偶的叫声刚设好就被顶掉
 *      · 这就是「发出声音的玩偶不能吸引听声怪」的根源
 *      · 而且每加一只新怪就要改一次 MakeNoise，越加越乱
 *
 * 新做法：噪音 → 写进这张表；怪 → 自己从表里挑。
 * 多个噪音源可以**共存**，怪按 strength 自己选最强的那个。
 *
 * 加新怪时只需要写「它读哪种 source」，不用碰 MakeNoise。
 */
#include "state.h"   /* v8: 走统一的全局状态入口 */
#include "common.h"
#include "suspicion.h"

SuspicionPoint susp[SUSP_MAX];
int suspWriteCursor = 0;

void SuspicionReset(void) {
    for (int i = 0; i < SUSP_MAX; i++) {
        susp[i].x = 0.0f;
        susp[i].y = 0.0f;
        susp[i].strength = 0.0f;
        susp[i].decay = 0.0f;
        susp[i].ttl = 0.0f;
        susp[i].source = -1;
    }
    suspWriteCursor = 0;
}

/*
 * 写一个可疑点。
 * 同源且距离很近的点会**合并**（取更高强度、更长寿），
 * 避免玩家连续走路时把 12 个槽位瞬间塞满。
 */
void SuspicionAdd(float x, float y, float strength, float decay, float ttl, int source) {
    if (strength <= 0.0f || ttl <= 0.0f) return;

    /* ① 先找同源且 2 格内的旧点，合并 */
    for (int i = 0; i < SUSP_MAX; i++) {
        if (susp[i].source != source || susp[i].strength <= 0.0f) continue;
        float dx = susp[i].x - x, dy = susp[i].y - y;
        if (dx * dx + dy * dy > 4.0f) continue;
        susp[i].x = (susp[i].x + x) * 0.5f;
        susp[i].y = (susp[i].y + y) * 0.5f;
        if (strength > susp[i].strength) susp[i].strength = strength;
        if (ttl > susp[i].ttl) susp[i].ttl = ttl;
        if (decay < susp[i].decay) susp[i].decay = decay;
        return;
    }

    /* ② 找空槽 */
    int slot = -1;
    for (int i = 0; i < SUSP_MAX; i++) {
        if (susp[i].strength <= 0.0f) { slot = i; break; }
    }
    /* ③ 没空槽 → 顶掉最弱的那条（环形游标保证不会总顶同一个） */
    if (slot < 0) {
        int weakest = 0;
        for (int i = 1; i < SUSP_MAX; i++)
            if (susp[i].strength < susp[weakest].strength) weakest = i;
        if (susp[weakest].strength > strength) return;   /* 新的比最弱的还弱，丢掉 */
        slot = weakest;
    }

    susp[slot].x = x;
    susp[slot].y = y;
    susp[slot].strength = strength;
    susp[slot].decay = decay;
    susp[slot].ttl = ttl;
    susp[slot].source = source;
}

/*
 * 选一个最值得去的点。
 *   计分 = strength - 距离 * distPenalty
 * 这样「很近的弱线索」和「很远的强线索」都能被选中，
 * 而不是无脑选最强的那个（那样怪会一直跑来跑去）。
 */
int SuspicionBest(float fromX, float fromY, float distPenalty) {
    int best = -1;
    float bestScore = 0.0f;
    for (int i = 0; i < SUSP_MAX; i++) {
        if (susp[i].strength <= 0.0f) continue;
        float dx = susp[i].x - fromX, dy = susp[i].y - fromY;
        float d = sqrtf(dx * dx + dy * dy);
        float score = susp[i].strength - d * distPenalty;
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

/* 某个位置的探索点被「消费」了（怪到了，或玩家把它打散） */
void SuspicionConsume(int idx) {
    if (idx < 0 || idx >= SUSP_MAX) return;
    susp[idx].strength = 0.0f;
    susp[idx].ttl = 0.0f;
    susp[idx].source = -1;
}

/* 把某个坐标附近的所有点清掉（扳手打散回声用） */
int SuspicionClearNear(float x, float y, float radius) {
    int n = 0;
    float r2 = radius * radius;
    for (int i = 0; i < SUSP_MAX; i++) {
        if (susp[i].strength <= 0.0f) continue;
        float dx = susp[i].x - x, dy = susp[i].y - y;
        if (dx * dx + dy * dy > r2) continue;
        SuspicionConsume(i);
        n++;
    }
    return n;
}

void SuspicionTick(float dt) {
    for (int i = 0; i < SUSP_MAX; i++) {
        if (susp[i].strength <= 0.0f) continue;
        susp[i].ttl -= dt;
        susp[i].strength -= susp[i].decay * dt;
        if (susp[i].ttl <= 0.0f || susp[i].strength <= 0.0f) {
            susp[i].strength = 0.0f;
            susp[i].ttl = 0.0f;
            susp[i].source = -1;
        }
    }
}

/* 最强制高点（给 HUD / 调试用） */
int SuspicionStrongest(void) {
    int best = -1;
    float s = 0.0f;
    for (int i = 0; i < SUSP_MAX; i++) {
        if (susp[i].strength > s) { s = susp[i].strength; best = i; }
    }
    return best;
}

int SuspicionActiveCount(void) {
    int n = 0;
    for (int i = 0; i < SUSP_MAX; i++)
        if (susp[i].strength > 0.0f) n++;
    return n;
}

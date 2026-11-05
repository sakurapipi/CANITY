/* ==================================================================
 * 舱门系统 —— 数据与状态机
 * ------------------------------------------------------------------
 * 这个文件**不认识**金币/电量/理智/保险丝。
 * 门槛判定在 game.c 的 HatchGateCheck()，效果结算在 HatchFireEffect()。
 *
 * 为什么这样切：
 *   ① 状态机能被单测单独跑（不需要整局状态）
 *   ② 门槛文案要读实时数值（"你有 87 金"），那些变量只在 game.c
 *   ③ 效果里有十几个玩法全局，塞进来会变成第二个 game.c
 * ================================================================ */

#include "common.h"
#include "hatch.h"

/* 系统头统一由 common.h 提供（partition 检查要求） */

Hatch hatches[MAX_HATCHES];
int   hatchCount = 0;

void HatchReset(void) {
    memset(hatches, 0, sizeof(hatches));
    hatchCount = 0;
}

int HatchAdd(float x, float y, int kind, int themeId) {
    if (hatchCount >= MAX_HATCHES) return -1;
    Hatch* h = &hatches[hatchCount];
    h->x = x;
    h->y = y;
    h->active = 1;
    h->kind = kind;
    h->state = HATCH_SEALED;
    h->themeId = themeId;
    h->animT = 0.0f;
    return hatchCount++;
}

/* 唯一的状态写入口 —— 所有转移都走这里，方便日后加断言/日志。 */
void HatchSetState(int idx, int state) {
    if (idx < 0 || idx >= hatchCount) return;
    Hatch* h = &hatches[idx];
    if (!h->active) return;
    /* 终态不可回退：FIRED 之后谁也不能把它变回 SEALED，
       否则"已经用过的门"会被重复触发（这正是一类经典刷子漏洞）。 */
    if (h->state == HATCH_FIRED && state != HATCH_FIRED) return;
    h->state = state;
    if (state == HATCH_OPENING) h->animT = 0.0f;
}

float HatchOpen01(int idx) {
    if (idx < 0 || idx >= hatchCount) return 0.0f;
    const Hatch* h = &hatches[idx];
    if (h->state == HATCH_SEALED) return 0.0f;
    if (h->state != HATCH_OPENING) return 1.0f;
    float t = h->animT / HATCH_OPEN_SECONDS;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    /* 缓动：先快后慢，机械门读感（linear 会显得像贴图在滑） */
    return 1.0f - (1.0f - t) * (1.0f - t);
}

void HatchTick(float dt) {
    for (int i = 0; i < hatchCount; i++) {
        Hatch* h = &hatches[i];
        if (!h->active || h->state != HATCH_OPENING) continue;
        h->animT += dt;
        if (h->animT >= HATCH_OPEN_SECONDS) {
            /* ---- AD-27：动画播完的**这一帧**才结算效果 ---- */
            h->animT = HATCH_OPEN_SECONDS;
            HatchSetState(i, HATCH_OPEN);
            HatchFireEffect(i);
            HatchSetState(i, HATCH_FIRED);
        }
    }
}

int HatchNearest(float px, float py, float maxD2) {
    int best = -1;
    float bestD = maxD2;
    for (int i = 0; i < hatchCount; i++) {
        const Hatch* h = &hatches[i];
        if (!h->active) continue;
        if (h->state == HATCH_OPENING || h->state == HATCH_FIRED) continue;
        float dx = h->x - px, dy = h->y - py;
        float d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

/* 主题 → 舱门种类。
   7 种主题一种一门；没配到的主题返回 -1（那就不放门）。 */
int HatchKindForTheme(int themeId) {
    switch (themeId) {
    case 5: return HATCH_BOILER;    /* 锈工厂 */
    case 1: return HATCH_STORAGE;   /* 监狱 */
    case 4: return HATCH_POWER;     /* 冰窖 */
    case 3: return HATCH_WARD;      /* 病房 */
    case 0: return HATCH_BRICK;     /* 砖墙 */
    case 2: return HATCH_HELL;      /* 地狱 */
    case 6: return HATCH_FLESH;     /* 血肉洞窟 */
    default: return -1;
    }
}


/* ==================================================================
 * 已用状态的存取（v33）
 * ------------------------------------------------------------------
 * 位索引 = HatchKind（0..6），一扇门一位。
 * 为什么不是"每扇门一位"：读档后重放出来的门顺序可能和存盘时不同
 * （取决于房间放置顺序），按**种类**记才稳定。
 * ================================================================ */
int HatchUsedMask(void) {
    int mask = 0;
    for (int i = 0; i < hatchCount; i++) {
        const Hatch* h = &hatches[i];
        if (!h->active) continue;
        if (h->state != HATCH_FIRED) continue;
        if (h->kind >= 0 && h->kind < 31) mask |= (1 << h->kind);
    }
    return mask;
}

void HatchApplyUsedMask(int mask) {
    if (mask == 0) return;
    for (int i = 0; i < hatchCount; i++) {
        Hatch* h = &hatches[i];
        if (!h->active) continue;
        if (h->kind < 0 || h->kind >= 31) continue;
        if (mask & (1 << h->kind)) {
            /* 直接落终态：不播动画、不给第二次机会 */
            h->state = HATCH_FIRED;
            h->animT = HATCH_OPEN_SECONDS;
        }
    }
}

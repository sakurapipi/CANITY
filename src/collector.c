/*
 * Canity — THE COLLECTOR 拾荒者（v7 新怪）
 *
 * 设计目标（见 方案 v7 第六部分 ④）
 * ────────────────────────────────
 * 它不是「随机扣理智」的怪，而是**看得见的经济掠夺**：
 *   · 只认钱 —— 地上的钱袋优先，其次是你身上的钱
 *   · 抓到你不会打你，而是**拿走携带金币的 10%** 然后转身走
 *   · 它慢（1.6 格/秒 < 玩家走速 2.0），永远走得掉
 *   · 用扳手击退它 **两次**（6 秒内）可以把赃款打掉，走过去按 E 捡回
 *
 * 为什么这样设计
 *   把「携带金币越多怪越强」从一个看不见的乘数，
 *   变成一个有实体、有反馈、可以博弈的东西。
 *   玩家能算出「丢 20% 换 30 秒安全」值不值。
 */
#include <math.h>
#include "state.h"   /* v8: 走统一的全局状态入口 */
#include "common.h"
#include "game.h"
#include "entity.h"
#include "audio.h"
#include "lang.h"

#define COLLECTOR_SPEED      1.6f    /* 格/秒，慢于玩家走速 2.0 */
#define COLLECTOR_STEAL_PCT  10      /* 拿走携带金币的 10% */
#define COLLECTOR_FLEE_TIME  10.0f   /* 拿到钱后逃跑多久（之后带钱消失） */
#define COLLECTOR_HITS_TO_DROP 2     /* 打两次掉赃款 */
#define COLLECTOR_HIT_WINDOW 6.0f    /* 两次之间不超过 6 秒 */
#define COLLECTOR_TOUCH      0.70f   /* 接触半径 */

typedef struct {
    int   active;
    float x, y;
    int   bounty;      /* 身上带的赃款 */
    int   hits;        /* 已挨了几下（到 2 掉钱） */
    float hitTimer;    /* 距离上次挨打的秒数 */
    float fleeTimer;   /* >0 = 正在带钱逃跑 */
} Collector;

Collector collector;

/* game.c 里的钱袋接口 */
extern float MoneyBagX(int), MoneyBagY(int), MoneyBagLife(int);
extern int   MoneyBagAmount(int);
extern void  MoneyBagTake(int idx);          /* 拿走（吃掉）某个钱袋 */
extern void  MoneyBagDropAt(float x, float y, int amount);   /* 在指定位置放一个钱袋 */

int   CollectorActive(void) { return collector.active; }
float CollectorX(void)      { return collector.x; }
float CollectorY(void)      { return collector.y; }
int   CollectorBounty(void) { return collector.bounty; }

void CollectorReset(void) {
    collector.active = 0;
    collector.x = 0.0f;
    collector.y = 0.0f;
    collector.bounty = 0;
    collector.hits = 0;
    collector.hitTimer = 0.0f;
    collector.fleeTimer = 0.0f;
}

/* 在离玩家 8~14 格的空地上生成 */
static int findSpotFar(float* ox, float* oy) {
    for (int t = 0; t < 100; t++) {
        float ang = (float)(rand() % 6283) / 1000.0f;
        float d = 8.0f + (float)(rand() % 600) / 100.0f;
        float x = posX + cosf(ang) * d;
        float y = posY + sinf(ang) * d;
        if (x < 1.5f || y < 1.5f || x > MAP_WIDTH - 1.5f || y > MAP_HEIGHT - 1.5f) continue;
        if (Blocked(x, y)) continue;
        *ox = x;
        *oy = y;
        return 1;
    }
    return 0;
}

/* 只在强度档 T7 以上（携带 ≥ 46000）才有资格出现 */
void CollectorSpawnIfEligible(void) {
    extern int carryGold;
    extern int GoldTier(void);
    CollectorReset();
    if (carryGold < 46000) return;      /* 钱少的时候它不该出现 */
    if (GoldTier() < 7) return;
    float x, y;
    if (!findSpotFar(&x, &y)) return;
    collector.x = x;
    collector.y = y;
    collector.active = 1;
    collector.bounty = 0;
    collector.hits = 0;
    collector.hitTimer = 0.0f;
    collector.fleeTimer = 0.0f;
}

/* 被扳手击退一次（由 game.c 的击退代码调用） */
void CollectorHitByWrench(float hx, float hy) {
    if (!collector.active) return;
    float dx = collector.x - hx, dy = collector.y - hy;
    if (dx * dx + dy * dy > 4.0f) return;     /* 2 格内才算打中 */

    /* 两次挨打间隔太久 → 计数清零 */
    if (collector.hitTimer > COLLECTOR_HIT_WINDOW) collector.hits = 0;
    collector.hitTimer = 0.0f;
    collector.hits++;

    if (collector.hits >= COLLECTOR_HITS_TO_DROP && collector.bounty > 0) {
        /* 赃款被打掉，掉在地上变成钱袋 */
        MoneyBagDropAt(collector.x, collector.y, collector.bounty);
        collector.bounty = 0;
        collector.hits = 0;
        PlaySfx(SFX_GLASS, 0.85f);
        pickupMsg = (g_lang == LANG_ZH) ? "赃款抢回来了！" : "BOUNTY RECOVERED";
        pickupMsgTimer = 1.6f;
    }
}

void UpdateCollector(float dt) {
    /* v2.3.9：训练营里当静态标本，不跑 AI */
    { extern int trainingMode; if (trainingMode) return; }
    if (!collector.active) return;

    collector.hitTimer += dt;

    /* ① 带钱逃跑：远离玩家，计时结束就消失（钱真的没了） */
    if (collector.fleeTimer > 0.0f) {
        collector.fleeTimer -= dt;
        if (collector.fleeTimer <= 0.0f) {
            collector.active = 0;           /* 带着赃款走了 */
            return;
        }
        float dx = collector.x - posX, dy = collector.y - posY;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > 0.001f) {
            float nx = collector.x + (dx / d) * COLLECTOR_SPEED * dt;
            float ny = collector.y + (dy / d) * COLLECTOR_SPEED * dt;
            if (!Blocked(nx, ny)) { collector.x = nx; collector.y = ny; }
        }
        return;
    }

    /* ② 挑目标：钱袋优先（最近的那个），否则朝玩家 */
    float tx = posX, ty = posY;
    int bagIdx = -1;
    float bestD = 1e9f;
    for (int i = 0; i < MAX_MONEYBAG; i++) {
        if (MoneyBagAmount(i) <= 0) continue;
        float dx = MoneyBagX(i) - collector.x, dy = MoneyBagY(i) - collector.y;
        float d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; bagIdx = i; }
    }
    if (bagIdx >= 0) { tx = MoneyBagX(bagIdx); ty = MoneyBagY(bagIdx); }

    /* ③ 移动 */
    float dx = tx - collector.x, dy = ty - collector.y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d > 0.35f) {
        float nx = collector.x + (dx / d) * COLLECTOR_SPEED * dt;
        float ny = collector.y + (dy / d) * COLLECTOR_SPEED * dt;
        if (!Blocked(nx, ny)) { collector.x = nx; collector.y = ny; }
    }

    /* ④ 走到钱袋 → 吃掉它（钱真的没了） */
    if (bagIdx >= 0 && d < 0.7f) {
        int amt = MoneyBagAmount(bagIdx);
        MoneyBagTake(bagIdx);
        collector.bounty += amt;
        collector.fleeTimer = COLLECTOR_FLEE_TIME;
        PlaySfx(SFX_GLASS, 0.9f);
        pickupMsg = (g_lang == LANG_ZH) ? "它捡走了钱袋" : "IT TOOK THE BAG";
        pickupMsgTimer = 1.6f;
        return;
    }

    /* ⑤ 摸到玩家 → 拿走携带金币的 10%，转身走 */
    dx = posX - collector.x; dy = posY - collector.y;
    d = sqrtf(dx * dx + dy * dy);
    if (d < COLLECTOR_TOUCH) {
        extern int carryGold;
        int steal = carryGold * COLLECTOR_STEAL_PCT / 100;
        if (steal > 0) {
            carryGold -= steal;
            if (carryGold < 0) carryGold = 0;
            collector.bounty += steal;
            AddHitFlash(0.55f);
            AddShake(0.18f, 0.14f);
            PlaySfx(SFX_HEART, 0.8f);
            pickupMsg = (g_lang == LANG_ZH) ? "它拿走了你的钱！" : "IT TOOK YOUR GOLD!";
            pickupMsgTimer = 1.8f;
        }
        collector.fleeTimer = COLLECTOR_FLEE_TIME;
        /* 弹开一点，避免连续每帧触发 */
        collector.x -= dx * 2.0f;
        collector.y -= dy * 2.0f;
        if (Blocked(collector.x, collector.y)) {
            float ox, oy;
            if (findSpotFar(&ox, &oy)) { collector.x = ox; collector.y = oy; }
        }
    }
}

/* v2.3.9：训练营用 —— 强制把Collector摆到指定位置当标本 */
void CollectorForcePlace(float x, float y) {
    collector.active = 1;
    collector.x = x;
    collector.y = y;
}

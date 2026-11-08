/*
 * Canity — 钱袋系统（v8，从 game.c 切出来的第一个独立区块）
 *
 * 只依赖三样：include/state.h 的全局状态、ActionIsDown() 按键查询、
 * SuspicionAdd() 可疑点表。这就是区块化的样本：
 * 一个功能 = 一个文件 + 只走公开接口。
 */
#include "common.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "state.h"
#include "suspicion.h"
#include "audio.h"
#include "lang.h"

/* ===== v7 **钱袋系统** =====
   按住 K 弹滑条 → 松开丢出。
   金额越大，写进可疑点表的强度越高 → 怪物越容易被引过去。
   走过去按 E 可以捡回来（45 秒内）。
   关键：**可逆**。没被吃掉就能捡回，玩家才愿意赌。 */
typedef struct {
    float x, y;
    int   amount;
    float life;
    int   active;
} MoneyBag;
static MoneyBag moneyBags[MAX_MONEYBAG];
/* v7：清道夫的触发条件用的两个计数器 */
float floorTime  = 0.0f;   /* 本层已停留秒数 */
int   hideStreak = 0;      /* 连续躲了几次藏身点（搜箱会清零） */
int  moneyDropOpen = 0;     /* 滑条是否展开 */
int  moneyDropPct  = 25;    /* 10 ~ 100，每档 10 */
static int moneyDropPrev = 0;

/* 给渲染层用的只读接口 */
float MoneyBagX(int i)      { return (i >= 0 && i < MAX_MONEYBAG) ? moneyBags[i].x : 0.0f; }
float MoneyBagY(int i)      { return (i >= 0 && i < MAX_MONEYBAG) ? moneyBags[i].y : 0.0f; }
float MoneyBagLife(int i)   { return (i >= 0 && i < MAX_MONEYBAG) ? moneyBags[i].life : 0.0f; }
int   MoneyBagAmount(int i) { return (i >= 0 && i < MAX_MONEYBAG && moneyBags[i].active) ? moneyBags[i].amount : 0; }

/* 给拾荒者用：吃掉某个钱袋 / 在指定位置放一个 */
void MoneyBagTake(int idx) {
    if (idx < 0 || idx >= MAX_MONEYBAG) return;
    SuspicionClearNear(moneyBags[idx].x, moneyBags[idx].y, 1.0f);
    moneyBags[idx].active = 0;
}
void MoneyBagDropAt(float x, float y, int amount) {
    if (amount <= 0) return;
    int slot = -1;
    for (int i = 0; i < MAX_MONEYBAG; i++) if (!moneyBags[i].active) { slot = i; break; }
    if (slot < 0) slot = 0;
    moneyBags[slot].x = x; moneyBags[slot].y = y;
    moneyBags[slot].amount = amount;
    moneyBags[slot].life = 45.0f;
    moneyBags[slot].active = 1;
    {   float st = (float)amount / 1000.0f * 5.0f;
        if (st > 100.0f) st = 100.0f;
        if (st < 5.0f) st = 5.0f;
        SuspicionAdd(x, y, st, 0.6f, 45.0f, SUSP_GOLD); }
}

void MoneyBagsResetForNewFloor(void) {

    for (int i = 0; i < MAX_MONEYBAG; i++) moneyBags[i].active = 0;
    moneyDropOpen = 0;
}

/* ===== v2.4.91 **脚下有没有钱袋** =====
   给 HUD 用：返回玩家附近（1.6 格内）最近那个钱袋的金额，
   没有就返回 0。UI 靠这个决定要不要显示「按 E 拾取」。 */
int MoneyBagNearestToPlayer(void) {
    int best = 0;
    float bestD = 1.6f * 1.6f;
    for (int i = 0; i < MAX_MONEYBAG; i++) {
        if (!moneyBags[i].active) continue;
        float dx = posX - moneyBags[i].x;
        float dy = posY - moneyBags[i].y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestD) { bestD = d2; best = moneyBags[i].amount; }
    }
    return best;
}

void MoneyTick(float dt) {
    {
        int kNow = (currentState == STATE_PLAYING) ? ActionIsDown(ACT_DROP_GOLD) : 0;
        if (currentState == STATE_PLAYING) {
            if (kNow && !moneyDropPrev && carryGold >= 100) {
                moneyDropOpen = 1;
            }
            if (moneyDropOpen) {
                /* ← → 调整比例（不用 A/D，那两个是走路） */
                if (keyPressed[VK_LEFT]) {
                    keyPressed[VK_LEFT] = 0;
                    moneyDropPct -= 10; if (moneyDropPct < 10) moneyDropPct = 100;
                }
                if (keyPressed[VK_RIGHT]) {
                    keyPressed[VK_RIGHT] = 0;
                    moneyDropPct += 10; if (moneyDropPct > 100) moneyDropPct = 10;
                }
                if (!kNow) {
                    /* 松手 → 真的丢 */
                    int amt = (int)((long long)carryGold * moneyDropPct / 100);
                    if (amt < 1) amt = 0;
                    if (amt > 0) {
                        int slot = -1;
                        for (int mi = 0; mi < MAX_MONEYBAG; mi++)
                            if (!moneyBags[mi].active) { slot = mi; break; }
                        if (slot < 0) {
                            float worst = 1e9f;
                            for (int mi = 0; mi < MAX_MONEYBAG; mi++) {
                                float dl = moneyBags[mi].life;
                                if (dl < worst) { worst = dl; slot = mi; }
                            }
                        }
                        if (slot >= 0) {
                            float bx = posX + dirX * 1.5f;
                            float by = posY + dirY * 1.5f;
                            if (Blocked(bx, by)) { bx = posX; by = posY; }
                            moneyBags[slot].x = bx;
                            moneyBags[slot].y = by;
                            moneyBags[slot].amount = amt;
                            moneyBags[slot].life = 45.0f;
                            moneyBags[slot].active = 1;
                            { extern int WalletSpendRun(int); WalletSpendRun(amt); }
                            if (carryGold < 0) carryGold = 0;
                            /* 吸引强度 = 金额 / 1000 × 5，封顶 100
                               丢 2000 → 10；丢 20000 → 100（比玩偶的 75 还高） */
                            {
                                float st = (float)amt / 1000.0f * 5.0f;
                                if (st > 100.0f) st = 100.0f;
                                if (st < 5.0f)   st = 5.0f;
                                SuspicionAdd(bx, by, st, 0.6f, 45.0f, SUSP_GOLD);
                            }
                            PlaySfx(SFX_PICKUP, 0.55f);
                        }
                    }
                    moneyDropOpen = 0;
                }
            }
        } else {
            moneyDropOpen = 0;
        }
        moneyDropPrev = kNow;
    }

    for (int mi = 0; mi < MAX_MONEYBAG; mi++) {
        if (!moneyBags[mi].active) continue;
        moneyBags[mi].life -= dt;
        if (moneyBags[mi].life <= 0.0f) { moneyBags[mi].active = 0; continue; }
        float bdx = posX - moneyBags[mi].x, bdy = posY - moneyBags[mi].y;
        if (bdx * bdx + bdy * bdy < 2.25f && keyPressed['E']) {
            keyPressed['E'] = 0;
            carryGold += moneyBags[mi].amount;
            SuspicionClearNear(moneyBags[mi].x, moneyBags[mi].y, 1.0f);
            moneyBags[mi].active = 0;
            PlaySfx(SFX_POWER, 0.7f);
            pickupMsg = (g_lang == LANG_ZH) ? "钱袋已收回" : "BAG RECOVERED";
            pickupMsgTimer = 1.2f;
        }
    }

}

/* Wrench charge -> swing -> contact; private per-swing and key state. */
#include "common.h"
#include "tutorial.h"
#include "lockbox.h"
#include "wrench.h"
#include "entity.h"
#include "state.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "loop_api.h"
#include "plat.h"
#include "daily.h"
#include "difficulty.h"
#include "upgrade_run.h"
#include "bench.h"
#include "skills.h"
#include "audio.h"
#include "spider.h"
#include "mimic.h"
#include "hints.h"
#include "lang.h"

#define WRENCH_SWING_FRAMES 5
#define WRENCH_HIT_FRAME 2

float wrenchSwingT = 0.0f;      /* 剩余时间 */
static float wrenchSwingPower = 1.0f;  /* 这次挥击的力度 */
static int wrenchSwingHit = 0;       /* 已经判定过了吗 */

static float WrenchSwingProgress(void) {
    if (wrenchSwingT <= 0.0f) return 1.0f;
    return 1.0f - wrenchSwingT / (WRENCH_SWING_TIME * DiffWrenchWindowMul());
}

/* 当前第几帧（0..4） */
static int WrenchSwingFrame(void) {
    float p = WrenchSwingProgress();
    int f = (int)(p * (float)WRENCH_SWING_FRAMES);
    if (f < 0) f = 0;
    if (f >= WRENCH_SWING_FRAMES) f = WRENCH_SWING_FRAMES - 1;
    return f;
}

/* ---- 真正结算击退（在命中帧调用一次）---- */
static unsigned int wrenchHitMask=0;
static void WrenchApplyHit(float power) {

    /* ============================================================
     * 修：**扳手打不到第 2、3 只听声鬼**
     * ------------------------------------------------------------
     * 原来这里是写死的两只：{&monster, &lightBeast}。
     * 听声鬼扩到三只之后，第 2、3 只根本不在列表里 ——
     * 玩家砸到它们身上毫无反应，也击退不了。
     *
     * 位掩码：1=第1只 2=追光鬼 8=第2只 16=第3只
     * （4 留给清道夫，别动）
     * ============================================================ */
    /* 2026-11-06：追光怪 +1 只，击退目标从 4 个扩到 5 个 */
    Monster* targets[5] = { Hunter0(), &lightBeast, Hunter1(), Hunter2(), &lightBeast2 };
    static const unsigned int tbits[5] = { 1u, 2u, 8u, 16u, 64u };
    for(int ti=0;ti<5;++ti) { Monster* target=targets[ti];
    if (target->active && !(wrenchHitMask&tbits[ti])) {
        float rng=RunUpgWrenchRange()+BenchKnockExtra();
        float dx = target->x - posX, dy = target->y - posY;
        float d2 = dx * dx + dy * dy;
        float reach = rng + 1.2f;
        if (d2 < reach * reach) {
            float d = sqrtf(d2);
            float ux = (d > 0.01f) ? dx / d : dirX;
            float uy = (d > 0.01f) ? dy / d : dirY;
            float push = rng * power;
            float nx = target->x + ux * push;
            float ny = target->y + uy * push;
            if (Blocked(nx, ny)) { nx = target->x + ux * push * 0.5f;
                                   ny = target->y + uy * push * 0.5f; }
            if (!Blocked(nx, ny)) { target->x = nx; target->y = ny; }
            wrenchHitMask|=tbits[ti];
            /* 第 1 轮：命中闪光 —— 让玩家**看得见**自己打中了 */
            {  wrenchHitFlashT = 0.22f; }
            target->stun=RunUpgWrenchStun()*BenchKnockStunMul();
            SpawnParticles(target->x, target->y, 1);   /* v3.6：受击血/墨粒子 */
            knockCountRun++;
            if(SkillOwned(SK_COUNTER_REPOSITION) && retaliateT<1.0f)retaliateT=1.0f;
            if(BenchShock()) { SuspicionReset();target->stateTimer=0.0f;target->tx=target->x;target->ty=target->y; }
            target->state = 0;
            target->pathLen = 0;
            target->pathIdx = 0;
            target->repath = 0.0f;
            PlaySfx(SFX_POWER, 0.60f);
            AddShake(0.16f, 0.12f);
        }
    }

    }

    /* ==================================================================
     * 2026-11-06（用户要求）：**扳手砸中缚丝蛛 → 它当场消失，30 秒不刷新**
     * ------------------------------------------------------------------
     * 用户原话：「也要出一个克制蜘蛛的方法，就是用那个扳手砸中蜘蛛的话，
     *           蜘蛛会消失，然后 30 秒之内都不会刷新在场上」。
     *
     * 蜘蛛不是 Monster（它不在这套猎手数组里），所以单独判一次。
     * 5 号位留给它，和清道夫的 4 号位并列。
     * ================================================================ */
    {
        if (SpiderActive() && !(wrenchHitMask & 32u)) {
            float sdx = SpiderX() - posX, sdy = SpiderY() - posY;
            float reach = RunUpgWrenchRange() + BenchKnockExtra() + 1.2f;
            if (sdx * sdx + sdy * sdy < reach * reach) {
                SpawnParticles(posX + sdx, posY + sdy, 1);
                SpiderDispel();
                wrenchHitMask |= 32u;
                knockCountRun++;
                AddShake(0.18f, 0.16f);
            }
        }
    }

    {
        if (CollectorActive() && !(wrenchHitMask&4u)) {
            float cdx = CollectorX() - posX, cdy = CollectorY() - posY;
            if (cdx * cdx + cdy * cdy < 4.0f) { CollectorHitByWrench(posX, posY);wrenchHitMask|=4u;knockCountRun++; }
        }
    }
}

/* ---- 开始一次挥击（只扣资源，不判定）---- */
void WrenchStartSwing(float charge) {
    /* v6 每日限制：今天不能使用扳手 */
    { 
      if (DailyForbidWrench()) {


          pickupMsg = g_lang == LANG_ZH ? "今天不能用扳手" : "TODAY: NO WRENCH";
          pickupMsgTimer = 1.6f;
          return;
      } }



    float cdMax = RunUpgWrenchCooldown();

    if (cdMax < 0.0f) {
        if (wrenchLastUsed >= wrenchCharges) {
            /* 2026-11-06：**用完/冷却必须给提示** ——
               原来静默 return，玩家以为键没反应（手机端尤其明显）。 */


            pickupMsg = (g_lang == LANG_ZH) ? "扳手次数用完了" : "WRENCH USES SPENT";
            pickupMsgTimer = 1.2f;
            /* 2026-11-08（第 4 轮）：HINT_WRENCH 文案写好了却从没触发过 */
            {  HintOnce(6); }
            return;
        }
        wrenchLastUsed++;
    } else {
        if (wrenchCd > 0.0f) {


            pickupMsg = (g_lang == LANG_ZH) ? "扳手冷却中" : "WRENCH ON COOLDOWN";
            pickupMsgTimer = 1.0f;
            return;
        }
        wrenchCd = cdMax;
    }

    /* 第 2 轮：难度决定判定窗口宽度（轻松 ×1.45 / 硬核 ×0.85） */
    wrenchSwingT     = WRENCH_SWING_TIME * DiffWrenchWindowMul();
    wrenchSwingPower = 0.70f + 0.30f * charge;
    wrenchSwingHit   = 0;
    wrenchHitMask = 0;
    /* 2026-11-06：**挥击要有看得见/听得见的结果**。
       手持模型早就删了，原来只有一声 SFX_STEP（脚步），
       玩家根本分不清"挥了"还是"没挥"。
       现在换成挥空的风声 + 一下轻微屏幕冲击。 */
    TutorialRecordAction(TUTOR_WRENCH);
    PlaySfx(SFX_THROW, 0.55f);
    AddShake(0.12f, 0.07f);
    /* 第 3 轮：挥空也要有准星反馈（命中时会被更亮的标记覆盖） */
    {  wrenchSwingFxT = WRENCH_SWING_TIME * 1.6f; }
}

/* ==================================================================
 * 2026-11-08（第 3 轮）：**测试钩子** —— 直接触发一次挥击
 * ------------------------------------------------------------------
 * 挥击平时由"按住左键 → 松开"驱动，测试里没有输入系统。
 * 这个钩子只做状态机那几步（和真实按键后做的事一样），
 * 生产路径不调用。先例：ScavengerGrabTest / RunUpgSetLevel。
 * ================================================================== */
void WrenchSwingStartForTest(float charge) {
    if (charge < 0.0f) charge = 0.0f;
    if (charge > 1.0f) charge = 1.0f;
    wrenchSwingT     = WRENCH_SWING_TIME * DiffWrenchWindowMul();
    wrenchSwingPower = 0.70f + 0.30f * charge;
    wrenchSwingHit   = 0;
    wrenchHitMask = 0;
    wrenchSwingFxT = WRENCH_SWING_TIME * 1.6f;
}

/* ---- 每帧推进挥击动画，在命中帧结算 ---- */
void WrenchTick(float dt) {

    if (wrenchSwingT <= 0.0f) return;

    wrenchSwingT -= dt;
    if (wrenchSwingT < 0.0f) wrenchSwingT = 0.0f;

    if (WrenchSwingFrame() >= WRENCH_HIT_FRAME) {
        wrenchSwingHit = 1;
        WrenchApplyHit(wrenchSwingPower);
        /* 2026-11-05：宝箱怪**不在 monHunters[] 里**，MonsterCollect 永远扫不到。
           这里单独扫一遍：伪装态被敲 → 直接现形且**不挨咬**。 */
        { 
          MimicWrenchSweep(posX, posY, dirX >= 0.0f ? 0.0f : 3.14159265f,
                           RunUpgWrenchRange()); }
    }
}

float wrenchCharge = 0.0f;    /* 0..1 */
/* 第 1 轮：扳手命中闪光剩余秒数（HUD 画准星命中标记用） */
float wrenchHitFlashT = 0.0f;
/* 第 3 轮：**挥空**的暗标记（与挥击同长）—— 让"挥了但没打中"也看得见 */
float wrenchSwingFxT  = 0.0f;
/* 第 7 轮：连续静止秒数（幽灵流派用） */
/* 第 8 轮：典当的冷却（秒） */
/* 第 9 轮：以牙还牙的加速剩余秒数 / 反杀是否已用（每局一次） */
static int wrenchPrev = 0;
static int wrenchInit = 0;
/* ===== v2.5.00 **装填闩** =====
   用户报告：「每次进游戏老是误触消耗一次扳手次数。」

   根因：在主菜单用**鼠标左键**点"新游戏"，进入游戏时左键往往还按着。
   旧代码在 resetInputState 那一帧只是把 wrenchPrev 同步成"已按下"，
   但**接下来它仍然在累积蓄力**，等你松手就判定成一次挥击 ——
   于是每局开场都白扣一次。

   现在加一个闩：reset 之后必须**先松开过一次**，才允许开始蓄力。
   拿鼠标点进游戏 → 松开 → 之后才是真正的一挥。 */
static int wrenchArmed = 1;

void WrenchKnockUpdate(float dt) {



    int down = (!lockActive) && (!isCharging) && ActionIsDown(ACT_CANCEL_CHARGE);

    if (resetInputState || !wrenchInit) {
        wrenchPrev = down;
        wrenchCharge = 0.0f;
        wrenchArmed = 0;          /* 上闩：先松开才有效 */
        wrenchInit = 1;
    }

    /* 只要处于"没按住"，就把闩打开 */
    if (!down) wrenchArmed = 1;


    if (down && !wrenchPrev) {
        /* 只有上过膛才允许开始蓄力 */
        wrenchCharge = wrenchArmed ? (dt / 0.45f) : 0.0f;
        if (wrenchCharge > 1.0f) wrenchCharge = 1.0f;
    } else if (down) {
        if (wrenchArmed) {
            wrenchCharge += dt / 0.45f;
            if (wrenchCharge > 1.0f) wrenchCharge = 1.0f;
        }
    } else if (!down && wrenchPrev) {
        if (wrenchArmed && wrenchCharge >= WRENCH_CHARGE_DEADZONE) {
            WrenchStartSwing(wrenchCharge);
        }
        wrenchCharge = 0.0f;
    }

    wrenchPrev = down;
}

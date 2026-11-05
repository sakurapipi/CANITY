/*
 * Canity — 投掷物系统（v8 重构第六刀 C）
 *
 * 原来整段写在 game.c 里（ThrowItem 107 行 + UpdateShots 234 行），
 * 现在独立成一个区块。依赖：
 *   · include/state.h      全局状态
 *   · include/suspicion.h  可疑点表（落地写 THE ECHO 的点）
 *   · game.c 暴露的公开变量（下面 extern 声明）
 */
#include "common.h"
#include "game.h"
#include "state.h"
#include "suspicion.h"
#include "throwing.h"
#include "hints.h"
#include "audio.h"
#include "lang.h"
#include "entity.h"
#include "skills.h"
#include "bench.h"
#include "relics.h"
#include "upgrade_run.h"
#include "weapons.h"

/* ---- game.c 里的公开变量（重构时逐步搬进 state.h） ---- */
extern float mouseCharge;
extern int   g_throwOk;
extern void  SpawnGlue(float,float);
extern void  SpawnShards(float,float,float,float,float);
extern int chargeBonus(float);
extern float weaponAtkCharge;
extern int   g_lang;

/* ===== v2.3.2 THE ECHO 改成「连投三次」才触发 =====
   每扔一次就出现一个小回声，太频繁也太奇怪，反而像 bug。
   现在：8 秒内连扔到第 3 个，才在那个落点留下一次强回响。 */
int   throwChain      = 0;    /* 连续扔了几个 */
float throwChainTimer = 0.0f; /* 归零倒计时，8 秒不扔就断链 */
#define THROW_CHAIN_NEED   3
#define THROW_CHAIN_IDLE   8.0f

/* ===== v2.3.6 回响统一入口 =====
   规则：
     · 任何投掷物都算进连投计数（8 秒内不扔就断链）
     · 连投到第 3 个 → 触发一次强回响
     · **全场最多只有一个回响** —— 触发前先清掉已有的，
       否则多个 100 强度的假线索叠在一起会把怪物数值彻底搞乱。 */
void EchoTryTrigger(float x, float y) {
    /* ==================================================================
     * 2026-11-05 修「回响触发机制」——用户报告「回响探测键有点小问题」
     * ------------------------------------------------------------------
     * 设计意图（原注释）：**任何投掷物都算进连投计数**。
     *
     * 但原来的接线只在「落在地面上」那一条分支里调了这个函数 ——
     * 于是下面三种结束方式**完全不计数**：
     *     · 撞墙      ← 走廊里最常见
     *     · 打中怪
     *     · 空中寿命到期
     * 玩家实际感受就是「扔了半天不回响」，因为大部分投掷都撞墙了。
     *
     * 现在四个出口（落地 / 撞墙 / 打怪 / 空中到期）都会调到这里，
     * 连投计数才和"扔了几次"真正对应上。
     * ================================================================== */
    extern SuspicionPoint susp[SUSP_MAX];
    throwChain++;
    throwChainTimer = THROW_CHAIN_IDLE;
    if (throwChain < THROW_CHAIN_NEED) return;
    throwChain = 0;

    /* 上限 1 个：先把旧的清掉 */
    for (int i = 0; i < SUSP_MAX; i++)
        if (susp[i].strength > 0.0f && susp[i].source == SUSP_THROW)
            SuspicionConsume(i);

    SuspicionAdd(x, y, 100.0f, 1.5f, 30.0f, SUSP_THROW);
    PlaySfx(SFX_ECHO, 0.95f);
    pickupMsg = (g_lang == LANG_ZH) ? "回响炸开了" : "ECHO BURST";
    pickupMsgTimer = 1.6f;
}

/* ==================================================================
 * 2026-11-05：**投石 → 破门石**（方案 B）
 * ------------------------------------------------------------------
 * 用户原话：「局里有个叫投石的技能的设计是怎么样的」→ 结论是它没有
 * 不可替代的功能（噪音输给诱饵、功能性输给烟雾/胶水）。
 *
 * 现在给它一件**别的道具都做不到**的事：
 *     丢过去的石头砸在锁箱上 → 锁直接崩开，跳过锁盘小游戏。
 *
 * 为什么这个功能该归它：
 *   · 物理上最自然 —— 抛物线扔一块石头砸锁，本来就是这件事
 *   · 锁盘现在有 6 种、开一把 8~15 秒，而它提供「用噪音换时间」的解法
 *   · 代价明确且够痛：20 格噪音，砸完就得跑
 *
 * 和「宝箱怪的扳手解法」是同一族设计：
 *   同一个障碍（锁/伪装），给两种完全不同的应对（动手 / 动脑）。
 * ================================================================ */
#define STONE_LOCK_RADIUS   2.5f     /* 命中判定的落点半径（格）—— 2.0 太严，放宽 */
#define STONE_LOCK_NOISE   20.0f     /* 砸锁的噪音 —— 这是它的代价 */
#define STONE_LOCK_CHANCE   100      /* 100 = 必成。留个口子方便以后调成概率 */

/* 落点 x,y 附近有没有锁箱；有就砸开。返回 1 = 砸开了。
   ⚠ 只处理 `locked`，不碰 `mimicHost`（宝箱怪不是锁箱）。 */
static int StoneBreakLockAt(float x, float y) {
    extern Container containers[];
    extern int containerCount;
    extern int g_lang;
    extern const char* pickupMsg;
    extern float pickupMsgTimer;

    int best = -1;
    float bestD = STONE_LOCK_RADIUS * STONE_LOCK_RADIUS;
    for (int i = 0; i < containerCount && i < MAX_CONTAINERS; i++) {
        if (!containers[i].active) continue;
        if (!containers[i].locked) continue;
        if (containers[i].mimicHost) continue;
        float dx = containers[i].x - x, dy = containers[i].y - y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestD) { bestD = d2; best = i; }
    }
    if (best < 0) return 0;
    if ((rand() % 100) >= STONE_LOCK_CHANCE) return 0;

    containers[best].locked   = 0;      /* 锁崩开 */
    containers[best].lockType = 0;
    containers[best].lockCode = 0;
    containers[best].searched = 0;

    /* 砸锁的动静比普通落地大得多 —— 这就是它的代价 */
    MakeNoise(x, y, STONE_LOCK_NOISE);
    PlaySfx(SFX_GLASS, 1.0f);
    { extern void AddShake(float, float); AddShake(0.10f, 0.08f); }

    pickupMsg = (g_lang == LANG_ZH) ? "石头砸开了锁！" : "THE STONE BROKE THE LOCK!";
    pickupMsgTimer = 1.6f;
    { extern int uiDirty; uiDirty = 1; }
    return 1;
}

void ThrowItem(int type, float charge) {
    { extern void HintOnce(int); HintOnce(0); }   /* v7 B4: 第一次投掷 */
    g_throwOk = 0;
    if (charge < 0.1f) charge = 0.1f;
    if (charge > 1.0f) charge = 1.0f;

    float speed = SHOT_SPEED + (SHOT_MAX_SPEED - SHOT_SPEED) * charge;
    float angle = 0.35f * charge;  /* 抛物线角度：蓄力越高越平直 */

    if (type == 1) {
        /* 玻璃瓶：抛物线飞行弹丸 */
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (shot[i].active) continue;
            shot[i].x = posX;
            shot[i].y = posY;
            shot[i].z = SHOT_HEIGHT;
            shot[i].vx = dirX * speed * cosf(angle);
            shot[i].vy = dirY * speed * cosf(angle);
            shot[i].vz = speed * sinf(angle);  /* 向上分量 */
            shot[i].life = 2.0f;
            shot[i].age = 0.0f;
            shot[i].charge = charge;
            shot[i].kind = 1;
            shot[i].dmg = 0;
            shot[i].pierce = 0;
            shot[i].mirrorRebounded = 0;
            shot[i].ignoreTarget = -1;
            shot[i].ignoreTargetTime = 0.0f;
            shot[i].active = 1; g_throwOk = 1;
            break;
        }
        PlaySfx(SFX_THROW, 0.6f);  /* 投掷专用音效：呼啸+出手闷响 */
        return;
    }

    /* ---- v3.3 投掷物重做：2 烟雾 / 3 胶水 / 4 回声，全部走 shot 弹道 ----
       槽位：0 光球 / 1 投石 / 2 烟雾 / 3 胶水 / 4 回声 / 5 诱饵（C 键放置，不走这里）
       shot.kind 约定：1 投石 / 4 烟雾 / 8 胶水 / 10 回声 */
    if (type >= 2) {
        int kind = (type == 2) ? 4 : (type == 3) ? 8 : 10;
        float itemSpeed = speed;
        float itemAngle = angle;
        float itemHeight = 1.0f;
        if (type == 2) {
            /* 烟雾弹：轻、抛得高、滞空久，落点好控制 */
            itemSpeed *= 1.05f;
            itemAngle = angle * 1.25f + 0.06f;
        } else if (type == 3) {
            /* 胶水瓶：重、低抛、飞得近 —— 它就是用来扔脚下的 */
            itemSpeed *= 0.88f;
            itemAngle *= 0.75f;
            itemHeight = 0.94f;
        } else {
            /* 回声弹：小、轻、平飞 */
            itemSpeed *= 1.10f;
            itemAngle *= 0.80f;
        }
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (shot[i].active) continue;
            shot[i].x = posX + dirX * 0.35f;
            shot[i].y = posY + dirY * 0.35f;
            shot[i].z = SHOT_HEIGHT * (type == 4 ? 0.9f : itemHeight);
            shot[i].vx = dirX * itemSpeed * cosf(itemAngle);
            shot[i].vy = dirY * itemSpeed * cosf(itemAngle);
            shot[i].vz = itemSpeed * sinf(itemAngle);
            shot[i].life = (type == 4) ? 1.6f : 2.0f;
            shot[i].age = 0.0f;
            shot[i].charge = charge;
            shot[i].kind = kind;
            shot[i].dmg = 0;
            shot[i].pierce = 0;   /* v3.3：镜片已删，不再有穿透弹 */
            shot[i].mirrorRebounded = 0;
            shot[i].ignoreTarget = -1;
            shot[i].ignoreTargetTime = 0.0f;
            shot[i].active = 1; g_throwOk = 1;
            break;
        }
        PlaySfx(SFX_THROW, 0.6f);
        return;
    }

    /* 荧光棒：抛物线飞行，落地后转为静止发光体 */
    float flareSpeed = 6.5f + charge * 3.0f;   /* v1.11：6.5~9.5 格/秒（原来满蓄力太远） */
    /* v1.11：仰角从 0.4~0.7 压到 0.12~0.22 rad（7°~13°）。
       之前仰角太大，荧光棒升到 3~4 倍视高，而视角不能往上看 ——
       玩家看到的就是"扔出去先空两秒，第三秒球才从上面掉下来"。
       压平之后出手就能看到弧线。 */
    float flareAngle = 0.12f + charge * 0.10f;
    for (int i = 0; i < MAX_THROWN; i++) {
        if (thrown[i].active) continue;
        /* v1.10：出手点往前挪 0.45 格。
           原来与玩家同一位置 → 投影距离 ty≈0，被 ProjectSprite 的
           "太近就丢弃"挡掉，画面上就是"扔出去先消失一下再出现"。 */
        thrown[i].x = posX + dirX * 0.45f;
        thrown[i].y = posY + dirY * 0.45f;
        thrown[i].z = SHOT_HEIGHT * 0.45f;   /* v1.11：从腹部/胯部出手，不再从头顶 */
        thrown[i].vz = flareSpeed * sinf(flareAngle);
        thrown[i].vx = dirX * flareSpeed * cosf(flareAngle);
        thrown[i].vy = dirY * flareSpeed * cosf(flareAngle);
        thrown[i].type = 0;        /* 0=荧光棒 */
        thrown[i].life = 25.0f;    /* 落地后发光时长 */
        thrown[i].active = 1; g_throwOk = 1;
        break;
    }
    PlaySfx(SFX_THROW, 0.6f);  /* 投掷专用音效：呼啸+出手闷响 */
}

/* 推进所有飞行中的玻璃瓶，结算命中 + 抛物线 + 金币 */
void UpdateShots(float dt) {
    /* v2.3.2：8 秒不扔就断链 */
    if (throwChainTimer > 0.0f) {
        throwChainTimer -= dt;
        if (throwChainTimer <= 0.0f) throwChain = 0;
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!shot[i].active) continue;
        shot[i].life -= dt;
        shot[i].age += dt;
        if (shot[i].ignoreTargetTime > 0.0f) {
            shot[i].ignoreTargetTime -= dt;
            if (shot[i].ignoreTargetTime <= 0.0f) {
                shot[i].ignoreTargetTime = 0.0f;
                shot[i].ignoreTarget = -1;
            }
        }

        /* v1.18：重力 + 落地。
           之前 shot 完全没有 z 积分 —— 砖头/酸瓶/烟雾弹都是
           从胸口直线飞出去、而且永远不落地（烟雾弹自然出不了烟）。 */
        shot[i].vz -= 16.0f * dt;
        shot[i].z += shot[i].vz * dt;
        if (shot[i].z <= 0.0f && shot[i].age > 0.02f) {
            shot[i].z = 0.0f;
            /* ---- v3.3 落地效果分派 ----
               kind 1 投石 / 4 烟雾 / 8 胶水 / 10 回声 ----
               噪音全部由**落点 + 蓄力**决定，不再固定：
                 投石   8 + 蓄力*12  ->  8 ~ 20（玩家自己调）
                 胶水   6（小）
                 烟雾   5（小）
                 回声   0（完全静音 —— 它就是用来偷情报的） */
            /* ===== v7 **THE ECHO** =====
               你扔出去的每个东西，落点都会留下一个「可疑位置」。
               它**自己不发声**（发声反而会分散怪物注意力），
               而是给听声怪一条「玩家刚才在这」的坐标，10 秒后消散。 */
            /* v2.3.6：任何投掷物落地都走统一入口 */
            EchoTryTrigger(shot[i].x, shot[i].y);
            if (shot[i].kind == 4) {
                SpawnSmoke(shot[i].x, shot[i].y);
            } else if (shot[i].kind == 8) {
                SpawnGlue(shot[i].x, shot[i].y);
                PlaySfx(SFX_GLUE, 0.85f);   /* v3.3：黏稠噗叽 */
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
            } else if (shot[i].kind == 10) {
                /* ===== v2.4.58 **回声弹重做：声呐扫描** =====
                   原来只是「把 15 格内怪物位置暴露 5 秒」——
                   可怪本来就在屏幕上看得见（而且有声音提示），
                   这个效果等于什么都没有。用户说的「没什么用」就是它。

                   新定位：**它是唯一的"信息道具"，负责揭开迷雾。**
                     ① 把落点周围 14 格的地图**永久标记为已探索**
                        （小地图从此显示那一块的地形）
                     ② 小地图上**高亮所有箱子 / 配电箱 / 出口** 6 秒
                     ③ 仍然暴露怪物位置（免费附赠）

                   为什么这样有用：
                     这游戏的核心是「没有地图 → 找不到保险丝 → 待太久 → 出怪」。
                     回声弹把"找路"的时间压下来，直接对抗那套压力循环。 */
                extern float echoScanT;
                extern float echoScanX, echoScanY;
                extern int explored[MAP_HEIGHT][MAP_WIDTH];   /* 真实类型：int，见 game.c:91 */

                echoScanT = 6.0f;
                echoScanX = shot[i].x;
                echoScanY = shot[i].y;

                /* ① 揭示落点周围 14 格地形 */
                {
                    int cx0 = (int)shot[i].x, cy0 = (int)shot[i].y;
                    int rad = 14;
                    for (int gy = cy0 - rad; gy <= cy0 + rad; gy++) {
                        if (gy < 0 || gy >= MAP_HEIGHT) continue;
                        for (int gx = cx0 - rad; gx <= cx0 + rad; gx++) {
                            if (gx < 0 || gx >= MAP_WIDTH) continue;
                            int ddx = gx - cx0, ddy = gy - cy0;
                            if (ddx * ddx + ddy * ddy > rad * rad) continue;
                            explored[gy][gx] = 1;
                        }
                    }
                }

                /* 2026-11-05：**回声弹改成资源雷达**
                   半径 3 → 10 格，容器标 30 秒（保险丝金 / 宝箱怪红闪），
                   活物标 5 秒，落地自带 20 格噪音。
                   详细设计见 game.c 的 EchoMarksScan 注释。 */
                {
                    extern void EchoMarksScan(float, float, float);
                    EchoMarksScan(shot[i].x, shot[i].y, ECHO_RADIUS);
                }
                pickupMsg = (g_lang == LANG_ZH) ? "声呐扫描：资源已标出（有声）"
                                                : "SONAR: RESOURCES MARKED (LOUD)";
                pickupMsgTimer = 1.8f;
                PlaySfx(SFX_ECHO, 0.85f);
            } else {
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
            }

            /* ===== v2.4.58 **投石（瓶子）打拾荒者 → 赃款直接爆出来** =====
               用户要求：「拾荒者被瓶子命中，也会把钱爆出来」。
               瓶子本来就该是"从远处打断它"的手段 —— 比扳手安全得多，
               代价是瓶子要花一个投掷格。
               命中半径 1.2 格（比扳手的 0.7 宽），因为瓶子有溅射。 */
            if (shot[i].kind == 1) {
                extern int   CollectorActive(void);
                extern float CollectorX(void), CollectorY(void);
                extern void  CollectorBurstBounty(void);
                if (CollectorActive()) {
                    float cdx = CollectorX() - shot[i].x;
                    float cdy = CollectorY() - shot[i].y;
                    if (cdx * cdx + cdy * cdy < 1.44f) {      /* 1.2 格 */
                        CollectorBurstBounty();
                    }
                }
                /* v2.4.58 瓶子增强：落点附近的普通怪被震一下（硬直 0.35 秒） */
                {

                    extern int trainingMode;
                    if (!trainingMode) {
                        /* 落点 1.5 格内的**所有**猎手都被震一下。
                           原来只判 monster，第 2、3 只完全免疫。 */
                        extern int MonsterCollect(Monster**, int, int);
                        /* 变量名别叫 near —— 那是 Windows 的宏（__near） */
                        Monster* inRange[MON_HUNTER_MAX + 1];
                        int nn = MonsterCollect(inRange, MON_HUNTER_MAX + 1, 1);
                        for (int q = 0; q < nn; q++) {
                            float mdx = inRange[q]->x - shot[i].x;
                            float mdy = inRange[q]->y - shot[i].y;
                            if (mdx * mdx + mdy * mdy < 2.25f) {  /* 1.5 格 */
                                if (inRange[q]->stun < 0.35f) inRange[q]->stun = 0.35f;
                            }
                        }
                    }
                }
            }
            {
                float nz = 14.0f;
                /* v3.4：SILENT THROW（局内树 id 2）投掷物落地不再产生噪音 */
                if (BenchSilentThrow()) nz = 0.0f;
                if (shot[i].kind == 1)      nz = 8.0f + shot[i].charge * 12.0f;  /* 8 ~ 20 */
                else if (shot[i].kind == 8) nz = 6.0f;
                else if (shot[i].kind == 4) nz = 5.0f;
                else if (shot[i].kind == 10) nz = 0.0f;                          /* 静音 */
                if (BenchSilentThrow()) nz = 0.0f;   /* v3.4：静投压过所有分支 */
                /* v6 第 ㉟ 轮：**诱饵**（改造树第 2 列的第二顶点）
           投掷物噪音 ×3 —— 纯引怪流。第 ⑩ 轮加了没接。 */
        { extern int BenchLure(void); if (BenchLure()) nz *= 3.0f; }
        if (nz > 0.0f) MakeNoise(shot[i].x, shot[i].y, nz);
            }
            shot[i].active = 0;
            continue;
        }

        /* 分段推进，避免高速穿过怪 */
        float stepLen = sqrtf(shot[i].vx*shot[i].vx + shot[i].vy*shot[i].vy + shot[i].vz*shot[i].vz) * dt;
        int steps = (int)(stepLen / 0.12f) + 1;
        float sdx = shot[i].vx * dt / (float)steps;
        float sdy = shot[i].vy * dt / (float)steps;
        float sdz = shot[i].vz * dt / (float)steps;

        for (int s = 0; s < steps && shot[i].active; s++) {
            shot[i].x += sdx;
            shot[i].y += sdy;
            shot[i].z += sdz;
            shot[i].vz -= GRAVITY * dt / (float)steps;  /* 重力 */

            /* 撞地面：碎裂生成金币 */
            if (shot[i].z <= 0.0f) {
                shot[i].z = 0.0f;
                /* 2026-11-05：**破门石** —— 落在锁箱旁边就把锁砸开。
                   放在碎片/音效之前：砸开了要盖掉普通落地的提示。 */
                if (shot[i].kind == 1) StoneBreakLockAt(shot[i].x, shot[i].y);
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
                PlaySfx(SFX_GLASS, 0.7f);
                /* v3.0 逃生试炼：**扔东西制造噪音把追捕者引开**是核心手法，
                   落地的动静要够大，半径 14 → 22 格。 */
                MakeNoise(shot[i].x, shot[i].y, 15.0f);   /* v3.0 平衡：22 → 15 */
                shot[i].active = 0;
                break;
            }

            /* ===== 撞墙 =====
               v3.3 **修「有时候不生效」** ——
               原来飞行途中撞墙时**只碎、不生成任何效果**，
               于是「对着地面扔胶水，弧线蹭到墙」就什么都不会出现，
               玩家感觉就是「胶水有时候不生效」。
               现在撞墙也走和落地一样的效果分派（只是落点变成墙面位置）。 */
            if (shot[i].age > 0.02f && ShotBlocked(shot[i].x, shot[i].y)) {
                /* 2026-11-05：**破门石** —— 锁箱贴着墙时，瓶子会先撞墙，
                   所以撞墙分支也要试一次，否则「贴着墙的锁箱永远砸不开」。 */
                if (shot[i].kind == 1) StoneBreakLockAt(shot[i].x, shot[i].y);
                /* 2026-11-05 修「回响触发机制」：撞墙也算一次投掷结束。
                   见下面 EchoTryTrigger 的说明。 */
                EchoTryTrigger(shot[i].x, shot[i].y);
                if (shot[i].kind == 4) {
                    SpawnSmoke(shot[i].x, shot[i].y);
                } else if (shot[i].kind == 8) {
                    SpawnGlue(shot[i].x, shot[i].y);
                    PlaySfx(SFX_GLUE, 0.85f);
                } else if (shot[i].kind == 10) {
                    extern float echoScanT;
                    extern float echoScanX, echoScanY;
                    echoScanT = 5.0f;
                    echoScanX = shot[i].x;
                    echoScanY = shot[i].y;
                    PlaySfx(SFX_ECHO, 0.80f);
                }
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
                PlaySfx(SFX_GLASS, 0.7f);
                MakeNoise(shot[i].x, shot[i].y, 15.0f);
                shot[i].active = 0;
                break;
            }

            /* 撞怪：按 kind 分派（1 玻璃瓶 / 2 砖头 / 3 酸瓶 / 4 烟雾弹） */
            float r = SHOT_RADIUS + 0.24f;
            {
                /* 投掷物也要能打中第 2、3 只听声鬼 ——
                   原来这里是手写两条 push，扩到三只之后它们免疫投掷物。 */
                Monster* targets[MON_HUNTER_MAX + 1];
                extern int MonsterCollect(Monster**, int, int);
                int nTargets = MonsterCollect(targets, MON_HUNTER_MAX + 1, 1);
                for (int t = 0; t < nTargets; t++) {
                    Monster* m = targets[t];
                    if (shot[i].ignoreTargetTime > 0.0f && shot[i].ignoreTarget == t) continue;
                    if (Dist2(shot[i].x, shot[i].y, m->x, m->y) >= r * r) continue;

                    int base = 1, type = DT_SHARD;
                    g_dmgIgnoreArmor = 0;
                    if (shot[i].kind == 2) {            /* 砖头：钝击 12 点，蓄满 15 */
                        base = 12 + chargeBonus(shot[i].charge) * 3;
                        type = DT_BLUNT;
                    } else if (shot[i].kind == 3) {     /* 酸瓶：金币 10 点、无视护甲 */
                        base = 10 + chargeBonus(shot[i].charge) * 2;
                        type = DT_SHARD;
                        g_dmgIgnoreArmor = 1;
                    } else if (shot[i].kind == 4) {     /* 烟雾弹：不造成伤害 */
                        SpawnSmoke(shot[i].x, shot[i].y);
                        shot[i].active = 0;
                        break;
                    } else if (shot[i].kind == 8) {    /* v2.0 胶水瓶：定身 2 秒，无伤害 */
                        m->stun = 2.0f;
                        m->glued = 1;   /* v2.2：胶水期间受伤 +50% */
                        shot[i].active = 0;
                        SpawnShards(shot[i].x, shot[i].y, 0, 0, 0.3f);
                        PlaySfx(SFX_GLASS, 0.6f);
                        break;
                    } else if (shot[i].kind == 9) {    /* v2.0 镜片：反弹一次 */
                        if (shot[i].charge > 0.5f && !shot[i].mirrorRebounded) {
                            /* 不再严格 180°折返：那会把弹片立刻送回原目标/玩家。
                               加一点侧向偏转，并短暂忽略被反弹的目标。 */
                            float oldVx = shot[i].vx, oldVy = shot[i].vy;
                            float side = 0.22f;
                            float rvx = -oldVx - oldVy * side;
                            float rvy = -oldVy + oldVx * side;
                            float rlen = sqrtf(rvx * rvx + rvy * rvy);
                            float olen = sqrtf(oldVx * oldVx + oldVy * oldVy);
                            if (rlen > 0.001f && olen > 0.001f) {
                                shot[i].vx = rvx * olen / rlen * 0.8f;
                                shot[i].vy = rvy * olen / rlen * 0.8f;
                            } else {
                                shot[i].vx = -oldVx * 0.8f;
                                shot[i].vy = -oldVy * 0.8f;
                            }
                            shot[i].x += shot[i].vx * 0.08f;
                            shot[i].y += shot[i].vy * 0.08f;
                            shot[i].charge = 0.0f;
                            shot[i].mirrorRebounded = 1;
                            shot[i].ignoreTarget = t;
                            shot[i].ignoreTargetTime = 0.18f;
                            break;                     /* 保留飞行 */
                        }
                        base = 1; type = DT_SHARD;
                    } else if (shot[i].kind == 7) {    /* v2.0 燃烧瓶：直接命中 = 10 点火伤 */
                        base = 10; type = DT_SHARD;
                        g_dmgIgnoreArmor = 0;
                    } else if (shot[i].kind == 6) {     /* v2.0 信号枪弹 */
                        base = shot[i].dmg > 0 ? shot[i].dmg : 8;
                        type = WeaponDamageType(curWeapon);
                    } else {                            /* 玻璃瓶 = 破门石 */
                        /* ==========================================================
                         * 2026-11-05 修「投掷物半空碎裂、没有效果」
                         * ----------------------------------------------------------
                         * 用户报告：**终点附近有两个物体时**，投掷物会在半空炸开、
                         * 什么效果都没有。
                         *
                         * 根因就是这里：投石原本和别的投掷物走同一条通用管线 ——
                         * 碰到第一只怪就 `shot[i].active = 0; break;` 销毁。
                         * 而 NO_COMBAT 之后 ApplyDamage **不造成任何伤害**
                         * （只有噪音 + 0.35 秒硬直），所以玩家看到的是：
                         *   瓶子飞到一半 → 碰上一只怪 → 碎 → 什么都没发生。
                         * 如果那只怪旁边还有第二只，视觉上更像"卡在半空"。
                         *
                         * 修法：**投石是工具，不是武器** —— 它穿过怪物，
                         * 只给 0.35 秒硬直，然后继续飞到玩家真正瞄的落点
                         * （那里才该砸锁 / 制造噪音）。
                         * 这样"两个物体在终点"也不会再吃掉它。
                         * ========================================================== */
                        ApplyDamage(m, 0, DT_SHARD, shot[i].x, shot[i].y);
                        SpawnShards(shot[i].x, shot[i].y, 0, 0, 0.25f);
                        continue;        /* ← 不销毁，继续飞 */
                    }
                    SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
                    ApplyDamage(m, base, type, shot[i].x, shot[i].y);
                    /* 2026-11-05：武器系统已下线，信号枪弹（kind 6）也已不再生成 */

                    g_dmgIgnoreArmor = 0;
                    if (shot[i].pierce) { shot[i].pierce = 0; continue; }   /* v2.0：穿透一次 */
                    /* 打在怪物身上同样是「这一次投掷结束了」 —— 回响要计数 */
                    EchoTryTrigger(shot[i].x, shot[i].y);
                    shot[i].active = 0;
                    break;
                }
                if (!shot[i].active) break;
            }
        }
        if (shot[i].life <= 0.0f || (shot[i].z <= 0.0f && shot[i].age > 0.02f)) {
            /* 2026-11-05：**空中过期也要生效**。
               原来这里只有 `SpawnShards()` 就销毁 —— 瓶子在半空炸开、
               一点效果都没有（另一半"半空碎裂"报告就是这条）。
               寿命到期意味着"它飞到哪儿就在哪儿落地"，所以补一次落地效果。 */
            EchoTryTrigger(shot[i].x, shot[i].y);   /* 空中到期也算一次投掷结束 */
            if (shot[i].kind == 1) {
                StoneBreakLockAt(shot[i].x, shot[i].y);
                MakeNoise(shot[i].x, shot[i].y, 15.0f);
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
            } else if (shot[i].kind == 4) {
                SpawnSmoke(shot[i].x, shot[i].y);       /* 烟雾弹落地成云 */
            } else if (shot[i].kind == 8) {
                SpawnGlue(shot[i].x, shot[i].y);
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
            } else if (shot[i].kind == 10) {
                extern void EchoMarksScan(float, float, float);
                EchoMarksScan(shot[i].x, shot[i].y, ECHO_RADIUS);
            } else if (shot[i].z > 0.0f) {
                SpawnShards(shot[i].x, shot[i].y, shot[i].vx, shot[i].vy, shot[i].charge);
            }
            shot[i].active = 0;
        }
    }

    /* 更新玻璃金币 */
    for (int i = 0; i < MAX_SHOTS * SHARD_COUNT; i++) {
        if (!shards_debris[i].active) continue;
        shards_debris[i].age += dt;
        shards_debris[i].life -= dt;
        shards_debris[i].x += shards_debris[i].vx * dt;
        shards_debris[i].y += shards_debris[i].vy * dt;
        shards_debris[i].z += shards_debris[i].vz * dt;
        shards_debris[i].vz -= GRAVITY * dt;
        if (shards_debris[i].z <= 0.0f) {
            shards_debris[i].z = 0.0f;
            shards_debris[i].vz *= -0.3f;  /* 反弹衰减 */
            shards_debris[i].vx *= 0.7f;
            shards_debris[i].vy *= 0.7f;
        }
        if (shards_debris[i].life <= 0.0f) {
            shards_debris[i].active = 0;
        }
    }
}

/*
 * Canity — 宝箱怪 MIMIC 实现
 *
 * 状态机：
 *   MIM_DISGUISED  伪装（呼吸 + 歪锁扣 + 没有浮尘）
 *     ├─ 玩家在旁边观察 1.5 秒不按 E  → MIM_SEEN（识破，0 收益，永不再激怒）
 *     ├─ 翻找进度 ≥ 60%              → MIM_ANGRY（挨咬 -25）
 *     ├─ 回声箭命中                   → 只打标记，状态不变
 *     └─ 扳手敲中                     → MIM_ANGRY（**不挨咬**）
 *   MIM_ANGRY      追击 8 秒，每 2 秒发一次 12 格噪音
 *     ├─ 抓中                         → 理智 -35 + 脱手全部投掷物
 *     ├─ 藏身 3 秒 / 拉开 3.5 格 / 硬直中被打断 → MIM_GONE
 *   MIM_GONE       原地生成空壳
 *
 * 它**不可杀**：没有 MIM_DEAD。扳手只能逼它现形和击退。
 */
#include "common.h"
#include "mimic.h"
#include "game.h"
#include "entity.h"
#include "ui.h"
#include "audio.h"
#include "lang.h"
#include "sprites.h"

Mimic mimics[MIM_MAX];
int   mimicCount = 0;
int   mimicsEscapedThisRun = 0;

/* 空壳要等它离场后才出现，这里记一下位置（容器由 game.c 那边生成） */
extern Container containers[MAX_CONTAINERS];
extern int containerCount;
extern int currentLevel;

/* ------------------------------------------------------------------ */
/* 生命周期                                                            */
/* ------------------------------------------------------------------ */

void MimicReset(void) {
    for (int i = 0; i < MIM_MAX; i++) {
        mimics[i].state = MIM_NONE;
        mimics[i].containerIdx = -1;
        mimics[i].marked = 0;
        mimics[i].stun = 0.0f;
        mimics[i].noiseT = 0.0f;
        mimics[i].seenTimer = 0;
    }
    mimicCount = 0;
}

/* 在 GenerateLevel 里、**ApplyLockboxes 之前**调用。
   只挑 content == 0 且 locked == 0 的箱子 ——
   保险丝箱（content == 1）绝不能被替换，否则整层无解。 */
void MimicTrySpawn(int level, int containerTotal) {
    if (level < MIM_START_LEVEL) return;
    if (mimicCount >= MIM_MAX) return;

    int pct = (level >= MIM_LATE_LEVEL) ? MIM_PCT_LATE : MIM_PCT_EARLY;
    if ((rand() % 100) >= pct) return;

    /* 挑一个普通空箱 */
    int cand[MAX_CONTAINERS];
    int n = 0;
    for (int i = 0; i < containerTotal && i < MAX_CONTAINERS; i++) {
        if (!containers[i].active) continue;
        if (containers[i].locked) continue;
        if (containers[i].content != 0) continue;   /* 保险丝箱永不替换 */
        if (containers[i].searched) continue;
        cand[n++] = i;
    }
    if (n <= 0) return;

    int pick = cand[rand() % n];
    int slot = mimicCount++;
    Mimic* m = &mimics[slot];
    m->state        = MIM_DISGUISED;
    m->x            = containers[pick].x;
    m->y            = containers[pick].y;
    m->timer        = 0.0f;
    m->noiseT       = 0.0f;
    m->breathPh     = (float)(rand() % 100) * 0.0628f;
    m->stun         = 0.0f;
    m->containerIdx = pick;
    m->marked       = 0;
    m->seenTimer    = 0;

    /* 它占住这个箱子槽，但**槽要保持 active** ——
       交互检测（hoverKind）扫的是 containers[]，停用了就按不到 E。 */
    containers[pick].mimicHost = 1;
    uiDirty = 1;
}

/* ------------------------------------------------------------------ */
/* 现形                                                                */
/* ------------------------------------------------------------------ */

static void MimicReveal(Mimic* m, int bitten) {
    if (!m || m->state != MIM_DISGUISED) return;
    m->state = MIM_ANGRY;
    m->timer = 0.0f;
    m->noiseT = MIM_NOISE_T;      /* 现形后 2 秒发第一声 */
    PlaySfx(SFX_GLASS, 0.85f);
    SpawnParticles(m->x, m->y, 2);
    if (bitten) {
        sanity -= MIM_SCARE_SANITY;
        if (sanity < 0.0f) sanity = 0.0f;
        pickupMsg = g_lang == LANG_ZH ? "箱子咬了你一口！" : "THE BOX BIT YOU!";
        pickupMsgTimer = 1.6f;
    } else {
        pickupMsg = g_lang == LANG_ZH ? "扳手敲出了原形！" : "THE WRENCH FLUSHED IT OUT";
        pickupMsgTimer = 1.6f;
    }
    uiDirty = 1;
}

/* ------------------------------------------------------------------ */
/* 离场：钻回墙里，原地留一个空壳                                       */
/* ------------------------------------------------------------------ */

static void MimicVanish(Mimic* m) {
    if (!m) return;
    m->state = MIM_GONE;
    /* 2026-11-05：**在原地留下空壳** —— 这是全部奖励的载体。
       它生成时占着一个 container 槽并把它 active=0 掉，
       这里把那个槽原样激活即可，不需要新位置。 */
    if (m->containerIdx >= 0) {
        containers[m->containerIdx].mimicHost = 0;   /* 解除占用 */
        extern void MimicSpawnShell(int slot, float x, float y);
        MimicSpawnShell(m->containerIdx, m->x, m->y);
    }
    m->containerIdx = -1;

    /* 记录奖励规格，game.c 生成空壳时读 */
    mimicsEscapedThisRun++;
    if (mimicsEscapedThisRun > MIM_MAX) mimicsEscapedThisRun = MIM_MAX;

    PlaySfx(SFX_POWER, 0.7f);
    SpawnParticles(m->x, m->y, 2);
    pickupMsg = g_lang == LANG_ZH ? "它钻回墙里了" : "IT SLIPPED INTO THE WALL";
    pickupMsgTimer = 1.4f;
    uiDirty = 1;
}

/* ------------------------------------------------------------------ */
/* 交互钩子                                                            */
/* ------------------------------------------------------------------ */

/* 翻找过程中每帧调一次。progress = holdTimer / SEARCH_TIME。
   返回 1 = 被咬（调用方要把 holdTimer 归零）。 */
int MimicOnSearchStep(int containerIdx, float progress) {
    for (int i = 0; i < mimicCount; i++) {
        Mimic* m = &mimics[i];
        /* 只有"已经离场"的才不参与；DISGUISED / SEEN 都还能咬 */
        if (m->state == MIM_NONE || m->state == MIM_ANGRY || m->state == MIM_GONE) continue;
        if (m->containerIdx != containerIdx) continue;
        if (progress < MIM_REVEAL_PCT) return 0;
        MimicReveal(m, 1);
        return 1;
    }
    return 0;
}

/* 回声箭：**只打标记，不惊动**。
   让玩家知道"这个箱子有问题"，从而提前找好藏身点再开。 */
void MimicOnEchoHit(int containerIdx) {
    for (int i = 0; i < mimicCount; i++) {
        Mimic* m = &mimics[i];
        if (m->state != MIM_DISGUISED && m->state != MIM_SEEN) continue;
        if (m->containerIdx != containerIdx) continue;
        m->marked = 1;
        PlaySfx(SFX_PICKUP, 0.5f);
        pickupMsg = g_lang == LANG_ZH ? "回声不对 —— 这箱子里有东西" : "ECHO IS WRONG - SOMETHING IS INSIDE";
        pickupMsgTimer = 1.8f;
        uiDirty = 1;
    }
}

/* 扳手挥击：伪装态 → 逼现形且**不挨咬**；现形态 → 击退 + 硬直。
   ⚠ 它不在 monHunters[] 里，MonsterCollect 永远扫不到 ——
     所以扳手那边必须单独调用这个函数（铁律四十 登记点 ⑫）。 */
int MimicWrenchSweep(float px, float py, float facing, float range) {
    int hitAny = 0;
    for (int i = 0; i < mimicCount; i++) {
        Mimic* m = &mimics[i];
        if (m->state != MIM_DISGUISED && m->state != MIM_SEEN && m->state != MIM_ANGRY) continue;

        float dx = m->x - px, dy = m->y - py;
        float d2 = dx * dx + dy * dy;
        if (d2 > range * range) continue;

        /* 朝向检查：只打身前半圆 */
        if (range > 0.01f) {
            float dot = dx * cosf(facing) + dy * sinf(facing);
            if (dot < -0.15f * range) continue;
        }

        if (m->state == MIM_DISGUISED) {
            MimicReveal(m, 0);             /* 不挨咬 */
        } else {
            m->stun = MIM_WRENCH_STUN;     /* 硬直，方便玩家搜空壳 */
            /* 击退 2 格 */
            float len = sqrtf(d2);
            if (len < 0.01f) len = 0.01f;
            m->x += dx / len * MIM_WRENCH_KNOCK;
            m->y += dy / len * MIM_WRENCH_KNOCK;
        }
        hitAny = 1;
    }
    return hitAny;
}

/* 胶水：定身 2 秒（复用 stun）。
   ⚠ 调用点在 throwing.c / game.c 的胶水落地处（铁律四十 登记点）。 */
void MimicOnGlue(float x, float y) {
    for (int i = 0; i < mimicCount; i++) {
        Mimic* m = &mimics[i];
        if (m->state != MIM_ANGRY) continue;
        float dx = m->x - x, dy = m->y - y;
        if (dx * dx + dy * dy > 2.2f * 2.2f) continue;
        m->stun = 2.0f;
        uiDirty = 1;
    }
}

/* ------------------------------------------------------------------ */
/* 每帧                                                                */
/* ------------------------------------------------------------------ */

void MimicTick(float dt) {
    if (dt <= 0.0f) return;

    for (int i = 0; i < mimicCount; i++) {
        Mimic* m = &mimics[i];
        m->timer += dt;
        m->breathPh += dt * 2.6f;       /* 破绽①：呼吸 */

        if (m->stun > 0.0f) {
            m->stun -= dt;
            if (m->stun < 0.0f) m->stun = 0.0f;
        }

        if (m->state == MIM_DISGUISED) {
            /* ============================================================
             * 2026-11-05 修：**这里原来会把宝箱怪变成死箱子**
             * ------------------------------------------------------------
             * 旧逻辑：玩家在 2.2 格内待满 1.5 秒 → 自动进入 MIM_SEEN。
             * 而 MimicOnSearchStep 只处理 MIM_DISGUISED ——
             * 于是"走过去、站一下、再开"这条最自然的路径，
             * 触发不到咬人 / 现形 / 追击，怪物彻底哑火。
             *
             * 而且「绕开 = 0 收益」定下来之后，MIM_SEEN 本来就没有奖励，
             * 它唯一的作用就是把这条怪废掉。
             *
             * 现在：靠近只给**一次提示**（教玩家看破绽），**不改状态**。
             * 想避开它就把 E 松开走人 —— 不给钱，也不需要"识破"这个状态。
             * ============================================================ */
            float dx = posX - m->x, dy = posY - m->y;
            if (dx * dx + dy * dy < 2.6f * 2.6f) {
                m->seenTimer++;
                if (m->seenTimer == 90) {     /* 只提示一次 */
                    pickupMsg = g_lang == LANG_ZH
                        ? "这个箱子在轻轻起伏……"
                        : "THIS CRATE IS BREATHING...";
                    pickupMsgTimer = 2.2f;
                    uiDirty = 1;
                }
            } else {
                m->seenTimer = 0;
            }
            continue;
        }

        if (m->state == MIM_SEEN) continue;

        if (m->state == MIM_ANGRY) {
            /* 抓中判定 */
            float dx = posX - m->x, dy = posY - m->y;
            float d2 = dx * dx + dy * dy;
            if (d2 < MIM_CATCH_R * MIM_CATCH_R && hiddenIn < 0) {
                sanity -= MIM_CATCH_SANITY;
                if (sanity < 0.0f) sanity = 0.0f;
                /* 强制脱手全部投掷物 */
                { extern int bottles, flares, smokeBombs, gluePots, echoBolts, decoys;
                  bottles = 0; flares = 0; smokeBombs = 0; gluePots = 0;
                  echoBolts = 0; decoys = 0; }
                PlaySfx(SFX_GLASS, 1.0f);
                MimicVanish(m);
                continue;
            }

            /* 甩掉：藏身点里 3 秒 */
            if (hiddenIn >= 0) {
                m->noiseT += dt;
                if (m->timer > 3.0f) { MimicVanish(m); continue; }
                continue;
            }

            /* 甩掉：拉开 3.5 格 */
            if (d2 > MIM_LOSE_DIST * MIM_LOSE_DIST) { MimicVanish(m); continue; }

            /* 超时 */
            if (m->timer > MIM_CHASE_TIME) { MimicVanish(m); continue; }

            /* 追击移动（硬直时不走） */
            if (m->stun <= 0.0f && d2 > 0.01f) {
                extern float playerMoveSpeedNow;
                float sp = playerMoveSpeedNow;
                if (sp < 1.6f) sp = 1.6f;              /* 走路基准 */
                sp *= MIM_SPEED_MUL;
                float len = sqrtf(d2);
                m->x += dx / len * sp * dt;
                m->y += dy / len * sp * dt;
            }

            /* 噪音：它真正的危险性 */
            m->noiseT -= dt;
            if (m->noiseT <= 0.0f) {
                m->noiseT = MIM_NOISE_T;
                MakeNoise(m->x, m->y, MIM_NOISE_R);
                PlaySfx(SFX_GLASS, 0.28f);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* 渲染                                                                */
/* ------------------------------------------------------------------ */

void MimicDraw(void) {
    /* 2026-11-05：绘制已交给 render.c 的渲染列表（kind 15），
       这样它和箱子/怪共享同一套深度排序与光照。
       这个函数留作调试入口（比如以后要画"回声标记红点"）。 */
}

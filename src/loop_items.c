/* Transient item pools: smoke, glue and decoys. */
#include "common.h"
#include "item_effects.h"
#include "world_api.h"
#include "player_api.h"
#include "present_api.h"
#include "loop_api.h"
#include "entity.h"
#include "state.h"
#include "shop.h"
#include "sound_event.h"
#include "audio.h"
#include "warden.h"
#include "mimic.h"
#include "lang.h"


int flares = 0, bottles = 0;
int phaseBeacons = 0;   /* 相位信标（唯一的一种相位投掷物） */
int bricks = 0, acids = 0, smokeBombs = 0;   /* v1.16 新投掷物 */
int firePots = 0, gluePots = 0, mirrors = 0; /* v2.0 三个新投掷物（v3.3：火/镜已删，只剩 gluPots 在用） */
int echoBolts = 0;   /* 2026-11-05 回声弹 = 资源雷达：ECHO_RADIUS 内标容器 30 秒 / 活物 5 秒 */
int decoys = 0;      /* v3.3 玩家诱饵玩偶：C 键放置，每 DECOY_SHOUT_EVERY 秒发声 */
int throwSelected = 0;        /* 0=荧光棒, 1=玻璃瓶, 2=预留 */

SmokeCloud smokes[MAX_SMOKE];
GlueZone  glues[MAX_GLUE];     /* v3.3 粘胶区域 */
Decoy decoysArr[MAX_DECOY];

void SpawnSmoke(float x, float y) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (smokes[i].active) continue;
        smokes[i].x = x; smokes[i].y = y;
        smokes[i].life = SMOKE_LIFE;
        smokes[i].active = 1;
        return;
    }
}

void PlaceDecoy(float x, float y) {
    int slot = -1;
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        /* 顶掉最老的那个 */
        float oldest = 1e9f; 
        for (int i = 0; i < MAX_DECOY; i++)
            if (decoysArr[i].life < oldest) { oldest = decoysArr[i].life; slot = i; }
    }
    if (slot < 0) return;
    decoysArr[slot].x = x;
    decoysArr[slot].y = y;
    decoysArr[slot].life = 10.0f;   /* v2.4.14：只活 10 秒 */
    decoysArr[slot].shoutT = DECOY_SHOUT_EVERY;
    decoysArr[slot].hp = DECOY_HP;
    decoysArr[slot].active = 1;
    decoysArr[slot].armed = 0;
}

void ArmDecoy(void) {
    /* 激活最近的一个未激活玩偶 */
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active || decoysArr[i].armed) continue;
        float d = Dist2(decoysArr[i].x, decoysArr[i].y, posX, posY);
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) {
        decoysArr[best].armed = 1;
        decoysArr[best].shoutT = 0.0f;   /* 激活立刻叫第一声 */
    }
}

int DecoyIntercept(void) {
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active || !decoysArr[i].armed) continue;
        if (decoysArr[i].hp <= 0) continue;
        /* 玩偶要在怪物附近（它替玩家吸引了怪） */
        if (Dist2(decoysArr[i].x, decoysArr[i].y, posX, posY) > 64.0f) continue;
        decoysArr[i].hp--;
        if (decoysArr[i].hp <= 0) {
            decoysArr[i].active = 0;
            pickupMsg = (g_lang == LANG_ZH) ? "玩偶散架了" : "DECOY BROKE";
        } else {
            pickupMsg = (g_lang == LANG_ZH) ? "玩偶替你挨了一下" : "DECOY TOOK THE HIT";
        }
        pickupMsgTimer = 1.6f;
        PlaySfx(SFX_GLASS, 0.8f);
        return 1;
    }
    return 0;
}

void SpawnGlue(float x, float y) {
    int best = -1;
    float bestLife = 1e9f;
    for (int i = 0; i < MAX_GLUE; i++) {
        if (!glues[i].active) { best = i; bestLife = -1.0f; break; }
        if (glues[i].life < bestLife) { bestLife = glues[i].life; best = i; }
    }
    if (best < 0) return;
    glues[best].x = x;
    glues[best].y = y;
    glues[best].life = GLUE_LIFE;
    glues[best].active = 1;
    /* 2026-11-05：宝箱怪不是普通怪，胶水管线扫不到它 —— 单独通知一次。
       命中后它硬直 2 秒，玩家可以趁机搜空壳（铁律二十二 登记点）。 */
    MimicOnGlue(x, y);
}

int PointInSmoke(float x, float y) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (!smokes[i].active) continue;
        float dx = x - smokes[i].x, dy = y - smokes[i].y;
        if (dx * dx + dy * dy < SMOKE_RADIUS * SMOKE_RADIUS) return 1;
    }
    return 0;
}

int SegmentInSmoke(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return PointInSmoke(x0, y0);
    dx /= d; dy /= d;
    for (float t = 0.0f; t <= d; t += 0.35f)
        if (PointInSmoke(x0 + dx * t, y0 + dy * t)) return 1;
    return PointInSmoke(x1, y1);
}

int PlayerInSmoke(void) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (!smokes[i].active) continue;
        float dx = posX - smokes[i].x, dy = posY - smokes[i].y;
        if (dx * dx + dy * dy < SMOKE_RADIUS * SMOKE_RADIUS) return 1;
    }
    return 0;
}

void UpdateSmokes(float dt) {
    for (int i = 0; i < MAX_SMOKE; i++) {
        if (!smokes[i].active) continue;
        smokes[i].life -= dt;
        if (smokes[i].life <= 0.0f) smokes[i].active = 0;
    }
    /* v3.4：记录本局有没有开过手电（表现加成用） */
    if (flashlightOn) runLightUse = 1;

    /* v6：出口弹窗的淡入计时 */
    if (exitChoiceOpen) exitChoiceT += dt;

    /* v3.4：商店每行的补货倒计时 */
    {
        ShopTickCooldown(dt);
    }

    MoneyTick(dt);
    {  UpdateWarden(dt); }   /* v8 B1: 守门人 */   

    /* v3.3：诱饵心跳 —— 每 DECOY_SHOUT_EVERY 秒叫一次，把怪引过去 */
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active) continue;
        decoysArr[i].life -= dt;
        if (decoysArr[i].life <= 0.0f) { decoysArr[i].active = 0; continue; }
        if (!decoysArr[i].armed) continue;
        decoysArr[i].shoutT -= dt;
        if (decoysArr[i].shoutT <= 0.0f) {
            decoysArr[i].shoutT = DECOY_SHOUT_EVERY;
            /* ===== v7 **玩偶重写** =====
               原来走 MakeNoise(半径20)，那个管道会直接改 monster.state，
               和玩家自己的脚步噪音抢同一个字段 —— 玩偶刚设好就被顶掉，
               所以「玩偶吸引不了听声怪」。

               现在直接往可疑点表写：strength 75（比冲刺的 45 高），
               decay 3/s、寿命 8 秒 —— 三个叫周期都能覆盖到。
               玩家自己的脚步只有 36 强度，抢不过它。 */
            /* ============================================================
             * 2026-11-06（v2.4-beta B05）：**诱饵走事件中心，并且有位置**
             * ------------------------------------------------------------
             * 原来这里直接 SuspicionAdd + 居中的 PlaySfx。
             * 结果：**你听不出自己的玩偶在左边还是右边** ——
             * 而"把怪引到那个方向"正是玩偶的全部意义。
             *
             * 现在：
             *   · 线索走 SoundEmit(SK_VOICE) —— 类别显式，参数在 kKindDefs 里
             *   · 声音走 AudioPlayWorld —— 从玩偶所在方向传过来
             * 参数保持原值（强度 75 / decay 3 / ttl 8），行为不变。
             * ============================================================ */
            {
                SoundEvent ev;
                memset(&ev, 0, sizeof(ev));
                ev.kind = SK_VOICE;
                ev.x = decoysArr[i].x;
                ev.y = decoysArr[i].y;
                ev.strength = 75.0f;
                ev.decay = 3.0f;
                ev.ttl = 8.0f;
                ev.eventId = 0u;              /* 每 1.5 秒一次是**独立**的叫声 */
                ev.sourceEntityId = -1;
                SoundEmit(&ev);
            }
            /* 保留一小圈物理噪音，让追光怪/环境音也有反应 */
            MakeNoise(decoysArr[i].x, decoysArr[i].y, 6.0f);
            { 
              AudioPlayWorld(SFX_DECOY, decoysArr[i].x, decoysArr[i].y, 0.70f, 22.0f, 3); }
        }
    }

    /* v3.3：粘胶区域 / 回声扫描的生命周期 */
    for (int i = 0; i < MAX_GLUE; i++) {
        if (!glues[i].active) continue;
        glues[i].life -= dt;
        if (glues[i].life <= 0.0f) { glues[i].active = 0; glues[i].life = 0.0f; }
    }
}

/* src/domain/detect_alert.c —— 被发现预警（第 4 轮）
 *
 * 只读 AI 状态：state 从 0 变成非 0 = 这一帧它把你从"不知道"切到"追"。
 */
#include "common.h"
#include "detect_alert.h"
#include "audio.h"
#include "entity.h"
#include "game.h"
#include "lang.h"

static int   s_prevState[MON_HUNTER_MAX];
static int   s_prevLight[LIGHTBEAST_MAX];
static int   s_prevParasite;
static int   s_init = 0;
static float s_age = 1.0e9f;

void DetectAlertReset(void) {
    int i;
    for (i = 0; i < MON_HUNTER_MAX; i++) s_prevState[i] = 0;
    for (i = 0; i < LIGHTBEAST_MAX; i++) s_prevLight[i] = 0;
    s_prevParasite = 0;
    s_init = 0;
    s_age = 1.0e9f;
}

float DetectAlertAge(void) { return s_age; }

/* 一个实体：上一帧没在追、这一帧在追 → 它刚发现你 */
static int checkOne(int prev, int now, int active, float x, float y) {
    if (!active) return 0;
    if (prev == 0 && now != 0) {
        AudioPlayWorld(SFX_SCREAM, x, y, 0.45f, 16.0f, 2);
        return 1;
    }
    return 0;
}

int DetectAlertTick(float dt) {
    int i, found = 0;

    if (!s_init) {
        DetectAlertReset();
        for (i = 0; i < MON_HUNTER_MAX; i++) s_prevState[i] = monHunters[i].state;
        for (i = 0; i < LIGHTBEAST_MAX; i++) s_prevLight[i] = lightBeasts[i].state;
        s_prevParasite = parasite.state;
        s_init = 1;
        return 0;
    }

    for (i = 0; i < MON_HUNTER_MAX; i++) {
        found += checkOne(s_prevState[i], monHunters[i].state,
                          monHunters[i].active, monHunters[i].x, monHunters[i].y);
        s_prevState[i] = monHunters[i].state;
    }
    for (i = 0; i < LIGHTBEAST_MAX; i++) {
        found += checkOne(s_prevLight[i], lightBeasts[i].state,
                          lightBeasts[i].active, lightBeasts[i].x, lightBeasts[i].y);
        s_prevLight[i] = lightBeasts[i].state;
    }
    found += checkOne(s_prevParasite, parasite.state, parasite.active,
                      parasite.x, parasite.y);
    s_prevParasite = parasite.state;

    if (found > 0) {
        s_age = 0.0f;
        /* 玩家反馈「不知道被谁盯上」——这里给一句能立刻照做的提示 */
        pickupMsg = (g_lang == LANG_ZH)
            ? "它盯上你了 —— 断视线，或者躲进柜子"
            : "IT SAW YOU - break line of sight, or hide";
        pickupMsgTimer = 1.8f;
    } else if (s_age < 1.0e8f) {
        s_age += dt;
    }
    return found;
}

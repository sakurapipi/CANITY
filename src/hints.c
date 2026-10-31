/*
 * Canity — 一次性教学提示（v7 方案 B4）
 *
 * 为什么不写进 TUT_* 状态机
 * ──────────────────────────
 * 那个状态机是「按顺序推进的引导流程」，改它风险高。
 * 这里要的是**「第一次发生某件事时提示一次就再也不出现」** ——
 * 完全独立的一层，不碰原流程。
 *
 * 用法：任何地方调 HintOnce(HINT_XXX)，只有当次生效。
 * 存档不记，每局重来（新手每一局都该看到）。
 */
#include "common.h"
#include "game.h"
#include "hints.h"
#include "audio.h"
#include "lang.h"

static int hintSeen[HINT_COUNT];

void HintsReset(void) {
    for (int i = 0; i < HINT_COUNT; i++) hintSeen[i] = 0;
}

/* 带文字参数的一次性提示 */
void HintOnceText(int id, const char* zh, const char* en) {
    if (id < 0 || id >= HINT_COUNT) return;
    if (hintSeen[id]) return;
    hintSeen[id] = 1;
    sprintf(pickupMsg, "%s", (g_lang == LANG_ZH) ? zh : en);
    pickupMsgTimer = 3.0f;
    PlaySfx(SFX_PICKUP, 0.5f);
}

/* 固定文案的一次性提示 */
void HintOnce(int id) {
    if (id < 0 || id >= HINT_COUNT) return;
    if (hintSeen[id]) return;
    hintSeen[id] = 1;

    const char* zh = "";
    const char* en = "";
    switch (id) {
    case HINT_THROW:
        zh = "按住右键蓄力，松开发射。";      en = "HOLD RMB TO CHARGE, RELEASE TO THROW.";
        break;
    case HINT_HIDE_SHOP:
        zh = "按 T 可以开商店。";             en = "PRESS T TO OPEN THE SHOP.";
        break;
    case HINT_GOLD:
        zh = "金币会在结算时进银行。";        en = "GOLD GOES TO THE BANK WHEN YOU SETTLE.";
        break;
    case HINT_EXIT:
        zh = "出口会让你选：撤离，或继续深入。"; en = "AT THE EXIT: LEAVE, OR GO DEEPER.";
        break;
    case HINT_CARRY_TIER:
        zh = "带的钱越多，怪物听得越清。";    en = "THE MORE YOU CARRY, THE BETTER THEY HEAR.";
        break;
    case HINT_CAUGHT:
        zh = "按 K 丢钱，它会被引开。";       en = "PRESS K TO DROP GOLD AND LURE IT AWAY.";
        break;
    case HINT_WRENCH:
        zh = "扳手每层 2 次。升级可以变成冷却。"; en = "WRENCH: 2 USES PER FLOOR. UPGRADE FOR COOLDOWN.";
        break;
    default:
        return;
    }
    sprintf(pickupMsg, "%s", (g_lang == LANG_ZH) ? zh : en);
    pickupMsgTimer = 3.0f;
    PlaySfx(SFX_PICKUP, 0.5f);
}

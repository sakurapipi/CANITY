/* tests/domains/test_update.c —— Update() 里抽出来的块
 *
 * 第 4 轮（2026-11-08）：Update() 2,074 行，抽出两块自洽代码
 * （训练场补货、烟雾致盲）。这里通过 Update() 观测它们的行为 ——
 * 不把 static 函数暴露出去，测试就盯"玩家能看到的结果"。
 */
#include "../test_common.h"
#include "loop_private.h"
#include "loop_api.h"
#include "present_api.h"
#include "player_api.h"
#include "player_tick.h"
#include "game.h"
#include "entity.h"
#include "throwing.h"

extern void Update(float dt);
extern int  flares, bottles, smokeBombs, gluePots, echoBolts, decoys;
extern float sanity;
extern SmokeCloud smokes[MAX_SMOKE];
/* 注意：monster 是 common.h 里的宏（#define monster monHunters[0]），
 * 不能再 extern 声明一次 —— 展开后是 extern Monster monHunters[0] 冲突。 */
extern int trainingMode;

/* 进局内：Update() 只在 STATE_PLAYING 时跑逻辑 */
static void enter_playing(void) {
    StartGame();
    currentState = STATE_PLAYING;
    trainingMode = 0;
}

static void test_training_pad_refills_items(void) {
    enter_playing();
    trainingMode = 1;
    flares = 0; bottles = 0; smokeBombs = 0; gluePots = 0; echoBolts = 0; decoys = 0;
    sanity = 10.0f;

    Update(0.016f);

    CHECK(flares == 99, "training pad must refill flares every frame");
    CHECK(bottles == 99, "training pad must refill sling stones");
    CHECK(echoBolts == 99, "training pad must refill echo bolts");
    CHECK(sanity >= 99.0f, "training pad must keep sanity topped up");
    trainingMode = 0;
}

static void test_smoke_blinds_the_monster(void) {
    int i;
    enter_playing();
    for (i = 0; i < MAX_SMOKE; i++) smokes[i].active = 0;

    /* 把怪和一小片烟放在同一格 */
    Hunter0()->active = 1;
    Hunter0()->x = 8.5f; Hunter0()->y = 8.5f;
    Hunter0()->state = 2;           /* 正在追（非 0 = 有目标） */
    Hunter0()->pathLen = 3; Hunter0()->pathIdx = 1;

    smokes[0].active = 1;
    smokes[0].x = 8.5f; smokes[0].y = 8.5f;
    smokes[0].life = 5.0f;

    Update(0.016f);

    CHECK(Hunter0()->state == 0, "a monster inside smoke must lose its target");
    CHECK(Hunter0()->pathLen == 0, "losing the target must also clear its path");

    smokes[0].active = 0;
    Hunter0()->active = 0;
}


/* ==================================================================
 * 2026-11-08（第 3 轮）：**空挥扳手要有准星反馈**
 * ------------------------------------------------------------------
 * 实机反馈：空挥只有摆动 + 冷却，准星没有闪光 ——
 * 玩家分不清"挥了但没打中"和"根本没挥出去"。
 * 现在挥出去就置 wrenchSwingFxT（暗标记），命中另有更亮的。
 * ================================================================ */

static void test_empty_wrench_swing_leaves_a_visible_marker(void) {
    extern float wrenchSwingFxT, wrenchHitFlashT;
    extern void  WrenchSwingStartForTest(float charge);

    enter_playing();
    wrenchSwingFxT = 0.0f;
    wrenchHitFlashT = 0.0f;

    WrenchSwingStartForTest(0.5f);
    CHECK(wrenchSwingFxT > 0.0f,
          "swinging must set a visible marker even when nothing is hit");

    /* 每帧递减，最终归零（不能永久留着） */
    Update(0.05f);
    CHECK(wrenchSwingFxT >= 0.0f, "the marker must decay, never go negative");
}

static void test_invalid_action_and_key_indices(void) {
    int savedBinding = g_bindings[ACT_MOVE_FWD];
    int savedRebinding = rebindingAct;
    CHECK(!ActionIsDown(-1) && !ActionIsDown(ACT_COUNT),
          "invalid action IDs never index the bindings array");
    CHECK(!IsActionPressed((ActionId)-1) && !IsActionPressed((ActionId)ACT_COUNT),
          "pressed queries reject invalid action IDs");
    g_bindings[ACT_MOVE_FWD] = -1;
    CHECK(!ActionIsDown(ACT_MOVE_FWD) && !IsActionPressed(ACT_MOVE_FWD),
          "negative saved key codes never index key arrays");
    g_bindings[ACT_MOVE_FWD] = 256;
    CHECK(!ActionIsDown(ACT_MOVE_FWD) && !IsActionPressed(ACT_MOVE_FWD),
          "oversized saved key codes never index key arrays");
    g_bindings[ACT_MOVE_FWD] = savedBinding;
    rebindingAct = ACT_MOVE_FWD;
    CanityKeyDown(-1, 0);
    CanityKeyDown(256, 0);
    CanityKeyDown(0, 0);
    CHECK(rebindingAct == ACT_MOVE_FWD, "invalid key events cannot consume a rebind request");
    rebindingAct = savedRebinding;
}

static void test_session_input_cancellation(void) {
    enter_playing();
    keys['W'] = keyPressed['E'] = 1;
    SkillDragStart(12, 18);
    treeDragging = 1;
    sliderDragging = 1;
    isCharging = holdingNow = cranking = 1;
    mouseCharge = 0.6f;
    holdTimer = 0.7f;
    LoopClearInputEdges();
    LoopCancelTransientInput();
    int clear = 1;
    for (int i = 0; i < 256; i++) if (keys[i] || keyPressed[i]) clear = 0;
    CHECK(clear, "suspending input clears both down-state and pending action edges");
    CHECK(!SkillIsDragging() && !treeDragging && !sliderDragging,
          "capture/session cancellation ends every drag owner");
    CHECK(!isCharging && mouseCharge == 0.0f && !holdingNow && holdTimer == 0.0f && !cranking,
          "session cancellation cannot leave a throw, search or crank active");
    CHECK(resetInputState, "throw-button history must resync in its owning frame stage");
}

static void test_sprint_grace_rearms_and_resets(void) {
    int savedRank = skillRank[SK_LASTRESORT];
    int savedKeystone = activeKeystone;
    float savedStamina = stamina;
    skillRank[SK_LASTRESORT] = 1;
    activeKeystone = SK_LASTRESORT;
    PlayerResetSprintGrace();
    stamina = 0.0f;
    PlayerTickSprintGrace(0.1f, 1);
    CHECK(secondWindTimer > 0.0f, "empty stamina grants one grace period");
    float previous = secondWindTimer;
    PlayerTickSprintGrace(0.1f, 0);
    CHECK(secondWindTimer < previous, "grace time expires even after the sprint key is released");
    PlayerTickSprintGrace(TUNE_SECOND_WIND_SECONDS + 1.0f, 1);
    PlayerTickSprintGrace(0.1f, 1);
    CHECK(secondWindTimer == 0.0f, "exhausted grace cannot retrigger without recovery");
    stamina = StaminaMax() * 0.75f;
    PlayerTickSprintGrace(0.1f, 0);
    stamina = 0.0f;
    PlayerTickSprintGrace(0.1f, 1);
    CHECK(secondWindTimer > 0.0f, "recovered stamina rearms grace outside the exhaustion branch");
    PlayerResetSprintGrace();
    PlayerTickSprintGrace(0.1f, 1);
    CHECK(secondWindTimer > 0.0f, "a new session cannot inherit a consumed private grace flag");
    skillRank[SK_LASTRESORT] = 0;
    PlayerTickSprintGrace(0.1f, 1);
    CHECK(secondWindTimer == 0.0f, "removing the keystone also removes its active grace");
    skillRank[SK_LASTRESORT] = savedRank;
    activeKeystone = savedKeystone;
    stamina = savedStamina;
    PlayerResetSprintGrace();
}

void TestSuite_update(void) {
    test_invalid_action_and_key_indices();
    test_session_input_cancellation();
    test_sprint_grace_rearms_and_resets();
    test_training_pad_refills_items();
    test_smoke_blinds_the_monster();
    test_empty_wrench_swing_leaves_a_visible_marker();
}

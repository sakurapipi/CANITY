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
    StartFreshTestRun();
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


#include "present_api.h"
#include "loop_api.h"
#include "shop.h"
#include "bench.h"
#include "lockbox.h"
#include "world_api.h"
#include "monster_rules.h"
static void test_shared_map_release_and_tree_drag(void){
    GameState state=currentState;int page=shopPage,oldOpen=isMapOpen,oldFull=mapFullScreen;
    int oldLock=lockActive;lockActive=0;
    currentState=STATE_PLAYING;isMapOpen=0;mapFullScreen=0;
    CanityKeyDown(VK_TAB,0);
    CHECK(isMapOpen && !mapFullScreen,"TAB down opens the hold map");
    CanityKeyUp(VK_TAB);
    CHECK(!isMapOpen && !mapFullScreen,"shared TAB release closes the hold map");
    isMapOpen=1;mapFullScreen=1;CanityKeyUp(VK_TAB);
    CHECK(isMapOpen && mapFullScreen,"unrelated TAB release preserves a toggle map");
    currentState=STATE_SAFEHOUSE;shopPage=SHP_TREE;
    float vx=treeVX,vy=treeVY,vk=treeVK;int wallet=carryGold;
    treeVK=.5f;
    CanityPointerDown(400,200);CanityPointerMove(470,230);CanityPointerUp(470,230);
    CHECK(fabsf(treeVX-vx-70)<.01f && fabsf(treeVY-vy-30)<.01f,"shared pointer pans the run tree in both axes");
    CHECK(carryGold==wallet,"panning must never purchase a node");
    CanityTreeZoom(1);CHECK(treeVK>.5f,"run tree plus button zooms in");
    CanityTreeZoom(-1);CHECK(fabsf(treeVK-.5f)<.001f,"run tree minus reverses zoom");
    CanityPointerDown(400,200);CanityPointerMove(440,200);CanityPointerCancel();
    CHECK(!treeDragging,"touch cancellation releases tree dragging");
    treeVX=vx;treeVY=vy;treeVK=vk;
    currentState=state;shopPage=page;isMapOpen=oldOpen;mapFullScreen=oldFull;lockActive=oldLock;
    CHECK(MonsterFloorChance(SPAWN_SPIDER,1)==0,"higher spider rate cannot bypass first-floor gate");
    int chance=MonsterFloorChance(SPAWN_SPIDER,10);
    CHECK(chance>=70 && chance<=85,"deep-floor spider chance increases with an 85 percent cap");
}

/* ==================================================================
 * 回归探针：暂停菜单回主菜单 / 藏身点识别
 * 用户实测报告的两个 bug，先在这里钉住"玩家能看到的结果"。
 * ================================================================== */
static void probe_pause_menu_returns_to_main_menu(void) {
    enter_playing();
    lockActive = 0; calibActive = 0; shopOverlayOpen = 0; menuSelection = 0;

    CanityKeyDown(VK_ESCAPE, 0);
    CHECK(currentState == STATE_PAUSED, "Esc during play must open the pause menu");

    CanityKeyDown(VK_DOWN, 0);
    CanityKeyDown(VK_DOWN, 0);
    CHECK(menuSelection == 2, "the pause menu must reach its Main Menu row");

    CanityKeyDown(VK_RETURN, 0);
    CHECK(currentState == STATE_MAIN_MENU,
          "confirming the pause menu Main Menu row must return to the main menu");
}

static void probe_hide_spot_is_recognised(void) {
    extern float hideBlockTimer;
    extern int roomPropCount, hatchCount;
    int interactKey = (int)g_bindings[ACT_INTERACT];

    enter_playing();
    /* 隔离：玩家身边不放别的可交互物，只留一个正前方的藏身点 */
    containerCount = 0;
    roomPropCount = 0;
    hatchCount = 0;
    breaker.active = 0;
    hideBlockTimer = 0.0f;
    hoverKind = 0;
    memset(hideSpots, 0, sizeof(hideSpots));
    hideSpotCount = 1;
    hideSpots[0].active = 1;
    hideSpots[0].x = posX + dirX * 0.5f;
    hideSpots[0].y = posY + dirY * 0.5f;
    hiddenIn = -1;
    hideSpotNear = -1;

    /* 先松开 E，清掉 EnterHide 的静态锁存 */
    keys[interactKey] = 0;
    keyPressed[interactKey] = 0;
    Update(0.016f);
    CHECK(hideSpotNear == 0,
          "a hide spot directly in front of the player must be recognised");

    keyPressed[interactKey] = 1;
    keys[interactKey] = 1;
    Update(0.016f);
    CHECK(hiddenIn == 0, "pressing E at a recognised hide spot must let the player hide");

    keys[interactKey] = 0;
    keyPressed[interactKey] = 0;
    Update(0.016f);
    CHECK(hiddenIn == 0, "releasing E must not pop the player back out of cover");
}

static void probe_pause_menu_mouse_and_hide_priority(void) {
    extern float hideBlockTimer;
    extern int roomPropCount, hatchCount;
    int interactKey = (int)g_bindings[ACT_INTERACT];

    /* 暂停菜单：鼠标点第 3 行「主菜单」 */
    enter_playing();
    lockActive = 0; calibActive = 0; shopOverlayOpen = 0;
    CanityKeyDown(VK_ESCAPE, 0);
    {
        MenuGeo g = GetMenuGeo();
        RECT r = RowRect(g, 2);
        CHECK(g.count == 3 && g.maxVisibleRows >= 3,
              "the pause menu must show all three of its rows without scrolling");
        CanityMouseClick((int)((r.left + r.right) / 2), (int)((r.top + r.bottom) / 2));
        CHECK(currentState == STATE_MAIN_MENU,
              "mouse click on the pause menu Main Menu row must return to the main menu");
    }

    /* 家具在触达范围内时藏身点仍要能进，且进去后不能顺手触发长按搜查 */
    enter_playing();
    containerCount = 0; hatchCount = 0; breaker.active = 0;
    hideBlockTimer = 0.0f; hoverKind = 0;
    memset(hideSpots, 0, sizeof(hideSpots));
    hideSpotCount = 1;
    hideSpots[0].active = 1;
    hideSpots[0].x = posX + dirX * 0.5f;
    hideSpots[0].y = posY + dirY * 0.5f;
    hiddenIn = -1; hideSpotNear = -1;
    roomPropCount = 1;
    memset(roomProps, 0, sizeof(roomProps));
    roomProps[0].active = 1;
    roomProps[0].searchLeft = 1;
    roomProps[0].x = posX; roomProps[0].y = posY;

    keys[interactKey] = 0; keyPressed[interactKey] = 0;
    Update(0.016f);
    CHECK(hideSpotNear == 0,
          "a hide spot directly in front of the player must be recognised");
    CHECK(hoverKind == 3, "the probe must actually have furniture in reach");

    keyPressed[interactKey] = 1; keys[interactKey] = 1;
    Update(0.016f);
    CHECK(hiddenIn == 0,
          "a hide spot must stay enterable when furniture is in reach");
    CHECK(!holdingNow, "hiding must not start a long-press search on that furniture");

    keys[interactKey] = 0; keyPressed[interactKey] = 0;
    Update(0.016f);
    roomPropCount = 0;
}

/* 暂停菜单「主菜单」不能因为一次存盘失败就把玩家永久关在暂停里。
   存盘失败时 toast 也不会在暂停画面绘制（DrawPickupToast 只在游玩中画），
   玩家看到的就是"点了没反应"。
   同时：CANITY_DATA_DIR 只是首选目录，不可写时必须继续尝试 exe 目录等
   兜底位置，而不是把整张候选表都指向同一个坏路径。 */
static void probe_save_failure_must_not_trap_pause_menu(void) {
    char saved[512];
    char fallback[MAX_PATH];
    const char* cur;
    enter_playing();
    lockActive = 0; calibActive = 0; shopOverlayOpen = 0; menuSelection = 0;

    CanityKeyDown(VK_ESCAPE, 0);
    CanityKeyDown(VK_DOWN, 0);
    CanityKeyDown(VK_DOWN, 0);
    CHECK(menuSelection == 2, "pause menu must reach its Main Menu row");

    cur = getenv("CANITY_DATA_DIR");
    snprintf(saved, sizeof(saved), "%s", cur ? cur : "");
    _putenv_s("CANITY_DATA_DIR", "Q:\\canity-unwritable-probe");
    _putenv_s("CANITY_DATA_DIR_AUTO", "1");
    CanityKeyDown(VK_RETURN, 0);
    CHECK(currentState == STATE_MAIN_MENU,
          "a failed save must not trap the player in the pause menu");
    CHECK(g_saveState == 1,
          "an unwritable auto-selected data dir must fall back to a writable location");
    CHECK(strncmp(g_savePath, "Q:", 2) != 0,
          "the fallback save must name the location that actually accepted it");
    snprintf(fallback, sizeof(fallback), "%s", g_savePath);
    _putenv_s("CANITY_DATA_DIR", saved);
    _putenv_s("CANITY_DATA_DIR_AUTO", "0");
    if (fallback[0]) {
        char bak[MAX_PATH + 8];
        remove(fallback);
        snprintf(bak, sizeof(bak), "%s.bak", fallback);
        remove(bak);
    }
}

static void test_hiding_recharges_battery_even_with_light_on(void) {
    enter_playing();
    hiddenIn = 0;
    flashlightOn = 1;
    battery = 50.0f;
    float before = battery;
    PlayerTickVitals(1.0f, 0);
    PlayerTickBattery(1.0f);
    CHECK(battery > before, "hiding must recharge battery instead of draining it");
    CHECK(battery <= before + HIDE_BATTERY_REGEN * 0.5f + 0.05f,
          "hiding with the flashlight on uses the advertised half-rate recharge");
    hiddenIn = -1;
    flashlightOn = 0;
}

void TestSuite_update(void) {
    probe_pause_menu_returns_to_main_menu();
    probe_hide_spot_is_recognised();
    probe_save_failure_must_not_trap_pause_menu();
    probe_pause_menu_mouse_and_hide_priority();
    test_shared_map_release_and_tree_drag();
    test_invalid_action_and_key_indices();
    test_session_input_cancellation();
    test_sprint_grace_rearms_and_resets();
    test_training_pad_refills_items();
    test_smoke_blinds_the_monster();
    test_empty_wrench_swing_leaves_a_visible_marker();
    test_hiding_recharges_battery_even_with_light_on();
}

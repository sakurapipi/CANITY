/* tests/domains/test_update.c —— Update() 里抽出来的块
 *
 * 第 4 轮（2026-11-08）：Update() 2,074 行，抽出两块自洽代码
 * （训练场补货、烟雾致盲）。这里通过 Update() 观测它们的行为 ——
 * 不把 static 函数暴露出去，测试就盯"玩家能看到的结果"。
 */
#include "../test_common.h"
#include "event_runtime.h"
#include "events.h"
#include "loop_private.h"
#include "loop_api.h"
#include "present_api.h"
#include "player_api.h"
#include "player_tick.h"
#include "game.h"
#include "run_rewards.h"
#include "entity.h"
#include "throwing.h"
#include "protocol.h"
#include "mimic.h"

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

#include "run_economy.h"
static void test_stable_rhythm_gold_cap(void){
    int training=trainingMode,level=currentLevel;trainingMode=0;currentLevel=4;RunEconomyReset();
    int amount=0;for(int i=0;i<20;i++)amount+=RunEconomyRhythmGold(100,100);
    CHECK(amount==90,"rhythm gold has a hard per-floor ceiling");
    CHECK(RunEconomyRhythmGold(100,100)==0,"repeating the minigame after the cap cannot print gold");
    trainingMode=1;CHECK(RunEconomyRhythmGold(100,100)==0,"training minigame coins never count as real rewards");
    trainingMode=training;currentLevel=level;RunEconomyReset();
}

/* 2.4.7-RC: regression code only; execution belongs to user acceptance. */
static void test_comfortable_vertical_projection(void) {
    int oldW=renderW,oldH=renderH,oldFlag=pitchLookEnabled;
    float oldPitch=pitchRatio,oldX=posX,oldY=posY;
    float oldDX=dirX,oldDY=dirY,oldPX=planeX,oldPY=planeY;
    renderW=800;renderH=601;
    posX=posY=0;dirX=1;dirY=0;planeX=0;planeY=.66f;
    pitchLookEnabled=1;
    const float distances[]={.24f,.35f,.5f,1,2,4};
    for(size_t d=0;d<sizeof(distances)/sizeof(distances[0]);d++) {
        float depth=distances[d];pitchRatio=0;UpdatePitch();
        SpriteBox level=ProjectSprite(depth,0,.5f,.1f);
        float unit=RenderProjectionScale(depth);
        float span=RenderProjectHeight(0,unit)-RenderProjectHeight(1,unit);
        for(int step=-10;step<=10;step++) {
            pitchRatio=step/10.0f;UpdatePitch();
            SpriteBox moved=ProjectSprite(depth,0,.5f,.1f);
            CHECK(isfinite(RenderProjectHeight(0,unit)) && isfinite(RenderProjectHeight(1,unit)),
                  "near-wall look keeps wall endpoints finite");
            CHECK(fabsf(RenderProjectHeight(0,unit)-RenderProjectHeight(1,unit)-span)<.01f,
                  "pitch translates wall endpoints without changing wall scale");
            CHECK(moved.width==level.width && abs(moved.height-level.height)<=1 &&
                  moved.centerX==level.centerX && moved.ty==level.ty,
                  "sprite scale and occlusion depth survive pitch and screen clipping");
            CHECK(abs(moved.baseY-level.baseY-horizon)<=1,
                  "sprite feet follow the same horizon as the wall");
            int row=(int)RenderProjectHeight(0,unit);
            CHECK(fabsf(RenderProjectionRowSlope(row)+RENDER_EYE_HEIGHT/depth)<.003f,
                  "floor inverse projection meets the wall bottom");
            CHECK(abs(horizon)<=renderH/4,"comfortable look stays within a quarter-screen shift");
        }
    }
    pitchRatio=NAN;UpdatePitch();CHECK(pitchRatio==0 && horizon==0,"invalid look resets safely");
    pitchRatio=INFINITY;UpdatePitch();CHECK(pitchRatio==0 && horizon==0,"infinite look resets safely");
    pitchRatio=1;pitchLookEnabled=0;UpdatePitch();
    CHECK(pitchRatio==0 && horizon==0,"disabled look restores a horizontal projection");
    renderW=oldW;renderH=oldH;pitchLookEnabled=oldFlag;pitchRatio=oldPitch;
    posX=oldX;posY=oldY;dirX=oldDX;dirY=oldDY;planeX=oldPX;planeY=oldPY;UpdatePitch();
}

static void test_pitch_gate_and_near_wall_input(void) {
    float oldX=posX,oldY=posY,oldPitch=pitchRatio;int oldFlag=pitchLookEnabled;
    int cells[3][3];
    for(int y=0;y<3;y++)for(int x=0;x<3;x++) {
        cells[y][x]=worldMap[9+y][9+x];worldMap[9+y][9+x]=0;
    }
    worldMap[10][11]=1;posX=10.75f;posY=10.5f;
    pitchLookEnabled=1;pitchRatio=.4f;RenderAddPitchInput(.4f);
    CHECK(fabsf(pitchRatio-.8f)<.001f,"upward look remains usable at collision distance");
    RenderAddPitchInput(-.9f);
    CHECK(fabsf(pitchRatio+.1f)<.001f,"near-wall input can cross level without a proximity lock");
    /* An in-front, ground-anchored model exercises legacy screen clipping. */
    float wx=posX+dirX,wy=posY+dirY;
    pitchLookEnabled=0;UpdatePitch();SpriteBox off=ProjectSprite(wx,wy,0,1);
    pitchLookEnabled=1;pitchRatio=0;UpdatePitch();SpriteBox zero=ProjectSprite(wx,wy,0,1);
    CHECK(off.ok==zero.ok && off.ty==zero.ty && off.centerX==zero.centerX &&
          off.baseY==zero.baseY && off.width==zero.width && off.height==zero.height &&
          off.x0==zero.x0 && off.x1==zero.x1 && off.y0==zero.y0 && off.y1==zero.y1,
          "zero-degree look has identical model dimensions to disabled look");
    pitchRatio=1;UpdatePitch();pitchLookEnabled=0;
    SpriteBox disabled=ProjectSprite(wx,wy,0,1);
    CHECK(disabled.baseY==off.baseY && disabled.height==off.height &&
          RenderProjectionCenterY()==(float)(renderH/2),
          "OFF gate ignores the previous enabled frame even before the next tick");
    UpdatePitch();CHECK(pitchRatio==0 && horizon==0,"OFF clears all vertical offset immediately");
    for(int y=0;y<3;y++)for(int x=0;x<3;x++)worldMap[9+y][9+x]=cells[y][x];
    posX=oldX;posY=oldY;pitchRatio=oldPitch;pitchLookEnabled=oldFlag;UpdatePitch();
}

static void test_exit_choices_require_confirmation(void) {
    StartFreshTestRun();currentState=STATE_PLAYING;trainingMode=0;runInProgress=1;exitGoDeeper=0;
    int bank=bankGold;
    unsigned char event[EVENT_WIRE_BYTES]={0};
    event[0]=1;event[4]=(unsigned char)currentLevel;event[8]=EV_FLOOD+1;
    CHECK(EventWireRestore(event,sizeof event,currentLevel),"exit scoring fixture restores");
    int previousScore=eventBonus;
    RunExitChoiceOpen();exitChoiceT=.25f;RunExitChoiceKey('2');
    CHECK(exitChoiceOpen && exitChoiceConfirm && runInProgress && bankGold==bank,"extract opens confirmation without ending the run or settling gold");
    exitChoiceT=.25f;RunExitChoiceKey(VK_SPACE);
    CHECK(exitChoiceOpen && !exitChoiceConfirm,"space safely cancels extraction confirmation");
    CHECK(eventBonus==previousScore,"canceling extraction does not grant event completion score");
    exitChoiceT=.25f;RunExitChoiceKey(VK_SPACE);
    CHECK(!exitChoiceOpen && exitGoDeeper && currentState==STATE_PLAYING,"space on the main exit panel continues the run");
    exitGoDeeper=0;RunExitChoiceOpen();exitChoiceT=.25f;
    RECT row;ExitChoiceButtonRect(1,&row);CanityMouseClick((row.left+row.right)/2,(row.top+row.bottom)/2);
    CHECK(exitChoiceConfirm && runInProgress,"mouse extraction also needs the same confirmation");
    exitChoiceT=.25f;ExitChoiceButtonRect(0,&row);CanityMouseClick((row.left+row.right)/2,(row.top+row.bottom)/2);
    CHECK(!exitChoiceConfirm && exitChoiceOpen,"mouse can return safely from extraction confirmation");
    exitChoiceT=.25f;RunExitChoiceKey('1');
    CHECK(exitGoDeeper && !exitChoiceOpen && runInProgress,"option one is always continue, never immediate extraction");
    exitGoDeeper=0;RunExitChoiceOpen();exitChoiceT=.25f;RunExitChoiceKey('2');
    exitChoiceT=.25f;RunExitChoiceKey('2');
    CHECK(currentState==STATE_GAMEOVER && !runInProgress && !exitChoiceOpen,"only explicit confirmed extraction ends and settles the run");
    CHECK(eventBonus==previousScore+EventDefinition(EV_FLOOD)->score,"confirming extraction grants the event score once");
    StartFreshTestRun();
}
static void test_three_peaceful_missions(void) {
    enter_playing();
    ProtocolBegin(1,0);
    for (int chapter=1;chapter<=3;chapter++) {
        GenerateLevel();
        CHECK(ProtocolPeacefulStory() && currentLevel==chapter && protocolRun.role==chapter,
              "story starts each chapter with its assigned role");
        CHECK(!ProtocolExitReady() && ProtocolAdvance()==0,"story gate requires its objectives");
        CHECK(!ProtocolNodeAvailable(0),"prologue disables role node purchases");
        CHECK(!mimicCount && !WardenActive() && !CollectorActive() && !SpiderActive(),
              "peaceful story has no special encounter population");
        for (int i=0;i<MON_HUNTER_MAX;i++)
            CHECK(!monHunters[i].active,"peaceful story has no hunters");
        for (int i=0;i<LIGHTBEAST_MAX;i++)
            CHECK(!lightBeasts[i].active,"peaceful story has no light beasts");
        float x,y,signal;
        CHECK(ProtocolProp(PROTOCOL_EVIDENCE,&x,&y)==1,"unsolved station is available");
        posX=x-1;posY=y;
        dirX=1;dirY=0;
        ProtocolInteract(PROTOCOL_EVIDENCE);
        CHECK(!ProtocolTrialVisual(&x,&y,&signal),"missing evidence blocks trial activation");
        CHECK(ProtocolProp(PROTOCOL_RELAY,&x,&y)==1,"key record awaits inspection");
        posX=x-1;posY=y;
        CHECK(ProtocolInteract(PROTOCOL_RELAY),"the player can inspect the required evidence");
        CHECK(ProtocolProp(PROTOCOL_EVIDENCE,&x,&y)==1,"unsolved station remains available");
        posX=x-1;posY=y;
        CHECK(ProtocolInteract(PROTOCOL_EVIDENCE) && ProtocolTrialVisual(&x,&y,&signal),
              "examining evidence unlocks the station cycle");
        if (chapter==3)
            for (int tick=0;tick<64;tick++) ProtocolCinematicTick(.05f);
        ProtocolActivate();
        for (int tick=0;tick<130 && !(protocolRun.flags&PROTOCOL_MISSION_SOLVED);tick++) {
            ProtocolTick(.05f);
            ProtocolCinematicTick(.05f);
        }
        CHECK((protocolRun.flags&PROTOCOL_MISSION_SOLVED) && !ProtocolExitReady(),
              "the role ability solves the live trial while exit awaits terminal");
        CHECK(ProtocolProp(PROTOCOL_TERMINAL,&x,&y)==1,"terminal is reachable after solving");
        posX=x-1;posY=y;
        CHECK(ProtocolInteract(PROTOCOL_TERMINAL) && ProtocolExitReady(),
              "reading the terminal opens evacuation");
        CHECK(ProtocolAdvance()==(chapter==3?2:1),"chapter advances or completes the campaign");
    }
    bankGold=0;
    carryGold=PROTOCOL_STORY_SUPPLIES+PROTOCOL_STORY_BANK_CAP+300;
    protocolProfile.endings&=~4u;
    CHECK(ProtocolStoryPayout()==PROTOCOL_STORY_BANK_CAP+500 &&
          bankGold==PROTOCOL_STORY_BANK_CAP+500,
          "first prologue settlement adds the capped earnings and one-time reward");
    carryGold=PROTOCOL_STORY_SUPPLIES;
    CHECK(ProtocolStoryPayout()==0,"starting supplies and the one-time reward cannot pay again");
    StartFreshTestRun();
}

void TestSuite_update(void) {
    test_comfortable_vertical_projection();
    test_pitch_gate_and_near_wall_input();
    probe_pause_menu_returns_to_main_menu();
    probe_hide_spot_is_recognised();
    probe_save_failure_must_not_trap_pause_menu();
    probe_pause_menu_mouse_and_hide_priority();
    /* 2026-11-16：上下视角改可开关。默认关 → 输入被丢弃；打开 → 跟随并夹取。
       （原 v2.4.3 断言"永远为 0"，现在随开关变化。） */
    {
        float savedPitch = pitchRatio;
        int savedFlag = pitchLookEnabled;
        float savedX=posX,savedY=posY;int savedCells[3][3];
        for(int y=0;y<3;y++)for(int x=0;x<3;x++) {
            savedCells[y][x]=worldMap[9+y][9+x];worldMap[9+y][9+x]=0;
        }
        posX=10.5f;posY=10.5f;

        pitchLookEnabled = 0;
        pitchRatio = .5f;
        CanityMouseLook(0, 60);
        CHECK(pitchRatio == 0, "vertical input is ignored while Vertical Look is off");
        UpdatePitch();
        CHECK(pitchRatio == 0, "level camera keeps the projection horizontal");

        pitchLookEnabled = 1;
        pitchRatio = 0;
        CanityMouseLook(0, 60);
        CHECK(pitchRatio < 0, "vertical input moves the camera when Vertical Look is on");
        CanityMouseLook(0, 100000);
        CHECK(pitchRatio >= -1.0f, "pitch is clamped at the low end");
        CanityMouseLook(0, -100000);
        CHECK(pitchRatio <= 1.0f, "pitch is clamped at the high end");

        pitchLookEnabled = savedFlag;
        pitchRatio = savedPitch;
        posX=savedX;posY=savedY;
        for(int y=0;y<3;y++)for(int x=0;x<3;x++)worldMap[9+y][9+x]=savedCells[y][x];
        UpdatePitch();
    }
    /* 2026-11-16（审计修复回归）：**Vertical Look 必须能存住**。
       只验"开关能改镜头"不够 —— 重启丢设置正是这一版修的缺陷。
       这里走真实的序列化出口：写一个带 pitchLook=1 的 blob，再读回来。 */
    {
        extern int SaveTestSerialize(const SaveBlob* blob, unsigned char** out);
        extern int SaveTestDeserialize(const void* buf, long n, SaveBlob* out);
        SaveBlob a;
        unsigned char* sbuf = NULL;
        int sn;
        memset(&a, 0, sizeof(a));
        a.magic = SAVE_MAGIC;
        a.version = SAVE_VERSION;
        a.pitchLook = 1;
        sn = SaveTestSerialize(&a, &sbuf);
        CHECK(sn > 0 && sbuf != NULL, "pitchLook blob must serialize");
        if (sn > 0 && sbuf) {
            SaveBlob b;
            memset(&b, 0, sizeof(b));
            CHECK(SaveTestDeserialize(sbuf, (long)sn, &b) > 0 && b.pitchLook == 1,
                  "pitchLook must round-trip through the save file");
            free(sbuf);
        }
    }
    test_exit_choices_require_confirmation();
    test_stable_rhythm_gold_cap();
    test_shared_map_release_and_tree_drag();
    test_invalid_action_and_key_indices();
    test_session_input_cancellation();
    test_sprint_grace_rearms_and_resets();
    test_training_pad_refills_items();
    test_smoke_blinds_the_monster();
    test_empty_wrench_swing_leaves_a_visible_marker();
    test_hiding_recharges_battery_even_with_light_on();
    test_three_peaceful_missions();
}

/* Orchestration ownership split; stage ordering remains in the entry point. */
#include "common.h"
#include "echo_marks.h"
#include "run_economy.h"
#include "hush.h"
#include "room_theme.h"
#include "loop_private.h"
#include "art_direction.h"
#include "core_tick.h"
#include "lockbox.h"
#include "mimic.h"
#include "echo_marks.h"
#include "run_stats.h"
#include "threat.h"
#include "threat_telegraph.h"
#include "training_stations.h"
#include "map_validator.h"
#include "hatch.h"
#include "cell_meta.h"
#include "map_rooms.h"
#include "sound_event.h"
#include "state.h"
#include "economy.h"
#include "throwing.h"
#include "uptimers.h"
#include "warden.h"
#include "hints.h"
#include "loop_api.h"
#include "calibration.h"
#include "wrench.h"
#include "item_effects.h"
#include "player_tick.h"
#include "interaction.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "phase.h"
#include "collapse.h"
#include "core_rng.h"
#include "difficulty.h"
#include "detect_alert.h"
#include "control_effect.h"
#include "spider.h"
#include "plat.h"
#include "build_tags.h"
#include "neutral.h"
#include "suspicion.h"
#include "weapons.h"
#include "shop.h"
#include "bench.h"
#include "events.h"
#include "achievements.h"
#include "telemetry.h"
#include "daily.h"
#include "steam_zone.h"
#include "music.h"
#include "wallet.h"
#include "campaign.h"
#include "upgrade_run.h"
#include "relics.h"
#include "sprites.h"
#include "extras.h"
#include "gamepad.h"
#include "lang.h"
#include "entity.h"
#include "render.h"
#include "ui.h"
#include "audio.h"
#include "save.h"
#include "skills.h"
#include "content_db.h"
#include "bestiary.h"
#include "score.h"
#include "loop_private.h"

static unsigned int s_previousHuntMask;
static int s_hidingEntireHunt;
void LoopResetHuntAchievements(void) {
    s_previousHuntMask=0u;
    s_hidingEntireHunt=0;
}

static float s_decoyKeyCd = 0.0f;



/* 2026-11-05：给 HUD 用 —— 玩家是否真的站在门口。
   安全区（1.4 格）比门口（1.05 格）大，所以「在安全区里但没进门」
   是一段真实存在的状态，提示要能区分这两件事。 */

/* 训练场每帧补货：无限道具、不掉理智、标本固定不动。
 * 第 4 轮从 Update() 抽出来 —— 它只用全局与 dt，不碰外层局部变量。 */
static void UpdateTrainingPad(float dt) {
        /* v3.3：投掷物重做后训练场要给全六种，方便逐个试手感。
           对应槽位：光球 / 投石 / 烟雾 / 胶水 / 回声 / 诱饵 */
        flares = 99;
        bottles = 99;      /* 投石 */
        smokeBombs = 99;
        gluePots = 99;
        echoBolts = 99;
        decoys = 99;
        /* 已删的三种清零，免得老存档带进来 */
        bricks = 0; acids = 0; firePots = 0; mirrors = 0;
        sanity = 100.0f;
        /* 教程里手电电量会正常消耗，这样「按住 R 摇电」才有可见效果；
           只做上限夹取，不再每帧强行充满。 */
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        stamina = StaminaMax();
        Hunter0()->active = 1;
        Hunter0()->state = 2;              /* 原地不动，当标本 */
        Hunter0()->stun = 0.0f;
        if (trainingDpsTimer > 0.0f) trainingDpsTimer -= dt;
        else trainingDps = 0;
}

/* 烟里的怪丢失目标 —— 这才是"遮蔽视线"该有的效果。
 * 第 4 轮从 Update() 抽出来 —— 只碰全局（monster / lightBeast / monHunters / smokes）。 */
static void UpdateSmokeBlindness(void) {
    /* v2.2：烟幕原来**只压暗玩家自己**（怪完全不看烟），等于自伤道具。
       现在：烟里的怪丢失目标 —— 这才是"遮蔽视线"该有的效果。 */
    for (int si = 0; si < MAX_SMOKE; si++) {

        if (!smokes[si].active) continue;
        if (Hunter0()->active && Dist2(Hunter0()->x, Hunter0()->y, smokes[si].x, smokes[si].y) < 2.6f) {
            Hunter0()->state = 0;
            Hunter0()->pathLen = 0;
        }
        if (lightBeast.active && Dist2(lightBeast.x, lightBeast.y, smokes[si].x, smokes[si].y) < 2.6f) {
            lightBeast.state = 0;
            lightBeast.pathLen = 0;
        }
        /* 2026-11-03：第 2、3 只听声鬼以前**完全不吃烟** ——
           烟只写了 monster 和 lightBeast，扩到三只之后这里没跟着改。 */
        for (int hi = 1; hi < MON_HUNTER_MAX; hi++) {

            if (!monHunters[hi].active) continue;
            if (Dist2(monHunters[hi].x, monHunters[hi].y, smokes[si].x, smokes[si].y) < 2.6f) {
                monHunters[hi].state = 0;
                monHunters[hi].pathLen = 0;
            }
        }
    }
}

/* Private update stages: return 0 to stop the current tick. */
static int UpdatePreflight(float dt) {

    if(calibActive && (currentState==STATE_PLAYING || currentState==STATE_SAFEHOUSE)){
        CalibTick(dt);CalibHandleInput(0);return 0;
    }
    if(currentState==STATE_SAFEHOUSE){ShopTickCooldown(dt);CalibTick(dt);}
    if(calibActive && currentState==STATE_SAFEHOUSE)return 0;
    TickFrameBegin();   /* 第 4 轮：每帧复位 tick 顺序守卫 */
    {  playerMoveSpeedNow = 0.0f; }   /* v8: 守门人判断「在动/冲刺」用 */
    /* v7：可疑点表每帧衰减 / 过期 */
    {  gameTime += dt; }   /* v2.4.3 全局秒表 */
    SuspicionTick(dt);
    /* v2.4-beta B01：清掉"本帧已见的 eventId"去重表。
       放在 Update 的帧首，和 DailyTick 同一处 —— 两者都是"每帧一次"的簿记。 */
    {  SoundEventTick(); }
    /* v2.4-beta B02：把玩家位姿喂给音频层，世界音效才能算出左右声像。
       每帧一次；音频线程只读快照，不做 BFS、不分配内存。 */
    { 
      AudioUpdateListener(posX, posY, dirX, dirY); }
    DailyTick();   /* Calendar rollover also applies while in menus. */
    if(currentState==STATE_PLAYING && exitChoiceOpen && !keyPressed['1'] && !keyPressed['2']) return 0;
    if(currentState==STATE_PLAYING && !trainingMode) {
        unsigned int chasing=0u; int soundHunters=0;
        for(int i=0;i<MON_HUNTER_MAX;i++) {
            Monster* actor=&monHunters[i];
            if(actor->active && actor->state==1 && Dist2(actor->tx,actor->ty,posX,posY)<4.0f) {
                chasing |= 1u<<i; soundHunters++;
            }
        }
        for(int i=0;i<2;i++) if(lightBeasts[i].active && lightBeasts[i].state==1)
            chasing |= 1u<<(MON_HUNTER_MAX+i);
        if(soundHunters>=2) {
            if(doubleHuntSeconds<30.0f) doubleHuntSeconds+=dt;
        } else if(doubleHuntSeconds<30.0f) doubleHuntSeconds=0.0f;
        /* Arm only when a hunt begins while already hidden; entering midway is not "entire". */
        if(hiddenIn<0) s_hidingEntireHunt=0;
        else if(chasing && !s_previousHuntMask) s_hidingEntireHunt=1;
        if(s_hidingEntireHunt && !chasing) { fullHideDone=1; s_hidingEntireHunt=0; }
        s_previousHuntMask=chasing;
    } else { s_previousHuntMask=0; s_hidingEntireHunt=0; }
    {   /* v7 B4：第一次携带超过 8000 时提一句 */
        if (carryGold >= 8000) HintOnce(4);
    }
    if(currentState==STATE_PLAYING && !exitChoiceOpen) { UpdateCollector(dt); }
    {   /* v7：清道夫 */

        if (currentState == STATE_PLAYING) floorTime += dt;
        if(currentState==STATE_PLAYING && !exitChoiceOpen) { ScavengerTickTrigger(dt); UpdateScavenger(dt); }
    }
    /* v7：扳手冷却递减 */
    {  if (wrenchCd > 0.0f) { wrenchCd -= dt*SkillCounterCooldownRate(); if (wrenchCd < 0.0f) wrenchCd = 0.0f; } }

    /* Playable timers are advanced only after the pause/map gate. */
    /* v2.1 屏幕特效推进：抖动 / 红闪 / 枪口闪 */
    PresentTickFeedback(dt);

    /* v2.9：剑气命中的顿帧 —— 整个逻辑冻结几十毫秒，打击感就出来了 */
    if (slashHitStop > 0.0f) {
        slashHitStop -= dt;
        if (slashHitStop < 0.0f) slashHitStop = 0.0f;
        return 0;
    }
    if (flashTime > 0.0f) flashTime -= dt;
    if (healFlash > 0.0f) healFlash -= dt;
    /* 全屏地图：游戏**暂停**（专心规划路线）。
       小窗地图：游戏**继续**（边看边玩，鼠标仍能转视角）。 */
    /* ===== v3.3：**看地图要耗电（10/秒）** =====
       用户原话：「开地图每秒钟点 10 点电量怎么不见了？
       我们需要这个来平衡强度啊，不然一直开着地图怎么玩。」

       这是**强度闸门**，不是惩罚：
       · 地图是唯一能规划路线的工具，不设成本就等于「一直开着」
       · 10/秒 意味着开 10 秒 = 一块电池的 1/3，想细看就要付电费
       · 藏身点里开着地图仍然划算（藏身自带 +4/秒 回电，净 -6/秒），
         所以「躲起来慢慢研究路线」依然是正确的解法
       · 电耗尽会自动关图并熄灯，不会被地图耗死

       （v2.4 的注保留：必须在「全屏地图暂停」判断之前 ——
        全屏看图时整个 Update 会提前 return，放后面就永远不扣电。） */
    /* 2026-11-05 用户要求：
       · **大地图不耗电**（全屏规划路线是免费的）
       · 小地图耗电，且可以被局内强化「小地图省电」降低（10 → 4 /秒） */
    if (isMapOpen && !mapFullScreen && currentState == STATE_PLAYING) {
        battery -= RunUpgMapDrainPct() * dt;
        if (battery <= 0.0f) {
            battery = 0.0f;
            isMapOpen = 0;
            mapFullScreen = 0;
            /* v8：战役第 1 关的规则是“手电关不掉”，地图耗电把灯关掉
               等于绕过了规则。这里保留开启状态（电量仍是 0，会自然变暗）。 */
            { 
              flashlightOn = CampaignFlashlightLocked() ? 1 : 0; }
            ForceCursor(0);
            firstMouseMove = 1;
            uiDirty = 1;
        }
    }

    if (currentState != STATE_PLAYING || (isMapOpen && mapFullScreen)) return 0;
    if (flashTime > 0.0f) return 0;

    /* 2026-11-06（子代理审计·中 8）：这三个只在真正游玩时推进 */

    return 1;
}

static int UpdatePlayableTimers(float dt, int* safe) {
    HushTick(dt);
    RoomThemeTick(dt);
    CalibTick(dt);   /* 校准小游戏计时（冷却与滑块） */
    MimicTick(dt);   /* 宝箱怪状态机 */
    {  EchoMarksTick(dt); }   /* 回声标记寿命 */

    runTime += dt;
    if (levelBanner > 0.0f) levelBanner -= dt;

    /* 成就检查 */
    LoopCheckAchievements();

    /* 状态效果计时。强度变化时重算 FOV（视野收窄要立刻生效） */
    if (UpdateFx(dt)) ApplyFov();

    /* v2.5：安全屋判定必须**在理智流失之前**算出来。
       原来这段在后面，于是安全屋里的基础流失照扣，而回智又被
       SAFE_HEAL_CAP(60) 挡住 —— 理智 70 走进安全屋会看到理智继续下降。 */
    int inSafe = (safeRoom.active &&
                  Dist2(posX, posY, safeRoom.x, safeRoom.y) < SAFE_ZONE_RADIUS * SAFE_ZONE_RADIUS);

    /* ===== v2.4.98 **出口也算安全区** =====
       用户报告的 bug：
       「我刚好到撤离点，同时清道夫刷新，我再撤一点没选继续/撤离，
         清道夫追上把我杀了，直接死亡。」

       根因：出口原来只是"按到 0.55 格内弹面板"，**不提供任何保护**。
       怪照追、照抓、照扣理智；理智一归零就直接判死。

       现在：
         · 距出口 1.4 格内 → 算安全区（怪不抓你）
         · 撤离选择面板打开期间 → **无条件安全** */
    {

        if (breakerOpen && Dist2(posX, posY, exitX, exitY) < 1.4f * 1.4f) inSafe = 1;
        if (exitChoiceOpen) inSafe = 1;
    }

    /* v3.0：**开锁盘时暂停理智流失** ——
       用户实测「开锁装置导致精神崩溃」：
       锁盘是模态层，玩家不能动也不能跑，理智却照扣。
       解一把弦线锁要 30~60 秒，按第 1 层 0.5/秒算是 15~30 点理智，
       到了第 10 层就是 30~60 点 —— 解谜变成自杀。
       现在把开锁当作和暂停同等的状态：理智冻结。 */
    /* 2026-11-05：过载 / 锯锁是**按住不放**的，需要逐帧推进。
       必须在下面的 if (lockActive) return 之前调用，否则永远不动。 */
    {  LockTick(dt); }
    if (lockActive) return 0;

    /* v3.3 **藏身回电** ——
       用户要求：「在隐藏点的时候，手电自动充电」。
       躲在藏身点里、且手电关着时回电最快；
       开着手电也回（毕竟在充电），但慢一半 —— 这样「藏起来充电」
       和「开着灯继续看」之间有取舍。 */

    *safe = inSafe;

    return 1;
}

static int UpdateSurvival(float dt, int inSafe) {
    if (!PlayerTickVitals(dt, inSafe)) return 0;
    if (sanity <= 0.0f) {
        sanity = 0.0f;
        runInProgress = 0;
        if (currentLevel > bestLevel) bestLevel = currentLevel;
        /* v2.0 SALVAGE：死亡时保留一半待上缴金币 */
    /* v6：这里原本是一加一减两个空操作（净效果 0），
       是合并货币时留下的。已删除。
       死亡时的保留比例统一由 WalletSettle 的 BANK_DEATH_REFUND 处理。 */
    gameWon = 0;
        BankSettle(0);          /* v3.4：死亡结算 —— 本金返还 50% */
        /* v6 **死亡后不允许 Continue 复活** ——
           原来 hasSave 死亡后仍是 1，回主菜单点 Continue 能把死前存档救回来，
           死亡被完全撤销（银行还多扣一次死亡结算）。 */
        hasSave = 0;
        bankStreak = 0;
        {
            ScoreInput si = { currentLevel, 0, isEndlessRun, runSearched,
                              bottlesKilled, runTime, modsSeenCount };
            ComputeScore(&si, &lastScore);
            if (isEndlessRun && lastScore.score > bestEndlessScore) bestEndlessScore = lastScore.score;
            if (!isEndlessRun && lastScore.score > bestMainScore) bestMainScore = lastScore.score;
        }
        currentState = STATE_GAMEOVER;
        menuSelection = 0;
        ForceCursor(1);
        /* 2026-11-06（R4）：光标样式归平台层；ForceCursor(1) 已处理可见性 */
        uiDirty = 1;
        return 0;
    }

    /* 手电筒耗电：电量耗尽自动熄灭（ECO 升级降低耗电） */
    /* POWER SIPHON：低电涓流回电。必须放在 if(flashlightOn) 外面 ——
       原来被包在手电分支里，实际效果是「电量见底 → 手电自动熄灭 → 再也回不来」，
       偏偏这个技能就是为电量见底准备的。 */
    PlayerTickBattery(dt);

    return 1;
}

static int UpdateCoverInput(void) {
    if(calibActive){CalibHandleInput(0);return 0;}
    if(!guideMode && keyPressed['B']){keyPressed['B']=0;if(!trainingMode){SaveGame();if(g_saveError)return 0;}LoopQueueStart(START_GUIDE,0);StartGame();return 0;}
    /* 先判断交互目标，决定这一帧能不能移动 */
    /* ===== v3.3 交互目标优先级重排 =====
       试玩实测卡关：「站在带锁箱子前时配电箱就在正前方，
       长按 E 永远命中箱子（反复弹锁盘），我始终没法给配电箱通电，
       也就无法通关第一层。」

       根因：锁盘是**模态层**，一旦打开就得先解完/退出来，
       而它和配电箱抢同一个 E —— 谁近谁赢，锁箱经常更近。

       现在的优先级（从高到低）：
         ① 配电箱   —— 直接推进关卡，必须永远能选到
         ② 未上锁的箱子 —— 翻一下就完事，无副作用
         ③ 上锁的箱子   —— 会把玩家拖进模态层，优先级最低
       这样「站在锁箱前也能给配电箱通电」，卡关消失。 */
    InteractionRefreshTargets();

    /* v3.1：藏身提示范围从 2.6 格收到 1.2 格。
       原来是一个直径 5 格的圆，站在房间中间就提示「按 E 躲藏」，
       玩家会以为整片空地都能藏；其实藏身点是一个具体位置。 */
    hideSpotNear = -1;

    /* v3.0：上锁的箱子**按一下 E 就弹锁盘**，不走长按翻箱；
       没锁的仍然长按搜索。 */
    if (hoverKind == 1 && hoverIndex >= 0 && hoverIndex < MAX_CONTAINERS &&
        containers[hoverIndex].active && !containers[hoverIndex].searched &&
        containers[hoverIndex].locked && !lockActive) {
        if (IsActionPressed(ACT_INTERACT)) {
            LockOpenFor(hoverIndex, containers[hoverIndex].lockType);
            keyPressed[g_bindings[ACT_INTERACT]] = 0;
            return 0;
        }
        holdingNow = 0;      /* 上锁时按住 E 不翻箱 */
    } else {
        /* v3.0：藏身点 —— 找最近的一个，按 E 进出 */
    {
        float bestHD = 1.2f * 1.2f;
        for (int i = 0; i < hideSpotCount; i++) {
            if (!hideSpots[i].active) continue;
            float dx = hideSpots[i].x - posX, dy = hideSpots[i].y - posY;
            float d = dx * dx + dy * dy;
            if (d >= bestHD) continue;
            /* 只提示“看得见的垃圾桶”：
               ① 必须在视线前方（背后的桶不提示，避免走在空地上突然冒出 HIDE）
               ② 玩家到桶之间不能隔墙
               ③ 距离 1.2 格以内 */
            if (dx * dirX + dy * dirY < 0.35f) continue;
            {
                int steps = (int)(sqrtf(d) * 10.0f) + 1;
                int blocked = 0;
                for (int s = 1; s < steps; s++) {
                    float t = (float)s / (float)steps;
                    int cx = (int)floorf(posX + dx * t);
                    int cy = (int)floorf(posY + dy * t);
                    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT ||
                        worldMap[cy][cx]) { blocked = 1; break; }
                }
                if (blocked) continue;
            }
            bestHD = d;
            hideSpotNear = i;
        }
        /* v3.0：**E 必须松开再按才算下一次** ——
           按住 0.5 秒时 WM_KEYDOWN 会自动重复，每次重复都置 keyPressed，
           于是「出来」刚执行完又被「进去」接上，净效果是 hiddenIn 恒为 0
           （验证报告 BUG2）。加一个锁存：只有键松开过才允许再次切换。 */
        {
            static int hideKeyLatch = 0;
            if (!ActionIsDown(ACT_INTERACT)) hideKeyLatch = 0;
            if (IsActionPressed(ACT_INTERACT) && hideKeyLatch) {
                keyPressed[g_bindings[ACT_INTERACT]] = 0;
                /* 吞掉这次重复，不切换 */
                goto hide_done;
            }

        /* 在藏身点里再按 E → 出来 */
        if (hiddenIn >= 0) {
            if (IsActionPressed(ACT_INTERACT)) {
                hideKeyLatch = 1;
                hiddenIn = -1;
                pickupMsg = g_lang == LANG_ZH ? "出来了" : "YOU LEAVE THE COVER";
                pickupMsgTimer = 1.0f;
                keyPressed[g_bindings[ACT_INTERACT]] = 0;
                uiDirty = 1;
            }
        } else if (hideSpotNear >= 0 && hideSpots[hideSpotNear].broken) {
                    /* v6 每日限制：不能用藏身点 */
                    { 
                      if (DailyForbidHide()) { pickupMsg = "TODAY: NO HIDE SPOTS"; pickupMsgTimer = 1.6f; hideKeyLatch = 1; } }
            /* ===== v2.4.17 **修复藏身点** =====
               清道夫撬坏的点按 E 花钱修。每修一次更贵。 */

            float cost = HIDESPOT_REPAIR_BASE;
            for (int rc = 0; rc < hideRepairCount; rc++) cost *= HIDESPOT_REPAIR_STEP;
            if (cost > 99999.0f) cost = 99999.0f;

            if (IsActionPressed(ACT_INTERACT)) {
                hideKeyLatch = 1;
                keyPressed[g_bindings[ACT_INTERACT]] = 0;
                if (carryGold >= cost && hideRepairCount < HIDESPOT_REPAIR_MAX) {
                    {  WalletSpendRun(cost); }
                    hideSpots[hideSpotNear].broken = 0;
                    hideRepairCount++;
                    pickupMsg = (g_lang == LANG_ZH) ? "藏身点修好了" : "HIDE SPOT REPAIRED";
                    pickupMsgTimer = 1.6f;
                    PlaySfx(SFX_GLASS, 0.6f);
                } else if (hideRepairCount >= HIDESPOT_REPAIR_MAX) {
                    pickupMsg = (g_lang == LANG_ZH) ? "修不了了（太破了）" : "TOO BROKEN TO REPAIR";
                    pickupMsgTimer = 1.6f;
                } else {
                    pickupMsg = (g_lang == LANG_ZH) ? "金币不够修这个" : "NOT ENOUGH GOLD";
                    pickupMsgTimer = 1.6f;
                }
                uiDirty = 1;
            }
        } else if (hideSpotNear >= 0 && hoverKind == 0 && hideBlockTimer <= 0.0f) {
            /* v3.0 修复：**附近有可交互的箱子时，藏身点不抢 E** ——
               原来藏身点无条件抢在容器前面消费按键
               （用户实测：箱子和躲藏点刷在一起时箱子开不了）。 */
            if (IsActionPressed(ACT_INTERACT)) {
                hideKeyLatch = 1;
                hiddenIn = hideSpotNear;
        {  HintOnce(1); }   /* v7 B4 */
        {  hideStreak++; }   /* v7：清道夫触发条件 */
                hideCount++;   /* v3.3：逃生向成就用 */
                posX = hideSpots[hiddenIn].x;
                posY = hideSpots[hiddenIn].y;
                UnstickPlayer();   /* 柜子中心被判阻塞时别卡死 */

                /* v3.0：**当着他的面钻进去 = 他会来翻** ——
                   原来钻进柜子怪物 100% 立刻丢目标，太安全了。
                   如果它此刻正在追你（state==1）而且离得近（6 格内），
                   它会跑到柜子前徘徊一阵，而不是当场忘掉你。 */
                if (Hunter0()->active) {
                    float hdx = Hunter0()->x - posX, hdy = Hunter0()->y - posY;
                    int sawYou = (Hunter0()->state == 1) && (hdx * hdx + hdy * hdy < 36.0f);
                    Hunter0()->state = sawYou ? 2 : 0;      /* 2 = 原地搜寻 */
                    Hunter0()->tx = hideSpots[hiddenIn].x;
                    Hunter0()->ty = hideSpots[hiddenIn].y;
                    Hunter0()->stateTimer = sawYou ? 5.0f : 0.0f;
                    Hunter0()->repath = 0.0f;
                    if (sawYou) {
                        pickupMsg = g_lang == LANG_ZH ? "他看见你进来了" : "IT SAW YOU HIDE";
                        pickupMsgTimer = 1.8f;
                    }
                }
                pickupMsg = g_lang == LANG_ZH ? "藏好了  （再按 E 出来）" : "HIDDEN  (E TO LEAVE)";
                pickupMsgTimer = 1.6f;
                keyPressed[g_bindings[ACT_INTERACT]] = 0;
                uiDirty = 1;
            }
        }
        hide_done: ;
        }
    }

    holdingNow = (!calibActive && hoverKind != 0 && ActionIsDown(ACT_INTERACT));
    }

    return 1;
}

static int UpdateEarlyActions(float dt) {
    /* ===== v3.0：主动大招（Q 键） =====
       原来 runUltimate 只被存读档，**从来没有激活入口** ——
       三选一拿到的大招等于永远用不出来。现在按 Q 释放，一局一次。 */
    if (ultTimer > 0.0f) ultTimer -= dt;
    /* v3.3：**B 键开/关训练营的分步引导** ——
       进 Tutorial 时默认不展开，只提示「按 B 激活引导」。 */
    /* v3.3：**T 在不能开店的地方也要给一句反馈** ——
        v6 **出口二选一**
       1 = 撤离（立刻按 携带倍数 × 深度倍数 结算，本局结束）
       2 = 深入（继续下一层）
       ESC 不生效 —— 必须选一个。 */
    if (exitChoiceOpen && currentState == STATE_PLAYING) {
        if (keyPressed['1']) {
            keyPressed['1'] = 0;
            {

                float dm = ExtractDepthMul(currentLevel);
                /* v7 第 ㊿ 轮：**这里原来自己减自己**
                   profit = carryGold - carry，而 carry 就是 carryGold
                   → 恒 0 → 深度倍数一分不加 → 「越深撤离越值钱」不存在。 */
                int profit = carryGold - carryAtRunStart;
                if (profit < 0) profit = 0;
                int extra=0; /* Depth and contract are applied once by BankSettle. */
                WalletAddRun(extra);
                exitChoiceOpen = 0;
                lastExtractDepthMul = dm;
                /* v6 **必须先清 runInProgress** —— BankSettle 内部会 SaveGame，
                   原来在它之后才清零，存出一份「钱已进银行、人还站在出口」
                   的僵尸档，下次启动会出现假的 Continue。 */
                runInProgress = 0;
                dailyEscapeCount++; DailyObserve(DQ_ESCAPE,dailyEscapeCount);
                gameWon=1; gameCleared=1; LoopCheckAchievements();
                BankSettle(1); hasSave=0;
            }
            gameWon = 1;
            currentState = STATE_GAMEOVER;
            menuSelection = 0;
            ForceCursor(1);
            uiDirty = 1;
            return 0;
        }
        if (keyPressed['2']) {
            keyPressed['2'] = 0;
            exitChoiceOpen = 0;
            exitGoDeeper = 1;      /* v6：下一帧走「换下一层」那段 */
            uiDirty = 1;
            return 0;
        }
        return 0;   /* 弹窗期间冻结其它一切操作 */
    }

    /* v3.3：T 键在不能开店的地方也要给一句反馈 ——
       试玩实测：「T 在走廊里按没反应，也没有『此处不能开商店』的提示」。
       静默是最糟糕的反馈，玩家会以为按键坏了。 */
    /* v7 第 52 轮：**这条拦截删掉了**
       它用的是 playerInSafe —— 那个变量在下面才更新，
       所以玩家刚走进安全屋的那一帧，值还是 false，
       T 被这里吃掉并弹一句「商店只在安全屋」，
       而真正的开店代码在后面，永远收不到这个键。
       （这就是「按 T 什么都没发生」的直接原因。）

       而且现在商店是浮层，不再依赖安全屋状态机，
       所以干脆允许**任何时候**按 T 打开。 */

    

    /* v3.3：**C 键激活诱饵**（右键负责放置，C 负责激活） */
    if (currentState == STATE_PLAYING && keyPressed['C']) {
        keyPressed['C'] = 0;
        int any = 0;
        for (int i = 0; i < MAX_DECOY; i++)
            if (decoysArr[i].active && !decoysArr[i].armed) { any = 1; break; }
        if (any) {
            ArmDecoy();TutorialRecordAction(TUTOR_DECOY);
            /* ===== v2.4.63 **不要写死秒数** =====
               原来文案写死「每 3 秒」，后来把 DECOY_SHOUT_EVERY 改成 1.5 之后
               这句话就成了错的（用户实机看到的就是它）。
               现在直接从常量生成，改参数文案自动跟着变。 */
            {
                static char decoyMsg[64];
                snprintf(decoyMsg, sizeof(decoyMsg),
                         (g_lang == LANG_ZH) ? "诱饵已激活 - 每 %.1f 秒叫一次"
                                             : "DECOY ARMED - SHOUTS EVERY %.1fs",
                         (double)DECOY_SHOUT_EVERY);
                pickupMsg = decoyMsg;
            }
            pickupMsgTimer = 2.0f;
        }
    }

    if (runUltimate >= 0 && !ultUsed && currentState == STATE_PLAYING
        && IsActionPressed(ACT_SWING)) {

        Monster* targets[MON_HUNTER_MAX + 1];
        int nTargets = MonsterCollect(targets, MON_HUNTER_MAX + 1, 1);
        ultUsed = 1;
        ultKind = runUltimate;
        keyPressed[g_bindings[ACT_SWING]] = 0;
        uiDirty = 1;

        /* 按名字判断是哪一个（inRunDefs 的顺序：WHISTLE / SMOKE OUT / ... / BLACKOUT） */
        const char* nm = inRunDefs[runUltimate].name;
        if (strstr(nm, "BLACKOUT")) {
            ultTimer = ULT_BLACKOUT_TIME;
            pickupMsg = g_lang == LANG_ZH ? "全黑  10 秒" : "BLACKOUT  10s";
            PlaySfx(SFX_POWER, 1.0f);
        } else if (strstr(nm, "WHISTLE")) {
            ultTimer = ULT_WHISTLE_TIME;
            /* 把所有追捕者引到远处一个点 */
            float fx = posX, fy = posY;
            for (int k = 0; k < 12; k++) {
                fx = 2.0f + (float)(rand() % (MAP_WIDTH - 4));
                fy = 2.0f + (float)(rand() % (MAP_HEIGHT - 4));
                float ddx = fx - posX, ddy = fy - posY;
                if (ddx * ddx + ddy * ddy > 144.0f) break;   /* 至少 12 格远 */
            }
            for (int k = 0; k < nTargets; k++) {
                targets[k]->state = 1;
                targets[k]->tx = fx;
                targets[k]->ty = fy;
                targets[k]->repath = 0.0f;
                targets[k]->stateTimer = 0.0f;
            }
            pickupMsg = g_lang == LANG_ZH ? "哨声  它们被引走了" : "WHISTLE  THEY LEFT";
            PlaySfx(SFX_CRANK, 1.0f);
        } else {
            /* SMOKE OUT：立刻断开所有视线 */
            ultTimer = 0.01f;
            for (int k = 0; k < 2; k++) {
                if (!targets[k]->active) continue;
                targets[k]->state = 0;
                targets[k]->pathLen = 0;
                targets[k]->pathIdx = 0;
                targets[k]->sightHold = 0.0f;
            }
            pickupMsg = g_lang == LANG_ZH ? "烟幕  视线已断" : "SMOKE OUT";
            PlaySfx(SFX_GLASS, 0.9f);
        }
        pickupMsgTimer = 2.0f;
    }

    return 1;
}

static int UpdateRestAndShop(float dt, int inSafe) {
    /* 带半径的碰撞：不再用「点」判定，避免贴墙时半个身子钻进墙里 */
    /* 安全屋：站在里面回理智 */
    playerInSafe = inSafe;   /* v2.5：inSafe 已在前面算过 */
    /* v2.5：站在安全区里按**空格**直接玩校准小游戏。
       原来必须先按 E 开面板、再在面板里按空格，多绕一步；
       用户要求"进安全区屏幕上出现一行字，按空格开始小游戏"。 */
    /* v3.0：**藏进藏身点也能校准** ——
       用户要求把校准系统移进藏身点，冷却 60 秒，主要奖励改成理智。
       这样柜子/床底从「单纯躲避」变成「喘息 + 回复」，才有存在感。 */
    CalibHandleInput(inSafe);

    /* v2.6：**进安全屋商店的热键改成 T**（用户要求）。
       原来用的是 ACT_INTERACT（默认 E），但 E 同时是「翻箱搜索」，
       站在安全屋里按 E 会让人搞不清是要搜箱子还是开店。
       E 保留给搜索，安全屋专用 T。 */
    /* v3.0：**藏身点里也能开商店** —— 用户要求「同时也可以打开商店面板」 */
    /* ==================================================================
     * v7 第 ㊿ 轮：**T 也走 GetAsyncKeyState 兜底**
     * ==================================================================
     * WM_KEYDOWN 只在窗口有焦点时才来。GDI 窗口被别的程序抢焦点时，
     * 玩家按 T 完全没反应 —— 而且这也让自动化测试无法验证这个功能。
     *
     * 这里自己做个 0→1 边沿检测，和 WM 那条互为兜底。
     * ================================================================ */
    {
        static int tPrev = 0;

        int tNow = PlatKeyDown('T');
        if (tNow && !tPrev && currentState == STATE_PLAYING && !shopOverlayOpen && ShopCanOpen()) {
            ShopOpen(currentLevel);
            currentState = STATE_SAFEHOUSE;
            shopPage = 0;
            shopRow = 0;
            ForceCursor(1);
            uiDirty = 1;
        }
        tPrev = tNow;
    }

    if (currentState == STATE_PLAYING && keyPressed['T'] && ShopCanOpen()) {   /* v7 第 52 轮：随时可开 */
        keyPressed['T'] = 0;
        /* v7 第 51 轮：**开浮层，不切状态**
           STATE_SAFEHOUSE 那条路径一进就崩（已用 demo4 复现），
           而浮层不动 currentState，绕开了整条坏路径。 */
        {

            ShopOpen(currentLevel);
            shopOverlayOpen = 0;
            currentState=STATE_SAFEHOUSE;shopPage=SHP_SHOP;shopRow=0;
            ForceCursor(1);
            uiDirty = 1;
        }
        return 0;
        /* v2.4：次数改由 CalibTick 按层发放，这里不再重置 */
        shopPage = 0;
        shopRow = 0;
        ForceCursor(1);
        uiDirty = 1;
        return 0;
    }
    if (inSafe) {
        /* 进安全屋 = 脱离：怪物立刻丢失目标、回到游荡，也不再抓人 */
        if (Hunter0()->active) { Hunter0()->state = 0; Hunter0()->pathLen = 0; Hunter0()->pathIdx = 0; }
    }
    if (inSafe) {
        /* v2.0：安全屋不再是免费满血点 —— 回智封顶 SAFE_HEAL_CAP（60），
           再往上要靠药箱/技能/遗物。原来 3/s 回满 + 8 个安全屋，
           让理智根本不像计时器（审计实测一局净 +187）。 */
        if (sanity < SAFE_HEAL_CAP) {
            /* SafeHealRate includes current RECOVER (node 10), exactly once. */
            sanity += SafeHealRate() * RunSanityGainMul() * dt;
            if (sanity > SAFE_HEAL_CAP) sanity = SAFE_HEAL_CAP;
        }
        if (sanity > SanityMax()) sanity = SanityMax();
        /* 「SAFE ZONE」提示只闪一下：刚进来时给 2 秒，之后不常驻。
           （玩家在安全屋里本来就知道自己在那，不需要一直盯着这行字。） */
        if (safeZoneMsg <= 0.0f) safeZoneMsg = 2.0f;
    }
    if (safeZoneMsg > 0.0f) {
        safeZoneMsg -= dt;
        if (safeZoneMsg < 0.0f) safeZoneMsg = 0.0f;
    }

    return 1;
}

static void UpdateLegacyEffects(float dt) {
    /* ---- v2.9 绣刀：发射剑气（穿透、14 格射程），60 秒冷却，命中不再减半 ---- */
    UpdateExtras(dt);      /* v2.0：额外威胁 */

    if (knifeCd > 0.0f) {
        knifeCd -= dt;
        if (knifeCd < 0.0f) knifeCd = 0.0f;
    }
    /* v2.9：RUST KNIFE 改成**发射一道剑气**向前飞，而不是原地挥砍。
       剑气一路穿透，碰到墙或飞满 SLASH_MAXDIST 才消失。 */
    /* v2.0：RUST KNIFE 已删除（剑气起手代码随之移除，只剩飞行中的剑气收尾） */
    if (slashActive) {
        float step = SLASH_SPEED * dt;
        slashX += slashDX * step;
        slashY += slashDY * step;
        slashTravel += step;

        /* 命中：剑气扫过的位置附近全部秒杀（穿透） */
        int hit = 0;
        if (Hunter0()->active && Dist2(slashX, slashY, Hunter0()->x, Hunter0()->y) < 0.55f) {
            ApplyDamage(Hunter0(), slashDmg, DT_SLASH, posX, posY);
            hit = 1;
        }
        if (lightBeast.active && Dist2(slashX, slashY, lightBeast.x, lightBeast.y) < 0.55f) {
            ApplyDamage(&lightBeast, slashDmg, DT_SLASH, posX, posY);
            hit = 1;
        }
        for (int i = 0; i < LEECH_MAX; i++) {
            if (!leeches[i].active) continue;
            if (Dist2(slashX, slashY, leeches[i].x, leeches[i].y) < 0.55f) {
                leeches[i].active = 0;
                hit = 1;
            }
        }
        if (hit) {
            /* 2026-11-05：法术武器已下线，这一行只会给一个不存在的武器加经验 */

            slashFade = 0.18f;                 /* 命中留一点余辉 */
            slashHitStop = 0.05f;              /* 50ms 顿帧 */
            /* 在命中点炸一圈金币（复用玻璃金币池） */
            SpawnShards(slashX, slashY, slashDX * 3.0f, slashDY * 3.0f, 1.0f);
            MakeNoise(slashX, slashY, KNIFE_NOISE * 0.5f);
            PlaySfx(SFX_THROW, 0.85f);
        }

        /* 撞墙 / 射程到头：收招 */
        if (Blocked(slashX, slashY) || slashTravel > slashMaxDist) {
            slashActive = 0;
            slashFade = 0.22f;
        }
    }
    if (slashFade > 0.0f) {
        slashFade -= dt;
        if (slashFade < 0.0f) slashFade = 0.0f;
    }

}

static void UpdatePhaseAndCrank(float dt) {
    /* ---- v1.8 投石：正前方 3~6 格制造半径 14 的噪声 ---- */
    if (decoySelfTimer > 0.0f) {
        decoySelfTimer -= dt;
        if (decoySelfTimer < 0.0f) decoySelfTimer = 0.0f;
    }
    /* G 键冷却倒计时 */
    if (s_decoyKeyCd > 0.0f) {
        s_decoyKeyCd -= dt;
        if (s_decoyKeyCd < 0.0f) s_decoyKeyCd = 0.0f;
    }
    /* ==================================================================
     * 2026-11-06（**P1-4 教程在受限状态下死锁**）
     * ------------------------------------------------------------------
     * 原来这里要求 HasKeystone(SK_DECOY) —— 但训练场不授予任何天赋，
     * 所以教程第 9 步（TUT_KNOCK）教的 G 键**根本没有反应**，
     * 玩家照做也没用，教程卡死。
     *
     * 现在：**训练场里无条件开放 G 键**。
     * 教程的意义就是把操作教一遍，不能先要求玩家去商店买天赋。
     * ================================================================ */
    /* ==================================================================
     * 相位信标：C 放置 / Q 传送（2026-11-06 用户要求）
     * ------------------------------------------------------------------
     * 放在按骰子之前：它们和投掷物无关，不该被蓄力/投掷状态影响。
     * ================================================================ */
    if (IsActionPressed(ACT_PHASE_TELE)) {
        keyPressed[g_bindings[ACT_PHASE_TELE]] = 0;
        if (!phaseOwned || phaseCharges <= 0) {
            pickupMsg = (g_lang == LANG_ZH) ? "没有相位信标" : "NO PHASE BEACON";
            pickupMsgTimer = 1.4f;
        } else if (!phasePlaced) {
            pickupMsg = (g_lang == LANG_ZH) ? "先按 C 放一个信标" : "PLACE A BEACON FIRST (C)";
            pickupMsgTimer = 1.4f;
        } else if (PhaseTryTeleport()) {
            pickupMsg = (g_lang == LANG_ZH) ? "相位传送！" : "PHASE SHIFT!";
            pickupMsgTimer = 1.4f;
            PlaySfx(SFX_POWER, 0.9f);
            uiDirty = 1;
        } else {
            /* 目标格被墙占了（期间塌过 / 被改过地形） */
            pickupMsg = (g_lang == LANG_ZH) ? "信标位置已失效" : "BEACON OBSTRUCTED";
            pickupMsgTimer = 1.4f;
            phasePlaced = 0;
        }
    }

    if((SkillOwned(SK_DECOY) || trainingMode) && !calibActive && IsActionPressed(ACT_DECOY))HushActivate();

    /* 手摇发电：站着摇，摇满一次回一截电；期间不能移动 */
    cranking = 0;
    if (!calibActive && ActionIsDown(ACT_CRANK) && battery < (float)BatteryMax() - 0.1f && !holdingNow) {
        cranking = 1;
        crankTimer += dt;
        crankClick += dt;
        if (crankClick >= 0.30f) {
            crankClick = 0.0f;
            PlaySfx(SFX_CRANK, 0.45f);
            MakeNoise(posX, posY, (SkillOwned(SK_SILENT_CRANK) && battery<BatteryMax()*.25f?0.0f:12.0f) * RunCrankNoiseMul()*(SkillOwned(SK_APPRENTICE)?0.5f:1.0f));   /* v2.2：FAST CRANK 的 +15% 噪声 */
        }
        /* v3.4：SPARE CELL 满级时摇电时间 -30%（2.4 → 1.68 秒） */
        if (crankTimer >= 2.4f * RunCrankTimeMul() * SkillCrankTimeMul() * BenchCrankTimeMul()) {
            crankTimer = 0.0f;
            int wasDead = (battery <= 0.5f);
            /* v3.4：SPARE CELL 满级 → 一次 +55（基础 32） */
            battery += SkillCrankBonus();
            crankTimeTotal += 3;  /* 2.4 秒算 3 秒，累计用于成就 */
            crankSecondsTotal += 3.0f;   /* v3.3：CRANK ADDICT 用的是这个 */
            if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
            /* v3.3 **充上电自动开灯** ——
               试玩实测：「电量归零灯自动熄灭，摇满后灯仍是 OFF，
               必须再单独按一次 F，而 HUD 从不提示 F 的存在」。
               你刚刚专门花 2.4 秒摇电，显然是想继续用灯。 */
            if (wasDead && !flashlightOn && FlashlightUseAllowed()) {
                flashlightOn = 1;
                pickupMsg = (g_lang == LANG_ZH) ? "手电已重新点亮" : "LIGHT BACK ON";
                pickupMsgTimer = 1.2f;
            }
            PlaySfx(SFX_POWER, 0.45f);
        }
    } else {
        /* v3.3 **早松手也要给部分电量** ——
           试玩实测：「按住 1.5 秒时屏幕上已经出现进度，松手后电量一点没加，
           也没有『松手太早』的提示」。
           原来 crankTimer 一松手就归零，不足 2.4 秒的部分全丢。
           现在按比例结算：攒了 1.5 秒就给 1.5/2.4 × 32 ≈ 20 点。
           ≥ 0.3 秒才给，避免「点一下就白送」。 */
        float full = 2.4f * RunCrankTimeMul() * SkillCrankTimeMul() * BenchCrankTimeMul();
        if (crankTimer >= 0.3f && full > 0.01f) {
            int wasDeadPart = (battery <= 0.5f);   /* v6：早松手也判断是否没电 */
            float part = crankTimer / full;
            if (part > 1.0f) part = 1.0f;
            /* 2026-11-06（用户要求）：每次摇电的比例提高一点。
               上限涨到 150 之后，32 点显得太抠 —— 摇满一次只补 1/5。
               38 仍然需要摇两次多才能充满，节奏没变。 */
            battery += 38.0f * part;
            if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
            crankTimeTotal += (int)(3.0f * part);
            crankSecondsTotal += 3.0f * part;
            PlaySfx(SFX_POWER, 0.30f + 0.25f * part);
            /* v6 **早松手也要能开灯** ——
               v3.3 的「充上电自动开灯」只写在「摇满 2.4 秒」那条分支里。
               而这轮加了「早松手给部分电量」之后：
                 电量 0 → 松手早 → 加到 ~20 → battery 不再是 0
                 → 下次摇满时 wasDead = 0 → **灯永远开不回来**。
               所以这里补上同样的判断。 */
            if (wasDeadPart && !flashlightOn && battery > 1.0f && FlashlightUseAllowed()) {
                flashlightOn = 1;
                pickupMsg = (g_lang == LANG_ZH) ? "手电已重新点亮" : "LIGHT BACK ON";
                pickupMsgTimer = 1.2f;
            }
        }
        crankTimer = 0.0f;
        crankClick = 0.0f;
    }

}

static int UpdateMovementAndHatches(float dt, const PlayerFrameInput* movement) {
    PlayerApplyMovement(dt, movement);

    /* ==================================================================
     * 2026-11-06：**舱门推进 + 效果计时器**
     * ------------------------------------------------------------------
     * HatchTick 必须每帧跑：它负责推进开门动画，并在动画播完那一帧
     * 结算效果。漏了它门就永远卡在 OPENING（这类"更新循环漏写"
     * 在 art_direction.h 里被点名过）。
     * ================================================================ */
    TickMark(TICK_HATCH);
    HatchTick(dt);
    if (hatchLightsT > 0.0f) { hatchLightsT -= dt; if (hatchLightsT < 0.0f) hatchLightsT = 0.0f; }
    if (hatchGoldMulT > 0.0f) { hatchGoldMulT -= dt; if (hatchGoldMulT < 0.0f) hatchGoldMulT = 0.0f; }
    if (hatchBloodT > 0.0f)   { hatchBloodT -= dt;   if (hatchBloodT < 0.0f) hatchBloodT = 0.0f; }
    if (hatchSpeedT > 0.0f)   { hatchSpeedT -= dt;   if (hatchSpeedT < 0.0f) hatchSpeedT = 0.0f; }

    /* ==================================================================
     * 2026-11-06：**崩塌流程**
     * ------------------------------------------------------------------
     * 黑屏期间逻辑冻结、输入吞掉（AD-28：模态要有明确的时间与输入边界）。
     * 注意放在 UpdateExplored 与移动**之前** —— 放在后面的话，
     * 玩家在黑屏里仍然能走两步，重掷后位置就不可预测了。
     * ================================================================ */
    TickMark(TICK_COLLAPSE);
    if (CollapseTick(dt)) return 0;

    UpdateExplored();

    return 1;
}

static void CompleteHeldHatch(void) {

                /* ================================================================
                 * 舱门：门槛判定 → 开门动画
                 * 效果**不在这里结算** —— 由 HatchTick 在动画播完那一帧调
                 * HatchFireEffect（AD-27：动画与生效窗口同一时间轴）。
                 * ================================================================ */
                static char why[96];
                why[0] = 0;
                if (HatchGateCheck(hoverIndex, why, (int)sizeof(why))) {
                    HatchSetState(hoverIndex, HATCH_OPENING);
                    pickupMsg = (g_lang == LANG_ZH) ? "舱门开启中…" : "HATCH CYCLING...";
                    pickupMsgTimer = 1.2f;
                } else {
                    pickupMsg = why[0] ? why : ((g_lang == LANG_ZH) ? "条件不满足" : "NOT YET");
                    pickupMsgTimer = 2.0f;
                }
                uiDirty = 1;
                hoverKind = 0;
}

static void CompleteHeldFurniture(void) {

                /* ================================================================
                 * 家具搜查结算（长按完成后）
                 * ----------------------------------------------------------------
                 * 用户要求：「收这种大箱子有概率收不出来东西，
                 *           不然的话奖励太丰厚了。」
                 * 所以每次有 **35% 空手** —— 大容器给得多，但也要赌。
                 * ================================================================ */

                if (hoverIndex >= 0 && hoverIndex < MAX_ROOM_PROPS && roomProps[hoverIndex].active) {
                    RoomProp* rp = &roomProps[hoverIndex];
                    if(rp->kind>=ROOMPROP_THEME_FIRST){
                        rp->searched++;rp->searchLeft=0;
                        if(rp->kind==6 || rp->kind==9){sanity=fminf(SanityMax(),sanity+6);pickupMsg=Tr("SALVAGED MEDICAL RESERVE: +6 SAN","取用急救储备：理智+6");}
                        else if(rp->kind==4){WalletAddRun(12);pickupMsg=Tr("ARCHIVE DRAWER: +12 GOLD","档案抽屉：金币+12");}
                        else {battery=fminf((float)BatteryMax(),battery+8);pickupMsg=Tr("RECOVERED DEVICE CELL: +8 BATTERY","检修装置取回电芯：电量+8");}
                        pickupMsgTimer=2;hoverKind=0;uiDirty=1;return;
                    }
                    int n = rp->searched;
                    rp->searched++;
                    if (rp->searchLeft > 0) rp->searchLeft--;

                    int empty = (rand() % 100) < 35;
                    if (empty) {
                        pickupMsg = (g_lang == LANG_ZH) ? "空的" : "NOTHING HERE";
                    } else if (n == 1) {
                        if (battery < (float)BatteryMax() - 1.0f) {
                            battery += 18.0f;
                            if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
                            pickupMsg = (g_lang == LANG_ZH) ? "翻到电池 +18" : "FOUND A BATTERY +18";
                        } else {
                            int g2 = 18 + (int)(rand() % 25);
                            WalletAddRun(g2);
                            static char gb2[64];
                            snprintf(gb2, sizeof(gb2), (g_lang == LANG_ZH) ? "翻到 %d 金币" : "FOUND %d GOLD", g2);
                            pickupMsg = gb2;
                        }
                    } else if (n == 2 && fusesHeld < fusesNeeded) {
                        fusesHeld++;
                        pickupMsg = (g_lang == LANG_ZH) ? "翻到一根保险丝！" : "FOUND A FUSE!";
                    } else {
                        int g2 = 18 + (int)(rand() % 25);
                        WalletAddRun(g2);
                        static char gb3[64];
                        snprintf(gb3, sizeof(gb3), (g_lang == LANG_ZH) ? "翻到 %d 金币" : "FOUND %d GOLD", g2);
                        pickupMsg = gb3;
                    }
                    pickupMsgTimer = 1.6f;
                    uiDirty = 1;
                    if (rp->searchLeft <= 0) hoverKind = 0;
                }
}

static void CompleteHeldContainer(void) {

                Container* c = &containers[hoverIndex];
                if(c->searched)return;
                c->searched = 1;
                for(int ei=0;ei<ECHO_MARK_MAX;ei++)if(echoMarkActive[ei] && echoMarkKind[ei]!=0 && Dist2(echoMarkX[ei],echoMarkY[ei],c->x,c->y)<.1f)echoMarkActive[ei]=0;
                if(!trainingMode && CellMetaAt((int)c->x,(int)c->y)->roomId){int reward=(int)((45+fminf(70,currentLevel*5))*EndlessDepthBonus(currentLevel));WalletAddRun(reward);totalGoldEarned+=reward;}
                if(!trainingMode){int stipend=(int)((8+fminf(20,currentLevel*2))*BenchCrateGoldMul());WalletAddRun(stipend);totalGoldEarned+=stipend;}
        {  hideStreak = 0; }   /* v7：搜过箱子，连躲计数清零 */
        SpawnParticles(c->x, c->y, 2);   /* v2.1：翻箱扬起灰尘 */
                searchedCount++;
                runSearched++;
        int shardGain = (int)(GoldSearchAt(currentLevel) * SkillScavengerShardMul() * SkillCrateGoldMul() * BenchCrateGoldMul()
                              * RelicShardMul() * modShardMul * RoomThemeLootMul() + 0.5f);   /* v2.0：遗物+事件 */
                /* 第 8 轮「赌徒」：50% 双倍 / 50% 归零 —— 把开箱变成赌注 */
                {

                    if (SkillRaiderGambleOwned()) {
                        if (RunRngNext() & 1u) shardGain *= 2;
                        else                   shardGain = 0;
                    }
                }
                /* 2026-11-08（第 5 轮）：战役金币倍率 */
                { 
                  shardGain = (int)(shardGain * CampaignGoldMulNow()); }
                WalletAddRun(shardGain);    /* v1.8：先记在待上缴里 */
                /* 2026-11-08（第 4 轮）：HINT_GOLD 文案写好了却从没触发过 ——
                   第一次拿到金币时教一句"钱在结算时进银行"。 */
                {  
                  HintOnce(2); }
                totalGoldEarned += shardGain;
                PlaySfx(SFX_PICKUP, 0.55f);
                MakeNoise(posX, posY, 8.0f * SkillSearchNoiseMul());
                if (c->content == 1) { fusesHeld++; fusesCollectedRun++; pickupMsg = "+FUSE"; }
                else if (c->content >= 4) {
                /* v1.16：纯随机产出，酸瓶与烟雾明显稀有 */
                /* v6 **删掉 BRICK / ACID 两行** ——
                   道具栏改成 6 格（光球/投石/烟雾/胶水/回声/诱饵）之后，
                   砖头与酸液早就没有槽位了，但箱子还在掉，
                   玩家会看到 “+ACID” 却什么都没有（用户报告）。
                   权重重新分配给现有 6 种。 */
                int roll = rand() % 100;
                if (roll < 22) { if (flares < BottleCap()) { flares++; pickupMsg = "+FLARE"; } }
                else if (roll < 44) {
                    /* v2.7：走 BottleCap()，否则 RUST KNIFE 的 cap=1 代价能被箱子刷开 */
                    if (bottles < BottleCap()) bottles++;
                    pickupMsg = "+BOTTLE";
                }
                else if (roll < 64) { if (smokeBombs < BottleCap()) { smokeBombs++; pickupMsg = "+SMOKE"; } }
                else if (roll < 78) { if (echoBolts < BottleCap()) { echoBolts++; pickupMsg = "+ECHO"; } }
                else if (roll < 90) { if (gluePots < BottleCap()) { gluePots++; pickupMsg = "+GLUE"; } }
                else                { if (decoys < BottleCap()) { decoys++; pickupMsg = "+DECOY"; } }
            }
                else if (c->content == 2) {
                    battery += BATTERY_PICKUP;
                    energy += ENERGY_FROM_BATTERY;   /* v2.3：电池顺带补武器能量 */
                    if (energy > ENERGY_MAX) energy = ENERGY_MAX;
                    if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
                    healFlash = 0.06f;
                    pickupMsg = "+BATTERY";
                } else if (c->content == 3) {
                    sanity += 25.0f * RunSanityGainMul();
                    if (sanity > SanityMax()) sanity = SanityMax();
                    healFlash = 0.15f;
                    pickupMsg = "+MEDKIT";
                }
                /* v3.3：新掉落表的四种投掷物 */
                else if (c->content == 6) { if (smokeBombs < BottleCap()) { smokeBombs++; pickupMsg = "+SMOKE"; } }
                else if (c->content == 7) { if (gluePots < BottleCap()) { gluePots++; pickupMsg = "+GLUE"; } }
                else if (c->content == 8) { if (echoBolts < BottleCap()) { echoBolts++; pickupMsg = "+ECHO"; } }
                else if (c->content == 9) { if (decoys < BottleCap()) { decoys++; pickupMsg = "+DECOY"; } }
                /* v3.4：箱子现在是主要金币来源（掉落表 35%） */
                else if (c->content == 10) {
                    int gld = (int)(GoldSearchAt(currentLevel) * SkillScavengerShardMul() * SkillCrateGoldMul() * BenchCrateGoldMul()
                                    * RelicShardMul() * modShardMul * RoomThemeLootMul() * SkillGoldMul() + 0.5f);
                    WalletAddRun(gld);
                    totalGoldEarned += gld;
                    {
                        static char gmsg[48];
                        snprintf(gmsg, sizeof(gmsg), "+%d GOLD", gld);
                        pickupMsg = gmsg;
                    }
                }
                /* SCAVENGER: every search also drops a random throwable */
        if (SkillScavenger()) {
            /* v3.3：6 选 1，对应新的六个槽位 */
            int r2 = rand() % 6;
            if (r2 == 0) { if (flares < BottleCap()) { flares++; pickupMsg = "+FLARE"; } }
            else if (r2 == 1) { if (bottles < BottleCap()) bottles++; pickupMsg = "+SLING"; }
            else if (r2 == 2) { if (smokeBombs < BottleCap()) { smokeBombs++; pickupMsg = "+SMOKE"; } }
            else if (r2 == 3) { if (gluePots < BottleCap()) { gluePots++; pickupMsg = "+GLUE"; } }
            else if (r2 == 4) { if (echoBolts < BottleCap()) { echoBolts++; pickupMsg = "+ECHO"; } }
            else { if (decoys < BottleCap()) { decoys++; pickupMsg = "+DECOY"; } }
        }
        /* ============================================================
         * 2026-11-05：**宝箱怪空壳的额外奖励**
         * ------------------------------------------------------------
         * 放在普通结算**之后**，这样它覆盖 pickupMsg，
         * 玩家看到的是"空壳里翻出 400 金币"而不是普通的 +GOLD 提示。
         *
         * 为什么不拦截普通结算：普通箱子那 8+1.5×层的金币量级很小，
         * 为了省这点钱去改一大段缩进，只会增加出错面（铁律四十九）。
         * ============================================================ */
        if (c->mimicShell) {
            int bigGold = MIM_GOLD_BASE + MIM_GOLD_STEP * currentLevel;
            bigGold = (int)((float)bigGold * RelicShardMul() * modShardMul * RoomThemeLootMul() * SkillGoldMul() + 0.5f);
            WalletAddRun(bigGold);
            totalGoldEarned += bigGold;

            fusesHeld++;                 /* 必给一根保险丝 */
            fusesCollectedRun++;

            {   /* 再给一件道具（沿用箱子那套权重） */
                int r = rand() % 100;
                if      (r < 20) { if (bottles   < BottleCap()) bottles++;   }
                else if (r < 38) { if (flares    < BottleCap()) flares++;    }
                else if (r < 54) { if (smokeBombs< BottleCap()) smokeBombs++; }
                else if (r < 70) { if (gluePots  < BottleCap()) gluePots++;  }
                else if (r < 85) { if (echoBolts < BottleCap()) echoBolts++; }
                else             { if (decoys     < BottleCap()) decoys++;   }
            }

            PlaySfx(SFX_POWER, 1.0f);
            {
                static char shellMsg[112];
                snprintf(shellMsg, sizeof(shellMsg),
                         g_lang == LANG_ZH ? "空壳里翻出  +%d 金币  +1 保险丝  +1 道具"
                                           : "SHELL LOOT  +%d GOLD  +1 FUSE  +1 ITEM",
                         bigGold);
                pickupMsg = shellMsg;
            }
            pickupMsgTimer = 2.2f;
            uiDirty = 1;
        }
        if (pickupMsg) pickupMsgTimer = 2.0f;
}

static void CompleteHeldBreaker(void) {

                /* v3.0：**NO_COMBAT 下没有 BOSS 可打** ——
                   bossDefeated 只在 #else 分支赋值（怪不可杀），
                   这里永远为 0，于是第 6/12/18 层「BOSS STILL ALIVE」
                   把一局钉死（体验评审实测：bossLevel=1 时通电恒失败）。 */
                int bossBlocked = 0;
#if !NO_COMBAT
                bossBlocked = (bossLevel && !bossDefeated);
#endif
                if (bossBlocked) {
                    pickupMsg = "BOSS STILL ALIVE";
                    pickupMsgTimer = 1.5f;
                } else if (fusesHeld >= fusesNeeded) {
                    breakerOpen = 1;
                {  WardenSpawnIfPowered(); }   /* v7 B1: 通电 -> 守门人出现 */
        SpawnParticles(breaker.x, breaker.y, 0);   /* v2.1：通电火花 */
                    fusesHeld -= fusesNeeded;
                    WalletAddRun((int)(GoldBreakerAt(currentLevel) * RelicShardMul() * modShardMul * RoomThemeLootMul() * SkillGoldMul() + 0.5f));   /* v2.0 */
                    totalGoldEarned += (int)GoldBreakerAt(currentLevel);
                    PlaySfx(SFX_POWER, 0.75f);
                }
}

static void UpdateHeldInteraction(float dt) {
    /* ---------------- 交互：长按 E 搜索容器 / 给配电箱上电 ---------------- */
    if (holdingNow) {
        if (holdTimer == 0.0f && (hoverKind == 1 || hoverKind == 3 || hoverKind == 4)) {
            /* 开始翻找：发出声音，吸引怪物（压迫感来源）。
               v2.4-rc：家具（货架/木箱堆/控制台）也走这条 ——
               用户实测"搜这种大容器的时候搜索进度条失效、不发声"，
               因为第一版用的是**按一下**而不是**长按**。 */
            PlaySfx(SFX_SEARCH, 0.55f);  /* 专用翻箱声：金属刮擦+木板闷响 */
            MakeNoise(posX, posY, 10.0f * SkillSearchNoiseMul() * RunSearchNoiseMul2());   /* v2.2：FAST SEARCH 的 +10% */  /* 比走路大一点，比跑步小 */
        }
        /* 2026-11-05：宝箱怪在翻找进度 60% 时咬人。
           低于 45% 松手完全安全 —— 所以"起疑就松手"是有效解法。
           被咬的那一帧不推进 holdTimer，也不触发"翻完"结算。 */
        int mimicBit = 0;
        if (hoverKind == 1) {
            float prog = holdTimer / (SEARCH_TIME * RunSearchTimeMul() * BenchSearchTimeMul());
            if (MimicOnSearchStep(hoverIndex, prog)) {
                holdTimer = 0.0f;
                hideBlockTimer = 0.4f;
                mimicBit = 1;
            }
        }
        holdTimer += dt;
        if (!mimicBit && holdTimer >= SEARCH_TIME * RunSearchTimeMul() * BenchSearchTimeMul()) {
            holdTimer = 0.0f;
            /* v3.0：翻完箱也上 0.4 秒封锁 ——
               否则 E 还按着，同一帧/下一帧就会被旁边的藏身点吸进去。 */
            hideBlockTimer = 0.4f;
            if (hoverKind == 5) RoomThemeUseSwitch();
            else if (hoverKind == 4) CompleteHeldHatch();
            else if (hoverKind == 3) CompleteHeldFurniture();
            else if (hoverKind == 1) CompleteHeldContainer();
            else if (hoverKind == 2) CompleteHeldBreaker();
            hoverKind = 0;
            hoverIndex = -1;
        }
    } else {
        holdTimer = 0.0f;
    }

}

static int UpdateThrowInput(float dt) {
    /* ===== v2.4.80 **左键：扳手击退（从零重写）** =====
       用户要求：「左键直接帮我重构代码吧，一直就没对过。」
       旧的 atkNow 块里混着 5 个历史版本的注释、三套阈值、
       和投掷抢键的判断 —— 已经没人能读懂它到底该干什么。

       新实现见 WrenchKnockUpdate()（在 Update 之前定义）：
         按住 = 蓄力，松手 = 击退，低于死区 = 什么都不做。
         只有这一条路径，没有"敲"，没有和投掷抢键。 */
    WrenchKnockUpdate(dt);
    {  TickSecondSpawn(dt); }   /* v2.4.94 第二只 */
    WrenchTick(dt);

    /* 投掷：数字键选槽位，鼠标左键蓄力投掷 */
    if (throwCd > 0.0f) throwCd -= dt;

    /* 数字键 1/2/3 切换投掷物槽位。
       键盘的情况已经在 WM_KEYDOWN 里即时处理（见 WindowProc）——
       原来在这里用 if/else 轮询 keyPressed，同一帧先按 1 再按 2 只认 1，
       按住不放时系统的自动重复还会一直压住另一个键，表现就是「按了没反应」。
       这里只保留「把槽位改绑到鼠标键」的兜底：鼠标键不产生 WM_KEYDOWN。 */
    if (LoopIsMouseButton(g_bindings[ACT_SELECT_1]) && IsActionPressed(ACT_SELECT_1)) LoopSelectVisibleSlot(0);
    else if (LoopIsMouseButton(g_bindings[ACT_SELECT_2]) && IsActionPressed(ACT_SELECT_2)) LoopSelectVisibleSlot(1);
    else if (LoopIsMouseButton(g_bindings[ACT_SELECT_3]) && IsActionPressed(ACT_SELECT_3)) LoopSelectVisibleSlot(2);

    /* 鼠标左键：按住蓄力，松开投掷；右键取消蓄力。
       严格的「未按下 -> 按下」边缘检测：
       - prev 保存上一逻辑帧的真实按下状态；
       - 菜单期间 Update 顶部早退，prev 冻结 —— 菜单里点 Back 的那次
         左键按下对这里完全不可见；切回游戏时左键还物理按着，
         若不处理会被误判成新按下，开局就凭空蓄力、松手即投掷。
       - 所以「回到游戏」的每条路径（Resume / Esc / 失焦自动恢复 / 读档 /
         新开局）都必须置位 resetInputState：复位时把 prev 同步成当前
         物理状态 —— 已按着的键不算边沿，必须先松开再按才会蓄力。 */
    static int mouseLeftPrev = -1;
    static int mouseRightPrev = -1;
    if (resetInputState) {
        resetInputState = 0;
        mouseLeftPrev = ActionIsDown(ACT_THROW);
        mouseRightPrev = ActionIsDown(ACT_CANCEL_CHARGE);
        isCharging = 0;
        mouseCharge = 0.0f;
    }
    /* v2.4.77：**投掷 = 右键**（ACT_THROW = VK_RBUTTON） */
    int mouseLeftNow = (!lockActive) && ActionIsDown(ACT_THROW);
    int mouseRightNow = (!lockActive) && ActionIsDown(ACT_CANCEL_CHARGE);   /* 左键：现在只用于取消 */

    if (mouseLeftNow && !mouseLeftPrev && throwCd <= 0.0f && !holdingNow && !cranking) {
        /* 未按下 -> 按下：开始蓄力 */
        isCharging = 1;
        mouseCharge = 0.0f;
    }
    if (isCharging) {
        /* v2.4.83：**取消投掷改回右键**。
           原来这里读的是左键（鼠标左键），而左键现在归扳手击退 ——
           于是在箱子旁边按住左键时，投掷蓄力被这一行直接取消掉，
           看起来就是「扳手在箱子附近用不了」。 */
        if (mouseRightNow && !mouseRightPrev) {
            isCharging = 0;
            mouseCharge = 0.0f;
        } else if (mouseLeftNow) {
            /* 持续按住：累积蓄力（仅当选中道具有库存时） */
            int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys, phaseBeacons };
            int sel = throwSelected;
            if (sel < 0 || sel >= ITEM_SLOT_COUNT) sel = 0;
            if (counts[sel] > 0) {
                /* v3.4：STEADY HAND（局内树 id 9）满蓄力 0.5s → 0.32s */
                mouseCharge += dt / (TUNE_CHARGE_FULL_SECONDS * SkillHeavyHandChargeMul());
                if (mouseCharge > 1.0f) mouseCharge = 1.0f;
            }
        } else {
            /* 松开左键：发射（v1.16：0..4 = 荧光棒/瓶/砖/酸/烟） */
            isCharging = 0;
            int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys, phaseBeacons };
            int sel = throwSelected;
            if (sel < 0 || sel >= ITEM_SLOT_COUNT) sel = 0;
            /* ===== v3.3 六槽位的右键行为 =====
               0 光球 / 1 投石 / 2 烟雾 / 3 胶水 / 4 回声  → 投掷
               5 诱饵                                     → **放在脚下**（不投）
               诱饵放下后要按 C 才激活（用户要求：C 是激活，右键才是放置）。 */
            if (sel == 5) {
                if (decoys > 0) {
                    PlaceDecoy(posX, posY);
                    decoys--;
                    totalThrown++;          /* 也算一次道具消耗 */
                    throwCd = 0.60f;
                    pickupMsg = (g_lang == LANG_ZH)
                        ? "诱饵已放下" : "DECOY PLACED";
                    pickupMsgTimer = 2.0f;
                    PlaySfx(SFX_PICKUP, 0.7f);
                }
                mouseCharge = 0.0f;
                mouseLeftPrev = mouseLeftNow;
                mouseRightPrev = mouseRightNow;
                return 0;
            }
            if (mouseCharge >= 0.1f && counts[sel] > 0) {
                /* v2.7：先投，真生成了才扣。原来先扣再投，
                   池子满时 ThrowItem 静默不生成，东西凭空消失。 */
                ThrowItem(sel, mouseCharge);
                
                if (g_throwOk) {
                    if (sel == 0) flares--;
                    else if (sel == 1) bottles--;
                    else if (sel == 2) smokeBombs--;
                    else if (sel == 3) gluePots--;
                    else if (sel == 4) { echoBolts--; {  echoCellsTotalRun += 15; } }
                    totalThrown++;   /* v3.3：逃生向成就用（整条链之后加） */
                    throwCd = 0.45f;
                    if (trainingMode) {
                        TutorialRecordThrow();
                        /* P1-2：投掷同样是"制造噪音"，让 TUT_KNOCK 也走得了 */
                        TutorialRecordNoise();
                    }
                } else {
                    pickupMsg = "NO ROOM";
                    pickupMsgTimer = 1.0f;
                }
            }
            mouseCharge = 0.0f;
        }
    }
    mouseLeftPrev = mouseLeftNow;
    mouseRightPrev = mouseRightNow;

    return 1;
}

static void UpdatePlayerAudio(float dt, const PlayerFrameInput* movement) {
    (void)movement;
    /* v1.8 偷电怪提示音：电量低于 50% 时，若身边有偷电怪就给"滋滋"声。
       关灯的幽灵流也能听见 —— 否则被偷电完全无感（模拟里 438 秒被偷）。 */
    if (battery < LEECH_HINT_BATTERY) {
        int ld2 = LeechNearestDist2();
        if (ld2 >= 0 && ld2 < 9.0f) {          /* 3 格内 */
            leechHintTimer += dt;
            if (leechHintTimer > 1.6f) {
                leechHintTimer = 0.0f;
                PlaySfx(SFX_CRANK, 0.22f);
            }
        } else {
            leechHintTimer = 0.0f;
        }
    }

    /* 脚步：走路是完全静音的，只有冲刺才会响、才会惊动怪 */
    {
        /* 噪声倍率三件套：技能 / 局内技能 / 诱饵自身 */
        float ghost = flashlightOn ? 1.0f : SkillGhostStepNoiseMul();
        float noiseMul = SkillNoiseMul() * RunNoiseMul() * ghost
                       * (decoySelfTimer > 0.0f ? DECOY_SELF_MUL : 1.0f);

        if (movement->moving && sprinting && !holdingNow && !cranking) {
            /* 冲刺：每 0.3 秒一次比较响的脚步 */
            stepTimer += dt;
            walkTimer = 0.0f;
            walkStepTimer = 0.0f;
            if (stepTimer >= 0.30f) {
                stepTimer = 0.0f;
                PlaySfx(SFX_STEP, 0.50f);
                /* v3.0：keystone SILENT RUN —— 冲刺完全不产生噪音 */
                /* 第 7 轮「影步」：在烟里冲刺不发声（烟雾本来就该掩声） */
                {

                    int inSmoke = 0;
                    if (SkillGhostStepOwned()) {

                        inSmoke = PlayerInSmoke();
                    }
                    if (!SilentRunOwned() && !inSmoke)
                        MakeNoise(posX, posY, 9.0f * noiseMul * BenchWalkNoiseMul()
                                  * QuietSoleMul());   /* 轻步 + 静底 */
                }
            }
        } else if (movement->moving && !holdingNow && !cranking) {
            /* v2.6：走路本来完全无声，现在连续走 5 秒之后开始有细微噪声。
               停下 / 翻箱 / 摇电都会把计时清零 —— 也就是"走一段、停一下"仍然安静。 */
            stepTimer = 0.0f;
            walkTimer += dt;
            if (walkTimer >= WALK_NOISE_DELAY) {
                walkStepTimer += dt;
                if (walkStepTimer >= WALK_NOISE_INTERVAL) {
                    walkStepTimer = 0.0f;
                    PlaySfx(SFX_STEP, 0.28f);          /* 比冲刺轻 */
                    {
        /* v3.4 GHOST STEP 满级：关着手电时**脚步声完全消失**（开灯才恢复）。
           原来这节点是「黑暗脚步 -12% 耗电」，玩家感知不到。 */
        /* 第 7 轮「静默落地」：静止满 3 秒后，接下来 3 秒脚步完全无声 */
        {

            int silentLand = (SkillGhostLandOwned() && ghostStillT >= 3.0f
                              && ghostStillT <= 6.0f);
            if (!(SkillGhostSilent() && !flashlightOn) && !silentLand)
                MakeNoise(posX, posY, WALK_NOISE_RADIUS * noiseMul
                                  * BenchWalkNoiseMul() * QuietSoleMul());;
        }
    }   /* 轻步 + 静底 */
                }
            }
        } else {
            /* v2.0：停下不再立刻清零 —— 原来"走 4.9 秒停一帧"可以永久静音。
               现在按 3 倍速衰减，想刷掉噪声必须真的停下来站一会儿。 */
            stepTimer = 0.0f;
            walkTimer -= dt * 3.0f;
            if (walkTimer < 0.0f) walkTimer = 0.0f;
            walkStepTimer = 0.0f;
        }
    }
    if (sanity < 45.0f) {
        heartTimer += dt;
        float hb = 1.15f - (45.0f - sanity) / 45.0f * 0.65f;
        if (heartTimer >= hb) {
            heartTimer = 0.0f;
            PlaySfx(SFX_HEART, 0.30f + (45.0f - sanity) / 45.0f * 0.35f);
        }
    } else {
        heartTimer = 0.0f;
    }

    TickMark(TICK_THROWING);
    UpdateShots(dt);
}

static void UpdateWorldThreats(float dt, const PlayerFrameInput* movement) {
    (void)movement;
    /* v1.17 训练场：无限道具、不掉理智、标本固定不动 */
    if (trainingMode && !guideMode) UpdateTrainingPad(dt);   /* 第 4 轮：抽成函数 */

    TickMark(TICK_SMOKES);
    UpdateSmokes(dt);
    /* v1.18：站在烟里 → **只把光压暗**（用户要求，不再收窄视野） */
    /* v1.21: in smoke go nearly black (like the old 0.09 ambient build) */
    /* v2.1：0.10 等于把画面关掉。要的是「看不清」不是「看不见」。 */
    smokeDim = PlayerInSmoke() ? 0.45f : 1.0f;

    UpdateSmokeBlindness();   /* 第 4 轮：抽成函数，位置不变 */

    EnsureThrowSelectedValid();   /* v1.14：手里不能是空的 */
    if (lastHitTimer > 0.0f) lastHitTimer -= dt;   /* v1.15：命中数字淡出 */
    if (wrenchHitFlashT > 0.0f) wrenchHitFlashT -= dt;   /* 第 1 轮：扳手命中闪光 */
    if (wrenchSwingFxT  > 0.0f) wrenchSwingFxT  -= dt;   /* 第 3 轮：挥空暗标记 */
    if (pawnCd > 0.0f) pawnCd -= dt;   /* 第 8 轮：典当冷却 */
    if (retaliateT > 0.0f) retaliateT -= dt;   /* 第 9 轮：以牙还牙 */
    UpdateMonster(dt);
    {  UpdateMonster2(dt); }
    {  UpdateMonster3(dt); }   /* v2.4.12 第二只 */
    {  FlankUpdate(dt); }   /* v2.4.95 包夹（必须在 AI 之后，否则目标被覆盖） */
    UpdateLightBeast(dt);
        {  UpdateLightBeast2(dt); }   /* 第二只 */
    /* 2026-11-06：跳脸怪已删除，UpdateParasite 不再调用。
       （控制层的每帧推进在下面，与它无关。） */
    /* 2026-11-06（第 4 轮）：控制效果层每帧推进。
       放在这里意味着**只有真正游玩时**才走 —— 暂停 / 设置界面 /
       失焦都不会调用 Update，计时自然停表。 */
    {  ControlTick(dt); }
    /* ==================================================================
     * 第 3 轮（2026-11-08）：**威胁预告**
     * ------------------------------------------------------------------
     * 玩家反馈「不恐怖 / 好安全」。调研结论：恐怖来自预期 ——
     * 怪靠近时先有征兆（远处脚步 → 中距离拖行 → 近了心跳 + 手电闪烁），
     * 而不是凭空出现在脸上。
     * 纯表现层：只读怪的位置，不改任何 AI 状态。
     * ================================================================ */
    /* 2026-11-08（第 11 轮）：训练营七站 —— 走进 1.5 格播一句提示 */
    
    {  (void)TelegraphTick(dt); }
    /* 第 4 轮：**被发现预警** —— 只有守门人有锁定预警，别的怪从
       "没发现"到"开追"是瞬间的，玩家只能硬吃。这里把那一瞬间变成可读的。 */
    {  (void)DetectAlertTick(dt); }
    /* 第 4 轮：第一次被追时，教一次"怎么躲"。
       只在第一次（本局）弹，之后不再打扰。 */
    
    /* ==================================================================
     * 2026-11-06 审计补：**控制效果的唯一输入点**
     * ------------------------------------------------------------------
     * 原来只有抱脸虫的 JMP_MASH 读空格。蛛网也走控制层，却没有代码读
     * ControlPress —— 蛛网会永久占住控制槽：既不限移动，也永远挣脱不掉。
     *
     * 现在所有控制类型共用这一个处理点，持有者用 ControlTakeResult() 取结果。
     * ================================================================ */
    {

        if (ControlActive()) {
            if (IsActionPressed(ACT_DODGE)) {
                ControlPress();
                /* ================================================================
                 * 2026-11-06（子代理审计·高 1）：**必须把边沿清掉**
                 * ----------------------------------------------------------------
                 * 主循环一帧可能跑多次 Update（120Hz 逻辑 vs 60Hz 渲染 ≈ 2 次），
                 * keyPressed 的边沿要到 ClearKeyEdges() 才清。这里消费了却不清，
                 * 同一帧后面的几次 Update 会把**同一次按键**再算一遍：
                 *   60fps 按 2 下就过（要求 3 下）；30fps 按 1 下就过。
                 * QTE 难度随帧率变化，正好推翻"按次计数"这条硬约束。
                 * ================================================================ */
                keyPressed[g_bindings[ACT_DODGE]] = 0;
            }
            if (ControlActive() && ControlTimeLeft() <= 0.0f) ControlTimeout();
        }
    }
    {  SpiderTick(dt); }   /* 2026-11-06 第 5 轮 */
    {  FlareHealTick(dt); }   /* 2026-11-06 第 10 轮 */
    {  NeutralTick(dt); }        /* 2026-11-06 第 11 轮 */
    UpdateLeech(dt);

    /* ==================================================================
     * 2026-11-05 修「荧光棒卡进墙里、建模消失」
     * ------------------------------------------------------------------
     * 原来的墙判定是**点**（ShotBlocked 只看中心那一格），而且出手后
     * 0.15 秒内 `justThrown` 完全跳过判定 ——
     *   玩家贴着墙扔 → 荧光棒先无碰撞飞 1.4 格（9.5 格/秒 × 0.15 秒）
     *   → 早已埋进墙里 → 下一帧判成"两个方向都被挡" → z=0 停在墙内
     *   → 渲染在墙背面 → 玩家看到的就是「东西没了」。
     *
     * 修法：
     *   ① 给荧光棒**实体半径**（中心 + 四个方向各推一个半径都能穿过才算空）
     *   ② 撞墙改成**反弹**（法向反射 + 切向摩擦），不再原地急停
     *   ③ 宽限期压到 0.08 秒；结束后如果还卡在墙里，朝玩家方向退出来
     * ================================================================== */
}

static void UpdateThreatsAndAudio(float dt, const PlayerFrameInput* movement) {
    UpdatePlayerAudio(dt, movement);
    UpdateWorldThreats(dt, movement);
}

static void UpdateWorldParticles(float dt) {
    #define THROWN_R        0.28f   /* 荧光棒的实体半径（格） */
    #define THROWN_BOUNCE   0.45f   /* 撞墙后保留的反弹速度 */
    #define THROWN_FRICTION 0.80f   /* 另一根轴的摩擦 */
    /* 带体积的墙判定：中心 + 四个方向各推一个半径，全都不是墙才算空 */
    #define THROWN_SOLID(x, y) ( \
        ShotBlocked((x), (y)) || \
        ShotBlocked((x) + THROWN_R, (y)) || \
        ShotBlocked((x) - THROWN_R, (y)) || \
        ShotBlocked((x), (y) + THROWN_R) || \
        ShotBlocked((x), (y) - THROWN_R) )

    /* 荧光棒/金币物理更新：飞行中受重力，落地转静止 */
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        if (thrown[i].z > 0.0f) {
            /* 飞行中：重力积分 */
            float nx = thrown[i].x + thrown[i].vx * dt;
            float ny = thrown[i].y + thrown[i].vy * dt;
            thrown[i].z += thrown[i].vz * dt;
            thrown[i].vz -= GRAVITY * dt;

            /* 撞墙预判：分离 X/Y 轴检测，允许滑动。
               v1.9：刚出手的前几帧跳过判定 —— 面朝墙扔的时候，
               nx/ny 立刻落进墙里，会被判成"两个方向都被挡"，
               于是速度清零、荧光棒垂直掉在脚下（玩家看到的就是"不飞"）。
               宽限期内允许它先离开手心。 */
            /* 宽限期 0.15 → 0.08 秒：够离开手心，又不至于钻进墙里 */
            int justThrown = (thrown[i].life > 24.92f);
            int blockedX = justThrown ? 0 : THROWN_SOLID(nx, thrown[i].y);
            int blockedY = justThrown ? 0 : THROWN_SOLID(thrown[i].x, ny);

            /* **反弹而不是急停**：法向速度取反并衰减，切向速度摩擦。
               这样贴着墙斜着扔会「擦墙滑一下再弹开」，
               直着砸墙会「弹回来落在脚边」，而不是原地消失。 */
            if (blockedX) {
                thrown[i].vx = -thrown[i].vx * THROWN_BOUNCE;
                thrown[i].vy *= THROWN_FRICTION;
                if (fabsf(thrown[i].vx) < 0.25f) thrown[i].vx = 0.0f;
            } else {
                thrown[i].x = nx;
            }
            if (blockedY) {
                thrown[i].vy = -thrown[i].vy * THROWN_BOUNCE;
                thrown[i].vx *= THROWN_FRICTION;
                if (fabsf(thrown[i].vy) < 0.25f) thrown[i].vy = 0.0f;
            } else {
                thrown[i].y = ny;
            }

            /* 宽限期结束后还卡在墙里（出生点贴墙）→ 朝玩家方向退出来。
               没有这一步，它仍然会停在墙内不再可见。 */
            if (!justThrown && THROWN_SOLID(thrown[i].x, thrown[i].y)) {
                float bx = thrown[i].x - dirX * 0.6f;
                float by = thrown[i].y - dirY * 0.6f;
                if (!THROWN_SOLID(bx, by)) { thrown[i].x = bx; thrown[i].y = by; }
                thrown[i].vx = 0.0f;
                thrown[i].vy = 0.0f;
            }
            /* v1.9：这里原来还有一次 vz -= GRAVITY*dt ——
               一帧减两次重力，抛物线直接塌成"砸脚面"。已删除。 */

            /* 落地 */
            if (thrown[i].z <= 0.0f) {
                thrown[i].z = 0.0f;
                thrown[i].vz = 0.0f;
                thrown[i].vx *= 0.3f;  /* 滑动摩擦 */
                thrown[i].vy *= 0.3f;
            }
        } else {
            /* 地面滑动衰减 + 滑动碰撞 */
            float nx = thrown[i].x + thrown[i].vx * dt;
            float ny = thrown[i].y + thrown[i].vy * dt;

            /* 地面滑行同样要用体积判定 + 轻微反弹，
               否则会「滑进墙脚」再消失 */
            int blockedX = THROWN_SOLID(nx, thrown[i].y);
            int blockedY = THROWN_SOLID(thrown[i].x, ny);

            if (!blockedX) thrown[i].x = nx;
            else thrown[i].vx = -thrown[i].vx * 0.35f;
            if (!blockedY) thrown[i].y = ny;
            else thrown[i].vy = -thrown[i].vy * 0.35f;

            thrown[i].vx *= 0.95f;
            thrown[i].vy *= 0.95f;
        }
        thrown[i].life -= dt;
        if (thrown[i].life <= 0.0f) thrown[i].active = 0;
    }

}

static void CompleteCampaignExit(void) {

            /* v7 第 ㊿ 轮：层数读 kCampaign[].layers，不再写死 3 */
            int stageLayers = CampaignLayersNow();
            if (stageLayers < 1) stageLayers = 1;
            int stageFloor = currentLevel - CampaignStartFloor(campaignStage);
            if (stageFloor + 1 < stageLayers) {
                currentLevel++;
                energy += ENERGY_PER_FLOOR;                      /* v2.3：每层唯一补给 */
        /* v2.4：过关奖励立刻到账，给玩家一个明确的反馈 */
        pickupMsg = (g_lang == LANG_ZH) ? "过关奖励  +能量 +理智" : "LEVEL CLEAR  +ENERGY +SANITY";
        pickupMsgTimer = 2.2f;
                if (energy > ENERGY_MAX) energy = ENERGY_MAX;
                GenerateLevel();
                floorStartTime=runTime;
                ClearFx();
                for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
                flashTime = 0.15f;
                UpdateExplored();
                return;
            }
            WalletAddRun(CampaignReward(campaignStage));
            if (campaignStage < CAMPAIGN_STAGE_COUNT - 1) {
                campaignStage++;
                currentLevel=CampaignStartFloor(campaignStage);
                srand((unsigned)(9000+campaignStage*733));
                GenerateLevel(); floorStartTime=runTime;
                flashTime=0.15f; UpdateExplored(); SaveGame();
                return;
            }
            gameWon = 1;
            gameCleared = 1;   /* v28：跨局记录，重启后 Endless 仍然解锁 */
            runInProgress = 0;
            /* v2.2：战役通关原来**没有 ComputeScore**，结算页显示上一局残留，
               也不给核心。现在补上。 */
            {
                ScoreInput si = { currentLevel, 1, 0, runSearched, bottlesKilled,
                                  runTime, modsSeenCount };
                ComputeScore(&si, &lastScore);
                lastScore.score += 5000;      /* 战役通关固定加成 */
            }
            dailyEscapeCount++; DailyObserve(DQ_ESCAPE,dailyEscapeCount);
            LoopCheckAchievements(); BankSettle(1); hasSave=0;
            campaignMode = 0;
            currentState = STATE_GAMEOVER;
            menuSelection = 0;
            ForceCursor(1);
            uiDirty = 1;
            return;
}

static void AdvanceRegularFloor(void) {
    if (isEndlessRun) {
            /* ---- v1.8：过关上缴（重生细胞式）---- */
            bankStreak++;
            /* v6：每日任务进度 —— 到达层数 / 箱子数 */
            { 

              if (DailyGoalId() == 2) DailySetProgress(currentLevel);
            }
            /* Retain carried gold until the player chooses extraction. */
            /* 记分 + 里程碑核心 */
            {
                ScoreInput si = { currentLevel, 0, 1, runSearched, bottlesKilled, runTime, modsSeenCount };
                ComputeScore(&si, &lastScore);
                lastScore.score += eventBonus;
    /* 2026-11-05：宝箱怪 —— 每甩掉一只 +400，一局上限 2 只。
       和事件加分走同一条通道（结算页不单独列一行）。 */
    { 
      lastScore.score += MIM_SCORE_BONUS * mimicsEscapedThisRun; }   /* v1.8：事件加分 */
                if (currentLevel > bestEndlessLevel) bestEndlessLevel = currentLevel;
                if (lastScore.score > bestEndlessScore) bestEndlessScore = lastScore.score;
            }
            if (currentLevel == 20 || currentLevel == 40 ||
                currentLevel == 60 || currentLevel == 80) WalletAddRun(GOLD_PER_CORE);   /* v3.4 */
            /* v3.0：**每层 +35 能量**（数值审查 #1 的核心修复）——
               原来这行只写在战役分支里，主线/无尽从不补给，
               一局只有 19 刀的能量，第 2 层就开始「打不起」。 */
            energy += ENERGY_PER_FLOOR;
            if (energy > (float)EnergyMax()) energy = (float)EnergyMax();
            currentLevel++;
GenerateLevel();
        } else {
            /* v1.8：主线也一样上缴 */
            bankStreak++;
            /* v6：pending 已并入 carryGold，不再需要合并 */
            energy += ENERGY_PER_FLOOR;                        /* v3.0：主线每层补给 */
            if (energy > (float)EnergyMax()) energy = (float)EnergyMax();
            currentLevel++;
            GenerateLevel();
        }
}

static void UpdateExitProgression(float dt) {
    /* 拾取提示计时 */
    if (pickupMsgTimer > 0.0f) {
        pickupMsgTimer -= dt;
        if (pickupMsgTimer <= 0.0f) pickupMsg = NULL;
    }

    /* 出口：通电后才能进下一关；第 WIN_LEVEL 层的出口 = 逃出生天。
       教程的出口只负责把教程标记为完成，不参与正常换层。 */
    /* v6：**走到出口先弹「撤离 / 深入」**，不再直接换层 */
    /* v6 **最终层不弹二选一** ——
       第 WIN_LEVEL 层的出口是「逃出生天」，必须走通关结算（含成就）。
       原来弹窗会把它截成「撤离」，导致通关画面/成就永远不出现。 */
    int isFinalFloor = campaignMode || (currentLevel >= WIN_LEVEL && !isEndlessRun);
    int atExit = (breakerOpen &&
                  Dist2(posX, posY, exitX, exitY) < EXIT_ENTER_R2 * EXIT_ENTER_R2);

    if(!trainingMode && atExit)RunEconomyFloorGold();

    /* v6 **出口二选一：弹窗** ——
       只有「还没弹过、也不是刚选了深入」才弹。 */
    if (!trainingMode && !isFinalFloor && !exitChoiceOpen && !exitGoDeeper && atExit) {
        exitChoiceOpen = 1;
            {  HintOnce(3); }   /* v7 B4 */
        exitChoiceT = 0.0f;
        MusicNotify(MUSIC_EVENT_GATE);
        PlaySfx(SFX_GATE, 0.65f);
        uiDirty = 1;
    }
    /* v6 **前进条件** ——
       原来这里写的是 !exitChoiceOpen，而上面刚把它置 1，
       所以整段「换下一层」永远不执行（按 2 只会重复弹窗）。
       现在：最终层直接前进；非最终层必须玩家确认过「深入」。 */
    if (!trainingMode && atExit && (isFinalFloor || exitGoDeeper)) {
        exitGoDeeper = 0;
        /* NOCTURNAL：本层全程手电关着过关 */
        if (!flashlightOn) flashlightOffLevel = 1;

        MusicNotify(MUSIC_EVENT_GATE);
        PlaySfx(SFX_GATE, 0.65f);
        float completedTime = runTime-floorStartTime;
        if (completedTime<90.0f) floorsFast++;
        if (!floorSpotted) floorsPacifist++;
        if (!floorTookDamage) { floorsFlawless++; floorsNoSanityLoss++; }
        else floorsNoSanityLoss=0;
        if (!runLightUse) floorsNoLight++;
        if (WardenActive() && !floorTookDamage) wardenCleanFloors++; else wardenCleanFloors=0;
        DailyObserve(DQ_NOHIT,floorsNoSanityLoss);
        int evThisLevel = levelEvent;   /* v1.8：GenerateLevel 会重置 levelEvent，先存 */
        /* Floor gold already paid once on reaching the powered exit. */
        /* v6 **探索奖励** —— 按本层容器数折算，补上之前缺失的 GOLD_NEWROOM 渠道。
           原来 GOLD_NEWROOM 只定义不发放，「首次进新房间 +5」这条收入是空的。 */
        /* Exploration stipend is included in the same once-per-floor reward. */   /* v2.0 */
        /* Both extraction and deeper-floor choices share that reward. */
        if (currentLevel > bestLevel) bestLevel = currentLevel;
        /* Achievement conditions are evaluated from LoopCheckAchievements. */
        /* 战役模式：每段连续 3 层；段内换层不重置资源，段间才切规则/重开资源。 */
        if (campaignMode) { CompleteCampaignExit(); return; }
        /* v2.2：每日挑战通关 —— 今天第一次给 1 核心 */
        if (dailyMode && !dailyClearedToday) {
            dailyClearedToday = 1;
            WalletAddRun(GOLD_PER_CORE);   /* v3.4 */
        }
        if (currentLevel >= WIN_LEVEL && !isEndlessRun) {
            /* 主线通关：走进第 10 层的出口就是结局，不再生成下一层 */
            gameWon = 1;
            gameCleared = 1;   /* v28：跨局记录，重启后 Endless 仍然解锁 */
            runInProgress = 0;
            /* Settle after all victory rewards and achievement checks. */
            currentState = STATE_GAMEOVER;
            menuSelection = 0;
            ForceCursor(1);
            /* 2026-11-06（R4）：光标样式归平台层；ForceCursor(1) 已处理可见性 */
            /* 通关时检查成就 */
            {
                /* v1.8：主线固定分 + 通关给 1 个核心 */
                ScoreInput si = { currentLevel, 1, 0, runSearched, bottlesKilled, runTime, modsSeenCount };
                ComputeScore(&si, &lastScore);
                if (lastScore.score > bestMainScore) bestMainScore = lastScore.score;
                WalletAddRun(GOLD_PER_CORE);   /* v3.4 */
            }
            AchRecordFlag(ACH_FLAG_FIRST_EXIT);
            {  dailyEscapeCount++; }   /* v7：每日「撤离 N 次」 */

            /* v2.8：属性面板删了，成就改成「技能树普通节点全满」 */

            /* v3.3 **修复 3 个永远解不开的通关成就** ——
               它们的判定写在 CheckAchievements 里（gameWon && ...），
               但 CheckAchievements 只在 STATE_PLAYING 执行，
               而本函数上面第 3485 行已经把 currentState 设成 GAMEOVER 了，
               所以这三条判定从来没有被求值过。
               在这里用同样的条件补一次。 */
            /* v7 第 ㊿ 轮：这三条原来用旧 40 条表的索引
               39 / 35 / 36 —— 新表只有 32 条，越界的被静默丢弃。
               39(TRUE ENDING) 和 35(BLOOD PRICE) 在新表里没有对应项，删掉；
               36(GHOST RUN) → 20(LIGHTLESS)，语义完全一致。 */

            DailyObserve(DQ_ESCAPE,dailyEscapeCount);
            LoopCheckAchievements(); BankSettle(1); hasSave=0;
            uiDirty = 1;
            return;
        }
        /* v2.1：**主线也回智**。原来只有无尽模式给，导致主线比无尽还穷，
           第 4~6 层必死 —— 一条 10 层的主线硬生生变成了不可能。
           无尽给 30（原 12 的 2.5 倍），主线给 18。 */
        {
            /* v3.0 平衡：**层清奖励随层涨** ——
       原来恒定 +18，而每层理智流失涨 0.012/秒，
       深层等于是净亏，第 24 层每层净掉 30+ 理智。 */
    /* v3.0 平衡修正：**奖励必须涨得比流失慢**。
       流失每层 +0.012/秒 = 每层 +0.72 点/分钟，
       上一版给 +2 点/层 → 净 +1.28/层，深层反而越玩越赚，难度曲线是反的。
       改成 +0.5 点/层：第 1 层净 -12、第 24 层净 -17，越深越紧。 */
    float gain = isEndlessRun ? (LEVEL_CLEAR_SANITY + 18.0f)
                              : (18.0f + (float)currentLevel * 0.5f);
            sanity += gain;
            if (sanity > SanityMax()) sanity = SanityMax();
        }
        AdvanceRegularFloor();
        /* ==================================================================
         * v6 第 ㊲ 轮：**局内三选一技能终于会出现了**
         * ==================================================================
         * 全量审计发现 OpenSkillPick() **全工程 0 调用点** ——
         * 这个界面（STATE_SKILL_PICK）、选项生成、按键处理、
         * ui.c 的绘制，全都在，**但没有任何东西会打开它**。
         * 一个完整的 Roguelite 系统就这么躺在代码里没人用。
         *
         * 设计：每通关 2 层给一次三选一。
         * 插在这里（而不是某个分支里），主线 / 下潜两条路都会经过。
         * ================================================================ */
        /* ==================================================================
         * v7 第 59 轮：**12 条遗物一条都拿不到**
         * ==================================================================
         * `OpenRelicPick()` 全工程 0 调用点 —— 和上面 OpenSkillPick 一模一样的病：
         * 选项池、pickMode==1、STATE_SKILL_PICK 的绘制与按键、RelicTake()
         * 全都写好了，**但没有任何东西会打开它**。
         * 结果 8 个 Relic*Mul() 恒返 1.0，遗物系统整条不存在。
         *
         * 设计：每 3 层给一次遗物三选一（比技能稀有），
         *       和技能错开层级（技能在偶数层，遗物在 3 的倍数层）。
         * ================================================================ */
        RewardScheduleForLevel(currentLevel);

    floorSpotted = 0;
    /* v3.3：换层时清空胶水，否则上一层扔的会残留到下一层 */
    for (int gi = 0; gi < MAX_GLUE; gi++) { glues[gi].active = 0; glues[gi].life = 0.0f; }
        floorTookDamage = 0;
        floorStartTime = runTime;
        LoopCheckAchievements();

        /* v1.8：通过本层 → 事件加分进本局总分 */
        if (evThisLevel >= 0) eventBonus += EventScore(evThisLevel);

        /* v3.3：取消每层/每两层的三选一。玩家明确要求不再弹 CHOOSE ONE。 */

        /* 重置本层/本局的成就追踪变量 */
        sprintedThisLevel = 0;
        fusesCollectedRun = 0;
        flashlightOffLevel = 0;
        /* v3.3：mapsFullyExplored 不再每层清零 ——
       否则一个 24 层的主线最多只有 1，MAP MASTER(>=3) 永远解不开。 */
        ClearFx();                    /* 换关解除所有状态效果 */
        for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
        flashTime = 0.15f;
        UpdateExplored();
    }

    /* 新手教程：放在最后，保证所有交互/移动状态都已经更新完 */
    if (trainingMode) TutorialUpdate(dt);

}

void Update(float dt) {
    int inSafe;
    if (!UpdatePreflight(dt)) return;
    if (!UpdatePlayableTimers(dt, &inSafe)) return;
    if (!UpdateSurvival(dt, inSafe)) return;
    if (!UpdateCoverInput()) return;
    if (!UpdateEarlyActions(dt)) return;
    UpdateTimers(dt);   /* v8: 回声扫描 / 锁定窗口 / 周期自动存档（src/uptimers.c） */
    /* v3.0 改造树「消失」：脱离视线 3 秒后完全隐身。
       判据用「怪物是否在追捕状态」——它没在追（游荡/搜寻）就算脱离视线。 */
    PlayerFrameInput movement = PlayerPrepareMovement(dt);
    if (!UpdateRestAndShop(dt, inSafe)) return;
    UpdateLegacyEffects(dt);
    UpdatePhaseAndCrank(dt);
    if (!UpdateMovementAndHatches(dt, &movement)) return;
    UpdateHeldInteraction(dt);
    if (!UpdateThrowInput(dt)) return;
    UpdateThreatsAndAudio(dt, &movement);
    UpdateWorldParticles(dt);
    RunEconomyTick(dt);
    UpdateExitProgression(dt);
}

void LoopResetDecoyCooldown(void) { s_decoyKeyCd = 0.0f;HushReset(); }


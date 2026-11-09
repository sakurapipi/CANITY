/* Orchestration ownership split; stage ordering remains in the entry point. */
#include "common.h"
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

void LoopCheckAchievements(void) {
    if(trainingMode) return;
    AchSnap s;
    memset(&s, 0, sizeof(s));
    s.level            = currentLevel;
    s.gameWon          = gameWon;
    s.totalGoldEarned  = totalGoldEarned;
    s.carryGold        = carryGold;
    {  s.bankGold = bankGold; }
    s.totalThrown      = totalThrown;
    s.hideCount        = hideCount;
    /* v7 第 ㊿ 轮：**这三个原来写死 0**
       → DISRUPTOR(击退20次) / PITCHER(投掷命中10次) / CARTOGRAPHER(回声100格)
         这三个成就永远解不开。现在读真实计数器。 */
    { 
      s.knockCount    = knockCountRun;
      s.throwHitCount = throwHitCountRun; }
    {  s.bestiarySeenMask = bestiarySeen; }
    {  s.skillNodesLit = SkillNodesLit(); }
    {  s.maxThreatThisRun = (int)ThreatValue(); }
    {  s.echoCellsTotal = echoCellsTotalRun; }

    /* flags：特殊条件（0 未用）
       1 进入过游戏 / 2 存活10分 / 3 存活30分 / 4 抢回赃款3次
       5 零被抓撤离 / 6 完美逃脱 / 7 守门人三层 / 8~14 见 kAchDefs 注释 */
    s.flags[ACH_FLAG_STARTED] = !trainingMode;
    if (gameWon && CatchCountThisRun() == 0) s.flags[ACH_FLAG_CLEAN_ESCAPE] = 1;
    /* v7 第 ㊿ 轮：TEN MINUTES / HALF HOUR 的 flag 从来没人置位 */
    if (runTime >= 600.0f)  s.flags[ACH_FLAG_TEN_MINUTES] = 1;
    if (runTime >= 1800.0f) s.flags[ACH_FLAG_HALF_HOUR] = 1;
    /* 撤离一次 → FIRST EXIT（独立 flag 15，不再和 FIRST BREATH 共用一个） */
    if (gameWon) s.flags[ACH_FLAG_FIRST_EXIT] = 1;
    s.flags[ACH_FLAG_PERFECT_ESCAPE]=gameWon && sanity>=SanityMax()-0.01f;
    s.flags[ACH_FLAG_WARDEN_CLEAN]=wardenCleanFloors>=3;
    s.flags[ACH_FLAG_UNDER_WARDEN]=gameWon && WardenActive() && WardenWatching();
    s.flags[ACH_FLAG_DOUBLE_HUNT]=doubleHuntSeconds>=30.0f;
    s.flags[ACH_FLAG_GOLD_ON_FLOOR_12]=currentLevel>=12 && WalletRun()>=ACH_GOLD_FLOOR_12;
    s.flags[ACH_FLAG_FULL_HIDE]=fullHideDone;
    /* 无光撤离 / 空手撤离 / 完美撤离 */
    if (gameWon && !runLightUse)        s.flags[ACH_FLAG_LIGHTLESS] = 1;
    if (gameWon && runSearched == 0)     s.flags[ACH_FLAG_NO_CONTAINER] = 1;
    /* v7 第 56 轮：最后两个没有置位点的 flag */
    { 
      if (bountyRecovered >= 3) s.flags[ACH_FLAG_CLAWBACK] = 1; }              /* CLAWBACK */
    { 
      if (metaResetDone) s.flags[ACH_FLAG_META_RESET] = 1; }                    /* META RESET */

        /* ==================================================================
     * v7 第 ㊿ 轮：**每日任务 8 个目标，原来只有 1 个会推进**
     * ==================================================================
     * 旧代码：DailySetProgress() 全项目只有一个调用点，
     *         条件是 `if (DailyGoalId() == 2)` 且包在 `if (isEndlessRun)` 里。
     *         → 抽到「开 40 个箱子」的玩家，HUD 永远显示 0/40。
     *
     * 现在每帧把当前局的状态映射到对应目标上。
     * DailySetProgress 是**取最大值**的，所以传累计值即可。
     * ================================================================ */
    {

        int v = 0;
        switch (DailyGoalId()) {
        case 0: v = totalGoldEarned;                        break;  /* 收集金币 */
        case 1: v = dailyEscapeCount;                       break;  /* 撤离次数 */
        case 2: v = currentLevel;                           break;  /* 到达层数 */
        case 3: v = runSearched;                            break;  /* 开箱数 */
        case 4: v = knockCountRun;                          break;  /* 击退 */
        case 5: v = throwHitCountRun;                       break;  /* 投掷命中 */
        case 6: v = (int)(runTime / 60.0f);                 break;  /* 存活分钟 */
        case 7: v = floorsNoSanityLoss;                     break;  /* 无伤层数 */
        default: break;
        }
        DailyObserve(DailyGoalId(),v);
    }

    AchCheck(&s);

    /* v7 第 ㊿ 轮：这里原来 `if (gameWon) s.flags[ACH_FLAG_TEN_MINUTES] = 0;`
       写在 AchCheck() **之后**，是空操作；而且 flags[2] 现在是
       TEN MINUTES 的标记，清掉它就是错。已删除。 */
}

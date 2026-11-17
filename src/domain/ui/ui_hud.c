/* 2026-11-16：从 ui.c 拆出的**局内 HUD**（域内解耦，行为不变）。
   ui.c 已到 3948 行，超出解耦闸门；HUD 这一簇（15 个函数、约 1400 行）
   只依赖公共绘制 API 与三个模块名表，整体搬走不改变状态归属。 */
#include "common.h"
#include "monster_rules.h"
#include "hush.h"
#include "room_theme.h"
#include "perception.h"
#include "item_effects.h"
#include "run_rewards.h"
#include "state.h"
#include "threat_telegraph.h"
#include "wrench.h"
#include "campaign.h"
#include "daily.h"
#include "economy.h"
#include "threat.h"
#include "upgrade_run.h"
#include "wallet.h"
#include "warden.h"
#include "build_tags.h"
#include "cell_meta.h"
#include "plat.h"
#include "spider.h"
#include "control_effect.h"
#include "hatch.h"
#include "map_rooms.h"
#include "art_direction.h"
#include "achievements.h"
#include "events.h"
#include "ui.h"
#include "ui_text.h"
#include "ui_pages.h"
#include "ui_menu.h"
#include "lang.h"
#include "weapons.h"
#include "shop.h"
#include "bench.h"
#include "lockbox.h"   /* v3.0 锁盘 */   /* v2.7 改造树 */
#include "ui_theme.h"
#include "sprites.h"
#include "relics.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "skills.h"
#include "bestiary.h"
#include "score.h"
#include "entity.h"
#include "render.h"
#include "save.h"

static const char* modNames[MOD_COUNT] = {"DARK", "HUNTED", "CRAMPED", "LEAKY", "RICH", "CALM"};

static const char* modDesc[MOD_COUNT] = {
    "FLASHLIGHT STARTS LOW",
    "THE THING MOVES FASTER",
    "FEWER CONTAINERS",
    "SANITY DRAINS FASTER",
    "MORE CONTAINERS",
    "SANITY DRAINS SLOWER"
};

static const char* themeNames[THEME_COUNT] = {
    "BRICK", "PRISON", "HELL", "WARD", "ICE VAULT", "RUST WORKS", "FLESH CAVE"
};

typedef struct HudBarLayout {
    int barW, barH, labelX, barX, rowGap, y1, y2;
} HudBarLayout;

static HudBarLayout HudBuildBarLayout(void) {
    /* 手电电量 / 体力：左下两条细条，标签按实际文字宽度排版 */
    int barW = UIS(180), barH = UIS(12);
    int labelX = UIS(20);
    /* v3.0：能量条已删，只剩 LIGHT / RUN 两条，按最宽标签算起点 */
    int barX = labelX + TextWidthWin("LIGHT") + UIS(14);
    /* v1.9：两行标签原来固定差 UIS(22)，而一行文字就有 30 逻辑像素高，
       于是 LIGHT 和 RUN 叠在一起（截图里能看到两个词压成一团）。
       现在行距 = 实际文字高度 + 一点间隙，任何窗口尺寸都不会重叠。 */
    int rowGap = TextHeightWin() + UIS(8);
    int y1 = winH - UIS(60) - rowGap;
    int y2 = y1 + rowGap;


    HudBarLayout layout = { barW, barH, labelX, barX, rowGap, y1, y2 };
    return layout;
}

static void DrawHudRunHeader(int effDetail) {
    char buf[96];

    /* ------------------------------------------------------------------ */
    /* HUD 详细度                                                        */
    /* ------------------------------------------------------------------ */
    /* 三档（设置里的 HUD Detail）：
         0 Minimal —— 只留必须实时知道的：数值、电量体力、状态效果
         1 Normal  —— 加：投掷物数量（有库存时才显示）、SAFE ZONE 短提示
         2 Full    —— 加：投掷物数量常驻、SAFE ZONE 常驻
       词条横幅在任何档位都只在进关后 5 秒出现（levelBanner 控制），
       因为它是「一次性信息」，常驻反而是噪音。 */

    /* ==================================================================
     * 2026-11-08（第 10 轮）：**HUD 精简**
     * ------------------------------------------------------------------
     * 玩家原话：「左上角太多数值了，新手看不懂」。
     * 常驻只留 4 项（LEVEL / GOLD / SANITY / THREAT>0），
     * 按住 Tab 临时展开完整 HUD —— 信息没删，只是默认不糊在脸上。
     * ================================================================ */

    int goldRowEnd = 0;   /* v2.4.90 顶行金币栏的右边界 */
    sprintf(buf, Tr("LEVEL %d", "第 %d 层"), currentLevel);
        if (campaignMode) {
            char cb[64];
            sprintf(cb, Tr("  CAMPAIGN %d/5 %s", "  战役 %d/5 %s"),
                    campaignStage + 1, campaignName(campaignStage));
            strncat(buf, cb, sizeof(buf) - strlen(buf) - 1);
        }
        if (dailyMode) {
            char db[48];
            sprintf(db, Tr("  DAILY %d", "  每日 %d"), dailySeed % 10000);
            strncat(buf, db, sizeof(buf) - strlen(buf) - 1);
        }
    DrawTextWin(UIS(20), UIS(18), buf, 0x0000FF00u);

    /* ===== v2.4.90 **LEVEL 右边显示当前持有的黄金** =====
       用户要求：「在 level one 的右边新增 1 个 UI 显示当前持有的黄金数量。」
       放在 LEVEL 同一行紧右边，不新增行 ——
       顶部每一行都很贵，能并到既有行就并。

       格式：GOLD 12345
       颜色：暖金 0xFFC040（和 LEVEL 的绿色、词条的橙色都区分开）
       宽度不够时退化成 G12.3k 这种缩写。 */
    {

        static char gbuf[48];
        int gx0 = UIS(20) + TextWidthWin(buf) + UIS(16);

        if (carryGold >= 1000000)
            snprintf(gbuf, sizeof(gbuf), "GOLD %.1fM", (double)carryGold / 1000000.0);
        else if (carryGold >= 10000)
            snprintf(gbuf, sizeof(gbuf), "GOLD %.1fk", (double)carryGold / 1000.0);
        else
            snprintf(gbuf, sizeof(gbuf), Tr("GOLD %d", "黄金 %d"), carryGold);

        /* 放不下就缩成 G12345 */
        if (gx0 + TextWidthWin(gbuf) > winW - UIS(12)) {
            snprintf(gbuf, sizeof(gbuf), "G%d", carryGold);
        }
        DrawTextWin(gx0, UIS(18), gbuf, UI_GOLD);

        /* ===== v6：金币右边再挂一个 THREAT =====
           威胁值是这套设计的核心反馈 —— 玩家必须随时看到
           "我点得越多，怪越凶"。所以它和金币同一行，紧挨着。

           颜色：正常灰青；超过 12 时用品红呼吸（警告） */
        {
            static char tbuf[32];
            float tv = ThreatValue();
            int tx0 = gx0 + TextWidthWin(gbuf) + UIS(14);
            /* 2026-11-08：**威胁为 0 时不显示** —— 开局那个 THREAT 0.0
               对新玩家没有任何决策价值，还占着第一行。
               注意：这里**不能 return**（那会把整个 DrawHUD 退掉，
               SANITY / FUSES / 状态条全都不画了 —— 实测截图为证）。 */
            if (tv <= 0.0f) {
                goldRowEnd = tx0;
            } else {
            snprintf(tbuf, sizeof(tbuf), "THREAT %.1f", (double)tv);
            {
                /* v6 铁律十九：三态全部来自调色盘
                     THREAT = 0   → PAL_DIM（灰，无威胁）
                     0 < T < 12   → PAL_CYAN（青，正常）
                     T >= 12      → PAL_MAGENTA（品红，警告）
                   "品红 = 警告" 是所有页面共用的语言。 */
                unsigned int tc;
                if (tv >= 12.0f)      tc = PAL_MAGENTA;
                else if (tv > 0.0f)   tc = PAL_CYAN;
                else                  tc = PAL_DIM;
                DrawTextWin(tx0, UIS(18), tbuf, tc);
            }
            goldRowEnd = tx0 + TextWidthWin(tbuf) + UIS(14);
            }   /* end tv > 0 */
        }
    }

    /* 当前词条：常驻显示在 LEVEL 那一行的右边 —— **不新增一行**。
       词条是难度曲线的一部分（HUNTED 怪更快、LEAKY 理智掉更快），
       玩家看不见就不知道自己在被什么惩罚。
       用暗橙色区分于绿色的 LEVEL。
       详细说明（"THE THING MOVES FASTER"）仍然只在进关 5 秒的横幅里出现，
       常驻的是「提醒」，横幅是「解释」。 */
    /* 2026-11-08：**hudDetail 三档真正生效**。
       词条改变规则（怪更快 / 理智掉更快），Normal 起显示。 */
    if (effDetail >= 1 && levelMod >= 0 && levelMod < MOD_COUNT) {
        int lx = goldRowEnd;
        sprintf(buf, "%s", modNames[levelMod]);
        DrawTextWin(lx, UIS(18), buf, 0x00C08040u);
    }
    /* v1.8：本层的随机事件（通过本层会给本局加分） */
    /* v1.8：事件**不再单独占一行**。
       原来画在 y=UIS(36)，而文字高度在小窗口下就有 30 多逻辑像素，
       结果和 LEVEL / SANITY 叠在一起（截图里就是这样）。
       现在并到首行右侧，跟词条同一行，永远不会撞。 */
    if (effDetail >= 2 && levelEvent >= 0 && levelEvent < EVENT_COUNT) {
        int ex = goldRowEnd;
        if (levelMod >= 0 && levelMod < MOD_COUNT) {
            char mb[32];
            sprintf(mb, "%s", modNames[levelMod]);
            ex += TextWidthWin(mb) + UIS(14);
        }
        /* 宽度不够时自动缩写，别让文字被窗口裁掉 */
        /* ==================================================================
         * v7 第 61 轮：**EventNameZh / EventDescZh 全工程 0 调用点**
         * ==================================================================
         * 中文界面下事件横幅永远是英文名，而且**每一层的规则说明
         * 从来没有显示过** —— 玩家只能靠死来学"这层到底怎么了"。
         *
         * 现在：中文用中文名；横幅放不下时，把说明塞进底部提示行。
         * ================================================================ */


        int avail = winW - ex - UIS(12);
        {
            const char* en = (g_lang == LANG_ZH) ? EventNameZh(levelEvent) : EventNameEn(levelEvent);
            sprintf(buf, "EVENT: %s +%d", en, EventScore(levelEvent));
        }
        if (TextWidthWin(buf) > avail) {
            sprintf(buf, "EV: %s +%d", EventShortEn(levelEvent), EventScore(levelEvent));
            if (TextWidthWin(buf) > avail) sprintf(buf, "+%d", EventScore(levelEvent));
        }
        DrawTextWin(ex, UIS(18), buf, 0x00E060C0u);

        /* v7 第 62 轮：CampaignRule 全工程 0 调用点 —— 每关改的规则从没告诉过玩家 */
        {


            if (campaignMode && levelBanner > 3.0f) {
                const char* r = (g_lang == LANG_ZH) ? CampaignRuleZh(campaignStage)
                                                    : CampaignRule(campaignStage);
                if (r && r[0]) {
                    char rb[180];
                    snprintf(rb, sizeof(rb), "%s", r);
                    ClampRowText(rb, winW - UIS(32));
                    DrawTextCenterWin(winW / 2, winH / 2 + UIS(40), rb, 0x0060D0FFu);
                }
            }
        }

        /* 规则说明：只在**本层刚开始的 6 秒**显示，不长期占屏 */
        {

            if (levelBanner > 3.0f) {
                const char* desc = (g_lang == LANG_ZH) ? EventDescZh(levelEvent)
                                                       : EventDescEn(levelEvent);
                if (desc && desc[0]) {
                    char db[160];
                    snprintf(db, sizeof(db), "%s", desc);
                    ClampRowText(db, winW - UIS(32));
                    DrawTextCenterWin(winW / 2, winH / 2 + UIS(70), db, 0x00E060C0u);
                }
            }
        }
    }

    /* v1.8：待上缴金币 + 绣刀冷却。
       注意：右上角 FPS 那一行也占着 y=UIS(18)，所以这两行要排在它下面，
       否则开 FPS 时会叠在一起。 */
    {
        /* v3.0：PENDING 行删掉后 hy 没人用了，显式标记避免编译告警。 */
        /* ==================================================================
         * 2026-11-05 修 UI 重叠：**日游行和 LEVEL 行画在同一个 y**
         * ------------------------------------------------------------------
         * 左边 HUD 的纵向分配是：
         *     UIS(18)                    LEVEL / CAMPAIGN / DAILY 种子  (3447)
         *     UIS(18)                    DAILY 进度 + 目标文本          (本块) ← 重叠
         *     hudY1 = 18 + 1*(TH+4)      SANITY
         *     hudY2 = 18 + 2*(TH+4)      FUSES
         *     hudY3 = 18 + 3*(TH+4)      CARRY / TIER
         *     18 + 4*(TH+4)              BOOST
         *
         * 两行都从 x≈12 起、青色和绿色几乎同色，叠起来就是一坨乱码
         * （用户截图：DAILY 0/15 和 LEVEL 那行糊在一起）。
         *
         * 现在把日游行排到整叠之后：18 + 5*(TH+4)。
         * ================================================================ */
        int hy = UIS(18) + 5 * (TextHeightWin() + UIS(4));
        if (showFps) hy += TextHeightWin() + UIS(4);
        (void)hy;
(void)hy;

        /* ==================================================================
         * v6 第 ㊶ 轮：**每日任务在局内终于可见了**
         * ==================================================================
         * daily.c 的 DailyCurrent / DailyGoalParam / DailyDone 一直是死代码 ——
         * 玩家只在开局看到一行提示，之后完全不知道进度到哪。
         * 这里用 PENDING 行删掉后腾出的位置，不占新空间。
         * ================================================================ */
        if (dailyMode) {

            if (DailyDone()) {
                sprintf(buf, "DAILY  COMPLETE");
                DrawTextWin(UIS(12), hy, buf, UI_ACCENT);
            } else {
                int cur = DailyCurrent(), goal = DailyGoalParam();
                int avail = winW - UIS(24);
                /* v7 第 61 轮：**DailyGoalTextZh 全工程 0 调用点**
                   中文界面下每日任务全程英文，玩家读不懂自己该干什么。 */
                sprintf(buf, "DAILY %d/%d  %s", cur, goal,
                        (g_lang == LANG_ZH) ? DailyGoalTextZh() : DailyGoalTextEn());
                if (TextWidthWin(buf) > avail) sprintf(buf, "DAILY %d/%d", cur, goal);
                DrawTextWin(UIS(12), hy, buf, PAL_CYAN);
            }
        }        /* v3.0：**删掉 HUD 上的 PENDING N** ——
       玩家实测「一整个小时都是 PENDING 12」，既看不懂又永远不变。
       待上缴金币只在安全屋/结算页显示就够了。 */

    }

    {
        int dg = 0;
        for (int i = 0; i < LEECH_MAX; i++) if (leeches[i].active && leeches[i].draining) dg = 1;
        if (dg) DrawTextCenterWin(winW / 2, UIS(18), "DRAIN", 0x00FF3030u);
    }

}

static void DrawHudVitalsAndStatus(int effDetail) {
    char buf[96];
    /* v2.9：左列改用文字高累加，不再写死 18/52/86 ——
       1080p 下原来 LEVEL 15..80 和 SANITY 44..109 压 36px（报告 严重 #3）。 */
    int hudTH = TextHeightWin();
    /* v2.9：**左上角暗底已彻底删除**（用户要求）。
       那块半透明黑边界很硬，在左上角看起来像一块阴影/渲染错误。
       对比度已由文字颜色本身解决（都提到 4.8:1 以上），不再需要底衬。 */
    int hudY1 = UIS(18) + hudTH + UIS(4);
    sprintf(buf, "SANITY: %.0f%%", sanity);
    DrawTextWin(UIS(20), hudY1, buf, (sanity < 30.0f) ? 0x00FF3C3Cu : 0x0000FFFFu);
    int hudY2 = hudY1 + hudTH + UIS(4);

    /* 2026-11-08：Minimal 档只留 4 项（LEVEL/GOLD/SANITY/THREAT）。
       保险丝进度从 Normal 起显示。 */
    if (effDetail >= 1) {
        if (breakerOpen) sprintf(buf, "POWER ON");
        else sprintf(buf, "FUSES %d/%d", fusesHeld, fusesNeeded);
        DrawTextWin(UIS(20), hudY2, buf, breakerOpen ? 0x0030FF60u : UI_ACCENT);
    }

    /* ===== v3.4 左上角新增两行（用户要求）=====
       ① 带入金币 —— 这局手上还有多少可花
       ② 强度加成 —— 携带额占历史最高的百分比，
                      直接对应银行结算的倍数（倍数 = 1 + 这个值）
       放在 FUSES 下面，和上面三行同一列。 */
    {

        int hudY3 = hudY2 + hudTH + UIS(4);
        /* ===== v2.4.30 **新怪状态行（只保留能改变玩家决定的）** =====
           用户问「这行字有没有实用价值，没有就删掉」，判断标准定成一条：

             **这条状态出现时，玩家会因此做一件不同的事吗？**

               会  → 留
               不会 → 删

           按这条标准砍掉三条：

             SCAVENGER APPROACHING   删 —— 生成时已经有「有东西在靠近…」，
                                          而且它会自动找过来，知道了也做不了什么
             THE COLLECTOR IS NEAR   删 —— 同上，纯粹是「有个东西存在」的信息
             WARDEN PATROLLING       删 —— 巡逻状态不影响任何操作，纯噪音

           保留两条，因为它们分别对应一个**具体动作**：

             WARDEN BLOCKS EXIT      留 —— 出口被堵，要么等 25 秒，要么绕开
             清道夫不再拆箱，不显示拆箱提示。
        */
        {

            const char* st = NULL;
            unsigned int stc = 0x00D0D0D0u;

            if (WardenActive() && WardenBlocking()) {
                st = Tr("WARDEN BLOCKS EXIT", "守门人堵住了出口"); stc = 0x00FF6050u;
            }
            if (st) {
                int sw = TextWidthWin(st);
                DrawTextWin(winW - UIS(20) - sw, hudY3 + hudTH * 3 + UIS(10), st, stc);
            }
        }

        /* v7：丢钱滑条（按住 K 时展开） */
        {

            if (moneyDropOpen) {
                int damt = (int)((long long)carryGold * moneyDropPct / 100);
                char sb2[128];
                sprintf(sb2, Tr("DROP %d%%  =  %d GOLD   LEFT/RIGHT   RELEASE K",
                                "丢钱 %d%%  =  %d 金币   ←/→ 调整   松开 K 丢出"),
                        moneyDropPct, damt);
                DrawTextWin(UIS(20), hudY3 + hudTH * 2 + UIS(10), sb2, UI_ACCENT);
            }
        }
        /* ==================================================================
         * 2026-11-08：**CARRY / TIER / BOOST 只在 Full 档显示**
         * ------------------------------------------------------------------
         * 这三项对新手没有任何决策价值：「CARRY 0  TIER 0  BOOST +0%」
         * 是纯噪音，而它们正好占了左上角两行。
         * 老玩家想要就开 Full —— 信息没删，只是默认不糊在脸上。
         * ================================================================ */
        if (effDetail >= 2) {
            int tier = GoldTier();
            sprintf(buf, Tr("CARRY %d / GOLD TIER %d", "携带 %d / 金币档 %d"), carryGold, tier);
            /* 档位越高颜色越烫：0~4 灰 / 5~9 黄 / 10~15 红 */
            unsigned int tc = (tier >= 10) ? 0x00FF6050u
                            : (tier >= 5  ? UI_ACCENT : 0x00A0A0A0u);
            DrawTextWin(UIS(20), hudY3, buf, tc);
        }

        if (effDetail >= 2) {
            float pressure=EconomyPressureRatio();
            int next=GoldNextThreshold();
            if(next>=0) snprintf(buf,sizeof(buf),Tr("PRESSURE %d%% / NEXT %dG","综合压力 %d%% / 下档 %d金"),(int)(pressure*100.0f+.5f),next);
            else snprintf(buf,sizeof(buf),Tr("PRESSURE %d%% / GOLD MAX","综合压力 %d%% / 金币满档"),(int)(pressure*100.0f+.5f));
            DrawTextWin(UIS(20),hudY3+hudTH+UIS(4),buf,pressure>=.8f?PAL_MAGENTA:PAL_CYAN);
        }   /* end hudDetail >= 2 */
    }

    /* ===== v2.4.56 **扳手状态**（用户要求补 UI）=====
       扳手是「打断拾荒者 → 抢回赃款」的唯一手段，
       但界面上从来没告诉玩家「还剩几次 / 冷却多久」。
       放在 BOOST 下面一行，和左侧 HUD 同一列。 */
    {



        /* ===== v2.4.65 **等级 0 也要显示** =====
           扳手在等级 0 就已经能用（2 次/层），
           我原来写了 `if (lv > 0)` —— 于是默认状态下这一行**永远不显示**，
           用户看到的就是「没有次数、没有功能」。 */
        {
            char wb[64];
            float cdMax = RunUpgWrenchCooldown();
            unsigned int wc = 0x00A0A0A0u;

            if (cdMax < 0.0f) {
                int left = wrenchCharges - wrenchLastUsed;
                if (left < 0) left = 0;
                snprintf(wb, sizeof(wb), Tr("WRENCH  %d/%d", "扳手  %d/%d"),
                         left, wrenchCharges);
                wc = (left > 0) ? 0x0060D0FFu : 0x00806060u;
            } else if (wrenchCd > 0.0f) {
                snprintf(wb, sizeof(wb), Tr("WRENCH  %.1fs", "扳手  %.1f秒"), wrenchCd);
                wc = 0x00806060u;
            } else {
                snprintf(wb, sizeof(wb), "%s", Tr("WRENCH  READY", "扳手  就绪"));
                wc = 0x0060D0FFu;
            }
            /* ============================================================
             * v6 第 ㊼ 轮：**这一行压在别的东西上**
             * ============================================================
             * 用户实机截图：左上角出现一坨叠字
             *   「WE0NHCA2F2」= "WRENCH 2/2" 和另一行文字糊在一起
             *
             * 原因：这里用**绝对行号 7** 算 y
             *   UIS(18) + (TH+4)*7 + 4
             * 而 HUD 其余部分用的是 hudTH 的累加。
             * 窗口稍微一变，第 7 行就落到状态效果 / 提示文字上面。
             *
             * 现在改成**贴着底部的 LIGHT 条往上放** —— 那块区域永远是空的。
             * ============================================================ */
            {
                DrawTextWin(UIS(20), winH - UIS(140), wb, wc);
            }
        }
    }

    /* ==================================================================
     * 守门人**锁定倒计时**（2026-11-03）
     * ------------------------------------------------------------------
     * 他看见你之后不再立刻开追，而是先亮 1.5 秒。
     * 这个数字必须显眼 —— 它是玩家唯一能读到的"还有多久挨抓"。
     * 放在顶部正中，配一条消耗中的进度条。
     * ================================================================ */
    {
        if (WardenSpotting()) {
            char cb[32];
            sprintf(cb, "%.1f", (double)WardenSpotLeft());
            int cxp = winW / 2;
            int yb  = UIS(54);                 /* 顶部，别压 CANITY HUD 第一行 */

            /* 文字：SPOTTED 0.9 */
            char line[48];
            sprintf(line, "%s  %s", "SPOTTED", cb);
            DrawTextCenterWin(cxp, yb, line, 0x00FF5030u);

            /* 进度条：从满到空 */
            {
                int barW = UIS(180), barH = UIS(6);
                int x0 = cxp - barW / 2, y0 = yb + UIS(26);
                float f = WardenSpotFrac();
                BlendRectWin(x0, y0, x0 + barW, y0 + barH, 0x00301810u, 0.75f);
                int fill = (int)((float)barW * f);
                if (fill > 0)
                    BlendRectWin(x0, y0, x0 + fill, y0 + barH, 0x00FF4020u, 0.95f);
                FrameRectWin(x0, y0, x0 + barW, y0 + barH, 0x00FF8060u, 0.85f, 2);
            }
        }
    }

    /* 状态效果：常驻显示在 FUSES 下面。
       玩家必须知道自己被怎么了 —— 否则「视野突然变窄」会被当成 bug。
       纵向排布（UIS 是设计分辨率，600 高）：
         86  FUSES
         116 状态效果 1   142 状态效果 2   168 状态效果 3
         196 FLARE / BOTTLE */
    {
        static const char* fxName[FX_COUNT] = { "BLINDED", "SLOWED", "DRAINING" };
        /* v2.4.36：**按真实行距算** ——
           HUD 用的是 hudTH + UIS(4) 当行距，不是 TextHeightWin()。
           我上一版按 TextHeightWin()*5 算，比真实高度小，
           所以 SLOWED 还是压在 BOOST 上（用户截图）。
           现在直接跟 BOOST 那一行对齐：
             BOOST 在 hudY3 + hudTH + UIS(4)，效果就从下一行开始。 */
        int fy = UIS(18) + (hudTH + UIS(4)) * 6 + UIS(6);   /* 6 行 HUD 之后的下一行 */
        for (int i = 0; i < FX_COUNT; i++) {
            float s = FxStrength(i);
            if (s <= 0.0f) continue;
            /* ============================================================
             * 修：**HUD 显示的是 FX 原始强度，不是玩家真实受到的效果**
             * ------------------------------------------------------------
             * FX_SLOW 的 strength 是"归一化到 0.45 系数"的中间量：
             *     game.c: slowMul = 1 - strength * 0.45
             * 所以给 20% 减速要写 strength = 0.444，
             * 而 HUD 直接把 0.444 打成 "SLOWED 44%" —— 玩家以为被减速 44%。
             * 现在按各效果的真实换算显示。
             * ============================================================ */
            float shown = s;
            if (i == FX_SLOW)  shown = s * 0.45f;    /* 实际减速百分比 */
            if (i == FX_DRAIN) shown = s * 1.5f;     /* 实际额外消耗 */
            sprintf(buf, "%s %d%%", fxName[i], (int)(shown * 100.0f + 0.5f));
            DrawTextWin(UIS(20), fy, buf, 0x00FF6060u);
            fy += hudTH + UIS(4);
        }
    }

}

static void DrawHudInventory(void) {
    /* v1.14：**右上角**滚动道具栏。
       原来放在左侧中部，和关卡主题名（如 ICE VAULT）撞在一起。
       现在右对齐贴右上角，纵向排列，滚轮切换时平滑滚动。
       加武器只改 ITEM_SLOT_COUNT + 两个数组。 */
    {
        /* v3.3 六个槽位的显示名：光球 / 投石 / 烟雾 / 胶水 / 回声 / 诱饵 */
    /* ==================================================================
     * 2026-11-06（用户实测：训练营里找不到相位信标）
     * ------------------------------------------------------------------
     * 真因：槽位从 6 扩到 8，但这两张名字表**只写了 6 项** ——
     * 剩下两项是 NULL，HUD 一画就是空，玩家永远看不到第 7/8 个物品。
     * 我在扔东西之前根本没想到名字表也要跟着扩。
     * ================================================================ */
    static const char* slotName[ITEM_SLOT_COUNT] = {
        "FLARE", "SLING", "SMOKE", "GLUE", "ECHO", "DECOY",
        "BEACON" };
    static const char* slotNameZh[ITEM_SLOT_COUNT] = {
        "光球", "投石", "烟雾", "胶水", "回声", "诱饵",
        "相位信标" };

        static float scroll = 0.0f;
        static double lastT = 0.0;

        double nowT = g_timeSec;
        float dtS = (float)(nowT - lastT);
        if (dtS <= 0.0f || dtS > 0.30f) dtS = 0.016f;
        lastT = nowT;
        {
            /* v1.17：只给"有货的槽位"排序号，空槽不参与滚动 */

                int cArr[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts,
                                             decoys, phaseBeacons };;
            int selVis = 0;
            for (int i = 0; i < ITEM_SLOT_COUNT; i++) {
                if (cArr[i] <= 0) continue;
                if (i == throwSelected) break;
                selVis++;
            }
            scroll += ((float)selVis - scroll) * (dtS * 15.0f);
        }

        int counts[ITEM_SLOT_COUNT];
        counts[0] = flares;
    counts[1] = bottles;
    counts[2] = smokeBombs;
    counts[3] = gluePots;
    counts[4] = echoBolts;
    { 
      counts[5] = decoys; counts[6] = phaseBeacons; }
    counts[5] = decoys;

        /* v3.3 **判据要覆盖全部 6 个槽位** ——
           试玩实测：「用过一次物品后，1 FLARE / 2 SLING 那一块再也不出现」。
           原来这里查的是旧的 8 个变量（bricks / acids / firePots / mirrors
           在投掷物重做后永远是 0），而**新加的 echoBolts / decoys 没算进来**。
           于是手上只剩回声弹或诱饵时，整条道具栏会消失。 */
    int anyItem = (flares > 0 || bottles > 0 || smokeBombs > 0 ||
                   gluePots > 0 || echoBolts > 0 || decoys > 0);
    int showList = (hudDetail >= 2) || (hudDetail >= 1 && anyItem);
        if (showList) {
            /* v2.6：**只显示三行** —— 上一件 / 当前 / 下一件。
               原来是有几种就铺几行，训练营里 8 种全有时铺满右半边屏幕，
               和最上面那行提示、右边的 WORD 全都压在一起。
               现在固定三行：中间是当前选中的（带高亮），上下各露出一部分。 */
            int rowH = TextHeightWin() + UIS(8);
            int margin = UIS(20);

            /* 收集"有货"的槽 */
            int vis[ITEM_SLOT_COUNT], nvis = 0, curVis = -1;
            for (int i = 0; i < ITEM_SLOT_COUNT; i++) {
                if (counts[i] <= 0) continue;
                if (i == throwSelected) curVis = nvis;
                vis[nvis++] = i;
            }
            if (nvis > 0) {
                if (curVis < 0) curVis = 0;
                /* 锚点：当前行固定在右下角，上面一行露在它上方 */
                int y0 = winH - UIS(120);

                for (int k = -1; k <= 1; k++) {
                    /* v3.0：**件数不够就不画上下行** ——
                       原来 nvis=2 时，上一行和下一行都循环取到同一件，
                       截图里 "1 FLARE x1" 出现两次。 */
                    if (nvis == 1 && k != 0) continue;
                    if (nvis == 2 && k != 0) {
                        /* 两件时：只画当前 + 另一件（各一行），不重复 */
                        if (curVis + k < 0 || curVis + k >= nvis) continue;
                    }
                    int idx = curVis + k;
                    while (idx < 0) idx += nvis;
                    while (idx >= nvis) idx -= nvis;

                    int i = vis[idx];
                    int y = y0 + k * rowH;
                    char line[48];
                    /* v3.3 **编号必须用真实槽位号，不能用压紧后的可见序号** ——
                       试玩实测：「用掉槽 1 的 FLARE 后，SLING 从槽 2 自动补到槽 1，
                       于是『按 1 选 Flare』永久失效」。
                       可见序号（idx）会随物品耗尽左移，而数字键绑的是真实槽位。 */
                    int slotNo = i + 1;
                    if (slotNo < ITEM_SLOT_COUNT) sprintf(line, "%d %s x%d", slotNo, Tr(slotName[i], slotNameZh[i]), counts[i]);
                    else             sprintf(line, "  %s x%d", Tr(slotName[i], slotNameZh[i]), counts[i]);

                    int tw = TextWidthWin(line);
                    int x = winW - margin - tw;
                    int selected = (k == 0);
                    if (!selected) {
                        /* 上下两行压暗，表示"还能切过去" */
                        DrawTextWin(x, y, line, 0x00606C6C);
                        continue;
                    }
                    BlendRectWin(x - UIS(6), y - UIS(3),
                                 x + tw + UIS(8), y + TextHeightWin() + UIS(1),
                                 0x00205030u, 0.55f);
                    BlendRectWin(x + tw + UIS(4), y - UIS(3),
                                 x + tw + UIS(7), y + TextHeightWin() + UIS(1),
                                 0x0060FF90u, 0.85f);
                    DrawTextWin(x, y, line, 0x00E8FFE0u);
                }
            }
        }
    }

}

static void DrawHudWorldHints(void) {
    char buf[96];
    /* ------------------------------------------------------------------ */
    /* 跳脸虫 QTE                                                        */
    /* ------------------------------------------------------------------ */
    /* ===== v2.4.91 **地上有钱袋 → 提示按 E 拾取** =====
       用户要求：「偷钱罐被打跑之后，地上掉金的时候要多添一行提示词，
       点 E 键拾取。」

       放在和"搜索"提示**同一个位置**（屏幕下方居中），
       所以玩家不需要学第二个位置；有搜索提示时让位给搜索。
       文字会呼吸（alpha 脉动），因为这是"现在就能做"的动作。 */
    {

        int bagAmt = MoneyBagNearestToPlayer();
        /* 2026-11-05：**拾取提示和藏身提示会画在同一行** ——
           实测截图里 "PRESS E - HIDE" 和 "PRESS E PICK UP 272 GOLD"
           叠成 "BRESE 272 HIDE"。
           藏身提示更紧急（关系到会不会被抓），所以它优先占这一行，
           钱袋提示让位。 */
        if (bagAmt > 0 && hoverKind == 0 && !(hideSpotNear >= 0 && hiddenIn < 0)) {
            char mb[80];
            sprintf(mb, Tr("PRESS %s  PICK UP  %d GOLD", "按 %s 拾取  %d 金币"),
                    VkName(g_bindings[ACT_INTERACT]), bagAmt);

            int tw2 = TextWidthWin(mb);
            int px3 = (winW - tw2) / 2;
            int py3 = winH - UIS(150);

            /* 呼吸：0.55 ~ 1.0 */
            float pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf((float)g_timeSec * 4.0f));

            /* 背板：让金色字在任何背景上都读得出来 */
            BlendRectWin(px3 - UIS(14), py3 - UIS(8),
                         px3 + tw2 + UIS(14), py3 + UIS(30),
                         0x00181008u, 0.72f);
            FrameRectWin(px3 - UIS(14), py3 - UIS(8),
                         px3 + tw2 + UIS(14), py3 + UIS(30),
                         0x00B08030u, 0.85f, 1);

            DrawTextWin(px3, py3, mb,
                        ((unsigned int)(0xC0 + (int)(0x3F * pulse)) << 16) |
                        ((unsigned int)(0x90 + (int)(0x40 * pulse)) << 8) |
                        0x20u);
        }
    }

    /* 词条横幅：只在进关后 5 秒出现。
       词条**名**已经常驻在 LEVEL 那一行右边了，所以这里只重复「说明」——
       玩家刚进关时需要知道这一层的规则具体是什么。 */
    if (levelBanner > 0.0f) {
        int y = UIS(204);   /* v2.6：左列固定带（状态效果之下） */
        if (levelMod >= 0) {
            DrawTextWin(UIS(20), y, modDesc[levelMod], 0x00C08050u);
        }
        DrawTextWin(UIS(20), y + TextHeightWin() + UIS(6), themeNames[(themeId >= 0 && themeId < THEME_COUNT) ? themeId : 0], 0x00B08040u);
    }

    if (showFps) {
        sprintf(buf, "FPS %d  %.1f/%.1fms  %dx%d", currentFps, lastRenderMs, frameMsMax, renderW, renderH);
        DrawTextWin(winW - UIS(20) - TextWidthWin(buf), UIS(18), buf, 0x00C8C8C8u);
    }

    /* v2.4.20：拾取提示**从这里移走** —— 见 DrawPickupToast()。
       原因：藏身遮罩是在 RenderFrame 末尾才画的，
       而 DrawHUD 早于它，所以躲进柜子时这条黄字会被 94% 黑遮罩盖掉
       （用户截图：「中间那条黄色的重要提示会丢失」）。
       现在它作为**最后一层**单独绘制，永远在最上面。 */

}

static void DrawHudSafeHints(void) {
    /* v2.5：站在安全区里 → 屏幕下方给一行提示（用户要求的那行字） */
    /* 2026-11-16（用户：躲进藏身点时提示字幕被覆盖、且多数没价值）：
       藏身状态下这两条提示（顶部"空格：开始校准"、左侧"T：商店 / 空格：校准"）
       会被藏身遮罩和小游戏面板盖住 —— 而且玩家人在垃圾桶里也点不了商店。
       所以藏身时直接不画；出柜子再照常显示。 */
    if (hiddenIn >= 0) return;
    if (playerInSafe && currentState == STATE_PLAYING) {
        const char* hint = NULL;
        unsigned int hcol = 0x00E8E060u;
        static char cool[96];
        if (calibActive) {
            hint = Tr("QWER: HIT NOTES", "QWER：跟随节律");
            hcol = 0x00FFFFFFu;
        } else if (calibCooldown > 0.0f) {
            sprintf(cool, "%s %.0fs", Tr("CALIB COOLING", "校准冷却"), calibCooldown);
            hint = cool;
            hcol = 0x00C08040u;
        } else {
            hint = Tr("SPACE: START CALIBRATION", "空格：开始校准");
        }
        /* v2.6：安全区里的操作提醒放到**左下角**（RUN 条下面）——
           原来挂在屏幕中间，直接压在校准条上。 */
        {
            /* ============================================================
             * 2026-11-05：**「站在门口却没进门」要明确告诉玩家**
             * ------------------------------------------------------------
             * 安全区半径 1.4 格，进口半径 1.05 格 —— 中间那一圈里
             * 屏幕上只有「T: 商店 空格: 校准」，玩家会以为自己已经到出口了。
             * 现在这一圈里直接写「走进光柱」。
             * ============================================================ */


            const char* shopHint = (breakerOpen && !PlayerAtExit())
                ? Tr("STEP INTO THE LIGHT", "走进光柱")
                : Tr("T: SHOP    SPACE: CALIBRATE", "T：商店    空格：校准");
            int sx = UIS(20);
            int sy = UIS(424);
            int sw = TextWidthWin(shopHint);
            BlendRectWin(sx - UIS(6), sy - UIS(3), sx + sw + UIS(6),
                         sy + TextHeightWin() + UIS(3), 0x000A0E14, 0.90f);
            DrawTextWin(sx, sy, shopHint, UI_ACCENT);
        }

        if (hint) {
            /* v2.5：挪到屏幕**最顶端居中** —— 原来在 3/4 高处会和武器行、
               道具栏、建议提示叠在一起。顶上先铺一条暗底防残影。 */
            int tw3 = TextWidthWin(hint);
            /* v2.6：限宽 —— 右侧还有道具栏，长句会横着压过去 */
            int maxW = winW - UIS(180) * 2;
            if (maxW < UIS(200)) maxW = winW - UIS(80);
            if (tw3 > maxW) { hint = Tr("SPACE: CALIBRATE", "空格：校准"); tw3 = TextWidthWin(hint); }
            int hx = (winW - tw3) / 2;
            /* v2.6：固定在**校准条正上方**那一条带里 */
            int hy = UIS(146) - TextHeightWin() - UIS(8);   /* v2.9：贴着条顶往上排 */
            BlendRectWin(hx - UIS(10), hy - UIS(4), hx + tw3 + UIS(10),
                         hy + TextHeightWin() + UIS(4), 0x000A0E14, 0.94f);
            DrawTextWin(hx, hy, hint, hcol);
        }
    }

    /* v2.5：站在安全屋里时把「回智封顶」写出来，否则玩家会以为安全屋坏了 */
    /* v2.6：这段原来画在 winH-60-2*rowGap-TH（≈310），正好压在武器行上。
       现在合并进 SAFE ZONE 那一行（262），不再单独占一条带。 */

    /* v3.0：**能量条已删除** —— 武器取消后这一整套都没意义了 */

    /* ===== v3.3：**旧的「开局四行说明」整块删除** =====
       用户要求：「旧的局内引导完全删掉，因为我们已经有引导模式了」。
       现在有 Tutorial 模式里的分步引导（TUTORIAL n/11，见 DrawTutorialPanel），
       覆盖了同样的内容而且更细；两套并存只会互相干扰
       （之前就出现过旧框的字从新面板后面透出来）。
       原来这块负责：
         · 第 1 层前 25 秒显示 4 行操作说明
         · 底部「PRESS SPACE TO DISMISS」可提前关闭
       全部移除。tutorialOff 变量还在 game.c 里（按键处理会置 1），
       但已经没有绘制点了，不影响编译。 */

}

static void DrawHudCoverAndThreat(void) {
    /* ===== v3.0 逃生向 HUD：藏身提示 + 被发现状态 =====
       怪已经打不死了，「它现在有没有在追我」是唯一需要盯的信息。 */
    {


        /* ① 藏身提示 */
        /* 2026-11-16（用户截图反馈）：藏身时这两行
           「按空格开始小游戏 (冷却 Ns)」「T - 打开商店」画在 winH-134，
           正好落在藏身遮罩的黑色区域里 —— 玩家完全看不见，属于无意义提示，删除。
           （藏身状态下按空格仍可开小游戏；安全屋界面里也有入口。） */
        if (hideSpotNear >= 0 && currentState == STATE_PLAYING &&
                   (hoverKind == 0 || hoverKind == 3 || hoverKind == 4)) {
            /* ===== v2.4.21 **坏掉的藏身点要报修复金额** =====
               用户反馈：柜子坏了之后只提示 "NOT ENOUGH GOLD"，
               根本不知道要多少钱 —— 等于让玩家盲赌。
               现在靠近就直接把金额和你的持有量一起报出来：
                 PRESS E - REPAIR 450   (YOU HAVE 60)      ← 钱够：正常色
                 PRESS E - REPAIR 450   (SHORT 390)        ← 钱不够：红色
            */



            if (hideSpotNear < HIDESPOT_MAX && hideSpots[hideSpotNear].broken) {
                   /* 它是 int，见 game.c */
                float cost = HIDESPOT_REPAIR_BASE;
                for (int rc = 0; rc < hideRepairCount; rc++) cost *= HIDESPOT_REPAIR_STEP;
                if (cost > 99999.0f) cost = 99999.0f;

                char rb[96];
                if (hideRepairCount >= HIDESPOT_REPAIR_MAX) {
                    snprintf(rb,sizeof(rb),"%s",Tr("BROKEN  -  TOO DAMAGED TO REPAIR",
                                   "柜子坏了  -  破得太狠，修不了"));
                    DrawTextWin(UIS(20), winH - UIS(214), rb, 0x00806060u);
                } else if ((float)carryGold >= cost) {
                    sprintf(rb, Tr("PRESS E  -  REPAIR  %d   (YOU HAVE %d)",
                                   "按 E 修理  %d 金币   （你有 %d）"),
                            (int)(cost + 0.5f), (int)(carryGold + 0.5f));
                    DrawTextWin(UIS(20), winH - UIS(214), rb, 0x0060FF90u);
                } else {
                    sprintf(rb, Tr("REPAIR COST  %d   (SHORT %d)",
                                   "修复需要  %d 金币   （还差 %d）"),
                            (int)(cost + 0.5f), (int)(cost - (float)carryGold + 0.5f));
                    DrawTextWin(UIS(20), winH - UIS(214), rb, 0x00FF6050u);
                }
            } else {
                /* 2026-11-05：**单独占一行**。
                   原来和「拾取金币」提示同在 winH-150 —— 实测叠成
                   "BRESE 272 HIDE"。靠条件让位不可靠（钱袋判定和藏身
                   判定不是同一帧状态），所以直接给它们各自的行。
                   底→上：150 钱袋 / 182 长按搜索 / 214 藏身-修理。 */
                DrawTextWin(UIS(20), winH - UIS(214),
                            Tr("PRESS E  -  HIDE", "按 E 躲进去"), 0x0090D0F0u);
            }
        }

        /* ② 敌人状态（三态雷达）—— 怪不可杀后这是唯一的威胁指标 */
        
            /* Encounter narration removed: threat is communicated by sound and the world. */


    /* ==================================================================
     * 2026-11-08（第 1 轮）：**扳手命中标记**
     * ------------------------------------------------------------------
     * 玩家原话：「扳手没啥效果，打不到」。命中确实有击退，但屏幕上
     * 没有任何确认 —— 分不清"打空了"和"打中了但怪没退"。
     * 这里在准星四周画四道短线，命中后 0.22 秒内淡出。
     * ================================================================== */
    /* 第 3 轮：**挥空**也要有反馈 —— 命中用亮色，挥空用暗色。
       原来打空时屏幕不动，玩家分不清"挥了没打中"和"没挥出去"。 */
    if (wrenchHitFlashT > 0.0f || wrenchSwingFxT > 0.0f) {

        int isHit = (wrenchHitFlashT > 0.0f);
        int cx3 = winW / 2, cy3 = winH / 2;
        int len = UIS(14), gap = UIS(6);
        float a = isHit ? (wrenchHitFlashT / 0.22f)
                        : (wrenchSwingFxT / (0.14f * 1.6f));
        if (a > 1.0f) a = 1.0f;
        if (a < 0.0f) a = 0.0f;
        /* 命中 = 亮橙黄；挥空 = 灰蓝（明显不同，不糊弄得过去） */
        unsigned int mc = isHit ? UI_ACCENT : 0x00708090u;
        /* This small HUD marker writes directly to the current UI pixel buffer. */
        {
            int pts[8][2];
            int k;
            pts[0][0] = cx3 - gap;              pts[0][1] = cy3;
            pts[1][0] = cx3 - gap - (int)(len * a); pts[1][1] = cy3;
            pts[2][0] = cx3 + gap;              pts[2][1] = cy3;
            pts[3][0] = cx3 + gap + (int)(len * a); pts[3][1] = cy3;
            pts[4][0] = cx3;                    pts[4][1] = cy3 - gap;
            pts[5][0] = cx3;                    pts[5][1] = cy3 - gap - (int)(len * a);
            pts[6][0] = cx3;                    pts[6][1] = cy3 + gap;
            pts[7][0] = cx3;                    pts[7][1] = cy3 + gap + (int)(len * a);
            for (k = 0; k < 8; k++) {
                int px = pts[k][0], py = pts[k][1];
                if (px < 0 || py < 0 || px >= renderW || py >= renderH) continue;
                pixels[(size_t)py * renderW + px] = mc;
            }
        }
    }

    /* ==================================================================
     * 2026-11-08（第 4 轮）：**躲在柜子里时的反馈**
     * ------------------------------------------------------------------
     * 玩家原话：「要怎么躲」。躲进去之后如果什么都没有，玩家无法判断
     * 是躲对了还是游戏卡了 —— 所以给两句话：
     *   附近有东西（<8 格）→ 「它在外面找你」
     *   确实安静（>12 格） → 「外面安静了」
     * 中间模糊区不显示，免得误导。
     * ================================================================ */
    /* 2026-11-16（用户截图反馈）：藏身时的「它在外面找你 / 它在附近 / 外面安静了」
       这一行画在 winH-112，正好落在藏身遮罩的黑区里 —— 玩家根本看不见，
       属于"被覆盖的无意义提示"，按要求直接删掉。 */
    if (hiddenIn >= 0) {
        /* 藏身时不再绘制任何会被遮罩吃掉的提示文字 */
    }

    /* 主题房间字幕同样会被藏身遮罩吃掉，藏身时不画 */
    /* Room identity and effects are composed by UiDrawRoomHint after the HUD. */
        
    }

}

static void DrawHudLightBar(const HudBarLayout* layout) {
    const int barW = layout->barW;
    const int barH = layout->barH;
    const int labelX = layout->labelX;
    const int barX = layout->barX;
    const int y1 = layout->y1;
    DrawTextWin(labelX, y1 - UIS(3), "LIGHT", 0x00A0A0A0u);
    /* 手电条颜色：开启=黄绿，关闭但有电=暗黄，无电=暗红 */
    unsigned int lightBarCol;
    if (!flashlightOn && battery <= 0.0f) lightBarCol = 0x00603030u;  /* 无电暗红 */
    else if (!flashlightOn) lightBarCol = 0x00806030u;  /* 关闭暗黄 */
    else lightBarCol = 0x00E8E060u;  /* 开启亮黄绿 */
    float batMax = (float)BatteryMax();
    /* v3.0：改用统一 UiBar */
    UiBar(barX, y1, barW, barH, battery / batMax, UI_BAR_NORMAL, lightBarCol);
    /* v3.0：**区分「没电」和「自己关了」** ——
       玩家实测：「LIGHT OFF 时电池条还有四五成，我分不清是没电还是我自己关了」。
       三态：DEAD（没电，暗红）/ OFF（关闭，暗黄）/ ON（开启，亮黄绿）。 */
    {
        const char* ls;
        unsigned int lc;
        if (battery <= 0.0f)      { ls = Tr("DEAD", "没电"); lc = 0x00FF4030u; }
        else if (!flashlightOn)   { ls = Tr("OFF",  "关闭"); lc = 0x00C09040u; }
        else                      { ls = Tr("ON",   "开启"); lc = 0x00E8E060u; }
        DrawTextWin(barX + barW + UIS(12), y1 - UIS(3), ls, lc);
    }

    /* v2.4：看图要花电 —— 开着图就在手电旁边挂一个持续的耗电提示，
       否则玩家只会觉得"电量掉得莫名其妙"。 */
    /* v3.0：**删掉这条重复提示** ——
       小地图自己的标题栏已经写着「MAP -10/S  HOLD TAB  M: FULL」，
       这里再挂一条既重复，又正好压在小地图上（用户截图确认）。 */

}

static void DrawHudPerception(void) {
    /* ===== v3.0 改造树感知系：预知 + 心眼 ===== */
    {



        if (currentState == STATE_PLAYING) {
            /* ① 预知：怪物进 8 格 → 屏幕边缘泛红（不需要看见它） */
            /* v6 每日限制：怪物不上小地图 —— 预知/心眼一起失效 */
            { 
              if (DailyForbidMonsterMap()) goto skip_monster_hints; }
            if (BenchPremonition() && Hunter0()->active) {
                float dx = Hunter0()->x - posX, dy = Hunter0()->y - posY;
                float d2 = dx * dx + dy * dy;
                if (d2 < 64.0f) {
                    /* 越近越红：边缘一圈渐变 */
                    float k = 1.0f - d2 / 64.0f;
                    if (k > 1.0f) k = 1.0f;
                    int band = UIS(10);   /* v3.0：40 → 10，原来每帧 160 次半透明叠加，画面抽搐 */
                    for (int i = 0; i < band; i++) {
                        float a = 0.55f * k * (1.0f - (float)i / (float)band);
                        BlendRectWin(0, i, winW, i + 1, 0x00FF0000u, a);
                        BlendRectWin(0, winH - 1 - i, winW, winH - i, 0x00FF0000u, a);
                        BlendRectWin(i, 0, i + 1, winH, 0x00FF0000u, a);
                        BlendRectWin(winW - 1 - i, 0, winW - i, winH, 0x00FF0000u, a);
                    }
                }
            }
            skip_monster_hints:
            /* ② 心眼：藏身时显示怪物距离 */
            if (BenchInnerEye() && hiddenIn >= 0 && Hunter0()->active) {
                float dx = Hunter0()->x - posX, dy = Hunter0()->y - posY;
                float dm = sqrtf(dx * dx + dy * dy);
                char ibuf[64];
                /* v3.0：挪到**顶部固定带**并加底色 ——
                   原来画在 winH/2+100，被场景/其他覆盖层吃掉，
                   验证报告实测像素差 0/2048（完全没画出来）。 */
                sprintf(ibuf, "%s %.1f", Tr("DIST", "距离"), (double)dm);
                {
                    int tw4 = TextWidthWin(ibuf);
                    int tx4 = (winW - tw4) / 2;
                    int ty4 = UIS(112);
                    BlendRectWin(tx4 - UIS(10), ty4 - UIS(6),
                                 tx4 + tw4 + UIS(10), ty4 + TextHeightWin() + UIS(6),
                                 0x00081018u, 0.88f);
                    DrawTextWin(tx4, ty4, ibuf, 0x0090D0F0u);
                }
            }
        }
    }

    /* v3.0：**噪音环** —— 扔瓶子/敲墙后显示声音强度，2.2 秒衰减 */
    {

        if (noiseDisplay > 0.01f && currentState == STATE_PLAYING) {
            int nr = UIS(26);   /* v3.0：固定半径，只让亮度随强度变 —— 原来半径也变，四角乱跳 */
            int ncx = winW / 2, ncy = winH / 2;
            unsigned int nc = (noiseDisplay > 0.7f) ? UI_DANGER
                            : (noiseDisplay > 0.4f) ? UI_WARN : UI_INFO;
            /* 四个弧段（简单画法：四角的小方框，避免画圆的开销） */
            for (int a = 0; a < 4; a++) {
                int ax = ncx + ((a & 1) ? nr : -nr) - UIS(3);
                int ay = ncy + ((a & 2) ? nr : -nr) - UIS(3);
                BlendRectWin(ax, ay, ax + UIS(6), ay + UIS(6), nc, 0.85f * noiseDisplay);
            }
        }
    }

}

static void DrawHudDamageHints(const HudBarLayout* layout) {
    const int barW = layout->barW;
    const int barH = layout->barH;
    const int labelX = layout->labelX;
    const int barX = layout->barX;
    const int rowGap = layout->rowGap;
    const int y1 = layout->y1;
    const int y2 = layout->y2;
    (void)barW; (void)barH; (void)labelX; (void)barX;
    (void)rowGap; (void)y1; (void)y2;
    DrawTextWin(labelX, y2 - UIS(3), "RUN", 0x00A0A0A0u);
    UiBar(barX, y2, barW, barH, stamina / StaminaMax(),
          (stamina < 25.0f) ? UI_BAR_WARN : UI_BAR_NORMAL, 0x0040C060u);
    /* v3.0：**武器行已删除** */

    /* v1.15：命中伤害数字（屏幕中央偏下，向上浮 + 淡出） */
    if (0 && lastHitTimer > 0.0f && lastHitDamage > 0) {
        char dbuf[32];
        sprintf(dbuf, "-%d", lastHitDamage);
        float a = lastHitTimer / 0.7f;
        if (a > 1.0f) a = 1.0f;
        int rise = (int)((1.0f - a) * UIS(26));
        int tw2 = TextWidthWin(dbuf);
        int dx2 = winW / 2 - tw2 / 2 + UIS(60);
        int dy2 = winH / 2 + UIS(40) - rise;
        unsigned int dc = lastHitBackstab ? 0x00FFE060u : 0x00F0F0F0u;
        int rr = (int)(((dc >> 16) & 0xFF) * a);
        int gg = (int)(((dc >> 8) & 0xFF) * a);
        int bb = (int)((dc & 0xFF) * a);
        DrawTextWin(dx2, dy2, dbuf, (unsigned int)((rr << 16) | (gg << 8) | bb));
        if (lastHitBackstab) {
            /* v3.0：SNEAK! 已删 —— 怪不可杀，偷袭没有意义 */
        }
    }

    /* v2.6：训练场右侧的「SPECIMEN DMG」绿色面板**已停用**（用户要求）——
       现在命中时屏幕上会直接跳伤害数字，这块面板重复且挡视线。
       代码保留，需要时把 #if 0 改成 #if 1 就能恢复。 */
#if 0
    /* v1.20：训练场右侧伤害面板（布局按文字高度算，杜绝重叠） */
    if (trainingMode) {
        int th = TextHeightWin();
        int pad = UIS(10);
        int pw = UIS(210);
        int ph = pad + th * 5 + UIS(28) + pad;     /* 标题 + 4 行 + 柱状图 + 上下留白 */
        int px2 = winW - pw - UIS(20);
        int py2 = winH / 2 - ph / 2;

        BlendRectWin(px2, py2, px2 + pw, py2 + ph, 0x00000810u, 0.92f);
        FrameRectWin(px2, py2, px2 + pw, py2 + ph, 0x0040C080u, 0.9f, UIS(2));

        char tb[48];
        int ty = py2 + pad;
        DrawTextWin(px2 + pad, ty, Tr("SPECIMEN DMG", "训练场伤害"), 0x0060FFB0u);
        ty += th;

        sprintf(tb, "TOTAL %d", trainingTotal);
        DrawTextWin(px2 + pad, ty, tb, 0x00FFFFFFu);
        ty += th;

        sprintf(tb, "LAST  %d", trainingLast);
        DrawTextWin(px2 + pad, ty, tb, 0x00FFE060u);
        ty += th;

        sprintf(tb, "HITS  %d", trainingCount);
        DrawTextWin(px2 + pad, ty, tb, 0x0090A0A0u);
        ty += th;

        sprintf(tb, "DPS   %d", trainingDps);
        DrawTextWin(px2 + pad, ty, tb, 0x0080D0FFu);
        ty += th + UIS(8);

        /* 柱状图：在 DPS 下方留出完整一块 */
        int barsH = UIS(20);
        int by = ty + barsH;
        for (int i = 0; i < 6; i++) {
            int h2 = trainingHist[i] * UIS(3);
            if (h2 > barsH) h2 = barsH;
            if (h2 <= 0) continue;
            BlendRectWin(px2 + pad + i * UIS(13), by - h2,
                         px2 + pad + i * UIS(13) + UIS(9), by, 0x0060FFB0u, 0.85f);
        }
    }
#endif
}

static void DrawHudInteractionHints(const HudBarLayout* layout) {
    const int barW = layout->barW;
    const int barH = layout->barH;
    const int labelX = layout->labelX;
    const int barX = layout->barX;
    const int rowGap = layout->rowGap;
    const int y1 = layout->y1;
    const int y2 = layout->y2;
    (void)barW; (void)barH; (void)labelX; (void)barX;
    (void)rowGap; (void)y1; (void)y2;
    /* v3.0：**武器行已删除** */

    /* ==================================================================
     * 2026-11-06（用户实测）：**被蛛网缠住时的屏内倒计时**
     * ------------------------------------------------------------------
     * 用户原话：「你要保证那个倒计时跟那个每次减 0.5 秒是同步的，
     *           不然我好不容易戳到只剩 1 秒钟，我还要再等就很烦」。
     *
     * 做法：屏幕上直接显示**同一个时间源**（ControlTimeLeft），
     * 每次戳下去数字立刻掉 0.5 —— 看到的和判定的永远是同一个值。
     * 同时显示"还要按/还要等"，让玩家知道是被卡住了还是在推进。
     * ================================================================ */
    {
        /* 原型在 control_effect.h，这里不要再写（会撞类型） */
        if (currentState == STATE_PLAYING && ControlActive() &&
            ControlCurrent() == CTRL_SPIDER_WEB) {
            float t = ControlTimeLeft();
            static char wbuf[64];
            int atFloor = (t <= SPIDER_WEB_MIN + 0.02f);
            if (atFloor)
                snprintf(wbuf, sizeof(wbuf), "%s  %.1fs",
                         (g_lang == LANG_ZH) ? "只能等" : "WAIT", (double)t);
            else
                snprintf(wbuf, sizeof(wbuf), "%s %s  %.1fs",
                         (g_lang == LANG_ZH) ? "连按" : "MASH",
                         VkName(g_bindings[ACT_DODGE]), (double)t);
            {
                int tw = TextWidthWin(wbuf);
                unsigned int col = atFloor ? 0x00A0A0A0u : 0x00FFE060u;
                /* 2026-11-06（用户实测）：**放到准星上方**。
                   原来在 winH/2 + 46，正好和底部的
                   「HOLD E SEARCH」/「PULL -> ...」撞在一起叠字。 */
                DrawTextWin((winW - tw) / 2, winH / 2 - UIS(64), wbuf, col);
            }
        }
    }

    /* 交互提示 + 长按进度 */
    int roomDevice=(hoverKind==4 && RoomThemeIsCurrent(hoverIndex)) ||
        (hoverKind==3 && hoverIndex>=0 && hoverIndex<roomPropCount && RoomPropIsControl(roomProps[hoverIndex].kind));
    if (hoverKind != 0 && !roomDevice) {
        char pbuf[192];   /* 2026-11-06：舱门门槛文案最长（含数字）要 96 字节，64 会溢出 */
        /* v2.7：提示要用当前绑定，改过键之后写死 E 就是骗人 */
        if (hoverKind == 1)
            sprintf(pbuf, "HOLD %s  SEARCH", VkName(g_bindings[ACT_INTERACT]));
        else if (hoverKind == 4) {
            /* AD-46：舱门提示**独占一行**，不和时间/搜索提示抢坐标 */
            char why[96];
            if (HatchGateCheck(hoverIndex, why, (int)sizeof(why)))
                sprintf(pbuf, "HOLD %s  %s", VkName(g_bindings[ACT_INTERACT]), HatchTitle(hoverIndex));
            else
                sprintf(pbuf, "%s  -  %s", HatchTitle(hoverIndex), why);
        }
        else if (hoverKind == 3 && hoverIndex>=0 && hoverIndex<roomPropCount &&
                 RoomPropIsControl(roomProps[hoverIndex].kind)) {
            snprintf(pbuf,sizeof(pbuf),"%s  %s",VkName(g_bindings[ACT_INTERACT]),RoomThemeFixtureLine(hoverIndex));
        }
        else if (hoverKind == 3) {
            /* 2026-11-06：家具可以搜（货架 3 次 / 木箱堆 2 次 / 控制台 2 次）。
               提示里带上还剩几次 —— 否则玩家不知道"还能不能翻"。 */

            int left = 0;
            if (hoverIndex >= 0 && hoverIndex < MAX_ROOM_PROPS)
                left = roomProps[hoverIndex].searchLeft;
            const char* action=(hoverIndex>=0 && hoverIndex<roomPropCount && roomProps[hoverIndex].kind>=ROOMPROP_THEME_FIRST)?Tr("INSPECT DEVICE","检修装置"):Tr("SEARCH","搜查");
            snprintf(pbuf,sizeof(pbuf),Tr("HOLD %s  %s  (%d LEFT)","按住%s  %s（剩%d次）"),VkName(g_bindings[ACT_INTERACT]),action,left);
        }
        else if (fusesHeld >= fusesNeeded)
            sprintf(pbuf, "HOLD %s  POWER ON", VkName(g_bindings[ACT_INTERACT]));
        else sprintf(pbuf, "NEED %d MORE FUSE", fusesNeeded - fusesHeld);

        /* 2026-11-06（用户实测）：舱门的**后果说明**再画一行。
           第一次放在 +UIS(26)，正好压在长按进度条上（截图里叠成一条）。
           进度条占 py+30..py+38，所以这里放到 +UIS(46)，
           顺序读起来也顺：标题 -> 进度条 -> 后果。 */
        if (hoverKind == 4) {

            const char* eff = HatchEffectLine(hoverIndex);
            if (eff && eff[0]) {
                char line[320];snprintf(line,sizeof(line),"%s",eff);
                ClampRowText(line,winW-UIS(24));
                int tw2=TextWidthWin(line);
                DrawTextWin((winW-tw2)/2,winH-UIS(214)+UIS(46),line,0x00E0B050u);
            }
        }

        ClampRowText(pbuf,winW-UIS(24));
        int tw = TextWidthWin(pbuf);
        int px = (winW - tw) / 2;
        /* 2026-11-05：原来和「拾取 / 藏身」提示同在 winH-150，会叠字。
           往上挪一行，进度条跟着走。 */
        int py = winH - UIS(hoverKind == 4 ? 214 : 182);
        DrawTextWin(px, py, pbuf, (hoverKind == 2 && fusesHeld < fusesNeeded) ? 0x00C05050u : 0x00E8E8E8u);

        int barW = UIS(240), barH = UIS(8);
        int bx = (winW - barW) / 2;
        int by = py + UIS(30);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00202020, 0.8f);
        if (holdingNow) {
            float f = holdTimer / SEARCH_TIME;
            if (f > 1.0f) f = 1.0f;
            BlendRectWin(bx, by, bx + (int)(barW * f), by + barH, 0x00C8E8C8, 0.95f);
        }
        FrameRectWin(bx, by, bx + barW, by + barH, 0x00808080, 0.7f, 1);
    }

    /* v2.6：SAFE ZONE 原来单独占一条带（winH/2+40），会和主题名/武器行抢位置。
       现在并进 FUSES 那一行（见上方的 FUSES 绘制），不再单独画。 */
}

static void DrawHudChargeHints(const HudBarLayout* layout) {
    const int barW = layout->barW;
    const int barH = layout->barH;
    const int labelX = layout->labelX;
    const int barX = layout->barX;
    const int rowGap = layout->rowGap;
    const int y1 = layout->y1;
    const int y2 = layout->y2;
    (void)barW; (void)barH; (void)labelX; (void)barX;
    (void)rowGap; (void)y1; (void)y2;
    /* v3.0：**武器行已删除** */

    /* 蓄力投掷进度条：按住鼠标左键时显示 */
    int mouseFree = (currentState != STATE_PLAYING) || (isMapOpen && mapFullScreen);
    /* 只有选中道具有库存时才显示蓄力条 */
    int hasItem = itemHasStock();   /* v1.18：按当前槽位判断，五种道具都能蓄力 */
    if (isCharging && mouseCharge > 0.0f && !mouseFree && hasItem) {
        int barW = UIS(280), barH = UIS(14);
        int bx = (winW - barW) / 2;
        /* v2.9：上移到 winH/2+48 —— 原来 +100 让百分比落在 420..451，
           正好压住 y=425..456 的武器行（报告 严重 #5）。 */
        int by = winH / 2 + UIS(48);
        /* 背景：深色半透明，带明显边框 */
        BlendRectWin(bx - 2, by - 2, bx + barW + 2, by + barH + 2, 0x00000000, 0.9f);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00101020, 0.95f);
        /* 填充：蓝→青→红渐变感 */
        unsigned int chargeCol;
        if (mouseCharge < 0.33f) chargeCol = 0x0040E0FFu;
        else if (mouseCharge < 0.66f) chargeCol = 0x0040FF80u;
        else if (mouseCharge < 1.0f) chargeCol = 0x00E0FF40u;
        else chargeCol = 0x00FF4040u;  /* 满蓄力红色警告 */
        BlendRectWin(bx, by, bx + (int)(barW * mouseCharge), by + barH, chargeCol, 1.0f);
        /* 发光边框 */
        FrameRectWin(bx, by, bx + barW, by + barH, chargeCol, 1.0f, 2);
        /* 标签带阴影 */
        /* v3.3 **标签要跟着真实行为走** ——
           试玩实测：「Help 写 THROW: RMB，屏幕上却显示 CHARGING KNOCK，
           松手后背包里的 FLARE 确实被消耗了」。玩家无法判断这一下是扔东西还是敲击。
           现在：手上选了投掷物 → CHARGING THROW；
                 没选（或该槽空）→ CHARGING KNOCK（真的只是敲击）。 */
        {


            int cnt[6] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys };
            int hasItem = (throwSelected >= 0 && throwSelected < 6 && cnt[throwSelected] > 0);
            DrawTextCenterWin(winW / 2, by - UIS(24),
                              hasItem ? Tr("CHARGING THROW", "投掷蓄力")
                                      : Tr("CHARGING KNOCK", "敲击蓄力"),
                              chargeCol);
        }
        /* 百分比数值 */
        char pct[16];
        sprintf(pct, "%d%%", (int)(mouseCharge * 100));
        DrawTextCenterWin(winW / 2, by + barH + UIS(6), pct, 0x00FFFFFFu);
    }

    /* v3.0：**大招提示** —— 拿到之后在 HUD 上告诉玩家「按 Q」 */
    {


        if (runUltimate >= 0 && currentState == STATE_PLAYING) {
            const char* nm = (runUltimate < INRUN_COUNT)
                ? inRunDefs[runUltimate].name : "?";
            char ub[64];
            if (!ultUsed) sprintf(ub, "Q: %s", nm);
            else          sprintf(ub, "%s USED", nm);
            int uw = TextWidthWin(ub);
            DrawTextWin(winW - UIS(24) - uw, y1 - UIS(3), ub,
                        ultUsed ? 0x00606060u : UI_ACCENT);
        }
    }
}

static void DrawHudCalibrationHints(const HudBarLayout* layout) {
    const int barW = layout->barW;
    const int barH = layout->barH;
    const int labelX = layout->labelX;
    const int barX = layout->barX;
    const int rowGap = layout->rowGap;
    const int y1 = layout->y1;
    const int y2 = layout->y2;
    (void)barW; (void)barH; (void)labelX; (void)barX;
    (void)rowGap; (void)y1; (void)y2;
    /* v3.0：**武器行已删除** */

    /* v3.0：**校准倒计时常驻显示** ——
       用户两次反馈「倒计时没有加进去」。
       之前只在 hiddenIn >= 0（藏进藏身点）时才画，玩家在走廊里
       根本看不到还剩多久。
       现在只要在游戏中且冷却中，就显示在 RUN 条下方一行。
       2026-11-16（用户：藏身时提示被遮罩盖掉）：这一行在左下角，
       藏身时正好落在遮罩黑区里 —— 画了也看不见，直接不画；
       藏身期间真正要用的提示改画在"缝隙"里（见 RenderFrameCalibration）。 */
    if (currentState == STATE_PLAYING && hiddenIn < 0) {

        char cd[48];
        unsigned int cdc;
        if (calibActive) {
            snprintf(cd,sizeof(cd),"%s",Tr("CALIBRATING", "校准中"));
            cdc = UI_ACCENT;
        } else if (calibCooldown > 0.0f) {
            sprintf(cd, Tr("CALIB %ds", "校准冷却 %d 秒"),
                    (int)(calibCooldown + 0.5f));
            cdc = 0x00909090u;
        } else {
            /* v3.3 **只有真的能用时才写 READY** ——
               试玩实测：「HUD 底部常驻 CALIB READY，我按空格两次什么都没发生。
               这行字在提示一个当前不存在的操作。」
               校准只在安全屋或藏身点里能开（见 game.c 的 inSafe || hiddenIn >= 0）。 */

            if (playerInSafe || hiddenIn >= 0) {
                snprintf(cd,sizeof(cd),"%s",Tr("CALIB READY", "校准就绪"));
                cdc = 0x0060FF90u;
            } else {
                snprintf(cd,sizeof(cd),"%s",Tr("CALIB - HIDE TO USE", "校准 - 需躲进藏身点"));
                cdc = 0x00505050u;   /* 压暗，不抢视线 */
            }
        }
        DrawTextWin(labelX, y2 + rowGap - UIS(6), cd, cdc);
        /* ===== v3.3：两个操作提示并到这一行右侧 =====
           用户反馈「这两个都在屏幕中间太遮挡了」。
           现在跟在 CALIB 后面，同一条基线、**无边框、暗色小字**，
           不占额外空间也不抢视线：
             [B] 引导    训练营里引导没展开时才有
             [C] 激活诱饵  场上有未激活的玩偶时才有 */
        {
            int px2 = labelX + TextWidthWin(cd) + UIS(20);
            int py2 = y2 + rowGap - UIS(6);




            if (trainingMode && !tutorialGuideOn) {
                const char* gtxt = Tr("[B] GUIDED FLOOR", "[B] 进入引导层");
                DrawTextWin(px2, py2, gtxt, 0x004A9480u);
                px2 += TextWidthWin(gtxt) + UIS(18);
            }
            {
                int unarmed = 0;
                for (int di = 0; di < MAX_DECOY; di++)
                    if (decoysArr[di].active && !decoysArr[di].armed) { unarmed = 1; break; }
                if (unarmed)
                    DrawTextWin(px2, py2, Tr("[C] ARM DECOY", "[C] 激活诱饵"), 0x00A8842Cu);
            }
        }

    }

    /* v3.0：**摇电只留一条黄线，不写任何文字** ——
       用户要求「改成之前底下有黄线的那一版，然后不带其他文字」。
       LIGHT 标签和 ON/OFF 状态照常显示，只在手电条下面压一条 4px 的
       黄色进度线。位置取 y1 与 y2（RUN 行）之间的空隙，不会碰到任何东西。 */
    if (cranking) {
        float cc = crankTimer / 2.4f;
        if (cc > 1.0f) cc = 1.0f;
        int lineY = y1 + barH + UIS(2);
        int lineH = UIS(4);
        /* 空隙不够就往上贴，绝不压到 RUN 行 */
        if (lineY + lineH > y2 - UIS(2)) {
            lineY = y2 - UIS(2) - lineH;
            if (lineY < y1 + barH) lineY = y1 + barH;
        }
        unsigned int ccol = (cc >= 1.0f) ? 0x0060FF90u : UI_ACCENT;
        BlendRectWin(barX, lineY, barX + barW, lineY + lineH, 0x00202020u, 0.80f);
        BlendRectWin(barX, lineY, barX + (int)(barW * cc), lineY + lineH, ccol, 1.0f);
    }

    /* 金币（GOLD）是大厅货币，游戏过程中用不到 —— 不放进 HUD。
       「M CLOSE MAP」也删掉了：地图标题上已经写着
       「M: FULL  M: CLOSE」，再在底部挂一行是重复的。 */
}

static void DrawHudActionHints(const HudBarLayout* layout) {
    DrawHudDamageHints(layout);
    DrawHudInteractionHints(layout);
    DrawHudChargeHints(layout);
    DrawHudCalibrationHints(layout);
}

void DrawHUD(void) {
    /* 锁盘是模态层：解锁时整层 HUD 让位，避免“藏好了 / 空格小游戏 /
       T 商店”这些提示从锁盘边缘透出来，和面板文字挤在一起。 */
    if (lockActive) return;
    /* Keep this order: later units intentionally overlay earlier drawing. */
    if (currentState == STATE_PLAYING && !isMapOpen) DrawCrosshair();
    int effDetail = keys[VK_TAB] ? 2 : hudDetail;
    DrawHudRunHeader(effDetail);
    DrawHudVitalsAndStatus(effDetail);
    DrawHudInventory();
    DrawHudWorldHints();
    HudBarLayout bars = HudBuildBarLayout();
    DrawHudSafeHints();
    DrawHudCoverAndThreat();
    DrawHudLightBar(&bars);
    DrawHudPerception();
    DrawHudActionHints(&bars);
}

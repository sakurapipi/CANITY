/* src/domain/ui/ui_menu.c —— 主菜单页（含改造树 / 银行页 / 校准条）
 *
 * 第 2 轮（2026-11-08）从 ui.c 原样搬过来，逻辑一行没改。
 * 契约：include/ui_menu.h
 */
#include "common.h"
#include "entity.h"
#include "run_stats.h"
#include "render.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "lang.h"
#include "ui.h"
#include "ui_text.h"
#include "ui_pages.h"    /* ClampRowText */
#include "ui_menu.h"
#include "ui_theme.h"
#include "art_direction.h"
#include "achievements.h"
#include "skills.h"
#include "bench.h"
#include "shop.h"
#include "upgrade_run.h"
#include "bestiary.h"
#include "sprites.h"
#include "economy.h"
#include "threat.h"
#include "save.h"
#include "daily.h"
#include "campaign.h"
#include "state.h"
#include "calibration.h"

extern int safehouseHoverRow;   /* 定义在 game.c */
#include "wallet.h"
#include "help_book.h"

/* 菜单内部互相引用，原文件里靠"先定义后使用"的巧合；这里显式前置声明 */
static int mainMenuMore=0;
static int MainMenuRowCount(void);
static const char* MenuLabel(int i);
static const char* MainMenuVisibleLabel(int index);
static void DrawMenuText(void);
static void DrawBestiaryPage(MenuGeo g);
static void DrawBenchTree(MenuGeo g);
static void DrawBankPage(void);
static void DrawSafehousePage(void);
static void DrawTextInRowLeft(const char* text, RECT row, unsigned int color);
static void DrawCalibStrip(MenuGeo g);

/* 分支名表：原来在 ui.c 里是 static，菜单搬出来后自己留一份。
   只是显示名，重复定义比为了它开一个新接口更划算。 */
static const char* kBranchName[7] = { "SILENCE", "ECONOMY", "CONCEAL",
                                      "SHAPE", "RISK", "CROSS", "ORIGIN" };
static const char* kBranchNameZh[7] = { "沉寂", "节流", "藏匿",
                                        "塑形", "冒险", "跨支", "起点" };

void MainMenuCompute(MainMenuLayout* L) {
    memset(L,0,sizeof(*L));int th=TextHeightWin();if(th<8)th=8;
    L->count=MainMenuRowCount();L->padX=UIS(30);
    L->panel.left=UIS(30);L->panel.right=winW*3/5;
    L->panel.top=UIS(25);L->panel.bottom=winH-UIS(25);
    L->titleY=UIS(55);L->subY=L->titleY+th+UIS(16);
    L->firstY=winH/4;L->rowH=(winH*3/5)/L->count;
    if(L->rowH<th+UIS(5))L->rowH=th+UIS(5);
    L->footRuleY=winH-UIS(78);L->footY=winH-UIS(56);
}


static int MainMenuRowCount(void) {
    return MainMenuCount();
}

int MainMenuCount(void) {return mainMenuMore?(EndlessUnlocked()?7:6):(hasSave?8:7);}


static int MenuItemIndex(void) {
    if (currentState == STATE_SETTINGS) return settingsSelection;
    if (currentState == STATE_HUB) return hubSelection;
    if (currentState == STATE_SKILL_PICK) return skillPickSel;
    /* 2026-11-08：选武器流程已删（武器改扳手） */
    if (currentState == STATE_SAFEHOUSE) return shopRow;
    if (currentState == STATE_ACHIEVEMENTS) return achSelection;
    if (currentState == STATE_BESTIARY) return bestiarySel;
    return menuSelection;
}

static const char* MenuLabel(int i) {
    static char buf[128];
    switch (currentState) {
        case STATE_MAIN_MENU: return MainMenuVisibleLabel(i);
        case STATE_PAUSED: {
            static const char* s[] = {"Resume", "Settings", "Main Menu"};
            static const char* szh[] = {"继续游戏", "设置", "主菜单"};
            return (g_lang == LANG_ZH) ? szh[i] : s[i];
        }
        case STATE_BESTIARY: {
            static char bb[192];
            if (i >= BESTIARY_COUNT) return "Back";
            if (!BestiaryIsSeen(i)) {
                sprintf(bb, "???   [LOCKED]");
            } else {
                sprintf(bb, "%s  [%s]", (g_lang == LANG_ZH) ? bestiary[i].nameZh : bestiary[i].nameEn,
                                    (g_lang == LANG_ZH) ? bestiary[i].tagZh : bestiary[i].tagEn);
            }
            return bb;
        }
        case STATE_HELP: {
            /* 只放玩法提示，不再列键位 —— 键位可在 Settings 里改绑，
               静态表一改绑就过时；真正的键位看 Settings → Movement / Actions / Menu Keys。
               行数要控制在 14 行以内：面板高度 = 14×28 + 标题 + 内边距
               ≈ 500，在 600 高的窗口里才留得下边距。 */
            /* v1.8：前 10 行是按键说明书，直接读当前绑定 ——
               新玩家最需要的就是"我该按哪个键"，而且改键之后这里会跟着变。 */
            static char kb[128];
            int ridx = i + (helpPage == 1 ? HELP_PAGE_ROWS : 0);   /* v2.2：分页 */
            if (ridx >= 0 && ridx < HELP_KEY_ROWS) {
                switch (ridx) {
                case 0: sprintf(kb, "%s:  %s %s %s %s", Tr("MOVE","移动"),
                                VkName(g_bindings[ACT_MOVE_FWD]), VkName(g_bindings[ACT_MOVE_LEFT]),
                                VkName(g_bindings[ACT_MOVE_BACK]), VkName(g_bindings[ACT_MOVE_RIGHT])); break;
                case 1: sprintf(kb, "%s: %s  (NOISE)", Tr("SPRINT","冲刺"), VkName(g_bindings[ACT_SPRINT])); break;
                case 2: sprintf(kb, "%s: HOLD %s  (NOISE)", Tr("SEARCH","翻找"), VkName(g_bindings[ACT_INTERACT])); break;
                case 3: sprintf(kb, "%s: HOLD %s  (+32)", Tr("CRANK","摇电"), VkName(g_bindings[ACT_CRANK])); break;
                case 4: sprintf(kb, "%s: %s", Tr("LIGHT","手电"), VkName(g_bindings[ACT_FLASHLIGHT])); break;
                case 5: sprintf(kb, "%s: %s (FULL)  TAB (MINI)", Tr("MAP","地图"), VkName(g_bindings[ACT_MAP])); break;
                case 6: sprintf(kb, "%s: %s", Tr("THROW","投掷"), VkName(g_bindings[ACT_THROW])); break;
                case 7: sprintf(kb, "%s: %s %s %s  (WHEEL: ALL)", Tr("SLOT","选槽"),
                                VkName(g_bindings[ACT_SELECT_1]), VkName(g_bindings[ACT_SELECT_2]),
                                VkName(g_bindings[ACT_SELECT_3])); break;
                case 8: sprintf(kb, "%s: %s  (KNOCK: NOISE)", Tr("KNOCK","敲击"),
                                VkName(g_bindings[ACT_CANCEL_CHARGE])); break;
                default: sprintf(kb, "%s: %s  DODGE: %s", Tr("PAUSE","暂停"),
                                VkName(g_bindings[ACT_PAUSE]), VkName(g_bindings[ACT_DODGE])); break;
                }
                {
                    MenuGeo hg = GetMenuGeo();
                    ClampRowText(kb, (hg.panel.right - hg.panel.left) - UIS(44));
                }
                return kb;
            }
            static const char* helps[] = {
                "SEARCH CONTAINERS FOR FUSES,",   /* v2.9：拆两行，原来后半句被截掉 */
    "THEN POWER THE BREAKER.",
                "",
                "RUNNING AND SEARCHING MAKE NOISE.",
                "WALKING IS QUIET, BUT 5S OF IT STARTS TO WHISPER.",
                "",
                "BOTTLE: THROW IT AT A MONSTER TO HIT IT.",
                "  THROW A BOTTLE TO MAKE NOISE AND LURE IT AWAY.",
                "  GIVES AWAY YOUR POSITION.",
                "",
                "FACE JUMPER: PRESS SPACE TO DODGE IT.",
        "SLING: THROW IT AT A LOCKED CRATE TO SHATTER THE LOCK (LOUD).",
                "  IF IT LANDS ON YOUR FACE, PRESS THE KEY.",
                "  MISS BOTH AND YOU LOSE SANITY."
            };
            const int helpRows = (int)(sizeof(helps) / sizeof(helps[0]));
            {
                int j = i - HELP_KEY_ROWS;
                if (j >= 0 && j < helpRows) {
                    static char hb2[192];
                    sprintf(hb2, "%s", helps[j]);
                    MenuGeo hg = GetMenuGeo();
                    ClampRowText(hb2, (hg.panel.right - hg.panel.left) - UIS(44));
                    return hb2;
                }
            }
            return "";
        }
        case STATE_SAFEHOUSE: {
            return ShopRowText(i);
        }
        case STATE_WEAPON_PICK: {
            static char wb[128];
            static const char* resName[WRES_COUNT] = { "STAMINA", "BATTERY", "SANITY", "ENERGY" };   /* v2.3：三把武器都烧能量 */
            if (i < 0 || i >= WPN_COUNT) return "";
            /* 2026-11-08（武器残留清理 A-1）：**不再打印 LV%d** ——
               武器早就改造成扳手，等级/经验的唯一增长入口 WeaponAddXp
               零调用，所以这里永远显示 LV0（假信息）。
               等级体系已废弃；状态机清理见下一轮。 */
            sprintf(wb, "%s   %s -%.0f", Tr(weaponDefs[i].dispName, weaponDefs[i].nameZh),
                    resName[weaponDefs[i].resource], weaponDefs[i].cost);
            (void)weaponRank;
            return wb;
        }
        case STATE_SKILL_PICK: {
            static char pb[128];
            if (i < 0 || i >= INRUN_CHOICES || skillPickOptions[i] < 0) return "";
            int id = skillPickOptions[i];
            if (pickMode == 1) {
                const RelicDef* rd = &relicDefs[id];
                sprintf(pb, "%s: %s", Tr(rd->nameEn, rd->nameZh),
                        Tr(rd->descEn, rd->descZh));
            } else {
                /* v2.9：三选一用中文（原来 24 条技能整页英文） */
        sprintf(pb, "%s: %s", Tr(inRunDefs[id].name, inRunDefs[id].nameZh),
                Tr(inRunDefs[id].desc, inRunDefs[id].descZh));
            }
            {
                MenuGeo g = GetMenuGeo();
                ClampRowText(pb, (g.panel.right - g.panel.left) - UIS(56));
            }
            return pb;
        }
        case STATE_BANK: return "BANK";   /* v6：银行页自己画，不行表 */
        case STATE_HUB: {
            static char hb[128];
            {
                /* v1.8 技能树页 */
                if (i >= SKILL_COUNT) return "Back";
                const SkillDef* d = &skillDefs[i];
                int r = skillRank[i];
                int cost = SkillCost(i);
                if (r >= d->maxRank) {
                    sprintf(hb, "%s   MAX", d->name);
                } else if (d->isKeystone) {
                    int pts = (d->branch < 3) ? SkillBranchPoints(d->branch) : 0;
                    if (r > 0) {
                        sprintf(hb, "*%s  OWNED", d->name);
                    } else if (activeKeystone >= 0) {
                        sprintf(hb, "*%s  LOCKED (OTHER ULT)", d->name);
                    } else if (d->branch < 3 && pts < SKILL_POINTS_FOR_KEYSTONE) {
                        sprintf(hb, "*%s  ULT %c %d/%d", d->name,
                                'A' + d->branch, pts, SKILL_POINTS_FOR_KEYSTONE);
                    } else {
                        sprintf(hb, "*%s  ULT %d G %dC", d->name, cost, d->coreCost);
                    }
                } else if (cost < 0) {
                    sprintf(hb, "[%s]  %s  MAX",
                            Tr(kBranchName[d->branch], kBranchNameZh[d->branch]), d->name);
                } else {
                        sprintf(hb, "[%s]  %s  %d/%d  %d G",
                                Tr(kBranchName[d->branch], kBranchNameZh[d->branch]),
                                d->name, r, d->maxRank, cost);   /* v7: 加分支标签 */
                }
                {
                    MenuGeo g = GetMenuGeo();
                    ClampRowText(hb, (g.panel.right - g.panel.left) - UIS(56));
                }
                return hb;
            }
            /* v3.4：**新增一行「携带档位」** —— 银行博弈的核心决策。
               用户要的「带进去越多，拿出来的越多」就靠这一行。
               行序：0..5 六项永久升级 / 6 携带档位 / 7 Back */
            if (i == UPGRADE_COUNT) {

                static const int pct[4] = { 25, 50, 75, 100 };
                int p = bankCarryPct;
                if (p < 0) p = 0;
                if (p > 3) p = 3;
                static char cb[96];
                snprintf(cb, sizeof(cb),
                         Tr("CARRY IN   %d%%  (%d / %d GOLD)",
                            "携带  %d%%  (%d / %d 金币)"),
                         pct[p],
                         (int)((float)bankGold * (float)pct[p] / 100.0f + 0.5f),
                         bankGold);
                return cb;
            }
            if (i >= UPGRADE_COUNT + 1) return "Back";
            int cost = UpgradeCost(i);
            if (cost < 0) sprintf(hb, "%s   LV%d/%d   MAX", UpgradeName(i), UpgradeLevel(i), UPG_MAX);
            else sprintf(hb, "%s   LV%d/%d   %d GOLD", UpgradeName(i), UpgradeLevel(i), UPG_MAX, cost);
            return hb;
        }
        case STATE_GAMEOVER: return "Return to Menu";
        case STATE_ACHIEVEMENTS: {
            /* v6：**按分类筛选 + 用表里的真实奖励**
               原来这里写着 "+1C/250S" —— 那是已删除的「核心 / 金币」，
               玩家看到的是两个不存在的货币。现在从 kAchDefs 读。 */

            if (i >= AchVisibleCount()) return "Back";
            int id = AchIdAtRow(i);
            if (id < 0) return "Back";
            static char ab[256];
            const AchDef* d = &kAchDefs[id];
            if (g_achievements[id].unlocked) {
                sprintf(ab, "[*] %s  +%d GOLD", g_achievements[id].name, d->reward);
            } else if (d->hidden) {
                sprintf(ab, "[ ] ???");
            } else {
                sprintf(ab, "[ ] %s  %d/%d  +%d",
                        g_achievements[id].name,
                        g_achievements[id].progress,
                        g_achievements[id].target,
                        d->reward);
            }
            return ab;
        }
        case STATE_SETTINGS: {
            if (settingsPage == SETPAGE_ROOT) {
                if (i < 3) return rootCategoryNames[i];
                return "Back";
            }
            if (settingsPage == SETPAGE_BINDDIR) {
                if (i < 3) return bindCategoryNames[i];
                return "Back";
            }
            int idx[SETTING_COUNT];
            int n = PageItems(idx);
            if (i >= n) return "Back";
            const SettingItem* it = &settings[idx[i]];
            if (IsKeyBindItem(it)) {
                return it->name;  /* 键位行：名称左对齐，键名右对齐（DrawMenuText 里画） */
            }
            if (it->kind == SET_SLIDER) {
                if (it->fvalue == &gammaValue) sprintf(buf, "%s: %.2f", it->name, *it->fvalue);
                else if (it->fvalue == &volumeF || it->fvalue == &musicVolumeF)
                    sprintf(buf, "%s: %.0f%%", it->name, *it->fvalue * 100.0f);
                else sprintf(buf, "%s: %.1fx", it->name, *it->fvalue);
            } else {
                int dispVal = *it->value;
                if (dispVal >= 0 && dispVal < it->count) {
                    /* v2.9：选项值也走翻译（原来 LOW/MEDIUM/HIGH 全英文） */
                    sprintf(buf, "%s: %s", it->name,
                            Tr(it->options[dispVal], it->options[dispVal]));
                } else {
                    sprintf(buf, "%s: %s", it->name,
                            Tr(it->options[0], it->options[0]));
                }
            }
            return buf;
        }
        default: return "";
    }
}

void PageTabRect(int idx, int* x0, int* y0, int* x1, int* y1) {
    /* v3.0：页签改成**左侧竖排**。
       调研结论：横向 5 个页签在窄面板里必然挤（要么截字、要么压余额），
       商店类 UI 的通用解法是竖排 sidebar —— 宽度压力归零，
       页签再多也只是往下排，不需要改布局。

       布局：
          面板左边 26% 宽的一列放 5 个页签
          右边 74% 是内容区 */
    MenuGeo pg=GetMenuGeo();
    int th=TextHeightWin();
    int sideW=(pg.panel.right-pg.panel.left)*26/100;
    if(sideW<UIS(140)) sideW=UIS(140);
    if(sideW>UIS(220)) sideW=UIS(220);
    int tabH=th+UIS(14), gap=UIS(8);
    *x0=pg.panel.left+UIS(14); *x1=*x0+sideW;
    *y0=pg.panel.top+th+UIS(28)+idx*(tabH+gap); *y1=*y0+tabH;
}

void DrawMenuPanels(void) {
    if(currentState==STATE_SAFEHOUSE) {
        BlendRectWin(0,0,winW,winH,UI_BG_PANEL,1.0f);
        if(shopPage!=SHP_TREE) { DrawSafehousePage(); return; }
    }
    MenuGeo g = GetMenuGeo();
    /* v2.7：改造树是**整屏**页面，不走下面的通用面板 ——
       否则旧面板和标题会画在树底下（截图里能看到 SAFE HOUSE - SHOP 残留）。 */
    if (currentState == STATE_SAFEHOUSE && shopPage == SHP_TREE) {
        DrawBenchTree(g);
        return;
    }
    /* v2.2：9-slice 铆接金属板（四角螺丝 + 双线边 + 底部警示条） */
    if (SpritesReady()) {
        UiPanel9(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, 0);
    } else {
        BlendRectWin(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, 0x000a1016, 0.94f);
        FrameRectWin(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, UI_EDGE, 0.85f, UIS(2));
    }

    /* ==================================================================
     * 2026-11-06（用户实测：**局内商店/改造树退不出去**）
     * ------------------------------------------------------------------
     * 手机上 esc 可能被浏览器吞、菜单键也可能按不到，
     * 所以给一块**画在界面上的返回按钮**（和技能树那个同一套）。
     * ================================================================ */
    if (currentState == STATE_SAFEHOUSE) {
        int bx0, by0, bx1, by1;
        ShopBackButtonRect(&bx0, &by0, &bx1, &by1);
        BlendRectWin(bx0, by0, bx1, by1, 0x00101418u, 0.90f);
        FrameRectWin(bx0, by0, bx1, by1, 0x00607080u, 0.95f, UIS(2));
        DrawTextCenterWin((bx0 + bx1) / 2, (by0 + by1) / 2 - TextHeightWin() / 2,
                          Tr("BACK", "返回"), UI_ACCENT);
    }

    /* v2.8：安全屋的**所有**页都要显示页签（不只改造树那一页） */
    if (currentState == STATE_SAFEHOUSE) {
        DrawPageTabs();
        /* v3.0：余额移到**页签下面**独立一行 ——
           5 个页签变宽后右端会压到余额（截图里 GOLD 被 UPGRADE 盖住）。 */
        {
            static char sb2[64];
            {  sprintf(sb2, "GOLD %d", carryGold); }   /* v6 第 40 轮：同 1567 行，双倍 */
            int bx0, by0, bx1, by1;
            PageTabRect(0, &bx0, &by0, &bx1, &by1);
            /* v6 **余额必须锚在面板右边缘，不是窗口右边缘** ——
               用 winW 算的话，窗口比面板宽时 GOLD 会跑到面板外面，
               看起来就是「字幕漂移到屏幕最右边」。 */
            DrawTextWin(g.panel.right - UIS(20) - TextWidthWin(sb2),
                        g.panel.top + UIS(12), sb2, UI_ACCENT);
        }
    }

    /* ==================================================================
     * 2026-11-06（用户要求，第二轮）：**F11 提示只出现一次**
     * ------------------------------------------------------------------
     * 用户原话：
     *   「每次我回主页那个切换键都会跳出来，你就只选择那个登录进去的
     *     第一次那个 F11 键切换提醒它会出来，而且那个全屏状态下
     *     那个切换键是不会跳出来的。然后把它的闪烁特效给关了，
     *     就在那里出现 3 秒钟」
     *
     * 三条规则：
     *   ① **整个进程只显示一次**（第一次进入主菜单开始计时）
     *   ② `isFullscreen` 为真时完全不显示
     *   ③ 不闪烁，静态显示 3 秒后消失
     * ================================================================ */
    {
        static int    s_hintDone  = 0;      /* 已经放过一次了 */
        static double s_hintStart = -1.0;   /* 第一次进主菜单的时刻 */



        if (!s_hintDone && !isFullscreen &&
            currentState == STATE_MAIN_MENU) {
            if (s_hintStart < 0.0) s_hintStart = g_timeSec;
            double age = g_timeSec - s_hintStart;
            if (age >= 0.0 && age < 3.0) {
                const char* hint = (g_lang == LANG_ZH)
                    ? "[F11]  全屏 / 窗口"
                    : "[F11]  FULLSCREEN / WINDOWED";
                int hw = TextWidthWin(hint);
                /* 面板上方、右对齐面板右边 —— 不遮标题、菜单行、BEST LEVEL */
                int hy = g.panel.top - UIS(26);
                if (hy < UIS(4)) hy = g.panel.top + UIS(4);
                DrawTextWin(g.panel.right - hw, hy, hint, UI_ACCENT);
            } else if (age >= 3.0) {
                s_hintDone = 1;             /* 放过一次，之后再也不放 */
            }
        }
    }

    /* ===== v2.2 统一装饰：所有界面共用，不再各写各的 ===== */
    {
        int L2 = g.panel.left, T2 = g.panel.top, R2 = g.panel.right, B2 = g.panel.bottom;
        int cx2 = (L2 + R2) / 2;
        int inner = UIS(30);

        /* 角标 */
        UiCornerMarks(L2, T2, R2, B2);

        /* 标题下横线：主菜单/技能树除外（它们自己有排版） */
        if (currentState != STATE_MAIN_MENU &&
            currentState != STATE_HUB &&
            currentState != STATE_BANK) {
            int ty = g.titleY + TextHeightWin() + TextHeightWin() / 2;
            if (currentState == STATE_SAFEHOUSE) {
                /* 安全屋：线只铺「内容列」，不越过左边那一列页签 */
                int tx0, ty0, tx1, ty1;
                PageTabRect(0, &tx0, &ty0, &tx1, &ty1);

                UiSectionRuleSpan(tx1 + UIS(24), R2 - inner, ty);
            } else {
                UiSectionRule(cx2, ty, (R2 - L2) - inner * 2);
            }
        }

        /* 页脚横线 + 操作提示：只在**有行列表**的界面画。
           v3.3：技能树也要排除 —— 它的 MenuGeo 是给命中测试用的：
             count = SKILL_COUNT(20)，rowH = nodeH
           但实际布局是 4 列 × 5 行，所以 firstY + (count-1)*rowH 算出来
           一路算到面板底之外，线要么被夹到 keytone 那一排上（用户截图），
           要么干脆不画。技能树自己有排版，不需要这条线。 */
        if (currentState != STATE_MAIN_MENU &&
            currentState != STATE_HUB &&
            currentState != STATE_BANK) {
            int fy = B2 - UIS(26);
            /* v2.9：**线要画在最后一行文字下面**。
               原来 
y - TH/2，主菜单第 10 行「Quit」正好被这条线横切
               （用户截图：Quit 上有条小横线）。
               现在先算出最后一行的底边，线放它下面 4px。 */
            int lastRowBottom = g.firstY + (g.count - 1) * g.rowH + TextHeightWin();
            /* v3.3：**间距从 4 加到 12** ——
               TextHeightWin() 只算了字高，实际字形（尤其是带下伸笔画的字母）
               会再往下多出几个像素，4 的间距不够，线会切到最后一行的字。
               12 在 800x600 下相当于半个字高，足够。 */
            int lineY = lastRowBottom + UIS(12);
            /* v3.3：**空间不够就干脆不画** ——
               原来写的是 if (lineY > fy) lineY = fy;
               也就是把线**往上夹**到 fy = 面板底 - 26。
               暂停菜单只有 3 行、行高又大，夹回来的位置正好横切
               「Main Menu」这行文字（用户截图）。
               现在改成：线必须落在最后一行下面且离面板底还有余量才画，
               放不下就不画 —— 少一条装饰线，不压字。 */
            if (lineY + UIS(2) <= fy) {
                int fx0 = L2 + inner;
                if (currentState == STATE_SAFEHOUSE) {
                    /* 同样只铺内容列 —— 否则页脚线会横穿页签列 */
                    int tx0, ty0, tx1, ty1;
                    PageTabRect(0, &tx0, &ty0, &tx1, &ty1);
                    fx0 = tx1 + UIS(24);
                }
                UiFooterRule(fx0, R2 - inner, lineY);
            }
        }
    }

    if (currentState == STATE_SAFEHOUSE) DrawCalibStrip(g);

    /* v1.22b：Hub 分页指示**挪到面板外**（屏幕右下角），
       原来画在面板里会和最后一行 Back 的高亮条重叠。 */
    /* v3.3 **Help 页补页脚** ——
       试玩实测：「Help 页没有 ESC BACK 页脚（成就页/技能树/设置页都有），
       第一眼不知道怎么退」。改成和 Hub 一样放在面板外的右上角。 */
    if (currentState == STATE_HELP) {
        char hf[64];
        sprintf(hf, "TAB  PAGE %d/2", helpPage + 1);
        int tw3 = TextWidthWin(hf);
        DrawTextWin(winW - tw3 - UIS(16), UIS(8), hf, 0x00C08040u);
        const char* back = Tr("ESC  BACK", "ESC  返回");
        DrawTextWin(winW - tw3 - UIS(16) - TextWidthWin(back) - UIS(24), UIS(8),
                    back, 0x00707070u);
    }

    /* v6：**页码要在 Hub 所有页上都显示，而且是 /3 不是 /2** ——
       用户截图：「点 HUB 进去页码已损坏」。原因是这里写死 hubPage==0，
       技能树页（页 1）和银行页（页 2）都不画，只剩面板边缘的残影。
       另外 Hub 现在有 3 页（升级 / 技能树 / 银行），分母也得改成 3。 */
    /* v6：Hub 合并成一页后不再需要页码 */
    /* 弹窗开着时不再高亮背后的列表行 */
    int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);
    if (currentState == STATE_BESTIARY) { DrawBestiaryPage(g); return; }
    int sel = MenuItemIndex();
    /* v2 技能树不是行列表：选中框由 DrawSkillTree 自己画在节点上 */
    int rowHighlight = !(currentState == STATE_HUB ||
                         currentState == STATE_BANK);
    /* v2.2：主菜单/技能树自己画选中态，通用行高亮一律关掉 */
    if (currentState == STATE_MAIN_MENU) rowHighlight = 0;

    /* v2.2：主菜单自己画选中态；通用行高亮只在真正的行列表界面生效 */
    if (currentState == STATE_MAIN_MENU) rowHighlight = 0;

    if (rowHighlight && !popupOpen &&
        sel >= g.scrollOffset && sel < g.scrollOffset + g.maxVisibleRows) {
        RECT r = RowRect(g, sel);
        /* v2.2：底纹 + 左竖线 + 三角光标（不再是纯绿条） */
        UiRowSel(r.left, r.top, r.right, r.bottom, 0.85f);
    }

    /* v3.0：**鼠标悬停高亮** —— 鼠标移到哪一行，哪一行就亮起来。
       没有这个的话玩家得靠猜才知道能点哪（用户反馈不想按回车）。 */
    if (currentState == STATE_SAFEHOUSE && safehouseHoverRow >= 0 &&
        safehouseHoverRow != sel && safehouseHoverRow < g.count) {
        RECT hr = RowRect(g, safehouseHoverRow);
        BlendRectWin(hr.left, hr.top, hr.right, hr.bottom, 0x00405060u, 0.55f);
        FrameRectWin(hr.left, hr.top, hr.right, hr.bottom, 0x0080A0B0u, 0.7f, 1);
    }
}

static void DrawMainMenu(const MainMenuLayout* M) {
    if(menuSelection<0 || menuSelection>=M->count)menuSelection=0;
    int th=TextHeightWin(),left=M->panel.left+M->padX;
    BlendRectWin(0,0,winW,winH,0x00040C17u,1);
    /* Perspective doorway fills the screen; selection remains on one coherent axis. */
    int cx=winW*4/5,cy=winH/2;
    for(int depth=0;depth<8;depth++) {
        int w=winW/(3+depth),h=winH/(2+depth);
        FrameRectWin(cx-w/2,cy-h/2,cx+w/2,cy+h/2,UI_EDGE,.65f-depth*.06f,2);
    }
    UiTitle((M->panel.left+M->panel.right)/2,M->titleY,"C  A  N  I  T  Y");
    DrawTextSmallWin(left,M->subY,Tr("LISTEN. HIDE. RETURN.","倾听。藏匿。归来。"),UI_INK_DIM);
    for(int i=0;i<M->count;i++) {
        int y=M->firstY+i*M->rowH;
        if(i==menuSelection)UiRowSel(left-UIS(12),y,M->panel.right,y+M->rowH-UIS(4),.7f);
        DrawTextWin(left+UIS(10),y+(M->rowH-th)/2,MenuLabel(i),i==menuSelection?UI_ACCENT:UI_INK);
    }
    BlendRectWin(left,M->footRuleY,winW-UIS(60),M->footRuleY+2,UI_EDGE,1);
    DrawTextSmallWin(left,M->footY,Tr("ENTER / SPACE / CLICK","回车 / 空格 / 单击"),UI_INK_DIM);
    DrawTextSmallWin(winW-TextSmallLiteralWidth(CANITY_VERSION_STR)-UIS(60),M->footY,CANITY_VERSION_STR,UI_INK_DIM);
}


static void DrawGenericMenuTitle(MenuGeo g) {
    unsigned int titleColor = (currentState == STATE_MAIN_MENU) ? UI_ACCENT : UI_ACCENT;
    /* 通关结局的标题用绿色 —— 和「理智耗尽」的暗红区分开 */
    if (currentState == STATE_GAMEOVER && gameWon) titleColor = 0x0060E070u;
    if (currentState == STATE_MAIN_MENU) {
        char sb[48];
        /* 页脚位置按"文字实际高度"算，不写死偏移 ——
           否则大字库下会被窗口下沿切掉半行（BEST LEVEL 就中过）。
           y 是文字左上角，所以最后一行要留出一个文字高度。 */
        int th = TextHeightWin();
        int y2 = winH - th - UIS(10);
        int y1 = y2 - th - UIS(6);
        /* v1.8：货币不再是裸文字 —— 前面加一颗菱形水晶图标 */
        sprintf(sb, "GOLD  %d", carryGold);
        {
            int tw = TextWidthWin(sb);
            int tx = (winW - tw) / 2;
            int cy2 = y1 + TextHeightWin() / 2;
            int s2 = UIS(5);
            for (int dy = -s2; dy <= s2; dy++) {
                int span = s2 - (dy < 0 ? -dy : dy);
                for (int dx = -span; dx <= span; dx++) {
                    unsigned int c = (dx == 0 || dy == 0) ? 0x00E8FBFFu : 0x0040B8F0u;
                    /* 屏幕坐标直接写像素池（菜单态整屏都在同一个缓冲里） */
                    int px = (tx + dx - UIS(14)) * renderW / winW;
                    int py = (cy2 + dy) * renderH / winH;
                    PutPixel(px, py, c);
                }
            }
            DrawTextWin(tx, y1, sb, 0x0060D0FFu);
        }
        char bb[48];
        sprintf(bb, "BEST LEVEL %d", bestLevel);
        DrawTextCenterWin(winW / 2, y2, bb, 0x00808080u);
        /* 版本号：右下角低调显示，方便对着存档确认是哪个版本 */
        DrawTextWin(winW - TextWidthWin(CANITY_VERSION_STR) - UIS(14),
                    y2, CANITY_VERSION_STR, 0x00585858u);
    }
    /* v2.9：标题为空就不画 —— 安全屋靠顶部页签表明位置，
       那行大红「SAFE HOUSE - SHOP」会把改造树整个盖住（用户要求去掉）。 */
    if (g.title && g.title[0]) DrawTitleWin(g.title, g.titleY, titleColor);
}


/* Wrap UTF-8 at complete codepoints. Every section has an explicit lower bound. */
static int DossierText(const char* text,int x,int y,int width,int bottom,unsigned int color){
    if(!text || width<=0)return y;
    int height=TextMediumLiteralHeight(),step=height+UIS(4);
    while(*text && y+height<=bottom){
        char line[256];int used=0,fit=0;
        if(*text=='\n'){text++;y+=step;continue;}
        while(text[used] && text[used]!='\n' && used<240){
            unsigned char c=(unsigned char)text[used];int n=c<128?1:c<224?2:c<240?3:4;
            if(used+n>=250)break;
            int valid=1;for(int i=1;i<n;i++)if(!text[used+i] || ((unsigned char)text[used+i]&0xC0)!=0x80)valid=0;
            if(!valid)n=1;
            memcpy(line+used,text+used,(size_t)n);used+=n;line[used]=0;
            if(TextMediumLiteralWidth(line)>width)break;
            fit=used;
        }
        if(!fit)break;
        line[fit]=0;DrawTextMediumLiteralWin(x,y,line,color);text+=fit;y+=step;
        if(*text=='\n')text++;
    }
    return y;
}

/* Use exactly the existing baked world sprite, cropped without stretching.
 * No game camera, light, sprite-accent or monster state is modified. */
static void DrawDossierPortrait(int spriteId, RECT box, int identified) {
    if (!SpritesReady() || !pixels || renderW <= 0 || renderH <= 0 || winW <= 0 || winH <= 0) return;
    Sprite* sprite = SpriteGet(spriteId);
    if (!sprite || !sprite->px) return;
    int sx = sprite->bx0, sy = sprite->by0;
    int sw = sprite->bx1-sx, sh = sprite->by1-sy;
    if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0 || sprite->bx1 > sprite->w || sprite->by1 > sprite->h) {
        sx = sy = 0; sw = sprite->w; sh = sprite->h;
    }
    int left = box.left*renderW/winW, top = box.top*renderH/winH;
    int bw = (box.right-box.left)*renderW/winW, bh = (box.bottom-box.top)*renderH/winH;
    if (bw <= 0 || bh <= 0 || sw <= 0 || sh <= 0) return;
    float k = (float)bw/sw; if ((float)bh/sh < k) k = (float)bh/sh;
    int w = (int)(sw*k), h = (int)(sh*k);
    if (w <= 0 || h <= 0) return;
    left += (bw-w)/2; top += (bh-h)/2;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        unsigned int pixel = sprite->px[(sy+y*sh/h)*sprite->w + sx+x*sw/w];
        float alpha = (float)(pixel >> 24)/255.0f;
        if (alpha <= 0.0f) continue;
        PutPixelBlend(left+x,top+y,identified ? (pixel&0xFFFFFFu) : 0x001C2935u,alpha);
    }
}

static void DrawBestiaryPage(MenuGeo g) {
    int th = TextHeightWin(), pad = UIS(16);
    int split = g.panel.left + (g.panel.right-g.panel.left)*32/100;
    int x = split+pad, right = g.panel.right-pad;
    BlendRectWin(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,0x00090F17u,.99f);
    FrameRectWin(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,0x00485668u,.95f,1);
    DrawTextWin(g.panel.left+pad,g.titleY,Tr("FIELD ARCHIVE","怪物档案"),UI_ACCENT);
    char text[160];
    snprintf(text,sizeof(text),Tr("ACTIVE %d / %d","现行 %d / %d"),BestiaryCountSeen(bestiarySeen),BestiaryActiveCount());
    DrawTextWin(right-TextWidthWin(text),g.titleY,text,0x008CCBD3u);
    BlendRectWin(split,g.firstY-UIS(8),split+1,g.panel.bottom-pad,0x00404C60u,.9f);
    for (int id = 0; id <= BESTIARY_COUNT; id++) {
        RECT row = RowRect(g,id);
        int selected = bestiarySel == id;
        if (selected) BlendRectWin(row.left,row.top,row.right,row.bottom,0x002B394Bu,.95f);
        if (id == BESTIARY_COUNT) snprintf(text,sizeof(text),"< %s",Tr("BACK","返回"));
        else if (!BestiaryIsActive(id)) snprintf(text,sizeof(text),"%02d  %s",id+1,Tr("ARCHIVED","旧版记录"));
        else snprintf(text,sizeof(text),"%02d  %s",id+1,BestiaryIsSeen(id) ? Tr(bestiary[id].nameEn,bestiary[id].nameZh) : "???");
        ClampRowText(text,row.right-row.left-UIS(12));
        DrawTextWin(row.left+UIS(6),row.top+(row.bottom-row.top-th)/2,text,selected?0x00FFE1A0u:0x0093A4B7u);
    }
    int id = bestiarySel;
    if (id < 0 || id >= BESTIARY_COUNT) {
        DossierText(Tr("Return to the main menu. Your archive is kept.","返回主菜单。已发现的档案会保留。"),x,g.firstY,right-x,g.panel.bottom-pad,0x0093A4B7u);
        return;
    }
    const BestiaryEntry* entry = &bestiary[id];
    int seen = BestiaryIsSeen(id), stage = BestiaryStage(id), active = BestiaryIsActive(id);
    int y = DossierText(seen ? ((id==BST_ID_HUNTER)?Tr("SOUND HUNTERS / THREE FORMS","听声鬼 / 三种形态"):Tr(entry->nameEn,entry->nameZh)) : Tr("UNIDENTIFIED","未知生物"),x,g.firstY,right-x,g.panel.bottom-pad,0x00F0E1C8u);
    snprintf(text,sizeof(text),"%s / %s",active ? Tr("ACTIVE","现行") : Tr("ARCHIVED","旧版"),seen?Tr(entry->tagEn,entry->tagZh):"???");
    y = DossierText(text,x,y,right-x,g.panel.bottom-pad,0x007DA7BEu);
    int bottom = g.panel.bottom-th-pad;
    int imageSize = (right-x)*46/100;
    if (imageSize > (bottom-y)*42/100) imageSize = (bottom-y)*42/100;
    if (imageSize < 12) return;
    RECT image = {x,y+UIS(6),x+imageSize,y+UIS(6)+imageSize};
    BlendRectWin(image.left,image.top,image.right,image.bottom,0x00131C29u,1.0f);
    FrameRectWin(image.left,image.top,image.right,image.bottom,0x00374B60u,.9f,1);
    RECT inset = {image.left+UIS(5),image.top+UIS(5),image.right-UIS(5),image.bottom-UIS(5)};
    if(id==BST_ID_HUNTER) {
        static const int variants[3]={SPR_MON_HUNTER,SPR_MON_HUNTER2,SPR_MON_HUNTER3};
        int width=(inset.right-inset.left)/3;
        for(int variant=0;variant<3;variant++) {
            RECT portrait={inset.left+variant*width,inset.top,inset.left+(variant+1)*width,inset.bottom};
            DrawDossierPortrait(variants[variant],portrait,seen);
        }
    } else DrawDossierPortrait(entry->spriteId,inset,seen);
    int sx = image.right+UIS(10), sy = image.top, sw = right-sx;
    snprintf(text,sizeof(text),Tr("KNOWLEDGE %d / 3","认知 %d / 3"),stage);
    sy = DossierText(text,sx,sy,sw,image.bottom,UI_ACCENT);
    if (seen) {
        snprintf(text,sizeof(text),Tr("RISK %d / 5","危险 %d / 5"),BestiaryDanger(id));
        sy = DossierText(text,sx,sy,sw,image.bottom,0x00DB997Du);
        snprintf(text,sizeof(text),Tr("ESCAPES %d","逃脱 %d"),bestiaryEscapes[id]);
        sy = DossierText(text,sx,sy,sw,image.bottom,0x0095B5BCu);
        snprintf(text,sizeof(text),Tr("CAUGHT %d","被抓 %d"),bestiaryCaught[id]);
        sy = DossierText(text,sx,sy,sw,image.bottom,0x0095B5BCu);
    }
    y = image.bottom+UIS(10);
    if (!active) {
        DossierText(Tr("Legacy record. This creature no longer spawns and is excluded from completion.","历史记录：当前版本不生成，不计入现行全图鉴。"),x,y,right-x,bottom,0x0093A4B7u);
    } else if (!seen) {
        DossierText(Tr("Encounter this creature in a run to identify its portrait and behavior.","在局内遭遇后，解锁肖像与身份。研究逐层积累，逃脱能加速揭示。"),x,y,right-x,bottom,0x0093A4B7u);
    } else {
        int half = y + (bottom-y)/2;
        y = DossierText(Tr("BEHAVIOR","行为"),x,y,right-x,half,UI_ACCENT);
        DossierText(stage>=BST_BEHAVED?Tr(entry->behaviorEn,entry->behaviorZh):Tr("Study across 3 floors or escape twice.","累计遭遇三层，或逃脱两次后揭示。"),x,y,right-x,half,0x00B6C7D1u);
        y = DossierText(Tr("COUNTERMEASURE","对策"),x,half+UIS(4),right-x,bottom,0x008CCBD3u);
        DossierText(stage>=BST_COUNTER?Tr(entry->counterEn,entry->counterZh):Tr("Study across 5 floors or escape three times.","累计遭遇五层，或逃脱三次后揭示。"),x,y,right-x,bottom,0x00B6C7D1u);
    }
    DrawTextSmallWin(x,bottom,Tr("ARROWS / MOUSE   ESC BACK","ARROWS / MOUSE   ESC BACK"),0x006E8497u);
}


static void DrawGenericMenuRows(MenuGeo g, int sel, int popupOpen, const int* idx, int pageItemCount) {
    /* THE HUB 升级页：价格列宽度按**本页最宽的价格**算一次，
       等级点列锚在它左边。这样每行都对齐，也不会出现
       "120 GOLD" 压到等级点上的情况。 */
    int hubPriceZone = 0;
    if (0) {
        for (int k = 0; k < UPGRADE_COUNT; k++) {
            char t[40];
            int c2 = UpgradeCost(k);
            if (c2 < 0) sprintf(t, "MAX"); else sprintf(t, "%d GOLD", c2);
            int w2 = TextWidthWin(t);
            if (w2 > hubPriceZone) hubPriceZone = w2;
        }
    }
    /* v6 **几何固定 ≠ 行数固定** ——
       安全屋面板固定成 9 行后，TREE 页本该 0 行（它自己画改造树），
       却把 9 行文本全叠上去，和树节点糊成一片（用户截图）。
       这里把「画几行」和「面板多高」分开：几何用 9，行数仍按 ShopRowCount()。 */
    int startRow = g.scrollOffset;
    int endRow = g.scrollOffset + g.maxVisibleRows;
    if (currentState == STATE_SAFEHOUSE) {
        int realRows = ShopRowCount();
        if (endRow > realRows) endRow = realRows;
    }
    if (endRow > g.count) endRow = g.count;
    if (endRow < 0) endRow = 0;

    for (int i = startRow; i < endRow; i++) {
        const SettingItem* it = NULL;
        if (currentState == STATE_SETTINGS && settingsPage != SETPAGE_ROOT && settingsPage != SETPAGE_BINDDIR) {
            if (i < pageItemCount) it = &settings[idx[i]];
            /* i >= pageItemCount 是 Back 行，it 保持 NULL */
        }

        unsigned int c;
        if (popupOpen) c = 0x00383E3Au;                       /* 弹窗后面的列表压暗 */
        else if (currentState == STATE_HUB && i < UPGRADE_COUNT) {
            int cost = UpgradeCost(i);
            if (cost < 0) c = 0x00707070u;
            else if (carryGold >= cost) c = (i == sel) ? 0x008CFF8Cu : 0x00A0FFA0u;
            else c = (i == sel) ? 0x00FF8080u : 0x00B06060u;
        }
        else c = (i == sel) ? 0x008CFF8Cu : 0x009B9B9Bu;

        if (currentState == STATE_GAMEOVER && i > 0) {
            /* 复盘：第 2、3 行放统计（通关与失败共用这一页，标题区分） */
            char line[112];
            int mins = (int)(runTime / 60.0f);
            int secs = (int)runTime - mins * 60;
            if (i == 1) {  snprintf(line,sizeof(line),"LEVEL %d  TIME %d:%02d  CAUGHT %d",currentLevel,mins,secs,lastRunCaught); }
            else if (gameWon)
                sprintf(line, "SCORE %d  D%d C%d K%d T%d",
                        lastScore.score, lastScore.s_depth, lastScore.s_coll,
                        lastScore.s_kill, lastScore.s_time);
            else {
                /* v7 第 ㊿ 轮：**死亡页必须能归因**
                   原来只有 LEVEL / TIME / SCORE —— 玩家死了不知道为什么，
                   也不知道身上 6,000 金只退回 2,100，
                   走出这一局学不到任何东西，只会觉得"我明明很小心"。

                   现在多给三行：被抓几次 / 金币损失 / 理智在哪层见底。 */


                int caught = lastRunCaught;
                if (i == 1) continue;   /* 第 1 行已被上面 LEVEL/TIME 占掉 */
                sprintf(line, "CAUGHT %d  SCORE %d", caught, lastScore.score);
                if (i == 2) {
                    /* 金币：本金 → 实退（35%） */
                    int kept = lastRunKept;
                    sprintf(line, "GOLD %d  KEPT %d  (35%%)", lastRunGold, kept);
                }
            }
            DrawTextInRow(line, RowRect(g, i), (i == sel) ? 0x008CFF8Cu : 0x009B9B9Bu);
            continue;
        }

        /* CONTROLS 页的说明文字用左对齐 ——
           居中对齐遇到长句子会超出面板左右边缘。 */
        if (currentState == STATE_HELP) {
            DrawTextInRowLeft(MenuLabel(i), RowRect(g, i), c);
            continue;
        }

        /* 键位绑定行：单行「名称 ····· 键名」，值右对齐。
           之前是两行式（标签一行 + 值一行），但行高 UIS(44) 扣掉边距只剩
           36px，装不下两行 24px 的文字，标签会压到相邻行 —— 显示不全。 */
        if (it && IsKeyBindItem(it)) {
            RECT row = RowRect(g, i);
            int th = TextHeightWin();
            int y = row.top + ((row.bottom - row.top) - th) / 2;
            DrawTextWin(row.left + UIS(8), y, it->name, c);
            int act = BindActionOfItem(it);
            const char* valStr;
            if (act >= 0 && act == rebindingAct) {
                valStr = "...";          /* 正在改绑这一项 */
            } else {
                int vk = *it->value;
                int dispVal = -1;
                for (int k = 0; k < OPT_KEY_COUNT; k++) {
                    if (optKeyVKs[k] == vk) { dispVal = k; break; }
                }
                if (dispVal >= 0) valStr = optKeys[dispVal];
                else valStr = (vk == 0) ? "None" : "?";
            }
            int tw = TextWidthWin(valStr);
            DrawTextWin(row.right - UIS(8) - tw, y, valStr, 0x0070B0D0u);
            continue;
        }

        /* ---- v2.1：THE HUB 升级页改成三列 ----
           原来是「名字 + LV + 价格」拼成一整串再居中画，窗口一窄就顶出面板
           （实测 800x600 下 QUIETER / ECO / REST 三行都出界被切）。
           现在拆成三列，每一列各自对齐、各自截断，永远不会出界。 */
    if (0) {
            /* v3.4：携带档位行 —— 只画文字，没有等级点 */
            DrawTextInRow(MenuLabel(i), RowRect(g, i), c);
            continue;
        }
    if (0) {
            RECT row = RowRect(g, i);
            int th2 = TextHeightWin();
            int y = row.top + ((row.bottom - row.top) - th2) / 2;
            int x0 = row.left + UIS(12);
            int x1 = row.right - UIS(12);

            /* 右列：价格（右对齐，买不起变暗红） */
            char price[40];
            int cost = UpgradeCost(i);
            unsigned int priceCol;
            if (cost < 0) {
                sprintf(price, "MAX");
                priceCol = UI_ACCENT;
            } else {
                sprintf(price, "%d GOLD", cost);
                priceCol = (carryGold >= cost) ? 0x0060D0FFu : 0x00C86868u;
            }
            int pw = TextWidthWin(price);
            DrawTextWin(x1 - pw, y, price, priceCol);

            /* 中列：等级用 3 个小方块表示（实心 = 已买），比 "LV2/3" 省地方也更直观。
               列位置锚在价格列左边 —— 跟着每行自己的价格浮动会让方块左右乱跳。 */
            int lv = UpgradeLevel(i);
            int sq = th2 / 2; if (sq < 6) sq = 6;
            int sp = sq / 2; if (sp < 2) sp = 2;
            int lvW = UPG_MAX * sq + (UPG_MAX - 1) * sp;
            int lvX = x1 - hubPriceZone - UIS(20) - lvW;
            int sy = row.top + ((row.bottom - row.top) - sq) / 2;
            for (int k = 0; k < UPG_MAX; k++) {
                int sx = lvX + k * (sq + sp);
                if (k < lv) BlendRectWin(sx, sy, sx + sq, sy + sq, 0x00C8E8FFu, 0.95f);
                else        FrameRectWin(sx, sy, sx + sq, sy + sq, PAL_DIM, 0.9f, 2);
            }

            /* 左列：名字，右边界卡到等级点左边 —— 放不下就 ".." 截断 */
            char nm[64];
            sprintf(nm, "%s", UpgradeName(i));
            ClampRowText(nm, lvX - UIS(14) - x0);
            DrawTextWin(x0, y, nm, c);
            continue;
        }

        if(currentState==STATE_SAFEHOUSE) {
            RECT row=RowRect(g,i);
            char text[256]; snprintf(text,sizeof(text),"%s",MenuLabel(i));
            ClampRowText(text,row.right-row.left-UIS(8));
            DrawTextInRowLeft(text,row,c);
        } else DrawTextInRow(MenuLabel(i), RowRect(g, i), c);
    }

    if(currentState==STATE_SAFEHOUSE && shopPage==SHP_SHOP) {

        char hint[80];snprintf(hint,sizeof(hint),"R  REFRESH %d/1",shopRefreshUsed?0:1);
        DrawTextInRowLeft(hint,RowRect(g,SHOP_OFFERS+1),PAL_CYAN);
    }
}

static void DrawMenuScrollIndicator(MenuGeo g) {
    /* 绘制滚动指示器 */
    if (g.count > g.maxVisibleRows) {
        int barH = g.panel.bottom - g.firstY;
        int thumbH = (int)((float)g.maxVisibleRows / g.count * barH);
        if (thumbH < 20) thumbH = 20;
        int thumbY = g.firstY + (int)((float)g.scrollOffset / (g.count - g.maxVisibleRows) * (barH - thumbH));
        int barX = g.panel.right - UIS(12);
        BlendRectWin(barX, g.firstY, barX + UIS(8), g.panel.bottom, 0x00202020, 0.5f);
        BlendRectWin(barX, thumbY, barX + UIS(8), thumbY + thumbH, 0x008CFF8C, 0.8f);

        /* 顶部/底部箭头提示 */
        if (g.scrollOffset > 0) {
            DrawTextCenterWin(g.panel.left + (g.panel.right - g.panel.left) / 2, g.firstY - UIS(20), "^", 0x008CFF8C);
        }
        if (g.scrollOffset + g.maxVisibleRows < g.count) {
            DrawTextCenterWin(g.panel.left + (g.panel.right - g.panel.left) / 2, g.panel.bottom + UIS(4), "v", 0x008CFF8C);
        }
    }
}

static void DrawMenuText(void) {
    if(currentState==STATE_SAFEHOUSE)return;
    /* ==================================================================
     * v7 第 ㊿ 轮：**商店必须在 GetMenuGeo() 之前分派**
     * ==================================================================
     * 上一版我把分派放在 GetMenuGeo() 之��—— 结果永远是
     * GetMenuGeo() → MakeGeo(SAFEHOUSE) 先崩，我那个独立面板
     * 一次都没跑到。
     *
     * 现在把它提到最前面：安全屋这一页完全不碰通用几何。
     * ================================================================ */
    /* v7 第 ㊿ 轮：安全屋回到通用菜单渲染 —— 它会画页签、
       改造树、局内强化、休息页。之前那条路径的崩溃已随
       ShopPriceAt() 的递归修复而消失。 */
    MenuGeo g = GetMenuGeo();
    int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);
    if (currentState == STATE_BESTIARY) { DrawBestiaryPage(g); return; }

    /* 成就页面：使用专用现代化渲染 */
    if (currentState == STATE_ACHIEVEMENTS) {
        UiDrawAchievementsPage(g);
        return;
    }

    /* v6：**Hub 有 3 页** —— 0 升级列表 / 1 技能树 / 2 银行。
       原来这里写死 hubPage == 1，导致银行页根本进不来，
       掉到下面的通用列表渲染，看起来就是「点 Bank 显示 Hub」。
       现在改成 >= 1，再把 1 和 2 分开。 */
    /* v6：**BANK 是独立状态**，不再和 Hub 共用页号 */
    if (currentState == STATE_BANK) {
        DrawBankPage();
        return;
    }
    if (currentState == STATE_HUB) {
        /* v7 第 54 轮：**技能树临时降级**
           git checkout -- src/skills.c 把 264 行未提交改动冲掉之后，
           切到 Hub 会直接崩溃（无崩溃日志 = 栈耗尽或死循环）。
           我重写的 SkillNodeX/Y 几何和原版不一致，连线也画不出来。
           在重写整个技能树之前，先保证**界面能进、不崩**，
           并且把列表版购买入口留着（Hub 走通用列表渲染）。 */
        int kHubFallbackTree = 0;   /* v7 第 58 轮：6 个空节点补回后技能树正常，永久启用 */   /* v7 第 57 轮：几何修了两次仍然崩，尊重事实——回到列表 */   /* v7 第 57 轮：几何修好后重新启用技能树 */   /* v7 第 54 轮：1=列表降级 0=技能树（树崩溃中） */
        if (!kHubFallbackTree) { DrawSkillTree(); return; }
        /* 降级：走通用列表渲染（下面会画 MenuLabel 行） */
    }


    /* v2.2 主菜单：自己的布局（标题 / 行 / 页脚） */
    if (currentState == STATE_MAIN_MENU) {
        MainMenuLayout M;
        MainMenuCompute(&M);
        DrawMainMenu(&M);
        return;
    }

    if(currentState==STATE_HELP){HelpBookDraw();return;}
    DrawGenericMenuTitle(g);
    int sel = MenuItemIndex();
    int idx[SETTING_COUNT];
    int pageItemCount = 0;
    if (currentState == STATE_SETTINGS && settingsPage != SETPAGE_ROOT && settingsPage != SETPAGE_BINDDIR) {
        pageItemCount = PageItems(idx);
    }

    /* 确保选中的项在可见范围内 */
    if (sel >= 0 && sel < g.count) {
        if (sel < g.scrollOffset) {
            g.scrollOffset = sel;
        } else if (sel >= g.scrollOffset + g.maxVisibleRows) {
            g.scrollOffset = sel - g.maxVisibleRows + 1;
        }
    }

    /* 将调整后的滚动偏移保存回全局数组，持久化到下次调用 */
    if (currentState >= 0 && currentState < 8) {
        menuScrollOffsets[currentState] = g.scrollOffset;
    }

    /* Bestiary uses its dedicated dossier layout above. */
    DrawGenericMenuRows(g, sel, popupOpen, idx, pageItemCount);
    DrawMenuScrollIndicator(g);
}

static void DrawTextInRowLeft(const char* text, RECT row, unsigned int color) {
    int th = TextHeightWin();
    int y = row.top + ((row.bottom - row.top) - th) / 2;
    DrawTextWin(row.left + UIS(8), y, text, color);
}

static void DrawCalibStrip(MenuGeo g) {
    /* v2.4：画在面板内部的**底部空白区**（面板已为它加高）。
       画在面板里才能保证每帧被不透明面板盖住，不会留残影。 */
    int pad = UIS(34);
    int barW = (g.panel.right - g.panel.left) - pad * 2;
    if (barW < UIS(160)) return;
    int bx = g.panel.left + pad;
    int barH = UIS(16);
    int by = g.panel.bottom - UIS(30) - barH;
    char line[128];


    /* v3.0：**安全屋里不再画校准条** ——
       用户截图：底部那条 "CALIBRATION [SPACE]" 和 SAVE FAILED 横幅叠在一起，
       还压着页签。
       校准已经搬到藏身点里（60 秒冷却，主要奖励改成理智），
       安全屋这条纯属残留。这里整段留空，只在**校准进行中**画一条提示
       （因为小游戏本身是从安全屋也能开的，得让玩家知道进度）。 */
    if (!calibActive) return;
    {
        sprintf(line, "%s", Tr("QWER NOTES  -  40s PROGRESS",
                               "QWER 接音符  ·  40 秒进度"));
    }
    BlendRectWin(bx - UIS(8), by - TextHeightWin() - UIS(8),
                 bx + barW + UIS(8), by + barH + UIS(6), 0x000A0E14, 0.96f);
    DrawTextWin(bx, by - TextHeightWin() - UIS(4), line, 0x00FFE060u);
    DrawCalibBar(bx, by, barW, barH);

    /* 结果提示 */
    if (calibResultTimer > 0.0f && calibResult) {
        const char* msg = "";
        unsigned int col = 0x00808080u;
        if (calibResult == 1) { msg = Tr("PERFECT", "完美"); col = 0x00FFE060u; }
        else if (calibResult == 2) { msg = Tr("CANCELLED", "已取消"); col = 0x0080E080u; }
        else { msg = Tr("MISSED", "失手"); col = 0x00FF6040u; }
        DrawTextWin(bx + barW - TextWidthWin(msg), by - TextHeightWin() - UIS(4), msg, col);
    }
}

/* Bounded segment rasterization: diagonal graph edges never touch the chrome. */
static void BenchGraphLink(int x0, int y0, int x1, int y1, unsigned int color, int solid) {
    int dx = x1-x0, dy = y1-y0;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (steps < 1 || steps > 12000) return;
    for (int i = 0; i <= steps; i += 3) {
        if (!solid && (i/9)%2) continue;
        int x = x0+(int)((float)dx*i/steps), y = y0+(int)((float)dy*i/steps);
        if (x >= winW*.30f && x+2 < winW-UIS(20) && y >= UIS(70) && y+2 < winH-UIS(178))
            BlendRectWin(x,y,x+2,y+2,color,.9f);
    }
}
static void DrawBenchTree(MenuGeo g) {
    (void)g;
    int th = TextHeightWin(), m = UIS(16);
    static int lastWidth = 0, lastHeight = 0;
    if (lastWidth != winW || lastHeight != winH) {
        BenchResetView(); lastWidth = winW; lastHeight = winH;
    }
    BlendRectWin(m,m,winW-m,winH-m,UI_BG_PANEL,.99f);
    FrameRectWin(m,m,winW-m,winH-m,UI_EDGE,.95f,1);
    DrawPageTabs();
    int bx0,by0,bx1,by1; ShopBackButtonRect(&bx0,&by0,&bx1,&by1);
    BlendRectWin(bx0,by0,bx1,by1,0x00243646u,.98f);
    DrawTextCenterWin((bx0+bx1)/2,(by0+by1-th)/2,Tr("BACK","返回"),UI_ACCENT);
    char line[160];
    snprintf(line,sizeof(line),Tr("RUN GOLD %d","局内金币 %d"),WalletRun());
    DrawTextWin(winW-m-UIS(12)-TextWidthWin(line),m,line,UI_ACCENT);
    int w,h; BenchNodeBoxSize(&w,&h);
    for (int id = 0; id < BENCH_COUNT; id++) {
        int pre = BenchPrereqId(id);
        int px = pre >= 0 ? (int)(BenchNodeWorldX(pre)*treeVK+treeVX) : (int)treeVX;
        int py = pre >= 0 ? (int)(BenchNodeWorldY(pre)*treeVK+treeVY) : (int)treeVY;
        int x = (int)(BenchNodeWorldX(id)*treeVK+treeVX), y = (int)(BenchNodeWorldY(id)*treeVK+treeVY);
        int lit = pre < 0 || BenchHas(pre);
        BenchGraphLink(px,py,x,y,id==shopNodeSel || pre==shopNodeSel?UI_INFO:(lit?UI_ACCENT:UI_EDGE_DIM),lit);
    }
    const unsigned int colors[4] = {0x79CDD5u,UI_ACCENT,UI_DANGER,0xC1B4E4u};
    for (int id = 0; id < BENCH_COUNT; id++) {
        int cx = (int)(BenchNodeWorldX(id)*treeVK+treeVX), cy = (int)(BenchNodeWorldY(id)*treeVK+treeVY);
        int x = cx-w/2, y = cy-h/2;
        if (x < winW*.30f || x+w > winW-UIS(20) || y < UIS(70) || y+h > winH-UIS(178)) continue;
        int group = BenchArchetype(id); if (group < 0) group = 3;
        int owned = BenchHas(id), available = BenchCanBuy(id);
        BlendRectWin(x,y,x+w,y+h,owned?0x0031474Au:0x00182432u,.97f);
        FrameRectWin(x,y,x+w,y+h,id==shopNodeSel?UI_ACCENT:colors[group],available||owned?1.0f:.45f,id==shopNodeSel?2:1);
        char name[40];
        snprintf(name,sizeof(name),"%s",BenchShortName(id));
        UiTrimSmallText(name,w-6);
        if(w>=TextSmallLiteralWidth(name)+4 && h>=12)
            DrawTextSmallLiteralWin(cx-TextSmallLiteralWidth(name)/2,cy-5,name,owned?0x00BCF0C9u:0x00BCC8D5u);
        else BlendRectWin(cx-2,cy-2,cx+2,cy+2,owned?UI_OK:colors[group],1);
        if (treeVK > .5f && h >= th+18) {
            snprintf(line,sizeof(line),"%d",benchNodes[id].cost);
            DrawTextSmallWin(x+4,y+h-12,line,0x00D1B373u);
        }
    }
    const int ends[3] = {7,3,11};
    for (int group=0; group<3; group++) {
        int cx=(int)(BenchNodeWorldX(ends[group])*treeVK+treeVX);
        int cy=(int)(BenchNodeWorldY(ends[group])*treeVK+treeVY)-h/2-16;
        const char* name=SkillArchetypeName(group,0);
        if (cx > winW*.34f && cx < winW*.93f && cy > UIS(70) && cy < winH-UIS(178))
            DrawTextSmallWin(cx-DrawTextSmallWidth(name)/2,cy,name,colors[group]);
    }
    int id=shopNodeSel;if(id<0 || id>=BENCH_COUNT)id=0;
    int x=(int)(winW*.30f)+UIS(12),y=winH-UIS(166),right=winW-m-UIS(12),bottom=winH-m;
    int width=right-x-UIS(140);BlendRectWin(x,y,right,bottom,UI_BG_PANEL,1);
    char text[192];snprintf(text,sizeof(text),"%s",Tr(benchNodes[id].name,benchNodes[id].nameZh));
    while(text[0] && TextMediumLiteralWidth(text)>width){size_t n=strlen(text)-1;while(n && ((unsigned char)text[n]&0xC0)==0x80)n--;text[n]=0;}
    DrawTextMediumLiteralWin(x,y+UIS(5),text,UI_ACCENT);y+=TextMediumLiteralHeight()+UIS(10);
    snprintf(text,sizeof(text),Tr("COST %d / THREAT +%.1f","价格 %d金币 / 威胁 +%.1f"),benchNodes[id].cost,(double)BenchThreatCostOf(id));UiTrimSmallText(text,width);DrawTextSmallLiteralWin(x,y,text,UI_GOLD);y+=UIS(23);
    y=DossierText(Tr(benchNodes[id].desc,benchNodes[id].descZh),x,y,width,bottom-UIS(27),UI_INK);
    int parent=BenchPrereqId(id);
    snprintf(text,sizeof(text),Tr("FIRST: %s / %s","前置：%s / %s"),parent>=0?Tr(benchNodes[parent].name,benchNodes[parent].nameZh):Tr("START","起点"),BenchHas(id)?Tr("OWNED","已拥有"):BenchCanBuy(id)?Tr("CAN BUY","可购买"):Tr("CHECK FUNDS / PREREQUISITE","检查金币或前置"));
    UiTrimSmallText(text,width);DrawTextSmallLiteralWin(x,bottom-UIS(21),text,UI_INFO);
    int x0,y0,x1,y1;BenchBuyButtonRect(&x0,&y0,&x1,&y1);
    UiRow(x0,y0,x1,y1,BenchCanBuy(id)?UI_ROW_SEL:UI_ROW_DISABLED,1);
    DrawTextSmallLiteralWin(x0+UIS(15),y0+UIS(12),Tr("BUY","购买"),UI_ACCENT);
}


void SafehouseCardRect(int row,RECT* rect) {
    *rect=(RECT){0,0,0,0};if(row<0 || shopPage==SHP_TREE || row>=ShopRowCount())return;
    MenuGeo g=GetMenuGeo();int tx0,ty0,tx1,ty1;PageTabRect(0,&tx0,&ty0,&tx1,&ty1);
    int left=tx1+UIS(20),right=g.panel.right-UIS(18),gap=UIS(10);
    int top=g.panel.top+TextHeightWin()*2+UIS(22),bottom=g.panel.bottom-UIS(64);
    int count=shopPage==SHP_UPGRADE?RUN_UPG_COUNT:shopPage==SHP_REST?5:SHOP_OFFERS;
    if(row==count){
        if(shopPage==SHP_SHOP)right-=UIS(240);
        *rect=(RECT){left,g.panel.bottom-UIS(43),right,g.panel.bottom-UIS(7)};return;
    }
    if(shopPage==SHP_REST){
        int width=(right-left)*52/100,height=(bottom-top-gap*4)/5;
        *rect=(RECT){left,top+row*(height+gap),left+width,top+row*(height+gap)+height};return;
    }
    int columns=shopPage==SHP_SHOP?(winW>=1000?4:2):(winW>=1000?3:2);
    int rows=(count+columns-1)/columns,width=(right-left-gap*(columns-1))/columns,height=(bottom-top-gap*(rows-1))/rows;
    int col=row%columns,line=row/columns;
    *rect=(RECT){left+col*(width+gap),top+line*(height+gap),left+col*(width+gap)+width,top+line*(height+gap)+height};
}
int SafehouseCardAt(int x,int y) {
    for(int row=0;row<ShopRowCount();row++) {
        RECT r;
        SafehouseCardRect(row,&r);
        if(r.right>r.left && x>=r.left && x<=r.right && y>=r.top && y<=r.bottom)return row;
    }
    return -1;
}
static void DrawSupplyIcon(int row,RECT rect) {
    int sprite=ShopProductSprite(row);
    if(sprite>=0) { DrawDossierPortrait(sprite,rect,1);
         return;
         }
    int x=(rect.left+rect.right)/2,y=(rect.top+rect.bottom)/2;
    int size=(rect.bottom-rect.top)/3;
    if(size<4)return;
    if(shopOffer[row]==6) {
        FrameRectWin(x-size,y-size/2,x+size,y+size/2,UI_ACCENT,1.0f,UIS(2));
        BlendRectWin(x+size,y-size/5,x+size+UIS(3),y+size/5,UI_ACCENT,1.0f);
        BlendRectWin(x-size+UIS(3),y-size/2+UIS(3),x+size/2,y+size/2-UIS(3),UI_BG_SEL,1.0f);
    } else {
        BlendRectWin(x-size/3,y-size,x+size/3,y+size,UI_ACCENT,1.0f);
        BlendRectWin(x-size,y-size/3,x+size,y+size/3,UI_ACCENT,1.0f);
    }
}
static void DrawSupplyCard(int row,RECT r) {
    int th=TextHeightWin(),pad=UIS(10),y=r.top+pad;
    int width=r.right-r.left-pad*2;
    if(ShopIsDiscounted(row)) {
        char badge[48];
        snprintf(badge,sizeof(badge),Tr("SAVE %d%%","限时优惠 -%d%%"),(int)(shopDiscountPct*100.0f+.5f));
        BlendRectWin(r.left,r.top,r.right,r.top+th+UIS(6),UI_BG_SEL,1.0f);
        DossierText(badge,r.left+pad,r.top+UIS(3),width,r.top+th+UIS(6),UI_ACCENT);
        y=r.top+th+pad;
    }
    y=DossierText(ShopProductName(row),r.left+pad,y,width,r.bottom-pad,UI_INK_TITLE);
    int available=r.bottom-y-th*3-pad;
    int side=available>UIS(28)?available:UIS(24);
    if(side>width)side=width;
    RECT icon={(r.left+r.right-side)/2,y,(r.left+r.right+side)/2,y+side};
    DrawSupplyIcon(row,icon);
    y=icon.bottom+UIS(4);
    char price[64];
    snprintf(price,sizeof(price),Tr("%d RUN GOLD","%d 局内金币"),ShopPriceAt(row));
    y=DossierText(price,r.left+pad,y,width,r.bottom-th-pad,UI_GOLD);
    if(ShopIsDiscounted(row) && y+12<r.bottom-th-pad) {
        char base[48];
        snprintf(base,sizeof(base),Tr("WAS %d","原价 %d"),ShopBasePriceAt(row));
        DrawTextSmallLiteralWin(r.left+pad,y,base,UI_INK_DIM);
        BlendRectWin(r.left+pad,y+5,r.left+pad+TextSmallLiteralWidth(base),y+6,UI_INK_DIM,1.0f);
    }
    char state[72];
    float cooldown=ShopCooldownOf(row);
    if(cooldown>.05f)snprintf(state,sizeof(state),Tr("RESTOCK %ds","补货中 %d秒"),(int)ceilf(cooldown));
    else snprintf(state,sizeof(state),"%s",ShopCanBuyProduct(row)?Tr("BUY","购买"):Tr("FULL OR INSUFFICIENT GOLD","已满或金币不足"));
    UiTrimSmallText(state,width);
    DrawTextSmallLiteralWin(r.left+pad,r.bottom-pad-12,state,ShopCanBuyProduct(row)?UI_OK:UI_INK_DIM);
}
static void DrawRecoveryCard(int row,RECT r) {
    int x=r.left+UIS(8),w=r.right-r.left-UIS(16),y=r.top+UIS(6);char text[160];
    snprintf(text,sizeof(text),"%s",ShopServiceName(row));UiTrimSmallText(text,w);
    DrawTextSmallLiteralWin(x,y,text,UI_INK_TITLE);
    if(row<3)snprintf(text,sizeof(text),Tr("%d GOLD | USES %d | NO LIMIT","%d金币 | 已用%d次 | 无次数上限"),ShopServicePrice(row),ShopServiceUses(row));
    else if(row==4)snprintf(text,sizeof(text),Tr("FREE | CD %ds","免费 | 冷却%d秒"),(int)ceilf(calibCooldown));
    else snprintf(text,sizeof(text),"%s",Tr("FREE | SKILL LIMIT","免费 | 技能限制"));
    UiTrimSmallText(text,w);DrawTextSmallLiteralWin(x,y+UIS(19),text,UI_GOLD);
    if(r.bottom-y>=UIS(55)){snprintf(text,sizeof(text),"%s",ShopServiceStatus(row));UiTrimSmallText(text,w);DrawTextSmallLiteralWin(x,y+UIS(38),text,ShopServiceCanUse(row)?UI_OK:UI_INK_DIM);}
}
static void DrawRecoveryDetail(void) {
    RECT first;SafehouseCardRect(0,&first);MenuGeo g=GetMenuGeo();
    int x=first.right+UIS(20),right=g.panel.right-UIS(28),bottom=g.panel.bottom-UIS(65),y=first.top;
    int row=shopRow<5?shopRow:0,w=right-x;char text[160];
    UiPanel(x-UIS(8),y,right+UIS(8),bottom,UI_PANEL_NORMAL);y+=UIS(12);
    y=DossierText(ShopServiceName(row),x,y,w,bottom,UI_INK_TITLE)+UIS(18);
    if(row<3){float values[3]={sanity,battery,stamina};float maximum=row==0?SanityMax():row==1?(float)BatteryMax():StaminaMax();
        snprintf(text,sizeof(text),Tr("CURRENT %.0f / %.0f","当前 %.0f / %.0f"),(double)values[row],(double)maximum);
        y=DossierText(text,x,y,w,bottom,UI_INK)+UIS(10);
        UiBar(x,y,w,UIS(8),values[row]/fmaxf(1,maximum),UI_BAR_NORMAL,UI_ACCENT);y+=UIS(20);
        snprintf(text,sizeof(text),Tr("RESTORE +%.0f / PRICE %d GOLD","本次恢复 +%.0f / 当前价格 %d金币"),(double)ShopServiceGain(row),ShopServicePrice(row));
        y=DossierText(text,x,y,w,bottom,UI_GOLD)+UIS(12);
        y=DossierText(row==0?Tr("Unlimited recovery. Every use doubles this service price for the rest of the run.","无次数限制。每次使用后，本项服务本局价格翻倍。"):Tr("No cooldown or use limit; prices double per use and persist between floors.","无冷却、无次数限制；每次价格翻倍，跨层保留涨价。"),x,y,w,bottom,UI_INK_DIM)+UIS(14);
    }else y=DossierText(row==3?Tr("Skip this non-boss floor. Requires the permanent SHORTCUT skill; once per run.","需要永久捷径技能，跳过当前非首领层，每局限一次。"):Tr("Each QWER hit immediately restores sanity; combos increase healing. Five misses fail. Finish for gold. Sanity/battery drain pauses during play.","每次QWER命中即时回理智，连击提高恢复，断连重置加成。失误5次失败，完成另奖金币；游玩期间不耗理智和电量。"),x,y,w,bottom,UI_INK)+UIS(15);
    DossierText(ShopServiceStatus(row),x,y,w,bottom-UIS(10),ShopServiceCanUse(row)?UI_OK:UI_INFO);
}
static void DrawUpgradeCard(int row,RECT r) {
    int pad=UIS(10),width=r.right-r.left-pad*2,y=r.top+pad;
    y=DossierText(Tr(RunUpgName(row),RunUpgNameZh(row)),r.left+pad,y,width,r.bottom-pad,UI_INK_TITLE);
    int level=RunUpgLevel(row),pip=UIS(10);
    for(int i=0;i<RUN_UPG_MAX;i++) {
        int x=r.left+pad+i*(pip+UIS(5));
        if(i<level)BlendRectWin(x,y,x+pip,y+pip,UI_ACCENT,1.0f);
        else FrameRectWin(x,y,x+pip,y+pip,UI_EDGE,.95f,1);
    }
    y+=pip+UIS(8);
    static const char* en[RUN_UPG_COUNT]={"+2 capacity / rank","-10% grab loss / rank","-15% sprint cost / rank","-15% light drain / rank","-1s cooldown / rank (base min 5s)","-2 points map drain / rank"};
    static const char* zh[RUN_UPG_COUNT]={"每级携带上限 +2","每级抓捕理智损失 -10%","每级冲刺耗体 -15%","每级手电耗电 -15%","每级基础冷却 -1秒，最低5秒","每级地图耗电 -2个百分点"};
    char effect[128];
    snprintf(effect,sizeof(effect),"%s",Tr(en[row],zh[row]));
    UiTrimSmallText(effect,width);
    if(y+12<r.bottom-pad-12)DrawTextSmallLiteralWin(r.left+pad,y,effect,UI_INK_DIM);
    char price[64];
    int cost=RunUpgCost(row);
    if(cost<0)snprintf(price,sizeof(price),"%s",Tr("MAX LEVEL","已满级"));
    else snprintf(price,sizeof(price),Tr("LV %d/%d / %d GOLD","等级 %d/%d / %d 金币"),level,RUN_UPG_MAX,cost);
    UiTrimSmallText(price,width);
    DrawTextSmallLiteralWin(r.left+pad,r.bottom-pad-12,price,cost>=0&&WalletCanPayRun(cost)?UI_GOLD:UI_INK_DIM);
}
static void DrawSafehousePage(void) {
    MenuGeo g=GetMenuGeo();
    int th=TextHeightWin();
    UiPanel9(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,0);
    DrawPageTabs();
    int x0,y0,x1,y1;
    ShopBackButtonRect(&x0,&y0,&x1,&y1);
    UiRow(x0,y0,x1,y1,UI_ROW_NORMAL,1.0f);
    DrawTextCenterWin((x0+x1)/2,(y0+y1-th)/2,Tr("BACK","返回"),UI_ACCENT);
    char balance[96];
    snprintf(balance,sizeof(balance),Tr("RUN GOLD %d","局内金币 %d"),WalletRun());
    DrawTextWin(g.panel.right-UIS(18)-TextWidthWin(balance),g.panel.top+UIS(10),balance,UI_ACCENT);
    if(shopPage==SHP_SHOP){RECT r;char refresh[96];ShopRefreshButtonRect(&r);
        UiRow(r.left,r.top,r.right,r.bottom,UI_ROW_NORMAL,1);
        if(ShopRefreshCooldown()>0)snprintf(refresh,sizeof(refresh),Tr("RESTOCK IN %ds  [F5]","刷新剩余 %d秒 [F5]"),(int)ceilf(ShopRefreshCooldown()));
        else snprintf(refresh,sizeof(refresh),"%s",Tr("REFRESH STOCK [F5]","刷新补给 [F5]"));
        UiTrimSmallText(refresh,r.right-r.left-UIS(12));
        DrawTextSmallLiteralWin(r.left+UIS(6),r.top+UIS(10),refresh,ShopRefreshCooldown()>0?UI_INK_DIM:UI_ACCENT);
    }
    int count=shopPage==SHP_UPGRADE?RUN_UPG_COUNT:(shopPage==SHP_REST?5:4);
    for(int row=0;row<ShopRowCount();row++) {
        RECT r;
        SafehouseCardRect(row,&r);
        if(r.right<=r.left || r.bottom<=r.top)continue;
        UiRow(r.left,r.top,r.right,r.bottom,shopRow==row?UI_ROW_SEL:UI_ROW_NORMAL,1.0f);
        FrameRectWin(r.left,r.top,r.right,r.bottom,shopRow==row?UI_EDGE_HOT:UI_EDGE,.95f,shopRow==row?2:1);
        if(row>=count) {
            const char* label=Tr("LEAVE SAFE HOUSE","离开安全屋");
            char footer[160];
            snprintf(footer,sizeof(footer),"%s",label);
            ClampRowText(footer,r.right-r.left-UIS(12));
            DrawTextCenterWin((r.left+r.right)/2,(r.top+r.bottom-th)/2,footer,UI_INFO);
        } else if(shopPage==SHP_SHOP)DrawSupplyCard(row,r);
        else if(shopPage==SHP_REST)DrawRecoveryCard(row,r);
        else DrawUpgradeCard(row,r);
    }
    if(shopPage==SHP_REST)DrawRecoveryDetail();
}
void BankBackRect(RECT* rect) { *rect=(RECT){UIS(24),UIS(20),UIS(130),UIS(58)};
     }
void BankSliderRect(int slider,RECT* rect) {
    int top=winH*34/100+slider*(winH*24/100);
    *rect=(RECT){UIS(35),top,winW*55/100,top+UIS(18)};
}
int BankSliderAt(int x,int y) {
    for(int slider=0;slider<2;slider++) { RECT r;
        BankSliderRect(slider,&r);
    if(x>=r.left-UIS(10)&&x<=r.right+UIS(10)&&y>=r.top-UIS(16)&&y<=r.bottom+UIS(16))return slider;
         }
    return -1;
}
void BankAdjustSlider(int slider,int delta) {
    int* value=slider==0?&bankCarryPct:&bankRiskPlan;
    int limit=slider==0?BANK_CARRY_STEPS-1:3;
    *value+=delta;
    if(*value<0)*value=0;
    if(*value>limit)*value=limit;
    uiDirty=1;
}
void BankSetSliderFromPointer(int slider,int x) {
    RECT r;
    BankSliderRect(slider,&r);
    if(r.right<=r.left)return;
    float ratio=(float)(x-r.left)/(r.right-r.left);
    if(ratio<0.0f)ratio=0.0f;
    if(ratio>1.0f)ratio=1.0f;
    int limit=slider==0?BANK_CARRY_STEPS-1:3;
    if(slider==0)bankCarryPct=(int)(ratio*limit+.5f);
    else bankRiskPlan=(int)(ratio*limit+.5f);
    uiDirty=1;
}
static void DrawBankPage(void) {
    BlendRectWin(0,0,winW,winH,UI_BG_PANEL,1);
    RECT back;BankBackRect(&back);UiRow(back.left,back.top,back.right,back.bottom,UI_ROW_NORMAL,1);
    DrawTextSmallLiteralWin(back.left+UIS(15),back.top+UIS(12),Tr("BACK","返回"),UI_ACCENT);
    UiTitle(winW/2,winH*14/100,Tr("EXPEDITION CONTROL","行动风险中枢"));
    char text[200];int pad=UIS(35);
    snprintf(text,sizeof(text),Tr("BANK: %d GOLD","银行存款：%d 金币"),WalletBank());
    DrawTextSmallLiteralWin(pad,winH*23/100,text,UI_ACCENT);
    for(int slider=0;slider<2;slider++) {
        RECT r;BankSliderRect(slider,&r);
        if(slider==0)snprintf(text,sizeof(text),Tr("CARRY %d%% / %d GOLD","带入 %d%% / %d 金币"),bankCarryPct*BANK_STEP_PCT,BankCarryAmount());
        else snprintf(text,sizeof(text),Tr("CONTRACT: %s","风险契约：%s"),BankRiskName(bankRiskPlan));
        UiTrimSmallText(text,r.right-r.left);DrawTextSmallLiteralWin(r.left,r.top-UIS(30),text,UI_INK_TITLE);
        int limit=slider?3:BANK_CARRY_STEPS-1,value=slider?bankRiskPlan:bankCarryPct;
        int handle=r.left+(r.right-r.left)*value/limit;
        BlendRectWin(r.left,r.top,r.right,r.bottom,UI_BG_ROW,1);
        BlendRectWin(r.left,r.top,handle,r.bottom,UI_BG_SEL,1);
        FrameRectWin(r.left,r.top,r.right,r.bottom,slider==BankSelectedControl()?UI_EDGE_HOT:UI_EDGE,1,1);
        BlendRectWin(handle-UIS(4),r.top-UIS(4),handle+UIS(4),r.bottom+UIS(4),UI_ACCENT,1);
        DossierText(slider?Tr("Higher risk raises awareness and extraction rewards.","提高风险会增加感知压力，也提高撤离收益。"):Tr("This amount leaves the bank only when a normal run starts.","只在普通对局开始时取出，不会因拖动立即扣款。"),r.left,r.bottom+UIS(20),r.right-r.left,winH*82/100,UI_INK_DIM);
    }
    int x=winW*62/100,right=winW-pad,y=winH*26/100,bottom=winH*81/100;
    UiPanel(x-UIS(14),y-UIS(12),right+UIS(10),bottom,UI_PANEL_NORMAL);
    y=DossierText(Tr("NEXT RUN","下局预测"),x,y,right-x,bottom,UI_INK_TITLE)+UIS(12);
    snprintf(text,sizeof(text),Tr("GOLD PRESSURE %d / 15","金币压力 %d / 15"),BankPreviewTier());
    y=DossierText(text,x,y,right-x,bottom,UI_ACCENT)+UIS(12);
    snprintf(text,sizeof(text),Tr("AWARENESS x%.2f","感知倍率 x%.2f"),(double)BankPreviewHearing());
    y=DossierText(text,x,y,right-x,bottom,UI_INK)+UIS(12);
    snprintf(text,sizeof(text),Tr("EXIT REWARD x%.2f","撤离收益 x%.2f"),(double)BankRiskExitMul(bankRiskPlan));
    y=DossierText(text,x,y,right-x,bottom,UI_INK)+UIS(12);
    snprintf(text,sizeof(text),Tr("DEFEAT REFUND %d%%","失败本金返还 %d%%"),(int)(BANK_DEATH_REFUND*100+.5f));
    DossierText(text,x,y,right-x,bottom,UI_INK_DIM);
    DossierText(Tr("Drag sliders / Up-Down select / Left-Right adjust / Enter save / Esc back","拖动滑条；上下选择，左右调整；回车保存，ESC返回"),pad,winH*87/100,winW-pad*2,winH-UIS(10),UI_INK_DIM);
}


/* 对外唯一入口：顺序与 RenderFrame 里原来那两行一致（面板 → 文字）。 */
void UiDrawMenuPages(void) {
    DrawMenuPanels();
    DrawMenuText();
}

static const char* MainMenuVisibleLabel(int index) {
    static const char* primaryEn[]={"Continue","New Run","Skill Web","Threat Bank","Bestiary","Archives / Modes","Training Camp","Quit"};
    static const char* primaryZh[]={"继续游戏","开始新局","局外技能网","威胁银行","怪物图鉴","档案与模式","自由训练营","退出游戏"};
    static const char* moreEn[]={"Achievements","Help / Skill Index","Settings","Campaign","Daily Run","Endless","Back"};
    static const char* moreZh[]={"成就档案","帮助与技能索引","设置","战役","每日挑战","无尽模式","返回"};
    if(!mainMenuMore){index+=hasSave?0:1;return Tr(primaryEn[index],primaryZh[index]);}
    if(!EndlessUnlocked() && index==5)index=6;
    return Tr(moreEn[index],moreZh[index]);
}
int MainMenuLegacySelection(void) {
    int index=menuSelection,base=hasSave?1:0,eo=EndlessUnlocked()?1:0;
    if(index<0 || index>=MainMenuCount()){menuSelection=0;index=0;}
    if(!mainMenuMore){
        index+=hasSave?0:1;
        if(index==5){mainMenuMore=1;menuSelection=0;uiDirty=1;return -1;}
        if(index==0)return 0;
        if(index==1)return base;
        if(index==2)return base+1+eo;
        if(index==3)return base+2+eo;
        if(index==4)return base+4+eo;
        if(index==6)return base+9+eo;
        return base+10+eo;
    }
    if(index==(EndlessUnlocked()?6:5)){mainMenuMore=0;menuSelection=0;uiDirty=1;return -1;}
    static const int offsets[5]={3,5,6,7,8};
    return index<5?base+offsets[index]+eo:base+1;
}

void ShopRefreshButtonRect(RECT* rect) {
    MenuGeo g=GetMenuGeo();
    *rect=(RECT){g.panel.right-UIS(245),g.panel.bottom-UIS(43),g.panel.right-UIS(20),g.panel.bottom-UIS(7)};
}

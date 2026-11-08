/* src/domain/ui/ui_menu.c —— 主菜单页（含改造树 / 银行页 / 校准条）
 *
 * 第 2 轮（2026-11-08）从 ui.c 原样搬过来，逻辑一行没改。
 * 契约：include/ui_menu.h
 */
#include "common.h"
#include "render.h"
#include "game.h"
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
#include "bestiary.h"
#include "save.h"
#include "daily.h"
#include "campaign.h"
#include "state.h"

extern int safehouseHoverRow;   /* 定义在 game.c */
#include "wallet.h"

/* 菜单内部互相引用，原文件里靠"先定义后使用"的巧合；这里显式前置声明 */
static int MainMenuRowCount(void);
static const char* MenuLabel(int i);
static void DrawMenuText(void);
static void DrawBenchTree(MenuGeo g);
static void DrawBankPage(void);
static void DrawTextInRowLeft(const char* text, RECT row, unsigned int color);
static void DrawCalibStrip(MenuGeo g);

/* 分支名表：原来在 ui.c 里是 static，菜单搬出来后自己留一份。
   只是显示名，重复定义比为了它开一个新接口更划算。 */
static const char* kBranchName[7] = { "SILENCE", "ECONOMY", "CONCEAL",
                                      "SHAPE", "RISK", "CROSS", "ORIGIN" };
static const char* kBranchNameZh[7] = { "沉寂", "节流", "藏匿",
                                        "塑形", "冒险", "跨支", "起点" };

void MainMenuCompute(MainMenuLayout* L) {
    int TH = TextHeightWin();
    if (TH < 8) TH = 8;
    memset(L, 0, sizeof(*L));

    int n = MainMenuRowCount();
    if (n < 1) n = 1;
    L->count = n;
    L->padX = TH;

    int panelW = TH * 18;
    if (panelW > winW - TH) panelW = winW - TH;
    if (panelW < TH * 12) panelW = TH * 12;

    int titleH  = (TH * 24) / 10;  /* CANITY + 副标题，留够上方呼吸位 */
    int gapTop  = (TH * 8) / 10;
    int gapRows = (TH * 11) / 10;  /* 副标题和第一行之间别贴在一起 */
    int gapFoot = (TH * 6) / 10;
    int footH   = (TH * 3) / 2;    /* 页脚：一条细线 + 一行字 */

    int rowH = (TH * 135) / 100;
    int bottomPad = (TH * 6) / 10;      /* 页脚和面板下边框之间的呼吸位 */
    int panelH = titleH + gapTop + gapRows + n * rowH + gapFoot + footH + bottomPad;
    int maxH = winH - TH;

    if (panelH > maxH) {
        /* 行数多（10 行）时压行高，文字本身不缩 */
        int fixed = titleH + gapTop + gapRows + gapFoot + footH + bottomPad;
        int avail = maxH - fixed;
        rowH = (n > 0) ? avail / n : rowH;
        if (rowH < (TH * 11) / 10) rowH = (TH * 11) / 10;
        panelH = fixed + n * rowH;
        if (panelH > maxH) panelH = maxH;
    }
    L->rowH = rowH;

    int left = (winW - panelW) / 2;
    L->panel.left   = left;
    L->panel.right  = left + panelW;
    L->panel.top    = (winH - panelH) / 2;
    L->panel.bottom = L->panel.top + panelH;

    L->titleY = L->panel.top + gapTop;
    L->subY   = L->titleY + TH + TH / 3;
    L->firstY = L->titleY + titleH;

    L->footRuleY = L->panel.bottom - bottomPad - footH;
    L->footY     = L->footRuleY + (footH - TH) / 2;
}

static int MainMenuRowCount(void) {
    return MainMenuCount();
}

int MainMenuCount(void) {
    /* v6 **行数必须等于标签表长度** ——
       加「Bank」一行后表变了但这里没跟上，导致最后一行 Quit
       永远画不出来、也选不到，菜单里没有任何退出游戏的途径。
       表长度：有档 12 / 无档 11（无无尽）；各 +1（有无尽）。 */
    int endlessUnlocked = EndlessUnlocked();
    if (endlessUnlocked) {
        return hasSave ? 13 : 12;
    }
    return hasSave ? 12 : 11;
}

static int MenuItemIndex(void) {
    if (currentState == STATE_SETTINGS) return settingsSelection;
    if (currentState == STATE_HUB) return hubSelection;
    if (currentState == STATE_SKILL_PICK) return skillPickSel;
    if (currentState == STATE_WEAPON_PICK) return weaponPickSel;
    if (currentState == STATE_SAFEHOUSE) return shopRow;
    if (currentState == STATE_ACHIEVEMENTS) return achSelection;
    if (currentState == STATE_BESTIARY) return bestiarySel;
    return menuSelection;
}

static const char* MenuLabel(int i) {
    static char buf[128];
    switch (currentState) {
        case STATE_MAIN_MENU: {
            static const char* withSave[] = {"Continue", "New Run", "Hub", "Bank", "Achievements", "Bestiary", "Help", "Settings", "Campaign", "Daily Run", "Tutorial", "Quit"};
            /* v2.0 中文菜单 */
            static const char* withSaveZh[] = {"继续", "新游戏", "大厅", "银行", "成就", "图鉴", "帮助", "设置", "战役", "每日挑战", "新手教程", "退出"};
            static const char* noSaveZh[]   = {"新游戏", "大厅", "银行", "成就", "图鉴", "帮助", "设置", "战役", "每日挑战", "新手教程", "退出"};
            static const char* withEndlessZh[]   = {"继续", "新游戏", "无尽", "大厅", "银行", "成就", "图鉴", "帮助", "设置", "战役", "每日挑战", "新手教程", "退出"};
            static const char* noSaveEndlessZh[] = {"新游戏", "无尽", "大厅", "银行", "成就", "图鉴", "帮助", "设置", "战役", "每日挑战", "新手教程", "退出"};
            static const char* noSave[] = {"New Run", "Hub", "Bank", "Achievements", "Bestiary", "Help", "Settings", "Campaign", "Daily Run", "Tutorial", "Quit"};
            static const char* withEndless[] = {"Continue", "New Run", "Endless", "Hub", "Bank", "Achievements", "Bestiary", "Help", "Settings", "Campaign", "Daily Run", "Tutorial", "Quit"};
            static const char* noSaveEndless[] = {"New Run", "Endless", "Hub", "Bank", "Achievements", "Bestiary", "Help", "Settings", "Campaign", "Daily Run", "Tutorial", "Quit"};
            /* Endless 解锁条件：通关过第 10 层 */
            int endlessUnlocked = EndlessUnlocked();
            int zh = (g_lang == LANG_ZH);
            if (endlessUnlocked) {
                if (zh) return hasSave ? withEndlessZh[i] : noSaveEndlessZh[i];
                return hasSave ? withEndless[i] : noSaveEndless[i];
            }
            if (zh) return hasSave ? withSaveZh[i] : noSaveZh[i];
            return hasSave ? withSave[i] : noSave[i];
        }
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
            sprintf(wb, "%s   %s -%.0f   LV%d", Tr(weaponDefs[i].dispName, weaponDefs[i].nameZh),
                    resName[weaponDefs[i].resource], weaponDefs[i].cost,
                    weaponRank[i]);
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
                extern int bankCarryPct, bankGold;
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
            extern const AchDef kAchDefs[ACH_COUNT];
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
        FrameRectWin(g.panel.left, g.panel.top, g.panel.right, g.panel.bottom, PAL_PANEL, 0.85f, UIS(2));
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
                          Tr("BACK", "返回"), 0x00E8C040u);
    }

    /* v2.8：安全屋的**所有**页都要显示页签（不只改造树那一页） */
    if (currentState == STATE_SAFEHOUSE) {
        DrawPageTabs();
        /* v3.0：余额移到**页签下面**独立一行 ——
           5 个页签变宽后右端会压到余额（截图里 GOLD 被 UPGRADE 盖住）。 */
        {
            static char sb2[64];
            { extern int carryGold; sprintf(sb2, "GOLD %d", carryGold); }   /* v6 第 40 轮：同 1567 行，双倍 */
            int bx0, by0, bx1, by1;
            PageTabRect(0, &bx0, &by0, &bx1, &by1);
            /* v6 **余额必须锚在面板右边缘，不是窗口右边缘** ——
               用 winW 算的话，窗口比面板宽时 GOLD 会跑到面板外面，
               看起来就是「字幕漂移到屏幕最右边」。 */
            DrawTextWin(g.panel.right - UIS(20) - TextWidthWin(sb2),
                        g.panel.top + UIS(12), sb2, 0x00E8C040u);
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
        extern double g_timeSec;
        extern int isFullscreen;

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
                DrawTextWin(g.panel.right - hw, hy, hint, 0x00E8C040u);
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
                extern void UiSectionRuleSpan(int, int, int);
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
               原来 y - TH/2，主菜单第 10 行「Quit」正好被这条线横切
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
    int TH = TextHeightWin();
    int n = M->count;
    int sel = menuSelection;
    if (sel < 0 || sel >= n) sel = 0;
    int cx = (M->panel.left + M->panel.right) / 2;

    /* 标题：故意用双空格拉开字距 —— 点阵字没有字距控制，这是唯一的办法 */
    UiTitle(cx, M->titleY, "C  A  N  I  T  Y");
    UiHint(cx, M->subY, "RAYCASTER HORROR");

    int split = n - 4;                 /* 系统组（Help/Settings/Training/Quit）之前分组 */
    for (int i = 0; i < n; i++) {
        int top = M->firstY + i * M->rowH;
        int y = top + (M->rowH - TH) / 2;
        int left = M->panel.left + M->padX;
        int right = M->panel.right - M->padX;

        if (split > 0 && i == split) {
            /* 画在行与行之间的空隙正中：上一行文字底 → 本行文字顶 的中点就是 top。
               （用 top - rowH/6 会直接切到上一行文字的底边，第一版就是这样） */
            int lth = TH / 12; if (lth < 2) lth = 2;
            int ly = top - lth / 2;
            BlendRectWin(left, ly, right, ly + lth, 0x00605A66u, 0.95f);
        }

        int isSel = (i == sel);
        if (isSel) {
            /* v2.2：底纹 + 左竖线（光标另画在文字那一行，避免行高不一致时错位） */
            int rt = top + M->rowH / 10;
            int rb = top + M->rowH - M->rowH / 10;
            BlendRectWin(left - TH / 2, rt, right + TH / 2, rb, UI_ACCENT, 0.10f);
            BlendRectWin(left - TH / 2, rt, left - TH / 2 + TH / 8, rb, UI_ACCENT, 0.95f);
            /* v2.2：主菜单不再叠三角光标 —— 行高与文字基线对不齐时会错行，
               左竖线 + 琥珀文字已经足够表达选中。 */
        }

        char num[12];   /* v2.2：sprintf %d 最多 11 字节 */
        sprintf(num, "%d", i + 1);
        DrawTextWin(left, y, num, isSel ? 0x00FFE080u : 0x00969AA6u);

        DrawTextWin(left + TH * 2, y, MenuLabel(i),
                    isSel ? UI_ACCENT : UI_INK);

        if (isSel) {
            const char* arrow = ">";
            DrawTextWin(right - TextWidthWin(arrow), y, arrow, 0x0060E070u);
        }
    }

    /* 页脚：细线 + 一行数据，全部**在面板里面**（原来露在面板外，很难看） */
    {
        int ft = TH / 10; if (ft < 2) ft = 2;
        /* 左右各内缩半行：面板四角那四条装饰短条会压到页脚首尾字符 */
        int fx0 = M->panel.left + M->padX + TH / 2;
        int fx1 = M->panel.right - M->padX - TH / 2;
        BlendRectWin(fx0, M->footRuleY, fx1, M->footRuleY + ft, 0x00502A32u, 0.95f);
    }

    /* 页脚：左边货币、右边最佳层数；
       版本号挪到屏幕右下角（面板内塞三项，800x600 下必然撞车） */
    char lstr[48], rstr[48];
    /* v3.4：**左边不再显示 GOLD** —— 用户要求「主界面最底部那个蓝色的
       GOLD 0 删掉，最底相当于只留一个最佳 level」。
       金币归属挪到了新的 BANK 页和游戏内 HUD，主菜单底部只留 BEST LEVEL。 */
    lstr[0] = '\0';
    sprintf(rstr, "BEST LEVEL %d", bestLevel);
    int lx = M->panel.left + M->padX + TH / 2;
    int rx = M->panel.right - M->padX - TH / 2;

    /* v3.3：**页脚两项会撞车** ——
       用户截图：金币涨到 7000 之后 "GOLD 7000" 变宽，
       和右对齐的 "BEST LEVEL 0" 直接叠在一起。
       原来完全没做宽度检查。现在按可用宽度逐级缩短右侧文案：
         ① BEST LEVEL 12  （完整）
         ② BEST 12        （去掉 LEVEL）
         ③ Lv12           （极窄窗口）
       仍然放不下就左右各让一半，保证至少一个字的间隙。 */
    {
        int avail = rx - lx;
        int gapMin = TH;                     /* 最小间隙 = 一个字高 */
        int wl = (lstr[0] ? TextWidthWin(lstr) : 0);
        int wr = TextWidthWin(rstr);
        if (wl + wr + gapMin > avail) {
            sprintf(rstr, "BEST %d", bestLevel);
            wr = TextWidthWin(rstr);
        }
        if (wl + wr + gapMin > avail) {
            sprintf(rstr, "Lv%d", bestLevel);
            wr = TextWidthWin(rstr);
        }
        /* 极端情况：左侧也收一点，保证不重叠 */
        if (wl + wr + gapMin > avail && wl > avail / 2) {
            sprintf(lstr, "%d", carryGold);
            wl = TextWidthWin(lstr);
        }
        DrawTextWin(lx, M->footY, lstr, 0x0060D0FFu);
        DrawTextWin(rx - wr, M->footY, rstr, 0x00808088u);
    }

    DrawTextWin(winW - TextWidthWin(CANITY_VERSION_STR) - UIS(8),
                winH - TextHeightWin() - UIS(4), CANITY_VERSION_STR, 0x007C8088u);
}

static void DrawMenuText(void) {
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

    unsigned int titleColor = (currentState == STATE_MAIN_MENU) ? 0x00BE1414u : 0x00D23C3Cu;
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

    /* v1.8：图鉴底部两行 —— 威胁 + 对策（未解锁则提示去遇到它） */
    if (currentState == STATE_BESTIARY && sel >= 0 && sel < BESTIARY_COUNT) {
        char l1[192], l2[192];
        MenuGeo bg = GetMenuGeo();
        int avail = (bg.panel.right - bg.panel.left) - UIS(56);
        /* ===== v6 **三阶揭示** =====
           见过 1 次 → 名字 + 危险度
           逃 3 次   → + 行为 + 实时数值
           逃 5 次   → + 对策 + 你的战绩

           数值来自 bestiary[sel].getSpeed() 等函数指针，
           读的是真实常量 —— 改怪物参数，这里自动变。 */
        if (!BestiaryIsSeen(sel)) {
            /* v6 第 ㉘ 轮：**这一行画到面板外面去了**
               其它分支都锚在 bg.panel.bottom，只有这条用 winH ——
               实测截图：「UNKNOWN...」压在面板下边框之外的黑色背景上。 */
            sprintf(l1, "UNKNOWN. ENCOUNTER IT IN A RUN TO UNLOCK.");
            ClampRowText(l1, avail);
            /* ==================================================================
             * v7 第 64 轮：**这一行删掉**
             * ==================================================================
             * 实测截图：面板只有 6 行高度，第 6 个条目恰好画在这个位置，
             * 于是变成「UNKNOWN. ENCOUNTE??? [LOCKED]UN TO UNLOCK.」。
             * 上面试过 -52 / -88 两个位置，都会和列表或被裁掉。
             *
             * 而 `??? [LOCKED]` 这五个字本身就把话说清楚了 ——
             * 一条放不下的说明不如不放。
             * ================================================================ */
            (void)l1;
        } else {
            int stage = BestiaryStage(sel);
            int danger = BestiaryDanger(sel);

            /* 第一行：危险度 + 实时数值（逃 3 次后才显示数值） */
            if (stage >= BST_BEHAVED) {
                float sp = bestiary[sel].getSpeed ? bestiary[sel].getSpeed() : 0.0f;
                float hr = bestiary[sel].getHear  ? bestiary[sel].getHear()  : 0.0f;
                float ch = bestiary[sel].getChase ? bestiary[sel].getChase() : 0.0f;
                sprintf(l1, "DANGER %d/5   SPEED %.2f   HEAR %.1f   CHASE %.1fs",
                        danger, (double)sp, (double)hr, (double)ch);
            } else {
                sprintf(l1, "DANGER %d/5   (%s TO REVEAL MORE)",
                        danger, "3 FLOORS / 2 ESCAPES");
            }

            /* 第二行：行为（逃 3 次）或对策（逃 5 次） */
            if (stage >= BST_COUNTER) {
                sprintf(l2, "COUNTER: %s",
                        (g_lang == LANG_ZH) ? bestiary[sel].counterZh : bestiary[sel].counterEn);
            } else if (stage >= BST_BEHAVED) {
                sprintf(l2, "%s",
                        (g_lang == LANG_ZH) ? bestiary[sel].behaviorZh : bestiary[sel].behaviorEn);
            } else {
                sprintf(l2, "%s", "???");
            }
            ClampRowText(l1, avail);
            ClampRowText(l2, avail);
            /* v3.3 **锚到面板底边，不是窗口底边** ——
               试玩实测：「面板下边框在 y≈670，THREAT/COUNTER 两行却画在
               y≈719 / 751，第二行贴到窗口最底边被切」。
               原来用的是 winH，而面板比窗口矮。 */
            int footY = bg.panel.bottom - UIS(52);
            DrawTextCenterWin(winW / 2, footY, l1, 0x00E0A050u);
            DrawTextCenterWin(winW / 2, footY + UIS(24), l2, 0x0060C0FFu);
        }
    }

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
            if (i == 1) { extern int lastRunCaught; snprintf(line,sizeof(line),"LEVEL %d  TIME %d:%02d  CAUGHT %d",currentLevel,mins,secs,lastRunCaught); }
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
                extern int CatchCountThisRun(void);
                extern int lastRunGold,lastRunKept,lastRunCaught;
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
                priceCol = 0x00E8C040u;
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
        extern int shopRefreshUsed;
        char hint[80];snprintf(hint,sizeof(hint),"R  REFRESH %d/1",shopRefreshUsed?0:1);
        DrawTextInRowLeft(hint,RowRect(g,SHOP_OFFERS+1),PAL_CYAN);
    }
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
        sprintf(line, "%s", Tr("CALIBRATION - PRESS SPACE TO LOCK",
                               "校准中 - 按空格锁定"));
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
        else if (calibResult == 2) { msg = Tr("CALIBRATED", "成功"); col = 0x0080E080u; }
        else { msg = Tr("MISSED", "失手"); col = 0x00FF6040u; }
        DrawTextWin(bx + barW - TextWidthWin(msg), by - TextHeightWin() - UIS(4), msg, col);
    }
}

static void DrawBenchTree(MenuGeo g) {
    (void)g;
    int th = TextHeightWin();

    /* v2.9：第一次进树页自动居中 —— 原来 treeVX/VY 默认 0，
       第一列框 -35..115，左半截在屏外（截图可见）。 */
    {
        static int treeCentred = 0;
        if (!treeCentred) {
            treeCentred = 1;
            BenchResetView();
        }
        if (currentState != STATE_SAFEHOUSE) treeCentred = 0;
    }
    int M = UIS(16);

    /* ---- 背景 ---- */
    BlendRectWin(M, M, winW - M, winH - M, 0x000A0E14, 0.97f);
    FrameRectWin(M, M, winW - M, winH - M, 0x00C8B060u, 0.9f, 1);

    /* ---- 顶部：页签 + 余额 ---- */
    DrawPageTabs();
    {
        static char sb[64];
        /* v6 第 ㊵ 轮：**这里原来显示双倍金币**
         * 第 ① 轮货币统一时，`carryGold + shards` 被机械替换成
         * `carryGold + carryGold` —— 界面显示的是真实值的两倍。
         * 玩家看到 GOLD 2000，实际只有 1000，进商店什么都买不起。 */
        { extern int carryGold; sprintf(sb, "GOLD %d", carryGold); }
        DrawTextWin(winW - M - UIS(16) - TextWidthWin(sb), UIS(14), sb, 0x00E8C040u);
        /* 缩放指示 */
        sprintf(sb, "%.1fx", treeVK);
        DrawTextWin(winW - M - UIS(16) - TextWidthWin(sb), UIS(14) + th + UIS(6), sb, 0x009AA8A8u);
    }

    /* ---- 节点（世界坐标 -> 屏幕） ---- */
    /* v2.9：框宽**从世界间距推**，不再从文字高推 ——
       从文字高推的话，窗口一大 TH 变大，框也跟着变宽，
       最后宽到和列间距一样，相邻节点就贴在一起（截图里 MAGAZI|HIDE）。
       现在：框宽 = 列间距 - 固定缝隙，永远留得住缝。 */
    int boxW = (int)((BENCH_NODE_WX - 46.0f) * treeVK);
    int boxH = (int)((BENCH_NODE_WY - 52.0f) * treeVK);
    if (boxW < 26) boxW = 26;
    if (boxH < 16) boxH = 16;
    /* 但高度至少要放得下一行字 */
    if (boxH < th) boxH = th;

    for (int id = 0; id < BENCH_COUNT; id++) {
        float wx = BenchNodeWorldX(id);
        float wy = BenchNodeWorldY(id);
        /* v2.9：screen = world * k + v（和思维导图一致） */
        int cx = (int)(wx * treeVK + treeVX);
        int cy = (int)(wy * treeVK + treeVY);
        int bx = cx - boxW / 2, by = cy - boxH / 2;

        /* 屏幕外的直接跳过（节点多了也不掉帧） */
        if (bx < winW*30/100 || by < UIS(70) || bx+boxW>winW-UIS(20) || by+boxH>winH-UIS(135)) continue;

        int owned = BenchHas(id);
        int prereq = BenchPrereqOk(id);
        int affordable = (carryGold >= benchNodes[id].cost);
        int sel = (id == shopNodeSel);

        /* 同列竖连线（画在节点下面） */
        if (benchNodes[id].row>0) {
            int pid = id>=16 ? (id-16)*4+2 : id-1;
            int pcx = (int)(BenchNodeWorldX(pid) * treeVK + treeVX);
            int pcy = (int)(BenchNodeWorldY(pid) * treeVK + treeVY);
            int upOwned = BenchHas(pid);
            int y0 = pcy + boxH / 2, y1 = cy - boxH / 2;
            int steps = (y1 - y0) / 6;
            for (int s = 0; s <= steps; s++) {
                int yy = y0 + s * 6;
                if (!upOwned && (s % 2)) continue;      /* 未解锁 = 虚线 */
                BlendRectWin(pcx - 1, yy, pcx + 2, yy + 4,
                             upOwned ? 0x00C8A040u : 0x00404040u, upOwned ? 0.95f : 0.7f);
            }
        }

        unsigned int fill, frame, textc;
        if (owned)           { fill = 0x00C8A040u; frame = 0x00FFE070u; textc = 0x00100C08u; }
        else if (!prereq)    { fill = 0x00161412u; frame = 0x00383028u; textc = 0x00504840u; }
        else if (affordable) { fill = 0x00202828u; frame = 0x00A0C0C0u; textc = 0x00E8F0F0u; }
        else                 { fill = 0x00201818u; frame = 0x00704040u; textc = 0x00C08080u; }

        BlendRectWin(bx, by, bx + boxW, by + boxH, fill, owned ? 0.95f : 0.88f);
        FrameRectWin(bx, by, bx + boxW, by + boxH,
                     sel ? 0x00FFFFFFu : frame, 0.95f, sel ? 3 : 1);

        if (treeVK > 0.35f) {
            /* v2.9：**名字独占框内一行，价格画在框下方**。
               之前挤同一行，800x600 下框内只有 138px，
               "MAGAZINE"(160px) 截断后价格还叠上去 → 糊成一坨。 */
            char nb[48];
            /* v2.9：框内也用中文名（原来只有底部详情是中文，同屏一中一英） */
            sprintf(nb, "%s%s", benchNodes[id].star ? "* " : "",
                    Tr(benchNodes[id].name, benchNodes[id].nameZh));
            ClampRowText(nb, boxW - UIS(10));
            /* 有价格时名字靠上，没价格时居中 */
            { int nameY = (boxH >= th * 2 + UIS(6)) ? (by + UIS(3))
                                                   : (by + (boxH - th) / 2);
              DrawTextWin(bx + UIS(5), nameY, nb, textc); }

            /* ============================================================
             * 修：**价格画在框下面，会压到下一行的节点名**
             * ------------------------------------------------------------
             * ui_layout 实测（800x600，SHP_TREE 页）：
             *   "350"(426,195..498,231)  x  "轻装"(426,225..498,261)
             * 价格是「本框底部 + 2px」，而下一行的框紧挨着 ——
             * 行距一小，价格的最后一个像素就落进下一行名字里。
             *
             * 现在改成**放进框里、贴底一行**，并留一个最小高度检查：
             * 框太矮（放不下两行字）就干脆不显示价格，只看底部详情条。
             * ============================================================ */
            if (treeVK > 0.55f && boxH >= th * 2 + UIS(6)) {
                char pb[24];
                sprintf(pb, "%d", benchNodes[id].cost);
                DrawTextWin(bx + UIS(5), by + boxH - th - UIS(3), pb,
                            owned ? 0x00807850u : (affordable ? 0x00A0E0A0u : 0x00C06060u));
            }
        }
    }

    /* ---- 底部详情 ---- */
    {
        /* v2.9：原来 dy=winH-96，三行累加后第三行 570 撞提示行 574。
           现在从下往上倒推：留出提示行 + 三行正文。 */
        int lineH = th + UIS(4);
        /* v6：底部还有一条 R x2 RESET / WHEEL ZOOM 操作提示，
           详情行不能画到它上面去（用户截图：SHORT BY 和 WHEEL 重叠） */
        int detailMaxY = winH - UIS(16) - th - UIS(6) - th - UIS(6);
        int dy = winH - UIS(10) - th - 3 * lineH;
        int id = shopNodeSel;
        if (id < 0 || id >= BENCH_COUNT) id = 0;
        static char l2[128];

        if (dy <= detailMaxY) DrawTextWin(UIS(28), dy, Tr(benchNodes[id].name, benchNodes[id].nameZh), 0x00FFFFFFu);
        sprintf(l2, "%d %s", benchNodes[id].cost, Tr("GOLD", "金币"));
        DrawTextWin(UIS(240), dy, l2, carryGold >= benchNodes[id].cost ? 0x00A0E0A0u : 0x00E06060u);
        /* v7 第 51 轮：**把 THREAT 代价写出来**
           改造树的代价是 THREAT，但整棵树从没显示过它 ——
           玩家买了节点、怪变凶了，却不知道是谁干的。
           现在价格旁边直接写 +N THREAT。 */
        {
            static char tb[48];
            extern int BenchThreatCostOf(int);
            int tc = BenchThreatCostOf(id);
            if (tc > 0) {
                sprintf(tb, "+%d THREAT", tc);
                DrawTextWin(UIS(400), dy, tb, 0x00FF8050u);
            }
        }

        dy += lineH;
        {
            static char db[128];
            sprintf(db, "%s", Tr(benchNodes[id].desc, benchNodes[id].descZh));
            ClampRowText(db, winW - UIS(56));
            if (dy <= detailMaxY) DrawTextWin(UIS(28), dy, db, 0x00C8D0D0u);
        }

        dy += lineH;
        if (BenchHas(id)) {
            if (dy <= detailMaxY) DrawTextWin(UIS(28), dy, Tr("OWNED", "已拥有"), 0x00C8A040u);
        } else if (!BenchPrereqOk(id)) {
            if (dy <= detailMaxY) DrawTextWin(UIS(28), dy, Tr("REQUIRES THE NODE ABOVE IT", "需要先买同一列的上一层"), 0x00E0A040u);
        } else if (carryGold < benchNodes[id].cost) {
            sprintf(l2, "%s %d", Tr("SHORT BY", "还差"), benchNodes[id].cost - carryGold);
            if (dy <= detailMaxY) DrawTextWin(UIS(28), dy, l2, 0x00E06060u);
        } else {
            if (dy <= detailMaxY) DrawTextWin(UIS(28), dy, Tr("ENTER TO BUY", "回车购买"), 0x0060FF90u);
        }

        {
            const char* kh = Tr("R x2 RESET / WHEEL ZOOM  DRAG PAN  HOME  ESC",
                                "滚轮缩放  拖拽平移  HOME  ESC");
            DrawTextWin(winW - UIS(24) - TextWidthWin(kh), winH - UIS(16) - th - UIS(6), kh, 0x009AA8A8u);
        }
    }
}

static void DrawBankPage(void) {
    SkillTreeLayout L;
    SkillTreeCompute(&L);

    int TH = TextHeightWin();
    extern int bankGold, bankHistMax, bankCarryPct, carryGold;
    extern int carryGold;

    DrawTitleWin("BANK", L.titleY, 0x00FFC040u);

    int y = L.curY + TH;
    int x = L.panel.left + L.padX * 3;
    char line[128];

    sprintf(line, Tr("BALANCE          %d GOLD", "存款          %d 金币"), bankGold);
    DrawTextWin(x, y, line, 0x00FFC040u);
    y += TH * 2;

    sprintf(line, Tr("HIGHEST EVER     %d GOLD", "历史最高      %d 金币"), bankHistMax);
    DrawTextWin(x, y, line, 0x00909090u);
    y += TH * 2;

    {
        int p = bankCarryPct;
        if (p < 0) p = 0;
        if (p >= BANK_CARRY_STEPS) p = BANK_CARRY_STEPS - 1;
        int pc = p * BANK_STEP_PCT;
        int amt = (int)((float)bankGold * (float)pc / 100.0f + 0.5f);
        sprintf(line, Tr("CARRY IN   %3d%%   (%d / %d GOLD)",
                         "携带   %3d%%   (%d / %d 金币)"),
                pc, amt, bankGold);
        DrawTextWin(x, y, line, 0x0060FF90u);
        y += TH + UIS(6);

        /* ===== v6 **可视化滑动条** —— 用户要「左右滑动选带多少」 =====
           21 格，亮的部分 = 已选比例，最右边一个小方块是把手。 */
        {
            int barW = L.panel.right - L.panel.left - L.padX * 6;
            if (barW > UIS(720)) barW = UIS(720);
            int barH = UIS(18);
            int bx = x;
            int by = y;
            /* 底槽 */
            BlendRectWin(bx, by, bx + barW, by + barH, 0x00202020u, 0.95f);
            FrameRectWin(bx, by, bx + barW, by + barH, 0x00505050u, 0.9f, 1);
            /* 已选填充 */
            if (p > 0) {
                int fw = barW * p / (BANK_CARRY_STEPS - 1);
                BlendRectWin(bx + 1, by + 1, bx + fw, by + barH - 1,
                             0x0030C060u, 0.90f);
            }
            /* 每 5 档一根刻度（0/25/50/75/100%） */
            for (int k = 0; k < BANK_CARRY_STEPS; k += 5) {
                int gx = bx + barW * k / (BANK_CARRY_STEPS - 1);
                BlendRectWin(gx, by + barH, gx + 1, by + barH + UIS(4),
                             0x00606060u, 0.9f);
            }
            /* 把手 */
            {
                int hx = bx + barW * p / (BANK_CARRY_STEPS - 1);
                int hw = UIS(6);
                BlendRectWin(hx - hw / 2, by - UIS(3),
                             hx + hw / 2, by + barH + UIS(3),
                             0x00FFE070u, 0.98f);
            }
            /* 刻度文字 */
            DrawTextWin(bx, by + barH + UIS(6), "0%", 0x00707070u);
            {
                const char* r100 = "100%";
                DrawTextWin(bx + barW - TextWidthWin(r100), by + barH + UIS(6),
                            r100, 0x00707070u);
            }
            y = by + barH + UIS(6) + TH + UIS(6);
        }
    }

    {
        /* v6：强度加成 = 携带额 / 历史最高（携带额现在按 5% 档位算） */
        int bp = bankCarryPct;
        if (bp < 0) bp = 0;
        if (bp >= BANK_CARRY_STEPS) bp = BANK_CARRY_STEPS - 1;
        int carryAmt = (int)((float)bankGold * (float)(bp * BANK_STEP_PCT) / 100.0f + 0.5f);
        int boost = (bankHistMax > 0)
                  ? (int)((float)carryAmt * 100.0f / (float)bankHistMax + 0.5f)
                  : 0;
        if (boost > 100) boost = 100;
        sprintf(line, Tr("NEXT RUN BOOST   +%d%%", "下局强度加成  +%d%%"), boost);
        DrawTextWin(x, y, line, 0x00FFD040u);
        y += TH * 2;
    }

    y += TH;
    DrawTextWin(x, y, Tr("ENTER  CYCLE CARRY RATIO",
                         "回车   切换携带比例"), 0x00707070u);
    y += TH;
    DrawTextWin(x, y, Tr("CARRY RATIO ONLY TAKES EFFECT NEXT RUN",
                         "携带比例只在下一局生效"), 0x00606060u);

    DrawTextCenterWin((L.panel.left + L.panel.right) / 2, L.hintY,
                      Tr("ENTER SWITCH   ESC BACK", "回车切换   ESC 返回"), 0x00707070u);
}

/* 对外唯一入口：顺序与 RenderFrame 里原来那两行一致（面板 → 文字）。 */
void UiDrawMenuPages(void) {
    DrawMenuPanels();
    DrawMenuText();
}

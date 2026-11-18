#include "protocol.h"
#include "threat_tier.h"
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
#include "ui_archive.h"
#include "room_theme.h"
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

static void DrawBenchTree(MenuGeo g);
static void DrawBankPage(void);
static void DrawSafehousePage(void);
static void DrawTextInRowLeft(const char* text, RECT row, unsigned int color);

/* 分支名表：原来在 ui.c 里是 static，菜单搬出来后自己留一份。
   只是显示名，重复定义比为了它开一个新接口更划算。 */
static const char* kBranchName[7] = { "SILENCE", "ECONOMY", "CONCEAL",
                                      "SHAPE", "RISK", "CROSS", "ORIGIN" };
static const char* kBranchNameZh[7] = { "沉寂", "节流", "藏匿",
                                        "塑形", "冒险", "跨支", "起点" };

static int InRoleDossier(void);

void MainMenuCompute(MainMenuLayout* L) {
    memset(L,0,sizeof(*L));
    int margin=UIS(20);
    int th=TextMediumLiteralHeight();
    L->count=MainMenuRowCount();
    L->padX=UIS(12);
    L->titleY=winH*7/100;
    L->subY=L->titleY+th+UIS(9);
    L->firstY=winH*22/100;
    L->footRuleY=winH*89/100;
    L->footY=L->footRuleY+UIS(12);
    int bottom=L->footRuleY-UIS(10);
    int wide=winW*5>=winH*6;
    int split=wide?winW*55/100:winW-margin;
    int rowsBottom=wide?bottom:bottom-(bottom-L->firstY)/(InRoleDossier()?2:4);
    L->rowH=(rowsBottom-L->firstY)/L->count;
    L->panel=(RECT){margin,L->firstY,split,L->firstY+L->count*L->rowH};
    L->detail=wide?(RECT){split+margin,L->firstY,winW-margin,bottom}:
        (RECT){margin,L->panel.bottom+UIS(8),winW-margin,bottom};
}

static int MainMenuRowCount(void) {
    return MainMenuCount();
}

enum { PM_ROOT, PM_MORE, PM_ROLES };
static int InRoleDossier(void) { return mainMenuMore==PM_ROLES; }
static const MainMenuAction rootActions[]={
    MENU_CONTINUE,MENU_STORY,MENU_CLASSIC,MENU_ROLES,MENU_TRAINING,
    MENU_UPGRADES,MENU_BANK,MENU_MORE,MENU_QUIT,MENU_NONE
};
static const MainMenuAction moreActions[]={
    MENU_DAILY,MENU_ENDLESS,MENU_PRACTICE_ARENA,MENU_BESTIARY,
    MENU_ACHIEVEMENTS,MENU_HELP,MENU_SETTINGS,MENU_BACK,MENU_NONE
};
static const MainMenuAction roleActions[]={
    MENU_ROLE_0,MENU_ROLE_1,MENU_ROLE_2,MENU_ROLE_3,MENU_BACK,MENU_NONE
};
static const MainMenuAction welcomeActions[]={
    MENU_FIRST_STORY,MENU_WELCOME_PRACTICE,MENU_WELCOME_SKIP,MENU_NONE
};
static int WelcomeOpen(void) { return protocolProfile.briefSeen==0; }
static const MainMenuAction *CurrentActions(void) {
    if (WelcomeOpen()) return welcomeActions;
    if (mainMenuMore==PM_ROLES) return roleActions;
    if (mainMenuMore==PM_MORE) return moreActions;
    return rootActions;
}
static int MenuActionVisible(MainMenuAction action) {
    return (action!=MENU_CONTINUE || hasSave) && (action!=MENU_ENDLESS || EndlessUnlocked());
}
static MainMenuAction MenuActionAt(int row) {
    for (const MainMenuAction *p=CurrentActions();*p!=MENU_NONE;p++) {
        if (MenuActionVisible(*p) && row--==0) return *p;
    }
    return MENU_NONE;
}
int MainMenuCount(void) {
    int count=0;
    for (const MainMenuAction *p=CurrentActions();*p!=MENU_NONE;p++)
        count+=MenuActionVisible(*p);
    return count;
}

static int MenuItemIndex(void) {
    if (currentState == STATE_SETTINGS) return settingsSelection;
    if (currentState == STATE_HUB) return hubSelection;
    if (currentState == STATE_SKILL_PICK) return skillPickSel;
    /* 2026-11-08：选武器流程已删（武器改扳手） */
    if (currentState == STATE_SAFEHOUSE) return shopRow;
    if (currentState == STATE_ACHIEVEMENTS) return achSelection;
    if (currentState == STATE_BESTIARY) return UiArchiveSelection();
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
            if (i >= UiArchiveEntryCount()) return "Back";
            if (UiArchiveTab()==ARCHIVE_ROOMS) return RoomThemeName(i,g_lang==LANG_ZH);
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
                case 2: sprintf(kb, "%s: HOLD %s  (NOISE)", Tr("SEARCH","搜查"), VkName(g_bindings[ACT_INTERACT])); break;
                case 3: snprintf(kb,sizeof(kb),Tr("CRANK: HOLD %s / OUTSIDE HIDING ONLY","摇电：按住%s / 仅隐藏点外可用"),VkName(g_bindings[ACT_CRANK])); break;
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
        case STATE_GAMEOVER:
            return protocolRun.story && !gameWon?Tr("Retry floor checkpoint","从楼层检查点重试"):Tr("Return to Menu","返回主菜单");
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
            const char* label=Transl(it->name);
            if (IsKeyBindItem(it)) {
                return label;
            }
            if (it->value == &pitchLookEnabled) {
                snprintf(buf, sizeof(buf), "%s: %s (%s)", label,
                         Transl(it->options[*it->value ? 1 : 0]),
                         Tr("Visual artifacts possible", "可能出现画面异常"));
                return buf;
            }
            if (it->kind == SET_SLIDER) {
                if (it->fvalue == &gammaValue) sprintf(buf, "%s: %.2f", label, *it->fvalue);
                else if (it->fvalue == &volumeF || it->fvalue == &musicVolumeF)
                    sprintf(buf, "%s: %.0f%%", label, *it->fvalue * 100.0f);
                else sprintf(buf, "%s: %.1fx", label, *it->fvalue);
            } else {
                int dispVal = *it->value;
                if (dispVal >= 0 && dispVal < it->count) {
                    /* v2.9：选项值也走翻译（原来 LOW/MEDIUM/HIGH 全英文） */
                    sprintf(buf, "%s: %s", label,
                            Transl(it->options[dispVal]));
                } else {
                    sprintf(buf, "%s: %s", label,
                            Transl(it->options[0]));
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

static const unsigned roleColors[]={0x84c7b3,0x8dbbe2,0xb7a4d2,0xe3b770};

static RECT RoleTrialRect(RECT card) {
    int pad=UIS(10);
    return (RECT){card.left+pad,card.bottom-pad-TextMediumLiteralHeight()-UIS(10),
                  card.right-pad,card.bottom-pad};
}
int MainMenuTrialRole(int x,int y) {
    if (currentState!=STATE_MAIN_MENU || WelcomeOpen() || mainMenuMore!=PM_ROLES ||
        menuSelection<0 || menuSelection>3) return -1;
    if (x<0) return menuSelection;
    MainMenuLayout layout;MainMenuCompute(&layout);
    RECT button=RoleTrialRect(layout.detail);
    return x>=button.left && x<button.right && y>=button.top && y<button.bottom?menuSelection:-1;
}
static void DrawRoleDetails(RECT card,int role) {
    int pad=UIS(10),th=TextMediumLiteralHeight();
    int x=card.left+pad,right=card.right-pad,y=card.top+pad;
    int width=right-x;
    RECT button=RoleTrialRect(card);
    int bottom=button.top-UIS(8);
    if (width<UIS(60) || y+th>bottom) return;
    unsigned color=roleColors[role];
    int portrait=width/4;
    if (portrait>bottom-y) portrait=bottom-y;
    if (portrait>=UIS(52) && card.bottom-card.top>UIS(160)) {
        int pixel=portrait/21;
        BlendRectWin(x,y,x+portrait,y+portrait,0x101a23,1);
        FrameRectWin(x,y,x+portrait,y+portrait,color,.5f,1);
        UiDrawCharacterPortrait(role,x+portrait/2,y+portrait/2,pixel);
        x+=portrait+pad;width=right-x;
    }
    y=UiDossierText(ProtocolRoleName(role),x,y,width,bottom,color)+UIS(6);
    char line[160];
    snprintf(line,sizeof line,Tr("SAN %d   CELL %d   STA %d","理智 %d   电量 %d   体力 %d"),
             ProtocolRoleBase(role,0),ProtocolRoleBase(role,1),ProtocolRoleBase(role,2));
    y=UiDossierText(line,x,y,width,bottom,UI_INK)+UIS(7);
    if (role && !(protocolProfile.unlocked&(1u<<role)))
        snprintf(line,sizeof line,Tr("Unlocks after chapter %d","完成第%d章后解锁"),role);
    else snprintf(line,sizeof line,"%s",protocolProfile.selected==(unsigned)role?
                  Tr("Selected for the next run","已选为下一局角色"):Tr("Enter to select","回车选择此角色"));
    y=UiDossierText(line,x,y,width,bottom,UI_INK_DIM)+UIS(12);
    y=UiDossierText(ProtocolAbilityName(role),x,y,width,bottom,color)+UIS(5);
    y=UiDossierText(ProtocolRoleAbility(role),x,y,width,bottom,UI_INK)+UIS(10);
    UiDossierText(ProtocolRoleBio(role),x,y,width,bottom,UI_INK_DIM);
    BlendRectWin(button.left,button.top,button.right,button.bottom,0x183039,1);
    FrameRectWin(button.left,button.top,button.right,button.bottom,color,.65f,1);
    DrawTextSmallLiteralWin(button.left+pad,button.top+(button.bottom-button.top-FONT_H)/2,
                            Tr("T / TRY IN PRACTICE","T / 进入训练场试用"),color);
}

static unsigned MenuTone(unsigned a,unsigned b,int strength) {
    unsigned result=0;
    for (int i=0;i<3;i++) {
        unsigned v=(((a>>(i*8))&255)*(100-strength)+((b>>(i*8))&255)*strength)/100;
        result|=v<<(i*8);
    }
    return result;
}
static void DrawFacilityArt(RECT art) {
    const unsigned accent=0x799BAB;
    int w=art.right-art.left,h=art.bottom-art.top;
    if (w<20 || h<20) return;
    int cx=art.left+w*47/100,cy=art.top+h*49/100;
    int dw=w/10,dh=h/3;
    for (int y=art.top;y<art.bottom;y++) {
        float d=(float)abs(y-cy)/fmaxf(1,h/2);
        int left=cx-dw-(int)(d*w*.28f);
        int right=cx+dw+(int)(d*w*.28f);
        unsigned wall=MenuTone(0x18242d,accent,9+(int)(d*12));
        unsigned middle=y<cy?MenuTone(0x253740,accent,14):MenuTone(0x0d151d,accent,8);
        BlendRectWin(art.left,y,left,y+1,wall,1);
        BlendRectWin(left,y,right,y+1,middle,1);
        BlendRectWin(right,y,art.right,y+1,wall,1);
    }
    int edge=UIS(2);
    for (int n=1;n<=5;n++) {
        int offset=n*n*w/106;
        int y=n*n*h/108;
        int x0=cx-dw-offset,x1=cx+dw+offset;
        BlendRectWin(x0,cy-dh/2-y,x1,cy-dh/2-y+edge,accent,.35f);
        BlendRectWin(x0,cy+dh/2+y,x1,cy+dh/2+y+edge,accent,.24f);
        BlendRectWin(x0,cy-dh/2-y,x0+edge,cy+dh/2+y,accent,.24f);
        BlendRectWin(x1-edge,cy-dh/2-y,x1,cy+dh/2+y,accent,.24f);
    }
    BlendRectWin(cx-dw,cy-dh/2,cx+dw,cy+dh/2,0x0c131a,1);
    FrameRectWin(cx-dw,cy-dh/2,cx+dw,cy+dh/2,accent,.9f,2);
    BlendRectWin(cx-1,cy-dh/2,cx+1,cy+dh/2,accent,.28f);
    for (int i=0;i<4;i++) {
        int y=cy+dh/2+h/20+i*h/21;
        int half=dw/6+i*w/45;
        BlendRectWin(cx-half,y,cx+half,y+edge,accent,.95f);
    }
    FrameRectWin(art.left,art.top,art.right,art.bottom,accent,.55f,1);
}
static void DrawMainMenuDetails(const MainMenuLayout *M) {
    RECT card=M->detail;
    if (card.right<=card.left || card.bottom<=card.top) return;
    if (!WelcomeOpen() && mainMenuMore==PM_ROLES) {
        if (menuSelection<4) {
            BlendRectWin(card.left,card.top,card.right,card.bottom,0x0e1b26,1);
            FrameRectWin(card.left,card.top,card.right,card.bottom,UI_EDGE,.75f,1);
            DrawRoleDetails(card,menuSelection);
        }
        return;
    }
    DrawFacilityArt(card);
}

static void DrawMainMenu(const MainMenuLayout* M) {
    if (menuSelection<0 || menuSelection>=M->count) menuSelection=0;
    int left=M->panel.left+M->padX;
    int th=TextMediumLiteralHeight();
    unsigned tint=ThreatTierColor(PreviewThreatTier());
    BlendRectWin(0,0,winW,winH,MenuTone(0x09131b,tint,8),1);
    BlendRectWin(0,0,winW,UIS(2),tint,1);
    BlendRectWin(M->panel.left,M->titleY,M->panel.left+UIS(2),M->subY+FONT_H,tint,.85f);
    DrawTextMediumLiteralWin(left,M->titleY,"C A N I T Y",0xe4e7de);
    DrawTextSmallLiteralWin(left,M->subY,Tr("DISORDER PROTOCOL","失序协议"),UI_INK_DIM);
    for(int i=0;i<M->count;i++) {
        int y=M->firstY+i*M->rowH,sel=i==menuSelection;
        MainMenuAction action=MenuActionAt(i);
        int primary=action==MENU_CONTINUE || action==MENU_STORY || action==MENU_FIRST_STORY || action==MENU_CLASSIC;
        int bottom=y+M->rowH-UIS(3);
        BlendRectWin(M->panel.left,y,M->panel.right,bottom,sel?MenuTone(0x112332,tint,28):primary?0x172a32:0x111e28,1);
        FrameRectWin(M->panel.left,y,M->panel.right,bottom,sel?tint:MenuTone(0x314653,tint,10),.85f,1);
        if (sel) BlendRectWin(M->panel.left,y,M->panel.left+UIS(3),bottom,tint,1);
        const char *label=MenuLabel(i);
        char line[128];
        if (action==MENU_CONTINUE && hasSave)
            snprintf(line,sizeof line,Tr("%s  /  %s F%02d","%s  /  %s 第%d层"),label,
                     SavePendingStory()?Tr("Story","剧情"):Tr("Saved","存档"),SavePendingFloor());
        else snprintf(line,sizeof line,"%s",label);
        int width=M->panel.right-left-UIS(8);
        unsigned color=sel?MenuTone(0xe7f2f0,tint,50):UI_INK;
        if (th<=M->rowH-UIS(2) && TextMediumLiteralWidth(line)<=width)
            DrawTextMediumLiteralWin(left,y+(M->rowH-th)/2,line,color);
        else {
            UiTrimSmallText(line,width);
            DrawTextSmallLiteralWin(left,y+(M->rowH-FONT_H)/2,line,color);
        }
    }
    DrawMainMenuDetails(M);
    BlendRectWin(M->panel.left,M->footRuleY,winW-UIS(20),M->footRuleY+1,UI_EDGE,.85f);
    DrawTextSmallLiteralWin(left,M->footY,Tr("Enter / Click","回车或单击"),UI_INK_DIM);
    DrawTextSmallLiteralWin(winW-TextSmallLiteralWidth(CANITY_VERSION_STR)-UIS(20),M->footY,CANITY_VERSION_STR,UI_INK_DIM);
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
    if (g.title && g.title[0]) DrawTitleWin(Transl(g.title), g.titleY, titleColor);
}


/* Wrap UTF-8 at complete codepoints. Every section has an explicit lower bound. */


/* Use exactly the existing baked world sprite, cropped without stretching.
 * No game camera, light, sprite-accent or monster state is modified. */





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
            if (protocolRun.story) {
                char settlement[96];
                snprintf(settlement,sizeof settlement,Tr("EVIDENCE TRANSMITTED   BANK +%d","证词已传出   银行入账 +%d"),lastRunKept);
                const char *label = i==1?ProtocolFloorName():
                    !gameWon?Tr("Escape: return to main menu","按 Esc 返回主菜单"):
                    ProtocolPeacefulStory()?settlement:
                    protocolRun.ending==1?Tr("Ending: Departure","结局：离开设施"):Tr("Ending: Protocol Reset","结局：协议归零");
                DrawTextInRow(label,RowRect(g,i),UI_INK);
                continue;
            }
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
            DrawTextWin(row.left + UIS(8), y, Transl(it->name), c);
            const char *valStr = BindActionOfItem(it) == rebindingAct
                ? "..." : Transl(VkName(*it->value));
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
        BlendRectWin(barX, thumbY, barX + UIS(8), thumbY + thumbH, UI_ACCENT, 0.8f);

        /* 顶部/底部箭头提示 */
        if (g.scrollOffset > 0) {
            DrawTextCenterWin(g.panel.left + (g.panel.right - g.panel.left) / 2, g.firstY - UIS(20), "^", UI_ACCENT);
        }
        if (g.scrollOffset + g.maxVisibleRows < g.count) {
            DrawTextCenterWin(g.panel.left + (g.panel.right - g.panel.left) / 2, g.panel.bottom + UIS(4), "v", UI_ACCENT);
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
    unsigned branchColor=roleColors[protocolRun.role];
    int w,h; BenchNodeBoxSize(&w,&h);
    for (int id = 0; id < BenchGraphCount(); id++) {
        int pre = BenchPrereqId(id);
        int px = pre >= 0 ? (int)(BenchNodeWorldX(pre)*treeVK+treeVX) : (int)treeVX;
        int py = pre >= 0 ? (int)(BenchNodeWorldY(pre)*treeVK+treeVY) : (int)treeVY;
        int x = (int)(BenchNodeWorldX(id)*treeVK+treeVX), y = (int)(BenchNodeWorldY(id)*treeVK+treeVY);
        int lit = pre < 0 || BenchHas(pre);
        if (id==BENCH_COUNT+3) {
            int other=pre==BENCH_COUNT+1?BENCH_COUNT+2:BENCH_COUNT+1;
            BenchGraphLink((int)(BenchNodeWorldX(other)*treeVK+treeVX),
                           (int)(BenchNodeWorldY(other)*treeVK+treeVY),
                           x,y,BenchHas(other)?UI_ACCENT:UI_EDGE_DIM,BenchHas(other));
        }
        unsigned path=id>=BENCH_COUNT?branchColor:UI_ACCENT;
        BenchGraphLink(px,py,x,y,id==shopNodeSel || pre==shopNodeSel?UI_INFO:(lit?path:UI_EDGE_DIM),lit);
    }
    const unsigned int colors[4] = {0x79CDD5u,UI_ACCENT,UI_DANGER,branchColor};
    for (int id = 0; id < BenchGraphCount(); id++) {
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
            snprintf(line,sizeof(line),"%d",BenchNodeCost(id));
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
    if (BenchGraphCount()>BENCH_COUNT) {
        int x=winW*.30f+UIS(14),y=UIS(55);
        char role[120];snprintf(role,sizeof role,Tr("SOUTH / %s","正南 / %s"),ProtocolAbilityName(protocolRun.role));
        UiTrimSmallText(role,winW*2/3-UIS(22));
        DrawTextSmallLiteralWin(x,y,role,branchColor);
    }
    int id=shopNodeSel;if(id<0 || id>=BenchGraphCount())id=0;
    int x=(int)(winW*.30f)+UIS(12),y=winH-UIS(166),right=winW-m-UIS(12),bottom=winH-m;
    int width=right-x-UIS(140);BlendRectWin(x,y,right,bottom,UI_BG_PANEL,1);
    char text[192];snprintf(text,sizeof(text),"%s",BenchNodeName(id));
    while(text[0] && TextMediumLiteralWidth(text)>width){size_t n=strlen(text)-1;while(n && ((unsigned char)text[n]&0xC0)==0x80)n--;text[n]=0;}
    DrawTextMediumLiteralWin(x,y+UIS(5),text,UI_ACCENT);y+=TextMediumLiteralHeight()+UIS(10);
    snprintf(text,sizeof(text),Tr("COST %d / THREAT +%.1f","价格 %d金币 / 威胁 +%.1f"),BenchNodeCost(id),(double)BenchThreatCostOf(id));UiTrimSmallText(text,width);DrawTextSmallLiteralWin(x,y,text,UI_GOLD);y+=UIS(23);
    y=UiDossierText(BenchNodeDescription(id),x,y,width,bottom-UIS(27),UI_INK);
    int parent=BenchPrereqId(id);
    const char *status=id>=BENCH_COUNT?ProtocolNodeStatus(id-BENCH_COUNT):
                       BenchHas(id)?Tr("OWNED","已拥有"):BenchCanBuy(id)?Tr("CAN BUY","可购买"):Tr("CHECK FUNDS / PREREQUISITE","检查金币或前置");
    snprintf(text,sizeof(text),Tr("FIRST: %s / %s","前置：%s / %s"),parent>=0?BenchNodeName(parent):Tr("START","起点"),status);
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
    if (shopPage==SHP_UPGRADE) {
        int columns=(right-left)>=UIS(410)?3:2;
        int rows=1+(RUN_UPG_ROLE+columns-1)/columns;
        int height=(bottom-top-gap*(rows-1))/rows;
        if (row==RUN_UPG_ROLE) {
            *rect=(RECT){left,top,right,top+height};
            return;
        }
        int width=(right-left-gap*(columns-1))/columns;
        int x=left+(row%columns)*(width+gap),y=top+(row/columns+1)*(height+gap);
        *rect=(RECT){x,y,x+width,y+height};
        return;
    }
    int columns=winW>=1000?4:2;
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
    if(sprite>=0) { UiDossierPortrait(sprite,rect,1);
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
        UiDossierText(badge,r.left+pad,r.top+UIS(3),width,r.top+th+UIS(6),UI_ACCENT);
        y=r.top+th+pad;
    }
    y=UiDossierText(ShopProductName(row),r.left+pad,y,width,r.bottom-pad,UI_INK_TITLE);
    int available=r.bottom-y-th*3-pad;
    int side=available>UIS(28)?available:UIS(24);
    if(side>width)side=width;
    RECT icon={(r.left+r.right-side)/2,y,(r.left+r.right+side)/2,y+side};
    DrawSupplyIcon(row,icon);
    y=icon.bottom+UIS(4);
    char price[64];
    int percent=ShopBeaconPercentAt(row);
    if (percent) snprintf(price,sizeof price,Tr("%d GOLD (%d%% OF BALANCE)","%d金币（余额的%d%%）"),ShopPriceAt(row),percent);
    else snprintf(price,sizeof price,Tr("%d RUN GOLD","%d 局内金币"),ShopPriceAt(row));
    y=UiDossierText(price,r.left+pad,y,width,r.bottom-th-pad,UI_GOLD);
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
    y=UiDossierText(ShopServiceName(row),x,y,w,bottom,UI_INK_TITLE)+UIS(18);
    if(row<3){float values[3]={sanity,battery,stamina};float maximum=row==0?SanityMax():row==1?(float)BatteryMax():StaminaMax();
        snprintf(text,sizeof(text),Tr("CURRENT %.0f / %.0f","当前 %.0f / %.0f"),(double)values[row],(double)maximum);
        y=UiDossierText(text,x,y,w,bottom,UI_INK)+UIS(10);
        UiBar(x,y,w,UIS(8),values[row]/fmaxf(1,maximum),UI_BAR_NORMAL,UI_ACCENT);y+=UIS(20);
        snprintf(text,sizeof(text),Tr("RESTORE +%.0f / PRICE %d GOLD","本次恢复 +%.0f / 当前价格 %d金币"),(double)ShopServiceGain(row),ShopServicePrice(row));
        y=UiDossierText(text,x,y,w,bottom,UI_GOLD)+UIS(12);
        y=UiDossierText(row==0?Tr("Unlimited recovery. Every use doubles this service price for the rest of the run.","无次数限制。每次使用后，本项服务本局价格翻倍。"):Tr("No cooldown or use limit; prices double per use and persist between floors.","无冷却、无次数限制；每次价格翻倍，跨层保留涨价。"),x,y,w,bottom,UI_INK_DIM)+UIS(14);
    }else y=UiDossierText(row==3?Tr("Skip this non-boss floor. Requires the permanent SHORTCUT skill; once per run.","需要永久捷径技能，跳过当前非首领层，每局限一次。"):Tr("Each QWER hit restores sanity and battery; combos improve recovery. Six misses end the game. Finish for gold. Passive resource drain pauses during play.","每次QWER命中恢复理智和电量，连击提高恢复量；累计6次失误结束。完成可获金币，游玩期间暂停自然理智与电量消耗。"),x,y,w,bottom,UI_INK)+UIS(15);
    UiDossierText(ShopServiceStatus(row),x,y,w,bottom-UIS(10),ShopServiceCanUse(row)?UI_OK:UI_INFO);
}
static void DrawUpgradeCard(int row,RECT r) {
    int pad=UIS(10),width=r.right-r.left-pad*2,y=r.top+pad;
    y=UiDossierText(Tr(RunUpgName(row),RunUpgNameZh(row)),r.left+pad,y,width,r.bottom-pad,UI_INK_TITLE);
    int level=RunUpgLevel(row),pip=UIS(10);
    for(int i=0;i<RUN_UPG_MAX;i++) {
        int x=r.left+pad+i*(pip+UIS(5));
        if(i<level)BlendRectWin(x,y,x+pip,y+pip,UI_ACCENT,1.0f);
        else FrameRectWin(x,y,x+pip,y+pip,UI_EDGE,.95f,1);
    }
    y+=pip+UIS(8);
    char effect[128];
    snprintf(effect,sizeof effect,"%s",Tr(RunUpgDesc(row),RunUpgDescZh(row)));
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
            DrawTextCenterWin((r.left+r.right)/2,(r.top+r.bottom-th)/2,footer,UI_ACCENT);
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
        UiDossierText(slider?Tr("The contract sets the minimum threat tier. Higher risk increases extraction rewards.","契约决定本局最低威胁档位，高风险会提高撤离收益。"):Tr("This amount leaves the bank only when a normal run starts.","只在普通对局开始时取出，不会因拖动立即扣款。"),r.left,r.bottom+UIS(20),r.right-r.left,winH*82/100,UI_INK_DIM);
    }
    int x=winW*62/100,right=winW-pad,y=winH*26/100,bottom=winH*81/100;
    UiPanel(x-UIS(14),y-UIS(12),right+UIS(10),bottom,UI_PANEL_NORMAL);
    y=UiDossierText(Tr("NEXT RUN","下局预测"),x,y,right-x,bottom,UI_INK_TITLE)+UIS(12);
    snprintf(text,sizeof(text),Tr("NEXT FLOOR: %s","下层预计：%s"),ThreatTierLabel(PreviewThreatTier()));
    y=UiDossierText(text,x,y,right-x,bottom,ThreatTierColor(PreviewThreatTier()))+UIS(8);
    snprintf(text,sizeof(text),Tr("GOLD PRESSURE %d / 15","金币压力 %d / 15"),BankPreviewTier());
    y=UiDossierText(text,x,y,right-x,bottom,UI_ACCENT)+UIS(12);
    snprintf(text,sizeof(text),Tr("AWARENESS x%.2f","感知倍率 x%.2f"),(double)BankPreviewHearing());
    y=UiDossierText(text,x,y,right-x,bottom,UI_INK)+UIS(12);
    snprintf(text,sizeof(text),Tr("EXIT REWARD x%.2f","撤离收益 x%.2f"),(double)BankRiskExitMul(bankRiskPlan));
    y=UiDossierText(text,x,y,right-x,bottom,UI_INK)+UIS(12);
    snprintf(text,sizeof(text),Tr("DEFEAT REFUND %d%%","失败返还 %d%%"),ThreatTierRefundPercent(PreviewThreatTier()));
    UiDossierText(text,x,y,right-x,bottom,UI_INK_DIM);
    /* All four plans remain selectable without a minimum bank balance. */
    static const char* tierNames[4] = {"I  / BLUE", "II / VIOLET", "III / AMBER", "IV / BLOOD"};
    int chipW=(winW-pad*2)/4;
    for(int stage=0;stage<4;stage++) {
        int chipX=pad+stage*chipW,chipY=winH*81/100;
        BlendRectWin(chipX,chipY,chipX+chipW-UIS(5),chipY+UIS(23),UI_BG_ROW,1);
        FrameRectWin(chipX,chipY,chipX+chipW-UIS(5),chipY+UIS(23),ThreatTierColor(stage),stage==PreviewThreatTier()?2:1,1);
        DrawTextSmallLiteralWin(chipX+UIS(6),chipY+UIS(4),tierNames[stage],ThreatTierColor(stage));
    }
    UiDossierText(Tr("Drag sliders / Up-Down select / Left-Right adjust / Enter save / Esc back","拖动滑条；上下选择，左右调整；回车保存，ESC返回"),pad,winH*87/100,winW-pad*2,winH-UIS(10),UI_INK_DIM);
}


/* 对外唯一入口：顺序与 RenderFrame 里原来那两行一致（面板 → 文字）。 */
void UiDrawMenuPages(void) {
    if(currentState==STATE_BESTIARY){UiDrawArchivePage(GetMenuGeo());return;}
    DrawMenuPanels();
    DrawMenuText();
}

static const char* MainMenuVisibleLabel(int index) {
    static const char *const labels[][2]={
        [MENU_CONTINUE]={"Continue","继续游戏"},
        [MENU_STORY]={"Story Prologue","剧情序章：失序协议"},
        [MENU_FIRST_STORY]={"Enter the Facility","进入设施（推荐）"},
        [MENU_CLASSIC]={"Classic Exploration","经典探索"},
        [MENU_ROLES]={"Character Dossiers","角色档案"},
        [MENU_TRAINING]={"Training Course","基础训练"},
        [MENU_UPGRADES]={"Permanent Skills","局外技能"},
        [MENU_BANK]={"Threat Bank","威胁银行"},
        [MENU_MORE]={"Modes & Extras","更多模式与资料"},
        [MENU_QUIT]={"Quit","退出游戏"},
        [MENU_PRACTICE_ARENA]={"Practice Arena","自由训练场"},
        [MENU_DAILY]={"Daily Challenge","每日挑战"},
        [MENU_ENDLESS]={"Endless","无尽模式"},
        [MENU_BESTIARY]={"Field Guide","图鉴"},
        [MENU_ACHIEVEMENTS]={"Achievements","成就档案"},
        [MENU_HELP]={"Help & Skill Index","帮助与技能索引"},
        [MENU_SETTINGS]={"Settings","设置"},
        [MENU_BACK]={"Back","返回"},
        [MENU_WELCOME_PRACTICE]={"Practice First","先练习"},
        [MENU_WELCOME_SKIP]={"Go to Main Menu","直接进入主菜单"}
    };
    MainMenuAction action=MenuActionAt(index);
    if (action>=MENU_ROLE_0 && action<=MENU_ROLE_3) return ProtocolRoleName(action-MENU_ROLE_0);
    return action==MENU_NONE?"":Tr(labels[action][0],labels[action][1]);
}
static void OpenMenu(int page,MainMenuAction focus) {
    mainMenuMore=page;menuSelection=0;uiDirty=1;
    for (int i=0;i<MainMenuCount();i++) if (MenuActionAt(i)==focus) {menuSelection=i;break;}
}
int MainMenuBack(void) {
    if (WelcomeOpen() || mainMenuMore==PM_ROOT) return 0;
    OpenMenu(PM_ROOT,mainMenuMore==PM_ROLES?MENU_ROLES:MENU_MORE);
    return 1;
}
MainMenuAction MainMenuResolve(void) {
    MainMenuAction action=MenuActionAt(menuSelection);
    if (action==MENU_NONE) {menuSelection=0;return MENU_NONE;}
    if (WelcomeOpen()) {
        protocolProfile.briefSeen=1;
        SaveGame();
        OpenMenu(PM_ROOT,MENU_STORY);
        if (action==MENU_WELCOME_SKIP) return MENU_NONE;
        if (action==MENU_WELCOME_PRACTICE) return MENU_TRAINING;
        return action;
    }
    if (action==MENU_BACK) {MainMenuBack();return MENU_NONE;}
    if (action==MENU_ROLES) {OpenMenu(PM_ROLES,MENU_ROLE_0+(int)protocolProfile.selected);return MENU_NONE;}
    if (action==MENU_MORE) {OpenMenu(PM_MORE,MENU_DAILY);return MENU_NONE;}
    if (action>=MENU_ROLE_0 && action<=MENU_ROLE_3) {
        if (ProtocolSelectRole(action-MENU_ROLE_0)) SaveGame();
        uiDirty=1;
        return MENU_NONE;
    }
    return action;
}

void ShopRefreshButtonRect(RECT* rect) {
    MenuGeo g=GetMenuGeo();
    *rect=(RECT){g.panel.right-UIS(245),g.panel.bottom-UIS(43),g.panel.right-UIS(20),g.panel.bottom-UIS(7)};
}

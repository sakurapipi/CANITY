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
#include "lockbox.h"
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

static int mapHeld = 0;

void LoopResetMapHold(void) { mapHeld = 0; }

static void ClickMenu(int mx, int my) {
    MouseToLogical(&mx, &my);
    if (currentState == STATE_HELP) { currentState = STATE_MAIN_MENU; uiDirty = 1; return; }

    /* v1.8：主菜单 / 暂停菜单鼠标点击直接执行。
       用"行带"算下标，矩形差几像素也点得中；没落在行上就选最近的一行，
       保证点击永远有反馈（之前玩家反馈"暂停页鼠标点了没反应"）。 */
    if (currentState == STATE_MAIN_MENU || currentState == STATE_PAUSED) {
        MenuGeo g = GetMenuGeo();
        int idx = -1;
        if (g.rowH > 0 && my >= g.firstY) idx = (my - g.firstY) / g.rowH;
        if (idx < 0 || idx >= g.count) {
            int best = 0, bestD = 1 << 30;
            for (int i = 0; i < g.count; i++) {
                RECT r = RowRect(g, i);
                int d = abs(my - (r.top + r.bottom) / 2);
                if (d < bestD) { bestD = d; best = i; }
            }
            idx = best;
        }
        if (idx >= 0 && idx < g.count) {
            menuSelection = idx;
            ActivateMenuItem();
            uiDirty = 1;
        }
        return;
    }

        /* v2.2 修复：安全屋 / 选武器原来没有命中分支，掉进通用兜底 ——
       点哪一行执行的都是"光标原来那一行"（买错东西、直接用掉跳层）。 */
    if (currentState == STATE_SAFEHOUSE) {
        MenuGeo g2 = GetMenuGeo();
        for (int i = 0; i < g2.count; i++) {
            RECT r = RowRect(g2, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                if (currentState == STATE_SAFEHOUSE) shopRow = i;
                /* 2026-11-08：武器光标已删 */
                ActivateMenuItem();
                return;
            }
        }
        return;
    }

if (currentState == STATE_HUB) {
        {
            int id = SkillNodeHitTest(mx, my);
            if (id >= 0) { hubSelection = id; ActivateHubRow(); }
            return;
        }
        MenuGeo hg = GetMenuGeo();
        for (int i = 0; i < hg.count; i++) {
            RECT r = RowRect(hg, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                hubSelection = i;
                ActivateHubRow();
                return;
            }
        }
        return;
    }

    if (currentState == STATE_SETTINGS) {
        if (settingsPopupOpen) {
            MenuGeo p = GetPopupGeo();
            const SettingItem* it = &settings[popupItem];

            if (it->kind == SET_SLIDER) {
                RECT t = SliderTrackRect(p);
                if (my >= t.top - UIS(16) && my <= t.bottom + UIS(16)) {
                    *it->fvalue = SliderXToValue(p, mx, it->fmin, it->fmax, it->fstep);
                    sliderDragging = 1;
                    PlatSetCapture(1);           /* 拖出窗口也能继续拖 */
                    uiDirty = 1;
                    return;
                }
                if (mx < p.panel.left || mx > p.panel.right || my < p.panel.top || my > p.panel.bottom) {
                    settingsPopupOpen = 0;
                    sliderDragging = 0;
                    uiDirty = 1;
                }
                return;
            }

            for (int i = p.scrollOffset; i < p.count && i < p.scrollOffset + p.maxVisibleRows; i++) {
                RECT r = RowRect(p, i);
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    ApplySetting(popupItem, i);
                    settingsPopupOpen = 0;
                    uiDirty = 1;
                    return;
                }
            }
            /* 点击面板外关闭 */
            if (mx < p.panel.left || mx > p.panel.right || my < p.panel.top || my > p.panel.bottom) {
                settingsPopupOpen = 0;
                uiDirty = 1;
            }
            return;
        }
        MenuGeo g = GetMenuGeo();
        for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                settingsSelection = i;
                ActivateSettingRow();
                return;
            }
        }
        return;
    }

    if (currentState == STATE_ACHIEVEMENTS) {
        MenuGeo g = GetMenuGeo();
        for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                achSelection = i;
                if (i >= ACH_COUNT) {
                    /* 点 Back 直接退出，不是只把光标移过去 */
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                    uiDirty = 1;
                }
                return;
            }
        }
        return;
    }

    if (currentState == STATE_BESTIARY) {
        MenuGeo g = GetMenuGeo();
        for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
            RECT r = RowRect(g, i);
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                bestiarySel = i;
                if (i >= BESTIARY_COUNT) {
                    /* 点 Back 直接退出 */
                    currentState = STATE_MAIN_MENU;
                    menuSelection = 0;
                }
                uiDirty = 1;
                return;
            }
        }
        return;
    }

    if (currentState == STATE_GAMEOVER) { ActivateMenuItem(); return; }

    MenuGeo g = GetMenuGeo();
    for (int i = g.scrollOffset; i < g.count && i < g.scrollOffset + g.maxVisibleRows; i++) {
        RECT r = RowRect(g, i);
        if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
            menuSelection = i;
            ActivateMenuItem();
            return;
        }
    }
}
#ifndef CANITY_WEB
/* CanityKeyDown 会调它；实现在下面的窗口分区里，网页端没有这个函数 */
static void MapCursorRefresh(void);
#endif

/* ==================================================================
 * 滚轮 —— **平台无关**（2026-11-06）
 * ------------------------------------------------------------------
 * 用户实测：「电脑版滚轮失效，导致切换不了投掷物」。
 * 桌面端滚轮走 WM_MOUSEWHEEL（浏览器里没有这个消息）。
 * 这里把三种用途抽出来共用：
 *   · 局内：切换投掷槽位（带阻尼：累加 60 才切一格，切完 90ms 冷却）
 *   · 技能树：缩放
 *   · 改造树：缩放
 * ================================================================== */
void SkillZoomAt(float factor, int mx, int my);
void BenchZoomAt(float factor, int mx, int my);
int  itemHasStock(void);

/* ==================================================================
 * 直接动作入口（2026-11-06 触屏用）
 * ------------------------------------------------------------------
 * 用户实测：「手电开关没什么用」「暂停键没有用」。
 * 原因不是游戏逻辑坏，而是浏览器里这两个键最容易被吞：
 *   · Esc 在全屏时被浏览器拿去退出全屏，页面根本收不到
 *   · F 之类的短按可能落在两次轮询之间
 * 所以触屏不合成按键了，直接调游戏函数。
 * ================================================================== */
/* ==================================================================
 * 2026-11-06（用户实测：「暂停键跟返回键功能差不多，没什么用」）
 * ------------------------------------------------------------------
 * 手机上把三个键（暂停 / 返回 / 退出当前页）合并成一个「菜单」：
 *   · 局内        -> 暂停
 *   · 暂停中      -> 继续
 *   · 技能树/商店/成就/图鉴/银行/设置 -> 存盘并回主菜单
 *   · 主菜单      -> 不动（避免误退）
 * 这样少一个键，也不会出现"点暂停没反应、点返回又不知道会去哪"。
 * ================================================================== */
void CanityMenuAction(void) {
    /* 从任何界面回到局内之前，先退出藏身状态 ——
       否则玩家会觉得"出来了但动不了"。 */
    if (currentState == STATE_SAFEHOUSE && currentState != STATE_PLAYING) {

        hiddenIn = -1;
    }
    switch (currentState) {
    case STATE_PLAYING:
        {  PauseGame(); }
        uiDirty = 1;
        break;
    case STATE_PAUSED:
        currentState = STATE_PLAYING;
        uiDirty = 1;
        break;
    case STATE_SAFEHOUSE:
        /* ==================================================================
         * 2026-11-06（用户实测：**局内商店点进技能树就退不出来**）
         * ------------------------------------------------------------------
         * SAFEHOUSE 是**局内商店**，它的正确出口是 ShopLeave() ——
         * 回到 PLAYING 继续这一局。
         * 我上一版把 SAFEHOUSE 和 HUB 并在一起、一律回主菜单：
         * 那等于一进商店就等于放弃本局，是错的。
         * ================================================================ */
        {  ShopLeave(); }
        uiDirty = 1;
        break;
    case STATE_HUB:
    case STATE_ACHIEVEMENTS:
    case STATE_BESTIARY:
    case STATE_BANK:
    case STATE_SETTINGS:
    case STATE_HELP:
        SaveGame();
        currentState = STATE_MAIN_MENU;
        menuSelection = 0;
        ForceCursor(1);
        uiDirty = 1;
        break;
    default:
        break;
    }
}

void CanityPauseToggle(void) {
    if (currentState == STATE_PLAYING) {

        PauseGame();
        uiDirty = 1;
    } else if (currentState == STATE_PAUSED) {
        currentState = STATE_PLAYING;
        uiDirty = 1;
    }
}

void CanityToggleFlashlight(void) {
    /* 复用键盘那条完整规则（每日限制 / 战役锁定 / 电量） */
    CanityKeyDown((int)g_bindings[ACT_FLASHLIGHT], 0);
    uiDirty = 1;
}

void CanityMouseWheel(int delta, int mx, int my) {
    if (delta == 0) return;

    if (currentState == STATE_HUB) {
        float f = (delta > 0) ? 1.12f : (1.0f / 1.12f);
        SkillZoomAt(f, mx, my);
        uiDirty = 1;
        return;
    }
    if (currentState == STATE_SAFEHOUSE && shopPage == SHP_TREE) {
        float f = (delta > 0) ? 1.12f : (1.0f / 1.12f);
        BenchZoomAt(f, mx, my);
        uiDirty = 1;
        return;
    }
    if (currentState == STATE_PLAYING) {
        static int wheelAcc = 0;
        static double wheelCd = 0.0;

        wheelAcc += delta;
        if (wheelAcc >= 60 || wheelAcc <= -60) {
            if (g_timeSec - wheelCd > 0.09) {
                int dir = (wheelAcc > 0) ? +1 : -1;
                wheelAcc = 0;
                wheelCd = g_timeSec;
                {
                    int n = ITEM_SLOT_COUNT;
                    int s = throwSelected + dir;
                    if (s < 0) s += n;
                    if (s >= n) s -= n;
                    throwSelected = s;
                    /* 落到空槽就继续找下一个有货的，避免"HUD 高亮但拿不出来" */
                    for (int k = 0; k < n && !itemHasStock(); k++) {
                        s = throwSelected + dir;
                        if (s < 0) s += n;
                        if (s >= n) s -= n;
                        throwSelected = s;
                    }
                    uiDirty = 1;
                }
            }
        }
    }
}

/* ==================================================================
 * 鼠标入口 —— **平台无关**（2026-11-06）
 * ------------------------------------------------------------------
 * 网页端没有窗口消息，菜单的悬停/点击原来全在 WndProc 里，
 * 于是"鼠标完全没反应"。
 *
 * 这里只覆盖**菜单类界面**（主菜单 / 暂停 / 设置等）的悬停与点击，
 * 那是玩家在浏览器里最先用到鼠标的地方。游戏内的视角仍走
 * CanityMouseLook（指针锁定后的相对位移），那条路是独立的。
 * ================================================================== */
void CanityMouseMove(int mx, int my) {
    /* 入参已是渲染(=逻辑)空间，见 HitTestMenu 的契约 */
    g_mousePos.x = mx;
    g_mousePos.y = my;
    if (currentState != STATE_PLAYING) {
        HitTestMenu(mx, my);
        uiDirty = 1;
    }
}

void CanityMouseClick(int mx, int my) {
    g_mousePos.x = mx;
    g_mousePos.y = my;

    if (currentState == STATE_MAIN_MENU || currentState == STATE_PAUSED) {
        /* 2026-11-08（第 6 轮）：**点空白处不该激活"当前选中项"**
           —— 原来 HitTestMenu 没命中任何行，ActivateMenuItem 却照样执行，
           等于"点空白 = 点光标那一行"，玩家一点角落就开局。 */
        int hitRow = 0;
        {
            MenuGeo g3 = GetMenuGeo();
            for (int i = 0; i < g3.count; i++) {
                RECT r = RowRect(g3, i);
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    hitRow = 1;
                    break;
                }
            }
        }
        HitTestMenu(mx, my);
        if (hitRow) ActivateMenuItem();
        uiDirty = 1;
        return;
    }
    /* 技能树「返回」按钮：点它就回主菜单（用户报告退不出去） */
    if (currentState == STATE_HUB) {

        if (HubBackButtonAt(mx, my)) {
            SaveGame();
            currentState = STATE_MAIN_MENU;
            menuSelection = 0;
            ForceCursor(1);
            uiDirty = 1;
            return;
        }
    }

    /* 商店/改造树左上角的返回按钮：点了就 ShopLeave（用户报退不出去） */
    if (currentState == STATE_SAFEHOUSE) {

        if (ShopBackButtonAt(mx, my)) {

            ShopLeave();
            return;
        }
    }

    /* 技能树的 + / - 缩放按钮优先命中（点它们不该去选节点） */
    if (currentState == STATE_HUB) {

        int hit = HubZoomButtonAt(mx, my);
        if (hit != 0) {
            SkillZoomAt(hit > 0 ? 1.25f : (1.0f / 1.25f), winW / 2, winH / 2);
            uiDirty = 1;
            return;
        }
    }

    if (currentState == STATE_SETTINGS || currentState == STATE_HUB ||
        currentState == STATE_SAFEHOUSE || currentState == STATE_ACHIEVEMENTS ||
        currentState == STATE_BESTIARY || currentState == STATE_BANK) {
        HitTestMenu(mx, my);
        uiDirty = 1;
    }
}

/* ==================================================================
 * 键盘按下处理 —— **平台无关**
 * ------------------------------------------------------------------
 * 2026-11-06（网页版修复）：这段逻辑原来整段长在 WindowProc 的
 * WM_KEYDOWN 里。网页端没有窗口消息，于是出现
 * "按键确实进了 keys[]，但菜单完全不动"。
 *
 * 现在抽成函数：
 *   Win32 -> WndProc 的 WM_KEYDOWN 调它（vk = wParam, repeat = lParam bit30）
 *   网页  -> 主循环里对每个 0->1 边沿调它（repeat 恒 0）
 *
 * 先处理全局快捷键/模态输入，再按状态分派；每次按键只选择一个处理器。
 * MenuKeyInput 保存本次归一化后的按键与导航语义，保留自动重复规则。
 * ================================================================== */
typedef struct MenuKeyInput {
    int vk, repeat;
    int up, down, left, right, confirm, cancel;
} MenuKeyInput;

static MenuKeyInput MenuKeyRead(int vk, int repeat) {
        int menuUp    = (vk == (int)g_bindings[ACT_MENU_UP])   || vk == 'W';
        int menuDown  = (vk == (int)g_bindings[ACT_MENU_DOWN]) || vk == 'S';
        int menuLeft  = (vk == (int)g_bindings[ACT_MENU_LEFT])  || vk == 'A';   /* v2.8 */
        int menuRight = (vk == (int)g_bindings[ACT_MENU_RIGHT]) || vk == 'D';   /* v2.8 */
        /* v1.8：空格也能确认（很多人不习惯按回车）。
           游戏里空格仍是跳脸虫的躲避键，两者不冲突。 */
        /* ==================================================================
         * 修：**确认键没有挡「键盘自动重复」**
         * ==================================================================
         * 按住 Space / Enter 时 Windows 每秒重发几十次 WM_KEYDOWN。
         * menuUp/menuDown 需要重复（连按可以滚列表），
         * 但 menuConfirm 一旦重复就是灾难：
         *
         *   设置页按 Space → 第 1 次打开弹窗
         *                  → 第 2 次立刻确认并关掉弹窗
         *   玩家看到的就是「空格没反应」。
         *
         * 同文件里 R 键（6282）和投掷槽键（6389）都判断了 bit30，
         * 只有这个聚合量漏了。给 confirm / cancel 加上。
         * ================================================================ */
        int keyRepeat   = (repeat) ? 1 : 0;
        int menuConfirm = !keyRepeat &&
                          ((vk == (int)g_bindings[ACT_MENU_CONFIRM]) || vk == VK_SPACE);
        int menuCancel  = !keyRepeat && (vk == (int)g_bindings[ACT_MENU_CANCEL]);

    MenuKeyInput input = { vk, repeat, menuUp, menuDown, menuLeft, menuRight, menuConfirm, menuCancel };
    return input;
}

static void KeyHandleMainMenu(const MenuKeyInput* key) {
    const int vk = key->vk;
    const int menuUp = key->up;
    const int menuDown = key->down;
    const int menuConfirm = key->confirm;

            /* v3.3 **数字键直接选中并确认** ——
               左边那列 1 2 3 … 本来就是快捷键暗示，但从来没接。
               试玩实测：「按 1 想选 New Run，完全没反应，
               必须方向键+回车」。现在按数字 = 选中该行 + 立刻执行。 */
            /* 2026-11-08（用户要求）：按 B 进引导模式 */
            if (vk == 'B') {
                LoopQueueStart(START_GUIDE, 0);
                StartGame();
                uiDirty = 1;
                return;
            }
            {
                int nk = -1;
                if (vk >= '1' && vk <= '9')      nk = (int)vk - '1';
                else if (vk == '0')                  nk = 9;
                if (nk >= 0 && nk < MainMenuCount()) {
                    menuSelection = nk;
                    ActivateMenuItem();
                    return;
                }
            }
            if (menuUp) { menuSelection = (menuSelection + MainMenuCount() - 1) % MainMenuCount(); uiDirty = 1; }
            if (menuDown) { menuSelection = (menuSelection + 1) % MainMenuCount(); uiDirty = 1; }
            MenuGeo g = GetMenuGeo();
            if (menuSelection < g.scrollOffset) menuScrollOffsets[STATE_MAIN_MENU] = menuSelection;
            else if (menuSelection >= g.scrollOffset + g.maxVisibleRows) menuScrollOffsets[STATE_MAIN_MENU] = menuSelection - g.maxVisibleRows + 1;
            if (menuConfirm) ActivateMenuItem();

}

static void KeyHandlePaused(const MenuKeyInput* key) {
    const int menuUp = key->up;
    const int menuDown = key->down;
    const int menuConfirm = key->confirm;
    const int menuCancel = key->cancel;

            if (menuUp) { menuSelection = (menuSelection + 2) % 3; uiDirty = 1; }
            if (menuDown) { menuSelection = (menuSelection + 1) % 3; uiDirty = 1; }
            if (menuConfirm) ActivateMenuItem();
            if (menuCancel) {
                currentState = STATE_PLAYING;
                resetInputState = 1;   /* Esc 回游戏：吞掉回焦路上可能的残留按下 */
                ForceCursor(0);
                firstMouseMove = 1;
                uiDirty = 1;
            }

}

static void KeyHandleSettings(const MenuKeyInput* key) {
    const int menuUp = key->up;
    const int menuDown = key->down;
    const int menuLeft = key->left;
    const int menuRight = key->right;
    const int menuConfirm = key->confirm;
    const int menuCancel = key->cancel;

            if (settingsPopupOpen) {
                const SettingItem* it = &settings[popupItem];
                if (it->kind == SET_SLIDER) {
                    float v = *it->fvalue;
                    if (menuLeft) v -= it->fstep;
                    if (menuRight) v += it->fstep;
                    if (v < it->fmin) v = it->fmin;
                    if (v > it->fmax) v = it->fmax;
                    if (v != *it->fvalue) { *it->fvalue = v; uiDirty = 1; }
                    if (menuConfirm || menuCancel) {
                        settingsPopupOpen = 0;
                        sliderDragging = 0;
                        SaveGame();   /* 滑块值确认后立刻落盘 */
                        uiDirty = 1;
                    }
                } else {
                    int n = it->count;
                    if (menuUp) { popupSelection = (popupSelection + n - 1) % n; uiDirty = 1; }
                    if (menuDown) { popupSelection = (popupSelection + 1) % n; uiDirty = 1; }
                    if (menuConfirm) { ApplySetting(popupItem, popupSelection); settingsPopupOpen = 0; }
                    if (menuCancel) { settingsPopupOpen = 0; uiDirty = 1; }
                }
            } else {
                int n = SettingsRowCount();
                MenuGeo g = GetMenuGeo();
                if (menuUp) {
                    settingsSelection = (settingsSelection + n - 1) % n;
                    if (settingsSelection < g.scrollOffset) menuScrollOffsets[STATE_SETTINGS] = settingsSelection;
                    uiDirty = 1;
                }
                if (menuDown) {
                    settingsSelection = (settingsSelection + 1) % n;
                    if (settingsSelection >= g.scrollOffset + g.maxVisibleRows) menuScrollOffsets[STATE_SETTINGS] = settingsSelection - g.maxVisibleRows + 1;
                    uiDirty = 1;
                }
                if (menuConfirm) ActivateSettingRow();
                if (menuCancel) {
                    /* Esc 逐级上退：键位页 -> 子目录 -> 根目录 -> 退出 */
                    if (settingsPage == SETPAGE_MOVE || settingsPage == SETPAGE_ACT || settingsPage == SETPAGE_MENUKEYS) {
                        settingsPage = SETPAGE_BINDDIR;
                    } else if (settingsPage == SETPAGE_BINDDIR) {
                        settingsPage = SETPAGE_ROOT;
                    } else if (settingsPage == SETPAGE_DISPLAY || settingsPage == SETPAGE_GAME) {
                        settingsPage = SETPAGE_ROOT;
                    } else {
                        currentState = prevState;
                        SaveGame();
                    }
                    settingsSelection = 0;
                    uiDirty = 1;
                }
            }

}

static void KeyHandleGameOver(const MenuKeyInput* key) {
    const int menuConfirm = key->confirm;

            if (menuConfirm) ActivateMenuItem();

}

static void KeyHandleHelp(const MenuKeyInput* key) {
    const int vk = key->vk;
    const int menuUp = key->up;
    const int menuDown = key->down;
    const int menuConfirm = key->confirm;
    const int menuCancel = key->cancel;

            if (vk == VK_TAB) { helpPage = 1 - helpPage; uiDirty = 1; }
            /* v6 **光标范围要和面板一致** ——
               原来光标在 HELP_ROWS(23) 里滚，但每页只画 8 行，
               移到第 9 行以后屏幕上没有任何高亮（玩家以为帮助页坏了），
               helps[6..12]（瓶子 / 跳脸虫说明）永远看不到。
               现在光标限制在单页 8 行，翻页由 Tab / 光标跨越自动跟随。 */
            {
                int before = menuSelection;
                if (menuUp)   menuSelection = (menuSelection + HELP_PAGE_ROWS - 1) % HELP_PAGE_ROWS;
                if (menuDown) menuSelection = (menuSelection + 1) % HELP_PAGE_ROWS;
                if (menuSelection != before) uiDirty = 1;
                /* 在单页内滚到底再按下 → 翻页 */
                if (menuDown && menuSelection == 0 && before == HELP_PAGE_ROWS - 1)
                    helpPage = (helpPage + 1) % 2;
                if (menuUp && before == 0)
                    helpPage = (helpPage + 1) % 2;
            }
            MenuGeo hg = GetMenuGeo();
            if (menuSelection < hg.scrollOffset) menuScrollOffsets[STATE_HELP] = menuSelection;
            else if (menuSelection >= hg.scrollOffset + hg.maxVisibleRows) menuScrollOffsets[STATE_HELP] = menuSelection - hg.maxVisibleRows + 1;
            if (menuCancel || menuConfirm) { currentState = STATE_MAIN_MENU; uiDirty = 1; }

}

static void KeyHandleSkillPick(const MenuKeyInput* key) {
    const int vk = key->vk;
    const int menuUp = key->up;
    const int menuDown = key->down;
    const int menuConfirm = key->confirm;

            /* v1.8：除了方向键，也接受 W/S（很多玩家习惯 WASD） */
            int up = menuUp || vk == 'W';
            int dn = menuDown || vk == 'S';
            if (up) { skillPickSel = (skillPickSel + INRUN_CHOICES - 1) % INRUN_CHOICES; uiDirty = 1; }
            if (dn) { skillPickSel = (skillPickSel + 1) % INRUN_CHOICES; uiDirty = 1; }
            if (menuConfirm) CloseSkillPick();

}

static void KeyHandleSafehouse(const MenuKeyInput* key) {
    const int vk = key->vk;
    const int repeat = key->repeat;
    const int menuUp = key->up;
    const int menuDown = key->down;
    const int menuLeft = key->left;
    const int menuRight = key->right;
    const int menuConfirm = key->confirm;
    const int menuCancel = key->cancel;

            /* ==================================================================
             * 2026-11-05 修三个「局内改造树」操作 bug
             * ------------------------------------------------------------------
             * ① **空格买不了东西**
             *    这段「空格 = 校准」在按键处理的最前面，而且无条件 return ——
             *    空格本来也算 menuConfirm（下面 ShopActivate 会处理），
             *    却被这里整个吃掉了。改造树页按空格永远没反应。
             *    修：树页不吃空格（让给 menuConfirm）；其它页保持原行为。
             *
             * ② **买完东西画面位置会变 / 左右移动像没动**
             *    原来这里有一行**无条件**的 BenchFocusNode(shopNodeSel) ——
             *    它是「把画布硬居中到选中节点」，每次按键都跑一遍：
             *      · 按回车买东西 → 先居中再买 → 视图跳一下
             *      · 按左右移动     → 改选中后立刻居中 → 节点永远在屏幕正中，
             *                        看起来"没有左右跳跃"，只有背景在滑
             *      · 鼠标拖过画布之后按任意键 → 画布被拽回原位
             *    修：只在**真的从别的页切到树页**时居中一次。
             * ================================================================== */
            if (vk == VK_SPACE && !(repeat) && shopPage != SHP_TREE) {
                if (calibActive) CalibLock();
                else             CalibStart();
                return;
            }
            if (vk == VK_TAB) ShopNextPage();
            /* 只在「切到树页」这一下居中，之后不再干扰玩家的视角 */
            {
                static int s_lastShopPage = -1;
                if (shopPage != s_lastShopPage) {
                    s_lastShopPage = shopPage;
                    if (shopPage == SHP_TREE) BenchFocusNode(shopNodeSel);
                }
            }
            if (vk == VK_HOME && currentState == STATE_HUB) SkillResetView();   /* v6：技能树复位 */
            if (vk == VK_HOME && shopPage == SHP_TREE) BenchResetView();   /* v2.8 */
            if (shopPage == SHP_TREE) {
                /* 2026-11-05：**移动不再强制居中**。
                   原来每次移动都 BenchFocusNode 把画布拉回使选中节点居中，
                   结果"节点永远在屏幕正中、只有背景在滑"，玩家感觉
                   「左右跳跃没反应」。现在只改选中，画布跟着玩家的
                   拖动/滚轮走；看不见时按 HOME 复位。 */
                if (menuUp)    ShopTreeMove(0, -1);
                if (menuDown)  ShopTreeMove(0, 1);
                if (menuLeft)  ShopTreeMove(-1, 0);
                if (menuRight) ShopTreeMove(1, 0);
            } else {
                if (menuUp)      ShopMoveSel(-1);
                if (menuDown)    ShopMoveSel(1);
                /* v2.8：非树页时左右键切页（和顶部按钮一致，不用记 TAB） */
                if (menuLeft)    ShopPrevPage();
                if (menuRight)   ShopNextPage();
            }
            if (menuConfirm) ShopActivate();
            if (menuCancel) {
                /* 2026-11-05：**ESC 和 LEAVE 行走同一条出口**。
                   原来这里各写一遍，ShopLeave 补了 firstMouseMove
                   而这里漏了 —— 于是 ESC 退出仍会视角翻转。
                   一份实现，两处调用，才不会再有这种半修。 */
                ShopLeave();
            }

}

static void KeyHandleHub(const MenuKeyInput* key) {
    const int vk = key->vk;
    const int menuUp = key->up;
    const int menuDown = key->down;
    const int menuLeft = key->left;
    const int menuRight = key->right;
    const int menuConfirm = key->confirm;
    const int menuCancel = key->cancel;

            /* v6 **ESC 必须在所有页都生效** ——
               原来 ESC 处理写在分支最末尾，而银行页用了 break 提前跳出，
               结果在银行页按 ESC 完全没反应，退不出去。
               现在把 ESC 提到最前面，任何页都先判它。 */
            if (menuCancel) {
                SaveGame();
                currentState = STATE_MAIN_MENU;
                menuSelection = 0;
                ForceCursor(1);
                uiDirty = 1;
                return;
            }

            /* v6：**Tab 在 Hub 的 3 页之间循环** ——
               升级列表(0) → 技能树(1) → 银行(2) → 0
               原来完全没有 Tab 处理，但屏幕右上角写着 TAB PAGE x/3，
               玩家按 Tab 毫无反应。 */
            /* v6：Hub **只有一页**，Tab 不再翻页（原来会切到空的属性页） */
            /* v6：**HOME 复位** —— 原来这句写在 SAFEHOUSE 分支里，
               条件恒假，技能树的复位键永远不生效。 */
            if (vk == VK_HOME) { SkillResetView(); return; }
            if (vk == VK_TAB) {
                return;
            }
            /* v2.2 修复：原来这里只处理 ESC —— 方向键和回车一个都不响应，
               而屏幕底部明写 ENTER BUY / ARROWS MOVE。纯键盘玩家
               一分金币都花不出去，整条局外成长线等于锁死。 */
            {
                int cur = hubSelection;
                /* v6：**按边导航** —— 辐射布局里节点不在直线上，
                   col/row 加减会跳到视觉上很远的地方。
                   现在按方向 + 距离找最近的邻居。 */
                int nid = -1;
                if (menuUp)    nid = SkillNavNearest(cur, 0);
                if (menuDown)  nid = SkillNavNearest(cur, 1);
                if (menuLeft)  nid = SkillNavNearest(cur, 2);
                if (menuRight) nid = SkillNavNearest(cur, 3);
                if (1) {
                    /* v6：**退役节点（maxRank 0）跳过** ——
                       原来会停在退役节点上，详情显示 [RETIRED]、
                       买不了，玩家以为卡死。 */
                    if (nid >= 0 && skillDefs[nid].maxRank > 0) {
                        hubSelection = nid;
                        {  SkillFocusNode(nid); }
                        uiDirty = 1;
                    }
                }
                if (menuConfirm) ActivateHubRow();
            }

}

static void KeyHandleBank(const MenuKeyInput* key) {
    const int menuUp = key->up;
    const int menuLeft = key->left;
    const int menuRight = key->right;
    const int menuConfirm = key->confirm;
    const int menuCancel = key->cancel;

            /* v6 **独立银行页的输入** —— 全部符合玩家习惯：
                 ESC                     返回主菜单
                 Enter / Space           下一个携带比例
                 S / ↓ / →               下一个比例
                 W / ↑ / ←               上一个比例
               鼠标点击在别处处理（点哪都算回车）。 */
            if (menuCancel) {
                SaveGame();
                currentState = STATE_MAIN_MENU;
                menuSelection = 0;
                ForceCursor(1);
                uiDirty = 1;
                return;
            }
            {

                int moved3 = 0;
                if (menuConfirm || menuRight) { bankCarryPct = (bankCarryPct + 1) % BANK_CARRY_STEPS; moved3 = 1; }
                else if (menuUp || menuLeft)             { bankCarryPct -= 1; moved3 = 1; }
                if (moved3) { PlaySfx(SFX_POWER, 0.6f); uiDirty = 1; }
            }

}

static void KeyHandleBestiary(const MenuKeyInput* key) {
    const int menuUp = key->up;
    const int menuDown = key->down;
    const int menuConfirm = key->confirm;
    const int menuCancel = key->cancel;

            int bn = BESTIARY_COUNT + 1;
            if (menuUp) { bestiarySel = (bestiarySel + bn - 1) % bn; uiDirty = 1; }
            if (menuDown) { bestiarySel = (bestiarySel + 1) % bn; uiDirty = 1; }
            MenuGeo bg = GetMenuGeo();
            if (bestiarySel < bg.scrollOffset) menuScrollOffsets[STATE_BESTIARY] = bestiarySel;
            else if (bestiarySel >= bg.scrollOffset + bg.maxVisibleRows)
                menuScrollOffsets[STATE_BESTIARY] = bestiarySel - bg.maxVisibleRows + 1;
            if (menuConfirm || menuCancel) {
                currentState = STATE_MAIN_MENU;
                menuSelection = 0;
                uiDirty = 1;
            }

}

static void KeyHandleAchievements(const MenuKeyInput* key) {
    const int vk = key->vk;
    const int menuUp = key->up;
    const int menuDown = key->down;
    const int menuConfirm = key->confirm;
    const int menuCancel = key->cancel;

            /* v6：**分类筛选** —— 32 个平铺看不完，TAB / Q / E 切分类 */
            if (vk == VK_TAB || vk == (int)'E') { AchCycleFilter(1); menuScrollOffsets[STATE_ACHIEVEMENTS] = 0; uiDirty = 1; return; }
            if (vk == (int)'Q') { AchCycleFilter(-1); menuScrollOffsets[STATE_ACHIEVEMENTS] = 0; uiDirty = 1; return; }
            int n = AchVisibleCount() + 1;   /* +1 = Back 行 */
            if (menuUp) { achSelection = (achSelection + n - 1) % n; uiDirty = 1; }
            if (menuDown) { achSelection = (achSelection + 1) % n; uiDirty = 1; }
            MenuGeo ag = GetMenuGeo();
            if (achSelection < ag.scrollOffset) menuScrollOffsets[STATE_ACHIEVEMENTS] = ag.scrollOffset;
            else if (achSelection >= ag.scrollOffset + ag.maxVisibleRows) menuScrollOffsets[STATE_ACHIEVEMENTS] = achSelection - ag.maxVisibleRows + 1;
            if (menuCancel || (menuConfirm && achSelection >= AchVisibleCount())) {
                currentState = STATE_MAIN_MENU;
                menuSelection = 0;
                uiDirty = 1;
            }

}

static void KeyHandlePlaying(const MenuKeyInput* key) {
    const int vk = key->vk;

        /* ================================================================
         * v7 第 51 轮：**商店浮层接管按键**
         * ================================================================ */
        {

            if (shopOverlayOpen) {
                int n = ShopRowCount();
                if (n < 1) n = 1;
                if (vk == VK_ESCAPE) {
                    shopOverlayOpen = 0;
                    ForceCursor(0);
                    resetInputState = 1;
                    uiDirty = 1;
                    return;
                } else if (vk == VK_UP) {
                    shopRow = (shopRow + n - 1) % n;
                    uiDirty = 1;
                    return;
                } else if (vk == VK_DOWN) {
                    shopRow = (shopRow + 1) % n;
                    uiDirty = 1;
                    return;
                } else if (vk == VK_RETURN || vk == VK_SPACE) {
                    if (shopRow >= n - 1) {
                        shopOverlayOpen = 0;
                        ForceCursor(0);
                        resetInputState = 1;
                    } else {
                        ShopActivate();
                    }
                    uiDirty = 1;
                    return;
                }
                return;
            }
        }

            if (vk == (int)g_bindings[ACT_PAUSE]) { autoPaused = 0; PauseGame(); }
            if (vk == (int)g_bindings[ACT_MAP]) {
                /* v2.4：M = 放大地图（全屏）。再按一次 M 关掉。
                   原来是 M 三态循环（小->全->关），一个键要按三次才回到游戏，不直观。 */
                if (isMapOpen && mapFullScreen) {
                    isMapOpen = 0;
                    mapFullScreen = 0;
                } else {
                    isMapOpen = 1;
                    mapFullScreen = 1;
                    TutorialRecordMapOpened();
                }
#ifndef CANITY_WEB
                MapCursorRefresh();
#endif
            }
            if (vk == VK_TAB) {
                /* v2.5：TAB 改成按住看、松开关。
                   原来按一下开再按一下关，玩家很容易一直开着跑图，
                   而地图每秒 -10 电，一趟下来电就空了。
                   按住之后，"看一眼"的成本天然就是看了多久。 */
                isMapOpen = 1;
                mapFullScreen = 0;
                mapHeld = 1;
                TutorialRecordMapOpened();
                #ifndef CANITY_WEB
                MapCursorRefresh();
                #endif
            }
            if (vk == (int)g_bindings[ACT_FLASHLIGHT]) {
                /* ==========================================================
                 * 2026-11-05 修：**两个规则抢手电，而且优先级判反了**
                 * ----------------------------------------------------------
                 * 旧顺序：先判战役「手电关不掉」→ 直接 return，
                 *         每日的「今天不能开手电」永远轮不到。
                 *
                 * 后果（用户报告）：「战役模式左脑打右脑又能重新开那个手电筒」
                 *   —— 战役关按 F 会无条件把手电打开，
                 *      连每日 NOLIGHT 限制和 SALVAGE keystone 都被绕过。
                 *
                 * 正确优先级：**禁止 > 强制 > 自由**
                 *   ① 禁止（每日 NOLIGHT / SALVAGE）最高 ——
                 *      它关系到每日任务的公平性，任何模式都不能覆盖它。
                 *   ② 强制（战役「关不掉」）次之。
                 *   ③ 都允许时才自由切换。
                 * ========================================================== */
                if (!FlashlightUseAllowed()) {
                    flashlightOn = 0; uiDirty = 1;
                    /* 2026-11-05：**提示必须说清是哪一个原因**。
                       原来两种情况都显示「今天不能开手电」，
                       于是买了 SALVAGE 的玩家会以为
                       「普通关卡被每日任务篡改了标志位」——
                       其实是他自己买的天赋永久禁灯。 */
                    { 
                      /* P1-1：SALVAGE 不再走到这里（它已经不拦开灯了），
                         所以只剩"每日限制"这一种原因。 */
                      if (DailyForbidLight())
                          pickupMsg = g_lang == LANG_ZH ? "今天不能开手电（每日限制）"
                                                        : "NO FLASHLIGHT TODAY (DAILY RULE)";
                      else
                          pickupMsg = g_lang == LANG_ZH ? "现在不能开手电" : "CANNOT USE FLASHLIGHT"; }
                    pickupMsgTimer = 1.6f;
                    return;
                }
                { 
                  if (CampaignFlashlightLocked()) {
                      flashlightOn = 1; uiDirty = 1;
                      pickupMsg = g_lang == LANG_ZH ? "这一关手电关不掉" : "FLASHLIGHT IS LOCKED ON";
                      pickupMsgTimer = 1.2f;
                      return;
                  } }
                if (battery > 0.0f) flashlightOn = !flashlightOn;
                uiDirty = 1;
            }

}

static int KeyHandleGlobalShortcut(int vk, int repeat) {

        /* v1.8：F11 或 Alt+Enter 切换最大化 / 还原（菜单里也能用） */
        /* v1.9：F9 导出当前帧的 UI 布局，给 tools/ui_layout_check.py 做重叠检查 */
        if (vk == VK_F9) {
            DumpUILayout("ui_layout.txt");
            return 1;
        }
        /* ==================================================================
         * v7 第 63 轮：**技能树重置入口**
         * ==================================================================
         * SkillResetAll() 之前连实现都没有（skills.h 有原型、
         * skills.c 里被 git 冲掉了），META RESET 成就因此永远拿不到。
         *
         * 入口：Hub 里按 R，**两秒内再按一次**才真的重置。
         * 破坏性操作要二次确认，但不用弹窗（弹窗会打断键盘流）。
         * ================================================================ */
        if(currentState==STATE_SAFEHOUSE && shopPage==SHP_TREE && vk=='R') {
            static DWORD lastReset=0; DWORD now=PlatTicksMs();
            if(repeat) return 1;
            if(lastReset && now-lastReset<2000) { BenchRefundRun();lastReset=0;pickupMsg=Tr("MODS RESET - GOLD REFUNDED","改造已重置，金币已退还"); }
            else {lastReset=now;pickupMsg=Tr("PRESS R AGAIN TO RESET MODS","再按 R 重置本局改造");}
            pickupMsgTimer=2.0f;uiDirty=1;return 1;
        }
        if(currentState==STATE_SAFEHOUSE && shopPage==SHP_SHOP && vk=='R') { if(!(repeat)) ShopRefresh(); return 1; }
        if (currentState == STATE_HUB && vk == 'R') {
            if (repeat) return 1;
            static DWORD lastR = 0;
            DWORD now = PlatTicksMs();
            if (lastR != 0 && (now - lastR) < 2000) {

                SkillResetAll();
                pickupMsg = Tr("SKILLS RESET - GOLD REFUNDED", "技能已重置，金币已退还");
                pickupMsgTimer = 2.5f;
                lastR = 0;
                uiDirty = 1;
            } else {

                pickupMsg = Tr("PRESS R AGAIN TO RESET ALL SKILLS",
                               "再按一次 R 重置全部技能");
                pickupMsgTimer = 2.0f;
                lastR = now;
                uiDirty = 1;
            }
            return 1;
        }

        if (vk == VK_F11 ||
            (vk == VK_RETURN && PlatKeyDown(VK_MENU))) {
            /* v1.24：F11 改成**真全屏切换**（无边框铺满整块屏），
               原来只是最大化窗口 —— 还留着标题栏和边框，看起来像窗口化。 */
            if (PlatMinimized()) PlatRestoreWindow();
            isFullscreen = isFullscreen ? 0 : 1;
            ApplyDisplayMode();
            uiDirty = 1;
            return 1;
        }

    return 0;
}

static int KeyHandleLock(int vk) {
        if (lockActive) {
            /* v3.3 **真的只认 Esc / M** ——
               注释一直这么写，代码却是「除打开键外任何键都关」，两回事。
               试玩实测：「解开锁盘时按一次 W，面板立刻消失，
               拼到一半的进度被丢掉」。玩家解谜时习惯性按 WASD，
               这个键位必然误触。
               现在白名单关闭：Esc / M / 打开锁盘的那个键（防止自动重复）。 */
            if ((int)vk == lockIgnoreVK) return 1;      /* 打开它的那一下 */
            if ((int)vk != VK_ESCAPE && (int)vk != 'M') {
                /* 其它键全部吞掉，不关面板、也不透传到游戏里 */
                return 1;
            }
            CloseLockOverlay();
            return 1;
        }

    return 0;
}

static int KeyPrepareInput(int* code, int repeat) {
    int vk = *code;
        /* 小键盘数字（NumLock 开启时是 VK_NUMPAD0..9）统一折算成主键盘数字，
           这样 1/2/3 选投掷槽位、以及改绑数字键都不会因为按小键盘而失效。 */
        if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
            vk = (int)('0' + (vk - VK_NUMPAD0));
        /* 改绑模式：吞掉所有按键（Esc 取消），不进菜单导航 */
        if (currentState == STATE_SETTINGS && rebindingAct >= 0) {
            RebindConsumeKey((int)vk);
            return 1;
        }
        /* v2.9：**只在游戏中才写 keys[]**。
           这是「面板里按 A/D 会漂移」的真正根因：
           按住 A/D 时 Windows 会以每秒几十次重发 WM_KEYDOWN（键盘自动重复），
           这里每次都把 keys['A'] 设回 1，而 PollInput 每帧清零一次 ——
           两条路径打架，结果就是玩家被推着走。
           菜单导航靠的是下面那些 vk 比较，不需要 keys[]。 */
        if (vk < 256) {
            if (currentState == STATE_PLAYING) {
                keys[vk] = 1;
                keyPressed[vk] = 1;   /* 边沿，供 QTE 消费 */
            }
        }

        /* ================================================================
         * v7 第 ㊿ 轮：**T 键在这里直接处理**
         * ================================================================
         * 原来 T 是在 Update 里读 keyPressed['T'] 的 —— 但
         * Update 里前面有若干 return / 提前分支，键很容易被跳过，
         * 而且玩家看到的症状就是「按 T 完全没反应」。
         *
         * 这里和「切投掷槽位」一样，在消息里立即处理，
         * 不依赖 Update 走到哪一行。
         * ================================================================ */
        if (currentState == STATE_PLAYING && vk == 'T') {
            /* v7 第 ㊿ 轮：**改回切状态**
               崩溃的真正原因是 ShopPriceAt() 无限递归（已修），
               不是这条状态路径。而这条路径是进改造树 / 局内强化 /
               休息 / 校准小游戏的**唯一入口**，不能没有。 */

            ShopOpen(currentLevel);
            currentState = STATE_SAFEHOUSE;
            shopPage = 0;
            shopRow = 0;
            keyPressed['T'] = 0;   /* 否则 Update 里那条还会再开一次 */
            ForceCursor(1);
            uiDirty = 1;
            return 1;
        }

        /* 切投掷槽位：在消息里即时处理，后按的键立即生效。
           bit30 = 系统自动重复；按住不放时不重复覆盖，否则会压住另一个键。
           （绑到鼠标键的情况在 Update 里轮询兜底。） */
        if (currentState == STATE_PLAYING && !(repeat)) {
            if (!LoopIsMouseButton(g_bindings[ACT_SELECT_1]) &&
                vk == (int)g_bindings[ACT_SELECT_1]) LoopSelectVisibleSlot(0);
            else if (!LoopIsMouseButton(g_bindings[ACT_SELECT_2]) &&
                vk == (int)g_bindings[ACT_SELECT_2]) LoopSelectVisibleSlot(1);
            else if (!LoopIsMouseButton(g_bindings[ACT_SELECT_3]) &&
                vk == (int)g_bindings[ACT_SELECT_3]) LoopSelectVisibleSlot(2);
        }

        /* 统一菜单导航按键判断 */
        /* v1.8：W/S 也能上下选菜单（很多玩家习惯 WASD，不想切手去按方向键） */
        /* v2.2：删掉 C 键开战役 —— 它会 DeleteSave()，按错就删档。
           战役/每日已进主菜单（第 7/8 行），那里先 SaveGame。 */
        /* v2.2：同上，删掉 D 键开每日。 */
        /* v2.0：安全屋是模态的，原来只能按 ESC / 选 LEAVE 出去，
           玩家一进去就像卡住。现在按任意移动键直接关门。 */
        /* v2.2 修复：上一版加了「W/A/S/D 直接关门」，但它放在菜单导航**之前**，
           把后面本该消费 W/S 的代码整段变成死代码 —— 想用 S 往下选，面板直接关，
           再按 E 进来光标又回到第 1 行，永远够不到第 2/3 行。
           现在只在玩家**同时按住方向无关的移动键组合**（A/D 横移）时才关门，
           W/S 留给导航；想退出请按 ESC 或选 LEAVE。 */
        /* v2.9：**真的删掉了**「A/D 关门」。
           上一版我以为删了，其实补丁打到了同文案的另一处，块还在 4345 行 ——
           结果按住 A/D 面板直接被关掉，state 从 11 跳回 1，玩家立刻被推着走。
           这就是「A/D 漂移」纠缠了这么多轮的真相。 */

    *code = vk;
    return 0;
}

int CanityKeyDown(int vk, int repeat) {
    if (KeyHandleGlobalShortcut(vk, repeat)) return 0;
    if (KeyHandleLock(vk)) return 0;
    if (KeyPrepareInput(&vk, repeat)) return 0;
    MenuKeyInput key = MenuKeyRead(vk, repeat);
    /* Select exactly one handler; state changes do not redispatch this key. */
    switch (currentState) {
    case STATE_MAIN_MENU: KeyHandleMainMenu(&key); break;
    case STATE_PAUSED: KeyHandlePaused(&key); break;
    case STATE_SETTINGS: KeyHandleSettings(&key); break;
    case STATE_GAMEOVER: KeyHandleGameOver(&key); break;
    case STATE_HELP: KeyHandleHelp(&key); break;
    case STATE_SKILL_PICK: KeyHandleSkillPick(&key); break;
    case STATE_SAFEHOUSE: KeyHandleSafehouse(&key); break;
    case STATE_HUB: KeyHandleHub(&key); break;
    case STATE_BANK: KeyHandleBank(&key); break;
    case STATE_BESTIARY: KeyHandleBestiary(&key); break;
    case STATE_ACHIEVEMENTS: KeyHandleAchievements(&key); break;
    case STATE_PLAYING: KeyHandlePlaying(&key); break;
    default: break;
    }
    return 0;
}

#ifndef CANITY_WEB
/* 地图形态变了之后刷新鼠标状态：**只有全屏图**才释放光标。
   小地图是角落叠层，鼠标继续锁在中心，边走边看。 */

/* ==================== 世界 · 关卡 ==================== */
static void MapCursorRefresh(void) {
    int wantCursor = (isMapOpen && mapFullScreen) ? 1 : 0;
    ForceCursor(wantCursor);
    if (wantCursor) /* 2026-11-06（R4）：光标样式归平台层；ForceCursor(1) 已处理可见性 */
    firstMouseMove = 1;
    uiDirty = 1;
}

static LRESULT WindowMouseMove(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    (void)hwnd; (void)uMsg; (void)wParam; (void)lParam;

            /* v6：技能树拖拽中 */
            if (currentState == STATE_HUB && SkillIsDragging()) {
                int wx = (short)LOWORD(lParam), wy = (short)HIWORD(lParam);
                POINT pt = { wx, wy };
                ScreenToClient(g_hwnd, &pt);
                int mx2 = pt.x, my2 = pt.y;
                MouseToLogical(&mx2, &my2);
                SkillDragMove(mx2, my2);
                return 0;
            }
            /* v3.0：**锁盘打开时也必须更新鼠标位置**。
               原来 g_mousePos 只在 currentState != STATE_PLAYING 时才写，
               而锁盘是游戏中打开的 → 坐标永远是 {0,0}，
               LockHitTest 全部落空，玩家点哪里都没反应。
               这是「锁盘每一个效果都不行」的根因。 */
            if (lockActive) {
                int lmx = (short)LOWORD(lParam), lmy = (short)HIWORD(lParam);
                MouseToLogical(&lmx, &lmy);
                g_mousePos.x = lmx;
                g_mousePos.y = lmy;
                /* 锁盘打开时鼠标是 UI 指针，不能再回中/转视角 */
                uiDirty = 1;
                return 0;
            }

            /* v3.0：安全屋行悬停 —— 记录鼠标落在第几行，供绘制高亮 */
            if (currentState == STATE_SAFEHOUSE) {
                int hx = (short)LOWORD(lParam), hy = (short)HIWORD(lParam);
                MouseToLogical(&hx, &hy);
                MenuGeo hg = GetMenuGeo();
                int f = -1;
                for (int i = 0; i < hg.count; i++) {
                    RECT r = RowRect(hg, i);
                    if (hx >= r.left && hx <= r.right && hy >= r.top && hy <= r.bottom) { f = i; break; }
                }
                if (f != safehouseHoverRow) { safehouseHoverRow = f; uiDirty = 1; }
                /* v3.0：页签悬停也一起算（原来这个变量全工程无人赋值） */
                {
                    int ht = -1;
                    /* v6：**TREE(2) / UPGRADE(4) 两页已退役** ——
                       Tab 会跳过它们，鼠标也必须跳过，否则键盘进不去、
                       鼠标却点得进，行为不一致。 */
                    for (int t = 0; t < SAFEHOUSE_PAGES; t++) {
                        /* v6：**不再跳过任何页** —— 5 个页签全可用 */
                        int tx0, ty0, tx1, ty1;
                        PageTabRect(t, &tx0, &ty0, &tx1, &ty1);
                        if (hx >= tx0 && hx <= tx1 && hy >= ty0 && hy <= ty1) { ht = t; break; }
                    }
                    if (ht != safehouseHoverTab) { safehouseHoverTab = ht; uiDirty = 1; }
                }
            }

            /* v2.9：改造树画布拖拽（自绘光标已删除，这里只服务拖拽） */
            if (currentState == STATE_SAFEHOUSE && shopPage == SHP_TREE) {
                int rx = (short)LOWORD(lParam), ry = (short)HIWORD(lParam);
                MouseToLogical(&rx, &ry);
                BenchDragMove(rx, ry);
            }
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            /* v1.8：三选一面板支持鼠标悬停选中 */
            if (currentState == STATE_SKILL_PICK) {
                MenuGeo sg = GetMenuGeo();
                for (int i = 0; i < INRUN_CHOICES; i++) {
                    RECT rr = RowRect(sg, i);
                    if (my >= rr.top && my < rr.bottom) {
                        if (skillPickSel != i) { skillPickSel = i; uiDirty = 1; }
                        break;
                    }
                }
                return 0;
            }

            if (currentState != STATE_PLAYING) {
                if (currentState == STATE_SETTINGS && rebindingAct >= 0) return 0;
                if (settingsPopupOpen && sliderDragging && popupItem >= 0 &&
                    settings[popupItem].kind == SET_SLIDER) {
                    const SettingItem* it = &settings[popupItem];
                    MenuGeo p = GetPopupGeo();
                    int lmx = mx, lmy = my;
                    MouseToLogical(&lmx, &lmy);   /* 拖拽坐标也必须换到 UI 逻辑空间 */
                    float v = SliderXToValue(p, lmx, it->fmin, it->fmax, it->fstep);
                    if (v != *it->fvalue) { *it->fvalue = v; uiDirty = 1; }
                    return 0;
                }
                g_mousePos.x = mx;
                g_mousePos.y = my;
                {
                    /* HitTestMenu 现在要逻辑坐标（见它的注释） */
                    int hmx = mx, hmy = my;
                    MouseToLogical(&hmx, &hmy);
                    HitTestMenu(hmx, hmy);
                }
                return 0;
            }
            /* 全屏地图：鼠标交给菜单（此时鼠标是释放的，可以点东西）。
               小窗地图：**不拦截** —— 鼠标仍然锁定在中心，
               玩家可以边看小地图边转视角、继续玩。 */
            if (isMapOpen && mapFullScreen) return 0;

            /* v1.8：中心点必须用**实时客户区**算。
               原来用 winW/winH（固定 800x600），最大化后真窗口是 1711x964，
               光标会被 SetCursorPos 拉到左上角，表现为"最大化了鼠标就用不了"。 */
            RECT cr;
            GetClientRect(hwnd, &cr);
            int cw2 = cr.right - cr.left, ch2 = cr.bottom - cr.top;
            /* 画面现在铺满整个客户区，光标就回到客户区中心 */
            POINT center = { cw2 / 2, ch2 / 2 };
            ClientToScreen(hwnd, &center);

            POINT currentPos;
            GetCursorPos(&currentPos);
            if (firstMouseMove) {
                firstMouseMove = 0;
                SetCursorPos(center.x, center.y);
                lastMousePos = center;
                return 0;
            }
            /* SetCursorPos 回到中心本身又会触发一次 WM_MOUSEMOVE，
               这种“回声”必须丢弃，否则消息队列被自己灌满，帧率会塌 */
            if (currentPos.x == center.x && currentPos.y == center.y) return 0;

            int dx = currentPos.x - lastMousePos.x;
            int dyv = currentPos.y - lastMousePos.y;
            CanityMouseLook(dx, dyv);
            SetCursorPos(center.x, center.y);
            lastMousePos = center;
            return 0;
}

static LRESULT WindowMouseWheel(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    (void)hwnd; (void)uMsg; (void)wParam; (void)lParam;

            /* v6：**技能树页用滚轮缩放**（和改造树同一套锚点逻辑） */
            if (currentState == STATE_HUB) {
                int wx = (short)LOWORD(lParam), wy = (short)HIWORD(lParam);
                POINT pt = { wx, wy };
                ScreenToClient(g_hwnd, &pt);
                int hx = pt.x, hy = pt.y;
                MouseToLogical(&hx, &hy);
                float hf = (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 1.12f : (1.0f / 1.12f);
                SkillZoomAt(hf, hx, hy);
                return 0;
            }
            /* v2.8：改造树页用滚轮缩放（以鼠标为锚点） */
            if (currentState == STATE_SAFEHOUSE && shopPage == SHP_TREE) {
                int wx = (short)LOWORD(lParam), wy = (short)HIWORD(lParam);
                POINT pt = { wx, wy };
                ScreenToClient(g_hwnd, &pt);
                int lx2 = pt.x, ly2 = pt.y;
                MouseToLogical(&lx2, &ly2);
                float f = (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 1.12f : (1.0f / 1.12f);
                BenchZoomAt(f, lx2, ly2);
                return 0;
            }
            /* v1.8：游玩中用滚轮切换投掷物（1/2/3 仍然可用） */
            if (currentState == STATE_PLAYING) {
                /* v1.14：滚轮阻尼。
                   ① 累加 delta，够 60 才切一格（原来一格 = 一次滚，太敏感）
                   ② 切换后 90ms 内不再响应（防止高精度滚轮连发）
                   ③ 顺手修 bug：原来模 3，但只有 ITEM_SLOT_COUNT 格 →
                      会选到不存在的槽位，HUD 数组越界。 */
                static int wheelAcc = 0;
                static double wheelCd = 0.0;
                int dz = GET_WHEEL_DELTA_WPARAM(wParam);
                wheelAcc += dz;
                if (wheelAcc >= 60 || wheelAcc <= -60) {
                    if (g_timeSec - wheelCd > 0.09) {
                        int dir = (wheelAcc > 0) ? +1 : -1;
                        /* ============================================================
                         * 2026-11-05 修 bug：**滚轮永久卡死**
                         * ------------------------------------------------------------
                         * 旧写法是 (sel + dir + 6) % 6 —— 会落在**空槽**上。
                         * 而 Update() 每帧都调 EnsureThrowSelectedValid()，
                         * 它发现当前槽空着，就把选中拽回"第一个有货的槽"。
                         *
                         * 于是：槽0 有货、槽1 空 → 滚轮 +1 到槽1 → 下一帧拽回槽0
                         *       → 再滚 +1 又到槽1 又拽回槽0 …… **永远出不去**。
                         * 玩家看到的就是"滚轮切换不出来"。
                         *
                         * 现在直接跳到**下一个有货的槽**，空槽整个跳过。
                         * ============================================================ */
                        int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs,
                                             gluePots, echoBolts, decoys, phaseBeacons };
                        int moved = 0;
                        for (int step = 0; step < ITEM_SLOT_COUNT; step++) {
                            throwSelected = (throwSelected + dir + ITEM_SLOT_COUNT) % ITEM_SLOT_COUNT;
                            if (counts[throwSelected] > 0) { moved = 1; break; }
                        }
                        if (!moved) throwSelected = 0;   /* 一件都没有：回 0 号槽 */
                        wheelCd = g_timeSec;
                        uiDirty = 1;
                    }
                    wheelAcc = 0;
                }
                return 0;
            }

            /* 鼠标滚轮滚动菜单 */
            if (currentState != STATE_PLAYING && !settingsPopupOpen) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                MenuGeo g = GetMenuGeo();
                if (g.count > g.maxVisibleRows) {
                    int maxOffset = g.count - g.maxVisibleRows;
                    if (maxOffset < 0) maxOffset = 0;
                    if (delta > 0) {
                        /* 向上滚动 */
                        menuScrollOffsets[currentState] = (menuScrollOffsets[currentState] > 0) ? menuScrollOffsets[currentState] - 1 : 0;
                    } else if (delta < 0) {
                        /* 向下滚动 */
                        menuScrollOffsets[currentState] = (menuScrollOffsets[currentState] < maxOffset) ? menuScrollOffsets[currentState] + 1 : maxOffset;
                    }
                    uiDirty = 1;
                    InvalidateRect(g_hwnd, NULL, FALSE);  /* 强制重绘 */
                }
            }
            return 0;
}

static LRESULT WindowLeftDown(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    (void)hwnd; (void)uMsg; (void)wParam; (void)lParam;

            /* v6：**技能树拖拽平移** —— 按下即开始拖，和改造树一致 */
            if (currentState == STATE_HUB) {
                int wx = (short)LOWORD(lParam), wy = (short)HIWORD(lParam);
                POINT pt = { wx, wy };
                ScreenToClient(g_hwnd, &pt);
                int dx = pt.x, dy = pt.y;
                MouseToLogical(&dx, &dy);
                /* ==================================================
                 * 2026-11-08（用户实机反馈）：**返回键和 +/- 按钮点了没反应**
                 * --------------------------------------------------
                 * 原来这里无条件 SkillDragStart + return 0 ——
                 * 技能树里每一次左键按下都被"拖拽"吞掉，按钮永远轮不到。
                 * 现在先问按钮，都没命中才开始拖。
                 * ================================================== */
                {

                    if (HubBackButtonAt(dx, dy)) {
                        SaveGame();
                        currentState = STATE_MAIN_MENU;
                        menuSelection = 0;
                        ForceCursor(1);
                        uiDirty = 1;
                        return 0;
                    }
                    {
                        int hit = HubZoomButtonAt(dx, dy);
                        if (hit != 0) {
                            SkillZoomAt(hit > 0 ? 1.25f : (1.0f / 1.25f),
                                        winW / 2, winH / 2);
                            uiDirty = 1;
                            return 0;
                        }
                    }
                }
                SkillDragStart(dx, dy);
                return 0;
            }
            /* v3.0：锁盘打开时，鼠标全部归锁盘 */
            if (lockActive) {
                int lx = (short)LOWORD(lParam), ly = (short)HIWORD(lParam);
                MouseToLogical(&lx, &ly);
                LockClick(lx, ly);
                uiDirty = 1;
                return 0;
            }
            /* v2.8：安全屋页签 + 改造树节点点击 */
            if (currentState == STATE_SAFEHOUSE) {
                int mx0 = (short)LOWORD(lParam), my0 = (short)HIWORD(lParam);
                MouseToLogical(&mx0, &my0);
                /* 页签 */
                for (int t = 0; t < SAFEHOUSE_PAGES; t++) {
                        /* v6：**退役页（TREE/UPGRADE）鼠标也要跳过** ——
                           hover 循环跳过了、click 循环没跳，
                           结果键盘进不去、鼠标却点得进，行为自相矛盾。 */
                        /* v6：**不再跳过任何页** —— 5 个页签全可用 */
                    int x0, y0, x1, y1;
                    PageTabRect(t, &x0, &y0, &x1, &y1);
                    if (mx0 >= x0 && mx0 <= x1 && my0 >= y0 && my0 <= y1) {
                        shopPage = t;
                        shopRow = 0;
                        uiDirty = 1;
                        return 0;
                    }
                }
                /* 空白处按住 → 开始拖拽画布（v2.9，和思维导图一致） */
                if (shopPage == SHP_TREE) {
                    BenchDragStart(mx0, my0);
                }
                /* 树节点 */
                if (shopPage == SHP_TREE) {
                    for (int id = 0; id < BENCH_COUNT; id++) {
                        int cx = (int)(BenchNodeWorldX(id) * treeVK + treeVX);
                        int cy = (int)(BenchNodeWorldY(id) * treeVK + treeVY);
                        int hw = (int)(75.0f * treeVK), hh = (int)(27.0f * treeVK);
                        if (mx0 >= cx - hw && mx0 <= cx + hw && my0 >= cy - hh && my0 <= cy + hh) {
                            shopNodeSel = id;
                            uiDirty = 1;
                            return 0;
                        }
                    }
                }
            }
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);

            /* v2.2 修复：原来这里无条件 CloseSkillPick() ——
               点面板外、点边框、误点一下都算选定（一局 24 层要弹 23 次）。
               现在必须真的点在某张卡上。 */
            if (currentState == STATE_SKILL_PICK) {
                ClickMenu(mx, my);
                return 0;
            }
            if (currentState != STATE_PLAYING) {
                /* 改绑模式：点击也可以绑定为「鼠标左键」 */
                if (currentState == STATE_SETTINGS && rebindingAct >= 0) {
                    RebindConsumeKey(VK_LBUTTON);
                    return 0;
                }
                ClickMenu(mx, my);
            }
            return 0;
}

static LRESULT WindowSizeChanged(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    (void)hwnd; (void)uMsg; (void)wParam; (void)lParam;

            /* v2.2 重大修复（用户实测：窗口一大，UI 全是毛病）：
             *
             * 原来这里**只重建渲染缓冲，从不更新 winW/winH**。
             * 而 winW/winH 是"窗口客户区尺寸"，被用在四处关键地方：
             *   ① UIS() / TextHeightWin() / TextScale() —— 整个 UI 的布局与字号
             *   ② MouseToLogical()                        —— 鼠标坐标换算
             *   ③ GetMenuGeo() 的面板位置
             *   ④ 呈现路径的拉伸目标
             * 于是窗口拉到 1644x997 后，UI 仍按 800x600 排版：面板只占左半边，
             * 鼠标点击按错的坐标命中。用户看到的"一大就全是 bug"就是这个。
             *
             * 现在：客户区一变就同步 winW/winH，再重建缓冲、重算投影、重排 UI。 */
            RECT cr;
            if (GetClientRect(g_hwnd, &cr) && cr.right > 0 && cr.bottom > 0) {
                int nw = (int)cr.right, nh = (int)cr.bottom;
                if (nw != clientW || nh != clientH) {
                    clientW = nw;
                    clientH = nh;
                    InitPixels();
                    ApplyFov();
                    uiDirty = 1;
                }
            }
            return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            SaveGame();
            PostQuitMessage(0);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            uiDirty = 1;
            return 0;
        }

        case WM_SIZE: return WindowSizeChanged(hwnd, uMsg, wParam, lParam);

        case WM_ACTIVATE:
            uiDirty = 1;
            if (LOWORD(wParam) == WA_INACTIVE) {
                memset(keys, 0, sizeof(keys));
                if (currentState == STATE_PLAYING) { autoPaused = 1; PauseGame(); }
            } else {
                /* 窗口重新获得焦点：若是因失焦自动暂停，则自动恢复 */
                if (currentState == STATE_PAUSED && autoPaused && prevState == STATE_PLAYING) {
                    currentState = STATE_PLAYING;
                    resetInputState = 1;   /* 鼠标在失焦期间的按下对游戏不可见，需重同步边沿 */
                    ForceCursor(0);
                    firstMouseMove = 1;
                    uiDirty = 1;
                }
            }
            return 0;

        case WM_KILLFOCUS:
            /* 按住 TAB 看图时切走窗口会收不到 KEYUP，这里兜底关掉 */
            if (mapHeld) { mapHeld = 0; isMapOpen = 0; mapFullScreen = 0; }
            memset(keys, 0, sizeof(keys));
            return 0;

        case WM_SETFOCUS:
            firstMouseMove = 1;
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                /* 游戏中（含小窗地图）隐藏光标 —— 鼠标是锁定的。
                   只有全屏地图才显示箭头光标。 */
                int mouseFree = (currentState != STATE_PLAYING) ||
                                (isMapOpen && mapFullScreen) || lockActive;
                if (!mouseFree) SetCursor(NULL);
                else /* 2026-11-06（R4）：光标样式归平台层；ForceCursor(1) 已处理可见性 */
                return TRUE;
            }
            break;

        case WM_MOUSEMOVE: return WindowMouseMove(hwnd, uMsg, wParam, lParam);

        case WM_MOUSEWHEEL: return WindowMouseWheel(hwnd, uMsg, wParam, lParam);

        case WM_LBUTTONDOWN: return WindowLeftDown(hwnd, uMsg, wParam, lParam);

        case WM_RBUTTONDOWN:
            if (currentState == STATE_SETTINGS && rebindingAct >= 0) {
                RebindConsumeKey(VK_RBUTTON);
            }
            return 0;

        case WM_MBUTTONDOWN:
            if (currentState == STATE_SETTINGS && rebindingAct >= 0) {
                RebindConsumeKey(VK_MBUTTON);
            }
            return 0;

        case WM_LBUTTONUP:
            /* 2026-11-05：松开要通知锁盘 —— 过载的判定就发生在这一刻 */
            if (lockActive) {
                LockHoldEnd();
                uiDirty = 1;
                return 0;
            }
            if (currentState == STATE_HUB) {
                /* 修：没拖动就当成单击 —— 选中鼠标下的节点（见 ui.c 的注释） */

                int wasClick = SkillDragWasClick();
                SkillDragEnd();
                if (wasClick) {
                    int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
                    POINT pt = { mx, my };
                    ScreenToClient(g_hwnd, &pt);
                    int dx = pt.x, dy = pt.y;
                    MouseToLogical(&dx, &dy);
                    int id = SkillNodeHitTest(dx, dy);
                    if (id >= 0 && hubSelection != id) { hubSelection = id; uiDirty = 1; }
                }
                return 0;
            }
            BenchDragEnd();   /* v2.9：结束画布拖拽 */
            if (sliderDragging) {
                PlatSetCapture(0);
                SaveGame();   /* 拖动滑块松手就落盘 */
            }
            sliderDragging = 0;
            return 0;

        case WM_CAPTURECHANGED:
            sliderDragging = 0;
            return 0;

        case WM_KEYDOWN:
            /* 2026-11-06：处理逻辑抽到 CanityKeyDown —— 网页端也走同一条路 */
            return CanityKeyDown((int)wParam, (lParam & (1L << 30)) ? 1 : 0);

        case WM_KEYUP:
            /* 锁盘打开键松开后，才允许它重新作为「任意键退出」 */
            if ((int)wParam == lockIgnoreVK) lockIgnoreVK = 0;
            /* 与 WM_KEYDOWN 保持同一套折算，否则小键盘数字的 keys[] 会卡在按下状态 */
            if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
                wParam = (WPARAM)('0' + (wParam - VK_NUMPAD0));
            /* v2.5：松开 TAB 立刻关掉小地图（按住才看） */
            if (wParam == VK_TAB && mapHeld) {
                mapHeld = 0;
                isMapOpen = 0;
                mapFullScreen = 0;
                MapCursorRefresh();
            }

            if (wParam < 256) keys[wParam] = 0;
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
#endif

/* 编译期守卫：表项数必须与 common.h 里的常量一致。
   常见坑：往 settings[] 里加了一项却忘了改 SETTING_COUNT，
   那样跨文件的 extern 数组尺寸会对不上。 */
typedef char settingsSizeCheck[(sizeof(settings) / sizeof(settings[0]) == SETTING_COUNT) ? 1 : -1];
typedef char optKeysSizeCheck[(sizeof(optKeys) / sizeof(optKeys[0]) == OPT_KEY_COUNT) ? 1 : -1];
typedef char optKeyVKsSizeCheck[(sizeof(optKeyVKs) / sizeof(optKeyVKs[0]) == OPT_KEY_COUNT) ? 1 : -1];

/* Player skill timers (not owned by the wrench state machine). */


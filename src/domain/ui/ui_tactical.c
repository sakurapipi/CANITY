#include "world_api.h"
#include "loop_private.h"
#include "save.h"
/* Inventory is a modal with stable IDs. Editing never compacts four bindings. */
#include "common.h"
#include "tactical_tools.h"
#include "threat_tier.h"
#include "ui.h"
#include "ui_theme.h"
#include "ui_archive.h"
#include "state.h"
#include "loop_api.h"
#include "player_api.h"
#include "present_api.h"
#include "control_effect.h"
#include "calibration.h"
#include "lockbox.h"
#include "lang.h"
static int opened = 0, selected = 0, quickSlot = 0, capture = 0;
static const char *feedback = "";
static int Rows(void) {
    int n = (winH * 48 / 100) / (TextHeightWin() + UIS(10));
    return n < 3 ? 3 : n > 9 ? 9 : n;
}
static int First(void) {
    int n = selected - Rows() / 2;
    if (n < 0)
        n = 0;
    if (n > ITEM_SLOT_COUNT - Rows())
        n = ITEM_SLOT_COUNT - Rows();
    return n;
}
static RECT QuickRect(int i) {
    int x = winW * 8 / 100, w = winW * 84 / 100 / 4, y = winH * 16 / 100;
    return (RECT){x + i * w, y, x + (i + 1) * w - UIS(4), y + TextHeightWin() + UIS(14)};
}
static void Close(void) {
    opened = 0;
    capture = 0;
    LoopClearInputEdges();
    ForceCursor(0);
    firstMouseMove = 1;
    uiDirty = 1;
}
int TacticalPanelOpen(void) {
    if (currentState != STATE_PLAYING) {
        opened = 0;
        capture = 0;
    }
    return opened;
}
static int KeyAllowed(int key) {
    if (key <= 0 || key >= 256 || key == VK_ESCAPE || key == VK_RETURN || key == VK_SPACE ||
        (key >= VK_F1 && key <= VK_F12) || key <= VK_MBUTTON || key == VK_SHIFT ||
        key == VK_CONTROL || key == VK_MENU || strchr("1234567890ICEQTGBMKR", key))
        return 0;
    for (int i = 0; i < ACT_COUNT; i++)
        if (g_bindings[i] == key)
            return 0;
    for (int i = 0; i < 4; i++)
        if (i != quickSlot && tactical.key[i] == key)
            return 0;
    return 1;
}
int TacticalInputKey(int key, int repeat) {
    if (currentState != STATE_PLAYING)
        return 0;
    if (opened) {
        if (repeat)
            return 1;
        if (capture) {
            if (key == VK_ESCAPE)
                capture = 0;
            else if (KeyAllowed(key)) {
                tactical.key[quickSlot] = key;
                capture = 0;
                feedback = Tr("KEY SAVED", "快捷键已保存");
            } else
                feedback = Tr("KEY ALREADY RESERVED", "此键已被占用");
            uiDirty = 1;
            return 1;
        }
        if (key == 'I' || key == VK_ESCAPE) {
            Close();
            return 1;
        }
        if (key == VK_UP)
            selected = (selected + ITEM_SLOT_COUNT - 1) % ITEM_SLOT_COUNT;
        if (key == VK_DOWN)
            selected = (selected + 1) % ITEM_SLOT_COUNT;
        if (key == VK_LEFT)
            quickSlot = (quickSlot + 3) % 4;
        if (key == VK_RIGHT)
            quickSlot = (quickSlot + 1) % 4;
        if ((key == VK_RETURN || key == 'K') && !SaveEvolutionCanPersist()) {
            feedback = Tr("SAVE EXTENSION DIRECTORY FULL", "存档扩展目录已满，保留原版存档");
            return 1;
        }
        if (key == VK_RETURN) {
            tactical.quick[quickSlot] = selected;
            feedback = Tr("ITEM BOUND", "物品已绑定");
        }
        if (key == VK_DELETE) {
            tactical.key[quickSlot] = 0;
            feedback = Tr("KEY CLEARED", "快捷键已清除");
        }
        if (key == 'K')
            capture = 1;
        if (key == 'H')
            tactical.assistEscape = !tactical.assistEscape;
        if (key == VK_SPACE) {
            throwSelected = selected;
            Close();
            return 1;
        }
        uiDirty = 1;
        return 1;
    }
    if (key == 'I' && !repeat && !ControlActive() && !exitChoiceOpen && !lockActive &&
        !calibActive) {
        opened = 1;
        selected = throwSelected >= 0 && throwSelected < ITEM_SLOT_COUNT ? throwSelected : 0;
        feedback = "";
        ForceCursor(1);
        LoopClearInputEdges();
        return 1;
    }
    if (!repeat)
        for (int i = 0; i < 4; i++)
            if (tactical.key[i] > 0 && tactical.key[i] == key) {
                throwSelected = tactical.quick[i];
                uiDirty = 1;
                return 1;
            }
    return 0;
}
static RECT Button(int i) {
    int x = winW * 65 / 100, y = winH * (45 + i * 7) / 100;
    return (RECT){x, y, winW * 90 / 100, y + TextHeightWin() + UIS(8)};
}
int TacticalInputClick(int x, int y) {
    if (!TacticalPanelOpen())
        return 0;
    if (y < winH * 14 / 100 && x > winW * 83 / 100) {
        Close();
        return 1;
    }
    for (int i = 0; i < 4; i++) {
        RECT r = QuickRect(i);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
            quickSlot = i;
            return 1;
        }
    }
    int top = winH * 27 / 100, line = TextHeightWin() + UIS(10), row = (y - top) / line;
    if (x >= winW * 8 / 100 && x < winW * 60 / 100 && y >= top && row < Rows()) {
        selected = First() + row;
        return 1;
    }
    for (int i = 0; i < 5; i++) {
        RECT r = Button(i);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
            TacticalInputKey(i == 0   ? VK_RETURN
                             : i == 1 ? 'K'
                             : i == 2 ? 'H'
                             : i == 3 ? VK_UP
                                      : VK_DOWN,
                             0);
            return 1;
        }
    }
    if (y >= winH * 85 / 100) {
        throwSelected = selected;
        Close();
    }
    uiDirty = 1;
    return 1;
}
void TacticalDrawPanel(void) {
    if (!TacticalPanelOpen())
        return;
    BlendRectWin(0, 0, winW, winH, UI_BG_PANEL, .98f);
    int left = winW * 8 / 100, right = winW * 92 / 100;
    DrawTextWin(left, winH * 7 / 100, Tr("TACTICAL INVENTORY", "战术背包"), UI_INK_TITLE);
    DrawTextSmallLiteralWin(winW * 85 / 100, winH * 8 / 100, Tr("CLOSE", "关闭"), UI_ACCENT);
    for (int i = 0; i < 4; i++) {
        RECT r = QuickRect(i);
        UiPanel(r.left, r.top, r.right, r.bottom, i == quickSlot ? UI_PANEL_HOT : UI_PANEL_NORMAL);
        char line[120];
        snprintf(line, sizeof(line), "%d  %s x%d [%s]", i + 1, InventoryName(tactical.quick[i]),
                 InventoryCount(tactical.quick[i]), VkName(tactical.key[i]));
        UiTrimSmallText(line, r.right - r.left - UIS(12));
        DrawTextSmallLiteralWin(r.left + UIS(6), r.top + UIS(6), line, UI_ACCENT);
    }
    int top = winH * 27 / 100, lineH = TextHeightWin() + UIS(10), first = First();
    char text[180];
    for (int row = 0; row < Rows(); row++) {
        int item = first + row, y = top + row * lineH;
        snprintf(text, sizeof(text), "%s  x%d", InventoryName(item), InventoryCount(item));
        if (item == selected)
            BlendRectWin(left, y, winW * 60 / 100, y + lineH, UI_BG_SEL, 1);
        UiTrimSmallText(text, winW * 50 / 100);
        DrawTextSmallLiteralWin(left + UIS(8), y + UIS(5), text,
                                item == selected ? UI_ACCENT : UI_INK);
    }
    int x = winW * 65 / 100, y = top;
    const ToolDef *t = ToolDefinition(selected - 7);
    UiDossierText(t ? ToolDetail(selected - 7)
                    : Tr("Original equipment. Keep using its existing throw or placement action.",
                         "原有装备，使用既有投掷或放置操作。"),
                  x, y, right - x, winH * 44 / 100, UI_INK);
    const char *labels[5] = {Tr("BIND / ENTER", "绑定到所选槽"),
                             capture ? Tr("PRESS AN UNUSED KEY", "请按未占用键")
                                     : Tr("CHANGE KEY / K", "设置快捷键"),
                             tactical.assistEscape ? Tr("ESCAPE: HOLD", "挣脱：持续按住")
                                                   : Tr("ESCAPE: TAP", "挣脱：逐次按键"),
                             Tr("PREVIOUS ITEM", "上一物品"), Tr("NEXT ITEM", "下一物品")};
    for (int i = 0; i < 5; i++) {
        RECT r = Button(i);
        UiPanel(r.left, r.top, r.right, r.bottom, UI_PANEL_NORMAL);
        DrawTextSmallLiteralWin(r.left + UIS(5), r.top + UIS(4), labels[i], UI_ACCENT);
    }
    DrawTextSmallLiteralWin(left, winH * 79 / 100, feedback, UI_WARN);
    UiDossierText(
        Tr("Seven new tools are sealed reserves: a mimic catch does not discard them. Space / tap "
           "below: equip and return.",
           "七种新工具属于密封储备，不随宝箱怪抓中丢弃。空格或点击下方区域：选用并返回。"),
        left, winH * 85 / 100, right - left, winH * 98 / 100, UI_INK_DIM);
}

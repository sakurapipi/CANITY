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
#include "economy.h"
#include "throwing.h"
#include "uptimers.h"
#include "warden.h"
#include "hints.h"
#include "calibration.h"
#include "wrench.h"
#include "item_effects.h"
#include "player_tick.h"
#include "interaction.h"
#include "player_api.h"
#include "world_api.h"
#include "phase.h"
#include "collapse.h"
#include "core_rng.h"
#include "difficulty.h"
#include "detect_alert.h"
#include "control_effect.h"
#include "spider.h"
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
#include "lang.h"
#include "render.h"
#include "ui.h"
#include "audio.h"
#include "save.h"
#include "skills.h"
#include "content_db.h"
#include "bestiary.h"
#include "score.h"
#include "loop_private.h"
/* src/loop_input.c —— 输入（2026-11-09，从 game.c 拆出） */
#include "common.h"
#include "entity.h"
#include "state.h"
#include "loop_api.h"
#include "present_api.h"
#include "plat.h"
#include "gamepad.h"

void ClearKeyEdges(void) {
    for (int i = 0; i < 256; i++) keyPressed[i] = 0;
}

int IsKeyBindItem(const SettingItem* it) {
    return it->kind == SET_LIST && BindActionOfItem(it) >= 0;
}




int g_bindings[ACT_COUNT] = BINDINGS_DEFAULT;

/* 设置菜单用的按键选项：名称与虚拟键码一一对应 */
const char* optKeys[] = {
    "None", "1","2","3","4","5","6","7","8","9","0",
    "A","B","C","D","E","F","G","H","I","J","K","L","M",
    "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
    "Space","LShift","RShift","LCtrl","RCtrl","LAlt","RAlt",
    "Up","Down","Left","Right","Enter","Esc","Tab",
    "LMB","RMB","MMB"
};
/* 对应的虚拟键码，与 optKeys 下标一一对应 */
const int optKeyVKs[] = {
    0, '1','2','3','4','5','6','7','8','9','0',
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    VK_SPACE, VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU,
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE, VK_TAB,
    VK_LBUTTON, VK_RBUTTON, VK_MBUTTON
};

/* 改绑提示框用的动作名（下标 = ActionId） */
const char* actionNames[ACT_COUNT] = {
    "MOVE FORWARD", "MOVE BACKWARD", "MOVE LEFT", "MOVE RIGHT",
    "SPRINT", "INTERACT", "CRANK BATTERY", "FLASHLIGHT", "TOGGLE MAP",
    "THROW", "CANCEL CHARGE", "SELECT FLARE", "SELECT BOTTLE", "SELECT SLOT 3",
    "PAUSE", "MENU UP", "MENU DOWN", "MENU LEFT", "MENU RIGHT",
    "MENU CONFIRM", "MENU CANCEL", "DODGE (QTE)",
    /* v2.7：这两项以前漏了，数组声明长度是 ACT_COUNT，缺的自动补 NULL，
       而 DrawRebindOverlay 会拿 actionNames[...] 去 strlen → 崩。
       今天它们还没进设置表所以摸不到，谁加一行改绑就立刻炸。 */
    "SWING KNIFE", "THROW DECOY"
};
/* 编译期守卫：表项数量必须与动作数一致，漏一个改绑框就会画到 NULL */
/* 注意：sizeof(array)/sizeof(elem) 拿到的是**声明长度**（缺省元素补 0），
   不是"写了几个初值"，所以下面这条守卫恒成立、拦不住漏项。
   真正的检查放在 SkillsSelfTest/启动自检里逐个判 NULL。 */
typedef char actionNamesSizeCheck[(ACT_COUNT == (int)(sizeof(actionNames) / sizeof(actionNames[0]))) ? 1 : -1];

int keys[256] = {0};
int keyPressed[256] = {0};

static int IsActionDown(ActionId act) {
    if ((unsigned)act >= (unsigned)ACT_COUNT) return 0;
    int vk = g_bindings[act];
    if (vk <= 0 || vk >= 256) return 0;
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) {
        return PlatKeyDown(vk);
    }
    return keys[vk];
}

int ActionIsDown(int act) { return IsActionDown((ActionId)act); }

static int prevDown[256];

void LoopClearInputEdges(void) {
    memset(keys, 0, sizeof(keys));
    memset(keyPressed, 0, sizeof(keyPressed));
    memset(prevDown, 0, sizeof(prevDown));
    resetInputState = 1;
}

static void SuspendInput(void) {
    LoopClearInputEdges();
    LoopCancelTransientInput();
    if (currentState == STATE_PLAYING) {
        autoPaused = 1;
        PauseGame();
    }
}

void PollInput(void) {

    /* v1.8：窗口最小化时收不到任何键盘消息（F11 也就救不回来）。
       这里用 GetAsyncKeyState 直接读物理按键：最小化状态下按 F11 或 Enter
       就能把自己还原 —— 不注册全局热键，也不会在正常游戏时抢键。 */
    if (PlatMinimized()) {
        SuspendInput();
        static int prevF11 = 0, prevEnter = 0;
        int f11 = PlatKeyDown(VK_F11);
        int ent = PlatKeyDown(VK_RETURN);
        if ((f11 && !prevF11) || (ent && !prevEnter)) {
            PlatRestoreWindow();
            resetInputState = 1;
            firstMouseMove = 1;
            uiDirty = 1;
        }
        prevF11 = f11;
        prevEnter = ent;
        return;
    }

    if (!PlatFocused()) {
        SuspendInput();
        return;
    }
    for (int a = 0; a < ACT_COUNT; a++) {
        int vk = g_bindings[a];
        if (vk <= 0 || vk > 255) continue;
        if (vk == VK_LBUTTON || vk == VK_RBUTTON) continue;   /* 鼠标键由 Windows 消息驱动 */
        int down = PlatKeyDown(vk);
        if (down && !prevDown[vk]) {
            keyPressed[vk] = 1;              /* 0→1 的边沿，等价于 WM_KEYDOWN 那次 */
            if (currentState == STATE_PLAYING) keyPressed[vk] = 1;
        }
        keys[vk] = down;
        prevDown[vk] = down;
    }

    /* v2.9：**不在游戏中就清空所有绑定键**。
       这是「面板里按 A/D 会漂移」的终极修法：
       之前只清 W/A/S/D 四个字面量，一旦按键绑到别的动作、
       或大小写不同（'a' vs 'A'）就漏掉，所以一直修不干净。
       现在直接遍历 g_bindings[]，凡是绑过的键全清。
       菜单导航走的是 WM_KEYDOWN 里的 wParam 比较，不依赖 keys[]。 */
    if (currentState != STATE_PLAYING) {
        for (int a = 0; a < ACT_COUNT; a++) {
            int vk = g_bindings[a];
            if (vk > 0 && vk < 256) keys[vk] = 0;
        }
        keys[VK_SHIFT] = 0;
        keys[VK_SPACE] = 0;
        keys[VK_LBUTTON] = keys[VK_RBUTTON] = 0;
    }

    /* v2.0：手柄（没有手柄时什么也不做） */
    GamepadPoll();
    /* 失焦自动暂停：回到前台就自动继续，不再依赖 WM_ACTIVATE 的时序 */
    if (autoPaused && currentState == STATE_PAUSED) {
        currentState = STATE_PLAYING;
        autoPaused = 0;
        resetInputState = 1;
        ForceCursor(0);
        firstMouseMove = 1;
        uiDirty = 1;
    }
    if (currentState != STATE_PAUSED) autoPaused = 0;
}

int IsActionPressed(ActionId act) {
    if ((unsigned)act >= (unsigned)ACT_COUNT) return 0;
    int vk = g_bindings[act];
    if (vk <= 0 || vk >= 256) return 0;
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) {
        /* 鼠标边缘触发简化：这里只返回按下状态，Update 里用前一帧对比实现边缘 */
        return PlatKeyDown(vk);
    }
    return keyPressed[vk];
}

const char* VkName(int vk) {
    for (int k = 0; k < OPT_KEY_COUNT; k++)
        if (optKeyVKs[k] == vk) return optKeys[k];
    /* v1.8：功能键不在选项表里时给个可读名字 —— 返回 NULL 会让
       sprintf("%s") 打出 "(null)"（HELP 页的 SPRINT 就中过这个招）。 */
    switch (vk) {
    case VK_SHIFT:   return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU:    return "Alt";
    case VK_LBUTTON: return "LMB";
    case VK_RBUTTON: return "RMB";
    case VK_SPACE:   return "Space";
    case VK_RETURN:  return "Enter";
    case VK_ESCAPE:  return "Esc";
    case VK_TAB:     return "Tab";
    case 0:          return "None";
    default:         return "?";
    }
}

int BindActionOfItem(const SettingItem* it) {
    for (int a = 0; a < ACT_COUNT; a++)
        if (it->value == &g_bindings[a]) return a;
    return -1;
}


int LoopIsMouseButton(int vk) {
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON;
}

void LoopSelectVisibleSlot(int n) {
    int counts[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys, phaseBeacons };
    int k = 0;
    for (int i = 0; i < ITEM_SLOT_COUNT; i++) {
        if (counts[i] <= 0) continue;
        if (k == n) {
            if (throwSelected != i) { throwSelected = i; uiDirty = 1; }
            return;
        }
        k++;
    }
}

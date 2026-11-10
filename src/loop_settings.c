/* Settings own their tables, category routing and configured values. */
#include "common.h"
#include "present_api.h"
#include "player_api.h"
#include "world_api.h"
#include "loop_api.h"
#include "state.h"
#include "difficulty.h"
#include "lang.h"
#include "save.h"
#include "content_db.h"
#include "render.h"
#include "audio.h"
#include "music.h"
#include "ui.h"
#include "plat.h"

/* 系统设置 */
int resIndex = RES_AUTO;
/* v1.8：分辨率档位扩充（RES_COUNT / RES_AUTO 定义在 common.h） */
const int resolutions[RES_COUNT][2] = {
    {800, 600}, {1024, 768}, {1280, 720}, {1366, 768},
    {1600, 900}, {1920, 1080}, {2560, 1440}, {0, 0}
};
int isFullscreen = 0;
int showFps = 0;
int fpsCapIndex = 4;                                   /* 30 / 60 / 120 / 144 / 不限制 */
const int fpsCaps[5] = {30, 60, 120, 144, 0};
int renderScaleIndex = 0;                              /* 0..3 = 100/75/50/25%，固定值，运行时不自动改变 */
const int renderScales[4] = {100, 75, 50, 25};
float mouseSens = 1.0f;                                /* 0.2x - 3.0x，滑块调节 */
int vignetteOn = 1;                                    /* 低理智红边特效 */
int hudDetail = 1;                                     /* 0=Minimal 1=Normal 2=Full */
int qteHint = 1;                                       /* 0=Minimal（只留大键+倒计时）1=Full（带说明） */
int fovIndex = 2;                                      /* 66 / 75 / 85 / 95 / 105 度 */
int settingsSelection = 0;
int settingsPopupOpen = 0;
int popupSelection = 0;
int popupItem = -1;
int rebindingAct = -1;       /* >=0 = 正在等待按键改绑的动作（ActionId），-1 = 未在改绑 */
int rebindConflict = -1;     /* 改绑确认中：目标键被该动作占用（ActionId），-1 = 无冲突 */
int resetDefaultsFlag = 0;   /* Reset to Defaults 行的哨兵变量（见 settings 表） */
int sliderDragging = 0;
float volumeF = 0.8f;     /* 0..1，音效音量，0 = 静音 */
float musicVolumeF = 0.8f;/* 0..1，音乐音量，0 = 静音 */
int dynamicRes = 1;               /* v1.9：自动画质开关 */
float gammaValue = 1.0f;
float gammaBuilt = -1.0f;

static const char* optRes[RES_COUNT] = {
    "800x600", "1024x768", "1280x720", "1366x768",
    "1600x900", "1920x1080", "2560x1440", "AUTO (FIT SCREEN)"
};
static const char* optFps[5] = {"30", "60", "120", "144", "Unlimited"};
static const char* optScale[4] = {"HIGH (100%)", "MEDIUM (75%)", "LOW (50%)", "MINIMAL (25%)"};
static const char* optFov[5] = {"66", "75", "85", "95", "105"};
static const char* optOnOff[2] = {"OFF", "ON"};
/* v6 第 ㊳ 轮：语言选项 —— 之前 g_lang 只能靠改存档改 */
static const char* optLang[2] = {"ENGLISH", "\xe4\xb8\xad\xe6\x96\x87"};
/* HUD 详细度：游戏画面上放多少字。
   Minimal = 只留必须实时知道的数值；Full = 全部显示。 */
static const char* optHud[3] = {"Minimal", "Normal", "Full"};
/* 第 2 轮：难度三档。名字走 difficulty.c 的表，和存档里的整数一一对应 */
static const char* optDiff[3] = {"EASY", "NORMAL", "HARDCORE"};
/* QTE 提示详细度：Full = 带英文说明，Minimal = 只留大按键和倒计时条 */
static const char* optQte[2] = {"Minimal", "Full"};
static const char* optSlot[SAVE_SLOTS] = {"SLOT 1", "SLOT 2", "SLOT 3"};
/* ==================================================================
 * 2026-11-09（第 5 轮）：设置项登记进内容中枢
 * 用标签字符串当 id（字符串常量，地址稳定 —— 中枢只存指针）。
 * ================================================================ */
void SettingsRegister(void) {
    int i;
    for (i = 0; i < SETTING_COUNT; i++) {
        if (settings[i].name && settings[i].name[0])
            (void)ContentAdd(CK_SETTING, settings[i].name, &settings[i]);
    }
}

SettingItem settings[] = {
    /* ========== 分类 1: DISPLAY ========== */
    {"Resolution",      SET_LIST,   optRes,      RES_COUNT, &resIndex,          NULL,      0,    0,    0, 1},
    {"Fullscreen",      SET_LIST,   optOnOff,    2, &isFullscreen,      NULL,      0,    0,    0, 1},
    {"Frame Limit",     SET_LIST,   optFps,      5, &fpsCapIndex,       NULL,      0,    0,    0, 1},
    {"Graphics Quality", SET_LIST,  optScale,    4, &renderScaleIndex,  NULL,      0,    0,    0, 1},
    {"Dynamic Res",     SET_LIST,   optOnOff,   2, &dynamicRes,        NULL,      0,    0,    0, 1},
    {"Gamma",           SET_SLIDER, NULL,        0, NULL,               &gammaValue, 0.7f, 1.6f, 0.05f, 1},
    {"Field of View",   SET_LIST,   optFov,      5, &fovIndex,          NULL,      0,    0,    0, 1},
    {"Save Slot",       SET_LIST,   optSlot, SAVE_SLOTS, &g_saveSlot,  NULL,      0,    0,    0, 6},

    /* ========== 分类: KEY BINDINGS / MOVEMENT ========== */
    {"Move Forward",    SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MOVE_FWD],      NULL, 0, 0, 0, 3},
    {"Move Backward",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MOVE_BACK],     NULL, 0, 0, 0, 3},
    {"Move Left",       SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MOVE_LEFT],     NULL, 0, 0, 0, 3},
    {"Move Right",      SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MOVE_RIGHT],    NULL, 0, 0, 0, 3},
    {"Sprint",          SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_SPRINT],        NULL, 0, 0, 0, 3},

    /* ========== 分类: KEY BINDINGS / ACTIONS ========== */
    /* 2026-11-06：相位信标（用户要求 Q 传送 / C 放置） */
    {"Phase: Teleport", SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_PHASE_TELE],    NULL, 0, 0, 0, 4},
    {"Interact",        SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_INTERACT],      NULL, 0, 0, 0, 4},
    {"Crank Battery",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_CRANK],         NULL, 0, 0, 0, 4},
    {"Flashlight",      SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_FLASHLIGHT],    NULL, 0, 0, 0, 4},
    {"Toggle Map",      SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MAP],           NULL, 0, 0, 0, 4},
    {"Throw",           SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_THROW],         NULL, 0, 0, 0, 4},
    {"ATTACK (hold)",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_CANCEL_CHARGE], NULL, 0, 0, 0, 4},
    /* v2.2：这一行其实就是攻击键（原来叫 Cancel Charge，玩家根本不知道） */
    {"Select Flare",    SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_SELECT_1],      NULL, 0, 0, 0, 4},
    {"Select Bottle",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_SELECT_2],      NULL, 0, 0, 0, 4},
    {"Select Slot 3",   SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_SELECT_3],      NULL, 0, 0, 0, 4},
    /* 2026-11-08（第 2 轮联通修复）：这三个动作有默认键、游戏里在用，
       但设置表里一直没有行 —— 玩家查不到也改不了。 */
    {"Attack (wrench)", SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_SWING],         NULL,      0,    0,    0, 5},
    {"Make Noise",      SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_DECOY],         NULL,      0,    0,    0, 5},
    {"Drop Gold",       SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_DROP_GOLD],     NULL,      0,    0,    0, 5},

    /* ========== 分类: KEY BINDINGS / MENU KEYS ========== */
    {"Dodge (QTE)",     SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_DODGE],         NULL, 0, 0, 0, 5},
    {"Pause",           SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_PAUSE],         NULL, 0, 0, 0, 5},
    {"Menu Up",         SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_UP],       NULL, 0, 0, 0, 5},
    {"Menu Down",       SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_DOWN],     NULL, 0, 0, 0, 5},
    {"Menu Left",       SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_LEFT],     NULL, 0, 0, 0, 5},
    {"Menu Right",      SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_RIGHT],    NULL, 0, 0, 0, 5},
    {"Menu Confirm",    SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_CONFIRM],  NULL, 0, 0, 0, 5},
    {"Menu Cancel",     SET_LIST,   optKeys,  OPT_KEY_COUNT, &g_bindings[ACT_MENU_CANCEL],   NULL, 0, 0, 0, 5},

    /* ========== 分类: GAMEPLAY ========== */
    /* 第 2 轮（2026-11-08）：**难度选择**
       朋友实测的反馈分两类：「太难，每次都硬吃」和「不恐怖，好安全」——
       同一套参数满足不了两端，所以做成玩家自己选。
       只调六个可感知的量（见 include/difficulty.h），不动玩法规则。 */
    {"Difficulty",      SET_LIST,   optDiff,     3, &difficulty,         NULL,      0,    0,    0, 6},
    {"Mouse Sens.",     SET_SLIDER, NULL,        0, NULL, &mouseSens, 0.2f, 3.0f, 0.1f, 6},
    {"SFX Volume",      SET_SLIDER, NULL,        0, NULL, &volumeF,  0.0f, 1.0f, 0.05f, 6},
    {"Music Volume",    SET_SLIDER, NULL,        0, NULL, &musicVolumeF, 0.0f, 1.0f, 0.05f, 6},
    /* v6 第 ㊳ 轮：**语言切换终于有入口了**
       审计发现 LangToggle() 全工程 0 调用点 ——
       游戏支持中英双语，但玩家在设置里找不到任何开关。
       这里直接绑 g_lang（0=EN，1=ZH），列表循环就能切。 */
    {"Language",        SET_LIST,   optLang,     2, &g_lang,             NULL,      0,    0,    0, 6},
    {"Sanity FX",       SET_LIST,   optOnOff,    2, &vignetteOn,        NULL,      0,    0,    0, 6},
    {"Show FPS",        SET_LIST,   optOnOff,    2, &showFps,           NULL,      0,    0,    0, 6},
    {"HUD Detail",      SET_LIST,   optHud,      3, &hudDetail,         NULL,      0,    0,    0, 6},
    {"QTE Hint",        SET_LIST,   optQte,      2, &qteHint,           NULL,      0,    0,    0, 6},
    /* Reset to Defaults：一次性动作行。value 指向 resetDefaultsFlag（没有真选项），
       ActivateSettingRow 里单独拦截执行。 */
    {"Reset to Defaults", SET_LIST, optOnOff,    2, &resetDefaultsFlag, NULL,      0,    0,    0, 6},
};
int settingsPage = 0;

/* 每页标题（下标 = settingsPage） */
const char* PageTitle(int page) {
    switch (page) {
        case SETPAGE_DISPLAY:  return "DISPLAY";
        case SETPAGE_BINDDIR:  return "KEY BINDINGS";
        case SETPAGE_MOVE:     return "MOVEMENT";
        case SETPAGE_ACT:      return "ACTIONS";
        case SETPAGE_MENUKEYS: return "MENU KEYS";
        case SETPAGE_GAME:     return "GAMEPLAY";
        default:               return "SETTINGS";
    }
}

/* 根目录 / Key Bindings 子目录的行文字，以及每行对应跳转的页 */
const char* rootCategoryNames[3] = {"Display", "Key Bindings", "Gameplay"};
static const int   rootCategoryPages[3] = {SETPAGE_DISPLAY, SETPAGE_BINDDIR, SETPAGE_GAME};
const char* bindCategoryNames[3] = {"Movement", "Actions", "Menu Keys"};
static const int   bindCategoryPages[3] = {SETPAGE_MOVE, SETPAGE_ACT, SETPAGE_MENUKEYS};

/* 当前页包含的设置项下标，返回个数。页 0/2 是目录，没有设置项。 */
int PageItems(int* out) {
    if (settingsPage == SETPAGE_ROOT || settingsPage == SETPAGE_BINDDIR) return 0;
    if (settingsPage < 1 || settingsPage > 6) return 0;
    int n = 0;
    for (int i = 0; i < SETTING_COUNT; i++)
        if (settings[i].group == settingsPage) out[n++] = i;
    return n;
}

void ApplySetting(int item, int value) {
    if (item < 0 || item >= SETTING_COUNT) return;
    /* 滑块项的 value 是 NULL（它们走 fvalue），这里只处理列表型。
       以前没做判断，靠调用方保证只传列表项；现在显式拦下来。 */
    if (settings[item].kind != SET_LIST || !settings[item].value) return;
    if (value < 0 || value >= settings[item].count) return;
    if(settings[item].value==&g_saveSlot){
        if(!SaveSelectSlot(value)){
            pickupMsg=Tr("RETURN TO MENU OR CHECK SAVE SLOT","请返回主菜单或检查存档");pickupMsgTimer=3;uiDirty=1;return;
        }
        uiDirty=1;return;
    }
    /* 按键绑定项存的是 VK 码（从 optKeyVKs 映射），其它列表项存下标 */
    int act = BindActionOfItem(&settings[item]);
    if (act >= 0) {
        g_bindings[act] = optKeyVKs[value];
    } else {
        *settings[item].value = value;
    }
    if (settings[item].value == &resIndex || settings[item].value == &isFullscreen) ApplyDisplayMode();
    else if (settings[item].value == &renderScaleIndex) InitPixels();
    else if (settings[item].value == &fovIndex) ApplyFov();
    SaveGame();   /* 设置改完立刻落盘，不再等退出设置页 */
    uiDirty = 1;
}

void ActivateSettingRow(void) {
    int rows = SettingsRowCount();
    if (settingsSelection < 0 || settingsSelection >= rows) return;

    /* 目录页（根目录 / Key Bindings 子目录）：进分类或返回 */
    if (settingsPage == SETPAGE_ROOT || settingsPage == SETPAGE_BINDDIR) {
        const int* pages = (settingsPage == SETPAGE_ROOT) ? rootCategoryPages : bindCategoryPages;
        if (settingsSelection < 3) {
            settingsPage = pages[settingsSelection];
            settingsSelection = 0;
            uiDirty = 1;
        } else {
            /* Back：根目录回上一状态，子目录回根目录 */
            if (settingsPage == SETPAGE_BINDDIR) {
                settingsPage = SETPAGE_ROOT;
            } else {
                currentState = prevState;
                SaveGame();
            }
            settingsPopupOpen = 0;
            uiDirty = 1;
        }
        return;
    }

    int idx[SETTING_COUNT];
    int n = PageItems(idx);
    if (settingsSelection >= n) {
        /* 键位页 / Display / Gameplay 的 Back：回到上级目录 */
        settingsPage = (settingsPage == SETPAGE_GAME || settingsPage == SETPAGE_DISPLAY)
                       ? SETPAGE_ROOT : SETPAGE_BINDDIR;
        settingsSelection = 0;
        settingsPopupOpen = 0;
        uiDirty = 1;
        return;
    }

    popupItem = idx[settingsSelection];
    /* Reset to Defaults：一键恢复出厂（键位 + 全部设置项）。
       用哨兵地址识别，不走弹窗。 */
    if (settings[popupItem].value == &resetDefaultsFlag) {
        static const int defBindings[ACT_COUNT] = BINDINGS_DEFAULT;
        for (int a = 0; a < ACT_COUNT; a++) g_bindings[a] = defBindings[a];
        resIndex = RES_AUTO; isFullscreen = 0; fpsCapIndex = 4; renderScaleIndex = 0; dynamicRes=1;
        fovIndex = 2; vignetteOn = 1; showFps = 0; hudDetail = 1; qteHint = 1;
        gammaValue = 1.0f; mouseSens = 1.0f; volumeF = 0.8f; musicVolumeF = 0.8f;
        ApplyDisplayMode();
        ApplyFov();
        PlaySfx(SFX_POWER, 0.5f);
        SaveGame();
        uiDirty = 1;
        return;
    }
    /* 键位项：进入「按任意键绑定」模式，不再弹 54 项的选项列表
       （那个列表本身也远超一屏，只能盲选）。 */
    if (IsKeyBindItem(&settings[popupItem])) {
        rebindingAct = BindActionOfItem(&settings[popupItem]);
        rebindConflict = -1;   /* 新的一次改绑，清掉上一次的冲突确认 */
        uiDirty = 1;
        return;
    }
    if (settings[popupItem].kind == SET_SLIDER) {
        popupSelection = 0;
    } else {
        popupSelection = *settings[popupItem].value;
        if (popupSelection >= settings[popupItem].count) popupSelection = 0;
    }
    settingsPopupOpen = 1;
    sliderDragging = 0;
    uiDirty = 1;
}

int SettingsRowCount(void) {
    if (settingsPage == SETPAGE_ROOT || settingsPage == SETPAGE_BINDDIR) return 3 + 1;  /* 三个分类 + Back */
    int tmp[SETTING_COUNT];
    return PageItems(tmp) + 1;              /* 若干项 + Back */
}

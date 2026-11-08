/* tests/domains/test_ui.c —— UI 页面层（几何缓存 / 文本工具）
 *
 * 第 1 轮（2026-11-08）：成就页从 ui.c 搬到 domain/ui/ui_pages.c 时加的。
 * AchCompute 同时服务"页面绘制"和"菜单命中测试"——它算错，
 * 玩家点得到的位置和看到的位置就会错位，所以必须有用例。
 */
#include "../test_common.h"
#include "ui.h"
#include "ui_pages.h"

extern int winW, winH;

static void test_ach_geo_stays_inside_window(void) {
    static const int sizes[3][2] = { { 1280, 720 }, { 800, 600 }, { 1920, 1080 } };
    int i;
    for (i = 0; i < 3; i++) {
        AchGeo A;
        winW = sizes[i][0];
        winH = sizes[i][1];
        AchCompute(&A);
        CHECK(A.panel.left >= 0, "achievement panel must not start left of the screen");
        CHECK(A.panel.top >= 0, "achievement panel must not start above the screen");
        CHECK(A.panel.right <= winW, "achievement panel must not overflow right");
        CHECK(A.panel.bottom <= winH, "achievement panel must not overflow bottom");
        CHECK(A.rowH > 0, "row height must be positive");
        CHECK(A.visible > 0, "at least one row must fit");
        CHECK(A.stripY2 <= A.panel.bottom, "the description strip must stay inside the panel");
    }
    winW = 1280; winH = 720;      /* 还原，别影响后面的用例 */
}

static void test_ach_geo_shrinks_rows_on_short_windows(void) {
    AchGeo big, small;
    winW = 1280; winH = 1080;
    AchCompute(&big);
    winW = 1280; winH = 400;      /* 很矮的窗口 */
    AchCompute(&small);
    CHECK(small.rowH <= big.rowH, "a shorter window must not produce taller rows");
    CHECK(small.panel.bottom <= 400, "the panel must fit the short window");
    winW = 1280; winH = 720;
}

static void test_clamp_row_text_never_overflows(void) {
    char buf[128];
    int avail;
    for (avail = 20; avail <= 200; avail += 30) {
        strcpy(buf, "ACHIEVEMENT UNLOCKED: FIRST STEPS");
        ClampRowText(buf, avail);
        CHECK(TextWidthWin(buf) <= avail, "clamped text must fit the available width");
    }
    /* 空字符串与 0 宽度不能崩 */
    ClampRowText(buf, 0);
    CHECK(buf[0] == 0, "zero available width must produce an empty string");
}


/* ==================================================================
 * 菜单截图（AD-66）
 * ------------------------------------------------------------------
 * 第 2 轮把 1,357 行菜单代码搬到 domain/ui/ui_menu.c。
 * 只跑数据测试证明不了"菜单还画得出来"——必须出图。
 * 设 CANITY_SHOT=<目录> 跑 test_runtime 就会写出 shot_menu.ppm。
 * ================================================================ */

extern void UiDrawMenuPages(void);
extern unsigned int* pixels;
extern int renderW, renderH;
extern GameState currentState;

static void write_ppm(const char* path, int w, int h, const unsigned int* px) {
    FILE* f = fopen(path, "wb");
    int i;
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (i = 0; i < w * h; i++) {
        unsigned int p = px[i];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)((p >> 16) & 0xFF);
        rgb[1] = (unsigned char)((p >> 8) & 0xFF);
        rgb[2] = (unsigned char)(p & 0xFF);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    fprintf(stderr, "  [shot] wrote %s\n", path);
}

static void test_menu_draws_without_crashing(void) {
    GameState savedState = currentState;
    int i, nonBlack = 0;

    currentState = STATE_MAIN_MENU;
    if (pixels && renderW > 0 && renderH > 0) {
        for (i = 0; i < renderW * renderH; i++) pixels[i] = 0;
        UiDrawMenuPages();
        for (i = 0; i < renderW * renderH; i++) {
            if (pixels[i] != 0) { nonBlack++; }
        }
        CHECK(nonBlack > 500, "drawing the main menu must actually put pixels on screen");
    }

    {
        const char* dir = getenv("CANITY_SHOT");
        if (dir && dir[0] && pixels && renderW > 0 && renderH > 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/shot_menu.ppm", dir);
            write_ppm(path, renderW, renderH, pixels);
        }
    }
    currentState = savedState;
}


/* ==================================================================
 * HUD 截图（AD-66）
 * ------------------------------------------------------------------
 * DrawHUD 是 1,299 行的单函数，以前没有任何视觉回归。
 * 设 CANITY_SHOT=<目录> 跑 test_runtime 会写出 shot_hud.ppm。
 * ================================================================ */

extern void DrawHUD(void);

static void test_hud_draws_without_crashing(void) {
    GameState savedState = currentState;
    int i, nonBlack = 0;

    currentState = STATE_PLAYING;
    /* 2026-11-08（实机反馈）：注入一条**长播报** + 打开 Full 档，
       这样 BOOST 行真的存在、播报也真的长 —— 才能验"压字"。
       原来这条用例的播报是空的，所以永远看不出问题。 */
    {
        extern const char* pickupMsg;
        extern float pickupMsgTimer;
        extern int hudDetail;
        extern int exitChoiceOpen, settingsPopupOpen;
        pickupMsg = "THREAT: SOUND HUNTER - blind, walks to the last sound it heard";
        pickupMsgTimer = 5.0f;
        hudDetail = 2;
        /* DrawPickupToast 有两个早退守卫：模态面板打开就不画。
           前面的用例可能把它们留成 1 —— 不清理就永远画不出来。 */
        exitChoiceOpen = 0;
        settingsPopupOpen = 0;
    }
    if (pixels && renderW > 0 && renderH > 0) {
        /* 每帧开头清矩形表 —— RenderFrame 里本来会调，测试没走那条路径。
           不清的话 dump 会把上一帧的菜单和这一帧的 HUD 混在一起，
           布局工具就会报"Hub 压在播报行上"这种假重叠。 */
        { extern void UiLayoutBegin(void); UiLayoutBegin(); }
        for (i = 0; i < renderW * renderH; i++) pixels[i] = 0;
        DrawHUD();
        /* v2.4.53 起播报是**独立函数**（DrawPickupToast），不归 DrawHUD 管。
           只画 DrawHUD 的话，这条永远进不了截图和布局 dump ——
           实机报告说"dump 里没有播报行"，一半原因就在这里。 */
        { extern void DrawPickupToast(void); DrawPickupToast(); }
        for (i = 0; i < renderW * renderH; i++) {
            if (pixels[i] != 0) { nonBlack++; }
        }
        CHECK(nonBlack > 500, "drawing the HUD must actually put pixels on screen");
    }

    {
        const char* dir = getenv("CANITY_SHOT");
        if (dir && dir[0] && pixels && renderW > 0 && renderH > 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/shot_hud.ppm", dir);
            write_ppm(path, renderW, renderH, pixels);
            /* 同时把这一帧的文字矩形落盘 —— tools/ui_layout_check.py 直接查压字 */
            { extern void DumpUILayout(const char*); DumpUILayout("ui_layout.txt"); }
        }
    }
    currentState = savedState;
}

void TestSuite_ui(void) {
    test_ach_geo_stays_inside_window();
    test_ach_geo_shrinks_rows_on_short_windows();
    test_clamp_row_text_never_overflows();
    test_menu_draws_without_crashing();
    test_hud_draws_without_crashing();
}

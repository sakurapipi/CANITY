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

void TestSuite_ui(void) {
    test_ach_geo_stays_inside_window();
    test_ach_geo_shrinks_rows_on_short_windows();
    test_clamp_row_text_never_overflows();
}

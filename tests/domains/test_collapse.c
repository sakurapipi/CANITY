/* tests/domains/test_collapse.c —— collapse 域的集成测试
 *
 * 第 2 轮（2026-11-08）从 test_runtime.c 原样搬过来，断言一条没改。
 * 入口：TestSuite_collapse()
 */
#include "../test_common.h"

static void test_collapse_layer_params(void) {
    extern int collapsedLevel;
    extern float GoldSearchAt(int level);
    extern int HatchPlaceInRoom(int fx, int fy, int w, int h, int themeId);

    int saveCollapsed = collapsedLevel;

    collapsedLevel = 0;
    float normal = GoldSearchAt(3);
    collapsedLevel = 1;
    float broken = GoldSearchAt(3);

    CHECK(normal > 0.0f, "normal gold must be positive");
    CHECK(broken > normal * 1.7f && broken < normal * 1.9f,
          "collapsed layer must pay 1.8x gold (user requirement)");

    /* 崩塌层不生成舱门 —— 传一个合法主题（5 = 锈工厂）也必须被拒 */
    CHECK(HatchPlaceInRoom(2, 2, 4, 4, 5) == -1,
          "collapsed layers must not spawn hatches (would allow infinite rerolls)");

    collapsedLevel = saveCollapsed;
}

void TestSuite_collapse(void) {
    test_collapse_layer_params();
}

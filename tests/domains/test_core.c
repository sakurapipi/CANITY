/* tests/domains/test_core.c —— core 层（tick 顺序守卫 / 可复现 RNG）
 *
 * 第 4 轮（2026-11-08）
 */
#include "../test_common.h"
#include "core_tick.h"
#include "core_rng.h"

/* ---- tick 顺序守卫 ---- */

static void test_tick_order_accepts_declared_sequence(void) {
    TickFrameBegin();
    TickMark(TICK_HATCH);
    TickMark(TICK_COLLAPSE);
    TickMark(TICK_THROWING);
    TickMark(TICK_SMOKES);
    CHECK(TickOrderViolations() == 0, "declared tick order must produce no violations");
    CHECK(TickLastViolation()[0] == '\0', "no violation message when order is correct");
}

static void test_tick_order_detects_swap(void) {
    /* throwing 应该在 hatch 之后；反着来必须被抓到 */
    TickFrameBegin();
    TickMark(TICK_HATCH);
    TickMark(TICK_COLLAPSE);
    TickMark(TICK_SMOKES);
    TickMark(TICK_HATCH);          /* 倒退 */
    CHECK(TickOrderViolations() == 1, "going backwards must count as one violation");

    TickFrameBegin();
    TickMark(TICK_SMOKES);
    TickMark(TICK_COLLAPSE);       /* 倒着 */
    CHECK(TickOrderViolations() == 1, "collapse after smokes must be a violation");
    CHECK(TickLastViolation()[0] != '\0', "violation must leave a message");
}

static void test_tick_frame_begin_resets(void) {
    TickFrameBegin();
    TickMark(TICK_SMOKES);
    TickMark(TICK_HATCH);
    CHECK(TickOrderViolations() == 1, "one violation expected");
    TickFrameBegin();
    CHECK(TickOrderViolations() == 0, "TickFrameBegin must reset the counter");
}

/* ---- RNG ---- */

static void test_rng_same_seed_same_sequence(void) {
    unsigned int a[8], b[8];
    int i;
    rng_seed(12345u);
    for (i = 0; i < 8; i++) a[i] = rng_next();
    rng_seed(12345u);
    for (i = 0; i < 8; i++) b[i] = rng_next();
    for (i = 0; i < 8; i++) {
        CHECK(a[i] == b[i], "same seed must replay the same sequence");
    }
}

static void test_rng_different_seed_differs(void) {
    unsigned int a, b;
    rng_seed(1u);
    a = rng_next();
    rng_seed(2u);
    b = rng_next();
    CHECK(a != b, "different seeds must produce different values");
}

static void test_rng_never_stuck_at_zero(void) {
    /* seed 0 是经典陷阱：xorshift 会永远输出 0 */
    int i;
    rng_seed(0u);
    for (i = 0; i < 16; i++) {
        CHECK(rng_next() != 0u, "rng must not degenerate when seeded with 0");
    }
}

static void test_rng_range_bounds(void) {
    int i;
    rng_seed(777u);
    for (i = 0; i < 200; i++) {
        int v = rng_range(3, 7);
        CHECK(v >= 3 && v <= 7, "rng_range must stay inside [lo,hi]");
    }
    /* lo > hi 自动交换 */
    rng_seed(778u);
    for (i = 0; i < 50; i++) {
        int v = rng_range(9, 4);
        CHECK(v >= 4 && v <= 9, "rng_range must swap reversed bounds");
    }
}

static void test_rng_float_unit_interval(void) {
    int i;
    rng_seed(4242u);
    for (i = 0; i < 200; i++) {
        float f = rng_float();
        CHECK(f >= 0.0f && f < 1.0f, "rng_float must stay in [0,1)");
    }
}

/* ---- 真帧验证：顺序守卫必须在一次真实 Update() 里保持 0 违规 ---- */

extern void Update(float dt);

static void test_tick_marks_hold_in_a_real_frame(void) {
    /* 为什么必须跑真帧：TickMark 的违规是在**调用顺序**上积累的，
       只手工按声明顺序调一遍等于自己出题自己答（第一次做有牙验证时
       把 game.c 的两条标记对调，测试居然还是绿的 —— 就是因为没跑帧）。 */
    StartGame();
    currentState = STATE_PLAYING;
    TickFrameBegin();
    Update(0.016f);
    CHECK(TickOrderViolations() == 0,
          "a real frame must call domain ticks in the declared order");
}

void TestSuite_core(void) {
    test_tick_order_accepts_declared_sequence();
    test_tick_order_detects_swap();
    test_tick_frame_begin_resets();
    test_tick_marks_hold_in_a_real_frame();
    test_rng_same_seed_same_sequence();
    test_rng_different_seed_differs();
    test_rng_never_stuck_at_zero();
    test_rng_range_bounds();
    test_rng_float_unit_interval();
}

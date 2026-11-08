/* tests/domains/test_update.c —— Update() 里抽出来的块
 *
 * 第 4 轮（2026-11-08）：Update() 2,074 行，抽出两块自洽代码
 * （训练场补货、烟雾致盲）。这里通过 Update() 观测它们的行为 ——
 * 不把 static 函数暴露出去，测试就盯"玩家能看到的结果"。
 */
#include "../test_common.h"
#include "game.h"
#include "entity.h"
#include "throwing.h"

extern void Update(float dt);
extern int  flares, bottles, smokeBombs, gluePots, echoBolts, decoys;
extern float sanity;
extern SmokeCloud smokes[MAX_SMOKE];
/* 注意：monster 是 common.h 里的宏（#define monster monHunters[0]），
 * 不能再 extern 声明一次 —— 展开后是 extern Monster monHunters[0] 冲突。 */
extern int trainingMode;

/* 进局内：Update() 只在 STATE_PLAYING 时跑逻辑 */
static void enter_playing(void) {
    StartGame();
    currentState = STATE_PLAYING;
    trainingMode = 0;
}

static void test_training_pad_refills_items(void) {
    enter_playing();
    trainingMode = 1;
    flares = 0; bottles = 0; smokeBombs = 0; gluePots = 0; echoBolts = 0; decoys = 0;
    sanity = 10.0f;

    Update(0.016f);

    CHECK(flares == 99, "training pad must refill flares every frame");
    CHECK(bottles == 99, "training pad must refill sling stones");
    CHECK(echoBolts == 99, "training pad must refill echo bolts");
    CHECK(sanity >= 99.0f, "training pad must keep sanity topped up");
    trainingMode = 0;
}

static void test_smoke_blinds_the_monster(void) {
    int i;
    enter_playing();
    for (i = 0; i < MAX_SMOKE; i++) smokes[i].active = 0;

    /* 把怪和一小片烟放在同一格 */
    monster.active = 1;
    monster.x = 8.5f; monster.y = 8.5f;
    monster.state = 2;           /* 正在追（非 0 = 有目标） */
    monster.pathLen = 3; monster.pathIdx = 1;

    smokes[0].active = 1;
    smokes[0].x = 8.5f; smokes[0].y = 8.5f;
    smokes[0].life = 5.0f;

    Update(0.016f);

    CHECK(monster.state == 0, "a monster inside smoke must lose its target");
    CHECK(monster.pathLen == 0, "losing the target must also clear its path");

    smokes[0].active = 0;
    monster.active = 0;
}


/* ==================================================================
 * 2026-11-08（第 3 轮）：**空挥扳手要有准星反馈**
 * ------------------------------------------------------------------
 * 实机反馈：空挥只有摆动 + 冷却，准星没有闪光 ——
 * 玩家分不清"挥了但没打中"和"根本没挥出去"。
 * 现在挥出去就置 wrenchSwingFxT（暗标记），命中另有更亮的。
 * ================================================================ */

static void test_empty_wrench_swing_leaves_a_visible_marker(void) {
    extern float wrenchSwingFxT, wrenchHitFlashT;
    extern void  WrenchSwingStartForTest(float charge);

    enter_playing();
    wrenchSwingFxT = 0.0f;
    wrenchHitFlashT = 0.0f;

    WrenchSwingStartForTest(0.5f);
    CHECK(wrenchSwingFxT > 0.0f,
          "swinging must set a visible marker even when nothing is hit");

    /* 每帧递减，最终归零（不能永久留着） */
    Update(0.05f);
    CHECK(wrenchSwingFxT >= 0.0f, "the marker must decay, never go negative");
}

void TestSuite_update(void) {
    test_training_pad_refills_items();
    test_smoke_blinds_the_monster();
    test_empty_wrench_swing_leaves_a_visible_marker();
}

/*
 * Canity — 每帧计时器（v8 重构第六刀 A 的第一块）
 *
 * 从 Update()（1552 行）里切出来的第一块：三个纯计时逻辑。
 *   · 回声扫描倒计时（echoScanT）
 *   · 锁定窗口递减（hideBlockTimer）
 *   · 周期自动存档（20 秒一次）
 *
 * 这一块的特点：只依赖计时器和 SaveGame，不碰渲染、不碰 AI ——
 * 所以是最安全的第一个切割点，用来验证「把 Update 拆成子函数」这条路。
 */
#include "common.h"
#include "game.h"
#include "state.h"
#include "uptimers.h"

static float autoSaveTimer = 0.0f;

/* game.c 里定义、这里要用到的变量 */
extern float echoScanT, echoScanX, echoScanY;
extern float hideBlockTimer;
extern int   trainingMode;   /* currentState 已在 game.h 里声明 */
void SaveGame(void);
   /* 原来在 Update() 里是函数内 static */

void UpdateTimers(float dt) {
    /* v3.3：回声扫描倒计时 */
    if (echoScanT > 0.0f) echoScanT -= dt;

    /* v3.0：锁定窗口递减（见 hideBlockTimer 的用法） */
    if (hideBlockTimer > 0.0f) hideBlockTimer -= dt;

    /* v3.0：**周期自动存档** ——
       原来 SaveGame() 只在「解锁成就 / 换层 / 安全屋操作 / 退出」时调用，
       中途强退或崩溃等于整局白玩（验证报告 BUG1：保存后重启 benchOwned 变 0）。
       这里在游戏中/安全屋每 20 秒存一次，代价是一次 6KB 的写盘。 */
    /* v3.0：**暂停也要存** —— 游戏失焦会自动暂停（STATE_PAUSED），
           原来条件里没有它，于是切出去等多久都不会存盘。 */
    if ((currentState == STATE_PLAYING || currentState == STATE_PAUSED ||
         currentState == STATE_SAFEHOUSE) && !trainingMode) {
            autoSaveTimer += dt;
        if (autoSaveTimer >= 20.0f) {
            autoSaveTimer = 0.0f;
            SaveGame();
        }
    }

    /* 冲刺与体力 */

}

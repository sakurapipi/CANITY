/*
 * Canity — 手柄分区的网页后端（暂不接 Gamepad API）
 * ---------------------------------------------------------------------
 * 2026-11-06：网页构建排除 gamepad.c（XInput 走 LoadLibrary），
 * 这个文件补上 gamepad.h 的三个接口。
 *
 * 浏览器有 Gamepad API，后续可以在这里实现；现在先保证
 * "没插手柄"这条路径正确，键盘鼠标流程完全可用。
 */
#include "common.h"
#include "gamepad.h"

void GamepadInit(void) { /* STUB: Browser Gamepad API has not been connected; keyboard and mouse are separate. */ }
void GamepadPoll(void) { /* STUB: Browser Gamepad API has not been connected; keyboard and mouse are separate. */ }
int  GamepadConnected(void) { /* STUB: Browser Gamepad API has not been connected; keyboard and mouse are separate. */ return 0; }


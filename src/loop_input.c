/* src/loop_input.c —— 输入（2026-11-09，从 game.c 拆出） */
#include "common.h"
#include "loop_api.h"
#include "present_api.h"

void ClearKeyEdges(void) {
    for (int i = 0; i < 256; i++) keyPressed[i] = 0;
}

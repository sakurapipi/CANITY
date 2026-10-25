/*
 * Canity — 自检 / 基准专用的 unity 构建入口
 *
 * test_canity.c 与 bench_canity.c 需要直接访问各个模块内部的 static 符号
 * （shadeLut、premulFloor、BuildShadeTables、g_jumperTex、parasite……），
 * 所以它们不链接多个 .o，而是 include 本文件，把六个模块合成一个编译单元。
 *
 * 这个文件**不参与游戏本身的构建**：
 *   游戏 = gcc src/*.c -Iinclude ...
 *
 * 注意：unity 构建能过 ≠ 多文件构建能过（static 在 unity 里是全局可见的）。
 * 改完代码请先用多文件命令编游戏，再跑 test.bat。
 */
#ifndef CANITY_UNITY_C
#define CANITY_UNITY_C

#include "../src/entity.c"
#include "../src/render.c"
#include "../src/ui.c"
#include "../src/game.c"
#include "../src/save.c"
#include "../src/audio.c"
#include "../src/main.c"

#endif /* CANITY_UNITY_C */

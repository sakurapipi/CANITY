/*
 * Canity — 结算分数（纯函数，不读 UI、不写状态）
 *
 * 公式来源：docs/终极方案_v2.md 第 3 节
 *   五项权重 40 / 20 / 15 / 15 / 10
 *   深度项用 log 归一（10 层 0.58 / 24 层 0.75 / 50 层 0.94 / 64 层 1.0）
 *   深度乘子 min(3, 1 + 0.15 × 层/10)
 *   难度乘子 1 + 0.06 × 本局见过的词条种类
 *   主线通关 = 固定分（SCORE_MAINLINE）
 */
#ifndef CANITY_SCORE_H
#define CANITY_SCORE_H

#include "common.h"

typedef struct {
    int   level;        /* 到达层数 */
    int   cleared;      /* 1 = 通关（主线 10 层出口 / 无尽死亡 = 0） */
    int   isEndless;    /* 1 = 无尽 */
    int   searched;     /* 本局翻箱数 */
    int   kills;        /* 本局瓶子击杀数 */
    float time;         /* 本局耗时（秒） */
    int   modsSeen;     /* 本局见过的词条种类数 */
} ScoreInput;

typedef struct {
    int   score;
    int   s_depth, s_coll, s_kill, s_time, s_done;  /* 各项原始分（0..权重） */
    float depth_mult, diff_mult;
} ScoreBreakdown;

void ComputeScore(const ScoreInput* in, ScoreBreakdown* out);

#endif /* CANITY_SCORE_H */

/*
 * Canity — 结算分数实现
 *
 * 五项归一化后加权求和，再乘深度乘子与难度乘子。
 * 深度项用 log 归一：线性封顶会让 30 层和 80 层同分（v1.7 的旧公式就是这样）。
 */
#include "common.h"
#include "score.h"

void ComputeScore(const ScoreInput* in, ScoreBreakdown* out) {
    if (!in || !out) return;

    int lvl = in->level;
    if (lvl < 0) lvl = 0;

    /* ---- 主线：固定分 ---- */
    if (!in->isEndless) {
        /* 失败：按到达层数给部分分（每层 100），不再是固定的一半满额 */
        out->score = in->cleared ? SCORE_MAINLINE : lvl * 100;
        out->s_depth = SCORE_W_DEPTH;   /* 展示用：主线视为满深度 */
        out->s_coll  = 0;
        out->s_kill  = 0;
        out->s_time  = 0;
        out->s_done  = in->cleared ? SCORE_W_DONE : 0;
        out->depth_mult = 1.0f;
        out->diff_mult  = 1.0f;
        return;
    }

    /* ---- 无尽：五项加权 ---- */
    float n_depth = 0.0f;
    if (lvl > 0) {
        n_depth = (float)(log(1.0 + (double)lvl) / log(1.0 + (double)SCORE_DEPTH_FULL));
        if (n_depth > 1.0f) n_depth = 1.0f;
    }
    float n_coll = 0.0f;
    if (lvl > 0) {
        n_coll = (float)in->searched / ((float)lvl * SCORE_BOXES_PER_LEVEL);
        if (n_coll > 1.0f) n_coll = 1.0f;
    }
    float n_kill = (float)in->kills / SCORE_KILL_FULL;
    if (n_kill > 1.0f) n_kill = 1.0f;
    float n_time = 0.0f;
    if (in->time > 0.01f) {
        n_time = ((float)lvl * SCORE_SECONDS_PER_LEVEL) / in->time;
        if (n_time > 1.0f) n_time = 1.0f;
    }
    float n_done = in->cleared ? 1.0f : (lvl >= WIN_LEVEL ? 0.25f : 0.0f);

    out->s_depth = (int)(SCORE_W_DEPTH * n_depth);
    out->s_coll  = (int)(SCORE_W_COLL  * n_coll);
    out->s_kill  = (int)(SCORE_W_KILL  * n_kill);
    out->s_time  = (int)(SCORE_W_TIME  * n_time);
    out->s_done  = (int)(SCORE_W_DONE  * n_done);

    int wsum = out->s_depth + out->s_coll + out->s_kill + out->s_time + out->s_done;

    out->depth_mult = 1.0f + 0.15f * (float)(lvl / 10);
    if (out->depth_mult > SCORE_DEPTH_MULT_CAP) out->depth_mult = SCORE_DEPTH_MULT_CAP;

    int mods = in->modsSeen;
    if (mods < 0) mods = 0;
    out->diff_mult = 1.0f + 0.06f * (float)mods;

    float raw = (float)SCORE_BASE * (float)wsum / 100.0f * out->depth_mult * out->diff_mult;
    out->score = (int)(raw + 0.5f);
}

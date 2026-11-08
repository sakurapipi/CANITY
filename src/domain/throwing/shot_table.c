/* src/domain/throwing/shot_table.c —— 投掷物规格表（纯数据，零依赖）
 *
 * 数值全部照抄改动前的 throwing.c（2026-11-08 之前的行为），
 * 一行没调 —— 这一轮是"把散在三处的常量收拢"，不是平衡性调整。
 */
#include "shot_spec.h"

/* DT_* 在 common.h 里；这里只要常量值，但不 include common.h 会被
 * 分区契约 S1 允许（common.h 是唯一带系统头的），所以正常 include。 */
#include "common.h"
#include "content_db.h"

static const ShotSpec kShotSpecs[] = {
/* dmgType = -1 表示"这种投掷物不造成伤害"。
   注意 common.h 里只有 DT_SLASH/BLUNT/SHARD 三种，没有 -1 —— 
   第一次写这张表时我用了 -1，编译直接报未声明。 */

/*  kind 名字        效果          噪音   噪声随蓄力 dmg  类型      无视甲 稀有 穿透 */
    {  1, "stone",   FX_SHARDS,     8.0f, 1,         0, DT_BLUNT, 0, 0, 1 },  /* 破门石：工具，穿过怪 */
    {  2, "brick",   FX_SHARDS,    14.0f, 0,        12, DT_BLUNT, 0, 0, 0 },
    {  3, "acid",    FX_SHARDS,    14.0f, 0,        10, DT_SHARD, 1, 0, 0 },
    {  4, "smoke",   FX_SMOKE,      5.0f, 0,         0, -1,  0, 0, 0 },
    {  6, "flaregun",FX_SHARDS,    14.0f, 0,         8, DT_SHARD, 0, 0, 0 },
    {  7, "firepot", FX_SHARDS,    14.0f, 0,        10, DT_SHARD, 0, 0, 0 },
    {  8, "glue",    FX_GLUE,       6.0f, 0,         0, -1,  0, 1, 0 },
    {  9, "mirror",  FX_SHARDS,    14.0f, 0,         1, DT_SHARD, 0, 1, 0 },
    { 10, "echo",    FX_ECHO_SCAN,  0.0f, 0,         0, -1,  0, 1, 0 },  /* 完全静音 */
    { 12, "phase",   FX_PHASE,     10.0f, 0,         0, -1,  0, 1, 0 },
    { 13, "anchor",  FX_PHASE,     10.0f, 0,         0, -1,  0, 1, 0 },
};

/* 未知 kind 的兜底：碎一下、出声、不伤害 —— 和改动前的 else 分支一致 */
static const ShotSpec kDefaultSpec =
    { 0, "unknown", FX_SHARDS, 14.0f, 0, 1, DT_SHARD, 0, 0, 0 };

/* ==================================================================
 * 2026-11-09（第 5 轮）：投掷物登记进内容中枢
 * ------------------------------------------------------------------
 * 用表里的名字（字符串常量）当 id；中枢只存指针，要求 id 地址稳定。
 * ================================================================ */
void ShotRegister(void) {
    int i;
    for (i = 0; i < ShotSpecCount(); i++) {
        const ShotSpec* sp = ShotSpecAt(i);
        if (sp && sp->name && sp->name[0])
            (void)ContentAdd(CK_SHOT, sp->name, sp);
    }
}

int ShotSpecCount(void) {
    return (int)(sizeof(kShotSpecs) / sizeof(kShotSpecs[0]));
}

const ShotSpec* ShotSpecAt(int i) {
    if (i < 0 || i >= ShotSpecCount()) return 0;
    return &kShotSpecs[i];
}

const ShotSpec* ShotSpecFor(int kind) {
    int i;
    for (i = 0; i < ShotSpecCount(); i++) {
        if (kShotSpecs[i].kind == kind) return &kShotSpecs[i];
    }
    return &kDefaultSpec;
}

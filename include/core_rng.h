/* include/core_rng.h —— 可复现的随机数（新代码用这个）
 *
 * 为什么需要
 *   全库 118 处 rand()，其中 51 处在 game.c。后果：
 *     · 测试没法复现（AD-64「测试不能依赖生成出来的地形」就是被这个逼出来的）
 *     · 出了问题没法"重放同一个种子"定位
 *
 * 为什么**没有**一次性替换掉 rand()
 *   关卡布局是 (worldMap + roomSeed) 的确定性函数，读档时会用同一个种子重放
 *   （见 map_rooms.h 的 RoomReplay）。如果现在换掉全局 RNG，
 *   **老存档重放出来的房间会变** —— 舱门/家具会落在新位置，
 *   玩家看到的是"读档后地图变了"。那是一次真实的存档兼容破坏。
 *
 * 所以分两步走（v2.5 计划）：
 *   ① 本轮：新代码用 RngNext()；老路径保持 rand() 不变（RngLegacyNext 只是转发）
 *   ② 将来升 SAVE_VERSION 时，让关卡生成也切到 RngNext，
 *      并在存档里记一个 rngVersion 字段区分老档
 */
#ifndef CANITY_CORE_RNG_H
#define CANITY_CORE_RNG_H

/* 播种（0 会被替换成一个非零常量，避免"全零态"卡死） */
void rng_seed(unsigned int seed);

/* 原始 32 位（xorshift32，周期 2^32-1，跨平台结果一致） */
unsigned int rng_next(void);

/* [lo, hi] 闭区间；lo > hi 时自动交换 */
int rng_range(int lo, int hi);

/* [0, 1) */
float rng_float(void);

/* ---- 存档保真：RNG 版本 ----
 * 老存档的游戏世界是用 C 标准库 rand() 生成的；换算法后重放会变样。
 * 所以按版本分派：1 = 旧 rand 序列（老档），2 = xorshift（新档）。
 * 只有"会写进存档的随机"（关卡/房间生成）需要这个开关。
 */
void rng_set_version(int version);
int  rng_version(void);

/* 按当前版本取一个随机数；版本 1 时等价于 rand() */
unsigned int rng_world_next(void);

/* 按当前版本播种（版本 1 -> srand(seed)，版本 2 -> rng_seed(seed)） */
void rng_world_seed(unsigned int seed);

/* 老路径：透传给 C 标准库 rand()，只为让"暂时不迁"的调用点显式可见。
 * 不要在**新增**代码里用它。 */
int rng_legacy_next(void);

#endif /* CANITY_CORE_RNG_H */

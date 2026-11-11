/*
 * CANITY — 共享美术运算接口。
 * 规范：art_direction.h（造型、光照、UI、状态反馈与验证规则）。
 * 实现：src/art.c；具体模型烘焙在 sprites.c，UI 主题与布局在各自模块。
 * 新的共享光照／材质算法在这里声明，不把所有表现代码集中到一个文件。
 */
#ifndef CANITY_ART_H
#define CANITY_ART_H

/* ---- §1 调色板 ---- */
extern const unsigned int ART_GREY_EDGE;
extern const unsigned int ART_GREY_BODY;
extern const unsigned int ART_GREY_LIT;
extern const unsigned int ART_GREY_HOT;
extern const unsigned int ART_EXIT_GAP_ON;
extern const unsigned int ART_EXIT_GAP_OFF;
extern const unsigned int ART_BEAM;

/* ---- §2 距离可读性 ---- */
float ArtMonsterLight(float wx, float wy);   /* 2.5 格全亮 / 11 格外 5% */
float ArtPropLight(float wx, float wy);      /* 3.5 格全亮 / 13 格外 12% */

/* ---- §3 仿 3D ---- */
float ArtBevelK(float u, float v, float thickness, float strength);
float ArtRecessK(float u, float v, float inset, float strength);

/* ---- §4 抖动 ---- */
int ArtBayer4(int x, int y);
int ArtDitherOn(float t, int x, int y, float scale);

/* ---- §5 辅助 ---- */
unsigned int ArtScale(unsigned int c, float k);

/* ---- §6 画风开关（2026-11-11，RC4 上重落） ----
 *   1 = 手工布偶（默认）：布片剪边、织物颗粒、舞台式暖光分级
 *   0 = 经典：改造前的暗色像素恐怖
 * 环境变量 CANITY_STYLE=classic 切回 0；其余取值一律按 1。
 * 只读一次环境变量并缓存 —— 渲染热路径里不做 getenv。 */
int ArtStyleDoll(void);

/* 布偶画风的材质分级：去饱和 38% + 高光偏琥珀 / 阴影偏冷青。
 * 在**烘焙期**调用（贴图、精灵），不要在每帧内层循环里调用。
 * ArtStyleDoll()==0 时原样返回，保证 classic 画面逐像素不变。 */
unsigned int ArtDollGrade(unsigned int c);

#endif

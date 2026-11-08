/* include/phase.h —— 相位信标（域契约）
 *
 * 第 3 轮（2026-11-08）从 game.c 搬出来。
 * 现在改信标只读三个文件：本头 < phase.c < docs/modules/phase.md
 *
 * 语义一句话：一枚信标 = 一次传送。
 * 投出 -> 落点校验 -> 成为锚点 -> 按 Q 传送一次 -> 锚点消失。
 *
 * 铁律（AD-68）：落地判据必须和传送判据是同一个谓词。
 * 早先落地只查 worldMap、传送还查 PropBlocksAt，于是"投得下、传不过去"。
 */
#ifndef CANITY_PHASE_H
#define CANITY_PHASE_H

extern int   phaseOwned;     /* 是否持有装置 */
extern int   phaseCharges;   /* 剩余传送次数（当前规则恒为 1） */
extern int   phasePlaced;    /* 场上是否已有锚点 */
extern float phaseX, phaseY; /* 锚点坐标 */

void PhaseGive(void);
void PhaseReset(void);
void PhaseResetPlacement(void);                        /* 换层：清锚点，留背包 */
void PhaseOnThrowLanded(int kind, float lx, float ly); /* 投掷物落地 kind 12/13 */
int  PhaseTryPlace(void);                              /* 原地放置（测试用） */
int  PhaseTryTeleport(void);                           /* 按 Q；1 = 成功 */

#endif /* CANITY_PHASE_H */

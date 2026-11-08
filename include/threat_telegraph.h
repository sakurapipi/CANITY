/* include/threat_telegraph.h —— 威胁预告层（第 3 轮，2026-11-08）
 *
 * 为什么要有
 *   玩家反馈：「这根本就不恐怖」「好安全」。
 *   调研结论：恐怖来自**预期**（进门前就知道屋里有东西），不是跳出来吓一跳。
 *   所以怪在靠近时必须有可读的**征兆**，而不是凭空出现在脸上。
 *
 * 契约
 *   · 只读怪的位置，不改任何 AI 状态（纯表现层）
 *   · 玩家能看见它（视线内）时不播——那时不需要预告
 *   · 每档独立冷却，避免连响
 */
#ifndef CANITY_THREAT_TELEGRAPH_H
#define CANITY_THREAT_TELEGRAPH_H

/* 每帧调用（在 Update 的怪物 AI 之后）。返回 0 = 附近没有威胁。 */
int  TelegraphTick(float dt);

/* 最近威胁的距离（格）；没有活怪时返回一个很大的数 */
float TelegraphNearestDist(void);

/* 距离档位：0 = 远 / 1 = 中 / 2 = 近 / -1 = 无威胁 */
int  TelegraphBand(void);

/* 环境征兆：手指电闪烁强度 0..1（0 = 不闪） */
float TelegraphFlicker(void);

/* 测试用：重置内部计时 */
void TelegraphReset(void);

#endif

/* include/collapse.h —— 崩塌层（域契约）
 *
 * 第 3 轮（2026-11-08）从 game.c 搬出来。
 *
 * 崩塌 = 锅炉房总闸触发的关卡事故：整层重掷地形，把玩家/怪物从墙里挪出来。
 * 硬约束：重掷后必须仍然可通关 —— CollapseLevelPlayable 校验连通性，
 * 不合格就再掷（有上限），否则玩家会被永久关在死图里。
 */
#ifndef CANITY_COLLAPSE_H
#define CANITY_COLLAPSE_H

extern int collapsedLevel;        /* 本层是否为崩塌层（影响生成参数） */
extern int s_collapseGen;         /* 1 = 下一次 GenerateLevel 走重掷分支 */

void CollapseBegin(void);
int  CollapseTick(float dt);
int  CollapseBusy(void);          /* 1 = 崩塌进行中（冻结输入用） */
int  CollapseRerollCount(void);   /* 重掷次数（测试/平衡用） */
int  CollapseUnstickPlayer(void); /* 把玩家从墙里挪出来；1 = 真的挪过 */


/* Read-only blackout intensity for presentation. */
extern float screenBlackout;

#endif /* CANITY_COLLAPSE_H */

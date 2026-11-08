/* include/difficulty.h —— 难度档位（v2.4.2 第 2 轮）
 *
 * 为什么要有
 *   朋友的实测反馈分两类：「太难，每次都硬吃」和「不恐怖，好安全」。
 *   这两条其实是同一件事的两端 —— 同一套参数不可能同时满足，
 *   所以做成玩家自己选。
 *
 * 三档只调**可感知的六个量**，不动玩法规则：
 *   ① 威胁出现频率   ② 理智流失速度   ③ 守门人堵门时长
 *   ④ 跳脸虫 QTE 窗口 ⑤ 扳手命中窗口   ⑥ 喘息窗口（san 满后多少秒不刷强威胁）
 *
 * 默认 = 标准，和改动前的手感完全一致。
 */
#ifndef CANITY_DIFFICULTY_H
#define CANITY_DIFFICULTY_H

enum { DIFF_EASY = 0, DIFF_NORMAL = 1, DIFF_HARD = 2, DIFF_COUNT = 3 };

extern int difficulty;          /* 当前档位（存档保存） */

/* ① 威胁频率：刷怪间隔 × 这个数（>1 = 更久才来 = 更轻松） */
float DiffThreatIntervalMul(void);

/* ② 理智流失：每秒掉理智 × 这个数 */
float DiffSanityDrainMul(void);

/* ③ 守门人堵门：秒数加这个值（负 = 更短） */
float DiffWardenBlockAdd(void);

/* ④ 跳脸虫 QTE 窗口：0.42 秒 × 这个数 */
float DiffQteWindowMul(void);

/* ⑤ 扳手命中窗口：挥击时长 × 这个数（越大越宽松） */
float DiffWrenchWindowMul(void);

/* ⑥ 喘息窗口：理智回满后强制平静这么多秒（三档都 >0，硬核最短） */
float DiffBreathWindow(void);

/* 中文/英文档位名（设置列表用） */
const char* DiffNameEn(int d);
const char* DiffNameZh(int d);

#endif /* CANITY_DIFFICULTY_H */

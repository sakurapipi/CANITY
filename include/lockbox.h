/*
 * Canity — 上锁箱子（v3.0，移植自上游 v2.2.0）
 *
 * 四种锁型，纯鼠标操作：
 *   拨盘 DIAL   3 个转盘拨对数字
 *   接线 WIRE   把同色端子连起来
 *   均衡 EQ     3 根推子调到正确高度
 *   调波 TUNE   示波器调振幅/频率/相位
 */
#ifndef CANITY_LOCKBOX_H
#define CANITY_LOCKBOX_H

#include "common.h"

#define LOCK_DIALS 3
#define LOCK_DIGITS 8
#define LOCK_WIRES 3
#define LOCK_EQ_BANDS 3
#define LOCK_EQ_STEPS 8
#define LOCK_TUNE_KNOBS 3
#define LOCK_TUNE_STEPS 8

enum {
    LOCK_TYPE_DIAL = 0,
    LOCK_TYPE_WIRE = 1,
    LOCK_TYPE_EQ   = 2,
    LOCK_TYPE_TUNE = 3,
    LOCK_TYPE_COUNT
};



typedef struct { int x0, y0, x1, y1; } LockBox;

/* 命中测试的返回码（上游 ui.h 里的同名枚举） */
enum {
    LOCK_HIT_NONE = 0,
    LOCK_HIT_DIAL,
    LOCK_HIT_UP,
    LOCK_HIT_DOWN,
    LOCK_HIT_NODE_L,
    LOCK_HIT_NODE_R,
    LOCK_HIT_BAR_UP,
    LOCK_HIT_BAR_DOWN,
    LOCK_HIT_KNOB_INC,
    LOCK_HIT_KNOB_DEC,
    LOCK_HIT_BACK
};

/* 运行状态（定义在 lockbox.c） */
extern int lockActive;
extern int lockTarget;
extern int lockCursor;
extern int lockType;
extern int lockDigits[LOCK_DIALS];
extern int lockWireSel;
extern int lockWireLink[LOCK_WIRES];
extern int lockEqVal[LOCK_EQ_BANDS];
extern int lockTuneVal[LOCK_TUNE_KNOBS];
extern int lockIgnoreVK;   /* 打开锁盘的那次按键：松开前不当作「任意键退出」 */

void LockOpenFor(int containerIndex, int type);   /* 打开某个箱子的锁盘 */
void LockClose(void);
void LockClick(int mx, int my);
void DrawLockOverlay(void);
void CloseLockOverlay(void);   /* v3.0 */
void LockClose(void);              /* 锁盘上的鼠标点击 */
int  LockHitTest(int mx, int my, int* action); /* 命中测试（供外部） */

/* 内部 */
int  LockDigit(int code, int dial);
int  LockCorrectCount(int code, const int* digits);
int  LockDialSolved(int dial);
int  LockWireColor(int leftIdx);
int  LockWireRightColor(int rightIdx);
int  LockWireConnect(int left, int right);
int  LockEqTarget(int band);
int  LockEqSolved(void);
int  LockTuneTarget(int knob);
int  LockTuneSolved(void);
void LockTurnDial(int dial, int delta);
void LockEqAdjust(int band, int delta);
void LockTuneAdjust(int knob, int delta);

#endif

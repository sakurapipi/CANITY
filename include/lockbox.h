/*
 * Canity — 上锁箱子（v3.0，移植自上游 v2.2.0）
 *
 * 六种锁型，纯鼠标操作：
 *   拨盘 DIAL     3 个转盘拨对数字（靠声音）
 *   接线 WIRE     把同色端子连起来
 *   均衡 EQ       3 根推子调到正确高度
 *   调波 TUNE     示波器调振幅/频率/相位
 *   过载 OVERLOAD 按住充能，在绿区松手（2026-11-05 新增）
 *   锯锁 SAW      按住来回拖，跟着节拍锯（2026-11-05 新增）
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
    /* 2026-11-05：两种新锁。
       OVERLOAD = 蓄力控制（按住→在绿区松手）
       SAW      = 节奏（按住来回拖，跟拍才涨进度，太快会卡锯） */
    LOCK_TYPE_OVERLOAD = 4,
    LOCK_TYPE_SAW      = 5,
    LOCK_TYPE_COUNT
};

/* 过载：目标区半宽（0..1 的比例）与红线 */
#define LOCK_OV_ZONE_W   0.10f
#define LOCK_OV_RED      1.00f
/* 锯锁：进度换算与卡锯阈值 */
#define LOCK_SAW_GAIN    0.00035f
#define LOCK_SAW_LOSS    0.00012f
#define LOCK_SAW_BEAT_HZ 0.34f     /* 节拍往返频率（Hz），越小越慢 */
#define LOCK_SAW_STALL   0.28f     /* 反向硬顶多久算卡锯（秒） */



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
    LOCK_HIT_HANDLE,      /* 2026-11-05：过载 / 锯锁的「按住」区域 */
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

/* ---- 2026-11-05 新增两种锁的状态 ---- */
extern int   lockMouseDown;      /* 鼠标左键是否按住（过载充能 / 锯） */
extern float lockOvVal;          /* 过载：当前充能 0..1.15 */
extern float lockOvTarget;       /* 过载：绿区中心 */
extern float lockOvFlash;        /* 过载：过红线后的火花计时 */
extern float lockSawProgress;    /* 锯锁：进度 0..1 */
extern float lockSawBeat;        /* 锯锁：节拍标记 0..1 */
extern float lockSawStall;       /* 锯锁：反向硬顶累计 */

void LockTick(float dt);         /* 每帧推进（按住类锁需要时间轴） */
void LockHoldBegin(void);        /* 左键按下 */
void LockHoldEnd(void);          /* 左键松开（过载在这里判定） */

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

extern int locksOpened;   /* v3.3：累计开锁数（成就用） */

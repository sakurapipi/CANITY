/*
 * Canity — 上锁箱子实现（v3.0，代码移植自上游 v2.2.0）
 *
 * 上游把锁状态放在 game.c 的三个平行数组里（contLocked/contLockType/contLockCode）。
 * 我们给 Container 加了对应字段，这里用 g_lock* 访问器桥接，
 * 抄过来的逻辑一行都不用改。
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include "lockbox.h"
#include "game.h"
#include "entity.h"
#include "ui.h"
#include "audio.h"
#include "lang.h"

/* v3.0：**锁盘用自己的 UI 基准**。
   上游的 LUIS(v) 是 v*(winH/600)，我的 LUIS(v) 是 v*(renderH/458)。
   锁盘坐标是按上游 600 基准设计的，用我的 UIS 会放大 1.55 倍 ——
   调波器面板 LUIS(520)=807px 超过屏幕 711px，整个界面就崩了。
   这里恢复成 600 基准，只作用于锁盘内部。 */
#define LUIS(v) ((int)((v) * winH / 600 + 0.5f))   /* winH == renderH（SyncWindowSize 保证） */


/* ---- 桥接层：把 Container 的字段暴露成上游那三个数组的名字 ---- */
static int g_lockCont[MAX_CONTAINERS];
static int g_lockType_[MAX_CONTAINERS];
static int g_lockCode_[MAX_CONTAINERS];

static void LockSyncFromContainers(void) {
    for (int i = 0; i < MAX_CONTAINERS; i++) {
        g_lockCont[i]  = containers[i].locked;
        g_lockType_[i] = containers[i].lockType;
        g_lockCode_[i] = containers[i].lockCode;
    }
}
static void LockSyncToContainers(void) {
    for (int i = 0; i < MAX_CONTAINERS; i++) {
        containers[i].locked   = g_lockCont[i];
        containers[i].lockType = g_lockType_[i];
        containers[i].lockCode = g_lockCode_[i];
    }
}

/* 上游的全局状态 */
int lockActive = 0;
int lockTarget = -1;
int lockCursor = 0;
int lockType = LOCK_TYPE_DIAL;
int lockDigits[LOCK_DIALS] = {0, 0, 0};
int lockWireSel = -1;
int lockWireLink[LOCK_WIRES] = {-1, -1, -1};
int lockEqVal[LOCK_EQ_BANDS] = {0, 0, 0};
int lockTuneVal[LOCK_TUNE_KNOBS] = {0, 0, 0};

int locksOpened = 0;
static float lockCooldown = 0.0f;


/* 上游用 contLocked[] / contLockType[] / contLockCode[]，
   这里用宏把它们映射到桥接数组，抄过来的代码不用改。 */
#define contLocked   g_lockCont
#define contLockType g_lockType_
#define contLockCode g_lockCode_
#define MAX_CONT  MAX_CONTAINERS

/* 对外的开关 */
void LockOpenFor(int idx, int type) {
    if (idx < 0 || idx >= MAX_CONTAINERS) return;
    LockSyncFromContainers();
    lockTarget = idx;
    lockType = type;
    lockActive = 1;
    lockCursor = 0;
    lockWireSel = -1;
    for (int i = 0; i < LOCK_DIALS; i++) lockDigits[i] = 0;   /* v3.0：开新锁必须清空拨盘 */
    for (int i = 0; i < LOCK_WIRES; i++) lockWireLink[i] = -1;
    for (int i = 0; i < LOCK_EQ_BANDS; i++) lockEqVal[i] = 0;
    for (int i = 0; i < LOCK_TUNE_KNOBS; i++) lockTuneVal[i] = 0;
    /* v3.0：**必须释放鼠标** —— 上游 OpenLockOverlay 有这段，我移植时漏了。
       不释放的话：光标还在隐藏（游戏内 ForceCursor(0)），
       鼠标还被锁在屏幕中心做视角控制，玩家既看不到指针也移不动，
       于是「锁盘每一个按钮都点不到」。 */
    {
        extern int isCharging;
        extern float mouseCharge;
        extern int firstMouseMove;
        isCharging = 0;          /* 开锁时不能带着投掷蓄力 */
        mouseCharge = 0.0f;
        ForceCursor(1);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        firstMouseMove = 1;
        PlaySfx(SFX_SEARCH, 0.4f);
    }
    uiDirty = 1;
}
void LockClose(void) {
    LockSyncToContainers();
    lockActive = 0;
    lockTarget = -1;
    uiDirty = 1;
}
int LockDigit(int code, int dial) {
    if (dial < 0 || dial >= LOCK_DIALS) return 0;
    return (code >> (dial * 4)) & (LOCK_DIGITS - 1);
}

/* 比对「位置正确」的位数，用于给玩家反馈 */
int LockCorrectCount(int code, const int* digits) {
    if (!digits) return 0;
    int n = 0;
    for (int d = 0; d < LOCK_DIALS; d++)
        if (digits[d] == LockDigit(code, d)) n++;
    return n;
}

/* 掷密码：全 0 也是合法密码（有没有锁由 contLocked 决定，不看密码值） */
static int RollLockCode(void) {
    int code = 0;
    for (int d = 0; d < LOCK_DIALS; d++)
        code |= (rand() % LOCK_DIGITS) << (d * 4);
    return code;
}

void LockAdjustDigit(int dial, int delta) {
    if (dial < 0 || dial >= LOCK_DIALS) return;
    int v = lockDigits[dial] + delta;
    v %= LOCK_DIGITS;
    if (v < 0) v += LOCK_DIGITS;   /* C 的 % 对负数取负，这里补回环形 */
    lockDigits[dial] = v;
}

void CloseLockOverlay(void) {
    int wasActive = lockActive;
    LockSyncToContainers();   /* v3.0：关锁盘时统一同步 */
    lockActive = 0;
    lockTarget = -1;
    lockCooldown = 0.35f;
    /* v3.0：**解锁后短暂禁止进藏身点** ——
       用户实测「长按解锁箱完之后会被自动吸附到柜子里」：
       E 还按着 -> 自动重复再置 keyPressed -> 藏身分支接管。
       这里上 0.4 秒封锁，等玩家松手。 */
    {
        extern float hideBlockTimer;
        hideBlockTimer = 0.4f;
    }
    if (wasActive) {
        /* 鼠标交还视角控制，并复位输入边沿 ——
           否则「点 BACK」的那一下左键会被当成投掷起手。 */
        ForceCursor(0);
        resetInputState = 1;
        firstMouseMove = 1;
    }
    uiDirty = 1;
}

/* v3.0：上游这个入口和我们的 LockOpenFor 是**两套重复逻辑**（死代码，零调用点）。
   保留符号但改成委托，避免以后有人调它时拿到不一致的行为。 */
void OpenLockOverlay(int index) {
    if (index < 0 || index >= MAX_CONTAINERS) return;
    LockOpenFor(index, g_lockType_[index]);
}

/* 开锁成功：三种锁型共用这一段 */
static void LockUnlock(int idx) {
    if (idx < 0 || idx >= MAX_CONTAINERS) return;
    g_lockCont[idx] = 0;
    g_lockType_[idx] = LOCK_TYPE_DIAL;      /* 开了就不必再留着谜题参数 */
    g_lockCode_[idx] = 0;
    locksOpened++;              /* LOCKSMITH 成就 */
    PlaySfx(SFX_POWER, 0.7f);
    pickupMsg = "UNLOCKED";
    pickupMsgTimer = 2.0f;
    /* v3.0：必须写回容器，否则箱子永久锁死 */
    if (idx >= 0 && idx < MAX_CONTAINERS) {
        containers[idx].locked = 0;   /* v3.0：双保险，直接落盘 */
    }
    LockSyncToContainers();
    CloseLockOverlay();
}

/* 某个拨盘是不是已经停在正确数字上。
   声音是主反馈，这个给的是视觉兜底（静音玩家 / 没听清时也能确认）。 */
int LockDialSolved(int dial) {
    if (!lockActive || lockTarget < 0 || lockTarget >= containerCount) return 0;
    if (dial < 0 || dial >= LOCK_DIALS) return 0;
    return lockDigits[dial] == LockDigit(g_lockCode_[lockTarget], dial);
}

/* 三个拨盘是否都停在正确数字上 */
static int LockAllCorrect(void) {
    if (lockTarget < 0 || lockTarget >= containerCount) return 0;
    return LockCorrectCount(g_lockCode_[lockTarget], lockDigits) == LOCK_DIALS;
}

/* ---- 拨盘锁 ---- */
/* 拨一格：这一格**拨对了**给清脆的「咔哒」，拨错给沉闷的一声。
   玩家靠耳朵找数字，不需要任何密码提示；三个盘都对了直接开锁。 */
void LockTurnDial(int dial, int delta) {
    if (!lockActive || lockType != LOCK_TYPE_DIAL) return;
    if (lockTarget < 0 || lockTarget >= containerCount) return;
    if (dial < 0 || dial >= LOCK_DIALS) return;
    int idx = lockTarget;
    if (!g_lockCont[idx]) { CloseLockOverlay(); return; }

    LockAdjustDigit(dial, delta);
    lockCursor = dial;
    int right = (lockDigits[dial] == LockDigit(g_lockCode_[idx], dial));
    PlaySfx(right ? SFX_PICKUP : SFX_GLASS, right ? 0.60f : 0.45f);

    if (LockAllCorrect()) LockUnlock(idx);
    uiDirty = 1;
}

/* ---- 接线锁 ----
   左端子颜色固定（上到下 0/1/2），右端子是它的一种排列。
   点左端再点右端：颜色相同就接上，不同就断开重选。 */
static const int kWirePerm[6][LOCK_WIRES] = {
    {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}
};

int LockWireColor(int leftIdx) {
    if (leftIdx < 0 || leftIdx >= LOCK_WIRES) return -1;
    return leftIdx;
}

int LockWireRightColor(int rightIdx) {
    if (rightIdx < 0 || rightIdx >= LOCK_WIRES) return -1;
    if (lockTarget < 0 || lockTarget >= containerCount) return -1;
    int p = g_lockCode_[lockTarget] % 6;
    if (p < 0) p += 6;
    return kWirePerm[p][rightIdx];
}

/* 返回 1 = 配色正确、已连上；0 = 颜色不对（拨错，给沉闷声） */
int LockWireConnect(int left, int right) {
    if (!lockActive || lockType != LOCK_TYPE_WIRE) return 0;
    if (left < 0 || left >= LOCK_WIRES || right < 0 || right >= LOCK_WIRES) return 0;
    if (lockWireLink[left] >= 0) return 0;          /* 这根已经接好了 */

    if (LockWireColor(left) != LockWireRightColor(right)) {
        PlaySfx(SFX_GLASS, 0.50f);
        lockWireSel = -1;
        return 0;
    }
    lockWireLink[left] = right;
    lockWireSel = -1;
    PlaySfx(SFX_PICKUP, 0.60f);

    int all = 1;
    for (int i = 0; i < LOCK_WIRES; i++) if (lockWireLink[i] < 0) all = 0;
    if (all && lockTarget >= 0) LockUnlock(lockTarget);
    uiDirty = 1;
    return 1;
}

static void LockWireClickNode(int idx, int side) {
    if (side == 0) {
        lockWireSel = idx;
        PlaySfx(SFX_PICKUP, 0.30f);
        uiDirty = 1;
    } else if (lockWireSel >= 0) {
        LockWireConnect(lockWireSel, idx);
    }
}

/* ---- 均衡器锁 ----
   3 根推子各 0..LOCK_EQ_STEPS-1 档，目标高度藏在 contLockCode 里。
   拨到正确档位给「咔哒」，否则沉闷；三根都对自动开锁。 */
int LockEqTarget(int band) {
    if (band < 0 || band >= LOCK_EQ_BANDS) return 0;
    if (lockTarget < 0 || lockTarget >= containerCount) return 0;
    return (g_lockCode_[lockTarget] >> (band * 3)) & 7;
}

int LockEqSolved(void) {
    if (lockTarget < 0 || lockTarget >= containerCount) return 0;
    for (int b = 0; b < LOCK_EQ_BANDS; b++)
        if (lockEqVal[b] != LockEqTarget(b)) return 0;
    return 1;
}

void LockEqAdjust(int band, int delta) {
    if (!lockActive || lockType != LOCK_TYPE_EQ) return;
    if (lockTarget < 0 || lockTarget >= containerCount) return;
    if (band < 0 || band >= LOCK_EQ_BANDS) return;
    int idx = lockTarget;
    int v = lockEqVal[band] + delta;
    v %= LOCK_EQ_STEPS;
    if (v < 0) v += LOCK_EQ_STEPS;
    lockEqVal[band] = v;

    PlaySfx(v == LockEqTarget(band) ? SFX_PICKUP : SFX_GLASS, 0.50f);
    if (LockEqSolved()) LockUnlock(idx);
    uiDirty = 1;
}

/* ---- 调波器锁 ----
   示波器上叠着两条波形：目标是暗色，玩家的是亮色。
   三个旋钮（振幅 / 频率 / 相位）拧到目标值，两条曲线就完全重合。
   旋钮到位给「咔哒」，没到位给沉闷声 —— 和另外两种锁一致。 */
int LockTuneTarget(int knob) {
    if (knob < 0 || knob >= LOCK_TUNE_KNOBS) return 0;
    if (lockTarget < 0 || lockTarget >= containerCount) return 0;
    return (g_lockCode_[lockTarget] >> (knob * 3)) & 7;
}

int LockTuneSolved(void) {
    if (lockTarget < 0 || lockTarget >= containerCount) return 0;
    for (int k = 0; k < LOCK_TUNE_KNOBS; k++)
        if (lockTuneVal[k] != LockTuneTarget(k)) return 0;
    return 1;
}

void LockTuneAdjust(int knob, int delta) {
    if (!lockActive || lockType != LOCK_TYPE_TUNE) return;
    if (lockTarget < 0 || lockTarget >= containerCount) return;
    if (knob < 0 || knob >= LOCK_TUNE_KNOBS) return;
    int idx = lockTarget;
    int v = lockTuneVal[knob] + delta;
    v %= LOCK_TUNE_STEPS;
    if (v < 0) v += LOCK_TUNE_STEPS;
    lockTuneVal[knob] = v;

    PlaySfx(v == LockTuneTarget(knob) ? SFX_PICKUP : SFX_GLASS, 0.50f);
    if (LockTuneSolved()) LockUnlock(idx);
    uiDirty = 1;
}

/* 鼠标点击锁盘：按锁型分派到各自的控件。
   布局由 ui.c 的 LockHitTest 提供，和绘制共用同一套几何。 */
void LockClick(int mx, int my) {
    int a = -1;
    int hit = LockHitTest(mx, my, &a);
    if (hit == LOCK_HIT_BACK) { CloseLockOverlay(); return; }
    if (a < 0 && hit != LOCK_HIT_BACK) return;

    if (lockType == LOCK_TYPE_DIAL) {
        if (hit == LOCK_HIT_UP) LockTurnDial(a, +1);
        else if (hit == LOCK_HIT_DOWN) LockTurnDial(a, -1);
        else if (hit == LOCK_HIT_DIAL) LockTurnDial(a, +1);   /* 点数字格 = 快转一格 */
    } else if (lockType == LOCK_TYPE_WIRE) {
        if (hit == LOCK_HIT_NODE_L) LockWireClickNode(a, 0);
        else if (hit == LOCK_HIT_NODE_R) LockWireClickNode(a, 1);
    } else if (lockType == LOCK_TYPE_EQ) {
        if (hit == LOCK_HIT_BAR_UP) LockEqAdjust(a, +1);
        else if (hit == LOCK_HIT_BAR_DOWN) LockEqAdjust(a, -1);
    } else if (lockType == LOCK_TYPE_TUNE) {
        if (hit == LOCK_HIT_KNOB_INC) LockTuneAdjust(a, +1);
        else if (hit == LOCK_HIT_KNOB_DEC) LockTuneAdjust(a, -1);
    }
}

static int LockIn(LockBox b, int x, int y) {
    return x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1;
}

static LockBox LockPanelBox(void) {
    /* 调波器的三个完整控件（- / 旋钮 / +）需要比旧版 540 基准更宽。
       其它锁型保留原尺寸，避免无关布局随调波器一起变形。 */
    int pw = (lockType == LOCK_TYPE_TUNE) ? LUIS(680) : LUIS(540);
    /* 调波器要放下示波器 + 三个旋钮，面板比别的锁高一点 */
    int ph = (lockType == LOCK_TYPE_TUNE) ? LUIS(540) : LUIS(440);

    /* v3.0：**面板不能超出窗口** ——
       LUIS 跟 winH 缩放，但窗口可能是宽扁的（1280×960 实测溢出右边，
       第 3 个拨盘与 BACK 出屏）。这里按 winW/winH 双向夹取。 */
    if (pw > winW - LUIS(40)) pw = winW - LUIS(40);
    if (ph > winH - LUIS(40)) ph = winH - LUIS(40);
    if (pw < LUIS(360)) pw = LUIS(360);
    if (ph < LUIS(320)) ph = LUIS(320);
    LockBox p;
    p.x0 = (winW - pw) / 2;
    p.y0 = (winH - ph) / 2;
    p.x1 = p.x0 + pw;
    p.y1 = p.y0 + ph;
    return p;
}

/* 第 d 个拨盘的三块：上箭头 / 数字格 / 下箭头 */
static void LockDialBox(int d, LockBox* up, LockBox* digit, LockBox* down) {
    LockBox p = LockPanelBox();
    int box = LUIS(96), gap = LUIS(34);
    int total = LOCK_DIALS * box + (LOCK_DIALS - 1) * gap;
    int bx = p.x0 + (p.x1 - p.x0 - total) / 2 + d * (box + gap);
    int by = p.y0 + LUIS(122);
    up->x0 = bx; up->y0 = by - LUIS(38); up->x1 = bx + box; up->y1 = by - LUIS(10);
    digit->x0 = bx; digit->y0 = by; digit->x1 = bx + box; digit->y1 = by + box;
    down->x0 = bx; down->y0 = by + box + LUIS(10); down->x1 = bx + box; down->y1 = by + box + LUIS(38);
}

/* 接线锁：左右两列端子。side 0 = 左，1 = 右 */
static void LockWireNodeBox(int side, int idx, LockBox* b) {
    LockBox p = LockPanelBox();
    int size = LUIS(44);
    int x = (side == 0) ? (p.x0 + LUIS(110)) : (p.x1 - LUIS(110) - size);
    int y = p.y0 + LUIS(132) + idx * LUIS(70);
    b->x0 = x; b->y0 = y; b->x1 = x + size; b->y1 = y + size;
}

/* 均衡器：第 band 根推子的柱体 / 上箭头 / 下箭头 */
static void LockEqBarBox(int band, LockBox* bar) {
    LockBox p = LockPanelBox();
    int bw = LUIS(64), bh = LUIS(170), gap = LUIS(46);
    int total = LOCK_EQ_BANDS * bw + (LOCK_EQ_BANDS - 1) * gap;
    int x0 = p.x0 + (p.x1 - p.x0 - total) / 2 + band * (bw + gap);
    int y1 = p.y0 + LUIS(295);
    bar->x0 = x0; bar->y0 = y1 - bh; bar->x1 = x0 + bw; bar->y1 = y1;
}

static void LockEqArrowBox(int band, int isUp, LockBox* b) {
    LockBox bar;
    LockEqBarBox(band, &bar);
    b->x0 = bar.x0;
    b->x1 = bar.x1;
    if (isUp) { b->y1 = bar.y0 - LUIS(8); b->y0 = b->y1 - LUIS(30); }
    else      { b->y0 = bar.y1 + LUIS(8); b->y1 = b->y0 + LUIS(30); }
}

/* 调波器：示波器区域 + 三个旋钮（每个旋钮左右各一个 - / + 按钮） */
static void LockTuneScopeBox(LockBox* s) {
    LockBox p = LockPanelBox();
    s->x0 = p.x0 + LUIS(90);
    s->y0 = p.y0 + LUIS(82);
    s->x1 = p.x1 - LUIS(90);
    s->y1 = p.y0 + LUIS(282);
}

static void LockTuneBoxes(int i, LockBox* dec, LockBox* knob, LockBox* inc) {
    LockBox p = LockPanelBox();
    int ks = LUIS(76), bs = LUIS(36);
    int buttonGap = LUIS(8), controlGap = LUIS(24);
    int controlW = bs + buttonGap + ks + buttonGap + bs;
    int total = LOCK_TUNE_KNOBS * controlW + (LOCK_TUNE_KNOBS - 1) * controlGap;
    int x0 = p.x0 + (p.x1 - p.x0 - total) / 2 + i * (controlW + controlGap);
    int ky = p.y0 + LUIS(312);
    dec->x0 = x0;
    dec->y0 = ky + (ks - bs) / 2;
    dec->x1 = dec->x0 + bs;
    dec->y1 = dec->y0 + bs;

    knob->x0 = dec->x1 + buttonGap;
    knob->y0 = ky;
    knob->x1 = knob->x0 + ks;
    knob->y1 = knob->y0 + ks;

    inc->x0 = knob->x1 + buttonGap;
    inc->y0 = dec->y0;
    inc->x1 = inc->x0 + bs;
    inc->y1 = inc->y0 + bs;
}

/* 调波器的绿色数值读数，放在旋钮内部，避免再抢占旋钮行下面的标签空间。 */
static void LockTuneReadout(LockBox knob, int value, int onTarget) {
    char buf[16];
    sprintf(buf, "%d/7", value);
    int pad = LUIS(5);
    int rw = TextWidthWin(buf) + pad * 2;
    int rh = TextHeightWin() + LUIS(4);
    int x0 = (knob.x0 + knob.x1 - rw) / 2;
    int y0 = knob.y1 - rh - LUIS(5);
    BlendRectWin(x0, y0, x0 + rw, y0 + rh, 0x00102218u, 0.96f);
    FrameRectWin(x0, y0, x0 + rw, y0 + rh,
                 onTarget ? 0x0040FF80u : 0x00307050u, 0.95f, LUIS(1));
    DrawTextCenterWin((knob.x0 + knob.x1) / 2, y0 + LUIS(2), buf,
                      onTarget ? 0x0080E0A0u : 0x0060B080u);
}

/* 底部只有一个 BACK 按钮：谜题一解开就自动开锁，不需要 TRY */
static LockBox LockBackBox(void) {
    LockBox p = LockPanelBox();
    int bw = LUIS(200), bh = LUIS(48);
    int x0 = p.x0 + (p.x1 - p.x0 - bw) / 2;
    int y0 = p.y1 - LUIS(95);
    LockBox b = { x0, y0, x0 + bw, y0 + bh };
    return b;
}

int LockHitTest(int mx, int my, int* a) {
    if (a) *a = -1;
    if (LockIn(LockBackBox(), mx, my)) return LOCK_HIT_BACK;

    if (lockType == LOCK_TYPE_DIAL) {
        for (int d = 0; d < LOCK_DIALS; d++) {
            LockBox up, dg, dn;
            LockDialBox(d, &up, &dg, &dn);
            if (LockIn(dg, mx, my)) { if (a) *a = d; return LOCK_HIT_DIAL; }
            if (LockIn(up, mx, my)) { if (a) *a = d; return LOCK_HIT_UP; }
            if (LockIn(dn, mx, my)) { if (a) *a = d; return LOCK_HIT_DOWN; }
        }
    } else if (lockType == LOCK_TYPE_WIRE) {
        for (int i = 0; i < LOCK_WIRES; i++) {
            LockBox b;
            LockWireNodeBox(0, i, &b);
            if (LockIn(b, mx, my)) { if (a) *a = i; return LOCK_HIT_NODE_L; }
            LockWireNodeBox(1, i, &b);
            if (LockIn(b, mx, my)) { if (a) *a = i; return LOCK_HIT_NODE_R; }
        }
    } else if (lockType == LOCK_TYPE_EQ) {
        for (int band = 0; band < LOCK_EQ_BANDS; band++) {
            LockBox up, dn;
            LockEqArrowBox(band, 1, &up);
            LockEqArrowBox(band, 0, &dn);
            if (LockIn(up, mx, my)) { if (a) *a = band; return LOCK_HIT_BAR_UP; }
            if (LockIn(dn, mx, my)) { if (a) *a = band; return LOCK_HIT_BAR_DOWN; }
        }
    } else if (lockType == LOCK_TYPE_TUNE) {
        for (int k = 0; k < LOCK_TUNE_KNOBS; k++) {
            LockBox dec, kn, inc;
            LockTuneBoxes(k, &dec, &kn, &inc);
            if (LockIn(dec, mx, my)) { if (a) *a = k; return LOCK_HIT_KNOB_DEC; }
            if (LockIn(inc, mx, my)) { if (a) *a = k; return LOCK_HIT_KNOB_INC; }
        }
    }
    return LOCK_HIT_NONE;
}

/* 一个带悬停反馈的按钮 */
static void LockDrawButton(LockBox b, const char* label, int hot, unsigned int accent) {
    BlendRectWin(b.x0, b.y0, b.x1, b.y1, hot ? 0x00323A42u : 0x001A2026u, 0.98f);
    FrameRectWin(b.x0, b.y0, b.x1, b.y1, hot ? accent : 0x004A525Au, 0.95f, hot ? 2 : 1);
    DrawTextCenterWin((b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2 - TextHeightWin() / 2,
                      label, hot ? 0x00FFE080u : 0x00C8D0D8u);
}

/* 开锁覆盖层（v1.8.0）：游戏**不暂停** —— 只是挡住移动与搜索，怪照常过来。
   所以先把整屏压暗，让面板从场景里浮出来，而不是在花花绿绿的背景上叠半透明块。 */
static void LockDrawDialBody(int hit, int ha) {
    for (int d = 0; d < LOCK_DIALS; d++) {
        LockBox up, dg, dn;
        LockDialBox(d, &up, &dg, &dn);
        int sel = (d == lockCursor);
        int hotUp = (hit == LOCK_HIT_UP && ha == d);
        int hotDn = (hit == LOCK_HIT_DOWN && ha == d);
        int hotDg = (hit == LOCK_HIT_DIAL && ha == d);

        LockDrawButton(up, "^", hotUp, 0x00E0C060u);
        LockDrawButton(dn, "v", hotDn, 0x00E0C060u);

        unsigned int fill = sel ? 0x00202832u : 0x0013181Du;
        unsigned int edge = sel ? 0x00E0C060u : (hotDg ? 0x00988A50u : 0x004A525Au);
        BlendRectWin(dg.x0, dg.y0, dg.x1, dg.y1, fill, 0.98f);
        FrameRectWin(dg.x0, dg.y0, dg.x1, dg.y1, edge, 0.95f, sel ? LUIS(2) : 1);
        /* 这一格已经拨对：内侧描一圈细绿边（声音之外的视觉兜底） */
        if (LockDialSolved(d)) {
            int t = LUIS(2);
            FrameRectWin(dg.x0 + t, dg.y0 + t, dg.x1 - t, dg.y1 - t,
                         0x0040FF80u, 0.85f, 2);
        }

        char dbuf[8];
        sprintf(dbuf, "%d", lockDigits[d] % LOCK_DIGITS);
        DrawTextCenterWin((dg.x0 + dg.x1) / 2, (dg.y0 + dg.y1) / 2 - TextHeightWin() / 2,
                          dbuf, sel ? 0x00FFE080u : 0x00D0D8E0u);
    }

    /* 解谜提示：声音是主反馈，绿色圈是视觉兜底 */
    LockBox p = LockPanelBox();
    DrawTextCenterWin(winW / 2, p.y0 + LUIS(286), "CLICK + GREEN RING = RIGHT DIGIT", 0x00B0B8C0u);
    DrawTextCenterWin(winW / 2, p.y0 + LUIS(310), "THUD = WRONG", 0x00889098u);
}

/* 端子颜色：0 红 / 1 绿 / 2 蓝 */
static unsigned int LockTermColor(int c) {
    if (c == 0) return 0x00E05050u;
    if (c == 1) return 0x0050D060u;
    if (c == 2) return 0x005090E0u;
    return 0x00909090u;
}

/* 粗线（Bresenham），用于接线锁的导线 */
static void LockDrawLine(int x0, int y0, int x1, int y1, unsigned int c) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = (dx < 0) ? -dx : dx, ady = (dy < 0) ? -dy : dy;
    int sx = (dx < 0) ? -1 : 1, sy = (dy < 0) ? -1 : 1;
    int err = adx - ady;
    for (int guard = 0; guard < 4000; guard++) {
        BlendRectWin(x0, y0, x0 + LUIS(3), y0 + LUIS(3), c, 0.95f);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -ady) { err -= ady; x0 += sx; }
        if (e2 < adx) { err += adx; y0 += sy; }
    }
}

static void LockDrawWireBody(int hit, int ha) {
    LockBox p = LockPanelBox();

    /* 先画已经接好的线，压在端子下面 */
    for (int i = 0; i < LOCK_WIRES; i++) {
        if (lockWireLink[i] < 0) continue;
        LockBox lb, rb;
        LockWireNodeBox(0, i, &lb);
        LockWireNodeBox(1, lockWireLink[i], &rb);
        LockDrawLine((lb.x0 + lb.x1) / 2, (lb.y0 + lb.y1) / 2,
                     (rb.x0 + rb.x1) / 2, (rb.y0 + rb.y1) / 2,
                     LockTermColor(LockWireColor(i)));
    }

    for (int i = 0; i < LOCK_WIRES; i++) {
        LockBox b;
        LockWireNodeBox(0, i, &b);
        BlendRectWin(b.x0, b.y0, b.x1, b.y1, LockTermColor(LockWireColor(i)), 0.95f);
        int selHere = (lockWireSel == i);
        unsigned int edge = selHere ? 0x00FFE080u
                          : (hit == LOCK_HIT_NODE_L && ha == i) ? 0x00FFFFFFu : 0x00202020u;
        FrameRectWin(b.x0, b.y0, b.x1, b.y1, edge, 0.95f, selHere ? LUIS(3) : LUIS(2));

        LockWireNodeBox(1, i, &b);
        BlendRectWin(b.x0, b.y0, b.x1, b.y1, LockTermColor(LockWireRightColor(i)), 0.95f);
        FrameRectWin(b.x0, b.y0, b.x1, b.y1,
                     (hit == LOCK_HIT_NODE_R && ha == i) ? 0x00FFFFFFu : 0x00202020u,
                     0.95f, LUIS(2));
    }

    DrawTextCenterWin(winW / 2, p.y0 + LUIS(96),
                      (lockWireSel >= 0) ? "PICK A MATCHING TERMINAL" : "PICK A LEFT TERMINAL",
                      (lockWireSel >= 0) ? 0x00E0C060u : 0x00707880u);
}

static void LockDrawEqBody(int hit, int ha) {
    LockBox p = LockPanelBox();
    for (int band = 0; band < LOCK_EQ_BANDS; band++) {
        LockBox bar, up, dn;
        LockEqBarBox(band, &bar);
        LockEqArrowBox(band, 1, &up);
        LockEqArrowBox(band, 0, &dn);

        int solved = (lockEqVal[band] == LockEqTarget(band));
        LockDrawButton(up, "^", hit == LOCK_HIT_BAR_UP && ha == band, 0x00E0C060u);
        LockDrawButton(dn, "v", hit == LOCK_HIT_BAR_DOWN && ha == band, 0x00E0C060u);

        /* 槽 */
        BlendRectWin(bar.x0, bar.y0, bar.x1, bar.y1, 0x0013181Du, 0.98f);
        FrameRectWin(bar.x0, bar.y0, bar.x1, bar.y1, 0x004A525Au, 0.9f, 1);

        /* 当前高度 */
        int barH = bar.y1 - bar.y0;
        int hgt = barH * (lockEqVal[band] + 1) / LOCK_EQ_STEPS;
        BlendRectWin(bar.x0 + LUIS(4), bar.y1 - hgt, bar.x1 - LUIS(4), bar.y1 - LUIS(4),
                     solved ? 0x0040C060u : 0x004080C0u, 0.95f);

        /* 目标高度：一条绿色标线 —— 这就是视觉线索 */
        int thgt = barH * (LockEqTarget(band) + 1) / LOCK_EQ_STEPS;
        int ty = bar.y1 - thgt;
        BlendRectWin(bar.x0 - LUIS(6), ty, bar.x1 + LUIS(6), ty + LUIS(3), 0x0040FF80u, 0.9f);
    }
    DrawTextCenterWin(winW / 2, p.y0 + LUIS(96), "PUSH EACH BAR TO THE GREEN LINE", 0x00707880u);
}

/* 调波器：画一条正弦波。amp/freq/phase 都是 0..7 的整数档。 */
static void LockTuneWave(LockBox a, int amp, int freq, int phase, unsigned int color) {
    int w = a.x1 - a.x0;
    if (w <= 0) return;
    int midY = (a.y0 + a.y1) / 2;
    float cycles = 1.0f + (float)freq * 3.0f / 7.0f;              /* 1 ~ 4 个周期 */
    float ph = (float)phase * 6.2831853f / (float)LOCK_TUNE_STEPS;
    float ampScale = 0.18f + 0.82f * (float)amp / 7.0f;
    float maxAmp = (float)(a.y1 - a.y0) * 0.42f;
    int prevX = 0, prevY = 0;
    for (int i = 0; i <= w; i += 2) {
        float u = (float)i / (float)w;
        float s = sinf(6.2831853f * cycles * u + ph);
        int x = a.x0 + i;
        int y = midY - (int)(s * maxAmp * ampScale);
        if (i > 0) LockDrawLine(prevX, prevY, x, y, color);
        prevX = x;
        prevY = y;
    }
}

static void LockDrawTuneBody(int hit, int ha) {
    static const char* knobNames[LOCK_TUNE_KNOBS] = {
        "1.AMPLITUDE", "2.FREQUENCY", "3.PHASE"
    };
    LockBox p = LockPanelBox();
    LockBox scope;
    LockTuneScopeBox(&scope);

    /* 示波器：底 + 边框 + 中线和竖分隔 */
    BlendRectWin(scope.x0, scope.y0, scope.x1, scope.y1, 0x00060C0Au, 0.96f);
    FrameRectWin(scope.x0, scope.y0, scope.x1, scope.y1, 0x00384A44u, 0.9f, LUIS(2));
    int midY = (scope.y0 + scope.y1) / 2;
    BlendRectWin(scope.x0 + LUIS(4), midY, scope.x1 - LUIS(4), midY + 1, 0x001A2624u, 0.9f);
    for (int q = 1; q <= 3; q++) {
        int gx = scope.x0 + (scope.x1 - scope.x0) * q / 4;
        BlendRectWin(gx, scope.y0 + LUIS(4), gx + 1, scope.y1 - LUIS(4), 0x001A2624u, 0.9f);
    }

    /* 目标波形（暗）+ 玩家波形（亮）：重合即匹配 */
    LockTuneWave(scope, LockTuneTarget(0), LockTuneTarget(1), LockTuneTarget(2), 0x004A5A62u);
    LockTuneWave(scope, lockTuneVal[0], lockTuneVal[1], lockTuneVal[2], 0x0060FFC0u);

    for (int k = 0; k < LOCK_TUNE_KNOBS; k++) {
        LockBox dec, kn, inc;
        LockTuneBoxes(k, &dec, &kn, &inc);
        int onTarget = (lockTuneVal[k] == LockTuneTarget(k));

        LockDrawButton(dec, "-", hit == LOCK_HIT_KNOB_DEC && ha == k, 0x00E0C060u);
        LockDrawButton(inc, "+", hit == LOCK_HIT_KNOB_INC && ha == k, 0x00E0C060u);

        BlendRectWin(kn.x0, kn.y0, kn.x1, kn.y1, 0x001A2026u, 0.98f);
        FrameRectWin(kn.x0, kn.y0, kn.x1, kn.y1,
                     onTarget ? 0x0040FF80u : 0x005A6268u, 0.95f, onTarget ? LUIS(2) : 1);

        /* 指针：0 档朝上，顺时针一圈 */
        float ang = (float)lockTuneVal[k] * 6.2831853f / (float)LOCK_TUNE_STEPS - 1.5707963f;
        int cx = (kn.x0 + kn.x1) / 2, cy = (kn.y0 + kn.y1) / 2;
        int r = (kn.x1 - kn.x0) / 2 - LUIS(8);
        LockDrawLine(cx, cy, cx + (int)(cosf(ang) * r), cy + (int)(sinf(ang) * r),
                     onTarget ? 0x0040FF80u : 0x00E0C060u);

        LockTuneReadout(kn, lockTuneVal[k], onTarget);

        DrawTextCenterWin((kn.x0 + kn.x1) / 2, kn.y1 + LUIS(10), knobNames[k],
                          onTarget ? 0x0080E0A0u : 0x00A8B0B8u);
    }

    DrawTextCenterWin(winW / 2, p.y0 + LUIS(236), "MATCH THE BRIGHT WAVE TO THE DIM ONE", 0x00707880u);
}

/* 开锁覆盖层总入口：四种锁型共用同一块面板与底栏 */

/* ==================== v3.0：脏污与工业质感 ====================
 * 用户要求「更像逃生里的接线盒，更脏、更有贵感，同时遵循上游风格」。
 * 上游是纯色块 + 细边框，干净得像调试面板；接线盒该有的东西一样没有：
 * 金属划痕、锈迹、铆钉、内凹边缘。
 * 这里补三层，全部**确定性**（不用 rand，避免每帧闪烁）：
 *   ① 细噪点     —— 模拟磨砂金属表面
 *   ② 锈斑       —— 几处不规则暖褐色块
 *   ③ 铆钉       —— 四角 + 长边中点的实心小圆
 * ============================================================== */

/* 确定性伪随机（同一个坐标永远同一个值，不会闪） */
static unsigned int LockNoiseAt(int x, int y) {
    unsigned int h = (unsigned int)(x * 374761393) + (unsigned int)(y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* ① 细噪点：稀疏的暗点 + 亮点，让平涂的表面「起毛」
   v3.0 修正：**第一版会崩**。
   原写法按 3px 步长扫满整个面板 —— 700x680 的面板就是
   233 x 226 = 5.2 万次 BlendRectWin/帧，每帧还要各自跑一遍像素循环，
   直接把渲染线程拖死（用户实测「开锁时程序崩溃」）。
   现在改成**固定上限的稀疏采样**：最多 320 个点，一个点一次 2x2 填充。 */
static void LockGrain(LockBox b, float amount) {
    if (amount <= 0.0f) return;
    int w = b.x1 - b.x0, h = b.y1 - b.y0;
    if (w < 8 || h < 8) return;
    int marks = 320;
    int sz = LUIS(2);
    if (sz < 1) sz = 1;
    for (int k = 0; k < marks; k++) {
        unsigned int hsh = LockNoiseAt(k * 37 + 11, k * 911 + 5);
        int x = b.x0 + (int)(hsh % (unsigned int)w);
        int y = b.y0 + (int)((hsh >> 10) % (unsigned int)h);
        int dark = (hsh >> 20) & 1u;
        unsigned int c = dark ? 0x00000000u : 0x00686058u;
        float a = (dark ? 0.30f : 0.12f) * amount;
        BlendRectWin(x, y, x + sz, y + sz, c, a);
    }
}

/* ② 锈斑：几处固定的暖褐色块，边缘用两层不同透明度做毛边 */
static void LockRust(LockBox b, float amount) {
    if (amount <= 0.0f) return;
    int w = b.x1 - b.x0, h = b.y1 - b.y0;
    if (w < 40 || h < 40) return;
    for (int k = 0; k < 5; k++) {
        unsigned int seed = LockNoiseAt(k * 131 + 7, k * 977 + 3);
        int cx = b.x0 + (int)(seed % (unsigned int)w);
        int cy = b.y0 + (int)((seed >> 8) % (unsigned int)h);
        int rw = LUIS(10 + (int)((seed >> 16) % 22u));
        int rh = LUIS(6 + (int)((seed >> 20) % 14u));
        BlendRectWin(cx - rw, cy - rh, cx + rw, cy + rh, 0x00703A18u, 0.10f * amount);
        BlendRectWin(cx - rw / 2, cy - rh / 2,
                     cx + rw / 2, cy + rh / 2, 0x00884A20u, 0.12f * amount);
    }
}

/* ③ 铆钉：实心小圆 + 左上高光，让边框有厚度 */
static void LockRivet(int cx, int cy, int r) {
    if (r < 2) r = 2;
    /* v3.0：原来每个像素调一次 BlendRectWin，4 个铆钉 = 300+ 次调用/帧。
       改成 3 个矩形近似一个带高光的圆盘，调用次数降到 12。 */
    BlendRectWin(cx - r, cy - r, cx + r + 1, cy + r + 1, 0x00383028u, 0.85f);      /* 本体 */
    BlendRectWin(cx - r, cy - r, cx + r + 1, cy - r + 2, 0x00A09888u, 0.75f);      /* 上高光 */
    BlendRectWin(cx - r, cy - r, cx - r + 2, cy + r + 1, 0x00807868u, 0.55f);      /* 左高光 */
    BlendRectWin(cx + r - 1, cy - r, cx + r + 1, cy + r + 1, 0x00000000u, 0.45f);  /* 右暗边 */
}

/* 一条「脏面板」：外框阴影 → 底漆 → 噪点 → 锈 → 内凹高光 → 铆钉 */
static void LockPanelDirty(LockBox b, float grime) {
    /* 投影 */
    BlendRectWin(b.x0 + LUIS(3), b.y0 + LUIS(3),
                 b.x1 + LUIS(3), b.y1 + LUIS(3), 0x00000000u, 0.55f);
    /* 底漆：偏冷的深灰绿，比原来的纯黑更像铁皮 */
    BlendRectWin(b.x0, b.y0, b.x1, b.y1, 0x0015191Cu, 0.99f);
    /* 脏污 */
    LockRust(b, grime);
    LockGrain(b, grime);
    /* 外框：上/左偏亮，下/右偏暗 = 内凹 */
    FrameRectWin(b.x0, b.y0, b.x1, b.y1, 0x00484034u, 0.95f, LUIS(2));
    BlendRectWin(b.x0, b.y0, b.x1, b.y0 + LUIS(1), 0x00706858u, 0.55f);
    BlendRectWin(b.x0, b.y0, b.x0 + LUIS(1), b.y1, 0x00706858u, 0.45f);
    BlendRectWin(b.x0, b.y1 - LUIS(1), b.x1, b.y1, 0x00000000u, 0.55f);
    BlendRectWin(b.x1 - LUIS(1), b.y0, b.x1, b.y1, 0x00000000u, 0.45f);
    /* 四角铆钉 */
    int rr = LUIS(4);
    int inset = LUIS(10);
    LockRivet(b.x0 + inset, b.y0 + inset, rr);
    LockRivet(b.x1 - inset, b.y0 + inset, rr);
    LockRivet(b.x0 + inset, b.y1 - inset, rr);
    LockRivet(b.x1 - inset, b.y1 - inset, rr);
}

void DrawLockOverlay(void) {
    if (!lockActive) return;   /* v3.0：没开锁盘就什么都不画 */
    LockBox p = LockPanelBox();
    int mx = g_mousePos.x, my = g_mousePos.y;
    int ha = -1;
    int hit = LockHitTest(mx, my, &ha);

    BlendRectWin(0, 0, winW, winH, 0x00000000u, 0.62f);

    /* v3.0：换成脏面板 —— 投影 + 铁皮底 + 锈 + 噪点 + 内凹 + 铆钉 */
    LockPanelDirty(p, 1.0f);

    /* 顶部铭牌：一小条深色底 + 蚀刻字，比裸字更像机器上的标签 */
    {
        int tw = TextWidthWin("LOCKED") + LUIS(28);
        int tx = (p.x0 + p.x1) / 2 - tw / 2;
        int ty = p.y0 + LUIS(18);
        BlendRectWin(tx, ty - LUIS(3), tx + tw, ty + TextHeightWin() + LUIS(3),
                     0x00201814u, 0.90f);
        FrameRectWin(tx, ty - LUIS(3), tx + tw, ty + TextHeightWin() + LUIS(3),
                     0x00805030u, 0.80f, 1);
        DrawTextCenterWin(winW / 2, ty, "LOCKED", 0x00FF6050u);
    }

    if (lockType == LOCK_TYPE_DIAL) {
        DrawTextCenterWin(winW / 2, p.y0 + LUIS(52), "TURN THE DIALS UNTIL EVERY ONE CLICKS", 0x0090A0A8u);
        LockDrawDialBody(hit, ha);
    } else if (lockType == LOCK_TYPE_WIRE) {
        DrawTextCenterWin(winW / 2, p.y0 + LUIS(52), "CONNECT EACH LEFT TERMINAL TO ITS COLOR", 0x0090A0A8u);
        LockDrawWireBody(hit, ha);
        DrawTextCenterWin(winW / 2, p.y0 + LUIS(336), "WRONG COLOR = THUD", 0x00889098u);
    } else if (lockType == LOCK_TYPE_EQ) {
        DrawTextCenterWin(winW / 2, p.y0 + LUIS(52), "MATCH EVERY BAR TO ITS GREEN LINE", 0x0090A0A8u);
        LockDrawEqBody(hit, ha);
    } else {
        DrawTextCenterWin(winW / 2, p.y0 + LUIS(52), "TUNE THE SCOPE UNTIL THE WAVES MATCH", 0x0090A0A8u);
        LockDrawTuneBody(hit, ha);
    }

    LockDrawButton(LockBackBox(), "BACK", hit == LOCK_HIT_BACK, 0x00C05050u);

    DrawTextCenterWin(winW / 2, p.y1 - LUIS(32), "MOUSE ONLY    -    ESC / M TO LEAVE", 0x00707880u);
}

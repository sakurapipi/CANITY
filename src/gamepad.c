#include <windows.h>
#include <math.h>
#include "entity.h"
#include "common.h"
#include "gamepad.h"
#include "game.h"

/* ------------------------------------------------------------------ */
/* v2.0 手柄支持（动态加载 xinput，不引入链接依赖）                      */
/*   - 左摇杆：移动（直接写 keys[]，和键盘共用同一套移动逻辑）           */
/*   - 右摇杆：转身                                              */
/*   - A 交互 / B 手电 / X 攻击 / Y 摇电 / LB 投掷 / Start 暂停          */
/* ------------------------------------------------------------------ */

typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XGPAD;
typedef struct { DWORD dwPacketNumber; XGPAD Gamepad; } XGSTATE;

typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XGSTATE*);

static PFN_XInputGetState pGetState = NULL;
static int gpTried = 0;
static int gpConnected = 0;
static int prevBtn = 0;

void GamepadInit(void) {
    if (gpTried) return;
    gpTried = 1;
    const char* dlls[4] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", "xinput1_2.dll" };
    for (int i = 0; i < 4 && !pGetState; i++) {
        HMODULE h = LoadLibraryA(dlls[i]);
        if (!h) continue;
        pGetState = (PFN_XInputGetState)GetProcAddress(h, "XInputGetState");
    }
}

int GamepadConnected(void) { return gpConnected; }

/* 死区 0.25 */
static float deadzone(SHORT v) {
    float f = (float)v / 32767.0f;
    if (f > -0.25f && f < 0.25f) return 0.0f;
    return f;
}

void GamepadPoll(void) {
    if (!pGetState) { GamepadInit(); if (!pGetState) return; }
    XGSTATE st;
    if (pGetState(0, &st) != 0) { gpConnected = 0; return; }
    gpConnected = 1;
    XGPAD* g = &st.Gamepad;

    /* 移动：写到 keys[]，和键盘完全同一套逻辑 */
    /* v2.2 修复：只在摇杆/扳机真的被推动时才写 keys[]。
       原来无条件写 —— 插上手柄（哪怕没碰）键盘就走不了、也冲刺不了，
       因为默认绑定正是 WASD + Shift。 */
    float lx = deadzone(g->sThumbLX), ly = deadzone(g->sThumbLY);
    int padMove = (lx != 0.0f || ly != 0.0f);
    int padSprint = (g->bLeftTrigger > 100 || (g->wButtons & 0x0100));
    if (padMove) {
        keys['W'] = (ly > 0.35f);
        keys['S'] = (ly < -0.35f);
        keys['A'] = (lx < -0.35f);
        keys['D'] = (lx > 0.35f);
    }
    if (padSprint) keys[VK_SHIFT] = 1;

    /* 转身：右摇杆直接改相机方向 */
    float rx = deadzone(g->sThumbRX);
    if (rx != 0.0f) {
        float ang = rx * 0.055f;
        float c = cosf(ang), s2 = sinf(ang);
        float nx = dirX * c - dirY * s2;
        float ny = dirX * s2 + dirY * c;
        dirX = nx; dirY = ny;
        /* v2.2 修复：原来把 plane 长度写死 0.66，用手柄转一次身
           FOV 设置（95/105 度）就被重置回 66 度。只旋转，不重设长度。 */
        {
            float px = planeX * c - planeY * s2;
            float py = planeX * s2 + planeY * c;
            planeX = px; planeY = py;
        }
    }

    /* 按钮边缘 → 写 keyPressed（一次性动作走同一条通路） */
    int btn = g->wButtons;
    int edge = btn & ~prevBtn;
    if (edge & 0x1000) { keyPressed[VK_RETURN] = 1; }        /* A 确认/交互 */
    if (edge & 0x2000) { keyPressed[VK_ESCAPE] = 1; }        /* B 取消/暂停 */
    if (edge & 0x4000) { keys[VK_LBUTTON] = 1; keyPressed[VK_LBUTTON] = 1; }  /* X 攻击 */
    if (edge & 0x8000) { keyPressed['R'] = 1; }              /* Y 摇电 */
    if (edge & 0x0001) { keyPressed[VK_UP] = 1; }            /* 十字上 */
    if (edge & 0x0002) { keyPressed[VK_DOWN] = 1; }          /* 十字下 */
    if (edge & 0x0004) { keyPressed[VK_LEFT] = 1; }
    if (edge & 0x0008) { keyPressed[VK_RIGHT] = 1; }
    if (!(btn & 0x4000)) keys[VK_LBUTTON] = 0;
    prevBtn = btn;
}

/* =====================================================================
 * CANITY 网页后端的 Win32 兼容垫片
 * ---------------------------------------------------------------------
 * 游戏代码里有 52 处 RECT、17 处 DWORD、25 个 VK_* 常量……它们大多只是
 * "顺手借用"的数据类型与键码编号，并不真的调用 Win32。
 * 与其把 60 个文件改成 #ifdef 迷宫，不如提供这一层最小的同名定义：
 *   · 平台层之外的代码**不调用**任何 Win32 函数，所以这里只有类型/常量
 *   · 真正的系统调用全部在 src/platform/plat_web.c
 *
 * 铁律：这里**不许**出现任何函数实现。有函数就说明有代码绕过了平台层。
 * ===================================================================== */
#ifndef CANITY_WEB_COMPAT_H
#define CANITY_WEB_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#define MAX_PATH 260
#define WINAPI
#define CALLBACK
#define TRUE 1
#define FALSE 0

typedef int            BOOL;
typedef unsigned int   DWORD;
typedef int            LONG;
typedef unsigned int   UINT;
typedef void*          HANDLE;
typedef void*          HWND;
typedef void*          HDC;
typedef void*          HINSTANCE;
typedef uint64_t       WPARAM;      /* 与 Win32 64 位一致 */
typedef int64_t        LPARAM;
typedef int64_t        LRESULT;

typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;

/* ---- 虚拟键码：只保留游戏实际用到的 25 个 ----
   编号与 Windows 一致，这样存档里的键位绑定跨平台通用。 */
#define VK_LBUTTON   0x01
#define VK_RBUTTON   0x02
#define VK_MBUTTON   0x04
#define VK_TAB       0x09
#define VK_RETURN    0x0D
#define VK_SHIFT     0x10
#define VK_CONTROL   0x11
#define VK_MENU      0x12
#define VK_ESCAPE    0x1B
#define VK_SPACE     0x20
#define VK_HOME      0x24
#define VK_LEFT      0x25
#define VK_UP        0x26
#define VK_RIGHT     0x27
#define VK_DOWN      0x28
#define VK_LSHIFT    0xA0
#define VK_RSHIFT    0xA1
#define VK_LCONTROL  0xA2
#define VK_RCONTROL  0xA3
#define VK_LMENU     0xA4
#define VK_RMENU     0xA5
#define VK_NUMPAD0   0x60
#define VK_NUMPAD9   0x69
#define VK_F9        0x78
#define VK_F11       0x7A

#endif /* CANITY_WEB_COMPAT_H */

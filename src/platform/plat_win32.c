/* =====================================================================
 * 平台层 · Windows 后端
 * ---------------------------------------------------------------------
 * 这里搬进原来散在 render.c / ui.c / main.c / save.c 里的 Win32 调用。
 * 游戏层只认 include/plat.h。
 *
 * 分期：R1 实现 时间 / 窗口 / 渲染目标 / 呈现 / 清理；
 *       R2 接输入；R3 接文件与音频。
 * 尚未接管的函数先给出**能编译且行为正确**的实现，不写空壳假成功。
 * ===================================================================== */
#include "common.h"
#include "plat.h"
#include "game.h"

#include <stdio.h>

/* ---------------------------------------------------------------------
 * 时间
 * ------------------------------------------------------------------- */
static LARGE_INTEGER s_freq;
static LARGE_INTEGER s_t0;
static int s_clockReady = 0;

static void PlatClockInit(void) {
    if (s_clockReady) return;
    QueryPerformanceFrequency(&s_freq);
    QueryPerformanceCounter(&s_t0);
    s_clockReady = 1;
}

void PlatSleepMs(int ms) {
    if (ms <= 0) return;
    Sleep((DWORD)ms);
}

double PlatNow(void) {
    PlatClockInit();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (s_freq.QuadPart <= 0) return 0.0;
    return (double)(now.QuadPart - s_t0.QuadPart) / (double)s_freq.QuadPart;
}

/* ---------------------------------------------------------------------
 * 窗口
 * ------------------------------------------------------------------- */
void PlatWindowSize(int* w, int* h) {
    int cw = 0, ch = 0;
    if (g_hwnd) {
        RECT cr;
        if (GetClientRect(g_hwnd, &cr)) {
            cw = cr.right - cr.left;
            ch = cr.bottom - cr.top;
        }
    }
    if (cw <= 0) cw = winW > 0 ? winW : 800;
    if (ch <= 0) ch = winH > 0 ? winH : 600;
    if (w) *w = cw;
    if (h) *h = ch;
}

int PlatFocused(void) {
    return (g_hwnd && GetForegroundWindow() == g_hwnd) ? 1 : 0;
}

void PlatRestoreWindow(void) {
    if (!g_hwnd) return;
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
}

int PlatMinimized(void) {
    return (g_hwnd && IsIconic(g_hwnd)) ? 1 : 0;
}

void PlatSetTitle(const char* utf8) {
    if (!g_hwnd || !utf8) return;
    SetWindowTextA(g_hwnd, utf8);
}

/* ---------------------------------------------------------------------
 * 渲染目标（DIB Section）
 * -------------------------------------------------------------------
 * 缓冲直接建在 DIB 上：GDI 走共享内存快路径，呈现时 BitBlt 而不是
 * 每次从用户内存 StretchDIBits。这一段是从 render.c 原样搬过来的。
 * ------------------------------------------------------------------- */
static HDC    s_memDC  = NULL;
static HBITMAP s_dib   = NULL;
static void*  s_dibBits = NULL;
static int    s_useDIB = 0;
static int    s_targetW = 0, s_targetH = 0;
static unsigned int* s_ownedPixels = NULL;   /* DIB 失败时退回 malloc */

unsigned int* PlatAcquireTarget(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    if (w == s_targetW && h == s_targetH) {
        if (s_useDIB) return (unsigned int*)s_dibBits;
        return s_ownedPixels;
    }

    /* 释放上一份 */
    if (s_memDC) { DeleteDC(s_memDC); s_memDC = NULL; }
    if (s_dib)   { DeleteObject(s_dib); s_dib = NULL; }
    s_dibBits = NULL;
    s_useDIB = 0;
    if (s_ownedPixels) { free(s_ownedPixels); s_ownedPixels = NULL; }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;          /* 负数 = 自上而下 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    s_memDC = CreateCompatibleDC(NULL);
    if (s_memDC) s_dib = CreateDIBSection(s_memDC, &bmi, DIB_RGB_COLORS, &s_dibBits, NULL, 0);
    if (s_memDC && s_dib && s_dibBits) {
        SelectObject(s_memDC, s_dib);
        s_useDIB = 1;
        s_targetW = w;
        s_targetH = h;
        return (unsigned int*)s_dibBits;
    }

    if (s_memDC) { DeleteDC(s_memDC); s_memDC = NULL; }
    if (s_dib)   { DeleteObject(s_dib); s_dib = NULL; }
    s_ownedPixels = (unsigned int*)malloc((size_t)w * (size_t)h * sizeof(unsigned int));
    if (!s_ownedPixels) return NULL;
    s_targetW = w;
    s_targetH = h;
    return s_ownedPixels;
}

const unsigned int* PlatTargetPtr(void) {
    return s_useDIB ? (const unsigned int*)s_dibBits : s_ownedPixels;
}

/* ---------------------------------------------------------------------
 * 呈现
 * -------------------------------------------------------------------
 * 原 ui.c 的 letterbox + 两次推送逻辑整体搬进来：
 *   铺满  -> 直接画到窗口 DC（省掉"填黑 + 离屏 + BitBlt"三步）
 *   有黑边 -> 黑底 + 缩放画面先拼在离屏，再一次性 BitBlt（不闪）
 * 缩放用 COLORONCOLOR（近邻）：点阵字保持硬边，且比 HALFTONE 快 30 倍。
 * ------------------------------------------------------------------- */
static HDC     s_bufDC  = NULL;
static HBITMAP s_bufBmp = NULL;
static int     s_bufW = -1, s_bufH = -1;

void PlatPresentFrame(const PlatPresentRect* r, const unsigned int* src, int sw, int sh) {
    if (!r || !src || !g_hwnd) return;
    if (r->cw <= 0 || r->ch <= 0) return;

    HDC hdc = GetDC(g_hwnd);
    if (!hdc) return;

    if (r->fullBleed) {
        SetStretchBltMode(hdc, COLORONCOLOR);
        if (s_useDIB && s_memDC) {
            StretchBlt(hdc, 0, 0, r->cw, r->ch, s_memDC, 0, 0, sw, sh, SRCCOPY);
        } else {
            BITMAPINFO bi;
            memset(&bi, 0, sizeof(bi));
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = sw;
            bi.bmiHeader.biHeight = -sh;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(hdc, 0, 0, r->cw, r->ch, 0, 0, sw, sh,
                          src, &bi, DIB_RGB_COLORS, SRCCOPY);
        }
    } else {
        if (!s_bufDC || s_bufW != r->cw || s_bufH != r->ch) {
            if (s_bufBmp) { DeleteObject(s_bufBmp); s_bufBmp = NULL; }
            if (s_bufDC) { DeleteDC(s_bufDC); s_bufDC = NULL; }
            s_bufDC = CreateCompatibleDC(hdc);
            if (s_bufDC) {
                s_bufBmp = CreateCompatibleBitmap(hdc, r->cw, r->ch);
                SelectObject(s_bufDC, s_bufBmp);
                s_bufW = r->cw;
                s_bufH = r->ch;
            }
        }
        if (s_bufDC) {
            RECT all = { 0, 0, r->cw, r->ch };
            HBRUSH blk = (HBRUSH)GetStockObject(BLACK_BRUSH);
            FillRect(s_bufDC, &all, blk);
            SetStretchBltMode(s_bufDC, COLORONCOLOR);
            if (s_useDIB && s_memDC) {
                StretchBlt(s_bufDC, r->dx, r->dy, r->dw, r->dh,
                           s_memDC, 0, 0, sw, sh, SRCCOPY);
            } else {
                BITMAPINFO bi;
                memset(&bi, 0, sizeof(bi));
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = sw;
                bi.bmiHeader.biHeight = -sh;
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                StretchDIBits(s_bufDC, r->dx, r->dy, r->dw, r->dh, 0, 0, sw, sh,
                              src, &bi, DIB_RGB_COLORS, SRCCOPY);
            }
            BitBlt(hdc, 0, 0, r->cw, r->ch, s_bufDC, 0, 0, SRCCOPY);
        }
    }

    ReleaseDC(g_hwnd, hdc);
}

void PlatShutdown(void) {
    if (s_bufBmp) { DeleteObject(s_bufBmp); s_bufBmp = NULL; }
    if (s_bufDC)  { DeleteDC(s_bufDC); s_bufDC = NULL; }
    if (s_dib)    { DeleteObject(s_dib); s_dib = NULL; }
    if (s_memDC)  { DeleteDC(s_memDC); s_memDC = NULL; }
    if (s_ownedPixels) { free(s_ownedPixels); s_ownedPixels = NULL; }
    s_dibBits = NULL;
    s_useDIB = 0;
    s_targetW = s_targetH = 0;
}

/* ---------------------------------------------------------------------
 * 输入（R2 接线；这里先给出正确实现）
 * ------------------------------------------------------------------- */
int PlatKeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
}

void PlatMouseDelta(int* dx, int* dy) {
    POINT p;
    static POINT last;
    static int have = 0;
    if (dx) *dx = 0;
    if (dy) *dy = 0;
    if (!GetCursorPos(&p)) return;
    if (!have) { last = p; have = 1; return; }
    if (dx) *dx = p.x - last.x;
    if (dy) *dy = p.y - last.y;
    last = p;
}

/* ShowCursor 是引用计数的，跨状态切换很容易把引用数弄歪；
   这里用 GetCursorInfo 把光标状态直接拧到目标值（原有逻辑）。 */
void PlatShowCursor(int visible) {
    CURSORINFO ci;
    for (int i = 0; i < 64; i++) {
        ci.cbSize = sizeof(ci);
        if (!GetCursorInfo(&ci)) break;
        int showing = (ci.flags & CURSOR_SHOWING) ? 1 : 0;
        if (showing == (visible ? 1 : 0)) break;
        ShowCursor(visible ? TRUE : FALSE);
    }
}

void PlatSetCursorLock(int on) {
    if (on) {
        if (g_hwnd) {
            RECT r;
            GetClientRect(g_hwnd, &r);
            POINT c;
            c.x = r.right / 2;
            c.y = r.bottom / 2;
            ClientToScreen(g_hwnd, &c);
            SetCursorPos(c.x, c.y);
        }
        ShowCursor(FALSE);
    } else {
        ShowCursor(TRUE);
    }
}

int PlatCursorLocked(void) { return 0; }   /* Win32 后端不跟踪；占位给网页端用 */

void PlatCursorPos(int* x, int* y) {
    POINT p;
    if (GetCursorPos(&p)) {
        if (g_hwnd) ScreenToClient(g_hwnd, &p);
        if (x) *x = p.x;
        if (y) *y = p.y;
    } else {
        if (x) *x = 0;
        if (y) *y = 0;
    }
}

void PlatRequestQuit(void) { PostQuitMessage(0); }

void PlatSetCapture(int on) {
    if (!g_hwnd) return;
    if (on) SetCapture(g_hwnd);
    else ReleaseCapture();
}

void PlatSetCursorPos(int x, int y) {
    POINT p;
    p.x = x;
    p.y = y;
    if (g_hwnd) ClientToScreen(g_hwnd, &p);
    SetCursorPos(p.x, p.y);
}

/* ---------------------------------------------------------------------
 * 文件（R3 接线；此处按 exe 目录实现，与现有行为一致）
 * ------------------------------------------------------------------- */
int PlatExeDir(char* out, int cap) {
    if (!out || cap <= 0) return 0;
    GetModuleFileNameA(NULL, out, (DWORD)cap);
    char* sl = strrchr(out, '\\');
    if (sl) sl[1] = 0; else out[0] = 0;
    return (int)strlen(out);
}

static void PlatJoin(char* out, int cap, const char* name) {
    char dir[MAX_PATH];
    PlatExeDir(dir, (int)sizeof(dir));
    snprintf(out, (size_t)cap, "%s%s", dir, name);
}

int PlatReadFileBytes(const char* name, void* buf, int cap) {
    if (!name || !buf || cap <= 0) return -1;
    char path[MAX_PATH];
    PlatJoin(path, (int)sizeof(path), name);
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return (int)n;
}

int PlatWriteFileBytes(const char* name, const void* buf, int len) {
    if (!name || (!buf && len > 0)) return -1;
    char path[MAX_PATH];
    PlatJoin(path, (int)sizeof(path), name);
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(buf, 1, (size_t)len, f);
    fclose(f);
    return (n == (size_t)len) ? 0 : -1;
}

/* ---------------------------------------------------------------------
 * 路径级文件原语（R3）
 * -------------------------------------------------------------------
 * 原子写仍是原来那一套：.tmp -> FlushFileBuffers -> 原子改名。
 * 这一段从 src/save/save_backend.c 原样搬过来 —— 存档后端从此只认平台层。
 * ------------------------------------------------------------------- */
int PlatReadFilePath(const char* path, void* buf, int cap) {
    if (!path || !path[0] || !buf || cap <= 0) return -1;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, (DWORD)cap, &got, NULL);
    CloseHandle(h);
    return ok ? (long)got : -1;
}

int PlatWriteFileAtomic(const char* path, const void* data, int len) {
    if (!path || !path[0] || (!data && len > 0)) return 87;   /* ERROR_INVALID_PARAMETER */
    char tmp[MAX_PATH + 16];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return (int)GetLastError();

    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, (DWORD)len, &wrote, NULL);
    DWORD err = ok ? 0 : GetLastError();
    if (ok && wrote != (DWORD)len) err = ERROR_WRITE_FAULT;

    /* FlushFileBuffers 失败 = 数据可能没落盘，等同于写失败，必须报出去。
       后续失败不能覆盖**第一个**错误码 —— 第一个才是根因。 */
    if (!err && !FlushFileBuffers(h)) err = GetLastError();
    if (!CloseHandle(h) && !err) err = GetLastError();

    if (err) { DeleteFileA(tmp); return (int)err; }
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        err = GetLastError();
        DeleteFileA(tmp);
        return (int)err;
    }
    return 0;
}

void PlatMakeDir(const char* path) {
    if (path && path[0]) CreateDirectoryA(path, NULL);
}

int PlatDeletePath(const char* path) {
    if (!path || !path[0]) return -1;
    return DeleteFileA(path) ? 0 : -1;
}

int PlatSystemLanguageZh(void) {
    return (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE) ? 1 : 0;
}

double PlatFileTime(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!path || !GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) return 0.0;
    ULARGE_INTEGER u;
    u.LowPart = fa.ftLastWriteTime.dwLowDateTime;
    u.HighPart = fa.ftLastWriteTime.dwHighDateTime;
    return (double)u.QuadPart / 10000000.0;   /* 100ns -> 秒 */
}

int PlatCopyPath(const char* from, const char* to) {
    if (!from || !to) return -1;
    return CopyFileA(from, to, FALSE) ? 0 : -1;
}

int PlatRenamePath(const char* from, const char* to) {
    if (!from || !to) return -1;
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

/* ---------------------------------------------------------------------
 * 音频（R3 接线）
 * ------------------------------------------------------------------- */
void PlatLog(const char* msg) {
    if (msg) OutputDebugStringA(msg);
}

void PlatSfx(int id, float gain) {
    (void)id;
    (void)gain;
}

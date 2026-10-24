/*
 * Canity — 纯 C + Win32 GDI 软件光栅化恐怖生存小引擎
 *
 * 架构要点：
 *   - 逻辑与渲染分离：固定 120Hz 逻辑步进（dt 驱动），渲染受帧数上限控制。
 *   - 内部分辨率渲染（Render Scale）：3D 与后处理都跑在 renderW×renderH，
 *     最后 StretchDIBits 放大到窗口；HUD / 菜单文字仍用 GDI 按窗口分辨率绘制。
 *   - 本文件只剩程序入口 WinMain 与主循环。
 *
 * 模块划分见 common.h 顶部注释。
 */
#include "common.h"
#include "entity.h"
#include "game.h"
#include "render.h"
#include "ui.h"
#include "audio.h"
#include "save.h"


/* ------------------------------------------------------------------ */
/* 入口：固定步长逻辑 + 可限帧渲染                                      */
/* ------------------------------------------------------------------ */

/* 正常构建时这里就是标准的程序入口。
   离线自检程序会用 `#define WinMain CanityAppMain` 把本函数改名，
   这样它就能 include 整个 canity_unity.c 并自己定义 main() —— 但那不影响本文件的独立性，
   单独编译 main.c 依然得到完整的游戏。 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    SetProcessDPIAware();
    srand((unsigned int)time(NULL));
    timeBeginPeriod(1);

    hasSave = LoadSettings();
    InitAudio();

    InitPixels();
    GenerateThemeTextures(0);   /* 启动用 BRICK 主题 */
    InitWorkers();

    const char CLASS_NAME[] = "CanityEngine";
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(0, CLASS_NAME, "Canity v" CANITY_VERSION_STR,
                            WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, 816, 639,
                            NULL, NULL, hInstance, NULL);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, nCmdShow);
    ForceCursor(1);
    ApplyDisplayMode();

    LARGE_INTEGER freq, now, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    double accum = 0.0;       /* 逻辑累加器 */
    double clock = 0.0;       /* 呈现排程时钟（秒，绝对；睡眠处会刷新） */
    double clockPrev = 0.0;   /* 上一次循环顶部的采样，仅用于算 dt */
    double nextRender = 0.0;  /* 下一帧的呈现时刻 */
    double fpsAcc = 0.0;
    int fpsFrames = 0;

    int running = 1;
    MSG msg;
    while (running) {
        int guard = 0;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (++guard > 256) break;   /* 防止任何消息风暴把渲染饿死 */
        }
        if (!running) break;

        QueryPerformanceCounter(&now);
        double absNow = (double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
        /* dt 必须基于「上一次循环顶部」的采样：睡眠/自旋里刷新的 clock 不能参与，
           否则一帧的真实时长会被算少，逻辑就变慢动作 */
        double dt = absNow - clockPrev;
        clockPrev = absNow;
        clock = absNow;
        g_timeSec = absNow;
        if (dt > 0.25) dt = 0.25;
        if (dt < 0.0) dt = 0.0;

        /* gamma 变动时重建查表（只在改动的那一帧做） */
        if (gammaValue != gammaBuilt) { BuildShadeTables(); gammaBuilt = gammaValue; }

        /* --- 逻辑：固定 120Hz 步进，与渲染帧率完全无关 --- */
        accum += dt;
        int steps = 0;
        while (accum >= LOGIC_STEP && steps < 16) {
            Update((float)LOGIC_STEP);
            accum -= LOGIC_STEP;
            steps++;
        }
        if (steps >= 16) accum = 0.0;
        /* 一批逻辑更新跑完后再清按键边沿。
           必须在批处理**之后**清 —— 一帧可能跑多次 Update，
           在 Update 里清会把按键吞掉。 */
        ClearKeyEdges();

        /* --- 渲染：按绝对时刻排程，帧数上限不会被渲染耗时吃掉 --- */
        int cap = fpsCaps[fpsCapIndex];
        double interval = (cap > 0) ? 1.0 / (double)cap : 0.0;

        int playing = (currentState == STATE_PLAYING);
        int doRender = playing ? (clock >= nextRender) : (uiDirty != 0);

        if (doRender) {
            /* 最小化时只清脏标记，避免 uiDirty 常挂导致忙等空转 */
            if (!IsIconic(g_hwnd)) {
                LARGE_INTEGER t0, t1;
                QueryPerformanceCounter(&t0);
                RenderFrame();
                QueryPerformanceCounter(&t1);
                lastRenderMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
                if (lastRenderMs > frameMsMax) frameMsMax = lastRenderMs;
                fpsFrames++;
            }
            uiDirty = 0;
            if (playing) {
                /* 从“上次计划的时刻”前进一个间隔，渲染耗时不叠加到周期里；
                   落后太多就重新对齐，避免追帧 */
                nextRender += interval;
                if (nextRender < clock) nextRender = clock + interval;
            }
        }

        fpsAcc += dt;
        if (fpsAcc >= 0.5) {
            currentFps = (int)(fpsFrames / fpsAcc + 0.5);
            fpsFrames = 0;
            fpsAcc = 0.0;
            frameMsMax = lastRenderMs;
        }

        /* --- 让出 CPU：先睡大头，最后 3ms 自旋，避免 Sleep 粒度吃掉帧预算 --- */
        if (playing) {
            if (interval > 0.0) {
                double remain = nextRender - clock;
                if (remain > 0.007) {
                    Sleep((DWORD)((remain - 0.004) * 1000.0));
                    QueryPerformanceCounter(&now);
                    clock = (double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
                }
                while (clock < nextRender) {
                    QueryPerformanceCounter(&now);
                    clock = (double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
                }
            } else {
                Sleep(0); /* 不限制帧数时也要让出时间片 */
            }
        } else {
            Sleep(uiDirty ? 0 : 4);
        }
    }

    if (currentState == STATE_PLAYING || currentState == STATE_PAUSED) SaveGame();
    if (g_memDC) DeleteDC(g_memDC);
    if (g_dib) DeleteObject(g_dib);
    if (!g_useDIB) free(pixels);
    free(vignetteMask);
    ShutdownAudio();
    ShutdownWorkers();
    timeEndPeriod(1);
    return 0;
}

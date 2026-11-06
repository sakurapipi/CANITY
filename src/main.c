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
#include "skills.h"
#include "entity.h"
#include "game.h"
#include "plat.h"
#include "sprites.h"
#include "render.h"
#include "ui.h"
#include "audio.h"
#include "save.h"
#include "steam_zone.h"
#include "music.h"
#include "shop.h"

/* ==================================================================
 * 2026-11-06（R4）：**桌面入口是 Win32 专属**
 * ------------------------------------------------------------------
 * 网页端的 main() 在 src/platform/plat_web.c —— 它要做的事一样，
 * 但没有 CreateWindowEx / 消息循环，改成 emscripten_set_main_loop。
 * ================================================================ */
#ifndef CANITY_WEB
/* ------------------------------------------------------------------ */
/* 入口：固定步长逻辑 + 可限帧渲染                                      */
/* ------------------------------------------------------------------ */

/* 正常构建时这里就是标准的程序入口。
   离线自检程序会用 `#define WinMain CanityAppMain` 把本函数改名，
   这样它就能 include 整个 canity_unity.c 并自己定义 main() —— 但那不影响本文件的独立性，
   单独编译 main.c 依然得到完整的游戏。 */
static int FileExists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* ===== 崩溃定位器 =====
   崩溃时把「异常地址 - 模块基址」写进 canity_crash.txt。
   canity.exe 没有 strip，拿到 RVA 就能用 nm 反查到函数名。 */
volatile int g_dbgMark = 0;   /* 崩溃现场标记：最后执行到哪一步 */
LONG WINAPI CanityCrashHandler(EXCEPTION_POINTERS* ep) {
    /* v6 第 ㊽ 轮：**崩溃日志必须落在 exe 旁边**
       原来写的是当前工作目录 —— 从别处启动就找不到，
       "没有日志"会被误判成"没崩溃"。和遥测同一个坑。 */
    char cpath[MAX_PATH + 32];
    {
        char exe[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
        char* slash = (n > 0 && n < sizeof(exe)) ? strrchr(exe, '\\') : NULL;
        if (slash) slash[1] = '\0'; else exe[0] = '\0';
        snprintf(cpath, sizeof(cpath), "%scanity_crash.txt", exe);
    }
    FILE* f = fopen(cpath, "a");
    if (f) {
        unsigned long base = (unsigned long)(size_t)GetModuleHandleA(NULL);
        unsigned long addr = 0, code = 0;
        if (ep && ep->ExceptionRecord) {
            addr = (unsigned long)(size_t)ep->ExceptionRecord->ExceptionAddress;
            code = (unsigned long)ep->ExceptionRecord->ExceptionCode;
        }
        fprintf(f, "code=0x%08lX base=0x%08lX addr=0x%08lX rva=0x%08lX mark=%d\n",
                code, base, addr, addr - base, g_dbgMark);
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetUnhandledExceptionFilter(CanityCrashHandler);

    /* v1.8：声明 DPI 感知。
       不声明的话，125%/150% 缩放的笔记本上 Windows 会把窗口"虚拟放大"，
       800x600 的窗口占掉更多像素，画面糊、还可能放不进屏幕。 */
    SetProcessDPIAware();

    (void)hPrevInstance;
    (void)lpCmdLine;

    SetProcessDPIAware();
    srand((unsigned int)time(NULL));
    timeBeginPeriod(1);

    SteamZoneInit();
    /* 2026-11-05：云->本地的回写搬到存档分区了。
       顺序很重要：必须在 LoadSettings() **之前**跑完，
       否则读到的还是旧档、云端赢的那份要等下次启动才生效。 */
    { extern void SaveBackendSyncAll(void); SaveBackendSyncAll(); }
    InitAchievements();
    hasSave = LoadSettings();

    /* ==================================================================
     * v6 第 ㉕ 轮：**UI 调试入口**（自检用，不影响正常游玩）
     * ==================================================================
     * 为什么要有这个
     *   「配色 / 布局好不好看」这件事，我一直说"需要你看"。
     *   但其实我可以自己截图 —— 只要能让程序**直接开到那一页**。
     *
     *   设了 CANITY_UI 环境变量就直接进那一页：
     *       set CANITY_UI=skills      技能树
     *       set CANITY_UI=ach         成就墙
     *       set CANITY_UI=bestiary    图鉴
     *       set CANITY_UI=bank        银行
     *   没设就正常启动。
     *
     *   这只是**读一个环境变量**，不影响存档、不影响正常玩家。
     * ================================================================ */
    {
        const char* ui = getenv("CANITY_UI");
        if (ui && ui[0]) {
            extern GameState currentState;
            SkillInitOnce();
            if (strcmp(ui, "skills") == 0 || strcmp(ui, "hub") == 0)
                currentState = STATE_HUB;
            else if (strcmp(ui, "ach") == 0)
                currentState = STATE_ACHIEVEMENTS;
            else if (strcmp(ui, "bestiary") == 0)
                currentState = STATE_BESTIARY;
            else if (strcmp(ui, "bank") == 0)
                currentState = STATE_BANK;
            /* v6 第 ㉚ 轮：**shop / bench 的调试入口已删除**
               它们要先进一局（StartGame → GenerateLevel），
               而这段代码跑在 CreateWindowEx **之前** ——
               实测直接进程无窗口（连开 6 个都没有 MainWindowTitle）。
               要看商店就在游戏里按 T，别在这里造第二套启动路径。 */
        }
    }

    InitAchievements();          /* 只填静态字段，不覆盖存档里的进度 */
    InitAudio();
    MusicInit();
    MusicLoadGameTracks();

    InitPixels();
    SpritesInit();   /* v2.1：烘焙精灵（一次性，约 40~80ms） */
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
                            WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,   /* v1.8：保留最大化按钮（原来被 ~WS_MAXIMIZEBOX 禁掉） */
                            CW_USEDEFAULT, CW_USEDEFAULT, 816, 639,
                            NULL, NULL, hInstance, NULL);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, nCmdShow);
    /* ==================================================================
     * 修：**启动后要手动点一下窗口键盘才生效**
     * ==================================================================
     * `ShowWindow` 只负责显示，不保证拿到**前台焦点**。
     * 从别的程序（终端 / 启动器 / 桌面快捷方式）拉起来时，
     * 窗口会开在后面，键盘消息全落在原来的前台窗口上 ——
     * 玩家看到画面了，但按什么键都没反应，得先用鼠标点一下。
     *
     * 三步把它拉到前台：
     *   ① 提到 Z 序最上（不移动、不改大小）
     *   ② 请求前台
     *   ③ 显式把键盘焦点给客户区
     * ②有可能被系统拒绝（前台锁），后面 ForceCursor + 首帧的
     * SetFocus 兜底，至少鼠标点一下之后不会再丢。
     * ================================================================== */
    SetWindowPos(g_hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
    ForceCursor(1);
    ApplyDisplayMode();
    { const char* preview=getenv("CANITY_UI");
      if(preview && (!strcmp(preview,"shop") || !strcmp(preview,"tree") || !strcmp(preview,"upgrade"))) {
        StartGame();ShopOpen(currentLevel);currentState=STATE_SAFEHOUSE;
        shopPage=!strcmp(preview,"tree")?SHP_TREE:(!strcmp(preview,"upgrade")?SHP_UPGRADE:SHP_SHOP);
        ForceCursor(1);uiDirty=1;
      }
    }

    /* 2026-11-06（R1）：时钟归平台层，主循环只拿单调秒数 */

    double accum = 0.0;       /* 逻辑累加器 */
    double clock = 0.0;       /* 呈现排程时钟（秒，绝对；睡眠处会刷新） */
    double clockPrev = 0.0;   /* 上一次循环顶部的采样，仅用于算 dt */
    double nextRender = 0.0;  /* 下一帧的呈现时刻 */
    double fpsAcc = 0.0;
    int fpsFrames = 0;

    int running = 1;
    MSG msg;

    while (running) {

        SyncWindowSize();   /* v2.2：每帧对齐 winW/winH（DPI/最大化/改窗口都能兜住） */
        int guard = 0;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (++guard > 256) break;   /* 防止任何消息风暴把渲染饿死 */
        }
        if (!running) break;

        double absNow = PlatNow();
        /* dt 必须基于「上一次循环顶部」的采样：睡眠/自旋里刷新的 clock 不能参与，
           否则一帧的真实时长会被算少，逻辑就变慢动作 */
        double dt = absNow - clockPrev;
        clockPrev = absNow;
        clock = absNow;
        g_timeSec = absNow;
        SteamZoneTick((float)dt);
        SteamZoneMusicDuckTick((float)dt);
        MusicScene scene=MUSIC_MENU;
        if(currentState==STATE_GAMEOVER) scene=gameWon?MUSIC_VICTORY:MUSIC_DEFEAT;
        else if(currentState==STATE_SAFEHOUSE) scene=MUSIC_SAFE;
        else if(currentState==STATE_PLAYING || currentState==STATE_PAUSED || currentState==STATE_SKILL_PICK) {
            scene=(monster.active && monster.state==2) || (lightBeast.active && lightBeast.state==2) ? MUSIC_CHASE : MUSIC_EXPLORE;
        }
        MusicSetScene(scene);
        MusicSetMasterVolume(musicVolumeF);
        MusicSetPaused(currentState==STATE_PAUSED || (g_hwnd && IsIconic(g_hwnd)));
        MusicTick((float)dt);
        if (dt > 0.25) dt = 0.25;
        if (dt < 0.0) dt = 0.0;

        /* gamma 变动时重建查表（只在改动的那一帧做） */
        if (gammaValue != gammaBuilt) { BuildShadeTables(); gammaBuilt = gammaValue; }

        /* --- 逻辑：固定 120Hz 步进，与渲染帧率完全无关 --- */
        accum += dt;
        int steps = 0;
        while (accum >= LOGIC_STEP && steps < 16) {
            PollInput();          /* v1.8：每帧轮询键盘，不依赖焦点消息 */
            UpdatePitch();        /* v1.13：真俯仰（每帧刷新 cos/sin/focal） */
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
                /* 2026-11-06（R1）：渲染耗时也走平台时钟 */
                double rt0 = PlatNow();
                RenderFrame();
                lastRenderMs = (PlatNow() - rt0) * 1000.0;
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

        /* v1.9：UI 布局自检 —— exe 同目录放一个 dump_ui.flag，
           游戏就会每 30 帧把当前帧的文字矩形写进 ui_layout.txt。
           这样不依赖键盘，脚本也能触发。 */
        {
            static int uiCheckFrames = 0;
            static int uiFlagChecked = 0;
            if (!uiFlagChecked) {
                char probe[MAX_PATH];
                GetModuleFileNameA(NULL, probe, MAX_PATH);
                char* sl = strrchr(probe, '\\');
                if (sl) sl[1] = 0; else probe[0] = 0;
                strncat(probe, "dump_ui.flag", MAX_PATH - strlen(probe) - 1);
                uiFlagChecked = FileExists(probe) ? 1 : -1;
            }
            if (uiFlagChecked > 0 && (++uiCheckFrames % 30) == 0) {
                DumpUILayout("ui_layout.txt");
            }
        }

        /* v1.9：把实测渲染耗时交给动态分辨率 */
        if (playing) UpdateDynamicRes(lastRenderMs);

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
                    /* 2026-11-06（R1）：把阻塞睡眠交给平台层。
                       网页后端里这是"让出一帧"，不能真睡。 */
                    PlatSleepMs((int)((remain - 0.004) * 1000.0));
                    clock = PlatNow();
                }
                while (clock < nextRender) {
                    clock = PlatNow();
                }
            } else {
                Sleep(0); /* 不限制帧数时也要让出时间片 */
            }
        } else {
            Sleep(uiDirty ? 0 : 4);
        }
    }

    if (currentState == STATE_PLAYING || currentState == STATE_PAUSED) SaveGame();
    PlatShutdown();
    free(vignetteMask);
    MusicShutdown();
    SteamZoneStoreStats();
    SteamZoneShutdown();
    ShutdownAudio();
    ShutdownWorkers();
    timeEndPeriod(1);
    return 0;
}

#endif /* CANITY_WEB */

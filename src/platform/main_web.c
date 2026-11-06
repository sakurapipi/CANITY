/*
 * Canity — 网页版入口（Emscripten）
 * ---------------------------------------------------------------------
 * 对应桌面端的 src/main.c（WinMain）：初始化顺序一样，只是没有建窗与
 * 消息循环，改成 emscripten_set_main_loop。
 *
 * 分区规则：入口文件属于"平台入口"这一格 ——
 *   src/main.c              Win32 入口（WinMain + 消息循环）
 *   src/platform/main_web.c 网页入口（main + rAF 主循环）
 * 两个入口都只调用游戏层公开的东西，不自己实现任何玩法。
 */
#include "common.h"
#include "plat.h"
#include "game.h"
#include "render.h"
#include "ui.h"
#include "save.h"
#include "audio.h"

/* ---------------------------------------------------------------------
 * 网页入口
 * -------------------------------------------------------------------
 * 对应 WinMain 的初始化顺序，去掉建窗/消息循环部分。
 * ------------------------------------------------------------------- */
#define WEB_LOGIC_STEP (1.0 / 120.0)

static int g_selftest = 0;
static double s_accum = 0.0;
static double s_prev = 0.0;
static int    s_booted = 0;

static void WebFrame(void) {
    double now = PlatNow();
    double dt = now - s_prev;
    s_prev = now;
    if (dt > 0.25) dt = 0.25;
    if (dt < 0.0) dt = 0.0;

    /* 窗口逻辑尺寸跟着 canvas 走（对应桌面版的 SyncWindowSize） */
    {
        int cw = 0, ch = 0;
        PlatWindowSize(&cw, &ch);
        if (cw > 0 && ch > 0) { winW = cw; winH = ch; }
    }

    extern void PollInput(void);
    extern void UpdatePitch(void);
    extern void ClearKeyEdges(void);
    extern void Update(float dt);

    /* 2026-11-06（R4）：**鼠标视角**。
       桌面版这段长在 WM_MOUSEMOVE 里；网页端没有窗口消息，
       所以在主循环里取一次位移，交给同一个 CanityMouseLook。 */
    {
        extern void CanityMouseLook(int dx, int dy);
        int mdx = 0, mdy = 0;
        PlatMouseDelta(&mdx, &mdy);
        if ((mdx || mdy) && PlatCursorLocked()) CanityMouseLook(mdx, mdy);
    }

    s_accum += dt;
    int steps = 0;
    while (s_accum >= WEB_LOGIC_STEP && steps < 16) {
        PollInput();
        UpdatePitch();
        Update((float)WEB_LOGIC_STEP);
        s_accum -= WEB_LOGIC_STEP;
        steps++;
    }
    if (steps >= 16) s_accum = 0.0;
    ClearKeyEdges();

    RenderFrame();

    {
        static int s_first = 0;
        if (!s_first) { s_first = 1; PlatMarkReady(); }
    }
}

int main(void) {
    PlatInit();

    srand((unsigned int)time(NULL));

    extern void InitAchievements(void);
    extern void MusicInit(void);
    extern void MusicLoadGameTracks(void);
    extern void InitPixels(void);
    extern void SpritesInit(void);
    extern void GenerateThemeTextures(int theme);
    extern void InitWorkers(void);
    extern int  LoadSettings(void);
    extern int  hasSave;

    InitAchievements();
    hasSave = LoadSettings();
    InitAudio();
    MusicInit();
    MusicLoadGameTracks();

    /* 先把逻辑窗口尺寸设成 canvas 尺寸，InitPixels 才知道宽高比 */
    {
        int cw = 0, ch = 0;
        PlatWindowSize(&cw, &ch);
        if (cw > 0 && ch > 0) { winW = cw; winH = ch; }
    }

    /* 2026-11-06：**分步计时** —— 网页版加载慢必须先知道慢在哪。
       ?perf=1 时把各阶段耗时写进页面标题，截图就能读。 */
    double tPix = 0.0, tSpr = 0.0, tTex = 0.0;
    {
        double t0 = PlatNow();
        InitPixels();
        tPix = PlatNow() - t0;

        t0 = PlatNow();
        SpritesInit();
        tSpr = PlatNow() - t0;

        t0 = PlatNow();
        GenerateThemeTextures(0);
        tTex = PlatNow() - t0;
    }
    InitWorkers();
    if (PlatShowPerf(tPix * 1000.0, tSpr * 1000.0, tTex * 1000.0,
                     (tPix + tSpr + tTex) * 1000.0)) {
        g_selftest = 1;   /* 复用"别收遮罩"这条：perf 模式下要让数字可见 */
    }

    s_prev = PlatNow();
    s_booted = 1;
    (void)s_booted;

    /* ==================================================================
     * 存档自检（?selftest=1）
     * ------------------------------------------------------------------
     * 网页端存档落在 localStorage。无头浏览器没法点菜单进游戏，
     * 所以给一条可自动化的验证路径：写一份、读回来、比对，
     * 结果写进 document.title —— Chrome --dump-dom 能直接读到。
     * ================================================================ */
    if (PlatSelfTestRequested()) {
        const char* probe = "canity-selftest-payload";
        char back[64];
        int wr = PlatWriteFileBytes("selftest.bin", probe, (int)strlen(probe));
        int rd = PlatReadFileBytes("selftest.bin", back, (int)sizeof(back) - 1);
        const char* verdict = "savetest-fail";
        if (back[0] == 0) back[0] = 0;
        if (wr == 0 && rd == (int)strlen(probe)) {
            back[rd] = 0;
            if (strcmp(back, probe) == 0) verdict = "savetest-ok";
        }
        /* 结论同时写进 title 和加载遮罩里的那行字：
           无头浏览器截一张图就能读到，不需要 DevTools。 */
        {
            /* 三个位置都写：title（--dump-dom 可读）、status 行、大标题。
               无头截图只要能看见其中任意一个就算通过。 */
            char js[320];
            snprintf(js, sizeof(js),
                     "try{document.title='%s';"
                     "var s=document.getElementById('status');if(s)s.textContent='%s';"
                     "var h=document.querySelector('#boot h1');if(h)h.textContent='%s';"
                     "var sub=document.querySelector('#boot .sub');if(sub)sub.textContent='%s';"
                     "}catch(e){console.error(e);}", verdict, verdict, verdict, verdict);
            emscripten_run_script(js);
        }
        g_selftest = 1;
    }

    /* 2026-11-06（R4）：**从 C 里收掉加载遮罩**。
       原来靠 JS 的 Module.onRuntimeInitialized 隐藏，实测那个回调
       在本配置下不触发，遮罩就一直压在游戏上（截图确认过大字 CANITY）。
       从 C 里显式收掉是确定的：走到这里说明初始化和首帧都准备好了。 */
    if (!g_selftest)
    emscripten_run_script(
        "var b=document.getElementById('boot');"
        "if(b){b.classList.add('hidden');setTimeout(function(){b.style.display='none';},400);}"
        "if(Module.canvas)Module.canvas.focus();");

    emscripten_set_main_loop(WebFrame, 0, 1);
    return 0;
}

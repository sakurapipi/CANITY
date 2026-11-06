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
    { extern double g_probeDt; g_probeDt = dt; }

    /* ==================================================================
     * 2026-11-06（用户实测：**UI 错位**）
     * ------------------------------------------------------------------
     * 桌面循环每帧调 SyncWindowSize()，而它末尾有一条关键约定：
     *     if (winW != renderW || winH != renderH) { winW = renderW; ... }
     * 也就是 **UI 空间 == 渲染空间**。
     *
     * 我原来在网页循环里把 winW/winH 直接设成 canvas 的 CSS 尺寸
     * （1708x960），而 renderW/renderH 只有 ~640x360 ——
     * DrawTextWin 会做 x*renderW/winW 的换算，两边比例一错，
     * 整个菜单就整体偏移、溢出面板（用户截图里的样子）。
     *
     * 正确做法：调同一个 SyncWindowSize()，别自己维护一套。
     * ================================================================ */
    {
        extern void SyncWindowSize(void);
        SyncWindowSize();
    }

    /* 时间基准：桌面端在 WinMain 里维护 g_timeSec，网页端也得给。 */
    {
        extern double g_timeSec;
        g_timeSec = now;
    }

    extern void CanityProbeSampleKeys(void);
    CanityProbeSampleKeys();

    extern void PollInput(void);
    extern void UpdatePitch(void);
    extern void ClearKeyEdges(void);
    extern void Update(float dt);

    /* ==================================================================
     * 2026-11-06（用户实测：**局内鼠标左右滑没反应**）
     * ------------------------------------------------------------------
     * 桌面端的视角旋转只在"光标被锁在窗口中心"时才生效。
     * 网页端原来根本没人去要 Pointer Lock —— Module.requestLock 一直是
     * 假，_locked 永远 false，于是 CanityMouseLook 一次都没被调用。
     *
     * 现在：进入局内就请求锁定（浏览器要求用户手势，所以真正的
     * requestPointerLock 由 canvas 的 click 处理器触发），
     * 离开局内就释放。
     * ================================================================ */
    {
        static int s_lockWanted = -1;   /* currentState 在 game.h 里已有正确类型 */
        int want = (currentState == STATE_PLAYING) ? 1 : 0;
        if (want != s_lockWanted) {
            PlatSetCursorLock(want);
            s_lockWanted = want;
        }
    }
    {
        extern void CanityMouseLook(int dx, int dy);
        int mdx = 0, mdy = 0;
        PlatMouseDelta(&mdx, &mdy);
        /* 局内就转，不要求 Pointer Lock 已生效（见上面那段注释） */
        if ((mdx || mdy) && currentState == STATE_PLAYING) {
            CanityMouseLook(mdx, mdy);
        }
    }

    /* ==================================================================
     * 2026-11-06（用户实测：**按键全失效**）
     * ------------------------------------------------------------------
     * 桌面端的按键有两个入口：PollInput（轮询，喂 keys[]/keyPressed[]）
     * 和 WindowProc 的 WM_KEYDOWN（菜单导航/确认/取消/快捷键全在那儿）。
     * 网页端只有前者 —— 所以走动可能正常，但菜单一点反应都没有。
     *
     * 现在补上第二条：这里做 0->1 边沿检测，交给同一个 CanityKeyDown。
     * ================================================================ */
    /* 鼠标：菜单悬停 + 点击（渲染空间坐标，已由平台层反算） */
    {
        extern void CanityMouseMove(int mx, int my);
        extern void CanityMouseClick(int mx, int my);
        int mx = 0, my = 0;
        if (PlatTakeMouseClick(&mx, &my)) CanityMouseClick(mx, my);
        else if (PlatTakeMouseMove(&mx, &my)) CanityMouseMove(mx, my);
    }

    {
        static int prevKey[256];
        for (int vk = 1; vk < 256; vk++) {
            int down = PlatKeyDown(vk);
            if (down && !prevKey[vk]) CanityKeyDown(vk, 0);
            prevKey[vk] = down;
        }
    }

    s_accum += dt;
    int steps = 0;
    while (s_accum >= WEB_LOGIC_STEP && steps < 16) {
        PollInput();
        UpdatePitch();
        Update((float)WEB_LOGIC_STEP);
        { extern int g_probeUpdates; g_probeUpdates++; }
        s_accum -= WEB_LOGIC_STEP;
        steps++;
    }
    if (steps >= 16) s_accum = 0.0;
    ClearKeyEdges();

    /* 渲染前再对齐一次：Update 里可能刚调用过 ApplyDisplayMode 之类
       会碰窗口尺寸的路径（按 F11、恢复默认设置、切换全屏）。
       这一行保证"画出来的那一帧"UI 空间和渲染空间一定相等 ——
       用户报的"退出对局后 UI 又错乱"就是从这类路径漏进来的。 */
    {
        extern void SyncWindowSize(void);
        SyncWindowSize();
    }

    RenderFrame();

    {
        static int s_first = 0;
        if (!s_first) { s_first = 1; PlatMarkReady(); }
    }
}

/* 只给探针用：统计当前按下的虚拟键数量 */
int g_probeKeysDown = 0;
int g_probeUpdates = 0;     /* Update() 被调用的累计次数 */
double g_probeDt = 0.0;     /* 上一帧的 dt（秒） */
void CanityProbeSampleKeys(void) {
    int n = 0;
    for (int vk = 1; vk < 256; vk++) if (PlatKeyDown(vk)) n++;
    g_probeKeysDown = n;
}

/* ==================================================================
 * 调试探针（2026-11-06）
 * ------------------------------------------------------------------
 * 网页端出问题，控制台和截图都只能看个大概。这两个函数让自动化脚本
 * 直接读到游戏内部状态，判断"按键到底有没有进到游戏里"。
 * 只读、无副作用，正式构建里留着也无害（不画、不写）。
 * ================================================================ */
EMSCRIPTEN_KEEPALIVE int CanityProbeState(void) {
    return (int)currentState;      /* currentState 在 game.h 里已有正确类型 */
}

EMSCRIPTEN_KEEPALIVE int CanityProbeMenu(void) {
    extern int menuSelection;
    return menuSelection;
}

/* 游戏内部真正在用的按键数组（不是 JS 那份原始状态） */
EMSCRIPTEN_KEEPALIVE int CanityProbeGameKey(int vk) {
    extern int keys[];
    extern int keyPressed[];
    if (vk < 0 || vk > 255) return -1;
    return (keys[vk] ? 1 : 0) | (keyPressed[vk] ? 2 : 0);
}

/* UI 布局的关键量：winW/winH 必须等于 renderW/renderH */
EMSCRIPTEN_KEEPALIVE int CanityProbeLayout(int which) {
    extern int winW, winH, renderW, renderH;
    switch (which) {
    case 0: return winW;
    case 1: return winH;
    case 2: return renderW;
    case 3: return renderH;
    default: return 0;
    }
}

/* 朝向探针：验证鼠标视角是否真的改了方向 */
EMSCRIPTEN_KEEPALIVE int CanityProbeDir(int which) {
    extern float dirX, dirY;
    if (which == 0) return (int)(dirX * 1000.0f);
    if (which == 1) return (int)(dirY * 1000.0f);
    return 0;
}

EMSCRIPTEN_KEEPALIVE int CanityProbeUpdates(void) {
    extern int g_probeUpdates;
    return g_probeUpdates;
}

EMSCRIPTEN_KEEPALIVE double CanityProbeDt(void) {
    extern double g_probeDt;
    return g_probeDt;
}

EMSCRIPTEN_KEEPALIVE int CanityProbeKeys(void) {
    /* 当前有多少个键被按下（含绑定键与菜单键） */
    extern int g_probeKeysDown;
    return g_probeKeysDown;
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

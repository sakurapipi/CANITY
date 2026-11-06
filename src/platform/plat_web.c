/* =====================================================================
 * 平台层 · 网页后端（Emscripten / WebAssembly）
 * ---------------------------------------------------------------------
 * 与 plat_win32.c 一一对应，只是把"世界"换成了浏览器：
 *   渲染目标 → 一片 malloc 缓冲（游戏直接写）
 *   呈现     → ImageData + putImageData
 *   输入     → DOM 键盘事件填一个 256 项数组 + 指针锁定的鼠标位移
 *   文件     → localStorage（只取路径的文件名部分做键）
 *   音频     → 一期空实现（R7 接 WebAudio）
 *
 * 本文件同时是网页版的**入口**：main() 初始化游戏并挂上
 * emscripten_set_main_loop。
 * ===================================================================== */
#include "common.h"
#include "plat.h"
#include "game.h"
#include "render.h"
#include "ui.h"
#include "save.h"

#include <emscripten.h>

/* ---------------------------------------------------------------------
 * JS 侧：键位数组 / 鼠标 / canvas / localStorage
 * ------------------------------------------------------------------- */
EM_JS(void, js_plat_init, (void), {
    if (typeof Module === 'undefined') return;
    Module._keys = new Uint8Array(256);
    Module._mouseDX = 0;
    Module._mouseDY = 0;
    Module._locked = false;
    Module._cw = 0;
    Module._ch = 0;

    var cv = Module.canvas || document.getElementById('canvas');
    Module.canvas = cv;
    Module.ctx2d = cv.getContext('2d');
    Module.ctx2d.imageSmoothingEnabled = false;

    if (!Module._listenersInstalled) {
        Module._listenersInstalled = true;

        function setKey(e, down) {
            var c = e.keyCode | 0;
            if (c >= 0 && c < 256) Module._keys[c] = down ? 1 : 0;
            /* 方向键/空格会滚动页面，游戏里必须吃掉 */
            if (down && (c === 32 || (c >= 37 && c <= 40) || c === 9)) e.preventDefault();
        }
        window.addEventListener('keydown', function (e) { setKey(e, true); }, false);
        window.addEventListener('keyup', function (e) { setKey(e, false); }, false);
        window.addEventListener('blur', function () {
            if (Module._keys) Module._keys.fill(0);
        }, false);

        /* 鼠标键：与 Win32 的 VK_LBUTTON(1)/VK_RBUTTON(2)/VK_MBUTTON(4) 对齐，
           这样存档里的键位绑定跨平台通用 */
        cv.addEventListener('mousedown', function (e) {
            if (e.button === 0) Module._keys[0x01] = 1;
            else if (e.button === 2) Module._keys[0x02] = 1;
            else if (e.button === 1) Module._keys[0x04] = 1;
        }, false);
        window.addEventListener('mouseup', function (e) {
            if (e.button === 0) Module._keys[0x01] = 0;
            else if (e.button === 2) Module._keys[0x02] = 0;
            else if (e.button === 1) Module._keys[0x04] = 0;
        }, false);

        cv.addEventListener('click', function () {
            if (Module.requestLock && cv.requestPointerLock) cv.requestPointerLock();
        }, false);
        document.addEventListener('pointerlockchange', function () {
            Module._locked = (document.pointerLockElement === cv);
        }, false);
        document.addEventListener('mousemove', function (e) {
            if (!Module._locked) return;
            Module._mouseDX += (e.movementX | 0);
            Module._mouseDY += (e.movementY | 0);
        }, false);

        function measure() {
            Module._cw = cv.clientWidth | 0;
            Module._ch = cv.clientHeight | 0;
        }
        window.addEventListener('resize', measure, false);
        measure();
        Module._measure = measure;
    }
});

EM_JS(void, js_get_size, (int* w, int* h), {
    if (Module._measure) Module._measure();
    HEAP32[w >> 2] = Module._cw || 800;
    HEAP32[h >> 2] = Module._ch || 600;
});

EM_JS(int, js_has_selftest, (void), {
    return (location.search || '').indexOf('selftest') >= 0 ? 1 : 0;
});

EM_JS(int, js_key_down, (int vk), {
    return (vk >= 0 && vk < 256 && Module._keys) ? Module._keys[vk] : 0;
});

EM_JS(void, js_take_mouse, (int* dx, int* dy), {
    HEAP32[dx >> 2] = Module._mouseDX | 0;
    HEAP32[dy >> 2] = Module._mouseDY | 0;
    Module._mouseDX = 0;
    Module._mouseDY = 0;
});

EM_JS(void, js_set_lock, (int on), {
    Module.requestLock = !!on;
    if (on && Module.canvas && Module.canvas.requestPointerLock) {
        Module.canvas.requestPointerLock();
    } else if (!on && document.exitPointerLock) {
        document.exitPointerLock();
    }
    if (Module.canvas) Module.canvas.style.cursor = on ? 'none' : 'default';
});

EM_JS(int, js_locked, (void), { return (Module._locked || Module._touchLook) ? 1 : 0; });

EM_JS(void, js_present, (const unsigned int* src, int w, int h), {
    var cv = Module.canvas;
    var ctx = Module.ctx2d;
    if (!cv || !ctx) return;
    if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
    /* 先渲染到一张同尺寸的 offscreen，再 putImageData —— 直接改 cv.width
       会清空画布，逐帧重建 imageData 是这里最稳的做法 */
    var img = Module._img;
    if (!Module._img || Module._imgW !== w || Module._imgH !== h) {
        Module._img = ctx.createImageData(w, h);
        Module._imgW = w; Module._imgH = h;
        img = Module._img;
    }
    var dst = img.data;
    var off = src >> 2;
    var n = w * h;
    for (var i = 0, j = 0; i < n; i++, j += 4) {
        var v = HEAPU32[off + i];
        dst[j]     = (v >>> 16) & 255;
        dst[j + 1] = (v >>> 8) & 255;
        dst[j + 2] = v & 255;
        dst[j + 3] = 255;
    }
    ctx.putImageData(img, 0, 0);
});

/* ---- localStorage 文件后端 ---- */
EM_JS(int, js_file_read, (const char* name, void* buf, int cap), {
    try {
        var key = 'canity:' + UTF8ToString(name);
        var val = localStorage.getItem(key);
        if (val === null) return -1;
        var bin = atob(val);
        var n = bin.length < cap ? bin.length : cap;
        for (var i = 0; i < n; i++) HEAPU8[(buf + i) | 0] = bin.charCodeAt(i);
        return n;
    } catch (e) { return -1; }
});

EM_JS(int, js_file_write, (const char* name, const void* buf, int len), {
    try {
        var key = 'canity:' + UTF8ToString(name);
        var s = '';
        for (var i = 0; i < len; i++) s += String.fromCharCode(HEAPU8[(buf + i) | 0]);
        localStorage.setItem(key, btoa(s));
        localStorage.setItem(key + '#ts', String(Date.now()));
        return 0;
    } catch (e) { return -1; }
});

EM_JS(int, js_file_copy, (const char* from, const char* to), {
    try {
        var v = localStorage.getItem('canity:' + UTF8ToString(from));
        if (v === null) return -1;
        localStorage.setItem('canity:' + UTF8ToString(to), v);
        return 0;
    } catch (e) { return -1; }
});

EM_JS(int, js_file_rename, (const char* from, const char* to), {
    try {
        var v = localStorage.getItem('canity:' + UTF8ToString(from));
        if (v === null) return -1;
        localStorage.setItem('canity:' + UTF8ToString(to), v);
        localStorage.removeItem('canity:' + UTF8ToString(from));
        return 0;
    } catch (e) { return -1; }
});

EM_JS(int, js_file_delete, (const char* name), {
    try { localStorage.removeItem('canity:' + UTF8ToString(name)); return 0; }
    catch (e) { return -1; }
});

EM_JS(double, js_file_time, (const char* name), {
    try {
        var v = localStorage.getItem('canity:' + UTF8ToString(name) + '#ts');
        if (!v) return 0;
        return parseFloat(v) / 1000.0;
    } catch (e) { return 0; }
});

/* ---------------------------------------------------------------------
 * 时间
 * ------------------------------------------------------------------- */
double PlatNow(void) {
    return emscripten_get_now() / 1000.0;
}

void PlatSleepMs(int ms) {
    (void)ms;   /* 主线程不能阻塞；让出一帧即可 */
}

/* ---------------------------------------------------------------------
 * 窗口
 * ------------------------------------------------------------------- */
void PlatWindowSize(int* w, int* h) {
    int cw = 800, ch = 600;
    js_get_size(&cw, &ch);
    if (cw <= 0) cw = 800;
    if (ch <= 0) ch = 600;
    if (w) *w = cw;
    if (h) *h = ch;
}

int PlatMinimized(void) { return 0; }

EM_JS(int, js_focused, (void), { return document.hasFocus() ? 1 : 0; });

int PlatFocused(void) { return js_focused(); }

void PlatRestoreWindow(void) {
    /* 浏览器里没有"还原窗口"这回事；抢焦点只能由用户手势触发 */
}

void PlatSetTitle(const char* utf8) {
    if (utf8) emscripten_run_script(
        "document.title = '" "CANITY" "';");
}

/* ---------------------------------------------------------------------
 * 渲染目标 + 呈现
 * ------------------------------------------------------------------- */
static unsigned int* s_target = NULL;
static int s_tw = 0, s_th = 0;

unsigned int* PlatAcquireTarget(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    if (w == s_tw && h == s_th && s_target) return s_target;
    if (s_target) { free(s_target); s_target = NULL; }
    s_target = (unsigned int*)malloc((size_t)w * (size_t)h * sizeof(unsigned int));
    if (!s_target) return NULL;
    s_tw = w;
    s_th = h;
    return s_target;
}

const unsigned int* PlatTargetPtr(void) { return s_target; }

void PlatPresentFrame(const PlatPresentRect* r, const unsigned int* src, int sw, int sh) {
    (void)r;   /* 网页端由 CSS 负责 letterbox（canvas 用 object-fit: contain） */
    if (!src) return;
    js_present(src, sw, sh);
}

void PlatShutdown(void) {
    if (s_target) { free(s_target); s_target = NULL; }
    s_tw = s_th = 0;
}

/* ---------------------------------------------------------------------
 * 输入
 * ------------------------------------------------------------------- */
int PlatKeyDown(int vk) { return js_key_down(vk); }

void PlatMouseDelta(int* dx, int* dy) {
    int a = 0, b = 0;
    js_take_mouse(&a, &b);
    if (dx) *dx = a;
    if (dy) *dy = b;
}

void PlatShowCursor(int visible) {
    emscripten_run_script(visible ?
        "if(Module.canvas)Module.canvas.style.cursor='default';" :
        "if(Module.canvas)Module.canvas.style.cursor='none';");
}

void PlatSetCursorLock(int on) { js_set_lock(on); }
int  PlatCursorLocked(void) { return js_locked(); }

void PlatCursorPos(int* x, int* y) {
    if (x) *x = 0;
    if (y) *y = 0;
}

void PlatSetCursorPos(int x, int y) { (void)x; (void)y; }

void PlatRequestQuit(void) {
    /* 浏览器里"退出游戏"= 回到标题；主循环不能自己停掉 */
    emscripten_run_script("window.close && window.close();");
}

void PlatSetCapture(int on) {
    /* 指针锁定是全局的，拖拽期间不需要额外捕获 */
    (void)on;
}

/* ---------------------------------------------------------------------
 * 文件：只取路径的文件名部分（去掉 \ 或 / 之前的目录）
 * ------------------------------------------------------------------- */
static const char* BaseName(const char* path) {
    const char* p = path;
    const char* best = path;
    if (!path) return "";
    for (p = path; *p; p++) {
        if (*p == '\\' || *p == '/') best = p + 1;
    }
    return best;
}

int PlatExeDir(char* out, int cap) {
    if (out && cap > 0) out[0] = 0;   /* 网页端没有"exe 目录" */
    return 0;
}

int PlatReadFileBytes(const char* name, void* buf, int cap) {
    if (!name || !buf || cap <= 0) return -1;
    return js_file_read(BaseName(name), buf, cap);
}

int PlatWriteFileBytes(const char* name, const void* buf, int len) {
    if (!name || (!buf && len > 0)) return -1;
    return js_file_write(BaseName(name), buf, len);
}

int PlatReadFilePath(const char* path, void* buf, int cap) {
    return PlatReadFileBytes(path, buf, cap);
}

int PlatWriteFileAtomic(const char* path, const void* data, int len) {
    /* localStorage 的 setItem 本身就是原子的：
       要么旧值还在，要么新值生效，不会出现"写一半"。
       所以这里直接写目标键，不需要 .tmp 中转。 */
    return PlatWriteFileBytes(path, data, len);
}

void PlatMakeDir(const char* path) { (void)path; }   /* 浏览器没有目录 */

int PlatDeletePath(const char* path) {
    if (!path) return -1;
    return js_file_delete(BaseName(path));
}

EM_JS(int, js_lang_zh, (void), {
    var l = (navigator.language || 'en').toLowerCase();
    return l.indexOf('zh') === 0 ? 1 : 0;
});

int PlatSystemLanguageZh(void) { return js_lang_zh(); }

double PlatFileTime(const char* path) {
    if (!path) return 0.0;
    return js_file_time(BaseName(path));
}

int PlatCopyPath(const char* from, const char* to) {
    if (!from || !to) return -1;
    return js_file_copy(BaseName(from), BaseName(to));
}

int PlatRenamePath(const char* from, const char* to) {
    if (!from || !to) return -1;
    return js_file_rename(BaseName(from), BaseName(to));
}

/* ---------------------------------------------------------------------
 * 音频（R7 换成 WebAudio；现在保持静音，但接口齐全）
 * ------------------------------------------------------------------- */
void PlatLog(const char* msg) {
    if (msg) emscripten_log(EM_LOG_CONSOLE, "%s", msg);
}

void PlatSfx(int id, float gain) { (void)id; (void)gain; }

/* audio.c 在网页构建里被排除，这里补上它的公开接口（静音实现）。
   music.c 仍参与编译 —— 它只操作 provider 表，不需要音频后端。 */
void PlaySfx(int id, float vol) { PlatSfx(id, vol); }
void AudioPlayWorld(int id, float x, float y, float vol, float maxDist, int priority) {
    (void)id; (void)x; (void)y; (void)vol; (void)maxDist; (void)priority;
}
void AudioUpdateListener(float px, float py, float dx, float dy) {
    (void)px; (void)py; (void)dx; (void)dy;
}
void AudioComputeGains(float lx, float ly, float ldx, float ldy,
                       float sx, float sy, float* outL, float* outR) {
    (void)lx; (void)ly; (void)ldx; (void)ldy; (void)sx; (void)sy;
    if (outL) *outL = 0.0f;
    if (outR) *outR = 0.0f;
}
void InitAudio(void) { }
void ShutdownAudio(void) { }

/* ---------------------------------------------------------------------
 * 配乐（music.c 在网页构建里被排除，MCI 是 Win32 的东西）
 * -------------------------------------------------------------------
 * 浏览器端后续可以用 WebAudio 播同样的曲目；现在先静音，
 * 不能让"没有 BGM"变成"没有游戏"。
 * ------------------------------------------------------------------- */
#include "music.h"

void MusicInit(void) { }
void MusicShutdown(void) { }
void MusicLoadGameTracks(void) { }
int  MusicRegisterProvider(MusicProviderId id, const MusicProvider* api, void* ctx) {
    (void)id; (void)api; (void)ctx; return 0;
}
int  MusicUseProvider(MusicProviderId id) { (void)id; return 0; }
int  MusicSetTrack(MusicScene scene, const char* utf8Path) {
    (void)scene; (void)utf8Path; return 0;
}
void MusicSetScene(MusicScene scene) { (void)scene; }
void MusicSetMasterVolume(float v) { (void)v; }
void MusicSetPaused(int paused) { (void)paused; }
void MusicDuck(float seconds, float gain) { (void)seconds; (void)gain; }
void MusicNotify(MusicEvent event) { (void)event; }
void MusicTick(float dt) { (void)dt; }
float MusicEffectiveGain(void) { return 0.0f; }

/* ---------------------------------------------------------------------
 * Steam 专区（steam_zone.c 在网页构建里被排除）
 * -------------------------------------------------------------------
 * 网页版没有 Steamworks。所有云同步/成就调用都变成安全的空操作：
 * 本地存档照常工作（localStorage），只是不参与云端对比。
 * ------------------------------------------------------------------- */
#include "steam_zone.h"

int  SteamZoneInit(void) { return 0; }
void SteamZoneUploadSave(const char* path) { (void)path; }
void SteamZoneShutdown(void) { }
void SteamZoneTick(float dt) { (void)dt; }
int  SteamZoneReady(void) { return 0; }
int  SteamZoneUnlockAchievement(const char* n) { (void)n; return 0; }
int  SteamZoneHasAchievement(const char* n) { (void)n; return 0; }
int  SteamZoneCloudUpload(const char* n, const void* d, int b) { (void)n; (void)d; (void)b; return 0; }
int  SteamZoneCloudDownload(const char* n, void* b, int s) { (void)n; (void)b; (void)s; return -1; }
int  SteamZoneCloudGetSize(const char* n) { (void)n; return -1; }
int  SteamZoneCloudExists(const char* n) { (void)n; return 0; }
void SteamZoneMusicPause(int p) { (void)p; }
int  SteamZoneMusicPlaying(void) { return 0; }
void SteamZoneOpenOverlay(const char* d) { (void)d; }
int  SteamZoneCloudDecide(const void* l, int ll, const void* r, int rl, int* pick) {
    (void)l; (void)ll; (void)r; (void)rl; if (pick) *pick = 0; return 0;
}
int  SteamZoneCloudSync(const char* n, void* lb, int* bs) { (void)n; (void)lb; (void)bs; return 0; }
void SteamZoneSetStatInt(const char* n, int v) { (void)n; (void)v; }
void SteamZoneStoreStats(void) { }
void SteamZoneSetRichPresence(const char* k, const char* v) { (void)k; (void)v; }
void SteamZoneMusicDuck(float s) { (void)s; }
void SteamZoneMusicDuckTick(float dt) { (void)dt; }
int  SteamZoneUnlockAchievementIndex(int i) { (void)i; return 0; }
const char* SteamZoneStatusText(void) { return "WEB"; }

/* ---------------------------------------------------------------------
 * 手柄（gamepad.c 在网页构建里被排除）
 * -------------------------------------------------------------------
 * 浏览器有 Gamepad API，后续可以接；现在先给出"没插手柄"的实现，
 * 让键盘鼠标流程完全可用。
 * ------------------------------------------------------------------- */
void GamepadInit(void) { }
void GamepadPoll(void) { }
int  GamepadConnected(void) { return 0; }

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
}

int main(void) {
    js_plat_init();

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

    InitPixels();
    SpritesInit();
    GenerateThemeTextures(0);
    InitWorkers();

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
    if (js_has_selftest()) {
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

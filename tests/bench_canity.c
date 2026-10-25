/*
 * Canity 性能基准 —— 分阶段量 Render3D 的耗时。
 *
 * 用途：改动渲染热路径后，用它对比前后差异。
 *
 * 测量方法（很重要）：这台机器单帧耗时波动很大（同一份代码能测出
 * 25~68ms 的差别），所以「先跑完 A 再跑完 B」会得出完全错误的结论。
 * 正确做法是**交替测量多轮取中位数** —— 每一轮把 A、B 都测一遍，
 * 让两组共享相同的机器状态。
 *
 * 编译：
 *   gcc bench_canity.c -o bench_canity.exe -O2 -lgdi32 -luser32 -lwinmm
 * 运行：
 *   bench_canity.exe [宽] [高] [renderScale%]
 */

#define CANITY_TEST 1
#define SAVE_FILE "canity_bench.sav"
#define WinMain CanityAppMain
#include "canity_unity.c"
#undef WinMain

#include <stdio.h>

#define ROUNDS  9      /* 交替轮数 */
#define FRAMES  20     /* 每轮帧数 */

static int cmpDouble(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* ---- 各阶段 ---- */
static void phaseFloor(void) {
    for (int x = 0; x < renderW; x++) {
        float f = 0.32f + (x % 7) * 0.1f;
        flashTab[x] = f;
        floorGain[x] = 0.52f + f * 0.5f;
        ceilTab[x] = 40;
    }
    RunPhase(1);
}
static void phaseWall(void)  { RunPhase(2); }
static void phasePost(void)  { RunPhase(3); }
static void phaseAll(void)   { Render3D(); }

typedef struct { const char* name; void (*fn)(void); } Phase;

static double benchOne(void (*fn)(void)) {
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < FRAMES; i++) { g_timeSec += 0.016; fn(); }
    QueryPerformanceCounter(&t1);
    return (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart / FRAMES;
}

static void setupScene(int screenW, int screenH, int scalePct, int withPost) {
    /* 复刻 InitPixels 的分辨率计算（含像素预算） */
    winW = screenW; winH = screenH;
    float s = scalePct / 100.0f;
    renderW = (int)(winW * s);
    renderH = (int)(winH * s);
    if (renderW < 160) renderW = 160;
    if (renderH < 120) renderH = 120;
    if (renderW > MAX_SCREEN) renderW = MAX_SCREEN;
    if (renderH > MAX_SCREEN) renderH = MAX_SCREEN;
    {
        const long budget = PIXEL_BUDGET;   /* 与 main.c 共用同一个常量，别各写一份 */
        long area = (long)renderW * (long)renderH;
        if (area > budget) {
            float f = sqrtf((float)budget / (float)area);
            renderW = (int)(renderW * f);
            renderH = (int)(renderH * f);
        }
    }

    if (g_memDC) { DeleteDC(g_memDC); g_memDC = NULL; }
    if (g_dib) { DeleteObject(g_dib); g_dib = NULL; }
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = renderW;
    bmi.bmiHeader.biHeight = -renderH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_memDC = CreateCompatibleDC(NULL);
    if (g_memDC) g_dib = CreateDIBSection(g_memDC, &bmi, DIB_RGB_COLORS, &g_dibBits, NULL, 0);
    if (g_memDC && g_dib && g_dibBits) {
        SelectObject(g_memDC, g_dib);
        pixels = (unsigned int*)g_dibBits;
        g_useDIB = 1;
    }
    GenerateVignette();

    currentState = STATE_PLAYING;
    currentLevel = 1;
    runInProgress = 1;
    flashTime = 0.0f;
    isMapOpen = 0;
    memset(keys, 0, sizeof(keys));
    GenerateLevel();
    UpdateExplored();
    flashlightOn = 1;
    battery = 100.0f;
    sanity = withPost ? 40.0f : 100.0f;   /* 40 会触发低理智红边后处理 */
}

static void runScenario(int screenW, int screenH, int scalePct, int withPost, int workers) {
    /* 线程数：0 = 单线程，其余 = 指定工作线程数 */
    g_workerCount = workers;
    g_parts = workers + 1;

    setupScene(screenW, screenH, scalePct, withPost);

    Phase phases[4] = {
        { "地板", phaseFloor },
        { "墙体", phaseWall },
        { "后处理", phasePost },
        { "Render3D 合计", phaseAll },
    };

    printf("\n屏幕 %dx%d, Render Scale %d%%, 内部分辨率 %dx%d (%ld 像素), %s, 工作线程 %d\n",
           screenW, screenH, scalePct, renderW, renderH,
           (long)renderW * renderH, withPost ? "低理智(带红边)" : "高理智", workers);

    double res[4][ROUNDS];
    for (int r = 0; r < ROUNDS; r++)
        for (int p = 0; p < 4; p++)
            res[p][r] = benchOne(phases[p].fn);

    for (int p = 0; p < 4; p++) {
        qsort(res[p], ROUNDS, sizeof(double), cmpDouble);
        printf("  %-14s 中位 %6.2f ms   最小 %6.2f ms\n",
               phases[p].name, res[p][ROUNDS / 2], res[p][0]);
    }
}

int main(int argc, char** argv) {
    int W = (argc >= 3) ? atoi(argv[1]) : 1920;
    int H = (argc >= 3) ? atoi(argv[2]) : 1080;
    int scale = (argc >= 4) ? atoi(argv[3]) : 100;

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    printf("Canity 渲染性能基准 —— 逻辑核心 %u\n", (unsigned int)si.dwNumberOfProcessors);
    printf("(交替测量 %d 轮 x %d 帧取中位数；纯 CPU 渲染，不含 GDI 呈现)\n", ROUNDS, FRAMES);

    /* 开真实线程池 */
    InitWorkers();

    runScenario(W, H, scale, 1, g_workerCount);   /* 低理智：带红边后处理 */
    runScenario(W, H, scale, 0, g_workerCount);   /* 高理智：无后处理 */
    runScenario(W, H, scale, 1, 0);               /* 单线程对照 */

    ShutdownWorkers();
    return 0;
}

/* =====================================================================
 * CANITY 平台层（Plat）—— 唯一允许接触操作系统的接口
 * ---------------------------------------------------------------------
 * 目的：游戏逻辑、渲染算法、UI 一条都不改，只在"和世界打交道"的地方
 * 抽出一层。桌面（Win32）与网页（Emscripten）各写一份实现。
 *
 * 铁律（tools/partition_check.py 会检查）：
 *   · 本头文件**不许**出现任何 Win32 类型（HWND / HDC / DWORD / VK_*）
 *   · 除 include/common.h 外，系统头只允许出现在 src/platform/ 下的 .c 文件
 *   · plat_*.c 之外的任何 .c 不得直接调用系统 API
 *
 * 设计取舍：接口宁可窄。每多一个函数，网页后端就多一份要维护的实现。
 * ===================================================================== */
#ifndef CANITY_PLAT_H
#define CANITY_PLAT_H

/* ---------------------------------------------------------------------
 * 时间
 * ------------------------------------------------------------------- */
/* 平台初始化（网页端绑定 canvas / 装事件监听；桌面端空实现）。 */
void PlatInit(void);

/* 是否请求了存档自检（?selftest=1；桌面端恒为 0）。 */
int PlatSelfTestRequested(void);

/* 把初始化各阶段耗时报到页面上（?perf=1 时使用；桌面端空实现）。 */
int  PlatShowPerf(double pixMs, double sprMs, double texMs, double totalMs);
/* 第一帧真正画出来的时刻（网页端用它标记性能基线）。 */
void PlatMarkReady(void);

/* 单调递增秒数，首次调用返回 0 附近。用于主循环累加与排程。 */
double PlatNow(void);

/* 毫秒级单调时钟（双键防误触这类"两秒内再按一次"用）。 */
unsigned int PlatTicksMs(void);

/* 让出指定毫秒。桌面真睡；网页端只让出一帧（不能阻塞主线程）。 */
void PlatSleepMs(int ms);

/* ---------------------------------------------------------------------
 * 窗口
 * ------------------------------------------------------------------- */
/* 客户区尺寸（不含边框）。网页后端返回 canvas 的 CSS 尺寸。 */
void PlatWindowSize(int* w, int* h);
int  PlatMinimized(void);
/* 窗口/画布是否处于前台（没有焦点时不采样按键）。 */
int  PlatFocused(void);
/* 从最小化恢复并抢回前台（Win32 有；网页端是空操作）。 */
void PlatRestoreWindow(void);
void PlatSetTitle(const char* utf8);

/* ---------------------------------------------------------------------
 * 渲染目标 + 呈现
 * -------------------------------------------------------------------
 * 平台层拥有"像素往哪个缓冲写"这件事：
 *   · Win32：DIB Section（GDI 共享内存，呈现时 BitBlt 直接拷）
 *   · Web  ：一片 malloc 缓冲，呈现时 putImageData
 * 游戏只拿到一个可写指针，不关心它从哪来。
 * ------------------------------------------------------------------- */
/* 重新分配 w*h 的可写 32 位像素缓冲（0xAARRGGBB，alpha 忽略）。失败返回 NULL。 */
unsigned int* PlatAcquireTarget(int w, int h);

/* 一次呈现的几何。全部单位是**窗口像素**。
 *   fullBleed = 1 表示画面铺满客户区，可以省掉离屏合成
 *   dx/dy/dw/dh 是画面在客户区里的目标矩形（有黑边时用来居中） */
typedef struct {
    int fullBleed;
    int dx, dy, dw, dh;
    int cw, ch;
} PlatPresentRect;

/* 把 src 里的 sw*sh 像素送到屏幕。 */
void PlatPresentFrame(const PlatPresentRect* r, const unsigned int* src, int sw, int sh);

/* 进程退出前释放平台资源（DC / 位图 / canvas）。 */
void PlatShutdown(void);

/* 当前目标缓冲的指针（网页里 JS 靠它拿 HEAPU8 视图）。 */
const unsigned int* PlatTargetPtr(void);

/* ---------------------------------------------------------------------
 * 输入
 * -------------------------------------------------------------------
 * vk 使用 Windows 虚拟键码（0x01..0xFE）作为**跨平台键位编号**。
 * 网页后端把 KeyboardEvent.code 映射到同一套编号，游戏层不需要知道。
 * ------------------------------------------------------------------- */
int  PlatKeyDown(int vk);

/* 自上一帧以来的鼠标位移（像素）。 */
void PlatMouseDelta(int* dx, int* dy);

/* 光标可见性（跨状态切换要能确定地拧到目标值）。 */
void PlatShowCursor(int visible);

/* 指针锁定（第一人称视角需要无限位移）。 */
void PlatSetCursorLock(int on);
int  PlatCursorLocked(void);

void PlatCursorPos(int* x, int* y);
void PlatSetCursorPos(int x, int y);

/* 请求退出主循环（菜单里的 Quit）。 */
void PlatRequestQuit(void);

/* 鼠标捕获：拖出窗口/画布也能继续收到移动（滑条、拖拽用）。 */
void PlatSetCapture(int on);

/* 取一次待处理的鼠标事件（网页端用；桌面端恒返回 0，走窗口消息）。
   坐标已经是**渲染空间**像素。 */
int PlatTakeMouseMove(int* x, int* y);
int PlatTakeMouseClick(int* x, int* y);

/* 取一次待处理的滚轮量（120 = 一格）。返回 1 表示有。 */
int PlatTakeWheel(int* delta, int* x, int* y);

/* ---------------------------------------------------------------------
 * 文件
 * -------------------------------------------------------------------
 * name 是**相对文件名**（含子目录用 /），不是绝对路径。
 * 平台层决定根目录：
 *   · Win32：exe 所在目录
 *   · Web  ：localStorage
 * ------------------------------------------------------------------- */
int PlatReadFileBytes(const char* name, void* buf, int cap);      /* 返回字节数，失败 <0 */
int PlatWriteFileBytes(const char* name, const void* buf, int len);/* 返回 0 成功 */
int PlatExeDir(char* out, int cap);                                /* 返回写入长度 */

/* ---------------------------------------------------------------------
 * 路径级原语（存档分区用；path 是完整路径，不是相对名）
 * -------------------------------------------------------------------
 * 网页端没有真实文件系统：这里的 path 只取**文件名部分**作为 localStorage
 * 的键，目录信息被丢弃。存档格式与调用方逻辑完全不变。
 * ------------------------------------------------------------------- */
int PlatReadFilePath(const char* path, void* buf, int cap);         /* 字节数，失败 <0 */
/* 原子写：先写临时文件 + 落盘 + 改名。0 = 成功，否则平台错误码。 */
int PlatWriteFileAtomic(const char* path, const void* data, int len);
/* 文件最后修改时间（秒，越大越新）。取不到返回 0。 */
double PlatFileTime(const char* path);
void PlatMakeDir(const char* path);                                 /* 失败静默 */
int  PlatDeletePath(const char* path);                              /* 0 = 成功 */

/* 系统语言是否中文（首次运行时用来决定默认语言）。 */
int  PlatSystemLanguageZh(void);
int PlatCopyPath(const char* from, const char* to);                 /* 0 = 成功 */
int PlatRenamePath(const char* from, const char* to);               /* 0 = 成功 */

/* ---------------------------------------------------------------------
 * 音频
 * ------------------------------------------------------------------- */
/* 播放一次音效。实体数据仍在 audio.c，这里只负责"送出去"。 */
/* 调试日志（桌面走 OutputDebugString / 网页走 console.error）。 */
void PlatLog(const char* msg);

void PlatSfx(int id, float gain);

#endif /* CANITY_PLAT_H */

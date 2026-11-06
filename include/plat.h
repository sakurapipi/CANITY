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
/* 单调递增秒数，首次调用返回 0 附近。用于主循环累加与排程。 */
double PlatNow(void);

/* 让出指定毫秒。桌面真睡；网页端只让出一帧（不能阻塞主线程）。 */
void PlatSleepMs(int ms);

/* ---------------------------------------------------------------------
 * 窗口
 * ------------------------------------------------------------------- */
/* 客户区尺寸（不含边框）。网页后端返回 canvas 的 CSS 尺寸。 */
void PlatWindowSize(int* w, int* h);
int  PlatMinimized(void);
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

/* 指针锁定（第一人称视角需要无限位移）。 */
void PlatSetCursorLock(int on);
int  PlatCursorLocked(void);

void PlatCursorPos(int* x, int* y);
void PlatSetCursorPos(int x, int y);

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
 * 音频
 * ------------------------------------------------------------------- */
/* 播放一次音效。实体数据仍在 audio.c，这里只负责"送出去"。 */
void PlatSfx(int id, float gain);

#endif /* CANITY_PLAT_H */

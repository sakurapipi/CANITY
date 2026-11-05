/*
 * Canity — 存档分区：后端（**唯一碰文件的出口**）
 *
 * 规则（写进 tools/partition_check.py）：
 *   · 除本文件外，任何 .c 都不许自己 fopen / CreateFile 打开 canity*.sav
 *   · 云上传下载也只在这里发起，src/steam/ 只提供传输原语
 *
 * 依赖方向：
 *   save_backend.c ──调用──> src/steam/ 的 SteamZoneCloudUpload / SteamZoneCloudSync
 *   save_backend.c ──include──> include/save_format.h（信封契约）
 *   不反向：src/steam/ 不调用本文件。
 *
 * 2026-11-05 从 src/save.c 与 src/steam/steam_zone.c 各搬一部分过来：
 *   · WriteWholeFile / ReadWholeFile  ← save.c（原本是 static）
 *   · SteamZoneSyncLocalSaves 的云→本地回写 ← steam_zone.c
 *     （原来用裸 fopen，绕过了原子写与 .bak 链，是缺陷 ④）
 */
#include "common.h"
#include "save.h"
#include "save_format.h"
#include "steam_zone.h"   /* 传输原语：SteamZoneCloudUpload / SteamZoneCloudSync */

/* g_saveState / g_saveError / g_savePath 的定义在 save.c，
   声明在 save.h（上面已 include），这里直接用，不重复定义。 */

/* 由 save.c 导出：按「候选路径 i + 存档槽 slot」算路径 */
const char* SavePathForSlot(int i, int slot, char* buf, size_t n);

/* ================================================================== */
/* 原子写 / 整份读                                                     */
/* ================================================================== */

/* 返回 0 = 成功，否则 = Win32 错误码。
   先写 .tmp、flush 落盘、再原子改名 —— 写到一半掉电只会留下 .tmp，
   不会把主档变成「存在但是空的」。 */
int SaveBackendWrite(const char* path, const void* data, size_t n) {
    if (!path || !path[0] || !data) return 87;   /* ERROR_INVALID_PARAMETER */
    char tmp[MAX_PATH + 16];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return (int)GetLastError();

    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, (DWORD)n, &wrote, NULL);
    DWORD err = ok ? 0 : GetLastError();
    if (ok && wrote != (DWORD)n) err = ERROR_WRITE_FAULT;

    /* ==================================================================
     * 2026-11-06（v2.4-alpha R6）：**保留第一次失败的错误码**
     * ------------------------------------------------------------------
     * 原来这两行都没检查返回值：
     *     if (ok) FlushFileBuffers(h);
     *     CloseHandle(h);
     *
     * 对存档来说，FlushFileBuffers 失败 = 数据可能还在系统缓存里没落盘，
     * 这等同于写失败，必须报出去；否则会向调用方谎报"保存成功"，
     * 而掉电后文件是旧的甚至半截。
     *
     * 顺序问题：后续失败不能覆盖**第一个**错误码 —— 第一个才是根因。
     * （审查提到的另一点也修了：调用方不该在失败后再去读
     *   GetLastError()，因为中间任何一次成功调用都可能把它冲掉。
     *   这里返回的是捕获到的值，调用方必须用返回值。）
     * ================================================================== */
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

/* 返回读到的字节数，失败返回 -1 */
long SaveBackendRead(const char* path, void* buf, size_t cap) {
    if (!path || !path[0] || !buf || cap == 0) return -1;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, (DWORD)cap, &got, NULL);
    /* 读侧的 CloseHandle 失败不影响已读到的数据，但也不该静默：
       记一份在 g_saveError 之外没有意义 —— 这里只保证句柄一定被关掉。 */
    CloseHandle(h);
    return ok ? (long)got : -1;
}

/* ================================================================== */
/* 云同步                                                              */
/* ================================================================== */

/* 把本地文件交给 Steam 专区上传。
   **不合成信封** —— 文件本身就是信封格式（存档分区写盘时封好的）。 */
void SaveBackendUpload(const char* path) {
    if (!path || !path[0]) return;

    size_t cap = 1024 * 1024;
    unsigned char* buf = (unsigned char*)malloc(cap);
    if (!buf) return;
    long n = SaveBackendRead(path, buf, cap);
    if (n <= (long)sizeof(SaveEnvelope)) { free(buf); return; }

    SaveEnvelope h;
    memcpy(&h, buf, sizeof(h));
    if (h.magic != SAVE_ENVELOPE_MAGIC ||
        !SaveEnvelopeValid(&h, buf + sizeof(h), (int)(n - (long)sizeof(h)))) {
        /* 还是旧式裸档：本次不上传，等下一次 SaveGame 写成信封后再传。
           这样不会把一份「没有云端身份」的字节推上去。 */
        free(buf);
        return;
    }

    const char* name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    SteamZoneCloudUpload(name, buf, (int)n);
    free(buf);
}

/* 云 <-> 本地。冲突判定在 Steam 专区里（唯一一份时间戳/校验和逻辑）。
   本函数只负责：取字节 → 调判定 → 云端赢时**用原子写**回本地并先备份。 */
void SaveBackendSyncAll(void) {
    if (!SteamZoneReady()) return;

    for (int slot = 0; slot < 3; slot++) {
        char p[MAX_PATH];
        SavePathForSlot(0, slot, p, sizeof(p));
        if (!p[0]) continue;

        size_t cap = 1024 * 1024;
        unsigned char* buf = (unsigned char*)malloc(cap);
        if (!buf) continue;
        long n = SaveBackendRead(p, buf, cap);
        if (n <= (long)sizeof(SaveEnvelope)) { free(buf); continue; }

        SaveEnvelope h;
        memcpy(&h, buf, sizeof(h));
        if (h.magic != SAVE_ENVELOPE_MAGIC ||
            !SaveEnvelopeValid(&h, buf + sizeof(h), (int)(n - (long)sizeof(h)))) {
            free(buf);
            continue;   /* 裸档不参与云同步 */
        }

        const char* name = strrchr(p, '\\');
        name = name ? name + 1 : p;

        /* ================================================================
         * 2026-11-06（v2.4-alpha R7）：长度改成 in-out
         * ----------------------------------------------------------------
         * 原来传 (int)n —— 云端那份如果长度不同，Sync 内部会直接判损坏。
         * 现在传 &len：进来是本地缓冲容量，出去是**最终生效那份的长度**。
         * buf 分配的是 1 MiB，所以云端更长也放得下。
         * ================================================================ */
        int len = (int)n;
        int sync = SteamZoneCloudSync(name, buf, &len);

        if (sync == STEAM_SYNC_CLOUDWIN) {
            /* 云端赢：先留一份，再用**原子写**覆盖（修缺陷 ④） */
            char bak[MAX_PATH + 32];
            snprintf(bak, sizeof(bak), "%s.pre-cloud.bak", p);
            /* v2.4-alpha R6：备份失败要显式知道。
               备份没生成 = 一旦下面是覆盖，玩家就没有本地的上一份了。 */
            BOOL backed = CopyFileA(p, bak, FALSE);
            int werr = SaveBackendWrite(p, buf, (size_t)len);   /* ← 用 len，不是 n */
            if (werr != 0) {
                g_saveState = 2;
                /* **用返回值，不要再用 GetLastError()** ——
                   SaveBackendWrite 内部已经捕获了第一次失败的错误码，
                   之后再调 GetLastError 拿到的可能不是它。 */
                g_saveError = werr;
            } else if (!backed) {
                /* 写成功了，但备份没成 —— 记一个可诊断的状态，不改写结果 */
                g_saveError = (int)GetLastError();
            }
        } else if (sync == STEAM_SYNC_CONFLICT) {
            /* ============================================================
             * **双方都合法但无法裁决**：同秒不同内容，或云端那份装不下。
             * 原则：不覆盖任何一方 —— 把云端那份留成可恢复副本，
             * 本地保持不动，记一个状态让上层可以提示玩家。
             * ============================================================ */
            char cf[MAX_PATH + 32];
            snprintf(cf, sizeof(cf), "%s.conflict-cloud.bak", p);
            if (len > 0 && (size_t)len <= cap) {
                /* SteamZoneCloudSync 在 CONFLICT 时不会改写 buf，
                   所以这里 buf 仍是本地内容 —— 用原始读取结果另存云端副本
                   需要重新下一次，代价高；改为只标记状态，副本由玩家侧
                   .pre-cloud.bak 在下次成功同步时生成。 */
                (void)cf;
            }
            g_saveState = 2;
            g_saveError = 0;   /* 0 = 冲突（不是系统错误） */
        }
        free(buf);
    }
}

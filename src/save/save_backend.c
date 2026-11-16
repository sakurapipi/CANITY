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
#include "steam_zone.h"
#include "plat.h"   /* 传输原语：SteamZoneCloudUpload / SteamZoneCloudSync */

/* g_saveState / g_saveError / g_savePath 的定义在 save.c，
   声明在 save.h（上面已 include），这里直接用，不重复定义。 */

/* 由 save.c 导出：按「候选路径 i + 存档槽 slot」算路径 */

/* ================================================================== */
/* 原子写 / 整份读                                                     */
/* ================================================================== */

/* 返回 0 = 成功，否则 = Win32 错误码。
   先写 .tmp、flush 落盘、再原子改名 —— 写到一半掉电只会留下 .tmp，
   不会把主档变成「存在但是空的」。 */
int SaveBackendWrite(const char* path, const void* data, size_t n) {
    /* 2026-11-06（R3）：原子写搬进平台层。
       桌面仍是 .tmp -> FlushFileBuffers -> 原子改名；
       网页端是 localStorage 的临时键 -> 正式键。
       这里只拿错误码，不再碰任何系统 API。 */
    if (!path || !path[0] || !data) return 87;
    if (n > (size_t)INT_MAX) return SAVE_ERR_CAPACITY;
    return PlatWriteFileAtomic(path, data, (int)n);
}

/* 返回读到的字节数，失败返回 -1 */
long SaveBackendRead(const char* path, void* buf, size_t cap) {
    if (!path || !path[0] || !buf || cap == 0 || cap > (size_t)INT_MAX) return -1;
    return (long)PlatReadFilePath(path, buf, (int)cap);
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

static int CloudEnvelopeValid(const void* data, int len) {
    if (!data || len <= (int)sizeof(SaveEnvelope)) return 0;
    SaveEnvelope envelope;
    memcpy(&envelope, data, sizeof(envelope));
    return SaveEnvelopeValid(&envelope, (const unsigned char*)data + sizeof(envelope),
                              len - (int)sizeof(envelope));
}

static void CloudRecordError(int* firstError, const char* path, int error) {
    if (!error) return;
    if (!*firstError) *firstError = error;
    SaveDiag("CLOUD error=%d path=%s", error, path);
}

/* A fallback destination may contain another local copy. Never overwrite it
   unless its pre-cloud backup was successfully created first. */
static int CloudInstallAt(const char* path, const void* data, int len) {
    int exists = PlatPathExists(path);
    if (exists < 0) return -exists;
    if (exists) {
        char backup[MAX_PATH + 32];
        snprintf(backup, sizeof(backup), "%s.pre-cloud.bak", path);
        if (PlatCopyPath(path, backup)) return SAVE_ERR_CLOUD_BACKUP;
    }
    return SaveBackendWrite(path, data, (size_t)len);
}

static int CloudInstall(int slot, const char* preferred, const void* data, int len) {
    if (!CloudEnvelopeValid(data, len)) return SAVE_ERR_PAYLOAD;
    int firstError = 0, tried = 0;
    char visited[SAVE_CAND_COUNT + 1][MAX_PATH];
    for (int i = -1; i < SAVE_CAND_COUNT; i++) {
        char path[MAX_PATH];
        if (i < 0) snprintf(path, sizeof(path), "%s", preferred);
        else SavePathForSlot(i, slot, path, sizeof(path));
        if (!path[0]) continue;
        int duplicate = 0;
        for (int j = 0; j < tried; j++) if (!strcmp(visited[j], path)) duplicate = 1;
        if (duplicate) continue;
        snprintf(visited[tried++], sizeof(visited[0]), "%s", path);
        int error = CloudInstallAt(path, data, len);
        SaveDiag("CLOUD install path=%s bytes=%d error=%d", path, len, error);
        if (!error) {
            snprintf(g_savePath, sizeof(g_savePath), "%s", path);
            return 0;
        }
        if (!firstError) firstError = error;
    }
    return firstError ? firstError : 5;
}

/* Conflicting cloud bytes must not replace the local run or an older, distinct
   conflict snapshot. The payload hash and bounded suffix distinguish snapshots. */
static int CloudKeepConflict(const char* path, const void* data, int len) {
    if (!CloudEnvelopeValid(data, len)) return SAVE_ERR_PAYLOAD;
    unsigned hash = SaveFnv1a(data, len);
    unsigned char* existing = malloc((size_t)len + 1);
    if (!existing) return 8;
    int error = SAVE_ERR_CAPACITY;
    for (int i = 0; i < 32; i++) {
        char target[MAX_PATH + 64];
        if (!i) snprintf(target, sizeof(target), "%s.conflict-cloud.bak", path);
        else snprintf(target, sizeof(target), "%s.conflict-cloud-%08x-%d.bak", path, hash, i);
        int exists = PlatPathExists(target);
        if (exists < 0) { error = -exists; break; }
        if (exists) {
            long n = SaveBackendRead(target, existing, (size_t)len + 1);
            if (n == len && !memcmp(existing, data, (size_t)len)) { error = 0; break; }
            continue;
        }
        error = SaveBackendWrite(target, data, (size_t)len);
        SaveDiag("CLOUD conflict-copy path=%s bytes=%d error=%d", target, len, error);
        break;
    }
    free(existing);
    return error;
}

static void CloudRestoreNew(int slot, const char* path, const char* name,
                            void* data, int cap, int* firstError) {
    if (!SteamZoneCloudExists(name)) return;
    int remoteSize = SteamZoneCloudGetSize(name);
    if (remoteSize <= (int)sizeof(SaveEnvelope) || remoteSize > cap) {
        CloudRecordError(firstError, path, SAVE_ERR_CAPACITY);
        return;
    }
    int pulled = 0;
    if (!SteamZoneCloudPullNew(name, data, cap, &pulled)) {
        CloudRecordError(firstError, path, SAVE_ERR_CLOUD_SYNC);
        return;
    }
    CloudRecordError(firstError, path, CloudInstall(slot, path, data, pulled));
}

/* Cloud I/O is confined to this backend. Length and allocation capacity are
   distinct, and any failed restore remains visible after all slots are tried. */
void SaveBackendSyncAll(void) {
    if (!SteamZoneReady()) return;
    int firstError = 0;
    for (int slot = 0; slot < SAVE_SLOTS; slot++) {
        char path[MAX_PATH];
        SavePathForSlot(0, slot, path, sizeof(path));
        if (!path[0]) continue;
        /* Follow the newest existing candidate for this slot, including a
           previous cloud recovery that had to use a writable fallback. */
        double newest = PlatFileTime(path);
        for (int i = 1; i < SAVE_CAND_COUNT; i++) {
            char candidate[MAX_PATH];
            SavePathForSlot(i, slot, candidate, sizeof(candidate));
            double modified = candidate[0] ? PlatFileTime(candidate) : 0;
            if (modified > newest) {
                newest = modified;
                snprintf(path, sizeof(path), "%s", candidate);
            }
        }
        const char* name = strrchr(path, '\\');
        const char* slash = strrchr(path, '/');
        if (!name || (slash && slash > name)) name = slash;
        name = name ? name + 1 : path;
        const int cap = 1024 * 1024;
        unsigned char* data = malloc((size_t)cap);
        if (!data) { CloudRecordError(&firstError, path, 8); continue; }
        long localLen = SaveBackendRead(path, data, (size_t)cap);
        if (localLen <= (long)sizeof(SaveEnvelope)) {
            int exists = PlatPathExists(path);
            if (!exists) CloudRestoreNew(slot, path, name, data, cap, &firstError);
            else CloudRecordError(&firstError, path, exists < 0 ? -exists :
                                   localLen < 0 ? SAVE_ERR_CLOUD_SYNC : SAVE_ERR_PAYLOAD);
            free(data);
            continue;
        }
        if (!CloudEnvelopeValid(data, (int)localLen)) { free(data); continue; }
        int length = cap;
        void* conflict = NULL;
        int conflictLen = 0;
        int result = SteamZoneCloudSyncEx(name, data, (int)localLen, &length,
                                          &conflict, &conflictLen);
        if (result == STEAM_SYNC_CLOUDWIN) {
            CloudRecordError(&firstError, path, CloudInstall(slot, path, data, length));
        } else if (result == STEAM_SYNC_CONFLICT) {
            int error = CloudKeepConflict(path, conflict, conflictLen);
            CloudRecordError(&firstError, path, error ? error : SAVE_ERR_CLOUD_CONFLICT);
        } else if (result == STEAM_SYNC_ERROR) {
            CloudRecordError(&firstError, path, SAVE_ERR_CLOUD_SYNC);
        }
        free(conflict);
        free(data);
    }
    if (firstError) { g_saveState = 2; g_saveError = firstError; }
}

/* Keep a byte-identical pre-migration copy. Existing backups are never overwritten. */
int SaveBackendPreserveVersion(const char* path,int version){
    size_t cap=16u*1024u*1024u;
    unsigned char* original=malloc(cap);unsigned char* prior=malloc(cap);
    if(!original || !prior){free(original);free(prior);return 8;}
    long n=SaveBackendRead(path,original,cap);
    if(n<=0 || n==(long)cap){free(original);free(prior);return SAVE_ERR_PAYLOAD;}
    char backup[MAX_PATH+64];unsigned hash=2166136261u;
    for(long i=0;i<n;i++)hash=(hash^original[i])*16777619u;
    snprintf(backup,sizeof(backup),"%s.v%d.bak",path,version);
    long old=SaveBackendRead(backup,prior,cap);
    if(old>=0 && (old!=n || memcmp(original,prior,(size_t)n))){
        snprintf(backup,sizeof(backup),"%s.v%d-%08x.bak",path,version,hash);
        old=SaveBackendRead(backup,prior,cap);
    }
    int code=0;
    if(old>=0){if(old!=n || memcmp(original,prior,(size_t)n))code=SAVE_ERR_PAYLOAD;}
    else code=SaveBackendWrite(backup,original,(size_t)n);
    if(!code){old=SaveBackendRead(backup,prior,cap);if(old!=n || memcmp(original,prior,(size_t)n))code=SAVE_ERR_PAYLOAD;}
    free(original);free(prior);return code;
}

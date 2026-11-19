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

static unsigned char cloudBlocked[SAVE_SLOTS];
/* Positive, process-local agreement is required in addition to negative disk
   markers. Restart never inherits permission from an absent/failed marker. */
static unsigned char cloudSessionReady[SAVE_SLOTS];
static unsigned char* cloudBase[SAVE_SLOTS];
static int cloudBaseLength[SAVE_SLOTS];
static void CloudDefer(int slot, const char* path, const void* data, int length, int* first);
static int CloudAcceptSession(int slot, const void* data, int length) {
    cloudSessionReady[slot] = 0;
    unsigned char* copy = length > 0 ? malloc((size_t)length) : NULL;
    if (length > 0 && !copy) return 8;
    if (length > 0) memcpy(copy, data, (size_t)length);
    free(cloudBase[slot]); cloudBase[slot] = copy; cloudBaseLength[slot] = length;
    cloudSessionReady[slot] = 1;
    return 0;
}
static const char* CloudName(const char* path) {
    const char* slash = strrchr(path, '/');
    const char* back = strrchr(path, '\\');
    const char* end = (!slash || (back && back > slash)) ? back : slash;
    return end ? end + 1 : path;
}
static int CloudSlot(const char* path) {
    for (int slot = 0; slot < SAVE_SLOTS; slot++) {
        char name[32]; snprintf(name, sizeof(name), "canity%d.sav", slot);
        if (!strcmp(CloudName(path), name)) return slot;
    }
    return -1;
}
static int CloudSaveValid(const void* data, int length) {
    unsigned magic = 0;
    if (data && length >= 4) memcpy(&magic, data, 4);
    return magic == SAVE_ENVELOPE_MAGIC && length > 0 && SaveValidateBytes(data, (size_t)length);
}
int SaveBackendCloudBlocked(int slot) {
    if (slot < 0 || slot >= SAVE_SLOTS) return 1;
    if (cloudBlocked[slot]) return 1;
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        char base[MAX_PATH], path[MAX_PATH + 32];
        SavePathForSlot(i, slot, base, sizeof(base)); if (!base[0]) continue;
        snprintf(path, sizeof(path), "%s.cloud-block", base);
        if (PlatPathExists(path) != 0) return 1;
        snprintf(path, sizeof(path), "%s.delete-journal", base);
        if (PlatPathExists(path) != 0) return 1;
    }
    return 0;
}
static int CloudBlock(int slot) {
    cloudBlocked[slot] = 1;
    cloudSessionReady[slot] = 0;
    free(cloudBase[slot]); cloudBase[slot] = NULL; cloudBaseLength[slot] = 0;
    const unsigned marker[3] = { 0x43424c32u, 1, (unsigned)slot };
    int first = 0;
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        char base[MAX_PATH], path[MAX_PATH + 32];
        SavePathForSlot(i, slot, base, sizeof(base)); if (!base[0]) continue;
        snprintf(path, sizeof(path), "%s.cloud-block", base);
        int error = SaveBackendWrite(path, marker, sizeof(marker));
        if (!error) return 0;
        if (!first) first = error;
    }
    return first ? first : 5;
}
static void CloudLocalPath(int slot, char* path, size_t cap) {
    SavePathForSlot(0, slot, path, cap);
    double newest = path[0] ? PlatFileTime(path) : 0;
    for (int i = 1; i < SAVE_CAND_COUNT; i++) {
        char candidate[MAX_PATH]; SavePathForSlot(i, slot, candidate, sizeof(candidate));
        double stamp = candidate[0] ? PlatFileTime(candidate) : 0;
        if (stamp > newest) { newest = stamp; snprintf(path, cap, "%s", candidate); }
    }
}
/* Routine saves stay local until an explicit choice resolves the slot. */
void SaveBackendUpload(const char* path) {
    if (!path || !path[0] || !SteamZoneReady()) return;
    int slot = CloudSlot(path);
    if (slot < 0 || !cloudSessionReady[slot] || SaveBackendCloudBlocked(slot)) {
        SaveDiag("CLOUD upload deferred: no session agreement slot=%d path=%s", slot, path); return;
    }
    unsigned char* data = malloc(SAVE_MAX_FILE_BYTES);
    if (!data) return;
    long n = SaveBackendRead(path, data, SAVE_MAX_FILE_BYTES);
    if (n <= 0 || !CloudSaveValid(data, (int)n)) {
        SaveDiag("CLOUD upload rejected invalid-local path=%s", path); free(data); return;
    }
    void* remote = NULL; int remoteLength = 0;
    int result = SteamZoneCloudInspect(CloudName(path), data, (int)n, &remote, &remoteLength);
    int absent = result == STEAM_SYNC_NOCLOUD;
    int valid = absent || (result != STEAM_SYNC_ERROR && CloudSaveValid(remote, remoteLength));
    int unchanged = valid && remoteLength == cloudBaseLength[slot] &&
        (!remoteLength || !memcmp(remote, cloudBase[slot], (size_t)remoteLength));
    int error = 0;
    if (!unchanged) {
        CloudDefer(slot, path, remote, remoteLength, &error);
    } else {
        /* Preserve the exact prior cloud bytes before an automatic replacement. */
        if (remoteLength) error = SaveBackendKeepSnapshot(path, "pre-upload-cloud", remote,
                                                          (size_t)remoteLength, NULL, 0);
        if (!error) {
            cloudSessionReady[slot] = 0;
            if (!SteamZoneCloudUpload(CloudName(path), data, (int)n)) error = SAVE_ERR_CLOUD_SYNC;
            else error = CloudAcceptSession(slot, data, (int)n);
        }
        if (error) CloudDefer(slot, path, remote, remoteLength, &error);
    }
    if (error) { g_saveState = 2; g_saveError = error; }
    free(remote); free(data);
}
static void CloudRecordError(int* first, const char* path, int error) {
    if (!error) return;
    if (!*first) *first = error;
    SaveDiag("CLOUD error=%d path=%s", error, path);
}
static int CloudInstallAt(const char* path, const void* data, int length) {
    int exists = PlatPathExists(path);
    if (exists < 0) return -exists;
    if (exists) {
        unsigned char* old = malloc(SAVE_MAX_FILE_BYTES);
        if (!old) return 8;
        long n = SaveBackendRead(path, old, SAVE_MAX_FILE_BYTES);
        int error = n >= 0 ? SaveBackendKeepSnapshot(path, "pre-cloud", old, (size_t)n, NULL, 0)
                            : SAVE_ERR_CLOUD_BACKUP;
        free(old);
        if (error) return SAVE_ERR_CLOUD_BACKUP;
    }
    return SaveBackendWrite(path, data, (size_t)length);
}
static int CloudInstall(int slot, const char* preferred, const void* data, int length) {
    if (!CloudSaveValid(data, length)) return SAVE_ERR_PAYLOAD;
    int first = 0, tried = 0;
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
        int existed = PlatPathExists(path);
        if (existed < 0) return -existed;
        int error = CloudInstallAt(path, data, length);
        SaveDiag("CLOUD install path=%s bytes=%d error=%d", path, length, error);
        if (!error) { snprintf(g_savePath, sizeof(g_savePath), "%s", path); return 0; }
        /* Existing progress is authoritative on backup/write failure: never
           activate a different cloud copy through a newer fallback candidate. */
        if (existed || error == SAVE_ERR_CLOUD_BACKUP) return SAVE_ERR_CLOUD_BACKUP;
        if (!first) first = error;
    }
    return first ? first : 5;
}
static int CloudKeepConflict(int slot, const char* preferred, const void* data, int length) {
    if (!CloudSaveValid(data, length)) return SAVE_ERR_PAYLOAD;
    int error = SaveBackendKeepSnapshot(preferred, "conflict-cloud", data, (size_t)length, NULL, 0);
    for (int i = 0; error && i < SAVE_CAND_COUNT; i++) {
        char path[MAX_PATH]; SavePathForSlot(i, slot, path, sizeof(path));
        if (path[0] && strcmp(path, preferred))
            error = SaveBackendKeepSnapshot(path, "conflict-cloud", data, (size_t)length, NULL, 0);
    }
    return error;
}
static void CloudDefer(int slot, const char* path, const void* data, int length, int* first) {
    int markerError = CloudBlock(slot);
    /* Snapshot preservation is independent: marker failure must not skip it. */
    int snapshotError = length > 0 ? CloudKeepConflict(slot, path, data, length) : 0;
    CloudRecordError(first, path, markerError);
    CloudRecordError(first, path, snapshotError);
    if (!markerError && !snapshotError) CloudRecordError(first, path, SAVE_ERR_CLOUD_CONFLICT);
}
static void CloudRestoreNew(int slot, const char* path, void* data, int* first) {
    const char* name = CloudName(path);
    if (!SteamZoneCloudExists(name)) {
        CloudRecordError(first, path, CloudAcceptSession(slot, NULL, 0)); return;
    }
    int size = SteamZoneCloudGetSize(name), pulled = 0;
    if (size <= (int)sizeof(SaveEnvelope) || (unsigned)size > SAVE_MAX_FILE_BYTES) {
        CloudRecordError(first, path, SAVE_ERR_CAPACITY); return;
    }
    if (!SteamZoneCloudPullNew(name, data, (int)SAVE_MAX_FILE_BYTES, &pulled)) {
        CloudRecordError(first, path, SAVE_ERR_CLOUD_SYNC); return;
    }
    int error = CloudInstall(slot, path, data, pulled);
    if (!error) error = CloudAcceptSession(slot, data, pulled);
    if (error) CloudDefer(slot, path, data, pulled, first);
    CloudRecordError(first, path, error);
}
void SaveBackendSyncAll(void) {
    int first = 0;
    for (int slot = 0; slot < SAVE_SLOTS; slot++)
        CloudRecordError(&first, "delete-journal", SaveBackendRecoverDelete(slot));
    if (!SteamZoneReady()) {
        if (first) { g_saveState = 2; g_saveError = first; }
        return;
    }
    for (int slot = 0; slot < SAVE_SLOTS; slot++) {
        if (SaveBackendCloudBlocked(slot)) { SaveDiag("CLOUD blocked slot=%d", slot); continue; }
        char path[MAX_PATH]; CloudLocalPath(slot, path, sizeof(path)); if (!path[0]) continue;
        unsigned char* data = malloc(SAVE_MAX_FILE_BYTES);
        if (!data) { CloudRecordError(&first, path, 8); continue; }
        long n = SaveBackendRead(path, data, SAVE_MAX_FILE_BYTES);
        if (n <= (long)sizeof(SaveEnvelope)) {
            int exists = PlatPathExists(path);
            if (!exists) CloudRestoreNew(slot, path, data, &first);
            else CloudRecordError(&first, path, exists < 0 ? -exists : SAVE_ERR_PAYLOAD);
        } else if (!SaveValidateBytes(data, (size_t)n)) {
            CloudRecordError(&first, path, SAVE_ERR_PAYLOAD);
        } else if (CloudSaveValid(data, (int)n)) {
            void* remote = NULL; int remoteLength = 0;
            int result = SteamZoneCloudInspect(CloudName(path), data, (int)n, &remote, &remoteLength);
            if (result == STEAM_SYNC_SAMECL && CloudSaveValid(remote, remoteLength) &&
                remoteLength == n && !memcmp(remote, data, (size_t)n)) {
                int error = CloudAcceptSession(slot, remote, remoteLength);
                if (error) CloudDefer(slot, path, remote, remoteLength, &first);
            } else if (result == STEAM_SYNC_NOCLOUD) {
                CloudRecordError(&first, path, CloudAcceptSession(slot, NULL, 0));
            } else if (result != STEAM_SYNC_ERROR && CloudSaveValid(remote, remoteLength)) {
                /* No automatic timestamp winner after restart. Different branches
                   require a human decision, even if every previous marker write failed. */
                CloudDefer(slot, path, remote, remoteLength, &first);
            } else {
                CloudBlock(slot);
                CloudRecordError(&first, path, result == STEAM_SYNC_ERROR ? SAVE_ERR_CLOUD_SYNC : SAVE_ERR_PAYLOAD);
            }
            free(remote);
        }
        free(data);
    }
    if (first) { g_saveState = 2; g_saveError = first; }
}

/* Resolving is explicit. Marker/tombstone history is archived before unblocking. */
static int CloudClear(int slot) {
    static const char* suffix[] = { ".cloud-block", ".delete-journal" };
    for (int i = 0; i < SAVE_CAND_COUNT; i++) for (int j = 0; j < 2; j++) {
        char base[MAX_PATH], path[MAX_PATH + 32];
        SavePathForSlot(i, slot, base, sizeof(base)); if (!base[0]) continue;
        snprintf(path, sizeof(path), "%s%s", base, suffix[j]);
        int exists = PlatPathExists(path);
        if (exists < 0) return -exists;
        if (!exists) continue;
        unsigned char* old = malloc(SAVE_MAX_FILE_BYTES);
        if (!old) return 8;
        long n = SaveBackendRead(path, old, SAVE_MAX_FILE_BYTES);
        int error = n > 0 ? SaveBackendKeepSnapshot(path, "resolved", old, (size_t)n, NULL, 0)
                          : SAVE_ERR_CLOUD_BACKUP;
        free(old); if (error) return error;
        error = PlatDeletePath(path); if (error < 0) return -error;
    }
    cloudBlocked[slot] = 0; return 0;
}
int SaveBackendResolveCloud(int slot, int choice) {
    if (slot < 0 || slot >= SAVE_SLOTS || (choice != SAVE_CLOUD_USE_LOCAL && choice != SAVE_CLOUD_USE_REMOTE)) return 87;
    int error = SaveBackendRecoverDelete(slot);
    if (error || !SteamZoneReady()) return error ? error : SAVE_ERR_CLOUD_SYNC;
    char path[MAX_PATH]; CloudLocalPath(slot, path, sizeof(path)); if (!path[0]) return 87;
    unsigned char* data = malloc(SAVE_MAX_FILE_BYTES);
    if (!data) return 8;
    const char* name = CloudName(path);
    int agreedLength = 0;
    if (choice == SAVE_CLOUD_USE_REMOTE) {
        int n = 0;
        if (!SteamZoneCloudPullNew(name, data, (int)SAVE_MAX_FILE_BYTES, &n)) error = SAVE_ERR_CLOUD_SYNC;
        else { error = CloudInstall(slot, path, data, n); if (!error) agreedLength = n; }
    } else {
        /* Preserve the latest remote too, even if it changed since the prompt. */
        if (SteamZoneCloudExists(name)) {
            int n = 0;
            if (!SteamZoneCloudPullNew(name, data, (int)SAVE_MAX_FILE_BYTES, &n)) error = SAVE_ERR_CLOUD_SYNC;
            else if (CloudSaveValid(data, n)) error = CloudKeepConflict(slot, path, data, n);
            else error = SaveBackendKeepSnapshot(path, "invalid-cloud", data, (size_t)n, NULL, 0);
        }
        if (!error) {
            long n = SaveBackendRead(path, data, SAVE_MAX_FILE_BYTES);
            if (!CloudSaveValid(data, (int)n)) error = SAVE_ERR_PAYLOAD;
            else if (!SteamZoneCloudUpload(name, data, (int)n)) error = SAVE_ERR_CLOUD_SYNC;
            else agreedLength = (int)n;
        }
    }
    if (!error) error = CloudClear(slot);
    if (!error) error = CloudAcceptSession(slot, data, agreedLength);
    if (error) { CloudBlock(slot); g_saveState = 2; g_saveError = error; }
    else { g_saveState = 1; g_saveError = 0; }
    free(data);
    return error;
}

/* Keep a byte-identical pre-migration copy. Existing backups are never overwritten. */
static int PreserveOriginal(const char *path,const char *suffix) {
    size_t cap=16u*1024u*1024u;
    unsigned char* original=malloc(cap);unsigned char* prior=malloc(cap);
    if(!original || !prior){free(original);free(prior);return 8;}
    long n=SaveBackendRead(path,original,cap);
    if(n<=0){free(original);free(prior);return SAVE_ERR_PAYLOAD;}
    char backup[MAX_PATH+64];unsigned hash=2166136261u;
    for(long i=0;i<n;i++)hash=(hash^original[i])*16777619u;
    snprintf(backup,sizeof(backup),"%s.%s.bak",path,suffix);
    long old=SaveBackendRead(backup,prior,cap);
    if(old>=0 && (old!=n || memcmp(original,prior,(size_t)n))){
        snprintf(backup,sizeof(backup),"%s.%s-%08x.bak",path,suffix,hash);
        old=SaveBackendRead(backup,prior,cap);
    }
    int code=0;
    if(old>=0){if(old!=n || memcmp(original,prior,(size_t)n))code=SAVE_ERR_PAYLOAD;}
    else code=PlatWriteFileExclusive(backup,original,(int)n);
    if(!code){old=SaveBackendRead(backup,prior,cap);if(old!=n || memcmp(original,prior,(size_t)n))code=SAVE_ERR_PAYLOAD;}
    free(original);free(prior);return code;
}

int SaveBackendPreserveVersion(const char* path,int version) {
    char suffix[24];
    snprintf(suffix,sizeof suffix,"v%d",version);
    return PreserveOriginal(path,suffix);
}
int SaveBackendPreserveRetiredStory(const char *path) {
    return PreserveOriginal(path,"retired-story");
}

#ifdef SAVE_BACKEND_TEST
/* Test-only restart model: durable fake files remain while volatile state is lost. */
void SaveBackendTestResetCloudCache(void) {
    memset(cloudBlocked, 0, sizeof(cloudBlocked));
    memset(cloudSessionReady, 0, sizeof(cloudSessionReady));
    for (int slot = 0; slot < SAVE_SLOTS; slot++) {
        free(cloudBase[slot]); cloudBase[slot] = NULL; cloudBaseLength[slot] = 0;
    }
}
#endif

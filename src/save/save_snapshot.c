/* Verified immutable recovery snapshots. */
#include "common.h"
#include "save.h"
#include "save_format.h"
#include "plat.h"

int SaveBackendKeepSnapshot(const char* base, const char* kind, const void* data,
                             size_t length, char* savedPath, size_t pathCapacity) {
    if (!base || !kind || !data || !length || length > SAVE_MAX_FILE_BYTES) return SAVE_ERR_CAPACITY;
    unsigned hash = SaveFnv1a(data, (int)length);
    unsigned char* prior = malloc(length + 1);
    if (!prior) return 8;
    int error = SAVE_ERR_CAPACITY;
    for (int i = 0; i < 64; i++) {
        char path[MAX_PATH + 96];
        int count = i ? snprintf(path, sizeof(path), "%s.%s-%08x-%u-%d.bak", base,
                                  kind, hash, (unsigned)length, i)
                      : snprintf(path, sizeof(path), "%s.%s.bak", base, kind);
        if (count < 0 || (size_t)count >= sizeof(path)) break;
        int exists = PlatPathExists(path);
        if (exists < 0) { error = -exists; break; }
        if (!exists) {
            error = PlatWriteFileExclusive(path, data, (int)length);
            if (error == 80 || error == 183) continue;
            if (error) break;
        }
        long old = SaveBackendRead(path, prior, length + 1);
        if (old != (long)length || memcmp(prior, data, length)) {
            if (exists) continue;
            error = SAVE_ERR_CLOUD_BACKUP;
            break;
        }
        if (savedPath && pathCapacity) snprintf(savedPath, pathCapacity, "%s", path);
        error = 0;
        SaveDiag("SNAPSHOT path=%s bytes=%u", path, (unsigned)length);
        break;
    }
    free(prior);
    return error;
}

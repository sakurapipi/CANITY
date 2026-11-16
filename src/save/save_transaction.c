/* PREPARED journals contain all original bytes. COMMITTED journals are durable
 * local deletion tombstones. Only candidate/suffix indices are serialized. */
#include "common.h"
#include "save.h"
#include "save_format.h"
#include "plat.h"
#define DELETE_MAGIC 0x44545832u
#define DELETE_ENTRIES (SAVE_CAND_COUNT * 3)
enum { DELETE_PREPARED = 1, DELETE_COMMITTED = 2 };
typedef struct { unsigned magic, schema, state, slot, count, bytes, checksum; } DeleteHead;
typedef struct { unsigned candidate, suffix, offset, length; double modified; } DeleteEntry;
#define DELETE_META (sizeof(DeleteHead) + DELETE_ENTRIES * sizeof(DeleteEntry))
static const char* deleteSuffix[] = { "", ".bak", ".tmp" };

static void DeleteTarget(int slot, const DeleteEntry* entry, char* path, size_t cap) {
    char base[MAX_PATH]; SavePathForSlot((int)entry->candidate, slot, base, sizeof(base));
    if (!base[0]) { if (cap) path[0] = 0; return; }
    snprintf(path, cap, "%s%s", base, deleteSuffix[entry->suffix]);
}
static int DeleteJournalValid(const unsigned char* data, long bytes, int slot) {
    if (bytes < (long)DELETE_META || bytes > (long)SAVE_MAX_FILE_BYTES) return 0;
    DeleteHead head; memcpy(&head, data, sizeof(head));
    if (head.magic != DELETE_MAGIC || head.schema != 1 || head.slot != (unsigned)slot ||
        head.count > DELETE_ENTRIES || head.bytes != (unsigned)bytes ||
        (head.state != DELETE_PREPARED && head.state != DELETE_COMMITTED)) return 0;
    unsigned expected = head.checksum; head.checksum = 0;
    if ((SaveFnv1a(data + sizeof(head), (int)bytes - (int)sizeof(head)) ^
         SaveFnv1a(&head, sizeof(head))) != expected) return 0;
    size_t used = DELETE_META;
    for (unsigned i = 0; i < head.count; i++) {
        DeleteEntry entry; memcpy(&entry, data + sizeof(head) + i * sizeof(entry), sizeof(entry));
        if (entry.candidate >= SAVE_CAND_COUNT || entry.suffix >= 3 || entry.offset != used ||
            used > (size_t)bytes || entry.length > (size_t)bytes - used || !isfinite(entry.modified) || entry.modified < 0) return 0;
        for (unsigned j = 0; j < i; j++) {
            DeleteEntry prior; memcpy(&prior, data + sizeof(head) + j * sizeof(prior), sizeof(prior));
            if (entry.candidate == prior.candidate && entry.suffix == prior.suffix) return 0;
        }
        used += entry.length;
    }
    return used == (size_t)bytes;
}
static int RestoreEntry(int slot, const DeleteEntry* entry, const unsigned char* data) {
    char path[MAX_PATH + 8]; DeleteTarget(slot, entry, path, sizeof(path));
    if (!path[0]) return SAVE_ERR_DELETE_TXN;
    int exists = PlatPathExists(path);
    if (exists < 0) return -exists;
    if (!exists) {
        int error = PlatWriteFileExclusive(path, data + entry->offset, (int)entry->length);
        if (error && error != 80 && error != 183) return error;
    }
    /* Existing new content is never overwritten; only byte-identical restored
       snapshots regain their original timestamps, so old .bak cannot win loading. */
    unsigned char* current = malloc((size_t)entry->length + 1);
    if (!current) return 8;
    long length = SaveBackendRead(path, current, (size_t)entry->length + 1);
    int same = length == (long)entry->length && !memcmp(current, data + entry->offset, entry->length);
    free(current);
    if (length < 0) return SAVE_ERR_DELETE_TXN;
    if (!same) return SAVE_ERR_DELETE_CONFLICT;
    if (same && entry->modified > 0 && PlatFileTime(path) != entry->modified)
        return PlatSetFileTime(path, entry->modified);
    return 0;
}
static int RollbackDelete(int slot, const unsigned char* data) {
    DeleteHead head; memcpy(&head, data, sizeof(head));
    int first = 0;
    for (unsigned i = 0; i < head.count; i++) {
        DeleteEntry entry;
        memcpy(&entry, data + sizeof(head) + i * sizeof(entry), sizeof(entry));
        int error = RestoreEntry(slot, &entry, data);
        if (error && !first) first = error;
    }
    return first;
}
static void SealDelete(unsigned char* data, size_t bytes, int slot, unsigned count, unsigned state) {
    DeleteHead head = { DELETE_MAGIC, 1, state, (unsigned)slot, count, (unsigned)bytes, 0 };
    head.checksum = SaveFnv1a(data + sizeof(head), (int)bytes - (int)sizeof(head)) ^
                    SaveFnv1a(&head, sizeof(head));
    memcpy(data, &head, sizeof(head));
}

int SaveBackendRecoverDelete(int slot) {
    if (slot < 0 || slot >= SAVE_SLOTS) return 87;
    unsigned char* data = NULL;
    int first = 0;
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        char base[MAX_PATH], path[MAX_PATH + 32];
        SavePathForSlot(i, slot, base, sizeof(base)); if (!base[0]) continue;
        snprintf(path, sizeof(path), "%s.delete-journal", base);
        int exists = PlatPathExists(path);
        if (!exists) continue;
        if (exists < 0) { first = -exists; break; }
        if (!data) data = malloc(SAVE_MAX_FILE_BYTES);
        if (!data) { first = 8; break; }
        long n = SaveBackendRead(path, data, SAVE_MAX_FILE_BYTES);
        if (!DeleteJournalValid(data, n, slot)) { first = SAVE_ERR_DELETE_TXN; break; }
        DeleteHead head; memcpy(&head, data, sizeof(head));
        if (head.state == DELETE_COMMITTED) continue;
        int error = RollbackDelete(slot, data);
        if (!error) { int removed = PlatDeletePath(path); if (removed < 0) error = -removed; }
        SaveDiag("DELETE recovery slot=%d error=%d", slot, error);
        if (error) { first = error; break; }
    }
    free(data); return first;
}

static int GatherDelete(int slot, unsigned char* data, size_t* total, unsigned* count) {
    char visited[DELETE_ENTRIES][MAX_PATH + 8];
    *total = DELETE_META; *count = 0;
    for (unsigned i = 0; i < SAVE_CAND_COUNT; i++) for (unsigned side = 0; side < 3; side++) {
        DeleteEntry entry = { i, side, (unsigned)*total, 0, 0 };
        char path[MAX_PATH + 8]; DeleteTarget(slot, &entry, path, sizeof(path));
        if (!path[0]) continue;
        int duplicate = 0;
        for (unsigned j = 0; j < *count; j++) if (!strcmp(visited[j], path)) duplicate = 1;
        if (duplicate) continue;
        int exists = PlatPathExists(path);
        if (!exists) continue;
        if (exists < 0) return -exists;
        if (*total >= SAVE_MAX_FILE_BYTES) return SAVE_ERR_CAPACITY;
        long n = SaveBackendRead(path, data + *total, SAVE_MAX_FILE_BYTES - *total);
        if (n < 0) return SAVE_ERR_DELETE_TXN;
        entry.length = (unsigned)n; entry.modified = PlatFileTime(path);
        if (!isfinite(entry.modified) || entry.modified <= 0) return SAVE_ERR_DELETE_TXN;
        memcpy(data + sizeof(DeleteHead) + *count * sizeof(entry), &entry, sizeof(entry));
        snprintf(visited[*count], sizeof(visited[0]), "%s", path);
        (*count)++; *total += (size_t)n;
    }
    return 0;
}
static int PersistDelete(int slot, unsigned char* data, size_t bytes, char* journal, size_t cap) {
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        char base[MAX_PATH]; SavePathForSlot(i, slot, base, sizeof(base)); if (!base[0]) continue;
        snprintf(journal, cap, "%s.delete-journal", base);
        int exists = PlatPathExists(journal);
        if (exists < 0) return -exists;
        if (exists) {
            unsigned char* old = malloc(SAVE_MAX_FILE_BYTES);
            if (!old) return 8;
            long n = SaveBackendRead(journal, old, SAVE_MAX_FILE_BYTES);
            int error = DeleteJournalValid(old, n, slot)
                ? SaveBackendKeepSnapshot(journal, "delete-history", old, (size_t)n, NULL, 0)
                : SAVE_ERR_DELETE_TXN;
            free(old); if (error) return error;
        }
        int error = SaveBackendWrite(journal, data, bytes);
        if (!error) return 0;
    }
    return SAVE_ERR_DELETE_TXN;
}
int SaveBackendDeleteSlot(int slot) {
    int error = SaveBackendRecoverDelete(slot);
    if (error) return error;
    unsigned char* data = calloc(1, SAVE_MAX_FILE_BYTES);
    if (!data) return 8;
    size_t bytes; unsigned count;
    error = GatherDelete(slot, data, &bytes, &count);
    char journal[MAX_PATH + 32] = "";
    if (!error) {
        SealDelete(data, bytes, slot, count, DELETE_PREPARED);
        error = PersistDelete(slot, data, bytes, journal, sizeof(journal));
    }
    if (!error) for (unsigned i = 0; i < count; i++) {
        DeleteEntry entry; char path[MAX_PATH + 8];
        memcpy(&entry, data + sizeof(DeleteHead) + i * sizeof(entry), sizeof(entry));
        DeleteTarget(slot, &entry, path, sizeof(path));
        int result = PlatDeletePath(path);
        if (result < 0) { error = -result; break; }
    }
    if (!error) {
        SealDelete(data, bytes, slot, count, DELETE_COMMITTED);
        error = SaveBackendWrite(journal, data, bytes);
    }
    if (error && journal[0] && PlatPathExists(journal) == 1) {
        int rollback = RollbackDelete(slot, data);
        SaveDiag("DELETE rollback slot=%d error=%d", slot, rollback);
    }
    free(data); return error;
}

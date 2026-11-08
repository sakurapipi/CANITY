/* src/content_db.c —— 内容中枢的实现（2026-11-09）
 *
 * 只做三件事：登记、按序号取、按 id 找。
 * 不涉及渲染、不涉及存档、不涉及 UI —— 保持它足够简单，
 * 才能被所有系统放心依赖。
 */
#include "common.h"
#include "content_db.h"

typedef struct {
    const char* id;      /* 指向各系统自己的字符串常量，中枢不复制 */
    const void* ptr;     /* 内容本体（各系统的结构体指针） */
} ContentSlot;

static ContentSlot s_slots[CK_COUNT][CONTENT_MAX];
static int         s_count[CK_COUNT];

void ContentReset(void) {
    memset(s_slots, 0, sizeof(s_slots));
    memset(s_count, 0, sizeof(s_count));
}

int ContentCount(ContentKind kind) {
    if ((int)kind < 0 || (int)kind >= (int)CK_COUNT) return 0;
    return s_count[kind];
}

const void* ContentGet(ContentKind kind, int index) {
    if ((int)kind < 0 || (int)kind >= (int)CK_COUNT) return NULL;
    if (index < 0 || index >= s_count[kind]) return NULL;
    return s_slots[kind][index].ptr;
}

const char* ContentIdAt(ContentKind kind, int index) {
    if ((int)kind < 0 || (int)kind >= (int)CK_COUNT) return NULL;
    if (index < 0 || index >= s_count[kind]) return NULL;
    return s_slots[kind][index].id;
}

int ContentFind(ContentKind kind, const char* id) {
    int i;
    if ((int)kind < 0 || (int)kind >= (int)CK_COUNT) return -1;
    if (!id || !id[0]) return -1;
    for (i = 0; i < s_count[kind]; i++) {
        const char* have = s_slots[kind][i].id;
        if (have && strcmp(have, id) == 0) return i;
    }
    return -1;
}

int ContentAdd(ContentKind kind, const char* id, const void* ptr) {
    if ((int)kind < 0 || (int)kind >= (int)CK_COUNT) return -1;
    if (!id || !id[0]) return -1;
    /* id 必须唯一 —— 重复登记是"两处各存一份"的典型症状，直接拒绝 */
    if (ContentFind(kind, id) >= 0) return -1;
    if (s_count[kind] >= CONTENT_MAX) return -1;
    s_slots[kind][s_count[kind]].id = id;
    s_slots[kind][s_count[kind]].ptr = ptr;
    s_count[kind]++;
    return s_count[kind] - 1;
}

int ContentDuplicateKind(void) {
    int k, i, j;
    for (k = 0; k < (int)CK_COUNT; k++) {
        for (i = 0; i < s_count[k]; i++) {
            for (j = i + 1; j < s_count[k]; j++) {
                const char* a = s_slots[k][i].id;
                const char* b = s_slots[k][j].id;
                if (a && b && strcmp(a, b) == 0) return k;
            }
        }
    }
    return -1;
}

int ContentTotal(void) {
    int k, n = 0;
    for (k = 0; k < (int)CK_COUNT; k++) n += s_count[k];
    return n;
}

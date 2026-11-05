/*
 * Canity — 存档分区：容器实现（打包 / 解包 / 校验和 / 块表）
 *
 * 纯 C99，不碰系统头、不碰 Win32、不做文件 I/O ——
 * 所以它能被链接进 tests/test_canity.c 做**真单元测试**（见 test_save_container_contract）。
 *
 * 设计不是「一份文件一个版本」，而是「**每个块一个版本**」：
 * 加字段只影响一个块，容器版本不变。详见 include/save_format.h 顶部注释。
 */
#include "common.h"
#include "save_format.h"

/* ------------------------------------------------------------------ */
/* FNV-1a —— 全工程唯一实现（steam_zone.c 也用这个）                    */
/* ------------------------------------------------------------------ */
unsigned int SaveFnv1a(const void* p, int n) {
    const unsigned char* b = (const unsigned char*)p;
    unsigned int h = 2166136261u;
    if (!b || n <= 0) return h;
    for (int i = 0; i < n; i++) {
        h ^= (unsigned int)b[i];
        h *= 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------------ */
/* 信封                                                                */
/* ------------------------------------------------------------------ */
void SaveEnvelopeSeal(SaveEnvelope* h, const void* payload, int n, unsigned int stamp) {
    if (!h) return;
    h->magic   = SAVE_ENVELOPE_MAGIC;
    h->version = SAVE_CONTAINER_VERSION;
    h->stamp   = stamp;
    h->payload = n;
    h->sum     = (payload && n > 0) ? SaveFnv1a(payload, n) : SaveFnv1a("", 0);
}

int SaveEnvelopeValid(const SaveEnvelope* h, const void* payload, int n) {
    if (!h) return 0;
    if (h->magic != SAVE_ENVELOPE_MAGIC) return 0;
    if (h->version != SAVE_CONTAINER_VERSION) return 0;
    /* payload 字段必须与调用方拿到的实际长度一致 —— 这一条挡住
       「文件被截断」和「文件被追加垃圾」两类损坏。 */
    if (h->payload != n) return 0;
    if (n <= 0) return 0;
    if (!payload) return 0;
    return (h->sum == SaveFnv1a(payload, n)) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* 块表：payload 头部 [版本 int][块数 int][保留 int]                    */
/* ------------------------------------------------------------------ */
static void PutInt(void* p, int v)   { memcpy(p, &v, sizeof(int)); }
static int  GetInt(const void* p)    { int v = 0; memcpy(&v, p, sizeof(int)); return v; }

/* 计算 payload 需要多少字节（不写） */
static int ContainerNeed(const SaveChunkDesc* desc, int n) {
    int need = SAVE_PAYLOAD_HEAD_SIZE + n * (int)sizeof(SaveChunkDesc);
    for (int i = 0; i < n; i++) {
        if (need > 0x7FFFFFF0 - (int)desc[i].size) return -1;   /* 溢出保护 */
        need += (int)desc[i].size;
    }
    return need;
}

int SaveContainerPack(const SaveChunkDesc* desc, const void* const* body, int n,
                      void* out, int cap, unsigned int* outSum) {
    if (!desc || !body || !out) return -1;
    if (n < 0 || n > 64) return -1;                 /* 块数上限，防呆 */
    if (outSum) *outSum = 0;

    int need = ContainerNeed(desc, n);
    if (need <= 0 || need > cap) return -1;

    unsigned char* p = (unsigned char*)out;
    PutInt(p + 0,  (int)SAVE_CONTAINER_VERSION);
    PutInt(p + 4,  n);
    PutInt(p + 8,  0);                               /* 保留 */

    SaveChunkDesc* tab = (SaveChunkDesc*)(void*)(p + SAVE_PAYLOAD_HEAD_SIZE);
    for (int i = 0; i < n; i++) tab[i] = desc[i];

    unsigned char* data = p + SAVE_PAYLOAD_HEAD_SIZE + n * (int)sizeof(SaveChunkDesc);
    for (int i = 0; i < n; i++) {
        if (desc[i].size > 0) {
            if (!body[i]) return -1;
            memcpy(data, body[i], (size_t)desc[i].size);
        }
        data += desc[i].size;
    }

    if (outSum) *outSum = SaveFnv1a(out, need);
    return need;
}

/* 校验 payload 头部与块表自洽；返回块数，非法返回 -1。
   注意：**不检查 id 是否认识** —— 未知块是合法数据，要保留。 */
int SaveContainerCount(const void* payload, int n) {
    if (!payload || n < SAVE_PAYLOAD_HEAD_SIZE) return -1;
    const unsigned char* p = (const unsigned char*)payload;

    int ver = GetInt(p + 0);
    int cnt = GetInt(p + 4);
    if (ver != (int)SAVE_CONTAINER_VERSION) return -1;
    if (cnt < 0 || cnt > 64) return -1;

    long long need = (long long)SAVE_PAYLOAD_HEAD_SIZE + (long long)cnt * (long long)sizeof(SaveChunkDesc);
    if (need > (long long)n) return -1;

    /* 逐块累加校验长度，确保没有越界声明 */
    const SaveChunkDesc* tab = (const SaveChunkDesc*)(const void*)(p + SAVE_PAYLOAD_HEAD_SIZE);
    for (int i = 0; i < cnt; i++) {
        need += (long long)tab[i].size;
        if (need > (long long)n) return -1;
    }
    return cnt;
}

int SaveContainerAt(const void* payload, int n, int i,
                    SaveChunkDesc* desc, const void** body) {
    int cnt = SaveContainerCount(payload, n);
    if (cnt < 0 || i < 0 || i >= cnt) return 0;

    const unsigned char* p = (const unsigned char*)payload;
    const SaveChunkDesc* tab = (const SaveChunkDesc*)(const void*)(p + SAVE_PAYLOAD_HEAD_SIZE);

    /* 数据区起点 + 前面所有块的 size */
    const unsigned char* data = p + SAVE_PAYLOAD_HEAD_SIZE + cnt * (int)sizeof(SaveChunkDesc);
    for (int k = 0; k < i; k++) data += tab[k].size;

    if (desc) *desc = tab[i];
    if (body) *body = (tab[i].size > 0) ? (const void*)data : NULL;
    return 1;
}

int SaveContainerFind(const void* payload, int n, unsigned short id,
                      const void** body, int* size) {
    int cnt = SaveContainerCount(payload, n);
    if (cnt < 0) return 0;
    for (int i = 0; i < cnt; i++) {
        SaveChunkDesc d;
        const void* b = NULL;
        if (!SaveContainerAt(payload, n, i, &d, &b)) continue;
        if (d.id != id) continue;
        if (body) *body = b;
        if (size) *size = (int)d.size;
        return 1;
    }
    return 0;
}

/*
 * Canity — 存档层实现：SaveBlob 读写 / 版本迁移 / 继续游戏
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "run_economy.h"
#include "player_tick.h"
#include "wallet.h"
#include "room_theme.h"
#include "hush.h"
#include "loop_private.h"
#include "cell_meta.h"
#include "control_effect.h"
#include "core_rng.h"
#include "difficulty.h"
#include "economy.h"
#include "hatch.h"
#include "item_effects.h"
#include "map_rooms.h"
#include "neutral.h"
#include "phase.h"
#include "run_stats.h"
#include "spider.h"
#include "state.h"
#include "campaign.h"
#include "achievements.h"
#include "save.h"
#include "plat.h"
#include "daily.h"
#include "threat.h"
#include "steam_zone.h"
extern int carryAtRunStart;
#include "bench.h"
#include "upgrade_run.h"
#include "lang.h"
#include "weapons.h"
#include "shop.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "entity.h"
#include "render.h"
#include "skills.h"
#include "bestiary.h"
#include "lockbox.h"   /* v3.0：读档要关锁盘 */

static SaveBlob g_save;

/* ------------------------------------------------------------------ */
/* 存档路径                                                            */
/* ------------------------------------------------------------------ */
/* 之前 SAVE_FILE 是**相对路径**，靠进程的 CWD 决定落点；
   而且只要那个目录对进程只读，fopen 就静默失败 —— 表现是
   "玩了一整局，重开什么都没了"，而且没有任何提示。

   现在给四个候选，写：第一个能写的；读：第一个存在的。
     ① exe 同目录        —— 便携版习惯，玩家能自己看到 / 备份
     ② %APPDATA%\Canity  —— 漫游配置，正常安装该用这个
     ③ %LOCALAPPDATA%\Canity
     ④ %TEMP%
   实测这台机器上 ①② 都被拦（ACCESS_DENIED），所以列表要往多处退。 */
#define SAVE_CAND_COUNT 6   /* v3.0：加 Documents 与 TEMP 子目录兜底 */

int g_saveSlot = 0;   /* v2.0：当前存档槽 0..2 */

static const char* SavePathCandSlot(int i, int slot, char* buf, size_t n);

/* 当前槽的候选路径（内部用） */
static const char* SavePathCand(int i, char* buf, size_t n) {
    return SavePathCandSlot(i, g_saveSlot, buf, n);
}

/* 2026-11-05：导出给存档后端用 —— 云同步要按槽号逐个算路径，
   不能靠临时改 g_saveSlot（那会影响正在进行的对局）。 */
const char* SavePathForSlot(int i, int slot, char* buf, size_t n) {
    return SavePathCandSlot(i, slot, buf, n);
}

/* v2.0：写盘前把旧档复制成 .bak */


static const char* SavePathCandSlot(int i, int slot, char* buf, size_t n) {
    buf[0] = 0;
    const char* isolated = getenv("CANITY_DATA_DIR");
    if (isolated && isolated[0]) {
        snprintf(buf, n, "%s\\canity%d.sav", isolated, slot);
        return buf;
    }
    if (i == 0) {
        /* 2026-11-06（R3）：目录名由平台层给（网页端为空 = 用相对名） */
        PlatExeDir(buf, (int)n);
        {
            char fn[64];
            snprintf(fn, sizeof(fn), "canity%d.sav", slot);
            strncat(buf, fn, n - strlen(buf) - 1);
        }
        return buf;
    }
    const char* base = NULL;
    const char* sub = NULL;
    if (i == 1)      { base = getenv("APPDATA");      sub = "Canity"; }
    else if (i == 2) { base = getenv("LOCALAPPDATA"); sub = "Canity"; }
    else if (i == 3) { base = getenv("TEMP");         sub = NULL;     }
    /* v3.0：**再补两条** —— 实测从某些启动方式下前四个候选全部返回
       ERROR_ACCESS_DENIED(5)，存档一次都没写成功（g_saveState=2）。
       Documents 和 TEMP 子目录是权限最稳的两个位置。 */
    else if (i == 4) { base = getenv("USERPROFILE");  sub = "Documents\\Canity"; }
    else             { base = getenv("TEMP");         sub = "Canity"; }
    if (!base || !*base) base = getenv("TEMP");
    if (!base || !*base) {
        size_t need = strlen(SAVE_FILE) + 1;
        if (n >= need) memcpy(buf, SAVE_FILE, need);
        else if (n) buf[0] = 0;
        return buf;
    }
    if (sub) {
        char dir[MAX_PATH];
        snprintf(dir, sizeof(dir), "%s\\%s", base, sub);
        /* v3.0：sub 里可能带反斜杠（Documents\Canity），逐级建 */
#ifndef CANITY_WEB
        PlatMakeDir(dir);              /* 网页端是空操作 */
#else
        (void)dir;
#endif
        {
            char* slash = strchr(sub, '\\');
            if (slash) {
                char lvl1[MAX_PATH];
                snprintf(lvl1, sizeof(lvl1), "%s\\%.*s", base, (int)(slash - sub), sub);
                PlatMakeDir(lvl1);
            }
        }
        char suffix[32]; snprintf(suffix,sizeof(suffix),"\\canity%d.sav",slot);
        if(strlen(dir)+strlen(suffix)<n) { strcpy(buf,dir); strcat(buf,suffix); }
    } else {
        snprintf(buf, n, "%s\\canity%d.sav", base, slot);
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/* 文件读写：直接走 Win32，不经过 stdio                                 */
/* ------------------------------------------------------------------ */
/* fopen 失败只会给一个 NULL：路径拼错、被权限拦、文件名非法，全都长一个样，
   errno 和 GetLastError 还是两套。存档又属于"失败也没人看得见"的那种，
   结果就是玩一整局、重开全没了。
   这里换成 CreateFileA + WriteFile：失败时能拿到确切的 GetLastError，
   并且把结果记到全局，UI 上可以直接告诉玩家"没存上"。 */
int  g_saveState = 0;          /* 0 还没试过  1 成功  2 失败 */
int  g_saveError = 0;          /* 失败时的 GetLastError */
int  g_saveErrAll[SAVE_CAND_COUNT] = {0};  /* 每个候选的失败码，排查用 */
char g_savePath[MAX_PATH] = "";/* 最近一次写入成功的路径 */

/* Compatibility wrappers delegate all byte I/O to the save backend. */
#include "save_format.h"

static int WriteWholeFile(const char* path, const void* data, size_t n) {
    return SaveBackendWrite(path, data, n);
}
static long ReadWholeFile(const char* path, void* buf, size_t cap) {
    return SaveBackendRead(path, buf, cap);
}

/* ==================================================================
 * 2026-11-05：**存档分区的序列化入口**
 * ------------------------------------------------------------------
 * 旧格式：文件 = SaveBlob 裸字节（magic = SAVE_MAGIC）
 * 新格式：
 *      [SaveEnvelope 20 字节]           ← Steam 专区认的就是这个头
 *      [payload: 块表 + SC_LEGACY29 块] ← SaveBlob 整体，暂时不动字段
 *
 * 为什么要套信封：steam_zone.c 的 SteamZoneCloudSync 要求文件本身
 * 以 SaveEnvelope 开头，否则直接返回 STEAM_SYNC_ERROR。
 * 以前是上传时临时包一层（ReadLocalEnvelope），现在由存档分区在写盘时封好，
 * 于是「本地文件」和「云端对象」是同一份字节。
 *
 * 为什么不现在拆块：本阶段目标是**建立契约 + 不丢老档**。
 * SaveBlob 整体作为 SC_LEGACY29 一块，第二阶段再逐块迁出。
 * ================================================================ */
#define SAVE_BLOB_MAX   ((int)sizeof(SaveBlob) + 4096)

/* ==================================================================
 * 2026-11-06（v2.4-alpha R9-R11）：**未知块原样保留**
 * ------------------------------------------------------------------
 * 旧行为：写盘只写 SC_LEGACY29，读盘只取它。
 * 于是任何新块（将来拆出来的 SC_SETTINGS / SC_WORLD）只要被旧版本
 * 读一次再存一次就**没了** —— 而"块表支持未知块"只是容器层的能力，
 * 不等于游戏完整读写流程会保留它。这是两件事。
 *
 * 现在的契约：
 *   读 → 把**完整的有序块目录**记下来（未知块数据独立拷贝）
 *   写 → 按原顺序重建；SC_LEGACY29 原位替换；未知块原样 memcpy
 *   首次运行 / 裸档升级 → 没有目录，只写一块
 *
 * 为什么存"全部块"而不是"只存未知块"：
 *   只存未知块会丢掉 SC_LEGACY29 在表里的位置。目前没有消费者依赖
 *   顺序，但**没证明过**——所以按原位替换做，代价只是多记一条表项。
 *
 * 重复 ID：默认 REJECT_AND_KEEP_ORIGINAL —— 不写盘、保留原文件、
 *   生成 .dup-id-<hex>.bak、报明确错误。在处理玩家存档时静默挑一份
 *   等于替玩家做决定。
 * ================================================================ */
#define SAVE_CARRY_MAX 64          /* 对齐 SaveContainerPack 的 n<=64 */

typedef struct {
    unsigned short id;
    unsigned short schema;
    unsigned int   size;
    unsigned char* data;           /* SC_LEGACY29 这一项为 NULL（写盘时替换） */
} SaveCarryChunk;

static SaveCarryChunk s_carry[SAVE_CARRY_MAX];
static int            s_carryCount = 0;
static int            s_saveReadOnly = 0;   /* 读档发现结构异常 → 拒绝改写 */

static void SaveCarryClear(void) {
    for (int i = 0; i < s_carryCount; i++) {
        if (s_carry[i].data) { free(s_carry[i].data); s_carry[i].data = NULL; }
    }
    s_carryCount = 0;
}

/* 未知块原始字节总量（容量预算用） */
static int SaveCarryBytes(void) {
    int n = 0;
    for (int i = 0; i < s_carryCount; i++) {
        if (s_carry[i].data) n += (int)s_carry[i].size;
    }
    return n;
}

/* 从 payload 捕获完整有序目录。返回 1 = 成功，<0 = SAVE_ERR_*。
   未知块的数据会**独立拷贝**一份 —— 调用方的 raw 缓冲马上就会被释放。 */
static int SaveCarryCapture(const void* payload, int payLen) {
    SaveCarryClear();
    int cnt = SaveContainerCount(payload, payLen);
    if (cnt < 0) return SAVE_ERR_PAYLOAD;

    /* 重复 ID 检查（包括两块 SC_LEGACY29） */
    for (int i = 0; i < cnt; i++) {
        SaveChunkDesc di;
        if (!SaveContainerAt(payload, payLen, i, &di, NULL)) return SAVE_ERR_PAYLOAD;
        for (int j = 0; j < i; j++) {
            SaveChunkDesc dj;
            if (!SaveContainerAt(payload, payLen, j, &dj, NULL)) return SAVE_ERR_PAYLOAD;
            if (di.id == dj.id) return SAVE_ERR_DUP_CHUNK;
        }
    }
    if (cnt > SAVE_CARRY_MAX) return SAVE_ERR_CAPACITY;

    for (int i = 0; i < cnt; i++) {
        SaveChunkDesc d;
        const void* body = NULL;
        if (!SaveContainerAt(payload, payLen, i, &d, &body)) { SaveCarryClear(); return SAVE_ERR_PAYLOAD; }

        s_carry[i].id = d.id;
        s_carry[i].schema = (unsigned short)d.schema;
        s_carry[i].size = d.size;
        s_carry[i].data = NULL;

        if (d.id == SC_LEGACY29) continue;     /* 写盘时用新的 SaveBlob 替换 */

        if (d.size > 0) {
            if (!body) { SaveCarryClear(); return SAVE_ERR_PAYLOAD; }
            s_carry[i].data = (unsigned char*)malloc((size_t)d.size);
            if (!s_carry[i].data) { SaveCarryClear(); return SAVE_ERR_CAPACITY; }
            memcpy(s_carry[i].data, body, (size_t)d.size);
        }
    }
    s_carryCount = cnt;
    return 1;
}

/* ==================================================================
 * 打包：**唯一的 SaveBlob → 文件字节出口**
 * ------------------------------------------------------------------
 * SaveSettingsOnly 与 SaveGame 保留各自的业务入口（它们改的字段不同），
 * 但序列化/容量计算/封信封只有这一份实现。
 * 原来两处各有一份 static 缓冲 sizeof(SaveBlob)+256 ——
 * 加未知块之后必然不够，而且改一处漏一处就是"设置能存、对局不能存"。
 *
 * 成功返回总字节数，*out 由 malloc 分配（调用方 free）。
 * 失败返回 SAVE_ERR_*（负数）。
 * ================================================================ */
static int SaveFileBytes(const SaveBlob* blob, unsigned char** out) {
    if (!blob || !out) return SAVE_ERR_CAPACITY;
    *out = NULL;
    if (s_saveReadOnly) return SAVE_ERR_DUP_CHUNK;   /* 结构异常，拒绝改写 */

    int nChunks = s_carryCount + 1;                  /* +1 = SC_LEGACY29 */
    if (nChunks > SAVE_CARRY_MAX) return SAVE_ERR_CAPACITY;

    SaveChunkDesc desc[SAVE_CARRY_MAX + 1];
    const void*   body[SAVE_CARRY_MAX + 1];

    int n = 0;
    int hadLegacy = 0;
    for (int i = 0; i < s_carryCount; i++) {
        desc[n].id     = s_carry[i].id;
        desc[n].schema = s_carry[i].schema;
        if (s_carry[i].id == SC_LEGACY29) {
            /* **原位替换**：位置不变，内容换成当前 SaveBlob */
            desc[n].size = (unsigned int)sizeof(SaveBlob);
            body[n] = (const void*)blob;
            hadLegacy = 1;
        } else {
            desc[n].size = s_carry[i].size;
            body[n] = s_carry[i].data;
        }
        n++;
    }
    if (!hadLegacy) {
        /* 首次运行 / 裸档升级：没有原目录，把已知块追加到末尾 */
        desc[n].id = SC_LEGACY29;
        desc[n].schema = 1;
        desc[n].size = (unsigned int)sizeof(SaveBlob);
        body[n] = (const void*)blob;
        n++;
    }

    /* 容量预算：信封 + 块表 + 各块数据 + 安全余量 */
    long need = (long)sizeof(SaveEnvelope) + (long)SAVE_PAYLOAD_HEAD_SIZE
              + (long)n * (long)sizeof(SaveChunkDesc)
              + (long)sizeof(SaveBlob) + (long)SaveCarryBytes() + 128L;
    if (need <= 0 || need > 64L * 1024L * 1024L) return SAVE_ERR_CAPACITY;

    unsigned char* buf = (unsigned char*)malloc((size_t)need);
    if (!buf) return SAVE_ERR_CAPACITY;

    int pay = SaveContainerPack(desc, body, n, buf + sizeof(SaveEnvelope),
                                (int)(need - (long)sizeof(SaveEnvelope)), NULL);
    if (pay <= 0) { free(buf); return SAVE_ERR_CAPACITY; }

    SaveEnvelope h;
    SaveEnvelopeSeal(&h, buf + sizeof(SaveEnvelope), pay, (unsigned int)time(NULL));
    memcpy(buf, &h, sizeof(h));

    *out = buf;
    return (int)sizeof(SaveEnvelope) + pay;
}

/* 从文件字节里取出 SaveBlob。
   返回 1 = 成功（*isEnvelope = 1 表示是新格式），0 = 不是可识别的存档。
   兼容：没有信封的裸档（SAVE_MAGIC）也认，交给老路径继续处理。
   失败时把具体原因写进 g_saveError。 */
static int ExtractSaveFile(const void* buf, long n, SaveBlob* out, int* isEnvelope,
                           int* payloadLen) {
    if (isEnvelope) *isEnvelope = 0;
    if (payloadLen) *payloadLen = 0;
    /* ==================================================================
     * 2026-11-06（v2.4-rc）：**每次读档都重新判定只读**
     * ------------------------------------------------------------------
     * 原来 s_saveReadOnly 一旦被置 1 就**再也不会清零** ——
     * 玩家只要遇到过一次结构异常的存档，这台机器上就再也存不了档
     * （所有写入都返回 SAVE_ERR_DUP_CHUNK）。
     * 这个标志描述的是"当前载入的这份存档可疑"，所以每次读档都要重算。
     * ================================================================ */
    s_saveReadOnly = 0;
    if (!buf || n <= 0 || !out) return 0;

    const unsigned char* p = (const unsigned char*)buf;

    /* ---- 新格式：信封 + 块表 ---- */
    if (n > (long)sizeof(SaveEnvelope)) {
        SaveEnvelope h;
        memcpy(&h, p, sizeof(h));
        if (h.magic == SAVE_ENVELOPE_MAGIC) {
            int payLen = (int)(n - (long)sizeof(SaveEnvelope));
            if (!SaveEnvelopeValid(&h, p + sizeof(h), payLen)) {
                g_saveError = SAVE_ERR_PAYLOAD;
                return 0;                                   /* 校验和不对 = 损坏 */
            }

            /* 先捕获完整目录（含未知块），再取已知块 */
            int cap = SaveCarryCapture(p + sizeof(h), payLen);
            if (cap < 0) {
                SaveCarryClear();
                s_saveReadOnly = 1;                          /* 拒绝后续改写 */
                g_saveError = cap;
                return 0;
            }

            const void* b = NULL;
            int sz = 0;
            if (!SaveContainerFind(p + sizeof(h), payLen, SC_LEGACY29, &b, &sz) || !b || sz <= 0) {
                SaveCarryClear();
                g_saveError = SAVE_ERR_PAYLOAD;
                return 0;
            }
            memset(out, 0, sizeof(*out));
            int copy = (sz < (int)sizeof(*out)) ? sz : (int)sizeof(*out);
            memcpy(out, b, (size_t)copy);
            /* 2026-11-07（B1 / S-04）：**报告真实块长度**，不是 sizeof(SaveBlob)。
               v33 档的 SC_LEGACY29 比当前结构短 16 字节，
               若这里一律返回 sizeof，下游会认为"字段齐全"，
               于是 v34 新增的四项不会被当成"文件里没有"。 */
            if (payloadLen) *payloadLen = sz;
            if (isEnvelope) *isEnvelope = 1;
            g_saveError = 0;
            return 1;
        }
    }

    /* ---- 旧格式：裸 SaveBlob（没有块目录，写盘时按"首次运行"处理） ---- */
    if (n >= (long)sizeof(unsigned int) + (long)sizeof(int)) {
        unsigned int magic = 0;
        memcpy(&magic, p, sizeof(magic));
        if (magic == SAVE_MAGIC) {
            SaveCarryClear();
            int copy = ((int)n < (int)sizeof(*out)) ? (int)n : (int)sizeof(*out);
            memset(out, 0, sizeof(*out));
            memcpy(out, p, (size_t)copy);
            if (payloadLen) *payloadLen = copy;
            g_saveError = 0;
            return 1;
        }
    }
    return 0;
}

/* 读一个存档文件并解出 SaveBlob。
   返回值语义和旧的 ReadWholeFile 保持兼容：
     >0  = **逻辑**文件字节数（信封格式返回 sizeof(SaveBlob)，
           裸档返回真实字节数 —— 后面的前缀迁移全靠这个数）
     -1  = 读不到 / 认不出 */
/* ==================================================================
 * 2026-11-07（v2.4.2 / B1）：**取出真正的旧载荷字节**
 * ------------------------------------------------------------------
 * 缺陷（用真实历史存档实测复现）：
 *   v2.3.9（v29）与 v2.4-rc（v30）的存档是 CSAV 信封格式，
 *   而迁移路径原来是：
 *       ReadWholeFile(usedPath, raw, ...);
 *       LoadLegacyBlob(raw, rn, ...);          // 拿整份文件当裸 blob
 *   前 20 字节是信封 + 后面是块表，LoadLegacyBlob 必然解析失败
 *   -> migrated = 0 -> version 归零 -> 走归档拒绝。
 *   玩家表现：**v2.3.9 / v2.4-rc 的存档升级后读不进来**。
 *
 * 为什么 v27 没坏：v2.3.6 写的是裸档，整份文件正好就是 blob。
 * 为什么 v33/v34 没坏：不在 25..30 的迁移区间，走正常信封解码。
 *
 * 本函数把两种格式统一成「旧载荷字节 + 真实长度」：
 *   信封档 -> SC_LEGACY29 块的字节
 *   裸  档 -> 整份文件字节
 * ================================================================== */
static long ExtractLegacyPayload(const char* path, unsigned char* out, long cap,
                                 int* isEnvelope) {
    unsigned char* raw;
    long n;

    if (isEnvelope) *isEnvelope = 0;
    if (!path || !path[0] || !out || cap <= 0) return 0;

    raw = (unsigned char*)malloc((size_t)cap);
    if (!raw) return 0;
    n = ReadWholeFile(path, raw, (size_t)cap);
    if (n <= 0) { free(raw); return 0; }

    /* ---- 信封档：只取 SC_LEGACY29 块 ---- */
    if (n > (long)sizeof(SaveEnvelope)) {
        SaveEnvelope h;
        memcpy(&h, raw, sizeof(h));
        if (h.magic == SAVE_ENVELOPE_MAGIC) {
            int payLen = (int)(n - (long)sizeof(SaveEnvelope));
            const void* b = NULL;
            int sz = 0;
            if (SaveEnvelopeValid(&h, raw + sizeof(h), payLen) &&
                SaveContainerFind(raw + sizeof(h), payLen, SC_LEGACY29, &b, &sz) &&
                b && sz > 0) {
                long copy = ((long)sz < cap) ? (long)sz : cap;
                memcpy(out, b, (size_t)copy);
                if (isEnvelope) *isEnvelope = 1;
                free(raw);
                return copy;
            }
            free(raw);
            return 0;
        }
    }

    /* ---- 裸档 ---- */
    {
        unsigned int magic = 0;
        if (n >= (long)sizeof(magic)) memcpy(&magic, raw, sizeof(magic));
        if (magic == SAVE_MAGIC) {
            long copy = (n < cap) ? n : cap;
            memcpy(out, raw, (size_t)copy);
            free(raw);
            return copy;
        }
    }

    free(raw);
    return 0;
}

/* 最近一次读档的真实载荷长度（S-04：信封档不能报 sizeof） */
static long s_lastPayloadLen = 0;

static long ReadSaveFile(const char* path, SaveBlob* out) {
    if (!path || !out) return -1;
    size_t cap = sizeof(SaveEnvelope) + sizeof(SaveBlob) + 65536;
    unsigned char* raw = (unsigned char*)malloc(cap);
    if (!raw) return -1;

    long n = ReadWholeFile(path, raw, cap);
    if (n <= 0) { free(raw); return -1; }

    long ret = -1;
    int isEnv = 0;
    int payLen = 0;
    if (ExtractSaveFile(raw, n, out, &isEnv, &payLen)) {
        /* 2026-11-07（B1 / S-04）：返回**真实载荷长度**。
           原来信封档一律返回 sizeof(SaveBlob)，等于宣称"所有字段都在文件里"。
           对 v33 档（载荷比当前结构短 16 字节）这个谎会让 isOldSave 恒假，
           新字段的"缺失默认值"逻辑永远不会触发。
           注：bindings 的重置判据用的是 bindNeed（远小于 v29 载荷），
           所以返回真实长度不会误伤键位表。 */
        ret = (payLen > 0) ? (long)payLen : (long)sizeof(SaveBlob);
        s_lastPayloadLen = ret;
    }
    free(raw);
    return ret;
}

static int SavePathValid(const char* path){
    size_t cap=sizeof(SaveBlob)+sizeof(SaveEnvelope)+65536;
    unsigned char* raw=malloc(cap);if(!raw)return 0;
    long n=ReadWholeFile(path,raw,cap);int ok=0;
    if(n>=8){
        const void* blob=raw;int length=(int)n;unsigned magic=0;memcpy(&magic,raw,4);
        if(magic==SAVE_ENVELOPE_MAGIC && n>(long)sizeof(SaveEnvelope)){
            SaveEnvelope envelope;memcpy(&envelope,raw,sizeof(envelope));
            const void* data=NULL;int size=0;
            if(SaveEnvelopeValid(&envelope,raw+sizeof(envelope),(int)n-(int)sizeof(envelope)) &&
               SaveContainerFind(raw+sizeof(envelope),(int)n-(int)sizeof(envelope),SC_LEGACY29,&data,&size)){
                blob=data;length=size;
                int count=SaveContainerCount(raw+sizeof(envelope),(int)n-(int)sizeof(envelope));
                unsigned short ids[SAVE_CARRY_MAX];
                if(count<1 || count>SAVE_CARRY_MAX)length=0;
                else for(int i=0;i<count;i++){
                    SaveChunkDesc desc;const void* chunk=NULL;
                    if(!SaveContainerAt(raw+sizeof(envelope),(int)n-(int)sizeof(envelope),i,&desc,&chunk)){length=0;break;}
                    for(int j=0;j<i;j++)if(ids[j]==desc.id)length=0;
                    ids[i]=desc.id;
                }
            }else length=0;
        }
        if(length>=1024){int version;memcpy(&magic,blob,4);memcpy(&version,(const char*)blob+4,4);ok=magic==SAVE_MAGIC && version>=25 && version<=SAVE_VERSION;}
    }
    free(raw);return ok;
}
static void BackupSavePath(const char* path){
    if(!SavePathValid(path))return;
    char backup[MAX_PATH+16];snprintf(backup,sizeof(backup),"%s.bak",path);
    PlatCopyPath(path,backup);
}

/* ==================================================================
 * 2026-11-06（R12）：测试钩子 —— 原样转发到真实的 static 实现。
 * 存在意义：让 test_runtime 能用**真正的**捕获/重建逻辑做往返测试，
 * 而不是另写一份测试专用的序列化（那样测不到实际代码）。
 * ================================================================ */
/* 仅测试用：复位"只读"标志。
   生产路径里它由 ExtractSaveFile 每次读档重算；
   测试里没有真实读档，所以需要一个显式复位入口。 */
/* 仅测试用：读回"最近一次载入的 blob"。
   LoadSettings 填的就是它；运行时的 worldMap 要到 ContinueRun 才灌回，
   所以验证迁移结果必须看这份数据。 */
const SaveBlob* SaveTestBlob(void) { return &g_save; }

/* 仅测试用：最近一次读档的**真实载荷长度**（S-04 的验收点）。 */
long SaveTestPayloadLen(void) { return s_lastPayloadLen; }

void SaveTestClearReadOnly(void) {
    s_saveReadOnly = 0;
    SaveCarryClear();
}

int SaveTestSerialize(const SaveBlob* blob, unsigned char** out) {
    return SaveFileBytes(blob, out);
}
int SaveTestDeserialize(const void* buf, long n, SaveBlob* out) {
    int isEnv = 0;
    int ok = ExtractSaveFile(buf, n, out, &isEnv, NULL);
    return ok ? (isEnv ? 2 : 1) : 0;
}

/* 显示 / 音频 / 键位这些设置项是所有存档共用的，
   单独抽出来，训练场里也能只写设置、不动正式进度。 */
static void CopyDisplaySettings(SaveBlob* s) {
    s->resIndex = resIndex;
    s->fullscreen = isFullscreen;
    s->fpsCap = fpsCapIndex;
    s->scale = renderScaleIndex;
    s->dynamicRes = dynamicRes;
    s->fov = fovIndex;
    s->vignette = vignetteOn;
    s->showFps = showFps;
    s->hudDetail = hudDetail;
    s->qteHint = qteHint;
    s->gamma = gammaValue;
    s->sens = mouseSens;
    s->volume = volumeF;
    s->musicVolume = musicVolumeF;
    for (int a = 0; a < BINDING_SLOTS; a++)
        s->bindings[a] = (a < ACT_COUNT) ? g_bindings[a] : 0;
    /* 2026-11-06：在最后一槽写绑定表版本，读档时据此判断要不要补默认键 */
    s->bindings[BINDING_SLOTS - 1] = BINDING_SCHEMA;
    s->lang = g_lang;
    s->saveSlot = g_saveSlot;
    s->bankRiskPlanPlus1=bankRiskPlan+1;
}

/* 训练场里 SaveGame 会整份跳过；但设置是在训练场里改的也得存。
   把当前存档读出来（没有就新建一份空档），只覆盖设置字段。 */
static void SaveSettingsOnly(void) {
    SaveBlob tmp = g_save;
    if (tmp.magic != SAVE_MAGIC || tmp.version != SAVE_VERSION ||
        tmp.saveSlot != g_saveSlot) {
        memset(&tmp, 0, sizeof(tmp));
        tmp.magic = SAVE_MAGIC;
        tmp.version = SAVE_VERSION;
        tmp.gameVersion = CANITY_VERSION_NUM;
        tmp.hasRun = 0;
    }
    CopyDisplaySettings(&tmp);
    /* 训练场不写进度，但「通关过」和「无尽已解锁」是跨局记录，
       不能被一次只写设置的存档覆盖掉。 */
    tmp.gameClearedSave = gameCleared;

    char p[MAX_PATH];
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        SavePathCand(i, p, sizeof(p));
        /* v2.4-alpha R10：走唯一序列化出口，容量按未知块动态算 */
        unsigned char* wbuf = NULL;
        int wn = SaveFileBytes(&tmp, &wbuf);
        if(wn>0)BackupSavePath(p);
        int wok = (wn > 0) && WriteWholeFile(p, wbuf, (size_t)wn) == 0;
        if (wbuf) { free(wbuf); wbuf = NULL; }
        if (wok) {
            g_save = tmp;
            g_saveState = 1; SaveBackendUpload(p);
            g_saveError = 0;
            strncpy(g_savePath, p, sizeof(g_savePath) - 1);
            g_savePath[sizeof(g_savePath) - 1] = 0;
            return;
        }
    }
    g_saveState = 2;
}

/* 训练/教程退出后，菜单要知道「真正的存档里还有没有一局」。
   直接读 g_save.hasRun，而不是沿用训练局强行置上的 hasSave。 */
int SaveHasPendingRun(void) {
    return g_save.hasRun ? 1 : 0;
}

/* Three stable storage regions; serialization must never restore live ranks. */
typedef char save_skill_capacity_check[(SKILL_COUNT <= 41+4+6) ? 1 : -1];
typedef char save_skill_regions_check[(sizeof(g_save.skillRank)/sizeof(int) == 26 &&
    sizeof(g_save.extraSkillRanks)/sizeof(int) == 4 &&
    sizeof(g_save.archetypeRanks)/sizeof(int) >= 11 &&
    sizeof(g_save.metaExpansionRanks)/sizeof(int) == 4 && sizeof(g_save.commonSkillRanks)/sizeof(int) >= SKILL_COUNT-45) ? 1 : -1];
typedef char save_bestiary_regions_check[(sizeof(g_save.bestiaryKnowledge)/sizeof(int) == BESTIARY_LEGACY_VALUES &&
    sizeof(g_save.bestiaryExtraKnowledge)/sizeof(int) == BESTIARY_EXTRA_VALUES) ? 1 : -1];
static void CaptureSkillRanks(void) {
    for (int id = 0; id < SKILL_COUNT; id++) {
        int rank = SkillClampRank(id, skillRank[id]);
        if (id < 26) g_save.skillRank[id] = rank;
        else if (id < 30) g_save.extraSkillRanks[id-26] = rank;
        else if(id<41)g_save.archetypeRanks[id-30] = rank;
        else if(id<45)g_save.metaExpansionRanks[id-41]=rank;
        else g_save.commonSkillRanks[id-45]=rank;
    }
}
static void RestoreSkillRanks(void) {
    for (int id = 0; id < SKILL_COUNT; id++) {
        int rank = id < 26 ? g_save.skillRank[id] :
                   id < 30 ? g_save.extraSkillRanks[id-26] :
                   id < 41 ? g_save.archetypeRanks[id-30] :
                   id < 45 ? g_save.metaExpansionRanks[id-41] : g_save.commonSkillRanks[id-45];
        skillRank[id] = SkillClampRank(id, rank);
    }
    SkillInitOnce();
}

static void CaptureSaveWorld(void) {
    int graduated=g_save.trainingGraduated;
    int archive[4];memcpy(archive,g_save.archivedJumperKnowledge,sizeof(archive));
    memset(&g_save, 0, sizeof(g_save));
    memcpy(g_save.archivedJumperKnowledge,archive,sizeof(archive));
    g_save.trainingGraduated=graduated;g_save.runMadness=RoomThemeMadness();
    g_save.roomEventSpent=roomTheme.spent;
    g_save.restCooldownSave=ShopRestCooldown();g_save.refreshCooldownSave=ShopRefreshCooldown();
    g_save.commonRuntimeMask=HushRuntimeMask();g_save.commonRuntimeFloor=HushRuntimeFloor();
    g_save.magic = SAVE_MAGIC;
    g_save.version = SAVE_VERSION;
    g_save.gameVersion = CANITY_VERSION_NUM;

    CopyDisplaySettings(&g_save);
    g_save.carryGold = carryGold;
    g_save.upgBattery = upgBattery;
    g_save.upgSpeed = upgSpeed;
    g_save.upgQuiet = upgQuiet;
    g_save.upgSanity = upgSanity;
    g_save.upgEco = upgEco;
    g_save.upgRest = upgRest;
    g_save.bestLevel = bestLevel;

    g_save.hasRun = runInProgress;
    g_save.level = currentLevel;
    g_save.sanity = sanity;
    g_save.battery = battery;
    g_save.stamina = stamina;
    g_save.energy = energy;
    g_save.benchOwnedSave = (unsigned)benchOwned;
    g_save.decorV43=1;memcpy(g_save.cellsV43,cellMeta,sizeof(cellMeta));
    g_save.propCountV43=roomPropCount;memcpy(g_save.propsV43,roomProps,sizeof(roomProps));
    g_save.hatchCountV43=hatchCount;memcpy(g_save.hatchesV43,hatches,sizeof(hatches));
    RoomThemeSaveEvent(&g_save.roomEventKindV43,&g_save.roomEventTimeV43);
    CollectorSaveRun(g_save.collectorRunV44);
    MoneyBagsSave(&g_save.moneyBagPositionsV45[0][0],g_save.moneyBagAmountsV45);
    g_save.collectorBountyV45=CollectorActive()?CollectorBounty():0;
    g_save.collectorPositionV45[0]=CollectorX();g_save.collectorPositionV45[1]=CollectorY();
    g_save.counterKillUsedV45=counterKillUsed?1:0;
    g_save.benchOwnedV42=benchOwned;ShopCaptureServices(g_save.serviceUsesV42);
    RunEconomyCapture(&g_save.rewardFloorV42,&g_save.escapeRewardMaskV42,&g_save.rhythmGoldV42);
    g_save.hideSpotSaveCount = hideSpotCount;
    for (int i = 0; i < 6; i++) g_save.hideSpotSave[i] = hideSpots[i];
    g_save.hiddenInSave = hiddenIn;
    /* v3.3：安全屋每层限一次的标记也要存，否则读档后可以同层再免费充一次 */
    {

        g_save.safeHealLvSave    = ShopSafeHealLv();
        g_save.safeChargeLvSave  = ShopSafeChargeLv();
        g_save.safeStaminaLvSave = ShopSafeStaminaLv();
    }
    /* v3.3：跨局累计的成就计数器 */
    {




        g_save.achFloorsNoLight    = floorsNoLight;
        g_save.achFloorsFlawless   = floorsFlawless;
        g_save.achFloorsPacifist   = floorsPacifist;
        g_save.achFloorsFast       = floorsFast;
        g_save.achMapsExplored     = mapsFullyExplored;
        g_save.achFusesRun         = fusesCollectedRun;
        g_save.achLocksOpened      = locksOpened;
        g_save.achHideCount        = hideCount;
        g_save.achTotalThrown      = totalThrown;
        g_save.achCrankSecondsTotal= crankSecondsTotal;
        g_save.achLowSanitySeconds = lowSanitySeconds;
    }
    /* v3.4 银行 */
    {

        g_save.bankGoldSave    = bankGold;
        g_save.bankHistMaxSave = bankHistMax;
        g_save.bankCarryPctSave= bankCarryPct;
        g_save.runPrincipalSave = carryAtRunStart;
        g_save.carryGoldSave   = carryGold;   /* v6：本局本金 */
    }
    /* v7 第 ㊿ 轮：数组是 runUpgSave[5]，原来只存 4 个 → 第 5 项（扳手）永远丢 */
    for (int i = 0; i < 5; i++) g_save.runUpgSave[i] = RunUpgLevel(i);
    /* v29：第 6 条单独存（数组不能加长，见 save.h 的注释） */
    { 
      g_save.runUpgMinimapSave = RunUpgMapDrainLevel(); }
    g_save.flashlightOn = flashlightOn;
    g_save.posX = posX; g_save.posY = posY;
    g_save.dirX = dirX; g_save.dirY = dirY;
    g_save.exitX = exitX; g_save.exitY = exitY;
    g_save.fusesHeld = fusesHeld;
    g_save.fusesNeeded = fusesNeeded;
    g_save.breakerOpen = breakerOpen;
    g_save.breakerActive = breaker.active;
    g_save.breakerX = breaker.x;
    g_save.breakerY = breaker.y;
    g_save.searchedCount = searchedCount;
    g_save.runTime = runTime;
    g_save.nContainers = containerCount;
    for (int i = 0; i < MAX_CONTAINERS; i++) g_save.cont[i] = containers[i];
    memcpy(g_save.worldMap, worldMap, sizeof(worldMap));
    /* v2.4-rc：房间重放种子也进档 */
    {  
      g_save.roomSeed = roomSeedUsed;
    /* 第 6 轮：记下本档用的是哪套随机序列 —— 读档时按它重放房间 */
    {  g_save.rngVersion = rng_version(); }
    /* 第 2 轮：难度存 difficulty+1 —— 老档缺这个字段读到 0，
       明确表示"没设过 → 用标准"，不会误判成轻松。 */
    {  g_save.difficultyPlus1 = difficulty + 1; }
      g_save.roomCount = RoomCount();
      /* v33：舱门已用状态（位掩码，按**种类**记，不记坐标） */
      {  g_save.hatchUsedMask = HatchUsedMask(); } }
    memcpy(g_save.explored, explored, sizeof(explored));

    /* v6 新增字段 */
    g_save.flares = flares;
    g_save.bottles = bottles;
    g_save.safeActive = safeRoom.active;
    g_save.safeX = safeRoom.x;
    g_save.safeY = safeRoom.y;
    g_save.levelMod = levelMod;
    g_save.modSanityMul = modSanityMul;
    g_save.modSpeedMul = modSpeedMul;
    g_save.themeId = themeId;
    g_save.nThrown = 0;
    for (int i = 0; i < MAX_THROWN; i++) {
        g_save.thr[i] = thrown[i];
        if (thrown[i].active) g_save.nThrown++;
    }
}

static void CaptureSaveProgress(void) {
    /* v7 新增：成就系统 */
    for (int i = 0; i < ACH_COUNT; i++) {
        g_save.achUnlocked[i] = g_achievements[i].unlocked;
        g_save.achProgress[i] = g_achievements[i].progress;
    }

    /* v9：投掷物 / 待上缴金币 / 局内技能，以前完全没存 */
    g_save.bricks = bricks;
    g_save.acids = acids;
    g_save.smokeBombs = smokeBombs;
    g_save.carryGold = carryGold;
    g_save.runSkillMask = runSkillMask;
    g_save.lang = g_lang;
    g_save.saveSlot = g_saveSlot;
    /* v13：v2.0 新系统 */
    for (int i = 0; i < RELIC_COUNT; i++) g_save.relicOwned[i] = relicOwned[i];
    g_save.relicTotal = relicTotal;
    g_save.dailyMode = dailyMode;
    /* v7：每日任务进度存盘 */
    { 
      int da[15] = {0};
      DailySaveTo(da);
      g_save.dailyGoalId    = da[0];
      g_save.dailyGoalParam = da[1];
      g_save.dailyProgress  = da[2];
      g_save.dailyDone      = da[3];
      g_save.dailyStreak    = da[4];
      g_save.dailyQuestDate = (unsigned)da[5];
      g_save.dailyLastCompletedDate = (unsigned)da[6];
      for (int j=0;j<8;++j) g_save.dailySamples[j] = da[7+j]; }
    { 



      g_save.knockRunSave=knockCountRun; g_save.throwHitRunSave=throwHitCountRun;
      g_save.echoRunSave=echoCellsTotalRun; g_save.dailyEscapeRunSave=dailyEscapeCount;
      g_save.cleanFloorsSave=floorsNoSanityLoss; g_save.wardenFloorsSave=wardenCleanFloors;
      g_save.fullHideSave=fullHideDone; g_save.doubleHuntSave=doubleHuntSeconds;
      g_save.lastRunGoldSave=lastRunGold; g_save.lastRunKeptSave=lastRunKept; g_save.lastRunCaughtSave=lastRunCaught; }
    /* 2026-11-08：26..29 进老字段，30..33 进新字段（v37 追加） */
    /* v37：幽灵流派 30..33 */
    { 
      g_save.shopRefreshFloorSave=shopRefreshFloor; g_save.shopRefreshUsedSave=shopRefreshUsed;
      g_save.contractKindSave=contractKind; g_save.contractLockedSave=shopContractUsed;
      g_save.threatSave=ThreatValue(); }
    BestiarySave(g_save.bestiaryKnowledge);
    BestiarySaveExtra(g_save.bestiaryExtraKnowledge);
    g_save.dailySeed = dailySeed;
    g_save.campaignMode = campaignMode;
    g_save.campaignStage = campaignStage;
    g_save.firePots = firePots;
    g_save.gluePots = gluePots;
    g_save.mirrors = mirrors;
    /* v34：2026-11-06 —— 这三类投掷物以前完全没进档，读档即消失。
       同一处写齐，避免以后再漏一个。 */
    { 
      g_save.echoBolts        = echoBolts;
      g_save.decoys           = decoys;
      g_save.phaseBeacons     = phaseBeacons;
      g_save.jumperSpawnedRun = jumperSpawnedRun; }
    g_save.bankStreak = bankStreak;
    g_save.modBatteryMul = modBatteryMul;
    g_save.modShardMul = modShardMul;
    g_save.modNoiseMul = modNoiseMul;
    /* v11：武器局外成长 */
    for (int i = 0; i < 3; i++) {
        g_save.weaponRank[i] = weaponRank[i];
        g_save.weaponXp[i] = weaponXp[i];
        g_save.weaponAttach[i][0] = weaponAttach[i][0];
        g_save.weaponAttach[i][1] = weaponAttach[i][1];
    }
    for (int i = 0; i < ATTACH_COUNT; i++) g_save.attachOwned[i] = attachOwned[i];
    g_save.curWeapon = curWeapon;
    g_save.runUltimate = runUltimate;

    CaptureSkillRanks();
    g_save.cores = cores;
    g_save.activeKeystone = activeKeystone;
    g_save.bestMainScore = bestMainScore;
    g_save.seenMonsters = bestiarySeen;

    /* v8 新增：无尽模式进度 */
    g_save.bestEndlessLevel = bestEndlessLevel;
    g_save.bestEndlessScore = bestEndlessScore;
    g_save.isEndlessRunSaved = isEndlessRun;
    /* v28：通关记录必须持久化，否则重启后 Endless 又从菜单里消失 */
    g_save.gameClearedSave = gameCleared;
}

static void WriteSaveCandidates(void) {
    /* 逐个候选试写，第一个成功的就用它 */
    char p[MAX_PATH];
    int firstErr = 0;
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        SavePathCand(i, p, sizeof(p));
        /* v2.4-alpha R10：同一个出口；失败返回 SAVE_ERR_*（负数），
           这里映射成一个非 0 值给下面的错误累积逻辑。 */
        unsigned char* wbuf2 = NULL;
        int wn2 = SaveFileBytes(&g_save, &wbuf2);
        int err;
        if (wn2 > 0) {
            BackupSavePath(p);
            err = WriteWholeFile(p, wbuf2, (size_t)wn2);
        } else {
            err = SAVE_ERR_CAPACITY;
            g_saveError = wn2;
        }
        if (wbuf2) { free(wbuf2); wbuf2 = NULL; }
        if (err == 0) {
            g_saveState = 1; SaveBackendUpload(p);
            g_saveError = 0;
            strncpy(g_savePath, p, sizeof(g_savePath) - 1);
            g_savePath[sizeof(g_savePath) - 1] = 0;
            return;
        }
        if (i == 0) firstErr = err;
        g_saveErrAll[i] = err;
    }
    /* 一个都写不进去：记下 exe 目录那次的错误码 + 路径，UI 会显示出来 */
    g_saveState = 2;
    g_saveError = firstErr;
    SavePathCand(0, p, sizeof(p));
    strncpy(g_savePath, p, sizeof(g_savePath) - 1);
    g_savePath[sizeof(g_savePath) - 1] = 0;
}

void SaveGame(void) {
    /* 训练场不是正式进度：写进去的话 Continue 会读出一张走廊图。
       但设置项仍然要落盘，否则在教程里改完设置重开就白改了。 */
    if (trainingMode) {
        SaveSettingsOnly();
        return;
    }
    /* ==================================================================
     * v28：**在菜单里改设置时，别用空的世界覆盖掉 Continue**
     * ------------------------------------------------------------------
     * SaveGame 会把「运行时世界」整份序列化。但启动后如果玩家还没点
     * Continue，运行时世界根本没加载（currentLevel/地图都是初值），
     * 在菜单里改一下语言、买个技能也会走到这里 —— 原来就会写出一份
     * hasRun=0、地图全空的存档，把可继续的进度毁掉。
     *
     * 结构体里从 hasRun 到 thr[] 正好是**连续的一整段进度字段**，
     * 所以没有活动对局时把这一段从上一份 blob 原样搬回来即可；
     * 大厅/银行/成就/设置仍然按当前运行时写。
     * ================================================================ */
    SaveBlob prev = g_save;
    int keepPendingRun =
        (!runInProgress && prev.magic == SAVE_MAGIC && prev.hasRun &&
         prev.saveSlot == g_saveSlot &&   /* 刚切换存档槽时不要把旧槽的进度带过来 */
         currentState != STATE_PLAYING && currentState != STATE_PAUSED &&
         currentState != STATE_GAMEOVER);
    /* ==================================================================
     * 2026-11-07（v2.4.2 / B2）：**运行段用显式区段表，不再靠一个偏移范围**
     * ------------------------------------------------------------------
     * 原来的写法是
     *     runLen = offsetof(hudDetail) - offsetof(hasRun)
     * 也就是「从 hasRun 到 hudDetail 之间的字节都算运行态」。
     * 问题：这个范围是**隐式**的，任何追加在 hudDetail 之后的运行态字段
     * 都会自动逃出保护，而且没有编译期报错。
     *
     * 实测没被保护的是（偏移来自 offsetof）：
     *     11600  isEndlessRunSaved
     *     12400  roomSeed           ← 房间布局种子
     *     12404  roomCount
     *     12408  hatchUsedMask      ← 哪些舱门用过
     *     12412  echoBolts / decoys / phaseBeacons / jumperSpawnedRun
     * 后果：玩家回主菜单改个设置再 Continue，房间会重新生成、
     *       已用舱门复位、三类投掷物与爆脸怪计数清零。
     * ================================================================ */
    enum { RUN_SPAN_COUNT = 4 };
    typedef struct { size_t off, len; } RunSpan;
    RunSpan runSpans[RUN_SPAN_COUNT];
    runSpans[0].off = offsetof(SaveBlob, hasRun);
    runSpans[0].len = offsetof(SaveBlob, hudDetail) - runSpans[0].off;
    runSpans[1].off = offsetof(SaveBlob, isEndlessRunSaved);
    runSpans[1].len = sizeof(int);
    runSpans[3].off=offsetof(SaveBlob,roomEventSpent);
    runSpans[3].len=sizeof(SaveBlob)-runSpans[3].off;
    runSpans[2].off = offsetof(SaveBlob, roomSeed);
    /* 第 6 轮：末尾多了 rngVersion —— 它也是"运行态"（读档重放要用），
       所以这一段必须把它包进来，否则回主菜单存盘会把它清成 0（旧序列），
       下一次读档房间就重放成另一套。 */
    runSpans[2].len = (offsetof(SaveBlob, rngVersion) + sizeof(int)) - runSpans[2].off;

    /* 编译期守卫：区段表的假设一旦被结构体改动打破，这里会编译失败。
       roomSeed..rngVersion 目前正好是 8 个 int：
         roomSeed / roomCount / hatchUsedMask / echoBolts / decoys /
         phaseBeacons / jumperSpawnedRun
       以后往这段里插字段，长度对不上就会编译失败，逼人重新核对区段表。 */
    typedef char assert_run_block_size[
        (((offsetof(SaveBlob, rngVersion) + (int)sizeof(int))
          - offsetof(SaveBlob, roomSeed)) == 8 * (int)sizeof(int)) ? 1 : -1];
    typedef char assert_run_span0_ends_at_hud[
        (offsetof(SaveBlob, hasRun) < offsetof(SaveBlob, hudDetail)) ? 1 : -1];
    typedef char assert_endless_flag_before_run_block[
        (offsetof(SaveBlob, isEndlessRunSaved) < offsetof(SaveBlob, roomSeed)) ? 1 : -1];
    /* 上面三个 typedef 是编译期断言，故意不引用；显式标明免得 -Wunused 报错。 */
    (void)sizeof(assert_run_block_size);
    (void)sizeof(assert_run_span0_ends_at_hud);
    (void)sizeof(assert_endless_flag_before_run_block);
    CaptureSaveWorld();
    CaptureSaveProgress();
    /* v28：没有活动对局（在菜单里改设置/买东西）→ 进度段沿用上一份存档，
       不要让空的运行时世界把 Continue 覆盖掉。 */
    if (keepPendingRun) {
        int si;
        int graduated=g_save.trainingGraduated;
        for (si = 0; si < RUN_SPAN_COUNT; si++) {
            if (runSpans[si].len == 0) continue;
            memcpy((char*)&g_save + runSpans[si].off,
                   (const char*)&prev + runSpans[si].off,
                   runSpans[si].len);
        }
        g_save.trainingGraduated=graduated;
    }

    WriteSaveCandidates();
}

/* v3.2：选择「最近保存过」的存档槽。
   原来 g_saveSlot 永远从 0 开始，玩家在槽 2 里改的设置/进度
   重启后会去读槽 0，看起来就是「设置没保存」。
   这里扫一遍 3 个槽的主档和 .bak，取修改时间最新的那个。 */
/* 2026-11-06（R4）：修改时间改走平台层（桌面 GetFileAttributesExA，
   网页端 localStorage 里的时间戳）。返回秒，0 = 取不到。 */
static double PathWriteTime(const char* path) {
    return PlatFileTime(path);
}

static int FindNewestSaveSlot(void) {
    int oldSlot = g_saveSlot;
    int bestSlot = -1;
    double bestTime = 0.0;

    for (int slot = 0; slot < SAVE_SLOTS; slot++) {
        g_saveSlot = slot;
        for (int i = 0; i < SAVE_CAND_COUNT; i++) {
            char p[MAX_PATH];
            SavePathCand(i, p, sizeof(p));
            for (int bak = 0; bak < 2; bak++) {
                char path[MAX_PATH];
                if (bak) { if(strlen(p)+4>=sizeof(path)) continue; strcpy(path,p); strcat(path,".bak"); }
                else     snprintf(path, sizeof(path), "%s", p);
                double ft = PathWriteTime(path);
                if (ft <= 0.0) continue;
                if (bestSlot < 0 || ft > bestTime) {
                    bestSlot = slot;
                    bestTime = ft;
                }
            }
        }
    }

    g_saveSlot = (bestSlot >= 0) ? bestSlot : oldSlot;
    return bestSlot;
}

/* 只读设置（启动时），返回是否还有可继续的进度 */
/* ===== v6 **银行状态恢复（LoadSettings / ContinueRun 共用）** =====
   原来只在 ContinueRun 里恢复，但 SaveGame 无条件写 bankGold 等字段。
   后果：启动后不进 Continue（直接看 BANK 页、改设置、或关窗口）就触发
        SaveGame，把存款覆盖成 0 —— 玩家全部存款静默消失。
   现在启动读设置时也恢复，写回去的值就是对的。 */
static void RestoreBankFromSave(void) {

    int rawGold = g_save.bankGoldSave;
    int rawHist = g_save.bankHistMaxSave;
    int rawPct  = g_save.bankCarryPctSave;

    bankGold     = rawGold > 0 ? rawGold : 0;
    bankHistMax  = rawHist > 0 ? rawHist : 1;
    bankCarryPct = rawPct;
    bankRiskPlan=g_save.bankRiskPlanPlus1>=1 && g_save.bankRiskPlanPlus1<=4?g_save.bankRiskPlanPlus1-1:1;

    /* Supported profiles store an explicit carry choice: zero means zero. */
    if (bankCarryPct < 0) bankCarryPct = 0;
    if (bankCarryPct >= BANK_CARRY_STEPS) bankCarryPct = BANK_CARRY_STEPS - 1;
    if (bankHistMax < bankGold) bankHistMax = bankGold;
    if (bankHistMax < 1) bankHistMax = 1;

    /* v6：本局本金 —— 老档没有这个字段（读出 0），此时按当前金币兜底，
       保证 profit = carryGold - carryGold 不会把全部余额当成利润。 */
    carryGold = g_save.carryGoldSave;
    carryAtRunStart = g_save.runPrincipalSave;
    /* v7 第 65 轮：**这里原来是一句自比较**
       `if (carryGold > carryGold) carryGold = carryGold;`
       —— 局部 extern 把全局自己遮住，条件永远为假（编译器 -Wtautological-compare 直接报）。
       本意应该是「夹到合法范围」，负值已经在上一条处理掉了，这句删掉即可。 */
    {  if (carryGold < 0) carryGold = 0; }
}

/* ==================================================================
 * v24 存档布局（2.3.3 及更早）
 * ------------------------------------------------------------------
 * v26 重建了成就表（40 → 32）、技能树（20 → 26 + 4 个额外），
 * 还删掉了 pendingShards，所以 v24 的字节**不能**直接当新结构读。
 * 这里保留一份旧结构，只在版本号和文件长度都精确匹配时启用迁移。
 * ================================================================ */
typedef struct {
    unsigned int magic;
    int version;
    int gameVersion;
    int resIndex, fullscreen, fpsCap, scale, fov, vignette, showFps;
    float gamma, sens;
    int shards, upgBattery, upgSpeed, upgQuiet, upgSanity;
    int bestLevel;
    float volume;
    int hasRun, level, fusesHeld, fusesNeeded, breakerOpen, breakerActive, searchedCount, nContainers;
    int flashlightOn;
    float sanity, battery, stamina, posX, posY, dirX, dirY, exitX, exitY, breakerX, breakerY, runTime;
    Container cont[MAX_CONTAINERS];
    int worldMap[MAP_HEIGHT][MAP_WIDTH];
    int explored[MAP_HEIGHT][MAP_WIDTH];
    int flares, bottles;
    int safeActive;
    float safeX, safeY;
    int levelMod;
    float modSanityMul, modSpeedMul;
    int themeId;
    int nThrown;
    Thrown thr[MAX_THROWN];
    int hudDetail, qteHint;
    int bindings[BINDING_SLOTS];
    int upgEco, upgRest;
    int achUnlocked[40];
    int achProgress[40];
    int bestEndlessLevel, bestEndlessScore, isEndlessRunSaved;
    int skillRank[20];
    int cores, activeKeystone, bestMainScore, seenMonsters;
    int bricks, acids, smokeBombs;
    int pendingShards;
    unsigned int runSkillMask;
    int lang, saveSlot;
    int weaponRank[3], weaponXp[3];
    int relicOwned[RELIC_COUNT], relicTotal;
    int dailyMode, dailySeed;
    int campaignMode, campaignStage;
    int firePots, gluePots, mirrors;
    int bankStreak;
    float modBatteryMul, modShardMul, modNoiseMul;
    int weaponAttach[3][2];
    int attachOwned[10];
    int curWeapon;
    int runUltimate;
    float energy;
    unsigned int benchOwnedSave;
    int runUpgSave[5];
    int hideSpotSaveCount;
    HideSpot hideSpotSave[6];
    int hiddenInSave;
    int safeHealLvSave, safeChargeLvSave, safeStaminaLvSave;
    int achFloorsNoLight, achFloorsFlawless, achFloorsPacifist, achFloorsFast;
    int achMapsExplored, achFusesRun, achLocksOpened;
    int achHideCount, achTotalThrown;
    float achCrankSecondsTotal, achLowSanitySeconds;
    int bankGoldSave, bankHistMaxSave, bankCarryPctSave, carryGoldSave;
} LegacySaveV24;

/* 迁移只保留能一一对应的字段：
   · 成就表从 40 条重建为 32 条，旧 ID 不保证对应 → 清零，避免误弹成就；
   · 技能树前 20 项按位置保留（新树 20..29 级从 0 开始）；
   · 其余大厅进度、金币、银行、遗物、藏身点原样带走。 */
/* ==================================================================
 * 2026-11-06：**通用老存档重排**
 * ------------------------------------------------------------------
 * 两次布局变更叠在一起，所以做成一个参数化函数：
 *   v25 ~ v28 : Container 32 字节、地图 24x24
 *   v29 ~ v30 : Container 40 字节、地图 24x24
 *   v31       : Container 40 字节、地图 36x36   ← 当前
 *
 * 只靠 memcpy 是**不行**的，三处都要重排：
 *   ① cont[]     —— 每条 32 -> 40 字节（v29 加了 mimicShell/mimicHost）
 *   ② worldMap   —— 24 宽 -> 36 宽，**行宽变了，必须逐格重排**
 *   ③ explored   —— 同上
 *   后面跟着的尾部（flares 之后）整体后移。
 *
 * 为什么第 ② 步不能 memcpy：老档是 y*24+x 的行主序，
 * 新结构是 y*36+x。直接拷会把第 1 行的后半截读成第 0 行的一部分。
 *
 * 返回 1 = 成功；0 = 参数不合理或文件太小。
 * ================================================================ */
#define LEGACY_CONTAINER_BYTES 32      /* v28 及以前的 Container 大小 */
/* 2026-11-06：v31 及以前的槽位数。MAX_CONTAINERS 8 -> 12 之后，
   老档里仍然只有 8 条记录 —— 迁移必须知道这个数，不能读新常量。 */
#define LEGACY_CONTAINER_COUNT 8
#define LEGACY_MAP_W 24
#define LEGACY_MAP_H 24

typedef char container_must_be_40[(sizeof(Container) == 40) ? 1 : -1];
typedef char map_must_be_at_least_24[((MAP_WIDTH >= 24) && (MAP_HEIGHT >= 24)) ? 1 : -1];

/* 计算"老布局"各段的偏移与长度。返回 0 = 不合理。 */
static int LegacySpan(int containerBytes, int containerCount, int mapW, int mapH,
                      size_t* preLen, size_t* oldContSz, size_t* oldMapSz,
                      size_t* oldExplSz, size_t* oldTailOff) {
    if (containerBytes != 32 && containerBytes != (int)sizeof(Container)) return 0;
    if (containerCount <= 0 || containerCount > MAX_CONTAINERS) return 0;
    if (mapW <= 0 || mapH <= 0 || mapW > MAP_WIDTH || mapH > MAP_HEIGHT) return 0;

    size_t pre = offsetof(SaveBlob, cont);
    size_t contSz = (size_t)containerBytes * (size_t)containerCount;
    size_t mapSz  = (size_t)mapW * (size_t)mapH * sizeof(int);
    size_t explSz = mapSz;

    if (preLen)     *preLen = pre;
    if (oldContSz)  *oldContSz = contSz;
    if (oldMapSz)   *oldMapSz = mapSz;
    if (oldExplSz)  *oldExplSz = explSz;
    if (oldTailOff) *oldTailOff = pre + contSz + mapSz + explSz;
    return 1;
}

static int LoadLegacyBlob(const unsigned char* src, size_t size,
                          int containerBytes, int containerCount,
                          int mapW, int mapH, SaveBlob* out) {
    size_t preLen = 0, oldContSz = 0, oldMapSz = 0, oldExplSz = 0, oldTailOff = 0;
    if (!src || !out || size < 16) return 0;
    if (!LegacySpan(containerBytes, containerCount, mapW, mapH,
                    &preLen, &oldContSz, &oldMapSz, &oldExplSz, &oldTailOff)) return 0;

    memset(out, 0, sizeof(*out));

    /* ---- ① 前缀（cont 之前两种布局完全相同） ---- */
    memcpy(out, src, (preLen < size) ? preLen : size);

    /* ---- ② cont[] 逐字段展开 ---- */
    {
        size_t off = preLen;
        for (int i = 0; i < containerCount && i < MAX_CONTAINERS; i++) {
            if (off + (size_t)containerBytes > size) break;
            const unsigned char* p = src + off;
            if (containerBytes == (int)sizeof(Container)) {
                memcpy(&out->cont[i], p, sizeof(Container));
            } else {
                memcpy(&out->cont[i].x,        p + 0,  sizeof(float));
                memcpy(&out->cont[i].y,        p + 4,  sizeof(float));
                memcpy(&out->cont[i].active,   p + 8,  sizeof(int));
                memcpy(&out->cont[i].searched, p + 12, sizeof(int));
                memcpy(&out->cont[i].content,  p + 16, sizeof(int));
                memcpy(&out->cont[i].locked,   p + 20, sizeof(int));
                memcpy(&out->cont[i].lockType, p + 24, sizeof(int));
                memcpy(&out->cont[i].lockCode, p + 28, sizeof(int));
                out->cont[i].mimicShell = 0;
                out->cont[i].mimicHost  = 0;
            }
            off += (size_t)containerBytes;
        }
    }

    /* ---- ③ worldMap：24 宽 -> 36 宽，逐格重排 ---- */
    {
        size_t oldMapOff = preLen + oldContSz;
        size_t newMapOff = offsetof(SaveBlob, worldMap);
        if (oldMapOff != newMapOff && oldMapSz != 0) {
            /* 偏移不同也没关系：我们从 src 的 oldMapOff 读、写到结构里的 worldMap */
        }
        for (int y = 0; y < mapH; y++) {
            for (int x = 0; x < mapW; x++) {
                size_t o = oldMapOff + ((size_t)y * (size_t)mapW + (size_t)x) * sizeof(int);
                if (o + sizeof(int) > size) continue;
                int v = 0;
                memcpy(&v, src + o, sizeof(int));
                out->worldMap[y][x] = v;
            }
        }
        /* 新地图多出来的区域：一律当作**墙** —— 老档没有那些格子，
           给 0 会造出一片穿越地图边界的空地。 */
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) {
                if (x < mapW && y < mapH) continue;
                out->worldMap[y][x] = 1;
            }
    }

    /* ---- ④ explored：同样逐格重排，多出来的留 0（未探索） ---- */
    {
        size_t oldExplOff = preLen + oldContSz + oldMapSz;
        for (int y = 0; y < mapH; y++)
            for (int x = 0; x < mapW; x++) {
                size_t o = oldExplOff + ((size_t)y * (size_t)mapW + (size_t)x) * sizeof(int);
                if (o + sizeof(int) > size) continue;
                int v = 0;
                memcpy(&v, src + o, sizeof(int));
                out->explored[y][x] = v;
            }
    }

    /* ---- ⑤ 尾部整体后移 ---- */
    {
        size_t newTailOff = offsetof(SaveBlob, flares);
        if (size > oldTailOff) {
            size_t n = size - oldTailOff;
            size_t cap = sizeof(SaveBlob) - newTailOff;
            if (n > cap) n = cap;
            memcpy((unsigned char*)out + newTailOff, src + oldTailOff, n);
        }
    }
    return 1;
}

/* 迁移之后，"当前布局里有多少字节是有效的" */
static size_t LegacyValidBytes(size_t oldSize, int containerBytes, int containerCount,
                               int mapW, int mapH) {
    size_t oldTailOff = 0;
    if (!LegacySpan(containerBytes, containerCount, mapW, mapH,
                    NULL, NULL, NULL, NULL, &oldTailOff)) return 0;
    size_t newTailOff = offsetof(SaveBlob, flares);
    size_t tail = (oldSize > oldTailOff) ? (oldSize - oldTailOff) : 0;
    size_t v = newTailOff + tail;
    if (v > sizeof(SaveBlob)) v = sizeof(SaveBlob);
    return v;
}

typedef char legacySave24_must_fit[(sizeof(LegacySaveV24) <= sizeof(SaveBlob)) ? 1 : -1];

typedef struct {
    size_t fileSize;
    int hasMusicField, hasDynamicResField, hasGameClearedField;
    char usedPath[MAX_PATH];
} LoadedSaveInfo;

static int forcedSaveSlot=-1;
static int ReadSettingsCandidate(LoadedSaveInfo* info) {
    char p[MAX_PATH],best[MAX_PATH]={0};double newest=-1;
    memset(&g_save,0,sizeof(g_save));
    if(forcedSaveSlot>=0)g_saveSlot=forcedSaveSlot;else FindNewestSaveSlot();
    for(int i=0;i<SAVE_CAND_COUNT;i++){
        SavePathCand(i,p,sizeof(p));
        for(int backup=0;backup<2;backup++){
            char path[MAX_PATH];if(backup){if(strlen(p)+4>=sizeof(path))continue;snprintf(path,sizeof(path),"%s.bak",p);}else snprintf(path,sizeof(path),"%s",p);
            if(!SavePathValid(path))continue;
            double time=PathWriteTime(path);
            if(!best[0] || time>newest){newest=time;snprintf(best,sizeof(best),"%s",path);}
        }
    }
    if(!best[0]){if(PlatSystemLanguageZh())g_lang=LANG_ZH;return 0;}
    long size=ReadSaveFile(best,&g_save);if(size<=0)return 0;
    info->fileSize=(size_t)size;snprintf(info->usedPath,sizeof(info->usedPath),"%s",best);
    return 1;
}

static int MigrateSettingsCandidate(LoadedSaveInfo* info) {
    int originalVersion=g_save.version;
    if((originalVersion>=25 && originalVersion<=30) || (originalVersion>=31 && originalVersion<SAVE_VERSION)){
        int error=SaveBackendPreserveVersion(info->usedPath,originalVersion);
        if(error){g_saveState=2;g_saveError=error;s_saveReadOnly=1;return 0;}
    }
    /* v24（2.3.3 及更早）的布局和现在不同：成就 40→32、技能树 20→26、
       还删了 pendingShards。只在版本号和文件长度都精确匹配时迁移，
       对不上就继续走“归档不解释”的安全路径。 */
    /* 2026-11-06（P0-1）：**v24 的迁移本身是坏的，先停用**。
       LegacySaveV24 定义时用的是**新版** Container（40 字节），
       所以 `info->fileSize == sizeof(LegacySaveV24)` 对真实的 v24 文件
       永远不成立；即使凑巧成立，那段 memcpy 也会把 cont[] 之后
       整段读错。与其用错误的迁移静默毁档，不如归档不解释 ——
       数据还在盘上（.v24.bak），可以人工回滚。 */
    if (g_save.version == 24) {
        (void)sizeof(LegacySaveV24);   /* 结构保留，仅为文档与 static assert */
    }
    /* v25 / v26 / v27：新字段都追加在结构末尾，前缀可以直接读。
       v25 缺 runPrincipalSave，用本局本金兜底（和原来的迁移一致）；
       v28 新增的 Dynamic Res / 通关记录由下面的 has*Field 判断补默认值。 */
    /* ==================================================================
     * 2026-11-06：v25 ~ v30 都需要**重排**，不只是改版本号
     * ------------------------------------------------------------------
     *   v25~v28 : Container 32 字节 + 地图 24x24
     *   v29~v30 : Container 40 字节 + 地图 24x24
     *   v31     : Container 40 字节 + 地图 36x36   ← 当前
     *
     * 旧代码只把 version 改成 SAVE_VERSION 就放行 —— 那就是 P0-1。
     * ================================================================ */
    if ((g_save.version >= 25 && g_save.version <= 30) || g_save.version == 31) {
        int legacyCont = (g_save.version <= 28) ? 32 : (int)sizeof(Container);
        int oldWidth=g_save.version==31?MAP_WIDTH:LEGACY_MAP_W;
        int oldHeight=g_save.version==31?MAP_HEIGHT:LEGACY_MAP_H;
        /* ================================================================
         * P0-1：**这里是真正的修复点**
         * ----------------------------------------------------------------
         * 旧代码只是把 version 改成 SAVE_VERSION 就放行 —— 但老文件里的
         * Container 是 32 字节，`cont[8]` 之后的每一字节都错位 64。
         * 现在重新读原始字节，按老布局重排。
         * ================================================================ */
        unsigned char* raw = (unsigned char*)malloc(sizeof(SaveBlob) + 4096);
        int migrated = 0;
        /* Original bytes were preserved before entering this migration. */
        if (raw && info->usedPath[0]) {
            long rn = ExtractLegacyPayload(info->usedPath, raw, (long)sizeof(SaveBlob) + 4096, NULL);
            if (rn > 0) {
                migrated = LoadLegacyBlob(raw, (size_t)rn, legacyCont,
                                          LEGACY_CONTAINER_COUNT,
                                          oldWidth, oldHeight, &g_save);
                if (migrated)
                    info->fileSize = LegacyValidBytes((size_t)rn, legacyCont,
                                                LEGACY_CONTAINER_COUNT,
                                                oldWidth, oldHeight);
            }
        }
        if (raw) free(raw);
        if (!migrated) {
            /* 重排失败：不解释，走下面的归档路径 */
            g_save.version = 0;
        } else {
            if (info->fileSize < sizeof(g_save))
                memset((char*)&g_save + info->fileSize, 0, sizeof(g_save) - info->fileSize);
            g_save.version = SAVE_VERSION;
            if (g_save.runPrincipalSave == 0) g_save.runPrincipalSave = g_save.carryGoldSave;
        }
    }
    /* ==================================================================
     * 2026-11-06（v2.4.1 / v34）：**v33 -> v34 只需要放行**
     * ------------------------------------------------------------------
     * v34 只在结构体**末尾**追加了 4 个 int，布局没有任何重排：
     *   echoBolts / decoys / phaseBeacons / jumperSpawnedRun
     * 所以 v33 的老档（比现在短 16 字节）按前缀读进来之后，
     * 这 4 项本来就已经是 0（LoadSettings 开头 memset 过整份 g_save）。
     *
     * 但**必须在这里把版本号改掉** —— 下面那句
     *   if (g_save.version != SAVE_VERSION) { 归档并 return 0; }
     * 会直接拒档。不做这一步，玩家一更新版本就丢整局进度。
     * 这正是"新增字段追加在末尾"还需要配套放行的原因。
     * ================================================================ */
    if (g_save.version == 31 || g_save.version == 32 || g_save.version == 33) {
        g_save.version = SAVE_VERSION;
    }
    /* ==================================================================
     * 2026-11-08（v35）：**v34 -> v35 只需要放行**
     * ------------------------------------------------------------------
     * v35 只在结构体末尾追加了一个 int（rngVersion），布局一点没动：
     *   读进来的 v34 档比现在短 4 字节，memset 过之后 rngVersion 自然是 0，
     *   而 0 的语义正好是"旧 rand 序列" —— 房间重放与玩家当年看到的一致。
     *
     * 所以这里不改任何字段，只把版本号改掉放行。
     * 不做这一步，下面那句 != SAVE_VERSION 会把整个 v34 档归档拒掉。
     * ================================================================ */
    if (g_save.version == 34) {
        g_save.version = SAVE_VERSION;
    }
    /* 2026-11-08（v36）：v35 -> v36 只需要放行 —— 末尾多一个 int
       （difficultyPlus1），布局没重排，读到 0 就是"没设过难度"。 */
    if (g_save.version == 35) {
        g_save.version = SAVE_VERSION;
    }
    /* 2026-11-08（v37）：末尾多 ghostSkillRanks[4]，布局没重排 —— 放行即可。
       v36 档比现在短 16 字节，memset 过之后该字段是 0（= 没买过）。 */
    if (g_save.version == 36) {
        g_save.version = SAVE_VERSION;
    }
    /* v38 appends only mimic knowledge; the zero-filled tail preserves v37 reads. */
    /* Valid user display choices survive migration; normal validation clamps invalid values. */
    if (g_save.version == 37 || g_save.version == 38 || g_save.version == 39 || g_save.version == 40 || g_save.version == 41 || g_save.version == 42 || g_save.version == 43 || g_save.version == 44) g_save.version = SAVE_VERSION;
    if(originalVersion>=25 && originalVersion<40){
        g_save.archivedJumperKnowledge[0]=(g_save.seenMonsters>>2)&1;
        for(int i=0;i<3;i++){g_save.archivedJumperKnowledge[i+1]=g_save.bestiaryKnowledge[i*6+2];g_save.bestiaryKnowledge[i*6+2]=0;}
        g_save.seenMonsters&=~(1<<2);
        /* A historical face-jumper encounter cannot unlock the new spider. */
    }
    if (g_save.version != SAVE_VERSION) {
        char oldp[MAX_PATH], arc[MAX_PATH + 24];   /* v3.0：留 ".v%d.bak" 的位置 */
        SavePathCand(0, oldp, sizeof(oldp));
        if (oldp[0]) {
            snprintf(arc, sizeof(arc), "%s.v%d.bak", oldp, g_save.version);
            PlatRenamePath(oldp, arc);
        }
        return 0;
    }
    return 1;
}

static void ApplyLoadedSettings(const LoadedSaveInfo* info) {
    /* 旧档没有音乐音量字段：在下面读到 volumeF 之后回退到音效音量。 */
    if (!info->hasMusicField) g_save.musicVolume = 0.0f;
    int isOldSave = (info->fileSize < sizeof(SaveBlob));

    /* 迁移：老存档缺的字段给默认值。
       v5 及更早没有键位表和 ECO/REST 升级 —— 键位取出厂默认（比
       全 0 好得多：全 0 意味着「没有绑定」，游戏直接没法玩），
       新升级从 0 级开始。 */
    if (isOldSave) {
        /* v6 之前没有 bindings/upgEco/upgRest 字段。
           判据：文件没包含**完整的** bindings 数组（老 v5 文件的长度
           正好等于 bindings 的偏移量，不能用严格小于偏移量来判断）。 */
        size_t bindOff  = (size_t)((char*)&g_save.bindings[0] - (char*)&g_save);
        size_t bindNeed = bindOff + sizeof(g_save.bindings);
        if (info->fileSize < bindNeed) {
            static const int defBindings[ACT_COUNT] = BINDINGS_DEFAULT;
            for (int a = 0; a < ACT_COUNT; a++) g_save.bindings[a] = defBindings[a];
            g_save.upgEco = 0;
            g_save.upgRest = 0;
        }
        if (g_save.version < 5) {
            g_save.hudDetail = 1;    /* Normal */
            g_save.qteHint = 1;      /* Full */
        }
    }
    /* 边界检查：存档可能被外部改坏，数值必须夹回合法范围 */
    if (g_save.hudDetail < 0 || g_save.hudDetail > 2) g_save.hudDetail = 1;
    if (g_save.qteHint < 0 || g_save.qteHint > 1) g_save.qteHint = 1;

    /* 注意这跟 gameVersion 是两回事 ——
       gameVersion 只是记录「这份存档是哪个应用版本写的」，
       升个补丁版本（比如 1.0.0 -> 1.0.1）不该让玩家丢进度，
       所以这里**不**拿它做加载门槛，只有 SAVE_VERSION 才是。 */

    resIndex = g_save.resIndex;
    isFullscreen = g_save.fullscreen;
    fpsCapIndex = g_save.fpsCap;
    renderScaleIndex = g_save.scale;
    /* v28：Dynamic Res 以前完全没有进存档，重启后总是回到默认开。 */
    dynamicRes = info->hasDynamicResField ? g_save.dynamicRes : 1;
    if (dynamicRes < 0 || dynamicRes > 1) dynamicRes = 1;
    fovIndex = g_save.fov;
    vignetteOn = g_save.vignette;
    showFps = g_save.showFps;
    hudDetail = g_save.hudDetail;
    qteHint = g_save.qteHint;
    /* ==================================================================
     * 第 2 轮：**难度是设置，读档时就该立刻生效**
     * ------------------------------------------------------------------
     * 一开始放到 ContinueRun 的房间重放块里 —— 结果"不继续对局"时
     * 难度就不还原（玩家的选择被静默丢弃）。设置项必须和 hudDetail
     * 一样，在 LoadSettings 里当场还原。
     *
     * 存的是 difficulty+1：0 = 老档（没设过）→ 标准，不是轻松。
     * ================================================================ */
    { 
      int dv = g_save.difficultyPlus1;
      difficulty = (dv >= 1 && dv <= 3) ? (dv - 1) : 1; }
    /* v28：**语言选项在启动时也必须恢复**。
       以前只有 ContinueRun 会写 g_lang —— 也就是说：
         · 改完语言、退出设置，当场生效（内存里已经改了）
         · 重启后 LoadSettings 读完 g_save.lang 却从不赋值 → 又变回英文
       这就是「语言选项没有保存」的直接原因。 */
    g_lang = (g_save.lang == LANG_ZH) ? LANG_ZH : LANG_EN;
    gammaValue = g_save.gamma;
    mouseSens = g_save.sens;
    carryGold = g_save.carryGold;
    upgBattery = g_save.upgBattery;
    upgSpeed = g_save.upgSpeed;
    upgQuiet = g_save.upgQuiet;
    upgSanity = g_save.upgSanity;
    upgEco = g_save.upgEco;
    upgRest = g_save.upgRest;
    bestLevel = g_save.bestLevel;
    volumeF = g_save.volume;
    /* v2.7：写成 !(x >= lo && x <= hi) 才能挡住 NaN（NaN 的所有比较都是假） */
    if (!(volumeF >= 0.0f && volumeF <= 1.0f)) volumeF = 0.8f;
    musicVolumeF = info->hasMusicField ? g_save.musicVolume : volumeF;
    if (!(musicVolumeF >= 0.0f && musicVolumeF <= 1.0f)) musicVolumeF = volumeF;
    if (carryGold < 0) carryGold = 0;
    if (upgBattery < 0 || upgBattery > UPG_MAX) upgBattery = 0;
    if (upgSpeed < 0 || upgSpeed > UPG_MAX) upgSpeed = 0;
    if (upgQuiet < 0 || upgQuiet > UPG_MAX) upgQuiet = 0;
    if (upgSanity < 0 || upgSanity > UPG_MAX) upgSanity = 0;
    if (upgEco < 0 || upgEco > UPG_MAX) upgEco = 0;
    if (upgRest < 0 || upgRest > UPG_MAX) upgRest = 0;

    /* 键位：逐项夹取。非 0 的值必须能在 optKeyVKs 里找到，否则还原成默认 ——
       防止损坏的存档把「移动」绑成一个不存在的键，玩家被锁死。 */
    {
        static const int defBindings[BINDING_SLOTS] = BINDINGS_DEFAULT;
        /* 存档里有 32 个槽，但运行时数组只有 ACT_COUNT 个 —— 只能拷前 ACT_COUNT 个 */
        /* 2026-11-06：**先看这份存档认不认识新动作**
           旧存档没有这个标记（那一槽是 0）→ 0 必须补成默认值，
           否则新加的动作永远是"没绑定"，功能做了也按不出来。 */
        int saveSchema = g_save.bindings[BINDING_SLOTS - 1];
        int needDefaults = (saveSchema != BINDING_SCHEMA);

        for (int a = 0; a < ACT_COUNT; a++) {
            int vk = g_save.bindings[a];
            if (vk == 0) {
                /* 老存档：用默认键补上；新存档：尊重玩家的"显式无绑定" */
                g_bindings[a] = needDefaults ? defBindings[a] : 0;
                continue;
            }
            int known = 0;
            for (int k = 0; k < OPT_KEY_COUNT; k++)
                if (optKeyVKs[k] == vk) { known = 1; break; }
            g_bindings[a] = known ? vk : defBindings[a];
        }
    }

    /* v2.7：resolutions[] 有 RES_COUNT=8 档，原来夹 0..4，
       选 1920x1080 / 2560x1440 重启就被重置回 800x600。 */
    if (resIndex < 0 || resIndex >= RES_COUNT) resIndex = RES_AUTO;
    if (fpsCapIndex < 0 || fpsCapIndex > 4) fpsCapIndex = 4;
    if (renderScaleIndex < 0 || renderScaleIndex > 3) renderScaleIndex = 0;
    if (fovIndex < 0 || fovIndex > 4) fovIndex = 2;
    if (!(gammaValue >= 0.7f && gammaValue <= 1.6f)) gammaValue = 1.0f;
    if (!(mouseSens >= 0.2f && mouseSens <= 3.0f)) mouseSens = 1.0f;
}

static void RestoreLoadedProgress(const LoadedSaveInfo* info) {
    /* v7 新增：恢复成就数据（如果存档里有）。
       v6 **判据改成「成就段是否完整在文件里」** ——
       原来用 info->fileSize >= sizeof(SaveBlob)，等于「文件必须包含当前结构体的全部尾部字段」。
       于是每次往 SaveBlob 尾巴追加字段，都会把所有历史存档判成老档，
       把成就 / 技能树 / cores / 图鉴 / 无尽进度整段清零，哪怕那些字段其实读得到。
       现在用成就段的起始偏移做判据：只要文件覆盖到该段就正常读。 */
        size_t achOff = (size_t)((char*)&g_save.achUnlocked[0] - (char*)&g_save);
    if (info->fileSize >= achOff) {
        for (int i = 0; i < ACH_COUNT; i++) {
            g_achievements[i].unlocked = g_save.achUnlocked[i];
            g_achievements[i].progress = g_save.achProgress[i];
            if (!g_achievements[i].unlocked && kAchDefs[i].kind == AK_BESTIARYMASTER) g_achievements[i].progress = 0;
        }
        /* 1.8.0：技能树 / 核心 / keystone */
        for (int i = 0; i < 26; i++) {
            int r = g_save.skillRank[i];
            if (r < 0) r = 0;
            if (r > skillDefs[i].maxRank) r = skillDefs[i].maxRank;
            skillRank[i] = r;
        }
        cores = g_save.cores < 0 ? 0 : g_save.cores;
        activeKeystone = g_save.activeKeystone;
        if (activeKeystone != -1 && !SkillIsPlayerFacing(activeKeystone)) activeKeystone = -1;
        if (activeKeystone >= 0 && !skillDefs[activeKeystone].isKeystone) activeKeystone = -1;
        bestMainScore = g_save.bestMainScore < 0 ? 0 : g_save.bestMainScore;
        /* P0-2：图鉴解锁位掩码只保留真实存在的位 ——
       坏档里 104857600 这种值会让"已解锁数量"变成荒谬的数字。 */
    {
        int mask = g_save.seenMonsters;
        int valid = 0;
        for (int bi = 0; bi < BESTIARY_COUNT && bi < 31; bi++) valid |= (1 << bi);
        bestiarySeen = mask & valid;
    }

        /* v8 新增：恢复无尽模式进度 */
        bestEndlessLevel = g_save.bestEndlessLevel;
        bestEndlessScore = g_save.bestEndlessScore;
        isEndlessRun = (g_save.isEndlessRunSaved == 1);
    }
    else {  /* 老档没有成就数据 */
        /* 老存档没有成就数据，全置 0 */
        for (int i = 0; i < ACH_COUNT; i++) {
            g_achievements[i].unlocked = 0;
            g_achievements[i].progress = 0;
        }
        bestEndlessLevel = 0;
        bestEndlessScore = 0;
        isEndlessRun = 0;
    }

    /* v28：恢复「曾经通关」记录。
       v27 及更早没有这个字段 —— 用 FIRST EXIT 成就回填，老玩家升级后
       不会突然失去已经解锁的无尽模式（这个成就正好在成功撤离/通关时解锁）。 */
    if (info->hasGameClearedField) {
        gameCleared = g_save.gameClearedSave ? 1 : 0;
    } else {
        gameCleared = (g_achievements[1].unlocked != 0);
    }

    /* 文件里也记了它属于哪个槽；以文件为准，防止扫描选错 */
    if (g_save.saveSlot >= 0 && g_save.saveSlot < SAVE_SLOTS)
        g_saveSlot = g_save.saveSlot;

    /* v37：幽灵流派 30..33 */
    SkillInitOnce();
    RestoreSkillRanks();
    BestiaryLoad(g_save.bestiaryKnowledge);
    BestiaryLoadExtra(g_save.bestiaryExtraKnowledge);
    RestoreBankFromSave();
    { int da[15] = {g_save.dailyGoalId, g_save.dailyGoalParam, g_save.dailyProgress,
        g_save.dailyDone, g_save.dailyStreak, (int)g_save.dailyQuestDate,
        (int)g_save.dailyLastCompletedDate};
      for (int j=0;j<8;++j) da[7+j]=g_save.dailySamples[j];
      DailyLoadFrom(da); }
}

int LoadSettings(void) {
    LoadedSaveInfo info;
    memset(&info, 0, sizeof(info));
    if (!ReadSettingsCandidate(&info)) return 0;
    if (!MigrateSettingsCandidate(&info)) return 0;
    info.hasMusicField=info.fileSize>=offsetof(SaveBlob,musicVolume)+sizeof(float);
    info.hasDynamicResField=info.fileSize>=offsetof(SaveBlob,dynamicRes)+sizeof(int);
    info.hasGameClearedField=info.fileSize>=offsetof(SaveBlob,gameClearedSave)+sizeof(int);
    ApplyLoadedSettings(&info);
    RestoreLoadedProgress(&info);
    return g_save.hasRun;
}

int SaveSelectSlot(int slot){
    if(slot<0 || slot>=SAVE_SLOTS)return 0;
    if(slot==g_saveSlot)return 1;
    if(runInProgress || trainingMode)return 0;
    int previous=g_saveSlot;
    SaveSettingsOnly();if(g_saveState!=1)return 0;
    g_saveSlot=slot;forcedSaveSlot=slot;
    LoadedSaveInfo info;memset(&info,0,sizeof(info));
    int found=ReadSettingsCandidate(&info);
    if(found && !MigrateSettingsCandidate(&info))found=-1;
    if(!found){
        for(int i=0;i<SAVE_CAND_COUNT;i++){char path[MAX_PATH];SavePathCand(i,path,sizeof(path));if(PlatFileTime(path)>0)found=-1;}
    }
    if(found<0){g_saveSlot=previous;forcedSaveSlot=previous;LoadSettings();forcedSaveSlot=-1;return 0;}
    if(!found){
        memset(&g_save,0,sizeof(g_save));g_save.magic=SAVE_MAGIC;g_save.version=SAVE_VERSION;g_save.gameVersion=CANITY_VERSION_NUM;
        CopyDisplaySettings(&g_save);g_save.difficultyPlus1=difficulty+1;
        g_save.bankRiskPlanPlus1=2;g_save.bankCarryPctSave=10;
        info.fileSize=sizeof(g_save);s_saveReadOnly=0;SaveCarryClear();
    }
    g_save.saveSlot=slot;
    info.hasMusicField=info.fileSize>=offsetof(SaveBlob,musicVolume)+sizeof(float);
    info.hasDynamicResField=info.fileSize>=offsetof(SaveBlob,dynamicRes)+sizeof(int);
    info.hasGameClearedField=info.fileSize>=offsetof(SaveBlob,gameClearedSave)+sizeof(int);
    DailyResetProfile();ApplyLoadedSettings(&info);RestoreLoadedProgress(&info);
    g_saveSlot=slot;forcedSaveSlot=-1;hasSave=g_save.hasRun!=0;runInProgress=0;
    return 1;
}

/* 把存档里的进度灌回运行时 */
static void RestoreRunGeometry(void) {
    /* v3.0：坐标必须夹在地图内且非 NaN */
    #define CLAMP_POS(v, wrap) do { \
        if (!((v) == (v)) || (v) < 1.0f || (v) > (float)(wrap) - 1.0f) (v) = 1.5f; \
    } while (0)
    posX = g_save.posX; posY = g_save.posY;
    CLAMP_POS(posX, MAP_WIDTH);
    CLAMP_POS(posY, MAP_HEIGHT);
    exitX = g_save.exitX; exitY = g_save.exitY;
    CLAMP_POS(exitX, MAP_WIDTH);
    CLAMP_POS(exitY, MAP_HEIGHT);
    #undef CLAMP_POS
    dirX = g_save.dirX; dirY = g_save.dirY;
    /* NaN/zero/overflow-facing data must never reach camera projection. */
    {
        double length2 = (double)dirX * dirX + (double)dirY * dirY;
        if (!(length2 >= 0.000001 && length2 <= 1000000.0)) {
            dirX = 1.0f; dirY = 0.0f;
        } else if (fabs(length2 - 1.0) > 0.000001) {
            float inverseLength = (float)(1.0 / sqrt(length2));
            dirX *= inverseLength; dirY *= inverseLength;
        }
    }
    fusesHeld = g_save.fusesHeld<0?0:g_save.fusesHeld>MAX_CONTAINERS?MAX_CONTAINERS:g_save.fusesHeld;
    fusesNeeded = g_save.fusesNeeded<1?1:g_save.fusesNeeded>MAX_CONTAINERS?MAX_CONTAINERS:g_save.fusesNeeded;
    breakerOpen = g_save.breakerOpen;
    breaker.active = g_save.breakerActive;
    breaker.x = g_save.breakerX;
    breaker.y = g_save.breakerY;
    searchedCount = g_save.searchedCount<0?0:g_save.searchedCount>MAX_CONTAINERS?MAX_CONTAINERS:g_save.searchedCount;
    runTime = g_save.runTime;
    containerCount = g_save.nContainers;
    /* 防呆：被改坏的存档可能写出越界的容器数。
       负数 = 完全无效，清零；超出上限 = 截断到 MAX_CONTAINERS（保住已有进度），
       不要直接清零 —— 清零会让整层的箱子全没。 */
    if (containerCount < 0) containerCount = 0;
    if (containerCount > MAX_CONTAINERS) containerCount = MAX_CONTAINERS;
    for (int i = 0; i < MAX_CONTAINERS; i++) containers[i] = g_save.cont[i];
    memcpy(worldMap, g_save.worldMap, sizeof(worldMap));
    /* ==================================================================
     * v2.4-rc：**读档后重建房间**
     * ------------------------------------------------------------------
     * worldMap 已经恢复，房间的墙本来就在里面；但 cellMeta（色调/声学）
     * 和 roomProps（家具 + 碰撞）是运行时数据，必须用保存的种子重放。
     * roomSeed == 0（v29 老档）→ 当作"无房间层"，什么都不做。
     * ================================================================== */
    { 

      /* 第 6 轮：**先定引擎再重放** —— 老档（0）走 C 标准库 rand 序列，
         新档（2）走 xorshift32。顺序反了房间就会重放成另一套布局。 */
      rng_set_version(g_save.rngVersion);
      if(g_save.decorV43==1 && g_save.propCountV43>=0 && g_save.propCountV43<=MAX_ROOM_PROPS && g_save.hatchCountV43>=0 && g_save.hatchCountV43<=MAX_HATCHES){
          memcpy(cellMeta,g_save.cellsV43,sizeof(cellMeta));roomPropCount=g_save.propCountV43;memcpy(roomProps,g_save.propsV43,sizeof(roomProps));hatchCount=g_save.hatchCountV43;memcpy(hatches,g_save.hatchesV43,sizeof(hatches));
          RoomSetCount(g_save.roomCount);RoomThemeBind();
      }else if (g_save.roomSeed != 0 && g_save.roomCount > 0)
          (void)RoomReplay(g_save.roomSeed, g_save.roomCount);
      else {  CellMetaReset(); }
      /* ================================================================
       * v33：**重放之后**才能标已用 —— RoomReplay 会先 HatchReset 清表，
       * 标早了就没有门可以标。老档这个字段是 0，行为与旧版一致。
       * ================================================================ */
      { 
        HatchApplyUsedMask(g_save.hatchUsedMask); } }
    memcpy(explored, g_save.explored, sizeof(explored));
}

static void RestoreRunInventory(void) {
    /* v4 新增：安全屋 / 投掷物 / 词条 / 主题 */
    /* v3.0：**道具数量必须夹取** ——
       玩家实测「道具栏突然变成 MIRROR X99 / FLARE X99 / BOTTLE X99」，
       就是因为这里只有直接赋值：坏存档/越界写入的值会原样进 HUD。
       上限给 99（正常玩法永远到不了），下限 0。 */
    /* v3.0：上限取**实际玩法的容量**，不是 99 ——
       上一版夹到 99，结果存档里的垃圾值被夹成 X99 直接显示出来，
       玩家看到的就是「道具栏变成 X99」。
       正常容量：瓶子/照明弹 8，其余稀有道具 4。 */
    #define CLAMP_ITEM(v)     do { if ((v) < 0) (v) = 0; if ((v) > 8) (v) = 8; } while (0)
    #define CLAMP_ITEM_RARE(v) do { if ((v) < 0) (v) = 0; if ((v) > 4) (v) = 4; } while (0)
    flares = g_save.flares;      CLAMP_ITEM(flares);
    bottles = g_save.bottles;    CLAMP_ITEM(bottles);
    /* v13：v2.0 新系统（不读回来 = 中途退出丢光遗物/Boss 状态/每日战役） */
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = g_save.relicOwned[i]<0?0:g_save.relicOwned[i]>99?99:g_save.relicOwned[i];
    relicTotal=0;for(int i=0;i<RELIC_COUNT;i++)relicTotal+=relicOwned[i];
    /* v8: boss 字段已移除。bossLevel 恒为 0，下面两行本就永不成立。 */
    /* ==================================================================
     * 2026-11-06（**P0-2 读档字段没有合法性夹取**）
     * ------------------------------------------------------------------
     * 现象：普通局启动后手电永远打不开 —— 因为 dailyMode 读成了 -1，
     *       而 DailyForbidLight() 的判据是 `dailyMode && ...`，
     *       -1 是真值，于是"每日模式的禁灯限制"漏进了普通模式。
     *
     * 根因：这几个是**布尔/枚举**语义，但读档时原样赋值，没有归一。
     *       存档被错误迁移过一次（P0-1）就会留下任意值，
     *       而一个"看起来像真"的垃圾值会静默改变整个玩法。
     *
     * 原则：**布尔只接受 0/1，枚举夹进合法区间**。
     *       宁可把坏值当成"关"，也不要让它带出一个不存在的规则。
     * ================================================================ */
    dailyMode = (g_save.dailyMode == 1) ? 1 : 0;
    dailySeed = g_save.dailySeed;
    campaignMode = (g_save.campaignMode == 1) ? 1 : 0;
    campaignStage = g_save.campaignStage;
    if (campaignStage < 0) campaignStage = 0;
    /* CAMPAIGN_STAGE_COUNT 是段数，关卡序号不设上限，这里只防负数与荒谬值 */
    if (campaignStage > 1000) campaignStage = 0;
    firePots = g_save.firePots;   CLAMP_ITEM_RARE(firePots);
    gluePots = g_save.gluePots;   CLAMP_ITEM_RARE(gluePots);
    mirrors = g_save.mirrors;   CLAMP_ITEM_RARE(mirrors);
    /* v34：三类投掷物 + 爆脸怪计数。v33 短档读进来是 0，夹取不改变语义。 */
    { 
      echoBolts    = g_save.echoBolts;     CLAMP_ITEM_RARE(echoBolts);
      decoys       = g_save.decoys;        CLAMP_ITEM_RARE(decoys);
      phaseBeacons = g_save.phaseBeacons;  CLAMP_ITEM(phaseBeacons);
      jumperSpawnedRun = g_save.jumperSpawnedRun;
      if (jumperSpawnedRun < 0) jumperSpawnedRun = 0;
      if (jumperSpawnedRun > JUMPER_MAX_PER_RUN) jumperSpawnedRun = JUMPER_MAX_PER_RUN; }
    bankStreak = g_save.bankStreak;
    modBatteryMul = g_save.modBatteryMul;
    if (!(modBatteryMul > 0.0f)) modBatteryMul = 1.0f;
    modShardMul = g_save.modShardMul;
    if (!(modShardMul > 0.0f)) modShardMul = 1.0f;
    modNoiseMul = g_save.modNoiseMul;
    if (!(modNoiseMul > 0.0f)) modNoiseMul = 1.0f;
    /* 读档后 Boss 状态要重建：已清的 Boss 不能再站起来 */
    if (flares < 0) flares = 0;
    if (bottles < 0) bottles = 0;
    /* v6 **旧安全区已删除** ——
       存档里 safeActive 还是 1 的老档，读回来会让旧安全区重新出现
       （用户：「有时候会冒出旧版本的安全区 / 旧道具旧模型」）。
       这里无条件清零，不管存档里写的是什么。 */
    safeRoom.active = 0;
    safeRoom.x = g_save.safeX;
    safeRoom.y = g_save.safeY;
    levelMod = g_save.levelMod;
    if (levelMod < -1 || levelMod >= MOD_COUNT) levelMod = -1;
    modSanityMul = g_save.modSanityMul;
    modSpeedMul = g_save.modSpeedMul;
    /* v2.7：NaN-safe。存档里混进 NaN 时 `<= 0` 判不出来，
       它会一路乘进理智流失，sanity 变 NaN → `sanity <= 0` 永不成立 → 死不掉。 */
    if (!(modSanityMul > 0.0f)) modSanityMul = 1.0f;
    if (!(modSpeedMul > 0.0f)) modSpeedMul = 1.0f;
    themeId = g_save.themeId;
    if (themeId < 0 || themeId >= THEME_COUNT) themeId = 0;
    for (int i = 0; i < MAX_THROWN; i++) thrown[i] = g_save.thr[i];

    /* v9：把新存的进度接回来（顺手夹一下，坏存档不至于把数量弄成负数） */
    bricks      = g_save.bricks;      CLAMP_ITEM_RARE(bricks);
    acids       = g_save.acids;       CLAMP_ITEM_RARE(acids);
    smokeBombs  = g_save.smokeBombs;  CLAMP_ITEM_RARE(smokeBombs);
    carryGold = g_save.carryGold < 0 ? 0 : g_save.carryGold;
    if (carryGold > 999999) carryGold = 0;      /* 明显是坏档 */
    runSkillMask = g_save.runSkillMask;
    /* v3.1：界面固定英文。老存档里的 lang 字段仍保留，但不再覆盖运行时语言；
       否则中文存档一进来整页菜单/提示还是中文。 */
    g_lang = (g_save.lang==LANG_ZH) ? LANG_ZH : LANG_EN;
}

static void RestoreRunEquipment(void) {
    /* v11：恢复武器局外成长 */
    for (int i = 0; i < 3; i++) {
        weaponRank[i] = (g_save.weaponRank[i] >= 0 && g_save.weaponRank[i] <= 3) ? g_save.weaponRank[i] : 0;
        weaponXp[i] = (g_save.weaponXp[i] >= 0 && g_save.weaponXp[i] < 1000) ? g_save.weaponXp[i] : 0;
        weaponAttach[i][0] = (g_save.weaponAttach[i][0] >= -1 && g_save.weaponAttach[i][0] < ATTACH_COUNT)
                             ? g_save.weaponAttach[i][0] : -1;
        weaponAttach[i][1] = (g_save.weaponAttach[i][1] >= -1 && g_save.weaponAttach[i][1] < ATTACH_COUNT)
                             ? g_save.weaponAttach[i][1] : -1;
    }
    for (int i = 0; i < ATTACH_COUNT; i++)
        attachOwned[i] = (g_save.attachOwned[i] > 0) ? g_save.attachOwned[i] : 0;
    if (g_save.curWeapon >= 0 && g_save.curWeapon < WPN_COUNT) curWeapon = g_save.curWeapon;
    runSkillCount = 0;
    for (unsigned int b = 0; b < 32u; b++)
        if (runSkillMask & (1u << b)) runSkillCount++;
    runUltimate = (g_save.runUltimate >= 0 && g_save.runUltimate < INRUN_COUNT)
                  ? g_save.runUltimate : -1;

    ApplyFov();
    GenerateThemeTextures(themeId);   /* 主题变了，墙面/地板贴图要跟着重建 */
    SpawnMonster();
    SpawnLightBeast();
    SpawnLightBeast2();   /* 2026-11-06：追光怪两只 */
    isEndlessRun = (g_save.isEndlessRunSaved == 1);  /* 恢复无尽模式标记 */
}

/* Repair only invalid loaded positions. Preserve IDs, contents and intact hiding places. */
static int LoadedFloorCell(int x,int y){
    return x>0 && y>0 && x<MAP_WIDTH-1 && y<MAP_HEIGHT-1 && !worldMap[y][x] && !PropBlocksAt(x,y);
}
static int LoadedHidePosition(float x,float y){
    if(!isfinite(x) || !isfinite(y) || x<1 || y<1 || x>=MAP_WIDTH-1 || y>=MAP_HEIGHT-1)return 0;
    if(!LoadedFloorCell((int)x,(int)y) || Blocked(x,y))return 0;
    /* A closet silhouette needs more room than the player's center point. */
    const float edge=.43f;
    return !worldMap[(int)(y-edge)][(int)(x-edge)] && !worldMap[(int)(y-edge)][(int)(x+edge)] &&
           !worldMap[(int)(y+edge)][(int)(x-edge)] && !worldMap[(int)(y+edge)][(int)(x+edge)];
}
static void RestoreUsableHideSpots(void){
    hideSpotNear=-1;
    if(!isfinite(posX) || !isfinite(posY) || posX<1 || posY<1 || posX>=MAP_WIDTH-1 || posY>=MAP_HEIGHT-1 ||
       Blocked(posX,posY) || !LoadedFloorCell((int)posX,(int)posY)){
        float best=1e30f,bx=posX,by=posY;
        float ox=isfinite(posX)?posX:1.5f,oy=isfinite(posY)?posY:1.5f;
        for(int y=1;y<MAP_HEIGHT-1;y++)for(int x=1;x<MAP_WIDTH-1;x++)if(LoadedFloorCell(x,y)){
            float d=Dist2(x+.5f,y+.5f,ox,oy);if(d<best){best=d;bx=x+.5f;by=y+.5f;}
        }
        if(best<1e29f){posX=bx;posY=by;}
    }
    unsigned char seen[MAP_HEIGHT][MAP_WIDTH]={{0}};int queue[MAP_WIDTH*MAP_HEIGHT],head=0,tail=0;
    int px=(int)posX,py=(int)posY;
    if(LoadedFloorCell(px,py)){seen[py][px]=1;queue[tail++]=py*MAP_WIDTH+px;}
    const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
    while(head<tail){int cell=queue[head++],x=cell%MAP_WIDTH,y=cell/MAP_WIDTH;
        for(int d=0;d<4;d++){int nx=x+dx[d],ny=y+dy[d];if(LoadedFloorCell(nx,ny) && !seen[ny][nx]){seen[ny][nx]=1;queue[tail++]=ny*MAP_WIDTH+nx;}}
    }
    for(int i=0;i<hideSpotCount;i++){
        HideSpot* h=&hideSpots[i];if(!h->active)continue;
        if(h->kind<0 || h->kind>2)h->kind=0;
        h->broken=h->broken?1:0;
        int valid=LoadedHidePosition(h->x,h->y);
        if(valid)valid=seen[(int)h->y][(int)h->x];
        if(valid)continue;
        float best=1e30f,bx=0,by=0,ox=isfinite(h->x)?h->x:posX,oy=isfinite(h->y)?h->y:posY;
        for(int y=1;y<MAP_HEIGHT-1;y++)for(int x=1;x<MAP_WIDTH-1;x++)if(seen[y][x]){
            float tx=x+.5f,ty=y+.5f;
            if(!LoadedHidePosition(tx,ty) || Dist2(tx,ty,exitX,exitY)<4 || (breaker.active && Dist2(tx,ty,breaker.x,breaker.y)<2))continue;
            int clash=0;
            for(int j=0;j<containerCount;j++)if(containers[j].active && Dist2(tx,ty,containers[j].x,containers[j].y)<1)clash=1;
            for(int j=0;j<hideSpotCount;j++)if(i!=j && hideSpots[j].active && isfinite(hideSpots[j].x) && isfinite(hideSpots[j].y) && Dist2(tx,ty,hideSpots[j].x,hideSpots[j].y)<4)clash=1;
            if(clash)continue;
            float distance=Dist2(tx,ty,ox,oy);if(distance<best){best=distance;bx=tx;by=ty;}
        }
        if(best<1e29f){h->x=bx;h->y=by;}else h->active=0;
    }
    if(hiddenIn<0 || hiddenIn>=hideSpotCount || !hideSpots[hiddenIn].active || hideSpots[hiddenIn].broken)hiddenIn=-1;
    else {posX=hideSpots[hiddenIn].x;posY=hideSpots[hiddenIn].y;}
    playerInSafe=hiddenIn>=0;holdTimer=0;hoverKind=0;hoverIndex=-1;
}

static void ResumeRestoredRun(void) {
    /* 读档不重掷跳脸虫：本关不会突然蹦出来。
       触发器留成 -1，等下一层进关时再掷。 */
    jumperTrigger = -1.0f;
    jumperPhase = JMP_IDLE;
    jumperTimer = 0.0f;
    parasite.active = 0;
    runInProgress = 1;
    /* v3.0：读档前先关掉锁盘 —— 否则 lockActive 残留，
       ui.c 无条件画锁盘会盖住游戏画面，且 WM_LBUTTONDOWN 先把点击全吞掉。 */
    if (lockActive) CloseLockOverlay();
    currentState = STATE_PLAYING;
    isMapOpen = 0;
    mapFullScreen = 0;
    flashTime = 0.0f;
    healFlash = 0.0f;
    resetInputState = 1;   /* 读档回游戏：主菜单点击的左键不能带进游戏 */
    ClearFx();
    for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
    ForceCursor(0);
    firstMouseMove = 1;
    uiDirty = 1;

    /* 恢复成就追踪变量 */
    bottlesKilled = 0;
    faceDodged = 0;
    crankTimeTotal = 0;
    fusesCollectedRun = 0;
    mapsFullyExplored = 0;
    tookDamage = 0;
    flashlightOffLevel = 0;
    sprintedThisLevel = 0;
    #undef CLAMP_ITEM
    #undef CLAMP_ITEM_RARE
}

void ContinueRun(void) {
    LoopResetHuntAchievements();
    /* ==================================================================
     * 2026-11-06（审计补）：**读档要清掉"场上的临时系统"**
     * ------------------------------------------------------------------
     * 蜘蛛 / 拾光虫 / 控制效果 / 信标放置 都是各自模块的 static 状态，
     * 不随存档走。读档时如果不重置，会把上一次运行的残留带进来：
     *   · 一进游戏就"被蛛网抓住"，场上却没有任何蜘蛛
     *     （而且旧代码里蛛网**没有挣脱路径**，等于永久卡死）
     *   · 蜘蛛坐标还停在上一次的位置，可能已经在墙里
     *   · 上一层放的信标坐标被当成这一层的
     * ================================================================== */
    {  ControlReset(); }
    /* 审计·低 11：抱脸虫的 10 秒锁定不随存档走，读档必须清掉，
       否则在锁定期间存档退出，读档后会莫名其妙继续不能动。 */
    {  jumperControlT = 0.0f; }
    {   SpiderReset(); }
    {  NeutralReset(); }
    {  PhaseResetPlacement(); }

    /* v28：**读档前必须先清模式标志**。
       原来越野：每日/战役 → 暂停 → 主菜单（标志还留着）→ 教程 → 退出教程
       （trainingMode 留着）→ Continue → 读出来的挑战地图上，
       trainingMode 仍是 1：道具无限、出口判定被跳过，于是卡在关卡里。
       现在训练/教程/每日/战役/无尽全部清零，再由存档决定这一局是什么模式。 */
    if (!g_save.hasRun) {
        /* 存档里没有可继续的进度（例如只写过设置的档）：别造一局空世界。 */
        hasSave = 0;
        runInProgress = 0;
        return;
    }
    ResetRunModeFlags();
    { 


      knockCountRun=g_save.knockRunSave; throwHitCountRun=g_save.throwHitRunSave;
      echoCellsTotalRun=g_save.echoRunSave; dailyEscapeCount=g_save.dailyEscapeRunSave;
      floorsNoSanityLoss=g_save.cleanFloorsSave; wardenCleanFloors=g_save.wardenFloorsSave;
      fullHideDone=g_save.fullHideSave; doubleHuntSeconds=g_save.doubleHuntSave; }
    { 
      shopRefreshFloor=g_save.shopRefreshFloorSave; shopRefreshUsed=g_save.shopRefreshUsedSave;
      contractKind=g_save.contractKindSave; shopContractUsed=g_save.contractLockedSave;
      /* Restore threat after the skill ranks below: saved totals include GREEDY HAND. */ }
    currentLevel = g_save.level;
    /* v3.0：**读档必须夹取** ——
       外部改过的档可能带 NaN：sanity=NaN 时 `sanity <= 0` 永不成立（死不掉），
       pos=NaN 时 Blocked() 失效（可以穿墙）。 */
    if (currentLevel < 1) currentLevel = 1;
    if (currentLevel > 999) currentLevel = 999;
    sanity = g_save.sanity;
    /* v3.0：NaN / 越界一律拉回合法区间 */
    if (!(sanity == sanity)) sanity = 100.0f;
    if (sanity < 0.0f) sanity = 0.0f;
    if (sanity > 100.0f) sanity = 100.0f;
    battery = g_save.battery;
    if (!(battery == battery)) battery = 100.0f;
    if (battery < 0.0f) battery = 0.0f;
    if (battery > 100.0f) battery = 100.0f;
    stamina = g_save.stamina;
    if (!(stamina == stamina)) stamina = 100.0f;
    if (stamina < 0.0f) stamina = 0.0f;
    if (stamina > 130.0f) stamina = 130.0f;
    energy = g_save.energy;
    benchOwned=(g_save.benchOwnedV42?g_save.benchOwnedV42:g_save.benchOwnedSave)&((UINT64_C(1)<<BENCH_COUNT)-1);
    hideSpotCount = g_save.hideSpotSaveCount;
    if (hideSpotCount < 0) hideSpotCount = 0;
    if (hideSpotCount > 6) hideSpotCount = 6;
    for (int i = 0; i < 6; i++) hideSpots[i] = g_save.hideSpotSave[i];
    hiddenIn = g_save.hiddenInSave;
    /* v3.3：恢复安全屋每层限一次的标记（老档没有这几个字段，读到 0 也安全） */
    {

        ShopSetSafeLv(g_save.safeHealLvSave,
                      g_save.safeChargeLvSave,
                      g_save.safeStaminaLvSave);
    }
    /* v3.3：恢复跨局累计的成就计数器（老档读到 0，安全） */
    {




        floorsNoLight      = g_save.achFloorsNoLight;
        floorsFlawless     = g_save.achFloorsFlawless;
        floorsPacifist     = g_save.achFloorsPacifist;
        floorsFast         = g_save.achFloorsFast;
        mapsFullyExplored  = g_save.achMapsExplored;
        fusesCollectedRun  = g_save.achFusesRun;
        locksOpened        = g_save.achLocksOpened;
        hideCount          = g_save.achHideCount;
        totalThrown        = g_save.achTotalThrown;
        crankSecondsTotal  = g_save.achCrankSecondsTotal;
        lowSanitySeconds   = g_save.achLowSanitySeconds;
    }
    /* v3.4/v6 银行：统一走 RestoreBankFromSave，只有一份逻辑 */
    /* v37：幽灵流派 30..33 */
    SkillInitOnce();
    RestoreSkillRanks();
    ThreatRestoreTotal(g_save.threatSave);
    BestiaryLoad(g_save.bestiaryKnowledge);
    BestiaryLoadExtra(g_save.bestiaryExtraKnowledge);
    ShopRestoreCooldowns(g_save.restCooldownSave,g_save.refreshCooldownSave);
    HushRestoreRuntime(g_save.commonRuntimeFloor,g_save.commonRuntimeMask);
    RestoreBankFromSave();
    if (hiddenIn >= hideSpotCount) hiddenIn = -1;
    RunUpgLoadFromSave(g_save.runUpgSave);
    { 
      RunUpgLoadExtra(g_save.runUpgMinimapSave); }   /* v2.3：老存档没有这个字段（读出 0）→ 下面补满 */
    energy=isfinite(energy)?fmaxf(0,fminf((float)EnergyMax(),energy)):(float)EnergyMax();
    flashlightOn = g_save.flashlightOn;
    /* v8：模式规则优先于存档里的开关状态 ——
       每日“不能开手电”/ SALVAGE keystone 读档后必须保持关闭；
       战役第 1 关“关不掉”读档后必须保持开启。 */
    {
        if (DailyForbidLight() || HasKeystone(SK_SALVAGE)) flashlightOn = 0;
        else if (CampaignFlashlightLocked()) flashlightOn = 1;
    }
    RestoreRunGeometry();
    RestoreUsableHideSpots();
    RestoreRunInventory();
    RestoreRunEquipment();
    roomTheme.spent=g_save.roomEventSpent?1:0;
    RoomThemeSetMadness(g_save.runMadness);
    RoomThemeLoadEvent(g_save.roomEventKindV43,g_save.roomEventTimeV43);
    CollectorLoadRun(g_save.collectorRunV44);
    MoneyBagsLoad(&g_save.moneyBagPositionsV45[0][0],g_save.moneyBagAmountsV45);
    if(g_save.collectorBountyV45>0){
        /* A transient thief becomes a recoverable bag instead of deleting stolen money. */
        float x=g_save.collectorPositionV45[0],y=g_save.collectorPositionV45[1];
        if(!isfinite(x) || !isfinite(y) || x<1 || y<1 || x>=MAP_WIDTH-1 || y>=MAP_HEIGHT-1 || Blocked(x,y)){x=posX;y=posY;}
        MoneyBagDropAt(x,y,g_save.collectorBountyV45);
    }
    counterKillUsed=g_save.counterKillUsedV45==1;
    {int data[15]={g_save.dailyGoalId,g_save.dailyGoalParam,g_save.dailyProgress,g_save.dailyDone,g_save.dailyStreak,(int)g_save.dailyQuestDate,(int)g_save.dailyLastCompletedDate};
     for(int i=0;i<8;i++){data[7+i]=g_save.dailySamples[i];}
     DailyLoadFrom(data);}
    CalibResetRun();
    ShopRestoreServices(g_save.serviceUsesV42);RunEconomyRestore(g_save.rewardFloorV42,g_save.escapeRewardMaskV42,g_save.rhythmGoldV42);
    ResumeRestoredRun();
}

void DeleteSave(void) {
    char p[MAX_PATH];
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        const char* cand = SavePathCand(i, p, sizeof(p));
        PlatDeletePath(cand);
        /* v29：**连 .bak / .tmp 一起删** ——
           LoadSettings 在主档读不到时会回退 .bak；
           只删主档的话，「新开一局 → 首次存档前退出/崩溃」
           下次启动就会从 .bak 把旧局复活，等于删档没删干净。 */
        {
            char side[MAX_PATH + 8];
            snprintf(side, sizeof(side), "%s.bak", cand);
            PlatDeletePath(side);
            snprintf(side, sizeof(side), "%s.tmp", cand);
            PlatDeletePath(side);
        }
    }
    g_save.hasRun = 0;
    hasSave = 0;
    runInProgress = 0;
}

int SaveAwardTrainingGraduation(void) {
    SaveSettingsOnly();
    if(g_saveState==2)return 0;
    if(g_save.trainingGraduated)return 2;
    int before=bankGold,beforeMax=g_save.bankHistMaxSave,beforeHistory=bankHistMax;
    WalletAddBank(1500);
    g_save.trainingGraduated=1;g_save.bankGoldSave=bankGold;
    if(g_save.bankHistMaxSave<bankGold)g_save.bankHistMaxSave=bankGold;
    SaveSettingsOnly();
    if(g_saveState==2){bankGold=before;bankHistMax=beforeHistory;g_save.bankGoldSave=before;g_save.bankHistMaxSave=beforeMax;g_save.trainingGraduated=0;return 0;}
    return 1;
}

/*
 * Canity — 存档分区：**容器契约**
 *
 * ==================================================================
 * 这个头文件是「存档分区」和「Steam 专区」之间**唯一**的契约。
 * ------------------------------------------------------------------
 * 分工：
 *   存档分区  src/save/          生产 / 消费字节（见 save_container.c、save_backend.c）
 *   Steam 专区 src/steam/        只负责搬运字节 + 冲突判定 + 配额
 *
 * 两边共享的就是下面这个 20 字节的信封。它必须位于存档文件偏移 0 ——
 * 这样 SteamZoneCloudSync() 可以直接吃文件原样字节，不需要现场包一层。
 *
 * 历史（2026-11-05 之前）：
 *   信封原来定义在 steam_zone.c 内部（SteamCloudHead），由 ReadLocalEnvelope()
 *   在**上传时临时合成**，version 写死 26，stamp 取文件 mtime。
 *   三个后果：跨机器拷贝存档会把时间戳重置导致冲突判错边、
 *   存档模块完全不知道自己的云端身份、类型被迫复制两份（铁律十八）。
 *   现在信封上移到这里，由存档分区在写盘时就封好。
 * ==================================================================
 *
 * 版本迭代规则（这是「存档能跟着代码一起迭代」的关键）：
 *
 *   · SAVE_CONTAINER_VERSION —— **容器结构**版本。块表格式本身变了才 +1。
 *     绝大多数迭代不需要动它。
 *   · SAVE_VERSION（在 save.h）—— **载荷**版本。字段增删改才 +1。
 *   · 每个块还有自己的 schema —— 单块的字段变化只加该块的 schema。
 *
 *   于是「加一个字段」只影响一个块，容器版本不变、其它块完全不受影响。
 */
#ifndef CANITY_SAVE_FORMAT_H
#define CANITY_SAVE_FORMAT_H

#include "common.h"

/* ================================================================== */
/* 信封                                                                */
/* ================================================================== */

#define SAVE_ENVELOPE_MAGIC     0x56415343u   /* 'CSAV' 小端，与 steam_zone 历史值一致 */
#define SAVE_CONTAINER_VERSION  1             /* 容器结构版本：块表格式 */

typedef struct {
    unsigned int magic;     /* SAVE_ENVELOPE_MAGIC */
    int          version;   /* SAVE_CONTAINER_VERSION */
    unsigned int stamp;     /* **存档写入时刻**（Unix 秒），不是文件 mtime */
    unsigned int sum;       /* payload 的 FNV-1a */
    int          payload;   /* payload 字节数 */
} SaveEnvelope;

/* 填好 magic/version/stamp/sum/payload 五个字段 */
void SaveEnvelopeSeal(SaveEnvelope* h, const void* payload, int n, unsigned int stamp);

/* 返回 1 = 信封与载荷自洽（magic / version / payload 长度 / 校验和全对） */
int  SaveEnvelopeValid(const SaveEnvelope* h, const void* payload, int n);

/* FNV-1a：信封校验和。**全工程唯一实现**，steam_zone.c 也用它。 */
unsigned int SaveFnv1a(const void* p, int n);

/* ================================================================== */
/* 块表（payload 内部）                                                */
/* ================================================================== */

/* payload 布局：
 *   [容器版本 int][块数 int][reserved int]
 *   [SaveChunkDesc × N]
 *   [块数据（按块表顺序紧密排列）]
 */
#define SAVE_PAYLOAD_HEAD_SIZE  12   /* 3 个 int */

typedef struct {
    unsigned short id;      /* SC_* */
    unsigned short schema;  /* 该块自己的版本，单块迭代时 +1 */
    unsigned int   size;    /* 块数据字节数 */
} SaveChunkDesc;

/* ---- 已知块 id ---- */
enum {
    SC_SETTINGS = 0x0001,   /* 显示 / 音频 / 键位 / 语言 —— 改设置时才变 */
    SC_LOBBY    = 0x0002,   /* 局外进度：金币 / 升级 / 技能 / 成就 / 银行 / 每日 */
    SC_RUN      = 0x0003,   /* 当前对局的全部状态 */
    SC_WORLD    = 0x0004,   /* worldMap + explored */
    SC_LEGACY29 = 0x00FF,   /* 过渡期：整份 SaveBlob 原样字节 */
    SC_MAX_ID   = 0x00FF
};

/* ================================================================== */
/* 打包 / 解包                                                          */
/* ================================================================== */

/*
 * 把 n 个块打包成 payload。out/outCap 由调用方提供。
 * 返回 payload 字节数；返回 <= 0 表示失败（容量不足或参数非法）。
 * outSum 可为 NULL，非 NULL 时写回 payload 的 FNV-1a。
 */
int SaveContainerPack(const SaveChunkDesc* desc, const void* const* body, int n,
                      void* out, int cap, unsigned int* outSum);

/* payload 里的块数量；非法返回 -1 */
int SaveContainerCount(const void* payload, int n);

/* 取第 i 块（**包括不认识的 id** —— 未知块保留就靠这个枚举）。
   body 指到 payload 内部，不要释放。返回 1 = 成功，0 = 越界/非法。 */
int SaveContainerAt(const void* payload, int n, int i,
                    SaveChunkDesc* desc, const void** body);

/* 按 id 查找。返回 1 = 找到，0 = 没有（或 payload 非法）。 */
int SaveContainerFind(const void* payload, int n, unsigned short id,
                      const void** body, int* size);

#endif /* CANITY_SAVE_FORMAT_H */

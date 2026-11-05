/* ==================================================================
 *  STEAM 专区 —— 实现
 * ==================================================================
 *
 *  为什么用「运行时动态加载」而不是链接 steam_api64.lib
 *  ────────────────────────────────────────────────────
 *  Steamworks SDK 只提供 MSVC 的 .lib。我们是 w64devkit（MinGW）单 exe 构建，
 *  直接链 .lib 要走 dlltool 生成 .a，而且一旦玩家机器上没有 DLL 就整个起不来。
 *
 *  改成 LoadLibrary("steam_api64.dll") + GetProcAddress：
 *    · 不需要任何导入库
 *    · 没有 DLL → 自动降级成单机模式，游戏照常
 *    · 不需要把 SDK 目录加进 include 路径
 *
 *  代价：函数名是字符串，写错了编译期查不出来 —— 所以
 *  下面每个符号都在 Init 里**全部解析一遍并统一检查**，
 *  缺一个就把状态标成"符号不全"，而不是半个功能能用。
 * ================================================================== */

#include "steam_zone.h"
#include "steam_ach.h"
#include "common.h"
#include "achievements.h"
#include <stdbool.h>

/* ---------------- 状态 ---------------- */
static int          s_ready = 0;          /* 1 = 完全可用 */
static int          s_status = 0;         /* 0 未初始化 1 就绪 2 无 DLL 3 符号缺 4 Init 失败 */
#if CANITY_STEAM
static HMODULE      s_dll = NULL;
#endif
static char         s_statusText[96] = "Steam: off";

/* ---------------- 扁平 C API 函数指针 ---------------- */
#if CANITY_STEAM

typedef void* HSteamUser;
typedef void* HSteamPipe;

static HSteamUser  (*p_SteamAPI_GetHSteamUser)(void);
static HSteamPipe  (*p_SteamAPI_GetHSteamPipe)(void);
static bool        (*p_SteamAPI_Init)(void);
static void        (*p_SteamAPI_Shutdown)(void);
static void        (*p_SteamAPI_RunCallbacks)(void);
static int         (*p_SteamAPI_RestartAppIfNecessary)(unsigned int);

/* UserStats（成就 / 统计） */
static void*       (*p_SteamUserStats)(void);
static int         (*p_ISteamUserStats_SetAchievement)(void*, const char*);
static int         (*p_ISteamUserStats_GetAchievement)(void*, const char*, int*);
static int         (*p_ISteamUserStats_StoreStats)(void*);
static int         (*p_ISteamUserStats_SetStatInt32)(void*, const char*, int);

/* RemoteStorage（云存档） */
static void*       (*p_SteamRemoteStorage)(void);
static int         (*p_ISteamRemoteStorage_FileWrite)(void*, const char*, const void*, int);
static int         (*p_ISteamRemoteStorage_FileRead)(void*, const char*, void*, int);
static int         (*p_ISteamRemoteStorage_GetFileSize)(void*, const char*);
static int         (*p_ISteamRemoteStorage_FileExists)(void*, const char*);
static int         (*p_ISteamRemoteStorage_GetQuota)(void*, unsigned long long*, unsigned long long*);

/* Music */
static void*       (*p_SteamMusic)(void);
static void        (*p_ISteamMusic_Pause)(void*);
static void        (*p_ISteamMusic_Play)(void*);
static int         (*p_ISteamMusic_IsPlaying)(void*);

/* Friends（覆盖层） */
static void*       (*p_SteamFriends)(void);
static void        (*p_ISteamFriends_ActivateGameOverlay)(void*, const char*);
static int         (*p_ISteamFriends_SetRichPresence)(void*, const char*, const char*);
static void        (*p_ISteamFriends_ClearRichPresence)(void*);

#endif   /* CANITY_STEAM：函数指针声明结束 */

/* ---------------- 已有成就去重表 ---------------- */
#define STEAM_ACH_CACHE_MAX 64
static char s_achCache[STEAM_ACH_CACHE_MAX][64];
static int  s_achCount = 0;

static int achCached(const char* name) {
    for (int i = 0; i < s_achCount; i++) {
        if (strcmp(s_achCache[i], name) == 0) return 1;
    }
    return 0;
}
#if CANITY_STEAM
static void achRemember(const char* name) {
    if (s_achCount >= STEAM_ACH_CACHE_MAX) return;
    strncpy(s_achCache[s_achCount], name, 63);
    s_achCache[s_achCount][63] = '\0';
    s_achCount++;
}
#endif

/* ---------------- 符号解析 ---------------- */
#define RESOLVE(type, var, name)                                   \
    do {                                                           \
        var = (type)GetProcAddress(s_dll, name);                   \
        if (!var) { missing++; }                                   \
    } while (0)

int SteamZoneInit(void) {
    if (s_ready) return 1;

    /* 没开编译开关 → 直接说"关着" */
#if !CANITY_STEAM
    s_status = 0;
    snprintf(s_statusText, sizeof(s_statusText), "Steam: off (built without CANITY_STEAM)");
    return 0;
#else
    s_dll = LoadLibraryA("steam_api64.dll");
    if (!s_dll) s_dll = LoadLibraryA("steam_api.dll");
    if (!s_dll) {
        s_status = 2;
        snprintf(s_statusText, sizeof(s_statusText), "Steam: steam_api64.dll not found");
        return 0;
    }

    int missing = 0;
    RESOLVE(HSteamUser (*)(void),        p_SteamAPI_GetHSteamUser, "SteamAPI_GetHSteamUser");
    RESOLVE(HSteamPipe (*)(void),        p_SteamAPI_GetHSteamPipe, "SteamAPI_GetHSteamPipe");
    RESOLVE(bool (*)(void),              p_SteamAPI_Init,          "SteamAPI_Init");
    RESOLVE(void (*)(void),              p_SteamAPI_Shutdown,      "SteamAPI_Shutdown");
    RESOLVE(void (*)(void),              p_SteamAPI_RunCallbacks,  "SteamAPI_RunCallbacks");
    RESOLVE(int (*)(unsigned int),       p_SteamAPI_RestartAppIfNecessary, "SteamAPI_RestartAppIfNecessary");

    RESOLVE(void* (*)(void),             p_SteamUserStats,         "SteamAPI_SteamUserStats_v012");
    RESOLVE(int (*)(void*, const char*), p_ISteamUserStats_SetAchievement,
            "SteamAPI_ISteamUserStats_SetAchievement");
    RESOLVE(int (*)(void*, const char*, int*), p_ISteamUserStats_GetAchievement,
            "SteamAPI_ISteamUserStats_GetAchievement");
    RESOLVE(int (*)(void*),              p_ISteamUserStats_StoreStats,
            "SteamAPI_ISteamUserStats_StoreStats");
    RESOLVE(int (*)(void*, const char*, int), p_ISteamUserStats_SetStatInt32,
            "SteamAPI_ISteamUserStats_SetStatInt32");   /* 可选，缺了不影响主功能 */

    RESOLVE(void* (*)(void),             p_SteamRemoteStorage,     "SteamAPI_SteamRemoteStorage_v016");
    RESOLVE(int (*)(void*, const char*, const void*, int),
            p_ISteamRemoteStorage_FileWrite, "SteamAPI_ISteamRemoteStorage_FileWrite");
    RESOLVE(int (*)(void*, const char*, void*, int),
            p_ISteamRemoteStorage_FileRead, "SteamAPI_ISteamRemoteStorage_FileRead");
    RESOLVE(int (*)(void*, const char*),
            p_ISteamRemoteStorage_GetFileSize, "SteamAPI_ISteamRemoteStorage_GetFileSize");
    RESOLVE(int (*)(void*, const char*),
            p_ISteamRemoteStorage_FileExists, "SteamAPI_ISteamRemoteStorage_FileExists");
    RESOLVE(int (*)(void*, unsigned long long*, unsigned long long*),
            p_ISteamRemoteStorage_GetQuota, "SteamAPI_ISteamRemoteStorage_GetQuota");

    RESOLVE(void* (*)(void),             p_SteamMusic,             "SteamAPI_SteamMusic_v001");
    RESOLVE(void (*)(void*),             p_ISteamMusic_Pause,      "SteamAPI_ISteamMusic_Pause");
    RESOLVE(void (*)(void*),             p_ISteamMusic_Play,       "SteamAPI_ISteamMusic_Play");
    RESOLVE(int (*)(void*),              p_ISteamMusic_IsPlaying,  "SteamAPI_ISteamMusic_IsPlaying");

    RESOLVE(void* (*)(void),             p_SteamFriends,           "SteamAPI_SteamFriends_v017");
    RESOLVE(void (*)(void*, const char*), p_ISteamFriends_ActivateGameOverlay,
            "SteamAPI_ISteamFriends_ActivateGameOverlay");
    RESOLVE(int (*)(void*, const char*, const char*), p_ISteamFriends_SetRichPresence,
            "SteamAPI_ISteamFriends_SetRichPresence");   /* 可选 */
    RESOLVE(void (*)(void*), p_ISteamFriends_ClearRichPresence,
            "SteamAPI_ISteamFriends_ClearRichPresence"); /* 可选 */

    if (missing > 0) {
        s_status = 3;
        snprintf(s_statusText, sizeof(s_statusText),
                 "Steam: dll found, %d symbols missing", missing);
        return 0;
    }

    if (!p_SteamAPI_Init()) {
        s_status = 4;
        snprintf(s_statusText, sizeof(s_statusText), "Steam: SteamAPI_Init failed (not running via Steam?)");
        return 0;
    }

    s_ready = 1;
    s_status = 1;
    snprintf(s_statusText, sizeof(s_statusText), "Steam: connected");
    return 1;
#endif
}

void SteamZoneShutdown(void) {
#if CANITY_STEAM
    if (s_ready && p_SteamAPI_Shutdown) p_SteamAPI_Shutdown();
#endif
    s_ready = 0;
    s_status = 0;
    snprintf(s_statusText, sizeof(s_statusText), "Steam: off");
}

void SteamZoneTick(float dt) {
    static float retry=0.0f;
    retry-=dt;
    if(s_ready && retry<=0.0f) {
        retry=10.0f;
        for(int i=0;i<ACH_COUNT;++i) if(g_achievements[i].unlocked) SteamZoneUnlockAchievement(kAchDefs[i].steamId);
    }
#if CANITY_STEAM
    if (s_ready && p_SteamAPI_RunCallbacks) p_SteamAPI_RunCallbacks();
#endif
}

int SteamZoneReady(void) { return s_ready; }

/* ---------------- 成就 ---------------- */
int SteamZoneUnlockAchievement(const char* apiName) {
    if (!s_ready || !apiName || !apiName[0]) return 0;
    if (achCached(apiName)) return 0;
#if CANITY_STEAM
    void* stats = p_SteamUserStats ? p_SteamUserStats() : NULL;
    if (!stats) return 0;
    if (!p_ISteamUserStats_SetAchievement(stats, apiName)) return 0;
    p_ISteamUserStats_StoreStats(stats);     /* 必须 Store 才会真正提交 */
    achRemember(apiName);
    return 1;
#else
    (void)apiName;
    return 0;
#endif
}

int SteamZoneHasAchievement(const char* apiName) {
    if (!s_ready || !apiName) return 0;
    if (achCached(apiName)) return 1;
#if CANITY_STEAM
    void* stats = p_SteamUserStats ? p_SteamUserStats() : NULL;
    if (!stats) return 0;
    int got = 0;
    if (p_ISteamUserStats_GetAchievement(stats, apiName, &got)) {
        if (got) { achRemember(apiName); return 1; }
    }
#endif
    return 0;
}

/* ---------------- 云存档 ---------------- */
int SteamZoneCloudUpload(const char* name, const void* data, int bytes) {
    if (!s_ready || !name || !data || bytes <= 0) return 0;
#if CANITY_STEAM
    void* rs = p_SteamRemoteStorage ? p_SteamRemoteStorage() : NULL;
    if (!rs) return 0;
    return p_ISteamRemoteStorage_FileWrite(rs, name, data, bytes) ? 1 : 0;
#else
    (void)name; (void)data; (void)bytes;
    return 0;
#endif
}

int SteamZoneCloudDownload(const char* name, void* buf, int bufSize) {
    if (!s_ready || !name || !buf || bufSize <= 0) return 0;
#if CANITY_STEAM
    void* rs = p_SteamRemoteStorage ? p_SteamRemoteStorage() : NULL;
    if (!rs) return 0;
    int sz = p_ISteamRemoteStorage_GetFileSize(rs, name);
    if (sz <= 0 || sz > bufSize) return 0;
    int got = p_ISteamRemoteStorage_FileRead(rs, name, buf, bufSize);
    return got > 0 ? got : 0;
#else
    (void)name; (void)buf; (void)bufSize;
    return 0;
#endif
}

/* ==================================================================
 * 2026-11-06（v2.4-alpha R7）：**只读的大小查询**
 * ------------------------------------------------------------------
 * SteamZoneCloudDownload 内部早就在调 GetFileSize，只是不外露。
 * 云同步要「不假设远端长度等于本地」，就需要单独拿到这个数。
 * 这里不造第二套传输 API，只是把已有的大小读出来。
 *
 * 上限用 STEAM_CLOUD_MAX_BYTES（预算，不是产品承诺）：
 * 超过就当读不出来 —— 总比 malloc 一个荒唐的大小好。
 * ================================================================== */
#define STEAM_CLOUD_MAX_BYTES  (1024 * 1024)

int SteamZoneCloudGetSize(const char* name) {
    if (!s_ready || !name || !name[0]) return 0;
#if CANITY_STEAM
    void* rs = p_SteamRemoteStorage ? p_SteamRemoteStorage() : NULL;
    if (!rs) return 0;
    int sz = p_ISteamRemoteStorage_GetFileSize(rs, name);
    if (sz <= 0 || sz > STEAM_CLOUD_MAX_BYTES) return 0;
    return sz;
#else
    (void)name;
    return 0;
#endif
}

int SteamZoneCloudExists(const char* name) {
    if (!s_ready || !name) return 0;
#if CANITY_STEAM
    void* rs = p_SteamRemoteStorage ? p_SteamRemoteStorage() : NULL;
    if (!rs) return 0;
    return p_ISteamRemoteStorage_FileExists(rs, name) ? 1 : 0;
#else
    (void)name;
    return 0;
#endif
}

long long SteamZoneCloudQuotaFree(void) {
    if (!s_ready) return -1;
#if CANITY_STEAM
    void* rs = p_SteamRemoteStorage ? p_SteamRemoteStorage() : NULL;
    if (!rs) return -1;
    unsigned long long total = 0, avail = 0;
    if (!p_ISteamRemoteStorage_GetQuota(rs, &total, &avail)) return -1;
    return (long long)avail;
#else
    return -1;
#endif
}

/* ---------------- 音乐 ---------------- */
void SteamZoneMusicPause(int pause) {
    if (!s_ready) return;
#if CANITY_STEAM
    void* mu = p_SteamMusic ? p_SteamMusic() : NULL;
    if (!mu) return;
    if (pause) p_ISteamMusic_Pause(mu);
    else       p_ISteamMusic_Play(mu);
#else
    (void)pause;
#endif
}

int SteamZoneMusicPlaying(void) {
    if (!s_ready) return 0;
#if CANITY_STEAM
    void* mu = p_SteamMusic ? p_SteamMusic() : NULL;
    if (!mu) return 0;
    return p_ISteamMusic_IsPlaying(mu) ? 1 : 0;
#else
    return 0;
#endif
}

/* ---------------- 覆盖层 ---------------- */
void SteamZoneOpenOverlay(const char* dialog) {
    if (!s_ready) return;
#if CANITY_STEAM
    void* fr = p_SteamFriends ? p_SteamFriends() : NULL;
    if (!fr) return;
    p_ISteamFriends_ActivateGameOverlay(fr, dialog ? dialog : "");
#else
    (void)dialog;
#endif
}

/* ==================================================================
 *  v5 新增实现
 * ================================================================== */

/* ---- 云存档冲突解决 ----
 *
 * 调研结论：**时间戳单独不能决定用哪一边**。
 * 所以我们在文件头塞一个 16 字节的小结构：
 *
 *    magic      'CSAV'
 *    version    存档格式版本
 *    stamp      写入时的 time()
 *    sum        FNV-1a 校验和（覆盖 payload）
 *
 * 判定顺序：
 *   ① 云端没有            → 上传本地
 *   ② 校验和相同          → 内容一致，什么都不做
 *   ③ 云端 stamp 更新     → 云端赢（返回 CLOUDWIN，调用方先备份本地）
 *   ④ 否则                → 本地赢，上传
 *
 * 注意：**永远不删本地**。云端赢的时候也是先备份再覆盖。
 */
/* ==================================================================
 * 2026-11-05：**信封上移到存档分区**
 * ------------------------------------------------------------------
 * 这里原来私有一份 SteamCloudHead + STEAM_CLOUD_MAGIC + fnv1a。
 * 现在它们住在 include/save_format.h，由**存档分区**所有：
 *   · 存档写盘时就把信封封好（SaveEnvelopeSeal），文件本身就是云格式；
 *   · Steam 专区只消费这份字节，不再现场合成。
 * 于是「存档版本」和「云存档格式」只有**一个**权威来源（铁律十八）。
 *
 * 依赖方向是单向的：
 *   src/save/  ──调用──>  src/steam/        （上传 / 下载）
 *   src/steam/ ──include──> save_format.h   （只共享类型，不反向调用）
 * ================================================================== */
#include "save_format.h"

/* 旧名字保留成本文件的别名，避免这一次改动同时动 60 行调用点。
   新代码请直接用 SaveEnvelope / SAVE_ENVELOPE_MAGIC / SaveFnv1a。 */
typedef SaveEnvelope SteamCloudHead;
#define STEAM_CLOUD_MAGIC SAVE_ENVELOPE_MAGIC
#define fnv1a(p, n)       SaveFnv1a((p), (n))

/* ==================================================================
 * 2026-11-06（v2.4-alpha R8）：**纯判定**
 * ------------------------------------------------------------------
 * 从 SteamZoneCloudSync 里拆出来的原因只有一个：**要能单测**。
 * 之前的逻辑依赖 s_ready，没有 Steam 客户端就整段 no-op，
 * 于是"长度不同的合法存档不会判损坏"这条根本没法验证 ——
 * 只能靠读代码，那正是审查反复警告的"假绿"。
 *
 * 这里只做字节层面的判断，不传输、不落盘、不看 Steam 状态。
 * ================================================================== */
int SteamZoneCloudDecide(const void* local, int localLen,
                         const void* remote, int remoteLen,
                         int* outLen) {
    if (!local || !remote) return STEAM_SYNC_ERROR;
    if (localLen <= (int)sizeof(SteamCloudHead)) return STEAM_SYNC_ERROR;
    if (remoteLen <= (int)sizeof(SteamCloudHead)) return STEAM_SYNC_ERROR;

    const SteamCloudHead* lh = (const SteamCloudHead*)local;
    SteamCloudHead ch;
    memcpy(&ch, remote, sizeof(ch));

    int localPayload  = localLen  - (int)sizeof(SteamCloudHead);
    int remotePayload = remoteLen - (int)sizeof(SteamCloudHead);

    /* 本地自己必须自洽 */
    if (lh->magic != STEAM_CLOUD_MAGIC || lh->payload != localPayload) return STEAM_SYNC_ERROR;
    if (lh->sum != fnv1a((const unsigned char*)local + sizeof(SteamCloudHead), localPayload))
        return STEAM_SYNC_ERROR;

    /* 远端按**自己的**长度独立校验 —— 长度不同不是损坏证据 */
    if (ch.magic != STEAM_CLOUD_MAGIC || ch.payload != remotePayload) return STEAM_SYNC_ERROR;
    if (ch.version != lh->version) return STEAM_SYNC_ERROR;
    if (ch.sum != fnv1a((const unsigned char*)remote + sizeof(SteamCloudHead), remotePayload))
        return STEAM_SYNC_ERROR;

    /* 内容一致（长度可以不同：同内容必然同长，这里仍两边都比一次） */
    if (ch.sum == lh->sum && remotePayload == localPayload) return STEAM_SYNC_SAMECL;

    /* stamp 只到秒：同秒不同内容 = 无法裁决 */
    if (ch.stamp == lh->stamp) return STEAM_SYNC_CONFLICT;

    if (ch.stamp > lh->stamp) {
        if (outLen) *outLen = remoteLen;
        return STEAM_SYNC_CLOUDWIN;
    }
    return STEAM_SYNC_LOCALWIN;
}

int SteamZoneCloudSync(const char* name, void* localBuf, int* bufSize) {
    if (!s_ready || !name || !localBuf || !bufSize) return STEAM_SYNC_ERROR;
    int cap = *bufSize;
    if (cap <= (int)sizeof(SteamCloudHead)) return STEAM_SYNC_ERROR;

    /* ① 云端没有 → 上传 */
    if (!SteamZoneCloudExists(name)) {
        return SteamZoneCloudUpload(name, localBuf, cap)
             ? STEAM_SYNC_NOCLOUD : STEAM_SYNC_ERROR;
    }

    /* ② 取**远端自己的长度**，不假设等于本地 */
    int rsize = SteamZoneCloudGetSize(name);
    if (rsize <= (int)sizeof(SteamCloudHead)) return STEAM_SYNC_ERROR;

    unsigned char* remote = (unsigned char*)malloc((size_t)rsize);
    if (!remote) return STEAM_SYNC_ERROR;
    int got = SteamZoneCloudDownload(name, remote, rsize);
    if (got != rsize) { free(remote); return STEAM_SYNC_ERROR; }

    /* ③ 判定（纯函数，已被 test_runtime 单测） */
    int winLen = 0;
    int verdict = SteamZoneCloudDecide(localBuf, cap, remote, rsize, &winLen);

    if (verdict == STEAM_SYNC_CLOUDWIN) {
        if (winLen <= 0 || winLen > cap) { free(remote); return STEAM_SYNC_CONFLICT; }
        memcpy(localBuf, remote, (size_t)winLen);
        *bufSize = winLen;
        free(remote);
        return STEAM_SYNC_CLOUDWIN;
    }

    free(remote);

    if (verdict == STEAM_SYNC_LOCALWIN) {
        return SteamZoneCloudUpload(name, localBuf, cap)
             ? STEAM_SYNC_LOCALWIN : STEAM_SYNC_ERROR;
    }
    return verdict;
}

/* ---- 统计 ---- */
void SteamZoneSetStatInt(const char* statName, int value) {
    if (!s_ready || !statName) return;
#if CANITY_STEAM
    void* stats = p_SteamUserStats ? p_SteamUserStats() : NULL;
    if (!stats || !p_ISteamUserStats_SetStatInt32) return;
    p_ISteamUserStats_SetStatInt32(stats, statName, value);
#else
    (void)statName; (void)value;
#endif
}

void SteamZoneStoreStats(void) {
    if (!s_ready) return;
#if CANITY_STEAM
    void* stats = p_SteamUserStats ? p_SteamUserStats() : NULL;
    if (stats && p_ISteamUserStats_StoreStats) p_ISteamUserStats_StoreStats(stats);
#endif
}

/* ---- 富状态 ---- */
void SteamZoneSetRichPresence(const char* key, const char* value) {
    if (!s_ready || !key) return;
#if CANITY_STEAM
    void* fr = p_SteamFriends ? p_SteamFriends() : NULL;
    if (!fr || !p_ISteamFriends_SetRichPresence) return;
    p_ISteamFriends_SetRichPresence(fr, key, value ? value : "");
#else
    (void)key; (void)value;
#endif
}

/* ---- 音乐闪避 ---- */
static float s_duckT = 0.0f;
static int   s_ducked = 0;

void SteamZoneMusicDuck(float seconds) {
    if (seconds <= 0.0f) return;
    s_duckT = seconds;
    if (!s_ducked && SteamZoneMusicPlaying()) {
        SteamZoneMusicPause(1);
        s_ducked = 1;
    }
}

void SteamZoneMusicDuckTick(float dt) {
    if (s_duckT <= 0.0f) return;
    s_duckT -= dt;
    if (s_duckT <= 0.0f) {
        s_duckT = 0.0f;
        if (s_ducked) {
            SteamZoneMusicPause(0);
            s_ducked = 0;
        }
    }
}

/* ---- 按表下标解锁（比传字符串安全）---- */
int SteamZoneUnlockAchievementIndex(int index) {
    if (index < 0 || index >= kSteamAchievementCount) return 0;
    return SteamZoneUnlockAchievement(kSteamAchievements[index].apiName);
}

const char* SteamZoneStatusText(void) { return s_statusText; }

static void* ReadLocalEnvelope(const char* path,int* bytes) {
    FILE* f=fopen(path,"rb");if(!f) return NULL;
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    if(n<=0 || n>1024*1024) {fclose(f);return NULL;}

    /* 2026-11-05：**文件本身可能已经是信封格式了**。
       存档分区从这一版起在写盘时就封好信封，所以上传路径应当原样搬运。
       判断方式：读出前 4 字节，等于 SAVE_ENVELOPE_MAGIC 且整份自洽 → 直接用。 */
    {
        unsigned char* raw=(unsigned char*)malloc((size_t)n);
        if(!raw) {fclose(f);return NULL;}
        if(fread(raw,1,(size_t)n,f)!=(size_t)n) {free(raw);fclose(f);return NULL;}
        if(n>(long)sizeof(SaveEnvelope)) {
            SaveEnvelope h;memcpy(&h,raw,sizeof(h));
            if(h.magic==SAVE_ENVELOPE_MAGIC && h.payload==(int)(n-(long)sizeof(h)) &&
               SaveEnvelopeValid(&h,raw+sizeof(h),h.payload)) {
                fclose(f);*bytes=(int)n;return raw;    /* 已经是云格式，原样返回 */
            }
        }
        free(raw);
        rewind(f);
    }

    unsigned char* p=(unsigned char*)malloc(sizeof(SteamCloudHead)+(size_t)n);
    if(!p) {fclose(f);return NULL;}
    if(fread(p+sizeof(SteamCloudHead),1,(size_t)n,f)!=(size_t)n) {free(p);fclose(f);return NULL;}
    fclose(f);WIN32_FILE_ATTRIBUTE_DATA info;unsigned stamp=0;
    if(GetFileAttributesExA(path,GetFileExInfoStandard,&info)) {
        ULARGE_INTEGER t;t.LowPart=info.ftLastWriteTime.dwLowDateTime;t.HighPart=info.ftLastWriteTime.dwHighDateTime;
        stamp=(unsigned)(t.QuadPart/10000000ULL-11644473600ULL);
    }
    SteamCloudHead h={STEAM_CLOUD_MAGIC,SAVE_CONTAINER_VERSION,stamp,fnv1a(p+sizeof(h),(int)n),(int)n};memcpy(p,&h,sizeof(h));
    *bytes=(int)(sizeof(h)+n);return p;
}
void SteamZoneUploadSave(const char* path) {
    if(!s_ready || !path) return;
    int bytes=0;void* p=ReadLocalEnvelope(path,&bytes);if(!p) return;
    const char* name=strrchr(path,'\\');name=name?name+1:path;
    SteamZoneCloudUpload(name,p,bytes);free(p);
}
/* 2026-11-05：云->本地的回写已搬到**存档分区的后端**
   （src/save/save_backend.c 的 SaveBackendSyncAll）。

   为什么搬：这一步要**写本地存档文件**，而存档文件只允许后端写
   （原来这里用裸 fopen，绕过了 tmp+fsync+原子改名和 .bak 链）。
   现在 Steam 专区只做它该做的事：传输原语 + 冲突判定。

   对外接口变化：调用方把 SteamZoneSyncLocalSaves() 换成 SaveBackendSyncAll()。 */


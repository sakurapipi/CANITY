/* ==================================================================
 *  ███████ ████████ ███████  █████  ███    ███
 *  ██         ██    ██      ██   ██ ████  ████
 *  ███████    ██    █████   ███████ ██ ████ ██
 *       ██    ██    ██      ██   ██ ██  ██  ██
 *  ███████    ██    ███████ ██   ██ ██      ██
 *
 *  STEAM 专区 —— 所有 Steam 平台对接代码的唯一入口
 * ==================================================================
 *
 *  设计原则（很重要）
 *  ────────────────
 *  ① **默认关闭**：CANITY_STEAM 没开时，全部是空函数，
 *     游戏照常跑、照常单 exe 发。Steam 版只是多打开一个开关。
 *
 *  ② **不引入 SDK 头文件**：Steamworks 的 steam_api.h 是 C++ 的，
 *     steam_api_flat.h 也要求 SDK 目录在 include 路径里。
 *     这里改成**运行时动态加载 steam_api64.dll**，
 *     用 GetProcAddress 取扁平 C API —— 于是：
 *       · 不需要 .lib / .a 导入库（MinGW 最头疼的部分）
 *       · 没有 DLL 就自动降级成"非 Steam 模式"，不会崩
 *
 *  ③ **所有调用点只认这一份头文件**，不许在别处直接写 SteamAPI_*。
 *
 *  对应关系（详细调研见 src/steam/README.md）
 *  ────────────────────────────────────────────
 *    成就      ISteamUserStats::SetAchievement + StoreStats
 *    云存档    ISteamRemoteStorage::FileWrite / FileRead
 *    音乐      ISteamMusic::Pause / SetVolume（控制 Steam 自带播放器）
 *    统计      ISteamUserStats::SetStat / StoreStats
 *    覆盖层    ISteamFriends::ActivateGameOverlay
 */
#ifndef CANITY_STEAM_ZONE_H
#define CANITY_STEAM_ZONE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 编译开关：0 = 不接 Steam（默认），1 = 开启 ---- */
#ifndef CANITY_STEAM
#define CANITY_STEAM 0
#endif

/* ---- 初始化 / 每帧 ---- */

/* 返回 1 = 连上 Steam；0 = 没连上（没开、没 DLL、没登录都会返回 0） */
int  SteamZoneInit(void);
void SteamZoneSyncLocalSaves(void);
void SteamZoneUploadSave(const char* path);
void SteamZoneShutdown(void);

/* 每帧调用 —— 泵 Steam 的回调队列。没初始化时是空操作。 */
void SteamZoneTick(float dt);

/* 是否真的连上了 Steam */
int  SteamZoneReady(void);

/* ---- 成就 ---- */

/* 解锁一个成就（Steam 后台配置的 API Name）。
   内部会做去重，重复调用不会重复上报。
   返回 1 = 本次真的解了；0 = 已经解过 / 没连上 / 没有这个 ID。 */
int SteamZoneUnlockAchievement(const char* apiName);

/* 查询某个成就是否已解锁 */
int SteamZoneHasAchievement(const char* apiName);

/* ---- 云存档 ---- */

/* 把本地文件内容传到 Steam 云。
   name 是云端文件名（建议 "canity0.sav" 这种，不带路径）。
   返回 1 = 成功。 */
int SteamZoneCloudUpload(const char* name, const void* data, int bytes);

/* 从 Steam 云读一个文件。buf 大小不够会失败。
   返回实际读到的字节数；0 = 没有这个文件 / 失败。 */
int SteamZoneCloudDownload(const char* name, void* buf, int bufSize);

/* 云端有没有这个文件 */
int SteamZoneCloudExists(const char* name);

/* 云端剩余配额（字节），失败返回 -1 */
long long SteamZoneCloudQuotaFree(void);

/* ---- 音乐（Steam 自带播放器）---- */

/* 暂停 / 恢复 Steam 的音乐播放器。
   游戏里放重要音效 / 剧情段时用它把玩家的背景音乐压下去。 */
void SteamZoneMusicPause(int pause);

/* 查询 Steam 播放器是否在放歌 */
int  SteamZoneMusicPlaying(void);

/* ---- 覆盖层 ---- */

/* 打开 Steam 覆盖层（成就页 / 社区页…），type: "achievements" / "" */
void SteamZoneOpenOverlay(const char* dialog);


/* ==================================================================
 *  v5 升级：云存档冲突解决 / 统计 / 富状态 / 音乐闪避
 * ==================================================================
 *  调研依据
 *  ────────
 *  · Steam 云端同步发生在**游戏开始前和结束后**，上传在退出后完成
 *  · **时间戳本身不能决定用哪一边** —— 必须同时看设备、会话、内容
 *    → 所以我们用「版本 + 时间戳 + 校验和」三件套，不一致时**先备份本地再覆盖**
 *  · 不是每个游戏都有云存档，文件清单由开发者决定
 *  · 成就要 StoreStats 才真正提交
 * ================================================================== */

/* ---- 云存档同步结果 ---- */
enum {
    STEAM_SYNC_NOCLOUD  = 0,   /* 云端没有 → 已把本地传上去 */
    STEAM_SYNC_SAMECL   = 1,   /* 两边一致，什么都没做 */
    STEAM_SYNC_CLOUDWIN = 2,   /* 云端更新 → 已写进 localBuf，调用方负责备份 */
    STEAM_SYNC_LOCALWIN = 3,   /* 本地更新 → 已上传 */
    STEAM_SYNC_ERROR    = 4    /* 出错，调用方按本地继续 */
};

/* 带冲突判定的云同步。
   localBuf 既是输入也是输出：CLOUDWIN 时它会被云端内容覆盖。
   返回上面那个 enum。 */
int SteamZoneCloudSync(const char* name, void* localBuf, int bufSize);

/* ---- 统计（Steamworks Stats）---- */

void SteamZoneSetStatInt(const char* statName, int value);
void SteamZoneStoreStats(void);

/* ---- 富状态（好友列表里显示你在第几层）---- */

void SteamZoneSetRichPresence(const char* key, const char* value);

/* ---- 音乐闪避 ----
   进入重要段落时把 Steam 播放器压低/暂停，几秒后自动恢复。
   不需要手动配对 Begin/End —— 传一个持续秒数就行。 */
void SteamZoneMusicDuck(float seconds);
void SteamZoneMusicDuckTick(float dt);

/* ---- 成就表（见 steam_ach.h）---- */

/* 按"成就表下标"解锁，比传字符串安全 */
int SteamZoneUnlockAchievementIndex(int index);

/* ---- 调试 ---- */

/* 返回当前连接状态的一句话描述，给设置页显示用 */
const char* SteamZoneStatusText(void);

#ifdef __cplusplus
}
#endif

#endif /* CANITY_STEAM_ZONE_H */

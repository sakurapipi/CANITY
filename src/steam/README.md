# STEAM 专区 —— 调研报告 + 对接指南

> 日期：2026-11-02
> 状态：**代码骨架已建好**（`include/steam_zone.h` + `src/steam/steam_zone.c`）
> 编译开关：`CANITY_STEAM`（默认 **0**，不影响单 exe 发布）

---

## 0. 一句话结论

**我们的项目（C99 + Win32 + MinGW + 单 exe）要接 Steam，最稳的路子不是链 SDK 的 .lib，
而是运行时 `LoadLibrary("steam_api64.dll")` + `GetProcAddress` 取扁平 C API。**

理由见第 2 节。

---

## 1. Steamworks 到底提供什么

| 功能 | 接口 | 对我们的用途 |
|---|---|---|
| **成就** | `ISteamUserStats::SetAchievement` / `StoreStats` | 24 个成就 |
| **统计** | `ISteamUserStats::SetStat` | 到达层数、总击杀、总金币 |
| **云存档** | `ISteamRemoteStorage::FileWrite/FileRead/GetQuota` | `canity0.sav` 上传 |
| **音乐** | `ISteamMusic::Pause/Play/IsPlaying` | **只能控制 Steam 自带播放器**，见第 5 节 |
| **覆盖层** | `ISteamFriends::ActivateGameOverlay` | 打开成就页 / 社区页 |
| **控制器** | `ISteamInput` | 手柄（本作键鼠为主，暂不做） |
| **创意工坊** | `ISteamUGC` | 自制地图（远期） |

---

## 2. 为什么用「动态加载」而不是链 .lib

### 问题

```
Steamworks SDK 只提供 MSVC 的 steam_api64.lib
我们用的是 w64devkit（MinGW / GCC）
```

三条路：

| 方案 | 做法 | 问题 |
|---|---|---|
| A | `dlltool` 把 .lib 转成 .a | 构建链要加一步，且**玩家没 Steam 时整个 exe 起不来** |
| B | 静态链 SDK | 违反 SDK 的再分发条款，且体积涨 |
| C | **`LoadLibrary` + `GetProcAddress`** | ✅ 采用 |

### 方案 C 的好处

```
· 不需要导入库，MinGW 原生支持
· 不需要把 SDK 目录加进 include 路径
· 没有 steam_api64.dll → 自动降级成单机模式，**游戏照常跑**
· 单 exe 发布不受影响
```

### 代价

函数名是**字符串**，写错编译期查不出来。

**对策**：`SteamZoneInit()` 里把所有符号**一次性全解析**并统计缺失数，
缺一个就整体标记为"符号不全"，而不是半个功能能用。这样出错时状态一目了然。

---

## 3. 初始化流程

```c
// 启动时（WinMain 早期）
if (SteamZoneInit()) {
    // 连上了：可以开始用成就 / 云存档
}

// 每帧
SteamZoneTick(dt);      // 泵回调队列，不调 Steam 的异步操作不会完成

// 退出时
SteamZoneShutdown();
```

### 开发期必须放一个 `steam_appid.txt`

```
项目根目录 / exe 同目录放一个 steam_appid.txt，内容就是 AppID 数字。
不放 → SteamAPI_Init() 失败（状态 4）。
正式发布时由 Steam 客户端自动提供，不需要这个文件。
```

### `SteamAPI_RestartAppIfNecessary`

```
如果玩家是直接双击 exe（不是从 Steam 启动），这个函数会
让 Steam 重新拉起游戏。我们已经在符号表里解析了它，
但**默认不调用** —— 因为单 exe 版还要能脱离 Steam 单独跑。
开启 Steam 版后可以在 Init 前调用。
```

---

## 4. 成就系统对接

### 4.1 流程

```c
SteamZoneUnlockAchievement("ACH_FIRST_ESCAPE");
```

内部做三件事：

```
① 查本地去重表（同名不再上报）
② SetAchievement(apiName)
③ StoreStats()   ← 不做这一步 Steam 不会真的记账
```

### 4.2 Steam 后台要配的东西

在 Steamworks 后台 → 成就 → 新建成就，**API Name 必须和代码里的字符串完全一致**。

建议命名（对应 v4 方案的 24 个成就）：

```
ACH_SURVIVE_01_FIRST_EXIT
ACH_SURVIVE_02_FLOOR_5
...
ACH_ECON_01_CARRY_20K
...
ACH_SKILL_01_PERFECT_ESCAPE
...
ACH_SECRET_01_UNDER_WARDEN
```

### 4.3 和本地成就系统的关系

```
本地成就（game.c 的 g_achievements）是**真相源**
Steam 成就只是镜像 —— 本地解了才上报，绝不能用 Steam 状态覆盖本地
```

理由：玩家可能先玩单机版（没 Steam），后来才在 Steam 上玩。

---

## 5. 音乐对接 —— 这里有个重要澄清

### 5.1 `ISteamMusic` **不是音乐中间件**

查 Steamworks 文档，`ISteamMusic` 的原话是：

> "Functions to control music playback in the steam client.
>  This interface allows games to pause, play, or adjust the volume of
>  music playing in the Steam client."

**它控制的是「Steam 客户端自带的音乐播放器」，不是给你的游戏提供音乐流。**

所以对我们只有一种用法：

```c
// 玩家在 Steam 里放歌时，游戏进入重要段落 → 暂停掉
SteamZoneMusicPause(1);
// 段落结束 → 恢复
SteamZoneMusicPause(0);
```

### 5.2 我们的音乐/音效本来就在代码里合成

```
src/audio.c —— 全部程序化合成（正弦/噪声/包络），零外部资源
```

**这条路不需要改** —— Steam 不提供音频合成/混音服务。

### 5.3 如果以后要空间音频

有 **Steam Audio**（独立 SDK，不是 Steamworks 的一部分），
提供 HRTF / 混响 / 遮挡。但：

```
· 它是独立 SDK，要额外的 DLL
· 我们的音频是即时合成的短音效，不是长音频资源
· 收益不明显（恐怖游戏靠的是低频压迫，不是精确定位）
```

**结论：暂不接，只在重要段落用 ISteamMusic 压掉 Steam 播放器。**

---

## 6. 云存档对接

### 6.1 接口

```c
SteamZoneCloudUpload("canity0.sav", data, bytes);   // 上传
SteamZoneCloudDownload("canity0.sav", buf, size);   // 下载
SteamZoneCloudExists("canity0.sav");
SteamZoneCloudQuotaFree();                          // 剩余配额
```

### 6.2 推荐的同步策略（重要）

```
启动时：
  ① 读本地 canity0.sav
  ② 如果云端有、且云端的修改时间更新 → 下载覆盖本地（先备份本地为 .bak）
  ③ 否则用本地

存档时：
  ① 先写本地（保证断电也不丢）
  ② 再上传云端（失败不影响本地）
```

**绝不要"只在云端存"** —— Steam 掉线、配额满、玩家换机器都会丢档。

### 6.3 配额

```
Steam 默认每个 App 的云存档配额是 1 GB（可在后台调整）
我们的存档：SaveBlob ≈ 几十 KB（含两张 24×24 的 int 地图）
→ 完全够用，不需要清理逻辑
```

### 6.4 文件名

```
只能用 Steam 允许的字符，**不能带路径分隔符**
我们三个存档槽 → canity0.sav / canity1.sav / canity2.sav
```

---

## 7. 代码分区结构

```
include/
  steam_zone.h          ← 唯一对外入口（所有调用点只认这个）

src/steam/
  steam_zone.c          ← 实现：动态加载 + 全部包装函数
  README.md             ← 本文
```

### 7.1 为什么先放一个文件

现在只有包装层（~350 行），拆成 5 个文件反而难读。
等真正接入（成就表 / 云存档策略 / 统计上报）之后再拆：

```
steam_zone.c        →  steam_core.c      动态加载 + Init/Tick
                       steam_ach.c       成就表 + 去重 + 上报
                       steam_cloud.c     云存档 + 冲突解决
                       steam_music.c     ISteamMusic
                       steam_stats.c     统计上报
```

### 7.2 铁律

```
① 任何 .c 文件里**不许**直接出现 SteamAPI_* 字符串
   —— 全部走 steam_zone.h 的包装函数
② 所有函数在没有 Steam 时必须是**安全空操作**，不许崩
③ 成就/统计以本地为真相源，Steam 只是镜像
```

第 ① 条可以写进自检（正则扫源码），和第 3 节的热键守卫是同一个思路。

---

## 8. 接入步骤（开 Steam 版时按这个顺序）

```
①  申请 AppID，在 Steamworks 后台建 24 个成就（API Name 见 4.2）
②  把 steam_api64.dll 放到 exe 同目录
③  开发期放 steam_appid.txt（内容 = AppID）
④  编译加 -DCANITY_STEAM=1
⑤  WinMain 里加 SteamZoneInit / SteamZoneTick / SteamZoneShutdown
⑥  本地成就解锁处加一行 SteamZoneUnlockAchievement("ACH_xxx")
⑦  存档写完后加一行 SteamZoneCloudUpload(...)
⑧  开 Steam 客户端跑一遍，在后台确认成就和云文件都上去了
```

---

## 9. 待确认

```
① 成就的 API Name 前缀用 ACH_ 还是 CANITY_？
② 云存档冲突策略：云端新 → 覆盖本地（先备份），这条路同意吗？
③ 音乐只在"重要段落"暂停 Steam 播放器 —— 有没有想加的时机？
④ 要不要现在就接 ISteamInput（手柄）？本作键鼠为主，我建议不做。
```

---

## 附录：参考资料

- [ISteamRemoteStorage（Steamworks 官方文档）](https://partner.steamgames.com/doc/api/ISteamRemoteStorage)
- [ISteamMusic（Steamworks 官方文档）](https://partner.steamgames.com/doc/api/ISteamMusic)
- [成就接入指南（Steamworks）](https://partner.steamgames.com/doc/features/achievements/ach_guide)
- Steamworks 的扁平 C 接口（`steam_api_flat.h`）是官方提供的，函数名形如
  `SteamAPI_ISteamUserStats_SetAchievement` —— 我们在动态加载里用的就是这些名字。

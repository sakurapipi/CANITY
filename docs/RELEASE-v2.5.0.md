# CANITY v2.5.0

[简体中文](#简体中文) · [English](#english)

## 简体中文

### 更新内容

- 「失序协议」加入三章 B-17 剧情。维修员、档案员和隔离警卫各有专属能力。通关可免费选一名特殊角色，档案中也可按每人 10,000 银行金币解锁。
- 经典与无尽包含十种特殊事件。图鉴提供怪物、主题房间和事件三类条目。积水回廊优先沿走廊铺设，空间不足时补成相邻水区。
- 三只听声鬼读取同一份声学线索，各自的听力差异仍然生效。金币与威胁、战役修正、遗物及楼层规则只各计算一次。墙体变化会刷新传播结果。
- 特殊事件完成分在实际进入下一层或确认撤离时结算。已经通电的出口金币沿用每层发放一次的规则。

### 存档与下载

存档格式为 v46。来自更新版本的存档会保留原文件。旧八层剧情在途存档会先归档，玩家确认后迁移银行和角色档案。归档名称显示在确认画面，可在保存目录找到它。若曾从备份读档，归档会按对应槽位的 `canityN.sav.retired-story.bak` 命名；同名归档已存在且内容不同时会自动加后缀。RC5 生成的 `canityN.sav.bak.retired-story.bak` 归档也可用。要继续旧剧情，把归档副本改名为 `canityN.sav`，放到 [RC3](https://github.com/sakurapipi/CANITY/releases/tag/v2.5.0-RC3) 的独立存档目录。网页版归档保存在当前站点的浏览器存储，恢复时请使用副本与独立浏览器环境。

- **Windows ZIP** · 完整解压，保留 `music/`，打开 `canity.exe`。
- **Web ZIP** · 解压后用 HTTP 服务打开。
- **单文件 HTML** · 内嵌游戏与配乐。手机可使用线上入口。
- **构建清单** · `build-manifest.json` 记录提交、工具链与附件 SHA-256。
- [网页版与手机试玩](https://sakurapipi.github.io/CANITY/) · [自动验证记录](https://github.com/sakurapipi/CANITY/actions)。

## English

### What's new

- The three-chapter B-17 prologue introduces the Technician, Archivist, and Security Guard, each with a distinct ability. Completing the prologue grants one free special character. Each character can also be purchased individually for 10,000 bank gold.
- Classic and Endless modes feature ten floor events. Enemies, themed rooms, and events have separate archive tabs. Flooded Corridors favors long wet stretches and fills short layouts with adjacent patches.
- The three sound hunters read the same acoustic clues while retaining individual hearing thresholds. Gold, threat, campaign rules, relics, and floor modifiers are applied once per sound. Topology changes invalidate the propagation cache.
- Event completion score is awarded when the player advances or confirms extraction. The powered exit still awards its gold once per floor.

### Saves and downloads

The save format remains v46. Keep a copy of your saves before upgrading. Saves from newer versions are preserved and require a compatible game. An active legacy eight-floor story is archived before migration. Confirm to retain your bank and unlocks. The confirmation shows the archive filename. A legacy run recovered from a backup is archived under the corresponding primary slot name, `canityN.sav.retired-story.bak`; distinct archives receive a suffix. Earlier RC5 archives named `canityN.sav.bak.retired-story.bak` also work. To finish the old route, rename a **copy** of the archive to `canityN.sav` in an isolated [RC3](https://github.com/sakurapipi/CANITY/releases/tag/v2.5.0-RC3) save directory. Browser archives live in site storage. Restore a copy in an isolated browser environment.

- **Windows ZIP** · Extract the complete archive, keep `music/`, and launch `canity.exe`.
- **Web ZIP** · Serve the extracted directory over HTTP.
- **Standalone HTML** · The game and music are embedded. On phones, use the online build.
- **Build manifest** · `build-manifest.json` records the commit, toolchain, and package SHA-256 hashes.
- [Play online](https://sakurapipi.github.io/CANITY/) · [Build and verification history](https://github.com/sakurapipi/CANITY/actions).

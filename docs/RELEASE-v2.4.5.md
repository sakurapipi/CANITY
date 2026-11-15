# CANITY v2.4.5 发布说明

日期：2026-11-16　｜　前序：v2.4.4（e24464c）
存档：`SAVE_VERSION 45` 未变，**旧档可直接沿用**；新增字段仍在结构末尾

---

## 一、这一版做了什么

| 类别 | 内容 |
|---|---|
| 审计修复（9 项） | 存档容器整数溢出、云端首次恢复、云同步长度契约、解耦闸门接 CI、Vertical Look 持久化、手机触屏纵向视角、Dynamic Res 假开关、删档校验、崩溃诊断 |
| 存档可诊断 | 新增 `canity_save.log`：每次保存/读档都记录路径、字节数、错误码、挑中哪一份 |
| 端到端验证 | Vertical Look 与存档读写**在真实游戏里跑通**（见第三节） |

---

## 二、存档问题的真相（这一版最重要的结论）

### 2.1 最初看到的现象

```
· 设置改了 → 重启后变回默认（Vertical Look 重回 OFF）
· 某些启动方式下底部红条：「存档写入失败 - 进度只在本次运行有效」
· 同一份游戏，两个位置的两份存档时间/大小不一致
```

### 2.2 排查过程（四项对照实验）

```
① 独立 C 程序写 %LOCALAPPDATA%\CANITY\saves\...  → errno=13（拒绝）
② PowerShell 写同一路径                          → 成功
③ 独立 C 程序写 exe 目录（项目工作区内）          → 成功
④ 游戏 + CANITY_DATA_DIR=工作区内目录            → 日志与存档都正常写出
```

**结论：写失败不是产品缺陷，而是"从受限环境派生的子进程被拒绝写用户目录"。**
玩家正常双击启动不受此限制。

### 2.3 但确实存在的产品弱点（已修/已改善）

```
① 显式 CANITY_DATA_DIR 且没有 CANITY_DATA_DIR_AUTO 时，6 个候选全是同一路径
   → 目录不可写就全盘失败。main.c 自己的默认目录**会**设 AUTO=1，所以正常启动能回退；
     外部显式指定时仍按"权威目录"处理（本次未改变该语义，避免破坏既有测试契约）。
② 写失败时只有一条红条，没有任何可诊断信息
   → 新增 canity_save.log：路径 / 字节数 / 错误码 / 读档挑中哪一份 / pitchLook 值
```

---

## 三、端到端验证（真实游戏，鼠标助手操作）

```
1. 改 Vertical Look → ON
2. 存档落盘：savetest\canity0.sav  18568 B
3. 重启，日志原文：
   1794776923 LOAD picked=...\savetest\canity0.sav size=18528 version=45 pitchLook=1
   1794776923 BOOT LoadSettings done hasSave=1 state=0 err=0
4. 进设置页截图：**Vertical Look: ON**（不是默认值）
```

`size=18528` 正好等于 `sizeof(SaveBlob)`（`offsetof(pitchLook)=18520`），
说明新字段真实写进了文件、又被读回内存。

---

## 四、新增诊断：canity_save.log

```
位置：exe 目录（固定写这里，不跟随 CANITY_DATA_DIR —— 那正是可能不可写的地方）
格式：<unix 时间戳> <事件>
事件：BOOT / LOAD cand / LOAD picked / SETTINGS cand / PROGRESS cand / FAILED all
用途：下次再遇到"存档丢了"，先看这个文件，一眼定位是"没写"还是"读错文件"
```

---

## 五、审计 11 项对照

| # | 项 | 状态 |
|---|---|---|
| 1 | 存档容器整数截断（P1 内存安全） | ✅ |
| 2 | 新设备云端恢复（P1） | ✅ |
| 3 | 闸门未阻止发布（P1 流程） | ✅ CI/Release 双接入 + baseline |
| 4 | 云同步长度参数（P1） | ✅ 拆成 localLen + capacity |
| 5 | Vertical Look 持久化（P2） | ✅ 已端到端验证 |
| 6 | 手机触屏纵向视角（P2） | ✅ |
| 7 | Dynamic Res 假开关（P2） | ✅ 菜单项隐藏 |
| 8 | 删档未校验（P2） | ✅ |
| 9 | 崩溃诊断（P2） | ✅ uintptr_t + 数据目录兜底 |
| 10 | 检查器自身缺陷（P2） | ✅ 大括号配平 + SOFT_200 参与判定 |
| 11 | 主循环 CPU 空转 | ⏸ 需实测数据，未处理 |

---

## 六、验证结果

```
C99 game build      PASS
partition           OK（S1~S5）
解耦闸门            PASS（已知欠账 2 项，新增 0 项）
VALIDATION SUMMARY  140 passed / 0 failed
```

---

## 七、变更文件

```
include/common.h                版本 2.4.5 / NUM 39；SETTING_COUNT 44；<stdarg.h>（S1）
include/save.h                  SaveBlob 末尾追加 pitchLook；SaveDiag 声明
include/steam_zone.h            云同步契约 localLen + capacity；PullNew
src/save.c                      pitchLook 写入/恢复、删档校验、SaveDiag、SavePathCandSlot 注释
src/save/save_container.c       size_t 溢出安全
src/save/save_backend.c         首次云端恢复 + 容量契约
src/steam/steam_zone.c          Sync 契约拆分 + SteamZoneCloudPullNew
src/steam/steam_zone_web.c      桩同步
src/loop_settings.c             Dynamic Res 菜单项隐藏
src/main.c                      崩溃地址 uintptr_t + 兜底路径；BOOT 诊断
web/shell.html                  触屏纵向视角接通
tools/coupling_gate.py          函数边界配平 / SOFT_200 判定 / baseline 模式
docs/coupling_gate_baseline.txt 新建（当前 2 项已知欠账）
.github/workflows/ci.yml        闸门步骤
.github/workflows/release.yml   发布前闸门
tests/test_canity.c             存档容器溢出回归 3 条
tests/domains/test_update.c     Vertical Look 序列化往返回归 2 条
res/canity.rc                   2,4,5,0 / "2.4.5"
docs/RELEASE-v2.4.5.md          本文件
```

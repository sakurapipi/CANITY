# CANITY v2.4.5-rc1 发布说明（审计修复版）

日期：2026-11-16　｜　前序：v2.4.4（e24464c）
状态：**本地 RC，未推送**　｜　存档：`SAVE_VERSION 45` 未变，v2.4.4 存档可直接读

---

## 一、这一版在修什么

依据 v2.4.4 的第三方审计报告（11 项），逐条处理：

| # | 审计项 | 严重度 | 本版状态 | 证据 |
|---|---|---|---|---|
| 1 | 存档容器整数转换缺陷（`unsigned int` → `int` 截断） | P1 | ✅ 已修 | `save_container.c` 全程 `size_t` + 先判上界再减 |
| 2 | 新设备无法从云端恢复存档 | P1 | ✅ 已修 | 新增 `SteamZoneCloudPullNew()`；`SaveBackendSyncAll` 首次恢复分支 |
| 3 | 解耦闸门未阻止发布 | P1 | ✅ 已修 | `ci.yml` / `release.yml` 双处接入；新增 baseline 机制 |
| 4 | Steam 云同步长度参数错误 | P1 | ✅ 已修 | 契约拆成 `localLen` + `capacity` |
| 5 | Vertical Look 无法持久保存 | P2 | ✅ 已修 | `SaveBlob` 末尾追加 `pitchLook`，写入/恢复/读档判偏移 |
| 6 | 手机版 Vertical Look 无效 | P2 | ✅ 已修 | `web/shell.html` 恢复 `mouseDY` 传递 |
| 7 | Dynamic Res 永久禁用但菜单仍显示 | P2 | ✅ 已修 | 菜单项隐藏，`SETTING_COUNT 45 → 44` |
| 8 | 删除存档未验证结果 | P2 | ✅ 已修 | 检查 `PlatDeletePath` 返回值，失败记 `g_saveError=32` |
| 9 | 崩溃诊断信息不完整 | P2 | ✅ 已修 | 地址改 `uintptr_t`；exe 目录不可写时退回数据目录 |
| 10 | 闸门检查器自身缺陷 | P2 | ✅ 已修 | 函数长度改**大括号配平**；`SOFT_200` 参与判定 |
| 11 | 主循环 CPU 空转风险 | P2（性能） | ⏸ 未处理 | 需实测 60/120/144 FPS 的 CPU 与帧间隔，本轮无测量条件 |
| — | UI / sprites 继续解耦 | 架构 | ⏸ 未处理 | 见第五节 |

---

## 二、逐项说明

### 1. 存档容器容量计算（P1，内存安全）

```c
/* 改前 */
if (need > 0x7FFFFFF0 - (int)desc[i].size) return -1;
need += (int)desc[i].size;

/* 改后 */
size_t sz = (size_t)desc[i].size;
if (sz > (size_t)0x7FFFFFF0u) return -1;
if (need > (size_t)0x7FFFFFF0u - sz) return -1;
need += sz;
```

审计复现：`size = 0xFFFFFFFF` 时容量被算成 19 字节、检查通过、随后 `memcpy` 用无符号原值。
**回归用例**（`tests/test_canity.c`）覆盖三个边界：`UINT_MAX`、`INT_MAX+1`、两块 `0x7FFFFFF0` 累计溢出。

### 2 + 4. Steam 云同步（P1）

```c
/* 改前：一个参数既当本地长度又当缓冲区容量 */
int len = (int)n;
SteamZoneCloudSync(name, buf, &len);

/* 改后：两个参数分开 */
int len = (int)n;                                  /* 出参：最终生效长度 */
SteamZoneCloudSync(name, buf, (int)n, &len);       /* 入参：localLen + capacity */
```

同时补上**新设备首次恢复**：本地读不到存档时不再直接跳过，而是
`SteamZoneCloudPullNew()` 拉云端那份 → 校验信封与校验和 → `SaveBackendWrite()` 原子落盘。

> 注：正式 Windows 发行包未启用 `CANITY_STEAM`，这一项影响的是未来启用 Steam 的构建。

### 5. Vertical Look 持久化（P2，本版新功能闭环）

```c
/* SaveBlob 末尾追加（旧档按偏移判断是否存在，不动既有字段顺序） */
int pitchLook;   /* v2.4.5-rc1 append-only */

/* 写 */
s->pitchLook = pitchLookEnabled ? 1 : 0;
/* 读 */
pitchLookEnabled = info->hasPitchLookField ? (g_save.pitchLook ? 1 : 0) : 0;
```

判据沿用项目既有做法：`info.fileSize >= offsetof(SaveBlob, pitchLook) + sizeof(int)`。
**回归用例**：`SaveTestSerialize` 写入 `pitchLook=1` → `SaveTestDeserialize` 读回断言为 1。

### 6. 手机触屏上下视角（P2）

```js
/* 改前 */
Module._mouseDY = 0;  // Vertical look intentionally disabled.

/* 改后 */
var dy = t.clientY - lastY;
lastY = t.clientY;
Module._mouseDY = (Module._mouseDY || 0) + dy * 2;
```
开关关闭时由游戏侧 `UpdatePitch()` 自行忽略，不需要 JS 再判断一次。

### 7. Dynamic Res 菜单项

`UpdateDynamicRes()` 里 `if (1) return;` 仍然保留 —— 审计说得对：直接删掉这行会
把"UI 缩放漂移"放回来。所以**先隐藏菜单项**，`SETTING_COUNT` 同步 45 → 44。
真正修复要等 3D 分辨率与 UI 布局尺寸解耦之后。

### 8. 删除存档校验

```c
int delMain = PlatDeletePath(cand);
...
if (!delMain || !delBak || !delTmp) { g_saveState = 2; g_saveError = 32; }
```
只把"本该删却失败"记为错误；文件不存在不算失败。**不阻断**其余候选路径的清理。

### 9. 崩溃日志

```c
uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
fprintf(f, "code=0x%08lX base=0x%p addr=0x%p rva=0x%p mark=%d\n", ...);
```
exe 目录只读时退回 `getenv("CANITY_DATA_DIR")`（main.c 早已写入，不新增跨模块接口）。

### 3 + 10. 解耦闸门（流程 + 工具）

**工具本身两处修**：
```
① 函数长度：改为**大括号配平**扫描（原来用"下一个函数声明"估算）
   → 修正后 >300 行的函数从"3 个"变成"0 个"（原判定是误报）
② SOFT_200 原来只打印不参与判定 → 现在超过 10 个就 FAIL
```

**接进流水线**（baseline 模式，符合审计建议"旧债不许增加、新问题立即阻断"）：
```
docs/coupling_gate_baseline.txt   已知欠账清单（当前 2 项）
ci.yml      新增步骤：coupling_gate.py --baseline …
release.yml 发布前同一条检查
```

当前 baseline：
```
域 ui 超预算：3050 > 2500（最大文件 2530）
文件 src/sprites.c 有 3863 行 > 2600
```

---

## 三、验证

```
C99 game build      PASS
partition           OK（S1~S5）
解耦闸门            PASS（已知欠账 2 项，新增 0 项）
VALIDATION SUMMARY  26 passed / 0 failed
新增回归用例        存档容器 3 条边界 + Vertical Look 序列化往返 2 条
```

---

## 四、未处理项（诚实记账）

| 项 | 原因 | 建议 |
|---|---|---|
| 主循环 CPU 空转（#11） | 需要实测 60/120/144 FPS 下的 CPU 占用与帧间隔 P95/P99，本轮无测量环境 | 有数据后再决定是否改等待计时器 |
| UI / sprites 解耦 | 两次拆分尝试失败（技能树、帧末叠加层），根因是**共享静态状态未收敛** | 先把 `kSkillList/skVX../actionError*` 收进结构体，再搬文件 |
| 完整真人玩法验收 | 静态审计 + 自动测试，不等于实机手感 | 交给用户实机 |

---

## 五、下一版建议顺序

```
1. SkillTree / UiToastState 状态收敛 → 再拆 ui_skilltree.c / ui_overlay.c
2. sprites.c 按"怪物烘焙 / 道具与舱门烘焙"拆两块
3. 帧率与 CPU 测量，再决定主循环是否换等待计时器
4. Dynamic Res 真正修复（3D 分辨率与 UI 缩放解耦后）
```

---

## 六、变更文件

```
src/save/save_container.c      容器容量 size_t + 溢出保护
src/save/save_backend.c        首次云端恢复 + 容量契约
src/steam/steam_zone.c         SteamZoneCloudSync 契约拆分 + PullNew
src/steam/steam_zone_web.c     桩同步
include/steam_zone.h           原型
include/save.h                 SaveBlob 追加 pitchLook
src/save.c                     持久化 / 读档 / 删除校验 / fieldSize 判据
src/loop_settings.c            Dynamic Res 菜单项隐藏
include/common.h               SETTING_COUNT 44；版本 2.4.5-rc1 / NUM 38
src/main.c                     崩溃地址 uintptr_t + 兜底目录
web/shell.html                 触屏 mouseDY
tools/coupling_gate.py         函数边界 / 软上限 / baseline
docs/coupling_gate_baseline.txt 新建
.github/workflows/ci.yml       闸门步骤
.github/workflows/release.yml  发布前闸门
tests/test_canity.c            容器溢出回归 3 条
tests/domains/test_update.c    Vertical Look 往返回归 2 条
res/canity.rc                  2,4,5,0
```

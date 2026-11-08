# collapse（崩塌层：整层重掷）

## 职责
锅炉房总闸触发的关卡事故：重掷地形、把玩家和怪物从墙里挪出来、保证仍然可通关。

## 契约
`include/collapse.h` —— 5 个函数 + 2 个状态（`collapsedLevel`、`s_collapseGen`）。
`s_collapseGen` 是**跨域标志**：collapse 置 1，game.c 的 `GenerateLevel` 读到就走进重掷分支。

## 绝不做
- 不直接调 `GenerateLevel` 之外的地形逻辑
- 不在重掷后跳过连通性校验（`CollapseLevelPlayable`）—— 否则会把玩家关进死图
- 不让 game.c 直接读 `collapseArmed`（用 `CollapseBusy()`，第 3 轮已改）

## 测试
`tests/domains/test_collapse.c`：层参数正确（1 个用例）。
关键行为：重掷后可达出口 / 玩家被挪出墙 / 重掷次数有上限。

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 重掷次数上限 | `CollapseBegin` 里的常量 |
| 崩塌持续时长 | `CollapseTick` 的计时 |
| 崩塌层生成参数 | `GenerateLevel`（在 game.c，靠 `collapsedLevel` 分叉） |

## 历史坑
- 重掷可能生成"死口袋"→ 必须有 `CollapseLevelPlayable` 校验 + 再掷。
- 玩家会被埋在墙里 → `CollapseUnstickPlayer` 兜底。

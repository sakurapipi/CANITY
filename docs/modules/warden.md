# warden（守门人：巡逻 / 锁定 / 堵门 / 追击）

## 职责
守门人一个实体的全部行为：状态机（PATROL/RUSH/BLOCK/RETREAT/CHASE/LOST）、
感知（视野锥 + 听觉）、巡逻与寻路、把玩家推开。

## 契约
`include/warden.h` —— 11 个符号。
核心：`UpdateWarden(dt)`（每帧）、`WardenSpawnIfPowered()`（通电后出现）、
`WardenActive/X/Y/Chasing/Spotting`（渲染与 HUD 用的只读查询）。

## 绝不做
- 不在别的模块里读 `warden.` 的内部字段（那个结构体是文件私有的）
- 不在这里处理玩家输入
- 不加新的"隐藏状态"（每个状态都要在 `WardenState` 枚举里）

## 测试
`python tools/build_validate.py` → PASS
关键行为：巡逻会离开出口 ≥3 格 / 锁定需要连续 1.5 秒 / 堵门只推人不拦路。

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 改巡逻范围 | `UpdateWarden` 里的 `turnAt` 与路点半径（4.0~7.9 格） |
| 改视野/听觉半径 | `WARDEN_SIGHT_*` / `WARDEN_HEAR_*`（common.h） |
| 改堵门时长 | `WARDEN_BLOCK_TIME`（18 秒） |
| 改锁定时间 | `wdSpotTimeNow()` |

## 历史坑
- **2026-11-07 卡死在离出口 2.36 格**：沿 BFS 梯度爬到局部最高点后既不前进也不掉头。
  修法是改成"路点制" + 用 `wdStepGradient` 的返回值兜住死点（AD-72）。
  **任何"沿梯度走"的寻路都要有"没挪动就换目标"的兜底**，否则一定会有死点。
- 视野以前是 8 格且不判墙（能隔墙看你），文档写的是 3/6 格。

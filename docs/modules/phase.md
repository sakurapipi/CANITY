# phase（相位信标：一枚 = 一次传送）

## 职责
相位装置的完整生命周期：开局发放、投掷物落地成为锚点、按 Q 传送一次、换层失效。
**不负责**投掷物怎么飞（那是 throwing），也不负责 HUD（那是 ui）。

## 契约
`include/phase.h` —— 6 个函数 + 5 个状态变量。
```c
PhaseGive / PhaseReset / PhaseResetPlacement
PhaseOnThrowLanded(kind, x, y)   // kind 12 = 信标, 13 = 锚
PhaseTryPlace / PhaseTryTeleport
```

## 绝不做
- 不直接读 `Blocked()`（它带玩家半径 0.24，贴墙必真）—— 按**格子**判
- 不改投掷物分派（落地由 throwing 的 `ShotImpact` 调进来）
- 不让"放置"和"传送"用两套判据（AD-68）

## 测试
`tests/domains/test_phase.c`（6 个用例）：一枚一次 / 贴墙能放 / 槽位可选 /
换层清锚点 / 价格百分比 / 完整流程。

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 传送次数 | `PHASE_MAX_CHARGES`（现在 1） |
| 落点容差 | `PhaseOnThrowLanded` 里的 8 邻 × 半径 2 搜索 |
| 传送后的位置修正 | `UnstickPlayer()`（在 game.c，不属本域） |

## 历史坑
- **落地判据 ≠ 传送判据** → 投得下、传不过去，还提示"信标位置已失效"（AD-68）。
- 用 `Blocked()` 判落点 → 贴墙必失败，信标全废（注释里记了两次同样的错）。
- 换层不清 `phasePlaced` → 按 Q 被传回上一层的坐标。

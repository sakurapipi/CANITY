# hatch（舱门：放置、开门动画、效果结算）

## 职责
地上舱门的运行时：放置、状态机（关/开/用掉）、开门动画、把玩家拉下去的时机。
奖励与惩罚内容由 `HatchApply` 一类的上层处理。

## 契约
`include/hatch.h` —— 12 个函数 + 2 个全局（`hatches[]`、`hatchCount`）。
`HatchTick` 每帧推进；`HatchPlaceInRoom` 在 **game.c**（放舱门是关卡生成的事）。

## 绝不做
- 不在 `HatchTick` 里直接发奖励（走回调/上层）
- 不改"同层同类只放一扇"的判据（那是 `HatchPlaceInRoom` 的 early return）

## 测试
`python tools/build_validate.py` → PASS
关键行为：开门动画播完那一帧才结算（AD-27）/ 同层不重复同类 / 门槛判定。

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 加一种舱门 | `HatchKindForTheme` + 效果结算表 |
| 改开门时长 | `HatchOpen01` 的速度常数 |
| 改每层放几扇 | `HatchPlaceInRoom`（game.c） |

## 历史坑
- 同层出现过两扇同类舱门（用户："一个房间一扇"）→ 加了同类 early return。
- 效果必须在开门动画**播完那一帧**结算，提前结算玩家看不到因果。

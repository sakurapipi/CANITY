# lockbox（锁箱：5 种锁的玩法与开锁判定）

## 职责
上锁箱子的交互：数字锁 / 接线锁 / 等式锁 / 调音锁 / 锯锁。
负责判定"开没开"，**不负责**发什么奖励（那是 game.c 的容器逻辑）。

## 契约
`include/lockbox.h` —— 23 个函数 + 18 个全局状态（`lockActive`、`lockTarget`、`lockDigits[]`…）。
注意：**这是全库导出全局变量最多的域**，将来若要收口，从这里开始。

## 绝不做
- 不在锁域里加钱/加物品（只返回"开了"，让上层发奖）
- 不改 `LockIgnoreVK` 的语义（它挡住开锁期间的移动键）

## 测试
`python tools/build_validate.py` → PASS
关键行为：每种锁的正确解都能开 / 错误操作不推进 / 锯锁的进度与停顿节奏。

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 加第 6 种锁 | `LockTick` 的分派 + 一个 `LockXxxSolved()` + `lockType` 枚举 |
| 改开锁概率/难度 | `LockDialSolved` / `LockCorrectCount` 等判定函数 |
| 改锁型随层数的发牌 | `LockboxPctForFloor`（在 **game.c**，不在本域） |

## 历史坑
- 锁箱出现频率原来写死，v28 改成随层数递增、封顶 80%。
- 锯锁的 `lockSawStall` 是"假进度"停顿，去掉后手感变成无脑按住。

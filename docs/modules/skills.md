# skills（技能树：局外 30 节点 + 局内三选一 + 加成查询）

## 职责
两棵树的数据与规则：局外 30 节点的购买/前置/keystone 上限，局内三选一（含遗物），
以及所有"这个技能给多少加成"的查询函数。UI 只负责画，不负责判。

## 契约
`include/skills.h` —— 86 个符号（**本库最大的契约头，但结构最干净**）。
数据表：`skillDefs[]`（30 行）、`inRunDefs[]`；状态：`skillRank[30]`、`cores`、`activeKeystone`、`runSkillMask`。

## 绝不做
- 不在 UI 里算价格/可买性（调 `SkillCanBuy` / `SkillCost`）
- 不用 `skillRank[id]` 直接比较，走 `SkillOwned(id)`（内含 keystone 特判）
- 不加新的全局数组；新数据加到 `SkillDef` 结构里

## 测试
`python tools/build_validate.py` → PASS
关键行为：前置未满足不可买 / keystone 只能 1 个 / 30 节点网格四向导航不落空位。
（技能测试暂时还留在 test_runtime.c）

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 加一个技能节点 | `skills.c` 的 `skillDefs[]` **加一行** + 一个加成函数 |
| 改某技能数值 | 对应 `SkillXxxMul()` 的返回值 |
| 改三选一池 | `inRunDefs[]` |
| 改节点网格布局 | `SkillNodeCol/Row/IdAt`（**改完必须跑导航用例**） |

## 历史坑
- `SKILL_COUNT` 从 15 扩到 30 时，作弊工具和存档都还是 15 —— 只写了一半技能（AD：改常量要 grep 全部引用）。
- 网格是"填满的矩形"，四向导航才不会落空位；改成不规则布局前先想清楚。

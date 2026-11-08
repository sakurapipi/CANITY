# threat_telegraph（威胁预告）

## 职责
怪靠近时先给**征兆**（远处脚步 → 中距离拖行 → 近了心跳 + 手电闪烁），
而不是凭空出现在脸上。纯表现层。

## 为什么
玩家反馈「这根本就不恐怖」「好安全」。
调研（IndieDB horror 设计文引 *Anatomy*）：恐怖来自**预期** ——
在你进门前就告诉你屋里有东西，让想象自己磨自己。

## 契约
`include/threat_telegraph.h`：
`TelegraphTick(dt)` / `TelegraphNearestDist()` / `TelegraphBand()` / `TelegraphFlicker()`

三档边界：远 >12 格（6.0 秒一次）、中 7~12 格（3.5 秒）、近 <7 格（2.0 秒）。

## 绝不做
- **不改任何 AI 状态** —— 只读位置
- 玩家**看得见它**时不播（有视线 + 前方 60° 内）。
  预告的意义是"还没看见"；转身背对它，预告会恢复 —— 这才是恐怖来源

## 测试
`tests/domains/test_actors.c`：没怪不响、20 格不响、9 格中档、3 格近档、
**正前方 3 格不响**。

## 历史坑
- `MON_HUNTER_MAX` / `LIGHTBEAST_MAX` 是**宏**，不能 `extern int`（展开成 `extern int 3;`）
- `MonsterVisible()` 不存在 —— 用现成的 `HasLOS()` + 朝向点积

# CANITY — Steam 商店页初稿

> 用途：上线前把文案先定下来，截图/胶囊图的构图也一起定，避免临门一脚返工。
> 状态：**初稿**，未接 Steamworks SDK。

---

## 短描述（≤ 300 字符）

**中文**

> 你的手电在耗电，你的理智在流逝，而黑暗里的东西只听声音。
> 三把武器，各烧一条命脉：扳手烧体力、信号枪烧电量、符印烧理智。
> 一层一层往下走，用碎片换更狠的改装 —— 看看你能撑到第几层。

**English**

> Your flashlight is dying. Your sanity is draining. The thing in the dark hunts by sound.
> Three weapons, three lifelines: the wrench burns stamina, the flare pistol burns charge,
> the sigil burns sanity. Descend, spend shards, and see how deep you get.

---

## 长描述

### 一句话钩子
**「在黑暗里，你连呼吸都要算计。」**

### 三个卖点

**1. 声音就是生命线**
走路是安静的 —— 但连续走五秒，脚步会开始有声音。
跑步、翻箱、摇电都会把寻声鬼引过来。你可以选择永远安静，那就永远拿不到补给。

**2. 三把武器 = 三条命脉**
- **摇把扳手**：烧体力，贴脸，伤害最高
- **信号枪**：烧电量，手电和子弹抢同一个电池
- **符印**：烧理智，用多了会疯
选武器 = 选"我愿意先破产哪条资源线"。

**3. 每三层一个安全屋**
休息、买配件、改装武器、存档 —— 但你只能选一样。
改装是永久的，遗物只在本局有效，而碎片永远不够。

### 特性列表
- 程序生成的 24 层地牢，4 个区段各有自己的光照与规则
- 7 种怪物，每一种都针对你的一种应对方式
- 3 把初始武器 × 10 个配件 × 15 个技能树节点 × 20 个遗物
- 每层末三选一，构筑你的单局 build
- 中英双语
- 全程序化美术与音效，无外部资源依赖

---

## 标签建议

```
Horror, Roguelike, First-Person, Difficult, Atmospheric,
Dark, Procedural Generation, Singleplayer, Indie, Retro
```

主标签优先级：**Horror > Roguelike > First-Person**

---

## 截图清单（8 张）

| # | 内容 | 构图要点 |
|---|---|---|
| 1 | 主视觉：黑暗走廊 + 远处一双发光的眼睛 | 手电锥形光 + 眼睛自发光是核心卖点 |
| 2 | 三把武器手持模型并排 | 展示 alpha 的武器系统 |
| 3 | 技能树界面（中文） | 展示成长深度 |
| 4 | 安全屋商店 / 改装台 | 展示经济与改装 |
| 5 | 剑气命中的瞬间（残影 + 伤害数字） | 展示打击感 |
| 6 | 遗物三选一 | 展示单局构筑 |
| 7 | 图鉴 / 成就页 | 展示内容量 |
| 8 | 结尾：BOSS 前的门 + 一行文字 | 悬念 |

**胶囊图（Capsule）构图**：黑底 + 一只握着摇把扳手的手 + 手电光斑 + 标题 CANITY 用等宽像素字。
头图（Header）用 1 号截图裁切。

---

## 待办（接 Steamworks 时）

- [ ] App ID 与 Steamworks SDK 接入
- [ ] 成就 40 → Steam Achievement API
- [ ] 每日挑战 → Leaderboard API
- [ ] Steam Cloud 存档
- [ ] 创意工坊（暂不考虑）

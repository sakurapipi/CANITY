# 拆 `game.h` 的方案（第 6 轮）

> 2026-11-09。这一轮只做**分析**，不改 C 代码 —— 拆头文件必须先把分类做对。

## 一、现状数字

```
include/game.h        357 行
函数声明              74 个
全局变量 extern      160 个   ← 这才是真问题
宏                    1 个
include 它的 .c       39 个
```

**160 个全局变量** 挂在一个头里。任何 include 它的文件，
都自动"认识"这 160 个全局 —— 这就是耦合的物理来源。

## 二、39 个引用者（按职责分组）

| 组 | 文件数 | 例子 | 它们大概需要什么 |
|---|---|---|---|
| **领域层** | 9 | collapse / hatch_place / phase / detect_alert / ui_menu / ui_pages / ui_text | 只读几个状态量（`currentLevel`、`posX/posY`） |
| **实体 AI** | 6 | entity / collector / flank / ai_common / scavenger / warden | 玩家位置、噪音、门的开合 |
| **呈现** | 4 | render / ui / bench / extras | 几乎全部状态（要画） |
| **平台/杂项** | 8 | audio / gamepad / daily / money / shop … | 一两个开关量 |
| **game.c 自己** | 1 | game.c | 全部（它是定义方） |
| **测试** | 若干 | tests/*.c | 少量 |

## 三、建议拆成 4 个头（不是 5 个）

```
include/loop_api.h      ← 状态机：currentState / StartGame / 暂停 / 关卡号
                           约 15 个符号，给"需要知道现在是什么状态"的文件

include/player_api.h    ← 玩家：posX/posY/dirX/dirY/sanity/stamina/battery/
                           flashlightOn/curWeapon/…
                           约 40 个符号，给 AI（它们要知道你在哪）和呈现

include/world_api.h     ← 世界：worldMap / MAP_* / exit 位置 / 舱门 / 崩塌
                           约 20 个符号，给渲染和 pathfinding

include/present_api.h   ← 呈现请求：uiDirty / ForceCursor / pickupMsg /
                           pickupMsgTimer / g_lang
                           约 15 个符号，给所有"想弹一句话"的地方

剩下的（约 70 个符号）留在 game.h，只有 game.c 自己用。
```

## 四、为什么这个顺序（先拆 game.h，不先拆 game.c）

```
game.h 被 39 个文件 include
   ↓
拆它 = 每个引用者只拿到自己需要的那一小撮
   ↓
然后拆 game.c 时，各块已经有自己的头，搬代码不用同时改 39 个文件
```

**反过来做（先拆 game.c）会失败**：game.c 拆成 5 个文件，
但那 39 个引用者还是全指着 game.h，等于白拆。

## 五、下一步的具体做法（第 7 轮起）

1. 建 4 个头，**先把声明抄过去**（不删 game.h 里的）
2. 让 39 个文件**逐个改成 include 新头**，一次一个，每次构建
3. 全部改完后，把 game.h 里的重复声明删掉
4. 加分区规则 S5：`game.h` 只能被 `game.c` 引用

**每步都能构建通过，随时可回滚。**

## 六、风险

| 风险 | 说明 |
|---|---|
| 重复声明 | 两个头声明同一个全局，GCC 允许（只要类型一致），但会掩盖归属 |
| `game.h` 里的 `#define` | 只有 1 个宏，影响小 |
| 测试文件 | 有几处测试读源码文本（如 `bs = read_file(...)`），改路径要同步 |

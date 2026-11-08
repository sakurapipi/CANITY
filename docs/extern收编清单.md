# 函数体内 extern 收编清单（第 19 轮）

> 全工程 **283 处**，分布在 **20 个文件**。

判定口径：函数体内（缩进 >= 4 空格）的 `extern` 声明。
这类声明绕过头文件，是"隐式依赖"的来源。

## 怎么用这份清单

1. 给文件**补上 missing 列的 include**
2. 然后那些 extern 才能删（删不掉的说明头里没有，需要先加进头）
3. 每改一个文件跑一次构建

| 文件 | extern | 分属 | 缺的头 |
|---|---|---|---|
| `src/game.c` | 65 | loop:1 player:5 present:16 world:5 | （都已包含） |
| `src/ui.c` | 45 | player:7 present:6 world:8 | （都已包含） |
| `src/render.c` | 27 | player:5 present:1 world:2 | （都已包含） |
| `src/entity.c` | 24 | player:2 present:5 world:6 | （都已包含） |
| `src/scavenger.c` | 16 | player:4 world:7 | （都已包含） |
| `src/save.c` | 15 | world:2 | （都已包含） |
| `src/platform/main_web.c` | 14 | present:3 world:2 | （都已包含） |
| `src/warden.c` | 13 | player:1 present:2 world:7 | （都已包含） |
| `src/lockbox.c` | 10 | player:1 present:4 world:1 | （都已包含） |
| `src/throwing.c` | 9 | present:2 world:2 | （都已包含） |
| `src/shop.c` | 9 | present:2 world:3 | （都已包含） |
| `src/wallet.c` | 8 | world:4 | world_api.h |
| `src/domain/ui/ui_menu.c` | 8 | present:2 world:2 | （都已包含） |
| `src/spider.c` | 6 | present:4 | present_api.h |
| `src/daily.c` | 5 | present:2 | （都已包含） |
| `src/achievements.c` | 3 | present:3 | present_api.h |
| `src/skills.c` | 2 | world:1 | （都已包含） |
| `src/art.c` | 2 | - | （都已包含） |
| `src/mimic.c` | 1 | - | （都已包含） |
| `src/collector.c` | 1 | - | （都已包含） |

## 关键发现（第 19 轮）

extern 最多的 5 个文件（game.c 65 / ui.c 45 / render.c 27 / entity.c 24 /
scavenger.c 16），**"缺的头"一栏全是"无"**。

意思是：它们函数体内 extern 引用的符号，**四个契约头里都没有** ——
这些符号**从来没有在任何头文件里被声明过**，只在各自文件里定义为全局。

别的文件要用，只能在函数体内写 `extern` 硬拿。

### 所以真正的收编工作是两步

1. **先把这些符号补进合适的头**（每个都要判断属于 loop / player / world / present 哪一层）
2. 然后才能删函数体内的 extern

这一步没有机械办法 —— 每个符号的归属都要人判断。

按第 18 轮的速度（每轮约 15 行），283 处大约需要 **20~25 轮**。

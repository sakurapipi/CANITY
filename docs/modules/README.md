# 模块卡片（agent 的入口）

## 这是什么

每个域一张卡，**≤ 40 行**。它不是给人看的文档，是给 agent 看的**入口**。

判断标准只有一条：

> 只看这张卡 + 契约头，能不能判断"这次改动该不该动这个域、动了要验什么"。

## 为什么要有

实测（本项目 60 次提交）：

```
一次典型改动要读：game.c 9,556 + test_runtime.c 4,707 + common.h 1,252
                = 15,515 行 ≈ 140k token
```

卡片的作用是把"先在 9,557 行里找"换成"先看一眼 40 行"。

文献支撑：
[arXiv 2607.17937](https://arxiv.org/html/2607.17937v2) 实测，同一长上下文下，
笼统让 agent 自查 → 10 次过 5 次；给一份写明义务的外部清单 → 10 次过 10 次（p=0.0325）。

## 怎么用（顺序不能乱）

1. 读这张卡的 **职责** 和 **绝不做**
2. 读对应的契约头（`include/<域>.h`）
3. 只读该域的实现文件
4. 开工前过一遍 `docs/TASK_CHECKLIST.md`

## 现有卡片

| 域 | 实现 | 必读量 | 卡片 |
|---|---|---|---|
| throwing 投掷物 | `src/throwing.c` | 744 行 | [throwing.md](throwing.md) |
| skills 技能 | `src/skills.c` | 1,007 行 | [skills.md](skills.md) |
| lockbox 锁箱 | `src/lockbox.c` | 1,219 行 | [lockbox.md](lockbox.md) |
| warden 守门人 | `src/warden.c` | 1,133 行 | [warden.md](warden.md) |
| shop 商店 | `src/shop.c` | 1,058 行 | [shop.md](shop.md) |
| hatch 舱门 | `src/hatch.c` | 259 行 | [hatch.md](hatch.md) |
| map_rooms 主题房间 | `src/map_rooms.c` | 832 行 | [map_rooms.md](map_rooms.md) |

## 还没卡片的（就是待拆的）

| 东西 | 行数 | 为什么没卡 |
|---|---|---|
| `src/game.c` | 9,556 | **上帝模块**，装了一堆域，正在按 v2 方案往外搬 |
| `src/ui.c` | 5,542 | 纯 UI，第 7 轮拆页 |
| `src/render.c` | 4,172 | 每帧热点，**永不动** |
| `tests/test_runtime.c` | 4,707 | 第 2 轮按域拆 |

## 新增域的流程

1. 建 `include/<域>.h`（≤120 行，**只导出函数**）
2. 建 `src/domain/<域>/<域>.c`（≤800 行）
3. 建 `tests/test_<域>.c`
4. 抄 `docs/modules/_TEMPLATE.md` 写卡
5. `python tools/module_cost.py` 确认没超预算

# ui（界面：文本层 / HUD / 菜单 / 商店 / 技能树）

## 职责
所有 2D 覆盖层：点阵文字、HUD、主菜单、设置、商店与改造树、技能树、图鉴。

## 结构（第 7 轮后）
| 文件 | 行数 | 内容 |
|---|---|---|
| `src/domain/ui/ui_text.c` | ~250 | 点阵字与中文图集 → **已拆**，只依赖 render/lang |
| `src/ui.c` | ~5,300 | 其余全部（含 1,299 行的 DrawHUD） |

## 契约
`include/ui.h`（对外）+ `include/ui_text.h`（文本层内部件：UiTrack）。

## 绝不做
- 不在 UI 里算价格/可买性/伤害（调对应域的查询函数）
- 不改 `UiTrack` 的记录语义（布局自检 `ui_layout.txt` 依赖它）
- 不把 `pixels` 缓冲的写入顺序搞乱（HUD 必须最后画）

## 测试
`python tools/build_validate.py` → PASS
布局/运行用例在 test_runtime.c（`test_runs_and_layout` 等）。
可见改动必须出**世界内截图**（`CANITY_SHOT`）—— AD-66。

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 字号/行高 | `ui_text.c` 的 `TextScale` / `TextHeightWin` |
| 某页面排版 | `ui.c` 里对应的 `DrawXxx`（菜单 `DrawMenu...`） |
| HUD 元素位置 | `DrawHUD`（1,299 行，改之前先 `grep` 目标字符串） |

## 历史坑
- `TextScale` 原来固定 2 倍，但测量又乘了 `renderH/458` ——
  布局按大字排、实际画小字，文字偏左（第 7 轮拆出来时正好看到这段注释）。
- 标题被画两遍（+1px 偏移同色）→ 字发毛，`DrawTitleWin` 只画一遍。
- 世界内截图用例必须在 `SpritesInit` **之后**跑，否则精灵是空的（AD-66）。

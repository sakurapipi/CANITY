# ui（界面：文本层 / 页面 / 菜单 / HUD）

## 职责
所有 2D 覆盖层：点阵文字、主菜单（含改造树/银行页/校准条）、成就页、图鉴、
设置、商店、技能树、HUD。

## 结构（第 2 轮后）
| 文件 | 行数 | 内容 |
|---|---|---|
| `domain/ui/ui_text.c` | ~250 | 点阵字 + 中文图集 + 宽度测量 |
| `domain/ui/ui_pages.c` | ~285 | 成就页、AchCompute 几何、ClampRowText/WrapText |
| `domain/ui/ui_menu.c` | ~1,410 | 主菜单整簇：面板/文字/改造树/银行页/校准条 |
| `ui.c` | ~3,670 | HUD（1,299 行单函数）、商店、技能树页、设置、弹窗 |

## 契约
`include/ui.h`（对外）+ `ui_text.h`（文本层）+ `ui_pages.h`（页面/几何）+
`ui_menu.h`（菜单 + UI 内部契约：SkillTreeLayout / UiPanel9 / UiRowSel 等）。

## 绝不做
- 不在 UI 里算价格/可买性/伤害（调对应域的查询函数）
- 不改 `UiTrack` / `AchCompute` 的几何语义 —— 菜单命中测试依赖同一份计算
- 不把 `pixels` 写入顺序搞乱（HUD 必须最后画）

## 测试
`python tools/build_validate.py` → PASS
`tests/domains/test_ui.c`：成就页几何（3 种窗口）、文本截断、
**菜单截图**（设 `CANITY_SHOT=<目录>` 会写出 `shot_menu.ppm`）。
可见改动必须出图看 —— AD-66。

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 菜单行/标题/页脚 | `ui_menu.c` 的 `MainMenuCompute` / `DrawMenuText` |
| 改造树 | `ui_menu.c` 的 `DrawBenchTree` |
| 成就页 | `ui_pages.c` 的 `UiDrawAchievementsPage` |
| 字号/行高 | `ui_text.c` 的 `TextScale` / `TextHeightWin` |
| HUD | `ui.c` 的 `DrawHUD`（1,299 行，改前先 grep 目标字符串） |

## 历史坑
- `TextScale` 原来固定 2 倍、测量又乘 `renderH/458` → 布局按大字排、实际画小字。
- 标题被画两遍（+1px 同色偏移）→ 字发毛，`DrawTitleWin` 只画一遍。
- **菜单搬出去时会连锁缺声明**：ui.c 从来没有"内部契约头"，
  任何函数往外搬都会带上十几个私有助手（UiPanel9/UiRowSel/UiTitle/…）。
  第 2 轮一次性把这张契约补进 `ui_menu.h`，以后搬 HUD 会容易得多。

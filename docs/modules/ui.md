# ui：界面与最终合成

## 状态归属

- `src/ui.c`：公共绘制原语、菜单几何、帧合成及最终呈现；拥有布局记录。
- `src/domain/ui/ui_skilltree.c`：技能树节点列表、布局、绘制与命中测试；私有
  `SkillTreeViewState` 统一管理缩放、平移、拖拽起点。调用方只用既有操作接口。
- `src/domain/ui/ui_overlay.c`：教程面板、存档失败横幅、拾取及操作失败提示；
  私有 `UiToastState` 复制消息文本并拥有过期时间，不借用调用方临时缓冲。
- `ui_text.c`：文字测量与绘制；`ui_pages.c`：独立页面。
- `ui_menu.c`：菜单页面；`ui_hud.c`：游戏 HUD。

## 契约

公共接口在 `include/ui.h`、`ui_text.h`、`ui_menu.h`、`ui_pages.h`；
通知层内部入口在 `src/domain/ui/ui_overlay.h`。
绘制与输入共用技能树几何；禁止再复制一份缩放状态或节点表。
最终层次由 `ui.c` 的帧合成顺序决定，移动实现不改变叠放次序。
业务价格、购买条件、技能效果仍由对应玩法模块计算。

## 维护

技能表生成器、技能布局检查器和静态契约均读取 `ui_skilltree.c`。
`tools/module_cost.py` 包含全部 UI 子模块；不提高历史预算来掩盖新增耦合。
2.4.8-RC 本轮仅修改源码，未运行构建、检查或桌面验收。

## 图鉴入口（RC2）

`ui_archive.c` 负责共享图鉴入口的怪物、主题房间两个分类及其几何。
怪物继续使用原来的 bestiarySel 和发现进度；房间独立记录浏览选择，七种全部可查。
分类按钮、列表、详情翻页按钮共享绘制与点击几何，键盘和触控走同一入口。
房间说明调用独立于运行态的 RoomThemeGuideEffect/Control，不伪造舱门或房间实体。
七种房间肖像直接读取启动烘焙的模型，双布局示意调用纯模板函数，不消耗随机数。

RC2的小游戏终端由 `src/domain/ui/ui_calibration.c`绘制；判定、收益与时间状态仍由 `loop_calibration.c`拥有，UI只读取，不推进谱面。

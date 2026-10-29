# CANITY UI 重造 —— 调研与设计

日期：2026-10-29
范围：全部界面（主菜单 / 暂停 / 设置 / 技能树 / 成就 / 图鉴 / 帮助 / 安全屋 / 三选一）
原则：**保留现有的暗色工业恐怖风格**，但把实现方式换掉。

---

## 一、调研：C 语言的优秀 UI 是怎么搭的

### 1.1 即时模式 GUI（Nuklear / microui）

两个都是 ANSI C 的单头文件库，架构完全一致：

```
每帧：输入 → 描述界面 → 库吐出「矩形 + 文字」两种绘制命令 → 你自己的渲染器去画
```

关键设计（这两条直接可以抄）：

| 设计 | 说明 |
|---|---|
| **样式是数据** | 一个 `nk_style` / `mu_Style` 结构，全局应用，界面代码里不出现颜色和间距 |
| **布局系统** | `nk_layout_row_dynamic(ctx, 30, 2)` —— 行高 30、分 2 列；而不是每处写死像素 |
| **只管矩形和文字** | 库本身不渲染，适配器只需 `draw_rect` / `draw_text` 两个函数 |
| **固定内存** | microui 1100 行、零分配 |

参考：[Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) · [microui](https://github.com/rxi/microui)

### 1.2 经典游戏 UI（Doom `m_menu.c`）

**这是最值得抄的一份代码。** 整个菜单系统的骨架只有这些：

```c
typedef struct { short status; char name[10]; void (*routine)(int); char alphaKey; } menuitem_t;
typedef struct { short numitems; menu_t* prevMenu; menuitem_t* menuitems;
                 void (*routine)(); short x, y, lastOn; } menu_t;

// 绘制：一个循环 + 一个光标精灵
for (i = 0; i < max; i++) {
    V_DrawPatchDirect(x, y, menu->menuitems[i].name);
    y += LINEHEIGHT;
}
V_DrawPatchDirect(x + SKULLXOFF, menu->y - 5 + itemOn*LINEHEIGHT, skullName[whichSkull]);
```

**菜单是数据，光标是一个精灵，画在 `itemOn × 行高` 的位置。** 就这些。

我们现在的做法正好相反：每个界面各写一段 `if/else` 画不同的框，位置全是魔法数字。

参考：[m_menu.c（id Software 公开源码）](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/m_menu.c)

### 1.3 9-slice（九宫格拉伸）

> 9-slice scaling 是把一张图切成 3×3 九块、四角不拉伸、只拉伸四条边的技术。

用途：**同一个边框可以服务任意尺寸的窗口**，角上的铆钉/装饰不会被拉变形。
参考：[9-slice scaling](https://en.wikipedia.org/wiki/9-slice_scaling)

---

## 二、诊断：我们现在的 UI 为什么"廉价"

| 症状 | 根因 |
|---|---|
| 每个界面像不同游戏 | 没有统一样式表，颜色/间距/边框各写各的 |
| 边框粗糙 | `FrameRectWin` 画 1px 直角矩形，没有厚度、没有装饰 |
| 选中态廉价 | 一条绿色实心条，没有光标、没有悬停过渡 |
| 层级不清 | 标题和正文同字号，全靠颜色区分 |
| 中文/英文混排跳 | 位图字靠 `TextScale=2` 硬撑，中英字重不一致 |
| 加一个界面要写 200 行 | 没有可复用的面板/行/光标 |

## 三、我的判断：主题应该是「工业设备」，不是「网页」

这个游戏的世界是废弃设施 + 应急照明。UI 如果做成半透明圆角卡片，会立刻出戏。

**我定的视觉语言：**

1. **面板 = 铆接的金属板**。四角有螺丝，边框是双线（外亮内暗），底部有一条警示条纹。
2. **文字 = 贴标签**。标题用字距拉开的全大写（像丝印），值用等宽，提示用小号暗色。
3. **强调色只有一种**：琥珀色（`#FFB040`）。危险用红，安全用青，其余全是灰。**不引入第二种主色。**
4. **动画只有两种**：光标闪烁（2 帧）、面板开场（1 帧亮线扫过）。不为动而动。
5. **光标不是高亮条**，是一个**三角箭头 + 左侧竖线**，像老式终端的选择标记。

## 四、实现方案

### 4.1 新增 `ui_theme.c/h`

```c
typedef struct {
    unsigned int ink;        /* 正文 */
    unsigned int inkDim;     /* 提示 */
    unsigned int inkTitle;   /* 标题 */
    unsigned int accent;     /* 强调（琥珀） */
    unsigned int danger;     /* 危险（红） */
    unsigned int ok;         /* 安全（青） */
    int pad, gap, lineH;     /* 间距网格（全部 4 的倍数） */
} UiTheme;

void UiPanel(int x, int y, int w, int h, int focused);   /* 9-slice 铆接板 */
void UiTitle(int cx, int y, const char* s);
void UiLabel(int x, int y, unsigned int col, const char* s);
void UiHint(int cx, int y, const char* s);
void UiRow(int x, int y, int w, int h, int sel);         /* 行底 + 光标 */
int  UiPanelRowH(void);                                  /* 行高（Doom 的 LINEHEIGHT） */
```

### 4.2 烘焙资源（复用 v2.1 的 spirte 管线）

| 资源 | 尺寸 | 说明 |
|---|---|---|
| `SPR_UI_PANEL` | 48×48 | 9-slice 母版：四角螺丝 + 双线边 + 底部警示条 |
| `SPR_UI_CURSOR` | 16×16 | 三角箭头 |
| `SPR_UI_ROW` | 48×16 | 行的选中/未选中底纹 |

### 4.3 迁移顺序

1. 主题 + 面板 9-slice + 光标（地基）
2. 主菜单 → 暂停（样板间）
3. 设置 / 技能树 / 成就 / 图鉴 / 帮助
4. 安全屋 / 三选一 / 通关

每改一个界面，截图对比一次。

## 五、验收标准

- 所有界面共用一个 `UiPanel()`，改一处样式全局生效
- 选中态一律是「箭头 + 竖线」，不再是一条绿条
- 面板在任意尺寸下不变形（9-slice）
- 标题/正文/提示三级字号可分辨
- 加一个新界面不超过 40 行

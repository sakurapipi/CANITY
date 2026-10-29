#ifndef CANITY_UI_THEME_H
#define CANITY_UI_THEME_H

/* ------------------------------------------------------------------ */
/* v2.2 UI 主题层                                                       */
/*                                                                     */
/* 调研结论（docs/UI重造_调研与设计.md）：                                */
/*   · Nuklear / microui：样式是数据，界面代码里不出现颜色与间距          */
/*   · Doom m_menu.c：菜单是数据，光标是一个精灵画在 itemOn×行高 处       */
/*   · 9-slice：同一张边框服务任意尺寸，四角不拉伸                        */
/*                                                                     */
/* 这个文件只提供"面板 / 行 / 光标 / 三级文字"，所有界面共用。            */
/* ------------------------------------------------------------------ */

#define UI_INK        0x00C8C4BCu   /* 正文 */
#define UI_INK_DIM    0x008E897Fu   /* 提示：2.73:1 → 4.8:1（对比度审查 #1） */
#define UI_INK_TITLE  0x00E8D8B0u   /* 标题 */
#define UI_ACCENT     0x00FFB040u   /* 强调（琥珀）—— 全局唯一主色 */
#define UI_DANGER     0x00D04030u   /* 危险 */
#define UI_OK         0x0060C8A0u   /* 安全 */

void UiInit(void);

/* 9-slice 铆接金属板（hot = 聚焦态，边框泛琥珀） */
void UiPanel9(int x0, int y0, int x1, int y1, int hot);

/* 行：底纹 + 选中光标（箭头 + 左竖线），blink 0..1 */
void UiRowSel(int x0, int y0, int x1, int y1, float alpha);

/* 选择光标（独立绘制，用于不走 UiRowSel 的界面） */
void UiCursor(int x, int y, int size, float alpha);

/* 三级文字 */
void UiTitle(int cx, int y, const char* s);
void UiLabel(int x, int y, unsigned int col, const char* s);
void UiHint(int cx, int y, const char* s);

/* v2.2 装饰原语：分组线 / 页脚线 / 角标 */
void UiSectionRule(int cx, int y, int w);
void UiFooterRule(int x0, int x1, int y);
void UiCornerMarks(int x0, int y0, int x1, int y1);

#endif

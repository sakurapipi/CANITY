#ifndef CANITY_UI_THEME_H
#define CANITY_UI_THEME_H

/* ------------------------------------------------------------------ */
/* UI 主题层 —— v3.0 重写（第 1 批：地基）                              */
/*                                                                     */
/* 勘查报告的核心结论：真正要重写的不是 28 个界面，而是                  */
/* 「每个界面各写一套面板/选中态/滚动条」这个习惯。                      */
/*                                                                     */
/* 所以这个文件定义三件事：                                              */
/*   ① 一套**语义色**（界面代码里不再出现颜色字面量）                    */
/*   ② 一套**间距网格**（4 的倍数，取代写死的 18/26/34…）               */
/*   ③ 三个**通用组件**：UiPanel / UiRow / UiBar                        */
/*                                                                     */
/* 现有 288 处颜色字面量会分批替换；新写的界面必须只用这里的名字。       */
/* ------------------------------------------------------------------ */

/* ==================== ① 语义色 ==================== */
/* 文字 */
#define UI_INK        0x00C8C4BCu   /* 正文 */
#define UI_INK_DIM    0x008E897Fu   /* 次要提示（4.8:1） */
#define UI_INK_FAINT  0x00606868u   /* 极弱（装饰） */
#define UI_INK_TITLE  0x00E8D8B0u   /* 标题 */

/* 语义 */
#define UI_ACCENT     0x00FFB040u   /* 强调（琥珀）—— 选中/已购 */
#define UI_DANGER     0x00D04030u   /* 危险/不可用 */
#define UI_OK         0x0060C8A0u   /* 安全/可执行 */
#define UI_WARN       0x00E0A020u   /* 警告/冷却中 */
#define UI_INFO       0x0080C0E0u   /* 信息/中性蓝 */

/* 面（背景） */
#define UI_BG_PANEL   0x00161C1Cu   /* 面板底 */
#define UI_BG_ROW     0x00202828u   /* 行底（可点） */
#define UI_BG_SEL     0x00403A20u   /* 行底（选中） */
#define UI_BG_HOVER   0x00303838u   /* 行底（悬停） */
#define UI_BG_LOCKED  0x00161412u   /* 行底（锁定） */

/* 边 */
#define UI_EDGE       0x00C8B060u   /* 面板外框 */
#define UI_EDGE_HOT   0x00FFE070u   /* 面板外框（聚焦） */
#define UI_EDGE_DIM   0x00505858u   /* 分隔线 */

/* ==================== ② 间距网格 ==================== */
/* 4 的倍数。UIS() 会按 renderH 缩放，所以这里写设计值即可。 */
#define SP1  4
#define SP2  8
#define SP3 12
#define SP4 16
#define SP5 20
#define SP6 24
#define SP7 32
#define SP8 40
#define SP9 48

/* ==================== ③ 通用组件 ==================== */

void UiInit(void);

/* ---- 面板：统一 6 套手写面板 ----
   kind: 0=普通 1=聚焦（游戏中要操作） 2=危险（存档失败等） 3=空白（无框） */
#define UI_PANEL_NORMAL 0
#define UI_PANEL_HOT    1
#define UI_PANEL_DANGER 2
#define UI_PANEL_BARE   3
void UiPanel(int x0, int y0, int x1, int y1, int kind);

/* ---- 行：统一 3 套选中态 ----
   state: 0=普通 1=选中 2=悬停 3=禁用 4=锁定（前置没满足） */
#define UI_ROW_NORMAL  0
#define UI_ROW_SEL     1
#define UI_ROW_HOVER   2
#define UI_ROW_DISABLED 3
#define UI_ROW_LOCKED  4
void UiRow(int x0, int y0, int x1, int y1, int state, float alpha);

/* ---- 进度条：统一 7 条手写进度条 ----
   frac 0..1；kind: 0=普通 1=低值警告 2=危险 */
#define UI_BAR_NORMAL 0
#define UI_BAR_WARN   1
#define UI_BAR_DANGER 2
void UiBar(int x, int y, int w, int h, float frac, int kind, unsigned int col);

/* ---- 保留的既有原语 ---- */
void UiPanel9(int x0, int y0, int x1, int y1, int hot);
void UiRowSel(int x0, int y0, int x1, int y1, float alpha);
void UiCursor(int x, int y, int size, float alpha);
void UiTitle(int cx, int y, const char* s);
void UiLabel(int x, int y, unsigned int col, const char* s);
void UiHint(int cx, int y, const char* s);
void UiSectionRule(int cx, int y, int w);
void UiFooterRule(int x0, int x1, int y);
void UiCornerMarks(int x0, int y0, int x1, int y1);

#endif

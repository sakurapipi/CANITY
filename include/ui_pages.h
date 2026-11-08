/* include/ui_pages.h —— UI 页面层（几何缓存 + 独立页面）
 *
 * 第 1 轮（2026-11-08）
 * ----------------------------------------------------------------
 * 上一次试图拆成就页时回滚了：它依赖 AchGeo/AchCompute，而**菜单命中测试
 * 也用同一个几何缓存**。所以这一次先把几何缓存变成共享件，再搬页面。
 *
 * 依赖方向：ui.c（菜单/HUD）→ 本头 → ui_text.h。本文件不反向依赖 ui.c 的私有件。
 */
#ifndef CANITY_UI_PAGES_H
#define CANITY_UI_PAGES_H

#include "common.h"
#include "ui.h"        /* MenuGeo */

/* 成就页的几何缓存。菜单命中测试与成就页绘制共用同一份计算，
   避免"点得到的位置和画出来的位置不一样"。 */
typedef struct {
    RECT panel;
    int  count;
    int  visible;
    int  titleY;
    int  firstY, rowH;
    int  stripRuleY, stripY1, stripY2;
    int  padX;
} AchGeo;

/* 成就页最多显示几行（原来是 ui.c 的私有 #define） */
#define ACH_VISIBLE_ROWS 10

void AchCompute(AchGeo* A);

/* 成就页绘制（原来的 static DrawAchievementsWindow） */
void UiDrawAchievementsPage(MenuGeo g);

/* 文本按**像素宽度**截断，超出补 ".."。23 处调用，所以对外。 */
void ClampRowText(char* buf, int availPx);

/* 按空格折行：至多 maxLines 行，每行不超过 availPx；返回实际行数。 */
int WrapText(const char* text, int availPx, char lines[][96], int maxLines);

#endif /* CANITY_UI_PAGES_H */

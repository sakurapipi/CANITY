/* include/ui_menu.h —— 主菜单页（从 ui.c 拆出）
 *
 * 第 2 轮（2026-11-08）：菜单簇只依赖 MenuGeo / 文本 API / ClampRowText / PAL_*，
 * 不碰 ui.c 的私有 static。整簇（含改造树、银行页、校准条）一次性搬走。
 *
 * 对外入口：UiDrawMenuPages()（= 面板 + 文字，顺序不能反）。
 * 共享件：DrawCalibBar 仍定义在 ui.c（RenderFrame 也调它），这里只是声明。
 */
#ifndef CANITY_UI_MENU_H
#define CANITY_UI_MENU_H

#include "common.h"
#include "ui.h"
#include "skills.h"   /* SKILL_GRID_COLS/ROWS（SkillTreeLayout 要用）*/

/* 主菜单面板布局：面板按文字高度定宽，行高 1.2~1.75 TH 自适应 */
typedef struct {
    RECT panel;
    int  count;
    int  titleY, subY;
    int  firstY, rowH;
    int  footRuleY, footY;
    int  padX;
} MainMenuLayout;


/* 第 2 轮：菜单搬出 ui.c 后，这几个绘制原语需要跨文件可见。
   签名照抄 ui.c 的真实定义（第一次我按猜测写，编译报了 33 个错）。 */
int  SpritesReady(void);
void UiPanel9(int x0, int y0, int x1, int y1, int hot);
void UiCornerMarks(int x0, int y0, int x1, int y1);
void UiSectionRule(int cx, int y, int w);
void UiFooterRule(int x0, int x1, int y);
void UiRowSel(int x0, int y0, int x1, int y1, float alpha);
void DrawPageTabs(void);
void ShopBackButtonRect(int* x0, int* y0, int* x1, int* y1);
void DrawCalibBar(int bx, int by, int barW, int barH);


/* ==================================================================
 * UI 内部契约（第 2 轮补）
 * ------------------------------------------------------------------
 * ui.c 一直没有"内部契约头"，所以任何函数往外搬都会连锁缺声明。
 * 下面这些是菜单用到的共享件，签名照抄 ui.c 的真实定义。
 * ================================================================== */

/* 技能树布局（DrawSkillTree 与菜单页共用） */
typedef struct {
    RECT panel;                 /* 面板外框 */
    RECT node[SKILL_COUNT];     /* 每个节点的方块（窗口坐标） */
    int  colCX[SKILL_GRID_COLS];
    int  rowCY[SKILL_GRID_ROWS];
    int  nodeW, nodeH;          /* 分支节点尺寸 */
    int  crossH;                /* 跨支节点高度（比分支节点矮一档） */
    int  titleY, curY;          /* 标题 / 页码+货币 两行 */
    int  hdrY, barY;            /* 分支配色标题 / 6 段进度条 */
    int  gridTop, gridBottom;
    int  crossTop, crossBot;
    int  detail1Y, detail2Y;    /* 底部详情两行 */
    int  hintY;                 /* 最底部按键提示 */
    int  padX;
} SkillTreeLayout;

void SkillTreeCompute(SkillTreeLayout* L);
void MainMenuCompute(MainMenuLayout* L);   /* GetMenuGeo 也要用 */
void DrawSkillTree(void);
void UiTitle(int cx, int y, const char* s);
void UiHint(int cx, int y, const char* s);
void DrawTitleWin(const char* t, int yWin, unsigned int color);
void DrawTextInRow(const char* text, RECT row, unsigned int color);

void UiDrawMenuPages(void);

/* 定义在 ui.c（RenderFrame 与改造树都用），拆出去后需要跨文件可见 */
void DrawCalibBar(int bx, int by, int barW, int barH);

#endif /* CANITY_UI_MENU_H */

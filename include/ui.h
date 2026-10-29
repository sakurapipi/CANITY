/*
 * Canity — UI 层：点阵字库 / 文字 / 菜单与弹窗几何 / HUD / 最终呈现
 *
 * 只依赖 common.h；绘制时要读游戏与实体状态，故 .c 里再包含 game.h / entity.h。
 */
#ifndef CANITY_UI_H
#define CANITY_UI_H

#include "common.h"

/* 整帧合成 + 呈现（场景 → 菜单/HUD → StretchBlt/StretchDIBits） */
void RenderFrame(void);

/* 菜单几何：game.c 的命中测试与鼠标拖拽也要用 */
MenuGeo GetMenuGeo(void);
MenuGeo GetPopupGeo(void);
RECT   SliderTrackRect(MenuGeo g);
int    SliderValueToX(MenuGeo g, float v, float lo, float hi);
float  SliderXToValue(MenuGeo g, int x, float lo, float hi, float step);
RECT   RowRect(MenuGeo g, int i);

/* 主菜单项数（随 hasSave / 当前状态变化） */
int MainMenuCount(void);

/* v2 技能树：逻辑画布坐标命中测试，返回节点 id，没点中返回 -1。
   game.c 的鼠标悬停/点击要用 —— 技能树不是行列表，不能按行高去猜。 */
int SkillNodeHitTest(int mx, int my);

/* 渲染坐标上的点阵字（小地图标题用）/ UI 尺寸缩放：
   实现留在 ui.c，但 render.c 与 game.c 也要用，所以在这里导出。 */
void PutTextRaw(int x, int y, const char* t, unsigned int color);
int  TextWidthRaw(const char* t);
int  UIS(int v);

/* 窗口过程把改绑按键交给它消费；返回 1 = 已处理 */
int RebindConsumeKey(int vk);

#endif /* CANITY_UI_H */
void PageTabRect(int idx, int* x0, int* y0, int* x1, int* y1);   /* v2.8 */

/* v3.0：绘制原语对外暴露（锁箱 / 其它模块要用） */
void BlendRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha);
void FrameRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha, int thick);
void DrawTextWin(int x, int y, const char* t, unsigned int color);
void DrawTextCenterWin(int cx, int y, const char* t, unsigned int color);
int  TextHeightWin(void);
int  TextWidthWin(const char* t);

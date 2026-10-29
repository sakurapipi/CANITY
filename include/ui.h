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

/* 渲染坐标上的点阵字（小地图标题用）/ UI 尺寸缩放：
   实现留在 ui.c，但 render.c 与 game.c 也要用，所以在这里导出。 */
void PutTextRaw(int x, int y, const char* t, unsigned int color);
int  TextWidthRaw(const char* t);
int  UIS(int v);

/* 窗口过程把改绑按键交给它消费；返回 1 = 已处理 */
int RebindConsumeKey(int vk);

/* 开锁锁盘：绘制在 ui.c，点击命中在 game.c —— 两边共用同一套几何。
   *a 的含义随锁型变：拨盘=盘号，接线=端子号，均衡器=推子号。 */
enum {
    LOCK_HIT_NONE = 0,
    LOCK_HIT_DIAL,      /* 拨盘：点数字格（快速 +1） */
    LOCK_HIT_UP,        /* 拨盘：点上方箭头 */
    LOCK_HIT_DOWN,      /* 拨盘：点下方箭头 */
    LOCK_HIT_NODE_L,    /* 接线：左端子 */
    LOCK_HIT_NODE_R,    /* 接线：右端子 */
    LOCK_HIT_BAR_UP,    /* 均衡器：推子 +1 */
    LOCK_HIT_BAR_DOWN,  /* 均衡器：推子 -1 */
    LOCK_HIT_KNOB_INC,  /* 调波器：旋钮 +1 */
    LOCK_HIT_KNOB_DEC,  /* 调波器：旋钮 -1 */
    LOCK_HIT_BACK       /* BACK 按钮 */
};
/* 返回命中类型；*a 为控件序号（未命中为 -1） */
int LockHitTest(int mx, int my, int* a);

#endif /* CANITY_UI_H */

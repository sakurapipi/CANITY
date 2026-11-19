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
void UiThreatNotice(int kind);
void ExitChoiceButtonRect(int index,RECT* rect);

/* 菜单几何：game.c 的命中测试与鼠标拖拽也要用 */
MenuGeo GetMenuGeo(void);
MenuGeo GetPopupGeo(void);
RECT   SliderTrackRect(MenuGeo g);
int    SliderValueToX(MenuGeo g, float v, float lo, float hi);
float  SliderXToValue(MenuGeo g, int x, float lo, float hi, float step);
RECT   RowRect(MenuGeo g, int i);

/* 主菜单项数（随 hasSave / 当前状态变化） */
typedef enum {
    MENU_NONE,MENU_CONTINUE,MENU_STORY,MENU_FIRST_STORY,MENU_CLASSIC,MENU_ROLES,
    MENU_TRAINING,MENU_UPGRADES,MENU_BANK,MENU_MORE,MENU_QUIT,MENU_PRACTICE_ARENA,
    MENU_DAILY,MENU_ENDLESS,MENU_BESTIARY,MENU_ACHIEVEMENTS,MENU_HELP,MENU_SETTINGS,
    MENU_BACK,MENU_WELCOME_PRACTICE,MENU_WELCOME_SKIP,
    MENU_ROLE_0,MENU_ROLE_1,MENU_ROLE_2,MENU_ROLE_3
} MainMenuAction;
MainMenuAction MainMenuResolve(void);
int MainMenuBack(void);
int MainMenuTrialRole(int x,int y);
int MainMenuUnlockRole(int x,int y);
void MainMenuOpenRoleReward(void);
void UiDrawCharacterPortrait(int role,int cx,int cy,int pixel);
int MainMenuCount(void);

/* v2 技能树：逻辑画布坐标命中测试，返回节点 id，没点中返回 -1。
   game.c 的鼠标悬停/点击要用 —— 技能树不是行列表，不能按行高去猜。 */
int SkillNodeHitTest(int mx, int my);
RECT SkillNodeRect(int id);

/* 渲染坐标上的点阵字（小地图标题用）/ UI 尺寸缩放：
   实现留在 ui.c，但 render.c 与 game.c 也要用，所以在这里导出。 */
void PutTextRaw(int x, int y, const char* t, unsigned int color);
int  TextWidthRaw(const char* t);
int  UIS(int v);

/* 窗口过程把改绑按键交给它消费；返回 1 = 已处理 */
int RebindConsumeKey(int vk);

void PageTabRect(int idx, int* x0, int* y0, int* x1, int* y1);   /* v2.8 */

/* v3.0：绘制原语对外暴露（锁箱 / 其它模块要用） */
void BlendRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha);
void FrameRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha, int thick);
/* 第 7 轮：文本层搬到 src/domain/ui/ui_text.c 后，这几个也需要跨文件可见 */
int  TextScale(void);
void DrawTextSmallWin(int xWin, int yWin, const char* t, unsigned int color);
int  DrawTextSmallWidth(const char* t);
void DrawTextSmallLiteralWin(int x,int y,const char* text,unsigned int color);
int TextSmallLiteralWidth(const char* text);
void UiTrimSmallText(char* text,int width);
void DrawTextWin(int x, int y, const char* t, unsigned int color);
void DrawTextCenterWin(int cx, int y, const char* t, unsigned int color);
int  TextHeightWin(void);
int  TextWidthWin(const char* t);

/* Input hit testing and skill-tree viewport operations. */
void HubBackButtonRect(int* x0, int* y0, int* x1, int* y1);
void HubZoomButtonRect(int which, int* x0, int* y0, int* x1, int* y1);
void DrawHubZoomButtons(void);
int HubBackButtonAt(int mx, int my);

int ShopBackButtonAt(int mx, int my);

int HubZoomButtonAt(int mx, int my);

void SkillZoomAt(float factor, int mx, int my);

void SkillFocusNode(int id);
void SkillCenterOn(int id);

int SkillDragWasClick(void);

void HubPurchaseRect(RECT* rect);
int HubPurchaseAt(int x,int y);
int TextMediumLiteralWidth(const char* text);
int TextMediumLiteralHeight(void);
void DrawTextMediumLiteralWin(int x,int y,const char* text,unsigned int color);
int SkillPickAt(int x,int y);
void RunReplacementButtonRect(int index,RECT* rect);
/* 2026-11-16：HUD 实现在 domain/ui/ui_hud.c。 */
void DrawHUD(void);

/* 2026-11-16：准星由 ui.c 定义，HUD（ui_hud.c）调用。 */
void DrawCrosshair(void);

#endif /* CANITY_UI_H */

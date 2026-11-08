/* include/ui_text.h —— 文本渲染层（从 ui.c 拆出）
 *
 * 第 7 轮（2026-11-08）：ui.c 5,543 行里最自洽的一块是文本层。
 * 它只依赖 render 的像素缓冲、lang 的 Transl、cjk 的码点表，
 * 不碰菜单状态 / 技能树 / 商店 —— 所以可以整块搬走而不改任何布局逻辑。
 *
 * 对外的 DrawText* / TextWidth* 仍然在 include/ui.h 里（调用方不用改）。
 * 这里只声明两个**跨文件共享的内部件**。
 */
#ifndef CANITY_UI_TEXT_H
#define CANITY_UI_TEXT_H

/* UI 布局记录（原来在 ui.c 里是 static；拆出文本层后两边都要用） */
void UiTrack(int x, int y, int w, int h, const char* label);

#endif /* CANITY_UI_TEXT_H */

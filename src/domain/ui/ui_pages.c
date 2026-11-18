/* src/domain/ui/ui_pages.c —— 成就页 + 文本工具
 *
 * 第 1 轮（2026-11-08）从 ui.c 原样搬过来，逻辑一行没改。
 * 契约：include/ui_pages.h
 */
#include "common.h"
#include "ui_theme.h"
#include "render.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "lang.h"
#include "ui.h"
#include "ui_text.h"
#include "ui_pages.h"
#include "achievements.h"
#include "art_direction.h"   /* PAL_* 调色板 */

/* AchGeo 的类型定义在 include/ui_pages.h（避免两处各写一份）*/

void AchCompute(AchGeo* A) {
    int TH = TextHeightWin();
    if (TH < 8) TH = 8;
    memset(A, 0, sizeof(*A));
    A->count = AchVisibleCount() + 1;          /* + Back */
    A->padX = TH;

    int panelW = TH * 24;
    if (panelW > winW - TH) panelW = winW - TH;
    if (panelW < TH * 12) panelW = TH * 12;

    int maxH = winH - TH;
    int titleH   = TH;
    int gapTop   = (TH * 6) / 10;
    int gapRows  = (TH * 6) / 10;
    int gapStrip = (TH * 4) / 10;
    int stripH   = (TH * 24) / 10;     /* 两行说明 */
    int gapBot   = (TH * 5) / 10;
    int fixed = titleH + gapTop + gapRows + gapStrip + stripH + gapBot;

    int visible = A->count;
    if (visible > ACH_VISIBLE_ROWS) visible = ACH_VISIBLE_ROWS;
    int rowH = (TH * 13) / 10;
    /* 窗口不够高就少显示几行，不把文字压扁 */
    while (visible > 5 && fixed + visible * rowH > maxH) visible--;
    A->visible = visible;

    int avail = maxH - fixed;
    rowH = (visible > 0) ? avail / visible : rowH;
    if (rowH > (TH * 15) / 10) rowH = (TH * 15) / 10;
    if (rowH < (TH * 11) / 10) rowH = (TH * 11) / 10;
    A->rowH = rowH;

    int panelH = fixed + visible * rowH;
    if (panelH > maxH) panelH = maxH;

    int left = (winW - panelW) / 2;
    A->panel.left = left;
    A->panel.right = left + panelW;
    A->panel.top = (winH - panelH) / 2;
    A->panel.bottom = A->panel.top + panelH;

    A->titleY = A->panel.top + gapTop;
    A->firstY = A->titleY + titleH + gapRows;
    A->stripRuleY = A->firstY + visible * rowH + gapStrip;
    A->stripY1 = A->stripRuleY + (TH * 3) / 10;
    A->stripY2 = A->stripY1 + TH;
}

int WrapText(const char* text, int availPx, char lines[][96], int maxLines) {
    int n = 0;
    char cur[96] = "";
    const char* p = text;
    while (*p && n < maxLines) {
        const char* sp = strchr(p, ' ');
        int len = sp ? (int)(sp - p) : (int)strlen(p);
        char word[48];
        if (len > 47) len = 47;
        memcpy(word, p, (size_t)len);
        word[len] = 0;
        char cand[160];
        if (cur[0]) snprintf(cand, sizeof(cand), "%s %s", cur, word);
        else        snprintf(cand, sizeof(cand), "%s", word);
        if (cur[0] && TextWidthWin(cand) > availPx) {
            snprintf(lines[n++], 96, "%s", cur);
            snprintf(cur, sizeof(cur), "%s", word);
        } else {
            snprintf(cur, sizeof(cur), "%.95s", cand);
        }
        p += len;
        while (*p == ' ') p++;
    }
    if (cur[0] && n < maxLines) snprintf(lines[n++], 96, "%s", cur);
    return n;
}

void ClampRowText(char* buf, int availPx) {
    if (availPx<=0) { buf[0]=0; return; }
    if (TextWidthWin(buf)<=availPx) return;
    size_t n=strlen(buf);
    while(n>0) {
        size_t k=n-1;
        while(k>0 && ((unsigned char)buf[k]&0xC0)==0x80) --k;
        buf[k]=0; n=k;
        if(TextWidthWin(buf)+TextWidthWin("..")<=availPx) {
            strcat(buf,".."); return;
        }
    }
    if(TextWidthWin("..")<=availPx) strcpy(buf,"..");
}

void UiDrawAchievementsPage(MenuGeo g) {
    AchGeo a;
    AchCompute(&a);
    int line = TextHeightWin();
    int left = a.panel.left + a.padX, right = a.panel.right - a.padX;
    int inner = right - left;
    UiPanel9(a.panel.left, a.panel.top, a.panel.right, a.panel.bottom, 0);
    DrawTextCenterWin((left + right) / 2, a.titleY, Tr("ACHIEVEMENTS", "成就"), UI_ACCENT);
    DrawTextSmallLiteralWin(left, a.titleY, Tr(AchCatNameEn(achFilter), AchCatNameZh(achFilter)), UI_INK);
    int done = AchUnlockedInFilter(), total = AchVisibleCount();
    char count[24];
    snprintf(count, sizeof(count), "%d/%d", done, total);
    DrawTextWin(right - TextWidthWin(count), a.titleY, count, UI_ACCENT);
    int trackY = a.firstY - UIS(5);
    BlendRectWin(left, trackY, right, trackY + UIS(2), UI_EDGE, 1);
    if (total)
        BlendRectWin(left, trackY, left + inner * done / total, trackY + UIS(2), UI_ACCENT, 1);
    for (int i = 0; i < a.visible && g.scrollOffset + i < a.count; i++) {
        int row = g.scrollOffset + i;
        int y0 = a.firstY + i * a.rowH;
        int y = y0 + (a.rowH - line) / 2;
        UiRow(left, y0, right, y0 + a.rowH - UIS(2),
              row == achSelection ? UI_ROW_SEL : UI_ROW_NORMAL, 1);
        int id = AchIdAtRow(row);
        if (id < 0) {
            DrawTextWin(left + line / 2, y, Tr("BACK", "返回"), UI_INK);
            continue;
        }
        const AchDef *def = &kAchDefs[id];
        const Achievement *entry = &g_achievements[id];
        int locked = !entry->unlocked;
        char name[100], progress[40], reward[24];
        snprintf(name, sizeof(name), "%s", locked && def->hidden ? "???" : Tr(def->nameEn, def->nameZh));
        if (!locked)
            snprintf(progress, sizeof(progress), "%s", Tr("DONE", "已完成"));
        else if (def->hidden)
            snprintf(progress, sizeof(progress), "?");
        else if (def->kind == AK_FLAG)
            snprintf(progress, sizeof(progress), "%s", Tr("TO DO", "未完成"));
        else
            snprintf(progress, sizeof(progress), "%d/%d",
                     entry->progress > entry->target ? entry->target : entry->progress, entry->target);
        reward[0] = 0;
        if (locked) snprintf(reward, sizeof(reward), "+%d", def->reward);
        int progressX = right - line / 2 - TextWidthWin(progress);
        int rewardX = progressX - line / 2 - TextWidthWin(reward);
        ClampRowText(name, rewardX - left - line * 2);
        DrawTextWin(left + line / 2, y, name, locked ? UI_INK : UI_INK_DIM);
        if (locked) DrawTextWin(rewardX, y, reward, UI_ACCENT);
        DrawTextWin(progressX, y, progress, locked ? UI_INK_DIM : UI_ACCENT);
    }

    int id = AchIdAtRow(achSelection);
    const char *detail = Tr("RETURN TO MAIN MENU", "返回主菜单");
    if (id >= 0) {
        const AchDef *def = &kAchDefs[id];
        detail = def->hidden && !g_achievements[id].unlocked
            ? Tr("HIDDEN CHALLENGE", "隐藏挑战")
            : Tr(def->descEn, def->descZh);
    }
    char description[192];
    snprintf(description, sizeof(description), "%s", detail);
    ClampRowText(description, inner);
    DrawTextWin(left, a.stripY1, description, UI_INK);
    DrawTextSmallLiteralWin(left, a.stripY2, Tr("TAB / Q / E: CATEGORY", "TAB / Q / E：切换分类"), UI_INK_DIM);
}

/* ------------------------------------------------------------------
 * 2026-11-08（实机反馈）：躲柜子时的威胁分档
 * ------------------------------------------------------------------
 * 三段全覆盖，不再留空白区：
 *   < 8 格    → 0（它在外面找你）
 *   8~12 格   → 1（它在附近）
 *   > 12 格   → 2（外面安静了）
 * 没有怪时 TelegraphNearestDist 返回 1e9，自然落进 2。
 * ------------------------------------------------------------------ */
int HideStatusBand(float nearestDist) {
    if (nearestDist < 8.0f)  return 0;
    if (nearestDist <= 12.0f) return 1;
    return 2;
}

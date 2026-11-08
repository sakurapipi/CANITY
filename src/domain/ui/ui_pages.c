/* src/domain/ui/ui_pages.c —— 成就页 + 文本工具
 *
 * 第 1 轮（2026-11-08）从 ui.c 原样搬过来，逻辑一行没改。
 * 契约：include/ui_pages.h
 */
#include "common.h"
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
    A->count = ACH_COUNT + 1;          /* + Back */
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
    AchGeo A;
    AchCompute(&A);
    int TH = TextHeightWin();
    int sel = achSelection;

    /* 面板：和 Hub / 技能树统一成深色 + 红色描边，不再单独一套灰 */
    BlendRectWin(A.panel.left, A.panel.top, A.panel.right, A.panel.bottom,
                 0x000a1016u, 0.96f);
    FrameRectWin(A.panel.left, A.panel.top, A.panel.right, A.panel.bottom,
                 0x00801818u, 0.85f, UIS(2));

    int cx = (A.panel.left + A.panel.right) / 2;
    DrawTextCenterWin(cx, A.titleY, "ACHIEVEMENTS", PAL_GOLD);

    int left  = A.panel.left + A.padX;
    int right = A.panel.right - A.padX;

    /* ================================================================
     * v6 第 ㉘ 轮：**标题行三块撞在一起**
     * ================================================================
     * 实测截图：「ALL 0/32」「ACHIEVEMENTS」「[TAB] CATEGORY」
     * 三个文本叠在同一个 y 上，右边两段互相压住，读不出来。
     *
     * 现在：
     *   左  = 分类名（ALL / PROGRESS / …）
     *   中  = 标题
     *   右  = 该类进度 12/32
     *   TAB 提示挪到**底部操作栏**（那里本来就有 R x2 RESET / WHEEL ZOOM 之类）
     * ================================================================ */
    {
        extern int achFilter, AchVisibleCount(void), AchUnlockedInFilter(void);
        static char cnt[48];

        /* 左：分类名（短，不会被挤） */
        /* 2026-11-08（第 2 轮联通修复）：中文模式一直显示英文分类名 ——
           AchCatNameZh 写好了但全工程 0 调用（审计报告点名）。
           这里按语言选。 */
        {
            extern int g_lang;
            extern const char* AchCatNameZh(int cat);
            const char* catName = (g_lang == 1) ? AchCatNameZh(achFilter)
                                                : AchCatNameEn(achFilter);
            DrawTextWin(left, A.titleY, catName, PAL_CYAN);
        }

        /* 右：该类进度 */
        sprintf(cnt, "%d/%d", AchUnlockedInFilter(), AchVisibleCount());
        DrawTextWin(right - TextWidthWin(cnt), A.titleY, cnt, PAL_GOLD);
    }

    int boxS = TH / 2;                  /* 状态小方块 */
    if (boxS < 6) boxS = 6;
    int nameX = left + boxS + TH / 2;
    char progSample[] = "999/999";
    int progW = TextWidthWin(progSample);

    for (int i = 0; i < A.visible; i++) {
        int row = g.scrollOffset + i;
        if (row >= A.count) break;
        int idx = AchIdAtRow(row);      /* v6：行号 → 成就 id（受筛选影响） */
        if (idx < 0) break;
        int top = A.firstY + i * A.rowH;
        if (top + A.rowH > A.stripRuleY) break;
        int y = top + (A.rowH - TH) / 2;
        int isSel = (row == sel);

        if (isSel) {
            int bt = top + A.rowH / 8, bb = top + A.rowH - A.rowH / 8;
            BlendRectWin(left - TH / 2, bt, right + TH / 2, bb, 0x001C3A1Cu, 0.85f);
            BlendRectWin(left - TH / 2, bt, left - TH / 2 + TH / 6, bb,
                         0x0060E070u, 0.95f);
        }

        if (idx >= ACH_COUNT) {          /* Back 行 */
            DrawTextWin(nameX, y, "Back", isSel ? 0x0098FF98u : 0x00808080u);
            continue;
        }

        const Achievement* a = &g_achievements[idx];

        /* 状态方块：解锁 = 实心绿，未解锁 = 空心 */
        int by = y + (TH - boxS) / 2;
        if (a->unlocked) {
            BlendRectWin(left, by, left + boxS, by + boxS, 0x0040D060u, 0.95f);
        } else {
            FrameRectWin(left, by, left + boxS, by + boxS, PAL_DIM, 0.9f, 2);
        }

        unsigned int nameCol = a->unlocked ? 0x00E8E8E8u
                                           : (isSel ? 0x0098FF98u : 0x009A9A9Au);
        /* ============================================================
         * v6 第 ㉘ 轮：**行里从来没显示过奖励**
         * ============================================================
         * 第 ⑥ 轮我在 ShopRowText 那张表里写了 "+%d GOLD"，
         * 但成就墙**根本不走那张表** —— 这里自己 sprintf 了一遍。
         * 结果：玩家看不到任何一个成就能拿多少金币。
         *
         * 现在：奖励画在进度左边，金色；隐藏成就没解锁时连名字都不给。
         * ============================================================ */
        {
            extern const AchDef kAchDefs[ACH_COUNT];
            const AchDef* d = &kAchDefs[idx];
            int locked = !a->unlocked;

            char nm[64];
            if (locked && d->hidden)        sprintf(nm, "???");
            else                            sprintf(nm, "%s", a->name);
            ClampRowText(nm, (right - progW - TH * 4) - nameX);
            DrawTextWin(nameX, y, nm, nameCol);

            /* ============================================================
             * 修：**奖励列与进度列重叠，`+600` 被挤成 `+6000`**
             * ============================================================
             * 原来奖励用**估算值** progW 定位，进度用**实测宽度**右对齐：
             *   · 进度 "0/4"     宽 ~24px  → 落在右侧，不撞
             *   · 进度 "0/100000" 宽 ~128px → 向左伸出去，压到奖励上
             * 实测 ui_layout.txt：`+600`(476..540) × `0/100000`(536..664)，交叠 4px。
             *
             * 现在先量出进度的真实宽度，再把它左边的空间让给奖励。
             * ============================================================ */
            char pr[32];
            if (locked && d->hidden) sprintf(pr, "?");
            /* ============================================================
             * 2026-11-08（实机反馈）：**flag 类成就的分母是错的**
             * ------------------------------------------------------------
             * 截图里出现 `FIRST EXIT 0/15`、`TEN MINUTES 0/2`、`HALF HOUR 0/3`。
             *
             * 原因：AK_FLAG 的 target 字段存的是**flag 下标**（1/15/2/3），
             * 不是"要达成多少次"。直接 %d/%d 打出来就成了 0/15 ——
             * 玩家会以为要撤离 15 次。
             *
             * flag 类只有"达成/未达成"两态，所以显示 OK / --。
             * ============================================================ */
            else if (d->kind == AK_FLAG)
                sprintf(pr, "%s", a->unlocked ? "OK" : "--");
            else                     sprintf(pr, "%d/%d", a->progress, a->target);
            {
                int pw = TextWidthWin(pr);
                if (locked) {
                    char rw[24];
                    sprintf(rw, "+%d", d->reward);
                    DrawTextWin(right - pw - TH / 2 - TextWidthWin(rw), y, rw,
                                isSel ? PAL_GOLD : 0x00A08040u);
                }
                DrawTextWin(right - pw, y, pr,
                            a->unlocked ? 0x0040D060u : (isSel ? 0x00C8C8C8u : 0x00747880u));
            }
        }
    }

    /* 右侧滚动条 */
    if (A.count > A.visible) {
        int trackW = TH / 8; if (trackW < 3) trackW = 3;
        int tx = A.panel.right - trackW - TH / 4;
        int ty = A.firstY;
        int th2 = A.visible * A.rowH;
        BlendRectWin(tx, ty, tx + trackW, ty + th2, 0x00303038u, 0.9f);
        int thumbH = (A.visible * th2) / A.count;
        if (thumbH < TH) thumbH = TH;
        int maxScroll = A.count - A.visible;
        int thumbY = ty + (maxScroll > 0 ? (g.scrollOffset * (th2 - thumbH) / maxScroll) : 0);
        if (thumbY + thumbH > ty + th2) thumbY = ty + th2 - thumbH;
        BlendRectWin(tx, thumbY, tx + trackW, thumbY + thumbH, 0x00808090u, 0.95f);
    }

    /* 底部说明条：固定位置 + 自动折行，永远不会跑出面板 */
    {
        int rt = TH / 10; if (rt < 2) rt = 2;
        BlendRectWin(left, A.stripRuleY, right, A.stripRuleY + rt, 0x00502A32u, 0.95f);

        if (sel >= 0 && sel < ACH_COUNT) {
            const Achievement* a = &g_achievements[sel];
            char wl[2][96];
            int n = WrapText(a->desc, right - left, wl, 2);
            for (int k = 0; k < n; k++)
                DrawTextWin(left, k == 0 ? A.stripY1 : A.stripY2, wl[k], 0x00C0C0C8u);
            if (n == 0)
                DrawTextWin(left, A.stripY1, "-", 0x00808088u);
        } else {
            DrawTextWin(left, A.stripY1, Tr("BACK TO MENU", "返回主菜单"), 0x00808088u);
        }

        /* 滚动位置提示（右侧，和说明分开放） */
        if (A.count > A.visible) {
            char sc[48];
            /* v3.3 **页码要减掉 Back 行** ——
               试玩实测：「标题写 ACHIEVEMENTS 0/40，右下角却写 1-10/41」。
               根因：A.count = ACH_COUNT + 1（末尾那行 Back 也算进去了），
               而标题用的是真正的 ACH_COUNT，两个数字说的不是一件事。 */
            int realCount = A.count - 1;          /* 去掉 Back 行 */
            if (realCount < 1) realCount = 1;
            sprintf(sc, "%d-%d/%d", g.scrollOffset + 1,
                    g.scrollOffset + A.visible > realCount ? realCount : g.scrollOffset + A.visible,
                    realCount);
            DrawTextWin(right - TextWidthWin(sc), A.stripY2, sc, 0x00606068u);
        }
    }
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

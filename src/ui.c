/*

 * Canity — UI 层实现：点阵字库 / 文字 / 菜单与弹窗 / HUD / 最终呈现
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "monster_rules.h"
#include "hush.h"
#include "room_theme.h"
#include "perception.h"
#include "item_effects.h"
#include "run_rewards.h"
#include "state.h"
#include "threat_telegraph.h"
#include "wrench.h"

/* 商店/改造树的返回按钮（实现在本文件靠后，这里先声明） */
void ShopBackButtonRect(int* x0, int* y0, int* x1, int* y1);
int  ShopBackButtonAt(int mx, int my);
#include "campaign.h"
#include "daily.h"
#include "economy.h"
#include "threat.h"
#include "upgrade_run.h"
#include "wallet.h"
#include "warden.h"
#include "build_tags.h"
#include "cell_meta.h"
#include "plat.h"
#include "spider.h"
#include "control_effect.h"
#include "hatch.h"
#include "map_rooms.h"
#include "art_direction.h"
#include "achievements.h"
#include "events.h"
#include "ui.h"
#include "ui_text.h"
#include "ui_pages.h"
#include "ui_menu.h"
#include "lang.h"
#include "weapons.h"
#include "shop.h"
#include "bench.h"
#include "lockbox.h"   /* v3.0 锁盘 */   /* v2.7 改造树 */
#include "ui_theme.h"
#include "sprites.h"
#include "relics.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "skills.h"
#include "bestiary.h"
#include "score.h"
#include "entity.h"
#include "render.h"
#include "save.h"

/* 前置声明 */

/* 成就页固定显示 10 行；面板/行高/列表起点都走 UIS，按窗口高度等比缩放 */

#define ACH_PANEL_W      UIS(600)
#define ACH_PANEL_H      UIS(400)



/* 大厅：金币货币 + 六项永久升级 */
/* v1.8：7 种主题。原来只有 4 项，但 themeNames[themeId] 在主题 ≥4 时
   越界读到 NULL → DrawTextWin 解引用空指针 → 直接崩溃。 */
/* ------------------------------------------------------------------ */
/* UI：字体缓存 / 半透明面板 / 文字                                     */
/* ------------------------------------------------------------------ */

/* 8x12 点阵字库，ASCII 32..126（由 GDI 位图字体 1:1 采样生成）
   FONT_W / FONT_H 定义在文件前部（RenderMap 需要提前用）。 */

/* 文字直接写进像素缓冲：和面板同一层，不存在“文字层丢失” */

/* v1.8：行文本按可用宽度截断。
   字号会随窗口尺寸变化（TextAdvWin），固定字符串在窄窗口/非 16:9 下会撑出面板，
   所以统一在绘制前剪一次，末尾用 ".." 表示省略。 */

/* v2.9：技能树节点名的中文并列表（SkillDef 没中文字段，这里按英文名查） */





/* xWin/yWin 用窗口坐标，内部换算到渲染坐标 */
/* v1.9：UI 布局自检 —— 每次画文字都记下它的矩形。
   改动 UI 后按 F9 就会把这一帧的所有文字矩形写入 ui_layout.txt，
   tools/ui_layout_check.py 读它做「矩形是否重叠」的判定 —— 不用猜像素。 */
#define UI_RECT_MAX 256
typedef struct { int x0, y0, x1, y1; char label[24]; } UiRectRec;
static UiRectRec g_uiRects[UI_RECT_MAX];
static int g_uiRectCount = 0;

void UiLayoutBegin(void) { g_uiRectCount = 0; }

void UiTrack(int x, int y, int w, int h, const char* label) {
    if (g_uiRectCount >= UI_RECT_MAX) return;
    UiRectRec* r = &g_uiRects[g_uiRectCount++];
    r->x0 = x; r->y0 = y; r->x1 = x + w; r->y1 = y + h;
    int i = 0;
    if (label) {
        for (; i < 23 && label[i]; i++) r->label[i] = label[i];
    }
    r->label[i] = 0;
}

void DumpUILayout(const char* path) {
    /* 写到 exe 所在目录，避免受工作目录影响 */
    char full[MAX_PATH];
    PlatExeDir(full, MAX_PATH);
    char* slash = strrchr(full, '\\');
    if (slash) slash[1] = 0; else full[0] = 0;
    strncat(full, path, MAX_PATH - strlen(full) - 1);

    FILE* f = fopen(full, "w");
    if (!f) {
        /* v2：exe 同目录写不进去时（实测 game\ 下 fopen 失败，存档也是同一个坑），
           退回 %TEMP%，否则自检脚本永远看不到任何东西。 */
        const char* tmp = getenv("TEMP");
        if (!tmp || !*tmp) tmp = getenv("TMP");
        if (tmp && *tmp) {
            snprintf(full, sizeof(full), "%s\\canity_ui_layout.txt", tmp);
            f = fopen(full, "w");
        }
    }
    if (!f) return;
    fprintf(f, "# CANITY UI layout dump  rects=%d  canvas=%dx%d\n", g_uiRectCount, winW, winH);
    for (int i = 0; i < g_uiRectCount; i++) {
        UiRectRec* r = &g_uiRects[i];
        fprintf(f, "%d %d %d %d %s\n", r->x0, r->y0, r->x1, r->y1, r->label);
    }
    fclose(f);
}


/* 标题加粗：右移一格再画一遍 */
void DrawTitleWin(const char* t, int yWin, unsigned int color) {
    /* 修：标题被画两遍（偏移 1px、同色）。
       ui_layout.txt 里每个标题都是两条记录：
         "SKILL TREE"(320,20,480,44) 和 (321,20,481,44)
       同色 +1px 不是阴影，只会让字发毛。画一遍就够。 */
    DrawTextCenterWin(winW / 2, yWin - 2 * TextScale(), t, color);
}

/* v2.2 修复：UI 尺寸必须跟着**实际文字高度**缩放，而不是窗口高度。
 *
 * 原来的 bug（用户反馈「窗口一大，UI 全是 bug」）：
 *   UIS()    = v * winH / 600            <- 线性跟窗口高
 *   文字高度 = 24 * winH / renderH        <- renderH 被 PIXEL_BUDGET 卡在 ~400
 * 大窗口下文字高度按 2.7 倍涨、UIS 只按 1.8 倍涨 -> 字比行高还高，
 * 面板、页脚、每一行全部溢出重叠。
 *
 * 现在统一：所有 UI 尺寸 = 设计值 x (实际文字高 / 600 高时的文字高)。 */
#define UI_BASE_TH 31          /* 800x600 时 TextHeightWin() 的实测值 */

int UIS(int v) {
    /* v3.3：UI 尺寸跟**实际字体高度**走，而不是单独跟 renderH 走。
       否则字体缩放取整后（比如 MINIMAL 下 s=1）面板还按 renderH 算，
       两者比例又对不上，界面就会忽大忽小。 */
    long s = ((long)v * TextHeightWin() + 12) / 24;
    if (s < 6) s = 6;
    if (s > 2000) s = 2000;
    return (int)s;
}

/* ============================================================
 * 技能树 v2 —— 节点式布局（3 列 × 4 行 + 底部跨支 1 行）
 *
 * 为什么不用 UIS()：UIS 是按窗口高度线性缩放的，而字号是
 * 12*2*(winH/renderH) —— renderH 又被像素预算压过一道，
 * 所以 800x600 与 1920x1080 下「文字/画布」的比例并不相同。
 * 这里所有尺寸都以 **TH（文字高度）** 为单位，两种分辨率才一致。
 * ============================================================ */


/* 分支主色：只用在标题、进度条、连线上，不参与节点状态色 */
static const unsigned int kBranchColor[7] = {0x79CDF2,0xB8D9FA,0xE8ADB7,0xB6BBC9,0xD0C6ED,0x98D8C8,0xF1F5FF};
/* v2.9：分支名走翻译（原来只有英文） */
/* v3.3：第 3 列从「伤害交付」改成「藏匿脱身」后，表头没跟着改 ——
   界面上写 DELIVERY，里面却是 HOLD BREATH / STILL FACE / SLIP AWAY /
   PLAY DEAD（见 skills.c 的「列 2：隐」）。改成 CONCEAL。 */

/* 面板内竖向预算，单位 = TH/10。
 *   TEXT 部分（文字行 + 跨支行高度）不吃压缩
 *   GAP  部分在窗口不够高时按比例压缩（k10 = 10 表示不压缩）
 *   WANT 是网格想占的高度（4 个节点 ×1.75TH + 3 个间隔 ×1/3TH） */
#define SST_TEXT10 74
#define SST_GAP10  30
#define SST_WANT10 80

/* ===== v3.4 **局外技能树布局表（界面重写）** =====
   旧版：20 个节点排 4 列 × 5 行（第 5 行是跨支 keystone）。
   问题：方案 v5 退役了 8 个纯属性节点，它们还占着格子，
        整页看起来空一半、而且「效果微乎其微」的节点和真机制节点混在一起。

   现在：只排活着的 12 个，4 列 × 3 行 + 底部一条跨支行。
        全部是机制，没有一个是 +N%。 */
/* ===== v6 **技能树改成列表式** =====
   用户要求：「彻底重写，以后想加多少技能就加多少技能，不用排版」。
   现在按「分支 → 层数」排成一条竖列表，和局内改造树完全一致的视觉：
     · 一行一个节点：名字 + 等级 + 价格
     · 上下键逐行移动
     · 底部详情条说明效果
   加技能只需要往 skillDefs 里加一行，列表自动变长，不用动布局。
   顺序表 = 显示顺序（也是导航顺序）。 */
/* ===== v6 **技能树：20 个节点，显式坐标表** =====
   用户：「多放点技能进去」「页面在重叠」。
   ① 之前用 SkillNodeRow(id) 推导行号，退役节点留下的空位让整列错位 →
      现在每个节点写死 (col,row)，布局绝对可控。
   ② 原来只有 12 个活节点（8 个被置为退役）→ 现在全部启用，共 20 个。

   网格：4 列 × 4 行 + 第 5 行 3 个 + 底部跨支行 SILENT RUN */


/* 活节点列表 = 坐标表的顺序（用于 owned 统计和初始光标） */
/* v6：**26 个节点全部上图**（原来是 20 个手写列表 + 一个跨支行）。
   现在直接按 0..SKILL_COUNT-1 生成，加节点不用改这里。 */
static const int kSkillList[] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,27,28,29,
    /* 2026-11-08：三个流派共 10 个节点。这里原来是手写 0..29 的字面量，
       注释却说"加节点不用改这里" —— 所以新节点一直没被画出来（界面显示 1/30）。 */
    30, 31, 32, 33,      /* 幽灵：呼吸同步 / 影步 / 静默落地 / 无形 */
    34, 35, 36,          /* 掠夺者：典当 / 赌徒 / 金币即力量 */
    37, 38, 39           /* 反击者：挣脱 / 以牙还牙 / 反杀 */,41,42,43,44,45,46,47,48,49,50
};
#define SKILL_LIST_COUNT ((int)(sizeof(kSkillList) / sizeof(kSkillList[0])))

/* ==================================================================
 * 2026-11-08：**编译期断言 —— 绘制列表必须覆盖全部节点**
 * ------------------------------------------------------------------
 * 教训：我在 skills.c 加了 10 个节点、布局算好了、测试也过了，
 * 但 `kSkillList[]` 还是手写的 0..29，界面一个都没画出来，
 * 玩家看到的是 `1 / 30 NODES`。
 *
 * 测试查的是 SKILL_COUNT，界面用的是 SKILL_LIST_COUNT —— 两个常量
 * 各自为政。这一行让它们一旦不一致就**编译不过**。
 * ================================================================ */
typedef char skill_list_must_cover_all_nodes[
    (sizeof(kSkillList) / sizeof(kSkillList[0]) == SKILL_PLAYER_COUNT) ? 1 : -1];
/* v6：中心节点取代原来的"跨支行" */
#define SKILL_CROSS_ID 25

/* 某个节点在列表里的位置；-1 = 不在列表里（退役节点） */
static int SkillListIndex(int id) {
    for (int k = 0; k < SKILL_LIST_COUNT; k++)
        if (kSkillList[k] == id) return k;
    return -1;
}




/* ============================================================
 * 主菜单布局（v2.2 重做）
 *
 * 原来的问题：面板固定 520 宽，8~10 行按 46px 硬排，
 * "Achievements" 12 个字符几乎顶满行宽，货币和 BEST LEVEL 还露在面板外面，
 * 整页看起来很挤。
 *
 * 现在：面板按文字高度定宽（26 TH），行高在 1.2~1.75 TH 之间自适应，
 * 标题区 / 行区 / 页脚三块分开，页脚画在面板里面。
 * ============================================================ */

/* 主菜单行数（含不固定的 Continue / Endless） */


/* 主菜单行数（与 MenuLabel 里的那张表保持一致）：Continue / Endless 会增减一行 */

/* ============================================================
 * 成就页布局（v2.3 重做）
 *
 * 这一页原来的毛病：
 *   ① 每行是 "[*] NAME  (3/5)" 拼成一整串居中画，长名字 + 大字号会顶出面板
 *   ② 说明是跟着鼠标飘的 tooltip，位置用的是**客户区像素**却当逻辑画布坐标用，
 *      窗口一大 tooltip 就飘到屏幕外（用户截图里那条横出来一截就是这个）
 *   ③ 面板固定 600x400，和 Hub / 技能树的风格完全不统一
 *
 * 现在：三列（状态框 / 名称 / 进度）+ 底部固定说明条（自动折行），
 * 尺寸全部按文字高度算，任何窗口都不出界。
 * ============================================================ */


/* 按空格折行：拆成至多 maxLines 行，每行不超过 availPx；返回实际行数 */

static MenuGeo MakeGeo(const char* title, int count, int rowH, int pad, int width, int titlePx, int stateIndex) {
    MenuGeo g;
    memset(&g, 0, sizeof(g));
    g.title = title;
    g.count = count;
    g.rowH = rowH;

    int titleH = titlePx;
    /* v2.9：**行高兜底** —— 原来 rowH 用 UIS 缩、文字高用 TH 缩，
       两套比例反向，窗口一大行就压字（报告 严重 #1）。
       这里保证行高至少容得下一行文字。 */
    {
        int minRow = TextHeightWin() + UIS(4);
        if (rowH < minRow) rowH = minRow;
    }
    int innerH = titleH + UIS(24) + count * rowH;
    /* v2.4：安全屋多留一条校准条的位置（画在面板底部空白区） */
    if (stateIndex == STATE_SAFEHOUSE) innerH += TextHeightWin() + UIS(40);
    int panelH = pad * 2 + innerH;
    /* v1.8：行数多时面板会长过窗口（技能树 16 行在 640px 窗口里就溢出了）。
       这里把高度夹到窗口内，再按夹后的高度重算可见行数 —— 剩下的靠滚动。 */
    {
        int maxH = winH - UIS(40);
        if (maxH < UIS(240)) maxH = UIS(240);
        if (panelH > maxH) panelH = maxH;
    }
    int panelW = width;
    if (panelW > winW - 60) panelW = winW - 60;
    if (panelW < 160) panelW = 160;

    g.panel.left = (winW - panelW) / 2;
    g.panel.right = g.panel.left + panelW;
    g.panel.top = (winH - panelH) / 2;
    g.panel.bottom = g.panel.top + panelH;
    g.titleY = g.panel.top + pad;
    g.firstY = g.titleY + titleH + UIS(24);

    /* 计算面板内容区高度，决定最多显示多少行 */
    int contentH = g.panel.bottom - g.firstY;
    g.maxVisibleRows = contentH / rowH;
    if (g.maxVisibleRows < 1) g.maxVisibleRows = 1;

    /* 使用持久化的滚动偏移 */
    /* v2.7：stateIndex 可能是 -1（弹窗几何），原来直接下标读 → 越界读 */
    g.scrollOffset = (stateIndex >= 0 && stateIndex < 16)
                     ? menuScrollOffsets[stateIndex] : 0;
    /* 修正偏移：不能超过最大值 */
    int maxOffset = g.count - g.maxVisibleRows;
    if (maxOffset < 0) maxOffset = 0;
    if (g.scrollOffset > maxOffset) g.scrollOffset = maxOffset;

    return g;
}

/* SkillTreeCompute：技能树几何。ui.c 的技能树页与 ui_menu.c 的菜单都要用，
 * 所以留在 ui.c，类型在 include/ui_menu.h。 */
void SkillTreeCompute(SkillTreeLayout* L) {
    int TH = TextHeightWin();
    if (TH < 8) TH = 8;
    memset(L, 0, sizeof(*L));

    int m = TH / 2;
    L->panel.left   = m;
    L->panel.top    = m;
    L->panel.right  = winW - m;
    L->panel.bottom = winH - m;
    int panelH = L->panel.bottom - L->panel.top;
    if (panelH < TH * 8) panelH = TH * 8;      /* 极小窗口兜底 */

    L->padX = (TH * 4) / 5;
    int innerL = L->panel.left + L->padX;
    int innerR = L->panel.right - L->padX;
    int innerW = innerR - innerL;

    /* 间距压缩系数：窗口越矮压得越多，下限 0.5 */
    int k10;
    {
        int avail10 = (panelH * 10) / TH;      /* 面板高度换成 TH/10 单位 */
        k10 = ((avail10 - SST_TEXT10 - SST_WANT10) * 10) / SST_GAP10;
        if (k10 < 5)  k10 = 5;
        if (k10 > 10) k10 = 10;
    }
#define SST_T(u) ((TH * (u)) / 10)                  /* 不压缩的固定高度 */
#define SST_G(u) ((TH * (u) * k10) / 100)           /* 可压缩的间距 */

    L->titleY = L->panel.top + SST_G(5);
    L->curY   = L->titleY + SST_T(10) + SST_G(2);
    L->hdrY   = L->curY   + SST_T(10) + SST_G(2);
    L->barY   = L->hdrY   + SST_T(10) + SST_G(1);
    L->gridTop = L->barY  + SST_T(2)  + SST_G(3);

    int bottom   = L->panel.bottom - SST_G(5);
    L->hintY     = bottom - SST_T(10);
    L->detail2Y  = L->hintY    - SST_G(2) - SST_T(10);
    L->detail1Y  = L->detail2Y - SST_G(2) - SST_T(10);
    L->crossBot  = L->detail1Y - SST_G(4);
    L->crossH    = SST_T(12);
    L->crossTop  = L->crossBot - L->crossH;
    L->gridBottom = L->crossTop - SST_G(4);

    /* 网格：4 行 + 3 个间隔，多出来的高度居中，不够就夹到上下限 */
    int gridH = L->gridBottom - L->gridTop;
    if (gridH < TH * 2) gridH = TH * 2;
    /* v3.4：**3 行**（12 个节点 = 4 列 × 3 行），原来 4 行 */
    int gap = TH / 3;
    if (gap < 4) gap = 4;
    int nodeH = (gridH - 2 * gap) / 3;
    if (nodeH > (TH * 26) / 10) nodeH = (TH * 26) / 10;
    if (nodeH < (TH * 14) / 10) nodeH = (TH * 14) / 10;
    L->nodeH = nodeH;

    int usedH = 3 * nodeH + 2 * gap;
    int y0 = L->gridTop + (gridH > usedH ? (gridH - usedH) / 2 : 0);
    for (int r = 0; r < 3; r++) L->rowCY[r] = y0 + r * (nodeH + gap) + nodeH / 2;
    L->rowCY[3] = L->crossTop + L->crossH / 2;
    L->rowCY[4] = L->rowCY[3];

    /* 列：4 列平分 */
    int gapX = (TH * 35) / 100;
    if (gapX < 6) gapX = 6;
    int nodeW = (innerW - (SKILL_GRID_COLS - 1) * gapX) / SKILL_GRID_COLS;
    if (nodeW < TH * 3) nodeW = TH * 3;
    L->nodeW = nodeW;
    for (int c = 0; c < SKILL_GRID_COLS; c++)
        L->colCX[c] = innerL + gapX + nodeW / 2 + c * (nodeW + gapX);

    /* v3.4：**只给 12 个活着的节点分配矩形**，退役的不占位。
       没分配的 RECT 留 0，绘制和命中测试都会跳过。 */
    /* v6：**恢复树状图的节点矩形**（4 列 × 3 行 + 底部跨支行）。
       只有活着的节点才分配矩形，退役的留 0 —— 绘制和命中都会跳过。 */
    for (int id = 0; id < SKILL_COUNT; id++)
        memset(&L->node[id], 0, sizeof(RECT));

    for (int k = 0; k < SKILL_LIST_COUNT; k++) {
        int id = kSkillList[k];
        if (id < 0 || id >= SKILL_COUNT) continue;
        if (id == SKILL_CROSS_ID) continue;          /* 跨支行单独处理 */
        int c = SkillNodeCol(id);
        int r = SkillNodeRow(id);
        if (c < 0 || c >= SKILL_GRID_COLS || r < 0 || r > 2) continue;
        RECT rc;
        rc.left   = L->colCX[c] - nodeW / 2;
        rc.right  = rc.left + nodeW;
        rc.top    = L->rowCY[r] - nodeH / 2;
        rc.bottom = rc.top + nodeH;
        L->node[id] = rc;
    }
    {
        RECT rc;
        rc.left   = L->colCX[0] - nodeW / 2;
        rc.right  = L->colCX[SKILL_GRID_COLS - 1] + nodeW / 2;
        rc.top    = L->crossTop;
        rc.bottom = rc.top + L->crossH;
        L->node[SKILL_CROSS_ID] = rc;
    }

#undef SST_T
#undef SST_G
}

MenuGeo GetMenuGeo(void) {
    switch (currentState) {
        case STATE_MAIN_MENU: {
            /* v2.2：主菜单用自己的布局（宽面板 + 页脚在面板内） */
            MainMenuLayout M;
            MainMenuCompute(&M);
            MenuGeo g;
            memset(&g, 0, sizeof(g));
            g.title = "CANITY";
            g.count = M.count;
            g.rowH = M.rowH;
            g.panel = M.panel;
            g.titleY = M.titleY;
            g.firstY = M.firstY;
            g.maxVisibleRows = M.count;   /* 全部可见，不滚动 */
            g.scrollOffset = 0;
            return g;
        }
        case STATE_BANK: {
            /* v6：银行页也用技能树那套全屏面板几何 */
            SkillTreeLayout L;
            SkillTreeCompute(&L);
            MenuGeo g;
            memset(&g, 0, sizeof(g));
            g.title = "BANK";
            g.count = 1;
            g.rowH = L.nodeH;
            g.panel = L.panel;
            g.titleY = L.titleY;
            g.firstY = L.gridTop;
            g.maxVisibleRows = 1;
            g.scrollOffset = 0;
            return g;
        }
        case STATE_HUB:
            {
                /* v2 技能树：面板几何由技能树自己的布局决定（全屏式大面板），
                   不走 MakeGeo 的"列表行"那套 —— 节点不是行。 */
                SkillTreeLayout L;
                SkillTreeCompute(&L);
                MenuGeo g;
                memset(&g, 0, sizeof(g));
                g.title = "SKILL TREE";
                g.count = SKILL_COUNT;
                g.rowH = L.nodeH;
                g.panel = L.panel;
                g.titleY = L.titleY;
                g.firstY = L.gridTop;
                g.maxVisibleRows = SKILL_COUNT;   /* 15 个节点全部可见，不滚动 */
                g.scrollOffset = 0;
                return g;
            }
            return MakeGeo("THE HUB", HUB_ROWS, UIS(50), UIS(30), UIS(700), UIS(46), STATE_HUB);
        case STATE_SKILL_PICK: {
            /* 2026-11-06（v2.4.1 第 3 轮）：第 6/12 层会连着给两次奖励，
               标题必须说清"这是第几次"，否则玩家以为界面卡住了、
               或者以为第二次是浪费。 */

            static char pickTitle[96];
            const char* base = (pickMode == 1)
                ? Tr("CHOOSE A RELIC", "选择遗物")
                : Tr("CHOOSE ONE", "三选一");
            /* 2026-11-06（v2.4.1 第 9 轮）：标题里带上当前流派倾向。
               用户要的是"看得出这一局正在形成什么玩法"，
               而不是只看到一排技能名。 */
            {

                BuildTag d = BuildDominant();
                const char* bn = (d != BUILD_NONE) ? BuildName(d, g_lang == LANG_ZH) : NULL;
                if (skillPickTotal > 1 && bn)
                    snprintf(pickTitle, sizeof(pickTitle), "%s  (%d/%d)  ·  %s",
                             base, skillPickRound, skillPickTotal, bn);
                else if (skillPickTotal > 1)
                    snprintf(pickTitle, sizeof(pickTitle), "%s  (%d/%d)",
                             base, skillPickRound, skillPickTotal);
                else if (bn)
                    snprintf(pickTitle, sizeof(pickTitle), "%s  ·  %s", base, bn);
                else
                    snprintf(pickTitle, sizeof(pickTitle), "%s", base);
            }
            return MakeGeo(pickTitle,
                           INRUN_CHOICES, UIS(48), UIS(30), UIS(780), UIS(56), STATE_SKILL_PICK);
        }
        case STATE_WEAPON_PICK: return MakeGeo(Tr("CHOOSE WEAPON", "选择初始武器"), WPN_COUNT, UIS(52), UIS(32), UIS(760), UIS(60), STATE_WEAPON_PICK);
        /* v2.9：安全屋不画大红标题 —— 顶部页签已经说清是哪个页面，
           那行「SAFE HOUSE - SHOP」横在中间会把改造树整个遮住（用户截图）。
           传空串让 MakeGeo 跳过标题绘制。 */
        /* v6 **安全屋面板固定尺寸** ——
           用户反馈：「切换菜单的时候明显会抽搐，因为后面的菜单大小不统一，
           每个按钮有自己的尺寸，导致老是漂移」。
           根因：这里传的是 ShopRowCount()，而每页行数不同：
             SHOP 9 行 / BENCH 4 行 / REST 5 行 → 面板高度每页都不一样。
           改成固定用最大行数（9），所有页共用同一块面板，
           切页时面板纹丝不动。 */
        case STATE_SAFEHOUSE: {
            MenuGeo g; memset(&g,0,sizeof(g));
            g.panel=(RECT){UIS(16),UIS(16),winW-UIS(16),winH-UIS(16)};
            g.title=""; g.count=ShopRowCount(); g.maxVisibleRows=g.count;
            g.firstY=g.panel.top+TextHeightWin()*2+UIS(24);
            int available=g.panel.bottom-g.firstY-UIS(58);
            g.rowH=available/9;
            if(g.rowH<TextHeightWin()+UIS(6)) g.rowH=TextHeightWin()+UIS(6);
            g.titleY=g.panel.top+UIS(8); g.scrollOffset=0;
            return g;
        }
        case STATE_BESTIARY: {
            MenuGeo g; memset(&g,0,sizeof(g));
            g.title=Tr("BESTIARY","怪物图鉴");
            g.panel=(RECT){UIS(20),UIS(20),winW-UIS(20),winH-UIS(20)};
            g.titleY=g.panel.top+UIS(14);
            g.firstY=g.titleY+TextHeightWin()+UIS(32);
            g.count=g.maxVisibleRows=BESTIARY_COUNT+1;
            g.rowH=(g.panel.bottom-g.firstY-TextHeightWin()-UIS(16))/g.count;
            g.scrollOffset=0; return g;
        }
        case STATE_HELP:      return MakeGeo(helpPage == 1 ? Tr("HELP  -  RULES", "帮助  -  规则") : Tr("HELP  -  CONTROLS", "帮助  -  按键"),
                                            HELP_PAGE_ROWS, UIS(29), UIS(22), UIS(1000), UIS(30), STATE_HELP);
        case STATE_PAUSED:    return MakeGeo("PAUSED", 3, UIS(56), UIS(38), UIS(520), UIS(56), STATE_PAUSED);
        case STATE_GAMEOVER:  return MakeGeo(gameWon ? "YOU ESCAPED" : "YOU LOST YOUR MIND", 3, UIS(48), UIS(34), UIS(660), UIS(40), STATE_GAMEOVER);
        case STATE_SETTINGS:  return MakeGeo(PageTitle(settingsPage), SettingsRowCount(), UIS(44), UIS(30), UIS(680), UIS(44), STATE_SETTINGS);
        case STATE_ACHIEVEMENTS: {
            /* v2.3: geometry and DrawAchievementsWindow share AchCompute, so
               the clickable rows and the drawn rows can never drift apart. */
            AchGeo A;
            AchCompute(&A);
            MenuGeo g;
            memset(&g, 0, sizeof(g));
            g.title = "ACHIEVEMENTS";
            /* v6：行数按当前分类筛选后的数量（否则会滚到空白行） */
            {  g.count = AchVisibleCount(); }
            if (g.count < 1) g.count = 1;
            g.rowH = A.rowH;
            g.panel = A.panel;
            g.titleY = A.titleY;
            g.firstY = A.firstY;
            g.maxVisibleRows = A.visible;

            g.scrollOffset = menuScrollOffsets[STATE_ACHIEVEMENTS];
            int maxOffset = g.count - g.maxVisibleRows;
            if (maxOffset < 0) maxOffset = 0;
            if (g.scrollOffset < 0) g.scrollOffset = 0;
            if (g.scrollOffset > maxOffset) g.scrollOffset = maxOffset;
            return g;
        }
        default: break;
    }
    return MakeGeo("", 0, UIS(44), UIS(30), UIS(400), UIS(44), -1);
}

MenuGeo GetPopupGeo(void) {
    if (popupItem < 0 || popupItem >= SETTING_COUNT) return GetMenuGeo();
    const SettingItem* it = &settings[popupItem];
    if (it->kind == SET_SLIDER) {
        /* 滑块面板：标题 + 数值 + 轨道 */
        MenuGeo g;
        memset(&g, 0, sizeof(g));
        g.title = it->name;
        g.count = 0;
        int pad = UIS(26), titlePx = UIS(34);
        int contentH = UIS(140);
        int panelW = UIS(460);
        if (panelW > winW - 80) panelW = winW - 80;
        int panelH = pad * 2 + titlePx + UIS(24) + contentH;
        g.panel.left = (winW - panelW) / 2;
        g.panel.right = g.panel.left + panelW;
        g.panel.top = (winH - panelH) / 2;
        g.panel.bottom = g.panel.top + panelH;
        g.titleY = g.panel.top + pad;
        g.firstY = g.titleY + titlePx + UIS(24);
        g.rowH = contentH;
        return g;
    }
    return MakeGeo(it->name, it->count, UIS(42), UIS(26), UIS(460), UIS(34), -1);
}

/* 滑块轨道（窗口坐标） */
RECT SliderTrackRect(MenuGeo g) {
    RECT r;
    int pad = UIS(40);
    r.left = g.panel.left + pad;
    r.right = g.panel.right - pad;
    r.top = g.firstY + UIS(46);
    r.bottom = r.top + UIS(12);
    return r;
}

/* 滑块值 -> 窗口 x */
int SliderValueToX(MenuGeo g, float v, float lo, float hi) {
    RECT t = SliderTrackRect(g);
    if (hi <= lo) return t.left;
    float f = (v - lo) / (hi - lo);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return t.left + (int)(f * (t.right - t.left));
}

/* 窗口 x -> 滑块值 */
float SliderXToValue(MenuGeo g, int x, float lo, float hi, float step) {
    RECT t = SliderTrackRect(g);
    float f = (t.right == t.left) ? 0.0f : (float)(x - t.left) / (float)(t.right - t.left);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    float v = lo + f * (hi - lo);
    v = lo + floorf((v - lo) / step + 0.5f) * step;   /* 吸附到步长 */
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

extern int safehouseHoverRow;   /* v3.0：鼠标悬停行（定义在 game.c） */
extern int safehouseHoverTab;   /* v3.0：悬停页签 */

RECT RowRect(MenuGeo g, int i) {
    RECT r;
    /* v3.0：安全屋的页签挪到了左侧竖排，内容行必须让开那一条 ——
       否则第一列文字会被 sidebar 盖住。 */
    int leftPad = UIS(18);
    if (currentState == STATE_SAFEHOUSE) {
        int sideW=(g.panel.right-g.panel.left)*26/100;
        if(sideW<UIS(140)) sideW=UIS(140);
        if(sideW>UIS(220)) sideW=UIS(220);
        leftPad=UIS(14)+sideW+UIS(20);
    }
    /* v7 第 ㊾ 轮：**坐标必须夹住**
       商店一进就崩，而 demo3（只调 ShopOpen、不进这一页）能活 ——
       说明崩在这条渲染路径。最可能的原因是某个坐标算成了
       巨大值 / 负数，BlendRectWin 拿着它去写像素缓冲。
       这里把 leftPad 和行高都夹进合理范围。 */
    if (leftPad < UIS(18)) leftPad = UIS(18);
    if (leftPad > UIS(400)) leftPad = UIS(400);
    r.left = g.panel.left + leftPad;
    r.right = g.panel.right - UIS(18);
    if (currentState == STATE_BESTIARY) r.right = g.panel.left + (g.panel.right-g.panel.left)*32/100 - UIS(8);
    if (r.right <= r.left) r.right = r.left + 1;

    int rowH = g.rowH;
    if (rowH < 4) rowH = 4;

    /* 应用滚动偏移：只绘制可见范围内的行 */
    int visibleIndex = i - g.scrollOffset;
    r.top = g.firstY + visibleIndex * rowH;
    r.bottom = r.top + rowH - UIS(8);
    if (r.bottom <= r.top) r.bottom = r.top + 1;
    return r;
}




/* 窗口坐标 -> 渲染坐标的矩形填充（半透明） */
void BlendRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha) {
    int rx0 = x0 * renderW / winW, rx1 = x1 * renderW / winW;
    /* v2.1：窗口像素折回渲染缓冲时，1px 的线在 1080p/1440p 会算成 0 宽直接消失。
       兜底保证至少 1 个渲染像素。 */
    if (rx1 <= rx0) rx1 = rx0 + 1;
    if (x1 - x0 >= 1 && rx1 - rx0 < 1) rx1 = rx0 + 1;
    int ry0 = y0 * renderH / winH, ry1 = y1 * renderH / winH;
    if (ry1 <= ry0) ry1 = ry0 + 1;   /* v2.1：同上的纵向兜底 */
    if (rx0 < 0) rx0 = 0;
    if (ry0 < 0) ry0 = 0;
    if (rx1 > renderW) rx1 = renderW;
    if (ry1 > renderH) ry1 = renderH;

    int r = (int)((color >> 16) & 0xFF), g = (int)((color >> 8) & 0xFF), b = (int)(color & 0xFF);
    int ia = (int)(alpha * 256.0f);
    int na = 256 - ia;

    for (int y = ry0; y < ry1; y++) {
        unsigned int* row = pixels + (size_t)y * renderW;
        for (int x = rx0; x < rx1; x++) {
            unsigned int d = row[x];
            int nr = (r * ia + ((int)((d >> 16) & 0xFF)) * na) >> 8;
            int ng = (g * ia + ((int)((d >> 8) & 0xFF)) * na) >> 8;
            int nb = (b * ia + ((int)(d & 0xFF)) * na) >> 8;
            row[x] = (unsigned int)((nr << 16) | (ng << 8) | nb);
        }
    }
}

void FrameRectWin(int x0, int y0, int x1, int y1, unsigned int color, float alpha, int thick) {
    BlendRectWin(x0, y0, x1, y0 + thick, color, alpha);
    BlendRectWin(x0, y1 - thick, x1, y1, color, alpha);
    BlendRectWin(x0, y0, x0 + thick, y1, color, alpha);
    BlendRectWin(x1 - thick, y0, x1, y1, color, alpha);
}

/* 主面板（不含弹窗）：弹窗必须画在列表文字之上，否则文字会压在弹窗上 */

/* v2.4：安全屋的校准条。
   一根横条，滑块左右扫，按空格锁定；中间亮区是普通区，更窄的白色是完美区。 */

/* v2.5：校准条本体（位置由调用方给）。
   拆出来是因为「游戏内按空格开始小游戏」时，安全屋面板根本没画，
   原来画在面板里的条就永远看不到。
   v3.5：这是**40 秒谱面进度条**，不是判定区。原来的绿色“安全区/完美区”
   是旧扫条玩法的残留，QWER 音游里它只会误导玩家对着某个时间点按空格。
   现在只画进度和播放头，判定看上方四条轨道的音符落点。 */
void DrawCalibBar(int bx, int by, int barW, int barH) {
    if (barW < UIS(120)) return;
    BlendRectWin(bx, by, bx + barW, by + barH, 0x00101418, 0.92f);
    {
        float p = calibPos;
        if (!(p >= 0.0f)) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        int px = bx + (int)(p * barW);
        if (px > bx) BlendRectWin(bx, by, px, by + barH, 0x00307A4A, 0.85f);
        int mw = UIS(4);
        BlendRectWin(px - mw / 2, by - UIS(3), px + mw / 2, by + barH + UIS(3),
                     calibActive ? 0x00FFFFFFu : 0x00606060u, 0.98f);
    }
    FrameRectWin(bx, by, bx + barW, by + barH, 0x00708090u, 0.8f, 1);
}



/* v2.8：局内改造树 —— **可平移缩放的画布**（思维导图式）。
   节点用世界坐标，绘制时统一变换：
       sx = (worldX - treeVX) * treeVK + winW/2
   好处：窗口多小都不会崩，节点数量可以无限加。

   顶部一排按钮 = 原来的四页（商店/改装/改造树/休息），
   点一下或按左右键切页，不用记 TAB。 */

/* 页签按钮：几何由 DrawPageTabs 计算，ClickPageTabs 复用同一套 */



void DrawPageTabs(void) {
    static const char* names[SAFEHOUSE_PAGES] = { "SUPPLIES", "RUN TREE", "RECOVERY", "UPGRADES" };
    static const char* namesZh[SAFEHOUSE_PAGES] = { "补给商店", "局内技能", "休整服务", "属性强化" };

    int th = TextHeightWin();
    int x0, y0, x1, y1;
    PageTabRect(0, &x0, &y0, &x1, &y1);

    /* 左侧竖分隔线：把 sidebar 和内容区切开 */
    int sepX = x1 + UIS(10);
    BlendRectWin(sepX, y0 - UIS(8), sepX + 1, winH - UIS(40), UI_ACCENT, 0.45f);

    for (int i = 0; i < SAFEHOUSE_PAGES; i++) {
        PageTabRect(i, &x0, &y0, &x1, &y1);
        int active = (shopPage == ShopPageAtTab(i));
        int hover  = (safehouseHoverTab == i);

        BlendRectWin(x0, y0, x1, y1,
                     active ? UI_ACCENT : (hover ? 0x00303838 : UI_BG_PANEL),
                     active ? 0.95f : 0.85f);
        FrameRectWin(x0, y0, x1, y1,
                     active ? UI_ACCENT : (hover ? PAL_CYAN : PAL_DIM),
                     0.9f, active ? 2 : 1);

        /* 选中时左侧加一根粗竖条，扫一眼就知道在哪一页 */
        if (active) {
            BlendRectWin(x0 - UIS(6), y0, x0 - UIS(1), y1, UI_ACCENT, 0.95f);
        }

        const char* t = Tr(names[i], namesZh[i]);
        DrawTextWin(x0 + UIS(12), y0 + (y1 - y0 - th) / 2, t,
                    active ? 0x00100C08u : 0x0090A0A0u);
    }
}





/* 弹窗面板：不透明，盖住下面的列表文字 */
void DrawPopupPanel(void) {
    MenuGeo p = GetPopupGeo();
    const SettingItem* it = &settings[popupItem];

    BlendRectWin(p.panel.left, p.panel.top, p.panel.right, p.panel.bottom, 0x0004080c, 1.0f);
    FrameRectWin(p.panel.left, p.panel.top, p.panel.right, p.panel.bottom, UI_EDGE, 0.95f, UIS(2));

    if (it->kind == SET_SLIDER) {
        RECT t = SliderTrackRect(p);
        int bend = UIS(3);
        BlendRectWin(t.left - bend, t.top - bend, t.right + bend, t.bottom + bend, 0x00404040, 0.9f);
        BlendRectWin(t.left, t.top, t.right, t.bottom, 0x00141414, 0.95f);
        int kx = SliderValueToX(p, *it->fvalue, it->fmin, it->fmax);
        BlendRectWin(t.left, t.top, kx, t.bottom, 0x0030a060, 0.95f);
        int k = UIS(8);
        BlendRectWin(kx - k, t.top - k, kx + k, t.bottom + k, 0x00d8e8d8, 0.95f);
        FrameRectWin(kx - k, t.top - k, kx + k, t.bottom + k, 0x00ffffff, 0.55f, 1);
    } else if (popupSelection >= 0 && popupSelection < p.count) {
        RECT r = RowRect(p, popupSelection);
        BlendRectWin(r.left, r.top, r.right, r.bottom, 0x00306030, 0.65f);
    }
}

void DrawTextInRow(const char* text, RECT row, unsigned int color) {
    int tw = TextWidthWin(text), th = TextHeightWin();
    int x = row.left + ((row.right - row.left) - tw) / 2;
    int y = row.top + ((row.bottom - row.top) - th) / 2;
    DrawTextWin(x, y, text, color);
}

/* 左对齐的一行：说明文字用。
   居中对齐遇到长句子会超出面板边缘（CONTROLS 页的说明就踩过这个坑）。 */

/* ============================================================
 * 技能树 v2 绘制
 * ============================================================ */

/* 节点四档状态色（用户定的顺序）：无色透明 -> 白 -> 金 -> keystone 彩色流转 */

/* 这一步能不能买（含"花 3 核心换 keystone"） */
static int SkillNodeBuyable(int id) {
    if (SkillCanBuy(id)) return 1;
    return SkillSwitchable(id) && WalletCanPayBank(SKILL_SWITCH_COST);
}



/* 虚线框。th 必须跟着文字高度缩放：
   绘制用的是**窗口坐标**，但真正落笔在 renderW x renderH 的缓冲里，
   1440p 全屏时 1 个窗口像素只有 0.27 个缓冲像素 —— 1px 的线会
   直接消失（第一版在全屏下虚线框整个不见了）。 */


/* 同列上下节点之间的连线：走线颜色 = 该分支主色，越往下点亮越实 */
/* ===== v6 **技能树相机（和局内改造树同一套模型）** =====
   局内改造树用的是 treeVX / treeVY / treeVK 三个量，
   世界坐标经  screen = world * VK + VX  映射到屏幕。
   这里给技能树配一份完全同构的，滚轮缩放 + 拖拽平移 + HOME 复位。

   为什么铺成世界坐标而不是直接排屏幕矩形：
   原来的 SkillTreeCompute 按面板尺寸算死矩形，窗口一变就得重算，
   而且没法缩放。改成固定世界网格后，缩放/平移只是改 VK/VX/VY。 */
#define SK_WX      210.0f    /* 世界网格：列间距 */
#define SK_WY      120.0f    /* 世界网格：行间距 */
#define SK_K_MIN   0.18f
#define SK_K_MAX   2.60f

static float skVX = 0.0f, skVY = 0.0f, skVK = 1.0f;
/* ==================================================================
 * 2026-11-08（用户实机反馈）：**方框尺寸不随缩放变化**
 * ------------------------------------------------------------------
 * 原来 nodeW/nodeH 乘了 skVK，滚轮一放大方框就跟着胀，
 * 立刻和邻居压在一起（"连成一片"）。
 * 现在尺寸固定在这个基准上，缩放只改坐标间距。
 * ================================================================ */
#define SK_NODE_K 0.78f
static int   skDragging = 0;
static float skDragMouseX = 0.0f, skDragMouseY = 0.0f;
static float skDragVX = 0.0f, skDragVY = 0.0f;

static void SkillNodeSize(int* width, int* height) {
    float scale = skVK < SK_NODE_K ? skVK : SK_NODE_K;
    int w = (int)(SK_WX * .55f * scale), h = (int)(SK_WY * .62f * scale);
    if (w < 26) w = 26;
    if (h < 18) h = 18;
    *width=w; *height=h;
}
/* ==================================================================
 * v6 **辐射布局**：四个方向各一条支线，联动节点在两支之间
 * ==================================================================
 * 原来：4 列 × 4 行的网格，视觉上是"表格"，不是"树"。
 *
 * 现在：坐标来自 skills.c 的 SkillNodeX/Y（归一化 0..1）：
 *         中心 (0.5, 0.5)
 *         静 = 向上   筹 = 向右   隐 = 向下   途 = 向左
 *         keystone 在各自支线更远处
 *         联动节点在两支之间的对角
 *
 * 这样连线画出来就是一张**放射状蜘蛛网**，
 * 而不是四条互不相干的竖线。
 * ================================================================== */
static void SkillWorldPos(int id, float* wx, float* wy) {

    /* 把 0..1 映射到世界坐标的宽高 */
    /* 世界尺度 ×6：
       归一化间距 0.10 → 世界 0.60·SK_WX，而节点宽 0.55·SK_WX，
       间距 > 宽度，节点才不会叠在一起。
       （×4 时间距只有 0.40·SK_WX < 0.78·SK_WX —— 实测截图里
         节点全糊成一片大矩形，这次修掉。） */
    *wx = SkillNodeX(id) * SK_WX * 6.0f;
    *wy = SkillNodeY(id) * SK_WY * 6.0f;
}
void SkillCenterOn(int id);   /* v6：摆到视野中心（定义在下面） */


int  SkillViewZoom(void)  { return (int)(skVK * 100.0f); }
/* v6 **把某个节点摆到视野正中** —— 用户反馈：
   「HUB 选技能时用 WASD 移动会丢失目标，因为地图太大」。
   这是「回到当前技能选项」的基础函数。 */
void SkillCenterOn(int id) {
    float wx, wy;
    SkillWorldPos(id, &wx, &wy);
    int nodeW, nodeH; SkillNodeSize(&nodeW,&nodeH);
    skVX = (float)winW * 0.50f - (wx * skVK + (float)nodeW * 0.5f);
    skVY = (float)winH * 0.42f - (wy * skVK + (float)nodeH * 0.5f);
    uiDirty = 1;
}

void SkillResetView(void) {
    float minX=1e9f,minY=1e9f,maxX=-1e9f,maxY=-1e9f;
    for (int id=0; id<SKILL_COUNT; id++) {
        if(!SkillIsPlayerFacing(id))continue;
        float x,y; SkillWorldPos(id,&x,&y);
        if(x<minX)minX=x;
        if(x>maxX)maxX=x;
        if(y<minY)minY=y;
        if(y>maxY)maxY=y;
    }
    SkillTreeLayout layout; SkillTreeCompute(&layout);
    int th=TextHeightWin();
    float top=layout.curY+th*2.0f, bottom=layout.panel.bottom-th*5.0f;
    skVK=(layout.panel.right-layout.panel.left-100.0f)/(maxX-minX+120.0f);
    float ky=(bottom-top)/(maxY-minY+100.0f);
    if(ky<skVK)skVK=ky;
    if(skVK<SK_K_MIN)skVK=SK_K_MIN;
    if(skVK>SK_NODE_K)skVK=SK_NODE_K;
    skVX=winW*.5f-(minX+maxX)*.5f*skVK;
    skVY=(top+bottom)*.5f-(minY+maxY)*.5f*skVK;
    uiDirty=1;
}

void SkillPanBy(float dx, float dy) { skVX += dx; skVY += dy; uiDirty = 1; }
void SkillZoomAt(float factor, int mx, int my) {
    float k0 = skVK, k2 = k0 * factor;
    if (k2 < SK_K_MIN) k2 = SK_K_MIN;
    if (k2 > SK_K_MAX) k2 = SK_K_MAX;
    if (k2 == k0) { uiDirty = 1; return; }
    skVX = (float)mx - ((float)mx - skVX) * (k2 / k0);
    skVY = (float)my - ((float)my - skVY) * (k2 / k0);
    skVK = k2;
    uiDirty = 1;
}
/* ==================================================================
 * 2026-11-06（用户实测）：**技能树只用鼠标调不了缩放**
 * ------------------------------------------------------------------
 * 桌面端靠滚轮（WM_MOUSEWHEEL），网页端没有滚轮消息 ——
 * 于是"用鼠标根本调不了倍率"。
 *
 * 这里加一对固定的 + / - 按钮：
 *   · 桌面与网页都能点（走同一个 CanityMouseClick）
 *   · 也顺手给纯键盘用户一个可见提示
 * ================================================================== */
void HubZoomButtonRect(int which /* 0=+ 1=- */, int* x0, int* y0, int* x1, int* y1) {
    int sz = UIS(44);
    int gap = UIS(10);
    /* 2026-11-06（用户实测：**左下角 UI 重叠**）
       左下角是节点详情文字（名称/描述/购买提示），按钮压上去了。
       现在放到**左侧竖直居中**：那一带是空的，而且拇指/鼠标都够得到。 */
    int bx = UIS(16);
    int by = winH / 2 - sz - gap / 2;
    if (which == 0) {              /* + 在上面 */
        by -= (sz + gap);
    }
    if (x0) *x0 = bx;
    if (y0) *y0 = by;
    if (x1) *x1 = bx + sz;
    if (y1) *y1 = by + sz;
}

/* 技能树「返回」按钮（左上角，写死位置，和 + / - 同一套风格） */
void HubBackButtonRect(int* x0, int* y0, int* x1, int* y1) {
    int w = UIS(104), h = UIS(40);
    int bx = UIS(16);
    int by = UIS(16);
    if (x0) *x0 = bx;
    if (y0) *y0 = by;
    if (x1) *x1 = bx + w;
    if (y1) *y1 = by + h;
}

/* 商店/改造树的返回按钮（左上角，和技能树同位置） */
void ShopBackButtonRect(int* x0, int* y0, int* x1, int* y1) {
    int w = UIS(104), h = UIS(40);
    if (x0) *x0 = UIS(16);
    if (y0) *y0 = UIS(16);
    if (x1) *x1 = UIS(16) + w;
    if (y1) *y1 = UIS(16) + h;
}

int ShopBackButtonAt(int mx, int my) {
    int x0, y0, x1, y1;
    ShopBackButtonRect(&x0, &y0, &x1, &y1);
    return (mx >= x0 && mx <= x1 && my >= y0 && my <= y1) ? 1 : 0;
}

int HubBackButtonAt(int mx, int my) {
    int x0, y0, x1, y1;
    HubBackButtonRect(&x0, &y0, &x1, &y1);
    return (mx >= x0 && mx <= x1 && my >= y0 && my <= y1) ? 1 : 0;
}

void DrawHubZoomButtons(void) {
    for (int which = 0; which < 2; which++) {
        int x0, y0, x1, y1;
        HubZoomButtonRect(which, &x0, &y0, &x1, &y1);
        BlendRectWin(x0, y0, x1, y1, 0x00101418u, 0.88f);
        FrameRectWin(x0, y0, x1, y1, 0x00607080u, 0.95f, UIS(2));
        DrawTextCenterWin((x0 + x1) / 2, (y0 + y1) / 2 - TextHeightWin() / 2,
                          which == 0 ? "+" : "-", UI_ACCENT);
    }
}

/* 命中返回：1 = 放大键，-1 = 缩小键，0 = 没点到 */
int HubZoomButtonAt(int mx, int my) {
    for (int which = 0; which < 2; which++) {
        int x0, y0, x1, y1;
        HubZoomButtonRect(which, &x0, &y0, &x1, &y1);
        if (mx >= x0 && mx <= x1 && my >= y0 && my <= y1)
            return which == 0 ? 1 : -1;
    }
    return 0;
}

void SkillDragStart(int mx, int my) {
    skDragging = 1;
    skDragMouseX = (float)mx; skDragMouseY = (float)my;
    skDragVX = skVX; skDragVY = skVY;
}
void SkillDragMove(int mx, int my) {
    if (!skDragging) return;
    skVX = skDragVX + ((float)mx - skDragMouseX);
    skVY = skDragVY + ((float)my - skDragMouseY);
    uiDirty = 1;
}
void SkillDragEnd(void) { skDragging = 0; }

/* ==================================================================
 * 修：**技能树里鼠标点节点选不中**
 * ==================================================================
 * WM_LBUTTONDOWN 在 HUB 里无条件 `SkillDragStart(...); return 0;`，
 * 于是永远走不到 ClickMenu 里的 SkillNodeHitTest ——
 * 那套命中测试和 ActivateHubRow 是**死代码**，玩家只能靠方向键选。
 *
 * 现在：按下记下当时的相机位置，松开时如果相机几乎没动，
 * 就当成「单击」→ 命中测试 → 选中节点。
 * ================================================================== */
int SkillDragWasClick(void) {
    float dx = skVX - skDragVX;
    float dy = skVY - skDragVY;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return skDragging && dx<4.0f && dy<4.0f;
}
int  SkillIsDragging(void) { return skDragging; }

/* 世界 → 屏幕 */
static int SkSX(float wx) { return (int)(wx * skVK + skVX); }
static int SkSY(float wy) { return (int)(wy * skVK + skVY); }



/* ===== v6 **技能树节点卡片（重写）** =====
   旧版：一个方块 + 居中的名字 + 底部等级点。
        问题是「卡片里除了名字什么都没有」—— 12 个节点长得一样，
        玩家必须逐个选中看底部详情才知道它是干什么的。

   新版卡片分三层：
     ① 名字（居中，大）
     ② 效果摘要（小一号，压暗，自动截断）—— 不选中也能看懂
     ③ 底部：等级点 + 状态徽标（OWNED / BUY / LOCKED）
   配色仍按分支走，但「可买」用呼吸金边、「已拥有」用实心绿，
   一眼能扫出「哪些我已经有、下一个该点哪」。 */



/* ===== v3.4 **BANK 页**（用户要求）=====
   主菜单新加的一栏，显示银行存款 + 携带档位 + 历史最高。
   这是整个经济链的「账户总览」：你现在有多少钱、打算带多少进去。 */


/* ===== v6 **出口二选一面板** =====
   「带着这些钱走人，还是再贪一层？」—— 这是整个经济链的情绪核心。
   面板上必须写清楚：撤离能拿多少、深入大概能多赚多少。 */
static void DrawExitChoice(void) {


    if (!exitChoiceOpen) return;

    /* ===== v2.4.45 **出口二选一面板重做** =====
       用户：「这个明显太粗糙了」。旧版问题（对照 /design 论坛的 UI 评审清单）：

         ① 只有一圈细边框 + 纯文字浮在场景上 → 没有"面板"的存在感
         ② 高度按 TH*8 硬算，但只有 5 行内容 → 大片空白
         ③ 每行间距三种（6 / 10 / 14），最右和最左对不齐 → 视觉噪音
         ④ 六行六种颜色（金/白/绿/橙/红/灰）→ 没有主次，全是重点
         ⑤ 「带的钱越多怪物听得越清」是**全局提示**，塞进选择面板里
            等于在决定面前多一句废话 —— 移出

       新规则（对齐调研结论）：
         · **像素风双层边框**：外暗底 + 内金框 + 四角短标
         · **高度按内容算**，不再留空
         · **一个行距常量**，所有行等距
         · **最多两个强调色**：绿=安全（撤离）/ 橙=风险（深入）；其余中性
         · **左标签 + 右数值**两列对齐，方便横向比较
         · 只保留一句脚注：死在里面保留 35%
    */
    int TH  = TextHeightWin();
    int GAP = UIS(8);                 /* **唯一行距** */
    int PAD = UIS(20);                /* **唯一内边距** */
    int ROW = TH + GAP;

    /* ===== v2.4.46 **背景不显示的根因** =====
       原来用 exitChoiceT 做淡入：`fade = exitChoiceT / 0.25f`，
       而 exitChoiceT 只在 UpdateSmokes() 里累加 ——
       面板打开时 Update() 提前 return，UpdateSmokes 根本不会被调用，
       于是 exitChoiceT 永远是 0 → **fade = 0 → 所有矩形 alpha 都是 0**，
       而文字没走 fade，所以画面上只剩裸文字、没有面板。
       （用户：「连个背景都没有，裸着的」。）

       修：面板一律不透明。淡入这点动画不值得拿"面板可能完全消失"去换。*/
    float fade = 1.0f;

    int w = UIS(540);
    if (w > winW - UIS(60)) w = winW - UIS(60);

    /* 内容行数：标题 1 + 空 1 + 选项 2 + 空 1 + 脚注 1 = 6 行 */
    int h = PAD * 2 + ROW * 6;
    int x = (winW - w) / 2;
    int y = (winH - h) / 2;

    /* ---- 全屏压暗 + 双层边框（像素风）---- */
    BlendRectWin(0, 0, winW, winH, 0x00000000u, 0.72f * fade);
    BlendRectWin(x - UIS(3), y - UIS(3), x + w + UIS(3), y + h + UIS(3),
                 0x00000000u, 0.55f * fade);
    BlendRectWin(x, y, x + w, y + h, 0x0012181Bu, 0.98f * fade);
    FrameRectWin(x, y, x + w, y + h, UI_ACCENT, 0.95f * fade, UIS(2));

    /* 四角短标：像素面板的标志性细节 */
    {
        int arm = UIS(12), t = UIS(2);
        unsigned int cc = UI_ACCENT;
        BlendRectWin(x, y, x + arm, y + t, cc, 0.9f * fade);
        BlendRectWin(x, y, x + t, y + arm, cc, 0.9f * fade);
        BlendRectWin(x + w - arm, y, x + w, y + t, cc, 0.9f * fade);
        BlendRectWin(x + w - t, y, x + w, y + arm, cc, 0.9f * fade);
        BlendRectWin(x, y + h - t, x + arm, y + h, cc, 0.9f * fade);
        BlendRectWin(x, y + h - arm, x + t, y + h, cc, 0.9f * fade);
        BlendRectWin(x + w - arm, y + h - t, x + w, y + h, cc, 0.9f * fade);
        BlendRectWin(x + w - t, y + h - arm, x + w, y + h, cc, 0.9f * fade);
    }

    int leftX  = x + PAD;
    int rightX = x + w - PAD;
    int curY   = y + PAD;

    /* ---- 标题 ---- */
    DrawTextWin(leftX, curY, Tr("THE EXIT IS OPEN", "出口已开"), UI_ACCENT);
    curY += ROW * 2;          /* 标题后空一行 */

    /* ---- 选项 1：撤离 ---- */
    {

        int quit=BankPreviewGain(1);

        char kb[16], lb[64], vb[48];
        snprintf(kb, sizeof(kb), "[1]");
        snprintf(lb, sizeof(lb), "%s", Tr("LEAVE NOW", "现在撤离"));
        snprintf(vb, sizeof(vb), "+%d GOLD", quit);

        DrawTextWin(leftX, curY, kb, UI_ACCENT);
        DrawTextWin(leftX + TextWidthWin("[1] ") + UIS(6), curY, lb, 0x00E0E0E0u);
        DrawTextWin(rightX - TextWidthWin(vb), curY, vb, 0x0060FF90u);
        curY += ROW;
    }

    /* ---- 选项 2：深入 ---- */
    {

        char kb[16], lb[64], vb[64];
        snprintf(kb, sizeof(kb), "[2]");
        snprintf(lb, sizeof(lb), "%s", Tr("GO DEEPER", "继续深入"));
        snprintf(vb, sizeof(vb), Tr("LV %d   x%.2f", "第 %d 层   x%.2f"),
                 currentLevel + 1, ExtractDepthMul(currentLevel + 1));

        DrawTextWin(leftX, curY, kb, UI_ACCENT);
        DrawTextWin(leftX + TextWidthWin("[1] ") + UIS(6), curY, lb, 0x00E0E0E0u);
        DrawTextWin(rightX - TextWidthWin(vb), curY, vb, 0x00FFA040u);
        curY += ROW * 2;      /* 选项后空一行 */

    }

    /* ---- 脚注：唯一保留的一句 ---- */
    DrawTextWin(leftX, curY, Tr("DIE INSIDE - YOU KEEP 35%",
                                "死在里面 - 只保留 35%"),
                0x00908880u);
}

/* ===== v6 **技能树（列表式，整体重写）** =====
   和局内改造树同一套视觉：一行一个节点，名字 + 等级 + 价格。
   加技能只要往 kSkillList 里加个 id，列表自动变长，不用改任何布局。
   底部固定详情条：名字 / 效果 / 状态。 */
/* ===== v6 **技能树（树状图，改回）** =====
   用户：「本意是像局内改造树那样规整的树状图，你给我改成表格了，有点违背」。
   所以改回：4 列 × 3 行节点图 + 连线，卡片用 SkillDrawNode，下面接详情条。
   加技能只要往 kSkillList / skillDefs 加，网格自动重排。 */
/* ===== v6 **技能树（树状图 + 缩放拖拽）** =====
   布局走固定的「世界网格」(SK_WX × SK_WY)，屏幕映射全靠相机
   screen = world * VK + VX。所以缩放/平移只是改三个浮点数，
   加技能也只需要往 kSkillList 里加一个 id，网格自动排。 */
typedef struct {
    SkillTreeLayout layout;
    int TH, sel, innerL, innerR, gTop, gBot;
} SkillTreeDrawContext;

static void DrawSkillTreeCanvas(const SkillTreeDrawContext* context) {
    int TH = context->TH;
    int sel = context->sel;
    int gTop = context->gTop;
    int gBot = context->gBot;
    /* ---------- 图区（可缩放/平移） ---------- */
    {
        int nodeW, nodeH; SkillNodeSize(&nodeW,&nodeH);
        if (nodeW < 24) nodeW = 24;
        if (nodeH < 18) nodeH = 18;
        /* ============================================================
         * 修：**上一条注释说了要让出一个节点高，但代码里没写**
         * ============================================================
         * 实测 ui_layout.txt（800x600）：
         *   "BRE.."(157,500..237,524) × "LIGHT STEP [SILENCE]"(31,492..383,516)
         *   交叠 67% —— 节点直接压在详情条第一行上。
         * 现在真的把图区下界抬到详情条之上一个节点高。
         * ============================================================ */
        gBot -= nodeH + TH / 2;
        if (gBot < gTop + nodeH) gBot = gTop + nodeH;



        /* Only actual prerequisite connections are drawn. */
        for (int id=0; id<SKILL_COUNT; id++) {
        if(!SkillIsPlayerFacing(id))continue;
            RECT target=SkillNodeRect(id);
            for (int e=0; e<SkillEdgeCount(id); e++) {
                int pre=SkillEdgeNeighbor(id,e); RECT source=SkillNodeRect(pre);
                int x0=(source.left+source.right)/2, y0=(source.top+source.bottom)/2;
                int x1=(target.left+target.right)/2, y1=(target.top+target.bottom)/2;
                int dx=x1-x0,dy=y1-y0,steps=abs(dx)>abs(dy)?abs(dx):abs(dy);
                if(steps<1 || steps>12000) continue;
                int lit=SkillOwned(pre);
                int route=(id==sel || pre==sel);
                for(int step=0;step<=steps;step+=3) {
                    if(!lit && (step/9)%2)continue;
                    int x=x0+(int)((float)dx*step/steps),y=y0+(int)((float)dy*step/steps);
                    if(x>=context->innerL && x+2<context->innerR && y>=gTop && y+2<gBot)
                        BlendRectWin(x,y,x+2,y+2,route?UI_INFO:(lit?UI_ACCENT:0x005C748Bu),.95f);
                }
            }
        }
        const int tips[3]={SK_GHOST_FORM,SK_RAIDER_POWER,SK_COUNTER_KILL};
        for(int group=0;group<3;group++) {
            RECT r=SkillNodeRect(tips[group]); const char* name=SkillArchetypeName(group,0);
            int x=(r.left+r.right)/2-DrawTextSmallWidth(name)/2, y=r.top-16;
            if(x>=context->innerL && x+DrawTextSmallWidth(name)<context->innerR && y>=gTop && y+12<gBot)
                DrawTextSmallWin(x,y,name,UI_ACCENT);
        }

        /* 节点 */
        for (int k = 0; k < SKILL_LIST_COUNT; k++) {
            int id = kSkillList[k];
            const SkillDef* d = &skillDefs[id];
            int owned = (skillRank[id] > 0);
            int canBuy = SkillNodeBuyable(id);
            int isSel = (id == sel);

            float wx, wy;
            SkillWorldPos(id, &wx, &wy);
            int x0 = SkSX(wx), y0 = SkSY(wy);
            /* 跨支行横跨四列 */
            /* v6：去掉「跨支行横跨四列」的特例 ——
               第 ⑨ 轮把 SKILL_CROSS_ID 改成了 25（= 中心 ORIGIN），
               于是**中心节点被画成一条横跨四列的大绿条**，盖住整个中间。
               现在所有节点一律等宽。 */
            int nw2 = nodeW;
            int x1 = x0 + nw2, y1 = y0 + nodeH;

            /* 图区裁剪：超出上下界的整个跳过（不裁剪像素，省开销） */
            if (y0 < gTop || y1 > gBot || x0 < context->innerL || x1 > context->innerR) continue;

            /* ============================================================
             * v6 第 ㉕ 轮：**这里原来绕过调色盘自己写死了一套颜色**
             * ============================================================
             * 上面那个 SkillNodeColors() 改了半天，这一段根本没用它 ——
             * 未拥有的节点 fill=0x181A1E / border=0x404040，
             * 画在 0x0a1016 的面板上等于**隐形**。
             * 实测截图：26 个节点只看得见名字，看不见框。
             * ============================================================ */
            unsigned int fill = owned ? 0x00384858u : 0x00222834u;
            unsigned int bd;
            if (owned)        bd = PAL_CYAN;
            else if (canBuy)  bd = (((int)(g_timeSec * 2.0f) & 1)) ? UI_ACCENT : 0x008A7830u;
            else              bd = 0x008090A0u;      /* 明显可见的灰蓝，不再是 0x404040 */

            if(id>=45){static const unsigned shared[3]={0x006EB9D9u,0x0094B994u,0x00B69ADBu};bd=shared[(id-45)/2];}
            BlendRectWin(x0, y0, x1, y1, fill, 0.92f);
            FrameRectWin(x0, y0, x1, y1, bd, 0.95f, isSel ? 3 : 2);

            /* 名字（缩得太小时只画名字） */
            {
                char nm[40];
                /* v6：节点里画短名，详情条才用全长名 */
                { 
                  snprintf(nm,sizeof(nm),"%s",SkillShortName(id)); }
                /* ============================================================
                 * 修：**节点名被截成 3 个字**
                 * ============================================================
                 * 节点框约 80px 宽，而文字按窗口缩放（scale≈2）算宽度，
                 * 一个字符 16px → 只能放 3 个，于是 "QUIET LID" 变 "QUI.."。
                 * 文件名是玩家唯一能认出这是哪个节点的线索，截成这样没有用。
                 *
                 * 改用 1x 小字（每个字符 8px），同样宽度能放 8~10 个字符。
                 * ============================================================ */
                unsigned int tc = owned ? 0x0090FFB0u : (canBuy ? UI_ACCENT : 0x00787878u);
                {


                    int sw = TextSmallLiteralWidth(nm);
                    UiTrimSmallText(nm,nw2-4);
                    sw = TextSmallLiteralWidth(nm);
                    DrawTextSmallLiteralWin((x0 + x1) / 2 - sw / 2,
                                     y0 + nodeH / 2 - 6, nm, tc);
                }
            }

            /* 等级点（缩得太小就不画，避免糊成一团） */
            if (skVK > 0.55f) {
                int pw = (int)(TH / 3 * skVK); if (pw < 4) pw = 4;
                int n = d->maxRank > 3 ? 3 : d->maxRank;
                int tw = n * pw + (n - 1) * (pw / 2);
                int px = (x0 + x1) / 2 - tw / 2;
                int py = y1 - pw - TH / 3;
                unsigned int dc = owned ? 0x0060FF90u : kBranchColor[d->branch];
                for (int m = 0; m < n; m++) {
                    int sx = px + m * (pw + pw / 2);
                    if (m < skillRank[id]) BlendRectWin(sx, py, sx + pw, py + pw, dc, 0.95f);
                    else                   FrameRectWin(sx, py, sx + pw, py + pw, dc, 0.55f, 1);
                }
            }

            /* 选中框 */
            if (isSel) {
                FrameRectWin(x0 - 3, y0 - 3, x1 + 3, y1 + 3, 0x00000000u, 0.85f, 2);
                FrameRectWin(x0 - 5, y0 - 5, x1 + 5, y1 + 5, UI_ACCENT, 0.95f, 2);
            }
        }
    }
}

static void DrawSkillTreeDetails(const SkillTreeDrawContext* context) {
    SkillTreeLayout L = context->layout;
    int TH = context->TH;
    int sel = context->sel;
    int innerL = context->innerL;
    int innerR = context->innerR;
    /* ---------- 底部固定：缩放值 + 详情 + 提示 ---------- */
    {
        const SkillDef* d = &skillDefs[sel];
        int dy = L.panel.bottom - TH * 4;
        char b[128];

        sprintf(b, "%.2fx", skVK);
        DrawTextWin(innerR - TextWidthWin(b), dy - TH, b, 0x00808080u);

        sprintf(b, "%s   [%s]", SkillDisplayName(sel),
                SkillArchetypeName(SkillArchetype(sel), g_lang == LANG_ZH));
        ClampRowText(b, innerR - innerL);
        DrawTextWin(innerL, dy, b, 0x00E8E8E8u);

        /* ================================================================
         * v6 第 48 轮：**技能树从来没显示过价格**
         * ================================================================
         * 用户实机反馈：「技能树那里面升级所需要的钱，你没标明出来」。
         *
         * 顺手挖出第二个 bug：下面那个「够不够钱」用的是
         * **carryGold（局内金币）**，而技能树花的是 **bankGold（银行）**。
         * 主菜单里 carryGold 恒为 0 —— 所以永远显示「金币不够」，
         * 哪怕银行里躺着十万。
         * ================================================================ */
        int costNow = SkillCost(sel);
        if(costNow>=0)costNow+=d->coreCost*GOLD_PER_CORE;
        if(SkillSwitchable(sel))costNow=SKILL_SWITCH_COST;

        {   /* 第二行：描述左，价格右 */
            static char cb[32];
            int cw = 0;
            if (costNow >= 0) {
                sprintf(cb, "%s %d", Tr("COST", "价格"), costNow);
                cw = TextWidthWin(cb) + UIS(16);
                DrawTextWin(innerR - TextWidthWin(cb), dy + TH, cb, UI_GOLD);
            }
            snprintf(b,sizeof(b),"%s",SkillDisplayDescription(sel));
            ClampRowText(b, innerR - innerL - cw);
            DrawTextWin(innerL, dy + TH, b, 0x008A8A8Au);
        }

        {
            const char* st; unsigned int sc;
            if (SkillSwitchable(sel)) { st=WalletCanPayBank(SKILL_SWITCH_COST)?Tr("ENTER TO EQUIP CROWN", "回车切换流派王冠"):Tr("BANK GOLD REQUIRED TO SWITCH", "切换王冠需要银行金币"); sc=0x00E0BD72u; }
            else if (skillRank[sel] >= d->maxRank) { st = Tr("ALREADY MAXED", "已满级"); sc = 0x0060FF90u; }
            else if (!(d->prereq < 0 || skillRank[d->prereq] > 0)) { static char required[160];snprintf(required,sizeof(required),Tr("FIRST UNLOCK: %s","先解锁：%s"),SkillDisplayName(d->prereq));st=required; sc = 0x00FF8080u; }
            else if (!SkillKeystoneUnlocked(sel)) { st = Tr("INVEST 5 POINTS IN THIS ARCHETYPE", "本流派需先投入五点"); sc = 0x00FF8080u; }
            else if (!WalletCanPayBank(costNow)) { st = Tr("NOT ENOUGH GOLD", "金币不够"); sc = 0x00FF8080u; }
            else if (!SkillCanBuy(sel)) { st = Tr("REQUIRES ALL CONNECTED NODES", "需要点亮全部连线前置"); sc = 0x00FF8080u; }
            else { st = Tr("ENTER / SPACE / CLICK TO BUY", "回车、空格或左键购买"); sc = 0x0060FF90u; }
            char status[160];snprintf(status,sizeof(status),"%s",st);
            ClampRowText(status,innerR-innerL-UIS(150));
            DrawTextWin(innerL,dy+TH*2,status,sc);
            RECT button;HubPurchaseRect(&button);
            int enabled=SkillCanBuy(sel) || (SkillSwitchable(sel)&&WalletCanPayBank(SKILL_SWITCH_COST));
            UiRow(button.left,button.top,button.right,button.bottom,enabled?UI_ROW_SEL:UI_ROW_DISABLED,1.0f);
            DrawTextCenterWin((button.left+button.right)/2,(button.top+button.bottom-TH)/2,
                              SkillSwitchable(sel)?Tr("EQUIP","装备"):Tr("BUY","购买"),enabled?UI_ACCENT:UI_INK_DIM);
        }
    }
    /* 2026-11-08：标题放到最上层 —— chrome 永远在内容之上 */
    DrawTitleWin(Tr("THREE PATHS", "三途技能网"), L.titleY, UI_ACCENT);

    DrawTextCenterWin((L.panel.left + L.panel.right) / 2, L.panel.bottom - TH,
                      /* ================================================
                       * 2026-11-08（实机反馈）：**这行越界 72px**
                       * ------------------------------------------------
                       * 实测 dump：`R x2 RESET / WHEEL ZOOM ...` 是 x=-72..872，
                       * 画布只有 800 —— 左右各被裁掉一截。
                       * 这里按可用宽度自动降级到短版本（信息没删，只是缩写）。
                       * ================================================ */
                      (TextWidthWin(Tr("TAB BRANCH / P PREREQUISITE / HOME RESET / WHEEL ZOOM / ENTER BUY",
                                       "TAB切流派  P看前置  HOME复位  滚轮缩放  回车购买"))
                       > (L.panel.right - L.panel.left) - UIS(16))
                      ? Tr("TAB BRANCH / P PREV / HOME RESET / ENTER BUY",
                           "TAB流派  P前置  HOME复位  回车购买")
                      : Tr("TAB BRANCH / P PREREQUISITE / HOME RESET / WHEEL ZOOM / ENTER BUY",
                           "TAB切流派  P看前置  HOME复位  滚轮缩放  回车购买"),
                      0x00707070u);

    /* 2026-11-06：左侧 + / - 缩放按钮 */
    DrawHubZoomButtons();

    /* 2026-11-06（用户实测：**点进技能树就退不出去**）
       底层有 ESC，但：① 全屏时浏览器会吃掉 ESC ② 界面上找不到它。
       所以在左上角画一个明确的「返回」按钮，鼠标/触屏都能点。 */
    {
        int bx0, by0, bx1, by1;
        HubBackButtonRect(&bx0, &by0, &bx1, &by1);
        BlendRectWin(bx0, by0, bx1, by1, 0x00101418u, 0.90f);
        FrameRectWin(bx0, by0, bx1, by1, 0x00607080u, 0.95f, UIS(2));
        DrawTextCenterWin((bx0 + bx1) / 2, (by0 + by1) / 2 - TextHeightWin() / 2,
                          Tr("BACK", "返回"), UI_ACCENT);
    }
}

void DrawSkillTree(void) {
    /* v6 **自检接线** —— SkillsSelfTest 原来全工程 0 调用点，
       护栏写好了却没人用。这里在首次进入技能树时跑一次，
       失败就把原因写进 pickupMsg（玩家和开发都能看到）。 */
    {
        static int skTested = 0;
        if (!skTested) {
            skTested = 1;
            /* v6 **自检失败不再弹窗给玩家** ——
               这是给开发看的护栏，玩家看到 "SKILL DATA ERROR: 35"
               只会以为游戏坏了。改成静默（需要排查时加 DSH_SKILL_DEBUG）。 */
            /* v7 第 ㊿ 轮：**结果不再丢掉**
               原来 `(void)r;` 把返回值扔了 —— 护栏装了没插电。
               现在失败时写进 output debug 字符串，开发能看到，
               玩家不受打扰。 */
            {
                int r = SkillsSelfTest();
                if (r) {
                    char m[96];
                    snprintf(m, sizeof(m),
                             "[CANITY] SKILL DATA SELF-TEST FAILED: code=%d "
                             "(SKILL_TOTAL_COST vs real table)", r);
                    PlatLog(m);
                }
            }
        }
    }
    SkillTreeLayout L;
    SkillTreeCompute(&L);
    int TH = TextHeightWin();
    int sel = hubSelection;
    if (SkillListIndex(sel) < 0) sel = kSkillList[0];
    hubSelection = sel;

    /* 首次进入把视图居中到第 0 行 */
    /* ===== v6 **相机自愈守卫** =====
       用户截图：进 HUB 一个节点都看不到（整页空白），说明 skVX/skVY 跑飞了
       （NaN / 巨大值），所有节点被 gTop..gBot 裁掉。
       这里每帧检查一次，只要状态非法就强制回到选中节点。 */
    {
        int bad = 0;
        if (!(skVK >= SK_K_MIN && skVK <= SK_K_MAX)) bad = 1;
        if (!(skVX > -20000.0f && skVX < 20000.0f)) bad = 1;
        if (!(skVY > -20000.0f && skVY < 20000.0f)) bad = 1;
        if (bad) { skVK = 0.78f; SkillCenterOn(hubSelection); }
    }
    /* 首次进入：居中到第一行 */
    {
        static int skCentred = 0;
        if (!skCentred) {
            skCentred = 1;
            SkillResetView();
        }
    }


    int innerL = L.panel.left + L.padX;
    int innerR = L.panel.right - L.padX;
    int gTop = L.curY + TH + TH / 2;          /* 图区上界 */
    int gBot = L.panel.bottom - TH * 4;        /* 图区下界 = 详情条顶边 */
    /* v6 **图区下界要留够详情条的空间** ——
       详情条三行从 panel.bottom - TH*4 开始，而节点高 0.62*SK_WY*skVK，
       原来 gBot 正好等于详情首行 y，第 3 行的节点会压上去。
       再让出「一个节点高 + 半行」，保证压不到。 */
    /* ---------- 顶部固定栏 ---------- */
    /* 2026-11-08（实机反馈）：标题原来画在节点**之前**，
       节点压上去把 "SKILL TREE" 盖成 "SK*LL T**EE"。
       这里只留一个占位的绘制标记，真正的标题挪到节点循环之后。 */
    {
        int owned = 0;
        for (int k = 0; k < SKILL_LIST_COUNT; k++)
            if (skillRank[kSkillList[k]] > 0) owned++;
        char b[80];
        sprintf(b, "%d / %d NODES", owned, SKILL_LIST_COUNT);
        DrawTextWin(innerL, L.curY, b, 0x0060D0FFu);
        {  sprintf(b, "GOLD %d", bankGold); }
        DrawTextWin(innerR - TextWidthWin(b), L.curY, b, UI_GOLD);
    }

    const SkillTreeDrawContext context = { L, TH, sel, innerL, innerR, gTop, gBot };
    DrawSkillTreeCanvas(&context);
    DrawSkillTreeDetails(&context);
}

/* 逻辑画布坐标 -> 节点 id（没点中 -1）。
   game.c 的鼠标悬停/点击走这个 —— 技能树不是行列表，按行高去猜会选中错的节点。 */
/* v6 **键盘选中时把节点拉回视野** ——
   原来只有滚轮/拖拽/HOME 能动相机，方向键选到屏外节点时画面毫无变化，
   玩家以为按键坏了（改造台有 BenchFocusNode，技能树一直没有对应物）。 */
void SkillFocusNode(int id) {
    /* v6 **相机跟随选中项** ——
       用户：「WASD 移动会丢失目标，地图太大」。
       原来只在节点跑出 18%~82% 才整块平移一次，节点能停在很靠边的位置，
       缩到 0.45x 时整棵树才 356px 宽，目标很容易看不见。
       现在按轴分别修正：目标落在中央安全带（横 30%~70%、纵 24%~60%）
       之外就把它拉回中心，轴间独立，不会来回抖。 */
    float wx, wy;
    SkillWorldPos(id, &wx, &wy);
    int nodeW, nodeH; SkillNodeSize(&nodeW,&nodeH);
    float cx = wx * skVK + skVX + (float)nodeW * 0.5f;
    float cy = wy * skVK + skVY + (float)nodeH * 0.5f;
    float tx = (float)winW * 0.50f;
    float ty = (float)winH * 0.42f;
    int moved = 0;
    if (cx < (float)winW * 0.30f || cx > (float)winW * 0.70f) { skVX += tx - cx; moved = 1; }
    if (cy < (float)winH * 0.24f || cy > (float)winH * 0.60f) { skVY += ty - cy; moved = 1; }
    if (moved) uiDirty = 1;
}

RECT SkillNodeRect(int id) {
    RECT r={0,0,0,0}; if(!SkillIsPlayerFacing(id)) return r;
    int w,h; SkillNodeSize(&w,&h);
    float x,y; SkillWorldPos(id,&x,&y);
    r.left=SkSX(x); r.top=SkSY(y); r.right=r.left+w; r.bottom=r.top+h;
    return r;
}

int SkillNodeHitTest(int mx, int my) {
    SkillTreeLayout layout; SkillTreeCompute(&layout);
    int th=TextHeightWin(), w,h; SkillNodeSize(&w,&h);
    int top=layout.curY+th+th/2, bottom=layout.panel.bottom-th*4-h-th/2;
    if(bottom<top+h)bottom=top+h;
    int left=layout.panel.left+layout.padX, right=layout.panel.right-layout.padX;
    if(mx<left || mx>right || my<top || my>bottom) return -1;
    for(int id=0;id<SKILL_COUNT;id++) {
        if(!SkillIsPlayerFacing(id))continue;
        RECT r=SkillNodeRect(id);
        if(r.top<top || r.bottom>bottom || r.left<left || r.right>right) continue;
        if(mx>=r.left && mx<=r.right && my>=r.top && my<=r.bottom) return id;
    }
    return -1;
}


/* ============================================================
 * 主菜单绘制（v2.2）
 *
 * 和技能树一样，这一页不再走"通用行列表"：
 * 标题 / 行 / 页脚三块分开，选中行有左侧亮条 + 右箭头，
 * 最后四行（Help / Settings / Training / Quit）前面加一条分组细线。
 * ============================================================ */


/* ============================================================
 * 成就页面专用渲染：精确布局
 * ============================================================ */

static void DrawRebindOverlay(void) {
    if (currentState != STATE_SETTINGS || rebindingAct < 0 || rebindingAct >= ACT_COUNT) return;

    int pad = UIS(26), titlePx = UIS(34);
    int panelW = UIS(520);
    if (panelW > winW - 80) panelW = winW - 80;
    int textW = TextWidthWin(actionNames[rebindingAct]);
    if (panelW < textW + UIS(60)) panelW = textW + UIS(60);
    int panelH = pad * 2 + titlePx + UIS(24) + TextHeightWin() * 3 + UIS(24);

    int left = (winW - panelW) / 2;
    int top = (winH - panelH) / 2;
    int right = left + panelW;
    int bottom = top + panelH;

    BlendRectWin(left, top, right, bottom, 0x0004080c, 1.0f);
    FrameRectWin(left, top, right, bottom, UI_EDGE, 0.95f, UIS(2));
    DrawTitleWin("REBIND", top + pad, 0x00E65A5Au);
    DrawTextCenterWin(winW / 2, top + pad + titlePx + UIS(24), actionNames[rebindingAct], 0x00A0FFA0u);
    if (rebindConflict >= 0 && rebindConflict < ACT_COUNT) {
        /* 冲突确认：告诉玩家会顶掉哪个动作的绑定，再按一次才生效 */
        char cbuf[96];
        sprintf(cbuf, "%s USES THIS KEY", actionNames[rebindConflict]);
        DrawTextCenterWin(winW / 2, bottom - pad - TextHeightWin() * 2, cbuf, UI_ACCENT);
        DrawTextCenterWin(winW / 2, bottom - pad - TextHeightWin(), Tr("PRESS AGAIN TO REPLACE", "再按一次替换"), 0x008C8C8Cu);
    } else {
        DrawTextCenterWin(winW / 2, bottom - pad - TextHeightWin() * 2, Tr("PRESS ANY KEY", "按任意键"), 0x008C8C8Cu);
        DrawTextCenterWin(winW / 2, bottom - pad - TextHeightWin(), Tr("ESC TO CANCEL", "ESC 取消"), 0x006E6E6Eu);
    }
}

/* 把一次按键交给改绑流程。返回 1 = 这个键被改绑消费掉了。
   Esc = 取消；冲突时第一次按键只显示确认提示（不静默抢键），
   同一个键再按一次才执行替换。
   鼠标键也能绑（WM_L/R/MBUTTONDOWN 走这里）。 */
int RebindConsumeKey(int vk) {
    if (rebindingAct < 0 || rebindingAct >= ACT_COUNT) return 0;
    if (vk == VK_ESCAPE) {
        rebindingAct = -1;
        rebindConflict = -1;
        uiDirty = 1;
        return 1;
    }
    /* 冲突检测：这个键已经绑在别的动作上了吗？ */
    int conflict = -1;
    for (int a = 0; a < ACT_COUNT; a++) {
        if (a != rebindingAct && g_bindings[a] == vk) { conflict = a; break; }
    }
    if (conflict >= 0 && rebindConflict != conflict) {
        /* 第一次按：只确认，不抢键。同一个键再按一次才执行替换。 */
        rebindConflict = conflict;
        uiDirty = 1;
        return 1;
    }
    /* 无冲突，或者玩家已经确认替换：清掉旧动作上的这个键 */
    if (conflict >= 0) g_bindings[conflict] = 0;
    rebindConflict = -1;
    g_bindings[rebindingAct] = vk;
    rebindingAct = -1;
    uiDirty = 1;
    return 1;
}

/* 弹窗里的文字：必须在弹窗面板之后画 */
void DrawPopupText(void) {
    if (currentState != STATE_SETTINGS || !settingsPopupOpen) return;
    MenuGeo p = GetPopupGeo();
    const SettingItem* it = &settings[popupItem];
    DrawTitleWin(p.title, p.titleY, 0x00E65A5Au);

    if (it->kind == SET_SLIDER) {
        char val[32];
        int isGamma = (it->fvalue == &gammaValue);
        if (isGamma) sprintf(val, "%.2f", *it->fvalue);
        else sprintf(val, "%.1fx", *it->fvalue);
        DrawTextCenterWin(winW / 2, p.firstY, val, 0x00A0FFA0u);

        RECT t = SliderTrackRect(p);
        char l[32], r[32];
        if (isGamma) {
            sprintf(l, "%.2f", it->fmin);
            sprintf(r, "%.2f", it->fmax);
        } else {
            sprintf(l, "%.1fx", it->fmin);
            sprintf(r, "%.1fx", it->fmax);
        }
        DrawTextWin(t.left, t.bottom + UIS(10), l, 0x008C8C8Cu);
        DrawTextWin(t.right - TextWidthWin(r), t.bottom + UIS(10), r, 0x008C8C8Cu);

        const char* hint = "DRAG OR A/D   ENTER OK";
        DrawTextCenterWin(winW / 2, p.panel.bottom - UIS(38), hint, 0x006E6E6Eu);
    } else {
        for (int i = 0; i < p.count; i++) {
            unsigned int c = (i == popupSelection) ? 0x00A0FFA0u : 0x00A0A0A0u;
            DrawTextInRow(settings[popupItem].options[i], RowRect(p, i), c);
        }
    }
}


/* v2.1：准星。直接写渲染缓冲像素，避免 UI 缩放把线算成 0 宽。
   散布随蓄力扩大 —— 玩家能看出"这一下有多重"。 */
void DrawCrosshair(void) {
    if (!pixels || renderW < 8 || renderH < 8) return;
    int cx = renderW / 2, cy = renderH / 2;
    int gap = 3 + (int)(mouseCharge * 7.0f);
    int len = 4;
    unsigned int col = 0x00D8E8F0u;
    for (int i = 0; i < len; i++) {
        int yy1 = cy - gap - i, yy2 = cy + gap + i;
        int xx1 = cx - gap - i, xx2 = cx + gap + i;
        if (yy1 >= 0 && yy1 < renderH) { pixels[(size_t)yy1 * renderW + cx] = col; if (cx + 1 < renderW) pixels[(size_t)yy1 * renderW + cx + 1] = col; }
        if (yy2 >= 0 && yy2 < renderH) { pixels[(size_t)yy2 * renderW + cx] = col; if (cx + 1 < renderW) pixels[(size_t)yy2 * renderW + cx + 1] = col; }
        if (xx1 >= 0 && yy1 >= 0) pixels[(size_t)cy * renderW + xx1] = col;
        if (xx2 >= 0 && xx2 < renderW) pixels[(size_t)cy * renderW + xx2] = col;
        if (yy1 >= 0 && yy1 < renderH && xx1 >= 0) pixels[(size_t)yy1 * renderW + xx1] = col;
        if (yy2 >= 0 && yy2 < renderH && xx2 < renderW) pixels[(size_t)yy2 * renderW + xx2] = col;
    }
    if (cx >= 1 && cx + 1 < renderW && cy >= 1 && cy + 1 < renderH) {
        pixels[(size_t)cy * renderW + cx] = 0x00FFFFFFu;
        pixels[(size_t)(cy - 1) * renderW + cx] = 0x00FFFFFFu;
        pixels[(size_t)(cy + 1) * renderW + cx] = 0x00FFFFFFu;
        pixels[(size_t)cy * renderW + cx - 1] = 0x00FFFFFFu;
        pixels[(size_t)cy * renderW + cx + 1] = 0x00FFFFFFu;
    }
}

/* 主题房间字幕（右上角）。从 DrawHUD 里抽出来的第一块：
 * 它只依赖 cellMeta / posX / posY 与公开的文本绘制，不碰任何外层局部变量，
 * 所以可以整块搬成函数而不改变作用域。 */


/* v3.1：新手教程的顶部提示条。步骤文案和推进逻辑都在 game.c，
   这里只负责画，避免两边各维护一套步骤。 */
static void DrawTutorialPanel(void) {
    if(!trainingMode || !guideMode || currentState!=STATE_PLAYING || calibActive || lockActive)return;
    int w=winW*43/100;if(w>UIS(400))w=UIS(400);
    int x=winW-w-UIS(12),top=UIS(16),pad=UIS(9),line=UIS(27),y=top+pad;
    BlendRectWin(x,top,x+w,top+UIS(170),UI_BG_PANEL,.96f);FrameRectWin(x,top,x+w,top+UIS(170),UI_EDGE_HOT,1,1);
    char text[256];snprintf(text,sizeof(text),Tr("COURSE %d/%d","课程 %d/%d"),TutorialStepNumber(),TutorialStepTotal());
    DrawTextSmallLiteralWin(x+pad,y,text,UI_ACCENT);y+=UIS(20);
    snprintf(text,sizeof(text),"%s",TutorialObjective());
    while(text[0] && TextMediumLiteralWidth(text)>w-pad*2){size_t n=strlen(text)-1;while(n && ((unsigned char)text[n]&0xC0)==0x80)n--;text[n]=0;}
    DrawTextMediumLiteralWin(x+pad,y,text,UI_INK_TITLE);y+=line;
    const char* hint=TutorialHint();
    for(int row=0;*hint && row<3;row++){
        int bytes=0,fit=0;
        while(hint[bytes] && bytes<230){unsigned char c=(unsigned char)hint[bytes];int n=c<128?1:c<224?2:c<240?3:4;
            if(bytes+n>=250)break;
            memcpy(text+bytes,hint+bytes,(size_t)n);bytes+=n;text[bytes]=0;
            if(TextMediumLiteralWidth(text)>w-pad*2)break;
            fit=bytes;
        }
        if(!fit)break;
        memcpy(text,hint,(size_t)fit);text[fit]=0;DrawTextMediumLiteralWin(x+pad,y,text,UI_INK);hint+=fit;y+=line;
    }
    float tx,ty;TutorialTarget(&tx,&ty);
    if(Dist2(tx,ty,posX,posY)>.1f){snprintf(text,sizeof(text),Tr("TARGET (%d,%d)","目标（%d,%d）"),(int)tx,(int)ty);DrawTextSmallLiteralWin(x+pad,top+UIS(150),text,UI_INFO);}
}

/* ------------------------------------------------------------------ */
/* 存档失败横幅                                                        */
/* ------------------------------------------------------------------ */
/* 以前写完不检查返回值：写不进去就一声不响，玩家玩一整局、重开全没了，
   还会以为是自己没退出/存档坏了。现在失败就在屏幕顶上钉一条红带，
   把 Win32 错误码和实际尝试的路径一起写出来 —— 至少排查有据可查。 */
static void DrawSaveStatusBanner(void) {
    if (g_saveState != 2) return;
    /* v3.0：**不在菜单/安全屋里显示** ——
       用户截图：这条红色横幅横穿 SHOP/BENCH/TREE/REST 四页底部，
       还写着 "TRIED: <本机绝对路径>"，
       第一眼让人以为存档废了。
       现在只在游戏中（PLAYING/PAUSED）提示，且**不显示路径和错误码**。 */
    if (currentState != STATE_PLAYING && currentState != STATE_PAUSED &&
        currentState != STATE_MAIN_MENU) return;
    int th = TextHeightWin();
    int h = th + th / 2;
    /* v3.0 **BUG 修复**：原来 msg 指向字符串字面量，直接传给 ClampRowText，
       而 ClampRowText 内部会 `buf[k] = 0;` / `buf[n] = '.';` ——
       往 .rdata 段里写，触发访问违例（程序直接消失）。
       只要文字超宽需要截断就会命中，窗口一窄必崩。
       现在用可写缓冲区。 */
    static char msg[160];
    snprintf(msg, sizeof(msg), "%s",
             (g_lang == LANG_ZH)
                ? "存档写入失败  -  进度只在本次运行有效"
                : "SAVE FAILED  -  PROGRESS ONLY LASTS THIS SESSION");
    ClampRowText(msg, winW - UIS(20));

    /* v2.9：挪到屏幕**底部** —— 原来钉在最顶上，把安全屋的页签整行盖住了
       （用户截图：SAVE FAILED 压在 SHOP/BENCH/TREE/REST 上）。 */
    int by0 = winH - h - th - UIS(4);
    BlendRectWin(0, by0, winW, by0 + h, 0x00240000u, 1.0f);
    BlendRectWin(0, by0, winW, by0 + UIS(2), 0x00C03030u, 0.95f);
    DrawTextCenterWin(winW / 2, by0 + (h - th) / 2, msg, 0x00FF9090u);

    /* v3.0：**删掉路径行** —— 玩家不需要看到本机路径这种调试信息。
       要定位问题看 g_saveErrAll 就够了。 */
}

/* ------------------------------------------------------------------ */
/* 一帧的呈现                                                          */
/* ------------------------------------------------------------------ */

/* ===== v2.4.20 **拾取/事件提示，最顶层** =====
   用户实测：躲进藏身点后，中间那条黄色提示被藏身遮罩吃掉。
   根因是绘制顺序：DrawHUD → ... → 藏身遮罩（最后画，盖住一切）。

   现在这条提示单独拿出来，在 RenderFrame 的**最后**调用，
   所以它永远压在所有东西上面。位置分两档：
     · 正常游玩：屏幕上方第 5 行以下（原位置）
     · 躲藏中：**下移到观察缝下方**，并加半透明底，
               避免和缝里的画面抢注意力
*/
static const char* actionErrorMessage=NULL;
static double actionErrorUntil=0;
void UiShowActionError(const char* message){
    actionErrorMessage=message;actionErrorUntil=PlatNow()+4;
}

void DrawPickupToast(void) {
    /* 锁盘打开时不画任何拾取/藏身 toast；它原来画在锁盘之后，
       会和锁盘底部说明文字直接叠在一起。 */
    if (lockActive) return;
    if(actionErrorMessage && PlatNow()<actionErrorUntil &&
       (currentState==STATE_PLAYING || currentState==STATE_SAFEHOUSE)){
        int width=DrawTextSmallWidth(actionErrorMessage),x=(winW-width)/2,y=UIS(90);
        BlendRectWin(x-UIS(8),y-UIS(4),x+width+UIS(8),y+FONT_H+UIS(7),UI_BG_PANEL,.95f);
        DrawTextSmallWin(x,y,actionErrorMessage,0x00FF7060u);return;
    }




    if (!pickupMsg || pickupMsgTimer <= 0.0f) return;
    if(currentState==STATE_PLAYING && MonsterChaseActive())return;

    /* ===== v2.4.52 **只在游玩中显示** =====
       用户截图：暂停菜单上浮着一行 "AT THE EXIT: LEAVE, OR GO DEEPER."
       —— 那是上一帧的事件提示，因为 toast 画在帧末（所有图层之上），
       暂停菜单先画、toast 后画，于是压在菜单上。
       现在非游玩状态直接不画。 */
    if (currentState != STATE_PLAYING && currentState != STATE_SAFEHOUSE) return;
    if(currentState==STATE_SAFEHOUSE){
        int width=DrawTextSmallWidth(pickupMsg);
        int x=(winW-width)/2,y=UIS(82);
        BlendRectWin(x-UIS(8),y-UIS(4),x+width+UIS(8),y+FONT_H+UIS(7),UI_BG_PANEL,.94f);
        DrawTextSmallWin(x,y,pickupMsg,UI_ACCENT);return;
    }

    /* ===== v2.4.53 **模态面板打开时不画 toast** =====
       用户截图：出口二选一面板上，toast 的 "AT THE EXIT: LEAVE, OR GO DEEPER."
       和面板标题 "THE EXIT IS OPEN" 叠在一起。
       按 art_direction.h 铁律十三：**事件提示不该和功能 UI 抢同一块区域**。
       模态面板打开时它是最重要的东西，其他提示一律让路。 */
    {

        if (exitChoiceOpen) return;
        if (settingsPopupOpen) return;
    }

    int tw = TextWidthWin(pickupMsg);
    int tx = (winW - tw) / 2;
    int ty;

    if (hiddenIn >= 0) {
        /* v2.4.26：**躲藏时用小号字，贴在最左下角** ——
           用户两次反馈：先压到方框下沿，字号又太大。
           现在：scale=1 的小字 + 左对齐 x=20 + y 贴状态面板上方，
           画的宽度按小号字重新算，绝不会伸到中间的方框里。 */
        int sw2 = DrawTextSmallWidth(pickupMsg);
        int shH = FONT_H + 2;
        tx = UIS(16);
        /* v2.4.27：**再往上挪 70** ——
           原来 y = winH-150 正好压在左下状态面板的上边框上
           （用户截图：黄框和 SANITY 面板贴在一起）。
           面板顶大约在 winH-160，留出 30 的空隙。 */
        ty = winH - UIS(220);
        if (ty < UIS(120)) ty = UIS(120);
        BlendRectWin(tx - UIS(4), ty - UIS(3), tx + sw2 + UIS(4), ty + shH + UIS(3),
                     0x00101010u, 0.80f);
        FrameRectWin(tx - UIS(4), ty - UIS(3), tx + sw2 + UIS(4), ty + shH + UIS(3),
                     0x00E8E060u, 0.55f, 1);
        DrawTextSmallWin(tx, ty, pickupMsg, 0x00F0E870u);
        return;
    } else {
        /* ================================================================
         * 2026-11-08（实机反馈）：**播报行压住 BOOST**
         * ----------------------------------------------------------------
         * 原来写死 ty = UIS(150)，而左上角第 5 行（BOOST）在 130..154 ——
         * 实测截图里 `THREAT: SOUND HUNTER…` 正好盖在 `BOOST +50%` 上。
         *
         * 现在按"最满 6 行"算出 HUD 区块下沿，再往下让 10px。
         * 同时：太长就降级成小号字（原来是直接画出去被窗口裁掉）。
         * ================================================================ */
        int hudBottom = UIS(18) + 6 * (TextHeightWin() + UIS(4));
        ty = hudBottom + UIS(10);
        if (ty < TextHeightWin() * 6) ty = TextHeightWin() * 6;
    }

    /* 2026-11-08：判据必须对**渲染缓冲**（renderW），不是窗口宽 ——
       文字是按窗口尺度排的，画进缓冲就溢出。实测 1280 窗口 / 800 缓冲下，
       `tw=992` 对 winW 判"不长"，但画出来右边被切掉一截。 */
    if (tw > renderW - 40) {
        /* 太长：小号字重算宽度并居中，绝不画到窗口外 */
        int sw3 = DrawTextSmallWidth(pickupMsg);
        int tx3 = (winW - sw3) / 2;
        if (tx3 < UIS(8)) tx3 = UIS(8);
        DrawTextSmallWin(tx3, ty, pickupMsg, 0x00F0E870u);
    } else {
        DrawTextWin(tx, ty, pickupMsg, 0x00E8E060u);
    }
}





static void RenderFrameScene(void) {
    int showScene = (currentState == STATE_PLAYING) ||
                    (currentState == STATE_PAUSED) ||
                    (currentState == STATE_SETTINGS && prevState == STATE_PAUSED) ||
                    (currentState == STATE_SAFEHOUSE);

    if (showScene) {
        /* v2.1：相机抖动（偏移玩家位置再还原，所有投影自动跟着抖） */
        /* Feedback must not push the render camera through walls. */
        Render3D();
        if (currentState == STATE_PLAYING && isMapOpen && mapFullScreen) RenderMap();
        /* 跳脸虫：屏幕空间 overlay，盖在场景之上、HUD 之下 */
        /* Floor transitions no longer overwrite the frame with white. */

        /* v2.1：受伤红闪（越靠边缘越红）+ 枪口闪光 */
        if (hitFlash > 0.0f) {
            float a = hitFlash * 0.42f;
            int cxx = renderW / 2, cyy = renderH / 2;
            float maxd = (float)(cxx * cxx + cyy * cyy);
            for (int y = 0; y < renderH; y++) {
                unsigned int* row = pixels + (size_t)y * renderW;
                for (int x = 0; x < renderW; x++) {
                    float dx = (float)(x - cxx), dy = (float)(y - cyy);
                    float t = (dx * dx + dy * dy) / maxd;      /* 0 中心 -> 1 角落 */
                    if (t < 0.35f) continue;
                    float k = (t - 0.35f) / 0.65f;
                    float al = a * k;
                    unsigned int d = row[x];
                    int dr = (int)((d >> 16) & 0xFF), dg = (int)((d >> 8) & 0xFF), db = (int)(d & 0xFF);
                    int nr = (int)(dr * (1.0f - al) + 220 * al);
                    int ng = (int)(dg * (1.0f - al) + 20 * al);
                    int nb = (int)(db * (1.0f - al) + 20 * al);
                    row[x] = ((unsigned int)nr << 16) | ((unsigned int)ng << 8) | (unsigned int)nb;
                }
            }
        }
        if (muzzleFlash > 0.0f) {
            /* 屏幕下方中央的一团暖光，像枪口焰 */
            int cxx = renderW / 2, cyy = (int)(renderH * 0.86f);
            int rad = (int)(renderW * 0.10f * (0.5f + muzzleFlash * 0.5f));
            if (rad < 6) rad = 6;
            for (int dy = -rad; dy <= rad; dy++) {
                int y = cyy + dy;
                if (y < 0 || y >= renderH) continue;
                unsigned int* row = pixels + (size_t)y * renderW;
                for (int dx = -rad; dx <= rad; dx++) {
                    int x = cxx + dx;
                    if (x < 0 || x >= renderW) continue;
                    float d2 = (float)(dx * dx + dy * dy) / (float)(rad * rad);
                    if (d2 > 1.0f) continue;
                    float al = (1.0f - d2) * muzzleFlash * 0.55f;
                    unsigned int d = row[x];
                    int dr = (int)((d >> 16) & 0xFF), dg = (int)((d >> 8) & 0xFF), db = (int)(d & 0xFF);
                    int nr = (int)(dr * (1.0f - al) + 255 * al);
                    int ng = (int)(dg * (1.0f - al) + 225 * al);
                    int nb = (int)(db * (1.0f - al) + 140 * al);
                    row[x] = ((unsigned int)nr << 16) | ((unsigned int)ng << 8) | (unsigned int)nb;
                }
            }
        }
    } else {
        memset(pixels, 0, (size_t)renderW * renderH * sizeof(unsigned int));
    }
}

int SkillPickAt(int x,int y){
    if(currentState!=STATE_SKILL_PICK)return -1;
    MenuGeo g=GetMenuGeo();
    for(int i=0;i<INRUN_CHOICES;i++){
        RECT r=RowRect(g,i);
        if(skillPickOptions[i]>=0 && x>=r.left && x<r.right && y>=r.top && y<r.bottom)return i;
    }return -1;
}
void RunReplacementButtonRect(int index,RECT* r){
    int gap=UIS(12),w=winW*34/100,h=UIS(42),x=winW*15/100;
    if(index)x+=w+gap;
    *r=(RECT){x,winH*58/100,x+w,winH*58/100+h};
}
static void DrawRunReplacement(void){
    if(!LoopRunReplacementPending())return;
    BlendRectWin(0,0,winW,winH,0x030609,.94f);
    int x=winW*12/100,y=winH*34/100,right=winW*88/100;
    UiPanel9(x,y,right,winH*72/100,1);
    DrawTextMediumLiteralWin(x+UIS(18),y+UIS(16),Tr("A RUN IS ALREADY IN PROGRESS","当前有正在游玩的进度"),UI_ACCENT);
    DrawTextSmallLiteralWin(x+UIS(18),y+UIS(57),Tr("Replace it with a new run? Cancel keeps your progress.","是否用新对局覆盖？取消将保留原有进度。"),UI_INK);
    for(int i=0;i<2;i++){
        RECT r;RunReplacementButtonRect(i,&r);UiRow(r.left,r.top,r.right,r.bottom,LoopRunReplacementChoice()==i?UI_ROW_SEL:UI_ROW_NORMAL,1);
        const char* label=i?Tr("REPLACE [Y]","覆盖进度 [Y]"):Tr("KEEP PROGRESS [ESC]","保留进度 [ESC]");
        DrawTextMediumLiteralWin(r.left+UIS(10),r.top+UIS(10),label,i?UI_DANGER:UI_OK);
    }
}

static void RenderFrameMenus(void) {
    /* 文字也在这一层，先合成再一次性呈现；弹窗必须在列表文字之后画 */
    if (currentState != STATE_PLAYING) {
        int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);
        /* 2026-11-16（用户实测）：满强度档时"第一级暂停页"仍是蓝色的。
           原因：暂停页为了能看见身后的场景，完全没有铺背景色 ——
           红色档位的面板下面透出来的还是蓝色走廊，所以整页看着是蓝的。
           现在改成**铺一层 72% 的主题底色**：场景仍可见，但整体跟着档位变红。 */
        if (currentState == STATE_PAUSED ||
            (currentState == STATE_SETTINGS && prevState == STATE_PAUSED)) {
            BlendRectWin(0,0,winW,winH,UI_BG_PANEL,0.72f);
        } else {
            BlendRectWin(0,0,winW,winH,UI_BG_PANEL,1.0f);
        }
        UiDreamFrame(); /* Collage belongs above the menu backdrop, below labels. */
        UiDrawMenuPages();   /* Menu composition has one shared entry point. */
        if (popupOpen) {
            DrawPopupPanel();
            DrawPopupText();
        }
        /* 改绑提示框：最顶层，盖住所有菜单 */
        /* 改绑提示框：最顶层，盖住所有菜单 */
        DrawRebindOverlay();
        /* 存档写不进去必须吼出来（见 save.c 的说明） */
        DrawSaveStatusBanner();
    } else if (isMapOpen && mapFullScreen) {
        /* 全屏地图：不画 HUD —— 整个屏幕就是地图，别在上面压一堆数值。
           小窗地图则照常画 HUD（它只是右下角一个叠加层）。 */
    } else {
        DrawHUD();
        /* The course board is composited after cover occlusion. */
    }
}

static void RenderFrameOcclusion(void) {
    /* v3.0：**帧末覆盖层** —— 不管什么状态都要画
       （锁盘只在游戏中打开；藏身遮罩也只在游戏中生效）。
       原来这两块写在 if (currentState != STATE_PLAYING) 分支里，
       结果玩游戏时锁盘一帧都不显示、藏身也没有任何视觉反馈。 */
    DrawLockOverlay();
    {

        /* v3.3：**只在真正游玩时做藏身遮罩** ——
           用户两次反馈：
             ①「打开商店之后，商店界面会被隐藏点覆盖」
             ②「隐藏点的窗口优先级甚至高于 ESC 的暂停页面，很违和」
           遮罩是帧末画的，优先级比商店和暂停页都高。
           现在只在 STATE_PLAYING 画，商店/暂停/其它状态一律不画。 */
        if (hiddenIn >= 0 && currentState == STATE_PLAYING) {
            /* 躲进垃圾桶：只留中间一条很窄的缝，四周是桶壁。
               旧版只是四条暗边，中间还留一大块，看起来不像“藏在里面”。 */
            int ow = winW * 18 / 100;
            int oh = winH * 38 / 100;
            if (ow < 90) ow = 90;
            if (oh < 110) oh = 110;
            int ox0 = (winW - ow) / 2;
            int oy0 = (winH - oh) / 2;
            int ox1 = ox0 + ow;
            int oy1 = oy0 + oh;

            BlendRectWin(0, 0, ox0, winH, 0x00000000u, 0.94f);
            BlendRectWin(ox1, 0, winW, winH, 0x00000000u, 0.94f);
            BlendRectWin(ox0, 0, ox1, oy0, 0x00000000u, 0.94f);
            BlendRectWin(ox0, oy1, ox1, winH, 0x00000000u, 0.94f);

            /* 铁皮桶口：内圈阴影 + 外圈金属边 */
            FrameRectWin(ox0 - UIS(4), oy0 - UIS(4), ox1 + UIS(4), oy1 + UIS(4),
                         0x00182018u, 0.95f, UIS(5));
            FrameRectWin(ox0, oy0, ox1, oy1, 0x00506058u, 0.9f, UIS(2));
            BlendRectWin(ox0, oy0, ox1, oy0 + UIS(8), 0x00000000u, 0.55f);
            BlendRectWin(ox0, oy1 - UIS(10), ox1, oy1, 0x00000000u, 0.45f);
            /* ===== v3.3：**藏身信息面板（紧凑版）** =====
               用户反馈：
                 ①「在隐藏点看不到判定条冷却、开商店、小游戏提示」
                 ②「理智值、体力条、手电电量这些关键信息读不到」
                 ③「面板太大，遮住观察视野了」

               所以这块面板要做两件事：
                 · 把遮罩挡掉的关键状态补回来（理智 / 电量 / 体力）
                 · 把可用操作列出来（空格 / T / E）
               但必须够小：**4 行**（3 状态 + 1 操作横排），
               位置放在**观察缝的下沿之下**，绝不压住缝。 */
            {




                char sSan[48], sBat[48], sSta[48];
                char l1[48], l2[32], l3[32];
                unsigned int c1, cSan, cBat, cSta;

                /* ---- 三条关键状态 ---- */
                {
                    int sp = (int)(sanity + 0.5f);
                    if (sp < 0) sp = 0;
                    if (sp > 100) sp = 100;
                    sprintf(sSan, Tr("SANITY  %d%%", "理智  %d%%"), sp);
                    cSan = (sp <= 25) ? 0x00FF5050u
                         : (sp <= 50) ? 0x00FFA040u : 0x00E0E0E0u;
                }
                {
                    float bmax = (float)BatteryMax();
                    int bp = (bmax > 0.5f) ? (int)(battery / bmax * 100.0f + 0.5f) : 0;
                    if (bp < 0) bp = 0;
                    if (bp > 100) bp = 100;
                    /* v3.3：**充电时给一行明确提示** ——
                       藏身回电是新机制，光看数字缓慢上涨不够明确，
                       直接在电量后面缀上实时速率。 */
                    {

                        float rr = HIDE_BATTERY_REGEN * (flashlightOn ? 0.5f : 1.0f);
                        if (bp >= 100)
                            sprintf(sBat, Tr("BATTERY  100%%  FULL",
                                             "电量  100%%  满"), 0);
                        else
                            sprintf(sBat, Tr("BATTERY  %d%%  +%.0f/s",
                                             "电量  %d%%  +%.0f/秒"), bp, rr);
                    }
                    cBat = (bp <= 15) ? 0x00FF5050u
                         : (bp <= 35) ? 0x00FFA040u : 0x00E8D060u;
                }
                {
                    float smax = StaminaMax();
                    int tp = (smax > 0.5f) ? (int)(stamina / smax * 100.0f + 0.5f) : 0;
                    if (tp < 0) tp = 0;
                    if (tp > 100) tp = 100;
                    sprintf(sSta, Tr("RUN  %d%%", "体力  %d%%"), tp);
                    cSta = (tp <= 20) ? 0x00FF5050u : 0x0060E080u;
                }

                /* ---- 操作（一段话横排，省高度）---- */
                if (calibActive) {
                    sprintf(l1, "%s", Tr("QWER", "QWER 校准"));
                    c1 = UI_ACCENT;
                } else if (calibCooldown > 0.0f) {
                    sprintf(l1, Tr("SPACE %ds", "空格 %d秒"),
                            (int)(calibCooldown + 0.5f));
                    c1 = 0x00808080u;
                } else {
                    sprintf(l1, "%s", Tr("SPACE GAME", "空格 小游戏"));
                    c1 = 0x0060FF90u;
                }
                sprintf(l2, "%s", Tr("T SHOP", "T 商店"));
                sprintf(l3, "%s", Tr("E OUT",  "E 出去"));

                int lh = TextHeightWin();
                int gap = UIS(3);
                int padX = UIS(12), padY = UIS(8);

                /* 宽度 = 状态列 与 操作行 里更宽的那个 */
                int bw = TextWidthWin(sSan);
                if (TextWidthWin(sBat) > bw) bw = TextWidthWin(sBat);
                if (TextWidthWin(sSta) > bw) bw = TextWidthWin(sSta);
                {
                    int aw = TextWidthWin(l1) + UIS(12)
                           + TextWidthWin(l2) + UIS(12)
                           + TextWidthWin(l3);
                    if (aw > bw) bw = aw;
                }
                bw += padX * 2;
                int bh = lh * 4 + gap * 3 + padY * 2;

                /* 位置：观察缝下沿之下；真放不下就贴底 */
                int bx = UIS(20);
                int by = oy1 + UIS(8);
                if (by + bh > winH - UIS(10)) by = winH - bh - UIS(10);
                if (by < UIS(8)) by = UIS(8);

                BlendRectWin(bx - UIS(2), by - UIS(2),
                             bx + bw + UIS(2), by + bh + UIS(2),
                             0x00000000u, 0.80f);
                BlendRectWin(bx, by, bx + bw, by + bh, 0x0014181Cu, 0.97f);
                FrameRectWin(bx, by, bx + bw, by + bh, 0x00706858u, 0.90f, 1);

                int ty2 = by + padY;
                DrawTextWin(bx + padX, ty2, sSan, cSan);
                DrawTextWin(bx + padX, ty2 + lh + gap, sBat, cBat);
                DrawTextWin(bx + padX, ty2 + (lh + gap) * 2, sSta, cSta);

                {
                    int ty3 = ty2 + (lh + gap) * 3;
                    int x3 = bx + padX;
                    DrawTextWin(x3, ty3, l1, c1);
                    x3 += TextWidthWin(l1) + UIS(12);
                    DrawTextWin(x3, ty3, l2, 0x00D8C880u);
                    x3 += TextWidthWin(l2) + UIS(12);
                    DrawTextWin(x3, ty3, l3, 0x0090A0A8u);
                }
            }

        }
            /* v3.0：**藏身遮罩取消** ——
               用户实测：「躲在柜子里视野受限，根本看不到任何小游戏提示」。
               遮罩本来是为了「从柜门缝往外看」的氛围，
               但它把 HUD 提示、判定条、商店开关全挡住了。
               现在完全不遮 —— 躲进去只是不能动、不会被抓，
               视野和平时一样。 */
            (void)0;
}   /* 2026-11-09 补：RenderFrameOcclusion 少一个闭合括号 */
}

static void RenderFrameCalibration(void) {
    DrawExitChoice();
    if(currentState==STATE_PLAYING){
        HushDraw();
        if(roomTheme.active && roomTheme.inside && !MonsterChaseActive()){
            char instruction[240];snprintf(instruction,sizeof(instruction),"%s",RoomThemeInstruction());UiTrimSmallText(instruction,winW-UIS(70));
            DrawTextSmallWin(UIS(35),UIS(72),instruction,UI_INK_DIM);
        }
    }
    if((currentState!=STATE_PLAYING && currentState!=STATE_SAFEHOUSE) || !calibActive)return;
    /* 2026-11-16（用户：小游戏窗口 UI 重叠）重排布局：
       原来"已恢复"那行(py=h-16)和按键行(target+12)挤在一起、退出提示还画在面板外，
       会和 HUD / 世界提示叠。现在：
         标题两行在顶部（COMBO / RESTORED 各占一行）
         四条轨道从第二行下方开始
         判定线在上方留出按键行的空间
         退出提示画在面板**内部**底部，不再外溢 */
    int th = TextHeightWin();
    int w=UIS(360),h=winH*70/100,x=(winW-w)/2,y=winH*14/100;
    int headY = y + UIS(8);
    int laneTop = headY + th * 2 + UIS(10);
    int target = y + h - UIS(52);
    if (target < laneTop + UIS(40)) target = laneTop + UIS(40);   /* 小窗口兜底 */
    BlendRectWin(x,y,x+w,y+h,0x00081220u,.96f);
    FrameRectWin(x,y,x+w,y+h,UI_ACCENT,.9f,2);
    int laneW=w/4;
    for(int lane=0;lane<4;lane++) {
        int lx=x+lane*laneW;
        BlendRectWin(lx+2,laneTop,lx+laneW-2,target+UIS(10),lane%2?0x00162939u:0x000D1C2Au,1);
        char key[2]={"QWER"[lane],0};
        DrawTextCenterWin(lx+laneW/2,target+UIS(12),key,UI_INK);
    }
    BlendRectWin(x+2,target,x+w-2,target+2,UI_ACCENT,1);
    for(int i=0;i<CalibNoteCount();i++) {
        float offset=CalibNoteOffset(i);
        if(offset<-.18f || offset>2.0f)continue;
        int ny=target-(int)(offset*(target-laneTop)/2.0f);
        int nx=x+CalibNoteLane(i)*laneW+4;
        BlendRectWin(nx,ny-UIS(4),nx+laneW-8,ny+UIS(4),UI_ACCENT,1);
    }
    char text[80];
    snprintf(text,sizeof(text),Tr("COMBO %d  ERRORS %d/6","连击%d  失误%d/6"),CalibCombo(),CalibErrors());
    DrawTextSmallWin(x+UIS(8),headY,text,UI_INK);
    snprintf(text,sizeof(text),Tr("RESTORED +%d SAN / +%.1f CELL","已恢复 +%d理智 / +%.1f电量"),CalibPendingReward(),(double)CalibBatteryRestored());
    UiTrimSmallText(text,w-UIS(12));
    DrawTextSmallLiteralWin(x+UIS(6),headY+th+UIS(2),text,UI_OK);
    /* 退出提示移到面板内部底部（原来在 y+h+8，会盖住 HUD 与藏身提示） */
    DrawTextSmallWin(x+UIS(8),y+h-UIS(14),Tr("SPACE TWICE: EXIT","连按两次空格退出"),UI_ACCENT);
}


static void PresentComposedFrame(void) {
    /* ==================================================================
     * 2026-11-06（网页版 R1）：**呈现整体交给平台层**
     * ------------------------------------------------------------------
     * letterbox 几何仍在这里算（纯数学，不碰任何系统类型），
     * 真正的 StretchBlt / StretchDIBits 由 plat_win32.c 负责；
     * 网页后端的同一份几何会变成 canvas 的绘制矩形。
     * ================================================================== */
    {
        int cw = 0, ch = 0;
        PlatWindowSize(&cw, &ch);
        if (cw <= 0 || ch <= 0) return;

        PlatPresentRect pr;
        memset(&pr, 0, sizeof(pr));
        pr.cw = cw;
        pr.ch = ch;

        /* 目标比例 = 渲染缓冲的宽高比；不匹配时留黑边，绝不拉伸变形 */
        {
            double wantR = (double)renderW / (double)renderH;
            double haveR = (double)cw / (double)ch;
            if (haveR > wantR) {
                pr.dh = ch;
                pr.dw = (int)((double)ch * wantR + 0.5);
            } else {
                pr.dw = cw;
                pr.dh = (int)((double)cw / wantR + 0.5);
            }
            if (pr.dw < 1) pr.dw = 1;
            if (pr.dh < 1) pr.dh = 1;
            /* 兜底：等比算出来不到窗口 70% 说明分辨率正在切换，宁可拉满 */
            if (pr.dw < cw * 7 / 10 || pr.dh < ch * 7 / 10) {
                pr.dw = cw;
                pr.dh = ch;
            }
            pr.dx = (cw - pr.dw) / 2;
            pr.dy = (ch - pr.dh) / 2;
        }

        /* 拾取/事件提示：最顶层，但必须**在呈现之前**画进像素缓冲 */
        DrawPickupToast();

        pr.fullBleed = (pr.dx == 0 && pr.dy == 0 && pr.dw == cw && pr.dh == ch);
        PlatPresentFrame(&pr, pixels, renderW, renderH);
    }
}

void RenderFrame(void) {
    static int previousState=-1;
    if(previousState!=(int)currentState){if(pixels)memset(pixels,0,(size_t)renderW*renderH*sizeof(*pixels));previousState=(int)currentState;uiDirty=1;}
    UiLayoutBegin();
    RenderFrameScene();
    if(currentState==STATE_PLAYING)UiDreamFrame();
    RenderFrameMenus();
    UiDecayFrame();
    RenderFrameOcclusion();
    if(currentState==STATE_PLAYING && isMapOpen && !mapFullScreen)RenderMap();
    DrawTutorialPanel();
    RenderFrameCalibration();
    DrawRunReplacement();
    PresentComposedFrame();
}


/* ================================================================== */
/* v2.2 UI 主题实现（合并进 ui.c，因为要用到本文件的 static 绘制原语）    */
/* ================================================================== */
#include "ui_theme.h"


#define UI_INSET 20

void UiInit(void) { /* FIXED_RESULT: UI has no initialization-owned state; resources are initialized by their owners. */ }

void UiPanel9(int x0,int y0,int x1,int y1,int hot) {
    if(x1<=x0 || y1<=y0)return;
    BlendRectWin(x0,y0,x1,y1,UI_BG_PANEL,1.0f);
    FrameRectWin(x0,y0,x1,y1,hot?UI_EDGE_HOT:UI_EDGE,.98f,UIS(1));
    BlendRectWin(x0+1,y0+1,x1-1,y0+UIS(3),UI_BG_SEL,.95f);
    UiCorrodeRect(x0,y0,x1,y1);
}


void UiCursor(int x,int y,int size,float alpha) {
    if(size<6)size=6;
    if(size>48)size=48;
    for(int line=0;line<size;line++) {
        int distance=abs(line-size/2);
        int span=size/2-distance;
        BlendRectWin(x,y+line,x+span+1,y+line+1,UI_ACCENT,alpha);
    }
}


void UiRowSel(int x0,int y0,int x1,int y1,float alpha) {
    BlendRectWin(x0,y0,x1,y1,UI_BG_SEL,alpha);
    BlendRectWin(x0,y0,x0+UIS(3),y1,UI_ACCENT,alpha);
}


/* v2.2：分组细线 —— 中间亮两头渐隐，用于把列表分成逻辑段 */

/* ==================== v3.0 统一组件 ====================
 * 勘查报告指出：6 套手写面板、3 套选中态、7 条手写进度条。
 * 这三个函数把它们收编，新界面只允许调用这三个。
 * ==================================================== */

void UiPanel(int x0, int y0, int x1, int y1, int kind) {
    if (kind == UI_PANEL_BARE) return;
    unsigned int edge, fill;
    switch (kind) {
    case UI_PANEL_HOT:    edge = UI_EDGE_HOT;  fill = UI_BG_PANEL; break;
    case UI_PANEL_DANGER: edge = UI_DANGER;    fill = 0x00240000u;  break;
    default:              edge = UI_EDGE;      fill = UI_BG_PANEL;  break;
    }
    BlendRectWin(x0, y0, x1, y1, fill, 0.97f);
    FrameRectWin(x0, y0, x1, y1, edge, 0.95f, 1);
    UiCorrodeRect(x0,y0,x1,y1);
}

void UiRow(int x0, int y0, int x1, int y1, int state, float alpha) {
    switch (state) {
    case UI_ROW_SEL:
        BlendRectWin(x0, y0, x1, y1, UI_BG_SEL, 0.85f * alpha);
        BlendRectWin(x0 - UIS(SP1), y0, x0 - UIS(SP1) + UIS(3), y1, UI_ACCENT, 0.95f);
        break;
    case UI_ROW_HOVER:
        BlendRectWin(x0, y0, x1, y1, UI_BG_HOVER, 0.55f * alpha);
        FrameRectWin(x0, y0, x1, y1, UI_INFO, 0.7f, 1);
        break;
    case UI_ROW_DISABLED:
        BlendRectWin(x0, y0, x1, y1, UI_BG_LOCKED, 0.60f * alpha);
        break;
    case UI_ROW_LOCKED:
        BlendRectWin(x0, y0, x1, y1, UI_BG_LOCKED, 0.85f * alpha);
        break;
    default:
        BlendRectWin(x0, y0, x1, y1, UI_BG_ROW, 0.45f * alpha);
        break;
    }
    UiCorrodeRect(x0,y0,x1,y1);
}

void UiBar(int x, int y, int w, int h, float frac, int kind, unsigned int col) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    unsigned int c = col;
    if (kind == UI_BAR_DANGER) c = UI_DANGER;
    else if (kind == UI_BAR_WARN) c = UI_WARN;
    BlendRectWin(x, y, x + w, y + h, 0x00202020u, 0.80f);
    if (frac > 0.0f) BlendRectWin(x, y, x + (int)((float)w * frac), y + h, c, 0.95f);
    FrameRectWin(x, y, x + w, y + h, UI_EDGE_DIM, 0.7f, 1);
}

void UiSectionRule(int cx, int y, int w) {
    int th = TextHeightWin() / 10;
    if (th < 2) th = 2;
    int half = w / 2;
    BlendRectWin(cx - half, y, cx + half, y + th, UI_ACCENT, 0.18f);
    BlendRectWin(cx - half / 4, y, cx + half / 4, y + th, UI_ACCENT, 0.55f);
}

/* 2026-11-05：**按左右边界画的分组线**。
   安全屋那几页左边有一列页签（sidebar），原来这条线是按整个面板居中的，
   会横穿到页签列里 —— 用户截图「中间那两个白线过界了」。
   现在给一个显式 x0..x1 的版本，安全屋用「内容列」的范围。 */
void UiSectionRuleSpan(int x0, int x1, int y) {
    int th = TextHeightWin() / 10;
    if (th < 2) th = 2;
    if (x1 <= x0) return;
    int cx = (x0 + x1) / 2;
    int half = (x1 - x0) / 2;
    BlendRectWin(x0, y, x1, y + th, UI_ACCENT, 0.18f);
    BlendRectWin(cx - half / 4, y, cx + half / 4, y + th, UI_ACCENT, 0.55f);
}

/* 页脚分隔线 —— 比分组线更暗，贴着面板底部 */
void UiFooterRule(int x0, int x1, int y) {
    int th = TextHeightWin() / 12;
    if (th < 2) th = 2;
    BlendRectWin(x0, y, x1, y + th, UI_INK_DIM, 0.55f);
}

/* 面板四角的 L 形角标（9-slice 的螺丝之外再补一层"仪表面板"感） */
void UiCornerMarks(int x0, int y0, int x1, int y1) {
    int arm = TextHeightWin();
    int th = TextHeightWin() / 9;
    if (th < 2) th = 2;
    /* 只画内侧两个角，和 9-slice 的螺丝错开 */
    BlendRectWin(x0 + arm, y0 + arm / 2, x0 + arm * 2, y0 + arm / 2 + th, UI_ACCENT, 0.35f);
    BlendRectWin(x0 + arm, y0 + arm / 2, x0 + arm / 2 + th, y0 + arm, UI_ACCENT, 0.35f);
    BlendRectWin(x1 - arm * 2, y1 - arm / 2, x1 - arm, y1 - arm / 2 + th, UI_ACCENT, 0.35f);
    BlendRectWin(x1 - arm / 2 - th, y1 - arm, x1 - arm / 2, y1 - arm / 2, UI_ACCENT, 0.35f);
}

void UiTitle(int cx,int y,const char* text) {
    DrawTextCenterWin(cx,y,text,UI_INK_TITLE);
    if(currentState==STATE_MAIN_MENU || perception.contamination>.05f){
        int width=TextWidthWin(text),bottom=y+TextHeightWin();
        float amount=currentState==STATE_MAIN_MENU?.35f:perception.contamination*.45f;
        for(int i=0;i<width;i+=17){unsigned h=(unsigned)(i*251+19);int length=2+(h%9);BlendRectWin(cx-width/2+i,bottom-2,cx-width/2+i+2,bottom+length,0x00802B48u,amount);}
    }
}

void UiLabel(int x, int y, unsigned int col, const char* s) {
    DrawTextWin(x, y, s, col);
}

void UiHint(int cx, int y, const char* s) {
    DrawTextCenterWin(cx, y, s, UI_INK_DIM);
}

void HubPurchaseRect(RECT* rect) {
    SkillTreeLayout layout;
    SkillTreeCompute(&layout);
    int th=TextHeightWin(),right=layout.panel.right-layout.padX;
    *rect=(RECT){right-UIS(138),layout.panel.bottom-th*2-UIS(3),right,layout.panel.bottom-th+UIS(3)};
}
int HubPurchaseAt(int x,int y) {
    RECT r;
    HubPurchaseRect(&r);
    return x>=r.left && x<=r.right && y>=r.top && y<=r.bottom;
}

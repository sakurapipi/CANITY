/*

 * Canity — UI 层实现：点阵字库 / 文字 / 菜单与弹窗 / HUD / 最终呈现
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
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


static const char* modNames[MOD_COUNT] = {"DARK", "HUNTED", "CRAMPED", "LEAKY", "RICH", "CALM"};
static const char* modDesc[MOD_COUNT] = {
    "FLASHLIGHT STARTS LOW",
    "THE THING MOVES FASTER",
    "FEWER CONTAINERS",
    "SANITY DRAINS FASTER",
    "MORE CONTAINERS",
    "SANITY DRAINS SLOWER"
};

/* 大厅：金币货币 + 六项永久升级 */
/* v1.8：7 种主题。原来只有 4 项，但 themeNames[themeId] 在主题 ≥4 时
   越界读到 NULL → DrawTextWin 解引用空指针 → 直接崩溃。 */
static const char* themeNames[THEME_COUNT] = {
    "BRICK", "PRISON", "HELL", "WARD", "ICE VAULT", "RUST WORKS", "FLESH CAVE"
};
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
static const char* SkillNameZh(const char* en) {
    /* v6：**按新的 20 个节点重写** ——
       原来 10 条里 6 条是已删节点的旧名（TYPE WORK / CARTOGRAPHER …），
       20 个节点只有 4 个有中文，中文模式下技能树 16 个名字是英文。 */
    static const char* tbl[][2] = {
        { "LIGHT STEP",  "轻步" },
        { "QUIET LID",   "静盖" },
        { "GHOST STEP",  "鬼步" },
        { "DECOY",       "诱饵" },
        { "WEATHERMAN",  "观测者" },
        { "SIPHON",      "虹吸" },
        { "CRANK WALK",  "摇电行走" },
        { "REST",        "休整" },
        { "METABOLISM",  "代谢" },
        { "LAST RES",    "绝境反击" },
        { "BREATH",      "屏息" },
        { "STILL FACE",  "静面" },
        { "SLIP AWAY",   "滑脱" },
        { "PLAY DEAD",   "装死" },
        { "SALVAGE",     "打捞" },
        { "CARTO",       "制图" },
        { "BREAKER",     "配电子" },
        { "FUSE SMITH",  "保险丝匠" },
        { "SHORTCUT",    "捷径" },
        { "SILENT RUN",  "无声疾行" },
    };
    for (int i = 0; i < (int)(sizeof(tbl) / sizeof(tbl[0])); i++)
        if (strcmp(tbl[i][0], en) == 0) return tbl[i][1];
    return en;
}




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
static const unsigned int kBranchColor[7] = {   /* v6：5 项（branch=4 = 跨支行） */
    0x0040D8FFu,   /* A 潜行 青 */
    0x0050E070u,   /* B 续航 绿 */
    0x00FF6050u,   /* C 反击 红 */
    0x00B080FFu,   /* 跨支   紫 */
    0x00FF9050u, 0x00C080FFu, 0x00808088u /* risk, cross, origin */
};
/* v2.9：分支名走翻译（原来只有英文） */
/* v3.3：第 3 列从「伤害交付」改成「藏匿脱身」后，表头没跟着改 ——
   界面上写 DELIVERY，里面却是 HOLD BREATH / STILL FACE / SLIP AWAY /
   PLAY DEAD（见 skills.c 的「列 2：隐」）。改成 CONCEAL。 */
static const char* kBranchName[7] = { "SILENCE", "ECONOMY", "CONCEAL", "SHAPE", "RISK", "CROSS", "ORIGIN" };
static const char* kBranchNameZh[7] = { "沉寂", "节流", "藏匿", "塑形", "冒险", "跨支", "起点" };

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
    37, 38, 39           /* 反击者：挣脱 / 以牙还牙 / 反杀 */,
    40,
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
    (sizeof(kSkillList) / sizeof(kSkillList[0]) == SKILL_COUNT) ? 1 : -1];
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
            g.rowH=(g.panel.bottom-g.firstY-TextHeightWin()*3-UIS(20))/g.count;
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
   原来画在面板里的条就永远看不到。 */
void DrawCalibBar(int bx, int by, int barW, int barH) {
    if (barW < UIS(120)) return;
    BlendRectWin(bx, by, bx + barW, by + barH, 0x00101418, 0.92f);
    {
        float zsc = CalibZoneScale();   /* v2.6：随层数缩小 */
        int zx0 = bx + (int)((calibZoneC - CALIB_ZONE_W * zsc) * barW);
        int zx1 = bx + (int)((calibZoneC + CALIB_ZONE_W * zsc) * barW);
        BlendRectWin(zx0, by, zx1, by + barH, 0x00307A4A, 0.85f);
        int px0 = bx + (int)((calibZoneC - CALIB_PERFECT_W * zsc) * barW);
        int px1 = bx + (int)((calibZoneC + CALIB_PERFECT_W * zsc) * barW);
        BlendRectWin(px0, by, px1, by + barH, 0x00E8F0A0, 0.95f);
    }
    {
        int mx = bx + (int)(calibPos * barW);
        int mw = UIS(4);
        BlendRectWin(mx - mw / 2, by - UIS(3), mx + mw / 2, by + barH + UIS(3),
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
    static const char* names[SAFEHOUSE_PAGES] = { "SHOP", "BENCH", "TREE", "REST", "UPGRADE" };
    static const char* namesZh[SAFEHOUSE_PAGES] = { "商店", "改装", "改造树", "休息", "强化" };

    int th = TextHeightWin();
    int x0, y0, x1, y1;
    PageTabRect(0, &x0, &y0, &x1, &y1);

    /* 左侧竖分隔线：把 sidebar 和内容区切开 */
    int sepX = x1 + UIS(10);
    BlendRectWin(sepX, y0 - UIS(8), sepX + 1, winH - UIS(40), 0x00C8B060u, 0.45f);

    for (int i = 0; i < SAFEHOUSE_PAGES; i++) {
        PageTabRect(i, &x0, &y0, &x1, &y1);
        int active = (shopPage == i);
        int hover  = (safehouseHoverTab == i);

        BlendRectWin(x0, y0, x1, y1,
                     active ? 0x00C8A040 : (hover ? 0x00303838 : 0x00161C1C),
                     active ? 0.95f : 0.85f);
        FrameRectWin(x0, y0, x1, y1,
                     active ? PAL_GOLD : (hover ? PAL_CYAN : PAL_DIM),
                     0.9f, active ? 2 : 1);

        /* 选中时左侧加一根粗竖条，扫一眼就知道在哪一页 */
        if (active) {
            BlendRectWin(x0 - UIS(6), y0, x0 - UIS(1), y1, 0x00FFE070u, 0.95f);
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
    FrameRectWin(p.panel.left, p.panel.top, p.panel.right, p.panel.bottom, PAL_PANEL, 0.95f, UIS(2));

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
    return SkillSwitchable(id) ? 1 : 0;
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
#define SK_K_MIN   0.45f
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
    int nodeW = (int)(SK_WX * 0.55f * SK_NODE_K);
    int nodeH = (int)(SK_WY * 0.62f * SK_NODE_K);
    skVX = (float)winW * 0.50f - (wx * skVK + (float)nodeW * 0.5f);
    skVY = (float)winH * 0.42f - (wy * skVK + (float)nodeH * 0.5f);
    uiDirty = 1;
}

void SkillResetView(void) {
    /* v6：**不再是「把树丢回世界原点」** ——
       那是原来 HOME 的行为，按一下整棵树跑到屏幕左上角。
       现在 HOME = 回到当前选中的技能，缩放回到 0.85。 */

    skVK = 0.78f;
    SkillCenterOn(hubSelection);
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
                          which == 0 ? "+" : "-", 0x00E8C040u);
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
    return (dx < 4.0f && dy < 4.0f) ? 1 : 0;
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
    FrameRectWin(x, y, x + w, y + h, 0x00E8C040u, 0.95f * fade, UIS(2));

    /* 四角短标：像素面板的标志性细节 */
    {
        int arm = UIS(12), t = UIS(2);
        unsigned int cc = 0x00FFD040u;
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
    DrawTextWin(leftX, curY, Tr("THE EXIT IS OPEN", "出口已开"), 0x00FFD040u);
    curY += ROW * 2;          /* 标题后空一行 */

    /* ---- 选项 1：撤离 ---- */
    {

        int quit=BankPreviewGain(1);

        char kb[16], lb[64], vb[48];
        snprintf(kb, sizeof(kb), "[1]");
        snprintf(lb, sizeof(lb), "%s", Tr("LEAVE NOW", "现在撤离"));
        snprintf(vb, sizeof(vb), "+%d GOLD", quit);

        DrawTextWin(leftX, curY, kb, 0x00E8C040u);
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

        DrawTextWin(leftX, curY, kb, 0x00E8C040u);
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
        int nodeW = (int)(SK_WX * 0.55f * SK_NODE_K);
        int nodeH = (int)(SK_WY * 0.62f * SK_NODE_K);
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


        /* 连线先画（在节点下面）
           v6：**改成读邻接表** —— 原来只画 d->prereq 一条线，
           新增的联动节点（两个邻居）根本画不出来。
           现在遍历每个节点的所有邻居，有几个画几条。 */
        for (int k = 0; k < SKILL_LIST_COUNT; k++) {
            int id = kSkillList[k];
            const SkillDef* d = &skillDefs[id];
            (void)d;
            if (SkillEdgeCount(id) <= 0) continue;
            float wx1, wy1, wx2, wy2;
            SkillWorldPos(SkillEdgeNeighbor(id, 0), &wx1, &wy1);
            SkillWorldPos(id, &wx2, &wy2);
            int x1 = SkSX(wx1) + nodeW / 2, y1 = SkSY(wy1) + nodeH;
            int x2_ = SkSX(wx2) + nodeW / 2, y2 = SkSY(wy2);
            /* ============================================================
             * v6 第 ㉖ 轮：**连线重画**
             * ============================================================
             * 原来：颜色 0x484848、宽度 2px、alpha 0.85，还分成两段 L 形。
             * 实测截图：整张网看起来是散的 —— 节点之间像没关系。
             *
             * 现在：
             *   · 已点亮 = PAL_GOLD（和节点同一个语言）
             *   · 未点亮 = 明显可见的灰蓝（不再是 0x484848）
             *   · 宽度随缩放放大（缩小时看得见）
             *   · 直角折线保留（赛博朋克电路感），但两段接得更紧
             * ============================================================ */
            int lit = (skillRank[id] > 0) && (skillRank[SkillEdgeNeighbor(id, 0)] > 0);
            unsigned int lc = lit ? PAL_GOLD : 0x00708098u;
            int lw = (int)(3.0f * skVK);
            if (lw < 2) lw = 2;
            if (lw > 8) lw = 8;
            int midY = (y1 + y2) / 2;
            BlendRectWin(x1 - lw / 2, y1, x1 + lw / 2, midY + lw / 2, lc, 0.95f);
            BlendRectWin(x2_ - lw / 2, midY - lw / 2, x2_ + lw / 2, y2, lc, 0.95f);
            BlendRectWin(x1 - lw / 2, midY - lw / 2, x2_ + lw / 2, midY + lw / 2, lc, 0.95f);
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
            if (y1 < gTop || y0 > gBot) continue;

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
            else if (canBuy)  bd = (((int)(g_timeSec * 2.0f) & 1)) ? PAL_GOLD : 0x008A7830u;
            else              bd = 0x008090A0u;      /* 明显可见的灰蓝，不再是 0x404040 */

            BlendRectWin(x0, y0, x1, y1, fill, 0.92f);
            FrameRectWin(x0, y0, x1, y1, bd, 0.95f, isSel ? 3 : 2);

            /* 名字（缩得太小时只画名字） */
            {
                char nm[40];
                /* v6：节点里画短名，详情条才用全长名 */
                { 
                  sprintf(nm, "%s", SkillShortName(id)); }
                /* ============================================================
                 * 修：**节点名被截成 3 个字**
                 * ============================================================
                 * 节点框约 80px 宽，而文字按窗口缩放（scale≈2）算宽度，
                 * 一个字符 16px → 只能放 3 个，于是 "QUIET LID" 变 "QUI.."。
                 * 文件名是玩家唯一能认出这是哪个节点的线索，截成这样没有用。
                 *
                 * 改用 1x 小字（每个字符 8px），同样宽度能放 8~10 个字符。
                 * ============================================================ */
                unsigned int tc = owned ? 0x0090FFB0u : (canBuy ? 0x00FFE080u : 0x00787878u);
                {


                    int sw = DrawTextSmallWidth(nm);
                    if (sw > nw2 - 4) ClampRowText(nm, nw2 - 4);
                    sw = DrawTextSmallWidth(nm);
                    DrawTextSmallWin((x0 + x1) / 2 - sw / 2,
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
                FrameRectWin(x0 - 5, y0 - 5, x1 + 5, y1 + 5, 0x00FFE080u, 0.95f, 2);
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

        sprintf(b, "%s   [%s]", Tr(d->name, SkillNameZh(d->name)),
                Tr(kBranchName[d->branch], kBranchNameZh[d->branch]));
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

        {   /* 第二行：描述左，价格右 */
            static char cb[32];
            int cw = 0;
            if (costNow >= 0) {
                sprintf(cb, "%s %d", Tr("COST", "价格"), costNow);
                cw = TextWidthWin(cb) + UIS(16);
                DrawTextWin(innerR - TextWidthWin(cb), dy + TH, cb, PAL_GOLD);
            }
            sprintf(b, "%s", d->desc);
            ClampRowText(b, innerR - innerL - cw);
            DrawTextWin(innerL, dy + TH, b, 0x008A8A8Au);
        }

        {
            const char* st; unsigned int sc;
            if (skillRank[sel] >= d->maxRank) { st = Tr("ALREADY MAXED", "已满级"); sc = 0x0060FF90u; }
            else if (!(d->prereq < 0 || skillRank[d->prereq] > 0)) { st = Tr("REQUIRES THE NODE ABOVE IT", "需要先点亮上面的节点"); sc = 0x00FF8080u; }
            else if (!WalletCanPayBank(costNow)) { st = Tr("NOT ENOUGH GOLD", "金币不够"); sc = 0x00FF8080u; }
            else { st = Tr("ENTER TO BUY", "回车购买"); sc = 0x0060FF90u; }
            DrawTextWin(innerL, dy + TH * 2, st, sc);
        }
    }
    /* 2026-11-08：标题放到最上层 —— chrome 永远在内容之上 */
    DrawTitleWin("SKILL TREE", L.titleY, 0x00D23C3Cu);

    DrawTextCenterWin((L.panel.left + L.panel.right) / 2, L.panel.bottom - TH,
                      /* ================================================
                       * 2026-11-08（实机反馈）：**这行越界 72px**
                       * ------------------------------------------------
                       * 实测 dump：`R x2 RESET / WHEEL ZOOM ...` 是 x=-72..872，
                       * 画布只有 800 —— 左右各被裁掉一截。
                       * 这里按可用宽度自动降级到短版本（信息没删，只是缩写）。
                       * ================================================ */
                      (TextWidthWin(Tr("R x2 RESET / WHEEL ZOOM   DRAG PAN   HOME RESET   ENTER BUY",
                                       "滚轮缩放   拖拽平移   HOME 复位   回车购买"))
                       > (L.panel.right - L.panel.left) - UIS(16))
                      ? Tr("R RESET - WHEEL ZOOM - ENTER BUY",
                           "R 复位 - 滚轮缩放 - 回车购买")
                      : Tr("R x2 RESET / WHEEL ZOOM   DRAG PAN   HOME RESET   ENTER BUY",
                           "滚轮缩放   拖拽平移   HOME 复位   回车购买"),
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
                          Tr("BACK", "返回"), 0x00E8C040u);
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
            skVK = 0.78f;
            SkillCenterOn(hubSelection);
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
        DrawTextWin(innerR - TextWidthWin(b), L.curY, b, 0x00FFC040u);
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
    int nodeW = (int)(SK_WX * 0.55f * SK_NODE_K);
    int nodeH = (int)(SK_WY * 0.62f * SK_NODE_K);
    float cx = wx * skVK + (float)nodeW * 0.5f;
    float cy = wy * skVK + (float)nodeH * 0.5f;
    float tx = (float)winW * 0.50f;
    float ty = (float)winH * 0.42f;
    int moved = 0;
    if (cx < (float)winW * 0.30f || cx > (float)winW * 0.70f) { skVX += tx - cx; moved = 1; }
    if (cy < (float)winH * 0.24f || cy > (float)winH * 0.60f) { skVY += ty - cy; moved = 1; }
    if (moved) uiDirty = 1;
}

RECT SkillNodeRect(int id) {
    RECT r={0,0,0,0}; if(id<0 || id>=SKILL_COUNT) return r;
    float wx,wy;SkillWorldPos(id,&wx,&wy);
    r.left=SkSX(wx);r.top=SkSY(wy);
    r.right=r.left+(int)(SK_WX*0.55f*skVK);r.bottom=r.top+(int)(SK_WY*0.62f*skVK);return r;
}
int SkillNodeHitTest(int mx, int my) {
    /* v6：**逆变换命中** —— 屏幕坐标先反解回世界坐标，再判矩形。 */
    int TH = TextHeightWin();
    int nodeW = (int)(SK_WX * 0.55f * SK_NODE_K);
    int nodeH = (int)(SK_WY * 0.62f * SK_NODE_K);
    if (nodeW < 24) nodeW = 24;
    if (nodeH < 18) nodeH = 18;
    (void)TH;
    for (int k = 0; k < SKILL_LIST_COUNT; k++) {
        int id = kSkillList[k];
        float wx, wy;
        SkillWorldPos(id, &wx, &wy);
        int x0 = SkSX(wx), y0 = SkSY(wy);
        if (mx >= x0 && mx <= x0 + nodeW && my >= y0 && my <= y0 + nodeH)
            return id;
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
    FrameRectWin(left, top, right, bottom, PAL_PANEL, 0.95f, UIS(2));
    DrawTitleWin("REBIND", top + pad, 0x00E65A5Au);
    DrawTextCenterWin(winW / 2, top + pad + titlePx + UIS(24), actionNames[rebindingAct], 0x00A0FFA0u);
    if (rebindConflict >= 0 && rebindConflict < ACT_COUNT) {
        /* 冲突确认：告诉玩家会顶掉哪个动作的绑定，再按一次才生效 */
        char cbuf[96];
        sprintf(cbuf, "%s USES THIS KEY", actionNames[rebindConflict]);
        DrawTextCenterWin(winW / 2, bottom - pad - TextHeightWin() * 2, cbuf, 0x00FFC040u);
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
static void DrawCrosshair(void) {
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
static void DrawHudThemeRoom(void) {
            /* ==================================================================
             * 2026-11-06（用户要求）：**右上角显示当前主题房间**
             * ------------------------------------------------------------------
             * 用户原话：「右上角新增一行字幕，告诉我当前的主题房间是什么」。
             *
             * 只在玩家**站在房间内部**时显示（roomId != 0）——
             * 站在走廊里写"砖墙"没有意义，反而像是坏了。
             * 画在敌人状态那一行**上面**，两者互不遮挡。
             * ================================================================ */


                int cxp = (int)posX, cyp = (int)posY;
                if (cxp >= 0 && cyp >= 0 && cxp < MAP_WIDTH && cyp < MAP_HEIGHT &&
                    cellMeta[cyp][cxp].roomId != 0) {
                    const char* tn = ThemeName((int)cellMeta[cyp][cxp].themeId,
                                               g_lang == LANG_ZH);
                    if (tn && tn[0]) {
                        int tw4 = TextWidthWin(tn);
                        DrawTextWin(winW - UIS(24) - tw4, winH - UIS(394) - UIS(30),
                                    tn, 0x00C090E0u);
                    }
                }
}

typedef struct HudBarLayout {
    int barW, barH, labelX, barX, rowGap, y1, y2;
} HudBarLayout;

static HudBarLayout HudBuildBarLayout(void) {
    /* 手电电量 / 体力：左下两条细条，标签按实际文字宽度排版 */
    int barW = UIS(180), barH = UIS(12);
    int labelX = UIS(20);
    /* v3.0：能量条已删，只剩 LIGHT / RUN 两条，按最宽标签算起点 */
    int barX = labelX + TextWidthWin("LIGHT") + UIS(14);
    /* v1.9：两行标签原来固定差 UIS(22)，而一行文字就有 30 逻辑像素高，
       于是 LIGHT 和 RUN 叠在一起（截图里能看到两个词压成一团）。
       现在行距 = 实际文字高度 + 一点间隙，任何窗口尺寸都不会重叠。 */
    int rowGap = TextHeightWin() + UIS(8);
    int y1 = winH - UIS(60) - rowGap;
    int y2 = y1 + rowGap;


    HudBarLayout layout = { barW, barH, labelX, barX, rowGap, y1, y2 };
    return layout;
}

static void DrawHudRunHeader(int effDetail) {
    char buf[96];

    /* ------------------------------------------------------------------ */
    /* HUD 详细度                                                        */
    /* ------------------------------------------------------------------ */
    /* 三档（设置里的 HUD Detail）：
         0 Minimal —— 只留必须实时知道的：数值、电量体力、状态效果
         1 Normal  —— 加：投掷物数量（有库存时才显示）、SAFE ZONE 短提示
         2 Full    —— 加：投掷物数量常驻、SAFE ZONE 常驻
       词条横幅在任何档位都只在进关后 5 秒出现（levelBanner 控制），
       因为它是「一次性信息」，常驻反而是噪音。 */

    /* ==================================================================
     * 2026-11-08（第 10 轮）：**HUD 精简**
     * ------------------------------------------------------------------
     * 玩家原话：「左上角太多数值了，新手看不懂」。
     * 常驻只留 4 项（LEVEL / GOLD / SANITY / THREAT>0），
     * 按住 Tab 临时展开完整 HUD —— 信息没删，只是默认不糊在脸上。
     * ================================================================ */

    int goldRowEnd = 0;   /* v2.4.90 顶行金币栏的右边界 */
    sprintf(buf, Tr("LEVEL %d", "第 %d 层"), currentLevel);
        if (campaignMode) {
            char cb[64];
            sprintf(cb, Tr("  CAMPAIGN %d/5 %s", "  战役 %d/5 %s"),
                    campaignStage + 1, campaignName(campaignStage));
            strncat(buf, cb, sizeof(buf) - strlen(buf) - 1);
        }
        if (dailyMode) {
            char db[48];
            sprintf(db, Tr("  DAILY %d", "  每日 %d"), dailySeed % 10000);
            strncat(buf, db, sizeof(buf) - strlen(buf) - 1);
        }
    DrawTextWin(UIS(20), UIS(18), buf, 0x0000FF00u);

    /* ===== v2.4.90 **LEVEL 右边显示当前持有的黄金** =====
       用户要求：「在 level one 的右边新增 1 个 UI 显示当前持有的黄金数量。」
       放在 LEVEL 同一行紧右边，不新增行 ——
       顶部每一行都很贵，能并到既有行就并。

       格式：GOLD 12345
       颜色：暖金 0xFFC040（和 LEVEL 的绿色、词条的橙色都区分开）
       宽度不够时退化成 G12.3k 这种缩写。 */
    {

        static char gbuf[48];
        int gx0 = UIS(20) + TextWidthWin(buf) + UIS(16);

        if (carryGold >= 1000000)
            snprintf(gbuf, sizeof(gbuf), "GOLD %.1fM", (double)carryGold / 1000000.0);
        else if (carryGold >= 10000)
            snprintf(gbuf, sizeof(gbuf), "GOLD %.1fk", (double)carryGold / 1000.0);
        else
            snprintf(gbuf, sizeof(gbuf), Tr("GOLD %d", "黄金 %d"), carryGold);

        /* 放不下就缩成 G12345 */
        if (gx0 + TextWidthWin(gbuf) > winW - UIS(12)) {
            snprintf(gbuf, sizeof(gbuf), "G%d", carryGold);
        }
        DrawTextWin(gx0, UIS(18), gbuf, 0x00FFC040u);

        /* ===== v6：金币右边再挂一个 THREAT =====
           威胁值是这套设计的核心反馈 —— 玩家必须随时看到
           "我点得越多，怪越凶"。所以它和金币同一行，紧挨着。

           颜色：正常灰青；超过 12 时用品红呼吸（警告） */
        {
            static char tbuf[32];
            float tv = ThreatValue();
            int tx0 = gx0 + TextWidthWin(gbuf) + UIS(14);
            /* 2026-11-08：**威胁为 0 时不显示** —— 开局那个 THREAT 0.0
               对新玩家没有任何决策价值，还占着第一行。
               注意：这里**不能 return**（那会把整个 DrawHUD 退掉，
               SANITY / FUSES / 状态条全都不画了 —— 实测截图为证）。 */
            if (tv <= 0.0f) {
                goldRowEnd = tx0;
            } else {
            snprintf(tbuf, sizeof(tbuf), "THREAT %.1f", (double)tv);
            {
                /* v6 铁律十九：三态全部来自调色盘
                     THREAT = 0   → PAL_DIM（灰，无威胁）
                     0 < T < 12   → PAL_CYAN（青，正常）
                     T >= 12      → PAL_MAGENTA（品红，警告）
                   "品红 = 警告" 是所有页面共用的语言。 */
                unsigned int tc;
                if (tv >= 12.0f)      tc = PAL_MAGENTA;
                else if (tv > 0.0f)   tc = PAL_CYAN;
                else                  tc = PAL_DIM;
                DrawTextWin(tx0, UIS(18), tbuf, tc);
            }
            goldRowEnd = tx0 + TextWidthWin(tbuf) + UIS(14);
            }   /* end tv > 0 */
        }
    }

    /* 当前词条：常驻显示在 LEVEL 那一行的右边 —— **不新增一行**。
       词条是难度曲线的一部分（HUNTED 怪更快、LEAKY 理智掉更快），
       玩家看不见就不知道自己在被什么惩罚。
       用暗橙色区分于绿色的 LEVEL。
       详细说明（"THE THING MOVES FASTER"）仍然只在进关 5 秒的横幅里出现，
       常驻的是「提醒」，横幅是「解释」。 */
    /* 2026-11-08：**hudDetail 三档真正生效**。
       词条改变规则（怪更快 / 理智掉更快），Normal 起显示。 */
    if (effDetail >= 1 && levelMod >= 0 && levelMod < MOD_COUNT) {
        int lx = goldRowEnd;
        sprintf(buf, "%s", modNames[levelMod]);
        DrawTextWin(lx, UIS(18), buf, 0x00C08040u);
    }
    /* v1.8：本层的随机事件（通过本层会给本局加分） */
    /* v1.8：事件**不再单独占一行**。
       原来画在 y=UIS(36)，而文字高度在小窗口下就有 30 多逻辑像素，
       结果和 LEVEL / SANITY 叠在一起（截图里就是这样）。
       现在并到首行右侧，跟词条同一行，永远不会撞。 */
    if (effDetail >= 2 && levelEvent >= 0 && levelEvent < EVENT_COUNT) {
        int ex = goldRowEnd;
        if (levelMod >= 0 && levelMod < MOD_COUNT) {
            char mb[32];
            sprintf(mb, "%s", modNames[levelMod]);
            ex += TextWidthWin(mb) + UIS(14);
        }
        /* 宽度不够时自动缩写，别让文字被窗口裁掉 */
        /* ==================================================================
         * v7 第 61 轮：**EventNameZh / EventDescZh 全工程 0 调用点**
         * ==================================================================
         * 中文界面下事件横幅永远是英文名，而且**每一层的规则说明
         * 从来没有显示过** —— 玩家只能靠死来学"这层到底怎么了"。
         *
         * 现在：中文用中文名；横幅放不下时，把说明塞进底部提示行。
         * ================================================================ */


        int avail = winW - ex - UIS(12);
        {
            const char* en = (g_lang == LANG_ZH) ? EventNameZh(levelEvent) : EventNameEn(levelEvent);
            sprintf(buf, "EVENT: %s +%d", en, EventScore(levelEvent));
        }
        if (TextWidthWin(buf) > avail) {
            sprintf(buf, "EV: %s +%d", EventShortEn(levelEvent), EventScore(levelEvent));
            if (TextWidthWin(buf) > avail) sprintf(buf, "+%d", EventScore(levelEvent));
        }
        DrawTextWin(ex, UIS(18), buf, 0x00E060C0u);

        /* v7 第 62 轮：CampaignRule 全工程 0 调用点 —— 每关改的规则从没告诉过玩家 */
        {


            if (campaignMode && levelBanner > 3.0f) {
                const char* r = (g_lang == LANG_ZH) ? CampaignRuleZh(campaignStage)
                                                    : CampaignRule(campaignStage);
                if (r && r[0]) {
                    char rb[180];
                    snprintf(rb, sizeof(rb), "%s", r);
                    ClampRowText(rb, winW - UIS(32));
                    DrawTextCenterWin(winW / 2, winH / 2 + UIS(40), rb, 0x0060D0FFu);
                }
            }
        }

        /* 规则说明：只在**本层刚开始的 6 秒**显示，不长期占屏 */
        {

            if (levelBanner > 3.0f) {
                const char* desc = (g_lang == LANG_ZH) ? EventDescZh(levelEvent)
                                                       : EventDescEn(levelEvent);
                if (desc && desc[0]) {
                    char db[160];
                    snprintf(db, sizeof(db), "%s", desc);
                    ClampRowText(db, winW - UIS(32));
                    DrawTextCenterWin(winW / 2, winH / 2 + UIS(70), db, 0x00E060C0u);
                }
            }
        }
    }

    /* v1.8：待上缴金币 + 绣刀冷却。
       注意：右上角 FPS 那一行也占着 y=UIS(18)，所以这两行要排在它下面，
       否则开 FPS 时会叠在一起。 */
    {
        /* v3.0：PENDING 行删掉后 hy 没人用了，显式标记避免编译告警。 */
        /* ==================================================================
         * 2026-11-05 修 UI 重叠：**日游行和 LEVEL 行画在同一个 y**
         * ------------------------------------------------------------------
         * 左边 HUD 的纵向分配是：
         *     UIS(18)                    LEVEL / CAMPAIGN / DAILY 种子  (3447)
         *     UIS(18)                    DAILY 进度 + 目标文本          (本块) ← 重叠
         *     hudY1 = 18 + 1*(TH+4)      SANITY
         *     hudY2 = 18 + 2*(TH+4)      FUSES
         *     hudY3 = 18 + 3*(TH+4)      CARRY / TIER
         *     18 + 4*(TH+4)              BOOST
         *
         * 两行都从 x≈12 起、青色和绿色几乎同色，叠起来就是一坨乱码
         * （用户截图：DAILY 0/15 和 LEVEL 那行糊在一起）。
         *
         * 现在把日游行排到整叠之后：18 + 5*(TH+4)。
         * ================================================================ */
        int hy = UIS(18) + 5 * (TextHeightWin() + UIS(4));
        if (showFps) hy += TextHeightWin() + UIS(4);
        (void)hy;
(void)hy;

        /* ==================================================================
         * v6 第 ㊶ 轮：**每日任务在局内终于可见了**
         * ==================================================================
         * daily.c 的 DailyCurrent / DailyGoalParam / DailyDone 一直是死代码 ——
         * 玩家只在开局看到一行提示，之后完全不知道进度到哪。
         * 这里用 PENDING 行删掉后腾出的位置，不占新空间。
         * ================================================================ */
        if (dailyMode) {

            if (DailyDone()) {
                sprintf(buf, "DAILY  COMPLETE");
                DrawTextWin(UIS(12), hy, buf, PAL_GOLD);
            } else {
                int cur = DailyCurrent(), goal = DailyGoalParam();
                int avail = winW - UIS(24);
                /* v7 第 61 轮：**DailyGoalTextZh 全工程 0 调用点**
                   中文界面下每日任务全程英文，玩家读不懂自己该干什么。 */
                sprintf(buf, "DAILY %d/%d  %s", cur, goal,
                        (g_lang == LANG_ZH) ? DailyGoalTextZh() : DailyGoalTextEn());
                if (TextWidthWin(buf) > avail) sprintf(buf, "DAILY %d/%d", cur, goal);
                DrawTextWin(UIS(12), hy, buf, PAL_CYAN);
            }
        }        /* v3.0：**删掉 HUD 上的 PENDING N** ——
       玩家实测「一整个小时都是 PENDING 12」，既看不懂又永远不变。
       待上缴金币只在安全屋/结算页显示就够了。 */

    }

    {
        int dg = 0;
        for (int i = 0; i < LEECH_MAX; i++) if (leeches[i].active && leeches[i].draining) dg = 1;
        if (dg) DrawTextCenterWin(winW / 2, UIS(18), "DRAIN", 0x00FF3030u);
    }

}

static void DrawHudVitalsAndStatus(int effDetail) {
    char buf[96];
    /* v2.9：左列改用文字高累加，不再写死 18/52/86 ——
       1080p 下原来 LEVEL 15..80 和 SANITY 44..109 压 36px（报告 严重 #3）。 */
    int hudTH = TextHeightWin();
    /* v2.9：**左上角暗底已彻底删除**（用户要求）。
       那块半透明黑边界很硬，在左上角看起来像一块阴影/渲染错误。
       对比度已由文字颜色本身解决（都提到 4.8:1 以上），不再需要底衬。 */
    int hudY1 = UIS(18) + hudTH + UIS(4);
    sprintf(buf, "SANITY: %.0f%%", sanity);
    DrawTextWin(UIS(20), hudY1, buf, (sanity < 30.0f) ? 0x00FF3C3Cu : 0x0000FFFFu);
    int hudY2 = hudY1 + hudTH + UIS(4);

    /* 2026-11-08：Minimal 档只留 4 项（LEVEL/GOLD/SANITY/THREAT）。
       保险丝进度从 Normal 起显示。 */
    if (effDetail >= 1) {
        if (breakerOpen) sprintf(buf, "POWER ON");
        else sprintf(buf, "FUSES %d/%d", fusesHeld, fusesNeeded);
        DrawTextWin(UIS(20), hudY2, buf, breakerOpen ? 0x0030FF60u : 0x00E8C040u);
    }

    /* ===== v3.4 左上角新增两行（用户要求）=====
       ① 带入金币 —— 这局手上还有多少可花
       ② 强度加成 —— 携带额占历史最高的百分比，
                      直接对应银行结算的倍数（倍数 = 1 + 这个值）
       放在 FUSES 下面，和上面三行同一列。 */
    {

        int hudY3 = hudY2 + hudTH + UIS(4);
        /* ===== v2.4.30 **新怪状态行（只保留能改变玩家决定的）** =====
           用户问「这行字有没有实用价值，没有就删掉」，判断标准定成一条：

             **这条状态出现时，玩家会因此做一件不同的事吗？**

               会  → 留
               不会 → 删

           按这条标准砍掉三条：

             SCAVENGER APPROACHING   删 —— 生成时已经有「有东西在靠近…」，
                                          而且它会自动找过来，知道了也做不了什么
             THE COLLECTOR IS NEAR   删 —— 同上，纯粹是「有个东西存在」的信息
             WARDEN PATROLLING       删 —— 巡逻状态不影响任何操作，纯噪音

           保留两条，因为它们分别对应一个**具体动作**：

             WARDEN BLOCKS EXIT      留 —— 出口被堵，要么等 25 秒，要么绕开
             SCAVENGER RIFLING ...   留 —— 你的箱子正在被吃掉，赶紧回去
        */
        {

            const char* st = NULL;
            unsigned int stc = 0x00D0D0D0u;

            if (WardenActive() && WardenBlocking()) {
                st = Tr("WARDEN BLOCKS EXIT", "守门人堵住了出口"); stc = 0x00FF6050u;
            } else if (ScavengerRifling()) {
                st = Tr("SCAVENGER RIFLING YOUR CRATES", "清道夫在翻你的箱子"); stc = 0x00FFB040u;
            }
            if (st) {
                int sw = TextWidthWin(st);
                DrawTextWin(winW - UIS(20) - sw, hudY3 + hudTH * 3 + UIS(10), st, stc);
            }
        }

        /* v7：丢钱滑条（按住 K 时展开） */
        {

            if (moneyDropOpen) {
                int damt = (int)((long long)carryGold * moneyDropPct / 100);
                char sb2[128];
                sprintf(sb2, Tr("DROP %d%%  =  %d GOLD   LEFT/RIGHT   RELEASE K",
                                "丢钱 %d%%  =  %d 金币   ←/→ 调整   松开 K 丢出"),
                        moneyDropPct, damt);
                DrawTextWin(UIS(20), hudY3 + hudTH * 2 + UIS(10), sb2, 0x00FFD040u);
            }
        }
        /* ==================================================================
         * 2026-11-08：**CARRY / TIER / BOOST 只在 Full 档显示**
         * ------------------------------------------------------------------
         * 这三项对新手没有任何决策价值：「CARRY 0  TIER 0  BOOST +0%」
         * 是纯噪音，而它们正好占了左上角两行。
         * 老玩家想要就开 Full —— 信息没删，只是默认不糊在脸上。
         * ================================================================ */
        if (effDetail >= 2) {
            int tier = GoldTier();
            sprintf(buf, Tr("CARRY  %d   TIER %d", "带入  %d   强度档 %d"), carryGold, tier);
            /* 档位越高颜色越烫：0~4 灰 / 5~9 黄 / 10~15 红 */
            unsigned int tc = (tier >= 10) ? 0x00FF6050u
                            : (tier >= 5  ? 0x00FFC040u : 0x00A0A0A0u);
            DrawTextWin(UIS(20), hudY3, buf, tc);
        }

        if (effDetail >= 2) {
            int boost = 0;
            if (bankHistMax > 0)
                boost = (int)((float)carryGold * 100.0f / (float)bankHistMax + 0.5f);
            if (boost > 100) boost = 100;
            sprintf(buf, Tr("BOOST  +%d%%", "强度  +%d%%"), boost);
            DrawTextWin(UIS(20), hudY3 + hudTH + UIS(4), buf,
                        boost >= 100 ? 0x0060FF90u
                                     : (boost >= 50 ? 0x00FFD040u : 0x00909090u));
        }   /* end hudDetail >= 2 */
    }

    /* ===== v2.4.56 **扳手状态**（用户要求补 UI）=====
       扳手是「打断拾荒者 → 抢回赃款」的唯一手段，
       但界面上从来没告诉玩家「还剩几次 / 冷却多久」。
       放在 BOOST 下面一行，和左侧 HUD 同一列。 */
    {



        /* ===== v2.4.65 **等级 0 也要显示** =====
           扳手在等级 0 就已经能用（2 次/层），
           我原来写了 `if (lv > 0)` —— 于是默认状态下这一行**永远不显示**，
           用户看到的就是「没有次数、没有功能」。 */
        {
            char wb[64];
            float cdMax = RunUpgWrenchCooldown();
            unsigned int wc = 0x00A0A0A0u;

            if (cdMax < 0.0f) {
                int left = wrenchCharges - wrenchLastUsed;
                if (left < 0) left = 0;
                snprintf(wb, sizeof(wb), Tr("WRENCH  %d/%d", "扳手  %d/%d"),
                         left, wrenchCharges);
                wc = (left > 0) ? 0x0060D0FFu : 0x00806060u;
            } else if (wrenchCd > 0.0f) {
                snprintf(wb, sizeof(wb), Tr("WRENCH  %.1fs", "扳手  %.1f秒"), wrenchCd);
                wc = 0x00806060u;
            } else {
                snprintf(wb, sizeof(wb), "%s", Tr("WRENCH  READY", "扳手  就绪"));
                wc = 0x0060D0FFu;
            }
            /* ============================================================
             * v6 第 ㊼ 轮：**这一行压在别的东西上**
             * ============================================================
             * 用户实机截图：左上角出现一坨叠字
             *   「WE0NHCA2F2」= "WRENCH 2/2" 和另一行文字糊在一起
             *
             * 原因：这里用**绝对行号 7** 算 y
             *   UIS(18) + (TH+4)*7 + 4
             * 而 HUD 其余部分用的是 hudTH 的累加。
             * 窗口稍微一变，第 7 行就落到状态效果 / 提示文字上面。
             *
             * 现在改成**贴着底部的 LIGHT 条往上放** —— 那块区域永远是空的。
             * ============================================================ */
            {
                DrawTextWin(UIS(20), winH - UIS(140), wb, wc);
            }
        }
    }

    /* ==================================================================
     * 守门人**锁定倒计时**（2026-11-03）
     * ------------------------------------------------------------------
     * 他看见你之后不再立刻开追，而是先亮 1.5 秒。
     * 这个数字必须显眼 —— 它是玩家唯一能读到的"还有多久挨抓"。
     * 放在顶部正中，配一条消耗中的进度条。
     * ================================================================ */
    {
        if (WardenSpotting()) {
            char cb[32];
            sprintf(cb, "%.1f", (double)WardenSpotLeft());
            int cxp = winW / 2;
            int yb  = UIS(54);                 /* 顶部，别压 CANITY HUD 第一行 */

            /* 文字：SPOTTED 0.9 */
            char line[48];
            sprintf(line, "%s  %s", "SPOTTED", cb);
            DrawTextCenterWin(cxp, yb, line, 0x00FF5030u);

            /* 进度条：从满到空 */
            {
                int barW = UIS(180), barH = UIS(6);
                int x0 = cxp - barW / 2, y0 = yb + UIS(26);
                float f = WardenSpotFrac();
                BlendRectWin(x0, y0, x0 + barW, y0 + barH, 0x00301810u, 0.75f);
                int fill = (int)((float)barW * f);
                if (fill > 0)
                    BlendRectWin(x0, y0, x0 + fill, y0 + barH, 0x00FF4020u, 0.95f);
                FrameRectWin(x0, y0, x0 + barW, y0 + barH, 0x00FF8060u, 0.85f, 2);
            }
        }
    }

    /* 状态效果：常驻显示在 FUSES 下面。
       玩家必须知道自己被怎么了 —— 否则「视野突然变窄」会被当成 bug。
       纵向排布（UIS 是设计分辨率，600 高）：
         86  FUSES
         116 状态效果 1   142 状态效果 2   168 状态效果 3
         196 FLARE / BOTTLE */
    {
        static const char* fxName[FX_COUNT] = { "BLINDED", "SLOWED", "DRAINING" };
        /* v2.4.36：**按真实行距算** ——
           HUD 用的是 hudTH + UIS(4) 当行距，不是 TextHeightWin()。
           我上一版按 TextHeightWin()*5 算，比真实高度小，
           所以 SLOWED 还是压在 BOOST 上（用户截图）。
           现在直接跟 BOOST 那一行对齐：
             BOOST 在 hudY3 + hudTH + UIS(4)，效果就从下一行开始。 */
        int fy = UIS(18) + (hudTH + UIS(4)) * 6 + UIS(6);   /* 6 行 HUD 之后的下一行 */
        for (int i = 0; i < FX_COUNT; i++) {
            float s = FxStrength(i);
            if (s <= 0.0f) continue;
            /* ============================================================
             * 修：**HUD 显示的是 FX 原始强度，不是玩家真实受到的效果**
             * ------------------------------------------------------------
             * FX_SLOW 的 strength 是"归一化到 0.45 系数"的中间量：
             *     game.c: slowMul = 1 - strength * 0.45
             * 所以给 20% 减速要写 strength = 0.444，
             * 而 HUD 直接把 0.444 打成 "SLOWED 44%" —— 玩家以为被减速 44%。
             * 现在按各效果的真实换算显示。
             * ============================================================ */
            float shown = s;
            if (i == FX_SLOW)  shown = s * 0.45f;    /* 实际减速百分比 */
            if (i == FX_DRAIN) shown = s * 1.5f;     /* 实际额外消耗 */
            sprintf(buf, "%s %d%%", fxName[i], (int)(shown * 100.0f + 0.5f));
            DrawTextWin(UIS(20), fy, buf, 0x00FF6060u);
            fy += hudTH + UIS(4);
        }
    }

}

static void DrawHudInventory(void) {
    /* v1.14：**右上角**滚动道具栏。
       原来放在左侧中部，和关卡主题名（如 ICE VAULT）撞在一起。
       现在右对齐贴右上角，纵向排列，滚轮切换时平滑滚动。
       加武器只改 ITEM_SLOT_COUNT + 两个数组。 */
    {
        /* v3.3 六个槽位的显示名：光球 / 投石 / 烟雾 / 胶水 / 回声 / 诱饵 */
    /* ==================================================================
     * 2026-11-06（用户实测：训练营里找不到相位信标）
     * ------------------------------------------------------------------
     * 真因：槽位从 6 扩到 8，但这两张名字表**只写了 6 项** ——
     * 剩下两项是 NULL，HUD 一画就是空，玩家永远看不到第 7/8 个物品。
     * 我在扔东西之前根本没想到名字表也要跟着扩。
     * ================================================================ */
    static const char* slotName[ITEM_SLOT_COUNT] = {
        "FLARE", "SLING", "SMOKE", "GLUE", "ECHO", "DECOY",
        "BEACON" };
    static const char* slotNameZh[ITEM_SLOT_COUNT] = {
        "光球", "投石", "烟雾", "胶水", "回声", "诱饵",
        "相位信标" };

        static float scroll = 0.0f;
        static double lastT = 0.0;

        double nowT = g_timeSec;
        float dtS = (float)(nowT - lastT);
        if (dtS <= 0.0f || dtS > 0.30f) dtS = 0.016f;
        lastT = nowT;
        {
            /* v1.17：只给"有货的槽位"排序号，空槽不参与滚动 */

                int cArr[ITEM_SLOT_COUNT] = { flares, bottles, smokeBombs, gluePots, echoBolts,
                                             decoys, phaseBeacons };;
            int selVis = 0;
            for (int i = 0; i < ITEM_SLOT_COUNT; i++) {
                if (cArr[i] <= 0) continue;
                if (i == throwSelected) break;
                selVis++;
            }
            scroll += ((float)selVis - scroll) * (dtS * 15.0f);
        }

        int counts[ITEM_SLOT_COUNT];
        counts[0] = flares;
    counts[1] = bottles;
    counts[2] = smokeBombs;
    counts[3] = gluePots;
    counts[4] = echoBolts;
    { 
      counts[5] = decoys; counts[6] = phaseBeacons; }
    counts[5] = decoys;

        /* v3.3 **判据要覆盖全部 6 个槽位** ——
           试玩实测：「用过一次物品后，1 FLARE / 2 SLING 那一块再也不出现」。
           原来这里查的是旧的 8 个变量（bricks / acids / firePots / mirrors
           在投掷物重做后永远是 0），而**新加的 echoBolts / decoys 没算进来**。
           于是手上只剩回声弹或诱饵时，整条道具栏会消失。 */
    int anyItem = (flares > 0 || bottles > 0 || smokeBombs > 0 ||
                   gluePots > 0 || echoBolts > 0 || decoys > 0);
    int showList = (hudDetail >= 2) || (hudDetail >= 1 && anyItem);
        if (showList) {
            /* v2.6：**只显示三行** —— 上一件 / 当前 / 下一件。
               原来是有几种就铺几行，训练营里 8 种全有时铺满右半边屏幕，
               和最上面那行提示、右边的 WORD 全都压在一起。
               现在固定三行：中间是当前选中的（带高亮），上下各露出一部分。 */
            int rowH = TextHeightWin() + UIS(8);
            int margin = UIS(20);

            /* 收集"有货"的槽 */
            int vis[ITEM_SLOT_COUNT], nvis = 0, curVis = -1;
            for (int i = 0; i < ITEM_SLOT_COUNT; i++) {
                if (counts[i] <= 0) continue;
                if (i == throwSelected) curVis = nvis;
                vis[nvis++] = i;
            }
            if (nvis > 0) {
                if (curVis < 0) curVis = 0;
                /* 锚点：当前行固定在右下角，上面一行露在它上方 */
                int y0 = winH - UIS(120);

                for (int k = -1; k <= 1; k++) {
                    /* v3.0：**件数不够就不画上下行** ——
                       原来 nvis=2 时，上一行和下一行都循环取到同一件，
                       截图里 "1 FLARE x1" 出现两次。 */
                    if (nvis == 1 && k != 0) continue;
                    if (nvis == 2 && k != 0) {
                        /* 两件时：只画当前 + 另一件（各一行），不重复 */
                        if (curVis + k < 0 || curVis + k >= nvis) continue;
                    }
                    int idx = curVis + k;
                    while (idx < 0) idx += nvis;
                    while (idx >= nvis) idx -= nvis;

                    int i = vis[idx];
                    int y = y0 + k * rowH;
                    char line[48];
                    /* v3.3 **编号必须用真实槽位号，不能用压紧后的可见序号** ——
                       试玩实测：「用掉槽 1 的 FLARE 后，SLING 从槽 2 自动补到槽 1，
                       于是『按 1 选 Flare』永久失效」。
                       可见序号（idx）会随物品耗尽左移，而数字键绑的是真实槽位。 */
                    int slotNo = i + 1;
                    if (slotNo < ITEM_SLOT_COUNT) sprintf(line, "%d %s x%d", slotNo, Tr(slotName[i], slotNameZh[i]), counts[i]);
                    else             sprintf(line, "  %s x%d", Tr(slotName[i], slotNameZh[i]), counts[i]);

                    int tw = TextWidthWin(line);
                    int x = winW - margin - tw;
                    int selected = (k == 0);
                    if (!selected) {
                        /* 上下两行压暗，表示"还能切过去" */
                        DrawTextWin(x, y, line, 0x00606C6C);
                        continue;
                    }
                    BlendRectWin(x - UIS(6), y - UIS(3),
                                 x + tw + UIS(8), y + TextHeightWin() + UIS(1),
                                 0x00205030u, 0.55f);
                    BlendRectWin(x + tw + UIS(4), y - UIS(3),
                                 x + tw + UIS(7), y + TextHeightWin() + UIS(1),
                                 0x0060FF90u, 0.85f);
                    DrawTextWin(x, y, line, 0x00E8FFE0u);
                }
            }
        }
    }

}

static void DrawHudWorldHints(void) {
    char buf[96];
    /* ------------------------------------------------------------------ */
    /* 跳脸虫 QTE                                                        */
    /* ------------------------------------------------------------------ */
    /* ===== v2.4.91 **地上有钱袋 → 提示按 E 拾取** =====
       用户要求：「偷钱罐被打跑之后，地上掉金的时候要多添一行提示词，
       点 E 键拾取。」

       放在和"搜索"提示**同一个位置**（屏幕下方居中），
       所以玩家不需要学第二个位置；有搜索提示时让位给搜索。
       文字会呼吸（alpha 脉动），因为这是"现在就能做"的动作。 */
    {

        int bagAmt = MoneyBagNearestToPlayer();
        /* 2026-11-05：**拾取提示和藏身提示会画在同一行** ——
           实测截图里 "PRESS E - HIDE" 和 "PRESS E PICK UP 272 GOLD"
           叠成 "BRESE 272 HIDE"。
           藏身提示更紧急（关系到会不会被抓），所以它优先占这一行，
           钱袋提示让位。 */
        if (bagAmt > 0 && hoverKind == 0 && !(hideSpotNear >= 0 && hiddenIn < 0)) {
            char mb[80];
            sprintf(mb, Tr("PRESS %s  PICK UP  %d GOLD", "按 %s 拾取  %d 金币"),
                    VkName(g_bindings[ACT_INTERACT]), bagAmt);

            int tw2 = TextWidthWin(mb);
            int px3 = (winW - tw2) / 2;
            int py3 = winH - UIS(150);

            /* 呼吸：0.55 ~ 1.0 */
            float pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf((float)g_timeSec * 4.0f));

            /* 背板：让金色字在任何背景上都读得出来 */
            BlendRectWin(px3 - UIS(14), py3 - UIS(8),
                         px3 + tw2 + UIS(14), py3 + UIS(30),
                         0x00181008u, 0.72f);
            FrameRectWin(px3 - UIS(14), py3 - UIS(8),
                         px3 + tw2 + UIS(14), py3 + UIS(30),
                         0x00B08030u, 0.85f, 1);

            DrawTextWin(px3, py3, mb,
                        ((unsigned int)(0xC0 + (int)(0x3F * pulse)) << 16) |
                        ((unsigned int)(0x90 + (int)(0x40 * pulse)) << 8) |
                        0x20u);
        }
    }

    /* 词条横幅：只在进关后 5 秒出现。
       词条**名**已经常驻在 LEVEL 那一行右边了，所以这里只重复「说明」——
       玩家刚进关时需要知道这一层的规则具体是什么。 */
    if (levelBanner > 0.0f) {
        int y = UIS(204);   /* v2.6：左列固定带（状态效果之下） */
        if (levelMod >= 0) {
            DrawTextWin(UIS(20), y, modDesc[levelMod], 0x00C08050u);
        }
        DrawTextWin(UIS(20), y + TextHeightWin() + UIS(6), themeNames[(themeId >= 0 && themeId < THEME_COUNT) ? themeId : 0], 0x00B08040u);
    }

    if (showFps) {
        sprintf(buf, "FPS %d  %.1f/%.1fms  %dx%d", currentFps, lastRenderMs, frameMsMax, renderW, renderH);
        DrawTextWin(winW - UIS(20) - TextWidthWin(buf), UIS(18), buf, 0x00C8C8C8u);
    }

    /* v2.4.20：拾取提示**从这里移走** —— 见 DrawPickupToast()。
       原因：藏身遮罩是在 RenderFrame 末尾才画的，
       而 DrawHUD 早于它，所以躲进柜子时这条黄字会被 94% 黑遮罩盖掉
       （用户截图：「中间那条黄色的重要提示会丢失」）。
       现在它作为**最后一层**单独绘制，永远在最上面。 */

}

static void DrawHudSafeHints(void) {
    /* v2.5：站在安全区里 → 屏幕下方给一行提示（用户要求的那行字） */
    if (playerInSafe && currentState == STATE_PLAYING) {
        const char* hint = NULL;
        unsigned int hcol = 0x00E8E060u;
        static char cool[96];
        if (calibActive) {
            hint = Tr("SPACE: LOCK CALIBRATION", "空格：锁定校准");
            hcol = 0x00FFFFFFu;
        } else if (calibCooldown > 0.0f) {
            sprintf(cool, "%s %.0fs", Tr("CALIB COOLING", "校准冷却"), calibCooldown);
            hint = cool;
            hcol = 0x00C08040u;
        } else {
            hint = Tr("SPACE: START CALIBRATION", "空格：开始校准");
        }
        /* v2.6：安全区里的操作提醒放到**左下角**（RUN 条下面）——
           原来挂在屏幕中间，直接压在校准条上。 */
        {
            /* ============================================================
             * 2026-11-05：**「站在门口却没进门」要明确告诉玩家**
             * ------------------------------------------------------------
             * 安全区半径 1.4 格，进口半径 1.05 格 —— 中间那一圈里
             * 屏幕上只有「T: 商店 空格: 校准」，玩家会以为自己已经到出口了。
             * 现在这一圈里直接写「走进光柱」。
             * ============================================================ */


            const char* shopHint = (breakerOpen && !PlayerAtExit())
                ? Tr("STEP INTO THE LIGHT", "走进光柱")
                : Tr("T: SHOP    SPACE: CALIBRATE", "T：商店    空格：校准");
            int sx = UIS(20);
            int sy = UIS(424);
            int sw = TextWidthWin(shopHint);
            BlendRectWin(sx - UIS(6), sy - UIS(3), sx + sw + UIS(6),
                         sy + TextHeightWin() + UIS(3), 0x000A0E14, 0.90f);
            DrawTextWin(sx, sy, shopHint, 0x00C8B060u);
        }

        if (hint) {
            /* v2.5：挪到屏幕**最顶端居中** —— 原来在 3/4 高处会和武器行、
               道具栏、建议提示叠在一起。顶上先铺一条暗底防残影。 */
            int tw3 = TextWidthWin(hint);
            /* v2.6：限宽 —— 右侧还有道具栏，长句会横着压过去 */
            int maxW = winW - UIS(180) * 2;
            if (maxW < UIS(200)) maxW = winW - UIS(80);
            if (tw3 > maxW) { hint = Tr("SPACE: CALIBRATE", "空格：校准"); tw3 = TextWidthWin(hint); }
            int hx = (winW - tw3) / 2;
            /* v2.6：固定在**校准条正上方**那一条带里 */
            int hy = UIS(146) - TextHeightWin() - UIS(8);   /* v2.9：贴着条顶往上排 */
            BlendRectWin(hx - UIS(10), hy - UIS(4), hx + tw3 + UIS(10),
                         hy + TextHeightWin() + UIS(4), 0x000A0E14, 0.94f);
            DrawTextWin(hx, hy, hint, hcol);
        }
    }

    /* v2.5：站在安全屋里时把「回智封顶」写出来，否则玩家会以为安全屋坏了 */
    /* v2.6：这段原来画在 winH-60-2*rowGap-TH（≈310），正好压在武器行上。
       现在合并进 SAFE ZONE 那一行（262），不再单独占一条带。 */

    /* v3.0：**能量条已删除** —— 武器取消后这一整套都没意义了 */

    /* ===== v3.3：**旧的「开局四行说明」整块删除** =====
       用户要求：「旧的局内引导完全删掉，因为我们已经有引导模式了」。
       现在有 Tutorial 模式里的分步引导（TUTORIAL n/11，见 DrawTutorialPanel），
       覆盖了同样的内容而且更细；两套并存只会互相干扰
       （之前就出现过旧框的字从新面板后面透出来）。
       原来这块负责：
         · 第 1 层前 25 秒显示 4 行操作说明
         · 底部「PRESS SPACE TO DISMISS」可提前关闭
       全部移除。tutorialOff 变量还在 game.c 里（按键处理会置 1），
       但已经没有绘制点了，不影响编译。 */

}

static void DrawHudCoverAndThreat(void) {
    /* ===== v3.0 逃生向 HUD：藏身提示 + 被发现状态 =====
       怪已经打不死了，「它现在有没有在追我」是唯一需要盯的信息。 */
    {


        /* ① 藏身提示 */
        if (hiddenIn >= 0) {
            /* v3.0：**只保留左下角那一条** ——
               玩家实测中间和左下各有一条 HIDDEN，重复且抢画面中央。 */
        } else if (hiddenIn >= 0) {
            /* v3.0：藏在藏身点里 —— **两行提示**（用户要求）
               第 1 行：空格开始校准小游戏（或显示冷却剩余）
               第 2 行：T 打开商店面板 */

            int hy = winH - UIS(150);
            int lh = TextHeightWin() + UIS(4);

            if (calibCooldown > 0.0f) {
                char cb[64];
                sprintf(cb, Tr("SPACE  -  CALIBRATE  (cooling %ds)",
                               "按空格开始小游戏  (冷却 %d 秒)"),
                        (int)(calibCooldown + 0.5f));
                DrawTextWin(UIS(20), hy, cb, 0x00808080u);
            } else {
                DrawTextWin(UIS(20), hy,
                            Tr("SPACE  -  START MINIGAME",
                               "按空格开始小游戏"), 0x0060FF90u);
            }
            DrawTextWin(UIS(20), hy + lh,
                        Tr("T  -  OPEN SHOP", "T  -  打开商店"), 0x00E8C040u);
        } else if (hideSpotNear >= 0 && currentState == STATE_PLAYING) {
            /* ===== v2.4.21 **坏掉的藏身点要报修复金额** =====
               用户反馈：柜子坏了之后只提示 "NOT ENOUGH GOLD"，
               根本不知道要多少钱 —— 等于让玩家盲赌。
               现在靠近就直接把金额和你的持有量一起报出来：
                 PRESS E - REPAIR 450   (YOU HAVE 60)      ← 钱够：正常色
                 PRESS E - REPAIR 450   (SHORT 390)        ← 钱不够：红色
            */



            if (hideSpotNear < HIDESPOT_MAX && hideSpots[hideSpotNear].broken) {
                   /* 它是 int，见 game.c */
                float cost = HIDESPOT_REPAIR_BASE;
                for (int rc = 0; rc < hideRepairCount; rc++) cost *= HIDESPOT_REPAIR_STEP;
                if (cost > 99999.0f) cost = 99999.0f;

                char rb[96];
                if (hideRepairCount >= HIDESPOT_REPAIR_MAX) {
                    sprintf(rb, Tr("BROKEN  -  TOO DAMAGED TO REPAIR",
                                   "柜子坏了  -  破得太狠，修不了"));
                    DrawTextWin(UIS(20), winH - UIS(214), rb, 0x00806060u);
                } else if ((float)carryGold >= cost) {
                    sprintf(rb, Tr("PRESS E  -  REPAIR  %d   (YOU HAVE %d)",
                                   "按 E 修理  %d 金币   （你有 %d）"),
                            (int)(cost + 0.5f), (int)(carryGold + 0.5f));
                    DrawTextWin(UIS(20), winH - UIS(214), rb, 0x0060FF90u);
                } else {
                    sprintf(rb, Tr("REPAIR COST  %d   (SHORT %d)",
                                   "修复需要  %d 金币   （还差 %d）"),
                            (int)(cost + 0.5f), (int)(cost - (float)carryGold + 0.5f));
                    DrawTextWin(UIS(20), winH - UIS(214), rb, 0x00FF6050u);
                }
            } else {
                /* 2026-11-05：**单独占一行**。
                   原来和「拾取金币」提示同在 winH-150 —— 实测叠成
                   "BRESE 272 HIDE"。靠条件让位不可靠（钱袋判定和藏身
                   判定不是同一帧状态），所以直接给它们各自的行。
                   底→上：150 钱袋 / 182 长按搜索 / 214 藏身-修理。 */
                DrawTextWin(UIS(20), winH - UIS(214),
                            Tr("PRESS E  -  HIDE", "按 E 躲进去"), 0x0090D0F0u);
            }
        }

        /* ② 敌人状态（三态雷达）—— 怪不可杀后这是唯一的威胁指标 */
        if (Hunter0()->active && currentState == STATE_PLAYING) {
            const char* st = NULL;
            unsigned int sc = 0x0080FF80u;
            if (Hunter0()->state == 1)      { st = Tr("HUNTING", "追捕中"); sc = 0x00FF3020u; }
            else if (Hunter0()->state == 2) { st = Tr("SEARCHING", "搜寻中"); sc = 0x00E0A020u; }
            else                          { st = Tr("WANDERING", "游荡"); sc = 0x0080C080u; }
            int tw3 = TextWidthWin(st);
            /* v6 **让开右下角的小地图** ——
               原来在 winH-UIS(210)（约 65% 处），正好被钉到右下角的地图盖住。
               地图占右下约 40% 高度，字幕上移到 55% 以上。 */
            /* 2026-11-06：用户要求"警告状态往上移几格"。
               小地图虽然已经缩小，但状态行和小地图之间仍偏挤，
               再上移 24 像素留出呼吸空间。 */
            DrawTextWin(winW - UIS(24) - tw3, winH - UIS(394), st, sc);

    /* ==================================================================
     * 2026-11-08（第 1 轮）：**扳手命中标记**
     * ------------------------------------------------------------------
     * 玩家原话：「扳手没啥效果，打不到」。命中确实有击退，但屏幕上
     * 没有任何确认 —— 分不清"打空了"和"打中了但怪没退"。
     * 这里在准星四周画四道短线，命中后 0.22 秒内淡出。
     * ================================================================== */
    /* 第 3 轮：**挥空**也要有反馈 —— 命中用亮色，挥空用暗色。
       原来打空时屏幕不动，玩家分不清"挥了没打中"和"没挥出去"。 */
    if (wrenchHitFlashT > 0.0f || wrenchSwingFxT > 0.0f) {

        int isHit = (wrenchHitFlashT > 0.0f);
        int cx3 = winW / 2, cy3 = winH / 2;
        int len = UIS(14), gap = UIS(6);
        float a = isHit ? (wrenchHitFlashT / 0.22f)
                        : (wrenchSwingFxT / (0.14f * 1.6f));
        if (a > 1.0f) a = 1.0f;
        if (a < 0.0f) a = 0.0f;
        /* 命中 = 亮橙黄；挥空 = 灰蓝（明显不同，不糊弄得过去） */
        unsigned int mc = isHit ? 0x00FFE080u : 0x00708090u;
        /* This small HUD marker writes directly to the current UI pixel buffer. */
        {
            int pts[8][2];
            int k;
            pts[0][0] = cx3 - gap;              pts[0][1] = cy3;
            pts[1][0] = cx3 - gap - (int)(len * a); pts[1][1] = cy3;
            pts[2][0] = cx3 + gap;              pts[2][1] = cy3;
            pts[3][0] = cx3 + gap + (int)(len * a); pts[3][1] = cy3;
            pts[4][0] = cx3;                    pts[4][1] = cy3 - gap;
            pts[5][0] = cx3;                    pts[5][1] = cy3 - gap - (int)(len * a);
            pts[6][0] = cx3;                    pts[6][1] = cy3 + gap;
            pts[7][0] = cx3;                    pts[7][1] = cy3 + gap + (int)(len * a);
            for (k = 0; k < 8; k++) {
                int px = pts[k][0], py = pts[k][1];
                if (px < 0 || py < 0 || px >= renderW || py >= renderH) continue;
                pixels[(size_t)py * renderW + px] = mc;
            }
        }
    }

    /* ==================================================================
     * 2026-11-08（第 4 轮）：**躲在柜子里时的反馈**
     * ------------------------------------------------------------------
     * 玩家原话：「要怎么躲」。躲进去之后如果什么都没有，玩家无法判断
     * 是躲对了还是游戏卡了 —— 所以给两句话：
     *   附近有东西（<8 格）→ 「它在外面找你」
     *   确实安静（>12 格） → 「外面安静了」
     * 中间模糊区不显示，免得误导。
     * ================================================================ */
    if (hiddenIn >= 0) {

        float nd = TelegraphNearestDist();
        /* 2026-11-08（实机反馈）：三段全覆盖。
           原来 8~12 格什么都不画 —— 玩家以为卡了。 */

        {
            int band = HideStatusBand(nd);
            const char* hs;
            unsigned int hc;
            if (band == 0) {
                hs = (g_lang == LANG_ZH) ? "它在外面找你" : "IT IS LOOKING FOR YOU";
                hc = 0x00FF8050u;
            } else if (band == 1) {
                hs = (g_lang == LANG_ZH) ? "它在附近" : "IT IS CLOSE";
                hc = 0x00FFD060u;
            } else {
                hs = (g_lang == LANG_ZH) ? "外面安静了" : "IT IS QUIET OUT THERE";
                hc = 0x0080FFA0u;
            }
            {
                int tw5 = TextWidthWin(hs);
                DrawTextWin((winW - tw5) / 2, winH - UIS(112), hs, hc);
            }
        }
    }

    DrawHudThemeRoom();   /* 第 3 轮：主题房间字幕抽成函数 */
        }
    }

}

static void DrawHudLightBar(const HudBarLayout* layout) {
    const int barW = layout->barW;
    const int barH = layout->barH;
    const int labelX = layout->labelX;
    const int barX = layout->barX;
    const int y1 = layout->y1;
    DrawTextWin(labelX, y1 - UIS(3), "LIGHT", 0x00A0A0A0u);
    /* 手电条颜色：开启=黄绿，关闭但有电=暗黄，无电=暗红 */
    unsigned int lightBarCol;
    if (!flashlightOn && battery <= 0.0f) lightBarCol = 0x00603030u;  /* 无电暗红 */
    else if (!flashlightOn) lightBarCol = 0x00806030u;  /* 关闭暗黄 */
    else lightBarCol = 0x00E8E060u;  /* 开启亮黄绿 */
    float batMax = (float)BatteryMax();
    /* v3.0：改用统一 UiBar */
    UiBar(barX, y1, barW, barH, battery / batMax, UI_BAR_NORMAL, lightBarCol);
    /* v3.0：**区分「没电」和「自己关了」** ——
       玩家实测：「LIGHT OFF 时电池条还有四五成，我分不清是没电还是我自己关了」。
       三态：DEAD（没电，暗红）/ OFF（关闭，暗黄）/ ON（开启，亮黄绿）。 */
    {
        const char* ls;
        unsigned int lc;
        if (battery <= 0.0f)      { ls = Tr("DEAD", "没电"); lc = 0x00FF4030u; }
        else if (!flashlightOn)   { ls = Tr("OFF",  "关闭"); lc = 0x00C09040u; }
        else                      { ls = Tr("ON",   "开启"); lc = 0x00E8E060u; }
        DrawTextWin(barX + barW + UIS(12), y1 - UIS(3), ls, lc);
    }

    /* v2.4：看图要花电 —— 开着图就在手电旁边挂一个持续的耗电提示，
       否则玩家只会觉得"电量掉得莫名其妙"。 */
    /* v3.0：**删掉这条重复提示** ——
       小地图自己的标题栏已经写着「MAP -10/S  HOLD TAB  M: FULL」，
       这里再挂一条既重复，又正好压在小地图上（用户截图确认）。 */

}

static void DrawHudPerception(void) {
    /* ===== v3.0 改造树感知系：预知 + 心眼 ===== */
    {



        if (currentState == STATE_PLAYING) {
            /* ① 预知：怪物进 8 格 → 屏幕边缘泛红（不需要看见它） */
            /* v6 每日限制：怪物不上小地图 —— 预知/心眼一起失效 */
            { 
              if (DailyForbidMonsterMap()) goto skip_monster_hints; }
            if (BenchPremonition() && Hunter0()->active) {
                float dx = Hunter0()->x - posX, dy = Hunter0()->y - posY;
                float d2 = dx * dx + dy * dy;
                if (d2 < 64.0f) {
                    /* 越近越红：边缘一圈渐变 */
                    float k = 1.0f - d2 / 64.0f;
                    if (k > 1.0f) k = 1.0f;
                    int band = UIS(10);   /* v3.0：40 → 10，原来每帧 160 次半透明叠加，画面抽搐 */
                    for (int i = 0; i < band; i++) {
                        float a = 0.55f * k * (1.0f - (float)i / (float)band);
                        BlendRectWin(0, i, winW, i + 1, 0x00FF0000u, a);
                        BlendRectWin(0, winH - 1 - i, winW, winH - i, 0x00FF0000u, a);
                        BlendRectWin(i, 0, i + 1, winH, 0x00FF0000u, a);
                        BlendRectWin(winW - 1 - i, 0, winW - i, winH, 0x00FF0000u, a);
                    }
                }
            }
            skip_monster_hints:
            /* ② 心眼：藏身时显示怪物距离 */
            if (BenchInnerEye() && hiddenIn >= 0 && Hunter0()->active) {
                float dx = Hunter0()->x - posX, dy = Hunter0()->y - posY;
                float dm = sqrtf(dx * dx + dy * dy);
                char ibuf[64];
                /* v3.0：挪到**顶部固定带**并加底色 ——
                   原来画在 winH/2+100，被场景/其他覆盖层吃掉，
                   验证报告实测像素差 0/2048（完全没画出来）。 */
                sprintf(ibuf, "%s %.1f", Tr("DIST", "距离"), (double)dm);
                {
                    int tw4 = TextWidthWin(ibuf);
                    int tx4 = (winW - tw4) / 2;
                    int ty4 = UIS(112);
                    BlendRectWin(tx4 - UIS(10), ty4 - UIS(6),
                                 tx4 + tw4 + UIS(10), ty4 + TextHeightWin() + UIS(6),
                                 0x00081018u, 0.88f);
                    DrawTextWin(tx4, ty4, ibuf, 0x0090D0F0u);
                }
            }
        }
    }

    /* v3.0：**噪音环** —— 扔瓶子/敲墙后显示声音强度，2.2 秒衰减 */
    {

        if (noiseDisplay > 0.01f && currentState == STATE_PLAYING) {
            int nr = UIS(26);   /* v3.0：固定半径，只让亮度随强度变 —— 原来半径也变，四角乱跳 */
            int ncx = winW / 2, ncy = winH / 2;
            unsigned int nc = (noiseDisplay > 0.7f) ? UI_DANGER
                            : (noiseDisplay > 0.4f) ? UI_WARN : UI_INFO;
            /* 四个弧段（简单画法：四角的小方框，避免画圆的开销） */
            for (int a = 0; a < 4; a++) {
                int ax = ncx + ((a & 1) ? nr : -nr) - UIS(3);
                int ay = ncy + ((a & 2) ? nr : -nr) - UIS(3);
                BlendRectWin(ax, ay, ax + UIS(6), ay + UIS(6), nc, 0.85f * noiseDisplay);
            }
        }
    }

}

static void DrawHudDamageHints(const HudBarLayout* layout) {
    const int barW = layout->barW;
    const int barH = layout->barH;
    const int labelX = layout->labelX;
    const int barX = layout->barX;
    const int rowGap = layout->rowGap;
    const int y1 = layout->y1;
    const int y2 = layout->y2;
    (void)barW; (void)barH; (void)labelX; (void)barX;
    (void)rowGap; (void)y1; (void)y2;
    DrawTextWin(labelX, y2 - UIS(3), "RUN", 0x00A0A0A0u);
    UiBar(barX, y2, barW, barH, stamina / StaminaMax(),
          (stamina < 25.0f) ? UI_BAR_WARN : UI_BAR_NORMAL, 0x0040C060u);
    /* v3.0：**武器行已删除** */

    /* v1.15：命中伤害数字（屏幕中央偏下，向上浮 + 淡出） */
    if (0 && lastHitTimer > 0.0f && lastHitDamage > 0) {
        char dbuf[32];
        sprintf(dbuf, "-%d", lastHitDamage);
        float a = lastHitTimer / 0.7f;
        if (a > 1.0f) a = 1.0f;
        int rise = (int)((1.0f - a) * UIS(26));
        int tw2 = TextWidthWin(dbuf);
        int dx2 = winW / 2 - tw2 / 2 + UIS(60);
        int dy2 = winH / 2 + UIS(40) - rise;
        unsigned int dc = lastHitBackstab ? 0x00FFE060u : 0x00F0F0F0u;
        int rr = (int)(((dc >> 16) & 0xFF) * a);
        int gg = (int)(((dc >> 8) & 0xFF) * a);
        int bb = (int)((dc & 0xFF) * a);
        DrawTextWin(dx2, dy2, dbuf, (unsigned int)((rr << 16) | (gg << 8) | bb));
        if (lastHitBackstab) {
            /* v3.0：SNEAK! 已删 —— 怪不可杀，偷袭没有意义 */
        }
    }

    /* v2.6：训练场右侧的「SPECIMEN DMG」绿色面板**已停用**（用户要求）——
       现在命中时屏幕上会直接跳伤害数字，这块面板重复且挡视线。
       代码保留，需要时把 #if 0 改成 #if 1 就能恢复。 */
#if 0
    /* v1.20：训练场右侧伤害面板（布局按文字高度算，杜绝重叠） */
    if (trainingMode) {
        int th = TextHeightWin();
        int pad = UIS(10);
        int pw = UIS(210);
        int ph = pad + th * 5 + UIS(28) + pad;     /* 标题 + 4 行 + 柱状图 + 上下留白 */
        int px2 = winW - pw - UIS(20);
        int py2 = winH / 2 - ph / 2;

        BlendRectWin(px2, py2, px2 + pw, py2 + ph, 0x00000810u, 0.92f);
        FrameRectWin(px2, py2, px2 + pw, py2 + ph, 0x0040C080u, 0.9f, UIS(2));

        char tb[48];
        int ty = py2 + pad;
        DrawTextWin(px2 + pad, ty, Tr("SPECIMEN DMG", "训练场伤害"), 0x0060FFB0u);
        ty += th;

        sprintf(tb, "TOTAL %d", trainingTotal);
        DrawTextWin(px2 + pad, ty, tb, 0x00FFFFFFu);
        ty += th;

        sprintf(tb, "LAST  %d", trainingLast);
        DrawTextWin(px2 + pad, ty, tb, 0x00FFE060u);
        ty += th;

        sprintf(tb, "HITS  %d", trainingCount);
        DrawTextWin(px2 + pad, ty, tb, 0x0090A0A0u);
        ty += th;

        sprintf(tb, "DPS   %d", trainingDps);
        DrawTextWin(px2 + pad, ty, tb, 0x0080D0FFu);
        ty += th + UIS(8);

        /* 柱状图：在 DPS 下方留出完整一块 */
        int barsH = UIS(20);
        int by = ty + barsH;
        for (int i = 0; i < 6; i++) {
            int h2 = trainingHist[i] * UIS(3);
            if (h2 > barsH) h2 = barsH;
            if (h2 <= 0) continue;
            BlendRectWin(px2 + pad + i * UIS(13), by - h2,
                         px2 + pad + i * UIS(13) + UIS(9), by, 0x0060FFB0u, 0.85f);
        }
    }
#endif
}

static void DrawHudInteractionHints(const HudBarLayout* layout) {
    const int barW = layout->barW;
    const int barH = layout->barH;
    const int labelX = layout->labelX;
    const int barX = layout->barX;
    const int rowGap = layout->rowGap;
    const int y1 = layout->y1;
    const int y2 = layout->y2;
    (void)barW; (void)barH; (void)labelX; (void)barX;
    (void)rowGap; (void)y1; (void)y2;
    /* v3.0：**武器行已删除** */

    /* ==================================================================
     * 2026-11-06（用户实测）：**被蛛网缠住时的屏内倒计时**
     * ------------------------------------------------------------------
     * 用户原话：「你要保证那个倒计时跟那个每次减 0.5 秒是同步的，
     *           不然我好不容易戳到只剩 1 秒钟，我还要再等就很烦」。
     *
     * 做法：屏幕上直接显示**同一个时间源**（ControlTimeLeft），
     * 每次戳下去数字立刻掉 0.5 —— 看到的和判定的永远是同一个值。
     * 同时显示"还要按/还要等"，让玩家知道是被卡住了还是在推进。
     * ================================================================ */
    {
        /* 原型在 control_effect.h，这里不要再写（会撞类型） */
        if (currentState == STATE_PLAYING && ControlActive() &&
            ControlCurrent() == CTRL_SPIDER_WEB) {
            float t = ControlTimeLeft();
            static char wbuf[64];
            int atFloor = (t <= SPIDER_WEB_MIN + 0.02f);
            if (atFloor)
                snprintf(wbuf, sizeof(wbuf), "%s  %.1fs",
                         (g_lang == LANG_ZH) ? "只能等" : "WAIT", (double)t);
            else
                snprintf(wbuf, sizeof(wbuf), "%s %s  %.1fs",
                         (g_lang == LANG_ZH) ? "连按" : "MASH",
                         VkName(g_bindings[ACT_DODGE]), (double)t);
            {
                int tw = TextWidthWin(wbuf);
                unsigned int col = atFloor ? 0x00A0A0A0u : 0x00FFE060u;
                /* 2026-11-06（用户实测）：**放到准星上方**。
                   原来在 winH/2 + 46，正好和底部的
                   「HOLD E SEARCH」/「PULL -> ...」撞在一起叠字。 */
                DrawTextWin((winW - tw) / 2, winH / 2 - UIS(64), wbuf, col);
            }
        }
    }

    /* 交互提示 + 长按进度 */
    if (hoverKind != 0) {
        char pbuf[192];   /* 2026-11-06：舱门门槛文案最长（含数字）要 96 字节，64 会溢出 */
        /* v2.7：提示要用当前绑定，改过键之后写死 E 就是骗人 */
        if (hoverKind == 1)
            sprintf(pbuf, "HOLD %s  SEARCH", VkName(g_bindings[ACT_INTERACT]));
        else if (hoverKind == 4) {
            /* AD-46：舱门提示**独占一行**，不和时间/搜索提示抢坐标 */
            char why[96];
            if (HatchGateCheck(hoverIndex, why, (int)sizeof(why)))
                sprintf(pbuf, "HOLD %s  %s", VkName(g_bindings[ACT_INTERACT]), HatchTitle(hoverIndex));
            else
                sprintf(pbuf, "%s  -  %s", HatchTitle(hoverIndex), why);
        }
        else if (hoverKind == 3) {
            /* 2026-11-06：家具可以搜（货架 3 次 / 木箱堆 2 次 / 控制台 2 次）。
               提示里带上还剩几次 —— 否则玩家不知道"还能不能翻"。 */

            int left = 0;
            if (hoverIndex >= 0 && hoverIndex < MAX_ROOM_PROPS)
                left = roomProps[hoverIndex].searchLeft;
            sprintf(pbuf, "PRESS %s  SEARCH  (%d LEFT)",
                    VkName(g_bindings[ACT_INTERACT]), left);
        }
        else if (fusesHeld >= fusesNeeded)
            sprintf(pbuf, "HOLD %s  POWER ON", VkName(g_bindings[ACT_INTERACT]));
        else sprintf(pbuf, "NEED %d MORE FUSE", fusesNeeded - fusesHeld);

        /* 2026-11-06（用户实测）：舱门的**后果说明**再画一行。
           第一次放在 +UIS(26)，正好压在长按进度条上（截图里叠成一条）。
           进度条占 py+30..py+38，所以这里放到 +UIS(46)，
           顺序读起来也顺：标题 -> 进度条 -> 后果。 */
        if (hoverKind == 4) {

            const char* eff = HatchEffectLine(hoverIndex);
            if (eff && eff[0]) {
                int tw2 = TextWidthWin(eff);
                DrawTextWin((winW - tw2) / 2, winH - UIS(214) + UIS(46),
                            eff, 0x00E0B050u);
            }
        }

        int tw = TextWidthWin(pbuf);
        int px = (winW - tw) / 2;
        /* 2026-11-05：原来和「拾取 / 藏身」提示同在 winH-150，会叠字。
           往上挪一行，进度条跟着走。 */
        int py = winH - UIS(hoverKind == 4 ? 214 : 182);
        DrawTextWin(px, py, pbuf, (hoverKind == 2 && fusesHeld < fusesNeeded) ? 0x00C05050u : 0x00E8E8E8u);

        int barW = UIS(240), barH = UIS(8);
        int bx = (winW - barW) / 2;
        int by = py + UIS(30);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00202020, 0.8f);
        if (holdingNow) {
            float f = holdTimer / SEARCH_TIME;
            if (f > 1.0f) f = 1.0f;
            BlendRectWin(bx, by, bx + (int)(barW * f), by + barH, 0x00C8E8C8, 0.95f);
        }
        FrameRectWin(bx, by, bx + barW, by + barH, 0x00808080, 0.7f, 1);
    }

    /* v2.6：SAFE ZONE 原来单独占一条带（winH/2+40），会和主题名/武器行抢位置。
       现在并进 FUSES 那一行（见上方的 FUSES 绘制），不再单独画。 */
}

static void DrawHudChargeHints(const HudBarLayout* layout) {
    const int barW = layout->barW;
    const int barH = layout->barH;
    const int labelX = layout->labelX;
    const int barX = layout->barX;
    const int rowGap = layout->rowGap;
    const int y1 = layout->y1;
    const int y2 = layout->y2;
    (void)barW; (void)barH; (void)labelX; (void)barX;
    (void)rowGap; (void)y1; (void)y2;
    /* v3.0：**武器行已删除** */

    /* 蓄力投掷进度条：按住鼠标左键时显示 */
    int mouseFree = (currentState != STATE_PLAYING) || (isMapOpen && mapFullScreen);
    /* 只有选中道具有库存时才显示蓄力条 */
    int hasItem = itemHasStock();   /* v1.18：按当前槽位判断，五种道具都能蓄力 */
    if (isCharging && mouseCharge > 0.0f && !mouseFree && hasItem) {
        int barW = UIS(280), barH = UIS(14);
        int bx = (winW - barW) / 2;
        /* v2.9：上移到 winH/2+48 —— 原来 +100 让百分比落在 420..451，
           正好压住 y=425..456 的武器行（报告 严重 #5）。 */
        int by = winH / 2 + UIS(48);
        /* 背景：深色半透明，带明显边框 */
        BlendRectWin(bx - 2, by - 2, bx + barW + 2, by + barH + 2, 0x00000000, 0.9f);
        BlendRectWin(bx, by, bx + barW, by + barH, 0x00101020, 0.95f);
        /* 填充：蓝→青→红渐变感 */
        unsigned int chargeCol;
        if (mouseCharge < 0.33f) chargeCol = 0x0040E0FFu;
        else if (mouseCharge < 0.66f) chargeCol = 0x0040FF80u;
        else if (mouseCharge < 1.0f) chargeCol = 0x00E0FF40u;
        else chargeCol = 0x00FF4040u;  /* 满蓄力红色警告 */
        BlendRectWin(bx, by, bx + (int)(barW * mouseCharge), by + barH, chargeCol, 1.0f);
        /* 发光边框 */
        FrameRectWin(bx, by, bx + barW, by + barH, chargeCol, 1.0f, 2);
        /* 标签带阴影 */
        /* v3.3 **标签要跟着真实行为走** ——
           试玩实测：「Help 写 THROW: RMB，屏幕上却显示 CHARGING KNOCK，
           松手后背包里的 FLARE 确实被消耗了」。玩家无法判断这一下是扔东西还是敲击。
           现在：手上选了投掷物 → CHARGING THROW；
                 没选（或该槽空）→ CHARGING KNOCK（真的只是敲击）。 */
        {


            int cnt[6] = { flares, bottles, smokeBombs, gluePots, echoBolts, decoys };
            int hasItem = (throwSelected >= 0 && throwSelected < 6 && cnt[throwSelected] > 0);
            DrawTextCenterWin(winW / 2, by - UIS(24),
                              hasItem ? Tr("CHARGING THROW", "投掷蓄力")
                                      : Tr("CHARGING KNOCK", "敲击蓄力"),
                              chargeCol);
        }
        /* 百分比数值 */
        char pct[16];
        sprintf(pct, "%d%%", (int)(mouseCharge * 100));
        DrawTextCenterWin(winW / 2, by + barH + UIS(6), pct, 0x00FFFFFFu);
    }

    /* v3.0：**大招提示** —— 拿到之后在 HUD 上告诉玩家「按 Q」 */
    {


        if (runUltimate >= 0 && currentState == STATE_PLAYING) {
            const char* nm = (runUltimate < INRUN_COUNT)
                ? inRunDefs[runUltimate].name : "?";
            char ub[64];
            if (!ultUsed) sprintf(ub, "Q: %s", nm);
            else          sprintf(ub, "%s USED", nm);
            int uw = TextWidthWin(ub);
            DrawTextWin(winW - UIS(24) - uw, y1 - UIS(3), ub,
                        ultUsed ? 0x00606060u : 0x00FFD040u);
        }
    }
}

static void DrawHudCalibrationHints(const HudBarLayout* layout) {
    const int barW = layout->barW;
    const int barH = layout->barH;
    const int labelX = layout->labelX;
    const int barX = layout->barX;
    const int rowGap = layout->rowGap;
    const int y1 = layout->y1;
    const int y2 = layout->y2;
    (void)barW; (void)barH; (void)labelX; (void)barX;
    (void)rowGap; (void)y1; (void)y2;
    /* v3.0：**武器行已删除** */

    /* v3.0：**校准倒计时常驻显示** ——
       用户两次反馈「倒计时没有加进去」。
       之前只在 hiddenIn >= 0（藏进藏身点）时才画，玩家在走廊里
       根本看不到还剩多久。
       现在只要在游戏中且冷却中，就显示在 RUN 条下方一行。 */
    if (currentState == STATE_PLAYING) {

        char cd[48];
        unsigned int cdc;
        if (calibActive) {
            sprintf(cd, Tr("CALIBRATING", "校准中"));
            cdc = 0x00FFD040u;
        } else if (calibCooldown > 0.0f) {
            sprintf(cd, Tr("CALIB %ds", "校准冷却 %d 秒"),
                    (int)(calibCooldown + 0.5f));
            cdc = 0x00909090u;
        } else {
            /* v3.3 **只有真的能用时才写 READY** ——
               试玩实测：「HUD 底部常驻 CALIB READY，我按空格两次什么都没发生。
               这行字在提示一个当前不存在的操作。」
               校准只在安全屋或藏身点里能开（见 game.c 的 inSafe || hiddenIn >= 0）。 */

            if (playerInSafe || hiddenIn >= 0) {
                sprintf(cd, Tr("CALIB READY", "校准就绪"));
                cdc = 0x0060FF90u;
            } else {
                sprintf(cd, Tr("CALIB - HIDE TO USE", "校准 - 需躲进藏身点"));
                cdc = 0x00505050u;   /* 压暗，不抢视线 */
            }
        }
        DrawTextWin(labelX, y2 + rowGap - UIS(6), cd, cdc);
        /* ===== v3.3：两个操作提示并到这一行右侧 =====
           用户反馈「这两个都在屏幕中间太遮挡了」。
           现在跟在 CALIB 后面，同一条基线、**无边框、暗色小字**，
           不占额外空间也不抢视线：
             [B] 引导    训练营里引导没展开时才有
             [C] 激活诱饵  场上有未激活的玩偶时才有 */
        {
            int px2 = labelX + TextWidthWin(cd) + UIS(20);
            int py2 = y2 + rowGap - UIS(6);




            if (trainingMode && !tutorialGuideOn) {
                const char* gtxt = Tr("[B] GUIDE", "[B] 引导");
                DrawTextWin(px2, py2, gtxt, 0x004A9480u);
                px2 += TextWidthWin(gtxt) + UIS(18);
            }
            {
                int unarmed = 0;
                for (int di = 0; di < MAX_DECOY; di++)
                    if (decoysArr[di].active && !decoysArr[di].armed) { unarmed = 1; break; }
                if (unarmed)
                    DrawTextWin(px2, py2, Tr("[C] ARM DECOY", "[C] 激活诱饵"), 0x00A8842Cu);
            }
        }

    }

    /* v3.0：**摇电只留一条黄线，不写任何文字** ——
       用户要求「改成之前底下有黄线的那一版，然后不带其他文字」。
       LIGHT 标签和 ON/OFF 状态照常显示，只在手电条下面压一条 4px 的
       黄色进度线。位置取 y1 与 y2（RUN 行）之间的空隙，不会碰到任何东西。 */
    if (cranking) {
        float cc = crankTimer / 2.4f;
        if (cc > 1.0f) cc = 1.0f;
        int lineY = y1 + barH + UIS(2);
        int lineH = UIS(4);
        /* 空隙不够就往上贴，绝不压到 RUN 行 */
        if (lineY + lineH > y2 - UIS(2)) {
            lineY = y2 - UIS(2) - lineH;
            if (lineY < y1 + barH) lineY = y1 + barH;
        }
        unsigned int ccol = (cc >= 1.0f) ? 0x0060FF90u : 0x00FFD040u;
        BlendRectWin(barX, lineY, barX + barW, lineY + lineH, 0x00202020u, 0.80f);
        BlendRectWin(barX, lineY, barX + (int)(barW * cc), lineY + lineH, ccol, 1.0f);
    }

    /* 金币（GOLD）是大厅货币，游戏过程中用不到 —— 不放进 HUD。
       「M CLOSE MAP」也删掉了：地图标题上已经写着
       「M: FULL  M: CLOSE」，再在底部挂一行是重复的。 */
}

static void DrawHudActionHints(const HudBarLayout* layout) {
    DrawHudDamageHints(layout);
    DrawHudInteractionHints(layout);
    DrawHudChargeHints(layout);
    DrawHudCalibrationHints(layout);
}

void DrawHUD(void) {
    /* Keep this order: later units intentionally overlay earlier drawing. */
    if (currentState == STATE_PLAYING && !isMapOpen) DrawCrosshair();
    int effDetail = keys[VK_TAB] ? 2 : hudDetail;
    DrawHudRunHeader(effDetail);
    DrawHudVitalsAndStatus(effDetail);
    DrawHudInventory();
    DrawHudWorldHints();
    HudBarLayout bars = HudBuildBarLayout();
    DrawHudSafeHints();
    DrawHudCoverAndThreat();
    DrawHudLightBar(&bars);
    DrawHudPerception();
    DrawHudActionHints(&bars);
}


/* v3.1：新手教程的顶部提示条。步骤文案和推进逻辑都在 game.c，
   这里只负责画，避免两边各维护一套步骤。 */
static void DrawTutorialPanel(void) {
    if (!trainingMode) return;

    /* v3.3：**引导关着的时候整个面板不画** ——
       用户反馈「按 B 激活引导」那行放屏幕中间太遮挡，
       所以这行已经挪到左下角 CALIB 那一行的右边了（见 DrawHUD）。
       这里只负责「别画面板」。 */
    {

        if (!tutorialGuideOn) return;
    }

    int th = TextHeightWin();
    int w = UIS(620);
    if (w > winW - UIS(24)) w = winW - UIS(24);
    int h = th * 3 + UIS(30);
    int x = (winW - w) / 2;
    int y = UIS(120);
    if (y + h > winH * 2 / 3) y = UIS(56);

    BlendRectWin(x, y, x + w, y + h, 0x00081018u, 0.90f);
    FrameRectWin(x, y, x + w, y + h, 0x00C8A040u, 0.92f, UIS(2));

    char line[160];

    if (tutorialDone) {
        DrawTextCenterWin(winW / 2, y + UIS(10), Tr("TUTORIAL COMPLETE", "教程完成"), 0x00FFE070u);
    } else {
        sprintf(line, "TUTORIAL  %d / %d", TutorialStepNumber(), TutorialStepTotal());
        DrawTextCenterWin(winW / 2, y + UIS(10), line, 0x00FFE070u);
    }

    snprintf(line, sizeof(line), "%s", TutorialObjective());
    ClampRowText(line, w - UIS(24));
    DrawTextCenterWin(winW / 2, y + UIS(10) + th, line, 0x00FFFFFFu);

    snprintf(line, sizeof(line), "%s", TutorialHint());
    ClampRowText(line, w - UIS(24));
    DrawTextCenterWin(winW / 2, y + UIS(10) + th * 2, line, 0x00A8B4B8u);
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
    if (currentState != STATE_PLAYING && currentState != STATE_PAUSED) return;
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
void DrawPickupToast(void) {




    if (!pickupMsg || pickupMsgTimer <= 0.0f) return;

    /* ===== v2.4.52 **只在游玩中显示** =====
       用户截图：暂停菜单上浮着一行 "AT THE EXIT: LEAVE, OR GO DEEPER."
       —— 那是上一帧的事件提示，因为 toast 画在帧末（所有图层之上），
       暂停菜单先画、toast 后画，于是压在菜单上。
       现在非游玩状态直接不画。 */
    if (currentState != STATE_PLAYING) return;   /* currentState 是本文件已有的全局 */

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
        float _sx = posX, _sy = posY;
        posX += g_shakeOffX;
        posY += g_shakeOffY;
        Render3D();
        posX = _sx;
        posY = _sy;
        if (currentState == STATE_PLAYING && isMapOpen) RenderMap();
        /* 跳脸虫：屏幕空间 overlay，盖在场景之上、HUD 之下 */
            if (flashTime > 0.0f)
            memset(pixels, 0xFF, (size_t)renderW * renderH * sizeof(unsigned int));

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

static void RenderFrameMenus(void) {
    /* 文字也在这一层，先合成再一次性呈现；弹窗必须在列表文字之后画 */
    if (currentState != STATE_PLAYING) {
        int popupOpen = (currentState == STATE_SETTINGS && settingsPopupOpen);
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
        DrawTutorialPanel();
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
                    sprintf(l1, "%s", Tr("SPACE LOCK", "空格 锁定"));
                    c1 = 0x00FFD040u;
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
    /* ===== v3.0：**校准条画在所有图层之上** =====
       用户反复反馈「躲在柜子里看不到判定轴」。
       之前试过「校准时跳过遮罩」，但只要还有别的覆盖层（低理智红晕、
       锁定框、letterbox）就会再次被埋。
       现在把整块校准 UI 挪到帧末、遮罩之后，**它一定是最后画的**。 */
    DrawExitChoice();   /* v6：出口二选一，画在最上层 */

    if (currentState == STATE_PLAYING && calibActive) {
        int bw2 = winW - UIS(320);
        if (bw2 < UIS(180)) bw2 = winW - UIS(120);
        int bx2 = (winW - bw2) / 2;
        int bh2 = UIS(20);
        int by2 = winH / 2 - bh2 / 2;      /* 屏幕正中，视野最好认 */

        /* 三层实心底：外框 + 内底 + 边框，保证在任何背景上都看得清 */
        BlendRectWin(bx2 - UIS(14), by2 - UIS(14),
                     bx2 + bw2 + UIS(14), by2 + bh2 + UIS(14),
                     0x00000000u, 0.88f);
        BlendRectWin(bx2 - UIS(10), by2 - UIS(10),
                     bx2 + bw2 + UIS(10), by2 + bh2 + UIS(10),
                     0x00101418u, 1.0f);
        FrameRectWin(bx2 - UIS(10), by2 - UIS(10),
                     bx2 + bw2 + UIS(10), by2 + bh2 + UIS(10),
                     0x00FFD040u, 1.0f, 2);
        DrawCalibBar(bx2, by2, bw2, bh2);
        DrawTextCenterWin(winW / 2, by2 - UIS(34),
                          Tr("SPACE TO LOCK", "空格锁定"), 0x00FFD040u);
    }

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
    UiLayoutBegin();
    RenderFrameScene();
    RenderFrameMenus();
    RenderFrameOcclusion();
    RenderFrameCalibration();
    PresentComposedFrame();
}


/* ================================================================== */
/* v2.2 UI 主题实现（合并进 ui.c，因为要用到本文件的 static 绘制原语）    */
/* ================================================================== */
#include "ui_theme.h"


#define UI_INSET 20

void UiInit(void) { /* FIXED_RESULT: UI has no initialization-owned state; resources are initialized by their owners. */ }

void UiPanel9(int x0, int y0, int x1, int y1, int hot) {
    if (!SpritesReady()) return;
    int id = hot ? SPR_UI_PANEL_HOT : SPR_UI_PANEL;
    int S = 128;
    int c = UI_INSET;
    int w = x1 - x0, h = y1 - y0;
    if (w < c * 2 + 2 || h < c * 2 + 2) return;
    DrawSpriteSub(id, 0, 0, c, c, x0, y0, c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, S - c, 0, c, c, x0 + w - c, y0, c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, 0, S - c, c, c, x0, y0 + h - c, c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, S - c, S - c, c, c, x0 + w - c, y0 + h - c, c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, c, 0, S - 2 * c, c, x0 + c, y0, w - 2 * c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, c, S - c, S - 2 * c, c, x0 + c, y0 + h - c, w - 2 * c, c, 1.0f, 1.0f);
    DrawSpriteSub(id, 0, c, c, S - 2 * c, x0, y0 + c, c, h - 2 * c, 1.0f, 1.0f);
    DrawSpriteSub(id, S - c, c, c, S - 2 * c, x0 + w - c, y0 + c, c, h - 2 * c, 1.0f, 1.0f);
    DrawSpriteSub(id, c, c, S - 2 * c, S - 2 * c,
                  x0 + c, y0 + c, w - 2 * c, h - 2 * c, 1.0f, 1.0f);
}

void UiCursor(int x, int y, int size, float alpha) {
    if (!SpritesReady()) return;
    if (size < 6) size = 6;
    if (size > 48) size = 48;
    DrawSprite(SPR_UI_CURSOR, x, y, size, size, 1.0f, alpha, 0, 0.0f);
}

void UiRowSel(int x0, int y0, int x1, int y1, float alpha) {
    int h = y1 - y0;
    if (h < 4) return;
    if (SpritesReady())
        DrawSpriteSub(SPR_UI_ROW, 0, 0, 128, 128, x0, y0, x1 - x0, h, 1.0f, alpha);
    int barW = h / 8;
    if (barW < 2) barW = 2;
    BlendRectWin(x0, y0 + h / 6, x0 + barW, y1 - h / 6, UI_ACCENT, alpha * 0.95f);
    int cs = h * 2 / 3;
    if (cs < 8) cs = 8;
    UiCursor(x0 + barW + 2, y0 + (h - cs) / 2, cs, alpha);
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

void UiTitle(int cx, int y, const char* s) {
    DrawTextCenterWin(cx, y, s, UI_INK_TITLE);
}

void UiLabel(int x, int y, unsigned int col, const char* s) {
    DrawTextWin(x, y, s, col);
}

void UiHint(int cx, int y, const char* s) {
    DrawTextCenterWin(cx, y, s, UI_INK_DIM);
}

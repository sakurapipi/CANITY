/* Skill-tree geometry, viewport ownership and drawing. */
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
#include "ui_theme.h"

static const unsigned int kBranchColor[7] = {0x79CDF2,0xB8D9FA,0xE8ADB7,0xB6BBC9,0xD0C6ED,0x98D8C8,0xF1F5FF};
#define SST_TEXT10 74
#define SST_GAP10  30
#define SST_WANT10 80
#define SK_WX      210.0f    /* 世界网格：列间距 */
#define SK_WY      120.0f    /* 世界网格：行间距 */
#define SK_K_MIN   0.18f
#define SK_K_MAX   2.60f
#define SK_NODE_K 0.78f
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

typedef struct {
    float x, y, zoom;
    int dragging;
    int centred, tested;
    float mouseX, mouseY, dragX, dragY;
} SkillTreeViewState;
static SkillTreeViewState skillView = {.zoom = 1.0f};
typedef struct {
    SkillTreeLayout layout;
    int TH, sel, innerL, innerR, gTop, gBot;
} SkillTreeDrawContext;

static int SkillListIndex(int id) {
    for (int k = 0; k < SKILL_LIST_COUNT; k++)
        if (kSkillList[k] == id) return k;
    return -1;
}

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

static int SkillNodeBuyable(int id) {
    if (SkillCanBuy(id)) return 1;
    return SkillSwitchable(id) && WalletCanPayBank(SKILL_SWITCH_COST);
}

static void SkillNodeSize(int* width, int* height) {
    float scale = skillView.zoom < SK_NODE_K ? skillView.zoom : SK_NODE_K;
    int w = (int)(SK_WX * .55f * scale), h = (int)(SK_WY * .62f * scale);
    if (w < 26) w = 26;
    if (h < 18) h = 18;
    *width=w; *height=h;
}

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

int  SkillViewZoom(void)  { return (int)(skillView.zoom * 100.0f); }

void SkillCenterOn(int id) {
    float wx, wy;
    SkillWorldPos(id, &wx, &wy);
    int nodeW, nodeH; SkillNodeSize(&nodeW,&nodeH);
    skillView.x = (float)winW * 0.50f - (wx * skillView.zoom + (float)nodeW * 0.5f);
    skillView.y = (float)winH * 0.42f - (wy * skillView.zoom + (float)nodeH * 0.5f);
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
    skillView.zoom=(layout.panel.right-layout.panel.left-100.0f)/(maxX-minX+120.0f);
    float ky=(bottom-top)/(maxY-minY+100.0f);
    if(ky<skillView.zoom)skillView.zoom=ky;
    if(skillView.zoom<SK_K_MIN)skillView.zoom=SK_K_MIN;
    if(skillView.zoom>SK_NODE_K)skillView.zoom=SK_NODE_K;
    skillView.x=winW*.5f-(minX+maxX)*.5f*skillView.zoom;
    skillView.y=(top+bottom)*.5f-(minY+maxY)*.5f*skillView.zoom;
    uiDirty=1;
}

void SkillPanBy(float dx, float dy) { skillView.x += dx; skillView.y += dy; uiDirty = 1; }

void SkillZoomAt(float factor, int mx, int my) {
    float k0 = skillView.zoom, k2 = k0 * factor;
    if (k2 < SK_K_MIN) k2 = SK_K_MIN;
    if (k2 > SK_K_MAX) k2 = SK_K_MAX;
    if (k2 == k0) { uiDirty = 1; return; }
    skillView.x = (float)mx - ((float)mx - skillView.x) * (k2 / k0);
    skillView.y = (float)my - ((float)my - skillView.y) * (k2 / k0);
    skillView.zoom = k2;
    uiDirty = 1;
}

void SkillDragStart(int mx, int my) {
    skillView.dragging = 1;
    skillView.mouseX = (float)mx; skillView.mouseY = (float)my;
    skillView.dragX = skillView.x; skillView.dragY = skillView.y;
}

void SkillDragMove(int mx, int my) {
    if (!skillView.dragging) return;
    skillView.x = skillView.dragX + ((float)mx - skillView.mouseX);
    skillView.y = skillView.dragY + ((float)my - skillView.mouseY);
    uiDirty = 1;
}

void SkillDragEnd(void) { skillView.dragging = 0; }

int SkillDragWasClick(void) {
    float dx = skillView.x - skillView.dragX;
    float dy = skillView.y - skillView.dragY;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return skillView.dragging && dx<4.0f && dy<4.0f;
}

int  SkillIsDragging(void) { return skillView.dragging; }

static int SkSX(float wx) { return (int)(wx * skillView.zoom + skillView.x); }

static int SkSY(float wy) { return (int)(wy * skillView.zoom + skillView.y); }

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
            if (skillView.zoom > 0.55f) {
                int pw = (int)(TH / 3 * skillView.zoom); if (pw < 4) pw = 4;
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

        sprintf(b, "%.2fx", skillView.zoom);
        int zoomX=innerR-TextWidthWin(b);
        if (SkillOwned(sel) && (sel==SK_DECOY || sel==SK_HUSH_ECHO || sel==SK_ECHO_MAPPING)) {
            char notice[120];
            snprintf(notice,sizeof notice,"%s",Tr("Retuned skill · Your rank was kept","技能已重制，已购等级保留"));
            ClampRowText(notice,zoomX-innerL-UIS(12));
            DrawTextWin(innerL,dy-TH,notice,UI_GOLD);
        }
        DrawTextWin(zoomX, dy - TH, b, 0x00808080u);

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
        if (!skillView.tested) {
            skillView.tested = 1;
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
       用户截图：进 HUB 一个节点都看不到（整页空白），说明 skillView.x/skillView.y 跑飞了
       （NaN / 巨大值），所有节点被 gTop..gBot 裁掉。
       这里每帧检查一次，只要状态非法就强制回到选中节点。 */
    {
        int bad = 0;
        if (!(skillView.zoom >= SK_K_MIN && skillView.zoom <= SK_K_MAX)) bad = 1;
        if (!(skillView.x > -20000.0f && skillView.x < 20000.0f)) bad = 1;
        if (!(skillView.y > -20000.0f && skillView.y < 20000.0f)) bad = 1;
        if (bad) { skillView.zoom = 0.78f; SkillCenterOn(hubSelection); }
    }
    /* 首次进入：居中到第一行 */
    {
        if (!skillView.centred) {
            skillView.centred = 1;
            SkillResetView();
        }
    }


    int innerL = L.panel.left + L.padX;
    int innerR = L.panel.right - L.padX;
    int gTop = L.curY + TH + TH / 2;          /* 图区上界 */
    int gBot = L.panel.bottom - TH * 4;        /* 图区下界 = 详情条顶边 */
    /* v6 **图区下界要留够详情条的空间** ——
       详情条三行从 panel.bottom - TH*4 开始，而节点高 0.62*SK_WY*skillView.zoom，
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
    float cx = wx * skillView.zoom + skillView.x + (float)nodeW * 0.5f;
    float cy = wy * skillView.zoom + skillView.y + (float)nodeH * 0.5f;
    float tx = (float)winW * 0.50f;
    float ty = (float)winH * 0.42f;
    int moved = 0;
    if (cx < (float)winW * 0.30f || cx > (float)winW * 0.70f) { skillView.x += tx - cx; moved = 1; }
    if (cy < (float)winH * 0.24f || cy > (float)winH * 0.60f) { skillView.y += ty - cy; moved = 1; }
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

int SkillPickAt(int x,int y){
    if(currentState!=STATE_SKILL_PICK)return -1;
    MenuGeo g=GetMenuGeo();
    for(int i=0;i<INRUN_CHOICES;i++){
        RECT r=RowRect(g,i);
        if(skillPickOptions[i]>=0 && x>=r.left && x<r.right && y>=r.top && y<r.bottom)return i;
    }return -1;
}

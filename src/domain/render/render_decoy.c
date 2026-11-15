/* 2026-11-16：从 render.c 拆出的**诱饵玩偶模型**（域内解耦，行为不变）。
   render.c 3888 行超出解耦闸门的单文件上限；这一簇（typedef + 7 个内部函数
   + 唯一入口 DrawDecoyModel）只依赖公共绘制 API，且**不碰 render.c 的任何 static**，
   所以整体搬走不会改变状态归属。
   S3：本文件只拥有 DrawDecoyModel 一个对外符号；其余保持 static。 */
#include "common.h"
#include "render.h"
#include "ui.h"
#include "ui_text.h"
#include "entity.h"
#include "player_api.h"
#include "world_api.h"
#include "lang.h"
#include "state.h"
#include "plat.h"

static void DcyPut(int x, int y, unsigned int c, float a) {
    if (x < 0 || x >= renderW || y < 0 || y >= renderH) return;
    PutPixelBlend(x, y, c, a);
}
/* 麻布织纹：2×2 棋盘，每格第二半稍暗。weave=0 时退化成普通点 */
static void DcyCloth(int x, int y, unsigned int c, float a, int weave) {
    if (x < 0 || x >= renderW || y < 0 || y >= renderH) return;
    if (weave && (((x >> 1) + (y >> 1)) & 1)) a *= 0.90f;
    PutPixelBlend(x, y, c, a);
}
static void DcyBar(int x0, int x1, int y, unsigned int c, float a) {
    if (y < 0 || y >= renderH) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; x++) DcyPut(x, y, c, a);
}
static void DcyVBar(int x, int y0, int y1, unsigned int c, float a) {
    if (x < 0 || x >= renderW) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) DcyPut(x, y, c, a);
}
/* 实心矩形（补丁用） */
static void DcyRect(int x0, int y0, int x1, int y1, unsigned int c, float a) {
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) DcyBar(x0, x1, y, c, a);
}
/* 虚线边框（针脚） */
static void DcyDashRect(int x0, int y0, int x1, int y1, unsigned int c, float a) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int x = x0; x <= x1; x += 2) { DcyPut(x, y0, c, a); DcyPut(x, y1, c, a); }
    for (int y = y0; y <= y1; y += 2) { DcyPut(x0, y, c, a); DcyPut(x1, y, c, a); }
}

typedef struct {
    const Decoy* d;
    float pulse;
    int cx;
    int baseY;
    int h;
    int armed;
    int legH;
    int bodyH;
    int neckH;
    int headD;
    int headR;
    int headDrop;
    int wt;
    int wb;
    int weave;
    int fine;
    float dim;
    unsigned int cEdge;
    unsigned int cLit;
    unsigned int cMid;
    unsigned int cShd;
    unsigned int cDeep;
    unsigned int cSeam;
    unsigned int cRope;
    unsigned int cRopeL;
    unsigned int cStraw;
    unsigned int cHead;
} DecoyDrawContext;

static unsigned int DecoyClothShade(const DecoyDrawContext* ctx, int px, int halfW) {
    return px * 5 < -halfW * 3 ? ctx->cLit :
           px * 5 < -halfW     ? ctx->cMid :
           px * 5 <  halfW     ? ctx->cMid :
           px * 5 <  halfW * 3 ? ctx->cShd : ctx->cDeep;
}

static void DrawDecoyLegs(const DecoyDrawContext* ctx) {
    int cx = ctx->cx;
    int baseY = ctx->baseY;
    int h = ctx->h;
    int legH = ctx->legH;
    int wb = ctx->wb;
    int weave = ctx->weave;
    int fine = ctx->fine;
    float dim = ctx->dim;
    unsigned int cEdge = ctx->cEdge;
    unsigned int cLit = ctx->cLit;
    unsigned int cMid = ctx->cMid;
    unsigned int cSeam = ctx->cSeam;

    /* ================= 腿（先画，脚在最下） ================= */
    {
        int legW = h / 22; if (legW < 1) legW = 1;
        int lx0 = cx - wb / 2;
        int lx1 = cx + wb / 2;
        /* 膝盖位置：腿高的 45% */
        int kneeY = baseY - legH * 45 / 100;
        /* 脚：腿高 14%，比腿本身宽 1px → 读作"踩在地上" */
        int footH = legH * 14 / 100; if (footH < 1) footH = 1;

        for (int i = 0; i < legH; i++) {
            int py = baseY - i;
            /* 手段③ AO：膝盖那一行压暗，让它像"折了一下"而不是"一根棍" */
            float ao = 1.0f;
            if (i == legH - (baseY - kneeY)) ao = 0.72f;
            else if (i == legH - (baseY - kneeY) - 1) ao = 0.86f;
            /* 手段④ 负空间：两腿之间留 1px 背景（若宽度够） */
            int outK = legW + 1;
            DcyPut(lx0 - outK, py, cEdge, 0.95f);
            DcyPut(lx1 + outK, py, cEdge, 0.95f);
            for (int k = -legW; k <= legW; k++) {
                /* 5 段明暗按 k/legW 走 */
                unsigned int c = DecoyClothShade(ctx, k, legW);
                /* 脚掌区域更宽 + 更亮（受光面朝上） */
                if (i < footH) { c = (k < 0) ? cLit : cMid; }
                DcyCloth(lx0 + k, py, c, dim * ao, weave);
                DcyCloth(lx1 + k, py, c, dim * ao, weave);
            }
            /* 脚掌外扩的 1px */
            if (i < footH) {
                DcyPut(lx0 - legW - 1, py, cMid, dim * 0.9f);
                DcyPut(lx1 + legW + 1, py, cMid, dim * 0.9f);
            }
        }
        /* 脚底线 + 脚踝缝（手段⑤ 虚线） */
        DcyBar(lx0 - legW - 1, lx0 + legW + 1, baseY, cSeam, 0.9f);
        DcyBar(lx1 - legW - 1, lx1 + legW + 1, baseY, cSeam, 0.9f);
        if (fine) {
            int ankY = baseY - footH - 1;
            for (int k = -legW; k <= legW; k += 2) {
                DcyPut(lx0 + k, ankY, cSeam, 0.8f);
                DcyPut(lx1 + k, ankY, cSeam, 0.8f);
            }
        }
    }
}

static void DrawDecoyArms(const DecoyDrawContext* ctx) {
    int cx = ctx->cx;
    int baseY = ctx->baseY;
    int h = ctx->h;
    int armed = ctx->armed;
    int legH = ctx->legH;
    int bodyH = ctx->bodyH;
    int wt = ctx->wt;
    int wb = ctx->wb;
    int weave = ctx->weave;
    int fine = ctx->fine;
    float dim = ctx->dim;
    unsigned int cEdge = ctx->cEdge;
    unsigned int cLit = ctx->cLit;
    unsigned int cMid = ctx->cMid;
    unsigned int cShd = ctx->cShd;
    unsigned int cSeam = ctx->cSeam;
    unsigned int cStraw = ctx->cStraw;

    /* ================================================================
     * 两条手臂 —— 关键改动：**跟着身体轮廓走**，并且留真实的负空间
     * ----------------------------------------------------------------
     * 上一版的问题：手臂画在固定 x（`cx - wb`），而身体是梨形、半宽
     * 每行都在变。结果手臂**埋进了身体里**，只剩一道竖纹，看不出是手臂。
     *
     * 现在：按每一行的 halfW 算手臂位置，往外偏 4px
     *     身体轮廓  cx-halfW-1
     *     背景缝    cx-halfW-2      ← 手段④ 负空间：真的透出背景
     *     手臂描边  cx-halfW-3
     *     手臂填充  cx-halfW-4 / -5
     *     手臂描边  cx-halfW-6
     * ============================================================== */
    {
        int bodyTop = baseY - legH - bodyH;
        /* 手臂的纵向范围：从肩膀稍下到手 */
        int armTopI = bodyH / 8;
        int armBotI = bodyH - bodyH / 10;
        if (armBotI <= armTopI) armBotI = armTopI + 1;

        #define DCY_HALFW(i) (wt + (wb - wt) * (i) / (bodyH > 1 ? bodyH - 1 : 1))

        /* 肘部：手臂范围的中点 */
        int elbowI = armTopI + (armBotI - armTopI) * 48 / 100;

        for (int i = armTopI; i <= armBotI; i++) {
            int py = bodyTop + i;
            int halfW = DCY_HALFW(i);
            int axOut  = cx - halfW - 6;    /* 外侧描边 */
            int axA    = cx - halfW - 5;    /* 填充：受光 */
            int axB    = cx - halfW - 4;    /* 填充：主体 */
            int axIn   = cx - halfW - 3;    /* 内侧描边（负空间那一侧） */
            /* 手段③ AO：肘部两行压暗，让手臂有"折了一下"的关节 */
            float ao = (i == elbowI) ? 0.72f : ((i == elbowI - 1 || i == elbowI + 1) ? 0.86f : 1.0f);

            DcyPut(axOut, py, cEdge, 0.95f);
            DcyCloth(axA, py, cLit, dim * ao, weave);
            DcyCloth(axB, py, cMid, dim * ao, weave);
            DcyPut(axIn,  py, cEdge, 0.95f);
            /* 负空间：内侧描边再往内一格保持背景（不画 = 保持原像素） */
        }

        /* 手：末端一块比手臂略宽的缝口 + 两道针脚 */
        {
            int py = bodyTop + armBotI + 1;
            int halfW = DCY_HALFW(armBotI);
            int hx0 = cx - halfW - 6;
            int hx1 = cx - halfW - 3;
            DcyBar(hx0, hx1, py,     cEdge, 0.95f);
            DcyBar(hx0, hx1, py + 1, cMid,  dim);
            DcyBar(hx0, hx1, py + 2, cEdge, 0.95f);
            if (fine) {
                DcyPut(hx0 + 1, py + 1, cLit,  dim * 0.95f);   /* 手背受光 */
                DcyPut(hx0 + 2, py + 1, cSeam, dim);           /* 针脚 */
                DcyPut(hx0 + 3, py + 1, cSeam, dim);
            }
        }

        /* 右臂：**断了**，只剩一截残根 + 锯齿断口 + 稻草 */
        {
            int stubTopI = bodyH / 4;
            int stubBotI = stubTopI + bodyH / 6;
            for (int i = stubTopI; i <= stubBotI; i++) {
                int py = bodyTop + i;
                int halfW = DCY_HALFW(i);
                int sxIn  = cx + halfW + 3;
                int sxB   = cx + halfW + 4;
                int sxA   = cx + halfW + 5;
                int sxOut = cx + halfW + 6;
                DcyPut(sxIn,  py, cEdge, 0.95f);
                DcyCloth(sxB, py, cShd, dim, weave);
                DcyCloth(sxA, py, cMid, dim, weave);
                DcyPut(sxOut, py, cEdge, 0.95f);
            }
            /* 断口：锯齿状 —— 读作"被撕开"，不是"被切齐" */
            {
                int py = bodyTop + stubBotI + 1;
                int halfW = DCY_HALFW(stubBotI);
                for (int k = 0; k <= 6; k++) {
                    int jy = py + ((k & 1) ? 0 : 1);
                    DcyPut(cx + halfW + 3 + k, jy, cSeam, 1.0f);
                }
                /* 稻草：从断口向外炸；长度用索引确定，不用 rand（保证每帧一致） */
                int nStraw = (h >= 40) ? 5 : 2;
                for (int s = 0; s < nStraw; s++) {
                    int len = h / 14 + ((s * 3) % 5);
                    int dx = (s - nStraw / 2);
                    for (int k = 0; k < len; k++) {
                        /* 形态差：鸣叫时稻草**炸开**（往外，像在震），
                           静默时**垂下来**（顺着重力） */
                        int sx2 = cx + halfW + 6 + dx + (armed ? k / 2 : 0);
                        int sy2 = py + 1 + k + (armed ? -k / 3 : 0);
                        DcyPut(sx2, sy2, cStraw,
                               0.88f - 0.05f * (float)k);
                    }
                }
            }
        }
        #undef DCY_HALFW
    }
}

static void DrawDecoyBody(const DecoyDrawContext* ctx) {
    int cx = ctx->cx;
    int baseY = ctx->baseY;
    int legH = ctx->legH;
    int bodyH = ctx->bodyH;
    int wt = ctx->wt;
    int wb = ctx->wb;
    int weave = ctx->weave;
    int fine = ctx->fine;
    float dim = ctx->dim;
    unsigned int cEdge = ctx->cEdge;
    unsigned int cShd = ctx->cShd;
    unsigned int cSeam = ctx->cSeam;

    /* ================= 身体：梨形 + 5 段明暗 + 织纹 + 两条麻袋缝 ================= */
    {
        int yTop = baseY - legH - bodyH;
        for (int i = 0; i < bodyH; i++) {
            int py = yTop + i;
            int halfW = wt + (wb - wt) * i / (bodyH > 1 ? bodyH - 1 : 1);
            DcyPut(cx - halfW - 1, py, cEdge, 0.95f);
            DcyPut(cx + halfW + 1, py, cEdge, 0.95f);
            for (int x = -halfW; x <= halfW; x++)
                DcyCloth(cx + x, py, DecoyClothShade(ctx, x, halfW), dim, weave);
        }
        /* 手段⑤ 两条麻袋竖缝：从肩走到胯，虚线 —— 让它像"布做的" */
        if (fine) {
            for (int i = 2; i < bodyH - 2; i += 2) {
                int halfW = wt + (wb - wt) * i / (bodyH > 1 ? bodyH - 1 : 1);
                DcyPut(cx - halfW / 2, yTop + i, cSeam, 0.62f);
                DcyPut(cx + halfW / 2, yTop + i, cSeam, 0.45f);
            }
        }
        /* 补丁：偏左下、比原来小、四边虚线针脚 + 内部织纹 */
        if (fine && bodyH >= 10 && wb >= 4) {
            int pw = wb * 3 / 4;  if (pw < 2) pw = 2;
            int ph = bodyH / 3;   if (ph < 2) ph = 2;
            int pxc = cx - wb / 2;
            int pyc = yTop + bodyH * 3 / 5;
            DcyRect(pxc - pw / 2, pyc, pxc + pw / 2, pyc + ph, cSeam, 0.90f * dim);
            /* 补丁内部稍亮一点（它是叠在身体上的一层布） */
            DcyRect(pxc - pw / 2 + 1, pyc + 1, pxc + pw / 2 - 1, pyc + ph - 1,
                    cShd, 0.55f * dim);
            DcyDashRect(pxc - pw / 2 - 1, pyc - 1, pxc + pw / 2 + 1, pyc + ph + 1,
                        cSeam, 0.95f);
        }
        /* ================= **修「小黑点」** =================
           用户说的「身体下面 / 腿上面那个小黑点」，是两腿之间漏出来的背景。
           原来 hem 处什么都没有，于是那 1~2 像素的暗背景看起来像破了个洞。
           现在：① 加一条下摆（hem）把身体和腿之间封住
                 ② 中间再补一条竖缝，让"两腿之间"是**有意的接缝**而不是洞 */
        {
            int hemY = baseY - legH;
            DcyBar(cx - wb - 1, cx + wb + 1, hemY,     cEdge, 0.95f);
            DcyBar(cx - wb,     cx + wb,     hemY + 1, cShd,  dim);
            DcyBar(cx - wb,     cx + wb,     hemY + 2, cEdge, 0.85f);
            /* 两腿之间：从 hem 往下 2px 的接缝，封掉那个黑洞 */
            DcyVBar(cx, hemY + 1, hemY + (fine ? 3 : 2), cSeam, 0.9f);
            if (fine) DcyBar(cx - 1, cx + 1, hemY + 1, cSeam, 0.7f);
        }
    }
}

static void DrawDecoyNeck(const DecoyDrawContext* ctx) {
    int cx = ctx->cx;
    int baseY = ctx->baseY;
    int h = ctx->h;
    int legH = ctx->legH;
    int bodyH = ctx->bodyH;
    int neckH = ctx->neckH;
    int wt = ctx->wt;
    int wb = ctx->wb;
    int fine = ctx->fine;
    float dim = ctx->dim;
    unsigned int cEdge = ctx->cEdge;
    unsigned int cSeam = ctx->cSeam;
    unsigned int cRope = ctx->cRope;
    unsigned int cRopeL = ctx->cRopeL;

    /* ================= 脖子 + 绕颈麻绳（地标） ================= */
    {
        int neckY = baseY - legH - bodyH - neckH;
        int nw = wt / 2;
        /* 脖子：深色、比身体窄 —— 头和身体不再黏在一起 */
        for (int i = 0; i < neckH; i++)
            DcyBar(cx - nw, cx + nw, neckY + i, cSeam, 0.88f);
        /* 脖子两侧的 AO：头压下来的影子 */
        DcyVBar(cx - nw - 1, neckY, neckY + neckH - 1, cEdge, 0.8f);
        DcyVBar(cx + nw + 1, neckY, neckY + neckH - 1, cEdge, 0.8f);

        int ropeY0 = neckY + (neckH > 3 ? 1 : 0);
        int ropeY1 = neckY + neckH - 2;
        if (ropeY1 < ropeY0) ropeY1 = ropeY0;
        int rw = wt + (wb - wt) / 3;

        /* 第 1 圈：描边 + 绳色 + 左上受光边（三段做圆绳的体积） */
        DcyBar(cx - rw, cx + rw, ropeY0 - 1, cEdge, 0.9f);
        DcyBar(cx - rw, cx + rw, ropeY0,     cRope, dim);
        {   int litEnd = cx + rw - rw / 2;
            DcyBar(cx - rw, litEnd, ropeY0, cRopeL, dim * 0.55f); }
        /* 第 2 圈（脖子够高才画） */
        if (neckH >= 4) DcyBar(cx - rw + 1, cx + rw - 1, ropeY1, cRope, dim);
        /* 绳结 + 垂下的绳头（右侧） */
        if (h >= 26) {
            int kx = cx + rw;
            DcyPut(kx,     ropeY0 - 1, cEdge, 1.0f);
            DcyPut(kx,     ropeY0,     cRopeL, dim);
            DcyPut(kx + 1, ropeY0,     cRope,  dim);
            DcyPut(kx,     ropeY0 + 1, cRope,  dim);
            int hang = neckH + bodyH / 5;
            for (int k = 0; k < hang; k++) {
                int hx = kx + (k > 2 ? 1 : 0);
                DcyPut(hx,     ropeY0 + 2 + k, cRope, dim * 0.92f);
                if (fine && (k & 1)) DcyPut(hx - 1, ropeY0 + 2 + k, cRopeL, dim * 0.4f);
            }
        }
    }
}

static void DrawDecoyHead(const DecoyDrawContext* ctx) {
    float pulse = ctx->pulse;
    int cx = ctx->cx;
    int baseY = ctx->baseY;
    int armed = ctx->armed;
    int legH = ctx->legH;
    int bodyH = ctx->bodyH;
    int neckH = ctx->neckH;
    int headD = ctx->headD;
    int headR = ctx->headR;
    int headDrop = ctx->headDrop;
    int weave = ctx->weave;
    float dim = ctx->dim;
    unsigned int cEdge = ctx->cEdge;
    unsigned int cLit = ctx->cLit;
    unsigned int cMid = ctx->cMid;
    unsigned int cShd = ctx->cShd;
    unsigned int cSeam = ctx->cSeam;
    unsigned int cHead = ctx->cHead;

    /* ================= 头 ================= */
    {
        int headCY = baseY - legH - bodyH - neckH - headR + headDrop;
        for (int dy = -headR - 1; dy <= headR + 1; dy++) {
            for (int dx = -headR - 1; dx <= headR + 1; dx++) {
                float dd = sqrtf((float)(dx * dx + dy * dy));
                int px = cx + dx, py = headCY + dy;
                if (dd > (float)headR) {
                    if (dd <= (float)headR + 1.2f) DcyPut(px, py, cEdge, 0.95f);
                    continue;
                }
                /* 球面 5 段：用 (-dx-dy)/D 做"左上受光"的投影量 */
                float lit = (float)(-dx - dy) / (float)(headD + 1);
                unsigned int col = (lit >  0.42f) ? cHead :
                                   (lit >  0.08f) ? cHead :
                                   (lit > -0.30f) ? cMid  : cShd;
                if (lit > 0.42f && headR >= 5) col = cLit;   /* 高光区用最亮档 */
                DcyCloth(px, py, col, dim, weave);
            }
        }
        /* 手段⑥ 边缘光：左上 1px 提亮，暗场里把剪影剥出来 */
        if (headR >= 4) {
            for (int dy = -headR; dy <= 0; dy++) {
                int dx = -(int)(sqrtf((float)(headR * headR - dy * dy)) + 0.5f);
                DcyPut(cx + dx, headCY + dy, cLit, 0.55f * dim);
            }
        }

        int eyeY = headCY - headR / 4;
        if (headR >= 3) {
            /* 纽扣眼：深色扣 + 1 像素受光点 */
            int ex = cx - headR / 2;
            int r  = (headR >= 6) ? 1 : 0;
            DcyBar(ex - r, ex + r, eyeY, 0x00282018u, 1.0f);
            if (r) {
                DcyBar(ex - r, ex + r, eyeY + 1, 0x00282018u, 1.0f);
                DcyBar(ex - r, ex + r, eyeY - 1, 0x00282018u, 1.0f);
                DcyPut(ex - 1, eyeY - 1, 0x00908870u, 0.9f);   /* 扣子受光点 */
                DcyPut(ex + 1, eyeY + 1, 0x00140E08u, 0.9f);   /* 扣眼 */
            }
            /* 空洞眼：纯黑，比纽扣眼小一圈。
               鸣叫态时**左眼（纽扣那颗）换成琥珀**——只有一颗亮，
               两眼全亮就变成"机器人在看你"，不像坏掉的玩偶。 */
            if (armed) {
                DcyPut(ex, eyeY - 1, 0x00FFC860u, 1.0f);
                if (r) DcyPut(ex - 1, eyeY - 1, 0x00FFE0A0u, 1.0f);
            }
            int ex2 = cx + headR / 2;
            DcyPut(ex2, eyeY, 0x00140E08u, 1.0f);
            if (headR >= 5) { DcyPut(ex2, eyeY + 1, 0x00140E08u, 1.0f);
                              DcyPut(ex2 + 1, eyeY, 0x00140E08u, 0.9f); }
        }

        /* ---- 嘴：**两个形态的第二个判读点** ---- */
        if (headR >= 5) {
            int my = headCY + headR / 2;
            if (!armed) {
                /* 形态 A：缝起来的十字 —— 它是"闭着"的 */
                DcyBar(cx - headR / 2, cx + headR / 3, my, cSeam, 0.92f);
                DcyVBar(cx - headR / 5, my - 1, my + 1, cSeam, 0.92f);
            } else {
                /* 形态 B：**张开的口** —— 菱形，越大越明显
                   （菱形而不是椭圆：在这个尺度上椭圆会糊成一个点） */
                int mw = headR / 2 + 1;
                int mh = headR / 3 + 1;
                for (int dy = -mh; dy <= mh; dy++) {
                    int half = mw - (mw * (dy < 0 ? -dy : dy)) / (mh + 1);
                    if (half < 0) half = 0;
                    for (int dx = -half; dx <= half; dx++)
                        DcyPut(cx + dx, my + dy, 0x00181012u, 1.0f);   /* 口内：比黑更深的暖黑 */
                }
                /* 口的上下缘：锯齿（读作"撕裂开的布"） */
                for (int k = -mw; k <= mw; k += 2) {
                    DcyPut(cx + k, my - mh - 1, cSeam, 0.9f);
                    DcyPut(cx + k + 1, my + mh + 1, cSeam, 0.7f);
                }
                /* 口腔深处一点暗红 —— 只有它让"张嘴"读得出来 */
                DcyPut(cx, my, 0x00401A14u, 1.0f);
                if (headR >= 7) DcyPut(cx, my + 1, 0x00401A14u, 0.85f);
            }
        }

        /* 激活态：头部一圈暖光（表示它在响，不把眼睛点亮成灯泡） */
        if (armed && headR >= 4) {
            int gr = headR + 1;
            for (int dy = -gr; dy <= gr; dy++) {
                for (int dx = -gr; dx <= gr; dx++) {
                    float dd = sqrtf((float)(dx * dx + dy * dy));
                    if (dd > (float)gr || dd < (float)headR) continue;
                    float a = (1.0f - dd / (float)gr) * 0.16f * pulse;
                    DcyPut(cx + dx, headCY + dy, 0x00C89040u, a);
                }
            }
        }

    }
}

static void DrawDecoyRipples(const DecoyDrawContext* ctx) {
    const Decoy* d = ctx->d;
    float pulse = ctx->pulse;
    int cx = ctx->cx;
    int baseY = ctx->baseY;
    int h = ctx->h;
    int armed = ctx->armed;
    int legH = ctx->legH;
    int bodyH = ctx->bodyH;
    int neckH = ctx->neckH;
    int headR = ctx->headR;
    int headDrop = ctx->headDrop;

    int headCY = baseY - legH - bodyH - neckH - headR + headDrop;
        /* ==================================================================
         * **声音波纹**：鸣叫态独有的第三层判读
         * ----------------------------------------------------------------
         * 两圈从头部向外扩散的弧，半径由 shoutT 驱动（1 → 0 表示
         * 这一次叫已经过去多久）。只在头部的**上半圈**画，
         * 腰线以下不画 —— 否则远处会看起来像"地上有光圈"。
         * 用点画（每 2 个像素一个）而不是实线：实线太抢戏。
         * ================================================================ */
        if (armed && h >= 24) {
            float t = 1.0f - d->shoutT / DECOY_SHOUT_EVERY;   /* 0 → 1 */
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            for (int ring = 0; ring < 2; ring++) {
                float ph = t + (float)ring * 0.5f;
                if (ph > 1.0f) ph -= 1.0f;
                float rr = (float)headR + 2.0f + ph * (float)h * 0.14f;
                float a  = (1.0f - ph) * 0.30f * pulse;
                if (a <= 0.02f) continue;
                int steps = (int)(rr * 3.2f) + 8;
                for (int s2 = 0; s2 < steps; s2 += 2) {
                    float ang = -3.14159f * (float)s2 / (float)steps;   /* 上半圈 */
                    int px = cx + (int)(cosf(ang) * rr);
                    int py = headCY + (int)(sinf(ang) * rr);
                    DcyPut(px, py, 0x00D8B070u, a);
                }
            }
        }

}

void DrawDecoyModel(int idx) {

    if (idx < 0 || idx >= MAX_DECOY) return;
    if (!decoysArr[idx].active) return;

    Decoy* d = &decoysArr[idx];

    /* 叫声脉冲：shoutT 刚重置时最大，之后衰减 —— 视觉上「它刚叫过」 */
    float pulse = 1.0f;
    if (d->armed) {
        float p = d->shoutT / DECOY_SHOUT_EVERY;      /* 1 → 0 */
        if (p > 1.0f) p = 1.0f;
        if (p > 0.82f) pulse = 1.0f + (p - 0.82f) / 0.18f * 0.22f;   /* 最高 +22% */
    }

    SpriteBox sb = ProjectSprite(d->x, d->y, 0.0f, 0.55f * pulse);
    if (!sb.ok) return;
    int cx = (sb.x0 + sb.x1) / 2;
    int baseY = sb.y1;                 /* 脚底 */
    int h = (sb.y1 - sb.y0);           /* 整体高度 */
    if (h < 8) return;                 /* 太远就别画，免得糊成一个点 */

    /* Quiet: slumped body and closed mouth. Armed: raised head and ripples. */
    const int armed = d->armed ? 1 : 0;

    /* 纵向：静默态身体被压扁，头往下沉 */
    int legH   = h * 28 / 100;
    int bodyH  = h * (armed ? 46 : 40) / 100;
    int neckH  = h * (armed ? 6 : 8) / 100;
    int headD  = h - legH - bodyH - neckH;
    if (headD < 3) headD = 3;
    int headR  = headD / 2;
    if (headR < 2) headR = 2;

    /* 头低垂：静默态往下沉，几乎埋进肩膀 —— 最直观的形态差 */
    int headDrop = armed ? 0 : (h / 16);

    /* 横向：鸣叫时胸口撑开，静默时塌下去 */
    int wt = h / 11 + (armed ? 1 : 0);  if (wt < 2) wt = 2;
    int wb = h / 8;                     if (wb <= wt) wb = wt + 1;

    /* ---- 尺寸门槛：够大才开高成本细节 ---- */
    const int weave = (h >= 34);        /* 织纹：太小会变噪点 */
    const int fine  = (h >= 48);        /* 精细件：膝盖/脚/补丁 */

    /* ---- 调色板：和 sprites.c 的 SPR_ITEM_DECOY 对齐 ---- */
    float dim  = d->armed ? 1.0f : 0.78f;
    const unsigned int cEdge = 0x00161A1Cu;
    const unsigned int cLit  = d->armed ? 0x00B4A88Cu : 0x00887E68u;
    const unsigned int cMid  = d->armed ? 0x00938A72u : 0x006E6656u;
    const unsigned int cShd  = d->armed ? 0x00625C4Eu : 0x00463F36u;
    const unsigned int cDeep = d->armed ? 0x00403A30u : 0x002C2720u;  /* 5 段的第 5 档 */
    const unsigned int cSeam = 0x00302820u;
    const unsigned int cRope = 0x00806848u;
    const unsigned int cRopeL= 0x00B09060u;
    const unsigned int cStraw= 0x00C8A040u;
    const unsigned int cHead = d->armed ? 0x00C0B498u : 0x008A8070u;

    const DecoyDrawContext ctx = {
        d, pulse, cx, baseY, h, armed,
        legH, bodyH, neckH, headD, headR, headDrop,
        wt, wb, weave, fine, dim,
        cEdge, cLit, cMid, cShd, cDeep, cSeam,
        cRope, cRopeL, cStraw, cHead
    };

    DrawDecoyLegs(&ctx);
    DrawDecoyArms(&ctx);
    DrawDecoyBody(&ctx);
    DrawDecoyNeck(&ctx);
    DrawDecoyHead(&ctx);
    DrawDecoyRipples(&ctx);
}

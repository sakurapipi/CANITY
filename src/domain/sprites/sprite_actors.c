#include "sprite_bake.h"
#include "common.h"
#include "render.h"
#include "art.h"

static void Limb(float x0, float y0, float x1, float y1, float w0, float w1,
                 unsigned int edge, unsigned int body, unsigned int hi) {
    float dx = x1 - x0, dy = y1 - y0;
    float L = sqrtf(dx * dx + dy * dy);
    if (L < 0.001f) return;
    float nx = -dy / L, ny = dx / L;

    float e[8] = {
        x0 + nx * (w0 * 0.5f + 2), y0 + ny * (w0 * 0.5f + 2),
        x1 + nx * (w1 * 0.5f + 2), y1 + ny * (w1 * 0.5f + 2),
        x1 - nx * (w1 * 0.5f + 2), y1 - ny * (w1 * 0.5f + 2),
        x0 - nx * (w0 * 0.5f + 2), y0 - ny * (w0 * 0.5f + 2)
    };
    cvPoly(e, 4, edge, 1.0f);

    float b[8] = {
        x0 + nx * (w0 * 0.5f), y0 + ny * (w0 * 0.5f),
        x1 + nx * (w1 * 0.5f), y1 + ny * (w1 * 0.5f),
        x1 - nx * (w1 * 0.5f), y1 - ny * (w1 * 0.5f),
        x0 - nx * (w0 * 0.5f), y0 - ny * (w0 * 0.5f)
    };
    cvPolyGrad(b, 4, hi, body, 1.0f);
}

/* v2.4.3: 回响的竖直中心。原来误用了 c(=CANVAS_W*0.5) 当 Y，
   导致绘制跑到画布外、越界写，把后续精灵烘焙一起搞坏。 */
#define ECHO_CY 150.0f

typedef struct MonsterBakeContext {
    int kind;
    int pose;
    float c;
    int side;
    int walk;
    float baseY;
    unsigned int edge;
    unsigned int body;
    unsigned int hi;
    float H;
    float footY;
    float headY;
    float cx;
} MonsterBakeContext;

/* Variant: kind == 10 */
static void BakeMonsterEcho(const MonsterBakeContext* bake) {
    float c = bake->c; (void)c;
    unsigned int edge = bake->edge; (void)edge;
    unsigned int body = bake->body; (void)body;
    unsigned int hi = bake->hi; (void)hi;
    float cx = bake->cx; (void)cx;
    (void)bake;
    {

        /* 三层同心环，越外越暗 */
        for (int r3 = 3; r3 >= 1; r3--) {
            float rr = 10.0f + (float)r3 * 14.0f;
            int seg = 26;
            for (int i2 = 0; i2 < seg; i2++) {
                float a0 = (float)i2 / (float)seg * 6.2831853f;
                float a1 = (float)(i2 + 1) / (float)seg * 6.2831853f;
                float x0 = cx + cosf(a0) * rr, y0 = c + cosf(a0) * 0.0f + 250.0f + sinf(a0) * rr * 0.55f;
                float x1 = cx + cosf(a1) * rr, y1 = ECHO_CY + sinf(a1) * rr * 0.55f;
                float al = (r3 == 1) ? 0.85f : (r3 == 2 ? 0.45f : 0.22f);
                cvLine(x0, y0, x1, y1, (r3 == 1) ? 3.0f : 2.0f, hi, al);
            }
        }
        /* 中心尖角菱形核心 */
        {
            float cy2 = ECHO_CY;
            float d[8] = { cx, cy2 - 26.0f,
                           cx + 17.0f, cy2,
                           cx, cy2 + 26.0f,
                           cx - 17.0f, cy2 };
            cvPoly(d, 4, edge, 1.0f);
            cvPolyGrad(d, 4, hi, body, 1.0f);
            /* 核心高光 */
            cvEllipse(cx, cy2, 6.0f, 9.0f, 0x00FFFFFFu, 0.9f);
        }
        /* 上下各一缕外泄的尖刺（不对称：上面长、下面短） */
        cvLine(cx, ECHO_CY - 26.0f, cx - 4.0f, ECHO_CY - 62.0f, 3.0f, hi, 0.5f);
        cvLine(cx, ECHO_CY + 26.0f, cx + 3.0f, ECHO_CY + 44.0f, 3.0f, hi, 0.5f);

    }
}

/* Variant: kind == 13 */
static void BakeMonsterMimic(const MonsterBakeContext* bake) {
    float c = bake->c; (void)c;
    float baseY = bake->baseY; (void)baseY;
    unsigned int body = bake->body; (void)body;
    (void)bake;
    {

        /* ============================================================
         * 宝箱怪现形态（2026-11-05）
         * ------------------------------------------------------------
         * 填黑剪影的四个地标：
         *   ① **掀开的盖子**（像宝箱盖一样向后翻）—— 最强的识别特征
         *   ② 四条短粗腿（箱子不会走路，这是"它活了"的证据）
         *   ③ **横贯上部的宽口**（比任何一只怪的嘴都宽）
         *   ④ 背甲的木板纹路 + 金包边 —— 和伪装态同源（铁律七）
         * ============================================================ */
        float bw = 44.0f;                 /* 比普通箱略宽：它撑开了 */
        float topY = baseY - 92.0f;
        float botY = baseY - 18.0f;

        /* ---- ① 掀开的盖子（向后翻 55°）---- */
        {
            float lid[8] = { c - bw, topY,
                             c + bw, topY,
                             c + bw - 16.0f, topY - 52.0f,
                             c - bw - 16.0f, topY - 52.0f };
            cvPoly(lid, 4, 0x00301408u, 1.0f);
            float lid2[8] = { c - bw + 4, topY - 2,
                              c + bw - 4, topY - 2,
                              c + bw - 20.0f, topY - 46.0f,
                              c - bw - 12.0f, topY - 46.0f };
            cvPolyGrad(lid2, 4, 0x00906A3Cu, 0x00301C0Cu, 1.0f);
            /* 盖子内侧是肉 —— 这点红让"箱子"立刻变成"生物" */
            cvLine(c - bw + 8, topY - 6, c + bw - 8, topY - 6, 7.0f, 0x00501818u, 1.0f);
            cvLine(c - bw + 10, topY - 6, c + bw - 10, topY - 6, 3.0f, 0x00A03028u, 0.85f);
        }

        /* ---- ② 口腔（宽口，横贯上部）---- */
        {
            cvLine(c - bw + 6, topY + 12, c + bw - 6, topY + 12, 26.0f, 0x00060404u, 1.0f);
            /* 上下两排牙：上排 9 颗、下排 9 颗，参差不齐 */
            for (int i = 0; i < 9; i++) {
                float tx = c - bw + 12.0f + (float)i * ((bw * 2.0f - 24.0f) / 8.0f);
                float h = 7.0f + (float)((i * 37) % 5);
                cvLine(tx, topY + 2, tx, topY + 2 + h, 4.0f, 0x00E8E0C8u, 1.0f);
                cvLine(tx + 2.0f, topY + 24, tx + 2.0f, topY + 24 - h, 4.0f, 0x00D8D0B0u, 1.0f);
            }
        }

        /* ---- ③ 箱体（和伪装态同一个色系）---- */
        {
            float body[8] = { c - bw, topY + 24, c + bw, topY + 24, c + bw, botY, c - bw, botY };
            cvPoly(body, 4, 0x00080604u, 1.0f);
            float body2[8] = { c - bw + 5, topY + 28, c + bw - 5, topY + 28,
                               c + bw - 5, botY - 4, c - bw + 5, botY - 4 };
            cvPolyGrad(body2, 4, 0x00885E32u, 0x00301C0Cu, 1.0f);
            /* 木板纹路：和伪装态一致（铁律七：让玩家认出是同一只） */
            for (int i = 1; i < 3; i++)
                cvLine(c - bw + 6, topY + 28 + (float)i * 22.0f,
                       c + bw - 6, topY + 28 + (float)i * 22.0f, 3.0f, 0x00281408u, 0.8f);
            /* 金包边留在两侧 —— 这就是"豪华箱子"的残留 */
            cvLine(c - bw + 6, topY + 28, c - bw + 6, botY - 4, 6.0f, 0x00B89238u, 0.95f);
            cvLine(c + bw - 6, topY + 28, c + bw - 6, botY - 4, 6.0f, 0x00B89238u, 0.95f);
        }

        /* ---- ④ 四条短粗腿（两条在前、两条在后，剪影错开）---- */
        {
            float legTop = botY - 2.0f;
            float legY[4] = { c - bw + 10, c - 8, c + 8, c + bw - 10 };
            float legL[4] = { 20.0f, 26.0f, 26.0f, 20.0f };
            for (int i = 0; i < 4; i++) {
                float lx = legY[i];
                float ly = legTop + legL[i];
                cvLine(lx, legTop, lx - 3.0f, ly, 9.0f, 0x002A1A0Eu, 1.0f);
                cvLine(lx - 1.0f, legTop, lx - 3.0f, ly, 5.0f, 0x005A3E22u, 0.95f);
                cvEllipse(lx - 3.5f, ly, 6.0f, 3.0f, 0x00140C06u, 1.0f);
            }
        }

        /* ---- ⑤ 两点眼：在口腔上方，极小的红 —— 铁律九（威胁要高对比）---- */
        cvEllipse(c - 16.0f, topY - 4.0f, 3.5f, 3.0f, 0x00FF4020u, 1.0f);
        cvEllipse(c + 16.0f, topY - 4.0f, 3.5f, 3.0f, 0x00FF4020u, 1.0f);

    }
}

/* Variant: kind == 0 || kind == 1 || kind == 2 || kind == 7 || kind == 8 || kind == 9 || kind == 11 || kind == 12 */
typedef struct {
    const MonsterBakeContext* mon;   /* Geometry source; ordinary member names are now safe as well. */
    float shoulderW;
    float shoulderY;
    float waistY;
    float pelvisY;
    float hry;
    float hrx;
    float hy;
} HumanoidBakeContext;

static void BakeHumanoidLegs(const HumanoidBakeContext* shape) {
    int pose = shape->mon->pose; (void)pose;
    int walk = shape->mon->walk; (void)walk;
    unsigned int edge = shape->mon->edge; (void)edge;
    unsigned int body = shape->mon->body; (void)body;
    unsigned int hi = shape->mon->hi; (void)hi;
    float footY = shape->mon->footY; (void)footY;
    float cx = shape->mon->cx; (void)cx;
    float shoulderW = shape->shoulderW; (void)shoulderW;
    float pelvisY = shape->pelvisY; (void)pelvisY;
    (void)shape;
    {
        /* --- 腿：大腿 + 小腿 + 脚，两段各带一点弯 --- */
        /* v3.0：**迈步**而不是「两腿对称外张」（建模审查 #2）。
           原来走路帧两腿同时外张（膝 ±5.8px、踝 ±0.1px），
           看起来是蹲一下，脚还随 baseY 往地里沉 4px。
           现在：一条腿往前伸、另一条往后蹬，髋部抬高、身体微倾。
           walkDir 让 pose1 和 pose3 的迈步方向相反，两个走路帧才不一样。 */
        int walkDir = (pose == 3) ? -1 : 1;
        float stepAmt = walk ? 9.0f : 0.0f;
        int crouched = shape->mon->kind == 0;
        for (int s2 = -1; s2 <= 1; s2 += 2) {
            float hx = cx + (float)s2 * shoulderW * 0.34f;
            /* s2 * walkDir = +1 的那条腿在前，-1 的在后 */
            float ph = (float)(s2 * walkDir);
            float kneeX = hx + ph * stepAmt * 0.55f + (crouched ? (float)s2 * 4.0f : 0.0f);
            float kneeY = (pelvisY + footY) * 0.5f - ph * stepAmt * 0.20f + (crouched ? 4.0f : 0.0f);
            float ankleX = hx + ph * stepAmt * 0.95f - (crouched ? (float)s2 * 1.5f : 0.0f);
            Limb(hx, pelvisY, kneeX, kneeY, 13.0f, 10.0f, edge, body, hi);
            Limb(kneeX, kneeY, ankleX, footY - 4.0f, 10.0f, 7.0f, edge, body, hi);
            cvEllipse(ankleX, footY - 3.0f - (walk ? ph * 2.0f : 0.0f), 7.0f, 3.4f, edge, 1.0f);
        }


    }
}

static void BakeHumanoidTorso(const HumanoidBakeContext* shape) {
    unsigned int edge = shape->mon->edge; (void)edge;
    unsigned int body = shape->mon->body; (void)body;
    unsigned int hi = shape->mon->hi; (void)hi;
    float cx = shape->mon->cx; (void)cx;
    float shoulderW = shape->shoulderW; (void)shoulderW;
    float shoulderY = shape->shoulderY; (void)shoulderY;
    float waistY = shape->waistY; (void)waistY;
    float pelvisY = shape->pelvisY; (void)pelvisY;
    (void)shape;
    {
        /* --- 骨盆 --- */
        float pel[8] = { cx - shoulderW * 0.36f, pelvisY - 10,
                         cx + shoulderW * 0.36f, pelvisY - 10,
                         cx + shoulderW * 0.30f, pelvisY + 8,
                         cx - shoulderW * 0.30f, pelvisY + 8 };
        cvPoly(pel, 4, edge, 1.0f);
        cvPolyGrad(pel, 4, hi, body, 1.0f);

        /* --- 躯干：肩宽 → 腰窄 → 骨盆，用 6 边形 --- */
        float torso[12] = {
            cx - shoulderW, shoulderY,
            cx + shoulderW, shoulderY,
            cx + shoulderW * 0.62f, waistY,
            cx + shoulderW * 0.36f, pelvisY,
            cx - shoulderW * 0.36f, pelvisY,
            cx - shoulderW * 0.62f, waistY
        };
        float outline[12];
        for (int i = 0; i < 6; i++) {
            outline[i * 2] = cx + (torso[i * 2] - cx) * 1.06f;
            outline[i * 2 + 1] = torso[i * 2 + 1];
        }
        cvPoly(outline, 6, edge, 1.0f);
        cvPolyGrad(torso, 6, hi, body, 1.0f);

        /* --- 肋骨：5 道弧线 --- */
        for (int i = 0; i < 5; i++) {
            float ry = shoulderY + 10.0f + (float)i * ((waistY - shoulderY) * 0.16f);
            float rw = shoulderW * (0.88f - (float)i * 0.10f);
            cvLine(cx - rw, ry, cx + rw, ry + 2.0f, 2.0f, edge, 0.5f);
        }
        /* 脊柱中线 */
        cvLine(cx, shoulderY + 4, cx, pelvisY, 2.0f, edge, 0.6f);


    }
}

static void BakeHumanoidArms(const HumanoidBakeContext* shape) {
    int walk = shape->mon->walk; (void)walk;
    unsigned int edge = shape->mon->edge; (void)edge;
    unsigned int body = shape->mon->body; (void)body;
    unsigned int hi = shape->mon->hi; (void)hi;
    float H = shape->mon->H; (void)H;
    float cx = shape->mon->cx; (void)cx;
    float shoulderW = shape->shoulderW; (void)shoulderW;
    float shoulderY = shape->shoulderY; (void)shoulderY;
    (void)shape;
    {
        /* --- 手臂：上臂 + 前臂 + 爪 --- */
        float armSwing = walk ? 0.0f : 0.0f;
        int hunched = shape->mon->kind == 0;
        for (int s2 = -1; s2 <= 1; s2 += 2) {
            float sx = cx + (float)s2 * shoulderW * 0.92f;
            float ex = cx + (float)s2 * (shoulderW + 10.0f) + armSwing;
            float ey = shoulderY + H * (hunched ? 0.245f : 0.20f);
            float wx = cx + (float)s2 * (shoulderW + (hunched ? 3.0f : 6.0f));
            float wy = shoulderY + H * (hunched ? 0.455f : 0.34f) + (walk ? (float)s2 * 6.0f : 0.0f);
            Limb(sx, shoulderY, ex, ey, 11.0f, 8.0f, edge, body, hi);
            Limb(ex, ey, wx, wy, 8.0f, 6.0f, edge, body, hi);
            /* 三根手指：听声鬼的爪子垂到膝下 */
            float claw = hunched ? 18.0f : 11.0f;
            for (int f = -1; f <= 1; f++) {
                float fx = wx + (float)f * 3.0f + (float)s2 * 2.0f;
                Limb(wx, wy, fx, wy + claw, 3.0f, 1.4f, edge, body, hi);
            }
        }


    }
}

static void BakeHumanoidNeck(const HumanoidBakeContext* shape) {
    int kind = shape->mon->kind; (void)kind;
    unsigned int edge = shape->mon->edge; (void)edge;
    unsigned int body = shape->mon->body; (void)body;
    unsigned int hi = shape->mon->hi; (void)hi;
    float H = shape->mon->H; (void)H;
    float headY = shape->mon->headY; (void)headY;
    float cx = shape->mon->cx; (void)cx;
    float shoulderY = shape->shoulderY; (void)shoulderY;
    (void)shape;
    {
        /* --- 脖子 ---
           长颈变体的地标就在这：脖子拉到 0.34H、细到 6px，
           于是**头、脖子、肩三段之间出现明显的负空间**，
           填黑之后轮廓是「一根竖杆顶个小球」，和第一只完全不同（铁律一/二）。 */
        if (kind == 12) {
            Limb(cx, headY + H * 0.12f, cx, shoulderY + 2.0f, 9.0f, 6.0f, edge, body, hi);
        } else if (kind == 11) {
            /* 匍匐者：几乎没有脖子，头直接压在肩上 */
            Limb(cx, headY + H * 0.24f, cx, shoulderY + 2.0f, 15.0f, 19.0f, edge, body, hi);
        } else {
            Limb(cx, headY + H * 0.19f, cx, shoulderY + 2.0f, 13.0f, 17.0f, edge, body, hi);
        }


    }
}

static void BakeHumanoidHead(const HumanoidBakeContext* shape) {
    unsigned int edge = shape->mon->edge; (void)edge;
    unsigned int body = shape->mon->body; (void)body;
    unsigned int hi = shape->mon->hi; (void)hi;
    float cx = shape->mon->cx; (void)cx;
    float hry = shape->hry; (void)hry;
    float hrx = shape->hrx; (void)hrx;
    float hy = shape->hy; (void)hy;
    (void)shape;
    {
        cvEllipse(cx, hy, hrx + 2.0f, hry + 2.0f, edge, 1.0f);
        cvEllipse(cx, hy - 1.0f, hrx, hry, body, 1.0f);
        cvEllipse(cx - hrx * 0.30f, hy - hry * 0.30f, hrx * 0.45f, hry * 0.40f, hi, 0.75f);


    }
}

static void BakeHumanoidCollectorDetails(const HumanoidBakeContext* shape) {
    unsigned int edge = shape->mon->edge; (void)edge;
    unsigned int body = shape->mon->body; (void)body;
    unsigned int hi = shape->mon->hi; (void)hi;
    float H = shape->mon->H; (void)H;
    float cx = shape->mon->cx; (void)cx;
    float shoulderW = shape->shoulderW; (void)shoulderW;
    float shoulderY = shape->shoulderY; (void)shoulderY;
    float hry = shape->hry; (void)hry;
    float hrx = shape->hrx; (void)hrx;
    float hy = shape->hy; (void)hy;
    (void)shape;
    {

            /* --- 拾荒者：独眼 + 背上的大麻袋 --- */
            /* 麻袋：一个大圆，最高点超过头顶（地标 #1） */
            float bagX = cx - shoulderW * 1.55f;
            float bagY = shoulderY + H * 0.02f;
            /* 口袋是唯一的大面积色块 —— 暗金，让「那是钱袋」一眼可辨 */
            cvEllipse(bagX, bagY, shoulderW * 1.25f, H * 0.20f, 0x00140C04u, 1.0f);
            /* v2.4.66：麻袋用洋红 key 画 —— 运行时按「有没有赃款」换成白/金 */
            cvEllipse(bagX, bagY, shoulderW * 1.10f, H * 0.175f, 0x00FF00FFu, 1.0f);
            cvEllipse(bagX - shoulderW * 0.28f, bagY - H * 0.05f,
                      shoulderW * 0.42f, H * 0.065f, 0x00FF40FFu, 0.9f);
            /* 袋口绳子 */
            cvLine(bagX + shoulderW * 0.9f, bagY - H * 0.13f,
                   cx - shoulderW * 0.5f, shoulderY + H * 0.06f, 3.0f, edge, 0.9f);
            /* 独眼（铁律五：eye-count wrong） */
            cvEllipse(cx + hrx * 0.10f, hy - 2.0f, hrx * 0.52f, hry * 0.46f, 0x00040206u, 1.0f);
            cvEllipse(cx + hrx * 0.12f, hy - 2.0f, hrx * 0.26f, hry * 0.24f, 0x00FFD070u, 0.95f);
            /* 护着口袋的那只手已经由手臂画出，这里加一只更靠前的 */
            Limb(cx + shoulderW * 0.95f, shoulderY + H * 0.18f,
                 bagX + shoulderW * 0.85f, bagY + H * 0.02f,
                 10.0f, 7.0f, edge, body, hi);

    }
}

static void BakeHumanoidScavengerDetails(const HumanoidBakeContext* shape) {
    unsigned int edge = shape->mon->edge; (void)edge;
    unsigned int body = shape->mon->body; (void)body;
    unsigned int hi = shape->mon->hi; (void)hi;
    float H = shape->mon->H; (void)H;
    float footY = shape->mon->footY; (void)footY;
    float cx = shape->mon->cx; (void)cx;
    float shoulderW = shape->shoulderW; (void)shoulderW;
    float shoulderY = shape->shoulderY; (void)shoulderY;
    float hry = shape->hry; (void)hry;
    float hrx = shape->hrx; (void)hrx;
    float hy = shape->hy; (void)hy;
    (void)shape;
    {

            /* --- 清道夫：无眼 + 两条垂到地上的长臂（地标 #1）--- */
            for (int a2 = -1; a2 <= 1; a2 += 2) {
                /* 不对称：右侧手臂更长（铁律二 ASYMMETRY） */
                float extra = (a2 > 0) ? 18.0f : 0.0f;
                float ax0 = cx + (float)a2 * shoulderW * 1.05f;
                float ay0 = shoulderY + H * 0.10f;
                float ax1 = cx + (float)a2 * (shoulderW * 1.35f);
                float ay1 = footY + extra;           /* 垂到地面以下 */
                Limb(ax0, ay0, ax1, ay1, 11.0f, 6.0f, edge, body, hi);
                /* 爪：五根，比正常人多两根 */
                for (int f = -2; f <= 2; f++) {
                    /* 爪尖给一点病绿 —— 小面积点缀，不破坏整体暗色 */
                    Limb(ax1, ay1 + 8.0f, ax1 + (float)f * 3.2f, ay1 + 14.0f,
                         2.0f, 0.9f, 0x00101810u, 0x004A7040u, 0x0070A060u);
                }
            }
            /* 无眼：只在脸上留两个深到看不见底的凹坑 */
            cvEllipse(cx - hrx * 0.34f, hy - 3.0f, hrx * 0.40f, hry * 0.34f, 0x00000000u, 1.0f);
            cvEllipse(cx + hrx * 0.34f, hy - 3.0f, hrx * 0.40f, hry * 0.34f, 0x00000000u, 1.0f);
            /* 咧开的裂缝嘴 */
            cvLine(cx - hrx * 0.55f, hy + hry * 0.55f,
                   cx + hrx * 0.55f, hy + hry * 0.40f, 3.0f, 0x00020303u, 0.95f);

    }
}

static void BakeHumanoidWardenDetails(const HumanoidBakeContext* shape) {
    int pose = shape->mon->pose; (void)pose;
    float footY = shape->mon->footY; (void)footY;
    float headY = shape->mon->headY; (void)headY;
    float cx = shape->mon->cx; (void)cx;
    float shoulderW = shape->shoulderW; (void)shoulderW;
    float shoulderY = shape->shoulderY; (void)shoulderY;
    float hrx = shape->hrx; (void)hrx;
    (void)shape;
    {

            /* ================================================================
             * v2.4.86 **守门人：按审美铁律重做（第 3 版）**
             * ----------------------------------------------------------------
             * 上一版被用户批「像水桶、没有特征、头部错乱」。
             * 复盘：
             *   · 身体是一个没有收放的矩形 → 填黑就是个桶
             *   · 肩膀是一条水平横杠 → 和别的怪的横线没区别
             *   · 脸是盒子 + 横排眼 → 沉默、无表情
             *
             * 这一版按 art_direction.h 的三条硬指标重画：
             *
             *   铁律一 填黑测试：剪影必须是「尖顶 + 双角 + 收腰长袍 + 长杖」
             *   铁律二 地标：**头上那根竖直的提灯杖**是全场唯一的超长直线
             *   铁律三 只三个特征：尖顶 / 双角肩 / 竖排三眼
             *
             * 特意避开的东西：
             *   · 不再用横条形肩膀（那是"桶"的来源）
             *   · 眼睛改成**竖排**（全场唯一），横排太普通
             *   · 身体做成上窄下宽的**收腰长袍**，剪影有方向
             * ================================================================ */
            {
                /* ---- 五个色值，全部蓝灰（没有一个绿）---- */
                unsigned int wEdge = 0x0006080Bu;
                unsigned int wDeep = 0x00161A21u;
                unsigned int wBody = 0x00283039u;
                unsigned int wLite = 0x0046505Eu;
                unsigned int wEye  = (pose == 2) ? 0x00FF2A18u : 0x0058B8F0u;

                float topY = headY - 16.0f;          /* 尖顶最高点 */

                /* ---------- ① 尖顶兜帽：三角，剪影的最高点 ---------- */
                {
                    float hood[8] = {
                        cx, topY,
                        cx + hrx * 1.15f, topY + 20.0f,
                        cx,              topY + 26.0f,
                        cx - hrx * 1.15f, topY + 20.0f
                    };
                    cvPoly(hood, 4, wEdge, 1.0f);
                    float hood2[8] = {
                        cx, topY + 3.0f,
                        cx + hrx * 0.95f, topY + 19.0f,
                        cx,              topY + 23.0f,
                        cx - hrx * 0.95f, topY + 19.0f
                    };
                    cvPolyGrad(hood2, 4, wLite, wBody, 1.0f);
                    /* 兜帽中缝 —— 一条竖线，把三角分成左右两片 */
                    cvLine(cx, topY + 4.0f, cx, topY + 22.0f, 2.0f, wDeep, 0.9f);
                }

                /* ---------- ② 脸：窄长竖条，**三只眼竖排**（全场唯一） ---------- */
                {
                    float fw = hrx * 0.72f;
                    float f0 = topY + 18.0f;
                    float f1 = topY + 44.0f;
                    float face[8] = { cx - fw, f0, cx + fw, f0, cx + fw, f1, cx - fw, f1 };
                    cvPoly(face, 4, wEdge, 1.0f);
                    cvPoly(face, 4, wDeep, 1.0f);

                    /* 竖排三眼 */
                    for (int e = 0; e < 3; e++) {
                        float ey = f0 + 5.0f + (float)e * 7.0f;
                        cvLine(cx - fw * 0.62f, ey, cx + fw * 0.62f, ey, 3.0f, wEye, 1.0f);
                    }
                }

                /* ---------- ③ 双角肩：左右各一个**向上的尖角** ---------- */
                {
                    float sw = shoulderW * 1.55f;
                    /* 左角 */
                    float lh[8] = { cx - sw, shoulderY + 4.0f,
                                    cx - sw * 0.30f, shoulderY - 2.0f,
                                    cx - sw * 0.55f, shoulderY + 12.0f,
                                    cx - sw * 0.95f, shoulderY + 14.0f };
                    cvPoly(lh, 4, wEdge, 1.0f);
                    cvPolyGrad(lh, 4, wLite, wBody, 1.0f);
                    /* 右角（不对称：略高 —— 铁律二 ASYMMETRY） */
                    float rh[8] = { cx + sw, shoulderY - 2.0f,
                                    cx + sw * 0.30f, shoulderY + 2.0f,
                                    cx + sw * 0.55f, shoulderY + 14.0f,
                                    cx + sw * 0.95f, shoulderY + 12.0f };
                    cvPoly(rh, 4, wEdge, 1.0f);
                    cvPolyGrad(rh, 4, wLite, wBody, 1.0f);
                }

                /* ---------- ④ 收腰长袍：上窄下宽，剪影有方向 ---------- */
                {
                    float r0 = shoulderY + 8.0f;
                    float r1 = footY + 6.0f;
                    float wTop = shoulderW * 0.72f;
                    float wMid = shoulderW * 0.62f;     /* 腰最细 */
                    float wBot = shoulderW * 1.30f;     /* 下摆最宽 */

                    /* 上半段（肩 → 腰） */
                    float up[8] = { cx - wTop, r0, cx + wTop, r0,
                                    cx + wMid, r0 + (r1 - r0) * 0.42f,
                                    cx - wMid, r0 + (r1 - r0) * 0.42f };
                    cvPoly(up, 4, wEdge, 1.0f);
                    cvPolyGrad(up, 4, wLite, wBody, 1.0f);

                    /* 下半段（腰 → 下摆） */
                    float dn[8] = { cx - wMid, r0 + (r1 - r0) * 0.42f,
                                    cx + wMid, r0 + (r1 - r0) * 0.42f,
                                    cx + wBot, r1,
                                    cx - wBot, r1 };
                    cvPoly(dn, 4, wEdge, 1.0f);
                    cvPolyGrad(dn, 4, wLite, wBody, 1.0f);

                    /* 腰带：一条横线，把上下两段分开 */
                    cvLine(cx - wMid - 1.0f, r0 + (r1 - r0) * 0.42f,
                           cx + wMid + 1.0f, r0 + (r1 - r0) * 0.42f,
                           4.0f, wDeep, 1.0f);

                    /* 下摆三道褶 —— 让"袍"读得出来，不是平板 */
                    for (int f = -1; f <= 1; f++) {
                        float fx = cx + (float)f * wBot * 0.45f;
                        cvLine(fx, r1 - (r1 - r0) * 0.30f, fx, r1 - 2.0f,
                               2.0f, wDeep, 0.75f);
                    }
                }

                /* ---------- ⑤ 提灯杖：右侧**一根超长竖杖**（地标 #1） ---------- */
                {
                    float sx = cx + shoulderW * 2.15f;
                    float s0 = topY + 6.0f;              /* 高过头顶 */
                    float s1 = footY - 4.0f;

                    /* 杖身 */
                    cvLine(sx, s0, sx, s1, 5.0f, wEdge, 1.0f);
                    cvLine(sx, s0 + 2.0f, sx, s1 - 2.0f, 3.0f, 0x005A4A32u, 1.0f);

                    /* 杖顶的灯 */
                    cvEllipse(sx, s0 - 2.0f, 11.0f, 13.0f, wEdge, 1.0f);
                    cvEllipse(sx, s0 - 2.0f, 7.0f, 9.0f, 0x00FFE8A0u, 1.0f);
                    cvEllipse(sx - 2.0f, s0 - 5.0f, 3.0f, 4.0f, 0x00FFFFFFu, 0.9f);

                    /* 杖下端的配重球 */
                    cvEllipse(sx, s1, 6.0f, 7.0f, wEdge, 1.0f);
                    cvEllipse(sx, s1, 4.0f, 5.0f, wBody, 1.0f);
                }
            }

    }
}

static void BakeHumanoidHunterFace(const HumanoidBakeContext* shape) {
    float cx = shape->mon->cx; (void)cx;
    float hry = shape->hry; (void)hry;
    float hrx = shape->hrx; (void)hrx;
    float hy = shape->hy; (void)hy;
    (void)shape;
    {

            /* 寻声鬼：没有眼睛 —— 它靠听。取而代之的是一张咧开的下颌。 */
            float jaw[8] = { cx - hrx * 1.00f, hy + 4,
                             cx + hrx * 1.00f, hy + 4,
                             cx + hrx * 0.62f, hy + hry + 16.0f,
                             cx - hrx * 0.62f, hy + hry + 16.0f };
            cvPoly(jaw, 4, 0x00040208u, 1.0f);
            /* 牙：五枚短牙，别做成灯罩栅栏 */
            for (int i = 0; i < 5; i++) {
                float tx = cx - hrx * 0.66f + (float)i * (hrx * 0.33f);
                cvLine(tx, hy + 6, tx + 1.0f, hy + 10.5f, 1.6f, 0x009A927Au, 0.82f);
                cvLine(tx, hy + hry + 11, tx + 1.0f, hy + hry + 6, 1.6f, 0x008C856Eu, 0.82f);
            }
            /* v3.6：眼窝改成**缝死的横向眼缝**。
               每边一道短黑缝 + 一个交叉针脚；远看是无眼，近看是缝眼。 */
            for (int side = -1; side <= 1; side += 2) {
                float ex = cx + (float)side * hrx * 0.48f;
                float ey = hy - 2.0f;
                cvLine(ex - 4.5f, ey - 0.8f, ex + 4.5f, ey - 0.8f, 2.0f, 0x00020305u, 0.95f);
                cvLine(ex - 3.0f, ey - 3.4f, ex + 3.0f, ey + 3.2f, 0.75f, 0x00B7AA90u, 0.78f);
                cvLine(ex + 3.0f, ey - 3.4f, ex - 3.0f, ey + 3.2f, 0.65f, 0x008C806Cu, 0.62f);
            }

    }
}

static void BakeHumanoidCrawlerFace(const HumanoidBakeContext* shape) {
    float cx = shape->mon->cx; (void)cx;
    float hry = shape->hry; (void)hry;
    float hrx = shape->hrx; (void)hrx;
    float hy = shape->hy; (void)hy;
    (void)shape;
    {

            /* ===== 听声鬼·匍匐者（第二只）=====
               同族识别点：无眼、牙齿下颌 —— 保住。
               自己的地标：**下颌横向铺得比头还宽**，而且牙是密集短牙，
               不是第一只那种 5 根长牙。
               轮廓读法：一个横着的梯形压在窄肩上 = 在贴地听。 */
            float jaw[8] = { cx - hrx * 1.05f, hy + 3,
                             cx + hrx * 1.05f, hy + 3,
                             cx + hrx * 0.78f, hy + hry + 7.0f,
                             cx - hrx * 0.78f, hy + hry + 7.0f };
            cvPoly(jaw, 4, 0x00040206u, 1.0f);
            /* 密集短牙：8 根，上下各一排 */
            for (int i = 0; i < 8; i++) {
                float tx = cx - hrx * 0.86f + (float)i * (hrx * 0.245f);
                cvLine(tx, hy + 4, tx, hy + 9, 2.0f, 0x00BEB49Cu, 0.9f);
                cvLine(tx, hy + hry + 6, tx, hy + hry + 1, 2.0f, 0x00A69C88u, 0.9f);
            }
            /* 无眼，但有**两条横向听缝**（不是眼窝 —— 斜切，像耳裂） */
            cvLine(cx - hrx * 0.72f, hy - 6, cx - hrx * 0.18f, hy - 4, 2.6f, 0x00020406u, 0.95f);
            cvLine(cx + hrx * 0.18f, hy - 4, cx + hrx * 0.72f, hy - 6, 2.6f, 0x00020406u, 0.95f);

    }
}

static void BakeHumanoidLongNeckFace(const HumanoidBakeContext* shape) {
    unsigned int edge = shape->mon->edge; (void)edge;
    unsigned int hi = shape->mon->hi; (void)hi;
    float cx = shape->mon->cx; (void)cx;
    float hry = shape->hry; (void)hry;
    float hrx = shape->hrx; (void)hrx;
    float hy = shape->hy; (void)hy;
    (void)shape;
    {

            /* ===== 听声鬼·长颈（第三只）=====
               同族识别点：无眼、牙齿下颌 —— 保住。
               自己的地标：**头很小、下颌窄长、头顶一根竖立的听针**。
               配合 0.52 的窄身和长脖子，填黑后是「细杆 + 尖顶」。 */
            float jaw[8] = { cx - hrx * 0.80f, hy + 3,
                             cx + hrx * 0.80f, hy + 3,
                             cx + hrx * 0.42f, hy + hry + 15.0f,
                             cx - hrx * 0.42f, hy + hry + 15.0f };
            cvPoly(jaw, 4, 0x00040208u, 1.0f);
            for (int i = 0; i < 4; i++) {
                float tx = cx - hrx * 0.55f + (float)i * (hrx * 0.37f);
                cvLine(tx, hy + 4, tx, hy + 11, 2.2f, 0x00C8C0A8u, 0.9f);
                cvLine(tx, hy + hry + 13, tx, hy + hry + 4, 2.2f, 0x00B0A890u, 0.9f);
            }
            /* 听针：头顶竖起一根细杆 + 末端小球（负空间最大的地标） */
            cvLine(cx, hy - hry - 1.0f, cx, hy - hry - 22.0f, 3.2f, edge, 1.0f);
            cvLine(cx, hy - hry - 2.0f, cx, hy - hry - 20.0f, 1.8f, hi, 0.9f);
            cvEllipse(cx, hy - hry - 24.0f, 5.0f, 5.0f, edge, 1.0f);
            cvEllipse(cx, hy - hry - 24.0f, 3.4f, 3.4f, hi, 0.85f);
            /* 无眼：两处凹陷，比第一只更小更偏外 */
            cvEllipse(cx - hrx * 0.50f, hy - 3, 3.4f, 2.8f, 0x00020406u, 0.9f);
            cvEllipse(cx + hrx * 0.50f, hy - 3, 3.4f, 2.8f, 0x00020406u, 0.9f);

    }
}

static void BakeHumanoidLightHunterFace(const HumanoidBakeContext* shape) {
    float cx = shape->mon->cx; (void)cx;
    float hrx = shape->hrx; (void)hrx;
    float hy = shape->hy; (void)hy;
    (void)shape;
    {

            /* 追光鬼：一对又大又亮的眼睛（它追光） */
            cvEllipse(cx - hrx * 0.40f, hy - 2, 7.0f, 6.0f, 0x00080A10u, 1.0f);
            cvEllipse(cx + hrx * 0.40f, hy - 2, 7.0f, 6.0f, 0x00080A10u, 1.0f);
            cvEllipse(cx - hrx * 0.40f, hy - 2, 5.0f, 4.2f, 0x00D8F4FFu, 1.0f);
            cvEllipse(cx + hrx * 0.40f, hy - 2, 5.0f, 4.2f, 0x00D8F4FFu, 1.0f);
            cvGlow(cx, hy - 2, 26.0f, 0x0060C0FFu, 0.30f);

    }
}

static void BakeHumanoidLeechFace(const HumanoidBakeContext* shape) {
    unsigned int edge = shape->mon->edge; (void)edge;
    float cx = shape->mon->cx; (void)cx;
    float shoulderY = shape->shoulderY; (void)shoulderY;
    float hy = shape->hy; (void)hy;
    (void)shape;
    {

            /* ===== v2.4.85 **这是绿色残留的真凶** =====
               原来这里是 `} else {`（没有条件）——
               于是**任何不是 kind 0/1 的怪都会被画上三只绿色小眼睛**，
               包括守门人（kind 9）。
               它在我重做的面甲之后执行，正好盖在脸上，
               所以不管我把 kind 9 的代码改多少遍，绿眼睛永远在。

               现在限定只给 kind 2（偷电怪）画。
               —— 这也解释了用户说的"头部错乱"：
                 一张脸上叠了两套眼睛。 */
            for (int i = -1; i <= 1; i++) {
                cvEllipse(cx + (float)i * 7.0f, hy - 4, 3.6f, 3.0f, 0x00080A08u, 1.0f);
                cvEllipse(cx + (float)i * 7.0f, hy - 4, 2.4f, 1.9f, 0x0070FFA0u, 1.0f);
            }
            for (int i = 0; i < 3; i++) {
                float bx = cx + (float)(i - 1) * 12.0f;
                cvLine(bx, shoulderY + 4, bx + 4.0f, shoulderY - 22.0f, 4.0f, edge, 1.0f);
                cvLine(bx, shoulderY + 4, bx + 4.0f, shoulderY - 22.0f, 2.4f, 0x0040A060u, 0.9f);
            }

    }
}

/* Fine anatomical/material landmarks are vector strokes, not enlarged noise. */
static void BakeHumanoidSoundHunterDetails(const HumanoidBakeContext* shape) {
    const MonsterBakeContext* b = shape->mon;
    float x = b->cx, sy = shape->shoulderY, wy = shape->waistY;
    float w = shape->shoulderW;
    /* 错版印刷的纸带：横向三条深浅不一的拼贴层，边缘留一道亮纤维。 */
    for (int i = 0; i < 3; i++) {
        float y = sy + 11.0f + (float)i * 12.0f;
        float half = w * (0.74f - (float)i * 0.045f);
        cvLine(x - half, y, x + half, y + 2.0f, 3.0f, 0x000B0D0E, 0.42f);
        cvLine(x - half, y - 1.2f, x + half, y + 0.8f, 0.9f, 0x007A7466, 0.55f);
        cvLine(x - half + 3.0f, y + 2.0f, x + half - 2.0f, y + 3.2f, 0.7f, 0x00343836, 0.55f);
    }
    /* 中央缝线：把“拼贴的身体”缝在一起。 */
    cvLine(x + 1.0f, sy + 6.0f, x - 1.0f, wy + 10.0f, 1.0f, 0x00191D1E, 0.85f);
    for (int i = 0; i < 11; i++) {
        float y = sy + 8.0f + (float)i * (wy - sy + 2.0f) / 11.0f;
        cvLine(x - 3.2f, y, x + 3.2f, y + 1.0f, 0.7f, 0x00B8AC92, 0.7f);
    }
    /* 一个很小的印刷套准十字，放在胸口偏左。 */
    float rx = x + w * 0.34f, ry = sy + (wy - sy) * 0.34f;
    cvLine(rx - 3.0f, ry, rx + 3.0f, ry, 0.7f, 0x00C9BFA6, 0.65f);
    cvLine(rx, ry - 3.0f, rx, ry + 3.0f, 0.7f, 0x00C9BFA6, 0.65f);

    /* 错版色块：冷青在左胸、洋红在右腰，像两套印版没有对齐。 */
    {
        float cyan[8] = { x - w * 0.62f, sy + 18.0f,
                          x - w * 0.20f, sy + 21.0f,
                          x - w * 0.24f, sy + 34.0f,
                          x - w * 0.64f, sy + 31.0f };
        cvPoly(cyan, 4, 0x003FA6B8u, 0.42f);
        float pink[8] = { x + w * 0.10f, wy - 4.0f,
                          x + w * 0.58f, wy - 8.0f,
                          x + w * 0.54f, wy + 6.0f,
                          x + w * 0.08f, wy + 9.0f };
        cvPoly(pink, 4, 0x00C84A78u, 0.36f);
    }
    /* 一截骨色胶带横过右肩，边缘不齐。 */
    cvLine(x + w * 0.18f, sy + 3.0f, x + w * 0.94f, sy + 11.0f, 9.0f, 0x00D6CDB8u, 0.28f);
    cvLine(x + w * 0.18f, sy + 3.0f, x + w * 0.94f, sy + 11.0f, 1.0f, 0x00EFE6CFu, 0.45f);
    /* 胸口的小纸标签，三行印刷字。 */
    {
        float tag[8] = { x + w * 0.08f, sy + 23.0f,
                         x + w * 0.44f, sy + 21.0f,
                         x + w * 0.43f, sy + 34.0f,
                         x + w * 0.07f, sy + 36.0f };
        cvPoly(tag, 4, 0x00CFC4A8u, 0.34f);
        for (int i = 0; i < 3; i++) {
            float ty = sy + 25.0f + (float)i * 3.2f;
            cvLine(x + w * 0.11f, ty, x + w * (0.34f - (float)i * 0.04f), ty + 0.5f, 0.6f, 0x001B1A16u, 0.55f);
        }
    }
}

static void BakeHumanoidCollagePaper(const HumanoidBakeContext* shape) {
    const MonsterBakeContext* b = shape->mon;
    float x = b->cx, sy = shape->shoulderY, wy = shape->waistY;
    float w = shape->shoulderW;
    if (b->kind == 1) {
        /* 追光鬼：一张被撕下来的“照片条”贴在胸前，边缘露白。 */
        float strip[8] = { x - w * 0.34f, sy + 14.0f, x - w * 0.06f, sy + 12.0f,
                           x - w * 0.02f, wy + 2.0f, x - w * 0.30f, wy + 4.0f };
        cvPoly(strip, 4, 0x00C9CFC6u, 0.40f);
        for (int i = 0; i < 4; i++)
            cvLine(x - w * 0.30f, sy + 20.0f + (float)i * 9.0f,
                   x - w * 0.06f, sy + 19.0f + (float)i * 9.0f, 0.7f, 0x0014171Au, 0.55f);
        float tape[8] = { x + w * 0.10f, sy + 4.0f, x + w * 0.84f, sy + 9.0f,
                          x + w * 0.83f, sy + 19.0f, x + w * 0.09f, sy + 14.0f };
        cvPoly(tape, 4, 0x00D8D2BEu, 0.30f);
        cvPoly((float[]){ x + w * 0.18f, wy - 2.0f, x + w * 0.45f, wy - 5.0f,
                          x + w * 0.42f, wy + 7.0f, x + w * 0.15f, wy + 9.0f },
               4, 0x003FA6B8u, 0.30f);
    } else if (b->kind == 7) {
        /* 拾荒者：钱袋上贴一张旧价签，再压一道冷青纸带。 */
        float bagX = x - w * 1.55f, bagY = sy + b->H * 0.02f;
        float tag[8] = { bagX - w * 0.34f, bagY - 4.0f, bagX + w * 0.16f, bagY - 8.0f,
                         bagX + w * 0.18f, bagY + 10.0f, bagX - w * 0.32f, bagY + 14.0f };
        cvPoly(tag, 4, 0x00D0C29Cu, 0.52f);
        for (int i = 0; i < 3; i++)
            cvLine(bagX - w * 0.28f, bagY - 1.0f + (float)i * 6.0f,
                   bagX + w * 0.08f, bagY - 2.0f + (float)i * 6.0f, 0.6f, 0x0016120Cu, 0.6f);
        cvLine(x + w * 0.10f, sy + 8.0f, x + w * 0.86f, sy + 15.0f, 8.0f, 0x003FA6B8u, 0.30f);
        cvLine(x + w * 0.10f, sy + 8.0f, x + w * 0.86f, sy + 15.0f, 0.8f, 0x00A9D4DEu, 0.5f);
    } else if (b->kind == 8) {
        /* 清道夫：三条竖着撕下来的纸带从肩垂到腰，红墨缝合。 */
        for (int i = 0; i < 3; i++) {
            float sx = x - w * 0.38f + (float)i * w * 0.30f;
            float strip[8] = { sx, sy + 10.0f, sx + w * 0.13f, sy + 8.0f,
                               sx + w * 0.10f, wy + 4.0f, sx - w * 0.03f, wy + 7.0f };
            cvPoly(strip, 4, (i == 1 ? 0x00B5AA8Eu : 0x007A8A78u), 0.34f);
        }
        cvLine(x - w * 0.30f, sy + 12.0f, x + w * 0.30f, wy - 2.0f, 0.8f, 0x00B03028u, 0.70f);
        for (int i = 0; i < 5; i++)
            cvLine(x - w * 0.28f + (float)i * w * 0.14f, sy + 18.0f + (float)i * 6.0f,
                   x - w * 0.24f + (float)i * w * 0.14f, sy + 22.0f + (float)i * 6.0f,
                   0.7f, 0x00D8C8A8u, 0.55f);
    } else if (b->kind == 9) {
        /* 守门人：胸口盖一枚洋红印章，肩甲贴金纸胶带。 */
        float seal[8] = { x - w * 0.22f, sy + 22.0f, x + w * 0.10f, sy + 19.0f,
                          x + w * 0.12f, sy + 42.0f, x - w * 0.20f, sy + 45.0f };
        cvPoly(seal, 4, 0x00C84A78u, 0.36f);
        cvLine(x - w * 0.14f, sy + 28.0f, x + w * 0.04f, sy + 27.0f, 1.2f, 0x00221A1Eu, 0.7f);
        cvLine(x - w * 0.12f, sy + 34.0f, x + w * 0.02f, sy + 33.0f, 1.0f, 0x00221A1Eu, 0.7f);
        cvLine(x - w * 0.72f, sy + 3.0f, x - w * 0.28f, sy + 9.0f, 7.0f, 0x00B58A3Cu, 0.34f);
        cvLine(x - w * 0.70f, sy + 3.0f, x - w * 0.30f, sy + 9.0f, 0.8f, 0x00E0C080u, 0.55f);
        cvLine(x + w * 0.34f, sy + 18.0f, x + w * 0.34f, sy + 26.0f, 0.8f, 0x003FA6B8u, 0.7f);
        cvLine(x + w * 0.30f, sy + 22.0f, x + w * 0.38f, sy + 22.0f, 0.8f, 0x003FA6B8u, 0.7f);
    }
}

static void BakeHumanoidLightHunterGrotesque(const HumanoidBakeContext* shape) {
    const MonsterBakeContext* b = shape->mon;
    float cx = b->cx, hy = shape->hy, hrx = shape->hrx, hry = shape->hry;
    float sy = shape->shoulderY, w = shape->shoulderW;
    unsigned int teeth = 0x00C8C0A8u;
    /* 纵裂的嘴：从双眼之间一直裂到下颌下面。 */
    float mouth[8] = { cx - 2.2f, hy + 5.0f, cx + 2.2f, hy + 5.0f,
                       cx + 3.0f, hy + hry + 12.0f, cx - 3.0f, hy + hry + 12.0f };
    cvPoly(mouth, 4, 0x00020305u, 0.95f);
    for (int i = 0; i < 4; i++) {
        float y = hy + 7.0f + (float)i * 5.0f;
        cvLine(cx - 2.0f, y, cx - 4.5f, y + 2.0f, 0.6f, teeth, 0.75f);
        cvLine(cx + 2.0f, y, cx + 4.5f, y + 2.0f, 0.6f, teeth, 0.75f);
    }
    /* 右侧多长出来的一簇小眼睛：光猎者不该只有一对。 */
    for (int i = 0; i < 3; i++) {
        float ex = cx + hrx * 0.62f + (float)i * 1.8f;
        float ey = hy - 8.0f + (float)i * 5.5f;
        cvEllipse(ex, ey, 2.6f, 2.2f, 0x00020305u, 0.95f);
        cvEllipse(ex, ey, 1.5f, 1.2f, 0x00D8F4FFu, 1.0f);
    }
    cvGlow(cx + hrx * 0.72f, hy - 1.0f, 15.0f, 0x0060C0FFu, 0.22f);
    /* 脖子下方再开一只“喉眼”，用来看见声音。 */
    cvEllipse(cx, sy - 3.0f, 3.2f, 2.6f, 0x00020305u, 0.95f);
    cvEllipse(cx, sy - 3.0f, 1.8f, 1.4f, 0x00E8F6FFu, 0.95f);

    /* 肋条被光照穿：左右各四道苍白骨线。 */
    for (int i = 0; i < 4; i++) {
        float y = sy + 13.0f + (float)i * 9.0f;
        cvLine(cx - w * 0.50f, y, cx - 3.0f, y + 2.0f, 0.8f, teeth, 0.55f);
        cvLine(cx + w * 0.50f, y + 2.0f, cx + 3.0f, y, 0.8f, teeth, 0.55f);
    }
    /* 两条退化的副臂，从肋下伸出来，末端只有三根指头。 */
    for (int side = -1; side <= 1; side += 2) {
        float ax = cx + (float)side * w * 0.54f, ay = sy + 22.0f;
        float bx = cx + (float)side * w * 1.02f, by = sy + 48.0f;
        float ex = cx + (float)side * w * 0.88f, ey = by + 24.0f;
        Limb(ax, ay, bx, by, 5.0f, 3.4f, b->edge, 0x00988C76u, b->hi);
        Limb(bx, by, ex, ey, 3.2f, 1.8f, b->edge, 0x00A89C80u, b->hi);
        for (int f = -1; f <= 1; f++)
            cvLine(ex, ey, ex + (float)f * 2.6f, ey + 7.0f, 0.8f, 0x00C8C0A8u, 0.82f);
    }
}

static void BakeHumanoidScavengerGrotesque(const HumanoidBakeContext* shape) {
    const MonsterBakeContext* b = shape->mon;
    float cx = b->cx, hy = shape->hy, hry = shape->hry;
    float sy = shape->shoulderY, wy = shape->waistY, w = shape->shoulderW;
    float footY = b->footY;
    unsigned int teeth = 0x00B8AC92u;
    /* 脸被从上到下劈开：没有眼睛，只有一条竖嘴和倒刺牙。 */
    float mouth[8] = { cx - 2.0f, hy - hry * 0.55f, cx + 2.0f, hy - hry * 0.55f,
                       cx + 4.0f, hy + hry + 10.0f, cx - 4.0f, hy + hry + 10.0f };
    cvPoly(mouth, 4, 0x00010304u, 0.95f);
    for (int i = 0; i < 7; i++) {
        float y = hy - hry * 0.45f + (float)i * 5.2f;
        cvLine(cx - 1.8f, y, cx - 5.0f, y + 2.2f, 0.65f, teeth, 0.82f);
        cvLine(cx + 1.8f, y, cx + 5.0f, y + 2.2f, 0.65f, teeth, 0.82f);
    }
    /* 第二张嘴长在胸口，牙齿从肋骨里往外翻。 */
    float chest[8] = { cx - 4.0f, sy + 12.0f, cx + 4.0f, sy + 12.0f,
                       cx + 9.0f, wy - 4.0f, cx - 9.0f, wy - 4.0f };
    cvPoly(chest, 4, 0x00020303u, 0.92f);
    for (int i = 0; i < 6; i++) {
        float y = sy + 15.0f + (float)i * 6.5f;
        cvLine(cx - 3.0f, y, cx - 8.0f, y + 2.5f, 0.7f, teeth, 0.78f);
        cvLine(cx + 3.0f, y, cx + 8.0f, y + 2.5f, 0.7f, teeth, 0.78f);
    }
    cvLine(cx - 7.0f, sy + 20.0f, cx + 7.0f, wy - 9.0f, 0.8f, 0x00B03028u, 0.7f);
    /* 从腰上垂下来的皮纸条，一直拖到脚边。 */
    for (int i = 0; i < 4; i++) {
        float sx = cx - w * 0.48f + (float)i * w * 0.30f;
        float strip[8] = { sx, wy - 2.0f, sx + 5.0f, wy - 4.0f,
                           sx + 7.0f, footY - 8.0f + (float)(i % 2) * 8.0f,
                           sx + 1.0f, footY - 3.0f };
        cvPoly(strip, 4, (i % 2) ? 0x00A89C80u : 0x007A826E, 0.34f);
        cvLine(sx + 2.0f, wy, sx + 3.0f, footY - 6.0f, 0.5f, 0x00111514u, 0.55f);
    }
    /* 脊骨从胸口一直顶到腰。 */
    for (int i = 0; i < 5; i++) {
        float y = sy + 7.0f + (float)i * 7.0f;
        cvEllipse(cx, y, 1.7f, 1.2f, 0x00B8AC92u, 0.65f);
    }
}

static void BakeHumanoidFineDetail(const HumanoidBakeContext* shape){
    const MonsterBakeContext* b=shape->mon;
    float x=b->cx,sy=shape->shoulderY,wy=shape->waistY,hy=shape->hy;
    float width=shape->shoulderW;
    if(b->kind==7){
        /* Frayed coat seam, broken zipper and stitched sack shoulder harness. */
        cvLine(x+5,sy+9,x+9,wy+23,.8f,0x898477,.65f);
        for(int i=0;i<18;i++){
            float y=sy+12+i*(wy-sy+12)/18;
            cvLine(x+3,y,x+8,y+1,.6f,0xB1A698,.5f);
        }
        cvLine(x-width*.75f,sy+3,x+width*.42f,wy-3,3,0x191C1D,.9f);
        cvLine(x-width*.75f+1,sy+3,x+width*.42f+1,wy-3,.7f,0x887A64,.7f);
        for(int i=0;i<6;i++)cvLine(x-width*.45f+i*5,wy+10,x-width*.39f+i*5,wy+14,.7f,0xCEC3A1,.5f);
    }else if(b->kind==9){
        /* Layered oxidised plate, exposed dark fasteners, cracked mask edges. */
        for(int i=0;i<5;i++){
            float y=sy+9+i*9;
            cvLine(x-width*.72f,y,x+width*.70f,y+4,1.2f,0x12181C,.85f);
            cvLine(x-width*.68f,y-1,x+width*.65f,y+3,.55f,0x7B8585,.6f);
            cvEllipse(x-width*.65f,y+2,1.1f,1.1f,0xB2A88C,.85f);
        }
        cvLine(x+shape->hrx*.6f,hy-8,x+shape->hrx*.25f,hy+1,.75f,0x060809,.9f);
        cvLine(x+shape->hrx*.25f,hy+1,x+shape->hrx*.55f,hy+10,.6f,0x080909,.9f);
    }else if(b->kind==1){
        /* Lens fracture and translucent throat filaments: no extra eye pair. */
        for(int i=0;i<4;i++){
            float xx=x+(i-1.5f)*3;
            cvLine(xx,hy+shape->hry*.7f,xx+1,sy+8,.65f,0x9BC4D0,.7f);
        }
        cvLine(x-shape->hrx*.8f,hy-6,x-shape->hrx*.3f,hy+1,.65f,0xCDE9E9,.6f);
        cvLine(x-shape->hrx*.3f,hy+1,x-shape->hrx*.5f,hy+8,.55f,0x18252D,.9f);
    }else{
        /* Asymmetric rib planes read as volume; long neck gets separate tendons. */
        unsigned int tendon=b->kind==8?0x737365:0x7A7771;
        for(int i=0;i<7;i++){
            float y=sy+10+i*(wy-sy-5)/8;
            float span=width*(.65f-i*.045f);
            cvLine(x-2,y,x-span,y-3,1.0f,0x080D10,.8f);
            cvLine(x+2,y+2,x+span*.88f,y-1,.85f,0x080D10,.8f);
            cvLine(x-3,y-1,x-span+1,y-4,.6f,tendon,.5f);
        }
        cvLine(x-1,sy+5,x+2,wy+7,.7f,0x9A8E7A,.55f);
        if(b->kind==12 || b->kind==8){
            for(int i=0;i<3;i++)cvLine(x-3+i*2,hy+shape->hry*.75f,x-5+i*4,sy+3,.65f,0x8C8A7C,.7f);
        }
        /* Dry split skin and a small stitch line on one cheek, away from the eyes. */
        float cheek=x-shape->hrx*.62f;
        cvLine(cheek,hy+3,cheek+2,hy+shape->hry*.65f,.65f,0x140D10,.9f);
        for(int i=0;i<4;i++)cvLine(cheek-1,hy+5+i*2,cheek+3,hy+6+i*2,.5f,0xA79D86,.6f);
    }
}

static void BakeMonsterHumanoid(const MonsterBakeContext* bake) {
    int kind = bake->kind;
    int side = bake->side;
    float H = bake->H;
    float headY = bake->headY;
    float narrow = (kind == 1) ? 0.70f : (kind == 2 ? 1.35f : 1.0f);   /* 体型胖瘦 */
    /* v2.4.1 审美规范：体型必须和移速一致（见 include/art_direction.h 铁律四） */
    if (kind == 7) narrow = 1.42f;   /* 拾荒者：最宽，负重 */
    if (kind == 8) narrow = 0.60f;   /* 清道夫：最瘦 */
    if (kind == 9) narrow = 1.62f;   /* 守门人：宽肩立柱 */
    /* 听声鬼变体：宽度也要和"怎么移动"一致（铁律四）
       · 匍匐者贴着地面爬/挪，肩更宽、重心更低
       · 长颈几乎不带体积，瘦成一根 */
    if (kind == 11) narrow = 1.34f;
    if (kind == 12) narrow = 0.52f;
    if (kind == 0) narrow = 1.10f;   /* hunched listener: heavier shoulders */
    float shoulderW = 30.0f * narrow;
    if (side) shoulderW *= 0.68f;

    float shoulderY = headY + H * 0.22f;
    float waistY    = headY + H * 0.55f;
    float pelvisY   = headY + H * 0.66f;
    /* Sound Hunter: the listener is hunched.  The head sinks, the shoulders
       ride up, and the pelvis lifts so the legs stay shorter than the torso.
       Render-only silhouette change; collision and AI are untouched. */
    if (kind == 0) {
        headY    += H * 0.035f;
        shoulderY = headY + H * 0.275f;
        waistY    = headY + H * 0.590f;
        pelvisY   = headY + H * 0.660f;
    }

    /* --- 头 --- */
    /* 头型也是区分点：
       · 匍匐者：头更扁更宽（贴地听声，下颌横向铺开）
       · 长颈：头更小更长（远端的耳朵） */
    float hry = (kind == 2) ? 15.0f
              : (kind == 11) ? 15.0f
              : (kind == 12) ? 21.0f : 19.0f;
    float hrx = (kind == 2) ? 17.0f
              : (kind == 11) ? 19.0f
              : (kind == 12) ? 11.0f : 15.0f;
    float hy = headY + hry + 2.0f;
    const HumanoidBakeContext shape = {
        bake, shoulderW, shoulderY, waistY, pelvisY, hry, hrx, hy
    };
    BakeHumanoidLegs(&shape);
    BakeHumanoidTorso(&shape);
    BakeHumanoidArms(&shape);
    BakeHumanoidNeck(&shape);
    BakeHumanoidHead(&shape);
    if (kind == 7) BakeHumanoidCollectorDetails(&shape);
    else if (kind == 8) BakeHumanoidScavengerDetails(&shape);
    else if (kind == 9) BakeHumanoidWardenDetails(&shape);
    if (kind == 0) BakeHumanoidHunterFace(&shape);
    else if (kind == 11) BakeHumanoidCrawlerFace(&shape);
    else if (kind == 12) BakeHumanoidLongNeckFace(&shape);
    else if (kind == 1) BakeHumanoidLightHunterFace(&shape);
    else if (kind == 2) BakeHumanoidLeechFace(&shape);
    BakeHumanoidFineDetail(&shape);
    if (kind == 0) BakeHumanoidSoundHunterDetails(&shape);
    else if (kind == 1 || kind == 7 || kind == 8 || kind == 9) BakeHumanoidCollagePaper(&shape);
    if (kind == 1) BakeHumanoidLightHunterGrotesque(&shape);
    else if (kind == 8) BakeHumanoidScavengerGrotesque(&shape);
}

/* Variant: kind == 5 */
static void BakeMonsterMite(const MonsterBakeContext* bake) {
    int walk = bake->walk; (void)walk;
    unsigned int edge = bake->edge; (void)edge;
    unsigned int body = bake->body; (void)body;
    unsigned int hi = bake->hi; (void)hi;
    float footY = bake->footY; (void)footY;
    float cx = bake->cx; (void)cx;
    (void)bake;
    {

        /* 盲螨：五节横body + 八条细腿 */
        for (int i = 0; i < 5; i++) {
            float sx = cx - 52.0f + (float)i * 26.0f;
            float sy = footY - 30.0f + sinf((float)i * 1.1f + (walk ? 1.6f : 0.0f)) * 5.0f;
            float rr = 17.0f - fabsf((float)i - 2.0f) * 2.2f;
            cvEllipse(sx, sy, rr + 2, rr + 2, edge, 1.0f);
            cvEllipse(sx, sy - 1, rr, rr, body, 1.0f);
            cvEllipse(sx - rr * 0.3f, sy - rr * 0.35f, rr * 0.45f, rr * 0.35f, hi, 0.6f);
        }
        for (int i = 0; i < 8; i++) {
            float sx = cx - 56.0f + (float)i * 16.0f;
            float kx = sx + (float)((i & 1) ? 10 : -10);
            Limb(sx, footY - 32.0f, kx, footY - 12.0f, 4.0f, 3.0f, edge, body, hi);
            Limb(kx, footY - 12.0f, kx + (float)((i & 1) ? 6 : -6), footY, 3.0f, 2.0f, edge, body, hi);
        }

    }
}

/* Variant: kind == 3 */
static void BakeMonsterJumper(const MonsterBakeContext* bake) {
    unsigned int edge = bake->edge; (void)edge;
    unsigned int body = bake->body; (void)body;
    unsigned int hi = bake->hi; (void)hi;
    float footY = bake->footY; (void)footY;
    float cx = bake->cx; (void)cx;
    (void)bake;
    {

        /* 跳脸虫：圆身 + 四对足 + 正面的大嘴 */
        float rr = 40.0f;
        float cy2 = footY - 52.0f;
        cvEllipse(cx, cy2, rr + 2, rr + 2, edge, 1.0f);
        cvEllipse(cx, cy2 - 1, rr, rr, body, 1.0f);
        cvEllipse(cx - rr * 0.3f, cy2 - rr * 0.35f, rr * 0.42f, rr * 0.34f, hi, 0.7f);
        /* 嘴 */
        cvEllipse(cx, cy2 + 8, 15.0f, 8.0f, 0x00040204u, 1.0f);
        for (int i = 0; i < 4; i++) {
            float tx = cx - 11.0f + (float)i * 7.0f;
            cvLine(tx, cy2 + 2, tx + 1.0f, cy2 + 8, 2.2f, 0x00C8C0A0u, 0.9f);
        }
        for (int i = 0; i < 4; i++) {
            float a = 0.5f + (float)i * 0.62f;
            float lx = cx + cosf(a) * 58.0f, ly = cy2 + sinf(a) * 40.0f;
            Limb(cx, cy2 - 6.0f, lx, ly, 6.0f, 3.0f, edge, body, hi);
        }

    }
}

/* Variant: kind == 4 */
static void BakeMonsterShade(const MonsterBakeContext* bake) {
    unsigned int edge = bake->edge; (void)edge;
    unsigned int body = bake->body; (void)body;
    unsigned int hi = bake->hi; (void)hi;
    float footY = bake->footY; (void)footY;
    float cx = bake->cx; (void)cx;
    (void)bake;
    {

        /* 影子：只有一团边缘 + 撕裂的下摆，没有实心身体 */
        for (int i = 0; i < 5; i++) {
            float sx = cx + (float)(i - 2) * 12.0f;
            float sy = footY - 70.0f - (float)(2 - abs(i - 2)) * 12.0f;
            cvEllipse(sx, sy, 20.0f, 30.0f, edge, 0.30f);
        }
        for (int i = 0; i < 7; i++) {
            float sx = cx - 40.0f + (float)i * 13.0f;
            float dx2 = (float)((i & 1) ? 10 : -10);
            Limb(sx, footY - 50.0f, sx + dx2, footY, 7.0f, 1.5f, edge, body, hi);
        }
        /* 一对极小的紫眼 —— 影子几乎没有实体，只留两点 */
        cvEllipse(cx - 8, footY - 88.0f, 3.0f, 2.4f, 0x007040A0u, 0.9f);
        cvEllipse(cx + 8, footY - 88.0f, 3.0f, 2.4f, 0x007040A0u, 0.9f);

    }
}

/* Variant: fallback */
static void BakeMonsterResonator(const MonsterBakeContext* bake) {
    unsigned int edge = bake->edge; (void)edge;
    unsigned int body = bake->body; (void)body;
    unsigned int hi = bake->hi; (void)hi;
    float H = bake->H; (void)H;
    float footY = bake->footY; (void)footY;
    float cx = bake->cx; (void)cx;
    (void)bake;
    {

        /* 回响：倒挂的钟形 + 纵向裂口 */
        float top = footY - H;
        float bell[10] = {
            cx - 30, top + 6,
            cx + 30, top + 6,
            cx + 42, top + H * 0.62f,
            cx + 16, footY - 6,
            cx - 16, footY - 6
        };
        float outline[10];
        for (int i = 0; i < 5; i++) {
            outline[i * 2] = cx + (bell[i * 2] - cx) * 1.08f;
            outline[i * 2 + 1] = bell[i * 2 + 1];
        }
        cvPoly(outline, 5, edge, 1.0f);
        cvPolyGrad(bell, 5, hi, body, 1.0f);
        for (int i = 0; i < 4; i++) {
            float lx = cx - 20.0f + (float)i * 13.0f;
            cvLine(lx, top + 20, lx + 2.0f, footY - 20, 2.0f, edge, 0.55f);
        }
        cvEllipse(cx, top + 16, 10.0f, 6.0f, 0x00060408u, 1.0f);
        cvEllipse(cx, top + 16, 6.0f, 3.4f, 0x00A0FFC0u, 0.85f);

    }
}

/* The power leech is an actual segmented parasite, not the old 32px rectangle. */
static void BakeMonsterParasite(const MonsterBakeContext* b){
    float x=b->c;
    for(int i=10;i>=0;i--){
        float y=111+i*11.5f,rx=9+(10-i)*1.05f,xx=x+(i%3-1)*1.2f;
        cvEllipse(xx+2,y+2,rx,10,0x080F12,1);
        cvEllipse(xx,y,rx,9,0x364D4F,1);
        cvLine(xx-rx*.65f,y-3,xx+rx*.6f,y-4,.7f,0x90A19B,.7f);
        cvLine(xx-rx*.3f,y+7,xx+rx*.4f,y+7,.7f,0x081114,.9f);
        for(int side=-1;side<=1;side+=2){
            cvLine(xx+side*(rx-1),y,xx+side*(rx+5),y+5,1,0x627C77,.9f);
            cvLine(xx+side*(rx+5),y+5,xx+side*(rx+2),y+8,.7f,0x151D21,1);
        }
    }
    cvEllipse(x,98,20,24,0x111C22,1);cvEllipse(x-1,97,15,19,0x58605A,1);
    cvEllipse(x,102,10,14,0x030506,1);cvEllipse(x,104,5,8,0x34202C,1);
    for(int i=0;i<8;i++){
        float a=i*6.2831853f/8;
        cvLine(x+cosf(a)*9,102+sinf(a)*12,x+cosf(a)*5,102+sinf(a)*7,1.2f,0xD0CAB3,1);
    }
    cvEllipse(x-8,86,2,1.4f,0x80B6AF,.9f);
    cvEllipse(x+7,85,1.4f,1,0xBDD9CC,.9f);
    cvLine(x-10,79,x-17,64,1,0x648780,.8f);cvLine(x+10,80,x+15,65,1,0x4E706B,.9f);
}

static unsigned int CreatureHash(unsigned int x){x^=x>>16;x*=0x7FEB352Du;x^=x>>15;x*=0x846CA68Bu;return x^(x>>16);}

/* 错版印刷套色偏移：把身体剪影复制两份，错开几像素压到轮廓外。
   只在透明处落色，不覆盖已经画好的身体；受击/光照仍走原有管线。
   七只图鉴怪共用这套“套色没对准”的物理效果，颜色按物种区分。 */
void BakeMisprintFringe(int kind) {
    SpriteBakeCanvasState* canvas = SpriteBakeCanvas();
    size_t count = (size_t)RASTER_W * (size_t)RASTER_H;
    unsigned int* src = (unsigned int*)malloc(count * sizeof(unsigned int));
    if (!src) return;
    memcpy(src, canvas->pixels, count * sizeof(unsigned int));
    static const int ox[4] = { -8, 8, -4, 6 };
    static const int oy[4] = {  4, -4, -8, 6 };
    unsigned int inkA = 0x003FA6B8u, inkB = 0x00C84A78u;
    if (kind == 1) { inkA = 0x006FD8E8u; inkB = 0x00E8E0C8u; }
    else if (kind == 7) { inkA = 0x00B58A3Cu; inkB = 0x003FA6B8u; }
    else if (kind == 8) { inkA = 0x003FA6B8u; inkB = 0x0094382Cu; }
    else if (kind == 9) { inkA = 0x00B03028u; inkB = 0x003FA6B8u; }
    else if (kind == 13) { inkA = 0x003FA6B8u; inkB = 0x00B58A3Cu; }
    else if (kind == 16) { inkA = 0x00C84A78u; inkB = 0x003FA6B8u; }
    const unsigned int ink[4] = { inkA, inkB, inkA, inkB };
    static const int alpha[4] = { 128, 112, 70, 62 };
    for (int y = 0; y < RASTER_H; y++) {
        for (int x = 0; x < RASTER_W; x++) {
            if ((src[(size_t)y * RASTER_W + x] >> 24) < 40) continue;
            for (int k = 0; k < 4; k++) {
                int nx = x + ox[k], ny = y + oy[k];
                if (nx < 1 || nx >= RASTER_W - 1 || ny < 1 || ny >= RASTER_H - 1) continue;
                unsigned int* dst = &canvas->pixels[(size_t)ny * RASTER_W + nx];
                if ((*dst >> 24) > 8) continue;
                *dst = ((unsigned int)alpha[k] << 24) | ink[k];
            }
        }
    }
    free(src);
}

void BakeCreatureSurface(int kind){
    SpriteBakeCanvasState* canvas = SpriteBakeCanvas();
    /* Stable per-surface texture: no runtime randomness or shimmering grain. */
    for(int y=1;y<RASTER_H-1;y++)for(int x=1;x<RASTER_W-1;x++){
        unsigned int* p=&canvas->pixels[(size_t)y*RASTER_W+x];unsigned int col=*p;
        if((col>>24)<220)continue;
        int r=(col>>16)&255,g=(col>>8)&255,b=col&255;
        /* Keep eyes, thin highlights and exact palette-swap keys intact. */
        if((r>145 && b>145 && g<115) || r+g+b>510)continue;
        unsigned int hash=CreatureHash((unsigned)x*1973u+(unsigned)y*9277u+(unsigned)(kind+1)*26699u);
        int grain=(int)(hash&15)-7;
        int line=(x+(y/21)*3)%(kind==13?29:43);
        float gain=1.0f+grain*.009f;
        if(kind==1)gain+=(line<2?-.12f:line==2?.07f:0)+(((hash&2047)<5)?-.16f:0);
        else if(kind==7 || kind==8)gain+=((x%7==0)?-.06f:0)+((y%11==0)?.035f:0)+(line<2?-.10f:line==2?.07f:0);
        else if(kind==9)gain+=(line<2?-.13f:line==2?.13f:0)+((hash&2047)<4?-.14f:0);
        else if(kind==13)gain+=(line<2?-.18f:line==2?.08f:0)+((hash&2047)<5?-.14f:0);
        else if(kind==0)gain+=(line<2?-.16f:line==2?.09f:0)+(((hash&2047)<7)?-.22f:0);
        else {gain+=(line==0?-.10f:0);if((hash&1023)<5)gain-=.20f;}
        if(kind==14 && (y%46)<3)gain-=.15f;
        r=(int)(r*gain);g=(int)(g*gain);b=(int)(b*gain);
        if(r>255)r=255;
        if(g>255)g=255;
        if(b>255)b=255;
        *p=(col&0xFF000000u)|((unsigned)r<<16)|((unsigned)g<<8)|(unsigned)b;
    }
}

/* 宝箱怪：木箱上贴货运标签和撕裂胶带，让它同时读作“箱子”和“拼贴画”。 */
static void BakeMimicCollage(float c, float baseY) {
    float topY = baseY - 92.0f;
    float label[8] = { c - 30.0f, baseY - 66.0f, c + 27.0f, baseY - 69.0f,
                       c + 29.0f, baseY - 43.0f, c - 28.0f, baseY - 40.0f };
    cvPoly(label, 4, 0x00D0C29Cu, 0.48f);
    for (int i = 0; i < 4; i++)
        cvLine(c - 24.0f, baseY - 61.0f + (float)i * 5.0f,
               c + 21.0f, baseY - 62.0f + (float)i * 5.0f, 0.7f, 0x0016120Cu, 0.55f);
    float tape[8] = { c - 44.0f, topY - 4.0f, c + 43.0f, topY + 2.0f,
                      c + 41.0f, topY + 13.0f, c - 45.0f, topY + 7.0f };
    cvPoly(tape, 4, 0x00D8D2BEu, 0.34f);
    cvPoly((float[]){ c + 12.0f, baseY - 34.0f, c + 40.0f, baseY - 37.0f,
                      c + 38.0f, baseY - 23.0f, c + 10.0f, baseY - 20.0f },
           4, 0x00C84A78u, 0.30f);
    cvLine(c - 38.0f, baseY - 26.0f, c - 18.0f, baseY - 27.0f, 0.8f, 0x003FA6B8u, 0.65f);
}

void BakeMonster(int id, int kind, int pose) {
    SpriteBakeCanvasState* canvas = SpriteBakeCanvas();
    canvas->scale=SPR_CREATURE_SCALE;
    cvClear();
    float c = CANVAS_W * 0.5f;

    int side = (pose >= 2) ? 1 : 0;
    int walk = (pose == 1 || pose == 3) ? 1 : 0;

    /* v3.0：走路时**不再整体下沉** —— 原来 baseY+4 让脚每次迈步都往地里踩 */
    /* v2.4-rc P1：244 -> 254。画布 256 高、缩放后是 128；
       原来底部留 12px（缩放后 6px），物件看起来悬空 5% 高度。
       贴到 254 之后包围盒底边就是画布底边，落地才实。 */
    float baseY = 254.0f;
    float lean  = walk ? 3.0f : 0.0f;

    /* 每个物种的调色板：暗边 / 中间调 / 受光 */
    unsigned int edge = 0x00080A0Eu, body = 0x00262C38u, hi = 0x003E4658u;
    float heightK = 1.0f;

    switch (kind) {
    case 0:  /* 寻声鬼：错版印刷——近黑纸胎 + 冷青油墨 */
        edge = 0x00030507u; body = 0x001A2026u; hi = 0x00313A44u; heightK = 0.98f; break;
    case 1:  /* 追光鬼：漂白照片——冷灰纸胎 + 亮眼 */
        edge = 0x00050609u; body = 0x001A222Cu; hi = 0x003C4E5Eu; heightK = 1.18f; break;
    case 2:  /* 偷电怪：矮胖，偏绿 */
        edge = 0x00060A08u; body = 0x00223428u; hi = 0x003A5442u; heightK = 0.78f; break;
    case 3:  /* 跳脸虫：暖褐 */
        edge = 0x000C0806u; body = 0x00342A20u; hi = 0x00524030u; heightK = 0.62f; break;
    case 4:  /* 影子：紫黑，几乎没实体 */
        edge = 0x0006040Cu; body = 0x001C1628u; hi = 0x002E2444u; heightK = 1.05f; break;
    case 5:  /* 盲螨：土黄，极扁 */
        edge = 0x000C0A04u; body = 0x00322914u; hi = 0x00503820u; heightK = 0.45f; break;
    /* v2.4.4 **统一暗色**（用户要求）：身体不再各自上色 ——
       多彩会削弱模型本身的明暗精度。现在三只怪的身体都是中性暗灰，
       只留极小的色相偏移；**颜色只留给识别特征**（口袋 / 爪 / 眼 / 灯）。 */
    case 7:  /* 拾荒者：油墨黑 + 旧金纸袋 */
        edge = 0x00060606u; body = 0x001C1E1Eu; hi = 0x003B3C38u; heightK = 0.74f; break;
    /* ===== 听声鬼变体（2026-11-03）=====
       同族识别点必须保住：**无眼、牙齿下颌、瘦长肢体、冷色、不用高饱和**。
       区分靠**体态与地标**，不靠颜色（art_direction 铁律一 / 五）。 */
    case 11: /* 听声鬼·匍匐者：更矮更宽，冷灰偏土 —— 像趴着听地面 */
        edge = 0x000A0A08u; body = 0x002A2A26u; hi = 0x00444138u; heightK = 0.76f; break;
    case 12: /* 听声鬼·长颈：更高更瘦，冷灰偏紫 —— 像把耳朵伸到天花板 */
        edge = 0x0008090Bu; body = 0x00252A34u; hi = 0x003C4456u; heightK = 1.22f; break;
    case 8:  /* 清道夫：冷青墨 + 撕纸条 */
        edge = 0x00050607u; body = 0x001A201Eu; hi = 0x00343C38u; heightK = 1.26f; break;
    case 9:  /* 守门人：钢灰纸甲 + 红章 */
        edge = 0x0006070Au; body = 0x001A1E26u; hi = 0x00363C48u; heightK = 0.96f; break;
    case 10: /* 回声：不是生物，是「正在发声的东西」—— 冷白偏青 */
        edge = 0x00020A0Eu; body = 0x001A3848u; hi = 0x005AA8C8u; heightK = 0.90f; break;
    case 13: /* 宝箱怪：旧木箱 + 洋红/冷青套色 */
        edge = 0x00060403u; body = 0x00241E18u; hi = 0x00483C30u; heightK = 1.00f; break;
    default: /* 回响：钟形 */
        edge = 0x000A080Cu; body = 0x002A2234u; hi = 0x00443850u; heightK = 1.00f; break;
    }

    float H = 196.0f * heightK;         /* v2.2：拉满画布高度，怪物才显得高 */
    float footY = baseY;
    float headY = footY - H;
    float cx = c + lean;

    /* ================= 人形：寻声鬼 / 追光鬼 / 偷电怪 ================= */
    /* ===== v2.4.2 回声（按 include/art_direction.h 设计）=====
       它不是生物，所以：**没有眼睛、没有四肢**（避免和第怪物混淆）。
       地标 = 三层同心环 + 中心一个尖角菱形核心。
       同心环 = 「正在向外广播声音」，尖角 = 恐怖该有的锐利感。
       玩家一眼就知道「那是个声源标记」，不会当成怪。 */
    MonsterBakeContext bake = { kind, pose, c, side, walk, baseY, edge, body, hi, H, footY, headY, cx };
    if(kind==14){
        BakeMonsterParasite(&bake);
    }else if (kind == 10) {
        BakeMonsterEcho(&bake);
    } else if (kind == 13) {
        BakeMonsterMimic(&bake);
        BakeMimicCollage(bake.c, bake.baseY);
    } else if (kind == 0 || kind == 1 || kind == 2 || kind == 7 || kind == 8 || kind == 9
               || kind == 11 || kind == 12) {
        BakeMonsterHumanoid(&bake);
    } else if (kind == 5) {
        BakeMonsterMite(&bake);
    } else if (kind == 3) {
        BakeMonsterJumper(&bake);
    } else if (kind == 4) {
        BakeMonsterShade(&bake);
    } else {
        BakeMonsterResonator(&bake);
    }


    /* 左缘轮廓光：让剪影在暗场里"立"起来 */
    {
        int w = RASTER_W;
        for (int y = 0; y < RASTER_H; y++) {
            for (int x = 1; x < w; x++) {
                unsigned int a = canvas->pixels[(size_t)y * w + x];
                unsigned int b = canvas->pixels[(size_t)y * w + x - 1];
                if ((a >> 24) > 128 && (b >> 24) < 32) {
                    unsigned int px2 = canvas->pixels[(size_t)y * w + x];
                    int r = (px2 >> 16) & 0xFF, g2 = (px2 >> 8) & 0xFF, bb = px2 & 0xFF;
                    r = r + 34; g2 = g2 + 34; bb = bb + 42;
                    if (r > 255) r = 255;
                    if (g2 > 255) g2 = 255;
                    if (bb > 255) bb = 255;
                    canvas->pixels[(size_t)y * w + x] = 0xFF000000u | (r << 16) | (g2 << 8) | bb;
                }
            }
        }
    }

    BakeCreatureSurface(kind);
    if (kind == 0 || kind == 1 || kind == 7 || kind == 8 || kind == 9 || kind == 13)
        BakeMisprintFringe(kind);
    cvFinish(SpriteBakeTarget(id));
    canvas->scale=1;
}

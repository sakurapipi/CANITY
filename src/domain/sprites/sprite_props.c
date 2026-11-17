#include "sprite_bake.h"
#include "common.h"
#include "render.h"
#include "art.h"

typedef struct {
    float c;
    float cy;
    unsigned int main;
    unsigned int dark;
    unsigned int hi;
} ItemBakeContext;

static void BakeItemGlove(const ItemBakeContext* bake) {
    float c = bake->c; (void)c;
    float cy = bake->cy; (void)cy;
    (void)bake;
    {
    /* ===== v2.4.60 **左手（手套）重画** =====
       原来只是一个扁椭圆 —— 在 3D 场景里读起来像"一团棕色"，
       而且尺寸 36×22 太小，缩到 69px 之后几乎看不见（用户：「左手没有东西」）。

       手是**托举**的姿态，所以剪影要能看出来：
         · 掌根：一个宽的圆角块（比原来大 1.6 倍）
         · 四指：从掌根往上伸，**包住物体下半部** —— 这是"托着"的关键
         · 拇指：单独一块，压在物体右下前方
       手的颜色压得很暗（0x2A2420），这样上面托的东西永远是亮的一方。 */
    {
        unsigned int gloveLo = 0x0016120Eu;
        unsigned int gloveMid = 0x00332B24u;
        unsigned int gloveHi = 0x0052483Cu;

        /* 掌根 */
        cvEllipse(c, cy + 56, 62.0f, 34.0f, gloveLo, 1.0f);
        cvEllipse(c, cy + 54, 57.0f, 30.0f, gloveMid, 1.0f);
        /* 掌根受光边（左上） */
        cvLine(c - 48, cy + 40, c - 20, cy + 30, 6.0f, gloveHi, 0.55f);

        /* 四指：从掌根往上，包住物体下半 */
        for (int f = 0; f < 4; f++) {
            float fx = c - 34.0f + (float)f * 22.0f;
            cvLine(fx, cy + 44, fx, cy + 12, 15.0f, gloveLo, 1.0f);
            cvLine(fx, cy + 42, fx, cy + 14, 11.0f, gloveMid, 1.0f);
            cvLine(fx - 4, cy + 40, fx - 4, cy + 16, 3.0f, gloveHi, 0.45f);
        }
        /* 拇指：右侧一块，压在前面 */
        cvLine(c + 30, cy + 58, c + 40, cy + 30, 18.0f, gloveLo, 1.0f);
        cvLine(c + 30, cy + 56, c + 39, cy + 32, 13.0f, gloveMid, 1.0f);
        cvLine(c + 25, cy + 54, c + 33, cy + 34, 3.0f, gloveHi, 0.4f);
    }


    }
}

static void BakeItemFlare(const ItemBakeContext* bake) {
    float c = bake->c; (void)c;
    float cy = bake->cy; (void)cy;
    (void)bake;
    {

        /* ==================================================================
         * 2026-11-07（v2.4.2）：**照明弹重画**
         * ------------------------------------------------------------------
         * 用户原话：「闪光弹的左手小物品建模有点不符合审美铁律」。
         * 旧版是一个**红色圆棒 + 橙色圆球** —— 填黑之后是"棒棒糖"，
         * 靠颜色才能认出是火，正好撞在铁律一（AD-01 用剪影确认身份）上。
         *
         * 主要特征（AD-03 只保留三个）：
         *   ① 火焰：**不规则泪滴 + 左右缺口**，核心近白、外焰橙、顶上一缕暗烟
         *      —— 这是唯一的地标，填黑也读得出"在烧"
         *   ② 棒身：**上窄下宽的锥形**（透视），左亮右暗（AD-07 明暗解释厚度）
         *   ③ 底部黄铜箍：两道横箍，把"工业品"和"火把"区分开
         *
         * 负空间（AD-02）：火焰与棒身之间留**有描边的颈**，不是裸缺口。
         * ================================================================ */
        unsigned int stickHi  = 0x00E4603A;   /* 受光面 */
        unsigned int stickMid = 0x00A82C14;   /* 主体 */
        unsigned int stickLo  = 0x00521208;   /* 背光面 */
        unsigned int stickEdge= 0x00200A04;   /* 外描边 */
        unsigned int capHi    = 0x00E8C060;
        unsigned int capMid   = 0x00A87C24;
        unsigned int capEdge  = 0x00402C08;
        unsigned int smokeCol = 0x00704A3C;
        unsigned int flameOut = 0x00C43A0Cu;
        unsigned int flameMid = 0x00FF8A1Eu;
        unsigned int flameCore= 0x00FFF2C0;

        /* ---- ① 顶上那缕暗烟：先画，压在火焰后面 ---- */
        cvLine(c + 5.0f, cy - 56.0f, c + 12.0f, cy - 74.0f, 7.0f, smokeCol, 0.20f);
        cvLine(c + 11.0f, cy - 72.0f, c + 17.0f, cy - 86.0f, 4.0f, smokeCol, 0.10f);
        cvSpeckle((int)c + 12, (int)cy - 80, 12, 4, smokeCol, 0.14f);

        /* ---- ② 棒身：上窄下宽的锥形 ---- */
        {
            float body[8] = {
                c - 15.0f, cy + 50.0f,     /* 左下 */
                c + 15.0f, cy + 50.0f,     /* 右下 */
                c + 11.0f, cy - 22.0f,     /* 右上 */
                c - 11.0f, cy - 22.0f      /* 左上 */
            };
            cvPoly(body, 4, stickEdge, 1.0f);                 /* 外描边 */
        }
        {
            float body[8] = {
                c - 12.5f, cy + 48.0f,
                c + 12.5f, cy + 48.0f,
                c + 8.5f,  cy - 21.0f,
                c - 8.5f,  cy - 21.0f
            };
            cvPolyGrad(body, 4, stickHi, stickLo, 1.0f);      /* 上亮下暗 */
        }
        /* 受光侧的高光条：一条竖线就够，多了会花 */
        cvLine(c - 6.0f, cy + 44.0f, c - 5.0f, cy - 18.0f, 3.0f, stickHi, 0.55f);
        /* 背光侧的暗面：让圆柱有转过去的体积 */
        cvLine(c + 8.0f, cy + 44.0f, c + 6.5f, cy - 18.0f, 4.0f, stickLo, 0.55f);

        /* ---- ③ 中段纸带：把"棒"切成工业品的比例 ---- */
        cvLine(c - 13.0f, cy + 10.0f, c + 13.0f, cy + 10.0f, 11.0f, capEdge, 0.55f);
        cvLine(c - 12.0f, cy + 9.0f,  c + 12.0f, cy + 9.0f,  8.0f, stickMid, 1.0f);
        cvLine(c - 12.0f, cy + 6.0f,  c + 12.0f, cy + 6.0f,  2.0f, stickHi, 0.5f);

        /* ---- ④ 底部黄铜箍：两道 ---- */
        cvLine(c - 15.0f, cy + 46.0f, c + 15.0f, cy + 46.0f, 12.0f, capEdge, 1.0f);
        cvLine(c - 14.0f, cy + 45.0f, c + 14.0f, cy + 45.0f, 8.0f, capMid, 1.0f);
        cvLine(c - 14.0f, cy + 41.0f, c + 14.0f, cy + 41.0f, 2.5f, capHi, 0.7f);
        cvLine(c - 15.0f, cy + 33.0f, c + 15.0f, cy + 33.0f, 5.0f, capEdge, 0.75f);
        cvLine(c - 14.0f, cy + 32.0f, c + 14.0f, cy + 32.0f, 3.0f, capMid, 0.9f);

        /* ---- ⑤ 火焰：不规则泪滴，左右带缺口（地标） ---- */
        /* 外焰：从棒口向外扩，尖端偏右 —— 不对称才有"被风带"的感觉 */
        {
            float f1[20] = {
                c - 11.0f, cy - 19.0f,
                c - 19.0f, cy - 36.0f,
                c - 10.0f, cy - 44.0f,
                c - 16.0f, cy - 58.0f,
                c -  3.0f, cy - 76.0f,      /* 焰尖 */
                c + 10.0f, cy - 60.0f,
                c +  6.0f, cy - 48.0f,
                c + 19.0f, cy - 42.0f,
                c + 13.0f, cy - 28.0f,
                c + 10.0f, cy - 19.0f
            };
            cvPoly(f1, 10, flameOut, 0.92f);
        }
        /* 中焰：比外焰小一圈，形状不同，避免"同心圆" */
        {
            float f2[18] = {
                c -  8.0f, cy - 20.0f,
                c - 11.0f, cy - 36.0f,
                c -  3.0f, cy - 46.0f,
                c -  8.0f, cy - 56.0f,
                c +  0.0f, cy - 68.0f,
                c +  9.0f, cy - 54.0f,
                c +  4.0f, cy - 42.0f,
                c + 12.0f, cy - 34.0f,
                c +  8.0f, cy - 21.0f
            };
            cvPoly(f2, 9, flameMid, 1.0f);
        }
        /* 焰芯：偏左下的白热点，不是正圆 */
        cvEllipse(c - 1.0f, cy - 36.0f, 6.0f, 11.0f, flameCore, 0.92f);
        cvEllipse(c - 1.0f, cy - 28.0f, 4.0f, 6.0f, flameCore, 1.0f);

        /* 棒口与焰底之间：一道暗颈，封住负空间（AD-02） */
        cvLine(c - 11.0f, cy - 20.0f, c + 9.0f, cy - 20.0f, 5.0f, stickEdge, 1.0f);
        cvLine(c -  9.0f, cy - 21.0f, c + 7.0f, cy - 21.0f, 2.0f, capMid, 0.55f);

        /* ---- ⑥ 火星：三两点就够，多了会变成噪点 ---- */
        cvSpeckle((int)c + 2, (int)cy - 70, 24, 6, 0x00FFC050u, 0.62f);
        cvSpeckle((int)c + 1, (int)cy - 58, 14, 3, flameCore, 0.45f);


    }
}

static void BakeItemStone(const ItemBakeContext* bake) {
    float c = bake->c; (void)c;
    float cy = bake->cy; (void)cy;
    (void)bake;
    {

        /* ===== v2.4.59 **投石（石弹）** =====
           地标：**不规则的多边形石头 + 手心里的裂口**
           旧版是一根绿玻璃瓶 —— 和"投石"两个字完全没关系，
           而且和胶水瓶/酸瓶共用同一个剪影，填黑分不出来。

           剪影规格（art_direction.h 铁律二）：
             · 主体是一块**多棱石**（6 边，宽度 > 高度，读作"重"）
             · 左上一道**亮棱**（受光面）
             · 右下一条**暗裂缝**（唯一的细节，也是地标）
             · 手里托着 —— 底部被手掌挡住一小块 */
        unsigned int rockHi  = 0x00A8A49Cu;
        unsigned int rockMid = 0x00605C58u;
        unsigned int rockLo  = 0x00282420u;

        /* 石头外轮廓：6 边，故意不对称 */
        float r1[12] = {
            c - 44, cy - 10,
            c - 26, cy - 34,
            c + 12, cy - 38,
            c + 42, cy - 18,
            c + 38, cy + 22,
            c - 22, cy + 28
        };
        cvPoly(r1, 6, 0x000A0806u, 1.0f);          /* 外描边 */

        float r2[12] = {
            c - 38, cy - 11,
            c - 22, cy - 30,
            c + 10, cy - 33,
            c + 36, cy - 16,
            c + 32, cy + 18,
            c - 19, cy + 23
        };
        cvPolyGrad(r2, 6, rockHi, rockMid, 1.0f);  /* 主体渐变：上亮下暗 */

        /* 左上受光亮棱 —— 仿 3D 的主光 */
        cvLine(c - 30, cy - 16, c + 6, cy - 28, 6.0f, rockHi, 0.85f);

        /* 右下暗裂缝 —— **唯一的地标** */
        cvLine(c + 4, cy - 20, c + 26, cy + 12, 4.0f, rockLo, 0.9f);
        cvLine(c + 26, cy + 12, c + 34, cy + 16, 3.0f, rockLo, 0.7f);

        /* 底部阴影（压在手掌上） */
        cvLine(c - 18, cy + 20, c + 30, cy + 16, 7.0f, 0x00000000u, 0.35f);


    }
}

static void BakeItemEcho(const ItemBakeContext* bake) {
    float c = bake->c; (void)c;
    float cy = bake->cy; (void)cy;
    (void)bake;
    {

        /* ===== v2.4.59 **回声（音叉）** =====
           地标：**两根平行叉股 + 底部一个圆形共鸣腔**
           旧版是烟雾弹换色（灰圆柱）—— 完全看不出"声"。
           音叉是全世界通用的"声音"符号，剪影独特，
           填黑之后两根竖线 + 一个圆球，绝对不会认错。

           剪影规格：
             · 两根**竖直叉股**（上端略微外张）—— 最主要的识别特征
             · 底部一个**圆球共鸣腔**（带一点高光）
             · 叉股之间透空（负空间），所以填黑也能分出两根 */
        unsigned int metHi  = 0x00E8E4D8u;
        unsigned int metMid = 0x00908C82u;
        unsigned int metLo  = 0x00444038u;

        /* 两根叉股：先描边再填 */
        /* 左股 */
        cvLine(c - 22, cy - 58, c - 26, cy + 12, 16.0f, 0x000A0C10u, 1.0f);
        cvLine(c - 22, cy - 58, c - 26, cy + 12, 11.0f, metMid, 1.0f);
        cvLine(c - 27, cy - 56, c - 30, cy + 10, 3.0f, metHi, 0.75f);   /* 左缘受光 */
        /* 右股 */
        cvLine(c + 22, cy - 58, c + 26, cy + 12, 16.0f, 0x000A0C10u, 1.0f);
        cvLine(c + 22, cy - 58, c + 26, cy + 12, 11.0f, metMid, 1.0f);
        cvLine(c + 27, cy - 56, c + 30, cy + 10, 3.0f, metLo, 0.6f);    /* 右缘背光 */

        /* 底部共鸣腔：圆球 */
        cvEllipse(c, cy + 20, 30.0f, 26.0f, 0x000A0C10u, 1.0f);
        cvEllipse(c, cy + 20, 26.0f, 22.0f, metMid, 1.0f);
        cvEllipse(c - 8, cy + 12, 12.0f, 8.0f, metHi, 0.55f);           /* 球面高光 */
        cvEllipse(c + 10, cy + 30, 8.0f, 6.0f, metLo, 0.45f);           /* 球面暗部 */

        /* 球上一个发光的缝 —— "它在响" */
        cvLine(c - 12, cy + 20, c + 12, cy + 20, 3.0f, 0x00A0E8FFu, 0.9f);


    }
}

static void BakeItemSmoke(const ItemBakeContext* bake) {
    float c = bake->c; (void)c;
    float cy = bake->cy; (void)cy;
    unsigned int main = bake->main; (void)main;
    unsigned int dark = bake->dark; (void)dark;
    unsigned int hi = bake->hi; (void)hi;
    (void)bake;
    {

        /* ===== 烟雾弹 =====
           地标：**粗短圆柱 + 顶部一圈拉环 + 两侧出烟孔**
           和瓶子最大的区别是"矮胖"，填黑是个方墩子。 */
        cvLine(c, cy - 34, c, cy + 40, 70.0f, 0x000A0C10u, 1.0f);
        cvLine(c, cy - 32, c, cy + 38, 62.0f, main, 1.0f);
        /* 顶部拉环 */
        cvEllipse(c, cy - 44, 16.0f, 12.0f, 0x000A0C10u, 1.0f);
        cvEllipse(c, cy - 44, 12.0f, 9.0f, hi, 0.85f);
        cvEllipse(c, cy - 44, 6.0f, 4.5f, 0x00000000u, 1.0f);
        /* 两侧出烟孔 */
        cvLine(c - 22, cy - 6, c - 22, cy + 18, 6.0f, dark, 0.9f);
        cvLine(c + 22, cy - 6, c + 22, cy + 18, 6.0f, dark, 0.9f);
        /* 筒身受光 */
        cvLine(c - 18, cy - 26, c - 18, cy + 30, 8.0f, hi, 0.45f);


    }
}

static void BakeItemDecoy(const ItemBakeContext* bake) {
    float c = bake->c; (void)c;
    float cy = bake->cy; (void)cy;
    (void)bake;
    {

        /* ==================================================================
         * 2026-11-05 重做：**假人玩偶（诱饵）**
         * ------------------------------------------------------------------
         * 旧版的问题（对着 art_direction.h 逐条核）：
         *   · 头和身体是两个正圆 → 没有任何体积感（铁律七）
         *   · "耳朵"是两根直线 → 读成天线，不是玩偶（铁律二）
         *   · 两颗发光黄眼 → **违反铁律五**：用荧光当恐怖，还读成可爱
         *   · 纯平面单色，左上没有受光、右下没有暗部（铁律七）
         *   · 缩小到实际显示的 ~90px 后只剩一个橙色团（铁律九）
         *
         * 新设计 —— **三个主要特征**（铁律三，只给三个）：
         *   ① 绕颈的粗麻绳 + 垂下的绳头   ← 唯一地标，其它 5 个道具都没有
         *   ② 纽扣眼：一颗扣子 + 一个缝出来的空洞   ← 生物形，但不发光
         *   ③ 右臂断了，只剩稻草茬          ← 不对称，叙事：它被用过一次
         *
         * 负空间（铁律二）：头和身体之间留一道**脖子缝**，绳横穿过去；
         *   缝的两侧能看到背景，头才不像"黏在身体上"。
         *
         * 材质（铁律七）：粗麻布 = 主体 + 暗部 + 受光三层，
         *   加缝线（短深线）和稻草（亮黄丝）两种次级材质。
         *
         * 光照方向和全场一致：**左上受光、右下暗部**，左缘一条轮廓光。
         * ================================================================== */
        const unsigned int dollEdge  = 0x000A0C10u;   /* 描边 */
        const unsigned int dollShade = 0x00402C18u;   /* 暗部 */
        const unsigned int dollCloth = 0x00987048u;   /* 麻布主体 */
        const unsigned int dollLit   = 0x00D0AC74u;   /* 左上受光 */
        const unsigned int dollSeam  = 0x00281A0Cu;   /* 缝线 */
        const unsigned int dollRope  = 0x00705838u;   /* 麻绳 */
        const unsigned int dollRopeL = 0x00A08050u;   /* 绳受光 */
        const unsigned int dollStraw = 0x00D8B440u;   /* 稻草 */

        /* ---- ① 稻草：先画，让它从头和肩后面冒出来 ---- */
        for (int s = 0; s < 7; s++) {
            float a = -0.9f + (float)s * 0.32f;
            float len = 16.0f + (float)((s * 7) % 11);
            cvLine(c + 26, cy - 30,
                   c + 26 + cosf(a) * len, cy - 30 + sinf(a) * len - 10.0f,
                   2.2f, dollStraw, 0.85f);
        }

        /* ---- ② 左臂：完整，垂在体侧，手是粗糙的缝口 ---- */
        cvLine(c - 34, cy - 6, c - 46, cy + 30, 17.0f, dollEdge, 1.0f);
        cvLine(c - 34, cy - 6, c - 45, cy + 28, 12.0f, dollShade, 1.0f);
        cvLine(c - 36, cy - 6, c - 46, cy + 26, 5.0f, dollCloth, 0.9f);
        cvEllipse(c - 47, cy + 32, 8.0f, 7.0f, dollEdge, 1.0f);
        cvEllipse(c - 47, cy + 31, 6.0f, 5.0f, dollShade, 1.0f);

        /* ---- ③ 两条腿：末端是粗缝口，不是尖的 ---- */
        for (int leg = 0; leg < 2; leg++) {
            float lx = (leg == 0) ? c - 15.0f : c + 15.0f;
            float lx2 = (leg == 0) ? c - 19.0f : c + 19.0f;
            cvLine(lx, cy + 30, lx2, cy + 66, 15.0f, dollEdge, 1.0f);
            cvLine(lx, cy + 30, lx2, cy + 64, 10.0f, dollShade, 1.0f);
            cvEllipse(lx2, cy + 68, 9.0f, 7.0f, dollEdge, 1.0f);
            cvEllipse(lx2, cy + 67, 7.0f, 5.0f, dollCloth, 0.95f);
            /* 脚上的缝线 */
            cvLine(lx2 - 4, cy + 66, lx2 + 4, cy + 66, 1.6f, dollSeam, 0.85f);
        }

        /* ---- ④ 身体：梨形（肩窄胯宽），三层色 ---- */
        {
            float body[10] = {
                c - 22, cy - 18,   /* 左肩 */
                c + 22, cy - 18,   /* 右肩 */
                c + 33, cy + 12,   /* 右腰 */
                c + 30, cy + 34,   /* 右胯 */
                c - 30, cy + 34    /* 左胯 */
            };
            float bodyO[10];
            for (int i = 0; i < 5; i++) {
                bodyO[i * 2]     = c + (body[i * 2] - c) * 1.14f;
                bodyO[i * 2 + 1] = body[i * 2 + 1];
            }
            cvPoly(bodyO, 5, dollEdge, 1.0f);
            cvPolyGrad(body, 5, dollCloth, dollShade, 1.0f);
            /* 左上受光面：从肩往腰的一条斜带 */
            {
                float lit[8] = { c - 20, cy - 16, c - 2, cy - 16, c - 10, cy + 30, c - 26, cy + 30 };
                cvPolyGrad(lit, 4, dollLit, dollCloth, 0.75f);
            }
            /* 补丁缝：身体上三道短缝线（次级材质） */
            cvLine(c - 16, cy + 2,  c - 4,  cy + 6,  1.6f, dollSeam, 0.8f);
            cvLine(c - 14, cy + 10, c - 2,  cy + 14, 1.6f, dollSeam, 0.8f);
            cvLine(c + 8,  cy + 24, c + 20, cy + 22, 1.6f, dollSeam, 0.7f);
            /* 右下暗部 */
            cvEllipse(c + 20, cy + 20, 11.0f, 14.0f, dollShade, 0.55f);
        }

        /* ---- ⑤ 脖子：一道**负空间缝**，头和身体不再黏在一起 ---- */
        cvLine(c + 1, cy - 24, c + 1, cy - 14, 13.0f, dollEdge, 1.0f);
        cvLine(c + 1, cy - 23, c + 1, cy - 15, 8.0f, dollShade, 1.0f);

        /* ---- ⑥ 头：略扁、略歪（重心偏右，不对称有依据） ---- */
        cvEllipse(c + 3, cy - 46, 27.0f, 25.0f, dollEdge, 1.0f);
        cvEllipse(c + 3, cy - 46, 23.0f, 21.0f, dollCloth, 1.0f);
        /* 头的暗部（右下）与受光（左上） */
        cvEllipse(c + 11, cy - 40, 14.0f, 12.0f, dollShade, 0.6f);
        cvEllipse(c - 6, cy - 53, 12.0f, 9.0f, dollLit, 0.55f);

        /* ---- ⑦ 纽扣眼 + 空洞眼（不发光） ---- */
        cvEllipse(c - 6, cy - 50, 6.5f, 6.5f, dollEdge, 1.0f);
        cvEllipse(c - 6, cy - 50, 5.0f, 5.0f, 0x002A2018u, 1.0f);
        cvEllipse(c - 7, cy - 51, 2.4f, 2.4f, 0x00807868u, 0.85f);   /* 扣子的受光点 */
        cvEllipse(c + 12, cy - 50, 4.2f, 4.2f, dollEdge, 1.0f);
        cvEllipse(c + 12, cy - 50, 2.6f, 2.6f, 0x00140E08u, 1.0f);

        /* ---- ⑧ 十字嘴：缝出来的（唯一的"脸"细节） ---- */
        cvLine(c - 6, cy - 36, c + 10, cy - 36, 2.0f, dollSeam, 0.9f);
        cvLine(c + 2, cy - 41, c + 2, cy - 31, 2.0f, dollSeam, 0.9f);

        /* ---- ⑨ 麻绳绕颈：地标，两圈 + 垂下的绳头 ---- */
        cvLine(c - 20, cy - 19, c + 24, cy - 19, 9.0f, dollEdge, 1.0f);
        cvLine(c - 20, cy - 19, c + 24, cy - 19, 6.0f, dollRope, 1.0f);
        cvLine(c - 19, cy - 14, c + 23, cy - 14, 8.0f, dollEdge, 1.0f);
        cvLine(c - 19, cy - 14, c + 23, cy - 14, 5.0f, dollRope, 1.0f);
        /* 绳的受光边 */
        cvLine(c - 18, cy - 21, c + 18, cy - 21, 2.0f, dollRopeL, 0.75f);
        /* 绳头：从右侧打结处垂下来 */
        cvLine(c + 22, cy - 15, c + 26, cy + 6, 5.0f, dollEdge, 1.0f);
        cvLine(c + 22, cy - 15, c + 25, cy + 4, 3.0f, dollRope, 1.0f);
        cvEllipse(c + 23, cy - 17, 6.0f, 5.0f, dollEdge, 1.0f);
        cvEllipse(c + 23, cy - 17, 4.0f, 3.4f, dollRope, 1.0f);

        /* ---- ⑩ 右臂：**断了**，只剩稻草茬 ---- */
        cvLine(c + 28, cy - 6, c + 36, cy + 2, 15.0f, dollEdge, 1.0f);
        cvLine(c + 28, cy - 6, c + 35, cy + 1, 10.0f, dollShade, 1.0f);
        /* 断口：一圈深色，外面炸出稻草 */
        cvEllipse(c + 37, cy + 3, 7.0f, 6.0f, dollEdge, 1.0f);
        cvEllipse(c + 37, cy + 3, 5.0f, 4.0f, dollSeam, 1.0f);
        for (int s = 0; s < 5; s++) {
            float a = -1.2f + (float)s * 0.42f;
            float len = 11.0f + (float)((s * 5) % 8);
            cvLine(c + 37, cy + 3,
                   c + 37 + cosf(a) * len, cy + 3 + sinf(a) * len + 4.0f,
                   2.0f, dollStraw, 0.9f);
        }

        /* ---- ⑪ 左上轮廓光：让它在暗场里有体积（和怪物同一套光照） ---- */
        cvLine(c - 18, cy - 56, c - 12, cy - 30, 2.4f, dollLit, 0.5f);
        cvLine(c - 24, cy - 14, c - 30, cy + 18, 2.4f, dollLit, 0.4f);


    }
}

static void BakeItemGlue(const ItemBakeContext* bake) {
    float c = bake->c; (void)c;
    float cy = bake->cy; (void)cy;
    unsigned int main = bake->main; (void)main;
    unsigned int dark = bake->dark; (void)dark;
    unsigned int hi = bake->hi; (void)hi;
    (void)bake;
    {

        /* ===== 胶水瓶 =====
           地标：**矮胖罐 + 一根斜出的粗滴管嘴**
           读作"会挤的东西"。 */
        cvLine(c, cy - 20, c, cy + 40, 64.0f, 0x000A0C10u, 1.0f);
        cvLine(c, cy - 18, c, cy + 38, 56.0f, main, 1.0f);
        /* 滴管嘴：一根斜出来的粗管 */
        cvLine(c + 8, cy - 18, c + 26, cy - 48, 20.0f, 0x000A0C10u, 1.0f);
        cvLine(c + 8, cy - 16, c + 25, cy - 46, 14.0f, main, 1.0f);
        cvEllipse(c + 27, cy - 50, 9.0f, 8.0f, hi, 0.9f);
        /* 罐身受光 */
        cvLine(c - 16, cy - 10, c - 16, cy + 30, 8.0f, hi, 0.45f);
        cvLine(c - 20, cy + 22, c + 20, cy + 22, 4.0f, dark, 0.5f);

    }
}

void BakeItem(int id, int kind) {
    cvClear();
    float c = CANVAS_W * 0.5f, cy = 128.0f;
    unsigned int main = 0x0040A060u, dark = 0x00102818u, hi = 0x0080E0A0u;

    switch (kind) {
    case 0: main = 0x00E04020u; dark = 0x00300808u; hi = 0x00FFC080u;  break; /* 光球 */
    case 2: main = 0x00808088u; dark = 0x00181820u; hi = 0x00C8C8D0u;   break; /* 烟雾 */
    case 5: main = 0x00C04818u; dark = 0x00301008u; hi = 0x00FFA040u;  break; /* 玩偶 */
    case 6: main = 0x00C8C040u; dark = 0x00302810u; hi = 0x00F0F080u;  break; /* 胶水 */
    default: main = 0x00B0C8D8u; dark = 0x00203040u; hi = 0x00F0FFFFu;   break; /* 石弹 / 音叉 */
    }
    const ItemBakeContext bake = { c, cy, main, dark, hi };
    BakeItemGlove(&bake);
    if (kind == 0) BakeItemFlare(&bake);
    else if (kind == 8) BakeItemStone(&bake);
    else if (kind == 9) BakeItemEcho(&bake);
    else if (kind == 2) BakeItemSmoke(&bake);
    else if (kind == 5) BakeItemDecoy(&bake);
    else if (kind == 6) BakeItemGlue(&bake);
    cvFinish(SpriteBakeTarget(id));
}

static void cvBevel(float x0, float y0, float x1, float y1,
                    float t, float strength) {
    unsigned int hiC = 0x00FFFFFFu;
    unsigned int loC = 0x00000000u;
    float hiA = strength;
    float loA = strength * 0.85f;
    /* 上：亮 */
    cvLine(x0, y0 + t * 0.5f, x1, y0 + t * 0.5f, t, hiC, hiA);
    /* 左：亮（弱一点，主光在上） */
    cvLine(x0 + t * 0.5f, y0, x0 + t * 0.5f, y1, t, hiC, hiA * 0.6f);
    /* 下：暗 */
    cvLine(x0, y1 - t * 0.5f, x1, y1 - t * 0.5f, t, loC, loA);
    /* 右：暗（弱一点） */
    cvLine(x1 - t * 0.5f, y0, x1 - t * 0.5f, y1, t, loC, loA * 0.7f);
}

/* 内凹：上壁暗、下壁亮 —— 读作「凹进去一层」 */
static void cvRecess(float x0, float y0, float x1, float y1,
                     float inset, float t, float strength) {
    cvLine(x0 + inset, y0 + inset, x1 - inset, y0 + inset, t, 0x00000000u, strength);
    cvLine(x0 + inset, y1 - inset, x1 - inset, y1 - inset, t, 0x00FFFFFFu, strength * 0.7f);
    cvLine(x0 + inset, y0 + inset, x0 + inset, y1 - inset, t, 0x00FFFFFFu, strength * 0.35f);
    cvLine(x1 - inset, y0 + inset, x1 - inset, y1 - inset, t, 0x00000000u, strength * 0.5f);
}

/* ==================================================================
 * 舱门（2026-11-06）—— 两扇对开，4 帧
 * ------------------------------------------------------------------
 * 为什么是"两扇对开"而不是"推开门"：
 *   精灵永远正对镜头，没有透视。绕铰链旋转做不出来，
 *   两扇向两侧滑开是唯一在正面视角下能读成"门开了"的做法。
 *
 * 审美法则逐条落实（art_direction.h 第 171-181 行）：
 *   ② 5 段明暗   门框竖向切 5 段，不是 3 段
 *   ③ AO         门板与门框相接处压暗；与地面相接压暗
 *   ④ 负空间     门板与门框之间留 1px 真透明；中缝留 1px
 *   ⑤ 缝线虚线   铆钉用 x += 3 的短划线，不用实线
 *   ⑥ 边缘光     左上轮廓提亮 1px
 *
 * 七主题共用：门框上的主题色条画成**洋红 key**，
 * 绘制前用 SpriteAccentSet 换色（引擎已有的机制，不新增 28 张图）。
 * ================================================================ */
void BakeHatch(int id, int frame) {
    cvClear();
    float c = CANVAS_W * 0.5f;
    float baseY = 244.0f;

    const float fx0 = c - 54.0f, fx1 = c + 54.0f;   /* 门框外沿 */
    const float fy0 = baseY - 118.0f, fy1 = baseY;  /* 门框内沿 */
    const float fr  = 9.0f;                          /* 门框厚度 */
    const float ix0 = fx0 + fr, ix1 = fx1 - fr;
    const float iy0 = fy0 + fr, iy1 = fy1;

    /* ---- ② 门框：5 段明暗 ---- */
    {
        float framePts[8] = { fx0, fy0, fx1, fy0, fx1, fy1, fx0, fy1 };
        cvPolyGrad(framePts, 4, 0x006A6660u, 0x002A2826u, 1.0f);
        for (int k = 1; k <= 4; k++) {
            float y = fy0 + (fy1 - fy0) * (float)k / 5.0f;
            cvLine(fx0, y, fx1, y, 1.0f, 0x00000000u, 0.16f);       /* 分段缝 */
            cvLine(fx0, y + 1, fx1, y + 1, 1.0f, 0x00888078u, 0.10f); /* 缝上的高光 */
        }
        /* ⑥ 边缘光：左上 */
        cvLine(fx0, fy0, fx1, fy0, 2.0f, 0x00B0ACA4u, 0.85f);
        cvLine(fx0, fy0, fx0, fy1, 2.0f, 0x00908C84u, 0.60f);
        /* 阴影侧：右下（替代整圈黑描边） */
        cvLine(fx1, fy0, fx1, fy1, 2.0f, 0x00000000u, 0.55f);
        cvLine(fx0, fy1, fx1, fy1, 2.0f, 0x00000000u, 0.60f);
    }

    /* ---- ②b 门后全黑：**必须在门框之后画** ----
       第一版先画洞再画框，框的渐变把整块矩形铺满，洞就被盖成了灰色，
       于是"开门"完全读不出来。 */
    /* ---- ① 门后：全黑（开门后看到的洞） ---- */
    {
        float hole[8] = { ix0, iy0, ix1, iy0, ix1, iy1, ix0, iy1 };
        cvPoly(hole, 4, 0x00000000u, 1.0f);
    }

    /* ---- ③ 两扇门板：向外滑开 ---- */
    {
        float open = (float)frame / 3.0f;              /* 0, 1/3, 2/3, 1 */
        float innerW = ix1 - ix0;
        float halfW = innerW * 0.5f;
        float slide = halfW * open;
        const float gap = 1.5f;                        /* 中缝负空间 */

        /* 左扇 [ix0, mid-gap] 向左滑；右扇 [mid+gap, ix1] 向右滑。
           滑到 open=1 时两扇宽度 <= 0，自然消失，露出后面的黑洞。 */
        float lx0 = ix0 - slide, lx1 = ix0 + halfW - gap - slide;
        float rx0 = ix0 + halfW + gap + slide, rx1 = ix1 + slide;

        for (int side = 0; side < 2; side++) {
            float x0 = side ? rx0 : lx0;
            float x1 = side ? rx1 : lx1;
            if (x1 - x0 < 2.0f) continue;              /* 已经滑出视野 */

            float pts[8] = { x0, iy0, x1, iy0, x1, iy1, x0, iy1 };
            cvPolyGrad(pts, 4, 0x00827E76u, 0x003A3632u, 1.0f);

            /* 5 段明暗 */
            for (int k = 1; k <= 4; k++) {
                float y = iy0 + (iy1 - iy0) * (float)k / 5.0f;
                cvLine(x0 + 1, y, x1 - 1, y, 1.0f, 0x00000000u, 0.16f);
                cvLine(x0 + 1, y + 1, x1 - 1, y + 1, 1.0f, 0x00A6A29Au, 0.10f);
            }
            /* 边缘光：每扇门自己的左上 */
            cvLine(x0, iy0, x1, iy0, 1.5f, 0x00B4B0A8u, 0.80f);
            cvLine(x0, iy0, x0, iy1, 1.5f, 0x00949088u, 0.55f);
            /* 阴影侧 */
            cvLine(x1, iy0, x1, iy1, 1.5f, 0x00000000u, 0.45f);
            /* 铆钉：虚线 */
            for (float ry = iy0 + 9.0f; ry < iy1 - 7.0f; ry += 14.0f) {
                for (float rx = x0 + 4.0f; rx < x1 - 3.0f; rx += 3.0f) {
                    cvLine(rx, ry, rx + 1.0f, ry, 1.0f, 0x00DCD8D0u, 0.28f);
                }
            }
            /* AO：门板接地 */
            cvLine(x0, iy1 - 2.0f, x1, iy1 - 2.0f, 3.0f, 0x00000000u, 0.30f);
        }
    }

    /* ---- ④ 主题指示灯：**不再画进精灵** ----
       原来这里画三块洋红 key，靠 SpriteAccentSet 换色。
       但在 128 画布 + 双线性放大之后几乎没有纯洋红像素，
       判据匹配不上，灯就一直是洋红色。
       现在由渲染器在门框顶部直接画（见 render.c case 17）。 */

    /* ---- ⑤ 门框底部 AO ---- */
    cvLine(fx0, fy1 - 2.0f, fx1, fy1 - 2.0f, 4.0f, 0x00000000u, 0.35f);

    cvFinish(SpriteBakeTarget(id));
}

typedef struct PropBakeContext {
    float c;
    float baseY;
} PropBakeContext;

/* Shared construction keeps closed/open/locked cases visibly the same object. */
static void BakeSupplyCase(const PropBakeContext* bake,int opened,int locked){
    float c=bake->c,y=bake->baseY;
    float side[]={c+42,y-70,c+53,y-80,c+53,y-9,c+42,y};
    cvPolyGrad(side,4,0x004D5359u,0x00151A20u,1);
    float body[]={c-48,y-70,c+42,y-70,c+42,y,c-48,y};
    cvPolyGrad(body,4,0x007E7259u,0x00302C27u,1);
    cvBevel(c-48,y-70,c+42,y,3,.4f);
    for(int i=0;i<5;i++){
        float x=c-36+i*16;
        cvLine(x,y-60,x,y-9,4,0x00252D34u,.75f);
        cvLine(x-1,y-59,x-1,y-10,1,0x00B0A58Cu,.45f);
    }
    float lid[]={c-48,y-70,c-38,y-82,c+53,y-82,c+42,y-70};
    cvPolyGrad(lid,4,0x009D9380u,0x00454C50u,1);
    if(opened){
        float raised[]={c-48,y-74,c-43,y-129,c+41,y-124,c+42,y-74};
        cvPolyGrad(raised,4,0x007C7568u,0x002E353Cu,1);
        cvRecess(c-38,y-117,c+32,y-84,2,2,.55f);
        float cavity[]={c-41,y-70,c-32,y-80,c+42,y-80,c+35,y-65};
        cvPoly(cavity,4,0x0007090Cu,1);
        cvLine(c-40,y-69,c+35,y-65,2,0x00B8B4A4u,.75f);
        cvLine(c-28,y-78,c+29,y-74,2,0x0020272Eu,.8f);
    }
    for(int sideIndex=0;sideIndex<2;sideIndex++){
        float x=sideIndex?c+35:c-41;
        cvLine(x,y-66,x,y-6,5,0x00454F59u,1);
        cvLine(x-2,y-65,x-2,y-7,1,0x00C0CCD3u,.6f);
        for(int row=0;row<2;row++){
            float ry=row?y-11:y-61;
            cvEllipse(x,ry,2,2,0x00171D23u,1);
            cvLine(x-1,ry-1,x+1,ry-1,1,0x00D1DBDFu,.8f);
        }
    }
    cvRecess(c-16,y-44,c+13,y-28,2,2,.6f);
    cvLine(c-11,y-40,c+8,y-40,3,0x009CA6ACu,.9f);
    cvLine(c-11,y-40,c-11,y-34,2,0x00606B74u,1);
    cvLine(c+8,y-40,c+8,y-34,2,0x00606B74u,1);
    if(locked){
        cvEllipse(c-1,y-60,7,8,0x00B6BCC1u,1);
        cvEllipse(c-1,y-60,4,5,0x00212A32u,1);
        float lock[]={c-10,y-60,c+8,y-60,c+8,y-43,c-10,y-43};
        cvPolyGrad(lock,4,0x00E2BD69u,0x007C552Bu,1);
        cvEllipse(c-1,y-53,2,2,0x00251D17u,1);
        cvLine(c-1,y-52,c-1,y-48,2,0x00251D17u,1);
    }
    for(int i=0;i<14;i++){
        float x=c-32+(i*19%64),yy=y-15-(i*13%43);
        cvLine(x,yy,x+3,yy-1,.6f,0x00D6CBB3u,.22f);
    }
    cvLine(c-45,y-2,c+40,y-2,3,0x00070A0Du,.7f);
}

static void BakeIndustrialBreaker(const PropBakeContext* bake,int powered){
    float c=bake->c,y=bake->baseY;
    float side[]={c+30,y-126,c+40,y-117,c+40,y-4,c+30,y};
    cvPolyGrad(side,4,0x0048525Bu,0x00121920u,1);
    float body[]={c-34,y-126,c+30,y-126,c+30,y,c-34,y};
    cvPolyGrad(body,4,0x00838F95u,0x002B353Eu,1);
    cvBevel(c-34,y-126,c+30,y,3,.5f);
    cvRecess(c-27,y-117,c+22,y-12,3,2,.5f);
    for(int i=0;i<3;i++){
        float x=c-19+i*13;
        cvLine(x,y-127,x,y-143,5,0x003D454Du,1);
        cvEllipse(x,y-142,5,3,0x00A8A59Bu,1);
        cvLine(x,y-146,x+(i-1)*8,y-154,2,0x00232A31u,1);
        float fuse[]={x-4,y-91,x+4,y-91,x+4,y-68,x-4,y-68};
        cvPolyGrad(fuse,4,0x00A5B3B4u,0x004D626Bu,.9f);
        cvLine(x,y-86,x,y-74,2,powered?0x0089C7ADu:0x0050544Eu,.85f);
    }
    cvLine(c-24,y-102,c+17,y-102,4,0x00161E25u,1);
    cvLine(c-23,y-103,c+16,y-103,1,powered?0x0076C9B6u:0x00C38B49u,.9f);
    for(int i=0;i<5;i++)cvLine(c-21,y-51+i*5,c-2,y-51+i*5,2,0x00101820u,.9f);
    cvEllipse(c+12,y-42,7,7,0x00121A23u,1);
    float tipX=c+40,tipY=powered?y-34:y-64;
    cvLine(c+12,y-42,tipX,tipY,5,0x00464F59u,1);
    cvLine(c+12,y-43,tipX,tipY-1,1.4f,0x00C6CED0u,.8f);
    cvEllipse(tipX,tipY,5,6,powered?0x003E7B70u:0x00943D31u,1);
    float label[]={c-22,y-27,c-5,y-27,c-5,y-15,c-22,y-15};
    cvPoly(label,4,0x00B8A771u,.8f);
    cvLine(c-18,y-24,c-12,y-18,2,0x00372F23u,1);
    cvLine(c-12,y-18,c-10,y-23,1,0x00372F23u,1);
    for(int i=0;i<4;i++)cvEllipse(c+(i&1?25:-29),y-(i<2?120:7),1.7f,1.7f,0x00D2D8D7u,.8f);
}

static void BakeEvacuationDoor(const PropBakeContext* bake,int powered){
    float c=bake->c,y=bake->baseY;
    float frame[]={c-46,y,c-46,y-164,c-37,y-174,c+37,y-174,c+46,y-164,c+46,y};
    cvPolyGrad(frame,6,0x00868F92u,0x00232B33u,1);
    cvRecess(c-38,y-160,c+38,y-6,4,3,.65f);
    float inside[]={c-33,y-154,c+33,y-154,c+33,y-7,c-33,y-7};
    cvPolyGrad(inside,4,0x000F1923u,0x0005070Bu,1);
    float gap=powered?12:1;
    for(int side=0;side<2;side++){
        float x0=side?c+gap:c-32,x1=side?c+32:c-gap;
        float panel[]={x0,y-152,x1,y-152,x1,y-9,x0,y-9};
        cvPolyGrad(panel,4,0x005A6973u,0x00232C35u,1);
        cvBevel(x0,y-152,x1,y-9,2,.4f);
        for(int r=0;r<3;r++)cvRecess(x0+4,y-141+r*43,x1-4,y-110+r*43,1,2,.4f);
        cvLine(side?x0+2:x1-2,y-145,side?x0+2:x1-2,y-14,1.4f,powered?0x0086BCBFu:0x007F9299u,.85f);
        if(!powered)cvLine(x0+2,y-68,x1-2,y-68,5,0x00908769u,1);
    }
    cvLine(c-26,y-165,c+26,y-165,4,powered?0x007BD6B6u:0x00A64632u,.95f);
    cvLine(c-37,y-3,c+37,y-3,4,0x00858E91u,1);
    for(int i=0;i<4;i++){
        float x=i&1?c+41:c-41,yy=i<2?y-150:y-17;
        cvEllipse(x,yy,2,2,0x00C0C8C8u,.85f);
    }
}

/* Variant: kind == 0 */
static void BakePropCrate(const PropBakeContext* bake) {
    BakeSupplyCase(bake,0,0);
}

/* Variant: kind == 14 */
static void BakePropBeaconGround(const PropBakeContext* bake) {
    float c = bake->c; (void)c;
    float baseY = bake->baseY; (void)baseY;
    (void)bake;
    {
    /* 相位信标：地面模型（2026-11-06 重做） */
        /* ==================================================================
         * 用户反馈：「你得在地上建的模像一个那个信标一样，然后冒光的」。
         *
         * 第一版是一个矮圆柱 + 顶部一个椭圆 —— 太小、太暗，
         * 在暗场里读不出"这是信标"，更像一块石头。
         *
         * 重做：
         *   · 拉高到 96 像素（原来 46），细长才像"标杆"
         *   · 三段结构：底座 / 杆身 / 顶灯
         *   · **顶灯 + 地面光晕**都是亮的，暗场里能被一眼看到
         *   · 光晕用椭圆叠在脚下，读作"它在往地上打光"
         * ================================================================== */
        float bx0 = c - 26, bx1 = c + 26;
        float by0 = baseY - 100, by1 = baseY;

        /* ① 地面光晕（先画，被主体压住一半） */
        cvEllipse(c, by1 - 4, 34.0f, 11.0f, 0x00A050FFu, 0.35f);
        cvEllipse(c, by1 - 4, 24.0f, 7.5f, 0x00C070FFu, 0.55f);

        /* ② 底座：三层台阶，越往上越窄 */
        {
            float b1[8] = { bx0 - 6, by1 - 2, bx1 + 6, by1 - 2, bx1 + 2, by1 - 12, bx0 - 2, by1 - 12 };
            cvPolyGrad(b1, 4, 0x00605A50u, 0x002A2622u, 1.0f);
            float b2[8] = { bx0 - 1, by1 - 12, bx1 + 1, by1 - 12, bx1 - 5, by1 - 20, bx0 + 5, by1 - 20 };
            cvPolyGrad(b2, 4, 0x006E685Cu, 0x00302C26u, 1.0f);
            /* 底座边缘光 + 接地 AO */
            cvLine(bx0 - 6, by1 - 2, bx1 + 6, by1 - 2, 2.0f, 0x00000000u, 0.55f);
            cvLine(bx0 - 1, by1 - 12, bx1 + 1, by1 - 12, 1.5f, 0x00B0AA9Eu, 0.55f);
        }

        /* ③ 杆身：细长，5 段明暗 */
        {
            float px0 = c - 7, px1 = c + 7;
            float py0 = by0 + 26, py1 = by1 - 18;
            float p[8] = { px0, py0, px1, py0, px1 - 2, py1, px0 + 2, py1 };
            cvPolyGrad(p, 4, 0x00787268u, 0x002E2A26u, 1.0f);
            for (int k = 1; k <= 4; k++) {
                float y = py0 + (py1 - py0) * (float)k / 5.0f;
                cvLine(px0 + 2, y, px1 - 2, y, 1.0f, 0x00000000u, 0.18f);
                cvLine(px0 + 2, y + 1, px1 - 2, y + 1, 1.0f, 0x00A29C90u, 0.10f);
            }
            cvLine(px0, py0, px0, py1, 1.5f, 0x00B4AEA2u, 0.70f);   /* 左上边缘光 */
            cvLine(px1, py0, px1, py1, 1.5f, 0x00000000u, 0.50f);   /* 阴影侧 */
        }

        /* ④ 顶灯：三圈同心，越里越亮 —— 这是"冒光"的主体 */
        cvEllipse(c, by0 + 22, 20.0f, 15.0f, 0x008040FFu, 0.30f);
        cvEllipse(c, by0 + 22, 13.0f, 10.0f, 0x00B060FFu, 0.60f);
        cvEllipse(c, by0 + 22, 8.0f,  6.5f, 0x00E8C0FFu, 0.95f);
        /* 灯下方的暗槽：让光看起来是"从壳里透出来"而不是贴上去 */
        cvLine(c - 10, by0 + 32, c + 10, by0 + 32, 2.5f, 0x00000000u, 0.60f);

        /* ⑤ 灯罩的两条支撑（把灯和杆连起来，不是悬空） */
        cvLine(c - 9, by0 + 30, c - 6, by0 + 26, 2.5f, 0x004A463Eu, 1.0f);
        cvLine(c + 9, by0 + 30, c + 6, by0 + 26, 2.5f, 0x004A463Eu, 1.0f);


    }
}

/* Variant: kind == 16 */
static void BakePropSpider(const PropBakeContext* bake){
    float x=bake->c,y=bake->baseY-39;
    cvEllipse(x,bake->baseY-2,61,7,0x010204,.4f);
    for(int side=-1;side<=1;side+=2)for(int i=0;i<4;i++){
        float hipY=y-8+i*6,kx=x+side*(30+i*7),ky=y-32+i*9;
        float fx=x+side*(57+i*4),fy=y+22+i*3;
        cvLine(x+side*13,hipY,kx,ky,4.6f,0x11161C,1);
        cvLine(kx,ky,fx,fy,3.5f,0x2C3541,1);
        cvLine(kx-side,ky,fx-side,fy,1.0f,0x8A8B85,.8f);
        cvLine(fx,fy,fx-side*4,fy+7,1.6f,0x171A22,1);
        cvEllipse(kx,ky,3,3.6f,0x67625B,.9f);
        /* 关节处包一圈撕纸，像标本针固定过的节肢。 */
        cvLine(kx - 4.0f, ky - 1.0f, kx + 4.0f, ky + 1.0f, 1.1f, 0x00D0C29Cu, 0.60f);
        cvLine(kx, ky - 4.0f, kx + 1.0f, ky + 4.0f, 0.7f, 0x00C84A78u, 0.55f);
        for(int hair=0;hair<4;hair++){
            float t=(hair+1)*.16f,hx=kx+(fx-kx)*t,hy=ky+(fy-ky)*t;
            cvLine(hx,hy,hx+side*3,hy-3,.5f,0xA8A08C,.7f);
        }
    }
    cvEllipse(x,y-3,27,23,0x161B25,1);cvEllipse(x-3,y-8,22,19,0x4E5060,1);
    for(int i=0;i<5;i++){
        float yy=y-21+i*7;
        cvLine(x-17,yy,x+17,yy+2,1.2f,0x171B23,.8f);
        cvLine(x-16,yy-1,x+15,yy+1,.65f,0x969085,.5f);
    }
    cvEllipse(x,y+15,17,14,0x252A35,1);cvEllipse(x-2,y+10,14,10,0x676373,1);
    /* 腹部贴一张小标签，套色偏移之后再压一层纸。 */
    {
        float lab[8] = { x - 12.0f, y + 8.0f, x + 9.0f, y + 6.0f,
                         x + 10.0f, y + 20.0f, x - 11.0f, y + 22.0f };
        cvPoly(lab, 4, 0x00D0C29Cu, 0.38f);
        for (int i = 0; i < 3; i++)
            cvLine(x - 8.0f, y + 12.0f + (float)i * 3.5f,
                   x + 6.0f, y + 11.5f + (float)i * 3.5f, 0.6f, 0x0016120Cu, 0.55f);
    }
    for(int side=-1;side<=1;side+=2){
        for(int i=0;i<4;i++){
            float xx=x+side*(4+i*3),yy=y+9+(i%2)*3;
            cvEllipse(xx,yy,1.6f-i*.15f,1.5f,0x9C271C,1);
            cvEllipse(xx-.35f,yy-.3f,.55f,.45f,0xF0CCAC,1);
        }
        cvLine(x+side*7,y+24,x+side*13,y+34,3.6f,0x17141B,1);
        cvLine(x+side*13,y+34,x+side*4,y+30,1.5f,0xCAC2A4,.9f);
        cvLine(x+side*14,y+20,x+side*23,y+28,1.2f,0x727276,.8f);
    }
    cvLine(x-1,y+31,x-5,y+43,.4f,0xA0B0AE,.5f);
    cvLine(x+1,y+31,x+6,y+44,.4f,0xA0B0AE,.5f);
}


/* Variant: kind == 15 */
static void BakePropBeaconHand(const PropBakeContext* bake) {
    float c = bake->c; (void)c;
    float baseY = bake->baseY; (void)baseY;
    (void)bake;
    {
    /* 相位信标：手持模型（2026-11-06） */
        /* ==================================================================
         * 用户要求：「手持建模也没有做」。
         * 竖持的小装置：手柄 + 主体 + 顶端两颗充能点。
         * 构图上比地面模型窄、偏竖，才能在画面右下角读出来。
         * ================================================================== */
        float cxx = c;
        float hy0 = baseY - 96, hy1 = baseY - 10;

        /* 手柄 */
        cvLine(cxx, hy1, cxx, hy0 + 44, 9.0f, 0x00322E28u, 1.0f);
        cvLine(cxx - 3, hy1, cxx - 3, hy0 + 44, 2.0f, 0x00706A60u, 0.55f);

        /* 主体 */
        {
            float body[8] = { cxx - 15, hy0 + 10, cxx + 15, hy0 + 10, cxx + 13, hy0 + 44, cxx - 13, hy0 + 44 };
            cvPolyGrad(body, 4, 0x007A746Au, 0x002E2A26u, 1.0f);
            for (int k = 1; k <= 4; k++) {
                float y = (hy0 + 10) + 34.0f * (float)k / 5.0f;
                cvLine(cxx - 13, y, cxx + 13, y, 1.0f, 0x00000000u, 0.16f);
                cvLine(cxx - 13, y + 1, cxx + 13, y + 1, 1.0f, 0x009A948Au, 0.10f);
            }
            cvLine(cxx - 15, hy0 + 10, cxx + 15, hy0 + 10, 1.5f, 0x00B4AEA2u, 0.85f);
            cvLine(cxx - 15, hy0 + 10, cxx - 13, hy0 + 44, 1.5f, 0x00928E84u, 0.55f);
            cvLine(cxx + 15, hy0 + 10, cxx + 13, hy0 + 44, 1.5f, 0x00000000u, 0.45f);
        }

        /* 顶端两颗充能点（表示还剩 2 次） */
        cvEllipse(cxx - 6, hy0 + 18, 3.2f, 3.2f, 0x00D890FFu, 0.95f);
        cvEllipse(cxx + 6, hy0 + 18, 3.2f, 3.2f, 0x00D890FFu, 0.95f);

        /* 底部 AO */
        cvLine(cxx - 10, hy1 - 2, cxx + 10, hy1 - 2, 3.0f, 0x00000000u, 0.30f);


    }
}

/* Variant: kind == 1 */
static void BakePropBreaker(const PropBakeContext* bake) {
    BakeIndustrialBreaker(bake,0);
}

/* Variant: kind == 2 */
static void BakePropExit(const PropBakeContext* bake) {
    BakeEvacuationDoor(bake,0);
}

/* Variant: kind == 4 */
static void BakePropOpenCrate(const PropBakeContext* bake) {
    BakeSupplyCase(bake,1,0);
}

/* Variant: kind == 5 */
static void BakePropLockedCrate(const PropBakeContext* bake) {
    BakeSupplyCase(bake,0,1);
}

/* Variant: kind == 7 */
static void BakePropMimicCrate(const PropBakeContext* bake) {
    float c = bake->c; (void)c;
    float baseY = bake->baseY; (void)baseY;
    (void)bake;
    {

        /* ============================================================
         * 宝箱怪伪装态（2026-11-05）
         * ------------------------------------------------------------
         * 和普通箱子（kind 0）**同宽同高** —— 铁律一：剪影必须仍读作"箱子"，
         * 否则玩家一眼就看出不对，破绽设计就废了。
         *
         * 只改两处「约定部件」（铁律十六）：
         *   ① 金属包边从暗铁色改成**暗金色** → 看起来更值钱（这是诱饵）
         *   ② 锁扣是**双层**（普通箱子单层） → 破绽②，细看才发现
         * ============================================================ */
        float b[8]  = { c - 46, baseY - 96, c + 46, baseY - 96, c + 46, baseY, c - 46, baseY };
        float b2[8] = { c - 40, baseY - 90, c + 40, baseY - 90, c + 40, baseY - 4, c - 40, baseY - 4 };
        cvPoly(b, 4, 0x00080604u, 1.0f);
        /* 箱体：比普通箱的木色更暖更亮（金丝楠那种） */
        cvPolyGrad(b2, 4, 0x00A87E46u, 0x004A3218u, 1.0f);
        for (int i = 0; i < 3; i++)
            cvLine(c - 40, baseY - 74 + (float)i * 26, c + 40, baseY - 74 + (float)i * 26,
                   3.0f, 0x00341E0Cu, 0.8f);

        /* ① 暗金色包边（区别 1：普通箱是 0x00605848 铁灰） */
        cvLine(c - 40, baseY - 90, c - 40, baseY - 4, 8.0f, 0x00C8A040u, 1.0f);
        cvLine(c + 40, baseY - 90, c + 40, baseY - 4, 8.0f, 0x00C8A040u, 1.0f);
        cvLine(c - 40, baseY - 90, c + 40, baseY - 90, 5.0f, 0x00E8C050u, 0.95f);
        cvSpeckle((int)c, (int)baseY - 50, 36, 50, 0x00402010u, 0.35f);
        cvBevel(c - 40, baseY - 90, c + 40, baseY - 4, 3.0f, 0.42f);
        /* 包角高光：让金边读作金属而不是涂色 */
        cvLine(c - 40, baseY - 90, c - 30, baseY - 90, 4.0f, 0x00FFE080u, 0.6f);
        cvLine(c + 30, baseY - 4,  c + 40, baseY - 4,  4.0f, 0x00603C10u, 0.6f);

        /* ② **双层锁扣**（破绽②）：上面一块大、下面一块小的错位 2 像素 */
        cvLine(c - 9, baseY - 62, c + 9, baseY - 62, 15.0f, 0x00D8B050u, 1.0f);
        cvLine(c - 9, baseY - 62, c + 9, baseY - 62, 11.0f, 0x00F0D070u, 0.95f);
        /* 第二层：偏右 2 像素、低 14 像素 —— 这就是"歪锁扣"的破绽 */
        cvLine(c - 2, baseY - 48, c + 14, baseY - 48, 9.0f, 0x00C8A040u, 1.0f);
        cvLine(c + 1, baseY - 48, c + 11, baseY - 48, 5.0f, 0x00FFE890u, 0.8f);
        cvEllipse(c, baseY - 62, 3.0f, 3.5f, 0x00140C04u, 1.0f);

    }
}

/* Variant: kind == 6 */
static void BakePropTrash(const PropBakeContext* bake) {
    float c = bake->c; (void)c;
    float baseY = bake->baseY; (void)baseY;
    (void)bake;
    {
     /* 藏身点：垃圾桶 */
        float body[8] = { c - 34, baseY - 150, c + 34, baseY - 150, c + 42, baseY, c - 42, baseY };
        cvPoly(body, 4, 0x000A0C0Au, 1.0f);
        float body2[8] = { c - 28, baseY - 144, c + 28, baseY - 144, c + 35, baseY - 5, c - 35, baseY - 5 };
        cvPolyGrad(body2, 4, 0x00607868u, 0x00283028u, 1.0f);
        for (int i = -2; i <= 2; i++)
            cvLine(c + (float)i * 13.0f, baseY - 140, c + (float)i * 16.0f, baseY - 8, 2.0f, 0x00202820u, 0.75f);
        cvEllipse(c, baseY - 150, 36.0f, 9.0f, 0x003A4A40u, 1.0f);
        cvEllipse(c, baseY - 152, 30.0f, 6.0f, 0x00060604u, 0.95f);
        cvLine(c - 38, baseY - 158, c + 38, baseY - 158, 5.0f, 0x00809878u, 0.9f);
        cvLine(c - 38, baseY - 158, c - 34, baseY - 168, 4.0f, 0x00607858u, 0.8f);
        cvLine(c + 38, baseY - 158, c + 34, baseY - 168, 4.0f, 0x00607858u, 0.8f);
        cvLine(c - 34, baseY - 168, c + 34, baseY - 168, 4.0f, 0x00809878u, 0.85f);
        /* v2.4.6 **圆柱受光**（不是斜角 —— 垃圾桶是圆的，斜角会把它读成方块）
           左侧 1/3 打亮、右侧 1/3 压暗，中间过渡，就读成「圆桶」了。 */
        cvLine(c - 26, baseY - 140, c - 20, baseY - 8, 7.0f, 0x00FFFFFFu, 0.26f);
        cvLine(c - 16, baseY - 142, c - 12, baseY - 8, 5.0f, 0x00FFFFFFu, 0.14f);
        cvLine(c + 16, baseY - 142, c + 22, baseY - 8, 7.0f, 0x00000000u, 0.30f);
        cvLine(c + 24, baseY - 140, c + 30, baseY - 8, 5.0f, 0x00000000u, 0.18f);
        /* 桶口内壁：上沿受光、内圈暗 */
        cvLine(c - 30, baseY - 152, c + 30, baseY - 152, 2.0f, 0x00FFFFFFu, 0.30f);
        cvLine(c - 28, baseY - 146, c + 28, baseY - 146, 2.0f, 0x00000000u, 0.35f);

    }
}

/* Variant: fallback */
static void BakePropSceneFixture(const PropBakeContext* bake) {
    SpriteBakeCanvasState* canvas = SpriteBakeCanvas();
    float c = bake->c; (void)c;
    float baseY = bake->baseY; (void)baseY;
    (void)bake;
    {

        /* 安全屋（v2.2 第三次重做）
           第一版是营火 -> 一颗蛋。第二版是沙袋+立杆+油灯 -> 一根路灯 + 两块鹅卵石 + 一张饼。
           教训（来自精灵审计）：
             · 圆/椭圆只读作"石头、土豆、蛋"；要读作"沙袋"必须是圆角矩形 + 束口线
             · 一个物件代表不了"一块区域"，必须先画地面圆环再画门框
             · 地面的"光池"如果 alpha 被写成 255，就是一张不透明薄饼
           这一版按审计的建议：地面断续圆环（留一个入口缺口）+ 门框 + 挂灯 + 沙袋堆。 */
        float baseYY = baseY - 2.0f;

        /* ① 地面圆环：2px 断续弧线，留一个缺口当入口。
              画在画布最下方，环宽铺满 —— "范围"是靠这个读出来的。 */
        {
            float rx = 108.0f, ry = 16.0f;
            for (int a = 0; a < 360; a += 6) {
                /* 入口缺口：60~120 度不画 */
                if (a > 55 && a < 125) continue;
                float rad = (float)a * 3.14159f / 180.0f;
                float ax = c + cosf(rad) * rx;
                float ay = baseYY + sinf(rad) * ry;
                cvEllipse(ax, ay, 2.2f, 1.8f, 0x00E0A050u, 0.55f);
            }
        }

        /* ② 门框：两根立柱 + 一根横梁，顶到画布 90% */
        {
            float fx0 = c - 46.0f, fx1 = c + 46.0f;
            float topY = baseY - 196.0f;
            /* 立柱 */
            for (int i = 0; i < 2; i++) {
                float px = (i == 0) ? fx0 : fx1;
                float post[8] = { px - 5, topY, px + 5, topY, px + 5, baseYY - 6, px - 5, baseYY - 6 };
                cvPoly(post, 4, 0x00100C08u, 1.0f);
                float post2[8] = { px - 3, topY + 3, px + 3, topY + 3, px + 3, baseYY - 9, px - 3, baseYY - 9 };
                cvPolyGrad(post2, 4, 0x00907850u, 0x004A3A22u, 1.0f);
            }
            /* 横梁 */
            float lintel[8] = { fx0 - 9, topY - 12, fx1 + 9, topY - 12, fx1 + 9, topY, fx0 - 9, topY };
            cvPoly(lintel, 4, 0x00100C08u, 1.0f);
            float lintel2[8] = { fx0 - 6, topY - 9, fx1 + 6, topY - 9, fx1 + 6, topY - 2, fx0 - 6, topY - 2 };
            cvPolyGrad(lintel2, 4, 0x00A08458u, 0x00544228u, 1.0f);
            /* 门洞内：暖色薄雾（比外面亮一档，但**半透明**） */
            for (int yy = (int)topY; yy < (int)(baseYY - 8); yy++) {
                for (int xx = (int)(fx0 + 5); xx < (int)(fx1 - 4); xx++) {
                    if (xx < 0 || xx >= CANVAS_W || yy < 0 || yy >= CANVAS_H) continue;
                    unsigned int* q = &canvas->pixels[(size_t)yy * CANVAS_W + xx];
                    unsigned int old = *q;
                    if ((old >> 24) > 40) continue;        /* 别盖住已画的东西 */
                    int orr = (old >> 16) & 0xFF, og = (old >> 8) & 0xFF, ob = old & 0xFF;
                    orr += 26; og += 18; ob += 8;
                    int na = (old >> 24) + 70;
                    if (na > 190) na = 190;
                    if (orr > 255) orr = 255;
                    if (og > 255) og = 255;
                    if (ob > 255) ob = 255;
                    *q = ((unsigned int)na << 24) | (orr << 16) | (og << 8) | ob;
                }
            }
        }

        /* ③ 挂灯：吊在横梁正中 */
        {
            float lx = c, ly = baseY - 178.0f;
            cvLine(lx, ly - 8, lx, ly, 2.0f, 0x00100C08u, 1.0f);
            float cage[8] = { lx - 10, ly, lx + 10, ly, lx + 7, ly + 24, lx - 7, ly + 24 };
            float co[8];
            for (int i = 0; i < 4; i++) {
                co[i * 2] = lx + (cage[i * 2] - lx) * 1.15f;
                co[i * 2 + 1] = ly + (cage[i * 2 + 1] - ly) * 1.08f;
            }
            cvPoly(co, 4, 0x00140C06u, 1.0f);
            cvPoly(cage, 4, 0x006A5220u, 1.0f);
            cvEllipse(lx, ly + 13.0f, 6.5f, 7.5f, 0x00FFE8A0u, 1.0f);
            cvEllipse(lx, ly + 13.0f, 3.6f, 4.4f, 0x00FFFFFFu, 0.92f);
            float cap[8] = { lx - 12, ly - 1, lx + 12, ly - 1, lx + 5, ly - 8, lx - 5, ly - 8 };
            cvPoly(cap, 4, 0x00140C06u, 1.0f);
            cvPoly(cap, 4, 0x007A5E28u, 0.9f);
            /* 只留一个 glow，且收紧半径 */
            cvGlow(lx, ly + 13.0f, 30.0f, 0x00FFB050u, 0.28f);
        }

        /* ④ 沙袋：2-1-2 堆在门框右侧。
              圆角矩形 + 束口线 + 缝线 —— 椭圆读作石头，这是审计里最关键的一条。 */
        {
            float bags[5][3] = {
                { c + 66.0f, baseYY - 10.0f, 1.0f },
                { c + 66.0f, baseYY - 26.0f, 1.0f },
                { c + 88.0f, baseYY - 10.0f, 1.0f },
                { c + 88.0f, baseYY - 26.0f, 1.0f },
                { c + 77.0f, baseYY - 42.0f, 1.0f }
            };
            for (int i = 0; i < 5; i++) {
                float bx = bags[i][0], by = bags[i][1];
                float w = 30.0f, h = 16.0f;
                /* 圆角矩形：中心实心 + 两端半圆 */
                cvEllipse(bx - w * 0.5f + 7.0f, by, 7.0f, h * 0.5f, 0x00120E0Au, 1.0f);
                cvEllipse(bx + w * 0.5f - 7.0f, by, 7.0f, h * 0.5f, 0x00120E0Au, 1.0f);
                for (int yy = (int)(by - h * 0.5f); yy <= (int)(by + h * 0.5f); yy++) {
                    for (int xx = (int)(bx - w * 0.5f + 6); xx <= (int)(bx + w * 0.5f - 6); xx++) {
                        cvPx(xx, yy, 0x00120E0Au, 1.0f);
                    }
                }
                /* 里面填土黄 */
                cvEllipse(bx - w * 0.5f + 8.0f, by - 1.0f, 6.0f, h * 0.5f - 1.5f, 0x00584A30u, 1.0f);
                cvEllipse(bx + w * 0.5f - 8.0f, by - 1.0f, 6.0f, h * 0.5f - 1.5f, 0x00584A30u, 1.0f);
                for (int yy = (int)(by - h * 0.5f + 2); yy <= (int)(by + h * 0.5f - 2); yy++) {
                    for (int xx = (int)(bx - w * 0.5f + 6); xx <= (int)(bx + w * 0.5f - 6); xx++) {
                        cvPx(xx, yy, 0x00584A30u, 1.0f);
                    }
                }
                /* 束口线（两端各一道竖线）+ 中间缝线 */
                cvLine(bx - w * 0.5f + 9.0f, by - h * 0.5f + 1, bx - w * 0.5f + 9.0f, by + h * 0.5f - 1, 1.6f, 0x00362C1Au, 0.9f);
                cvLine(bx + w * 0.5f - 9.0f, by - h * 0.5f + 1, bx + w * 0.5f - 9.0f, by + h * 0.5f - 1, 1.6f, 0x00362C1Au, 0.9f);
                cvLine(bx - w * 0.5f + 10.0f, by, bx + w * 0.5f - 10.0f, by + 0.5f, 1.2f, 0x0074643Cu, 0.7f);
                /* 顶部受光 */
                cvLine(bx - w * 0.5f + 10.0f, by - h * 0.5f + 2, bx + w * 0.5f - 10.0f, by - h * 0.5f + 2, 1.4f, 0x007C6A44u, 0.6f);
            }
        }

    }
}

void BakeProp(int id, int kind) {
    SpriteBakeCanvasState* canvas = SpriteBakeCanvas();
    canvas->scale=(kind==16 || kind==7 || kind==0 || kind==1 || kind==2 || kind==4 || kind==5 || kind==17 || kind==18)?SPR_CREATURE_SCALE:1;
    cvClear();
    if (kind==16 && BakeEvolvedCreature(kind,0)) {
        cvFinish(SpriteBakeTarget(id));
        canvas->scale=1;
        return;
    }

    float c = CANVAS_W * 0.5f;
    float baseY = 244.0f;
    PropBakeContext bake = { c, baseY };
    if (kind == 0) {
        BakePropCrate(&bake);
    } else if (kind == 17) {
        BakeIndustrialBreaker(&bake,1);
    } else if (kind == 18) {
        BakeEvacuationDoor(&bake,1);
    } else if (kind == 14) {
        BakePropBeaconGround(&bake);
    } else if (kind == 16) {
        BakePropSpider(&bake);
    } else if (kind == 15) {
        BakePropBeaconHand(&bake);
    } else if (kind == 1) {
        BakePropBreaker(&bake);
    } else if (kind == 2) {
        BakePropExit(&bake);
    } else if (kind == 4) {
        BakePropOpenCrate(&bake);
    } else if (kind == 5) {
        BakePropLockedCrate(&bake);
    } else if (kind == 7) {
        BakePropMimicCrate(&bake);
    } else if (kind == 6) {
        BakePropTrash(&bake);
    } else {
        BakePropSceneFixture(&bake);
    }

    if(kind==16){BakeCreatureSurface(14);BakeEvolutionDetails(16,0);}
    if(kind==7)BakeCreatureSurface(13);
    if(kind==16 || kind==7)BakeMisprintFringe(kind==16?16:13);
    cvFinish(SpriteBakeTarget(id));
    canvas->scale=1;
}

/* ------------------------------------------------------------------ */
/* UI 资源（v2.2）                                                      */
/*   9-slice 母版：四角各一颗螺丝，双线边框（外亮内暗），底部警示条。      */
/*   四角 12px 不拉伸，四条边拉伸 —— 见 UiPanel()。                      */
/* ------------------------------------------------------------------ */
void BakeUiPanel(int id, int hot) {
    cvClear();
    unsigned int edgeHi = hot ? 0x00E0A050u : 0x00807868u;
    unsigned int edgeLo = hot ? 0x00604020u : 0x00342C24u;
    unsigned int fill   = hot ? 0x00241E16u : 0x001C1814u;   /* v2.2：提亮，暗场里要看得见 */

    /* 底 */
    float bg[8] = { 4, 4, 252, 4, 252, 252, 4, 252 };
    cvPoly(bg, 4, fill, 0.97f);
    /* 双线边框 */
    float o[8] = { 2, 2, 254, 2, 254, 254, 2, 254 };
    cvPoly(o, 4, edgeHi, 0.9f);
    float i1[8] = { 6, 6, 250, 6, 250, 250, 6, 250 };
    cvPoly(i1, 4, edgeLo, 0.9f);
    float i2[8] = { 9, 9, 247, 9, 247, 247, 9, 247 };
    cvPoly(i2, 4, fill, 1.0f);

    /* 底部警示条（工业感的主要来源） */
    for (int x = 8; x < 248; x += 16) {
        float st[8] = { (float)x, 244, (float)x + 8, 244, (float)x + 16, 250, (float)x + 8, 250 };
        cvPoly(st, 4, hot ? 0x00C08020u : 0x00504820u, 0.75f);
    }

    /* 四角螺丝 */
    int sx[4] = { 12, 244, 12, 244 }, sy[4] = { 12, 12, 244, 244 };
    for (int i = 0; i < 4; i++) {
        cvEllipse((float)sx[i], (float)sy[i], 6.0f, 6.0f, 0x000A0806u, 1.0f);
        cvEllipse((float)sx[i], (float)sy[i], 4.0f, 4.0f, 0x00908878u, 1.0f);
        cvLine((float)sx[i] - 3, (float)sy[i], (float)sx[i] + 3, (float)sy[i] + 1, 1.6f, 0x00302820u, 1.0f);
    }

    cvFinish(SpriteBakeTarget(id));
}

void BakeUiCursor(void) {
    cvClear();
    /* 三角箭头：实心 + 描边 */
    float t[6] = { 40, 30, 40, 226, 150, 128 };
    float t2[6] = { 52, 52, 52, 204, 128, 128 };
    cvPoly(t, 3, 0x000A0806u, 1.0f);
    cvPolyGrad(t2, 3, 0x00FFD880u, 0x00C08020u, 1.0f);
    cvFinish(SpriteBakeTarget(SPR_UI_CURSOR));
}

void BakeUiRow(void) {
    cvClear();
    /* 选中行底：中间亮两头暗的横带 */
    for (int y = 0; y < CANVAS_H; y++) {
        float t = ((float)y / (float)(CANVAS_H - 1)) - 0.5f;
        float k = 1.0f - fabsf(t) * 2.2f;
        if (k < 0.0f) continue;
        for (int x = 0; x < CANVAS_W; x++) {
            float fx = (float)x / (float)(CANVAS_W - 1);
            float fade = 1.0f;
            if (fx < 0.10f) fade = fx / 0.10f;
            if (fx > 0.90f) fade = (1.0f - fx) / 0.10f;
            cvPx(x, y, 0x00C8A050u, k * fade * 0.22f);
        }
    }
    cvFinish(SpriteBakeTarget(SPR_UI_ROW));
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */
void BakeRoomFixture(int id,int kind){
    cvClear();float cx=CANVAS_W*.5f,base=244;
    unsigned int accent[7]={0xC08036,0x8AA1AC,0x52C5DA,0xD5E2CF,0x88BBED,0xC83720,0x9B2C32};
    unsigned int a=accent[kind];
    cvEllipse(cx,base-2,64,10,0x050607,.65f);
    if(kind==0){
        float shell[]={cx-44,base-18,cx-44,base-156,cx-27,base-176,cx+36,base-164,cx+48,base-20};
        cvPolyGrad(shell,6,0x625244,0x282626,1);cvEllipse(cx-2,base-94,29,29,0x11181B,1);
        cvEllipse(cx-2,base-94,22,22,a,.8f);cvLine(cx-2,base-116,cx+14,base-97,4,0xE8DEAA,1);
        cvLine(cx-43,base-145,cx-68,base-145,14,0x555353,1);cvLine(cx-68,base-145,cx-68,base-37,13,0x3E4746,1);
    }else if(kind==1){
        for(int row=0;row<4;row++){float y=base-24-row*39;float face[]={cx-53,y,cx-53,y-28,cx+48,y-28,cx+58,y};cvPolyGrad(face,4,0x47555A,0x18262A,1);cvLine(cx-15,y-15,cx+14,y-15,3,a,1);}
        cvLine(cx-60,base-180,cx-60,base,7,0x1B252B,1);cvLine(cx+61,base-180,cx+61,base,7,0x697476,1);
    }else if(kind==2){
        float baseplate[]={cx-56,base-12,cx-44,base-43,cx+39,base-43,cx+61,base-12};cvPolyGrad(baseplate,4,0x51646C,0x18222B,1);
        cvLine(cx,base-40,cx,base-175,14,0x6D828A,1);
        for(int i=0;i<6;i++)cvEllipse(cx,base-57-i*21,28-i*2,7,0x8C5535,1);
        cvGlow(cx,base-185,20,a,.5f);cvEllipse(cx,base-178,15,15,a,.9f);
    }else if(kind==3){
        float bed[]={cx-73,base-35,cx-50,base-83,cx+55,base-83,cx+79,base-35};cvPolyGrad(bed,4,0xB1BAAE,0x4D615D,1);
        cvLine(cx-62,base-37,cx-62,base,6,0x6A7778,1);cvLine(cx+65,base-36,cx+65,base,6,0x5B6C6E,1);
        cvEllipse(cx-26,base-71,25,9,0xDDD9BA,1);cvLine(cx+22,base-77,cx+38,base-45,14,0x67302B,.85f);
        cvLine(cx-61,base-72,cx-72,base-155,4,0x9DAEB5,1);cvEllipse(cx-72,base-126,8,17,0x8E3525,.8f);
    }else if(kind==4){
        float crystal[]={cx,base-190,cx-42,base-85,cx,base-38,cx+45,base-86};cvPolyGrad(crystal,4,0xBCDDF1,0x1E495E,.93f);
        cvLine(cx,base-187,cx,base-40,2,0xF1FAFF,.9f);cvGlow(cx,base-109,30,a,.2f);
        float stand[]={cx-55,base-12,cx-38,base-42,cx+39,base-42,cx+55,base-12};cvPolyGrad(stand,4,0x687680,0x222932,1);
    }else if(kind==5){
        float plinth[]={cx-64,base-11,cx-47,base-68,cx+44,base-68,cx+64,base-11};cvPolyGrad(plinth,4,0x5F5045,0x241A19,1);
        float tooth[]={cx-37,base-70,cx-49,base-149,cx-16,base-118,cx,base-190,cx+18,base-116,cx+47,base-148,cx+35,base-70};cvPolyGrad(tooth,7,0xA49072,0x44242A,1);
        cvEllipse(cx,base-92,21,8,0xA51E16,1);cvGlow(cx,base-91,21,0xE83317,.3f);
    }else{
        cvLine(cx-43,base-22,cx-42,base-178,9,0x535E5A,1);cvLine(cx+46,base-23,cx+46,base-179,9,0x2B3B36,1);
        cvLine(cx-43,base-176,cx+46,base-176,9,0x596B62,1);
        cvEllipse(cx-10,base-103,28,47,0x8D2630,1);cvEllipse(cx+15,base-112,26,40,0xB93B3D,1);
        cvLine(cx-23,base-147,cx-25,base-187,12,0x6A1B25,1);cvLine(cx+14,base-145,cx+28,base-183,9,0x732B30,1);
        for(int i=0;i<4;i++)cvLine(cx-36+i*22,base-65,cx-60+i*36,base-15,5,0x7D3333,.8f);
    }
    for(int i=0;i<9;i++)cvLine(cx-42+i*11,base-20,cx-46+i*11,base-29-i%4*9,1,0x9B5836,.5f);
    cvFinish(SpriteBakeTarget(id));
}

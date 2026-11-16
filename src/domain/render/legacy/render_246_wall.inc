/* Private legacy mode: sourced from v2.4.6 (569f2d4), with pitch fixed at zero.
 * Included beside its dispatcher to retain the original private renderer state.
 * Experimental changes belong in the other path, never in this reference. */
static void RenderWallSurfaceLegacy246(int x, int halfH, const WallRay* ray) {
    float rayDirX = ray->rayDirX;
    float rayDirY = ray->rayDirY;
    int mapX = ray->mapX;
    int mapY = ray->mapY;
    int side = ray->side;
    float perpWallDist = ray->perpWallDist;
    /* v1.13 真俯仰：墙的下沿(v=-眼高)与上沿(v=1-眼高)各自旋转后投影。
       同一列 z 不变 → 屏幕上仍是线性，贴图线性采样依然正确。 */
    int lineHeight = (int)(renderH / perpWallDist);
    (void)lineHeight;
    const float eyeH = 0.5f;
    float vb2 = (-eyeH) * 1.0f - perpWallDist * 0.0f;
    float db2 = (-eyeH) * 0.0f + perpWallDist * 1.0f;
    float vt2 = (1.0f - eyeH) * 1.0f - perpWallDist * 0.0f;
    float dt2 = (1.0f - eyeH) * 0.0f + perpWallDist * 1.0f;
    if (db2 < 0.05f) db2 = 0.05f;
    if (dt2 < 0.05f) dt2 = 0.05f;
    int yBottom = (int)((float)halfH - vb2 * (float)renderH / db2);
    int yTop    = (int)((float)halfH - vt2 * (float)renderH / dt2);
    if (yBottom <= yTop) return;
    int startF = yTop;
    int drawStart = yTop < 0 ? 0 : yTop;
    int drawEnd = yBottom > renderH ? renderH : yBottom;
    if (drawEnd <= drawStart) return;

    float wallX = (side == 0) ? (posY + perpWallDist * rayDirY) : (posX + perpWallDist * rayDirX);
    wallX -= floorf(wallX);
    int texX = (int)(wallX * (float)TEX_WIDTH);
    if (texX < 0) texX = 0;
    if (texX > TEX_WIDTH - 1) texX = TEX_WIDTH - 1;
    if ((side == 0 && rayDirX > 0) || (side == 1 && rayDirY < 0)) texX = TEX_WIDTH - texX - 1;

    float span = (float)(yBottom - yTop);
    if (span < 1.0f) span = 1.0f;
    float step = (float)TEX_HEIGHT / span;
    /* v2.0：每像素吃掉的 texel 数 → mip 级。step>1.5 说明在缩小，退到低一级 */
    int mipLod = 0;
    {
        float st = step;
        while (st > 1.5f && mipLod < MIP_LEVELS - 1) { st *= 0.5f; mipLod++; }
    }
    const unsigned int* mipSrc = mipChain[mipLod];
    int mipW = TEX_WIDTH >> mipLod;
    int mipMaskY = (TEX_HEIGHT >> mipLod) - 1;
    int mipShift = mipLod;
    float texPos = (float)(drawStart - startF) * step;

    /* v1.9 光照：
       ① 距离衰减改成 1/(1+d*k) 曲线 —— 近处亮、远处平滑收黑，没有硬断崖
       ② 距离雾：远处叠加一点冷色（青蓝），空间感立刻出来
       ③ 手电光斑用 smoothstep 软边，不再是硬切一刀 */
    /* v1.10：手电削弱。
       原来开灯只有 1/(1+d*0.22) 的衰减 —— 10 格外还亮着，等于整张地图都看得见。
       现在拆成两部分：
         环境光 amb：永远很暗（开灯 0.42 倍，关灯更暗）
         手电项   ：有效距离 8 格，按 (1-d/8)² 衰减，超出即 0
       所以光照范围由"手电照到哪"决定，不再是"整条走廊"。 */
    int lightOnW = (flashlightOn && battery > 0.0f);
    float amb = 1.0f / (1.0f + perpWallDist * (lightOnW ? 0.70f : 0.50f));
    /* v1.12：手电换成和荧光棒同一套光效 ——
       锥形（横向 flashTab）× 平方距离衰减，再叠一层暖色加色。
       原来只有"乘法提亮"，画面偏冷、也不像有个灯泡在照。 */
    float reach = 1.0f - perpWallDist / (9.0f*BenchLightRangeMul());
    if (reach < 0.0f) reach = 0.0f;
    reach *= reach;
    float flash = flashTab[x];
    float torch = lightOnW ? (flash * reach * 0.62f) : 0.0f;

    float lit = (amb * (lightOnW?.26f:.045f) + torch) * smokeDim;
    if (side == 1) lit *= 0.62f;

    float cone = flash - 0.35f;
    if (cone < 0.0f) cone = 0.0f;
    cone = cone * cone * (3.0f - 2.0f * cone > 1.0f ? 1.0f : 3.0f - 2.0f * cone);   /* smoothstep */

    /* 距离雾（冷色，加法叠加，零额外采样） */
    float fog = perpWallDist * 0.085f;
    if (fog > 0.85f) fog = 0.85f;
    if (!lightOnW) fog *= 0.04f;    /* 留一点，纯粹压黑会没有纵深 */

    /* 光斑也受有效距离约束：远处不许再加亮 */
    /* 光球照明：命中点离任一荧光棒越近越亮（暖色） */
    float hx = posX + rayDirX * perpWallDist;
    float hy = posY + rayDirY * perpWallDist;
    float fl = FlareLightAt(hx, hy);
    lit += fl;
    if (lit > 1.35f) lit = 1.35f;

    /* v1.12：手电改**白光**（略偏冷），和荧光棒的暖橙拉开差别 */
    int addR = (int)((cone * reach * (float)(themeLitR + 24) * 0.42f + fog * (float)themeAmbR
                      + fl * 210.0f + torch * (float)themeLitR) * smokeDim);
    int addG = (int)((cone * reach * (float)(themeLitG + 24) * 0.42f + fog * (float)themeAmbG
                      + fl * 150.0f + torch * (float)themeLitG) * smokeDim);
    int addB = (int)((cone * reach * (float)(themeLitB + 24) * 0.42f + fog * (float)themeAmbB
                      + fl * 78.0f  + torch * (float)themeLitB) * smokeDim);

    /* ==================================================================
     * 2026-11-06（v2.4-rc C4）：**主题房间的补光色**
     * ------------------------------------------------------------------
     * 计划里明确"不做第二套纹理集"（双倍纹理常驻内存），
     * 所以房间的辨识度靠**墙面补光**表现：
     *   同一张贴图，但这个房间的光偏冷/偏橙/偏紫。
     *
     * 成本：**每列一次**（不是每像素），普通区直接返回 0 跳过。
     * 这也是为什么不做地板 —— 地板是逐像素的，加查表会掉帧。
     * ================================================================ */
    {

        int tr = 0, tg = 0, tb = 0;
        if (CellTintRGB(mapX, mapY, &tr, &tg, &tb)) {
            float k = smokeDim;          /* 浓烟里补光同样被压 */
            addR += (int)(tr * k);
            addG += (int)(tg * k);
            addB += (int)(tb * k);
        }
    }
    /* ==================================================================
     * 2026-11-06（用户要求）：**房间外墙要有"装修过"的样子**
     * ------------------------------------------------------------------
     * 只有色调变化不够 —— 玩家还是分不出"这是房间"和"这是走廊"。
     * 这里取出房间外墙的标记与主题色，在下面的像素循环里画一条**腰线**：
     * 墙面 26%~34% 高度处一条横向色带（主题色偏亮），
     * 上下各留一格暗边。现实里那是瓷砖腰线 / 护墙板的分界。
     *
     * 每列算一次，不进像素循环 —— 循环里只多两次比较。
     * ================================================================ */
    int bandR = 0, bandG = 0, bandB = 0, hasBand = 0;
    int bandLo1 = 17, bandHi1 = 22, bandLo2 = -1, bandHi2 = -1;
    {


        int tr2 = 0, tg2 = 0, tb2 = 0;
        /* ==================================================================
         * 2026-11-06（用户实测）：**只有房间**内**侧该是主题色**
         * ------------------------------------------------------------------
         * 用户原话：「房间里面是蓝的，然后外墙什么都看不出来，跟普通墙差不多」，
         * 并且指出「有一部分墙体是双面墙体，里面是蓝色的，外面是迷宫的时候
         * 也可以看到里面是蓝色的」。
         *
         * 根因：房间外墙是**一格**，从房间内和从迷宫里看到的是同一格。
         * 现在按"观察者那一侧"判断：只有视点这一侧的邻格是房间内部
         * （roomId != 0）时才上主题色，外面看就是普通墙。
         * ================================================================ */
        int nbX = mapX, nbY = mapY;
        /* Use the caster's exact steps; do not infer a second sign convention. */
        int stepX = ray->stepX;
        int stepY = ray->stepY;
        if (side == 0) nbX -= stepX; else nbY -= stepY;
        int fromInsideRoom = 0;
        if (nbX >= 0 && nbY >= 0 && nbX < MAP_WIDTH && nbY < MAP_HEIGHT)
            fromInsideRoom = (cellMeta[nbY][nbX].roomId != 0);
        if (fromInsideRoom && CellIsRoomWall(mapX, mapY, &tr2, &tg2, &tb2)) {
            /* ==================================================================
             * 2026-11-06（用户要求）：**七种主题不能长一个样**
             * ------------------------------------------------------------------
             * 用户原话：「大多数都长得差不多……每一个特色也不是很明确，
             *           里面的设计审美概念都不是很明确」。
             *
             * 之前所有主题共用同一条腰线（vy 17..22）。现在按主题给
             * **不同的腰线剖面**：位置、宽度、条数都不同，
             * 于是站在房间里一眼能分清这是哪种空间。
             *
             * vy 是 0..63 的墙面竖直坐标（0 = 天花，63 = 地面）：
             *   砖墙   踢脚线：低、窄           (54..58)
             *   监狱   两道横栏：中上 + 中下    (14..17, 34..37)
             *   地狱   顶部压边：高、厚         (6..14)
             *   病房   腰线 + 踢脚：双细线      (28..31, 56..58)
             *   冰窖   细腰线：极高             (20..23)
             *   锈工厂 粗腰带：中、最宽         (26..36)
             *   血肉   无腰线，改成底部渐暗     (56..63)
             * ================================================================ */

            int th = (int)cellMeta[mapY][mapX].themeId;
            if (th < 0 || th >= THEME_COUNT) th = 0;
            bandLo1 = 54; bandHi1 = 58; bandLo2 = -1; bandHi2 = -1;
            switch (th) {
            case 0: bandLo1 = 54; bandHi1 = 58; break;            /* 砖墙：踢脚 */
            case 1: bandLo1 = 14; bandHi1 = 17;                   /* 监狱：双横栏 */
                    bandLo2 = 34; bandHi2 = 37; break;
            case 2: bandLo1 = 6;  bandHi1 = 14; break;            /* 地狱：顶部压边 */
            case 3: bandLo1 = 28; bandHi1 = 31;                   /* 病房：双细线 */
                    bandLo2 = 56; bandHi2 = 58; break;
            case 4: bandLo1 = 20; bandHi1 = 23; break;            /* 冰窖：高细线 */
            case 5: bandLo1 = 26; bandHi1 = 36; break;            /* 锈工厂：宽腰带 */
            case 6: bandLo1 = 56; bandHi1 = 63; break;            /* 血肉：底部压暗 */
            default: break;
            }
            /* 腰线色 = 主题色 × 3.2，并给一个下限，暗主题也能看出来 */
            bandR = tr2 * 26 / 10; if (bandR < 26) bandR = 26;
            bandG = tg2 * 26 / 10; if (bandG < 22) bandG = 22;
            bandB = tb2 * 26 / 10; if (bandB < 22) bandB = 22;
            hasBand = 1;
        }
    }

    int k8 = (int)(lit * 256.0f);
    if (k8 < 0) k8 = 0;
    else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
    k8 = shadeLut[k8];      /* gamma 校正，和地板共用同一张表 */

    unsigned int* dst = pixels + (size_t)drawStart * renderW + x;

    /* v1.9：墙脚 / 墙顶 AO —— 沿墙面上下各压暗 16 像素，
       墙角有了"厚度"，不再是均匀一片贴图 */
    int aoBot = drawEnd - 16;
    int aoTop = drawStart + 16;

    /* v2.1：竖直方向的光斑。
       原来亮度只按 x 算（flashTab[x]），同一列从上到下完全一样，
       整面墙就是一块均匀灰板 —— 贴纸感的真正来源。
       这里预生成 64 项竖直权重（中间亮、上下收），每像素多一次查表。 */
    int vLut[64];
    {
        for (int i = 0; i < 64; i++) {
            float v = ((float)i + 0.5f) / 64.0f;
            float dv = v - 0.46f;                    /* 略偏上：视线落点在下半 */
            float fall = 1.0f - dv * dv * 3.2f;      /* 中间 1，两端 <0 */
            if (fall < 0.18f) fall = 0.18f;
            vLut[i] = (int)(fall * 256.0f);
        }
    }

    for (int y = drawStart; y < drawEnd; y++) {
        int texY = ((int)texPos) & (TEX_HEIGHT - 1);
        texPos += step;
        /* v2.0：走了 mip 就按低位采样（lod 0 时与原路径完全等价） */
        unsigned int t = mipSrc[((texY >> mipShift) & mipMaskY) * mipW + (texX >> mipShift)];
        int vy = (int)(((float)(y - drawStart) * 64.0f) / (float)(drawEnd - drawStart + 1));
        if (vy < 0) vy = 0;
        if (vy > 63) vy = 63;
        int k8v = (k8 * vLut[vy]) >> 8;              /* 竖直光斑 */
        k8v += bump[texY][texX];                     /* v2.0：凹凸 */
        if (k8v < 0) k8v = 0;
        if (k8v > 1023) k8v = 1023;
        int r = (int)(((t >> 16) & 0xFF) * k8v) >> 8;
        int g = (int)(((t >> 8) & 0xFF) * k8v) >> 8;
        int b = (int)((t & 0xFF) * k8v) >> 8;

        r += addR; g += addG; b += addB;

        /* 房间腰线：vy 0..63 的竖直坐标，26%~34% 处一条亮带，
           紧贴它上下各一格暗边 —— 有明暗对比才读得出"这是装饰" */
        if (hasBand) {
            int inBand = (vy >= bandLo1 && vy <= bandHi1);
            if (bandLo2 >= 0 && vy >= bandLo2 && vy <= bandHi2) inBand = 1;
            if (inBand) {
                r += bandR; g += bandG; b += bandB;
            } else if (vy == bandLo1 - 1 || vy == bandHi1 + 1) {
                r -= 18; g -= 18; b -= 18;
                if (r < 0) r = 0;
                if (g < 0) g = 0;
                if (b < 0) b = 0;
            }
        }

        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        *dst = (unsigned int)((r << 16) | (g << 8) | b);
        dst += renderW;
    }

    /* v1.9：AO 从逐像素判断里挪出来，改成两段小循环。
       热点循环里少两次比较 + 三次乘法，而这两段只有 32 行。 */
    {
        int yb0 = aoBot > drawStart ? aoBot : drawStart;
        unsigned int* p = pixels + (size_t)yb0 * renderW + x;
        for (int y = yb0; y < drawEnd; y++, p += renderW) {
            int f = 256 - (y - aoBot) * 13;
            if (f < 116) f = 116;
            unsigned int c = *p;
            int rr = (int)((c >> 16) & 0xFF) * f >> 8;
            int gg = (int)((c >> 8) & 0xFF) * f >> 8;
            int bb = (int)(c & 0xFF) * f >> 8;
            *p = (unsigned int)((rr << 16) | (gg << 8) | bb);
        }
        int yet = aoTop < drawEnd ? aoTop : drawEnd;
        p = pixels + (size_t)drawStart * renderW + x;
        for (int y = drawStart; y < yet; y++, p += renderW) {
            int f = 256 - (aoTop - y) * 11;
            if (f < 140) f = 140;
            unsigned int c = *p;
            int rr = (int)((c >> 16) & 0xFF) * f >> 8;
            int gg = (int)((c >> 8) & 0xFF) * f >> 8;
            int bb = (int)(c & 0xFF) * f >> 8;
            *p = (unsigned int)((rr << 16) | (gg << 8) | bb);
        }
    }
}

/* Private legacy mode: sourced from v2.4.6 (569f2d4), with pitch fixed at zero.
 * Included beside its dispatcher to retain the original private renderer state.
 * Experimental changes belong in the other path, never in this reference. */
static void RenderWallSurfaceLegacy246(int x, int halfH, const WallRay* ray) {
    float rayDirX = ray->rayDirX;
    float rayDirY = ray->rayDirY;
    int mapX = ray->mapX;
    int mapY = ray->mapY;
    int architectureMaterial=-1;
    if(!storyMaterialActive && mapX>=0 && mapY>=0 && mapX<MAP_WIDTH && mapY<MAP_HEIGHT &&
       (cellMeta[mapY][mapX].flags&(CELLFLAG_ROOM_V2|CELLFLAG_ROOMWALL)))
        architectureMaterial=SceneMaterialAt(mapX,mapY);
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
        if (!storyMaterialActive && architectureMaterial<0 && CellTintRGB(mapX, mapY, &tr, &tg, &tb)) {
            float k = smokeDim;          /* 浓烟里补光同样被压 */
            addR += (int)(tr * k);
            addG += (int)(tg * k);
            addB += (int)(tb * k);
        }
    }

    /* Room walls use the floor material; no screen-relative decorative bands. */
    int k8 = (int)(lit * 256.0f);
    if (k8 < 0) k8 = 0;
    else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
    k8 = shadeLut[k8];      /* gamma 校正，和地板共用同一张表 */

    unsigned int* dst = pixels + (size_t)drawStart * renderW + x;

    /* v1.9：墙脚 / 墙顶 AO —— 沿墙面上下各压暗 16 像素，
       墙角有了"厚度"，不再是均匀一片贴图 */
    int aoBot = (architectureMaterial>=0?yBottom:drawEnd) - 16;
    int aoTop = (architectureMaterial>=0?yTop:drawStart) + 16;

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
        unsigned int t = architectureMaterial>=0
            ?sceneTexture[architectureMaterial][0][texY&~1][texX&~1]
            :mipSrc[((texY >> mipShift) & mipMaskY) * mipW + (texX >> mipShift)];
        int vy = (int)(((float)(y - drawStart) * 64.0f) / (float)(drawEnd - drawStart + 1));
        /* The current room's lighting is attached to the actual wall, not
           the clipped viewport span: walking closer cannot stretch the wash. */
        if(architectureMaterial>=0)vy=texY*64/TEX_HEIGHT;
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
        if(yb0>drawEnd)yb0=drawEnd;
        unsigned int* p = pixels + (size_t)(yb0<drawEnd?yb0:drawStart) * renderW + x;
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

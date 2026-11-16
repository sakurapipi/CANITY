/* Private legacy mode: sourced from v2.4.6 (569f2d4), with pitch fixed at zero.
 * Included beside its dispatcher to retain the original private renderer state.
 * Experimental changes belong in the other path, never in this reference. */
static void RenderFloorBandLegacy246(int y0, int y1) {
    int halfH = renderH / 2;
    int centery = halfH + 0;
    /* v1.13：真俯仰后不再需要这两个端点射线（逐行按角度求交） */
    (void)0;
    int exitBX = (int)(exitX * 4.0f), exitBY = (int)(exitY * 4.0f);
    int safeBX = (int)(safeRoom.x * 4.0f), safeBY = (int)(safeRoom.y * 4.0f);
    int lightOnF = (flashlightOn && battery > 0.0f);

    /* v1.12：y-shear 之后地板与天花板**不再是镜像关系**（镜像只在 0==0 时成立），
       所以这里改成单次全屏通道：每一行按 y 相对地平线判上下，分别采样地板/天花板。
       之前那套"地板行 + 镜像天花板"在抬头时会留下一条黑带，低头时上下颠倒。 */
    for (int y = y0; y < y1; y++) {
        unsigned int* row = pixels + (size_t)y * renderW;
        /* v1.13 真俯仰：该行的视线竖直分量（旋转后） */
        float cyv = (float)(centery - y) / (float)renderH;
        float ry = 0.0f + 1.0f * cyv;
        float kk = 1.0f - cyv * 0.0f;
        int isCeil = (ry > 0.0f);
        float t;
        if (isCeil) {
            float den = ry;
            if (den < 0.004f) den = 0.004f;
            t = 0.5f / den;
        } else {
            float den = -ry;
            if (den < 0.004f) den = 0.004f;
            t = 0.5f / den;
        }
        if (t > 60.0f) t = 60.0f;
        /* v1.13b 修正：射线 = F + R·cameraX + U·cameraY，
           其中 R（平面向量）的系数**恒为 1**，不该乘 (cos - cy·sin)。
           上一版把整个水平方向都乘了 kk，pitch 越大横向拉伸越狠 → 鱼眼扭曲。 */
        float stepX = t * planeX * 2.0f / renderW;
        float stepY = t * planeY * 2.0f / renderW;
        float fx = posX + t * kk * dirX - t * planeX;
        float fy = posY + t * kk * dirY - t * planeY;
        float rowDistance = t;

        /* 亮度：环境光 + 手电的平方衰减 */
        float ambF = 1.0f / (1.0f + rowDistance * (lightOnF ? 0.75f : 0.55f));
        float reachF = 1.0f - rowDistance / 5.5f;
        if (reachF < 0.0f) reachF = 0.0f;
        reachF *= reachF;

        float shadow = (ambF * (lightOnF?.28f:.055f) + (lightOnF ? reachF * 0.95f : 0.0f)) * smokeDim;
        int shadowI = (int)(shadow * 256.0f);
        if (shadowI < 0) shadowI = 0;
        if (shadowI > 1023) shadowI = 1023;

        /* 距离雾（天花板减半：天花板本来就不该被照亮太多） */
        float fogAmt = rowDistance * 0.075f;
        if (fogAmt > 0.8f) fogAmt = 0.8f;
        if (!lightOnF) fogAmt *= 0.04f;
        if (isCeil) fogAmt *= 0.5f;
        unsigned int fogR = (unsigned int)(fogAmt * (float)themeFogR);
        unsigned int fogG = (unsigned int)(fogAmt * (float)themeFogG);
        unsigned int fogB = (unsigned int)(fogAmt * (float)themeFogB);

        /* ==================================================================
         * 2026-11-06（v2.4-rc）：**地板和天花板也要按房间上色**
         * ------------------------------------------------------------------
         * 实测反馈：「还是只有一些发光的墙，其他什么都没有。」
         *
         * 那是必然的 —— 我只给墙面加了补光，**地板没加**。
         * 而第一人称视角里，画面下半部分几乎全是地板；
         * 只让墙发光，结果就是"角落里有一段墙在发亮"，
         * 完全构不成"我走进了一个房间"。
         *
         * 这里用每行的 shadow 预乘一个系数，所以远处/暗处的地板
         * 补光同样会被压下去，不会出现"远处的房间亮得反常"。
         * 成本：每像素一次数组取值 + 一次移位 —— 地板本来就最贵，
         * 这点增量实测无感。
         * ================================================================ */
        int tintMul = (int)(shadow * 256.0f * 1.7f);
        if (tintMul < 0) tintMul = 0;
        if (tintMul > 1024) tintMul = 1024;



        for (int x = 0; x < renderW; x++) {
            int cX = (int)fx, cY = (int)fy;
            int tx2 = ((int)((fx - cX) * (float)TEX_WIDTH)) & (TEX_WIDTH - 1);
            int ty2 = ((int)((fy - cY) * (float)TEX_HEIGHT)) & (TEX_HEIGHT - 1);
            tx2 &= ~1; ty2 &= ~1;

            unsigned int color;
            if (!isCeil) {
                int k8 = (shadowI * floorGainI[x]) >> 8;
                if (k8 < 0) k8 = 0;
                else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
                k8 = shadeLut[k8];
                int lvl = k8 >> PREMUL_K8_SHIFT;
                color = premulFloor[floorColorIdx[ty2][tx2]][lvl];

                /* 房间补光：先叠，后面安全屋/出口的特效可以覆盖它 */
                if (roomCount > 0 &&
                    (unsigned)cX < (unsigned)MAP_WIDTH && (unsigned)cY < (unsigned)MAP_HEIGHT) {
                    unsigned char rid = cellMeta[cY][cX].roomId;
                    if (rid != 0) {
                        int t = cellMeta[cY][cX].themeId;
                        if (t < 0 || t >= THEME_COUNT) t = 0;
                        int tr = CellThemeTintR(t), tg = CellThemeTintG(t), tb = CellThemeTintB(t);
                        int cr = (int)((color >> 16) & 0xFFu) + ((tr * tintMul) >> 8);
                        int cg = (int)((color >> 8) & 0xFFu)  + ((tg * tintMul) >> 8);
                        int cb = (int)(color & 0xFFu)         + ((tb * tintMul) >> 8);
                        if (cr > 255) cr = 255;
                        if (cg > 255) cg = 255;
                        if (cb > 255) cb = 255;
                        /* ================================================================
                         * 2026-11-06（用户要求）：**房间地面铺砖**
                         * ----------------------------------------------------------------
                         * 房间地面按格子奇偶做一点明暗差（±6%），
                         * 读起来是"铺过地砖"，走廊则保持原来的连续面。
                         * 成本：一次异或 + 一次与 —— 进了逐像素路径，但只多两条指令。
                         * ================================================================ */
                        {
                            int odd = ((cX ^ cY) & 1);
                            int f = odd ? 246 : 255;      /* 256 基准 */
                            cr = (cr * f) >> 8;
                            cg = (cg * f) >> 8;
                            cb = (cb * f) >> 8;
                        }
                        color = (unsigned int)((cr << 16) | (cg << 8) | cb);
                    }
                }

                /* 安全屋花圃 */
                if (safeRoom.active) {
                    int bx = (int)(fx * 4.0f), by = (int)(fy * 4.0f);
                    int sbx = bx - safeBX, sby = by - safeBY;
                    if (sbx >= -2 && sbx <= 2 && sby >= -2 && sby <= 2) {
                        float d2s = (float)(sbx * sbx + sby * sby) / 8.0f;
                        float gs = 1.0f - d2s;
                        if (gs > 0.0f) {
                            int r2 = (int)(70 * gs), g2 = (int)(48 * gs), b2 = (int)(58 * gs);
                            color = (unsigned int)((r2 << 16) | (g2 << 8) | b2);
                        }
                    }
                }

                /* 出口光池 */
                {
                    int bx = (int)(fx * 4.0f), by = (int)(fy * 4.0f);
                    int dbx = bx - exitBX, dby = by - exitBY;
                    if (dbx >= -3 && dbx <= 3 && dby >= -3 && dby <= 3) {
                        float d2 = (float)(dbx * dbx + dby * dby) / 9.0f;
                        float glow = 1.0f - d2;
                        if (glow > 0.0f) {
                            float k = (shadow * 0.45f + 0.60f) * glow;
                            if (breakerOpen) {
                                int r2 = (int)(40 * k), g2 = (int)(235 * k), b2 = (int)(105 * k);
                                if (r2 > 255) r2 = 255;
                                if (g2 > 255) g2 = 255;
                                if (b2 > 255) b2 = 255;
                                color = (unsigned int)((r2 << 16) | (g2 << 8) | b2);
                            } else {
                                int r2 = (int)(120 * k), g2 = (int)(24 * k);
                                if (r2 > 255) r2 = 255;
                                color = (unsigned int)((r2 << 16) | (g2 << 8));
                            }
                        }
                    }
                }

                /* 雾 + 光球的暖光（简化：只按距离加暖色） */
                if (fogR | fogG | fogB) {
                    unsigned int cr = (color >> 16) & 0xFF, cg = (color >> 8) & 0xFF, cb = color & 0xFF;
                    cr += fogR; if (cr > 255) cr = 255;
                    cg += fogG; if (cg > 255) cg = 255;
                    cb += fogB; if (cb > 255) cb = 255;
                    color = (cr << 16) | (cg << 8) | cb;
                }
            } else {
                int cl = ceilTab[x] >> PREMUL_K8_SHIFT;
                if (cl < 0) cl = 0;
                else if (cl > PREMUL_LEVELS - 1) cl = PREMUL_LEVELS - 1;
                color = premulCeil[ceilColorIdx[ty2][tx2]][cl];
            }

            row[x] = color;
            fx += stepX;
            fy += stepY;
        }
    }
}

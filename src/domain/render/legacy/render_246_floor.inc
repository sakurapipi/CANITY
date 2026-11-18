/* Private legacy mode: sourced from v2.4.6 (569f2d4), with pitch fixed at zero.
 * Included beside its dispatcher to retain the original private renderer state.
 * Experimental changes belong in the other path, never in this reference. */
static void RenderFloorBandLegacy246(int y0, int y1) {
    int halfH = renderH / 2;
    int centery = halfH + 0;
    /* v1.13：真俯仰后不再需要这两个端点射线（逐行按角度求交） */
    (void)0;
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

        for (int x = 0; x < renderW; x++) {
            int cX = (int)fx, cY = (int)fy;
            int tx2 = ((int)((fx - cX) * (float)TEX_WIDTH)) & (TEX_WIDTH - 1);
            int ty2 = ((int)((fy - cY) * (float)TEX_HEIGHT)) & (TEX_HEIGHT - 1);
            tx2 &= ~1; ty2 &= ~1;

            int material=SceneMaterialAt(cX,cY);
            unsigned int color;
            if (!isCeil) {
                int k8 = (shadowI * floorGainI[x]) >> 8;
                if (k8 < 0) k8 = 0;
                else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
                k8 = shadeLut[k8];
                int lvl = k8 >> PREMUL_K8_SHIFT;
                color = SurfaceTexel(material,1,tx2,ty2,lvl);

                if(safeRoom.active)color=SafetyFloorMarker(color,fx,fy,safeRoom.x,safeRoom.y,1);
                if (!storyMaterialActive) color=SafetyFloorMarker(color,fx,fy,exitX,exitY,breakerOpen);

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
                color = SurfaceTexel(material,2,tx2,ty2,cl);
            }

            row[x] = color;
            fx += stepX;
            fy += stepY;
        }
    }
}

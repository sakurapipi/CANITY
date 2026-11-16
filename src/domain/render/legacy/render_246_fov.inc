/* Private legacy mode: sourced from v2.4.6 (569f2d4), with pitch fixed at zero.
 * Included beside its dispatcher to retain the original private renderer state.
 * Experimental changes belong in the other path, never in this reference. */
static void ApplyFovLegacy246(void) {
    float L = fovPlaneLen[fovIndex];
    float blind = FxStrength(FX_BLIND);
    if (blind > 0.0f) {
        /* 与最窄档（66 度，fovPlaneLen[0]）线性插值 */
        float narrow = fovPlaneLen[0];
        L = narrow + (L - narrow) * (1.0f - blind);
    }
    /* v1.9：像素必须是方的。
       射线投射里 屏幕像素宽/高的比例 = (renderW/2)/L : renderH ，
       要保持 1:1 就得 L = (renderW/renderH)/2。
       原来的 L 是按 4:3 调好的，所以这里乘上「实际宽高比 / 4:3」即可 ——
       效果是宽屏上**水平视野变宽**（Hor+），而不是把画面拉扁。 */
    {
        float aspect = (renderH > 0) ? (float)renderW / (float)renderH : 4.0f / 3.0f;
        L *= aspect / (4.0f / 3.0f);
    }
    planeX = -dirY * L;
    planeY = dirX * L;
}

/* Private legacy mode: sourced from v2.4.6 (569f2d4), with pitch fixed at zero.
 * Included beside its dispatcher to retain the original private renderer state.
 * Experimental changes belong in the other path, never in this reference. */
static SpriteBox ProjectSpriteLegacy246(float wx, float wy, float baseZ, float halfH) {
    SpriteBox s = {0};
    float dx = wx - posX, dy = wy - posY;
    float invDet = 1.0f / (planeX * dirY - dirX * planeY);
    float tx = invDet * (dirY * dx - dirX * dy);
    float ty = invDet * (-planeY * dx + planeX * dy);
    if (ty <= 0.02f) return s;   /* v1.10：太近就丢弃的阈值放宽，避免出手瞬间消失 */

    /* v1.13 真俯仰：地面点的竖直偏移旋转后再投影 */
    float v2 = (-0.5f) * 1.0f - ty * 0.0f;
    float d2 = (-0.5f) * 0.0f + ty * 1.0f;
    if (d2 < 0.05f) return s;
    float unit = (float)renderH / d2;
    int cx = (int)((renderW / 2.0f) * (1.0f + tx / ty));
    int half = (int)(unit * 0.28f);
    int cyFloor = renderH / 2 - (int)(v2 * unit);
    /* P1：底边按 baseZ 抬高（baseZ=0 时与原实现完全一致） */
    int cyBase = cyFloor - (int)(unit * baseZ);
    int hgt = (int)(unit * halfH);
    int yTop = cyBase - hgt;

    s.ty = ty;
    s.x0 = cx - half; if (s.x0 < 0) s.x0 = 0;
    s.x1 = cx + half; if (s.x1 >= renderW) s.x1 = renderW - 1;
    s.y0 = yTop; if (s.y0 < 0) s.y0 = 0;
    s.y1 = cyBase; if (s.y1 >= renderH) s.y1 = renderH - 1;
    s.ok = (s.x1 > s.x0 && s.y1 > s.y0);
    /* Adapt the extended result without changing legacy geometry. */
    s.centerX=(s.x0+s.x1)/2;s.baseY=s.y1;
    s.width=s.x1-s.x0;s.height=s.y1-s.y0;
    return s;
}

/* CANITY — 区域属性表实现（v2.4-rc C2）
 *
 * 全部是查表与夹取，没有分配、没有随机、没有 IO。
 * 因此它既能被 game.c 在生成时写，也能被 render.c 在每列读，
 * 还能被 test 直接造数据验证。
 */
#include "common.h"
#include "cell_meta.h"
#include "room_template.h"

CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];
int roomCount = 0;

void CellMetaReset(void) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) {
            cellMeta[y][x].roomId = 0;
            cellMeta[y][x].themeId = 0;
            cellMeta[y][x].acousticId = ACOUSTIC_DEFAULT;
            cellMeta[y][x].flags = 0;
        }
    roomCount = 0;
}

void CellMetaFillRoom(int x0, int y0, int w, int h,
                      int roomId, int themeId, int acousticId, unsigned flags) {
    if (roomId < 0) roomId = 0;
    if (roomId > 255) roomId = 255;
    if (themeId < 0) themeId = 0;
    if (themeId >= THEME_COUNT) themeId = THEME_COUNT - 1;

    for (int y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= MAP_HEIGHT) continue;
        for (int x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= MAP_WIDTH) continue;
            cellMeta[y][x].roomId = (unsigned char)roomId;
            cellMeta[y][x].themeId = (unsigned char)themeId;
            cellMeta[y][x].acousticId = (unsigned char)acousticId;
            cellMeta[y][x].flags = (unsigned char)flags;
        }
    }
    if (roomId > roomCount) roomCount = roomId;
}

const CellMeta* CellMetaAt(int cx, int cy) {
    static const CellMeta kDefault = { 0, 0, ACOUSTIC_DEFAULT, 0 };
    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return &kDefault;
    return &cellMeta[cy][cx];
}

/* ==================================================================
 * 墙面 tint
 * ------------------------------------------------------------------
 * 返回 0 表示"不染色"，render 直接走原路径。
 * 普通区（roomId == 0）**恒为 0** —— 这是让"没有房间的层"零成本的关键。
 *
 * 强度取 96/255 ≈ 38%：再高就把原主题的纹理细节洗掉了。
 * ================================================================ */
unsigned char CellTintFor(int cx, int cy) {
    const CellMeta* m = CellMetaAt(cx, cy);
    if (!m || m->roomId == 0) return 0;
    return 96;
}

/* 每个主题给一个"补光色"。数值是**加色量**（0..255 通道），
   直接叠在墙面的光照项上 —— 所以它读起来像"这个房间的光不一样"，
   而不是"贴图被染色了"。幅度取 20~34：再高会把原纹理的细节洗掉。 */
static const unsigned char kRoomTintRGB[THEME_COUNT][3] = {
    { 20, 18, 20 },   /* 0 砖墙     中性偏灰 */
    { 16, 22, 30 },   /* 1 监狱     冷蓝金属 */
    { 34, 14, 12 },   /* 2 地狱     暗红 */
    { 18, 28, 22 },   /* 3 病房     病态绿 */
    { 12, 24, 34 },   /* 4 冰窖     青蓝 */
    { 34, 24, 12 },   /* 5 锈工厂   橙锈 */
    { 32, 12, 24 }    /* 6 血肉洞窟 紫红 */
};

int CellTintRGB(int cx, int cy, int* r, int* g, int* b) {
    const CellMeta* m = CellMetaAt(cx, cy);
    if (!m || m->roomId == 0) return 0;
    if (r || g || b) {
        int t = (int)m->themeId;
        if (t < 0 || t >= THEME_COUNT) t = 0;
        if (r) *r = kRoomTintRGB[t][0];
        if (g) *g = kRoomTintRGB[t][1];
        if (b) *b = kRoomTintRGB[t][2];
    }
    return 1;
}

int CellAcousticAt(float x, float y) {
    int cx = (int)floorf(x);
    int cy = (int)floorf(y);
    const CellMeta* m = CellMetaAt(cx, cy);
    return m->acousticId;
}

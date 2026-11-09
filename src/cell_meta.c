/* CANITY — 区域属性表实现（v2.4-rc C2）
 *
 * 全部是查表与夹取，没有分配、没有随机、没有 IO。
 * 因此它既能被 game.c 在生成时写，也能被 render.c 在每列读，
 * 还能被 test 直接造数据验证。
 */
#include "common.h"
#include "room_theme.h"
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
    { 12, 26, 36 },   /* 0 砖墙     中性偏灰 */
    { 16, 22, 30 },   /* 1 监狱     冷蓝金属 */
    { 10, 28, 30 },   /* 2 地狱     暗红 */
    { 16, 30, 34 },   /* 3 病房     病态绿 */
    { 12, 24, 34 },   /* 4 冰窖     青蓝 */
    { 22, 20, 40 },   /* 5 锈工厂   橙锈 */
    { 32, 12, 24 }    /* 6 血肉洞窟 紫红 */
};

/* ==================================================================
 * 2026-11-06（用户要求）：**主题房间的显示名**
 * ------------------------------------------------------------------
 * 用户：「右上角新增一行字幕，告诉我当前的主题房间是什么」。
 *
 * 名字和 kRoomTintRGB 的注释一一对应 —— 以后加主题时两边一起改。
 * ================================================================ */
const char* ThemeName(int themeId,int zh) {return RoomThemeName(themeId,zh);}


/* 逐像素路径上不能走函数调用（地板循环每帧几十万次），
   所以把主题色拆成三个内联查询，让 render 直接取。 */
int CellThemeTintR(int themeId) {
    if (themeId < 0 || themeId >= THEME_COUNT) themeId = 0;
    return kRoomTintRGB[themeId][0];
}
int CellThemeTintG(int themeId) {
    if (themeId < 0 || themeId >= THEME_COUNT) themeId = 0;
    return kRoomTintRGB[themeId][1];
}
int CellThemeTintB(int themeId) {
    if (themeId < 0 || themeId >= THEME_COUNT) themeId = 0;
    return kRoomTintRGB[themeId][2];
}

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


/* ==================================================================
 * 2026-11-06：这个格子是不是**房间外墙**？是就带回它的主题色。
 * ------------------------------------------------------------------
 * 为什么不复用 CellTintRGB：那个判的是 roomId != 0（房间**内部**），
 * 而墙的 roomId 必须是 0。房间外墙靠 CELLFLAG_ROOMWALL 单独标。
 * ================================================================ */
int CellIsRoomWall(int cx, int cy, int* r, int* g, int* b) {
    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return 0;
    const CellMeta* m = &cellMeta[cy][cx];
    if (!(m->flags & CELLFLAG_ROOMWALL)) return 0;
    int t = (int)m->themeId;
    if (r) *r = CellThemeTintR(t);
    if (g) *g = CellThemeTintG(t);
    if (b) *b = CellThemeTintB(t);
    return 1;
}

#ifndef CANITY_SPRITES_H
#define CANITY_SPRITES_H

/* ------------------------------------------------------------------ */
/* v2.1 烘焙精灵管线                                                    */
/*                                                                     */
/* 为什么要有这一层：这个项目原来所有"模型"都是每帧用矩形/椭圆现拼出来   */
/* 的，形状少、没有抗锯齿、没有动画帧。正统做法（Doom / Build /         */
/* Wolfenstein）是：美术资源**离线烘成位图**，运行时只做"查表 + 拷像素"。 */
/* 这里把"离线"放到启动时（约 40~80ms，一次性付清）。                   */
/*                                                                     */
/* 画布用 2× 超采样，降采样时自然得到抗锯齿边缘。                       */
/* ------------------------------------------------------------------ */

#define SPR_W 128
#define SPR_H 128
#define SPR_SHADES 24      /* 亮度档数，blit 时查表，不做逐像素浮点乘 */

typedef struct {
    int w, h;
    unsigned int* px;          /* 0xAARRGGBB，A=0 表示透明 */
    unsigned int* shade;       /* [SPR_SHADES] 预乘亮度后的 RGB */
} Sprite;

/* 精灵 id */
enum {
    SPR_WRENCH = 0,
    SPR_PISTOL,
    SPR_SIGIL,
    /* 投掷物（左手） */
    SPR_ITEM_FLARE, SPR_ITEM_BOTTLE, SPR_ITEM_BRICK, SPR_ITEM_ACID,
    SPR_ITEM_SMOKE, SPR_ITEM_FIRE, SPR_ITEM_GLUE, SPR_ITEM_MIRROR,
    /* 怪物：三只主怪各 4 帧 = 正面/正面走/侧面/侧面走；
       其余各 1 帧。朝向由 DrawBakedMonster 按玩家与怪的夹角选，
       侧面靠水平镜像覆盖另一侧（Doom 也是这么省美术的）。 */
    SPR_MON_HUNTER,  SPR_MON_HUNTER_W,  SPR_MON_HUNTER_S,  SPR_MON_HUNTER_SW,
    SPR_MON_LIGHT,   SPR_MON_LIGHT_W,   SPR_MON_LIGHT_S,   SPR_MON_LIGHT_SW,
    SPR_MON_LEECH,   SPR_MON_LEECH_W,   SPR_MON_LEECH_S,   SPR_MON_LEECH_SW,
    SPR_MON_JUMPER, SPR_MON_SHADE, SPR_MON_SPIDER, SPR_MON_ECHO,
    /* 道具 */
    SPR_PROP_CRATE, SPR_PROP_BREAKER, SPR_PROP_EXIT, SPR_PROP_SAFE,
    SPR_PROP_CRATE_OPEN,   /* 已翻过的空箱（盖子掀开） */
    SPR_PROP_CRATE_LOCKED, /* 未解锁：正面挂金锁 */
    SPR_PROP_TRASH,        /* 藏身点：垃圾桶 */
    /* UI（v2.2） */
    SPR_UI_PANEL,      /* 9-slice 铆接金属板 */
    SPR_UI_PANEL_HOT,  /* 同上，聚焦态（边框泛琥珀） */
    SPR_UI_CURSOR,     /* 三角选择光标 */
    SPR_UI_ROW,        /* 行底纹（选中） */
    SPR_COUNT
};

void   SpritesInit(void);                       /* 启动时烘一次 */
Sprite* SpriteGet(int id);
int    SpritesReady(void);

/* 运行时绘制：双线性采样 + 亮度查表 + alpha + 可选深度测试 */
void DrawSprite(int id, int x, int y, int w, int h,
                float light, float alpha, int zTest, float zDist);

/* 带色调偏移的版本（Boss 换色、受击闪白） */
void DrawSpriteTint(int id, int x, int y, int w, int h,
                    float light, float alpha, int zTest, float zDist,
                    unsigned int tint, float tintMix);

/* 子矩形 blit：9-slice 与图标裁切用（源矩形必须是精灵内部坐标） */
void DrawSpriteSub(int id, int sx, int sy, int sw, int sh,
                   int dx, int dy, int dw, int dh, float light, float alpha);

/* 水平镜像 blit（怪物侧面靠它覆盖另一侧，Doom 的做法） */
void DrawSpriteFlip(int id, int x, int y, int w, int h,
                    float light, float alpha, int zTest, float zDist);

#endif

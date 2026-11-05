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
    /* ==================================================================
     * 2026-11-06（v2.4-rc P1）：**内容包围盒**
     * ------------------------------------------------------------------
     * 画布是 128x128，但每张图真正的像素只占其中一块 ——
     * 之前 DrawBakedProp 一律按正方形绘制，于是窄高的货架被横向拉宽、
     * 矮扁的控制台被竖向拉高，而且底部留白让它看起来悬空。
     *
     * 这里记下"真正有像素的矩形"，绘制时按它的宽高比还原，
     * 既不再变形，也不再有底部悬空。
     * ================================================================== */
    int bx0, by0, bx1, by1;
} Sprite;

/* 精灵 id */
enum {
    SPR_WRENCH = 0,     /* 2026-11-05：唯一的工具。原来的 SPR_PISTOL（信号枪）
                           与 SPR_SIGIL（符印发射器）已删除 —— 那两个武器系统
                           在 NO_COMBAT 时就下线了，模型却还留着，
                           每日/战役的随机武器会把它画到屏幕上。 */
    /* 投掷物（左手）—— v2.4.82 按**槽位**命名，和 ITEM_SLOT_* 一一对应。
       旧的名字（BOTTLE/BRICK/ACID/FIRE/MIRROR）是已删除的投掷物，
       留着只会让人以为还有那几种东西。 */
    SPR_ITEM_FLARE,     /* 0 光球 */
    SPR_ITEM_SLING,     /* 1 投石（石弹） */
    SPR_ITEM_SMOKE,     /* 2 烟雾 */
    SPR_ITEM_GLUE,      /* 3 胶水 */
    SPR_ITEM_ECHO,      /* 4 回声（音叉） */
    SPR_ITEM_DECOY,     /* 5 玩偶 */
    SPR_ITEM__COUNT,
    /* 怪物：三只主怪各 4 帧 = 正面/正面走/侧面/侧面走；
       其余各 1 帧。朝向由 DrawBakedMonster 按玩家与怪的夹角选，
       侧面靠水平镜像覆盖另一侧（Doom 也是这么省美术的）。 */
    SPR_MON_HUNTER,  SPR_MON_HUNTER_W,  SPR_MON_HUNTER_S,  SPR_MON_HUNTER_SW,
    SPR_MON_LIGHT,   SPR_MON_LIGHT_W,   SPR_MON_LIGHT_S,   SPR_MON_LIGHT_SW,
    SPR_MON_LEECH,   SPR_MON_LEECH_W,   SPR_MON_LEECH_S,   SPR_MON_LEECH_SW,
    /* 2026-11-03：听声鬼数量 1→3，后两只换造型（见 art_direction.h）
       第二只「匍匐者」：矮、宽、长臂拖地，头压低
       第三只「长颈」  ：高、极瘦、脖子拉长，头和肩之间留空隙
       两套各 4 帧，和第一只同规矩（正面/正面走/侧面/侧面走）。 */
    SPR_MON_HUNTER2, SPR_MON_HUNTER2_W, SPR_MON_HUNTER2_S, SPR_MON_HUNTER2_SW,
    SPR_MON_HUNTER3, SPR_MON_HUNTER3_W, SPR_MON_HUNTER3_S, SPR_MON_HUNTER3_SW,
    SPR_MON_JUMPER, SPR_MON_SHADE, SPR_MON_SPIDER, SPR_MON_ECHO,
    SPR_MON_MIMIC,         /* 2026-11-05 宝箱怪现形态：箱子站起来 + 宽口 */
    /* 道具 */
    SPR_PROP_CRATE, SPR_PROP_BREAKER, SPR_PROP_EXIT, SPR_PROP_SAFE,
    SPR_PROP_CRATE_OPEN,   /* 已翻过的空箱（盖子掀开） */
    SPR_PROP_CRATE_LOCKED, /* 未解锁：正面挂金锁 */
    SPR_PROP_TRASH,        /* 藏身点：垃圾桶 */
    SPR_PROP_CRATE_MIMIC,  /* 2026-11-05 宝箱怪伪装态：华丽箱子（金包边 + 双层锁扣） */

    /* ==================================================================
     * 2026-11-06（v2.4-rc）：**主题房间的家具**
     * ------------------------------------------------------------------
     * 房间有了色彩和形状，但"里面什么都没有" —— 因为之前模板里的
     * "货架"只是**一格墙**，在黑暗里就是一根墙垛，不是货架。
     * 这里补两个真道具（烘焙精灵，不是墙格）：
     *   SPR_PROP_SHELF   货架：三层横板，填黑后是 〓 形
     *   SPR_PROP_CONSOLE 控制台：矮柜 + 斜面板 + 面板灯
     * 两者都按 art_direction.h 的"填黑测试"设计剪影。
     * ================================================================== */
    /* ==================================================================
     * 2026-11-06（v2.4-rc P3）：**分层伪立体**
     * ------------------------------------------------------------------
     * 一个物件拆成 2~3 个精灵，用不同的 baseZ 叠起来。
     * 走动时层与层之间有**视差** —— 这是 billboard 单图做不到的，
     * 也是"看起来是立体的"最关键的一步。
     * ================================================================== */
    SPR_PROP_SHELF_FRAME,  /* 货架：两根立柱 + 侧梁（不含板） */
    SPR_PROP_SHELF_BOARD,  /* 货架：单层横板 */
    SPR_PROP_CONSOLE_BASE, /* 控制台：柜体 */
    SPR_PROP_CONSOLE_PANEL,/* 控制台：抬起的斜面板 + 灯 */
    SPR_PROP_SHELF,        /* 保留：单图整货架（旧调用点仍用） */
    SPR_PROP_CONSOLE,      /* 保留：单图整控制台 */
    /* UI（v2.2） */
    SPR_UI_PANEL,      /* 9-slice 铆接金属板 */
    SPR_UI_PANEL_HOT,  /* 同上，聚焦态（边框泛琥珀） */
    SPR_UI_CURSOR,     /* 三角选择光标 */
    SPR_UI_ROW,        /* 行底纹（选中） */
    /* 2026-11-06：舱门 4 帧（两扇对开，七主题靠 accent 换色共用一套） */
    SPR_HATCH_0,       /* 全闭 */
    SPR_HATCH_1,       /* 开 1/3 */
    SPR_HATCH_2,       /* 开 2/3 */
    SPR_HATCH_3,       /* 全开 */
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

/* ==================================================================
 * 2026-11-06：**带深度测试的子矩形 blit**
 * ------------------------------------------------------------------
 * DrawSpriteSub 是给 UI 用的（无深度测试）。
 * 世界里的道具必须过 zBuffer，但又只想画**艺术包围盒**那一块 ——
 * 否则画布上的透明区也会被逐像素遍历（箱子实测浪费 2.67 倍面积，
 * 这正是"路过就掉帧"的来源）。
 * ================================================================ */
void DrawSpriteSubZ(int id, int sx, int sy, int sw, int sh,
                    int dx, int dy, int dw, int dh,
                    float light, float alpha, float zDist);

/* 水平镜像 blit（怪物侧面靠它覆盖另一侧，Doom 的做法） */
void DrawSpriteFlip(int id, int x, int y, int w, int h,
                    float light, float alpha, int zTest, float zDist);


/* v2.4.66：局部换色（只换 key 色像素） */
void SpriteAccentSet(unsigned int col);
void SpriteAccentClear(void);


void SpriteDumpPPM(int id, const char* path);

#endif

#ifndef CANITY_SHOP_H
#define CANITY_SHOP_H

#include "weapons.h"

/* ------------------------------------------------------------------ */
/* v2.0 安全屋（每 3 层一个）                                           */
/*   三页：SHOP 商店 / BENCH 改装台 / REST 休息                          */
/*   全部用碎片结算                                                     */
/* ------------------------------------------------------------------ */

#define SHOP_OFFERS 3
#define SAFEHOUSE_PAGES 5   /* v3.0：+ 强化 */

typedef enum {
    SHP_SHOP = 0,
    SHP_BENCH,
    SHP_TREE,    /* v2.7：局内改造树 —— v2.9 挪到 REST 前面，
                    否则页签文字数组和 enum 顺序不一致：
                    点「TREE」实际打开的是 REST（截图可见） */
    SHP_REST,
    SHP_UPGRADE   /* v3.0：碎片强化 */
} ShopPage;

extern int shopPage;                  /* 当前页 */
extern int shopRow;                   /* 当前页光标 */
extern int shopOffer[SHOP_OFFERS];    /* 配件 id，-1 = 已买 */
extern int shopPrice[SHOP_OFFERS];
extern int shopNodeSel;   /* v2.7：改造树光标节点 */
extern int attachOwned[ATTACH_COUNT]; /* 已拥有（未装）的配件数量 */

/* 进入安全屋：按层号重掷报价，重置休息次数 */
void ShopOpen(int level);
void SafeLevelReset(void);   /* v2.2 */

/* 面板结构 */
int  ShopPageCount(void);
const char* ShopPageTitle(void);
int  ShopRowCount(void);
const char* ShopRowText(int row);

/* 操作 */
void ShopMoveSel(int delta);
void ShopActivate(void);     /* 回车：买 / 装 / 休息 */
void ShopNextPage(void);
void ShopPrevPage(void);   /* v2.8 */
void ShopTreeMove(int dx, int dy);   /* v2.7 */     /* TAB */

int  AttachPrice(int id);

#endif

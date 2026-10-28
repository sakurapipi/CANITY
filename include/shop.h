#ifndef CANITY_SHOP_H
#define CANITY_SHOP_H

#include "weapons.h"

/* ------------------------------------------------------------------ */
/* v2.0 安全屋（每 3 层一个）                                           */
/*   三页：SHOP 商店 / BENCH 改装台 / REST 休息                          */
/*   全部用碎片结算                                                     */
/* ------------------------------------------------------------------ */

#define SHOP_OFFERS 3
#define SAFEHOUSE_PAGES 3

typedef enum {
    SHP_SHOP = 0,
    SHP_BENCH,
    SHP_REST
} ShopPage;

extern int shopPage;                  /* 当前页 */
extern int shopRow;                   /* 当前页光标 */
extern int shopOffer[SHOP_OFFERS];    /* 配件 id，-1 = 已买 */
extern int shopPrice[SHOP_OFFERS];
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
void ShopNextPage(void);     /* TAB */

int  AttachPrice(int id);

#endif

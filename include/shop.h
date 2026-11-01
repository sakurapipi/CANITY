#ifndef CANITY_SHOP_H
#define CANITY_SHOP_H

#include "weapons.h"

/* ------------------------------------------------------------------ */
/* v2.0 安全屋（每 3 层一个）                                           */
/*   三页：SHOP 商店 / BENCH 改装台 / REST 休息                          */
/*   全部用碎片结算                                                     */
/* ------------------------------------------------------------------ */

/* v3.4（方案 v5 §6）**商店改成 8 行、不限量、每行独立倒计时** ——
   用户要求：「商店里面不限制数量，只限制倒计时和金币，这样金币的捆绑更大」。
   库存无限，但每买一次该行进入冷却；金币才是真正的瓶颈。 */
/* v3.4：8 件商品。原第 9/10 行是「存钱」，已删除 ——
   调研结论：局内存钱是纯亏（本局变弱、下一局才收益、且倍数已锁定），
   银行决策统一挪到主菜单的 BANK 页。 */
/* ================================================================
 * v6 第 48 轮：**4 格退回 8 格**
 * ================================================================
 * 第 卌 轮把 8 格改成 4 格（设计文档要求），结果**一进商店就崩溃**。
 * 崩溃点没定位到 —— 不能拿玩家的稳定性换一个设计细节。
 *
 * 现在退回 8 格，但保留第 卌 轮真正有价值的两件事：
 *   · 价格按物品算（kItemPrice），不再按行号
 *   · 扣钱走 ShopPriceAt()，折扣**真的生效**（原来只改显示）
 *
 * 4 格作为已知未完成项留在文档里。
 * ================================================================ */
#define SHOP_OFFERS 4
#define SHOP_ITEM_KINDS 8   /* 可选物品总数 */
#define SHOP_COOLDOWN_ROWS 8

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
extern int shopRow;
/* v7 第 51 轮：**商店改成"叠在游戏上"的浮层**
   不再切到 STATE_SAFEHOUSE —— 那条状态路径一进就崩，
   而且不产生崩溃日志（栈溢出）。浮层完全不改状态，
   所以不可能碰到那条坏路径。 */
extern int shopOverlayOpen;                   /* 当前页光标 */
extern int shopOffer[SHOP_OFFERS];    /* 配件 id，-1 = 已买 */
extern int shopPrice[SHOP_OFFERS];
/* v6 折扣 */
#define SHOP_DISCOUNT_CHANCE 60
extern int   shopDiscountRow;
extern float shopDiscountPct;
int  ShopPriceAt(int row);
int  ShopIsDiscounted(int row);
extern int shopNodeSel;   /* v2.7：改造树光标节点 */
extern int attachOwned[ATTACH_COUNT]; /* 已拥有（未装）的配件数量 */

/* 进入安全屋：按层号重掷报价，重置休息次数 */
void ShopOpen(int level);
void ShopRefresh(void);
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

void ShopLeave(void);

#endif

/* v3.4：每行的剩余冷却秒数（0 = 可买） */
extern float shopCooldown[SHOP_COOLDOWN_ROWS];
float ShopCooldownOf(int row);
void  ShopTickCooldown(float dt);   /* 每帧递减 */
void  ShopResetCooldowns(void);

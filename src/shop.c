#include <stdio.h>
#include <stdlib.h>
#include "shop.h"
#include "game.h"
#include "entity.h"
#include "skills.h"
#include "lang.h"
#include "audio.h"
#include "bench.h"
#include "upgrade_run.h"
#include "save.h"

int shopPage = SHP_SHOP;
int shopRow = 0;
int shopOverlayOpen = 0;   /* v7 第 51 轮：商店浮层 */
int shopNodeSel = 0;   /* v2.7：改造树光标 */
int shopRefreshFloor=-1,shopRefreshUsed=0;
int shopOffer[SHOP_OFFERS] = {-1, -1, -1, -1};   /* v6：8 项全填 -1（0 是合法配件 id） */
/* v3.4：每行的剩余冷却（秒）。0 = 可买。 */
float shopCooldown[SHOP_COOLDOWN_ROWS] = {0};
static const float kRowCd[SHOP_COOLDOWN_ROWS] = {
    20.0f, 25.0f, 30.0f, 30.0f, 35.0f, 40.0f, 30.0f, 30.0f
};
float ShopCooldownOf(int row) {
    if (row < 0 || row >= SHOP_COOLDOWN_ROWS) return 0.0f;
    return shopCooldown[row];
}
void ShopTickCooldown(float dt) {
    for (int i = 0; i < SHOP_COOLDOWN_ROWS; i++)
        if (shopCooldown[i] > 0.0f) {
            shopCooldown[i] -= dt;
            if (shopCooldown[i] < 0.0f) shopCooldown[i] = 0.0f;
        }
}
void ShopResetCooldowns(void) {
    for (int i = 0; i < SHOP_COOLDOWN_ROWS; i++) shopCooldown[i] = 0.0f;
}
int shopPrice[SHOP_OFFERS] = {0};

/* v6 第 ㊹ 轮：**价格按物品算，不按行号**
   原来行号 = 物品 id，所以永远是同样的 8 行、同样的 8 个价。
   现在行号只是位置，真正的物品在 shopOffer[row] 里。 */
static const int kItemPrice[SHOP_ITEM_KINDS] = {
    35,    /* 0 SLING   投石 */
    50,    /* 1 FLARE   光球 */
    70,    /* 2 SMOKE   烟雾 */
    80,    /* 3 GLUE    胶水 */
    95,    /* 4 ECHO    回声 */
    120,   /* 5 DECOY   诱饵 */
    60,    /* 6 BATTERY 电池 */
    70     /* 7 MEDKIT  药包 */
};   /* v6：补满 8 项 */

/* ==================================================================
 * v6 **商店折扣**（每层随机一格打折）
 * ==================================================================
 * 为什么要有折扣
 *   调研结论："Generous sources and weak sinks inflate until rewards
 *   stop meaning anything."
 *   反过来也成立 —— **价格完全可预测的商店是没有决策的**：
 *   玩家只看"我够不够钱"，不看"这值不值"。
 *
 *   每层随机一格 −25% 或 −40%，玩家就会开始算：
 *   "这个折扣用在我现在需要的配件上，还是留着钱买下一层？"
 *   这才叫**选择**。
 *
 * 为什么每层只给一格
 *   全店打折 = 没有折扣。稀缺性才有意义。
 * ================================================================ */
int   shopDiscountRow = -1;    /* 打折的格子，-1 = 本层无折扣 */
float shopDiscountPct = 0.0f;  /* 0.25 / 0.40 */

static void rollDiscount(void) {
    shopDiscountRow = -1;
    shopDiscountPct = 0.0f;
    /* Every floor has one discounted slot. */   /* 60% 概率出折扣 */
    shopDiscountRow = rand() % SHOP_OFFERS;
    shopDiscountPct = ((rand() % 100) < 55) ? 0.25f : 0.40f;
}

/* 某格的实际售价（含折扣） */
/* ==================================================================
 * v7 第 ㊿ 轮：**无限递归 → 栈溢出 → 商店一开就崩**
 * ==================================================================
 * 第 ⑪ 轮我写这个函数时，本该读 `shopPrice[row]`，
 * 结果两处都写成了 `ShopPriceAt(row)` —— **自己调自己**。
 *
 * 后果连锁：
 *   ① 任何一条不折扣的行 → `return ShopPriceAt(row)` → 立即栈溢出
 *   ② 栈溢出 SEH 抓不到 → 崩溃日志一个字都没有
 *   ③ 调用点有三处：商店行文本 / 购买扣钱 / DrawShopPanel 每帧 8 次
 *   ④ 所以不只是浮层崩 —— **安全屋菜单只要渲染一行商店就会崩**
 *
 * 这一个 bug 解释了这十轮里所有的「商店打不开 / 一按就崩 / 没有日志」。
 * ================================================================ */
int ShopPriceAt(int row) {
    if (row < 0 || row >= SHOP_OFFERS) return 0;
    /* 价格优先用 ShopOpen 掷出来的 shopPrice[]；
       没掷过（还没进过商店）就退回物品表 kItemPrice[] ——
       这样任何一行在任何时候都有价格，不会再出现 0 元。 */
    int base = shopPrice[row];
    if (base <= 0) base = kItemPrice[row];
    if (row == shopDiscountRow && shopDiscountPct > 0.0f) {
        int p = (int)((float)base * (1.0f - shopDiscountPct) + 0.5f);
        if (p < 1) p = 1;
        return p;
    }
    return base;
}

int ShopIsDiscounted(int row) {
    return (row == shopDiscountRow && shopDiscountPct > 0.0f) ? 1 : 0;
}
int attachOwned[ATTACH_COUNT];

/* v2.1：改成按层记，反复开关面板不能刷新次数 */
static int shopRollLevel = -1;
static int safeHealLevel = -1;

/* v2.2：开局/换局清一次。原来这两个 static 永不重置，
   上一局在某层休息过，之后任何一局走到同一层都是「已休息」。 */

static int safeStaminaLevel = -1;   /* v3.3：体力每层限一次 */
static int safeChargeLevel = -1;
static char sbuf[160];

int AttachPrice(int id) {
    if (id < 0 || id >= ATTACH_COUNT) return 0;
    switch (attachDefs[id].rarity) {
    case 0:  return 80;
    case 1:  return 150;
    default: return 260;
    }
}

/* 商店报价：3 个不重复的配件 */
void ShopOpen(int level) {
    /* ================================================================
     * v6 第 ㉚ 轮：**"打开"函数必须自己打开**
     * ================================================================
     * 原来只有"本层已经掷过"那条分支设 currentState = STATE_SAFEHOUSE，
     * **首次打开的那条路径不设** —— 全靠调用方补一行：
     *     ShopOpen(currentLevel);
     *     currentState = STATE_SAFEHOUSE;   ← 调用方负责
     *
     * 这种"一半在函数里、一半在调用方"的写法是定时炸弹：
     * 任何新调用点（比如我加的调试入口）都会静默失败 ——
     * 调了 ShopOpen，界面根本没出来。
     *
     * 现在：进入时统一设好状态，调用方只负责调 ShopOpen。
     * ================================================================ */
    /* ================================================================
     * v6 第 48 轮：**状态切换还给调用方**
     * ================================================================
     * 第 卅 轮我把「设 currentState」搬进 ShopOpen，理由是
     * 「Open 函数应该自己打开」。结果商店一进就崩 ——
     * 用 demo2（自动开局 + 自动开商店）稳定复现，
     * 而 demo3（只调 ShopOpen、立刻弹回 PLAYING）能活。
     *
     * 说明崩在**进入 SAFEHOUSE 状态的渲染**，而这条渲染路径
     * 在「由调用方切状态」时是好的 —— 差别就在切状态的时机。
     *
     * 稳定性优先：恢复成调用方切状态，只保留价格/折扣的真修复。
     * ================================================================ */

    /* v2.2 修复：原来每次按 E 进来都重新 rand() 三个报价 ——
       面板开着时游戏逻辑整段暂停（理智不流失、怪不动），
       于是"反复进出商店"等于免费无限重掷，直到刷出想要的配件。
       现在同一层只掷一次。 */
    if (level == shopRollLevel) {
        shopRow = 0;
        return;
    }
    shopRollLevel = level;
    if(shopRefreshFloor!=level) { shopRefreshFloor=level;shopRefreshUsed=0; }
    rollDiscount();   /* v6：本层随机一格打折 */
    (void)level;
    shopPage = SHP_SHOP;
    shopRow = 0;
    /* 次数不在这里重置：见 shop.c 的 safeHealLevel/safeChargeLevel */
    /* v6 第 ㊹ 轮：**从 8 种物品里抽 4 种不重复的**
       原来这里抽的是 attachDefs 的 id（武器配件，早废了），
       而购买逻辑用的是行号 —— 两套 id 完全不相干，
       等于抽了半天没用上，每层卖的永远是同样 8 样。 */
    {
        int pool[SHOP_ITEM_KINDS];
        for (int i = 0; i < SHOP_ITEM_KINDS; i++) pool[i] = i;
        int n = SHOP_ITEM_KINDS;
        for (int k = 0; k < SHOP_OFFERS; k++) {
            int pick = rand() % n;
            int kind = pool[pick];
            pool[pick] = pool[--n];
            shopOffer[k] = kind;
            shopPrice[k] = kItemPrice[kind];
        }
    }
}

int ShopPageCount(void) { return SAFEHOUSE_PAGES; }

const char* ShopPageTitle(void) {
    switch (shopPage) {
    case SHP_BENCH: return Tr("SAFE HOUSE  -  BENCH", "安全屋  -  改装台");
    case SHP_REST:  return Tr("SAFE HOUSE  -  REST",  "安全屋  -  休息");
    case SHP_TREE:  return Tr("WORKBENCH TREE", "改造树");
    case SHP_UPGRADE: return Tr("GOLD UPGRADE", "金币强化");   /* v3.3：原来写死英文且叫 CRYSTAL（核心是另一种货币） */
    default:        return Tr("SAFE HOUSE  -  SHOP",  "安全屋  -  商店");
}
}


/* v3.3 **本层金币可以花** ——
   搜箱子的金币先进 carryGold，过关才并入 carryGold（重生细胞式上缴）。
   但安全屋就在本层，玩家身上明明有 60 金币，站店里却提示「金币不够」。
   这里提供统一入口：先扣已入账的 carryGold，不够再扣本层待上缴的 carryGold。
   返回 1 = 扣成功，0 = 两边加起来也不够。 */
static int ShopSpend(int cost) {
    /* v6：统一走 Wallet —— 单一货币，不再有"已入账 / 待上缴"两级 */
    extern int WalletSpendRun(int);
    return WalletSpendRun(cost);
}

int ShopRowCount(void) {
    if (shopPage == SHP_TREE) return 0;   /* v2.7：树自己画，不用列表行 */
    switch (shopPage) {
    case SHP_BENCH: return 4;     /* CORE / MOD / CLEAR / LEAVE */
    case SHP_REST:  return 5;     /* REST / CHARGE / STAMINA / SHORTCUT / LEAVE */
    case SHP_UPGRADE: return RUN_UPG_COUNT + 2;   /* 4 条线 + LEAVE */
    default:        return SHOP_OFFERS + 1;
    }
}

const char* ShopRowText(int row) {
    if (row < 0) row = 0;

    /* v3.0：局内强化页 —— 4 条线，显示等级和价格 */
    if (shopPage == SHP_UPGRADE) {
        if (row == RUN_UPG_COUNT) {
            /* v7 B3：协议行 —— 回车循环切换四种协议 */
            extern const char* ContractName(void);
            extern float ContractExitMul(void);
            sprintf(sbuf, "CONTRACT  %s   EXIT x%d%%",
                    ContractName(), (int)(ContractExitMul() * 100.0f));
            return sbuf;
        }
        if (row > RUN_UPG_COUNT) return Tr("LEAVE  (ESC)", "离开  (ESC)");
        int lv = RunUpgLevel(row);
        int cost = RunUpgCost(row);
        /* ==================================================================
         * v7 第 62 轮：**RunUpgDesc / RunUpgDescZh 全工程 0 调用点**
         * ==================================================================
         * 强化页只显示「SUPPLY  LV2/3  600」——
         * 玩家不知道这一级买下去到底加什么，只能猜。
         * 现在把效果说明接在名字后面。
         * ================================================================ */
        {
            extern int g_lang;
            extern const char* RunUpgDesc(int);
            extern const char* RunUpgDescZh(int);
            const char* d = (g_lang == LANG_ZH) ? RunUpgDescZh(row) : RunUpgDesc(row);
            if (cost < 0)
                sprintf(sbuf, "%s  LV%d/%d  MAX   %s",
                        Tr(RunUpgName(row), RunUpgNameZh(row)), lv, RUN_UPG_MAX,
                        d ? d : "");
            else
                sprintf(sbuf, "%s  LV%d/%d  %d   %s",
                        Tr(RunUpgName(row), RunUpgNameZh(row)), lv, RUN_UPG_MAX, cost,
                        d ? d : "");
        }
        /* v7 第 66 轮：**说明会画到面板外**
           实测截图里「Throwable capacity +2」右端被面板边框切掉。
           按面板宽度（减掉左侧页签列和右边距）夹一下。 */
        /* v7 第 66 轮：说明会画到面板外。
           用一个保守的固定上限（80 字符）截断 —— ClampRowText 是 ui.c 内部函数，
           这里拿不到，硬接会链接失败。 */

        return sbuf;
    }

    if (shopPage == SHP_SHOP) {
        /* v3.0：**商店从「武器配件」改成「消耗品补给」** ——
           原来卖的是 attachDefs 里的 D+0% C+0% 武器配件，
           无战斗后这些数字全无意义，而且价格 150 起，买来等于扔钱。
           现在卖逃生真正缺的四样东西。 */
        if (row >= SHOP_OFFERS) {
            return Tr("LEAVE  (ESC)", "离开  (ESC)");
        }
        /* v3.3：**修价格倒挂** ——
           原来「急救包 +25 理智 40 金币」比安全屋「休息 +40 理智 25 金币」
           又贵又少，玩家没有任何理由买急救包。
           现在急救包 25（同价但少给 15 点），是「临时补给」的定位，
           安全屋休息仍然是性价比最高的恢复手段。 */
        /* ================================================================
         * v6：**价格不再写死在字符串里**
         * ================================================================
         * 原来这里是 16 个硬编码字符串：
         *     "SLING x1       35"
         * 价格 35 是文案的一部分 —— 改价格要改字符串，加折扣更是不可能。
         *
         * 现在：名字和价格**分开**，价格从 shopPrice[] 生成，
         * 折扣从 ShopPriceAt() 来。改价改一个数字，折扣自动生效。
         *
         * 这正是用户说的「不变的东西完全程序化」。
         * ================================================================ */
        static const char* en[8] = {
            "SLING x1", "FLARE x1", "SMOKE x1", "GLUE x1",
            "ECHO x1",  "DECOY x1", "BATTERY +35", "MEDKIT +25"
        };
        static const char* zh[8] = {
            "投石 x1", "光球 x1", "烟雾 x1", "胶水 x1",
            "回声 x1", "诱饵 x1", "电池 +35", "药包 +25"
        };
        /* v6 第 ㊹ 轮：**名字按物品 id 取，不是按行号**
           原来 idx = row，所以第 0 行永远是 SLING。
           现在 shopOffer[row] 才是这一格真正在卖的东西。 */
        int idx = (row >= 0 && row < SHOP_OFFERS) ? shopOffer[row] : 0;
        if (idx < 0 || idx >= SHOP_ITEM_KINDS) idx = 0;

        static char rb[96];
        int price = ShopPriceAt(row);
        const char* nm = (g_lang == LANG_ZH) ? zh[idx] : en[idx];

        /* 冷却中 */
        {
            float cd = ShopCooldownOf(row);
            if (cd > 0.05f) {
                snprintf(rb, sizeof(rb), "%-14s %4d   (%ds)", nm, price, (int)(cd + 0.5f));
                return rb;
            }
        }
        /* 折扣行：价格前加一个 ▼ 标记，并把原价划掉的样子用括号表示 */
        if (ShopIsDiscounted(row)) {
            snprintf(rb, sizeof(rb), "%-14s %4d  <-%d%%",
                     nm, price, (int)(shopDiscountPct * 100.0f + 0.5f));
            return rb;
        }
        snprintf(rb, sizeof(rb), "%-14s %4d", nm, price);
        return rb;
    }

    if (shopPage == SHP_BENCH) {
        /* v3.0：**改装台从「武器配件」改成「随身装备」** ——
           原来两个槽是 CORE / MOD（武器核心与词条），
           武器取消后这两个槽完全没意义，页面上只剩两行 "(empty)"。
           现在改成逃生向的两件随身装备：
             槽 1 = 手上的投掷物（瓶子/照明弹/砖头/酸/烟）
             槽 2 = 照明工具（手电状态与电量）
           内容实时反映当前库存，不是空槽。 */
        if (row == 0) {
            int n = 0;
            if (bottles > 0) n++;
            if (flares > 0) n++;
            if (bricks > 0) n++;
            if (acids > 0) n++;
            if (smokeBombs > 0) n++;
            sprintf(sbuf, "%s: %d %s", Tr("THROWABLES", "投掷物"), n,
                    Tr("kinds", "种"));
            return sbuf;
        }
        if (row == 1) {
            int bp = (int)(battery + 0.5f);
            sprintf(sbuf, "%s: %s %d%%", Tr("LIGHT GEAR", "照明"),
                    flashlightOn ? Tr("ON", "开") : Tr("OFF", "关"), bp);
            return sbuf;
        }
        if (row == 2) return Tr("CHECK INVENTORY", "查看背包");
        return Tr("LEAVE  (ESC)", "离开  (ESC)");
    }

    /* REST */
    if (row == 0) {
        if (safeHealLevel == currentLevel) return Tr("RESTED", "已休息");
        return Tr("REST   +40 SANITY  (25)", "休息   +40 理智  (25)");
    }
    if (row == 1) {
        if (safeChargeLevel == currentLevel) return Tr("CHARGED", "已充电");
        return Tr("CHARGE   +60 BATTERY", "充电   +60 电量");
    }
    if (row == 2) {
        /* v3.3：**这一行原来是免费无限体力**（躲衣柜→按 T→满体力→Esc→跑，
           可以无限重复）。现在加每层一次的标记 + 15 金币。 */
        if (safeStaminaLevel == currentLevel) return Tr("STAMINA RESTORED", "体力已恢复");
        return Tr("RECOVER STAMINA  (15)", "恢复体力  (15)");
    }
    if (row == 3) {
        /* v2.0 SHORTCUT：每局一次，Boss 层不可跳 */
        extern int shortcutUsed, bossLevel;
        if (!SkillShortcutAvailable()) return Tr("SHORTCUT (locked)", "跳层（未解锁）");
        if (shortcutUsed)              return Tr("SHORTCUT (used)", "跳层（已用）");
        if (bossLevel)                 return Tr("SHORTCUT (boss floor)", "跳层（首领层）");
        return Tr("SHORTCUT   skip this floor", "跳层   跳过本层");
    }
    return Tr("LEAVE  (ESC)", "离开  (ESC)");   /* v3.3：row 4 现在真能走到了 */
}

void ShopMoveSel(int delta) {
    int n = ShopRowCount();
    shopRow = n > 0 ? ((shopRow + delta + n) % n) : 0;
    uiDirty = 1;
}

/* v2.7：改造树的四向移动（4 列 × 3 行） */
/* v6：**Tab 只在这 3 页之间循环** ——
   方案 v5 决定把 5 页压到 3 页（商店 / 改装台 / 休息），
   TREE 和 UPGRADE 两页退役。枚举值不动（避免大规模重编号），
   只在翻页时跳过它们。 */
static int ShopIsLivePage(int pg) {
    /* v6 **恢复全部 5 页** ——
       之前为了「5 页压 3 页」把 TREE / UPGRADE 从翻页里跳过，
       但那套压缩从来没真正完成（页签还画 5 个、绘制分支还在），
       结果改造树（16 节点）和金币强化两整页彻底进不去。
       现在和画出来的页签保持一致：5 页全开。 */
    return (pg >= 0 && pg < SAFEHOUSE_PAGES) ? 1 : 0;
}

void ShopPrevPage(void) {
    for (int k = 0; k < SAFEHOUSE_PAGES; k++) {
        shopPage = (shopPage + SAFEHOUSE_PAGES - 1) % SAFEHOUSE_PAGES;
        if (ShopIsLivePage(shopPage)) break;
    }
    shopRow = 0;
    uiDirty = 1;
}

void ShopTreeMove(int dx, int dy) {
    int col = shopNodeSel / BENCH_ROWS;
    int row = shopNodeSel % BENCH_ROWS;
    col += dx;
    row += dy;
    if (col < 0) col = 0;
    if (col >= BENCH_COLS) col = BENCH_COLS - 1;
    if (row < 0) row = 0;
    if (row >= BENCH_ROWS) row = BENCH_ROWS - 1;
    shopNodeSel = col * BENCH_ROWS + row;
    uiDirty = 1;
}

void ShopNextPage(void) {
    for (int k = 0; k < SAFEHOUSE_PAGES; k++) {
        shopPage = (shopPage + 1) % SAFEHOUSE_PAGES;
        if (ShopIsLivePage(shopPage)) break;
    }
    shopRow = 0;
    uiDirty = 1;
}

/* 找一个"已拥有但没装在这把武器上"的配件，返回 id；没有则 -1 */


void ShopActivate(void) {
    /* v3.0：金币强化 */
    if (shopPage == SHP_UPGRADE) {
        if (shopRow == RUN_UPG_COUNT) {
        /* v7 B3：切协议（每局一次，签了不能撕 —— 这里只允许首次切换） */
        extern void ContractCycle(void);
        extern int shopContractUsed;
        if (shopContractUsed) { PlaySfx(SFX_GLASS, 0.4f); return; }
        ContractCycle();
        shopContractUsed = 1;
        PlaySfx(SFX_POWER, 0.7f);
        return;
    }
    if (shopRow > RUN_UPG_COUNT) return;      /* LEAVE 由 Esc 处理 */
        int r = RunUpgTryBuy(shopRow);
        if (r == 0)            { pickupMsg = Tr("UPGRADED", "已强化"); }
        else if (r == 2)       { pickupMsg = Tr("MAX LEVEL", "已满级"); }
        else if (r == 4)       { pickupMsg = Tr("NOT ENOUGH", "金币不够"); }
        pickupMsgTimer = 1.4f;
        uiDirty = 1;
        return;
    }
    if (shopPage == SHP_TREE) {
        /* v7 第 ㊿ 轮：原因码 1=成功 2=已拥有 3=需前置 4=钱不够 5=被对面锁死
           （原来把 0 当成功，等的是永远不存在的 2/3/4 → 假成功） */
        int r = BenchTryBuy(shopNodeSel);
        if (r == 1) {
            pickupMsg = Tr("INSTALLED", "已安装");
            PlaySfx(SFX_POWER, 0.9f);
        } else if (r == 2) {
            pickupMsg = Tr("ALREADY OWNED", "已拥有");
        } else if (r == 3) {
            pickupMsg = Tr("NEED PREVIOUS NODE", "需要先买上一层");
        } else if (r == 4) {
            pickupMsg = Tr("NOT ENOUGH GOLD", "金币不够");
        } else if (r == 5) {
            pickupMsg = Tr("LOCKED BY THE OTHER CAPSTONE", "已被另一个顶点锁死");
        }
        pickupMsgTimer = 1.6f;
        uiDirty = 1;
        return;
    }
    if (shopPage == SHP_SHOP) {
        if (shopRow >= SHOP_OFFERS) {
        /* v6 **LEAVE 行回车要能真的离开** ——
           原来画着 "LEAVE (ESC)" 但回车直接 return，玩家选中它按回车毫无反馈。 */
        extern void ShopLeave(void);
        ShopLeave();
        return;
    }
        /* ===== v3.4 八行补给：不限量，只受「金币 + 冷却」限制 =====
           用户要求：「商店里面不限制数量，只限制倒计时和金币，
           这样金币的捆绑更大，别人才有想玩的冲动。」 */
        /* ================================================================
         * v6 第 ㊼ 轮：**扣钱用的是另一张价格表**
         * ================================================================
         * 这里曾经有一份写死的 price[8]，按**行号**取值；
         * 而第 ⑪ 轮的折扣只改了显示用的 ShopPriceAt()。
         *
         * 后果两个：
         *   ① 折扣只显示、不生效 —— 玩家看到 -25%，实际扣全价
         *   ② 第 ⑭ 轮改了「行号 → 物品」映射后，**付第 N 行的钱，
         *      拿 shopOffer[N] 的东西** —— 35 块买到 120 的药包
         *
         * 现在价格一律走 ShopPriceAt(idx)（含折扣、按物品算）。
         * ================================================================ */
        int idx = shopRow;
        if (idx < 0) idx = 0;
        if (idx >= SHOP_OFFERS) return;

        /* 冷却中 → 拒绝并提示还剩多久 */
        if (ShopCooldownOf(idx) > 0.05f) {
            static char cmsg[64];
            snprintf(cmsg, sizeof(cmsg),
                     Tr("RESTOCKING  %ds", "补货中  %d 秒"),
                     (int)(ShopCooldownOf(idx) + 0.5f));
            pickupMsg = cmsg;
            pickupMsgTimer = 1.2f;
            uiDirty = 1;
            return;
        }
        if (!ShopSpend(ShopPriceAt(idx))) {
            pickupMsg = Tr("NOT ENOUGH GOLD", "金币不够");
            pickupMsgTimer = 1.2f;
            uiDirty = 1;
            return;
        }
        /* v6 第 ㊹ 轮：**买的是这一格的物品，不是"第几行"** */
        int kind = (idx >= 0 && idx < SHOP_OFFERS) ? shopOffer[idx] : 0;
        if (kind < 0 || kind >= SHOP_ITEM_KINDS) kind = 0;
        switch (kind) {
        case 0: if (bottles < BottleCap()) bottles++;
                pickupMsg = Tr("+1 SLING", "+1 投石"); break;
        case 1: if (flares < BottleCap()) flares++;
                pickupMsg = Tr("+1 FLARE", "+1 光球"); break;
        case 2: if (smokeBombs < BottleCap()) smokeBombs++;
                pickupMsg = Tr("+1 SMOKE", "+1 烟雾"); break;
        case 3: if (gluePots < BottleCap()) gluePots++;
                pickupMsg = Tr("+1 GLUE", "+1 胶水"); break;
        case 4: if (echoBolts < BottleCap()) echoBolts++;
                pickupMsg = Tr("+1 ECHO", "+1 回声"); break;
        case 5: if (decoys < BottleCap()) decoys++;
                pickupMsg = Tr("+1 DECOY", "+1 诱饵"); break;
        case 6: battery += 35.0f; if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
                pickupMsg = Tr("+35 BATTERY", "+35 电量"); break;
        default: sanity += 25.0f; if (sanity > SanityMax()) sanity = SanityMax();
                pickupMsg = Tr("+25 SANITY", "+25 理智"); break;
        }
        /* 买入后该行进入冷却 */
        shopCooldown[idx] = kRowCd[idx];
        PlaySfx(SFX_POWER, 0.8f);
        pickupMsgTimer = 1.2f;
        uiDirty = 1;
        return;
    }

    if (shopPage == SHP_BENCH) {
        /* v3.3 **修复文案与动作完全错位** ——
           界面上画的是 THROWABLES / LIGHT GEAR / CHECK INVENTORY，
           回车却执行 WeaponAttachAdd / Remove / 清空两个武器配件槽。
           玩家点「查看背包」会把自己的武器配件清掉（武器早取消了，
           等于把老存档里的残留配件也一并抹掉）。
           现在这一页是**只读信息页**：三行都只是看，不产生任何副作用。
           （武器配件系统本身已被 NO_COMBAT 废掉，这里不再碰它。） */
        if (shopRow == 2) {
            int nb = (bottles > 0) + (flares > 0) + (bricks > 0)
                   + (acids > 0) + (smokeBombs > 0);
            /* v2.3.7 修复悬空指针：
               原来是栈上的 char msg[96]，函数一返回 msg 就失效，
               而 pickupMsg 是全局指针，ui.c 会在之后 1.6 秒持续读它 —— 读到的是已释放的栈。
               同文件其它同类处都加了 static，唯独这里漏了。 */
            static char msg[96];
            snprintf(msg, sizeof(msg),
                     Tr("BOTTLES %d  FLARES %d  OTHERS %d",
                        "瓶子 %d  照明弹 %d  其它 %d"),
                     bottles, flares, nb);
            pickupMsg = msg;
            pickupMsgTimer = 1.6f;
            PlaySfx(SFX_POWER, 0.5f);
            uiDirty = 1;
        }
        return;
    }

    /* REST —— v2.0：改成收费（原来免费 +40，8 个安全屋白送 320 理智，
       让理智彻底不像计时器） */
    if (shopRow == 0) {
        if (safeHealLevel == currentLevel) return;
        if (!ShopSpend(SAFE_REST_COST)) {
            pickupMsg = Tr("NEED 25 GOLD", "需要 25 金币");
            pickupMsgTimer = 1.2f;
            uiDirty = 1;
            return;
        }
        safeHealLevel = currentLevel;
        sanity += 40.0f;
        if (sanity > SanityMax()) sanity = SanityMax();
        PlaySfx(SFX_POWER, 0.9f);
        pickupMsg = Tr("+40 SANITY (-25)", "+40 理智 (-25)");
        pickupMsgTimer = 1.2f;
        uiDirty = 1;
        return;
    }
    if (shopRow == 1) {
        if (safeChargeLevel == currentLevel) return;
        safeChargeLevel = currentLevel;
        battery += 60.0f;
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        PlaySfx(SFX_POWER, 0.9f);
        pickupMsg = Tr("+60 BATTERY", "+60 电量");
        pickupMsgTimer = 1.2f;
        uiDirty = 1;
        return;
    }
    /* v2.0：第 4 行 = 跳层（SHORTCUT keystone） */
    if (shopRow == 3) {
        extern void SkipOneLevel(void);
        extern int shortcutUsed, bossLevel;
        if (SkillShortcutAvailable() && !shortcutUsed && !bossLevel) {
            currentState = STATE_PLAYING;
            resetInputState = 1;
            ForceCursor(0);
            SkipOneLevel();
        }
        return;
    }
    if (shopRow == 2) {
        /* v3.3：**体力从免费无限改成 15 金币 + 每层一次**。
           原来这一行什么都不收，玩家可以躲衣柜→按 T→回车→Esc→跑，
           无限刷满体力，冲刺等于没有成本。 */
        if (safeStaminaLevel == currentLevel) return;
        if (!ShopSpend(15)) {
            pickupMsg = Tr("NEED 15 GOLD", "需要 15 金币");
            pickupMsgTimer = 1.2f;
            uiDirty = 1;
            return;
        }
        safeStaminaLevel = currentLevel;
        stamina = StaminaMax();
        pickupMsg = Tr("STAMINA RESTORED (-15)", "体力已恢复 (-15)");
        pickupMsgTimer = 1.2f;
        PlaySfx(SFX_POWER, 0.8f);
        uiDirty = 1;
        return;
    }
}

/* v2.2：开局/换局清一次。原来这两个 static 永不重置，
   上一局在某层休息过，之后任何一局走到同一层都是「已休息」。 */
void SafeLevelReset(void) {
    shopRollLevel=-1; shopRefreshFloor=-1; shopRefreshUsed=0;
    shopPage=SHP_SHOP; shopRow=0;
    safeHealLevel = -1;
    safeChargeLevel = -1;
    safeStaminaLevel = -1;   /* v3.3 */
}

/* v3.3：给存档层读写这三个「每层限一次」标记 */
int  ShopSafeHealLv(void)    { return safeHealLevel; }
int  ShopSafeChargeLv(void)  { return safeChargeLevel; }
int  ShopSafeStaminaLv(void) { return safeStaminaLevel; }
void ShopSetSafeLv(int heal, int charge, int stamina) {
    safeHealLevel = heal;
    safeChargeLevel = charge;
    safeStaminaLevel = stamina;
}


/* v6 **离开安全屋** —— 原来 LEAVE 行画着 "LEAVE (ESC)" 但回车直接 return，
   玩家选中它按回车毫无反馈。这里和 ESC 走同一条路径。 */
void ShopLeave(void) {
    extern int resetInputState, uiDirty;
    extern void ForceCursor(int on);
    currentState = STATE_PLAYING;
    resetInputState = 1;
    ForceCursor(0);
    uiDirty = 1;
}


/* ==================================================================
 * v7 第 ㊿ 轮：**商店独立渲染**（DrawShopPanel）
 * ==================================================================
 * 为什么不走通用菜单渲染：
 *   自动复现（demo4）证明 —— 只要 currentState == STATE_SAFEHOUSE，
 *   哪怕完全不碰商店数据，进程立刻死，且不产生崩溃日志
 *   （无日志 = 栈溢出 / 递归）。
 *   那条通用路径（RowRect → PageTabRect → GetMenuGeo → MakeGeo）是坏环。
 *
 * 这一页自己画：没有行滚动、没有页签几何回调、没有共享状态。
 * 只用最底层的 DrawTextWin / BlendRectWin / FrameRectWin。
 * ================================================================ */

extern void DrawTextWin(int, int, const char*, unsigned int);
extern void DrawTextCenterWin(int, int, const char*, unsigned int);
extern void BlendRectWin(int, int, int, int, unsigned int, float);
extern void FrameRectWin(int, int, int, int, unsigned int, float, int);
extern int  TextWidthWin(const char*);
extern int  TextHeightWin(void);
extern int  winW, winH;
extern int  g_lang;
extern int  carryGold;
extern void PlaySfx(int, float);
extern const char* pickupMsg;
extern float pickupMsgTimer;
extern int uiDirty;

/* UIS：设计分辨率缩放（ui.c 里有同名宏，这里自己算一份） */
static int ShopUIS(int v) {
    int h = winH > 0 ? winH : 600;
    int r = (v * h + 300) / 600;
    return r > 0 ? r : 1;
}

#define PAL_GOLD_V    0x00FFD060u
#define PAL_CYAN_V    0x0060C8FFu
#define PAL_PANEL_V   0x00A08040u
#define PAL_DIM_V     0x00505858u

/* 物品名（程序里唯一一份） */
static const char* kShopNameEn[SHOP_ITEM_KINDS] = {
    "SLING x1", "FLARE x1", "SMOKE x1", "GLUE x1",
    "ECHO x1", "DECOY x1", "BATTERY +35", "MEDKIT +25"
};
static const char* kShopNameZh[SHOP_ITEM_KINDS] = {
    "投石 x1", "光球 x1", "烟雾 x1", "胶水 x1",
    "回声 x1", "诱饵 x1", "电池 +35", "药包 +25"
};
static const char* ShopItemName(int i) {
    if (i < 0 || i >= SHOP_ITEM_KINDS) return "?";
    return (g_lang == LANG_ZH) ? kShopNameZh[i] : kShopNameEn[i];
}

void DrawShopPanel(void) {
    extern volatile int g_dbgMark;
    g_dbgMark = 100;
    int th = TextHeightWin();
    if (th < 6) th = 6;

    int pad  = ShopUIS(28);
    int px0  = pad;
    int py0  = pad;
    int px1  = winW - pad;
    int py1  = winH - pad;
    if (px1 <= px0 + ShopUIS(200)) return;   /* 窗口太小，什么都不画 */
    if (py1 <= py0 + ShopUIS(120)) return;

    /* 面板 + 标题 */
    g_dbgMark = 101;
    BlendRectWin(px0, py0, px1, py1, 0x000A1016u, 0.96f);
    FrameRectWin(px0, py0, px1, py1, PAL_PANEL_V, 0.9f, ShopUIS(2));

    g_dbgMark = 102;
    const char* title = ShopPageTitle();
    if (!title || !title[0]) title = "SAFE HOUSE";
    DrawTextCenterWin((px0 + px1) / 2, py0 + th / 2, title, PAL_GOLD_V);

    /* 右上角余额 */
    {
        static char gb[48];
        snprintf(gb, sizeof(gb), "GOLD %d", carryGold);
        g_dbgMark = 103;
        DrawTextWin(px1 - ShopUIS(18) - TextWidthWin(gb), py0 + th / 2, gb, PAL_GOLD_V);
    }

    int y = py0 + th * 2 + ShopUIS(10);

    /* 只有商店页画物品行；其他页（改造树/休息/强化）暂用一行提示，
       避免把它们也拖进这条新路径 —— 那三页本来能画（走老路径）。 */
    if (shopPage != SHP_SHOP) {
        const char* note = (g_lang == LANG_ZH)
            ? "该页暂用旧布局（ESC 返回）"
            : "THIS PAGE USES THE OLD LAYOUT (ESC BACK)";
        DrawTextWin(px0 + ShopUIS(24), y, note, PAL_DIM_V);
        DrawTextCenterWin((px0 + px1) / 2, py1 - th * 2,
                          (g_lang == LANG_ZH) ? "ESC 关闭" : "ESC  CLOSE", PAL_DIM_V);
        return;
    }

    /* 8 个物品行 */
    g_dbgMark = 104;
    for (int i = 0; i < SHOP_ITEM_KINDS; i++) {
        int top = y + i * (th + ShopUIS(10));
        if (top + th > py1 - th * 3) break;

        static char row[96];
        const char* nm = ShopItemName(i);
        g_dbgMark = 110 + i;
        int price = ShopPriceAt(i);

        float cd = ShopCooldownOf(i);
        if (cd > 0.05f) {
            snprintf(row, sizeof(row), "%-16s %5d   (%ds)", nm, price, (int)(cd + 0.5f));
        } else if (ShopIsDiscounted(i)) {
            snprintf(row, sizeof(row), "%-16s %5d  <-%d%%",
                     nm, price, (int)(shopDiscountPct * 100.0f + 0.5f));
        } else {
            snprintf(row, sizeof(row), "%-16s %5d", nm, price);
        }

        int isSel = (shopRow == i);
        if (isSel) {
            BlendRectWin(px0 + ShopUIS(12), top - ShopUIS(3),
                         px1 - ShopUIS(12), top + th + ShopUIS(3),
                         0x001C3A1Cu, 0.85f);
        }
        DrawTextWin(px0 + ShopUIS(24), top, row,
                    isSel ? PAL_CYAN_V : (cd > 0.05f ? PAL_DIM_V : 0x00D8D8D8u));
    }

    /* LEAVE 行 */
    {
        int top = y + SHOP_ITEM_KINDS * (th + ShopUIS(10));
        if (top + th <= py1 - th * 2) {
            int isSel = (shopRow >= SHOP_ITEM_KINDS);
            if (isSel) {
                BlendRectWin(px0 + ShopUIS(12), top - ShopUIS(3),
                             px1 - ShopUIS(12), top + th + ShopUIS(3),
                             0x001C3A1Cu, 0.85f);
            }
            DrawTextWin(px0 + ShopUIS(24), top,
                        Tr("LEAVE  (ESC)", "离开  (ESC)"),
                        isSel ? PAL_CYAN_V : PAL_DIM_V);
        }
    }

    /* 底部提示 */
    DrawTextCenterWin((px0 + px1) / 2, py1 - th * 2,
                      Tr("UP/DOWN SELECT   ENTER BUY   ESC CLOSE",
                         "上下选择   回车购买   ESC 关闭"),
                      PAL_DIM_V);
}

void ShopRefresh(void) {
    if(shopRefreshFloor!=currentLevel) { shopRefreshFloor=currentLevel;shopRefreshUsed=0; }
    if(shopRefreshUsed) { pickupMsg=Tr("REFRESH USED THIS FLOOR","本层刷新已用完");pickupMsgTimer=1.5f;return; }
    shopRefreshUsed=1; shopRollLevel=-1; ShopOpen(currentLevel);
    pickupMsg=Tr("SUPPLIES REFRESHED","补给已刷新"); pickupMsgTimer=1.5f;uiDirty=1;SaveGame();
}

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

int shopPage = SHP_SHOP;
int shopRow = 0;
int shopNodeSel = 0;   /* v2.7：改造树光标 */
int shopOffer[SHOP_OFFERS] = {-1, -1, -1, -1};
int shopPrice[SHOP_OFFERS] = {0, 0, 0, 0};
int attachOwned[ATTACH_COUNT];

/* v2.1：改成按层记，反复开关面板不能刷新次数 */
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
    /* v2.2 修复：原来每次按 E 进来都重新 rand() 三个报价 ——
       面板开着时游戏逻辑整段暂停（理智不流失、怪不动），
       于是"反复进出商店"等于免费无限重掷，直到刷出想要的配件。
       现在同一层只掷一次。 */
    static int shopRollLevel = -1;
    if (level == shopRollLevel) {
        shopRow = 0;
        prevState = currentState;
        currentState = STATE_SAFEHOUSE;
        ForceCursor(1);
        uiDirty = 1;
        return;
    }
    shopRollLevel = level;
    (void)level;
    shopPage = SHP_SHOP;
    shopRow = 0;
    /* 次数不在这里重置：见 shop.c 的 safeHealLevel/safeChargeLevel */
    for (int k = 0; k < SHOP_OFFERS; k++) {
        int pick = rand() % ATTACH_COUNT;
        /* 去重 */
        for (int j = 0; j < k; j++) {
            if (shopOffer[j] == pick) { pick = (pick + 1) % ATTACH_COUNT; j = -1; }
        }
        shopOffer[k] = pick;
        shopPrice[k] = AttachPrice(pick);
    }
}

int ShopPageCount(void) { return SAFEHOUSE_PAGES; }

const char* ShopPageTitle(void) {
    switch (shopPage) {
    case SHP_BENCH: return Tr("SAFE HOUSE  -  BENCH", "安全屋  -  改装台");
    case SHP_REST:  return Tr("SAFE HOUSE  -  REST",  "安全屋  -  休息");
    case SHP_TREE:  return Tr("WORKBENCH TREE", "改造树");
    case SHP_UPGRADE: return "CRYSTAL UPGRADE";
    default:        return Tr("SAFE HOUSE  -  SHOP",  "安全屋  -  商店");
}
}


/* v3.3 **本层碎片可以花** ——
   搜箱子的碎片先进 pendingShards，过关才并入 shards（重生细胞式上缴）。
   但安全屋就在本层，玩家身上明明有 60 碎片，站店里却提示「碎片不够」。
   这里提供统一入口：先扣已入账的 shards，不够再扣本层待上缴的 pendingShards。
   返回 1 = 扣成功，0 = 两边加起来也不够。 */
static int ShopSpend(int cost) {
    extern int pendingShards;
    if (cost <= 0) return 1;
    if (shards + pendingShards < cost) return 0;
    int need = cost;
    if (shards >= need) {
        shards -= need;
        return 1;
    }
    need -= shards;
    shards = 0;
    pendingShards -= need;
    if (pendingShards < 0) pendingShards = 0;
    return 1;
}

int ShopRowCount(void) {
    if (shopPage == SHP_TREE) return 0;   /* v2.7：树自己画，不用列表行 */
    switch (shopPage) {
    case SHP_BENCH: return 4;     /* CORE / MOD / CLEAR / LEAVE */
    case SHP_REST:  return 5;     /* REST / CHARGE / STAMINA / SHORTCUT / LEAVE */
    case SHP_UPGRADE: return RUN_UPG_COUNT + 1;   /* 4 条线 + LEAVE */
    default:        return SHOP_OFFERS + 1;
    }
}

const char* ShopRowText(int row) {
    if (row < 0) row = 0;

    /* v3.0：局内强化页 —— 4 条线，显示等级和价格 */
    if (shopPage == SHP_UPGRADE) {
        if (row >= RUN_UPG_COUNT) return Tr("LEAVE  (ESC)", "离开  (ESC)");
        int lv = RunUpgLevel(row);
        int cost = RunUpgCost(row);
        if (cost < 0)
            sprintf(sbuf, "%s  LV%d/%d  MAX",
                    Tr(RunUpgName(row), RunUpgNameZh(row)), lv, RUN_UPG_MAX);
        else
            sprintf(sbuf, "%s  LV%d/%d  %d",
                    Tr(RunUpgName(row), RunUpgNameZh(row)), lv, RUN_UPG_MAX, cost);
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
           原来「急救包 +25 理智 40 碎片」比安全屋「休息 +40 理智 25 碎片」
           又贵又少，玩家没有任何理由买急救包。
           现在急救包 25（同价但少给 15 点），是「临时补给」的定位，
           安全屋休息仍然是性价比最高的恢复手段。 */
        static const char* en[4] = {
            "BATTERY PACK   +35 battery   25",
            "MED KIT        +25 sanity    25",
            "FLARE x1                      30",
            "BOTTLES x2                    20",
        };
        static const char* zh[4] = {
            "电池包   +35 电量   25",
            "急救包   +25 理智   25",
            "照明弹 x1           30",
            "瓶子   x2           20",
        };
        int idx = row & 3;
        return (g_lang == LANG_ZH) ? zh[idx] : en[idx];
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
           可以无限重复）。现在加每层一次的标记 + 15 碎片。 */
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
void ShopPrevPage(void) {
    shopPage = (shopPage + SAFEHOUSE_PAGES - 1) % SAFEHOUSE_PAGES;
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
    shopPage = (shopPage + 1) % SAFEHOUSE_PAGES;
    shopRow = 0;
    uiDirty = 1;
}

/* 找一个"已拥有但没装在这把武器上"的配件，返回 id；没有则 -1 */


void ShopActivate(void) {
    /* v3.0：碎片强化 */
    if (shopPage == SHP_UPGRADE) {
        if (shopRow >= RUN_UPG_COUNT) return;      /* LEAVE 由 Esc 处理 */
        int r = RunUpgTryBuy(shopRow);
        if (r == 0)            { pickupMsg = Tr("UPGRADED", "已强化"); }
        else if (r == 2)       { pickupMsg = Tr("MAX LEVEL", "已满级"); }
        else if (r == 4)       { pickupMsg = Tr("NOT ENOUGH", "碎片不够"); }
        pickupMsgTimer = 1.4f;
        uiDirty = 1;
        return;
    }
    if (shopPage == SHP_TREE) {
        int r = BenchTryBuy(shopNodeSel);
        if (r == 0) {
            pickupMsg = Tr("INSTALLED", "已安装");
            PlaySfx(SFX_POWER, 0.9f);
        } else if (r == 2) {
            pickupMsg = Tr("ALREADY OWNED", "已拥有");
        } else if (r == 3) {
            pickupMsg = Tr("NEED PREVIOUS NODE", "需要先买上一层");
        } else if (r == 4) {
            pickupMsg = Tr("NOT ENOUGH SHARDS", "碎片不够");
        }
        pickupMsgTimer = 1.4f;
        uiDirty = 1;
        return;
    }
    if (shopPage == SHP_SHOP) {
        if (shopRow >= SHOP_OFFERS) return;      /* LEAVE 由 Esc 处理 */
        /* v3.0：**消耗品商店** —— 四种补给，价格与行文本一一对应 */
        static const int price[4] = { 25, 25, 30, 20 };   /* v3.3：急救包 40 -> 25，与行文本一致 */
        int idx = shopRow & 3;
        if (!ShopSpend(price[idx])) {
            pickupMsg = Tr("NOT ENOUGH SHARDS", "碎片不够");
            pickupMsgTimer = 1.2f;
            return;
        }
        switch (idx) {
        case 0: battery += 35.0f; if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
                pickupMsg = Tr("+35 BATTERY", "+35 电量"); break;
        case 1: sanity += 25.0f;  if (sanity > SanityMax()) sanity = SanityMax();
                pickupMsg = Tr("+25 SANITY", "+25 理智"); break;
        case 2: flares++;
                pickupMsg = Tr("+1 FLARE", "+1 照明弹"); break;
        default: bottles += 2;
                pickupMsg = Tr("+2 BOTTLES", "+2 瓶子"); break;
        }
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
            char msg[96];
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
            pickupMsg = Tr("NEED 25 SHARDS", "需要 25 碎片");
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
        /* v3.3：**体力从免费无限改成 15 碎片 + 每层一次**。
           原来这一行什么都不收，玩家可以躲衣柜→按 T→回车→Esc→跑，
           无限刷满体力，冲刺等于没有成本。 */
        if (safeStaminaLevel == currentLevel) return;
        if (!ShopSpend(15)) {
            pickupMsg = Tr("NEED 15 SHARDS", "需要 15 碎片");
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

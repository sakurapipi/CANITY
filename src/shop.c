#include <stdio.h>
#include <stdlib.h>
#include "shop.h"
#include "game.h"
#include "entity.h"
#include "skills.h"
#include "lang.h"
#include "audio.h"

int shopPage = SHP_SHOP;
int shopRow = 0;
int shopOffer[SHOP_OFFERS] = {-1, -1, -1};
int shopPrice[SHOP_OFFERS] = {0, 0, 0};
int attachOwned[ATTACH_COUNT];

/* v2.1：改成按层记，反复开关面板不能刷新次数 */
static int safeHealLevel = -1;

/* v2.2：开局/换局清一次。原来这两个 static 永不重置，
   上一局在某层休息过，之后任何一局走到同一层都是「已休息」。 */

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
    default:        return Tr("SAFE HOUSE  -  SHOP",  "安全屋  -  商店");
    }
}

int ShopRowCount(void) {
    switch (shopPage) {
    case SHP_BENCH: return 4;     /* CORE / MOD / CLEAR / LEAVE */
    case SHP_REST:  return 4;     /* REST / CHARGE / SWITCH / LEAVE */
    default:        return SHOP_OFFERS + 1;
    }
}

const char* ShopRowText(int row) {
    if (row < 0) row = 0;

    if (shopPage == SHP_SHOP) {
        if (row >= SHOP_OFFERS) {
            return Tr("LEAVE  (ESC)", "离开  (ESC)");
        }
        int id = shopOffer[row];
        if (id < 0) return Tr("SOLD OUT", "已售出");
        if (attachOwned[id] > 0) return Tr("OWNED", "已拥有");
        const AttachDef* a = &attachDefs[id];
        /* v2.0：把配件数值写出来（原来只有名字，玩家花 260 碎片全靠猜） */
        sprintf(sbuf, "%s  [%s]  D%+.0f%%  C%+.0f%%  %d",
                Tr(a->nameKey, a->nameZh),
                (a->slot == 0) ? Tr("CORE", "核心") : Tr("MOD", "词条"),
                (a->dmgMul - 1.0f) * 100.0f,
                (a->costMul - 1.0f) * 100.0f,
                shopPrice[row]);
        return sbuf;
    }

    if (shopPage == SHP_BENCH) {
        if (row == 0 || row == 1) {
            int slot = row;
            int cur = WeaponAttachAt(curWeapon, slot);
            const char* slotName = (slot == 0) ? Tr("CORE", "核心") : Tr("MOD", "词条");
            if (cur < 0) {
                sprintf(sbuf, "%s: %s", slotName, Tr("(empty)", "（空）"));
            } else {
                sprintf(sbuf, "%s: %s", slotName, Tr(attachDefs[cur].nameKey, attachDefs[cur].nameZh));
            }
            return sbuf;
        }
        if (row == 2) return Tr("CLEAR BOTH SLOTS", "清空两个槽位");
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
        /* v2.0：换武器。原来武器开局选完就锁死 24 层。 */
        extern int curWeapon;
        static const char* wname[WPN_COUNT] = { "CRANKWRENCH", "FLARE PISTOL", "SIGIL" };
        sprintf(sbuf, "%s  [%s]", Tr("SWITCH WEAPON", "换武器"), wname[curWeapon % WPN_COUNT]);
        return sbuf;
    }
    if (row == 3) {
        /* v2.0 SHORTCUT：每局一次，Boss 层不可跳 */
        extern int shortcutUsed, bossLevel;
        if (!SkillShortcutAvailable()) return Tr("SHORTCUT (locked)", "跳层（未解锁）");
        if (shortcutUsed)              return Tr("SHORTCUT (used)", "跳层（已用）");
        if (bossLevel)                 return Tr("SHORTCUT (boss floor)", "跳层（首领层）");
        return Tr("SHORTCUT   skip this floor", "跳层   跳过本层");
    }
    return Tr("LEAVE  (ESC)", "离开  (ESC)");
}

void ShopMoveSel(int delta) {
    int n = ShopRowCount();
    shopRow = (shopRow + delta + n) % n;
    uiDirty = 1;
}

void ShopNextPage(void) {
    shopPage = (shopPage + 1) % SAFEHOUSE_PAGES;
    shopRow = 0;
    uiDirty = 1;
}

/* 找一个"已拥有但没装在这把武器上"的配件，返回 id；没有则 -1 */


void ShopActivate(void) {
    if (shopPage == SHP_SHOP) {
        if (shopRow >= SHOP_OFFERS) return;      /* LEAVE 由 Esc 处理 */
        int id = shopOffer[shopRow];
        if (id < 0) return;
        if (attachOwned[id] > 0) {          /* v2.0：非消耗品不重复卖 */
            pickupMsg = Tr("ALREADY OWNED", "已拥有");
            pickupMsgTimer = 1.2f;
            return;
        }
        if (shards < shopPrice[shopRow]) {
            pickupMsg = Tr("NOT ENOUGH SHARDS", "碎片不够");
            pickupMsgTimer = 1.2f;
            return;
        }
        shards -= shopPrice[shopRow];
        attachOwned[id]++;
        shopOffer[shopRow] = -1;
        PlaySfx(SFX_POWER, 0.8f);
        pickupMsg = Tr("PURCHASED", "已购买");
        pickupMsgTimer = 1.2f;
        uiDirty = 1;
        return;
    }

    if (shopPage == SHP_BENCH) {
        if (shopRow == 0 || shopRow == 1) {
            int slot = shopRow;
            /* v2.0：按索引轮换该槽位的所有已拥有配件（原来只会在前两件之间来回） */
            {
                static int benchIdx[2] = {0, 0};
                int list[ATTACH_COUNT], n = 0;
                for (int i = 0; i < ATTACH_COUNT; i++)
                    if (attachOwned[i] > 0 && attachDefs[i].slot == slot) list[n++] = i;
                if (n == 0) {
                    WeaponAttachRemove(curWeapon, slot);
                    pickupMsg = Tr("REMOVED", "已卸下");
                } else {
                    benchIdx[slot] = (benchIdx[slot] + 1) % (n + 1);   /* 0 = 空 */
                    if (benchIdx[slot] == 0) {
                        WeaponAttachRemove(curWeapon, slot);
                        pickupMsg = Tr("REMOVED", "已卸下");
                    } else {
                        WeaponAttachAdd(curWeapon, list[benchIdx[slot] - 1]);
                        pickupMsg = Tr("INSTALLED", "已安装");
                    }
                }
            }
            pickupMsgTimer = 1.2f;
            PlaySfx(SFX_POWER, 0.6f);
            uiDirty = 1;
            return;
        }
        if (shopRow == 2) {
            WeaponAttachRemove(curWeapon, 0);
            WeaponAttachRemove(curWeapon, 1);
            pickupMsg = Tr("SLOTS CLEARED", "已清空");
            pickupMsgTimer = 1.2f;
            uiDirty = 1;
            return;
        }
        return;
    }

    /* REST —— v2.0：改成收费（原来免费 +40，8 个安全屋白送 320 理智，
       让理智彻底不像计时器） */
    if (shopRow == 0) {
        if (safeHealLevel == currentLevel) return;
        if (shards < SAFE_REST_COST) {
            pickupMsg = Tr("NEED 25 SHARDS", "需要 25 碎片");
            pickupMsgTimer = 1.2f;
            uiDirty = 1;
            return;
        }
        shards -= SAFE_REST_COST;
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
        extern int curWeapon;
        curWeapon = (curWeapon + 1) % WPN_COUNT;
        pickupMsg = Tr("WEAPON SWAPPED", "已换武器");
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
}

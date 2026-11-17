/* Portraits are baked from the same geometry as the live monster. */
#include "common.h"
#include "evolution_art.h"
#include "sprites.h"
#include "threat_tier.h"
#include "render.h"
#include "ui.h"
#include "present_api.h"
#include "lang.h"
void DrawEvolutionPortrait(int bestiary, int variant, int tier, RECT box, int identified) {
    if (winW <= 0 || winH <= 0 || box.right <= box.left || box.bottom <= box.top)
        return;
    int id = SpritesEvolutionPortrait(bestiary, variant, tier);
    Sprite *model = SpriteGet(id);
    if (!model || model->bx1 < model->bx0 || model->by1 < model->by0)
        return;
    BlendRectWin(box.left, box.top, box.right, box.bottom, 0x0C151D, 1);
    unsigned color = ThreatTierColor(tier);
    int left = box.left * renderW / winW, top = box.top * renderH / winH;
    int width = (box.right - box.left) * renderW / winW,
        height = (box.bottom - box.top) * renderH / winH;
    int sw = model->bx1 - model->bx0 + 1, sh = model->by1 - model->by0 + 1;
    if (width < 2 || height < 2)
        return;
    float scale = fminf(width * .87f / sw, height * .90f / sh);
    int x = left + (width - (int)(sw * scale)) / 2 - (int)(model->bx0 * scale);
    int y = top + (height - (int)(sh * scale)) / 2 - (int)(model->by0 * scale);
    for (int row = 0; row < 5; row++) {
        int yy = box.top + (box.bottom - box.top) * (row + 1) / 6;
        BlendRectWin(box.left, yy, box.right, yy + 1, 0x58707D, .13f);
    }
    if (identified)
        DrawSprite(id, x, y, (int)(model->w * scale), (int)(model->h * scale), 1, 1, 0, 0);
    else
        DrawSpriteTint(id, x, y, (int)(model->w * scale), (int)(model->h * scale), 1, 1, 0, 0,
                       0x22333C, 1);
    BlendRectWin(box.left, box.bottom - 2, box.right, box.bottom, color, 1);
}
const char *EvolutionGuideText(int id, int tier) {
    static const char *en[7][4] = {
        {"Blind; follows sound, never sight.",
         "Base + one delayed 0.9s recheck of the old sound point.",
         "Recheck + hearing up to 5% farther, within the original cap.",
         "Recheck + bounded hearing; after 1.2s shares one approximate sound point with one peer. "
         "Never shares live player position."},
        {"Tracks light; turn it off or use cover.",
         "Remembers the last visible light point for 0.9s; does not track your new route.",
         "Afterimage + at most 5% more flashlight range; absolute cap 20 cells. Thrown lights "
         "unchanged.",
         "After 1.8s continuous light: 0.90s warning, direction-locked 1.8-cell dash, 0.8s "
         "recovery / 14s cooldown. Cut light to cancel windup."},
        {"0.8s windup; timed web control; wrench disperses it for 30s. No damage.",
         "A missed shot leaves a 2s ground trace: -10% movement, no hard control.",
         "Ground trace; normal shot cooldown 7.5s. Web escape rules unchanged.",
         "At most every third attack: 1s warning, two 2.5s edge anchors (-15% speed). No wall, no "
         "extra hard control."},
        {"Three thefts per run: 20/30/40%; recover actual loot with a wrench.",
         "Once per theft: 0.18s bag feint, then an alternate legal escape branch.",
         "Feint + escape speed 2.34 instead of 2.30; theft rate and hit requirements unchanged.",
         "After its feint, drops broken fake coin glints for 2.5s. They cannot be picked up, "
         "scored or sold."},
        {"Checks an old hide only after losing direct sight; the original cupboard grab remains "
         "separate.",
         "Checks up to two old hide points / appearance, 1.2s each; memory lasts 12s.",
         "Old-hide check lasts 1.5s; normal movement, 180s trigger and 35s cooldown unchanged.",
         "Once per appearance: walks away 2-3 cells, pauses 0.6s, then rechecks the remembered "
         "hide. No teleport or new grab damage."},
        {"Powered patrol; movement reveals you; at most two exit blockades.",
         "At a patrol turn: a 0.9s lantern sweep / 20s cooldown. Standing still and cover still "
         "work.",
         "Sweep + blockade 2s longer, hard cap 24s. No extra blockade or chase speed.",
         "Once per blockade: 1.2s lantern warning, narrow scan removes 10 stamina only while "
         "moving. Stop, take cover or jam the windup."},
        {"Search to 60% provokes a bite. Wrench reveals safely; escape its chase. Cannot be "
         "killed.",
         "Nearby disguised chest makes one faint coin rattle; never becomes harmless by "
         "observation.",
         "Rattle + angry chase timeout 8.4s. Distance, hiding and stun escape remain effective.",
         "Once per angry chase: 0.95s tongue warning, fixed direction / 8 stamina, then 0.5s "
         "recovery. No sanity or inventory penalty from the tongue."}};
    static const char *zh[7][4] = {
        {"全盲，只调查声音；不会靠视觉定位。",
         "基础行为＋原声源到达后，额外延迟复查一次，持续0.9秒。",
         "保留复查；有效听觉最多增加5%，仍受原传播上限约束。",
         "保留复查与限幅听觉；延迟1.2秒向一个同伴分享一个模糊声源点，不共享玩家实时位置。"},
        {"追随光源，关灯与遮挡仍有效。", "保留最后可见光点0.9秒，不读取关灯后的新位置。",
         "残光记忆＋主手电感光距离最多增加5%，绝对上限20格，投掷光球不变。",
         "持续照射1.8秒后，前摇0.90秒，锁方向冲1.8格，后摇0.8秒，冷却14秒；前摇时关灯可取消。"},
        {"前摇0.8秒，定时蛛网控制；扳手驱散30秒，不造成伤害。",
         "落空留下2秒丝痕，移速降低10%，不触发强制束缚。",
         "保留丝痕；吐丝冷却由8秒降至7.5秒，挣脱规则不变。",
         "每三次攻击至多一次双锚：前摇1秒，边缘锚点持续2.5秒，减速15%；不堵路、不追加硬控。"},
        {"每局至多盗窃三次，比例20%、30%、40%；扳手可追回真实赃款。",
         "每次盗窃一次假动作，停顿0.18秒后走另一条合法逃跑分支。",
         "保留假动作，逃速2.30变为2.34；偷钱比例与扳手命中要求不变。",
         "假动作后留下一次破损假币光斑，2.5秒消失；不能拾取、计分或结算。"},
        {"只有失去直接视线才检查旧藏点，与原有拉柜攻击分开。",
         "每次出场至多回查两个旧藏点，每次1.2秒，痕迹记忆12秒。",
         "回查停留1.5秒；移速、180秒触发和35秒退场冷却不再加码。",
         "同次出场至多佯退一次：走出2至3格，停0.6秒再回查旧柜；不瞬移，不增加拉柜伤害。"},
        {"通电后巡逻，移动才暴露；每局至多堵门两次。",
         "折返点提灯慢扫0.9秒，冷却20秒；站定或遮挡仍有效。",
         "保留扫视；堵门加2秒，最高24秒；不增加堵门次数与追击速度。",
         "每次堵门一次巡检脉冲，前摇1.2秒，只对移动目标窄扇区扣10体力；站定、绕墙或脉冲弹可反制。"},
        {"搜到60%会咬，扳手可免初始咬伤逼现形；不可击杀，可甩掉追击。",
         "伪装箱近距发出一次假财细响；观察不会自动使其无害。",
         "保留假财声，怒化追击8.4秒；拉开距离、藏身和硬直脱离仍有效。",
         "一次怒化至多一次舌鞭：前摇0.95秒，锁定方向扣8体力，后摇0."
         "5秒；不扣理智，不触发丢弃背包。"}};
    if (id < 0 || id >= 7)
        return "";
    if (tier < 0 || tier > 3)
        tier = 0;
    return Tr(en[id][tier], zh[id][tier]);
}

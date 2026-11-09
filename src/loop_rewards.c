/* Run draft selection and chained reward lifecycle. */
#include "common.h"
#include "wallet.h"
#include "run_rewards.h"
#include "loop_api.h"
#include "loop_private.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "entity.h"
#include "state.h"
#include "render.h"
#include "skills.h"
#include "relics.h"
#include "build_tags.h"
#include "audio.h"
#include "lang.h"

int skillPickOptions[INRUN_CHOICES] = {-1, -1, -1};
int skillPickSel = 0;
int pickMode = 0;
static int pendingSkillDraft=0;      /* v2.0：0 = 局内技能，1 = 遗物 */
int skillPickRound = 1;
int skillPickTotal = 1;

void RewardScheduleForLevel(int level) {
    int relic = (level % 3 == 0);
    int skill = (level % 2 == 0);
    pendingSkillDraft = (relic && skill) ? 1 : 0;
    skillPickTotal = (relic && skill) ? 2 : 1;
    skillPickRound = 1;
    if (relic) {
        if (!OpenRelicPick() && skill) {
            /* 遗物池空了：不能把这一层的技能奖励一起吞掉 */
            skillPickTotal = 1;
            skillPickRound = 1;
            OpenSkillPick();
        }
    } else if (skill) {
        OpenSkillPick();
    }
}

/* ==================================================================
 * 2026-11-06（v2.4.1 第 8 轮）：**抽卡专用 PRNG**
 * ------------------------------------------------------------------
 * 地图 / 怪物 / 箱子 / 抽卡现在共用 rand()。
 * 一旦抽卡里多插几次 rand()，整个地图生成序列会跟着漂 ——
 * 之前用 map_gen_dump 攒的死路/模板覆盖率证据会全部对不上种子。
 *
 * 所以抽卡用自己的 xorshift 状态，存在 SaveBlob 末尾（v34 追加）。
 * 读档时状态为 0 → 重新播种。
 * ================================================================== */


/* 这张卡现在拿了有没有用。目前只排除"效果尚未实现"的新卡，
   避免出了拿不到东西的卡（v2.4.1 只做了 3 张新卡的效果）。 */
static int CardUsableNow(int irId) {

    return BuildCardImplemented(irId);
}

/* ---- v1.8 局内技能三选一 ---- */
void OpenSkillPick(void) {
    int pool[INRUN_COUNT];
    int n = 0;
    for (int i = 0; i < INRUN_COUNT; i++)
        if (RunSkillPickable(i, currentLevel) && CardUsableNow(i)) pool[n++] = i;
    if (n <= 0) return;

    /* ==================================================================
     * 2026-11-06（v2.4.1 第 8 轮）：**有方向的随机**
     * ------------------------------------------------------------------
     * 用户要求："局内加点要有明确的流派"，但也不能失去 Roguelike 的
     * 不可预测性。所以三张卡按结构给：
     *   ① 主流派 1 张
     *   ② 异流派 1 张（鼓励混搭）
     *   ③ 通用或稀有 1 张
     * 还没成型（BUILD_NONE）时退化成"三桶各抽一张"。
     *
     * 全部走 RunRngNext()，**不碰 rand()**。
     * ================================================================ */
    {


        BuildTag dom = BuildDominant();

        int bDom[INRUN_COUNT], nDom = 0;
        int bOth[INRUN_COUNT], nOth = 0;
        int bUni[INRUN_COUNT], nUni = 0;
        for (int i = 0; i < n; i++) {
            BuildTag t = BuildTagOfSkill(pool[i]);
            if (t == BUILD_NONE) bUni[nUni++] = pool[i];
            else if (dom != BUILD_NONE && t == dom) bDom[nDom++] = pool[i];
            else bOth[nOth++] = pool[i];
        }

        int out[INRUN_CHOICES];
        int on = 0;

        /* 从桶里取一张（取走就删掉，避免重复） */
        #define TAKE_FROM(arr, cnt) \
            do { if ((cnt) > 0) { \
                     int _k = (int)(RunRngNext() % (unsigned)(cnt)); \
                     out[on++] = (arr)[_k]; \
                     (arr)[_k] = (arr)[--(cnt)]; \
                 } } while (0)

        if (dom != BUILD_NONE) {
            TAKE_FROM(bDom, nDom);      /* ① 主流派 */
            TAKE_FROM(bOth, nOth);      /* ② 异流派 */
            TAKE_FROM(bUni, nUni);      /* ③ 通用/稀有 */
        } else {
            TAKE_FROM(bUni, nUni);
            TAKE_FROM(bOth, nOth);
            TAKE_FROM(bDom, nDom);
        }

        /* 不足就用剩下的补齐（池子被稀有度/等级筛过，可能不够三张） */
        while (on < INRUN_CHOICES) {
            if (nUni > 0) { TAKE_FROM(bUni, nUni); continue; }
            if (nOth > 0) { TAKE_FROM(bOth, nOth); continue; }
            if (nDom > 0) { TAKE_FROM(bDom, nDom); continue; }
            break;
        }
        #undef TAKE_FROM

        /* 规则：不能三张全是即时补给 —— 那样这一轮等于没得选。 */
        {
            int allInstant = (on == INRUN_CHOICES);
            for (int k = 0; k < on; k++)
                if (inRunDefs[out[k]].kind != IRK_INSTANT) { allInstant = 0; break; }
            if (allInstant) {
                /* 用一张非即时卡换掉最后一张（如果池里还有） */
                for (int i = 0; i < n; i++) {
                    int id = pool[i];
                    int used = 0;
                    for (int k = 0; k < on; k++) if (out[k] == id) used = 1;
                    if (used) continue;
                    if (inRunDefs[id].kind == IRK_INSTANT) continue;
                    out[on - 1] = id;
                    break;
                }
            }
        }

        for (int k = 0; k < INRUN_CHOICES; k++)
            skillPickOptions[k] = (k < on) ? out[k] : -1;
    }
    skillPickSel = 0;
    pickMode = 0;
    prevState = currentState;
    currentState = STATE_SKILL_PICK;
    ForceCursor(1);
    uiDirty = 1;
}

/* v2.0：遗物三选一（复用同一块界面） */
/* 2026-11-06 审计补：**返回值**。
   原来 n <= 0 时直接 return，而 RewardScheduleForLevel 里
   "遗物 + 技能"是链式的 —— 遗物池空了，技能奖励也被一起吞掉。
   现在返回 1 = 真的开了界面，0 = 没得选。 */
int OpenRelicPick(void) {
    int pool[RELIC_COUNT];
    int n = 0;
    for (int i = 0; i < RELIC_COUNT; i++) {
        if (relicOwned[i] >= 3) continue;      /* 同类最多 3 层 */
        pool[n++] = i;
    }
    if (n <= 0) return 0;
    for (int k = 0; k < RELIC_CHOICES; k++) {
        if (n <= 0) { skillPickOptions[k] = -1; continue; }
        int pick = (int)(RunRngNext() % (unsigned)n);   /* 审计·中 9：不碰全局 rand */
        skillPickOptions[k] = pool[pick];
        pool[pick] = pool[--n];
    }
    skillPickSel = 0;
    pickMode = 1;
    prevState = currentState;
    currentState = STATE_SKILL_PICK;
    ForceCursor(1);
    uiDirty = 1;
    return 1;
}

static void ApplyInstantSkill(int id) {
    switch (id) {
    case IR_BATTERYPACK:
        battery += 45.0f;
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        break;
    case IR_BOTTLE:
        if (bottles < BottleCap()) bottles++;
        break;
    case IR_FLARE: flares++; break;
    case IR_MEDKIT:
        sanity += 25.0f;
        if (sanity > SanityMax()) sanity = SanityMax();
        break;
    case IR_SHARDBAG: WalletAddRun(40); break;
    /* v2.0 新增 */
    case IR_FIELDREPAIR:
        battery += 35.0f;
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        sanity -= 5.0f; if (sanity < 0.0f) sanity = 0.0f;
        break;
    case IR_NERVESHOT:
        sanity += 15.0f; if (sanity > SanityMax()) sanity = SanityMax();
        battery -= 10.0f; if (battery < 0.0f) battery = 0.0f;
        break;
    case IR_PACKRAT:
        if (bottles < BottleCap()) bottles++;
        if (bottles < BottleCap()) bottles++;
        WalletAddRun(30);
        break;
    case IR_OILCAN:
        battery += 25.0f;
        if (battery > (float)BatteryMax()) battery = (float)BatteryMax();
        stamina += 20.0f;
        if (stamina > StaminaMax()) stamina = StaminaMax();
        break;
    case IR_LUCKYFUSE:
        fusesHeld++;
        break;
    case IR_TIMESTOP: {
        /* v2.0：TIME STOP 原来是空卡（24 张里唯一一张没 case 的） */
        MonsterStunAll(8.0f);
        break;
    }
    case IR_STATICFIELD: {
        /* 大招：瘫痪本层所有怪 6 秒 */
        MonsterStunAll(6.0f);
        for (int i = 0; i < LEECH_MAX; i++) if (leeches[i].active) leeches[i].repelTimer = 6.0f;
        sanity -= 8.0f;
        if (sanity < 0.0f) sanity = 0.0f;
        break;
    }
    case IR_MISDIRECT: {
        /* 大招：把最近的怪瞬移走 */
        if (Hunter0()->active) {
            int x = 1 + rand() % (MAP_WIDTH - 2), y = 1 + rand() % (MAP_HEIGHT - 2);
            Hunter0()->x = x + 0.5f; Hunter0()->y = y + 0.5f;
            Hunter0()->state = 0; Hunter0()->pathLen = 0; Hunter0()->pathIdx = 0;
        }
        decoySelfTimer = 3.0f;
        break;
    }
    default: break;
    }
}

void CloseSkillPick(void) {
    if(currentState!=STATE_SKILL_PICK || skillPickSel<0 || skillPickSel>=INRUN_CHOICES || skillPickOptions[skillPickSel]<0)return;
    int id = skillPickOptions[skillPickSel];
    if (id >= 0) {
        if (pickMode == 1) {
            RelicTake(id);
            PlaySfx(SFX_POWER, 0.9f);
        } else {
            RunSkillTake(id);
            ApplyInstantSkill(id);
        }
    }
    int wasRelic=pickMode;
    pickMode = 0;
    for (int k = 0; k < INRUN_CHOICES; k++) skillPickOptions[k] = -1;
    currentState = prevState==STATE_SKILL_PICK?STATE_PLAYING:prevState;
    isMapOpen=0;mapFullScreen=0;LoopClearInputEdges();LoopCancelTransientInput();
    if(pixels)memset(pixels,0,(size_t)renderW*renderH*sizeof(*pixels));
    if (wasRelic && pendingSkillDraft) {
        /* 链式第二次：轮次标记成 2，界面显示"（2/2）" */
        pendingSkillDraft = 0;
        skillPickRound = 2;
        if (skillPickTotal < 2) skillPickTotal = 2;
        OpenSkillPick();
        return;
    }
    ForceCursor(currentState!=STATE_PLAYING);
    firstMouseMove=1;
    /* 鼠标点的卡：回到局内时左键还按着，不清就会立刻开始蓄力、
       松手朝面前的墙扔一个道具（还带 14 半径噪声把怪引来）。 */
    resetInputState = 1;
    uiDirty = 1;
}

void RewardResetPending(void) { pendingSkillDraft = 0; }

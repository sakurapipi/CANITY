/* Player stages; callers preserve pause/lock/transition ordering. */
#include "common.h"
#include "run_economy.h"
#include "calibration.h"
#include "player_tick.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "loop_api.h"
#include "entity.h"
#include "state.h"
#include "skills.h"
#include "bench.h"
#include "relics.h"
#include "daily.h"
#include "campaign.h"
#include "difficulty.h"
#include "wallet.h"
#include "audio.h"
#include "plat.h"
#include "lockbox.h"
#include "control_effect.h"
#include "collapse.h"
#include "hatch.h"
#include "upgrade_run.h"
#include "lang.h"

static int secondWindUsed = 0;
float secondWindTimer = 0.0f;   /* v3.0 二次呼吸：体力归零后的宽限秒数 */
float ghostStillT = 0.0f;
float pawnCd = 0.0f;
float retaliateT = 0.0f;
int   counterKillUsed = 0;

/* A drain multiplier must never turn resource consumption into generation. */
static float PositiveDrainFactor(float value){return isfinite(value) && value>0?value:1;}

int PlayerTickVitals(float dt,int inSafe){
    if(!(dt>0) || !isfinite(dt) || calibActive)return 1;
    if(!isfinite(sanity))sanity=SanityMax();
    if(!isfinite(battery))battery=0;
    float drain=SanityDrainRate()*PositiveDrainFactor(SkillCalmMul())*
        PositiveDrainFactor(modSanityMul)*(1+fmaxf(0,FxStrength(FX_DRAIN))*1.5f)*
        PositiveDrainFactor(RelicSanityMul())*PositiveDrainFactor(ColdBloodMul())*
        PositiveDrainFactor(BenchSanityDrainMul());
    if(hiddenIn<0 && Hunter0()->active && Hunter0()->state==1){
        float range=flashlightOn?MONSTER_SIGHT:MONSTER_SIGHT_DARK;
        if(Dist2(posX,posY,Hunter0()->x,Hunter0()->y)<range*range)drain+=SEEN_SANITY_EXTRA;
    }
    int composed=SkillOwned(SK_COUNTER_COMPOSE) && isfinite(retaliateT) && retaliateT>0 && retaliateT<=10;
    if(!inSafe && hiddenIn<0 && !composed){
        float ghostMul=SkillGhostBreathRank()>0 && ghostStillT>2 && ghostStillT<=22?.5f:1;
        float siphonMul=SkillPwrSiphonActive() && !flashlightOn && battery<BatteryMax()*.25f?SkillPwrSiphonSanityMul():1;
        float loss=fmaxf(SanityDrainRate()*.05f,drain)*PositiveDrainFactor(siphonMul)*DiffSanityDrainMul()*ghostMul*dt;
        if(HasKeystone(SK_METAB)){
            /* Partial conversion, not permanent immunity while a generator is running. */
            float converted=loss*.5f,available=fmaxf(0,battery)/META_COST;
            float paid=fminf(converted,available);
            battery=fmaxf(0,battery-paid*META_COST);
            loss=loss-converted+(converted-paid)*META_STARVE_MUL;
        }
        CalibAccumulateDrain(loss);sanity-=loss;
    }
    if(SkillRaiderPawnOwned() && pawnCd<=0 && sanity<30){
        int cost=200*(currentLevel>0?currentLevel:1);
        if(WalletSpendBank(cost)){
            sanity=fminf(SanityMax(),sanity+20);pawnCd=30;PlaySfx(SFX_PICKUP,.5f);
            pickupMsg=Tr("PAWNED GOLD FOR SANITY","典当：金币换理智");pickupMsgTimer=1.6f;
        }
    }
    if(hiddenIn>=0){
        if(!flashlightOn)battery=fminf((float)BatteryMax(),battery+HIDE_BATTERY_REGEN*dt);
        sanity=fminf(SanityMax(),sanity+HIDE_SANITY_REGEN*dt);
    }
    lowSanitySeconds+=(sanity<=1)?dt:0;
    if(exitChoiceOpen && sanity<1)sanity=1;
    battery=fmaxf(0,fminf((float)BatteryMax(),battery));
    sanity=fminf(SanityMax(),sanity);
    return 1;
}

void PlayerTickBattery(float dt){
    if(!(dt>0) || !isfinite(dt) || calibActive)return;
    if(!isfinite(battery))battery=0;
    if(!flashlightOn && hiddenIn<0 && SkillPwrSiphonActive()){
        float threshold=BatteryMax()*.25f;
        if(battery<threshold)battery=fminf(threshold,battery+SkillPwrSiphonAdd()*dt);
    }
    if(flashlightOn && !CampaignFlashlightLocked()){
        float rate=BatteryDrainRate()*PositiveDrainFactor(RunBatteryDrainMul())*PositiveDrainFactor(SkillGhostStepDrainMul());
        battery-=rate*dt;
    }
    battery=fmaxf(0,fminf((float)BatteryMax(),battery));
    if(battery<=0)flashlightOn=CampaignFlashlightLocked()?1:0;
}

void PlayerResetSprintGrace(void) {
    secondWindUsed = 0;
    secondWindTimer = 0.0f;
}

void PlayerTickSprintGrace(float dt, int wantSprint) {
    if (!HasKeystone(SK_LASTRESORT)) {
        PlayerResetSprintGrace();
        return;
    }
    /* Recovery must be checked outside the empty-stamina trigger branch. */
    if (stamina > StaminaMax() * 0.5f) secondWindUsed = 0;
    if (!secondWindUsed && stamina <= 0.0f && wantSprint) {
        secondWindTimer = TUNE_SECOND_WIND_SECONDS;
        secondWindUsed = 1;
    }
    if (secondWindTimer > 0.0f && dt > 0.0f) {
        secondWindTimer -= dt;
        if (secondWindTimer < 0.0f) secondWindTimer = 0.0f;
    }
    /* A grace period permits sprinting; it never forces a released sprint key. */
}

PlayerFrameInput PlayerPrepareMovement(float dt) {

    int wantSprint = ActionIsDown(ACT_SPRINT);
    PlayerTickSprintGrace(dt, wantSprint);
    int inF = ActionIsDown(ACT_MOVE_FWD) - ActionIsDown(ACT_MOVE_BACK);
    int inS = ActionIsDown(ACT_MOVE_RIGHT) - ActionIsDown(ACT_MOVE_LEFT);
    /* ==================================================================
     * 2026-11-06（用户要求）：**被爆脸怪抓住后 10 秒动不了**
     * 在读完输入之后直接清零，比在每个调用点加判断安全 ——
     * 后面还有冲刺/贴墙滑动等多条路径，漏一处就能溜。
     * ================================================================ */
    {

        /* 2026-11-06 审计补：**任何控制生效时都不能动**。
           原来只判 jumperControlT（抱脸虫的 10 秒），
           于是蛛网虽然"抓住了玩家"，却完全不限制移动 —— 效果等于没有。 */
            /* 审计·中 6：崩塌黑屏期间也不能走。
           原来 CollapseTick 的"吞输入"放在移动**之后**，
           0.8 秒淡出里玩家照样能挪两步，重掷后还得靠 CollapseUnstickPlayer
           去补救"被埋进新地形"。 */
        if (jumperControlT > 0.0f || ControlActive() || CollapseBusy()) { inF = 0; inS = 0; }
    }
    /* v3.0：藏在柜子里不能动（但可以转头偷看） */
    if (hiddenIn >= 0) { inF = 0; inS = 0; }
    int moving = (inF != 0 || inS != 0);
    /* ==================================================================
     * 2026-11-08（第 7 轮）：**静止计时** —— 幽灵流派四个节点共用
     * ------------------------------------------------------------------
     * 玩家原话「流派感不明显」：30 个节点里 25 个是倍率，没有一个
     * 改变"什么时候该站着不动"。这四个把静止变成战术。
     * ================================================================ */
    if (moving) ghostStillT = 0.0f;
    else if (ghostStillT < 99.0f) ghostStillT += dt;
    /* v2.1：走路相位，手持模型和准星都跟着动 */
    bobPhase += dt * (sprinting ? 11.0f : (moving ? 7.0f : 1.6f));
    if (bobPhase > 6283.0f) bobPhase -= 6283.0f;

    /* v3.0：二次呼吸宽限期内，体力为 0 也允许冲刺 */
    sprinting = (wantSprint && moving && (stamina > 1.0f || secondWindTimer > 0.0f));
    if (sprinting) {
        sprintedThisLevel = 1;  /* 标记本局冲刺过 */
        sprintedAnyFloor = 1;   /* v3.3：跨层不清零，SILENT SHADOW 用 */
        stamina -= STAMINA_DRAIN * BenchSprintCostMul() * RunUpgStaminaMul() * dt;   /* v3.0 体能强化 */
        if (stamina < 0.0f) stamina = 0.0f;
        staminaIdle = 0.5f;
    } else if (staminaIdle > 0.0f) {
        staminaIdle -= dt;
    } else if (stamina < StaminaMax()) {
        /* v3.0：藏在藏身点里体力回得更快（×3）—— 这是藏身点的主要收益 */
    stamina += STAMINA_REGEN * dt * ((hiddenIn >= 0) ? HIDE_STAMINA_REGEN_MUL : 1.0f);
        if (stamina > StaminaMax()) stamina = StaminaMax();
    }


    PlayerFrameInput input = { inF, inS, moving };
    return input;
}

void PlayerApplyMovement(float dt, const PlayerFrameInput* input) {
    int inF = input->forward, inS = input->strafe;
    /* 移速：基础值 + 永久升级 - 状态效果的减速（最多 -45%） */
    float slowMul = 1.0f - FxStrength(FX_SLOW) * 0.45f;
    float darkAdd = flashlightOn ? 0.0f : RunDarkSpeedAdd();
    /* CRANK WALK：有这个技能时摇电也允许移动，只是移速打折 */
    float crankPenalty = SkillCrankWalkPenalty();
    if(SkillOwned(SK_APPRENTICE))crankPenalty*=0.5f;
    float crankMul = cranking ? (1.0f - crankPenalty) : 1.0f;
    if (cranking) sprinting = 0;            /* 边摇边冲刺太离谱 */
    /* v3.3：**玩家自己也会被胶水粘住** —— 这是胶水的代价，
       扔之前要看清自己站哪，否则等于把自己钉住。 */
    /* v3.3：玩家侧也加体积半径，踩到边就减速（否则「有时候不生效」）。
       减速量 -45% → -20%，只做轻微提醒，不至于逃跑时被自己钉住。 */
    float glueSelf = 1.0f;
    {
        const float gr = GLUE_RADIUS + GLUE_BODY_PAD;
        for (int gi = 0; gi < MAX_GLUE; gi++) {
            if (!glues[gi].active) continue;
            if (Dist2(posX, posY, glues[gi].x, glues[gi].y) < gr * gr) {
                glueSelf = GLUE_PLAYER_MUL;
                break;
            }
        }
    }
    /* ==================================================================
     * v7 第 60 轮：**RunSpeedMul() 全工程 0 调用点**
     * ==================================================================
     * 局内技能的卡片明写「LIGHT FOOT: speed -3%」「IRON SKIN: speed -4%」，
     * 而 RunSpeedMul() 就是这两个代价的实现 —— 但从来没人调它。
     * 于是这两条"代价"一分钱都没收，玩家只拿到了好处。
     *
     * 接在这里：它只乘玩家的移动速度，不影响冲刺倍率本身。
     * ================================================================ */
    float sp = MOVE_SPEED * (1.0f + SkillSpeedAdd() + darkAdd) * slowMul * RelicSpeedMul() * glueSelf * dt * BenchMoveMul()
             * RunSpeedMul()
             * (sprinting ? SPRINT_MULT : 1.0f) * crankMul * (hatchSpeedT > 0.0f ? 1.25f : 1.0f);

        /* 2026-11-08（第 5 轮）：**战役移速倍率**（之前 6 个倍率全部零调用） */
    {

        sp *= CampaignSpeedMulNow();
    }
    /* 第 9 轮「以牙还牙」：受伤后 3 秒 +30% 移速 */
    if (retaliateT > 0.0f) sp *= 1.30f;
    /* 第 8 轮「金币即力量」：每个强度档 +1.5% 移速，上限 +20% */
    {


        if (SkillRaiderPowerOwned()) {
            float add = (float)GoldTier() * 0.015f;
            if (add > 0.20f) add = 0.20f;
            sp *= (1.0f + add);
        }
    }
    /* 第 7 轮「无形」：隐身期间移速 -25%（代价） */
    {

        if (SkillGhostFormOwned() && ghostStillT >= 3.0f) sp *= 0.75f;
    }
    if (sprinting && sp < 2.25f*dt) sp=2.25f*dt;
        {  playerMoveSpeedNow = dt>0.0f ? sp/dt : 0.0f; }   /* v8: 守门人据此判断在动/冲刺 */
    float perpX = -dirY, perpY = dirX;      /* 单位侧向，与 FOV 无关 */
    float dx = 0.0f, dy = 0.0f;
    /* v3.0：**锁盘打开时角色不能移动** ——
       用户实测「解锁的时候角色会乱动，导致这个功能失效」：
       移动条件里根本没检查 lockActive，锁盘开着 WASD 照样走，
       一走开箱子就失去目标，锁盘直接失效。 */
    if (lockActive) {
        dx = 0.0f; dy = 0.0f; sprinting = 0;
    }
    if (!lockActive && !holdingNow && (!cranking || crankPenalty > 0.0f)) {          /* 翻找/摇电时不接受移动输入 */
        if (inF > 0) { dx += dirX * sp;  dy += dirY * sp; }
        if (inF < 0) { dx -= dirX * sp;  dy -= dirY * sp; }
        if (inS > 0) { dx += perpX * sp; dy += perpY * sp; }
        if (inS < 0) { dx -= perpX * sp; dy -= perpY * sp; }
        if (inF != 0 && inS != 0) { dx *= 0.7071f; dy *= 0.7071f; }   /* 斜向不加速 */
    } else {
        sprinting = 0;
    }

    if (dx != 0.0f && !Blocked(posX + dx, posY)) posX += dx;
    if (dy != 0.0f && !Blocked(posX, posY + dy)) posY += dy;

}

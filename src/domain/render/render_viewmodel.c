#include "tactical_tools.h"
#include "protocol.h"
/* 2026-11-16：从 render.c 拆出的**第一人称手持模型**（域内解耦，行为不变）。 */
#include "threat_telegraph.h"
#include "room_theme.h"
#include "perception.h"
#include "room_material.h"
#include "collapse.h"
#include "echo_marks.h"
#include "item_effects.h"
#include "phase.h"
#include "upgrade_run.h"
#include "warden.h"
#include "wrench.h"
#include "control_effect.h"
#include "spider.h"
#include "hatch.h"
#include "map_rooms.h"
#include "cell_meta.h"
#include "suspicion.h"
#include "art.h"
#include "bench.h"
#include "weapons.h"
#include "loop_api.h"
#include "present_api.h"
#include "mimic.h"
#include "entity.h"
#include "common.h"
#include "render.h"
#include "ui.h"
#include "sprites.h"
#include "player_api.h"
#include "world_api.h"
#include "lang.h"
#include "state.h"
#include "plat.h"

static int DrawAbilityHand(float sway,float lift) {
    int role=protocolRun.role;
    float age=ProtocolAbilityAge(), remaining=ProtocolAbilityRemaining();
    float impact=ProtocolShieldImpact();
    float show=role==3?fmaxf(remaining,impact):fminf(remaining,.85f-age);
    if (show<=0) return 0;
    float raise=fminf(1,age*8.f)*fminf(1,show*8.f);
    float ease=raise*raise*(3.f-2.f*raise);
    static const int hands[]={SPR_ABILITY_WRIST,SPR_ABILITY_EMITTER,
                              SPR_ABILITY_PROJECTOR,SPR_ABILITY_SHIELD};
    int size=(int)(renderH*(role==3?.54f:.39f));
    int x=(int)(renderW*(role==3?.28f:.37f)-size*.5f-sway*.55f);
    int y=(int)(renderH-size*ease+size*.2f+lift*.4f);
    if (impact>0) y+=(int)(size*impact*.35f);
    DrawSprite(hands[role],x,y,size,size,1.f+impact*.9f,1,0,0);
    return 1;
}

void DrawViewModel(void) {
    /* v3.0：**藏进柜子还举着扳手**是最出戏的一处 ——
       无战斗改造后第一人称模型不该再出现，藏身时更要藏起来。 */
    {

#if NO_COMBAT
        /* ===== v2.4.62 **这就是"左手看不到"的根因** =====
           原来写的是 `if (hiddenIn >= 0 || 1) return;` ——
           `|| 1` 让这个条件**永远成立**，于是 DrawViewModel()
           每一帧都在第一行返回：右手扳手、左手投掷物**都没画过**。
           用户反复说「左手没有东西」，根因在这里，不在模型。 */
        if (hiddenIn >= 0) return;   /* 无战斗：只在藏身时不画 */
#else
        if (hiddenIn >= 0) return;
#endif
    }

    if (!pixels || !SpritesReady()) return;

    /* 走路摆动 + 开火后坐 */
    float sway = sinf(bobPhase) * (float)renderW * 0.006f;
    /* v3.0：lift 原来用 |sin(φ/2)|，纵向频率只有横向的 1/2 ——
       手持模型会「横着摆两下、竖向才起一下」，而且 |sin| 谷底有速度拐点。
       改成 |sin(φ)|，两轴同频，走路手感一致（建模审查 #5）。 */
    float lift = fabsf(sinf(bobPhase)) * (float)renderH * 0.007f;
    float kick = weaponAtkCharge * (float)renderH * 0.010f
               + muzzleFlash * (float)renderH * 0.016f;

    int size = (int)((float)renderH * 0.54f);
    if (size < 64) size = 64;

    /* ==================================================================
     * 2026-11-05 用户要求：**只留扳手建模**
     * ------------------------------------------------------------------
     * 这里原来按 curWeapon 换三套手持模型：
     *   WPN_MELEE  → SPR_WRENCH   （摇把扳手）
     *   WPN_RANGED → SPR_PISTOL   （信号枪，已下线的武器系统）
     *   WPN_SPELL  → SPR_SIGIL    （符印/法术发射器，已下线的武器系统）
     *
     * 而每日模式会 `curWeapon = dailySeed % WPN_COUNT`、战役会
     * `curWeapon = stage % WPN_COUNT` —— 于是**随机抽到法术时，
     * 屏幕上会卡出那个发射器建模**（用户报告）。
     *
     * NO_COMBAT 之后本作只有扳手一件工具，所以直接钉死。
     * SPR_PISTOL / SPR_SIGIL 两个精灵与烘焙函数一并删除。
     * ================================================================== */
    /* 2026-11-16：挥击时切成"出招姿态"变体（钳口张开、头前探），
       其余时间仍是待机那把。 */
    /* 诊断开关：设 WRENCH_STRIKE_TEST=1 时，待机也画出招款，
       用来判断"变体到底有没有生效"。
       2026-11-16 临时加入，确认后删除。 */
    int sid = getenv("WRENCH_STRIKE_TEST") ? SPR_WRENCH_STRIKE : SPR_WRENCH;
    float light = 0.85f;   /* 挥击动画会按帧微调亮度，所以不能是 const */

    int cx = (int)((float)renderW * 0.72f + sway);
    int cy = (int)((float)renderH * 1.04f + lift - kick);
    /* ===== 2026-11-16（第二轮反馈）**前刺式挥击** =====
       用户原话：「本质不像动画，只是左右摆两帧」「角度太多、转太快」
                   「要的是直线往前 P 一下」。
       所以这次不做"抡"，改成**向前刺/推**：
         · 朝屏幕中心（准星）方向推出去：dx 向左、dy 向上、同时放大
           —— 第一人称里"靠近镜头 + 朝准星移动"就是"往前P"
         · 角度只留 ±9°（0.16 rad）做味道，不再大幅抡
         · 时间拉长到 0.266s（1.9×），帧数 8 → **12**，
           每帧角度/位移增量变小 → 不会再有"一帧跳 90°"的抽动感
         · 命中仍在 0.056s：0.056/0.266 ≈ 0.21 → 第 2~3 帧之间，与刺出同刻
       帧表：p = 播放进度，dx/dy 为屏宽/高比例（负=向左/向上）
         fr0-2  后拉蓄力（缩小、右下沉）
         fr3    **刺出**（放大 1.20、左移上抬、角度 +0.16）← 命中
         fr4-5  保持/微收
         fr6-11 缓慢回到原位
    */
    float swingProg = -1.0f;
    float swingAngle = 0.0f;
    if (wrenchSwingFxT > 0.0f) {
        const float kWin = WRENCH_SWING_TIME * 1.9f;
        swingProg = 1.0f - wrenchSwingFxT / kWin;
        if (swingProg < 0.0f) swingProg = 0.0f;
        if (swingProg > 1.0f) swingProg = 1.0f;
    }
    if (swingProg >= 0.0f) {
        static const float kAng[12] = {
            -0.02f, -0.06f, -0.10f,  0.16f,
             0.14f,  0.10f,  0.06f,  0.03f,
             0.01f,  0.00f,  0.00f,  0.00f };
        static const float kDx[12] = {
            -0.006f, -0.016f, -0.024f, -0.058f,
            -0.046f, -0.034f, -0.024f, -0.016f,
            -0.009f, -0.004f, -0.001f,  0.000f };
        static const float kDy[12] = {
             0.006f,  0.014f,  0.020f, -0.052f,
            -0.040f, -0.028f, -0.019f, -0.012f,
            -0.007f, -0.003f, -0.001f,  0.000f };
        static const int   kSz[12] = {
             97,  94,  92, 120,
            116, 111, 106, 103,
            101, 100, 100, 100 };
        static const float kLi[12] = {
            0.94f, 0.90f, 0.88f, 1.26f,
            1.16f, 1.08f, 1.03f, 1.01f,
            1.00f, 1.00f, 1.00f, 1.00f };
        int fr = (int)(swingProg * 12.0f);
        if (fr > 11) fr = 11;
        /* 刺出到收势之间（fr3~fr6）换成出招姿态的模型 */
        if (fr >= 2 && fr <= 8) sid = SPR_WRENCH_STRIKE;   /* 窗口加宽：0.044~0.20s 都用出招款 */

        /* 2026-11-16：**残影暂时关闭** —— 用户实测"攻击时多出一个方框"，
           最可能是半透明残影的方形包围盒糊在画面上。
           先关掉确认；如果方框消失，之后改用更小的偏移重开。 */
        if (0 && fr >= 2 && fr <= 6) {
            for (int g = 2; g >= 1; g--) {
                int gf = fr - g;
                if (gf < 0) continue;
                float ga = (g == 2) ? 0.08f : 0.18f;
                int gx = cx + (int)((float)renderW * kDx[gf]);
                int gy = cy + (int)((float)renderH * kDy[gf]);
                int gs = size * (kSz[gf] + 3) / 100;
                DrawSpriteRot(sid, gx - gs / 2, gy - gs, gs, gs,
                              light * kLi[gf], ga, kAng[gf], 0.52f, 0.92f);
            }
        }

        cx += (int)((float)renderW * kDx[fr]);
        cy += (int)((float)renderH * kDy[fr]);
        size = size * kSz[fr] / 100;
        light *= kLi[fr];
        swingAngle = kAng[fr];
    }
    int px = cx - size / 2;
    int py = cy - size;
    /* 有挥击时用旋转绘制，否则走原来的直绘（零开销、零回归） */
    if (swingAngle != 0.0f) {
        DrawSpriteRot(sid, px, py, size, size, light, 1.0f, swingAngle, 0.52f, 0.92f);
    } else {
        DrawSprite(sid, px, py, size, size, light, 1.0f, 0, 0.0f);
    }

    /* 2026-11-06（用户要求）：**手持小图删掉**。
       用户原话：「把那个叫什么传送的那个小图也给删掉，然后就留一个信标」。
       现在只在世界里放信标模型 + 按 Q 传送，不再有屏幕左下角的手持图。 */

    /* 开火瞬间在武器前端补一团光 */
    /* 2026-11-05：**没有枪了** —— 枪口闪光永远不画（原来按 curWeapon 判，
   而 curWeapon 会被每日/战役随机成法术，于是偶尔闪一下不存在的光）。 */
    if (0) {
        int gx = cx;
        int gy = py + (int)((float)size * 0.18f);
        int gr = (int)((float)size * 0.10f * (0.5f + muzzleFlash * 0.5f));
        if (gr < 2) gr = 2;
        for (int y = -gr; y <= gr; y++) {
            int yy = gy + y;
            if (yy < 0 || yy >= renderH) continue;
            for (int x = -gr; x <= gr; x++) {
                int xx = gx + x;
                if (xx < 0 || xx >= renderW) continue;
                float d2 = (float)(x * x + y * y) / (float)(gr * gr);
                if (d2 >= 1.0f) continue;
                PutPixelAdd(xx, yy, 0x00FFB060u, (1.0f - d2) * muzzleFlash * 0.8f);
            }
        }
    }

    if(tactical.prismFlicker>0 && flashlightOn){int size=renderH/24;if(size<4)size=4;for(int y=0;y<size;y++)for(int x=0;x<size;x++)PutPixelBlend(renderW/2+x,renderH-size*2+y,0x10202A,.7f);}
    /* The ability model occupies the same hand as the held throwable. */
    if (!DrawAbilityHand(sway,lift)) {

           /* 2026-11-06 相位投掷物 */
        /* v6 **槽位表必须和 ITEM_SLOT_COUNT(6) 一致** ——
           原来用旧的 8 个变量初始化 6 元素数组：多出初值被丢弃，
           索引整体错位（槽 2..5 读到砖头/酸液/燃烧瓶等已删除物品）。 */
        int counts[ITEM_SLOT_COUNT];InventoryCounts(counts);
        int sel = throwSelected;
        if (sel >= 0 && sel < ITEM_SLOT_COUNT && counts[sel] > 0) {
            /* v2.9：按报告重算 —— 原来 100px 大、落进枪模框内、亮度 0.55，
               结果是"更近的左手物体反而更小更暗"，而且底边被屏幕裁掉 9px。
               现在：69px、x=0.40、y=0.95，落在资源条右边、枪模左边。 */
            int isize = (int)((float)renderH * 0.15f);
            if (isize < 24) isize = 24;
            /* v2.6：原来在 renderW*0.10（最左边），被 ENERGY/LIGHT/RUN
               三条资源条整个压住。挪到中间偏右、手电筒 ON 文字的右边。 */
            int ix = (int)((float)renderW * 0.40f - sway);
            int iy = (int)((float)renderH * 0.95f + lift - kick * 0.6f);
            /* v3.0：亮度 0.75 → 0.92（枪是 0.85~1.0），alpha 0.95 → 1.0。
               注释写着"亮一点"但实际比枪暗，同一只手的两个物体不该有明暗断层
               （建模审查 #12）。 */
            /* ===== v2.4.59 **手持模型映射表** =====
               原来是 `SPR_ITEM_FLARE + sel` 直接顺序对应 ——
               但槽位是 FLARE/SLING/SMOKE/GLUE/ECHO/DECOY，
               精灵却是 FLARE/BOTTLE/BRICK/ACID/SMOKE/FIRE，
               于是「烟雾弹拿的是砖头」「回声弹拿的是烟雾弹」。
               现在用显式表，每个槽位拿到对的模型。 */
            static const int slotSpr[ITEM_SLOT_COUNT] = {
                SPR_ITEM_FLARE, SPR_ITEM_SLING, SPR_ITEM_SMOKE,
                SPR_ITEM_GLUE,  SPR_ITEM_ECHO,  SPR_ITEM_DECOY,
                SPR_PHASE_HAND   /* 相位信标：复用手持模型 */
            };
            DrawSprite(sel>=7?SPR_TOOL_BASE+sel-7:slotSpr[sel], ix, iy - isize, isize, isize,
                       0.92f, 1.0f, 0, 0.0f);
        }
    }
}

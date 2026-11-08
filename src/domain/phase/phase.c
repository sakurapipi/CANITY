/* src/domain/phase/phase.c —— 相位信标
 *
 * 第 3 轮（2026-11-08）从 game.c 原样搬过来，逻辑一行没改。
 * 契约：include/phase.h   卡片：docs/modules/phase.md
 *
 * 依赖的都是游戏层公开符号，不反向依赖本域：
 *   posX/posY (entity.h)   worldMap/uiDirty/pickupMsg/UnstickPlayer (game.h)
 *   PropBlocksAt (map_rooms.h)   PlaySfx (audio.h)
 * 不要显式 include <math.h>：分区契约 S1 要求系统头只在 common.h。
 */
#include "common.h"
#include "game.h"
#include "entity.h"
#include "lang.h"
#include "audio.h"
#include "map_rooms.h"
#include "phase.h"

/* ==================================================================
 * 2026-11-06（用户要求）：**相位信标**
 * ------------------------------------------------------------------
 * 用户原话：「新增相位传送装置，按 Q 进行传送，按 C 进行放置，
 *           一个人身上上限只能有一个，使用次数为 2，价格极其昂贵为百分比价格。」
 *
 * 规则：
 *   · 身上最多 1 个
 *   · 总共 2 次使用（放 1 次 + 传 1 次算一次完整循环？）
 *     —— 这里按"**传送次数**"算 2 次：放置不消耗次数，
 *        每次传送消耗 1 次，用完装置消失。
 *   · 数量上限 1：场上同时只能存在 1 个信标，再按 C 只会重放（不叠加）。
 *
 * 为什么"放置不消耗次数"：
 *   如果放置也消耗，玩家在一个地方放错就得废掉整个装置，
 *   而这是最贵的一件道具 —— 惩罚过重。
 * ================================================================ */
int   phaseOwned   = 0;   /* 是否持有装置 */
int   phaseCharges = 0;   /* 剩余传送次数（上限 2） */
int   phasePlaced  = 0;   /* 场上是否已有信标 */
float phaseX = 0.0f, phaseY = 0.0f;

/* ==================================================================
 * 2026-11-07（v2.4.2 / B3）：**一枚信标 = 一次传送**
 * ------------------------------------------------------------------
 * 原来的规则是"投一次给 2 次充电，传送成功就清 placed"：
 *     投出 -> phasePlaced=1, phaseCharges=2
 *     按 Q -> 传送成功 -> phasePlaced=0, phaseCharges-- (剩 1)
 * 剩下 1 次充能但场上已经没有锚点 —— **第二次传送永远用不出来**。
 *
 * 更麻烦的是数字语义打架：HUD 显示「相位信标 ×2」指的是**库存**，
 * 而 phaseCharges 是"传送次数"。玩家看到 2，实际能传 4 次（旧规则下
 * 如果锚点不清的话），中间态完全无法解释。
 *
 * 现在统一成：投出消耗 1 枚库存 -> 场上 1 个锚点 -> 传送 1 次 -> 锚点消失。
 * 想再传就再投一枚。语义一行说得清，也少一套计数。
 * ================================================================ */
#define PHASE_MAX_CHARGES 1

void PhaseGive(void) {
    phaseOwned = 1;
    phaseCharges = PHASE_MAX_CHARGES;
    phasePlaced = 0;
}

void PhaseReset(void) {
    phaseOwned = 0;
    phaseCharges = 0;
    phasePlaced = 0;
    phaseX = phaseY = 0.0f;
}

/* ==================================================================
 * 2026-11-06（用户要求）：**相位投掷物落地结算**
 * ------------------------------------------------------------------
 * kind 12 = 信标：落点成为回溯点，之后按 Q 传送回来
 * kind 13 = 锚  ：落点**立刻**把玩家拉过去
 *
 * 两个都要先校验落点可站立 —— 投进墙里的话，
 * 信标设为无效、锚直接不传送，绝不能把玩家塞进几何体。
 * ================================================================ */
void PhaseOnThrowLanded(int kind, float lx, float ly) {
    /* ==================================================================
     * 2026-11-06（用户实测）：**信标丢出去炸掉，坐标不生效**
     * ------------------------------------------------------------------
     * 原因是我这里用了 Blocked() —— 它是**带玩家半径 0.24** 的判定。
     * 投掷物撞墙时落点必然贴着墙，Blocked 必然为真，
     * 于是每一次落地都被判成"落点不能用"，只剩爆炸特效。
     *
     * 现在改成**按格子**判：只要这一格是地板就接受；
     * 如果这一格恰好是墙，就向四邻找一格地板（最多 1 格），
     * 找不到才真的拒绝。
     * ================================================================ */
    {
        int cx = (int)floorf(lx), cy = (int)floorf(ly);
        int ok = 0;
        /* ================================================================
         * 2026-11-07（v2.4.2）：**落地判据必须和传送判据一致**
         * ----------------------------------------------------------------
         * 原来落地只查 worldMap（是不是墙），传送却还要查 PropBlocksAt
         * （那格有没有家具）。于是"信标落得下、但传不过去"成为可能：
         * 投出去看着成功了，按 Q 却提示"信标位置已失效"并把信标清掉。
         * 玩家感受就是"相位传送有时候不生效"。
         *
         * 现在落地时就用**同一套判据**挑格子；四邻找不到再扩到 8 邻、
         * 半径 2 格。真的没有可站格才拒绝（并明确提示）。
         * ============================================================== */
        extern int PropBlocksAt(int px, int py);
        static const int dx8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
        static const int dy8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        int step, k;
        ok = 0;
        for (step = 0; step <= 2 && !ok; step++) {
            for (k = 0; k < 8 && !ok; k++) {
                int nx = cx + dx8[k] * step;
                int ny = cy + dy8[k] * step;
                if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
                if (worldMap[ny][nx]) continue;
                if (PropBlocksAt(nx, ny)) continue;     /* 和传送同一判据 */
                cx = nx; cy = ny; ok = 1;
            }
        }
        if (!ok) {
            pickupMsg = (g_lang == LANG_ZH) ? "落点不能用" : "BAD LANDING SPOT";
            pickupMsgTimer = 1.4f;
            return;
        }
        /* 用格子中心，保证信标/玩家不会卡在墙缝上 */
        lx = (float)cx + 0.5f;
        ly = (float)cy + 0.5f;
    }

    if (kind == 12) {
        /* 信标：设定回溯点，可供**一次**传送（见 PHASE_MAX_CHARGES 的注释） */
        phaseX = lx; phaseY = ly;
        phasePlaced = 1;
        phaseOwned = 1;
        phaseCharges = PHASE_MAX_CHARGES;
        pickupMsg = (g_lang == LANG_ZH) ? "信标已固定 - 按 Q 传送回来（一次）"
                                        : "BEACON SET - PRESS Q TO RETURN (ONE USE)";
        pickupMsgTimer = 2.0f;
        PlaySfx(SFX_POWER, 0.7f);
    }
    uiDirty = 1;
}

/* ==================================================================
 * 2026-11-06（审计补）：**换层必须清掉信标的"已放置"状态**
 * ------------------------------------------------------------------
 * PhaseReset() 只在 StartGame 里调用，换层时不会走。
 * 结果：在第 3 层放下的信标，坐标会一直留到第 4 层 ——
 * 按 Q 会被传送到上一层的坐标上（可能正好是这层的空地，
 * 那就是"莫名其妙被传走"；也可能落进墙里被拒绝）。
 *
 * 注意两者要分开：
 *   phaseOwned / phaseCharges 是**背包物品**，跨层保留；
 *   phasePlaced / phaseX / phaseY 是**场上装置**，换层即失效。
 * ================================================================ */
void PhaseResetPlacement(void) {
    phasePlaced = 0;
    phaseX = 0.0f;
    phaseY = 0.0f;
}

/* 2026-11-07（B3）：这个入口目前没有玩家可达路径（信标已改成投掷物），
   保留是为了测试与将来的"原地放置"。语义必须和新规则一致：
   放一个 = 一次传送。 */
int PhaseTryPlace(void) {
    if (!phaseOwned || phaseCharges <= 0) return 0;
    /* 场上只允许一个：重复按 C 就挪到脚下，不叠第二个 */
    phaseX = posX;
    phaseY = posY;
    phasePlaced = 1;
    phaseCharges = PHASE_MAX_CHARGES;
    return 1;
}

int PhaseTryTeleport(void) {
    if (!phaseOwned || phaseCharges <= 0 || !phasePlaced) return 0;
    /* ==================================================================
     * 2026-11-06（用户实测第二轮）：**旁边有箱子就失效**
     * ------------------------------------------------------------------
     * 这里原来也是 Blocked() —— 带 0.24 的玩家半径。
     * 信标落在贴着墙/家具的位置时，那个半径判定就为真 → 传送被拒。
     * 和落地判定是同一个错误，我只改了一处。
     *
     * 现在按**格子**判：只要这一格是地板就能站；
     * 真的变成墙（比如期间塌过）才拒绝。
     * ================================================================ */
    {
        extern int PropBlocksAt(int x, int y);
        int cx = (int)floorf(phaseX), cy = (int)floorf(phaseY);
        if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) return 0;
        if (worldMap[cy][cx]) return 0;
        /* 2026-11-06：家具也算不可落点（否则人会卡在家具体积里）。
           2026-11-07：落地时已经用**同一判据**挑过格子，
           所以这里判到为真只可能是"投出之后地形变了"（比如塌方）。 */
        if (PropBlocksAt(cx, cy)) return 0;
        /* 注意：这里**不做邻近找格**。
           早先试过"目标被占就往旁边挪一格"，但那会让人穿到墙的另一侧 ——
           等于免费穿墙。目标不可用就明确失败，让玩家重掷一枚信标。 */
    }
    posX = phaseX;
    posY = phaseY;
    phasePlaced = 0;
    phaseCharges--;
    if (phaseCharges <= 0) phaseOwned = 0;   /* 用完即消失 */

    UnstickPlayer();
    return 1;
}

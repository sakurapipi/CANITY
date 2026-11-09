/* src/domain/collapse/collapse.c —— 崩塌层
 *
 * 第 3 轮（2026-11-08）从 game.c 原样搬过来，逻辑一行没改。
 * 契约：include/collapse.h   卡片：docs/modules/collapse.md
 */
#include "common.h"
#include "room_theme.h"
#include "neutral.h"
#include "spider.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "entity.h"
#include "lang.h"
#include "audio.h"
#include "collapse.h"
#include "cell_meta.h"    /* CellMeta（快照要存 cellMeta）*/
#include "map_rooms.h"    /* RoomProp / MAX_ROOM_PROPS */
#include "hatch.h"        /* Hatch（快照要存舱门）*/
#include "wallet.h"
#include "map_validator.h" /* MapCheck / MapValidate */
#include "render.h"        /* texture/themeId 等渲染侧全局（崩塌重掷要看）*/

int   collapsedLevel   = 0;      /* 本层是否为崩塌层（影响生成参数） */
/* 2026-11-06：崩塌重掷期间置 1，用来区分"崩塌重掷"与"正常换层" */
/* 崩塌要求"下一次 GenerateLevel 走重掷分支"。
 * game.c 的 GenerateLevel 会读它并清零 —— 跨域标志，所以不能 static。 */
int s_collapseGen = 0;
float screenBlackout   = 0.0f;   /* 0 = 无，1 = 全黑 */
static float collapseT = 0.0f;   /* 崩塌计时 */
static int   collapseArmed = 0;  /* 1 = 崩塌流程进行中 */
static int   s_collapseRerolled = 0;  /* 本次崩塌是否已经执行过重掷 */
static int   s_collapseRerollCount = 0; /* 进程内累计重掷次数（测试用） */

/* 原层快照 —— 只在崩塌开始时写一次，失败时整块还原 */
typedef struct {
    RoomThemeState roomThemeSave;
    int   worldMap[MAP_HEIGHT][MAP_WIDTH];
    int   explored[MAP_HEIGHT][MAP_WIDTH];
    CellMeta cellMeta[MAP_HEIGHT][MAP_WIDTH];
    Container containers[MAX_CONTAINERS];
    int   containerCount;
    HideSpot hideSpots[HIDESPOT_MAX];
    int   hideSpotCount;
    RoomProp roomProps[MAX_ROOM_PROPS];
    int   roomPropCount;
    Hatch hatches[MAX_HATCHES];
    int   hatchCount;
    Breaker breaker;
    float exitX, exitY, posX, posY;
    int   fusesHeld, fusesNeeded, roomCount;
    /* 审计·中 7：这几项也会被 GenerateLevel 改掉，回滚时必须还原，
       否则地图回来了、墙面贴图/词条/事件却还是新层的。 */
    int   themeIdSave;
    int   levelModSave;
    float modSanityMulSave, modSpeedMulSave;
    float modBatteryMulSave, modShardMulSave, modNoiseMulSave;
} CollapseSnapshot;

static CollapseSnapshot* s_collapseSnap = NULL;

static void CollapseSnapshotTake(void) {
    
    if (!s_collapseSnap) s_collapseSnap = (CollapseSnapshot*)malloc(sizeof(CollapseSnapshot));
    if (!s_collapseSnap) return;
    s_collapseSnap->roomThemeSave=roomTheme;
    memcpy(s_collapseSnap->worldMap, worldMap, sizeof(worldMap));
    memcpy(s_collapseSnap->explored, explored, sizeof(explored));
    memcpy(s_collapseSnap->cellMeta, cellMeta, sizeof(cellMeta));
    memcpy(s_collapseSnap->containers, containers, sizeof(containers));
    memcpy(s_collapseSnap->hideSpots, hideSpots, sizeof(hideSpots));
    memcpy(s_collapseSnap->roomProps, roomProps, sizeof(roomProps));
    memcpy(s_collapseSnap->hatches, hatches, sizeof(hatches));
    s_collapseSnap->containerCount = containerCount;
    s_collapseSnap->hideSpotCount  = hideSpotCount;
    s_collapseSnap->roomPropCount  = roomPropCount;
    s_collapseSnap->hatchCount     = hatchCount;
    s_collapseSnap->breaker        = breaker;
    s_collapseSnap->exitX = exitX; s_collapseSnap->exitY = exitY;
    s_collapseSnap->posX  = posX;  s_collapseSnap->posY  = posY;
    s_collapseSnap->themeIdSave     = themeId;
    s_collapseSnap->levelModSave    = levelMod;
    s_collapseSnap->modSanityMulSave = modSanityMul;
    s_collapseSnap->modSpeedMulSave  = modSpeedMul;
    s_collapseSnap->modBatteryMulSave = modBatteryMul;
    s_collapseSnap->modShardMulSave   = modShardMul;
    s_collapseSnap->modNoiseMulSave   = modNoiseMul;
    s_collapseSnap->fusesHeld = fusesHeld;
    s_collapseSnap->fusesNeeded = fusesNeeded;
    s_collapseSnap->roomCount = roomCount;
}

static void CollapseSnapshotRestore(void) {
    if (!s_collapseSnap) return;
    roomTheme=s_collapseSnap->roomThemeSave;
    memcpy(worldMap, s_collapseSnap->worldMap, sizeof(worldMap));
    memcpy(explored, s_collapseSnap->explored, sizeof(explored));
    memcpy(cellMeta, s_collapseSnap->cellMeta, sizeof(cellMeta));
    memcpy(containers, s_collapseSnap->containers, sizeof(containers));
    memcpy(hideSpots, s_collapseSnap->hideSpots, sizeof(hideSpots));
    memcpy(roomProps, s_collapseSnap->roomProps, sizeof(roomProps));
    memcpy(hatches, s_collapseSnap->hatches, sizeof(hatches));
    containerCount = s_collapseSnap->containerCount;
    hideSpotCount  = s_collapseSnap->hideSpotCount;
    roomPropCount  = s_collapseSnap->roomPropCount;
    hatchCount     = s_collapseSnap->hatchCount;
    breaker        = s_collapseSnap->breaker;
    exitX = s_collapseSnap->exitX; exitY = s_collapseSnap->exitY;
    posX  = s_collapseSnap->posX;  posY  = s_collapseSnap->posY;
    fusesHeld = s_collapseSnap->fusesHeld;
    themeId      = s_collapseSnap->themeIdSave;
    levelMod     = s_collapseSnap->levelModSave;
    modSanityMul = s_collapseSnap->modSanityMulSave;
    modSpeedMul  = s_collapseSnap->modSpeedMulSave;
    modBatteryMul= s_collapseSnap->modBatteryMulSave;
    modShardMul  = s_collapseSnap->modShardMulSave;
    modNoiseMul  = s_collapseSnap->modNoiseMulSave;
    /* 审计·中 7：临时实体会在新层重新生成过一次，回滚后必须按老地图重刷，
       否则蜘蛛/拾光虫停在新层的出生点（可能是墙里或不可达区）。 */
    {   SpiderReset(); }
    {  NeutralReset(); }
    {   SpawnLightBeast(); }
    {  SpawnLightBeast2(); }
    fusesNeeded = s_collapseSnap->fusesNeeded;
    roomCount = s_collapseSnap->roomCount;
}

/* 崩塌层是否可玩 —— 用现成的校验器，不自己写一套判据 */
static int CollapseLevelPlayable(void) {
    MapCheck mc;
    memset(&mc, 0, sizeof(mc));
    MapValidate(&mc);
    if (!mc.ok) return 0;
    if (!mc.exitReachable) return 0;
    if (!mc.spawnOpen) return 0;
    if (mc.propsOutsideRoom > 0) return 0;
    return 1;
}

/* ==================================================================
 * 2026-11-06（第 6 轮）：**把玩家从墙里挪出来**
 * ------------------------------------------------------------------
 * 返回 1 = 挪过位置，0 = 原地就是空的。
 *
 * 为什么单独成函数：第一次我把它内联写在崩塌分支里，
 * 端到端用例**抓不到** —— 那个种子里玩家恰好落在空地，
 * 删掉修复测试照样绿（实测）。
 * 抽出来之后可以直接"把玩家塞进墙里"再断言。
 * ================================================================ */
int CollapseUnstickPlayer(void) {
    if (!Blocked(posX, posY)) return 0;
    for (int r = 1; r <= 12; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                /* 只走这一圈的边上，圈内已经找过 */
                if (dx > -r && dx < r && dy > -r && dy < r) continue;
                int cx = (int)posX + dx, cy = (int)posY + dy;
                if (cx < 1 || cy < 1 || cx >= MAP_WIDTH - 1 || cy >= MAP_HEIGHT - 1) continue;
                if (worldMap[cy][cx]) continue;
                float nx = (float)cx + 0.5f, ny = (float)cy + 0.5f;
                if (Blocked(nx, ny)) continue;      /* 家具也算 */
                posX = nx; posY = ny;
                return 1;
            }
        }
    }
    return 0;
}

void CollapseBegin(void) {
    collapseArmed = 1;
    /* 起手就抖一下，告诉玩家"出事了" */
    AddShake(0.35f, 0.45f);
    collapseT = 0.0f;
    screenBlackout = 0.0f;
    s_collapseRerolled = 0;
}

/* 每帧推进。返回 1 表示"正在崩塌，本帧应吞掉玩家输入"。 */
int CollapseTick(float dt) {
    if (!collapseArmed) return 0;

    collapseT += dt;

    if (collapseT < 0.8f) {
        /* 阶段 A：淡出 —— 顺便**疯狂抖动**
         * 2026-11-06（用户要求）：
         *   「崩塌的时候你可以再做一点震撼一点的特效，
         *     比如屏幕疯狂抖动，然后再黑屏」
         *
         * 抖动幅度跟着黑屏进度递增（越黑越猛），每帧续期，
         * 所以它不会衰减 —— 读起来像结构在持续崩解。 */
        screenBlackout = collapseT / 0.8f;
        if (screenBlackout > 1.0f) screenBlackout = 1.0f;
        {
            float mag = 0.10f + 0.55f * screenBlackout * screenBlackout;
            AddShake(0.06f, mag);
        }
        return 1;
    }

    /* ==================================================================
     * 2026-11-06（**第 5 轮修**）：重掷触发条件原来是
     *     if (screenBlackout >= 1.0f && collapseT - dt < 0.8f)
     * 这个条件是**永远不会成立的**：
     *   淡出阶段只在 collapseT < 0.8 时跑，所以 blackout 最多到 0.98x；
     *   跨过 0.8 的那一帧用的还是**上一帧**的值（约 0.98），
     *   于是 `>= 1.0` 为假 → 重掷被跳过；
     *   下一帧 blackout 终于等于 1.0，但 `collapseT - dt < 0.8` 又为假。
     * 结果：玩家看到黑屏，然后**什么都没发生**。
     *
     * 实测这件事被端到端用例掩盖过 —— 用例只断言"塌完之后地图可玩"，
     * 而"根本没塌"同样满足那个断言。所以这里同时加了一个计数器，
     * 让测试能断言"重掷真的执行了"。
     * ================================================================ */
    if (!s_collapseRerolled && collapseT >= 0.8f) {
        s_collapseRerolled = 1;
        s_collapseRerollCount++;
        CollapseSnapshotTake();
        if(!s_collapseSnap){collapseArmed=0;screenBlackout=0;pickupMsg=(g_lang==LANG_ZH)?"这里塌不动":"IT WON'T GIVE WAY";pickupMsgTimer=2;return 1;}
        collapsedLevel = 1;
        s_collapseGen = 1;      /* 告诉 GenerateLevel：这一次是崩塌重掷 */
        GenerateLevel();
        if (!CollapseLevelPlayable()) {
            /* ---- 用户要求：失败就回到原来的层 ---- */
            CollapseSnapshotRestore();
            collapsedLevel = 0;
            pickupMsg = (g_lang == LANG_ZH) ? "这里塌不动" : "IT WON'T GIVE WAY";
            pickupMsgTimer = 2.2f;
        } else {
            /* ================================================================
             * 2026-11-06（第 6 轮）：**重掷后玩家可能被埋进新地形**
             * ----------------------------------------------------------------
             * GenerateLevel 换掉了整张图，但玩家坐标没动。
             * 原来那里是空地、现在是墙 —— 玩家会被卡在墙里：
             * Blocked() 对当前位置返回 1，两个轴向都动不了，
             * 除了重开没有任何出路。
             *
             * 现在：塌成功之后检查脚下，被埋就挪到最近的空地中心。
             * 顺着半径一圈圈找，找到的第一个空地就是最近的。
             * ================================================================ */
            CollapseUnstickPlayer();
        }
        /* 重掷会把舱门表清空并重置计时器 —— 这是期望行为，
           崩塌层不生成舱门（防止连锁刷）。 */
    }

    if (collapseT >= 2.0f) {
        collapseArmed = 0;
        screenBlackout = 0.0f;
        collapseT = 0.0f;
        return 0;
    }
    /* 阶段 B：淡入 */
    screenBlackout = 1.0f - (collapseT - 0.8f) / 1.2f;
    if (screenBlackout < 0.0f) screenBlackout = 0.0f;
    return 1;
}

int CollapseBusy(void) { return collapseArmed; }

/* 仅测试用：进程内累计执行过几次"重掷"。
   没有它就没法区分"塌了"和"黑屏完什么都没发生"，
   而那正是本轮抓到的 bug。 */
int CollapseRerollCount(void) { return s_collapseRerollCount; }

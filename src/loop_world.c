/* src/loop_world.c —— P2 世界（2026-11-09，C 段第 1 块）
 *
 * 从 `game.c` 搬出来的**世界查询**函数。这是拆 game.c 的第一块 ——
 * 先搬"零依赖、纯查询"的，证明机制可行，再搬有状态的。
 *
 * 目前：金币匣位置 / 出口位置。
 * 后续：GenerateLevel 一系的函数会陆续搬进来。
 */
#include "common.h"
#include "loop_api.h"
#include "entity.h"
#include "state.h"
#include "world_api.h"
#include "events.h"
#include "player_api.h"

int worldMap[MAP_HEIGHT][MAP_WIDTH];
int explored[MAP_HEIGHT][MAP_WIDTH];
int currentLevel = 1;
float exitX = 0.0f, exitY = 0.0f;
int levelMod = -1;
float modSanityMul = 1.0f;
float modSpeedMul = 1.0f;
float modBatteryMul = 1.0f;   /* v2.0 事件：手电耗电倍率 */
float modShardMul = 1.0f;     /* v2.0 事件：金币倍率 */
float modNoiseMul = 1.0f;     /* v2.0 事件：噪声倍率（死寂） */
float levelBanner = 0.0f;      /* 进关后显示词条名的秒数 */
float safeZoneMsg = 0.0f;      /* 进入安全屋后提示「SAFE ZONE」的剩余秒数 */
int upgBattery = 0;      /* 电量上限 +20/级 */
int upgSpeed = 0;        /* 移速 +6%/级 */
int upgQuiet = 0;        /* 噪声半径 -15%/级 */
int upgSanity = 0;       /* 理智流失 -10%/级 */
int upgEco = 0;          /* 手电耗电 -12%/级 */
int upgRest = 0;         /* 安全屋回智 +25%/级 */
SafeRoom safeRoom;
Container containers[MAX_CONTAINERS];
HideSpot hideSpots[HIDESPOT_MAX];
int hideSpotCount = 0;
int hideRepairCount = 0;   /* v2.4.17：本局已修复次数 */
int hiddenIn = -1;          /* >=0 表示藏在第几个点里 */
int hideSpotNear = -1;      /* 当前离得最近、可以钻的点 */
int lockedThisFloor = 0;   /* v3.0：本层已上锁的箱子数 */
int containerCount = 0;
Breaker breaker;
int fusesHeld = 0;        /* 手上已有保险丝 */
int fusesNeeded = 2;      /* 本关需要几根 */
int breakerOpen = 0;      /* 配电箱是否已通電 */

float GoldBreakerAt(int level) { return (25.0f + 3.0f * (float)level) * EndlessDepthBonus(level); }
float GoldExitAt(int level)    { return (30.0f + 6.0f * (float)level) * EndlessDepthBonus(level); }

int PlayerAtExit(void) {
    return (breakerOpen && Dist2(posX, posY, exitX, exitY) < EXIT_ENTER_R2 * EXIT_ENTER_R2);
}

float EndlessDepthBonus(int level){
    if(!isEndlessRun || level<=10)return 1;
    float d=(float)(level-10);return fminf(3.0f,1+d*.06f+d*d*.0015f);
}

int ShotBlocked(float x, float y) {
    int cx = (int)floorf(x), cy = (int)floorf(y);
    if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return 1;
    return worldMap[cy][cx] ? 1 : 0;
}

float ExtractDepthMul(int level){
    float m=1+.035f*fmaxf(0,(float)(level-1));
    return fminf(isEndlessRun?2.6f:1.6f,m);
}

int UpgradeCost(int kind) {
    int lvl = UpgradeLevel(kind);
    if (lvl >= UPG_MAX) return -1;
    return 30 + lvl * 45;      /* 30 / 75 / 120 */
}


int EndlessUnlocked(void) {
    return (bestLevel >= 10) || gameCleared || gameWon;
}

int EventScore(int ev) {
    return EventScoreOf(ev);   /* v6：查 events.c 的表 */
}

int UpgradeLevel(int kind) {
    switch (kind) {
        case 0: return upgBattery;
        case 1: return upgSpeed;
        case 2: return upgQuiet;
        case 3: return upgSanity;
        case 4: return upgEco;
        default: return upgRest;
    }
}

const char* UpgradeName(int kind) {
    switch (kind) {
        case 0: return "BATTERY +20";
        case 1: return "SPEED +6%";
        case 2: return "QUIETER -15%";
        case 3: return "CALM -10%";
        case 4: return "ECO -12% DRAIN";
        default: return "REST +25% HEAL";
    }
}


#define PLAYER_RADIUS 0.24f

void UnstickPlayer(void) {
    if (!Blocked(posX, posY)) return;
    static const float ND[8][2] = {
        { 0.5f, 0.0f }, { -0.5f, 0.0f }, { 0.0f, 0.5f }, { 0.0f, -0.5f },
        { 0.5f, 0.5f }, { -0.5f, 0.5f }, { 0.5f, -0.5f }, { -0.5f, -0.5f }
    };
    for (int step = 1; step <= 4; step++) {
        for (int k = 0; k < 8; k++) {
            float nx = posX + ND[k][0] * (float)step;
            float ny = posY + ND[k][1] * (float)step;
            if (!Blocked(nx, ny)) { posX = nx; posY = ny; return; }
        }
    }
}

int Blocked(float x, float y) {
    int x0 = (int)floorf(x - PLAYER_RADIUS), x1 = (int)floorf(x + PLAYER_RADIUS);
    int y0 = (int)floorf(y - PLAYER_RADIUS), y1 = (int)floorf(y + PLAYER_RADIUS);
    if (x0 < 0 || y0 < 0 || x1 >= MAP_WIDTH || y1 >= MAP_HEIGHT) return 1;
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++) {
            if (worldMap[cy][cx]) return 1;
            /* ==================================================================
             * 2026-11-06（v2.4-rc P2）：**房间家具是实体**
             * ------------------------------------------------------------------
             * 在此之前全工程没有任何物件参与碰撞 —— 渲染层自己的注释就写着
             * 「箱子/配电箱没有碰撞，玩家可以站进模型里」。
             * 那正是"没有实体模型"的根源：不是画得不像，是它本来就不是实体。
             *
             * 这里让家具进入**唯一**的碰撞入口，玩家和所有 AI 都自动生效。
             * ================================================================== */
            /* ==================================================================
             * 2026-11-06（用户要求）：**取消伪实体阻挡**
             * ------------------------------------------------------------------
             * 用户实测：「伪实体堵路，堵住道路，导致出口进不去。
             *           我觉得还是不要用伪实体了。」
             *
             * 家具（货架/木箱堆/控制台）是**摆件**，不是墙。
             * 让它们参与碰撞会带来三个后果：
             *   ① 1 格宽的过道被从中间截断（房间生成时已经尽力避让，
             *      但模板尺寸固定，总有避不开的时候）
             *   ② AI 的 A* 把家具当墙绕，寻路变得又贵又难看
             *   ③ 最严重的：撤离点被堵死就等于这一层报废
             *
             * 所以它们全部改成**可穿透**。视觉上仍然有体量，
             * 但玩家和怪都能走过去 —— 和箱子/配电箱原本的行为一致。
             * ================================================================ */
            (void)cx; (void)cy;
        }
    return 0;
}

int SpotClear(float x, float y, float minD, int selfKind, int selfIdx) {
    float m2 = minD * minD;
    for (int i = 0; i < containerCount; i++) {
        if (selfKind == 1 && i == selfIdx) continue;
        if (containers[i].active && Dist2(x, y, containers[i].x, containers[i].y) < m2) return 0;
    }
    if (selfKind != 2 && breaker.active && Dist2(x, y, breaker.x, breaker.y) < m2) return 0;
    if (selfKind != 3 && safeRoom.active && Dist2(x, y, safeRoom.x, safeRoom.y) < m2) return 0;
    /* v3.3 **修复藏身点被箱子压住** ——
       原来这里只查容器/配电箱/安全屋，**完全没查藏身点**。
       而生成顺序是「先藏身点、后容器」，所以后放的箱子可以直接怼在垃圾桶上
       （用户截图：垃圾桶和木箱叠在同一格，只看见 HIDE 提示却找不到桶）。
       注意：藏身点自己是最后才写进 hideSpots[] 的，
       所以调用 SpotClear 时不会把自己也判进去。 */
    for (int i = 0; i < hideSpotCount; i++) {
        if (!hideSpots[i].active) continue;
        if (Dist2(x, y, hideSpots[i].x, hideSpots[i].y) < m2) return 0;
    }
    return 1;
}

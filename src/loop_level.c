/* Level generation owns its temporary workspace and private generation rules.
 * Keep stage order and random-number consumption unchanged during refactoring.
 */
#include "common.h"
#include "room_theme.h"
#include "map_topology.h"
#include "control_effect.h"
#include "echo_marks.h"
#include "neutral.h"
#include "spider.h"
#include "throwing.h"
#include "wrench.h"
#include "events.h"
#include "loop_api.h"
#include "tutorial.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "entity.h"
#include "state.h"
#include "map_validator.h"
#include "hatch.h"
#include "cell_meta.h"
#include "map_rooms.h"
#include "core_rng.h"
#include "phase.h"
#include "collapse.h"
#include "difficulty.h"
#include "sound_event.h"
#include "suspicion.h"
#include "hints.h"
#include "warden.h"
#include "wallet.h"
#include "upgrade_run.h"
#include "skills.h"
#include "relics.h"
#include "daily.h"
#include "campaign.h"
#include "lockbox.h"
#include "mimic.h"
#include "extras.h"
#include "render.h"
#include "bestiary.h"
#include "lang.h"

typedef struct LevelBuildWorkspace {
    int cells[MAP_WIDTH * MAP_HEIGHT][2];
    int emptyCount;
    int contCap;
} LevelBuildWorkspace;

int g_forceMod = -1;

static void RollLevelMod(int roll100, int pick) {
    levelMod = -1;
    /* v2.0 修复：4 个乘子必须**全部**重置。
       原来只重置 sanity/speed，于是「涨水」事件抽到一次之后，
       之后每一层、甚至之后每一次 New Run 手电耗电都永久 ×2。 */
    modSanityMul = 1.0f;
    modSpeedMul = 1.0f;
    modBatteryMul = 1.0f;
    modShardMul = 1.0f;
    modNoiseMul = 1.0f;
    if (currentLevel >= 2 && roll100 < 60) {
        levelMod = pick % MOD_COUNT;
        if (levelMod == 1) modSpeedMul = 1.25f;       /* HUNTED */
        else if (levelMod == 3) modSanityMul = 1.35f; /* LEAKY */
        else if (levelMod == 5) modSanityMul = 0.80f; /* CALM */
    }
}

static int ContainerTargetCount(int mod, int base) {
    int n = base;
    if (mod == 2) n -= 2;       /* CRAMPED */
    else if (mod == 4) n += 2;  /* RICH */
    if (n > MAX_CONTAINERS) n = MAX_CONTAINERS;
    if (n < 2) n = 2;
    return n;
}

static void FindFarthestFromCell(int sx, int sy, float* outX, float* outY) {
    static int dist[MAP_HEIGHT][MAP_WIDTH];
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) dist[y][x] = -1;

    int qx[MAP_WIDTH * MAP_HEIGHT], qy[MAP_WIDTH * MAP_HEIGHT];
    int head = 0, tail = 0;
    if (sx < 1 || sx >= MAP_WIDTH - 1 || sy < 1 || sy >= MAP_HEIGHT - 1) { sx = 1; sy = 1; }
    dist[sy][sx] = 0;
    qx[tail] = sx; qy[tail] = sy; tail++;

    const int dx4[4] = { 1, -1, 0, 0 };
    const int dy4[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        int cx = qx[head], cy = qy[head]; head++;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx4[k], ny = cy + dy4[k];
            if (nx < 1 || nx >= MAP_WIDTH - 1 || ny < 1 || ny >= MAP_HEIGHT - 1) continue;
            if (worldMap[ny][nx]) continue;
            if (dist[ny][nx] >= 0) continue;
            dist[ny][nx] = dist[cy][cx] + 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }

    int best = -1, bx = MAP_WIDTH - 3, by = MAP_HEIGHT - 3;
    for (int y = 1; y < MAP_HEIGHT - 1; y++) {
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (worldMap[y][x]) continue;
            if (dist[y][x] > best) { best = dist[y][x]; bx = x; by = y; }
        }
    }
    *outX = (float)bx + 0.5f;
    *outY = (float)by + 0.5f;
}

/* 旧接口：从出生点 (1,1) 出发 */
/* ==================================================================
 * 2026-11-06（用户实测）：**出口不能刷在特殊房间里**
 * ------------------------------------------------------------------
 * 用户原话：「有一些出口会刷在那个特殊房间里面，导致也只有一条路可以先往出口」。
 *
 * 根因：FindFarthestFromCell 只按"离出生点最远"挑格，
 * 而房间内部是**挖空**的 —— 房间往往正好是最远的区域，
 * 于是出口落进房间，而房间只有 1~2 个门口 → 到出口只剩一条路。
 *
 * 修法：在 BFS 距离场里**把房间里侧格排除在候选之外**，
 * 出口只从普通迷宫格子里挑。找不到合适的再退回原行为。
 * ================================================================ */
static void FindFarthestCell(float* outX, float* outY) {
    FindFarthestFromCell(1, 1, outX, outY);
    /* 落进房间就换一个：从远到近扫，取第一个不在房间里的空格 */
    {

        if (roomCount > 0 &&
            cellMeta[(int)(*outY)][(int)(*outX)].roomId != 0) {
            float bestX = *outX, bestY = *outY;
            int bestD = -1;
            for (int y = 1; y < MAP_HEIGHT - 1; y++) {
                for (int x = 1; x < MAP_WIDTH - 1; x++) {
                    if (worldMap[y][x]) continue;
                    if (cellMeta[y][x].roomId != 0) continue;   /* 房间内部跳过 */
                    int dx = x - 1, dy = y - 1;
                    int d = dx * dx + dy * dy;
                    if (d > bestD) { bestD = d; bestX = (float)x + 0.5f; bestY = (float)y + 0.5f; }
                }
            }
            if (bestD > 0) { *outX = bestX; *outY = bestY; }
        }
    }
}

/* ==================================================================
 * v28：**上锁箱子的出现频率**
 * ------------------------------------------------------------------
 * 原来只有「阶段1：保险丝箱」会掷锁：18% 概率、每层最多 2 个；
 * 阶段2 补出来的补给箱完全不参与，而保险丝箱本来就只有 2~6 个 ——
 * 结果是很多层一个锁箱都见不到，四种锁盘几乎玩不到。
 *
 * 现在改成**所有容器放完之后统一判定**：每个箱子 30% 概率上锁，
 * 每层最多 3 个。等容器数量确定后再判定，才能准确保证
 * 「未上锁的箱子仍然够放本层要求的保险丝」—— 保险丝不许进锁箱，
 * 锁太多会让整层无法通关。
 * ================================================================ */
/* ==================================================================
 * 2026-11-05 用户要求：**锁箱比例随层数递增，封顶 80%**
 * ------------------------------------------------------------------
 *   第 1 层   0%   —— 全部不上锁，先熟悉开箱
 *   第 2 层   5%
 *   之后每层 +9%，第 11 层起封顶 80%（8 箱 = 6 锁 2 不锁）
 *
 * 为什么用「比例」而不是「每箱掷骰」：
 *   掷骰在 8 个箱子上方差极大，同样 30% 有时出 0 个、有时出 4 个。
 *   改成先算「这一层该有几个锁箱」，再随机挑哪几个 —— 曲线才可控。
 *   小数部分用概率进位，所以第 2 层 40% 的机会出 1 个，不是永远 0。
 *
 * **硬约束（必须留着）**：至少 2 个箱子不上锁。
 *   保险丝只放未上锁的箱子（下面那段有说明），全锁 = 整层无解。
 * ================================================================ */
#define LOCKBOX_PCT_MIN_FLOOR    2      /* 第几层开始上锁 */
#define LOCKBOX_PCT_BASE         5      /* 第 2 层的比例 */
#define LOCKBOX_PCT_STEP         9      /* 之后每层 +9 */
#define LOCKBOX_PCT_CAP         70      /* 封顶（2026-11-05 用户：80 → 70，减轻开锁疲劳） */
#define LOCKBOX_MIN_UNLOCKED     2      /* 永远留这么多个不锁的 */


/* ==================== 道具 · 投掷 ==================== */
static int LockboxPctForFloor(int lv) {
    if (lv < LOCKBOX_PCT_MIN_FLOOR) return 0;
    int pct = LOCKBOX_PCT_BASE + (lv - LOCKBOX_PCT_MIN_FLOOR) * LOCKBOX_PCT_STEP;
    if (pct > LOCKBOX_PCT_CAP) pct = LOCKBOX_PCT_CAP;
    return pct;
}

static void ApplyLockboxes(void) {
    lockedThisFloor = 0;
    if (containerCount <= 0) return;

    int pct = LockboxPctForFloor(currentLevel);
    if (pct <= 0) return;

    /* 这一层该锁几个：整数部分 + 小数部分概率进位 */
    int scaled = containerCount * pct;          /* 分子 */
    int want   = scaled / 100;
    if ((rand() % 100) < (scaled % 100)) want++;

    /* 至少留 LOCKBOX_MIN_UNLOCKED 个不上锁 —— 保险丝要靠它们 */
    int limit = containerCount - LOCKBOX_MIN_UNLOCKED;
    if (want > limit) want = limit;
    if (want <= 0) return;

    int room = want;

    /* 打乱顺序，避免「永远从 0 号箱开始锁」的规律感。 */
    int order[MAX_CONTAINERS];
    for (int i = 0; i < containerCount; i++) order[i] = i;
    for (int i = containerCount - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }

    /* ==================================================================
     * 2026-11-05：**锁型发牌**
     * ------------------------------------------------------------------
     * 原来每箱独立 rand() % LOCK_TYPE_COUNT：
     *   · 同一层可能连出三个「接线」，也可能一整层见不到新锁；
     *   · 6 种锁能不能被玩到，靠运气。
     *
     * 现在按「一副牌」发：本层有 N 个锁箱就从洗好的 6 张里取前 N 张，
     * 所以**同层绝不重复**；牌每层重新洗，
     * 一层最多 6 个锁箱（MAX_CONTAINERS 8 - 2 个保底未上锁），
     * 所以一层里 6 种锁都可能出现，跨层也一定都能轮到。
     * ================================================================ */
    int typeDeck[LOCK_TYPE_COUNT];
    for (int i = 0; i < LOCK_TYPE_COUNT; i++) typeDeck[i] = i;
    for (int i = LOCK_TYPE_COUNT - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = typeDeck[i]; typeDeck[i] = typeDeck[j]; typeDeck[j] = t;
    }

    for (int n = 0; n < containerCount && lockedThisFloor < room; n++) {
        int idx = order[n];
        if (containers[idx].locked) continue;
        /* 数量已经在上面算好了（want），这里不再掷骰 —— 只决定锁哪几个 */
        containers[idx].lockType = typeDeck[lockedThisFloor % LOCK_TYPE_COUNT];
        containers[idx].lockCode = rand() & 0x7FF;
        lockedThisFloor++;
        containers[idx].locked = 1;
    }
}

/* ==================================================================
 * 2026-11-06：舱门放置（一个房间一扇）
 * ------------------------------------------------------------------
 * 是 game.c 的函数而不是 map_rooms.c 的，因为要写 hatch 表，
 * 而 hatch 表在 hatch.c；map_rooms 只负责"告诉我房间在哪"。
 * ================================================================ */
/* ==================================================================
 * 2026-11-06：舱门的**运行时状态**与效果
 * ------------------------------------------------------------------
 * 所有计时器都是"秒"，每帧递减。集中放这里，方便一眼看完。
 * ================================================================ */

/* ==================================================================
 * 2026-11-06：**崩塌**（锅炉房总闸）
 * ------------------------------------------------------------------
 * 用户定义：「黑屏 2 秒钟，然后出现到一个崩塌层。」
 *
 * 时间轴（总长 2.0 秒）：
 *   0.0 ~ 0.8   淡出（screenBlackout 0 -> 1）
 *   0.8         **全黑那一帧**执行重掷（玩家看不到突变）
 *   0.8 ~ 2.0   淡入（1 -> 0）
 *
 * 重掷失败（新层不可通关）→ **回滚到原层**，并提示"这里塌不动"。
 * 用户明确要求回滚，所以必须先把原层完整快照下来。
 * ================================================================ */

/* ==================================================================
 * 2026-11-06（用户实测）：**每层特殊房间预算**
 * ------------------------------------------------------------------
 * 用户原话：
 *   「训练营的时候不要把那个特殊地形给加进去」
 *   「你给每一层都加一个特殊房间上限，不然特殊房间太多了」
 *
 * 抽成纯函数是为了能直接测：走 GenerateLevel 的话，
 * 模板放置失败时会回滚，roomCount 不一定反映"预算"。
 * ================================================================ */
/* ==================================================================
 * 2026-11-06（用户实测）：**死路重连抽成函数，房间之后再跑一次**
 * ------------------------------------------------------------------
 * 用户截图指出「主题房间的出口处也有死路」。
 * 原来这段只在**房间生成之前**跑，房间门外挖出来的新死路没人处理。
 * ================================================================ */
static void BraidDeadEnds(void) {
    /* Collapse maps obey the same connectivity/dead-end bounds. */
    for (int pass = 0; pass < 8; pass++) {
        for (int y = 1; y < MAP_HEIGHT - 1; y++) {
            for (int x = 1; x < MAP_WIDTH - 1; x++) {
                if (worldMap[y][x]) continue;
                int openN = 0;
                if (!worldMap[y][x - 1]) openN++;
                if (!worldMap[y][x + 1]) openN++;
                if (!worldMap[y - 1][x]) openN++;
                if (!worldMap[y + 1][x]) openN++;
                if (openN != 1) continue;
                /* 2026-11-06：**房间内外一律不动**。
                   第二次 braid 跑在房间生成之后，如果不设这条保护，
                   房间角上的凹角会被当成死路 → 打穿房间外墙，
                   房间直接漏到迷宫里（而且主题色的"内侧面"判定也会失效）。 */
                if (cellMeta[y][x].roomId != 0) continue;
                static const int dxs[4] = { 1, -1, 0, 0 };
                static const int dys[4] = { 0, 0, 1, -1 };
                int done = 0;
                for (int k = 0; k < 4 && !done; k++) {
                    int wx = x + dxs[k], wy = y + dys[k];
                    if (wx < 1 || wy < 1 || wx >= MAP_WIDTH - 1 || wy >= MAP_HEIGHT - 1) continue;
                    if (!worldMap[wy][wx]) continue;
                    if (cellMeta[wy][wx].flags & CELLFLAG_ROOMWALL) continue;   /* 房间外墙不许穿 */
                    if (cellMeta[wy][wx].roomId != 0) continue;
                    int bx = wx + dxs[k], by = wy + dys[k];
                    if (bx < 1 || by < 1 || bx >= MAP_WIDTH - 1 || by >= MAP_HEIGHT - 1) continue;
                    if (!worldMap[by][bx]) {
                        worldMap[wy][wx] = 0;
                        done = 1;
                    } else {
                        int c2x = bx + dxs[k], c2y = by + dys[k];
                        if (c2x < 1 || c2y < 1 || c2x >= MAP_WIDTH - 1 || c2y >= MAP_HEIGHT - 1) continue;
                        if (worldMap[c2y][c2x]) continue;
                        worldMap[by][bx] = 0;
                        done = 1;
                    }
                }
            }
        }
    }
}

int ThemeRoomBudgetForLevel(int level,int training) {(void)level;return training?0:1;}


static void ApplyCampaignRules(int stage) {
    /* v6：改成**表驱动** —— 规则细节全在 campaign.c 的 kCampaign[] 里，
       这里只把环境倍率乘进去。加一关只要加一行表。 */


    (void)stage;
    if (!campaignMode) return;
    if (campaignStage < 0 || campaignStage >= CAMPAIGN_STAGE_COUNT) return;
    {
        const CampaignDef* d = &kCampaign[campaignStage];
        modSpeedMul   *= d->speedMul;
        modSanityMul  *= d->sanityMul;
        modBatteryMul *= d->batteryMul;
        modShardMul   *= d->goldMul;
    }
}

static void ResetFloorSystems(void) {

    /* ==================================================================
     * **崩塌标记的生命周期**
     * ------------------------------------------------------------------
     * collapsedLevel 只能在"崩塌重掷"的那一次生成里为 1；
     * 正常换层必须清掉，否则从崩塌层往下走，后面每一层都还是崩塌层。
     * 这个 bug 不会编译报错，只会让玩家觉得"怎么一直在塌"。
     * ================================================================ */
    {
        int wasCollapse = s_collapseGen;
        s_collapseGen = 0;
        if (!wasCollapse) collapsedLevel = 0;
    }

    /* 2026-11-06：每层重建舱门表与所有舱门计时器 —— 舱门是"本层装置"，
       不跨层保留（跨层保留要动存档，见后续 SAVE_VERSION 33）。 */
    HatchReset();
    hatchLightsT = hatchGoldMulT = hatchBloodT = hatchSpeedT = 0.0f;
    BestiaryBeginFloor();
    SuspicionReset();   /* v7：新一层，清空可疑点 */
    {   /* v7：新一层，清空地上的钱袋 */
        MoneyBagsResetForNewFloor();
    }
    {  HintsReset(); }   /* v7 B4: 一次性提示 */
    {  WardenReset(); }   /* v7 B1: 新一层清掉守门人 */
    {   /* v7：清道夫计时 / 连躲计数 / 重置（每层都清） */


        ScavengerReset(); floorTime = 0.0f; hideStreak = 0;
    {  EchoMarksReset(); }   /* v2.4.62 换层清空回声标记 */
    { 
      FlankBuild(); ResetSecondSpawn();
      {  CatchCountResetFloor(); }
    }   /* v2.4.94 封喉表 + 第二只计时 + 理智档位重置 */
    }
    {   /* v7：拾荒者按强度档决定是否出现 */
        CollectorReset();
    }
    {   /* v7：扳手次数每层补满 */


        wrenchCharges = RunUpgWrenchCharges();
        wrenchLastUsed = 0;
        wrenchCd = 0.0f;
    }
    /* v2.0 修复：Boss 标记必须在**任何 SpawnMonster() 之前**设置。
       原来它写在函数中段（怪物已经刷出来之后），导致第 6/12/18 层的
       第一只怪是普通血量，被打死重生后才 ×8 —— Boss 整体错位一层。 */
    /* ===== v7 **BOSS 已删除** =====
       它原本只是「听声怪 x 血量」，没有任何独有机制 ——
       三个 Boss 层的差别只有 hp 倍率和速度，玩家分不出来。
       现在恒为 0：不再有 Boss 层，第 6/12/18 层就是普通层
       （怪物强度靠 v7 的 T0~T15 十六档强度档位来给）。
       保留 bossLevel 变量，免得 save / HUD / shop 的引用断掉。 */
    bossLevel = 0;
    bossDefeated = 0;
    monsterMuted = trainingMode ? 1 : 0;   /* 训练场里怪不叫 */
}

static void GenerateMazeTopology(void) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) { worldMap[y][x] = 1; explored[y][x] = 0; }

    /* DFS 递归回溯（数组栈） */
    int stackX[MAP_WIDTH * MAP_HEIGHT], stackY[MAP_WIDTH * MAP_HEIGHT], top = 0;
    int cx = 1, cy = 1;
    worldMap[cy][cx] = 0; stackX[top] = cx; stackY[top] = cy; top++;
    int dxs[] = {2, 0, -2, 0}, dys[] = {0, 2, 0, -2};

    while (top > 0) {
        cx = stackX[top - 1]; cy = stackY[top - 1];
        int validDirs[4], count = 0;
        for (int i = 0; i < 4; i++) {
            int nx = cx + dxs[i], ny = cy + dys[i];
            if (nx > 0 && nx < MAP_WIDTH - 1 && ny > 0 && ny < MAP_HEIGHT - 1 && worldMap[ny][nx] == 1)
                validDirs[count++] = i;
        }
        if (count > 0) {
            int dir = validDirs[rand() % count];
            int nx = cx + dxs[dir], ny = cy + dys[dir];
            worldMap[cy + dys[dir] / 2][cx + dxs[dir] / 2] = 0;
            worldMap[ny][nx] = 0;
            stackX[top] = nx; stackY[top] = ny; top++;
        } else top--;
    }

    /* 打破死胡同，生成环路 */
    for (int y = 1; y < MAP_HEIGHT - 1; y++) {
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (worldMap[y][x] == 1 &&
                ((worldMap[y][x - 1] == 0 && worldMap[y][x + 1] == 0) ||
                 (worldMap[y - 1][x] == 0 && worldMap[y + 1][x] == 0))) {
                /* ==================================================================
                 * 2026-11-06（第二轮实测修正）：45% -> 22%
                 * ------------------------------------------------------------------
                 * 用户反馈：「空旷场地太多了，迷宫不够。」
                 *
                 * 45% 是我上一轮为了"多路径"拍的，但 36x36 的偶数格其实很多，
                 * 45% 打通就把迷宫填成了大厅 —— 反而更难认路。
                 *
                 * 22% 仍然比最初的 30% 更低，但 36x36 的基础骨架更宽，
                 * 绝对环路数依然够"绕开守门人"。
                 * ================================================================ */
                /* 崩塌层把环路打得更碎（8%）：用户要的"有些路会被堵死" */
                if (rand() % 100 < (collapsedLevel ? 8 : 22)) worldMap[y][x] = 0;
            }
        }
    }

    /* ==================================================================
     * 2026-11-06（用户实测）：**死路太多**
     * ------------------------------------------------------------------
     * 用户原话：「目前这个算法导致地图内很多死路，导致游玩体验很差」。
     *
     * DFS 生成的迷宫天生多死路 —— 每棵树的分支末端都是死路。
     * 环路打通（上面那一步）只能随机消灭一部分，剩下的仍然很多。
     *
     * 这里加一个针对性回合：**逐个处理死路格**
     *   死路格 = 四邻里只有 1 个是空地的位置
     *   对每个死路格，往它的墙方向看 1~2 格，如果那边是空地，
     *   就把中间那堵墙打掉 —— 死路立刻变成环。
     *
     * 只在能接上时才打，接不上就保留（不会凭空挖出新的死路）。
     * 崩塌层跳过：那时候本来就是故意要"路更堵"。
     * ================================================================ */
    BraidDeadEnds();

    /* 随机小房间（严格限制在内部格，避免打穿边界墙）
       2026-11-06：5 -> 3。每个 blob 是 2x2 的强制空地，
       36x36 上 5 个会把本来就减少的墙体再挖薄。 */
    for (int i = 0; i < 3; i++) {
        int rx = 2 + rand() % (MAP_WIDTH - 4);
        int ry = 2 + rand() % (MAP_HEIGHT - 4);
        for (int oy = 0; oy < 2; oy++)
            for (int ox = 0; ox < 2; ox++)
                worldMap[ry + oy][rx + ox] = 0;
    }

    /* ==================================================================
     * 2026-11-06（v2.4-rc C3）：**主题房间**
     * ------------------------------------------------------------------
     * 放在上面那 5 个 2x2 小房间**之后**：
     *   · 开关关闭时，上面那段代码一字未改 → 地图与 v2.4-beta **逐位相同**
     *   · 房间随后覆盖上去，所以房间里的柱子不会被 blob 打穿
     *
     * 放不下就跳过（GenerateThemeRooms 内部处理），
     * 绝不因为房间放不下让整层生成失败。
     * ================================================================ */
#if CANITY_ROOMS
    CellMetaReset();
    {

        /* ==================================================================
         * 2026-11-06（用户实测）：**训练营不要特殊房间；并且要给每层加上限**
         * ------------------------------------------------------------------
         * 用户原话：
         *   「训练营的时候不要把那个特殊地形给加进去」
         *   「你给每一层都加一个特殊房间上限，不然特殊房间太多了」
         *
         * 训练营里房子会把出生区/教程路径切碎，走起来像穿模；
         * 而且房间的墙在地图种子里是**后期覆盖**上去的，
         * 教程那种小块地图上覆盖很容易压到关键格。
         *
         * 上限：前两层 1 间，之后 2 间，硬上限 MAX_THEME_ROOMS_PER_FLOOR。
         * ================================================================ */
        int want = ThemeRoomBudgetForLevel(currentLevel, trainingMode);
        /* v2.4-rc：记下这一阶段的种子。读档后用它重放，
           否则每层重新载入都会丢掉房间的色调、家具和碰撞。 */
        /* 第 6 轮：**新开局一律用版本 2（xorshift32）**。
           老存档读进来时 SaveLoad 会把版本设回 0/1，所以那里的重放仍走 rand()。
           这里显式设 2，确保新档的房间布局跨平台一致。 */
        {  rng_set_version(2); }
        roomSeedUsed = (int)(rng_world_next() & 0x7FFFFFFF);
        if (want > 0) {
            { 
              rng_world_seed((unsigned)roomSeedUsed); }
            (void)GenerateThemeRooms(want);
        } else {
            /* ==================================================================
             * 2026-11-06（用户实测）：**roomCount 是上一次的陈旧值**
             * ------------------------------------------------------------------
             * roomCount 只在 GenerateThemeRooms 里被抬高 / 回滚时重算。
             * want == 0（训练营、或房间放不下）时根本没人调它，
             * 于是它保留上一层的值 —— 训练营里 roomCount != 0，
             * 依赖它的地方（出口过滤、渲染补光、小地图）全都以为有房间。
             * 这里显式清零。
             * ================================================================ */

            roomCount = 0;
        }
    }
#else
    CellMetaReset();
#endif
    /* 2026-11-06：房间门可能挖出新的盲道 —— 再braid 一次 */
    BraidDeadEnds();

    /* DARK 词条（手电起始电量低）的处理放在掷出词条之后 ——
       原来它写在掷词条之前，用的是**上一层**的 levelMod，
       导致「拿到 DARK 的这一层不生效，要等下一层才生效」。 */

}

static void CollectFloorCandidates(LevelBuildWorkspace* work) {
    if(!campaignMode){posX = 1.5f; posY = 1.5f;}
    dirX = 1.0f; dirY = 0.0f;
    ApplyFov();

    /* 出生区只挖内部格 1..3，第 0 行/列必须保持是墙 */
    for (int y = 1; y <= 3; y++)
        for (int x = 1; x <= 3; x++) worldMap[y][x] = 0;

    /* 出口门：固定在出生点正前方（右侧），锁死等待配电箱通电 */
    /* 生成迷宫时保证出生点右侧有通路 */
    worldMap[1][2] = 0; worldMap[1][3] = 0;  /* 确保右侧有路 */
    if(!campaignMode)FindFarthestCell(&exitX, &exitY);   /* v2.1：离出生点最远的空格 */

    /* ==================================================================
     * 2026-11-06（v2.4-rc）：**容器候选必须从出生点可达**
     * ------------------------------------------------------------------
     * 1000 种子洪泛发现：DFS 迷宫 + 5 个随机 2x2 空地会造出
     * 与出生点**不连通**的孤立空间，平均占 1.96% 的可通行格。
     *
     * 而下面这段候选列表原来是"所有空地" —— 于是保险丝/配电箱
     * 有概率被放进孤岛里，整层直接不可通关。
     * 这是**真正的死局来源**，比"出口选错"隐蔽得多。
     *
     * 现在先做一次从出生点出发的 BFS，只把可达格收进候选。
     * ================================================================ */
    static int reach[MAP_HEIGHT][MAP_WIDTH];
    {
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) reach[y][x] = 0;
        int rqx[MAP_WIDTH * MAP_HEIGHT], rqy[MAP_WIDTH * MAP_HEIGHT];
        int rh = 0, rt = 0;
        int sx=(int)posX,sy=(int)posY;
        if(sx>0 && sy>0 && sx<MAP_WIDTH-1 && sy<MAP_HEIGHT-1 && !worldMap[sy][sx]){reach[sy][sx]=1;rqx[rt]=sx;rqy[rt]=sy;rt++;}
        while (rh < rt) {
            int cx = rqx[rh], cy = rqy[rh]; rh++;
            static const int ddx[4] = { 1, -1, 0, 0 };
            static const int ddy[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; k++) {
                int nx = cx + ddx[k], ny = cy + ddy[k];
                if (nx < 1 || nx >= MAP_WIDTH - 1 || ny < 1 || ny >= MAP_HEIGHT - 1) continue;
                if (worldMap[ny][nx]) continue;
                {  if (PropBlocksAt(nx, ny)) continue; }
                if (reach[ny][nx]) continue;
                reach[ny][nx] = 1;
                rqx[rt] = nx; rqy[rt] = ny; rt++;
            }
        }
    }

    /* 收集空地用于放置容器/配电箱/安全屋（排除出生区、出口周围、不可达区） */
    work->emptyCount = 0;
    for (int y = 1; y < MAP_HEIGHT - 1; y++) {
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            if (worldMap[y][x] == 0 && reach[y][x] &&
                Dist2(x+.5f,y+.5f,posX,posY) > 16.0f) {
                work->cells[work->emptyCount][0] = x;
                work->cells[work->emptyCount][1] = y;
                work->emptyCount++;
            }
        }
    }

    /* ==================================================================
     * 2026-11-06（v2.4-rc P-6）：**这里原来是无条件覆盖**
     * ------------------------------------------------------------------
     * 旧代码在这一行又跑了一次 FindFarthestCell，把上面刚算好的
     * 战役出口**直接盖掉** —— 那个分支等于从来没生效过。
     * 现在只在"不是战役 3/5 关"时兜底：那两关的出口挪到配电箱
     * 定稿之后再算，那时 breaker.x/y 才是本层的真实位置。
     * ================================================================ */
    {
        if (!(CampaignExitDistNow() > 1.01f)) {
            if(!campaignMode)FindFarthestCell(&exitX, &exitY);
        }
    }
    /* ==================================================================
     * 2026-11-06（v2.4-rc / D）：**撤离点必须有两条以上通路**
     * ------------------------------------------------------------------
     * 现象：撤离点只有一个方向能进，守门人站在那条路上就完全堵死。
     * 做法：数四邻开口，少于 2 个就打通一堵"背后还有空地"的墙。
     * 只在可能时才动（找不到就放弃），不会把地图挖乱。
     * ================================================================ */
    {
        int ex = (int)exitX, ey = (int)exitY;
        if (ex >= 1 && ey >= 1 && ex < MAP_WIDTH - 1 && ey < MAP_HEIGHT - 1 && !worldMap[ey][ex]) {
            static const int dx4[4] = { 1, -1, 0, 0 };
            static const int dy4[4] = { 0, 0, 1, -1 };
            int openN = 0;
            for (int k = 0; k < 4; k++) {
                int nx = ex + dx4[k], ny = ey + dy4[k];
                if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
                if (!worldMap[ny][nx]) openN++;
            }
            if (openN < 2) {
                for (int k = 0; k < 4; k++) {
                    int wx = ex + dx4[k], wy = ey + dy4[k];
                    if (wx < 1 || wy < 1 || wx >= MAP_WIDTH - 1 || wy >= MAP_HEIGHT - 1) continue;
                    if (!worldMap[wy][wx]) continue;              /* 已是通路 */
                    int bx = wx + dx4[k], by = wy + dy4[k];
                    if (bx < 1 || by < 1 || bx >= MAP_WIDTH - 1 || by >= MAP_HEIGHT - 1) continue;
                    if (worldMap[by][bx]) continue;               /* 后面还得是空地 */
                    worldMap[wy][wx] = 0;                         /* 打通第二条路 */
                    break;
                }
            }
        }
    }

}

static int ConfigureFloorResources(void) {
    breakerOpen = 0;  /* 出口门锁死，需通电开启 */

    /* 容器 / 保险丝 / 配电箱 */
    /* v3.0：每层生成 3~5 个藏身点 */
    hideSpotCount = 0;
    hiddenIn = -1;
    hideSpotNear = -1;
    for (int i = 0; i < HIDESPOT_MAX; i++) hideSpots[i].active = 0;

    if (lockActive) CloseLockOverlay();   /* v3.0：换层时别把锁盘带进新关 */
    for (int i = 0; i < MAX_CONTAINERS; i++) {
        containers[i].active = 0;
        containers[i].locked = 0;      /* v3.0：换层清锁 */
        containers[i].lockType = 0;
        containers[i].lockCode = 0;
        containers[i].searched = 0;
    }
    lockedThisFloor = 0;   /* v3.0：每层重新计数 */
    containerCount = 0;
    fusesHeld = 0;
    breakerOpen = 0;
    hoverKind = 0;
    hoverIndex = -1;
    holdTimer = 0.0f;
    breaker.active = 0;

    /* 随机词条：第 2 关起，60% 概率
       两条分支都消耗同样次数的 rand()，保证后续随机序列对齐。 */
    {
        int roll100 = rand() % 100;
        int pick = rand() % MOD_COUNT;
        if (g_forceMod == -2) {
            levelMod = -1;
            modSanityMul = 1.0f;
            modSpeedMul = 1.0f;
        } else if (g_forceMod >= 0) {
            levelMod = g_forceMod % MOD_COUNT;
            modSanityMul = 1.0f;
            modSpeedMul = 1.0f;
            if (levelMod == 1) modSpeedMul = 1.25f;
            else if (levelMod == 3) modSanityMul = 1.35f;
            else if (levelMod == 5) modSanityMul = 0.80f;
        } else {
            RollLevelMod(roll100, pick);
        }
    }
    /* 词条倍率已在上面重置并抽取；战役规则现在叠加到本层基础倍率上，
       这样首层和后续层都会生效，同时不会覆盖随机词条。 */
    /* ==================================================================
     * v6 第 ㉑ 轮：**每日任务的修正终于生效了**
     * ==================================================================
     * 第 ⑦ 轮写好 daily.c 的 6 个修正函数，但**一个调用点都没有** ——
     * 玩家看到的每日任务只有"目标"，没有"世界规则变了"。
     *
     * 这里在事件表之后叠一层，顺序：基础 → 层词条 → 事件 → 战役 → 每日
     * ================================================================ */
    if (dailyMode) {
        modSpeedMul   *= DailyMonsterSpeedMul();
        modBatteryMul *= DailyBatteryDrainMul();
        modShardMul   *= DailyGoldMul();
        modNoiseMul   *= DailyNoiseMul();
    }

    if (campaignMode) ApplyCampaignRules(campaignStage);
    levelBanner = 5.0f;

    /* DARK：手电起始电量只有 55%。必须放在掷词条之后，
       否则用的是上一层的 levelMod，词条会延迟一层生效。 */
    if (levelMod == 0) battery = (float)BatteryMax() * 0.55f;

    /* 本关需要的保险丝：2 根起，每 3 关 +1，最多 4（主线）；无尽模式封顶到 MAX_CONTAINERS */
    /* ==================================================================
     * v7 第 ㊿ 轮：**战役模式用 kCampaign[].fuses**
     * ==================================================================
     * 那个字段填好了（4/3/6/3·3·4/8）但全工程 0 个读取点，
     * 于是 5 关的保险丝全走普通曲线 —— 战役规模差异完全不成立。
     * ================================================================ */
    if (campaignMode) {
        int cf = CampaignFusesNow();
        if (cf >= 2 && cf <= MAX_CONTAINERS) {
            fusesNeeded = cf;
            fusesNeeded -= (HasKeystone(SK_FUSE_LESS) ? 1 : 0) + RunFuseLess() + SkillOwned(SK_SHORTCUTJR);
            if (fusesNeeded < 2) fusesNeeded = 2;
        } else {
            fusesNeeded = 2 + currentLevel / 3;
            if (fusesNeeded > 6) fusesNeeded = 6;
        }
    } else if (isEndlessRun) {
        fusesNeeded = 2 + currentLevel / 3;   /* 见下方与箱子数联动的夹取 */
        if (fusesNeeded > MAX_CONTAINERS) fusesNeeded = MAX_CONTAINERS; /* 新增限制 */
    } else {
        /* v3.0 平衡：**不再封顶 4** ——
           原来第 6 层之后恒定 4 根，18 层零变化，玩家感觉不到难度推进。
           新曲线：每 3 层 +1，封顶 6；并且不许多于「箱子数 - 2」，
           给电池/药/投掷物留出货位（原来无尽第 18 层 8 根 = 全箱都是保险丝）。 */
        fusesNeeded = 2 + currentLevel / 3;
        if (fusesNeeded > 6) fusesNeeded = 6;
        fusesNeeded -= (HasKeystone(SK_FUSE_LESS) ? 1 : 0) + RunFuseLess() + SkillOwned(SK_SHORTCUTJR);
        if (fusesNeeded < 2) fusesNeeded = 2;
    }

    /* 容器两阶段生成：
       阶段1：必须先放够 fusesNeeded 个箱子（每个保险丝一个箱子）
       阶段2：继续填充到 contCap（词条 RICH/CRAMPED 影响这个上限） */
    int baseContainers = CONTAINERS_BASE - 2 + (rand() % 3);
    /* 无尽模式：保底容器数随层数增长，确保始终有足够箱子放保险丝 */
    if (isEndlessRun) {
        baseContainers += currentLevel / 5;  /* 每 5 层 +1 保底 */
    }
    /* ==================================================================
     * v7 第 ㊿ 轮：**保险丝曲线被容器数夹死**
     * ==================================================================
     * 曲线本来写的是 fusesNeeded = 2 + currentLevel/3（封顶 6），
     * 但箱子只有 3~5 个，紧接着这一行：
     *     if (!campaignMode && fusesNeeded > contCap - 2) fusesNeeded = contCap - 2;
     * 把曲线整个压回 2~3 根 —— **24 层没有任何爬升**。
     *
     * 审计原话：「第 2 层和第 20 层做的事一模一样」。
     *
     * 修法：反着来 —— 先让箱子数**跟着保险丝需求涨**（保险丝 + 2 个余量给
     * 电池/药/投掷物），再夹到 MAX_CONTAINERS。这样曲线才真的生效。
     * ================================================================ */
    /* ==================================================================
     * 2026-11-06（用户要求）：**地图变大，箱子也要变多**
     * ------------------------------------------------------------------
     * 36x36 的可走面积是 24x24 的两倍多，而箱子数一直是"保险丝 + 2"，
     * 于是搜刮密度被摊薄了一半 —— 走很久遇不到一个箱子。
     *
     * 这里在原来基础上按地图规模补一档：
     *   24x24 时代 +2 够用；现在按面积比给 +4（并夹到 MAX_CONTAINERS）。
     * ================================================================ */
    if (baseContainers < fusesNeeded + 4) baseContainers = fusesNeeded + 4;
    if (baseContainers > MAX_CONTAINERS) baseContainers = MAX_CONTAINERS;
    if (baseContainers < 5) baseContainers = 5;

    int contCap = (int)(ContainerTargetCount(levelMod, baseContainers) * DailyCrateCountMul() + 0.5f);

    /* ==================================================================
     * 2026-11-06（用户实测）：**箱子数量不足会让舱门永远打不开**
     * ------------------------------------------------------------------
     * 用户原话：「房间有时候不刷新足够的箱子，导致大箱子打开不了。」
     *
     * 成因是两层削减叠加：
     *   · CRAMPED 词条  -2
     *   · 每日挑战 DailyCrateCountMul() 可能 < 1
     * 于是 contCap 被压到只剩 fusesNeeded —— 那一层所有箱子都是保险丝，
     * 一个金币都搜不到，而仓储室的门槛是 150 金。
     *
     * 修法：给 contCap 一个下限 **fusesNeeded + 3**，
     * 保证至少有 3 个非保险丝箱子能出金币/道具。
     * 上限仍然是 MAX_CONTAINERS。
     * ================================================================ */
    if (contCap < fusesNeeded + 3) contCap = fusesNeeded + 3;
    if (contCap > MAX_CONTAINERS) contCap = MAX_CONTAINERS;
    if (contCap < fusesNeeded) contCap = fusesNeeded;

    /* v3.0 平衡：保险丝不许多于「箱子数 - 2」——
       否则整层所有箱子都是保险丝，电池/药/投掷物一件都搜不到。
       （上面的 baseContainers 已经保证了 fusesNeeded + 2，这里通常不再触发） */
    if (!campaignMode && fusesNeeded > contCap - 2) fusesNeeded = contCap - 2;
    if (fusesNeeded < 2) fusesNeeded = 2;


    return contCap;
}

static void PlaceFloorContainers(LevelBuildWorkspace* work) {
    int attempts = 0;
    /* 阶段1：保证 fusesNeeded 个箱子 */
    while (work->emptyCount>0 && containerCount < fusesNeeded && containerCount < MAX_CONTAINERS && attempts < 512) {
        attempts++;
        int idx = rand() % work->emptyCount;
        float cx = work->cells[idx][0] + 0.5f, cy = work->cells[idx][1] + 0.5f;
        if (Dist2(cx, cy, exitX, exitY) < 9.0f) continue;
        /* ==================================================================
         * 2026-11-06（用户要求）：**主题房间里不再刷箱子**
         * ------------------------------------------------------------------
         * 用户原话：「里面家具有很多，导致那个资源溢出，导致通货膨胀」。
         *
         * 房间本来就有 3~6 件家具，再叠上箱子，一间房就是一个补给站。
         * 现在房间只作**场景与危险**（含控制闸），不产资源；
         * 箱子一律刷在普通迷宫里。箱子总数不变（fusesNeeded + 3），
         * 只是不再堆在房间里。
         * ================================================================ */
        {

            int mx = (int)cx, my = (int)cy;
            if (mx >= 0 && my >= 0 && mx < MAP_WIDTH && my < MAP_HEIGHT &&
                cellMeta[my][mx].roomId != 0) continue;
        }
        if (!SpotClear(cx, cy, 1.6f, 1, -1)) continue;

        int ok = 1;
        for (int k = 0; k < containerCount; k++)
            if (Dist2(cx, cy, containers[k].x, containers[k].y) < 2.0f) { ok = 0; break; }
        if (!ok) continue;

        containers[containerCount].x = cx;
        containers[containerCount].y = cy;
        containers[containerCount].searched = 0;
        containers[containerCount].content = 0;  /* 先占位，后面再分发内容 */
        containers[containerCount].active = 1;
        containerCount++;
    }
    /* v3.0：藏身点 —— 两轮放置：
       第一轮按正常约束挑（避开出口/容器/配电箱/彼此）
       第二轮**放宽**（只要求不是墙），保证一定生得出来。
       之前只做第一轮，结果 SpotClear 把候选全拒了 → 一个都没有
       （用户实测 hideSpotCount=0）。 */
    hideSpotCount = 0;
    {
        /* ==================================================================
         * 2026-11-06（用户要求）：**隐藏点之间必须有最小间距**
         * ------------------------------------------------------------------
         * 「你要有一个隐藏点的距离设置，一定区域之内不能出现两个隐藏点，
         *   不然没什么用。」
         *
         * 原来第一轮只要求 2 格、兜底更是只有 1.2 格 ——
         * 同一段走廊里能出两三个桶，等于一段路给三次免死，
         * 而地图另一头一个都没有。
         *
         * 现在硬性要求：
         *   第一轮 7 格   （主约束）
         *   第二轮 5 格   （兜底放宽，但绝不低于它）
         * 宁可少放一个，也不要在同一片区域堆两个。
         * ================================================================ */
        const float HIDESPOT_MIN_GAP2      = 49.0f;   /* 7 格 */
        const float HIDESPOT_MIN_GAP2_RELAX = 25.0f;  /* 5 格（兜底下限） */
        /* ==================================================================
         * 2026-11-06（用户要求）：藏身点"稍微增加一点，随层数逐渐减少"
         * ------------------------------------------------------------------
         * 原来是 2~3 个、与层数无关。地图 36x36 之后一层要跑更久，
         * 2~3 个藏身点在长图里几乎遇不到。
         *
         * 现在：**第 1 层最多 6 个，越深越少，最低 3 个**。
         * 递减让后期的紧张感回升 —— 前期给你工具熟悉，后期收回。
         * 上限 6 等于 HIDESPOT_MAX，不触碰存档布局。
         * ================================================================ */
        int want = 6 - (currentLevel - 1) / 5;
        if (want > HIDESPOT_MAX) want = HIDESPOT_MAX;
        if (want < 3) want = 3;
        /* 抖 1 个，避免每层数量完全可预测 */
        want += (rand() % 2) ? 0 : -1;
        if (want < 3) want = 3;
        if (want > HIDESPOT_MAX) want = HIDESPOT_MAX;

        /* ---- 第一轮：严格 ---- */
        for (int tries = 0; tries < 300 && hideSpotCount < want && work->emptyCount > 0; tries++) {
            int idx = rand() % work->emptyCount;
            float hx2 = work->cells[idx][0] + 0.5f, hy2 = work->cells[idx][1] + 0.5f;
            if (Dist2(hx2, hy2, exitX, exitY) < 6.0f) continue;
            if (!SpotClear(hx2, hy2, 1.4f, 1, -1)) continue;
            int bad = 0;
            for (int k = 0; k < hideSpotCount; k++)
                if (Dist2(hx2, hy2, hideSpots[k].x, hideSpots[k].y) < HIDESPOT_MIN_GAP2) { bad = 1; break; }
            if (bad) continue;
            hideSpots[hideSpotCount].x = hx2;
            hideSpots[hideSpotCount].y = hy2;
            hideSpots[hideSpotCount].kind = rand() % 3;
            hideSpots[hideSpotCount].active = 1;
            hideSpotCount++;
        }

        /* ---- 第二轮：兜底，只要求脚下不是墙 ---- */
        for (int tries = 0; tries < 400 && hideSpotCount < want && work->emptyCount > 0; tries++) {
            int idx = rand() % work->emptyCount;
            float hx2 = work->cells[idx][0] + 0.5f, hy2 = work->cells[idx][1] + 0.5f;
            /* 兜底也不能贴着出口，否则藏身点会和离关目标重叠，
               造成玩家按 E 后看似藏好、实际却站在出口触发区里的错觉。 */
            if (Dist2(hx2, hy2, exitX, exitY) < 6.0f) continue;
            int bad = 0;
            for (int k = 0; k < hideSpotCount; k++)
                if (Dist2(hx2, hy2, hideSpots[k].x, hideSpots[k].y) < HIDESPOT_MIN_GAP2_RELAX) { bad = 1; break; }
            if (bad) continue;
            /* 兜底也不能压在箱子/配电箱上，否则垃圾桶会被道具盖住，
               玩家只看到 HIDE 提示却找不到桶。 */
            if (!SpotClear(hx2, hy2, 0.9f, 1, -1)) continue;
            hideSpots[hideSpotCount].x = hx2;
            hideSpots[hideSpotCount].y = hy2;
            hideSpots[hideSpotCount].kind = rand() % 3;
            hideSpots[hideSpotCount].active = 1;
            hideSpotCount++;
        }
    }


    /* 阶段2：继续填充到 work->contCap（受词条影响） */
    attempts = 0;
    /* v3.3：**必须防 work->emptyCount==0** —— 下面有 rand() % work->emptyCount，
       空地为 0 时是整数除零，x86 上直接 SIGFPE 崩溃（不是返回 0）。
       地图极端退化时（整层都是墙）会走到这里。 */
    while (containerCount < work->contCap && containerCount < MAX_CONTAINERS
           && work->emptyCount > 0 && attempts < 512) {
        attempts++;
        int idx = rand() % work->emptyCount;
        float cx = work->cells[idx][0] + 0.5f, cy = work->cells[idx][1] + 0.5f;
        if (Dist2(cx, cy, exitX, exitY) < 9.0f) continue;
        if (!SpotClear(cx, cy, 1.6f, 1, -1)) continue;

        int ok = 1;
        for (int k = 0; k < containerCount; k++)
            if (Dist2(cx, cy, containers[k].x, containers[k].y) < 2.0f) { ok = 0; break; }
        if (!ok) continue;

        containers[containerCount].x = cx;
        containers[containerCount].y = cy;
        containers[containerCount].searched = 0;
        containers[containerCount].content = 0;
        containers[containerCount].active = 1;
        containerCount++;
    }

    /* v3.3 **折中兜底** ——
       上面那轮要求容器离藏身点 1.6 格。加了这条检查之后，
       小地图里可能放不满 work->contCap，而**箱子少 = 保险丝少 = 整层卡关**，
       比视觉重叠严重得多。
       所以再补一轮宽松放置：只要求离藏身点 0.9 格（不压在同一格），
       离其它容器仍然 1.2 格。这样最坏情况是「挨着」而不是「叠在一起」。 */
    attempts = 0;
    while (containerCount < work->contCap && containerCount < MAX_CONTAINERS
           && work->emptyCount > 0 && attempts < 256) {
        attempts++;
        int idx = rand() % work->emptyCount;
        float cx = work->cells[idx][0] + 0.5f, cy = work->cells[idx][1] + 0.5f;
        if (Dist2(cx, cy, exitX, exitY) < 9.0f) continue;

        int ok = 1;
        for (int k = 0; k < containerCount; k++)
            if (Dist2(cx, cy, containers[k].x, containers[k].y) < 1.44f) { ok = 0; break; }
        if (!ok) continue;
        for (int k = 0; k < hideSpotCount; k++) {
            if (!hideSpots[k].active) continue;
            if (Dist2(cx, cy, hideSpots[k].x, hideSpots[k].y) < 0.81f) { ok = 0; break; }
        }
        if (!ok) continue;

        containers[containerCount].x = cx;
        containers[containerCount].y = cy;
        containers[containerCount].searched = 0;
        containers[containerCount].content = 0;
        containers[containerCount].active = 1;
        containerCount++;
    }

        /* v28：容器数量已经确定，统一判定这一层有哪些箱子上了锁。 */
        /* 2026-11-05：宝箱怪必须在 ApplyLockboxes **之前**替换掉箱子 ——
       否则那个箱子会先被发一把锁，变成"上锁的宝箱怪"（铁律四十九）。 */
}

static void DistributeFloorLoot(void) {
    MimicReset();
    MimicTrySpawn(currentLevel, containerCount);
    ApplyLockboxes();
    for(int i=0;i<containerCount;i++){int x=(int)containers[i].x,y=(int)containers[i].y;
        if(x>=0 && y>=0 && x<MAP_WIDTH && y<MAP_HEIGHT && cellMeta[y][x].roomId){containers[i].locked=1;containers[i].lockType=(currentLevel+cellMeta[y][x].themeId)%LOCK_TYPE_COUNT;containers[i].lockCode=(int)(rng_world_next()&0xFFF);}}


        /* 修复死局 Bug：优先保证能通关（放满 fusesNeeded）。
   只有在箱子极其充裕（多出 2 个以上）时，才留格子给其他物品。 */
        int need = fusesNeeded;
        if (need > containerCount) {
            need = containerCount; // 极端兜底，防止越界
        }
        /* ==================================================================
         * 2026-11-05：**保险丝改成「先未上锁、再锁箱」两遍填充**
         * ------------------------------------------------------------------
         * 原来是「保险丝绝对不许进锁箱」，而锁箱现在最高能占到 80%（8 箱锁 6）。
         * 那样未上锁的只剩 2 个，深层却需要 6 根保险丝 ——
         * 物理上放不下，整层直接无解（比"开不了锁"更严重的死锁）。
         *
         * 现在两遍：
         *   ① 先把保险丝塞进**未上锁**的箱子 —— 保证玩家不开锁也有保底路线；
         *   ② 还有剩的再塞进锁箱 —— 深层必须开锁才能凑齐，这就是锁箱的代价。
         * 因为 ① 只有 2 个空位，深层至少 2 根一定拿得到，不会一开局就卡死。
         * ================================================================ */
        {
            /* 打乱一遍箱子顺序，避免「保险丝永远在前几个箱子」的规律感 */
            int ord[MAX_CONTAINERS];
            for (int i = 0; i < containerCount; i++) ord[i] = i;
            for (int i = containerCount - 1; i > 0; i--) {
                int j = rand() % (i + 1);
                int t = ord[i]; ord[i] = ord[j]; ord[j] = t;
            }
            /* ============================================================
             * 2026-11-05 用户：「锁箱里必定有保险丝」+「至少 1 根不用开锁」
             * ------------------------------------------------------------
             * 三遍填：
             *   ① **锁箱优先**吃保险丝 —— 开锁=有明确回报，不是白费力气；
             *      但每留一根（keepFree）给未上锁的箱子
             *   ② 未上锁的箱子填剩下的
             *   ③ 万一未上锁的箱子不够，再回填锁箱
             *
             * 结果：深层每个锁箱都装着保险丝，而玩家**永远有 1 根**
             * 不开锁就能拿到，不会因为开锁不熟练直接卡死整层。
             * ============================================================ */
            const int keepFree = 1;
            for (int pass = 0; pass < 3 && need > 0; pass++) {
                for (int oi = 0; oi < containerCount && need > 0; oi++) {
                    int k = ord[oi];
                    if (containers[k].content != 0) continue;
                    if (pass == 0) {                       /* ① 锁箱，留 keepFree */
                        if (!containers[k].locked) continue;
                        if (need <= keepFree) continue;
                    } else if (pass == 1) {                /* ② 未上锁 */
                        if (containers[k].locked) continue;
                    } else {                               /* ③ 回填锁箱 */
                        if (!containers[k].locked) continue;
                    }
                    containers[k].content = 1;
                    need--;
                }
            }
        }
        for (int k = 0; k < containerCount; k++) {
            if (containers[k].content == 0) {
                int r = rand() % 100;
                /* v3.3 掉落表（投掷物重做后按新的 6 槽位重新分配）
                   content: 2 电池 / 3 药 / 4 光球 / 5 投石 /
                            6 烟雾 / 7 胶水 / 8 回声 / 9 诱饵 / 0 空
                   配比思路：投石最多（基础诱饵）、光球次之（照明刚需）、
                   烟雾和胶水是战术道具、回声和诱饵稀有。 */
                /* v3.4（方案 v5 §7）**箱子改成金币来源** ——
                   用户原话：「箱子太多了，我希望是在商店里面能买」。
                   所以箱子不再自动给道具：金币 35% / 空 25% / 道具 40%。
                   道具主要靠商店买，「找箱子」这件事的价值转移到金币上。 */
                if      (r < 35) containers[k].content = 10;  /* 金币     35% */
                else if (r < 50) containers[k].content = 2;   /* 电池     15% */
                else if (r < 62) containers[k].content = 3;   /* 药       12% */
                else if (r < 70) containers[k].content = 5;   /* 投石      8% */
                else if (r < 78) containers[k].content = 4;   /* 光球      8% */
                else if (r < 83) containers[k].content = 6;   /* 烟雾      5% */
                else if (r < 88) containers[k].content = 7;   /* 胶水      5% */
                else if (r < 92) containers[k].content = 8;   /* 回声      4% */
                else if (r < 95) containers[k].content = 9;   /* 诱饵      3% */
                else             containers[k].content = 0;   /* 空       25% */
            }
        }

    /* ==================================================================
     * 2026-11-06（v2.4-rc P-6）：**战役第 3/5 关的出口在这里才算**
     * ------------------------------------------------------------------
     * 旧版本在生成早期就读 breaker.x/y 求"离配电箱最远的格"，
     * 但配电箱要到本段之后才真正放置 —— 读到的是**上一层**的位置；
     * 之后还有一处无条件覆盖，于是这个设计从来没有生效过。
     *
     * 现在挪到配电箱定稿之后：breaker.x/y 是本层真实位置，
     * "通电之后出口在图纸另一头"这个语义才成立。
     * ================================================================ */
    {
        if (CampaignExitDistNow() > 1.01f && breaker.active) {
            FindFarthestFromCell((int)breaker.x, (int)breaker.y, &exitX, &exitY);
        }
    }


}

static void SpawnFloorPopulation(void) {
        {
        GenerateThemeTextures(campaignMode?CampaignThemeNow():dailyMode?DailyVisualThemeNow():RoomThemeForFloor(currentLevel));
    }
    SpawnMonster();
    /* ================================================================
     * 修：**换层后第 2、3 只听声鬼会立刻全部出现**
     * ----------------------------------------------------------------
     * GenerateLevel() 原来只重生第一只（SpawnMonster）、重生追光鬼、清偷电怪，
     * 唯独没碰 monHunters[1] / [2]。
     * 于是它们在上一层还是 active=1 的话，会**原样带到新的一层**：
     *   · 位置还是上一层的坐标（可能落进墙里）
     *   · 出场时间表 s_hunterSpawned 仍等于 3，调度器永远不会再刷
     * 玩家看到的就是「一进第二层，三只同时在场」。
     *
     * 现在换层时把它们清掉，并把时间表重置成「开局 1 只」：
     * 新的一层重新按 60 秒 / 120 秒出场。
     * ================================================================ */
    {

        for (int hi = 1; hi < MON_HUNTER_MAX; hi++) {
            monHunters[hi].active = 0;
            monHunters[hi].stun = 0.0f;
            monHunters[hi].hitCooldown = 0.0f;
            monHunters[hi].pathLen = 0;
            monHunters[hi].pathIdx = 0;
            monHunters[hi].suspTarget = -1;
            monHunters[hi].huntTimer = 0.0f;
            monHunters[hi].stillTimer = 0.0f;
        }
        {  ResetSecondSpawn(); }
    }
    /* 2026-11-06：用户要求追光怪 +1 只 */
    SpawnLightBeast();
    SpawnLightBeast2();
    /* ==================================================================
     * 2026-11-06（审计补）：**换层必须重置"场内系统"**
     * ------------------------------------------------------------------
     * 之前这些 Reset 只写在 StartGame 里 —— 换层根本不走。
     * 后果（都是真的会卡住玩家的）：
     *   · 在蛛网里走完一层 → 新层蜘蛛已经重置，控制槽却还占着，
     *     没有任何代码会释放它 → 玩家永久无法移动
     *   · 信标"已放置"跨层保留 → 按 Q 被传送到上一层的坐标
     *   · 照明棒治疗预算按**局**累计，而不是设计里的按层 24 点
     * ================================================================ */
    {        ControlReset(); }
    {      FlareHealReset(); }
    {  PhaseResetPlacement(); }

    /* 2026-11-06（第 11 轮）：拾光虫 —— 第 2 层起、30%、每层最多 1 只 */
    {  
      NeutralReset(); NeutralTrySpawn(currentLevel); }

    /* 2026-11-06（第 5 轮）：缚丝蛛 —— 第 4 层起、30%、每层最多 1 只 */
    {  
      SpiderReset(); SpiderTrySpawn(currentLevel); }

    /* v1.8 偷电怪：第 3 层起出现。主线每层 1 只；无尽每 5 层 +1，最多 3 只。 */
    ResetLeeches();
    if (currentLevel >= LEECH_MIN_LEVEL) {
        int n = 1;
        if (isEndlessRun) {
            n = 1 + (currentLevel - LEECH_MIN_LEVEL) / 5;
            if (n > LEECH_MAX) n = LEECH_MAX;
        }
        for (int i = 0; i < n; i++) SpawnLeech(i);
    }
    /* v1.8：随机事件（第 2 层起，按概率） */
    /* 先生成基础游荡怪，再应用 MIRROR 的额外刷怪；否则事件刚加的怪会被
       后面的 ExtrasSpawnForLevel() 清空，玩家只能看到速度/理智修正。 */
    ExtrasSpawnForLevel(currentLevel);
    levelEvent = -1;
    if (currentLevel >= 2 && (rand() % 100) < EVENT_CHANCE)
        {   /* v6：按权重抽（血月/空投更常见，幸运更少见） */
            unsigned int sd = (unsigned int)rand();
            levelEvent = EventPickRandom(&sd);
        }
    /* v2.0：每日挑战的固定加成，每层都生效 */
    if (dailyMode) {
        modSanityMul *= 1.25f;
        modShardMul *= 1.25f;
    }
    /* ===== v6：事件效果统一从表里取 =====
       加一个事件只需要在 events.c 的 kEvents[] 里加一行，
       这里永远不用改。 */
    {

        const EventEffect* fx = EventEffectFor(levelEvent);
        modSpeedMul  *= fx->monsterSpeedMul;
        modNoiseMul  *= fx->noiseMul;
        modBatteryMul *= fx->batteryDrainMul;
        modSanityMul *= fx->sanityDrainMul;
        modShardMul  *= fx->goldMul;

        /* 电池上限按百分比压 */
        if (fx->batteryCapPct != 100 && fx->batteryCapPct > 0) {
            float cap = (float)BatteryMax() * (float)fx->batteryCapPct / 100.0f;
            if (battery > cap) battery = cap;
        }

        /* 额外游荡怪 */
        if (fx->extraMonsters > 0) {
            int added = 0;
            for (int i = 0; i < EXTRA_MAX && added < fx->extraMonsters; i++) {
                if (extras[i].active) continue;
                ExtrasSpawnOne(i);
                if (extras[i].active) added++;
            }
        }

        /* 空投：额外给投掷物 */
        if (levelEvent == EV_SUPPLY) {
            flares += 2;
            if (bottles < BottleCap()) bottles += 2;
        }
    }

    /* Retired extras/bosses have no current catalog identity. */

    /* v2.0 CARTOGRAPHER：开局就把安全屋和配电箱的位置标在小地图上 */
    if (SkillCartoUnlock()) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                int ax = (int)safeRoom.x + dx, ay = (int)safeRoom.y + dy;
                if (safeRoom.active && ax > 0 && ax < MAP_WIDTH - 1 && ay > 0 && ay < MAP_HEIGHT - 1)
                    explored[ay][ax] = 1;
                ax = (int)breaker.x + dx; ay = (int)breaker.y + dy;
                if (ax > 0 && ax < MAP_WIDTH - 1 && ay > 0 && ay < MAP_HEIGHT - 1)
                    explored[ay][ax] = 1;
            }
        }
    }
    { 
      BestiaryMarkSeen(BST_ID_HUNTER); BestiaryNoteFloor(BST_ID_HUNTER, currentLevel); }   /* 寻声鬼：每层都有 */
    /* Discovery is recorded silently in the archive. */
    if (lightBeast.active) { BestiaryMarkSeen(BST_ID_LIGHT); BestiaryNoteFloor(BST_ID_LIGHT, currentLevel); }           /* 追光鬼 */
    /* Leeches and spiders are live entities without catalog rows; never borrow IDs 2/3. */

    /* 记录本局见过的词条种类（算难度乘子用） */
    if (levelMod >= 0) {
        int bit = 1 << levelMod;
        if (!(modsSeenMask & bit)) { modsSeenMask |= bit; modsSeenCount++; }
    }
    /* 跳脸虫：第 3 层起按概率出现，不是每关必出 ——
       稀有才有惊吓感。触发时机也随机（进关 8~20 秒后），
       不然一进关就跳出来太突兀。 */
    /* ==================================================================
     * 2026-11-06（用户要求）：**一局总量受控**
     * ------------------------------------------------------------------
     * 用户原话：「刷新频率为一局只有上限 4 只，随着层数增加上限 4 只」。
     *
     * 旧逻辑是"每层 35% 概率出 1 只"，一局打 10 层可能遇 3~4 次，
     * 而且完全随机 —— 有的局一次没有，有的局连着来。
     *
     * 新逻辑：把**一局的总量**当预算来花。
     *   本局上限 = min(4, 1 + 层数/2)
     *     第 3 层 → 2 只，第 5 层 → 3 只，第 7 层起 → 4 只
     *   已经出过的数量达到上限 → 本层不再安排
     * ================================================================ */
    {
        /* ==================================================================
         * 2026-11-06（用户要求）：**跳脸怪彻底删除**
         * ------------------------------------------------------------------
         * 用户原话：「把那个跳脸怪给彻底删除」。
         *
         * 这里原来按"一局总量"排程（min(4, 1+层数/2) 只）。
         * 现在恒为 -1 —— 本层不会出现爆脸怪。
         * 它的角色由缚丝蛛接管（第 4 层起、30%、每层最多 1 只）。
         * ================================================================ */
        jumperTrigger = -1.0f;
    }
    jumperPhase = JMP_IDLE;
    jumperTimer = 0.0f;
    parasite.active = 0;
    /* 2026-11-06：一局的总量预算与控制计时由 StartGame 负责清零 */
    {  
      jumperSpawnedRun = 0; jumperControlT = 0.0f; }
    for (int i = 0; i < MAX_THROWN; i++) thrown[i].active = 0;

    /* ===== v6 **删除旧安全区** =====
       用户：「安全区是旧版本遗物，隐藏点才是主流机制」。
       核实：商店（T 键）、操作提示、校准小游戏三处的判定
       早就写成了 `inSafe || hiddenIn >= 0` —— 也就是说
       **藏在藏身点里已经能做安全区能做的全部事情**。
       安全区那半边是纯冗余，而且它的模型会顶掉配电箱的位置。
       现在 safeRoom.active 恒为 0，不再生成、不再绘制。 */
}

static void PlaceFloorBreaker(const LevelBuildWorkspace* work) {
    safeRoom.active = 0;

    /* 配电箱：放在离出生点 15 格以上的远处，迫使玩家深入探索 */
    breaker.active = 0;
    for (int t = 0; work->emptyCount>0 && t < 256; t++) {
        int idx = rand() % work->emptyCount;
        float bx = work->cells[idx][0] + 0.5f, by = work->cells[idx][1] + 0.5f;
        float dFromSpawn = Dist2(bx, by, 1.5f, 1.5f);
        if (dFromSpawn < 225.0f) continue;  /* 离出生点至少 15 格 (15^2=225) */
        if (!SpotClear(bx, by, 1.6f, 2, -1)) continue;
        breaker.x = bx;
        breaker.y = by;
        breaker.active = 1;
        break;
    }
    /* 强制兜底：如果随机找不到，遍历全图强行放一个 */
    if (!breaker.active) {
        float bestD2 = -1.0f;
        for (int y = 1; y < MAP_HEIGHT - 1; y++) {
            for (int x = 1; x < MAP_WIDTH - 1; x++) {
                if (worldMap[y][x]) continue;
                float bx = x + 0.5f, by = y + 0.5f;
                float dFromSpawn = Dist2(bx, by, 1.5f, 1.5f);
                if (dFromSpawn < 225.0f) continue;
                if (!SpotClear(bx, by, 1.2f, 2, -1)) continue;  /* 放宽到 1.2f */
                if (dFromSpawn > bestD2) {
                    bestD2 = dFromSpawn;
                    breaker.x = bx;
                    breaker.y = by;
                }
            }
        }
        if (bestD2 > 0.0f) breaker.active = 1;
    }
    /* ===== 最终兜底（v6 重写）=====
       原来这段有三个问题，正好对上玩家报的两个 bug：

       ① **安全屋会把配电箱顶掉** ——
          判重只写了「同一格」（cx == safeRoom.x），没有用半径。
          安全屋建模占 1.6 格，配电箱落在它隔壁一格也算「在安全屋里」，
          玩家看到的就是「安全区盖住了电闸，这层没有电闸」。

       ② **配电箱兜底被 %3 挡住** ——
          整段挂在 `currentLevel % 3 == 0` 里面。
          非 3 倍层只要前两轮随机都失败，就**永远没有配电箱**，
          而配电箱是通关必需品 → 那一层直接卡死。

       ③ 兜底没用 SpotClear，可以落在墙角/箱子里。

       现在安全区已删，只保留配电箱的两段兜底。 */


    /* ② 配电箱兜底（**无条件**，每层必须有） */
    if (!breaker.active) {
        for (int y = 1; y < MAP_HEIGHT - 1 && !breaker.active; y++) {
            for (int x = 1; x < MAP_WIDTH - 1 && !breaker.active; x++) {
                if (worldMap[y][x]) continue;
                float cx = x + 0.5f, cy = y + 0.5f;
                if (Dist2(cx, cy, 1.5f, 1.5f) < 25.0f) continue;
                if (!SpotClear(cx, cy, 1.2f, 2, -1)) continue;
                /* 离安全屋至少 4 格，绝不和它重叠 */
                if (safeRoom.active && Dist2(cx, cy, safeRoom.x, safeRoom.y) < 16.0f) continue;
                breaker.x = cx; breaker.y = cy; breaker.active = 1;
            }
        }
    }
    /* ③ 最后保险：全图任意空格（只在真的没别的地方时才走） */
    if (!breaker.active) {
        for (int y = 1; y < MAP_HEIGHT - 1 && !breaker.active; y++) {
            for (int x = 1; x < MAP_WIDTH - 1 && !breaker.active; x++) {
                if (worldMap[y][x]) continue;
                float cx = x + 0.5f, cy = y + 0.5f;
                if (safeRoom.active && Dist2(cx, cy, safeRoom.x, safeRoom.y) < 9.0f) continue;
                breaker.x = cx; breaker.y = cy; breaker.active = 1;
            }
        }
    }
}

void GenerateLevel(void) {
    static LevelBuildWorkspace work;
    ResetFloorSystems();
    memset(&roomTheme,0,sizeof(roomTheme));
    if (trainingMode) {
        TrainingBuildArena();
        return;
    }
    GenerateThemeTextures(campaignMode?CampaignThemeNow():dailyMode?DailyVisualThemeNow():RoomThemeForFloor(currentLevel));
    GenerateMazeTopology();
    if(campaignMode)CampaignBuildFloor();
    /* Pure topology gate. An invalid random layout falls back to a connected
       open maintenance floor, with no room/props/hatches to trap the player. */
    if(!MapTopologyAccepts(MapTopologyMeasure(&worldMap[0][0],MAP_WIDTH,MAP_HEIGHT))){
        for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)worldMap[y][x]=(x==0 || y==0 || x>=MAP_WIDTH-1 || y>=MAP_HEIGHT-1)?1:0;
        CellMetaReset();RoomPropReset();roomCount=0;HatchReset();RoomThemeBind();
    }
    CollectFloorCandidates(&work);
    if(work.emptyCount==0){
        /* A malformed authored/legacy room must not feed rand()%0. */
        CellMetaReset();RoomPropReset();HatchReset();roomCount=0;RoomThemeBind();
        for(int y=0;y<MAP_HEIGHT;y++)for(int x=0;x<MAP_WIDTH;x++)worldMap[y][x]=(x==0 || y==0 || x==MAP_WIDTH-1 || y==MAP_HEIGHT-1);
        posX=3.5f;posY=18.5f;exitX=32.5f;exitY=18.5f;
        CollectFloorCandidates(&work);
    }
    work.contCap = ConfigureFloorResources();
    PlaceFloorContainers(&work);
    DistributeFloorLoot();
    SpawnFloorPopulation();
    if(isEndlessRun && currentLevel>10){float pressure=fminf(.65f,(currentLevel-10)*.025f);modSanityMul*=1+pressure;modBatteryMul*=1+pressure*.6f;modNoiseMul*=1+pressure*.4f;}
    PlaceFloorBreaker(&work);
    RoomThemePlaceVault();
    CollectorSpawnIfEligible();
}

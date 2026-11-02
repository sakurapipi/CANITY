/*
 * Canity — 实体层实现：玩家状态 / 状态效果 / 三种怪物 + 跳脸虫 QTE
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "music.h"
#include "entity.h"
/* v6：威胁值接口 */
float ThreatChaseBonus(void);
float ThreatSpawnAdvance(void);
float ThreatSpeedCap(void);
float ThreatHearMul(void);
#include "suspicion.h"
#include "upgrade_run.h"
#include "bench.h"
#include "relics.h"
#include "weapons.h"
#include "game.h"
#include "skills.h"
#include "game.h"
#include "render.h"
#include "audio.h"
#include "extras.h"


float posX = 1.5f, posY = 1.5f;
float dirX = 1.0f, dirY = 0.0f;
float planeX = 0.0f, planeY = 0.66f;
float sanity = 100.0f;
static StatusFx g_fx[FX_COUNT];

/* 效果强度总览：给 HUD 用。返回 0 表示没有该效果。 */
float FxStrength(int kind) {
    if (kind < 0 || kind >= FX_COUNT) return 0.0f;
    return g_fx[kind].strength;
}

/* 施加 / 刷新一个状态效果。
   新强度取「当前强度」与「新强度」的较大值 —— 寄生怪反复附着时
   不会把已有的强效果削弱掉。剩余时间取较大值。 */
void ApplyFx(int kind, float strength, float duration) {
    if (kind < 0 || kind >= FX_COUNT) return;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (g_fx[kind].strength < strength) g_fx[kind].strength = strength;
    if (g_fx[kind].remain < duration) g_fx[kind].remain = duration;
    /* 视野收窄要立刻生效 */
    if (kind == FX_BLIND) ApplyFov();
}

/* 推进所有效果的计时。到期归零。
   返回 1 表示某个效果的强度发生了变化 —— 调用方需要重算 FOV。
   用一个很小的 epsilon 判到期：浮点累加 1.0 秒后往往剩下 1e-7 这样的正数，
   直接判 `remain <= 0` 会让效果多活一帧。 */
int UpdateFx(float dt) {
    int changed = 0;
    for (int i = 0; i < FX_COUNT; i++) {
        if (g_fx[i].remain <= 0.0f) continue;
        float before = g_fx[i].strength;
        g_fx[i].remain -= dt;
        if (g_fx[i].remain <= 0.0001f) {
            g_fx[i].remain = 0.0f;
            g_fx[i].strength = 0.0f;
        }
        if (g_fx[i].strength != before) changed = 1;
    }
    return changed;
}

/* 清除全部状态效果（换关、读档、死亡时调用） */
void ClearFx(void) {
    for (int i = 0; i < FX_COUNT; i++) {
        g_fx[i].strength = 0.0f;
        g_fx[i].remain = 0.0f;
    }
}
Monster monHunters[MON_HUNTER_MAX];   /* 听声鬼：0=第一只 1=第二只 2=第三只 */
Monster lightBeast;      /* 只看光 */
Monster parasite;   /* 复用 Monster 结构体（只需要位置和几个状态字段） */

/* QTE 运行时状态 */
int jumperPhase = JMP_IDLE;
float jumperTimer = 0.0f;     /* 当前阶段的倒计时 */
int jumperQteKey = 0;         /* 阶段二要按的键编号 */
int jumperResolved = 0;       /* 本次事件是否已结算（防止重复结算） */
float jumperTrigger = -1.0f;  /* 本关什么时候触发（秒）；<0 = 本关不出现 */
float battery = 100.0f;          /* 0..100 */
int flashlightOn = 1;
float stamina = 100.0f;          /* 0..100 */
int sprinting = 0;
float staminaIdle = 0.0f;        /* 停止冲刺后的恢复延迟 */
/* ------------------------------------------------------------------ */
/* 难度曲线：全是纯函数，只吃层数，方便离线自检                        */
/* ------------------------------------------------------------------ */

/* 怪物速度随层数线性上升，但**永远慢于玩家走速 MOVE_SPEED**。
   玩家走 2.5，怪最快 2.40 —— 你永远跑得掉，但「跑掉」的代价越来越大：
   走路完全静音，而翻找箱子会发出半径 8 的噪声，怪跑得快意味着
   「搜箱子」在深层更危险。压迫感来自空间被压缩，不是来自时间被砍短。

   modMul 是 HUNTED 词条的倍率（1.25）。它叠在层数曲线之后，
   最后统一夹到 MONSTER_SPEED_FINAL_MAX —— 这条夹取保证了
   「即使在最深的一层 + HUNTED，怪也不会跑得比玩家快」。 */
static float MonsterSpeedFor(int level, float base, float modMul) {
    if (level < 1) level = 1;
    float s = base + MONSTER_SPEED_STEP * (float)(level - 1);
    if (s > MONSTER_SPEED_MAX) s = MONSTER_SPEED_MAX;
    s *= modMul;
    if (s > MONSTER_SPEED_FINAL_MAX) s = MONSTER_SPEED_FINAL_MAX;

    /* ==================================================================
     * v7 第 ㊿ 轮：**THREAT 的速度加成回来了，但先修寻路**
     * ==================================================================
     * 第 ③ 轮把上限换成 ThreatSpeedCap()（2.05~2.33）后，
     * 用户实机反馈「听声鬼会瞬移」。
     *
     * 真正的原因不是速度本身，是**寻路重算间隔跟不上**：
     * 原来每 0.45 秒才 MonPath() 一次，速度 2.33 时这 0.45 秒里
     * 怪物已经走了 1.05 格 —— 切角、穿墙、看起来就是"瞬移"。
     *
     * 修法两件一起做：
     *   ① repath 间隔 0.45 → 0.22（见下面 Hunt 分支）
     *   ② 速度上限收敛到 2.20（原文 2.10，仍 < 玩家冲刺 3.0）
     * ================================================================== */
    {
        extern float ThreatSpeedCap(void);
        float hard = ThreatSpeedCap();
        if (hard > 2.20f) hard = 2.20f;      /* 不许超过 2.20 */
        if (hard < MONSTER_SPEED_FINAL_MAX) hard = MONSTER_SPEED_FINAL_MAX;
        if (s > hard) s = hard;
    }

    return s;
}


/* v3.3 胶水：这个位置有没有粘胶区域。返回 1 = 踩到了。
   怪物和追光鬼共用 —— 胶水对两种追击者都有效，这是它值钱的地方。 */
/* v6：金币招怪 —— 速度倍数（实现见 game.c） */
float GoldSpeedMul(void);
float GoldHearMul(void);

static int InGlue(float x, float y) {
    /* v3.3：**判定加上体积半径** ——
       原来只判中心点，怪贴边擦过、身体已经压到胶水上却不算，
       玩家感觉就是「有时候不生效」。
       现在按 GLUE_RADIUS + GLUE_BODY_PAD 判定：身体边缘一碰到就减速。 */
    const float r = GLUE_RADIUS + GLUE_BODY_PAD;
    for (int i = 0; i < MAX_GLUE; i++) {
        if (!glues[i].active) continue;
        float dx = x - glues[i].x, dy = y - glues[i].y;
        if (dx * dx + dy * dy < r * r) return 1;
    }
    return 0;
}

/* 被抓后的无敌时间：深层收紧，让连续被抓不再是无解连击。
   第 6 层开始，每深一层减 0.1 秒，第 15 层触到下限 3.0 秒。 */
static float HitCooldownFor(int level) {
    float t;
    if (level < HIT_COOLDOWN_START_LEVEL) {
        t = HIT_COOLDOWN_BASE;
    } else {
        t = HIT_COOLDOWN_BASE - 0.1f * (float)(level - HIT_COOLDOWN_START_LEVEL + 1);
    }
    /* v6 第 ㊱ 轮：**THREAT 的第 5 项效果终于接上了**
       第 ③ 轮我在报告里写「五项效果全部接线」——
       实际上 ThreatCooldownDelta 一直是死代码。
       这是唯一一项影响"你多久能被抓第二次"的效果。 */
    { extern float ThreatCooldownDelta(void); t += ThreatCooldownDelta(); }
    if (t < HIT_COOLDOWN_MIN) t = HIT_COOLDOWN_MIN;
    if (t < 0.25f) t = 0.25f;          /* 硬下限：再强也不许贴脸连抓 */
    return t;
}

static int g_prev[MAP_HEIGHT][MAP_WIDTH];

/* 网格 BFS，填进 monster.path（不含起点） */
static int MonPathFor(Monster* m, int sx, int sy, int tx, int ty) {
    m->pathLen = 0;
    m->pathIdx = 0;
    if (sx < 1 || sy < 1 || sx >= MAP_WIDTH - 1 || sy >= MAP_HEIGHT - 1) return 0;
    if (tx < 1 || ty < 1 || tx >= MAP_WIDTH - 1 || ty >= MAP_HEIGHT - 1) return 0;
    if (worldMap[sy][sx] || worldMap[ty][tx]) return 0;
    if (sx == tx && sy == ty) return 0;

    static int queue[MAP_WIDTH * MAP_HEIGHT];
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) g_prev[y][x] = -1;

    int head = 0, tail = 0;
    queue[tail++] = sy * MAP_WIDTH + sx;
    g_prev[sy][sx] = sy * MAP_WIDTH + sx;

    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    while (head < tail) {
        int cur = queue[head++];
        int cx = cur % MAP_WIDTH, cy = cur / MAP_WIDTH;
        if (cx == tx && cy == ty) break;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
            if (worldMap[ny][nx]) continue;
            if (g_prev[ny][nx] != -1) continue;
            g_prev[ny][nx] = cur;
            queue[tail++] = ny * MAP_WIDTH + nx;
        }
    }
    if (g_prev[ty][tx] == -1) return 0;

    int tmp[MON_MAXPATH][2], n = 0;
    int cx = tx, cy = ty;
    while (!(cx == sx && cy == sy) && n < MON_MAXPATH) {
        tmp[n][0] = cx; tmp[n][1] = cy; n++;
        int p = g_prev[cy][cx];
        cx = p % MAP_WIDTH; cy = p / MAP_WIDTH;
    }
    m->pathLen = n;
    for (int i = 0; i < n; i++) {
        m->path[i][0] = tmp[n - 1 - i][0];
        m->path[i][1] = tmp[n - 1 - i][1];
    }
    return n;
}

/* 老的无参封装 MonPath() 已删除：它写死 &monster，
   曾让第 2/3 只听声鬼的路径全写进第一只，导致它们站着不动。
   现在统一走 MonPathFor(Monster*, ...)。 */

/* 把怪放到离玩家足够远的空格 */
/* ==================================================================
 * v2.4.93 **被抓的理智惩罚：递增档位**
 * ------------------------------------------------------------------
 * 用户要求：「每次被撞到，理智递减 —— 第一次 10%，第二次 15%，
 *            越减越多，封顶 40%。计数换层重置。」
 *
 * 为什么递增：固定扣 18 意味着"被抓一次和被抓五次一样疼"，
 * 玩家不会因为连续失误而真正感到压力。
 * 递增之后，"今天这层已经被抓三次了"本身就是压迫感。
 *
 * 为什么换层重置：整局累积会让前期小失误在深层变成必死。
 * 每层重新计数，深度惩罚靠层数本身（怪更快）来实现。
 * ================================================================ */
static int s_catchCountFloor = 0;

/* 看一眼本次会扣多少（不消耗档位） */
float CatchSanityPeek(void) {
    static const float tier[6] = { 10.0f, 15.0f, 21.0f, 28.0f, 36.0f, 40.0f };
    int i = s_catchCountFloor;
    if (i < 0) i = 0;
    if (i > 5) i = 5;
    return tier[i];
}

/* 真的被抓到一次 —— 档位 +1 */
void CatchCountBump(void) {
    MusicNotify(MUSIC_EVENT_CAUGHT);
    s_catchCountFloor++;
    if (s_catchCountFloor > 6) s_catchCountFloor = 6;
}

/* 换层重置 */
void CatchCountResetFloor(void) { s_catchCountFloor = 0; }

int CatchCountThisFloor(void) { return s_catchCountFloor; }

/* v6：**本局累计**被抓次数（上面那个每层清零，两个用途不同） */
static int s_catchCountRun = 0;
void CatchCountBumpRun(void)  { s_catchCountRun++; }
int  CatchCountThisRun(void)  { return s_catchCountRun; }
void CatchCountResetRun(void) { s_catchCountRun = 0; }

void SpawnMonster(void) {
    monster.active = 1;
    monster.state = 0;
    monster.speed = MonsterSpeedFor(currentLevel, MONSTER_SPEED_BASE, modSpeedMul);
    monster.pathLen = 0;
    monster.pathIdx = 0;
    monster.repath = 0.0f;
    monster.suspCool = 0.0f;   /* v2.3.1 */
    monster.stateTimer = 0.0f;
    monster.wanderTimer = 1.5f;
    monster.hitCooldown = HitCooldownFor(currentLevel);
    monster.growlTimer = 3.0f;
    /* v2.1：Boss 层把寻声鬼强化成 Boss（8 倍血、更大只、更慢） */
    extern int bossLevel, bossMaxHp;
    monster.hp = MonsterHpFor(currentLevel);
    /* v2.0：战役每关加码 —— 原来 5 关全都用第 1 层数值，没有任何递进 */
    {
        extern int campaignMode, campaignStage;
        if (campaignMode && campaignStage > 0)
            monster.hp = (int)((float)monster.hp * (1.0f + 0.28f * (float)campaignStage) + 0.5f);
    }
    if (bossLevel) {
        /* v2.0：3 个 Boss 各走一条路线 —— 第 6 层「快」、12 层「厚」、18 层「全能」。
           原来三个 Boss 都是同一只怪 ×8 血 ×0.72 速，玩家完全分不出来。 */
        int lv = currentLevel;
        if (lv <= 6)        { monster.hp *= 6;    monster.speed *= 1.10f; }   /* 快攻 */
        else if (lv <= 12)  { monster.hp *= 6;    monster.speed *= 0.62f; }   /* v2.2：10→6（符印原来打不完） */   /* 厚甲 */
        else                { monster.hp *= 5.5f; monster.speed *= 0.95f; }  /* v2.2：9→5.5 */   /* 全能 */
        bossMaxHp = monster.hp;
        /* v2.2：Boss 原来三抗全同 → 符印（理智）和信号枪（电量）
           在 12/18 层算术上打不死（要 216/264 点，上限 100）。
           给法术和金币各 -30% 抗性（弱点），让三把武器都能通。 */
        monster.resist[DT_SLASH] = -30;
        monster.resist[DT_SHARD] = -30;
        monster.resist[DT_BLUNT] = 10;
    } else {
        bossMaxHp = monster.hp;
    }
    /* v1.15：护甲 0；抗玻璃（它是被瓶子砸醒的），弱钝击 */
    monster.armor = 0;
    monster.resist[DT_SLASH] = 0;
    monster.resist[DT_BLUNT] = -25;
    monster.resist[DT_SHARD] = 20;
    monster.stun = 0.0f;
    for (int t = 0; t < 512; t++) {
        int x = 1 + rand() % (MAP_WIDTH - 2);
        int y = 1 + rand() % (MAP_HEIGHT - 2);
        if (worldMap[y][x]) continue;
        if (Dist2(x + 0.5f, y + 0.5f, posX, posY) < 100.0f) continue;
        if (!SpotClear(x + 0.5f, y + 0.5f, 1.4f, 4, -1)) continue;
        monster.x = x + 0.5f;
        monster.y = y + 0.5f;
        return;
    }
    monster.x = exitX;
    monster.y = exitY;
}

int HasLOS(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return 1;
    dx /= d; dy /= d;
    for (float t = 0.2f; t < d; t += 0.22f) {
        int cx = (int)(x0 + dx * t), cy = (int)(y0 + dy * t);
        if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT) return 0;
        if (worldMap[cy][cx]) return 0;
    }
    return 1;
}

/* 玩家制造声音：只有跑步、翻找、手摇会调用 */
/* v3.0：藏身判定 —— 很多 AI 分支都要问「玩家是不是藏着」 */
extern int hiddenIn;

/* v3.0：**噪音可视化** —— 逃生类的第三块反馈。
   玩家扔瓶子/敲墙之后必须看得见「声音传了多远、持续多久」，
   否则这个核心手法是纯盲操作。
   noiseDisplay 是 0..1 的衰减值，HUD 拿它画噪音环。 */
float noiseDisplay = 0.0f;
float noiseDisplayTimer = 0.0f;
float noiseSrcX = 0.0f, noiseSrcY = 0.0f;   /* v3.0：听觉用 */

/* v3.3 光球护佑：藏身点 4 格内有没有还亮着的光球。
   有的话，躲在里面更难被搜到 —— 柜门缝隙被外面的光晃着，怪不容易注意到。 */
static int FlareNearHide(void) {
    extern Thrown thrown[MAX_THROWN];
    if (hiddenIn < 0 || hiddenIn >= hideSpotCount) return 0;
    float hx = hideSpots[hiddenIn].x, hy = hideSpots[hiddenIn].y;
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        float dx = thrown[i].x - hx, dy = thrown[i].y - hy;
        if (dx * dx + dy * dy <= 16.0f) return 1;    /* 4 格 */
    }
    return 0;
}

void MakeNoise(float x, float y, float radius) {
    /* ==================================================================
     * v7 第 ㊿ 轮：**事件的「怪物听觉」倍率一直是死的**
     * ==================================================================
     * events.h 里定义了 monsterHearMul，但全工程 0 个读取点。
     * 于是「浓雾：怪物听觉 -20%」和「死寂：怪物听觉 +40%」
     * 这两个描述**一个字都没发生** —— 玩家还会按它调整策略。
     *
     * 这里把它接进噪音半径：听觉倍率直接缩放"能传多远"。
     * ================================================================ */
    {
        extern int levelEvent;
        extern float EventMonsterHearMul(int);   /* events.c 提供的取值函数 */
        float h = EventMonsterHearMul(levelEvent);
        if (h > 0.01f && h != 1.0f) radius *= h;
    }

    /* 听觉差异**不再在这里做** ——
       2026-11-03 用户改成分只在个体上（100/75/50%）。
       每只怪在挑可疑点时按自己的倍率抬阈值，见 UpdateOneHunter。 */

    /* v3.0：**先记录再判距离** ——
       原来这段在主循环之后，而前面有 `if (Dist2 > radius) return;`，
       于是「没有怪听见就零反馈」：玩家在空地扔石头，HUD 一点反应都没有。 */
    {
        extern float noiseDisplay, noiseDisplayTimer, noiseSrcX, noiseSrcY;
        extern int vanished;
        if (!vanished) {
            float k = radius / 15.0f;
            if (k > 1.0f) k = 1.0f;
            if (k > noiseDisplay) noiseDisplay = k;
            noiseDisplayTimer = 2.2f;
            noiseSrcX = x; noiseSrcY = y;
        }
    }

    /* v3.0 修正：隐身时**只屏蔽远处的噪音，近身噪音仍然生效** ——
       原来隐身时无条件 return，配上 state 自锁就成了永久隐身。
       现在 4 格内的噪音照常传出去，玩家不能贴脸敲墙还安全。 */
    {
        extern int vanished;
        if (vanished && radius < 4.0f) return;
    }
    /* v3.0：**噪音要通知所有追捕者** ——
       原来只写 monster，于是扔瓶子引开对追光鬼/影子/蜘蛛完全无效，
       「用噪音控制敌人」这个逃生核心手法只对三分之一的怪成立。 */
    /* ===== v7 **可疑点表** =====
       原来这里直接把 monster.state 设成 1 —— 于是所有噪音源抢同一个字段，
       谁后写谁赢。玩偶的叫声刚设好就被玩家自己的脚步顶掉，
       这就是「发出声音的玩偶吸引不了听声怪」的根源。
       现在改成写表：多个噪音源共存，怪物自己挑最强的那个。 */
    {
        float st = radius * 2.6f;              /* 半径 14 → 36；半径 20 → 52 */
        if (st > 100.0f) st = 100.0f;
        int srcS = (radius <= 14.0f) ? SUSP_WALK : SUSP_SEARCH;
        /* 走路噪音衰减快、寿命短；翻箱/敲击衰减慢、寿命长 */
        float decS = (srcS == SUSP_WALK) ? 14.0f : 8.0f;
        float ttlS = (srcS == SUSP_WALK) ? 3.0f : 6.0f;
        SuspicionAdd(x, y, st, decS, ttlS, srcS);
    }

    {
        extern Monster lightBeast;
        /* 追光怪仍然直接通知（它不吃"声音"这套逻辑，它只认光） */
        /* 原来写的是 { &lightBeast, &lightBeast } 而循环只跑 1 次 ——
           数组第二个元素从来没被用过，属历史残留，直接收成一个。 */
        Monster* all[1] = { &lightBeast };
        for (int mi = 0; mi < 1; mi++) {
            Monster* mm = all[mi];
            if (!mm->active) continue;
            float r2 = radius;
            extern float modNoiseMul;
            /* v3.0：**引怪工具不吃「安静」倍率** ——
               玩家买 QUIET LID 是想走路更安静；如果敲击/投掷的声音也跟着缩小，
               等于「越升级越没法用声音控制敌人」，和核心玩法直接冲突。
               15 以上的是敲击/投掷，14 以下的是脚步等被动噪音。 */
            /* v6 **补齐漏乘的倍率** ——
               原来 SkillQuietMul 只在这里乘一次，而 GoldHearMul / RelicNoiseMul /
               modNoiseMul 在函数末尾才乘、那时主怪早就通知完了。
               于是「GHOST SHELL 降噪 45%」和「DEAD SILENCE 事件」对两只主怪完全无效，
               遗物唯一卖点白买。现在被动噪音把三项一起算。 */
            if (r2 <= 14.0f) {
                r2 *= SkillQuietMul() * RelicNoiseMul() * modNoiseMul;
                { extern float GoldHearMul(void), ThreatHearMul(void);
      r2 *= GoldHearMul() * ThreatHearMul(); }   /* v6：金币 + 威胁都放大听觉 */
            }
            if (Dist2(x, y, mm->x, mm->y) > r2 * r2) continue;
            mm->state = 1;
            mm->tx = x;
            mm->ty = y;
            mm->repath = 0.0f;
            mm->stateTimer = 0.0f;
            

/* v3.0 改造树「屏息」：玩家藏着时，追捕者更快放弃搜寻。
               之前这条补丁没落地（验证报告：BenchHideStealth 在 exe 里 0 个调用点）。 */
            /* v3.0：屏息 = 改造树 + 技能树 HOLD BREATH，再加上静面 STILL FACE */
            if (hiddenIn >= 0 && (BenchHideStealth() < 1.0f || HideStealthMul() < 1.0f)) {
                mm->stateTimer = 1.2f * StillFaceMul();
            }
            /* v3.3：光球护佑 —— 藏身点 4 格内有亮着的光球时，
               怪更快放弃（光晃着柜门缝，它认不准）。 */
            if (hiddenIn >= 0 && FlareNearHide()) {
                if (mm->stateTimer > 0.8f) mm->stateTimer = 0.8f;
            }

        }
    }
    extern float modNoiseMul;
    /* v6 **金币招怪**：携带金币越多，怪物听得越远。
       这是「奖励就是风险」原则在 CANITY 的落点 ——
       钱让你变强，同时也让你更容易被听见。 */
    {
        extern float GoldHearMul(void), ThreatHearMul(void), CampaignNoiseAlertMul(void);
        radius *= GoldHearMul() * ThreatHearMul() * CampaignNoiseAlertMul();   /* v6：金币 + 威胁 + 战役 */
    }
    radius *= SkillQuietMul() * RelicNoiseMul() * modNoiseMul;   /* QUIET LID + 遗物 + 事件 */
    /* v3.3：盲螨只认声音。它原来每帧直接朝玩家冲刺，所以玩家“静音走路”
       也会被它追；现在只有噪音落在半径内才给它目标。 */
    for (int i = 0; i < EXTRA_MAX; i++) {
        Extra* e = &extras[i];
        if (!e->active || e->kind != 1) continue;
        if (Dist2(x, y, e->x, e->y) > radius * radius) continue;
        e->hasTarget = 1;
        e->tx = x; e->ty = y;
        e->timer = 0.0f;
    }
    if (!monster.active) return;
    if (Dist2(x, y, monster.x, monster.y) > radius * radius) return;
    monster.state = 1;
    monster.tx = x;
    monster.ty = y;
    monster.repath = 0.0f;
    monster.stateTimer = 0.0f;
    /* v3.0：记录一次噪音事件，供 HUD 显示 */
    {
        extern float noiseDisplay, noiseDisplayTimer;
        float k = radius / 22.0f;
        if (k > 1.0f) k = 1.0f;
        if (k > noiseDisplay) noiseDisplay = k;
        noiseDisplayTimer = 2.2f;
        noiseSrcX = x; noiseSrcY = y;
    }
}


void SpawnLightBeast(void) {
    /* 注意：不要整体拷贝 monster —— 那会把寻声怪的旧路径、旧命中冷却
       一并带过来，光敏怪会开局就沿着别人的路走、还带着别人的无敌时间。
       这里只借用它的类型，清干净状态再重置。 */
    memset(&lightBeast, 0, sizeof(lightBeast));
    lightBeast.active = 1;
    lightBeast.state = 0;
    lightBeast.speed = MonsterSpeedFor(currentLevel, LIGHTBEAST_SPEED_BASE, modSpeedMul);
    lightBeast.pathLen = 0;
    lightBeast.pathIdx = 0;
    lightBeast.repath = 0.0f;
    lightBeast.stateTimer = 0.0f;
    lightBeast.wanderTimer = 1.5f;
    lightBeast.hitCooldown = HitCooldownFor(currentLevel);
    lightBeast.growlTimer = 3.0f;
    lightBeast.hp = (int)((float)MonsterHpFor(currentLevel) * LIGHTBEAST_HP_MUL);
    /* v1.15：有甲、抗刀、怕钝击 */
    lightBeast.armor = 1;
    lightBeast.resist[DT_SLASH] = 20;
    lightBeast.resist[DT_BLUNT] = -20;
    lightBeast.resist[DT_SHARD] = 0;
    lightBeast.stun = 0.0f;
    for (int t = 0; t < 512; t++) {
        int x = 1 + rand() % (MAP_WIDTH - 2);
        int y = 1 + rand() % (MAP_HEIGHT - 2);
        if (worldMap[y][x]) continue;
        if (Dist2(x + 0.5f, y + 0.5f, posX, posY) < 144.0f) continue;
        if (monster.active && Dist2(x + 0.5f, y + 0.5f, monster.x, monster.y) < 4.0f) continue;
        if (!SpotClear(x + 0.5f, y + 0.5f, 1.4f, 4, -1)) continue;
        lightBeast.x = x + 0.5f;
        lightBeast.y = y + 0.5f;
        return;
    }
    lightBeast.x = exitX;
    lightBeast.y = exitY;
}

void UpdateLightBeast(float dt) {
    /* v3.0：**光敏怪也要更新朝向和步频** ——
       原来它从不更新 facing/walkPhase，画出来永远是朝 +X、双腿不动。 */
    {
        float ldx = lightBeast.x - lightBeast.prevX;
        float ldy = lightBeast.y - lightBeast.prevY;
        float ld2 = ldx * ldx + ldy * ldy;
        if (ld2 > 1e-8f) {
            lightBeast.facing = atan2f(ldy, ldx);
            lightBeast.walkPhase += sqrtf(ld2) * 3.5f;
            if (lightBeast.walkPhase > 1000.0f) lightBeast.walkPhase -= 1000.0f;
        }
        lightBeast.prevX = lightBeast.x;
        lightBeast.prevY = lightBeast.y;
    }

    if (!lightBeast.active) return;

    /* 硬直：被玻璃瓶砸中后短暂不能动 */
    if (lightBeast.stun > 0.0f) {
        lightBeast.stun -= dt;
        if (lightBeast.stun < 0.0f) lightBeast.stun = 0.0f;
        return;
    }

    /* 目标：看得见的玩家（手电开着）优先，其次是地上的荧光棒 */
    float tx = 0.0f, ty = 0.0f;
    int has = 0;
    if (flashlightOn && battery > 0.0f && Dist2(posX, posY, lightBeast.x, lightBeast.y) < 256.0f &&
        HasLOS(lightBeast.x, lightBeast.y, posX, posY)) {
        tx = posX; ty = posY; has = 1;
    } else {
        for (int i = 0; i < MAX_THROWN; i++) {
            if (!thrown[i].active || thrown[i].type != 0) continue;
            if (Dist2(thrown[i].x, thrown[i].y, lightBeast.x, lightBeast.y) < 256.0f &&
                HasLOS(lightBeast.x, lightBeast.y, thrown[i].x, thrown[i].y)) {
                tx = thrown[i].x; ty = thrown[i].y; has = 1; break;
            }
        }
    }

    if (has) {
        lightBeast.state = 1;
        lightBeast.tx = tx; lightBeast.ty = ty;
        lightBeast.repath -= dt;
        if (lightBeast.repath <= 0.0f) {
            lightBeast.repath = 0.35f;
            MonPathFor(&lightBeast, (int)lightBeast.x, (int)lightBeast.y, (int)tx, (int)ty);
        }
    } else if (lightBeast.state == 1) {
        lightBeast.state = 0;
        lightBeast.wanderTimer = 0.0f;
        lightBeast.pathLen = 0;
    } else {
        lightBeast.wanderTimer -= dt;
        if (lightBeast.wanderTimer <= 0.0f && lightBeast.pathLen == 0) {
            lightBeast.wanderTimer = 2.0f;
            int cx = (int)lightBeast.x, cy = (int)lightBeast.y;
            int nx = cx + (rand() % 9) - 4, ny = cy + (rand() % 9) - 4;
            if (nx > 0 && ny > 0 && nx < MAP_WIDTH - 1 && ny < MAP_HEIGHT - 1 && !worldMap[ny][nx])
                MonPathFor(&lightBeast, cx, cy, nx, ny);
        }
    }

    if (lightBeast.pathIdx < lightBeast.pathLen) {
        float px = lightBeast.path[lightBeast.pathIdx][0] + 0.5f;
        float py = lightBeast.path[lightBeast.pathIdx][1] + 0.5f;
        float dx = px - lightBeast.x, dy = py - lightBeast.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 0.12f) lightBeast.pathIdx++;
        else {
            /* v3.3：追光鬼同样会被胶水减速 */
    float step = lightBeast.speed * dt * (InGlue(lightBeast.x, lightBeast.y) ? GLUE_MONSTER_MUL : 1.0f);
            if (step > d) step = d;
            lightBeast.x += dx / d * step;
            lightBeast.y += dy / d * step;
        }
    }

    float pd2 = Dist2(lightBeast.x, lightBeast.y, posX, posY);
    /* v3.3：诱饵同样能替玩家挡追光鬼这一下 */
    if (lightBeast.hitCooldown <= 0.0f && pd2 < MONSTER_CATCH_R * MONSTER_CATCH_R && DecoyIntercept()) {
        lightBeast.hitCooldown = HIT_COOLDOWN_BASE;
    } else if (lightBeast.hitCooldown <= 0.0f && pd2 < MONSTER_CATCH_R * MONSTER_CATCH_R) {
            if (hiddenIn < 0) {   /* v3.0：藏起来抓不到 */
        sanity -= 14.0f;
            }
        if (sanity < 0.0f) sanity = 0.0f;
        extern int tookDamage;
        { extern int floorTookDamage; floorTookDamage = 1; }
        tookDamage = 1;
        healFlash = 0.16f;
        if (!monsterMuted) PlaySfx(SFX_SCREAM, 1.0f);
        SpawnLightBeast();
    }
    if (lightBeast.hitCooldown > 0.0f) lightBeast.hitCooldown -= dt;
}

/* ------------------------------------------------------------------ */
/* 跳脸虫（Face Jumper）—— QTE 怪                                      */
/* ------------------------------------------------------------------ */

/* 失败惩罚：扣理智 + 短时 debuff */
static void JumperFail(void) {
    sanity -= JUMPER_FAIL_SANITY;
    if (sanity < 0.0f) sanity = 0.0f;
    extern int tookDamage;
    { extern int floorTookDamage; floorTookDamage = 1; }
        tookDamage = 1;
    ApplyFx(FX_BLIND, JUMPER_FAIL_FX_POWER, JUMPER_FAIL_FX_TIME);
    ApplyFx(FX_SLOW, JUMPER_FAIL_FX_POWER, JUMPER_FAIL_FX_TIME);
    healFlash = 0.25f;
    if (!monsterMuted) PlaySfx(SFX_SCREAM, 1.0f);
}

/* 事件结束（无论成功失败）—— 虫子退场，本关不再出现 */
static void JumperFinish(void) {
    jumperPhase = JMP_DONE;
    jumperTimer = 0.0f;
    jumperResolved = 1;
    parasite.active = 0;
}

/* 阶段一成功：躲开 */
static void JumperDodged(void) {
    extern int faceDodged;
    faceDodged++;
    JumperFinish();
    PlaySfx(SFX_PICKUP, 0.6f);
}

/* 阶段二成功：按对了键 */
static void JumperKeyHit(void) {
    extern int faceDodged;
    faceDodged++;
    JumperFinish();
    PlaySfx(SFX_POWER, 0.6f);
}

/* 生成跳脸虫：放在玩家视野前方一段距离，
   然后它会「弹」到玩家脸上。 */
void SpawnParasite(void) {
    memset(&parasite, 0, sizeof(parasite));
    parasite.active = 1;
    parasite.attached = 0;
    parasite.ramp = 0.0f;
    parasite.attachTimer = 0.0f;
    parasite.hp = 1;
    parasite.stun = 0.0f;

    /* 起点：玩家正前方 4~6 格，随机偏一点 */
    float d = 4.0f + (float)(rand() % 3);
    float ang = (float)(rand() % 7 - 3) * 0.12f;
    float ca = cosf(ang), sa = sinf(ang);
    parasite.x = posX + (dirX * ca - dirY * sa) * d;
    parasite.y = posY + (dirY * ca + dirX * sa) * d;
    if (ShotBlocked(parasite.x, parasite.y)) {
        parasite.x = posX + dirX * 3.0f;
        parasite.y = posY + dirY * 3.0f;
    }

    jumperPhase = JMP_APPROACH;
    jumperTimer = JUMPER_APPROACH_TIME;
    jumperResolved = 0;
    jumperQteKey = rand() % JMP_KEY_COUNT;
    if (!monsterMuted) PlaySfx(SFX_SCREAM, 0.35f);
}

void UpdateParasite(float dt) {
    /* 还没到触发时机 */
    if (jumperTrigger >= 0.0f && jumperPhase == JMP_IDLE) {
        jumperTrigger -= dt;
        if (jumperTrigger <= 0.0f) {
            jumperTrigger = -1.0f;
            SpawnParasite();
        }
    }
    if (!parasite.active) return;

    switch (jumperPhase) {
    case JMP_APPROACH: {
        /* 从远处弹到玩家脸上：位置插值，尺寸也会变大（更吓人） */
        jumperTimer -= dt;
        float t = 1.0f - jumperTimer / JUMPER_APPROACH_TIME;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        /* 起点就是生成时记下的位置；这里直接朝玩家脸逼近 */
        float tx = posX + dirX * 0.75f;
        float ty = posY + dirY * 0.75f;
        parasite.x += (tx - parasite.x) * (dt * 7.0f);
        parasite.y += (ty - parasite.y) * (dt * 7.0f);
        parasite.ramp = t;    /* 复用 ramp 记录「逼近程度」，供渲染放大 */

        if (jumperTimer <= 0.0f) {
            /* 到位了：进入阶段一「躲开」 */
            jumperPhase = JMP_DODGE;
            jumperTimer = JUMPER_DODGE_WINDOW;
            parasite.x = tx;
            parasite.y = ty;
            if (!monsterMuted) PlaySfx(SFX_SCREAM, 0.8f);
        }
        break;
    }

    case JMP_DODGE: {
        jumperTimer -= dt;
        /* 玩家按「躲开键」（默认 SPACE，可改绑）= 躲开 */
        if (IsActionPressed(ACT_DODGE)) {
            JumperDodged();
            return;
        }
        if (jumperTimer <= 0.0f) {
            /* 没躲开：跿上脸，进入阶段二 */
            jumperPhase = JMP_QTE;
            jumperTimer = JUMPER_QTE_WINDOW;
            if (!monsterMuted) PlaySfx(SFX_SCREAM, 1.0f);
        }
        break;
    }

    case JMP_QTE: {
        jumperTimer -= dt;
        /* 阶段二：按下指定的键。
           键池不再是硬编码的 A/S/D/F —— 那样玩家改绑后提示键和实际
           绑定就对不上了。改成从**已绑定的动作键**里取：QTE 让你按的
           永远是你自己绑过的键，肌肉记忆直接可用。 */
        {
            /* v2.7：每帧现算。缓存一次的话，玩家在设置里改过移动键之后，
           提示显示新键、判定用旧键，按了没反应只能白吃惩罚。 */
        static const ActionId poolSrc[JMP_KEY_COUNT] = {
            ACT_MOVE_FWD, ACT_MOVE_BACK, ACT_MOVE_LEFT, ACT_MOVE_RIGHT
        };
        int want = g_bindings[poolSrc[jumperQteKey % JMP_KEY_COUNT]];
        if (want == 0) want = g_bindings[ACT_MOVE_FWD];   /* 绑成 None 也别卡死 */
            if (want != 0 && keyPressed[want]) {
                JumperKeyHit();
                return;
            }
        }
        if (jumperTimer <= 0.0f) {
            /* 超时：惩罚 */
            JumperFail();
            JumperFinish();
        }
        break;
    }

    default:
        break;
    }
}

/* ==================================================================
 * 修：**MonsterWander() 原来不带参数，只给第一只挑漫游点**
 * ------------------------------------------------------------------
 * 里面全程写死 `monster.x` / `monster.tx` / `MonPath(...)`，
 * 所以第二、第三只走到"该换漫游点"的分支时，
 * 新目标被写进了**第一只**身上，自己 pathLen 永远是 0 → 站着不动。
 * 这跟 MonPath 那个是同一类问题：老的单例时代留下的无参封装。
 * ================================================================ */
static void MonsterWanderFor(Monster* m) {
    int cx = (int)m->x, cy = (int)m->y;
    for (int t = 0; t < 24; t++) {
        int nx = cx + (rand() % 9) - 4;
        int ny = cy + (rand() % 9) - 4;
        if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
        if (worldMap[ny][nx]) continue;
        if (MonPathFor(m, cx, cy, nx, ny) > 0) {
            m->tx = nx + 0.5f;
            m->ty = ny + 0.5f;
            return;
        }
    }
}

/* 老的无参封装 MonsterWander() 已删除，原因同上：会写到第一只身上。 */




/* ===== v2.4.12 第二只听声怪 =====
   训练营要同时放两只听声怪做行为测试。
   这段是从 UpdateMonster 机械复制出来的，唯一的差别是操作 monster2。
   两只怪逻辑完全一致 —— 所以测出来的行为可以直接代表正式关卡。 */
/* ==================================================================
 * 听声鬼数量调度（2026-11-03 用户重做）
 * ------------------------------------------------------------------
 * 用户原话：
 *   「开局只一只，一分钟之后是两只，两分钟之后是 3 只。
 *     数量每多一只，听声鬼的听觉就会降低。
 *     拿听觉范围来换数量。」
 *
 * 设计意图：压力是**数量**给的，不是单只更强。
 *   · 1 只：听觉 100%，它是你唯一的听众，扔个瓶子就能骗走。
 *   · 2 只：听觉 75%，骗走一只还有另一只，包夹开始成形。
 *   · 3 只：听觉 55%，单个听得不远，但三个方向都有人在走 ——
 *           你不再能靠"一直跑"逃掉，只能靠路线规划。
 * 撞上任何一只仍然是 CatchSanityPeek 的递增档位（10/15/21/28/36/40），
 * 所以"越多越痛"是自动成立的。
 * ================================================================ */
#define HUNTER_SPAWN_2_T   60.0f   /* 第二只：60 秒 */
#define HUNTER_SPAWN_3_T  120.0f   /* 第三只：120 秒 */

static float s_hunterSpawnT = 0.0f;
static int   s_hunterSpawned = 1;   /* 第一只开局就在，所以从 1 算起 */

void ResetSecondSpawn(void) {
    s_hunterSpawnT = 0.0f;
    s_hunterSpawned = 1;
}

/* 当前活着几只听声鬼 */
int HunterActiveCount(void) {
    int n = 0;
    for (int i = 0; i < MON_HUNTER_MAX; i++)
        if (monHunters[i].active) n++;
    return n;
}

/* ==================================================================
 * 每只的听觉倍率（2026-11-03 用户新设计）
 * ------------------------------------------------------------------
 * 原来是「按数量统一降听觉」，用户改成分只在个体上：
 *   第 1 只  100% —— 它就是基准，耳朵最好，没有额外能力
 *   第 2 只   75% —— 矮个子，耳朵钝一点，但撞到你挂 20% 减速
 *   第 3 只   50% —— 长颈，耳朵最差，可是跑得最快、追得最久
 *
 * 这样三只是三种威胁，不是「同一只怪的三份拷贝」。
 * ================================================================ */
static const float kHunterHear[3] = { 1.00f, 0.75f, 0.50f };

int HunterIndex(const Monster* m) {
    int i = (int)(m - monHunters);
    if (i < 0 || i >= MON_HUNTER_MAX) i = 0;
    return i;
}

float HunterHearMulAt(int idx) {
    if (idx < 0 || idx >= MON_HUNTER_MAX) return 1.0f;
    return kHunterHear[idx];
}

/* 兼容旧调用：返回"场上最好的耳朵"，只用于 HUD/调试显示 */
float HunterHearMul(void) {
    float best = 0.0f;
    for (int i = 0; i < MON_HUNTER_MAX; i++) {
        if (!monHunters[i].active) continue;
        float k = kHunterHear[i];
        if (k > best) best = k;
    }
    return best > 0.0f ? best : 1.0f;
}

/* 第 3 只的移动速度倍率（长得最瘦、跑得最快） */
#define HUNTER3_SPEED_MUL   1.45f
/* 第 3 只的追击时长倍率（是另外两只的两倍） */
#define HUNTER3_CHASE_MUL   2.0f
/* 第 2 只撞到玩家挂的减速 */
#define HUNTER2_SLOW_PCT    0.20f
#define HUNTER2_SLOW_T      10.0f

/* 在玩家 10~16 格外找一个能站的格子，把第 idx 只放下去 */
static int PlaceHunter(int idx) {
    for (int t = 0; t < 160; t++) {
        float ang = (float)(rand() % 6283) / 1000.0f;
        float d = 10.0f + (float)(rand() % 600) / 100.0f;
        float x = posX + cosf(ang) * d;
        float y = posY + sinf(ang) * d;
        if (x < 1.5f || y < 1.5f || x > MAP_WIDTH - 1.5f || y > MAP_HEIGHT - 1.5f) continue;
        if (Blocked(x, y)) continue;

        Monster* M = &monHunters[idx];
        M->active = 1;
        /* ============================================================
         * **这里必须给速度** —— 之前漏了。
         * 只有 SpawnMonster() 会设 monster.speed，而它只管第一只；
         * PlaceHunter 不给 speed，第 2/3 只就带着 speed=0 出场：
         * 路径算出来了、pathIdx 也在推进，但 step = speed*dt = 0，
         * 于是一动不动。调试输出里 hunter2/hunter3 的 speed 就是 0.00。
         * ============================================================ */
        M->speed = MonsterSpeedFor(currentLevel, MONSTER_SPEED_BASE, modSpeedMul)
                 * (idx == 2 ? HUNTER3_SPEED_MUL : 1.0f);
        M->x = x;
        M->y = y;
        M->tx = posX;
        M->ty = posY;
        M->state = 0;
        M->huntTimer = 0.0f;
        M->stun = 0.0f;
        M->hitCooldown = HitCooldownFor(currentLevel);
        M->pathLen = 0;
        M->pathIdx = 0;
        M->repath = 0.0f;
        M->sightHold = 0.0f;
        M->stillTimer = 0.0f;
        M->wanderTimer = 1.5f;
        M->prevX = x;
        M->prevY = y;

        PlaySfx(SFX_WARDEN, 0.55f);
        pickupMsg = (idx == 1) ? "A SECOND SET OF FOOTSTEPS..."
                               : "FOOTSTEPS FROM EVERY SIDE...";
        pickupMsgTimer = 2.2f;
        return 1;
    }
    return 0;
}

/* ==================================================================
 * **只给测试用**：一次性召出三只听声鬼
 * ------------------------------------------------------------------
 * 2026-11-03：这个函数原来挂在 F3 热键上方便手动验证，
 * 发行前热键已经删掉（调试热键不进正式版），函数保留给
 * tests/test_runtime.c 的 test_three_hunters / test_hunter_models 用。
 * 三只都会重新摆位（哪怕已经活着）。
 * ================================================================ */
void TestSummonAllHunters(void) {
    for (int i = 0; i < MON_HUNTER_MAX; i++) {
        monHunters[i].active = 0;
        monHunters[i].stun = 0.0f;
        monHunters[i].hitCooldown = 0.0f;
        monHunters[i].pathLen = 0;
        monHunters[i].pathIdx = 0;
        monHunters[i].state = 0;
        monHunters[i].huntTimer = 0.0f;
        monHunters[i].stillTimer = 0.0f;
        monHunters[i].sightHold = 0.0f;
    }
    for (int i = 0; i < MON_HUNTER_MAX; i++)
        PlaceHunter(i);

    /* 调度器别再补了 —— 三只已经全在场 */
    s_hunterSpawned = MON_HUNTER_MAX;

    pickupMsg = "DEBUG: 3 SOUND HUNTERS";
    pickupMsgTimer = 2.0f;
    uiDirty = 1;
    PlaySfx(SFX_WARDEN, 0.9f);
}

void TickSecondSpawn(float dt) {
    if (currentState != STATE_PLAYING) return;
    if (trainingMode) return;

    /* 玩家在出口附近时不刷 —— 别在过门的瞬间糊一只到脸上 */
    {
        extern int breakerOpen, exitChoiceOpen;
        if (exitChoiceOpen) return;
        if (breakerOpen) {
            extern float Dist2(float, float, float, float);
            extern float exitX, exitY;
            if (Dist2(posX, posY, exitX, exitY) < 9.0f) return;
        }
    }

    s_hunterSpawnT += dt;
    if (s_hunterSpawned >= MON_HUNTER_MAX) return;

    /* 威胁越高来得越早（保留原来的两个修正，但上限不超过 25%） */
    float adv = 0.0f;
    { extern float ThreatSpawnAdvance(void); adv = ThreatSpawnAdvance(); }
    { extern float DailySecondSpawnOffset(void); adv -= DailySecondSpawnOffset(); }

    float base = (s_hunterSpawned == 1) ? HUNTER_SPAWN_2_T : HUNTER_SPAWN_3_T;
    if (adv > base * 0.25f) adv = base * 0.25f;
    if (adv < 0.0f) adv = 0.0f;
    if (s_hunterSpawnT < base - adv) return;

    int idx = s_hunterSpawned;
    if (!PlaceHunter(idx)) return;      /* 没找到位置 → 下帧再试 */
    s_hunterSpawned = idx + 1;
}


/* 前向声明：UpdateMonster2 里命中玩家后要把怪推开（定义在本文件后面） */
static void KnockbackMonster(Monster* m);

static void UpdateOneHunter(Monster* M, float dt) {
    /* ===== v2.4.14 **怪物扑到玩偶上** =====
       用户反馈：怪贴到玩偶上之后「不消失、没有实际反应、直接走开了」。
       这里补上：怪进入 0.7 格内 → 玩偶当场炸掉（消失）+ 怪顿一下 +
       接下来 10 秒不再刷新目标（suspCool = 10），所以它会原地乱走而不是立刻改追别的。
    */
    {
        extern Decoy decoysArr[];
        for (int di = 0; di < MAX_DECOY; di++) {
            if (!decoysArr[di].active || !decoysArr[di].armed) continue;
            float ddx = M->x - decoysArr[di].x;
            float ddy = M->y - decoysArr[di].y;
            if (ddx * ddx + ddy * ddy > 0.49f) continue;   /* 0.7 格内 */

            decoysArr[di].active = 0;      /* 玩偶被扑碎 */
            decoysArr[di].armed  = 0;
            M->stun = 10.0f;             /* v2.4.15：**定身 10 秒**（用户要求） */
            M->state = 0;
            M->suspCool = 10.0f;           /* **10 秒内不再刷新目标** */
            M->suspTarget = -1;
            M->pathLen = 0;
            M->pathIdx = 0;
            M->repath = 0.0f;
            PlaySfx(SFX_GLASS, 1.0f);
            {
                extern const char* pickupMsg;
                extern float pickupMsgTimer;
                extern int   uiDirty;
                extern int   g_lang;
                pickupMsg = (g_lang == 1) ? "玩偶被扑碎了" : "DECOY SHREDDED";
                pickupMsgTimer = 1.4f;
                uiDirty = 1;
            }
            break;
        }
    }

    /* v3.0：**寻声怪要能看见玩家** ——
       原来它只对噪音有反应（MakeNoise 是唯一的 state=1 入口），
       于是玩家站在它面前 2 米，它依然在"游荡"，看起来像坏了。
       这里补视线检测：视野内 + HasLOS 通 → 切追捕并把目标锁定到玩家。
       手电关着时视野减半，这样"关灯潜行"才有实际收益。 */
    if (M->active && !playerInSafe) {
        float sdx = posX - M->x, sdy = posY - M->y;
        float sd2 = sdx * sdx + sdy * sdy;
        /* v3.0：BLACKOUT 生效期间追捕者完全失明 */
        extern float ultTimer;
        float sight = flashlightOn ? MONSTER_SIGHT : MONSTER_SIGHT_DARK;
        if (ultTimer > 0.0f) sight = 0.0f;
        if (sd2 < sight * sight && HasLOS(M->x, M->y, posX, posY)) {
            /* v3.0 削弱：**连续看见 0.6 秒才真的发现** ——
               原来只要一帧视线通就立刻切追捕，玩家连反应的机会都没有。
               现在有 0.6 秒的「察觉期」，这段时间内跑出视野就能断开。 */
            M->sightHold += dt;
            if (M->state != 1 && M->sightHold >= 0.6f) {
                M->state = 1;
        { extern int floorSpotted, runSpotted; floorSpotted = 1; runSpotted = 1; }
                M->repath = 0.0f;
                M->stateTimer = 0.0f;
                PlaySfx(SFX_HEART, 0.55f);   /* 心跳：它发现你了 */
            }
            /* 追捕中持续刷新目标（不然它只会跑向"最后一次听到的位置"） */
            M->tx = posX;
            M->ty = posY;
        } else {
            M->sightHold = 0.0f;    /* 视线断开 → 察觉计时清零 */
        }
    }

    /* ===== v7 **听声怪从可疑点表里挑目标** =====
       只在「没在追人」的时候挑，而且要求强度够高（>28），
       否则刚走到就会因为一条 15 强度的走路噪音又改向，来回抖。

       distPenalty 0.9：越近的点越容易被选，
       避免它一直跑去地图另一头追一条早就过期的线索。 */
    /* v2.3.1 崩溃+闪烁修复：
       原来条件是 state != 1，于是**每一帧**都会重新设 state=1 并清空路径，
       而状态机自己又会把 state 打回 0/2 —— 两者打架，导致
         · HUD 右侧的追击状态在 WANDERING / HUNTING 之间来回闪
         · 路径被每帧强制重算，A* 反复写 M->path[]
           → msvcrt 里 memcpy/memset 越界，c0000005 崩溃
       现在：只在「游荡中」才接新线索，且冷却 1.5 秒，路径只在真正换目标时清。 */
    if (M->active && M->state == 0) {
        if (M->suspCool > 0.0f) {
            M->suspCool -= dt;
        } else {
            int idx = SuspicionBest(M->x, M->y, 0.9f);
            /* ============================================================
             * 修：**阈值原来是 28，比几乎所有噪音源都高 → 等于没有耳朵**
             * ------------------------------------------------------------
             * MakeNoise 写进可疑点的强度是 radius * 2.6：
             *     走路  9.0 → 23.4   ← 28 挡住
             *     翻箱 10.0 → 26.0   ← 28 挡住
             *     摇电 12.0 → 31.2   ← 只有这个能过
             * 于是玩家正常走/搜全程「没反应」，三只只在摇电时动一下，
             * 其余时间都在漫游 —— 就是"走一走卡一卡 + 没听觉"的来源。
             *
             * 基准取 15（按各噪音源的实际强度反推出来的）：
             *
             *   噪音源        半径   写入强度
             *   走路           9.0     23.4
             *   翻箱（轻）     8.0     20.8
             *   翻箱（标准）  10.0     26.0
             *   摇电          12.0     31.2
             *   玩偶            —      75.0
             *
             * 于是三只的"能听见什么"是：
             *   第 1 只  阈 15  → 全部听得见（走路都惊动它）
             *   第 2 只  阈 20  → 走路以上；只有最轻的翻箱听不见
             *   第 3 只  阈 30  → **正常走路听不见**，只有摇电/玩偶
             * 这才是「第一只听力最好」，而且第 3 只靠速度而不是耳朵。
             * ============================================================ */
            float need = 15.0f / HunterHearMulAt(HunterIndex(M));
            if (idx >= 0 && susp[idx].strength > need) {
                M->state = 1;
                M->tx = susp[idx].x;
                M->ty = susp[idx].y;
                M->repath = 0.0f;
                M->stateTimer = 0.0f;
                M->sightHold = 0.0f;
                M->suspTarget = idx;
                M->suspCool = 1.5f;
            } else {
                M->suspTarget = -1;
            }
        }
    }

    /* 到达可疑点 → 消费掉（不然它会一直站在那儿） */
    if (M->active && M->state == 1 && M->suspTarget >= 0 &&
        M->suspTarget < SUSP_MAX) {
        float ddx = M->x - susp[M->suspTarget].x;
        float ddy = M->y - susp[M->suspTarget].y;
        if (ddx * ddx + ddy * ddy < 0.64f) {
            SuspicionConsume(M->suspTarget);
            M->suspTarget = -1;
        }
    }

    /* v3.0 消失：隐身时追捕者立刻丢失目标 */
    {
        extern int vanished;
        if (vanished && M->active) {
            M->state = 0;
            M->pathLen = 0;
            M->pathIdx = 0;
        }
    }

    /* v3.0：噪音显示衰减 */
    {
        extern float noiseDisplay, noiseDisplayTimer;
        if (noiseDisplayTimer > 0.0f) {
            noiseDisplayTimer -= dt;
            if (noiseDisplayTimer <= 0.0f) noiseDisplay = 0.0f;
        }
    }
    /* v3.0：进入函数时先把**上一帧位置**推进 —— 函数末尾用它算位移，
       从而得到「怪自己面朝哪」和「走了多远」。
       原来朝向用的是屏幕方位角（16:9 下恒为正面，6 张侧面帧全废），
       走路帧用的是全局时钟（静止也踏步、所有怪同相）。 */
    {

        M->prevX = M->x;
        M->prevY = M->y;
    }
    if (!M->active) return;
    /* v1.8：玩家进安全屋 → 怪物立刻丢目标 */
    if (playerInSafe && M->state != 0) { M->state = 0; M->pathLen = 0; M->pathIdx = 0; }
    if (M->hitCooldown > 0.0f) M->hitCooldown -= dt;
    if (M->growlTimer > 0.0f) M->growlTimer -= dt;

    /* 硬直：被玻璃瓶砸中后短暂不能动，也不能抓人 */
    if (M->stun > 0.0f) {
        M->stun -= dt;
        if (M->stun < 0.0f) M->stun = 0.0f;
        return;
    }

    if (M->state == 1) {
        M->repath -= dt;
        if (M->repath <= 0.0f) {
            M->repath = 0.45f;
            /* ============================================================
                 * 修：**这句原来调的是 MonPath()，而 MonPath 写死了 &monster**
                 * ------------------------------------------------------------
                 *   static int MonPath(int sx,int sy,int tx,int ty)
                 *       { return MonPathFor(&monster, sx,sy,tx,ty); }
                 *
                 * 于是第二、第三只算出来的路径全写进了**第一只**的 path[]，
                 * 自己那个 M->pathLen 永远是 0 —— 没有路径就不会走，
                 * 所以它们「召出来站着不动」。
                 * 直接调带 Monster* 的 MonPathFor。
                 * ============================================================ */
                MonPathFor(M, (int)M->x, (int)M->y, (int)M->tx, (int)M->ty);
        }
        if (Dist2(M->x, M->y, M->tx, M->ty) < 0.45f) {
            M->state = 2;
                /* v3.4：甩脱追捕给金币 —— 这是逃生玩法的核心动作，
                   但它原来是零奖励，玩家没有「主动甩怪」的动力。 */
                {
                    extern int carryGold, totalGoldEarned;
                    extern const char* pickupMsg;
                    extern float pickupMsgTimer;
                    carryGold += GOLD_ESCAPE;
                    totalGoldEarned += GOLD_ESCAPE;
                    pickupMsg = "ESCAPED  +GOLD";
                    pickupMsgTimer = 1.2f;
                }          /* 到了声源，原地搜一圈 */
            M->stateTimer = 3.5f;
            M->pathLen = 0;
            M->pathIdx = 0;
        }
            /* v3.0：**追捕要有超时** ——
           用户实测「怪物不会离开」：只要视线通就一直贴着，
           永远不放弃也永远抓不到（抓取要求 0.74 格内）。
           追 8 秒还没抓到就降级为搜寻（3 秒），给玩家脱身窗口。 */
        M->huntTimer += dt;
        if (M->huntTimer > (HUNT_CHASE_MAX + ThreatChaseBonus())
                        * (HunterIndex(M) == 2 ? HUNTER3_CHASE_MUL : 1.0f)) {
            /* v7 第 50 轮：战役第 5 关的「永不放弃」原来只对第一只猎手生效
               （第一只在上面有 CampaignNoGiveUp 判断，这只没有）
               → 第 5 关实际上只有一半是"永不放弃"。 */
            { extern int CampaignNoGiveUp(void);
              if (CampaignNoGiveUp()) M->huntTimer = 0.0f; }
            { extern void BestiaryMarkEscaped(int); BestiaryMarkEscaped(0); }   /* v6 图鉴：甩掉了 */
            /* v3.0：**超时后要真的走开，不能原地搜** ——
               用户截图「还是在堵门」：原来超时只切到 state=2「原地搜寻」，
               而原地就在玩家旁边，于是它永远堵在那儿。
               现在给它一个「背离玩家 10 格」的撤退目标，走远了再变游荡。 */
            float ax = M->x - posX, ay = M->y - posY;
            float al = sqrtf(ax * ax + ay * ay);
            if (al < 0.01f) { ax = 1.0f; ay = 0.0f; al = 1.0f; }
            ax /= al; ay /= al;
            float rx = M->x + ax * 10.0f;
            float ry = M->y + ay * 10.0f;
            /* 夹在地图内 */
            if (rx < 1.5f) rx = 1.5f;
            if (ry < 1.5f) ry = 1.5f;
            if (rx > MAP_WIDTH - 1.5f) rx = MAP_WIDTH - 1.5f;
            if (ry > MAP_HEIGHT - 1.5f) ry = MAP_HEIGHT - 1.5f;

            M->state = 2;              /* 搜寻（会走开） */
            M->stateTimer = 6.0f;
            M->tx = rx;
            M->ty = ry;
            M->huntTimer = 0.0f;
            M->repath = 0.0f;
            M->pathLen = 0;
            M->pathIdx = 0;
        }
} else if (M->state == 2) {
        M->stateTimer -= dt;
        if (M->pathLen == 0 || M->pathIdx >= M->pathLen) {
            if (M->stateTimer <= 0.0f) {
                M->state = 0;
                M->wanderTimer = 0.5f;
            } else {
                MonsterWanderFor(M);
            }
        }
    } else {
        M->wanderTimer -= dt;
        if (M->wanderTimer <= 0.0f) {
            M->wanderTimer = 2.5f + (float)(rand() % 25) / 10.0f;
            MonsterWanderFor(M);
        }
    }

    /* 沿路径走 */
    if (M->pathIdx < M->pathLen) {
        float tx = M->path[M->pathIdx][0] + 0.5f;
        float ty = M->path[M->pathIdx][1] + 0.5f;
        float dx = tx - M->x, dy = ty - M->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 0.12f) {
            M->pathIdx++;
        } else {
            /* v3.3：踩到胶水 → 移速 x0.20（-80%）。还在动，只是变成慢动作。 */
    float step = M->speed * dt * (InGlue(M->x, M->y) ? GLUE_MONSTER_MUL : 1.0f) * GoldSpeedMul();
            if (step > d) step = d;
            M->x += dx / d * step;
            M->y += dy / d * step;
        }
    }

    /* 靠近时发出低吼（也是给玩家的听觉提示） */
    float pd2 = Dist2(M->x, M->y, posX, posY);
    if (pd2 < 36.0f && M->growlTimer <= 0.0f) {
        M->growlTimer = 2.6f;
        if (!monsterMuted) PlaySfx(SFX_SCREAM, pd2 < 9.0f ? 0.55f : 0.28f);
    }

    /* 抓到玩家：掉理智 + 怪被弹开（以后可以换成真正的地图事件） */
    /* v3.3：**诱饵先替玩家挨这一下** ——
       附近有激活的玩偶就抵掉，玩家不掉理智，怪物还会转向玩偶。 */
    /* v3.4 CATCH BREAK（局内树 id 14）：每局 2 次，被抓时自动挣脱不掉理智。
       优先级：诱饵 > CATCH BREAK > 正常受伤。 */
    extern int catchBreakUsed;   /* v3.4 CATCH BREAK 计数 */
    if (!playerInSafe && M->hitCooldown <= 0.0f && pd2 < MONSTER_CATCH_R * MONSTER_CATCH_R &&
        BenchCatchBreak() && catchBreakUsed < 2) {
        catchBreakUsed++;
        M->state = 2;
        M->pathLen = 0;
        M->pathIdx = 0;
        pickupMsg = "BROKE FREE!";
        pickupMsgTimer = 1.2f;
        PlaySfx(SFX_GLASS, 0.9f);
    } else     if (!playerInSafe && M->hitCooldown <= 0.0f && pd2 < MONSTER_CATCH_R * MONSTER_CATCH_R && DecoyIntercept()) {
        M->hitCooldown = HitCooldownFor(currentLevel);
        for (int di = 0; di < MAX_DECOY; di++) {
            if (decoysArr[di].active && decoysArr[di].armed) {
                M->state = 1;
                M->tx = decoysArr[di].x;
                M->ty = decoysArr[di].y;
                M->repath = 0.0f;
                M->pathLen = 0;
                M->pathIdx = 0;
                break;
            }
        }
    } else if (!playerInSafe && M->hitCooldown <= 0.0f && pd2 < MONSTER_CATCH_R * MONSTER_CATCH_R) {
        /* v1.8：被抓的惩罚随层数增长，且受局内"铁皮"减免 */
        float catchLoss = CatchSanityPeek();   /* v2.4.93 递增档位 */
        /* v3.0：**藏在柜子里 = 完全抓不到**。
           原来只包住了扣理智那一行，后面的红屏/尖叫/tookDamage/
           怪物重生全在 if 外照跑 —— 怪贴到 0.55 格内照样红屏，
           而且 tookDamage=1 会让 PERFECT ESCAPE 成就失效。 */
        /* v3.0 改造树「假死」：每局一次，被抓住时骗过怪物 */
        {
            extern int playDeadUsed;
            extern int BenchPlayDead(void);
            /* v3.0：假死现在技能树和改造树任一拥有都可用 */
            if (PlayDeadAvailable() && !playDeadUsed) {
                playDeadUsed = 1;
                M->stun = 2.5f;
                M->state = 0;
                M->pathLen = 0;
                M->hitCooldown = 2.5f;
                pickupMsgTimer = 2.0f;
                PlaySfx(SFX_HEART, 1.0f);
                uiDirty = 1;
                return;
            }
        }

        if (hiddenIn < 0) {
            /* v3.0：厚皮三层叠加 —— 改造树 -15%、局内 THICK HIDE -35% */
            sanity -= catchLoss * RunDamageTakenMul() * BenchFearDamageMul() * ThickHideMul();
            CatchCountBump();   /* v2.4.93：这一次记账，下次更疼 */
            { extern void CatchCountBumpRun(void); CatchCountBumpRun(); }
            { extern void BestiaryMarkCaught(int); BestiaryMarkCaught(0); }   /* 图鉴：任何一只听声鬼抓到都算 */
            { extern void FlankOnCaught(void); FlankOnCaught(); }   /* v2.4.96 封喉者扑上来 */

            /* ============================================================
             * **第 2 只（矮个子）的特殊能力：撞到就挂 20% 减速**
             * ------------------------------------------------------------
             * 它耳朵只有 75%，但一旦贴上来就让你瘸一阵 ——
             * 三只里它是唯一会"留下持续后果"的。
             * 走现成的 FX_SLOW 通道（game.c: slowMul = 1 - strength*0.45），
             * 所以 0.20 / 0.45 ≈ 0.444 正好等于 20% 减速。
             * ============================================================ */
            if (HunterIndex(M) == 1) {
                extern void ApplyFx(int, float, float);
                ApplyFx(FX_SLOW, HUNTER2_SLOW_PCT / 0.45f, HUNTER2_SLOW_T);
            }

    AddHitFlash(0.85f);          /* v2.1：被抓的红闪 */
    AddShake(0.30f, 0.22f);
    PlaySfx(SFX_HEART, 0.95f);
        if (sanity < 0.0f) sanity = 0.0f;
        extern int tookDamage;
        { extern int floorTookDamage; floorTookDamage = 1; }
        tookDamage = 1;
        healFlash = 0.18f;
        if (!monsterMuted) PlaySfx(SFX_SCREAM, 1.0f);
        if (M == &monHunters[0]) {
            SpawnMonster();          /* 第一只：重生并换位置（老行为） */
        } else {
            /* 第二/第三只没有重生逻辑，必须自己补冷却 + 击退，
               否则会站在原地每帧再判一次命中 → 秒杀。 */
            M->hitCooldown = HitCooldownFor(currentLevel);
            KnockbackMonster(M);
        }
        /* ============================================================
         * 修：**第二只听声鬼「站着不动就把玩家秒了」**
         * ============================================================
         * 上面这句 SpawnMonster() 重生的是**第一只**（monster.），
         * 它顺带把 monster.hitCooldown 设好、位置也换掉，所以玩家只挨一下。
         *
         * 但 M 既没设冷却、也没被挪走：
         *   · 命中后它还在原地
         *   · 玩家还在 0.55 格内
         *   · hitCooldown 仍是 0
         * → 下一帧再判一次命中 → 每秒 60 次扣理智 → 一瞬间归零。
         * 玩家看到的就是「那只站着不动的听声鬼一碰就秒杀」。
         *
         * 现在补齐两件事：给冷却 + 把它推开（和第一只被重生挪走等价）。
         * ============================================================ */
        M->hitCooldown = HitCooldownFor(currentLevel);
        KnockbackMonster(M);
        }
    }
    /* v3.0：用实际位移更新朝向和走路相位（全局 M 单例） */
    {
        float mdx = M->x - M->prevX;
        float mdy = M->y - M->prevY;
        float d2 = mdx * mdx + mdy * mdy;
        if (d2 > 1e-7f) {
            M->facing = atan2f(mdy, mdx);
            M->walkPhase += sqrtf(d2) * 3.5f;   /* v3.0：1.2 太慢，腿看起来是滑行；3.5 步频自然又不闪 */
            if (M->walkPhase > 1000.0f) M->walkPhase -= 1000.0f;
        }

        /* ===== v3.0：**站着不动 2 秒就丢失目标** =====
           用户实测「老是堵门」：怪物卡在门口不移动，却一直保有你的位置，
           于是它永远站在那儿等你出来。
           现在统计「连续没移动」的时长，超过 2 秒就当作它跟丢了：
           清空目标、回到游荡、并给一个新的漫游点，逼它走开。 */
        if (d2 > 0.0004f) {          /* 这一帧动了（约 2cm） */
            M->stillTimer = 0.0f;
        } else {
            M->stillTimer += dt;
            if (M->stillTimer > 2.0f && M->state != 0) {
                M->state = 0;
                M->pathLen = 0;
                M->pathIdx = 0;
                M->sightHold = 0.0f;
                M->huntTimer = 0.0f;
                M->stillTimer = 0.0f;
                M->wanderTimer = 0.0f;   /* 立刻挑新漫游点，别又站着 */
            }
        }

        M->prevX = M->x;
        M->prevY = M->y;
    }
}


/* 三个入口都是薄壳 —— 逻辑只有一份（UpdateOneHunter）。
   原来 UpdateMonster / UpdateMonster2 是 96% 相同的两份拷贝，
   第二只漏了冷却、漏了「永不放弃」，都是复制粘贴带出来的。 */
void UpdateMonster (float dt) { UpdateOneHunter(&monHunters[0], dt); }
void UpdateMonster2(float dt) { UpdateOneHunter(&monHunters[1], dt); }
void UpdateMonster3(float dt) { UpdateOneHunter(&monHunters[2], dt); }




/* ------------------------------------------------------------------ */
/* 玻璃瓶命中怪物                                                       */
/* ------------------------------------------------------------------ */

/* 让被砸中的怪后退（沿「远离玩家」的方向），并进入硬直。
   后退距离要够明显 —— 玩家需要从反馈里知道「我打中了」。 */
static void KnockbackMonster(Monster* m) {
    float dx = m->x - posX, dy = m->y - posY;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) { dx = -dirX; dy = -dirY; d = 1.0f; }
    else { dx /= d; dy /= d; }

    /* 沿反方向找第一个能站住的位置 */
    for (int step = 3; step >= 1; step--) {
        float nx = m->x + dx * MONSTER_FLEE_DIST * (float)step / 3.0f;
        float ny = m->y + dy * MONSTER_FLEE_DIST * (float)step / 3.0f;
        if (!Blocked(nx, ny)) { m->x = nx; m->y = ny; break; }
    }
    /* v2.7：接上 STAGGER（瓶子硬直 +0.2s/级）。原来这里写死 MONSTER_STUN，
       导致技能树里买了 STAGGER 完全没反应。 */
    m->stun = MONSTER_STUN + SkillStunAdd();
    { extern int knockCountRun; knockCountRun++; }   /* v7：DISRUPTOR 成就用 */
    m->pathLen = 0;
    m->pathIdx = 0;
    m->state = 0;              /* 被打断，重新游荡 */
    m->wanderTimer = 0.6f;
}

/* 玻璃瓶命中一只怪。返回 1 表示命中。 */
/* v1.15 伤害结算。
   base  基础伤害
   type  伤害类型（决定吃哪条抗性）
   sx,sy 伤害来源位置（用于判定背刺：来源在怪的背后 → ×1.5） */
extern int g_dmgIgnoreArmor;   /* v1.16：1 = 本次伤害无视护甲（酸瓶） */

/* v2.1：怪物血量随层成长。第 1 层 60，第 10 层 240。
   武器满蓄力 59/39/72 → 开局 1~2 击，第 10 层 3~6 击，
   熟练度（+24%）和配件（×1.5）第一次有了可观察的效果。 */

int MonsterHpFor(int level) {
    if (level < 1) level = 1;
    return MONSTER_HP + MONSTER_HP_STEP * (level - 1);
}

#if NO_COMBAT
/* v3.0 逃生试炼：**攻击不再造成伤害**。
   打中怪物只制造噪音（把附近的追捕者引过来）+ 短暂硬直。
   玩家永远不会「打死」任何东西 —— 这是逃生类的核心前提。 */
int ApplyDamage(Monster* m, int base, int type, float sx, float sy) {
    (void)base; (void)type; (void)sx; (void)sy;
    if (!m || !m->active) return 0;
    MakeNoise(m->x, m->y, 14.0f);
    m->stun = 0.35f;
    { extern int throwHitCountRun; throwHitCountRun++; }   /* v7：PITCHER 成就用 */
    PlaySfx(SFX_GLASS, 0.5f);
    SpawnParticles(m->x, m->y, 2);
    return 0;
}
/* v2.4.7 删除死代码：NO_COMBAT=1 时上面那段就够了，
   这里原来是完整的伤害/抗性/护甲/背刺结算管线（146 行），
   因为条件编译永远不参与编译。留着只会让人以为战斗系统还在 —— 
   谁哪天把 NO_COMBAT 翻成 0，那套逻辑会突然复活，而 UI 和平衡全按「打不死」写的。 */
#endif  /* NO_COMBAT */

int HitMonster(Monster* m, int dmg) {
    if (!m->active) return 0;
    if (m->stun > 0.0f) return 0;    /* 已经在硬直里，不重复结算 */

    /* 跳脸虫不在这条路径上 —— 它是屏幕空间的 QTE 事件，
       不在世界里，也没有实体可以砸。 */

    m->hp -= dmg;
    m->hitCooldown = 0.0f;           /* 硬直期间不该抓人 */

    if (m->hp <= 0) {
        /* 血量耗尽：直接驱离（重新生成到别处），而不是留在原地 */
        if (m == &monster) SpawnMonster();
        else SpawnLightBeast();
        /* 成就：用瓶子击杀怪物 */
        extern int bottlesKilled;
        bottlesKilled++;
    } else {
        KnockbackMonster(m);
    }
    PlaySfx(SFX_GLASS, 0.9f);
    if (!RunBottleSilent())          /* v1.8：静默瓶规则可免除这次暴露 */
        MakeNoise(posX, posY, 9.0f);   /* v2.2：打怪发声应当暴露玩家，而不是让怪自己失忆 */ /* 砸怪的代价：暴露自己的位置 */
    return 1;
}


/* ================================================================== */
/* v1.8 偷电怪：不造成直接伤害，只悄悄抽电；只有手电照到才可见          */
/* ================================================================== */
Leech leeches[LEECH_MAX];

void ResetLeeches(void) {
    for (int i = 0; i < LEECH_MAX; i++) {
        leeches[i].active = 0;
        leeches[i].x = leeches[i].y = 0.0f;
        leeches[i].age = leeches[i].repelTimer = leeches[i].litTimer = 0.0f;
    }
}

/* 随机挑一个地图空地生成 */
void SpawnLeech(int index) {
    /* ===== v7 **电池水蛭已删除** =====
       它偷的是「电量焦虑」，而现在：
         · 藏身点能回电（4.0/秒）
         · 摇电早松手也给电
         · 电池包 60 金币一个
       电量焦虑已经被削光了，它跟着一起废掉。
       保留函数壳子，让 game.c 的调用点不用改。 */
    (void)index;
    if (1) return;

    if (index < 0 || index >= LEECH_MAX) return;
    for (int attempt = 0; attempt < 200; attempt++) {
        int x = 1 + rand() % (MAP_WIDTH - 2);
        int y = 1 + rand() % (MAP_HEIGHT - 2);
        if (worldMap[y][x]) continue;
        float cx = x + 0.5f, cy = y + 0.5f;
        if (Dist2(cx, cy, posX, posY) < 9.0f) continue;      /* 别贴脸出生 */
        leeches[index].x = cx;
        leeches[index].y = cy;
        leeches[index].age = 0.0f;
        leeches[index].repelTimer = 0.0f;
        leeches[index].litTimer = 0.0f;
        leeches[index].active = 1;
        return;
    }
}

/* 最近偷电怪的距离平方（HUD / 声音提示用） */
int LeechNearestDist2(void) {
    float best = -1.0f;
    for (int i = 0; i < LEECH_MAX; i++) {
        if (!leeches[i].active) continue;
        float d = Dist2(posX, posY, leeches[i].x, leeches[i].y);
        if (best < 0.0f || d < best) best = d;
    }
    if (best < 0.0f) return -1;
    return (int)best;
}

void UpdateLeech(float dt) {
    for (int i = 0; i < LEECH_MAX; i++) {
        Leech* L = &leeches[i];
        if (!L->active) continue;
        L->age += dt;
        L->draining = 0;

        /* v1.8：手电照住 0.6 秒 → 直接逃走消失。
           必须放在最前面：下面"被照退避"那一支会 continue，
           放在后面的话计时永远累积不到（第一版就踩了这个坑）。 */
        {
            float ldx = L->x - posX, ldy = L->y - posY;
            float ld2 = ldx * ldx + ldy * ldy;
            int lit = 0;
            if (flashlightOn && battery > 0.0f && ld2 < 64.0f) {
                float ang = atan2f(ldy, ldx), face = atan2f(dirY, dirX);
                float diff = ang - face;
                while (diff > 3.14159f) diff -= 6.28318f;
                while (diff < -3.14159f) diff += 6.28318f;
                /* 不要求严格视线：它在墙后也能偷电，玩家举着手电对着它照
                   就应该算数（否则会出现"照了没反应"的怪异手感）。 */
                if (fabsf(diff) < 0.6f) lit = 1;
            }
            if (lit) {
                L->litTimer += dt;
                if (L->litTimer >= 0.6f) {
                    L->active = 0;
                    if (!monsterMuted) PlaySfx(SFX_SCREAM, 0.55f);
                    continue;
                }
            } else {
                L->litTimer -= dt * 0.5f;
                if (L->litTimer < 0.0f) L->litTimer = 0.0f;
            }
        }
        if (L->repelTimer > 0.0f) {
            L->repelTimer -= dt;
            /* 被手电照着就后退 */
            float dx = L->x - posX, dy = L->y - posY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 0.01f) {
                L->x += dx / d * 1.2f * dt;
                L->y += dy / d * 1.2f * dt;
            }
            continue;
        }
        /* 速度随时间成长，上限 1.7（永远慢于玩家 2.0） */
        float sp = LEECH_SPEED_BASE + LEECH_SPEED_GROW * L->age;
        if (sp > LEECH_SPEED_CAP) sp = LEECH_SPEED_CAP;
        if (levelMod >= 0 && levelMod == 1) sp *= LEECH_HUNTED_MUL;   /* HUNTED 词条 */

        float dx = posX - L->x, dy = posY - L->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > LEECH_RANGE) {
            /* 追玩家，但会被墙挡住（简单轴分离） */
            float nx = L->x + dx / d * sp * dt;
            float ny = L->y + dy / d * sp * dt;
            if (!Blocked(nx, L->y)) L->x = nx;
            if (!Blocked(L->x, ny)) L->y = ny;
        } else if (!playerInSafe) {
            /* 范围内：抽电（安全屋里不抽） */
            float rate = LEECH_DRAIN_BASE + LEECH_DRAIN_STEP * (float)currentLevel;
            if (rate > LEECH_DRAIN_CAP) rate = LEECH_DRAIN_CAP;
            battery -= rate * dt;
            if (battery < 0.0f) battery = 0.0f;
            L->draining = 1;
        }

    }
}

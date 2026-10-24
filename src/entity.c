/*
 * Canity — 实体层实现：玩家状态 / 状态效果 / 三种怪物 + 跳脸虫 QTE
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "entity.h"
#include "game.h"
#include "render.h"
#include "audio.h"


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
Monster monster;
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
    return s;
}

/* 被抓后的无敌时间：深层收紧，让连续被抓不再是无解连击。
   第 6 层开始，每深一层减 0.1 秒，第 15 层触到下限 3.0 秒。 */
static float HitCooldownFor(int level) {
    if (level < HIT_COOLDOWN_START_LEVEL) return HIT_COOLDOWN_BASE;
    float t = HIT_COOLDOWN_BASE - 0.1f * (float)(level - HIT_COOLDOWN_START_LEVEL + 1);
    if (t < HIT_COOLDOWN_MIN) t = HIT_COOLDOWN_MIN;
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

static int MonPath(int sx, int sy, int tx, int ty) { return MonPathFor(&monster, sx, sy, tx, ty); }

/* 把怪放到离玩家足够远的空格 */
void SpawnMonster(void) {
    monster.active = 1;
    monster.state = 0;
    monster.speed = MonsterSpeedFor(currentLevel, MONSTER_SPEED_BASE, modSpeedMul);
    monster.pathLen = 0;
    monster.pathIdx = 0;
    monster.repath = 0.0f;
    monster.stateTimer = 0.0f;
    monster.wanderTimer = 1.5f;
    monster.hitCooldown = HitCooldownFor(currentLevel);
    monster.growlTimer = 3.0f;
    monster.hp = MONSTER_HP;
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
void MakeNoise(float x, float y, float radius) {
    if (!monster.active) return;
    radius *= (1.0f - 0.15f * upgQuiet);
    if (Dist2(x, y, monster.x, monster.y) > radius * radius) return;
    monster.state = 1;
    monster.tx = x;
    monster.ty = y;
    monster.repath = 0.0f;
    monster.stateTimer = 0.0f;
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
    lightBeast.hp = LIGHTBEAST_HP;
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
            float step = lightBeast.speed * dt;
            if (step > d) step = d;
            lightBeast.x += dx / d * step;
            lightBeast.y += dy / d * step;
        }
    }

    float pd2 = Dist2(lightBeast.x, lightBeast.y, posX, posY);
    if (lightBeast.hitCooldown <= 0.0f && pd2 < 0.55f) {
        sanity -= 14.0f;
        if (sanity < 0.0f) sanity = 0.0f;
        healFlash = 0.16f;
        PlaySfx(SFX_SCREAM, 1.0f);
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
    ApplyFx(FX_BLIND, JUMPER_FAIL_FX_POWER, JUMPER_FAIL_FX_TIME);
    ApplyFx(FX_SLOW, JUMPER_FAIL_FX_POWER, JUMPER_FAIL_FX_TIME);
    healFlash = 0.25f;
    PlaySfx(SFX_SCREAM, 1.0f);
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
    JumperFinish();
    PlaySfx(SFX_PICKUP, 0.6f);
}

/* 阶段二成功：按对了键 */
static void JumperKeyHit(void) {
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
    PlaySfx(SFX_SCREAM, 0.35f);
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
            PlaySfx(SFX_SCREAM, 0.8f);
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
            PlaySfx(SFX_SCREAM, 1.0f);
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
            static int keyCodes[JMP_KEY_COUNT];
            static int poolBuilt = 0;
            if (!poolBuilt) {
                /* 从常用动作里挑 4 个不重复、非 0 的绑定键做池子 */
                static const ActionId poolSrc[JMP_KEY_COUNT] = {
                    ACT_MOVE_FWD, ACT_MOVE_BACK, ACT_MOVE_LEFT, ACT_MOVE_RIGHT
                };
                for (int i = 0; i < JMP_KEY_COUNT; i++) keyCodes[i] = g_bindings[poolSrc[i]];
                poolBuilt = 1;
            }
            int want = keyCodes[jumperQteKey % JMP_KEY_COUNT];
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

static void MonsterWander(void) {
    int cx = (int)monster.x, cy = (int)monster.y;
    for (int t = 0; t < 24; t++) {
        int nx = cx + (rand() % 9) - 4;
        int ny = cy + (rand() % 9) - 4;
        if (nx < 1 || ny < 1 || nx >= MAP_WIDTH - 1 || ny >= MAP_HEIGHT - 1) continue;
        if (worldMap[ny][nx]) continue;
        if (MonPath(cx, cy, nx, ny) > 0) {
            monster.tx = nx + 0.5f;
            monster.ty = ny + 0.5f;
            return;
        }
    }
}

void UpdateMonster(float dt) {
    if (!monster.active) return;
    if (monster.hitCooldown > 0.0f) monster.hitCooldown -= dt;
    if (monster.growlTimer > 0.0f) monster.growlTimer -= dt;

    /* 硬直：被玻璃瓶砸中后短暂不能动，也不能抓人 */
    if (monster.stun > 0.0f) {
        monster.stun -= dt;
        if (monster.stun < 0.0f) monster.stun = 0.0f;
        return;
    }

    if (monster.state == 1) {
        monster.repath -= dt;
        if (monster.repath <= 0.0f) {
            monster.repath = 0.45f;
            MonPath((int)monster.x, (int)monster.y, (int)monster.tx, (int)monster.ty);
        }
        if (Dist2(monster.x, monster.y, monster.tx, monster.ty) < 0.45f) {
            monster.state = 2;          /* 到了声源，原地搜一圈 */
            monster.stateTimer = 3.5f;
            monster.pathLen = 0;
            monster.pathIdx = 0;
        }
    } else if (monster.state == 2) {
        monster.stateTimer -= dt;
        if (monster.pathLen == 0 || monster.pathIdx >= monster.pathLen) {
            if (monster.stateTimer <= 0.0f) {
                monster.state = 0;
                monster.wanderTimer = 0.5f;
            } else {
                MonsterWander();
            }
        }
    } else {
        monster.wanderTimer -= dt;
        if (monster.wanderTimer <= 0.0f) {
            monster.wanderTimer = 2.5f + (float)(rand() % 25) / 10.0f;
            MonsterWander();
        }
    }

    /* 沿路径走 */
    if (monster.pathIdx < monster.pathLen) {
        float tx = monster.path[monster.pathIdx][0] + 0.5f;
        float ty = monster.path[monster.pathIdx][1] + 0.5f;
        float dx = tx - monster.x, dy = ty - monster.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 0.12f) {
            monster.pathIdx++;
        } else {
            float step = monster.speed * dt;
            if (step > d) step = d;
            monster.x += dx / d * step;
            monster.y += dy / d * step;
        }
    }

    /* 靠近时发出低吼（也是给玩家的听觉提示） */
    float pd2 = Dist2(monster.x, monster.y, posX, posY);
    if (pd2 < 36.0f && monster.growlTimer <= 0.0f) {
        monster.growlTimer = 2.6f;
        PlaySfx(SFX_SCREAM, pd2 < 9.0f ? 0.55f : 0.28f);
    }

    /* 抓到玩家：掉理智 + 怪被弹开（以后可以换成真正的地图事件） */
    if (monster.hitCooldown <= 0.0f && pd2 < 0.55f) {
        sanity -= 18.0f;
        if (sanity < 0.0f) sanity = 0.0f;
        healFlash = 0.18f;
        PlaySfx(SFX_SCREAM, 1.0f);
        SpawnMonster();
    }
}


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
    m->stun = MONSTER_STUN;
    m->pathLen = 0;
    m->pathIdx = 0;
    m->state = 0;              /* 被打断，重新游荡 */
    m->wanderTimer = 0.6f;
}

/* 玻璃瓶命中一只怪。返回 1 表示命中。 */
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
    } else {
        KnockbackMonster(m);
    }
    PlaySfx(SFX_GLASS, 0.9f);
    MakeNoise(m->x, m->y, 9.0f);     /* 砸怪的代价：暴露自己的位置 */
    return 1;
}

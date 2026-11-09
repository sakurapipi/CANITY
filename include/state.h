/*
 * Canity — 全局状态（v8 区块化第一步）
 * =====================================
 *
 * 为什么要这个文件
 * ────────────────
 * 改造前：127 处 `extern` 散落在 5 个 .c 文件里，任何文件都能直接读写
 *         任何别的文件的内部状态 → 「改 A 坏 B」的根本原因。
 *
 * 这个文件是**所有共享状态的唯一声明处**。规则：
 *
 *   ① 只放**声明**，不放逻辑、不放函数实现
 *   ② 需要共享的东西，从这里拿；不要在 .c 里写 `extern int xxx;`
 *   ③ 按区块分组，每一组对应一个未来的模块目录
 *
 * 依赖方向（只能单向，不许反向）：
 *
 *     表现层  ui / render / sprites      → 只读状态
 *        ↓
 *     逻辑层  ai / world / economy       → 读写状态
 *        ↓
 *     状态层  state.h                    → 纯数据
 *
 * 迁移策略：**不一次性搬完**。
 *   新写的代码（v7 之后）一律走 state.h；
 *   老代码原地不动，等它所在的那一块要改的时候顺手搬过来。
 *   这样每一步都是小改、可编译、可验证。
 */
#ifndef CANITY_STATE_H
#define CANITY_STATE_H

/* ================================================================== */
/* 区块 ①  玩家本体                                                    */
/* ================================================================== */
extern float posX, posY;          /* 位置 */
extern float dirX, dirY;          /* 朝向 */
extern float sanity;              /* 理智 0~100 */
extern float battery;             /* 手电电量 */
extern float stamina;             /* 体力 */
extern int   flashlightOn;
extern int   hiddenIn;            /* 当前藏身点下标，-1 = 不在藏身 */

/* ================================================================== */
/* 区块 ②  经济（v7 新增的全部集中在这里）                              */
/* ================================================================== */
extern int   carryGold;            /* 局内携带金币：可花、可被偷、决定强度档 */
extern int   bankGold;             /* 银行存款 */
extern int   bankHistMax;          /* 历史最高存款（携带倍数的分母） */
extern int   bankCarryPct;         /* 下一个携带比例档位 0..20 */

/* 强度档位（16 档，T0~T15）—— 只影响听觉与机制，永不影响移速 */
int   GoldTier(void);              /* 当前档位 0~15 */
float GoldTierMul(void);           /* 当前档位的强度倍率 1.00~1.75 */
float GoldHearMul(void);           /* 听觉半径倍数 */
float GoldSpeedMul(void);          /* 恒为 1.0，保留给旧调用点 */

/* 钱袋（v7） */
#define MAX_MONEYBAG 3
extern int   moneyDropOpen;        /* 滑条是否展开 */
extern int   moneyDropPct;         /* 10~100，每档 10 */
float MoneyBagX(int i);
float MoneyBagY(int i);
float MoneyBagLife(int i);
int   MoneyBagAmount(int i);
void  MoneyBagTake(int idx);
void  MoneyBagDropAt(float x, float y, int amount);
void  MoneyBagsResetForNewFloor(void);
void  MoneyTick(float dt);     /* v8: 钱袋每帧逻辑（src/money.c） */
int   ActionIsDown(int act);   /* v8: 按键查询公开包装 */

/* ================================================================== */
/* 区块 ③  AI 共享感知（v7 新模块）                                     */
/* ================================================================== */
#include "suspicion.h"             /* SuspicionPoint susp[12] 等 */

/* 清道夫触发用的计时器 */
extern float floorTime;            /* 本层已停留秒数 */
extern int   hideStreak;           /* 连续躲藏次数（搜箱清零） */

/* ================================================================== */
/* 区块 ④  新怪物（v7）                                                */
/* ================================================================== */
/* 拾荒者 */
int   CollectorActive(void);
float CollectorX(void);
float CollectorY(void);
int   CollectorBounty(void);
void  CollectorReset(void);
void  CollectorSpawnIfEligible(void);
void  CollectorHitByWrench(float hx, float hy);
void  UpdateCollector(float dt);

/* 清道夫 */
int   ScavengerActive(void);
float ScavengerX(void);
float ScavengerY(void);
float ScavengerWarnT(void);
void  ScavengerReset(void);
void  ScavengerTickTrigger(float dt);
void  UpdateScavenger(float dt);

/* ================================================================== */
/* 区块 ⑤  扳手（常驻能力 + 商店升级）                                  */
/* ================================================================== */
extern int   wrenchCharges;        /* 次数制下本层剩余次数 */
extern float wrenchCd;             /* 冷却制下剩余冷却 */

/* ================================================================== */
/* 区块 ⑥  待清理（v7 删除后留下的空壳）                                */
/* ================================================================== */
/*
 * 这些**已经不起作用**，只是为了不断开 save / HUD / shop 的引用而保留。
 * 重构到对应区块时一起删：
 *
 *   leeches[] / LEECH_* 常量        —— 电池水蛭已删
 *   extras.c 整个文件               —— 三只游荡怪已删
 *   bossLevel / bossMaxHp / bossDefeated —— BOSS 已删（恒为 0）
 */

/* Collector/scavenger observations shared by gameplay and presentation. */
float CollectorSpeedNow(void);

float ScavengerSpeedNow(void);

int   CollectorHasMoney(void);

int   ScavengerRifling(void);

void CollectorBurstBounty(void);

int MoneyBagNearestToPlayer(void);

#endif /* CANITY_STATE_H */

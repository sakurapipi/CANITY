/* ==================================================================
 * CANITY — 声音事件中心（v2.4-beta B01）
 * ------------------------------------------------------------------
 * 解决的问题（v2.4 第二轮审查里最一致的一条意见）：
 *
 *   现在 `MakeNoise(x, y, radius)` 用一个 **radius** 同时表达三件事：
 *     ① 能传多远        ② 强度多大        ③ 算哪一类线索
 *   而第 ③ 件是这么写的：
 *       int srcS = (radius <= 14.0f) ? SUSP_WALK : SUSP_SEARCH;
 *       float ttl = (srcS == SUSP_WALK) ? 3.0f : 6.0f;
 *   于是**改半径会顺手改线索类别和寿命** ——
 *   把破门石从 20 调到 14，它会从「搜索/敲击」变成「脚步」，
 *   寿命从 6 秒砍到 3 秒。那不是调数值，是换了一种东西。
 *
 * 这个头把三件事拆开：
 *   kind        —— 类别（决定 decay / ttl / 落到哪个可疑点来源）
 *   strength    —— 强度（0..100）
 *   maxRadius   —— 传播上限（格）
 * 三者独立设置，调任何一个都不影响另外两个。
 *
 * 兼容策略
 *   `MakeNoise()` 保留，内部合成一个 SoundEvent 再调 SoundEmit。
 *   只要合成出来的 kind/strength/decay/ttl 和旧算法一一对应，
 *   行为就是**逐位不变**的 —— 老调用点一个都不用改。
 *   新代码（破门石、诱饵、回声弹）直接用显式 kind。
 * ================================================================ */
#ifndef CANITY_SOUND_EVENT_H
#define CANITY_SOUND_EVENT_H

#include "common.h"

/* 声音类别。加一类 = 在 sound_event.c 的 kKindDefs 里加一行。 */
typedef enum {
    SK_WALK = 0,    /* 脚步 / 被动噪音：短寿命、快衰减 */
    SK_IMPACT,      /* 撞击：破门石破锁、瓶子落地 */
    SK_SEARCH,      /* 翻箱 / 开锁 / 敲墙 */
    SK_VOICE,       /* 诱饵鸣叫 —— 最强、寿命最长 */
    SK_MONSTER,     /* 怪物自己发出的声音 */
    SK_SCAN,        /* 回声弹的扫描脉冲 */
    SK_COUNT
} SoundKind;

typedef struct {
    unsigned int eventId;      /* 去重用；0 = 不去重（每次都是独立事件） */
    SoundKind    kind;
    float x, y;
    float strength;            /* 0..100，超出会被夹住 */
    float maxRadius;           /* 传播上限（格）；<=0 表示用类别默认值 */
    float decay;               /* <=0 表示用类别默认值 */
    float ttl;                 /* <=0 表示用类别默认值 */
    int   sourceEntityId;      /* -1 = 环境/玩家；用于排障与未来 AI 记忆 */
} SoundEvent;

/* 类别元数据（只读） */
int   SoundKindSusSource(SoundKind k);   /* → SUSP_* */
float SoundKindDecay(SoundKind k);
float SoundKindTtl(SoundKind k);
float SoundKindDefaultStrength(SoundKind k);
const char* SoundKindName(SoundKind k);  /* 排障 / 测试用 */

/* 提交一个事件。
   自己做：夹住数值 → 同帧同 eventId 去重 → 写入可疑点表 → 计数。
   返回 1 = 真的结算了；0 = 被去重或参数无效。 */
int SoundEmit(const SoundEvent* ev);

/* 每帧开头调一次，清掉"本帧已见 eventId"表。 */
void SoundEventTick(void);

/* 统计与测试钩子 */
void SoundEventResetStats(void);
int  SoundEventTotalEmitted(void);   /* 通过校验的次数 */
int  SoundEventTotalDeduped(void);   /* 被去重挡掉的次数 */
int  SoundEventLastKind(void);       /* 最近一次结算的 kind；-1 = 还没有 */
float SoundEventLastStrength(void);
float SoundEventLastTtl(void);

#endif /* CANITY_SOUND_EVENT_H */

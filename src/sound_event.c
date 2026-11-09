/* CANITY — 声音事件中心实现（v2.4-beta B01）
 *
 * 只依赖 suspicion.h（可疑点表）和 common.h。
 * 刻意**不**碰 entity.c 的内部状态（hiddenIn / monster），
 * 那些留在 MakeNoise 里 —— 它们是"谁听得见"的规则，不是"事件长什么样"。
 */
#include "common.h"
#include "sound_event.h"
#include "suspicion.h"

/* ==================================================================
 * 类别元数据表
 * ------------------------------------------------------------------
 * 这张表是 B01 的全部意义所在：**类别由 kind 决定，不由半径决定**。
 * 加一类声音 = 加一行，而不是在调用点里再写一个 if。
 * ================================================================ */
typedef struct {
    int   susSource;
    float decay;
    float ttl;
    float defStrength;
    const char* name;
} SoundKindDef;

static const SoundKindDef kKindDefs[SK_COUNT] = {
    /* 数值对齐 v2.4-alpha 之前的 MakeNoise 行为，保证兼容期逐位不变：
       radius <= 14 → SUSP_WALK(decay 14, ttl 3, 强度 36)
       radius >  14 → SUSP_SEARCH(decay 8, ttl 6, 强度 52) */
    /* SK_WALK    */ { SUSP_WALK,   14.0f, 3.0f, 36.0f, "walk"    },
    /* SK_IMPACT  */ { SUSP_SEARCH,  8.0f, 6.0f, 52.0f, "impact"  },
    /* SK_SEARCH  */ { SUSP_SEARCH,  8.0f, 6.0f, 52.0f, "search"  },
    /* SK_VOICE   */ { SUSP_DECOY,   4.0f, 8.0f, 80.0f, "voice"   },
    /* SK_MONSTER */ { SUSP_SEARCH,  8.0f, 5.0f, 40.0f, "monster" },
    /* SK_SCAN    */ { SUSP_THROW,   6.0f, 6.0f, 45.0f, "scan"    }
};

static const SoundKindDef* KindDef(SoundKind k) {
    if ((int)k < 0 || (int)k >= SK_COUNT) return &kKindDefs[SK_WALK];
    return &kKindDefs[(int)k];
}

int   SoundKindSusSource(SoundKind k)       { return KindDef(k)->susSource; }
float SoundKindDecay(SoundKind k)           { return KindDef(k)->decay; }
float SoundKindTtl(SoundKind k)             { return KindDef(k)->ttl; }
float SoundKindDefaultStrength(SoundKind k){ return KindDef(k)->defStrength; }
const char* SoundKindName(SoundKind k)      { return KindDef(k)->name; }

/* ==================================================================
 * 同帧去重
 * ------------------------------------------------------------------
 * 一次物理事件（比如破门石砸开锁）可能在多个分支里被结算 ——
 * 落地、撞墙、到期。它们描述的是**同一件事**，不该产生两个线索。
 *
 * 规则：同一个非零 eventId 在**同一帧内**只结算第一次，后面的挡掉。
 * 不同帧、或 eventId == 0 时不去重 —— 玩家连续走两步是两次真实的脚步，
 * 不能因为"长得一样"就合并。
 * ================================================================ */
#define SOUND_DEDUPE_SLOTS 16
static unsigned int s_seen[SOUND_DEDUPE_SLOTS];
static int          s_seenCount = 0;

static int AlreadySeen(unsigned int id) {
    for (int i = 0; i < s_seenCount; i++) {
        if (s_seen[i] == id) return 1;
    }
    return 0;
}
static void Remember(unsigned int id) {
    if (s_seenCount >= SOUND_DEDUPE_SLOTS) {
        /* 满了就整体右移一格丢弃最老的 —— 同帧 16 个以上不同事件极罕见 */
        for (int i = 1; i < SOUND_DEDUPE_SLOTS; i++) s_seen[i - 1] = s_seen[i];
        s_seenCount = SOUND_DEDUPE_SLOTS - 1;
    }
    s_seen[s_seenCount++] = id;
}

void SoundEventTick(void) {
    s_seenCount = 0;
}

/* ==================================================================
 * 统计
 * ================================================================ */
static int   s_totalEmitted = 0;
static int   s_totalDeduped = 0;
static int   s_lastKind     = -1;
static float s_lastStrength = 0.0f;
static float s_lastTtl      = 0.0f;

void SoundEventResetStats(void) {
    s_totalEmitted = 0;
    s_totalDeduped = 0;
    s_lastKind = -1;
    s_lastStrength = 0.0f;
    s_lastTtl = 0.0f;
    s_seenCount = 0;
}
/* Runtime emission/deduplication telemetry; counters change as events are handled. */
int   SoundEventTotalEmitted(void) { return s_totalEmitted; }
int   SoundEventTotalDeduped(void) { return s_totalDeduped; }
int   SoundEventLastKind(void)     { return s_lastKind; }
float SoundEventLastStrength(void) { return s_lastStrength; }
float SoundEventLastTtl(void)      { return s_lastTtl; }

/* ==================================================================
 * 提交
 * ================================================================ */
int SoundEmit(const SoundEvent* ev) {
    if (!ev) return 0;

    /* 非有限值直接拒绝 —— 一个 NaN 坐标会污染整个可疑点表 */
    if (!(ev->x == ev->x) || !(ev->y == ev->y)) return 0;

    /* ① 去重（只在有 eventId 时） */
    if (ev->eventId != 0u) {
        if (AlreadySeen(ev->eventId)) {
            s_totalDeduped++;
            return 0;
        }
    }

    const SoundKindDef* def = KindDef(ev->kind);

    /* ② 数值：(<=0) 用类别默认值，然后夹进合法区间 */
    float st  = (ev->strength  > 0.0f) ? ev->strength  : def->defStrength;
    float dec = (ev->decay     > 0.0f) ? ev->decay     : def->decay;
    float ttl = (ev->ttl       > 0.0f) ? ev->ttl       : def->ttl;
    if (st > 100.0f) st = 100.0f;
    if (st < 0.0f)   st = 0.0f;
    if (dec < 0.01f) dec = 0.01f;
    if (ttl < 0.01f) ttl = 0.01f;
    if (ttl > 60.0f) ttl = 60.0f;

    /* ③ 强度为 0 的事件没有意义（SuspicionAdd 也会直接丢掉） */
    if (st <= 0.0f) return 0;

    /* ④ 写可疑点表 —— **类别来自 kind，不来自半径** */
    SuspicionAdd(ev->x, ev->y, st, dec, ttl, def->susSource);

    if (ev->eventId != 0u) Remember(ev->eventId);

    s_totalEmitted++;
    s_lastKind     = (int)ev->kind;
    s_lastStrength = st;
    s_lastTtl      = ttl;
    return 1;
}

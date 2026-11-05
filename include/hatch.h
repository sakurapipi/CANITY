#ifndef HATCH_H
#define HATCH_H

/* ==================================================================
 * 舱门系统（2026-11-06）
 * ------------------------------------------------------------------
 * 用户定义：「舱门平常是封闭着的，付钱之后舱门可以打开。」
 *
 * 一个房间 = 一扇舱门。舱门就是"主题控制器"：
 *   封闭(SEALED) → 长按 + 门槛满足 → 开门动画(OPENING)
 *   → 动画播完那一帧结算效果 → 已触发(FIRED)
 *
 * 分层（刻意这样切）：
 *   hatch.c   数据 / 状态机 / 动画 / 就近查询   —— 不认识任何玩法全局
 *   game.c    HatchGateCheck / HatchFireEffect  —— 门槛与效果都在那儿，
 *             因为金币/电量/理智/保险丝/家具/容器全在 game.c
 *
 * 这样 hatch.c 可以被单测单独跑，不需要整局状态。
 * ================================================================ */

#define MAX_HATCHES 4          /* 一层最多 4 间主题房 */

/* 状态：**只能由 HatchSetState 改** —— 见 art_direction.h 里
   "同一状态有多个写入点 → 抢写" 的一类教训。 */
enum {
    HATCH_SEALED = 0,   /* 封闭；可交互 */
    HATCH_OPENING,      /* 开门动画中；逻辑冻结、输入被吞 */
    HATCH_OPEN,         /* 门开着 */
    HATCH_FIRED         /* 效果已结算；不再响应 */
};

enum {
    HATCH_BOILER = 0,   /* 锅炉房 · 崩塌 */
    HATCH_STORAGE,      /* 仓储室 · 开仓 */
    HATCH_POWER,        /* 供电室 · 全灯清雾 */
    HATCH_WARD,         /* 病房   · 包扎 */
    HATCH_BRICK,        /* 砖墙   · 拆墙开捷径 */
    HATCH_HELL,         /* 地狱   · 血祭 */
    HATCH_FLESH,        /* 血肉洞窟 · 喂食 */
    HATCH_KIND_COUNT
};

#define HATCH_OPEN_SECONDS 0.45f   /* 开门动画时长（AD-27：动画与生效同一时间轴） */

typedef struct {
    float x, y;
    int   active;
    int   kind;
    int   state;
    int   themeId;
    float animT;        /* 0..HATCH_OPEN_SECONDS */
} Hatch;

extern Hatch hatches[MAX_HATCHES];
extern int   hatchCount;

void HatchReset(void);
int  HatchAdd(float x, float y, int kind, int themeId);
void HatchSetState(int idx, int state);          /* 唯一的状态写入口 */
void HatchTick(float dt);                        /* 推进动画；播完调用 HatchFireEffect */
int  HatchNearest(float px, float py, float maxD2);
float HatchOpen01(int idx);                      /* 0 = 全闭，1 = 全开 */
int  HatchKindForTheme(int themeId);

/* ---- 由 game.c 提供（门槛与效果都在那边）---- */
int  HatchGateCheck(int idx, char* why, int cap);   /* 1 = 可以开 */
void HatchFireEffect(int idx);
const char* HatchTitle(int idx);

#endif /* HATCH_H */

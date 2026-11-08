/* src/domain/training/training_stations.c —— 七站式教学（第 11 轮） */
#include "common.h"
#include "training_stations.h"

typedef struct {
    int x, y;              /* 地格坐标 */
    const char* en;
    const char* zh;
} TrainStation;

/* 走廊在第 MAP_HEIGHT/2 行，从 x=2 到 MAP_WIDTH-3。
   七站等距铺开，间距约 4 格 —— 走两步就到下一站，不会迷路。 */
static const TrainStation kStations[TRAIN_STATION_COUNT] = {
    {  4, MAP_HEIGHT / 2, "WALK: WASD, look with the mouse",
                          "走路：WASD，鼠标看方向" },
    {  8, MAP_HEIGHT / 2, "LIGHT: F toggles. The light beast only sees a lit player",
                          "手电：F 开关。追光鬼只看得见开灯的你" },
    { 12, MAP_HEIGHT / 2, "NOISE: G throws a decoy. Sound hunters walk to the last noise",
                          "声音：G 扔诱饵。听声鬼走向最后听到的噪音" },
    { 16, MAP_HEIGHT / 2, "SEARCH: hold E on a crate (solve the lock first)",
                          "搜刮：对着箱子按住 E（先解密码锁）" },
    { 20, MAP_HEIGHT / 2, "HIDE: E next to a locker. It reaches the door but never opens it",
                          "躲藏：柜子旁按 E。它只会摸到柜门，不会拉开" },
    { 24, MAP_HEIGHT / 2, "FUSES: pick up fuses, then open the breaker",
                          "配电：捡保险丝，再去配电箱开门" },
    { 28, MAP_HEIGHT / 2, "EXIT: walk into the open door to leave",
                          "撤离：门开了走进去就离开" },
};

static int s_current = -1;

void TrainingStationsReset(void) { s_current = -1; }

int TrainingStationX(int idx) {
    if (idx < 0 || idx >= TRAIN_STATION_COUNT) return -1;
    return kStations[idx].x;
}

int TrainingStationY(int idx) {
    if (idx < 0 || idx >= TRAIN_STATION_COUNT) return -1;
    return kStations[idx].y;
}

const char* TrainingStationText(int idx) {
    if (idx < 0 || idx >= TRAIN_STATION_COUNT) return "";
    return kStations[idx].zh;      /* 中文优先；英文见表里的 en 字段 */
}

int TrainingStationsTick(float px, float py) {
    int i;
    for (i = 0; i < TRAIN_STATION_COUNT; i++) {
        float dx = px - ((float)kStations[i].x + 0.5f);
        float dy = py - ((float)kStations[i].y + 0.5f);
        if (dx * dx + dy * dy <= 1.5f * 1.5f) {
            if (s_current != i) {
                s_current = i;
                return i;          /* 只播一次：离开再回来才会重播 */
            }
            return -1;
        }
    }
    return -1;                     /* 站在两站之间：不重置，避免来回刷 */
}

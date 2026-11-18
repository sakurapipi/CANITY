/* src/domain/training/training_stations.c —— 七站式教学（第 11 轮） */
#include "common.h"
#include "training_stations.h"
#include "lang.h"

typedef struct {
    int x, y;              /* 地格坐标 */
    const char* en;
    const char* zh;
} TrainStation;

static const TrainStation kStations[TRAIN_STATION_COUNT] = {
    {18, 18, "CENTRAL HUB | North: locks and target range. South: recovery and extraction.",
             "中央大厅｜北侧是锁具和靶场，南侧是休整区和撤离区。"},
    {27, 9, "RANGE | Aim at the dummy. Try a light or fully charged throw.",
            "靶场｜瞄准训练靶，对比轻投与满蓄力。"},
    {8, 8, "LOCK LAB | Six locks. Solve one by hand, then try a charged stone on another.",
           "锁具室｜六种锁具。先亲手解锁，再试试用蓄力投石砸开另一把。"},
    {8, 26, "COVER BAY | Enter a locker, then open the supply shop or practice calibration.",
            "藏身区｜进入藏身点后，可打开补给商店或练习校准。"},
    {12, 27, "RECOVERY | Watch light and sanity recover while you stay in cover.",
             "休整区｜藏好后观察手电和理智的恢复。"},
    {25, 26, "BREAKER | Bring the fuse from the hub to power the exit.",
             "配电区｜把中央大厅箱内的保险丝带来，接通出口。"},
    {30, 29, "EXIT DRILL | Reach the powered door. Practice gold stays in the arena.",
             "撤离演练｜走到通电的门旁。练习金币留在训练场。"},
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
    return Tr(kStations[idx].en, kStations[idx].zh);
}

int TrainingStationAt(float x, float y) {
    for (int i = 0; i < TRAIN_STATION_COUNT; i++)
        if (Dist2(x, y, kStations[i].x + .5f, kStations[i].y + .5f) < 2.25f)
            return i;
    return -1;
}

int TrainingStationsTick(float px, float py) {
    int at = TrainingStationAt(px, py);
    if (at < 0 || at == s_current) return -1;
    s_current = at;
    return at;
}

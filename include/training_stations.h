/* include/training_stations.h —— 训练营七站（第 11 轮，2026-11-08）
 *
 * 玩家反馈：「新手教程不够完整」「要怎么躲」「不知道哪个怪是哪个」。
 * 训练营原来只是"一条走廊 + 一只标本怪"，没有分步引导。
 *
 * 这七个站点按顺序教：移动 → 手电 → 声音 → 搜刮 → 躲藏 → 配电 → 撤离。
 * 走进 1.5 格触发一次提示，不打断操作、不需要按键。
 */
#ifndef CANITY_TRAINING_STATIONS_H
#define CANITY_TRAINING_STATIONS_H

#define TRAIN_STATION_COUNT 7

/* 每帧调用（只在 trainingMode 时）。返回"这一步刚被触发"的站点下标，否则 -1。 */
int TrainingStationsTick(float px, float py);

/* 站点坐标（测试与地图标记用） */
int TrainingStationX(int idx);
int TrainingStationY(int idx);

/* 站点文案（中英） */
const char* TrainingStationText(int idx);

/* 重置（每局开始） */
void TrainingStationsReset(void);

#endif

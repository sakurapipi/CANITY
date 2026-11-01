/*
 * Canity — 单局遥测（v6）
 *
 * 每局结束写一行 CSV，用来**实测**经济，而不是靠推算。
 */
#ifndef CANITY_TELEMETRY_H
#define CANITY_TELEMETRY_H

/* 一局结束时调用
   level    结束时的层数
   banked   进银行的金币
   carried  结束时携带的金币
   survived 1 = 撤离 / 0 = 被抓或理智耗尽
   seconds  本局时长
   caught   本局被抓次数 */
void TelemetryRunEnd(int level, int banked, int carried,
                     int survived, float seconds, int caught);

#endif /* CANITY_TELEMETRY_H */

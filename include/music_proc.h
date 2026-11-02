#ifndef CANITY_MUSIC_PROC_H
#define CANITY_MUSIC_PROC_H

/*
 * 内置程序化配乐（零素材）。
 *
 * 主线程调用 SetScene / SetGain 下指令，音频线程调用 Mix 取样本。
 * 合成状态只属于音频线程；两个入口用 Windows 原子操作交接，不加锁、
 * 不阻塞，也不会和 audio.c 的混音锁产生嵌套。
 *
 * 场景编号沿用 MusicScene 的整数值；胜利/失败会被合成器映射为静音，
 * 只保留音效，避免结算界面的音乐压过提示音。
 */
#define MUSIC_PROC_RATE 22050
#define MUSIC_PROC_SILENT (-1)

void MusicProcSetScene(int scene);
void MusicProcSetGain(float gain);
void MusicProcMix(float* left, float* right, int frames);

#endif /* CANITY_MUSIC_PROC_H */

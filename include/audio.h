/*
 * Canity — 音频层：程序化合成 + 软件混音（不依赖任何素材文件）
 *
 * 全部音频状态都在 audio.c 内部，对外只暴露三个函数。
 */
#ifndef CANITY_AUDIO_H
#define CANITY_AUDIO_H

#include "common.h"

enum { SFX_STEP = 0, SFX_HEART, SFX_PICKUP, SFX_POWER, SFX_GATE, SFX_CRANK,
       SFX_SCREAM, SFX_GLASS, SFX_SEARCH, SFX_THROW, SFX_COUNT };

void PlaySfx(int id, float vol);
void InitAudio(void);
void ShutdownAudio(void);

#endif /* CANITY_AUDIO_H */

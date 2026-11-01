/*
 * Canity — 音频层：程序化合成 + 软件混音（不依赖任何素材文件）
 *
 * 全部音频状态都在 audio.c 内部，对外只暴露三个函数。
 */
#ifndef CANITY_AUDIO_H
#define CANITY_AUDIO_H

#include "common.h"

enum { SFX_STEP = 0, SFX_HEART, SFX_PICKUP, SFX_POWER, SFX_GATE, SFX_CRANK,
       SFX_SCREAM, SFX_GLASS, SFX_SEARCH, SFX_THROW,
       /* v3.3 投掷物重做配套 */
       SFX_GLUE,     /* 胶水落地：黏稠的「噗叽」 */
       SFX_ECHO,     /* 回声弹：短促「叮」+ 尾音回响 */
       SFX_DECOY,    /* 诱饵玩偶：木头/金属的「嘎吱」 */
       SFX_WARDEN,   /* v2.4.89 守门人：极低频压迫 */
       SFX_SCAVENGER,/* v2.4.89 清道夫：刺耳刮擦（拆东西）*/
       SFX_SCAV_STEP,/* v2.4.92 清道夫：走路的高速摩擦 */
       SFX_COUNT };

void PlaySfx(int id, float vol);
void InitAudio(void);
void ShutdownAudio(void);

#endif /* CANITY_AUDIO_H */

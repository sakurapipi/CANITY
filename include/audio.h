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
       SFX_IMPACT, SFX_HEARTBEAT, SFX_METAL_SCRAPE, SFX_DRIP, SFX_STEAM, SFX_HUM,
       SFX_WRIGGLE, SFX_BONE, SFX_GLASS_CRACK, SFX_HINGE, SFX_BREATH,
       SFX_COUNT };
void AudioBuildAtmosphere(short* output,int frames,int kind,int rate);

void PlaySfx(int id, float vol);
float AudioOutputLatencySeconds(void);

/* ==================================================================
 * 2026-11-06（v2.4-beta B02）：**世界音效**
 * ------------------------------------------------------------------
 * PlaySfx 保持原样（菜单 / 成就 / 提示，居中等功率，行为不变）。
 * 需要「听得出方向」的声音走这里：
 *   x,y      世界坐标
 *   maxDist  超过这个距离完全听不见（内部平方衰减）
 *   priority 越大越重要；8 路满员时只抢占优先级更低的，
 *            抢不到就丢弃 —— 破门石的冲击声不该被路过的脚步挤掉。
 * ================================================================ */
void AudioPlayWorld(int id, float x, float y, float vol, float maxDist, int priority);

/* 每帧由游戏线程喂一次玩家位姿；转身时声像才会跟着平滑移动。 */
void AudioUpdateListener(float px, float py, float dirX, float dirY);

/* B02：声像 + 距离衰减的纯计算（不读全局、不加锁）。
   存在只为可单测 —— 试听时判"手感"，这里判"数学"。 */
void AudioComputeGains(float lisX, float lisY, float lisDirX, float lisDirY,
                       float srcX, float srcY, float maxDist,
                       float* outL, float* outR);

void InitAudio(void);
void ShutdownAudio(void);

void AudioMaintain(void);
#endif /* CANITY_AUDIO_H */

#ifndef CANITY_MUSIC_PCM_H
#define CANITY_MUSIC_PCM_H
/* Immutable PCM is loaded before InitAudio and retained until process exit. */
int MusicPcmLoad(int scene, const char* path);
int MusicPcmSelect(int scene);
void MusicPcmGain(float gain);
void MusicPcmMood(float sanityRatio);
void MusicPcmSeek(int sample44100);
void MusicPcmMix(float* left, float* right, int count);
void MusicBusProcess(float* left,float* right,int count);
int MusicPcmAvailable(int scene);
void MusicPcmShutdown(void);
#endif

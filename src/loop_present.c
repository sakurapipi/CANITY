/* loop_present.c —— 从 game.c 拆出（extract_function.py） */
#include "common.h"
#include "present_api.h"
#include "player_api.h"
#include "world_api.h"
#include "state.h"
#include "entity.h"

void AddHitFlash(float amount) {
    hitFlash += amount;
    if (hitFlash > 1.0f) hitFlash = 1.0f;
}


static float shakeTime = 0.0f, shakeDur = 1.0f, shakeMag = 0.0f;

void AddShake(float seconds, float mag) {
    if (seconds > shakeTime) { shakeTime = seconds; shakeDur = seconds; shakeMag = mag; }
    else if (mag > shakeMag) shakeMag = mag;
}

void PresentTickFeedback(float dt) {
    if (hitFlash > 0.0f) { hitFlash -= dt * 3.2f; if (hitFlash < 0.0f) hitFlash = 0.0f; }
    if (muzzleFlash > 0.0f) { muzzleFlash -= dt * 9.0f; if (muzzleFlash < 0.0f) muzzleFlash = 0.0f; }
    if (shakeTime > 0.0f) {
        shakeTime -= dt;
        if (shakeTime <= 0.0f) { shakeTime = 0.0f; shakeMag = 0.0f; }
        float k = (shakeDur > 0.0f) ? (shakeTime / shakeDur) : 0.0f;
        float amp = shakeMag * k * k;              /* 尾巴快速收敛 */
        g_shakeOffX = ((float)(rand() % 2001) - 1000.0f) / 1000.0f * amp;
        g_shakeOffY = ((float)(rand() % 2001) - 1000.0f) / 1000.0f * amp * 0.8f;
    } else {
        g_shakeOffX = 0.0f;
        g_shakeOffY = 0.0f;
    }
}

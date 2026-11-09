/* loop_present.c —— 从 game.c 拆出（extract_function.py） */
#include "common.h"
#include "present_api.h"
#include "present_api.h"
#include "player_api.h"
#include "world_api.h"
#include "state.h"
#include "entity.h"

void AddHitFlash(float amount) {
    hitFlash += amount;
    if (hitFlash > 1.0f) hitFlash = 1.0f;
}

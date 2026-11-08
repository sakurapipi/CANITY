#include "extras.h"
#include "common.h"
#include "entity.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "audio.h"
#include "skills.h"

Extra extras[EXTRA_MAX];

void ExtrasReset(void) {
    /* v7：三只游荡怪（影子/盲螨/回响）已删除 —— 这里只清空槽位 */
    for (int i = 0; i < EXTRA_MAX; i++) {
        extras[i].active = 0;
        extras[i].kind = -1;
        extras[i].timer = 0.0f;
        extras[i].cool = 0;
        extras[i].hasTarget = 0;
    }
}

/* v2.2：补一只游荡怪（EV_MIRROR 用）—— v7 起不生成任何东西 */
void ExtrasSpawnOne(int idx) {
    (void)idx;
}

/* v7：不再生成游荡怪 */
void ExtrasSpawnForLevel(int level) {
    (void)level;
    ExtrasReset();
}

/* v7：没有游荡怪了，这里只做空转（保留接口，render/EV 不用改） */
void UpdateExtras(float dt) {
    (void)dt;
}

int ExtrasCount(void) { return 0; }

const Extra* ExtraAt(int i) {
    if (i < 0 || i >= EXTRA_MAX) return NULL;
    return &extras[i];
}

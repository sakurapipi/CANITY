/* Meta mechanisms and run-only tuning meet here. No new saved IDs or timers:
 * hunter resistance and interrupted phases already belong to the evo chunk. */
#include "common.h"
#include "progression.h"
#include "skills.h"
#include "bench.h"
#include "tactical_tools.h"
#include "monster_evolution.h"
#include "entity.h"
#include "world_api.h"

float ProgressionToolValue(int stat) {
    static const float base[] = {TOOL_ACOUSTIC_RADIUS, TOOL_ACOUSTIC_SECONDS, TOOL_PRISM_BLIND_SECONDS,
                                 8, 1, 1.5f, 3, 1.8f};
    static const int bonus[] = {BENCH_DAMP_RADIUS,  BENCH_DAMP_TIME,  BENCH_BLIND_TIME,
                                BENCH_DUST_TIME,    BENCH_JAM_TIME,   BENCH_SOLVENT_TIME,
                                BENCH_TETHER_RANGE, BENCH_TRACE_RANGE};
    if (stat < 0 || stat >= 8)
        return 0;
    float value = base[stat] + BenchTacticalBonus(bonus[stat]);
    if (stat == PG_DUST_TIME && RunSkillOwned(IR_DUSTCOAT))
        value += 4;
    return value;
}
int ProgressionAcousticLink(float *ax, float *ay, float *bx, float *by) {
    int rank = SkillClampRank(SK_QUIET, skillRank[SK_QUIET]);
    if (rank <= 0 || tactical.acoustic[0].ttl <= 0 || tactical.acoustic[1].ttl <= 0)
        return 0;
    ToolZone *a = &tactical.acoustic[0], *b = &tactical.acoustic[1];
    float reach = 4 + rank * 2.0f, d = Dist2(a->x, a->y, b->x, b->y);
    if (d < .01f || d > reach * reach || !HasLOS(a->x, a->y, b->x, b->y))
        return 0;
    if (ax)
        *ax = a->x;
    if (ay)
        *ay = a->y;
    if (bx)
        *bx = b->x;
    if (by)
        *by = b->y;
    return 1;
}
int ProgressionInAcousticLink(float x, float y) {
    float ax, ay, bx, by;
    if (!ProgressionAcousticLink(&ax, &ay, &bx, &by))
        return 0;
    float dx = bx - ax, dy = by - ay, t = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy);
    if (t < 0 || t > 1)
        return 0;
    float px = ax + t * dx, py = ay + t * dy;
    return Dist2(x, y, px, py) <= .25f && !Blocked(x, y) && HasLOS(x, y, px, py);
}
void ProgressionWrenchContact(Monster *target) {
    int rank = SkillClampRank(SK_STILL, skillRank[SK_STILL]);
    if (!rank)
        return;
    for (int i = 0; i < MON_HUNTER_MAX; i++) {
        if (target != &monHunters[i])
            continue;
        EvoActor *a = &evolution.actors[i];
        a->phase = 0;
        a->aux = 0;
        a->signal = 0;
        a->flags |= 3u;
        a->resist = fmaxf(a->resist, 2 + rank * 2.0f);
        EvolutionRelease(i);
        break;
    }
}
void ProgressionTetherLanding(float *x, float *y) {
    if (!x || !y)
        return;
    int rank = SkillClampRank(SK_GOLDSCENT, skillRank[SK_GOLDSCENT]);
    if (rank <= 0)
        return;
    float fraction = rank / 3.0f, nx = *x + (posX - *x) * fraction,
          ny = *y + (posY - *y) * fraction;
    if (!Blocked(nx, ny) && HasLOS(*x, *y, nx, ny)) {
        *x = nx;
        *y = ny;
    }
}

int SkillPremonition(void) { return skillRank[SK_GHOSTSTEP] >= 3; }
int SkillInnerEye(void) { return skillRank[SK_CARTO] >= 2; }

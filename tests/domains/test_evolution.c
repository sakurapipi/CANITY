#include "progression.h"
#include "bench.h"
#include "upgrade_run.h"
#include "economy.h"
/* Authored for 2.4.9 RC1. Execution is intentionally left to the maintainer. */
#include "../test_common.h"
#include "threat_tier.h"
#include "monster_evolution.h"
#include "tactical_tools.h"
#include "evolution_save.h"
#include "save_format.h"
static void tier_boundaries(void) {
    CHECK(ThreatTierFromPressure(-1) == 0, "negative pressure is safe T0");
    CHECK(ThreatTierFromPressure(NAN) == 0 && ThreatTierFromPressure(INFINITY) == 0,
          "nonfinite pressure cannot create a high-tier attack");
    CHECK(ThreatTierFromPressure(3.999f) == 0 && ThreatTierFromPressure(4) == 1,
          "first exact boundary is four");
    CHECK(ThreatTierFromPressure(7.999f) == 1 && ThreatTierFromPressure(8) == 2,
          "second exact boundary is eight");
    CHECK(ThreatTierFromPressure(12.999f) == 2 && ThreatTierFromPressure(13) == 3,
          "last exact boundary is thirteen");
    trainingMode = 0;
    ThreatTierRestore(1, 2, 8);
    int plan = bankRiskPlan;
    bankRiskPlan = (plan + 1) % 4;
    CHECK(CurrentRunThreatTier() == 2, "moving the bank preview never changes the current floor");
    bankRiskPlan = plan;
    int carryPlan = bankCarryPct;
    bankCarryPct = 0;
    for (int selected = 0; selected < 4; selected++) {
        bankRiskPlan = selected;
        CHECK(PreviewThreatTier() == selected, "bank exposes four tiers even with no carried gold");
    }
    bankCarryPct = carryPlan;
    bankRiskPlan = plan;
    trainingMode = 1;
    CHECK(CurrentRunThreatTier() == 0, "practice defaults to original behavior");
    trainingMode = 0;
}
static void tool_commit_guards(void) {
    TacticalResetRun();
    EvolutionResetFloor();
    currentLevel = 1;
    currentState = STATE_PLAYING;
    hiddenIn = -1;
    posX = 3.5f;
    posY = 3.5f;
    dirX = 1;
    dirY = 0;
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            worldMap[y][x] = (x == 0 || y == 0 || x == MAP_WIDTH - 1 || y == MAP_HEIGHT - 1);
    tactical.inventory[TOOL_ACOUSTIC] = 2;
    for (int i = 0; i < MAX_SHOTS; i++)
        shot[i].active = 1;
    CHECK(!TacticalTryUse(7 + TOOL_ACOUSTIC, .5f) && tactical.inventory[TOOL_ACOUSTIC] == 2,
          "full flight pool does not consume a capsule");
    for (int i = 0; i < MAX_SHOTS; i++)
        shot[i].active = 0;
    CHECK(TacticalTryUse(7 + TOOL_ACOUSTIC, .5f) && tactical.inventory[TOOL_ACOUSTIC] == 1,
          "one allocated projectile commits exactly one inventory unit");
    tactical.inventory[TOOL_TRACE] = 1;
    CHECK(!TacticalTryUse(7 + TOOL_TRACE, .5f) && tactical.inventory[TOOL_TRACE] == 1,
          "no old trace means no powder payment");
    tactical.inventory[TOOL_SOLVENT] = 1;
    ControlReset();
    CHECK(!TacticalEmergencySolvent() && tactical.inventory[TOOL_SOLVENT] == 1,
          "an ended control cannot consume emergency solvent");
    tactical.acoustic[0] = (ToolZone){posX, posY, 8, TOOL_ACOUSTIC};
    CHECK(fabsf(TacticalFootstepMul(posX, posY, 1) - .10f) < .001f,
          "capsule applies only its declared footstep reduction");
    CHECK(fabsf(TacticalFootstepMul(posX, posY, .05f) - 1) < .001f,
          "Hush wins; capsule does not multiply its reduction");
    int gold = carryGold;
    evolution.zones[2] = (EvoZone){3.5f, 3.5f, .5f, 2.5f, EVO_ZONE_COINS};
    EvolutionTick(3);
    CHECK(carryGold == gold && evolution.zones[2].ttl == 0,
          "fake coins expire without touching money");
    for (int i = 0; i < MAX_SHOTS; i++)
        shot[i].active = 0;
}
static void wire_boundaries(void) {
    unsigned char e[EVOLUTION_WIRE_BYTES], t[TACTICAL_WIRE_BYTES];
    EvolutionResetFloor();
    TacticalResetRun();
    EvolutionSnapshot a = {1, 3, 13, evolution}, b;
    Shot flights[MAX_SHOTS] = {{0}}, loaded[MAX_SHOTS];
    TacticalState inventory;
    a.state.actors[EVO_MIMIC].flags = 1;
    a.state.actors[EVO_WARDEN].flags = 2;
    CHECK(EvolutionWireEncode(&a, e, sizeof(e)) == sizeof(e),
          "evolution wire has an explicit byte count");
    CHECK(EvolutionWireDecode(&b, e, sizeof(e)) && b.tier == 3 &&
              b.state.actors[EVO_MIMIC].flags == 1,
          "tier and one-use markers round trip without C padding");
    CHECK(!EvolutionWireDecode(&b, e, sizeof(e) - 1), "truncated evolution chunk is rejected");
    tactical.acoustic[0] = (ToolZone){3.5f, 3.5f, TOOL_ACOUSTIC_SECONDS, TOOL_ACOUSTIC};
    tactical.inventory[TOOL_REVEAL] = 3;
    tactical.quick[3] = 13;
    CHECK(TacticalWireEncode(&tactical, flights, t, sizeof(t)) == sizeof(t),
          "tactical inventory has a separate versioned chunk");
    CHECK(TacticalWireDecode(&inventory, loaded, t, sizeof(t)) &&
              inventory.inventory[TOOL_REVEAL] == 3 && inventory.quick[3] == 13,
          "nonempty reserves and stable fourth binding round trip");
    t[8] = 255;
    CHECK(!TacticalWireDecode(&inventory, loaded, t, sizeof(t)),
          "out-of-range inventory cannot alias another tool");
    CHECK(EvolutionChunkValid(SC_EVOLUTION_V1, 2, e, sizeof(e)),
          "a future schema remains opaque for byte preservation");
}
static void sound_sharing_is_not_player_tracking(void) {
    EvolutionResetFloor();
    SuspicionReset();
    ThreatTierRestore(1, 3, 13);
    trainingMode = 0;
    for (int i = 0; i < MON_HUNTER_MAX; i++) {
        memset(&monHunters[i], 0, sizeof(Monster));
        monHunters[i].active = 1;
        monHunters[i].x = 8.5f + i;
        monHunters[i].y = 8.5f;
    }
    posX = 24.5f;
    posY = 24.5f;
    SuspicionAdd(8.5f, 8.5f, 75, 1, 12, SUSP_DECOY);
    EvolutionHunterHeard(0, 0);
    (void)EvolutionHunterOverride(&monHunters[0], .6f);
    (void)EvolutionHunterOverride(&monHunters[0], .6f);
    CHECK(monHunters[1].state == 2 && monHunters[1].tx == 8.5f && monHunters[1].ty == 8.5f,
          "shared investigation uses the causal sound point, never the player's current position");
    CHECK(monHunters[2].state == 0, "one sound is shared with at most one peer");
    EvolutionHunterHeard(1, 0);
    CHECK(evolution.actors[1].aux == 0, "a receiver cannot rebroadcast the same source event");
}
static void light_memory_and_cancellation(void) {
    EvolutionResetFloor();
    ThreatTierRestore(1, 1, 4);
    hiddenIn = -1;
    playerInSafe = 0;
    Monster *actor = &lightBeasts[0];
    memset(actor, 0, sizeof(*actor));
    actor->active = 1;
    actor->x = 6.5f;
    actor->y = 4.5f;
    int has = 1;
    float x = 10.5f, y = 4.5f;
    (void)EvolutionLightUpdate(actor, &has, 1, &x, &y, .1f);
    has = 0;
    x = y = 0;
    (void)EvolutionLightUpdate(actor, &has, 0, &x, &y, .2f);
    CHECK(has && x == 10.5f && y == 4.5f, "afterimage follows the last observed light point");
    has = 0;
    (void)EvolutionLightUpdate(actor, &has, 0, &x, &y, 1);
    CHECK(!has, "afterimage expires and never becomes continuous invisible tracking");
    EvolutionResetFloor();
    ThreatTierRestore(1, 3, 13);
    for (int i = 0; i < 20; i++) {
        has = 1;
        x = 10.5f;
        y = 4.5f;
        (void)EvolutionLightUpdate(actor, &has, 1, &x, &y, .1f);
    }
    CHECK(evolution.actors[EVO_LIGHT].phase == 1,
          "continuous real illumination opens a warned dash, not an instant hit");
    hiddenIn = 0;
    has = 0;
    (void)EvolutionLightUpdate(actor, &has, 0, &x, &y, .1f);
    CHECK(!evolution.actors[EVO_LIGHT].phase,
          "legal hiding cancels a warned or active light attack");
    hiddenIn = -1;
}

static void progression_mechanisms(void) {
    for (int i = 0; i < SKILL_COUNT; i++)
        skillRank[i] = 0;
    benchOwned = 0;
    RunSkillsReset();
    RunUpgReset();
    TacticalResetRun();
    EvolutionResetFloor();
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            worldMap[y][x] = (x == 0 || y == 0 || x == MAP_WIDTH - 1 || y == MAP_HEIGHT - 1);
    tactical.acoustic[0] = (ToolZone){5.5f, 5.5f, 12, TOOL_ACOUSTIC};
    tactical.acoustic[1] = (ToolZone){15.5f, 5.5f, 12, TOOL_ACOUSTIC};
    skillRank[SK_QUIET] = 2;
    CHECK(!ProgressionInAcousticLink(10.5f, 5.5f), "rank two does not bridge a ten-cell gap");
    skillRank[SK_QUIET] = 3;
    CHECK(ProgressionInAcousticLink(10.5f, 5.5f),
          "rank three links two fields across the uncovered gap");
    CHECK(!ProgressionInAcousticLink(10.5f, 6.3f),
          "a linked corridor is not an unlimited silence plane");
    worldMap[5][10] = 1;
    CHECK(!ProgressionInAcousticLink(10.5f, 5.5f), "solid walls break the acoustic link");
    worldMap[5][10] = 0;
    tactical.acoustic[1].ttl = 0;
    CHECK(!ProgressionInAcousticLink(10.5f, 5.5f), "an expired endpoint cannot sustain the link");
    benchOwned = (UINT64_C(1) << BENCH_ACOUSTIC_VOLUME) | (UINT64_C(1) << BENCH_ACOUSTIC_RESERVE) |
                 (UINT64_C(1) << BENCH_PRISM_COATING) | (UINT64_C(1) << BENCH_REVEAL_COATING) |
                 (UINT64_C(1) << BENCH_PULSE_CAPACITOR) |
                 (UINT64_C(1) << BENCH_SOLVENT_CONCENTRATE) | (UINT64_C(1) << BENCH_MAGNET_REEL) |
                 (UINT64_C(1) << BENCH_TRACE_REACH);
    CHECK(ProgressionToolValue(PG_DAMP_RADIUS) == 5 && ProgressionToolValue(PG_DAMP_TIME) == 16,
          "local equipment increases real damping area and lifetime");
    CHECK(ProgressionToolValue(PG_BLIND_TIME) == 5 && ProgressionToolValue(PG_SOLVENT_TIME) == 2,
          "local optics and solvent change their actual effect parameters");
    runSkillMask = 1u << IR_DUSTCOAT;
    CHECK(ProgressionToolValue(PG_DUST_TIME) == 16,
          "run-card dust and local coating combine with a bounded duration");
    unsigned char bytes[TACTICAL_WIRE_BYTES];
    Shot empty[MAX_SHOTS] = {{0}}, restored[MAX_SHOTS];
    TacticalState decoded;
    tactical.acoustic[0].ttl = 16;
    tactical.dust[0] = 16;
    CHECK(TacticalWireEncode(&tactical, empty, bytes, sizeof(bytes)) == sizeof(bytes) &&
              TacticalWireDecode(&decoded, restored, bytes, sizeof(bytes)) && decoded.dust[0] == 16,
          "upgraded tool durations survive the unchanged independent save layout");
    benchOwned = (UINT64_C(1) << BENCH_RETURN_SPRING) | (UINT64_C(1) << BENCH_TOOL_COOLING);
    runSkillMask = 1u << IR_TOOLCOOLING;
    RunUpgSetLevel(4, 3);
    CHECK(RunUpgWrenchCooldown() == 5,
          "all cooldown modifiers respect the final five-second floor");
    benchOwned = 0;
    RunSkillsReset();
    RunUpgReset();
    skillRank[SK_STILL] = 2;
    monHunters[0].state = 1;
    evolution.actors[0].phase = 1;
    evolution.actors[0].aux = 1;
    ProgressionWrenchContact(&monHunters[0]);
    CHECK(evolution.actors[0].phase == 0 && evolution.actors[0].aux == 0 &&
              evolution.actors[0].resist == 6 && monHunters[0].state == 1,
          "signal break cancels evolution without erasing the original pursuit");
    posX = 5.5f;
    posY = 5.5f;
    skillRank[SK_GOLDSCENT] = 3;
    float bagX = 8.5f, bagY = 5.5f;
    ProgressionTetherLanding(&bagX, &bagY);
    CHECK(bagX == posX && bagY == posY, "retrieval changes the bag landing point, not its amount");
    EvolutionResetFloor();
    TacticalResetRun();
    skillRank[SK_BREAKER] = 2;
    hiddenIn = -1;
    playerInSafe = 0;
    memset(lightBeasts, 0, sizeof(lightBeasts));
    lightBeasts[0].active = 1;
    lightBeasts[0].x = 7.5f;
    lightBeasts[0].y = 5.5f;
    TacticalImpact(TOOL_SHOT_FIRST + TOOL_PRISM, 7.5f, 5.5f);
    EvolutionTick(4.1f);
    int has = 1;
    float tx = 20.5f, ty = 5.5f;
    EvolutionLightUpdate(&lightBeasts[0], &has, 1, &tx, &ty, .1f);
    CHECK(has && tx == 7.5f && ty == 5.5f,
          "polarized guidance investigates the hit point after blindness");
    for (int i = 0; i < SKILL_COUNT; i++)
        skillRank[i] = 0;
    SkillInitOnce();
    benchOwned = 0;
    RunSkillsReset();
    RunUpgReset();
}

void TestSuite_evolution(void) {
    tier_boundaries();
    tool_commit_guards();
    wire_boundaries();
    sound_sharing_is_not_player_tracking();
    light_memory_and_cancellation();
    progression_mechanisms();
    TacticalResetRun();
    EvolutionResetFloor();
    ThreatTierRestore(1, 0, 0);
}

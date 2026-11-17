/* Written for 2.4.8-RC; execution belongs to the next acceptance run. */
#include "../test_common.h"

void TestSuite_warden(void) {
    int oldLevel = currentLevel, oldOpen = breakerOpen;
    float patrol = WardenSpeedNow(), chase = WardenChaseSpeedNow();
    CHECK(isfinite(patrol) && patrol > 0, "warden patrol speed is finite and positive");
    CHECK(isfinite(chase) && chase > patrol, "warden chase is faster than patrol");
    /* Current design has fixed speeds, not a floor-dependent speed curve. */
    for (int level = 1; level <= 100; level++) {
        currentLevel = level;
        CHECK(WardenSpeedNow() == patrol && WardenChaseSpeedNow() == chase,
              "floor changes do not silently scale fixed warden speeds");
    }
    WardenReset();
    CHECK(!WardenActive() && !WardenWatching() && !WardenBlocking(),
          "reset removes active, watching and blocking state together");
    breakerOpen = 0;
    WardenSpawnIfPowered();
    CHECK(!WardenActive(), "warden never spawns before breaker power");
    WardenReset();
    currentLevel = oldLevel; breakerOpen = oldOpen;
}

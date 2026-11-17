/* Room furniture ownership and placement rollback, linked to the real module. */
#include "../test_common.h"

void TestSuite_map_rooms(void) {
    RoomProp previous[MAX_ROOM_PROPS];
    int previousCount = roomPropCount;
    memcpy(previous, roomProps, sizeof(previous));
    RoomPropReset();
    CHECK(roomPropCount == 0, "room reset clears count");
    CHECK(RoomPropAdd(5.5f, 6.5f, ROOMPROP_SHELF), "first shelf is placed");
    CHECK(RoomPropAt(5.5f, 6.5f) == 0 && !PropBlocksAt(5, 6),
          "decorative shelf remains visible but does not block any traversal system");
    CHECK(roomProps[0].searchLeft == 3 && roomProps[0].searched == 0,
          "new shelf starts with its search budget");
    RoomPropRemoveLast();
    CHECK(roomPropCount == 0 && !roomProps[0].active && !roomProps[0].blocks &&
          roomProps[0].height == 0 && roomProps[0].searchLeft == 0,
          "rollback clears all furniture state, not only its coordinates");
    CHECK(RoomPropAt(5.5f, 6.5f) == -1 && !PropBlocksAt(5, 6),
          "rolled-back furniture cannot remain visible or blocking");
    for (int i = 0; i < MAX_ROOM_PROPS; i++)
        CHECK(RoomPropAdd(4.5f + i, 8.5f, ROOMPROP_CONSOLE),
              "all furniture capacity slots are usable");
    CHECK(!RoomPropAdd(25.5f, 8.5f, ROOMPROP_SHELF) && roomPropCount == MAX_ROOM_PROPS,
          "capacity overflow does not append or corrupt the table");
    RoomPropReset();
    CHECK(!roomProps[MAX_ROOM_PROPS-1].blocks && !roomProps[MAX_ROOM_PROPS-1].active,
          "reset clears even the final occupied record");
    memcpy(roomProps, previous, sizeof(previous)); roomPropCount = previousCount;
}

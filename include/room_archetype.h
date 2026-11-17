#ifndef CANITY_ROOM_ARCHETYPE_H
#define CANITY_ROOM_ARCHETYPE_H
#include "room_template.h"
#define ROOM_LAYOUT_LEGACY 1
#define ROOM_LAYOUT_COMPACT 2
#define ROOM_LAYOUT_CURRENT 3
#define ROOM_ARCHETYPE_COUNT 7
#define ROOM_LAYOUT_VARIANTS 2
/* Append-only prop namespaces; old snapshot IDs 0..9 keep their meaning. */
#define ROOMPROP_LANDMARK_FIRST 32
#define ROOMPROP_CONTROL_FIRST 48
#define ROOMPROP_CONTROL_END (ROOMPROP_CONTROL_FIRST + ROOM_ARCHETYPE_COUNT * 2)
typedef struct {
    int themeId, materialId, landmarkKind, ambientSoundId, lightingProfile, hatchKind;
    unsigned preparationMask;
    float ambientPeriod;
} RoomArchetype;
const RoomArchetype* RoomArchetypeGet(int theme);
int RoomLayoutVersion(int seed);
int RoomLayoutSeed(unsigned entropy);
int RoomLayoutVariant(int seed);
int RoomTemplateBuildVersion(int theme,int variant,int rotation,int version,RoomTemplate* out);
int RoomThemeForMaterial(int material);
int RoomTemplateBuildCurrent(int theme,int variant,int rotation,RoomTemplate* out);
int RoomPropIsLandmark(int kind);
int RoomPropIsControl(int kind);
#endif

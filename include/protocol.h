#ifndef CANITY_PROTOCOL_H
#define CANITY_PROTOCOL_H

#define PROTOCOL_NODE_COUNT 10
#define PROTOCOL_STORY_SUPPLIES 150
#define PROTOCOL_STORY_BANK_CAP 220
#define PROTOCOL_MISSIONS 128u
#define PROTOCOL_MISSION_SOLVED 256u
#define PROTOCOL_EVAC_LAYOUT 512u
#define PROTOCOL_STORY_PAID 1024u
#define PROTOCOL_STORY_FLAG_MASK 2047u
#define PROTOCOL_ROLE_RANK_SHIFT 8
#define SAVE_RUN_PLAY_DEAD_USED (1u<<4)
#define SAVE_RUN_SHORTCUT_USED (1u<<5)

typedef struct {
    int role, story, flags, ending;
    unsigned nodes;
    float cooldown, threatRaw;
    unsigned used;
} ProtocolRun;

typedef struct {
    unsigned briefSeen, selected, unlocked, endings;
    int guide;
} ProtocolProfile;

extern ProtocolRun protocolRun;
extern ProtocolProfile protocolProfile;

const char *ProtocolRoleName(int role);
const char *ProtocolRoleBio(int role);
const char *ProtocolRoleAbility(int role);
int ProtocolRoleBase(int role,int resource);
float ProtocolAbilityRemaining(void);
float ProtocolAbilityAge(void);
float ProtocolShieldImpact(void);
void ProtocolAbilityEffect(char *out,int capacity);
void ProtocolTickAbilityInput(void);
float ProtocolSprintCap(void);
float ProtocolPursuitDrain(void);
float ProtocolWrenchStunBonus(void);
int ProtocolSprintGrace(float dt,int requested);
const char *ProtocolAbilityName(int role);
int ProtocolSelectRole(int role);
int ProtocolBase(int resource);
void ProtocolBegin(int story, int fixedRole);
void ProtocolFloorReset(void);
void ProtocolTick(float dt);
void ProtocolActivate(void);
float ProtocolSprintBonus(void);
float ProtocolSprintDrain(void);
float ProtocolCrankMul(void);
float ProtocolContactLoss(float loss,float raw);
float ProtocolHunterMul(const void *actor, int optical);
int ProtocolOpticalTarget(float *x, float *y);
int ProtocolHearingTarget(int index,float dt,int chasing,int seesPlayer,float x,float y,float *tx,float *ty);
int ProtocolAbilityMarker(float *x, float *y);
int ProtocolNodeCost(int id);
int ProtocolNodeOwned(int id);
int ProtocolNodeAvailable(int id);
float ProtocolNodeThreat(int id);
const char *ProtocolNodeName(int id);
const char *ProtocolNodeShortName(int id);
const char *ProtocolNodeStatus(int id);
const char *ProtocolNodeDetail(int id);
int ProtocolBuyNode(int id);
int ProtocolRefund(void);
void ProtocolRestore(void);
void ProtocolCinematicStart(void);
void ProtocolCinematicTransition(int ending);
int ProtocolCinematicActive(void);
void ProtocolCinematicTick(float dt);
int ProtocolCinematicSkip(void);
void ProtocolCinematicDraw(void);
const char *ProtocolFloorName(void);
int ProtocolProp(int id,float *x,float *y);
int ProtocolTarget(float *x,float *y);
int ProtocolShowGuide(void);
int ProtocolPeacefulStory(void);
void ProtocolClearStoryActors(void);
void ProtocolBuildFloor(void);
void ProtocolResetTrial(void);
int ProtocolTrialVisual(float *x,float *y,float *signal);
int ProtocolFieldContains(float x,float y);
void ProtocolReleaseStoryNodes(void);
int ProtocolStoryPayout(void);
int ProtocolClaimLegacyEndingBonus(void);
int ProtocolTier(int tier);
int ProtocolFloorMaterial(void);
float ProtocolMonsterThreat(float threat);
enum { PROTOCOL_TERMINAL, PROTOCOL_RELAY, PROTOCOL_CORE, PROTOCOL_TRACE, PROTOCOL_ROUTE, PROTOCOL_EVIDENCE, PROTOCOL_DEVICE_COUNT };
int ProtocolNearestDevice(void);
const char *ProtocolDeviceHint(int id,int *ready);
int ProtocolInteract(int id);
void ProtocolEnterShelter(void);
void ProtocolCommitShelter(void);
const char *ProtocolExitHint(void);
const char *ProtocolRadioLine(void);
int ProtocolBreakerReady(void);
int ProtocolShelterPowered(void);
int ProtocolExitReady(void);
int ProtocolAdvance(void);
const char *ProtocolObjective(void);

#endif

/* tests/test_common.h —— 所有测试文件共享的设施
 *
 * 第 2 轮（2026-11-08）从 test_runtime.c 抽出来。
 * 原因：那个文件原来 4,707 行，38% 的提交都要读它（见 docs/架构重构方案_v2.md）。
 * 现在按域拆开，改 phase 只需要读 tests/domains/test_phase.c。
 */
#ifndef CANITY_TEST_COMMON_H
#define CANITY_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "spider.h"
#include "control_effect.h"
#include "build_tags.h"
#include "map_validator.h"
#include "hatch.h"
#include "game.h"
#include "entity.h"
#include "skills.h"
#include "bench.h"
#include "shop.h"
#include "daily.h"
#include "campaign.h"
#include "achievements.h"
#include "save.h"
#include "save_format.h"   /* R8: SaveEnvelope / SaveEnvelopeSeal */
#include "steam_zone.h"    /* R8: SteamZoneCloudDecide / STEAM_SYNC_* */
#include "sound_event.h"   /* B01: SoundEmit / SoundKind */
#include "audio.h"          /* B02: AudioComputeGains */
#include "sound_propagation.h"  /* B03: SoundReach */
#include "map_rooms.h"        /* C3: GenerateThemeRooms */
#include "cell_meta.h"        /* C2: cellMeta / CellTintFor */
#include "wallet.h"
#include "threat.h"
#include "ui.h"
#include "render.h"
#include "sprites.h"
#include "music.h"
#include "bestiary.h"
#include "warden.h"
#include "state.h"
#include "upgrade_run.h"
#include <stddef.h>

extern int carryAtRunStart,exitGoDeeper,exitChoiceOpen;
extern int knockCountRun,throwHitCountRun,echoCellsTotalRun;
extern int lastRunGold,lastRunKept,lastRunCaught;
extern int shopRefreshFloor,shopRefreshUsed,contractKind;
extern void BankSettle(int survived);
extern int BankPreviewGain(int survived);
extern void CloseSkillPick(void);
extern void GenerateLevel(void);
extern void SkillResetView(void);

/* 断言计数：定义在 test_runtime.c，所有测试文件共用同一份 */
extern int checks;

#define CHECK(c,m) do { ++checks; if(!(c)) { fprintf(stderr,"FAIL: %s (line %d)\n",m,__LINE__);exit(1); } } while(0)

/* 跨文件使用的辅助函数（定义留在 test_runtime.c） */
void clear_meta(void);
void daily_case(int goal, int target);
int  fixture_copy(const char* from, const char* to);
void finish_floor(void);
void mask32(int sprId, unsigned char out[32][32]);


/* 假音频后端（音乐测试用）：从 test_runtime.c 搬到共享头，
 * 让 tests/domains/test_progress.c 也能用 */
typedef struct {int plays,stops,pauses,scenes,ticks;float gain;} FakeMusic;
static int fake_play(void* p,const char* track,int loop){(void)track;(void)loop;((FakeMusic*)p)->plays++;return 1;}

static inline void fake_stop(void* p){((FakeMusic*)p)->stops++;}
static inline void fake_pause(void* p,int pause){((FakeMusic*)p)->pauses=pause;}
static inline void fake_gain(void* p,float gain){((FakeMusic*)p)->gain=gain;}
static inline void fake_scene(void* p,MusicScene scene){(void)scene;((FakeMusic*)p)->scenes++;}
static inline void fake_tick(void* p,float dt){(void)dt;((FakeMusic*)p)->ticks++;}

#endif /* CANITY_TEST_COMMON_H */

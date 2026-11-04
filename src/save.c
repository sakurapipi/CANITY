/*
 * Canity — 存档层实现：SaveBlob 读写 / 版本迁移 / 继续游戏
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "achievements.h"
#include "save.h"
#include "daily.h"
#include "threat.h"
#include "steam_zone.h"
extern int carryAtRunStart;
#include "bench.h"
#include "upgrade_run.h"
#include "lang.h"
#include "weapons.h"
#include "shop.h"
#include "game.h"
#include "entity.h"
#include "render.h"
#include "skills.h"
#include "bestiary.h"
#include "lockbox.h"   /* v3.0：读档要关锁盘 */

static SaveBlob g_save;

/* ------------------------------------------------------------------ */
/* 存档路径                                                            */
/* ------------------------------------------------------------------ */
/* 之前 SAVE_FILE 是**相对路径**，靠进程的 CWD 决定落点；
   而且只要那个目录对进程只读，fopen 就静默失败 —— 表现是
   "玩了一整局，重开什么都没了"，而且没有任何提示。

   现在给四个候选，写：第一个能写的；读：第一个存在的。
     ① exe 同目录        —— 便携版习惯，玩家能自己看到 / 备份
     ② %APPDATA%\Canity  —— 漫游配置，正常安装该用这个
     ③ %LOCALAPPDATA%\Canity
     ④ %TEMP%
   实测这台机器上 ①② 都被拦（ACCESS_DENIED），所以列表要往多处退。 */
#define SAVE_CAND_COUNT 6   /* v3.0：加 Documents 与 TEMP 子目录兜底 */

int g_saveSlot = 0;   /* v2.0：当前存档槽 0..2 */

static const char* SavePathCand(int i, char* buf, size_t n);

/* v2.0：写盘前把旧档复制成 .bak */
static void BackupSave(void) {
    char p[MAX_PATH];
    SavePathCand(0, p, sizeof(p));
    if (!p[0]) return;
    /* v3.0：缓冲区要留后缀的位置，否则满路径时后缀被截断 */
    char b[MAX_PATH + 16];
    snprintf(b, sizeof(b), "%s.bak", p);
    CopyFileA(p, b, FALSE);
}

static const char* SavePathCand(int i, char* buf, size_t n) {
    buf[0] = 0;
    const char* isolated = getenv("CANITY_DATA_DIR");
    if (isolated && isolated[0]) {
        snprintf(buf, n, "%s\\canity%d.sav", isolated, g_saveSlot);
        return buf;
    }
    if (i == 0) {
        GetModuleFileNameA(NULL, buf, (DWORD)n);
        char* sl = strrchr(buf, '\\');
        if (sl) sl[1] = 0; else buf[0] = 0;
        {
            char fn[64];
            snprintf(fn, sizeof(fn), "canity%d.sav", g_saveSlot);
            strncat(buf, fn, n - strlen(buf) - 1);
        }
        return buf;
    }
    const char* base = NULL;
    const char* sub = NULL;
    if (i == 1)      { base = getenv("APPDATA");      sub = "Canity"; }
    else if (i == 2) { base = getenv("LOCALAPPDATA"); sub = "Canity"; }
    else if (i == 3) { base = getenv("TEMP");         sub = NULL;     }
    /* v3.0：**再补两条** —— 实测从某些启动方式下前四个候选全部返回
       ERROR_ACCESS_DENIED(5)，存档一次都没写成功（g_saveState=2）。
       Documents 和 TEMP 子目录是权限最稳的两个位置。 */
    else if (i == 4) { base = getenv("USERPROFILE");  sub = "Documents\\Canity"; }
    else             { base = getenv("TEMP");         sub = "Canity"; }
    if (!base || !*base) base = getenv("TEMP");
    if (!base || !*base) {
        size_t need = strlen(SAVE_FILE) + 1;
        if (n >= need) memcpy(buf, SAVE_FILE, need);
        else if (n) buf[0] = 0;
        return buf;
    }
    if (sub) {
        char dir[MAX_PATH];
        snprintf(dir, sizeof(dir), "%s\\%s", base, sub);
        /* v3.0：sub 里可能带反斜杠（Documents\Canity），逐级建 */
        CreateDirectoryA(dir, NULL);
        {
            char* slash = strchr(sub, '\\');
            if (slash) {
                char lvl1[MAX_PATH];
                snprintf(lvl1, sizeof(lvl1), "%s\\%.*s", base, (int)(slash - sub), sub);
                CreateDirectoryA(lvl1, NULL);
            }
        }
        char suffix[32]; snprintf(suffix,sizeof(suffix),"\\canity%d.sav",g_saveSlot);
        if(strlen(dir)+strlen(suffix)<n) { strcpy(buf,dir); strcat(buf,suffix); }
    } else {
        snprintf(buf, n, "%s\\canity%d.sav", base, g_saveSlot);
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/* 文件读写：直接走 Win32，不经过 stdio                                 */
/* ------------------------------------------------------------------ */
/* fopen 失败只会给一个 NULL：路径拼错、被权限拦、文件名非法，全都长一个样，
   errno 和 GetLastError 还是两套。存档又属于"失败也没人看得见"的那种，
   结果就是玩一整局、重开全没了。
   这里换成 CreateFileA + WriteFile：失败时能拿到确切的 GetLastError，
   并且把结果记到全局，UI 上可以直接告诉玩家"没存上"。 */
int  g_saveState = 0;          /* 0 还没试过  1 成功  2 失败 */
int  g_saveError = 0;          /* 失败时的 GetLastError */
int  g_saveErrAll[SAVE_CAND_COUNT] = {0};  /* 每个候选的失败码，排查用 */
char g_savePath[MAX_PATH] = "";/* 最近一次写入成功的路径 */

/* 返回 0 = 成功，否则 = Win32 错误码 */
static int WriteWholeFile(const char* path, const void* data, size_t n) {
    /* v2.2：先写临时文件、flush 落盘、再原子改名。
       原来是 CREATE_ALWAYS 原地覆盖 —— 写到一半掉电/被杀就只剩半截文件，
       而半截文件能过 magic+version 校验，于是"存档存在但是空的"。 */
    char tmp[MAX_PATH + 16];   /* v3.0：留 ".tmp" 的位置 */
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return (int)GetLastError();
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, (DWORD)n, &wrote, NULL);
    DWORD err = ok ? 0 : GetLastError();
    if (ok && wrote != (DWORD)n) err = ERROR_WRITE_FAULT;
    if (ok) FlushFileBuffers(h);
    CloseHandle(h);
    if (err) { DeleteFileA(tmp); return (int)err; }
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        err = GetLastError();
        DeleteFileA(tmp);
        return (int)err;
    }
    return 0;
}

/* 返回读到的字节数，失败返回 -1 */
static long ReadWholeFile(const char* path, void* buf, size_t cap) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, (DWORD)cap, &got, NULL);
    CloseHandle(h);
    return ok ? (long)got : -1;
}

/* 显示 / 音频 / 键位这些设置项是所有存档共用的，
   单独抽出来，训练场里也能只写设置、不动正式进度。 */
static void CopyDisplaySettings(SaveBlob* s) {
    s->resIndex = resIndex;
    s->fullscreen = isFullscreen;
    s->fpsCap = fpsCapIndex;
    s->scale = renderScaleIndex;
    s->dynamicRes = dynamicRes;
    s->fov = fovIndex;
    s->vignette = vignetteOn;
    s->showFps = showFps;
    s->hudDetail = hudDetail;
    s->qteHint = qteHint;
    s->gamma = gammaValue;
    s->sens = mouseSens;
    s->volume = volumeF;
    s->musicVolume = musicVolumeF;
    for (int a = 0; a < BINDING_SLOTS; a++)
        s->bindings[a] = (a < ACT_COUNT) ? g_bindings[a] : 0;
    s->lang = g_lang;
    s->saveSlot = g_saveSlot;
}

/* 训练场里 SaveGame 会整份跳过；但设置是在训练场里改的也得存。
   把当前存档读出来（没有就新建一份空档），只覆盖设置字段。 */
static void SaveSettingsOnly(void) {
    SaveBlob tmp = g_save;
    if (tmp.magic != SAVE_MAGIC || tmp.version != SAVE_VERSION ||
        tmp.saveSlot != g_saveSlot) {
        memset(&tmp, 0, sizeof(tmp));
        tmp.magic = SAVE_MAGIC;
        tmp.version = SAVE_VERSION;
        tmp.gameVersion = CANITY_VERSION_NUM;
        tmp.hasRun = 0;
    }
    CopyDisplaySettings(&tmp);
    /* 训练场不写进度，但「通关过」和「无尽已解锁」是跨局记录，
       不能被一次只写设置的存档覆盖掉。 */
    tmp.gameClearedSave = gameCleared;

    char p[MAX_PATH];
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        SavePathCand(i, p, sizeof(p));
        if (WriteWholeFile(p, &tmp, sizeof(tmp)) == 0) {
            g_save = tmp;
            g_saveState = 1; SteamZoneUploadSave(p);
            g_saveError = 0;
            strncpy(g_savePath, p, sizeof(g_savePath) - 1);
            g_savePath[sizeof(g_savePath) - 1] = 0;
            return;
        }
    }
    g_saveState = 2;
}

/* 训练/教程退出后，菜单要知道「真正的存档里还有没有一局」。
   直接读 g_save.hasRun，而不是沿用训练局强行置上的 hasSave。 */
int SaveHasPendingRun(void) {
    return g_save.hasRun ? 1 : 0;
}

void SaveGame(void) {
    /* 训练场不是正式进度：写进去的话 Continue 会读出一张走廊图。
       但设置项仍然要落盘，否则在教程里改完设置重开就白改了。 */
    if (trainingMode) {
        SaveSettingsOnly();
        return;
    }
    /* ==================================================================
     * v28：**在菜单里改设置时，别用空的世界覆盖掉 Continue**
     * ------------------------------------------------------------------
     * SaveGame 会把「运行时世界」整份序列化。但启动后如果玩家还没点
     * Continue，运行时世界根本没加载（currentLevel/地图都是初值），
     * 在菜单里改一下语言、买个技能也会走到这里 —— 原来就会写出一份
     * hasRun=0、地图全空的存档，把可继续的进度毁掉。
     *
     * 结构体里从 hasRun 到 thr[] 正好是**连续的一整段进度字段**，
     * 所以没有活动对局时把这一段从上一份 blob 原样搬回来即可；
     * 大厅/银行/成就/设置仍然按当前运行时写。
     * ================================================================ */
    SaveBlob prev = g_save;
    int keepPendingRun =
        (!runInProgress && prev.magic == SAVE_MAGIC && prev.hasRun &&
         prev.saveSlot == g_saveSlot &&   /* 刚切换存档槽时不要把旧槽的进度带过来 */
         currentState != STATE_PLAYING && currentState != STATE_PAUSED &&
         currentState != STATE_GAMEOVER);
    size_t runOff = offsetof(SaveBlob, hasRun);
    size_t runLen = offsetof(SaveBlob, hudDetail) - runOff;
    memset(&g_save, 0, sizeof(g_save));
    g_save.magic = SAVE_MAGIC;
    g_save.version = SAVE_VERSION;
    g_save.gameVersion = CANITY_VERSION_NUM;

    CopyDisplaySettings(&g_save);
    g_save.carryGold = carryGold;
    g_save.upgBattery = upgBattery;
    g_save.upgSpeed = upgSpeed;
    g_save.upgQuiet = upgQuiet;
    g_save.upgSanity = upgSanity;
    g_save.upgEco = upgEco;
    g_save.upgRest = upgRest;
    g_save.bestLevel = bestLevel;

    g_save.hasRun = runInProgress;
    g_save.level = currentLevel;
    g_save.sanity = sanity;
    g_save.battery = battery;
    g_save.stamina = stamina;
    g_save.energy = energy;
    g_save.benchOwnedSave = benchOwned;
    g_save.hideSpotSaveCount = hideSpotCount;
    for (int i = 0; i < 6; i++) g_save.hideSpotSave[i] = hideSpots[i];
    g_save.hiddenInSave = hiddenIn;
    /* v3.3：安全屋每层限一次的标记也要存，否则读档后可以同层再免费充一次 */
    {
        extern int  ShopSafeHealLv(void), ShopSafeChargeLv(void), ShopSafeStaminaLv(void);
        g_save.safeHealLvSave    = ShopSafeHealLv();
        g_save.safeChargeLvSave  = ShopSafeChargeLv();
        g_save.safeStaminaLvSave = ShopSafeStaminaLv();
    }
    /* v3.3：跨局累计的成就计数器 */
    {
        extern int floorsNoLight, floorsFlawless, floorsPacifist, floorsFast;
        extern int mapsFullyExplored, fusesCollectedRun, hideCount, totalThrown;
        extern float crankSecondsTotal, lowSanitySeconds;
        extern int locksOpened;
        g_save.achFloorsNoLight    = floorsNoLight;
        g_save.achFloorsFlawless   = floorsFlawless;
        g_save.achFloorsPacifist   = floorsPacifist;
        g_save.achFloorsFast       = floorsFast;
        g_save.achMapsExplored     = mapsFullyExplored;
        g_save.achFusesRun         = fusesCollectedRun;
        g_save.achLocksOpened      = locksOpened;
        g_save.achHideCount        = hideCount;
        g_save.achTotalThrown      = totalThrown;
        g_save.achCrankSecondsTotal= crankSecondsTotal;
        g_save.achLowSanitySeconds = lowSanitySeconds;
    }
    /* v3.4 银行 */
    {
        extern int bankGold, bankHistMax, bankCarryPct, carryGold;
        g_save.bankGoldSave    = bankGold;
        g_save.bankHistMaxSave = bankHistMax;
        g_save.bankCarryPctSave= bankCarryPct;
        g_save.runPrincipalSave = carryAtRunStart;
        g_save.carryGoldSave   = carryGold;   /* v6：本局本金 */
    }
    /* v7 第 ㊿ 轮：数组是 runUpgSave[5]，原来只存 4 个 → 第 5 项（扳手）永远丢 */
    for (int i = 0; i < 5; i++) g_save.runUpgSave[i] = RunUpgLevel(i);
    /* v29：第 6 条单独存（数组不能加长，见 save.h 的注释） */
    { extern int RunUpgMapDrainLevel(void);
      g_save.runUpgMinimapSave = RunUpgMapDrainLevel(); }
    g_save.flashlightOn = flashlightOn;
    g_save.posX = posX; g_save.posY = posY;
    g_save.dirX = dirX; g_save.dirY = dirY;
    g_save.exitX = exitX; g_save.exitY = exitY;
    g_save.fusesHeld = fusesHeld;
    g_save.fusesNeeded = fusesNeeded;
    g_save.breakerOpen = breakerOpen;
    g_save.breakerActive = breaker.active;
    g_save.breakerX = breaker.x;
    g_save.breakerY = breaker.y;
    g_save.searchedCount = searchedCount;
    g_save.runTime = runTime;
    g_save.nContainers = containerCount;
    for (int i = 0; i < MAX_CONTAINERS; i++) g_save.cont[i] = containers[i];
    memcpy(g_save.worldMap, worldMap, sizeof(worldMap));
    memcpy(g_save.explored, explored, sizeof(explored));

    /* v6 新增字段 */
    g_save.flares = flares;
    g_save.bottles = bottles;
    g_save.safeActive = safeRoom.active;
    g_save.safeX = safeRoom.x;
    g_save.safeY = safeRoom.y;
    g_save.levelMod = levelMod;
    g_save.modSanityMul = modSanityMul;
    g_save.modSpeedMul = modSpeedMul;
    g_save.themeId = themeId;
    g_save.nThrown = 0;
    for (int i = 0; i < MAX_THROWN; i++) {
        g_save.thr[i] = thrown[i];
        if (thrown[i].active) g_save.nThrown++;
    }

    /* v7 新增：成就系统 */
    for (int i = 0; i < ACH_COUNT; i++) {
        g_save.achUnlocked[i] = g_achievements[i].unlocked;
        g_save.achProgress[i] = g_achievements[i].progress;
    }

    BackupSave();     /* v2.0：写坏之前先留一份 .bak */
    /* v9：投掷物 / 待上缴金币 / 局内技能，以前完全没存 */
    g_save.bricks = bricks;
    g_save.acids = acids;
    g_save.smokeBombs = smokeBombs;
    g_save.carryGold = carryGold;
    g_save.runSkillMask = runSkillMask;
    g_save.lang = g_lang;
    g_save.saveSlot = g_saveSlot;
    /* v13：v2.0 新系统 */
    for (int i = 0; i < RELIC_COUNT; i++) g_save.relicOwned[i] = relicOwned[i];
    g_save.relicTotal = relicTotal;
    g_save.dailyMode = dailyMode;
    /* v7：每日任务进度存盘 */
    { extern void DailySaveTo(int*);
      int da[15] = {0};
      DailySaveTo(da);
      g_save.dailyGoalId    = da[0];
      g_save.dailyGoalParam = da[1];
      g_save.dailyProgress  = da[2];
      g_save.dailyDone      = da[3];
      g_save.dailyStreak    = da[4];
      g_save.dailyQuestDate = (unsigned)da[5];
      g_save.dailyLastCompletedDate = (unsigned)da[6];
      for (int j=0;j<8;++j) g_save.dailySamples[j] = da[7+j]; }
    { extern int knockCountRun,throwHitCountRun,echoCellsTotalRun,dailyEscapeCount;
      extern int floorsNoSanityLoss,wardenCleanFloors,fullHideDone;
      extern float doubleHuntSeconds;
      extern int lastRunGold,lastRunKept,lastRunCaught;
      g_save.knockRunSave=knockCountRun; g_save.throwHitRunSave=throwHitCountRun;
      g_save.echoRunSave=echoCellsTotalRun; g_save.dailyEscapeRunSave=dailyEscapeCount;
      g_save.cleanFloorsSave=floorsNoSanityLoss; g_save.wardenFloorsSave=wardenCleanFloors;
      g_save.fullHideSave=fullHideDone; g_save.doubleHuntSave=doubleHuntSeconds;
      g_save.lastRunGoldSave=lastRunGold; g_save.lastRunKeptSave=lastRunKept; g_save.lastRunCaughtSave=lastRunCaught; }
    for(int i=26;i<SKILL_COUNT;++i) g_save.extraSkillRanks[i-26]=skillRank[i];
    { extern int shopRefreshFloor,shopRefreshUsed,contractKind,shopContractUsed;
      g_save.shopRefreshFloorSave=shopRefreshFloor; g_save.shopRefreshUsedSave=shopRefreshUsed;
      g_save.contractKindSave=contractKind; g_save.contractLockedSave=shopContractUsed;
      g_save.threatSave=ThreatValue(); }
    BestiarySave(g_save.bestiaryKnowledge);
    g_save.dailySeed = dailySeed;
    g_save.campaignMode = campaignMode;
    g_save.campaignStage = campaignStage;
    g_save.firePots = firePots;
    g_save.gluePots = gluePots;
    g_save.mirrors = mirrors;
    g_save.bankStreak = bankStreak;
    g_save.modBatteryMul = modBatteryMul;
    g_save.modShardMul = modShardMul;
    g_save.modNoiseMul = modNoiseMul;
    /* v11：武器局外成长 */
    for (int i = 0; i < 3; i++) {
        g_save.weaponRank[i] = weaponRank[i];
        g_save.weaponXp[i] = weaponXp[i];
        g_save.weaponAttach[i][0] = weaponAttach[i][0];
        g_save.weaponAttach[i][1] = weaponAttach[i][1];
    }
    for (int i = 0; i < ATTACH_COUNT; i++) g_save.attachOwned[i] = attachOwned[i];
    g_save.curWeapon = curWeapon;
    g_save.runUltimate = runUltimate;

    for (int i = 0; i < 26; i++) g_save.skillRank[i] = skillRank[i];
    g_save.cores = cores;
    g_save.activeKeystone = activeKeystone;
    g_save.bestMainScore = bestMainScore;
    g_save.seenMonsters = bestiarySeen;

    /* v8 新增：无尽模式进度 */
    g_save.bestEndlessLevel = bestEndlessLevel;
    g_save.bestEndlessScore = bestEndlessScore;
    g_save.isEndlessRunSaved = isEndlessRun;
    /* v28：通关记录必须持久化，否则重启后 Endless 又从菜单里消失 */
    g_save.gameClearedSave = gameCleared;

    /* v28：没有活动对局（在菜单里改设置/买东西）→ 进度段沿用上一份存档，
       不要让空的运行时世界把 Continue 覆盖掉。 */
    if (keepPendingRun)
        memcpy((char*)&g_save + runOff, (const char*)&prev + runOff, runLen);

    /* 逐个候选试写，第一个成功的就用它 */
    char p[MAX_PATH];
    int firstErr = 0;
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        SavePathCand(i, p, sizeof(p));
        int err = WriteWholeFile(p, &g_save, sizeof(g_save));
        if (err == 0) {
            g_saveState = 1; SteamZoneUploadSave(p);
            g_saveError = 0;
            strncpy(g_savePath, p, sizeof(g_savePath) - 1);
            g_savePath[sizeof(g_savePath) - 1] = 0;
            return;
        }
        if (i == 0) firstErr = err;
        g_saveErrAll[i] = err;
    }
    /* 一个都写不进去：记下 exe 目录那次的错误码 + 路径，UI 会显示出来 */
    g_saveState = 2;
    g_saveError = firstErr;
    SavePathCand(0, p, sizeof(p));
    strncpy(g_savePath, p, sizeof(g_savePath) - 1);
    g_savePath[sizeof(g_savePath) - 1] = 0;
}

/* v3.2：选择「最近保存过」的存档槽。
   原来 g_saveSlot 永远从 0 开始，玩家在槽 2 里改的设置/进度
   重启后会去读槽 0，看起来就是「设置没保存」。
   这里扫一遍 3 个槽的主档和 .bak，取修改时间最新的那个。 */
static int PathWriteTime(const char* path, FILETIME* out) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) return 0;
    *out = fa.ftLastWriteTime;
    return 1;
}

static int FindNewestSaveSlot(void) {
    int oldSlot = g_saveSlot;
    int bestSlot = -1;
    FILETIME bestTime;
    memset(&bestTime, 0, sizeof(bestTime));

    for (int slot = 0; slot < SAVE_SLOTS; slot++) {
        g_saveSlot = slot;
        for (int i = 0; i < SAVE_CAND_COUNT; i++) {
            char p[MAX_PATH];
            SavePathCand(i, p, sizeof(p));
            for (int bak = 0; bak < 2; bak++) {
                char path[MAX_PATH];
                if (bak) { if(strlen(p)+4>=sizeof(path)) continue; strcpy(path,p); strcat(path,".bak"); }
                else     snprintf(path, sizeof(path), "%s", p);
                FILETIME ft;
                if (!PathWriteTime(path, &ft)) continue;
                if (bestSlot < 0 || CompareFileTime(&ft, &bestTime) > 0) {
                    bestSlot = slot;
                    bestTime = ft;
                }
            }
        }
    }

    g_saveSlot = (bestSlot >= 0) ? bestSlot : oldSlot;
    return bestSlot;
}

/* 只读设置（启动时），返回是否还有可继续的进度 */
/* ===== v6 **银行状态恢复（LoadSettings / ContinueRun 共用）** =====
   原来只在 ContinueRun 里恢复，但 SaveGame 无条件写 bankGold 等字段。
   后果：启动后不进 Continue（直接看 BANK 页、改设置、或关窗口）就触发
        SaveGame，把存款覆盖成 0 —— 玩家全部存款静默消失。
   现在启动读设置时也恢复，写回去的值就是对的。 */
static void RestoreBankFromSave(void) {
    extern int bankGold, bankHistMax, bankCarryPct, carryGold;
    int rawGold = g_save.bankGoldSave;
    int rawHist = g_save.bankHistMaxSave;
    int rawPct  = g_save.bankCarryPctSave;

    bankGold     = rawGold > 0 ? rawGold : 0;
    bankHistMax  = rawHist > 0 ? rawHist : 1;
    bankCarryPct = rawPct;

    /* 三个字段全是 0 → 这份存档根本没有银行段（老档），把 0..3 换算成 21 档 */
    if (rawGold == 0 && rawHist == 0 && rawPct >= 0 && rawPct <= 3) {
        static const int oldMap[4] = { 5, 10, 15, 20 };
        bankCarryPct = oldMap[rawPct];
    }
    if (bankCarryPct < 0) bankCarryPct = 0;
    if (bankCarryPct >= BANK_CARRY_STEPS) bankCarryPct = BANK_CARRY_STEPS - 1;
    if (bankHistMax < bankGold) bankHistMax = bankGold;
    if (bankHistMax < 1) bankHistMax = 1;

    /* v6：本局本金 —— 老档没有这个字段（读出 0），此时按当前金币兜底，
       保证 profit = carryGold - carryGold 不会把全部余额当成利润。 */
    carryGold = g_save.carryGoldSave;
    carryAtRunStart = g_save.runPrincipalSave;
    /* v7 第 65 轮：**这里原来是一句自比较**
       `if (carryGold > carryGold) carryGold = carryGold;`
       —— 局部 extern 把全局自己遮住，条件永远为假（编译器 -Wtautological-compare 直接报）。
       本意应该是「夹到合法范围」，负值已经在上一条处理掉了，这句删掉即可。 */
    { extern int carryGold; if (carryGold < 0) carryGold = 0; }
}

/* ==================================================================
 * v24 存档布局（2.3.3 及更早）
 * ------------------------------------------------------------------
 * v26 重建了成就表（40 → 32）、技能树（20 → 26 + 4 个额外），
 * 还删掉了 pendingShards，所以 v24 的字节**不能**直接当新结构读。
 * 这里保留一份旧结构，只在版本号和文件长度都精确匹配时启用迁移。
 * ================================================================ */
typedef struct {
    unsigned int magic;
    int version;
    int gameVersion;
    int resIndex, fullscreen, fpsCap, scale, fov, vignette, showFps;
    float gamma, sens;
    int shards, upgBattery, upgSpeed, upgQuiet, upgSanity;
    int bestLevel;
    float volume;
    int hasRun, level, fusesHeld, fusesNeeded, breakerOpen, breakerActive, searchedCount, nContainers;
    int flashlightOn;
    float sanity, battery, stamina, posX, posY, dirX, dirY, exitX, exitY, breakerX, breakerY, runTime;
    Container cont[MAX_CONTAINERS];
    int worldMap[MAP_HEIGHT][MAP_WIDTH];
    int explored[MAP_HEIGHT][MAP_WIDTH];
    int flares, bottles;
    int safeActive;
    float safeX, safeY;
    int levelMod;
    float modSanityMul, modSpeedMul;
    int themeId;
    int nThrown;
    Thrown thr[MAX_THROWN];
    int hudDetail, qteHint;
    int bindings[BINDING_SLOTS];
    int upgEco, upgRest;
    int achUnlocked[40];
    int achProgress[40];
    int bestEndlessLevel, bestEndlessScore, isEndlessRunSaved;
    int skillRank[20];
    int cores, activeKeystone, bestMainScore, seenMonsters;
    int bricks, acids, smokeBombs;
    int pendingShards;
    unsigned int runSkillMask;
    int lang, saveSlot;
    int weaponRank[3], weaponXp[3];
    int relicOwned[RELIC_COUNT], relicTotal;
    int dailyMode, dailySeed;
    int campaignMode, campaignStage;
    int firePots, gluePots, mirrors;
    int bankStreak;
    float modBatteryMul, modShardMul, modNoiseMul;
    int weaponAttach[3][2];
    int attachOwned[10];
    int curWeapon;
    int runUltimate;
    float energy;
    unsigned int benchOwnedSave;
    int runUpgSave[5];
    int hideSpotSaveCount;
    HideSpot hideSpotSave[6];
    int hiddenInSave;
    int safeHealLvSave, safeChargeLvSave, safeStaminaLvSave;
    int achFloorsNoLight, achFloorsFlawless, achFloorsPacifist, achFloorsFast;
    int achMapsExplored, achFusesRun, achLocksOpened;
    int achHideCount, achTotalThrown;
    float achCrankSecondsTotal, achLowSanitySeconds;
    int bankGoldSave, bankHistMaxSave, bankCarryPctSave, carryGoldSave;
} LegacySaveV24;

/* 迁移只保留能一一对应的字段：
   · 成就表从 40 条重建为 32 条，旧 ID 不保证对应 → 清零，避免误弹成就；
   · 技能树前 20 项按位置保留（新树 20..29 级从 0 开始）；
   · 其余大厅进度、金币、银行、遗物、藏身点原样带走。 */
static void MigrateV24(SaveBlob* cur, const LegacySaveV24* old) {
    memset(cur, 0, sizeof(*cur));
    /* 到 upgRest 为止两边布局完全一致（成就是第一个尺寸变化的数组） */
    memcpy(cur, old, offsetof(LegacySaveV24, achUnlocked));
    cur->magic = SAVE_MAGIC;
    cur->version = SAVE_VERSION;

    cur->bestEndlessLevel = old->bestEndlessLevel;
    cur->bestEndlessScore = old->bestEndlessScore;
    cur->isEndlessRunSaved = old->isEndlessRunSaved;
    for (int i = 0; i < 20; i++) cur->skillRank[i] = old->skillRank[i];
    cur->cores = old->cores;
    cur->activeKeystone = old->activeKeystone;
    cur->bestMainScore = old->bestMainScore;
    cur->seenMonsters = old->seenMonsters;
    cur->bricks = old->bricks;
    cur->acids = old->acids;
    cur->smokeBombs = old->smokeBombs;
    cur->runSkillMask = old->runSkillMask;
    cur->lang = old->lang;
    cur->saveSlot = old->saveSlot;
    for (int i = 0; i < 3; i++) {
        cur->weaponRank[i] = old->weaponRank[i];
        cur->weaponXp[i] = old->weaponXp[i];
    }
    for (int i = 0; i < RELIC_COUNT; i++) cur->relicOwned[i] = old->relicOwned[i];
    cur->relicTotal = old->relicTotal;
    cur->dailyMode = old->dailyMode;
    cur->dailySeed = old->dailySeed;
    cur->campaignMode = old->campaignMode;
    cur->campaignStage = old->campaignStage;
    cur->firePots = old->firePots;
    cur->gluePots = old->gluePots;
    cur->mirrors = old->mirrors;
    cur->bankStreak = old->bankStreak;
    cur->modBatteryMul = old->modBatteryMul;
    cur->modShardMul = old->modShardMul;
    cur->modNoiseMul = old->modNoiseMul;
    for (int i = 0; i < 3; i++) {
        cur->weaponAttach[i][0] = old->weaponAttach[i][0];
        cur->weaponAttach[i][1] = old->weaponAttach[i][1];
    }
    for (int i = 0; i < 10; i++) cur->attachOwned[i] = old->attachOwned[i];
    cur->curWeapon = old->curWeapon;
    cur->runUltimate = old->runUltimate;
    cur->energy = old->energy;
    cur->benchOwnedSave = old->benchOwnedSave;
    for (int i = 0; i < 5; i++) cur->runUpgSave[i] = old->runUpgSave[i];
    cur->hideSpotSaveCount = old->hideSpotSaveCount;
    for (int i = 0; i < 6; i++) cur->hideSpotSave[i] = old->hideSpotSave[i];
    cur->hiddenInSave = old->hiddenInSave;
    cur->safeHealLvSave = old->safeHealLvSave;
    cur->safeChargeLvSave = old->safeChargeLvSave;
    cur->safeStaminaLvSave = old->safeStaminaLvSave;
    cur->achFloorsNoLight = old->achFloorsNoLight;
    cur->achFloorsFlawless = old->achFloorsFlawless;
    cur->achFloorsPacifist = old->achFloorsPacifist;
    cur->achFloorsFast = old->achFloorsFast;
    cur->achMapsExplored = old->achMapsExplored;
    cur->achFusesRun = old->achFusesRun;
    cur->achLocksOpened = old->achLocksOpened;
    cur->achHideCount = old->achHideCount;
    cur->achTotalThrown = old->achTotalThrown;
    cur->achCrankSecondsTotal = old->achCrankSecondsTotal;
    cur->achLowSanitySeconds = old->achLowSanitySeconds;
    cur->bankGoldSave = old->bankGoldSave;
    cur->bankHistMaxSave = old->bankHistMaxSave;
    cur->bankCarryPctSave = old->bankCarryPctSave;
    cur->carryGoldSave = old->carryGoldSave;
    cur->runPrincipalSave = old->carryGoldSave;   /* v25 新增字段，用本金兜底 */
    cur->musicVolume = volumeF;                   /* v27 字段：老档沿用音效音量 */
}

typedef char legacySave24_must_fit[(sizeof(LegacySaveV24) <= sizeof(SaveBlob)) ? 1 : -1];

int LoadSettings(void) {
    char p[MAX_PATH];
    memset(&g_save, 0, sizeof(g_save));
    /* 先定位最近使用的存档槽，再按槽读文件 */
    FindNewestSaveSlot();
    /* 按文件的**实际字节数**读，比 sizeof(SaveBlob) 短的就是老存档 */
    long fs = -1;
    for (int i = 0; i < SAVE_CAND_COUNT && fs < 0; i++) {
        const char* cand = SavePathCand(i, p, sizeof(p));
        fs = ReadWholeFile(cand, &g_save, sizeof(g_save));
        if (fs <= 0) {
            /* v2.2：主档读不出来就试 .bak —— 原来只写不读，等于白备份 */
            char bak[MAX_PATH];
            snprintf(bak, sizeof(bak), "%s.bak", cand);
            fs = ReadWholeFile(bak, &g_save, sizeof(g_save));
        }
    }
    if (fs <= 0) {
        /* v8：第一次运行按系统语言选中文 —— 中文 Windows 上不用先去设置里找开关。 */
        if (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE) g_lang = LANG_ZH;
        return 0;
    }
    size_t fileSize = (size_t)fs;
    size_t want = (fileSize < sizeof(g_save)) ? fileSize : sizeof(g_save);
    size_t got = want;
    /* v3.0：**magic 不对要换下一个候选** ——
       原来直接 return 0，于是「路径 0 有个损坏文件」会把
       「路径 1 有个好文件」整个挡住（验证报告 BUG7）。 */
    if (!got || g_save.magic != SAVE_MAGIC) {
        for (int i = 1; i < SAVE_CAND_COUNT; i++) {
            const char* c2 = SavePathCand(i, p, sizeof(p));
            long f2 = ReadWholeFile(c2, &g_save, sizeof(g_save));
            if (f2 > 0 && g_save.magic == SAVE_MAGIC) { fs = f2; break; }
            char b2[MAX_PATH];
            snprintf(b2, sizeof(b2), "%s.bak", c2);
            f2 = ReadWholeFile(b2, &g_save, sizeof(g_save));
            if (f2 > 0 && g_save.magic == SAVE_MAGIC) { fs = f2; break; }
        }
        if (g_save.magic != SAVE_MAGIC) return 0;
        fileSize = (size_t)fs;
    }

    /* 存档**格式**版本：接受 v4 / v5（老结构）与 v6（当前结构）。
       老存档按文件长度就能认出来 —— 新增字段永远追加在结构末尾，
       老文件的字节数正好少一块。这里**不**做「版本不符就整份丢弃」：
       键位与显示偏好丢了没有意义，而丢弃整份存档会让玩家进度全没。 */
    /* 1.8.0 是测试版：只认当前格式，旧存档直接丢弃（已确认可接受）。 */
    /* v2.0：v13 → v14 迁移。
       技能树从 15 节点变 20 节点，skillRank 是定长数组 → 结构体尺寸变了。
       策略：技能等级全清、把已投入的金币和核心按旧价退回，其余进度原样保留。
       旧价：普通节点 {100,500,900}、keystone 2000 + 1 核、跨支 1400 + 2 核。 */
    /* v2.2 修复：v13→v14 的"迁移"是错的 —— 它把 v13 的文件字节
       原地当成 v14 的 SaveBlob 解释，而 v14 的 skillRank[15]→[20]
       多出 20 字节，导致 skillRank 之后的所有字段整体错位 5 个 int：
       成就被清零、核心被读成随机值、武器熟练度串到别的字段。

       真正安全的做法：不认识的版本**不解释**，把旧档改名归档，
       当作"没有存档"处理。玩家的数据一定还在盘上，可以回滚，
       而不是被一个错误的迁移静默毁掉。 */
    /* 读进来的字节可能比当前结构短：先把尾部清零，后面才能安全地按
       “缺字段取默认值”处理。 */
    int hasMusicField = (fileSize >= offsetof(SaveBlob, musicVolume) + sizeof(float));
    /* v28 字段：v27 及更早的存档不包含它们，读档时按「字段是否完整在文件里」判断，
       缺字段时取默认值（Dynamic Res 默认开、未通关）。 */
    int hasDynamicResField = (fileSize >= offsetof(SaveBlob, dynamicRes) + sizeof(int));
    int hasGameClearedField = (fileSize >= offsetof(SaveBlob, gameClearedSave) + sizeof(int));
    if (fileSize < sizeof(g_save))
        memset((char*)&g_save + fileSize, 0, sizeof(g_save) - fileSize);

    /* v24（2.3.3 及更早）的布局和现在不同：成就 40→32、技能树 20→26、
       还删了 pendingShards。只在版本号和文件长度都精确匹配时迁移，
       对不上就继续走“归档不解释”的安全路径。 */
    if (g_save.version == 24 && fileSize == sizeof(LegacySaveV24)) {
        LegacySaveV24 old;
        memcpy(&old, &g_save, sizeof(old));
        MigrateV24(&g_save, &old);
        fileSize = sizeof(SaveBlob);
    }
    /* v25 / v26 / v27：新字段都追加在结构末尾，前缀可以直接读。
       v25 缺 runPrincipalSave，用本局本金兜底（和原来的迁移一致）；
       v28 新增的 Dynamic Res / 通关记录由下面的 has*Field 判断补默认值。 */
    if (g_save.version == 25 || g_save.version == 26 ||
        g_save.version == 27 || g_save.version == 28) {
        g_save.version = SAVE_VERSION;
        if (g_save.runPrincipalSave == 0) g_save.runPrincipalSave = g_save.carryGoldSave;
    }
    if (g_save.version != SAVE_VERSION) {
        char oldp[MAX_PATH], arc[MAX_PATH + 24];   /* v3.0：留 ".v%d.bak" 的位置 */
        SavePathCand(0, oldp, sizeof(oldp));
        if (oldp[0]) {
            snprintf(arc, sizeof(arc), "%s.v%d.bak", oldp, g_save.version);
            MoveFileExA(oldp, arc, MOVEFILE_REPLACE_EXISTING);
        }
        return 0;
    }
    /* 旧档没有音乐音量字段：在下面读到 volumeF 之后回退到音效音量。 */
    if (!hasMusicField) g_save.musicVolume = 0.0f;
    int isOldSave = (fileSize < sizeof(SaveBlob));

    /* 迁移：老存档缺的字段给默认值。
       v5 及更早没有键位表和 ECO/REST 升级 —— 键位取出厂默认（比
       全 0 好得多：全 0 意味着「没有绑定」，游戏直接没法玩），
       新升级从 0 级开始。 */
    if (isOldSave) {
        /* v6 之前没有 bindings/upgEco/upgRest 字段。
           判据：文件没包含**完整的** bindings 数组（老 v5 文件的长度
           正好等于 bindings 的偏移量，不能用严格小于偏移量来判断）。 */
        size_t bindOff  = (size_t)((char*)&g_save.bindings[0] - (char*)&g_save);
        size_t bindNeed = bindOff + sizeof(g_save.bindings);
        if (fileSize < bindNeed) {
            static const int defBindings[ACT_COUNT] = BINDINGS_DEFAULT;
            for (int a = 0; a < ACT_COUNT; a++) g_save.bindings[a] = defBindings[a];
            g_save.upgEco = 0;
            g_save.upgRest = 0;
        }
        if (g_save.version < 5) {
            g_save.hudDetail = 1;    /* Normal */
            g_save.qteHint = 1;      /* Full */
        }
    }
    /* 边界检查：存档可能被外部改坏，数值必须夹回合法范围 */
    if (g_save.hudDetail < 0 || g_save.hudDetail > 2) g_save.hudDetail = 1;
    if (g_save.qteHint < 0 || g_save.qteHint > 1) g_save.qteHint = 1;

    /* 注意这跟 gameVersion 是两回事 ——
       gameVersion 只是记录「这份存档是哪个应用版本写的」，
       升个补丁版本（比如 1.0.0 -> 1.0.1）不该让玩家丢进度，
       所以这里**不**拿它做加载门槛，只有 SAVE_VERSION 才是。 */

    resIndex = g_save.resIndex;
    isFullscreen = g_save.fullscreen;
    fpsCapIndex = g_save.fpsCap;
    renderScaleIndex = g_save.scale;
    /* v28：Dynamic Res 以前完全没有进存档，重启后总是回到默认开。 */
    dynamicRes = hasDynamicResField ? g_save.dynamicRes : 1;
    if (dynamicRes < 0 || dynamicRes > 1) dynamicRes = 1;
    fovIndex = g_save.fov;
    vignetteOn = g_save.vignette;
    showFps = g_save.showFps;
    hudDetail = g_save.hudDetail;
    qteHint = g_save.qteHint;
    /* v28：**语言选项在启动时也必须恢复**。
       以前只有 ContinueRun 会写 g_lang —— 也就是说：
         · 改完语言、退出设置，当场生效（内存里已经改了）
         · 重启后 LoadSettings 读完 g_save.lang 却从不赋值 → 又变回英文
       这就是「语言选项没有保存」的直接原因。 */
    g_lang = (g_save.lang == LANG_ZH) ? LANG_ZH : LANG_EN;
    gammaValue = g_save.gamma;
    mouseSens = g_save.sens;
    carryGold = g_save.carryGold;
    upgBattery = g_save.upgBattery;
    upgSpeed = g_save.upgSpeed;
    upgQuiet = g_save.upgQuiet;
    upgSanity = g_save.upgSanity;
    upgEco = g_save.upgEco;
    upgRest = g_save.upgRest;
    bestLevel = g_save.bestLevel;
    volumeF = g_save.volume;
    /* v2.7：写成 !(x >= lo && x <= hi) 才能挡住 NaN（NaN 的所有比较都是假） */
    if (!(volumeF >= 0.0f && volumeF <= 1.0f)) volumeF = 0.8f;
    musicVolumeF = hasMusicField ? g_save.musicVolume : volumeF;
    if (!(musicVolumeF >= 0.0f && musicVolumeF <= 1.0f)) musicVolumeF = volumeF;
    if (carryGold < 0) carryGold = 0;
    if (upgBattery < 0 || upgBattery > UPG_MAX) upgBattery = 0;
    if (upgSpeed < 0 || upgSpeed > UPG_MAX) upgSpeed = 0;
    if (upgQuiet < 0 || upgQuiet > UPG_MAX) upgQuiet = 0;
    if (upgSanity < 0 || upgSanity > UPG_MAX) upgSanity = 0;
    if (upgEco < 0 || upgEco > UPG_MAX) upgEco = 0;
    if (upgRest < 0 || upgRest > UPG_MAX) upgRest = 0;

    /* 键位：逐项夹取。非 0 的值必须能在 optKeyVKs 里找到，否则还原成默认 ——
       防止损坏的存档把「移动」绑成一个不存在的键，玩家被锁死。 */
    {
        static const int defBindings[BINDING_SLOTS] = BINDINGS_DEFAULT;
        /* 存档里有 32 个槽，但运行时数组只有 ACT_COUNT 个 —— 只能拷前 ACT_COUNT 个 */
        for (int a = 0; a < ACT_COUNT; a++) {
            int vk = g_save.bindings[a];
            if (vk == 0) { g_bindings[a] = 0; continue; }   /* 允许显式无绑定 */
            int known = 0;
            for (int k = 0; k < OPT_KEY_COUNT; k++)
                if (optKeyVKs[k] == vk) { known = 1; break; }
            g_bindings[a] = known ? vk : defBindings[a];
        }
    }

    /* v2.7：resolutions[] 有 RES_COUNT=8 档，原来夹 0..4，
       选 1920x1080 / 2560x1440 重启就被重置回 800x600。 */
    if (resIndex < 0 || resIndex >= RES_COUNT) resIndex = 0;
    if (fpsCapIndex < 0 || fpsCapIndex > 4) fpsCapIndex = 1;
    if (renderScaleIndex < 0 || renderScaleIndex > 3) renderScaleIndex = 0;
    if (fovIndex < 0 || fovIndex > 4) fovIndex = 2;
    if (!(gammaValue >= 0.7f && gammaValue <= 1.6f)) gammaValue = 1.0f;
    if (!(mouseSens >= 0.2f && mouseSens <= 3.0f)) mouseSens = 1.0f;

    /* v7 新增：恢复成就数据（如果存档里有）。
       v6 **判据改成「成就段是否完整在文件里」** ——
       原来用 fileSize >= sizeof(SaveBlob)，等于「文件必须包含当前结构体的全部尾部字段」。
       于是每次往 SaveBlob 尾巴追加字段，都会把所有历史存档判成老档，
       把成就 / 技能树 / cores / 图鉴 / 无尽进度整段清零，哪怕那些字段其实读得到。
       现在用成就段的起始偏移做判据：只要文件覆盖到该段就正常读。 */
        size_t achOff = (size_t)((char*)&g_save.achUnlocked[0] - (char*)&g_save);
    if (fileSize >= achOff) {
        for (int i = 0; i < ACH_COUNT; i++) {
            g_achievements[i].unlocked = g_save.achUnlocked[i];
            g_achievements[i].progress = g_save.achProgress[i];
        }
        /* 1.8.0：技能树 / 核心 / keystone */
        for (int i = 0; i < 26; i++) {
            int r = g_save.skillRank[i];
            if (r < 0) r = 0;
            if (r > skillDefs[i].maxRank) r = skillDefs[i].maxRank;
            skillRank[i] = r;
        }
        cores = g_save.cores < 0 ? 0 : g_save.cores;
        activeKeystone = g_save.activeKeystone;
        if (activeKeystone < -1 || activeKeystone >= SKILL_COUNT) activeKeystone = -1;
        if (activeKeystone >= 0 && !skillDefs[activeKeystone].isKeystone) activeKeystone = -1;
        bestMainScore = g_save.bestMainScore < 0 ? 0 : g_save.bestMainScore;
        bestiarySeen = g_save.seenMonsters;

        /* v8 新增：恢复无尽模式进度 */
        bestEndlessLevel = g_save.bestEndlessLevel;
        bestEndlessScore = g_save.bestEndlessScore;
        isEndlessRun = g_save.isEndlessRunSaved;
    }
    else {  /* 老档没有成就数据 */
        /* 老存档没有成就数据，全置 0 */
        for (int i = 0; i < ACH_COUNT; i++) {
            g_achievements[i].unlocked = 0;
            g_achievements[i].progress = 0;
        }
        bestEndlessLevel = 0;
        bestEndlessScore = 0;
        isEndlessRun = 0;
    }

    /* v28：恢复「曾经通关」记录。
       v27 及更早没有这个字段 —— 用 FIRST EXIT 成就回填，老玩家升级后
       不会突然失去已经解锁的无尽模式（这个成就正好在成功撤离/通关时解锁）。 */
    if (hasGameClearedField) {
        gameCleared = g_save.gameClearedSave ? 1 : 0;
    } else {
        gameCleared = (g_achievements[1].unlocked != 0);
    }

    /* 文件里也记了它属于哪个槽；以文件为准，防止扫描选错 */
    if (g_save.saveSlot >= 0 && g_save.saveSlot < SAVE_SLOTS)
        g_saveSlot = g_save.saveSlot;

    for(int i=26;i<SKILL_COUNT;++i) { int r=g_save.extraSkillRanks[i-26]; skillRank[i]=(r>=0 && r<=3)?r:0; }
    SkillInitOnce();
    BestiaryLoad(g_save.bestiaryKnowledge);
    RestoreBankFromSave();
    { int da[15] = {g_save.dailyGoalId, g_save.dailyGoalParam, g_save.dailyProgress,
        g_save.dailyDone, g_save.dailyStreak, (int)g_save.dailyQuestDate,
        (int)g_save.dailyLastCompletedDate};
      for (int j=0;j<8;++j) da[7+j]=g_save.dailySamples[j];
      DailyLoadFrom(da); }
    return g_save.hasRun;
}

/* 把存档里的进度灌回运行时 */
void ContinueRun(void) {
    /* v28：**读档前必须先清模式标志**。
       原来越野：每日/战役 → 暂停 → 主菜单（标志还留着）→ 教程 → 退出教程
       （trainingMode 留着）→ Continue → 读出来的挑战地图上，
       trainingMode 仍是 1：道具无限、出口判定被跳过，于是卡在关卡里。
       现在训练/教程/每日/战役/无尽全部清零，再由存档决定这一局是什么模式。 */
    if (!g_save.hasRun) {
        /* 存档里没有可继续的进度（例如只写过设置的档）：别造一局空世界。 */
        hasSave = 0;
        runInProgress = 0;
        return;
    }
    ResetRunModeFlags();
    { extern int knockCountRun,throwHitCountRun,echoCellsTotalRun,dailyEscapeCount;
      extern int floorsNoSanityLoss,wardenCleanFloors,fullHideDone;
      extern float doubleHuntSeconds;
      knockCountRun=g_save.knockRunSave; throwHitCountRun=g_save.throwHitRunSave;
      echoCellsTotalRun=g_save.echoRunSave; dailyEscapeCount=g_save.dailyEscapeRunSave;
      floorsNoSanityLoss=g_save.cleanFloorsSave; wardenCleanFloors=g_save.wardenFloorsSave;
      fullHideDone=g_save.fullHideSave; doubleHuntSeconds=g_save.doubleHuntSave; }
    { extern int shopRefreshFloor,shopRefreshUsed,contractKind,shopContractUsed;
      shopRefreshFloor=g_save.shopRefreshFloorSave; shopRefreshUsed=g_save.shopRefreshUsedSave;
      contractKind=g_save.contractKindSave; shopContractUsed=g_save.contractLockedSave;
      ThreatReset(); ThreatAdd(g_save.threatSave); }
    currentLevel = g_save.level;
    /* v3.0：**读档必须夹取** ——
       外部改过的档可能带 NaN：sanity=NaN 时 `sanity <= 0` 永不成立（死不掉），
       pos=NaN 时 Blocked() 失效（可以穿墙）。 */
    if (currentLevel < 1) currentLevel = 1;
    if (currentLevel > 999) currentLevel = 999;
    sanity = g_save.sanity;
    /* v3.0：NaN / 越界一律拉回合法区间 */
    if (!(sanity == sanity)) sanity = 100.0f;
    if (sanity < 0.0f) sanity = 0.0f;
    if (sanity > 100.0f) sanity = 100.0f;
    battery = g_save.battery;
    if (!(battery == battery)) battery = 100.0f;
    if (battery < 0.0f) battery = 0.0f;
    if (battery > 100.0f) battery = 100.0f;
    stamina = g_save.stamina;
    if (!(stamina == stamina)) stamina = 100.0f;
    if (stamina < 0.0f) stamina = 0.0f;
    if (stamina > 130.0f) stamina = 130.0f;
    energy = g_save.energy;
    benchOwned = g_save.benchOwnedSave;
    hideSpotCount = g_save.hideSpotSaveCount;
    if (hideSpotCount < 0) hideSpotCount = 0;
    if (hideSpotCount > 6) hideSpotCount = 6;
    for (int i = 0; i < 6; i++) hideSpots[i] = g_save.hideSpotSave[i];
    hiddenIn = g_save.hiddenInSave;
    /* v3.3：恢复安全屋每层限一次的标记（老档没有这几个字段，读到 0 也安全） */
    {
        extern void ShopSetSafeLv(int, int, int);
        ShopSetSafeLv(g_save.safeHealLvSave,
                      g_save.safeChargeLvSave,
                      g_save.safeStaminaLvSave);
    }
    /* v3.3：恢复跨局累计的成就计数器（老档读到 0，安全） */
    {
        extern int floorsNoLight, floorsFlawless, floorsPacifist, floorsFast;
        extern int mapsFullyExplored, fusesCollectedRun, hideCount, totalThrown;
        extern float crankSecondsTotal, lowSanitySeconds;
        extern int locksOpened;
        floorsNoLight      = g_save.achFloorsNoLight;
        floorsFlawless     = g_save.achFloorsFlawless;
        floorsPacifist     = g_save.achFloorsPacifist;
        floorsFast         = g_save.achFloorsFast;
        mapsFullyExplored  = g_save.achMapsExplored;
        fusesCollectedRun  = g_save.achFusesRun;
        locksOpened        = g_save.achLocksOpened;
        hideCount          = g_save.achHideCount;
        totalThrown        = g_save.achTotalThrown;
        crankSecondsTotal  = g_save.achCrankSecondsTotal;
        lowSanitySeconds   = g_save.achLowSanitySeconds;
    }
    /* v3.4/v6 银行：统一走 RestoreBankFromSave，只有一份逻辑 */
    for(int i=26;i<SKILL_COUNT;++i) { int r=g_save.extraSkillRanks[i-26]; skillRank[i]=(r>=0 && r<=3)?r:0; }
    SkillInitOnce();
    BestiaryLoad(g_save.bestiaryKnowledge);
    RestoreBankFromSave();
    if (hiddenIn >= hideSpotCount) hiddenIn = -1;
    RunUpgLoadFromSave(g_save.runUpgSave);
    { extern void RunUpgLoadExtra(int);
      RunUpgLoadExtra(g_save.runUpgMinimapSave); }   /* v2.3：老存档没有这个字段（读出 0）→ 下面补满 */
    if (!(energy > 0.0f)) energy = ENERGY_MAX;
    flashlightOn = g_save.flashlightOn;
    /* v8：模式规则优先于存档里的开关状态 ——
       每日“不能开手电”/ SALVAGE keystone 读档后必须保持关闭；
       战役第 1 关“关不掉”读档后必须保持开启。 */
    {
        extern int DailyForbidLight(void);
        extern int CampaignFlashlightLocked(void);
        if (DailyForbidLight() || HasKeystone(SK_SALVAGE)) flashlightOn = 0;
        else if (CampaignFlashlightLocked()) flashlightOn = 1;
    }
    /* v3.0：坐标必须夹在地图内且非 NaN */
    #define CLAMP_POS(v, wrap) do { \
        if (!((v) == (v)) || (v) < 1.0f || (v) > (float)(wrap) - 1.0f) (v) = 1.5f; \
    } while (0)
    posX = g_save.posX; posY = g_save.posY;
    CLAMP_POS(posX, MAP_WIDTH);
    CLAMP_POS(posY, MAP_HEIGHT);
    exitX = g_save.exitX; exitY = g_save.exitY;
    CLAMP_POS(exitX, MAP_WIDTH);
    CLAMP_POS(exitY, MAP_HEIGHT);
    #undef CLAMP_POS
    dirX = g_save.dirX; dirY = g_save.dirY;
    exitX = g_save.exitX; exitY = g_save.exitY;
    fusesHeld = g_save.fusesHeld;
    fusesNeeded = g_save.fusesNeeded;
    breakerOpen = g_save.breakerOpen;
    breaker.active = g_save.breakerActive;
    breaker.x = g_save.breakerX;
    breaker.y = g_save.breakerY;
    searchedCount = g_save.searchedCount;
    runTime = g_save.runTime;
    containerCount = g_save.nContainers;
    /* 防呆：被改坏的存档可能写出越界的容器数。
       负数 = 完全无效，清零；超出上限 = 截断到 MAX_CONTAINERS（保住已有进度），
       不要直接清零 —— 清零会让整层的箱子全没。 */
    if (containerCount < 0) containerCount = 0;
    if (containerCount > MAX_CONTAINERS) containerCount = MAX_CONTAINERS;
    for (int i = 0; i < MAX_CONTAINERS; i++) containers[i] = g_save.cont[i];
    memcpy(worldMap, g_save.worldMap, sizeof(worldMap));
    memcpy(explored, g_save.explored, sizeof(explored));

    /* v4 新增：安全屋 / 投掷物 / 词条 / 主题 */
    /* v3.0：**道具数量必须夹取** ——
       玩家实测「道具栏突然变成 MIRROR X99 / FLARE X99 / BOTTLE X99」，
       就是因为这里只有直接赋值：坏存档/越界写入的值会原样进 HUD。
       上限给 99（正常玩法永远到不了），下限 0。 */
    /* v3.0：上限取**实际玩法的容量**，不是 99 ——
       上一版夹到 99，结果存档里的垃圾值被夹成 X99 直接显示出来，
       玩家看到的就是「道具栏变成 X99」。
       正常容量：瓶子/照明弹 8，其余稀有道具 4。 */
    #define CLAMP_ITEM(v)     do { if ((v) < 0) (v) = 0; if ((v) > 8) (v) = 8; } while (0)
    #define CLAMP_ITEM_RARE(v) do { if ((v) < 0) (v) = 0; if ((v) > 4) (v) = 4; } while (0)
    flares = g_save.flares;      CLAMP_ITEM(flares);
    bottles = g_save.bottles;    CLAMP_ITEM(bottles);
    /* v13：v2.0 新系统（不读回来 = 中途退出丢光遗物/Boss 状态/每日战役） */
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = g_save.relicOwned[i];
    relicTotal = g_save.relicTotal;
    /* v8: boss 字段已移除。bossLevel 恒为 0，下面两行本就永不成立。 */
    dailyMode = g_save.dailyMode;
    dailySeed = g_save.dailySeed;
    campaignMode = g_save.campaignMode;
    campaignStage = g_save.campaignStage;
    firePots = g_save.firePots;   CLAMP_ITEM_RARE(firePots);
    gluePots = g_save.gluePots;   CLAMP_ITEM_RARE(gluePots);
    mirrors = g_save.mirrors;   CLAMP_ITEM_RARE(mirrors);
    bankStreak = g_save.bankStreak;
    modBatteryMul = g_save.modBatteryMul;
    if (!(modBatteryMul > 0.0f)) modBatteryMul = 1.0f;
    modShardMul = g_save.modShardMul;
    if (!(modShardMul > 0.0f)) modShardMul = 1.0f;
    modNoiseMul = g_save.modNoiseMul;
    if (!(modNoiseMul > 0.0f)) modNoiseMul = 1.0f;
    /* 读档后 Boss 状态要重建：已清的 Boss 不能再站起来 */
    if (flares < 0) flares = 0;
    if (bottles < 0) bottles = 0;
    /* v6 **旧安全区已删除** ——
       存档里 safeActive 还是 1 的老档，读回来会让旧安全区重新出现
       （用户：「有时候会冒出旧版本的安全区 / 旧道具旧模型」）。
       这里无条件清零，不管存档里写的是什么。 */
    safeRoom.active = 0;
    safeRoom.x = g_save.safeX;
    safeRoom.y = g_save.safeY;
    levelMod = g_save.levelMod;
    if (levelMod < -1 || levelMod >= MOD_COUNT) levelMod = -1;
    modSanityMul = g_save.modSanityMul;
    modSpeedMul = g_save.modSpeedMul;
    /* v2.7：NaN-safe。存档里混进 NaN 时 `<= 0` 判不出来，
       它会一路乘进理智流失，sanity 变 NaN → `sanity <= 0` 永不成立 → 死不掉。 */
    if (!(modSanityMul > 0.0f)) modSanityMul = 1.0f;
    if (!(modSpeedMul > 0.0f)) modSpeedMul = 1.0f;
    themeId = g_save.themeId;
    if (themeId < 0 || themeId >= THEME_COUNT) themeId = 0;
    for (int i = 0; i < MAX_THROWN; i++) thrown[i] = g_save.thr[i];

    /* v9：把新存的进度接回来（顺手夹一下，坏存档不至于把数量弄成负数） */
    bricks      = g_save.bricks;      CLAMP_ITEM_RARE(bricks);
    acids       = g_save.acids;       CLAMP_ITEM_RARE(acids);
    smokeBombs  = g_save.smokeBombs;  CLAMP_ITEM_RARE(smokeBombs);
    carryGold = g_save.carryGold < 0 ? 0 : g_save.carryGold;
    if (carryGold > 999999) carryGold = 0;      /* 明显是坏档 */
    runSkillMask = g_save.runSkillMask;
    /* v3.1：界面固定英文。老存档里的 lang 字段仍保留，但不再覆盖运行时语言；
       否则中文存档一进来整页菜单/提示还是中文。 */
    g_lang = (g_save.lang==LANG_ZH) ? LANG_ZH : LANG_EN;
    /* v11：恢复武器局外成长 */
    for (int i = 0; i < 3; i++) {
        weaponRank[i] = (g_save.weaponRank[i] >= 0 && g_save.weaponRank[i] <= 3) ? g_save.weaponRank[i] : 0;
        weaponXp[i] = (g_save.weaponXp[i] >= 0 && g_save.weaponXp[i] < 1000) ? g_save.weaponXp[i] : 0;
        weaponAttach[i][0] = (g_save.weaponAttach[i][0] >= -1 && g_save.weaponAttach[i][0] < ATTACH_COUNT)
                             ? g_save.weaponAttach[i][0] : -1;
        weaponAttach[i][1] = (g_save.weaponAttach[i][1] >= -1 && g_save.weaponAttach[i][1] < ATTACH_COUNT)
                             ? g_save.weaponAttach[i][1] : -1;
    }
    for (int i = 0; i < ATTACH_COUNT; i++)
        attachOwned[i] = (g_save.attachOwned[i] > 0) ? g_save.attachOwned[i] : 0;
    if (g_save.curWeapon >= 0 && g_save.curWeapon < WPN_COUNT) curWeapon = g_save.curWeapon;
    runSkillCount = 0;
    for (unsigned int b = 0; b < 32u; b++)
        if (runSkillMask & (1u << b)) runSkillCount++;
    runUltimate = (g_save.runUltimate >= 0 && g_save.runUltimate < INRUN_COUNT)
                  ? g_save.runUltimate : -1;

    ApplyFov();
    GenerateThemeTextures(themeId);   /* 主题变了，墙面/地板贴图要跟着重建 */
    SpawnMonster();
    SpawnLightBeast();
    isEndlessRun = g_save.isEndlessRunSaved;  /* 恢复无尽模式标记 */
    /* 读档不重掷跳脸虫：本关不会突然蹦出来。
       触发器留成 -1，等下一层进关时再掷。 */
    jumperTrigger = -1.0f;
    jumperPhase = JMP_IDLE;
    jumperTimer = 0.0f;
    parasite.active = 0;
    runInProgress = 1;
    /* v3.0：读档前先关掉锁盘 —— 否则 lockActive 残留，
       ui.c 无条件画锁盘会盖住游戏画面，且 WM_LBUTTONDOWN 先把点击全吞掉。 */
    if (lockActive) CloseLockOverlay();
    currentState = STATE_PLAYING;
    isMapOpen = 0;
    mapFullScreen = 0;
    flashTime = 0.0f;
    healFlash = 0.0f;
    resetInputState = 1;   /* 读档回游戏：主菜单点击的左键不能带进游戏 */
    ClearFx();
    for (int i = 0; i < MAX_SHOTS; i++) shot[i].active = 0;
    ForceCursor(0);
    firstMouseMove = 1;
    uiDirty = 1;

    /* 恢复成就追踪变量 */
    bottlesKilled = 0;
    faceDodged = 0;
    crankTimeTotal = 0;
    fusesCollectedRun = 0;
    mapsFullyExplored = 0;
    tookDamage = 0;
    flashlightOffLevel = 0;
    sprintedThisLevel = 0;
    #undef CLAMP_ITEM
    #undef CLAMP_ITEM_RARE
}

void DeleteSave(void) {
    char p[MAX_PATH];
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        const char* cand = SavePathCand(i, p, sizeof(p));
        DeleteFileA(cand);
        /* v29：**连 .bak / .tmp 一起删** ——
           LoadSettings 在主档读不到时会回退 .bak；
           只删主档的话，「新开一局 → 首次存档前退出/崩溃」
           下次启动就会从 .bak 把旧局复活，等于删档没删干净。 */
        {
            char side[MAX_PATH + 8];
            snprintf(side, sizeof(side), "%s.bak", cand);
            DeleteFileA(side);
            snprintf(side, sizeof(side), "%s.tmp", cand);
            DeleteFileA(side);
        }
    }
    g_save.hasRun = 0;
    hasSave = 0;
    runInProgress = 0;
}

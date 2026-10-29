/*
 * Canity — 存档层实现：SaveBlob 读写 / 版本迁移 / 继续游戏
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "save.h"
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
    char b[MAX_PATH];
    snprintf(b, sizeof(b), "%s.bak", p);
    CopyFileA(p, b, FALSE);
}

static const char* SavePathCand(int i, char* buf, size_t n) {
    buf[0] = 0;
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
        snprintf(buf, n, "%s\\canity%d.sav", dir, g_saveSlot);
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
    char tmp[MAX_PATH];
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

void SaveGame(void) {
    /* 训练场不是正式进度：写进去的话 Continue 会读出一张走廊图 */
    if (trainingMode) return;
    memset(&g_save, 0, sizeof(g_save));
    g_save.magic = SAVE_MAGIC;
    g_save.version = SAVE_VERSION;
    g_save.gameVersion = CANITY_VERSION_NUM;

    g_save.resIndex = resIndex;
    g_save.fullscreen = isFullscreen;
    g_save.fpsCap = fpsCapIndex;
    g_save.scale = renderScaleIndex;
    g_save.fov = fovIndex;
    g_save.vignette = vignetteOn;
    g_save.showFps = showFps;
    g_save.hudDetail = hudDetail;
    g_save.qteHint = qteHint;
    g_save.gamma = gammaValue;
    g_save.sens = mouseSens;
    g_save.shards = shards;
    g_save.upgBattery = upgBattery;
    g_save.upgSpeed = upgSpeed;
    g_save.upgQuiet = upgQuiet;
    g_save.upgSanity = upgSanity;
    g_save.upgEco = upgEco;
    g_save.upgRest = upgRest;
    g_save.bestLevel = bestLevel;
    g_save.volume = volumeF;
    for (int a = 0; a < BINDING_SLOTS; a++)
        g_save.bindings[a] = (a < ACT_COUNT) ? g_bindings[a] : 0;

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
    for (int i = 0; i < 4; i++) g_save.runUpgSave[i] = RunUpgLevel(i);   /* v2.3 */
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
    /* v9：投掷物 / 待上缴碎片 / 局内技能，以前完全没存 */
    g_save.bricks = bricks;
    g_save.acids = acids;
    g_save.smokeBombs = smokeBombs;
    g_save.pendingShards = pendingShards;
    g_save.runSkillMask = runSkillMask;
    g_save.lang = g_lang;
    g_save.saveSlot = g_saveSlot;
    /* v13：v2.0 新系统 */
    for (int i = 0; i < RELIC_COUNT; i++) g_save.relicOwned[i] = relicOwned[i];
    g_save.relicTotal = relicTotal;
    g_save.bossLevel = bossLevel;
    g_save.bossMaxHp = bossMaxHp;
    g_save.bossDefeated = bossDefeated;
    g_save.dailyMode = dailyMode;
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

    for (int i = 0; i < SKILL_COUNT; i++) g_save.skillRank[i] = skillRank[i];
    g_save.cores = cores;
    g_save.activeKeystone = activeKeystone;
    g_save.bestMainScore = bestMainScore;
    g_save.seenMonsters = bestiarySeen;

    /* v8 新增：无尽模式进度 */
    g_save.bestEndlessLevel = bestEndlessLevel;
    g_save.bestEndlessScore = bestEndlessScore;
    g_save.isEndlessRunSaved = isEndlessRun;

    /* 逐个候选试写，第一个成功的就用它 */
    char p[MAX_PATH];
    int firstErr = 0;
    for (int i = 0; i < SAVE_CAND_COUNT; i++) {
        SavePathCand(i, p, sizeof(p));
        int err = WriteWholeFile(p, &g_save, sizeof(g_save));
        if (err == 0) {
            g_saveState = 1;
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

/* 只读设置（启动时），返回是否还有可继续的进度 */
int LoadSettings(void) {
    char p[MAX_PATH];
    memset(&g_save, 0, sizeof(g_save));
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
    if (fs <= 0) return 0;
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
       策略：技能等级全清、把已投入的碎片和核心按旧价退回，其余进度原样保留。
       旧价：普通节点 {100,500,900}、keystone 2000 + 1 核、跨支 1400 + 2 核。 */
    /* v2.2 修复：v13→v14 的"迁移"是错的 —— 它把 v13 的文件字节
       原地当成 v14 的 SaveBlob 解释，而 v14 的 skillRank[15]→[20]
       多出 20 字节，导致 skillRank 之后的所有字段整体错位 5 个 int：
       成就被清零、核心被读成随机值、武器熟练度串到别的字段。

       真正安全的做法：不认识的版本**不解释**，把旧档改名归档，
       当作"没有存档"处理。玩家的数据一定还在盘上，可以回滚，
       而不是被一个错误的迁移静默毁掉。 */
    if (g_save.version != SAVE_VERSION) {
        char oldp[MAX_PATH], arc[MAX_PATH];
        SavePathCand(0, oldp, sizeof(oldp));
        if (oldp[0]) {
            snprintf(arc, sizeof(arc), "%s.v%d.bak", oldp, g_save.version);
            MoveFileExA(oldp, arc, MOVEFILE_REPLACE_EXISTING);
        }
        return 0;
    }
    if (g_save.version != SAVE_VERSION) return 0;
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
    fovIndex = g_save.fov;
    vignetteOn = g_save.vignette;
    showFps = g_save.showFps;
    hudDetail = g_save.hudDetail;
    qteHint = g_save.qteHint;
    gammaValue = g_save.gamma;
    mouseSens = g_save.sens;
    shards = g_save.shards;
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
    if (shards < 0) shards = 0;
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

    /* v7 新增：恢复成就数据（如果存档里有） */
    if (fileSize >= sizeof(SaveBlob)) {
        for (int i = 0; i < ACH_COUNT; i++) {
            g_achievements[i].unlocked = g_save.achUnlocked[i];
            g_achievements[i].progress = g_save.achProgress[i];
        }
        /* 1.8.0：技能树 / 核心 / keystone */
        for (int i = 0; i < SKILL_COUNT; i++) {
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
    } else {
        /* 老存档没有成就数据，全置 0 */
        for (int i = 0; i < ACH_COUNT; i++) {
            g_achievements[i].unlocked = 0;
            g_achievements[i].progress = 0;
        }
        bestEndlessLevel = 0;
        bestEndlessScore = 0;
        isEndlessRun = 0;
    }

    return g_save.hasRun;
}

/* 把存档里的进度灌回运行时 */
void ContinueRun(void) {
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
    if (hiddenIn >= hideSpotCount) hiddenIn = -1;
    RunUpgLoadFromSave(g_save.runUpgSave);   /* v2.3：老存档没有这个字段（读出 0）→ 下面补满 */
    if (!(energy > 0.0f)) energy = ENERGY_MAX;
    flashlightOn = g_save.flashlightOn;
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
    flares = g_save.flares;
    bottles = g_save.bottles;
    /* v13：v2.0 新系统（不读回来 = 中途退出丢光遗物/Boss 状态/每日战役） */
    for (int i = 0; i < RELIC_COUNT; i++) relicOwned[i] = g_save.relicOwned[i];
    relicTotal = g_save.relicTotal;
    bossLevel = g_save.bossLevel;
    bossMaxHp = g_save.bossMaxHp;
    bossDefeated = g_save.bossDefeated;
    dailyMode = g_save.dailyMode;
    dailySeed = g_save.dailySeed;
    campaignMode = g_save.campaignMode;
    campaignStage = g_save.campaignStage;
    firePots = g_save.firePots;
    gluePots = g_save.gluePots;
    mirrors = g_save.mirrors;
    bankStreak = g_save.bankStreak;
    modBatteryMul = g_save.modBatteryMul;
    if (!(modBatteryMul > 0.0f)) modBatteryMul = 1.0f;
    modShardMul = g_save.modShardMul;
    if (!(modShardMul > 0.0f)) modShardMul = 1.0f;
    modNoiseMul = g_save.modNoiseMul;
    if (!(modNoiseMul > 0.0f)) modNoiseMul = 1.0f;
    /* 读档后 Boss 状态要重建：已清的 Boss 不能再站起来 */
    if (bossDefeated) monster.active = 0;
    else if (bossLevel && monster.active) monster.hp = bossMaxHp;
    if (flares < 0) flares = 0;
    if (bottles < 0) bottles = 0;
    safeRoom.active = g_save.safeActive;
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
    bricks      = g_save.bricks      < 0 ? 0 : g_save.bricks;
    acids       = g_save.acids       < 0 ? 0 : g_save.acids;
    smokeBombs  = g_save.smokeBombs  < 0 ? 0 : g_save.smokeBombs;
    pendingShards = g_save.pendingShards < 0 ? 0 : g_save.pendingShards;
    if (pendingShards > 999999) pendingShards = 0;      /* 明显是坏档 */
    runSkillMask = g_save.runSkillMask;
    /* v3.0：**存档槽必须读回来** ——
       原来 g_saveSlot 只在启动时置 0，LoadSettings 只按 0 号槽找文件，
       于是「在 2 号槽玩的档」重启后被静默切回 0 号槽，
       下一次自动存档就把 0 号槽覆盖掉。 */
    if (g_save.saveSlot >= 0 && g_save.saveSlot < 8) g_saveSlot = g_save.saveSlot;
    g_lang = (g_save.lang >= 0 && g_save.lang < LANG_COUNT) ? g_save.lang : LANG_EN;
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
}

void DeleteSave(void) {
    char p[MAX_PATH];
    for (int i = 0; i < SAVE_CAND_COUNT; i++) DeleteFileA(SavePathCand(i, p, sizeof(p)));
    g_save.hasRun = 0;
    hasSave = 0;
    runInProgress = 0;
}

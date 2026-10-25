/*
 * Canity — 存档层实现：SaveBlob 读写 / 版本迁移 / 继续游戏
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "common.h"
#include "save.h"
#include "game.h"
#include "entity.h"
#include "render.h"


static SaveBlob g_save;

void SaveGame(void) {
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
    for (int a = 0; a < ACT_COUNT; a++) g_save.bindings[a] = g_bindings[a];

    g_save.hasRun = runInProgress;
    g_save.level = currentLevel;
    g_save.sanity = sanity;
    g_save.battery = battery;
    g_save.stamina = stamina;
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

    FILE* f = fopen(SAVE_FILE, "wb");
    if (!f) return;
    fwrite(&g_save, sizeof(g_save), 1, f);
    fclose(f);
}

/* 只读设置（启动时），返回是否还有可继续的进度 */
int LoadSettings(void) {
    FILE* f = fopen(SAVE_FILE, "rb");
    if (!f) return 0;
    memset(&g_save, 0, sizeof(g_save));
    /* 按文件的**实际字节数**读，而不是 sizeof(SaveBlob) ——
       这样老存档（结构更短）也能读进来，新字段留在 0。 */
    size_t fileSize = 0;
    fseek(f, 0, SEEK_END);
    long fs = ftell(f);
    if (fs > 0) fileSize = (size_t)fs;
    fseek(f, 0, SEEK_SET);
    size_t want = (fileSize < sizeof(g_save)) ? fileSize : sizeof(g_save);
    size_t got = fread(&g_save, 1, want, f);
    fclose(f);
    if (!got || g_save.magic != SAVE_MAGIC) return 0;

    /* 存档**格式**版本：接受 v4 / v5（老结构）与 v6（当前结构）。
       老存档按文件长度就能认出来 —— 新增字段永远追加在结构末尾，
       老文件的字节数正好少一块。这里**不**做「版本不符就整份丢弃」：
       键位与显示偏好丢了没有意义，而丢弃整份存档会让玩家进度全没。 */
    if (g_save.version != SAVE_VERSION && g_save.version != 4 && g_save.version != 5) return 0;
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
    if (volumeF < 0.0f || volumeF > 1.0f) volumeF = 0.8f;
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
        static const int defBindings[ACT_COUNT] = BINDINGS_DEFAULT;
        for (int a = 0; a < ACT_COUNT; a++) {
            int vk = g_save.bindings[a];
            if (vk == 0) { g_bindings[a] = 0; continue; }   /* 允许显式无绑定 */
            int known = 0;
            for (int k = 0; k < OPT_KEY_COUNT; k++)
                if (optKeyVKs[k] == vk) { known = 1; break; }
            g_bindings[a] = known ? vk : defBindings[a];
        }
    }

    if (resIndex < 0 || resIndex > 4) resIndex = 0;
    if (fpsCapIndex < 0 || fpsCapIndex > 4) fpsCapIndex = 1;
    if (renderScaleIndex < 0 || renderScaleIndex > 3) renderScaleIndex = 0;
    if (fovIndex < 0 || fovIndex > 4) fovIndex = 2;
    if (gammaValue < 0.7f || gammaValue > 1.6f) gammaValue = 1.0f;
    if (mouseSens < 0.2f || mouseSens > 3.0f) mouseSens = 1.0f;

    /* v7 新增：恢复成就数据（如果存档里有） */
    if (fileSize >= sizeof(SaveBlob)) {
        for (int i = 0; i < ACH_COUNT; i++) {
            g_achievements[i].unlocked = g_save.achUnlocked[i];
            g_achievements[i].progress = g_save.achProgress[i];
        }
    } else {
        /* 老存档没有成就数据，全置 0 */
        for (int i = 0; i < ACH_COUNT; i++) {
            g_achievements[i].unlocked = 0;
            g_achievements[i].progress = 0;
        }
    }

    return g_save.hasRun;
}

/* 把存档里的进度灌回运行时 */
void ContinueRun(void) {
    currentLevel = g_save.level;
    sanity = g_save.sanity;
    battery = g_save.battery;
    stamina = g_save.stamina;
    flashlightOn = g_save.flashlightOn;
    posX = g_save.posX; posY = g_save.posY;
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
    if (containerCount < 0 || containerCount > MAX_CONTAINERS) containerCount = 0;
    for (int i = 0; i < MAX_CONTAINERS; i++) containers[i] = g_save.cont[i];
    memcpy(worldMap, g_save.worldMap, sizeof(worldMap));
    memcpy(explored, g_save.explored, sizeof(explored));

    /* v4 新增：安全屋 / 投掷物 / 词条 / 主题 */
    flares = g_save.flares;
    bottles = g_save.bottles;
    if (flares < 0) flares = 0;
    if (bottles < 0) bottles = 0;
    safeRoom.active = g_save.safeActive;
    safeRoom.x = g_save.safeX;
    safeRoom.y = g_save.safeY;
    levelMod = g_save.levelMod;
    if (levelMod < -1 || levelMod >= MOD_COUNT) levelMod = -1;
    modSanityMul = g_save.modSanityMul;
    modSpeedMul = g_save.modSpeedMul;
    if (modSanityMul <= 0.0f) modSanityMul = 1.0f;
    if (modSpeedMul <= 0.0f) modSpeedMul = 1.0f;
    themeId = g_save.themeId;
    if (themeId < 0 || themeId > 3) themeId = 0;
    for (int i = 0; i < MAX_THROWN; i++) thrown[i] = g_save.thr[i];

    ApplyFov();
    GenerateThemeTextures(themeId);   /* 主题变了，墙面/地板贴图要跟着重建 */
    SpawnMonster();
    SpawnLightBeast();
    /* 读档不重掷跳脸虫：本关不会突然蹦出来。
       触发器留成 -1，等下一层进关时再掷。 */
    jumperTrigger = -1.0f;
    jumperPhase = JMP_IDLE;
    jumperTimer = 0.0f;
    parasite.active = 0;
    runInProgress = 1;
    currentState = STATE_PLAYING;
    isMapOpen = 0;
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
    remove(SAVE_FILE);
    g_save.hasRun = 0;
    hasSave = 0;
    runInProgress = 0;
}

/*
 * Canity — 单局遥测（v6）
 *
 * 每局结束写一行 CSV，用来**实测**经济，而不是靠推算。
 *
 * v6 第 46 轮修正
 * ──────────────
 * 原来 `fopen("canity_runs.csv")` 用的是**当前工作目录**。
 * 玩家如果从别的目录启动游戏（比如快捷方式、聊天软件转发），
 * CSV 会写到那个目录去 —— 玩完找不到文件。
 *
 * 现在改成**exe 所在目录**，跟游戏本体放一起，永远找得到。
 */
#include "common.h"
#include "telemetry.h"

#define TELEM_FILE  "canity_runs.csv"
#define TELEM_CAP   500

static int s_counted = -1;

/* 拼出 "exe 所在目录\canity_runs.csv" */
static void buildPath(char* out, int cap) {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (n == 0 || n >= sizeof(exe)) {
        strncpy(out, TELEM_FILE, (size_t)cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    /* 砍掉 exe 文件名 */
    char* slash = strrchr(exe, '\\');
    if (slash) slash[1] = '\0';
    else exe[0] = '\0';
    snprintf(out, (size_t)cap, "%s%s", exe, TELEM_FILE);
}

static int countLines(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    int n = 0;
    int ch;
    while ((ch = fgetc(f)) != EOF) if (ch == '\n') n++;
    fclose(f);
    return n;
}

void TelemetryRunEnd(int level, int banked, int carried,
                     int survived, float seconds, int caught) {
    char path[MAX_PATH + 32];
    buildPath(path, sizeof(path));

    if (s_counted < 0) s_counted = countLines(path);

    int fresh = (s_counted == 0);
    FILE* f = fopen(path, fresh ? "w" : "a");
    if (!f) return;

    if (fresh) {
        fprintf(f, "level,banked,carried,survived,seconds,caught\n");
    }

    fprintf(f, "%d,%d,%d,%d,%.1f,%d\n",
            level, banked, carried, survived, (double)seconds, caught);
    fclose(f);

    s_counted++;

    /* 超过上限整体轮换，保留最近一半 */
    if (s_counted > TELEM_CAP) {
        char tmp[MAX_PATH + 40];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        FILE* in = fopen(path, "r");
        FILE* out = fopen(tmp, "w");
        if (in && out) {
            char line[256];
            int keep = s_counted - TELEM_CAP / 2;
            int i = 0;
            while (fgets(line, sizeof(line), in)) {
                if (i == 0 || i >= keep) fputs(line, out);
                i++;
            }
            s_counted = TELEM_CAP / 2;
        }
        if (in) fclose(in);
        if (out) fclose(out);
        remove(path);
        rename(tmp, path);
    }
}

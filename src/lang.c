#include <stdio.h>
#include <string.h>
#include "lang.h"

int g_lang = LANG_EN;

const char* LangName(int id) {
    return (id == LANG_ZH) ? "中文" : "ENGLISH";
}

/* v6 第 38 轮：设置菜单的 Language 行直接绑 g_lang（列表循环），
   所以不经过这个函数。保留它给未来的快捷键 / 控制台用。 */
void LangToggle(void) {
    g_lang = (g_lang + 1) % LANG_COUNT;
}

/* ------------------------------------------------------------------ */
/* 绘制层翻译表                                                         */
/*   在 DrawTextWin / TextWidthWin 里统一替换，调用点一行都不用改。      */
/*   找不到精确条目时按前缀做模式翻译（动态字符串如 "GOLD 123"）。      */
/* ------------------------------------------------------------------ */
typedef struct { const char* en; const char* zh; } TrEntry;

#include "lang_table.inc"

#define KTR_N ((int)(sizeof(kTrTable) / sizeof(kTrTable[0])))

const char* Transl(const char* en) {
    if (!en) return en;
    if (g_lang != LANG_ZH) return en;

    for (int i = 0; i < KTR_N; i++) {
        if (strcmp(en, kTrTable[i].en) == 0) return kTrTable[i].zh;
    }

    /* 动态字符串：按前缀造 */
    static char buf[160];
    if (!strncmp(en, "GOLD ", 7))  { snprintf(buf, sizeof(buf), "金币 %s", en + 7); return buf; }
    if (!strncmp(en, "CORES ", 6))   { snprintf(buf, sizeof(buf), "核心 %s", en + 6); return buf; }
    if (!strncmp(en, "BEST LEVEL ", 11)) { snprintf(buf, sizeof(buf), "最高层 %s", en + 11); return buf; }
    if (!strncmp(en, "PENDING ", 8)) { snprintf(buf, sizeof(buf), "待上缴 %s", en + 8); return buf; }
    if (!strncmp(en, "LEVEL ", 6))   { snprintf(buf, sizeof(buf), "第 %s 层", en + 6); return buf; }
    if (!strncmp(en, "TIME ", 5))    { snprintf(buf, sizeof(buf), "时间 %s", en + 5); return buf; }
    if (!strncmp(en, "FUSES ", 6))   { snprintf(buf, sizeof(buf), "保险丝 %s", en + 6); return buf; }
    if (!strncmp(en, "SCORE ", 6))   { snprintf(buf, sizeof(buf), "得分 %s", en + 6); return buf; }
    if (!strncmp(en, "RUN FAILED", 10)) return "本局失败";
    if (!strncmp(en, "KNIFE", 5))    { snprintf(buf, sizeof(buf), "锈刃 %s", en + 5); return buf; }
    {   /* "100 GOLD" -> "100 金币" */
        int n = 0; char tail[24];
        if (sscanf(en, "%d GOLD%23s", &n, tail) == 1) { snprintf(buf, sizeof(buf), "%d 金币", n); return buf; }
        if (sscanf(en, "%d CORES%23s", &n, tail) == 1)  { snprintf(buf, sizeof(buf), "%d 核心", n); return buf; }
    }

    return en;
}


/* v8：TSV 单一词条源优先，内联 Tr 的第二个参数只当兜底。
   这样改翻译只需要动 l10n/zh.tsv，不会再出现“表里改了、调用点没改”。 */
const char* LangTr(const char* en, const char* zh) {
    if (g_lang != LANG_ZH) return en;
    {
        const char* t = Transl(en);
        if (t && t != en) return t;
    }
    return zh;
}

#include "common.h"   /* 系统头只在 common.h 里出现（模块约定） */
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
    static char buffers[8][320];
    static unsigned int nextBuffer;
    char* buf=buffers[(nextBuffer++)&7u];
    if (!strncmp(en, "GOLD ", 5))  { snprintf(buf, sizeof(buffers[0]), "金币 %s", en + 5); return buf; }
    if (!strncmp(en, "CORES ", 6))   { snprintf(buf, sizeof(buffers[0]), "核心 %s", en + 6); return buf; }
    if (!strncmp(en, "BEST LEVEL ", 11)) { snprintf(buf, sizeof(buffers[0]), "最高层 %s", en + 11); return buf; }
    if (!strncmp(en, "PENDING ", 8)) { snprintf(buf, sizeof(buffers[0]), "待上缴 %s", en + 8); return buf; }
    if (!strncmp(en, "LEVEL ", 6))   { snprintf(buf, sizeof(buffers[0]), "第 %s 层", en + 6); return buf; }
    if (!strncmp(en, "TIME ", 5))    { snprintf(buf, sizeof(buffers[0]), "时间 %s", en + 5); return buf; }
    if (!strncmp(en, "FUSES ", 6))   { snprintf(buf, sizeof(buffers[0]), "保险丝 %s", en + 6); return buf; }
    if (!strncmp(en, "SCORE ", 6))   { snprintf(buf, sizeof(buffers[0]), "得分 %s", en + 6); return buf; }
    if (!strncmp(en, "RUN FAILED", 10)) return "本局失败";
    if (!strncmp(en, "KNIFE", 5))    { snprintf(buf, sizeof(buffers[0]), "锈刃 %s", en + 5); return buf; }
    {   /* "100 GOLD" -> "100 金币" */
        int n = 0, consumed = 0;
        /* ==================================================================
         * 修：**这里原来是 `sscanf(...) == 1`，条件永远成立**
         * ==================================================================
         * 本意是「形如 `100 GOLD`」，但 `sscanf` 的返回值是**成功赋值的转换项数**：
         * `%d` 一匹配上就返回 1，后面的 " GOLD" 根本不参与判断。
         * 于是**任何以数字开头的字符串**都会被改成「N 金币」。
         *
         * 后果（实测）：
         *   · 主菜单行号 "1".."9" 变成 "1 金币".."9 金币"，
         *     多出来的「金币」正好压在下一列菜单文字上 → 每一行都多出一个「金」
         *   · 顺手把所有「以数字开头」的 HUD 文字也改坏了
         *
         * 改法：用 `%n` 拿到已消费长度，再确认整串被吃干净（允许尾随空格）。
         * ================================================================ */
        if (sscanf(en, "%d GOLD%n", &n, &consumed) == 1) {
            const char* rest = en + consumed;
            while (*rest == ' ') rest++;
            if (*rest == '\0') { snprintf(buf, sizeof(buffers[0]), "%d 金币", n); return buf; }
        }
        if (sscanf(en, "%d CORES%n", &n, &consumed) == 1) {
            const char* rest = en + consumed;
            while (*rest == ' ') rest++;
            if (*rest == '\0') { snprintf(buf, sizeof(buffers[0]), "%d 核心", n); return buf; }
        }
    }

    return en;
}


/* v8：TSV 单一词条源优先，内联 Tr 的第二个参数只当兜底。
   这样改翻译只需要动 l10n/zh.tsv，不会再出现“表里改了、调用点没改”。 */
const char* LangTr(const char* en,const char* zh) {
    return g_lang==LANG_ZH ? (zh && zh[0] ? zh : en) : en;
}


#ifndef CANITY_LANG_H
#define CANITY_LANG_H

/* ------------------------------------------------------------------ */
/* v2.0 双语                                                            */
/*   用法： Tr("English", "中文")                                       */
/*   故意做成"就地成对"而不是 key 表：字符串就在调用点旁边，             */
/*   改文案的时候不会出现「表里有键、界面里没有」的漂移。                */
/* ------------------------------------------------------------------ */

typedef enum {
    LANG_EN = 0,
    LANG_ZH = 1,
    LANG_COUNT
} LangId;

extern int g_lang;

static inline const char* Tr(const char* en, const char* zh) {
    return (g_lang == LANG_ZH) ? zh : en;
}

/* 绘制层翻译：找不到就原样返回（动态字符串按前缀造） */
const char* Transl(const char* en);

const char* LangName(int id);        /* "ENGLISH" / "中文" */
void LangToggle(void);

#endif

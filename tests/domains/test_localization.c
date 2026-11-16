/* Localization owns glossary selection and baked glyph coverage. */
#include "../test_common.h"
#include "lang.h"

void TestSuite_localization(void) {
    int savedLanguage=g_lang;
    g_lang=LANG_ZH;
    CHECK(!strcmp(LangTr("Experimental Look","fallback"),"上下视角(实验)"),
          "canonical glossary overrides a stale inline experimental label");
    CHECK(!strcmp(LangTr("unregistered localization fixture","中文回退"),"中文回退"),
          "unknown glossary keys retain their explicit Chinese fallback");
    CHECK(!strcmp(LangTr("%d RUN GOLD","fallback"),"%d 局内金币"),
          "localized formatting retains its numeric argument");
    const unsigned int missingBefore[]={0x9a8c,0x753b,0x4e91};
    for(int scale=1;scale<=4;scale++)for(size_t i=0;i<3;i++) {
        const unsigned long long* rows=CjkRows(missingBefore[i],scale);
        unsigned long long ink=0;
        if(rows)for(int y=0;y<12*scale;y++)ink|=rows[y];
        CHECK(rows && ink,"new Chinese characters have ink at every supported UI scale");
    }
    g_lang=LANG_EN;
    CHECK(!strcmp(LangTr("Experimental Look","上下视角(实验)"),"Experimental Look"),
          "English mode preserves the original label");
    g_lang=savedLanguage;
}


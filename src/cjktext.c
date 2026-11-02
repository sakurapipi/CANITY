/*
 * Canity — UTF-8 解码与小号中文字形（v8）
 *
 * v2.0 的实现是用 GDI + 微软雅黑现画中文，字号跟着 renderH/22 连续变化，
 * 和英文点阵的整数缩放永远对不齐（720p 下中文 18px、英文 12px），混排会
 * 压行、错位。现在中文改成 12px 单色点阵图集（src/cjk_glyphs.c），
 * 和 8x12 英文点阵共用同一行高与整数缩放，运行时不再依赖系统字体。
 *
 * 字形来源：Noto Sans SC（SIL OFL 1.1），只烘源码实际用到的字符；
 * 许可证见 l10n/OFL-NotoSansSC.txt。
 */
#include "common.h"
#include "render.h"

int CjkHasNonAscii(const char* s) {
    if (!s) return 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++)
        if (*p >= 0x80) return 1;
    return 0;
}

unsigned int CjkDecodeOne(const char** sp) {
    if (!sp || !*sp) return 0;
    const unsigned char* s = (const unsigned char*)*sp;
    unsigned int cp;
    int len;

    if (s[0] < 0x80) {
        cp = s[0];
        len = 1;
    } else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((unsigned int)(s[0] & 0x1F) << 6) | (unsigned int)(s[1] & 0x3F);
        len = 2;
        if (cp < 0x80) cp = 0xFFFD;
    } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((unsigned int)(s[0] & 0x0F) << 12) |
             ((unsigned int)(s[1] & 0x3F) << 6) |
             (unsigned int)(s[2] & 0x3F);
        len = 3;
        if (cp < 0x800) cp = 0xFFFD;
    } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 &&
               (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        cp = ((unsigned int)(s[0] & 0x07) << 18) |
             ((unsigned int)(s[1] & 0x3F) << 12) |
             ((unsigned int)(s[2] & 0x3F) << 6) |
             (unsigned int)(s[3] & 0x3F);
        len = 4;
        if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
    } else {
        cp = 0xFFFD;
        len = 1;
    }
    *sp += len;
    return cp;
}

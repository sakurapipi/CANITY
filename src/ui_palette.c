#include "threat_tier.h"
#include "common.h"
#include "ui_theme.h"
#include "ui.h"
#include "present_api.h"
#include "economy.h"
#include "state.h"
#include "threat.h"
#include "loop_api.h"
#include "player_api.h"
#include "render.h"
static int ThemeTier(void) {
    int live = currentState == STATE_PLAYING || currentState == STATE_SAFEHOUSE ||
               currentState == STATE_PAUSED || currentState == STATE_SKILL_PICK ||
               (currentState == STATE_SETTINGS && prevState == STATE_PAUSED);
    return live ? CurrentRunThreatTier() : PreviewThreatTier();
}
unsigned int UiThemeColor(int role) {
    static const unsigned int colors[4][7] = {
        {0x79CDF2, 0xF0F5FF, 0x0B111B, 0x172433, 0x294D6C, 0x4C718D, 0x243F56},
        {0xA99AFF, 0xEEE8FF, 0x10101D, 0x26213D, 0x4F4270, 0x7E6C9D, 0x3C3159},
        {0xF0C45A, 0xFFF2CF, 0x16140B, 0x302918, 0x665221, 0xA58535, 0x493B1D},
        {0xF12B1C, 0xF1E5CF, 0x0C0708, 0x210D0E, 0x581718, 0x9A3029, 0x3C1015}};
    int tier = ThemeTier();
    return colors[tier][role >= 0 && role < 7 ? role : 0];
}

/* Blood tier: broken registration rails, dry stains, hanging filaments and
 * interrupted warning bars. Confined to gutters: no scene warp or strobe. */
void UiDecayFrame(void) {
    int tier = ThemeTier();
    if (tier < 2 || winW < 100 || winH < 100)
        return;
    int red = tier == 3, gutter = UIS(red ? 17 : 9), count = red ? 44 : 18;
    if (gutter > winW / 30)
        gutter = winW / 30;
    for (int i = 0; i < count; i++) {
        int y = (i * 83 + 11) % winH, x = i % 2 ? winW - gutter : 0;
        int length = UIS(4 + i % 9);
        BlendRectWin(x, y, x + gutter, y + UIS(1), red ? 0x78251C : UI_EDGE, .9f);
        BlendRectWin(x + gutter / 2, y, x + gutter / 2 + 1, y + length, red ? 0xB53D29 : UI_ACCENT,
                     .6f);
        if (red) {
            int tooth = UIS(2 + i % 5);
            for (int row = 0; row < tooth; row++)
                BlendRectWin(x, y + row, x + gutter - row, y + row + 1, 0x060708, .95f);
            if (i % 4 == 0) {
                BlendRectWin(x + 2, y + 2, x + gutter - 2, y + 3, 0xC1AA87, .45f);
                for (int wire = 0; wire < 3; wire++)
                    BlendRectWin(x + 2 + wire * 3, y + 4, x + 3 + wire * 3, y + length + wire * 2,
                                 0x4E2925, .8f);
            }
        }
    }
    if (red)
        for (int i = 0; i < 19; i++) {
            int x = winW * (i + 1) / 20, span = UIS(7 + i % 5);
            BlendRectWin(x, 1, x + span, 3, 0xAC3727, .8f);
            BlendRectWin(x + 2, 4, x + span / 2, 5, 0x725044, .6f);
            BlendRectWin(x, winH - 3, x + span / 2, winH - 1, 0x551B18, .8f);
        }
}
void UiCorrodeRect(int x0, int y0, int x1, int y1) {
    int tier = ThemeTier();
    if (tier < 2 || x1 - x0 < 20 || y1 - y0 < 16)
        return;
    int red = tier == 3, depth = red ? UIS(9) : UIS(4);
    if (depth > (y1 - y0) / 5)
        depth = (y1 - y0) / 5;
    unsigned seed = (unsigned)(x0 * 127 + y0 * 331 + x1 * 17);
    int count = red ? 17 : 6;
    for (int i = 0; i < count; i++) {
        seed = seed * 1664525u + 1013904223u;
        int x = x0 + 3 + (int)(seed % (unsigned)(x1 - x0 - 9));
        int len = 2 + (int)(seed % (red ? 9u : 5u));
        /* Scrape only the topmost 2px; deeper damage belongs at corners. */
        BlendRectWin(x, y0, x + len < x1 ? x + len : x1, y0 + 2, 0x080B0D, .95f);
        if (red && (i % 3 == 0)) {
            int xx = i % 2 ? x0 + 2 : x1 - 4;
            for (int row = 0; row < depth; row++) {
                BlendRectWin(xx, y0 + row, xx + 1 + (row % 2), y0 + row + 1, 0x07090B, .95f);
                BlendRectWin(xx, y1 - 1 - row, xx + 1, y1 - row, 0xBA5135, .48f);
            }
        }
    }
    if (red)
        for (int side = 0; side < 2; side++) {
            int x = side ? x1 - 4 : x0 + 1;
            for (int n = 0; n < 4; n++) {
                int y = y0 + 4 + (y1 - y0 - 8) * (n + 1) / 5;
                BlendRectWin(x, y, x + 3, y + 2, 0x9B3527, .8f);
                BlendRectWin(x + 1, y + 2, x + 2, y + 5, 0x342121, .9f);
            }
        }
}

/* Original weirdcore collage: photocopied eye cutouts, misregistered red/cyan
 * ink, torn paper and registration marks. No full-screen strobe or text mask. */
void UiDreamFrame(void) {
    if (currentState == STATE_PAUSED ||
        (currentState == STATE_SETTINGS && prevState == STATE_PAUSED))
        return;
    if (winW < 160 || winH < 120)
        return;
    int live = currentState == STATE_PLAYING;
    float ratio = live ? sanity / fmaxf(1.0f, SanityMax()) : .55f;
    float stress = live ? fmaxf(0, (.62f - ratio) / .62f) : .16f;
    if (live && (!vignetteOn || stress <= 0))
        return;
    int w = UIS(45), h = UIS(19), pad = UIS(9);
    if (w > winW / 10)
        w = winW / 10;
    if (h > winH / 16)
        h = winH / 16;
    int drift = (int)(sinf((float)g_timeSec * .34f) * UIS(3));
    for (int side = 0; side < 2; side++) {
        int cx = side ? winW - pad - w : pad + w, cy = winH / 3 + side * winH / 5 + drift;
        float alpha = live ? .12f + stress * .24f : .15f;
        for (int y = -h; y <= h; y++) {
            int half = (int)(w * (1.0f - fabsf((float)y) / fmaxf(1, h)));
            BlendRectWin(cx - half, cy + y, cx + half + 1, cy + y + 1, 0xDAD8CC, alpha);
            if (abs(y) < h / 2) {
                int iris = h / 2 - abs(y) / 2;
                BlendRectWin(cx - iris + 2, cy + y, cx + iris + 2, cy + y + 1, 0x181A22,
                             alpha + .15f);
                BlendRectWin(cx - 1, cy + y, cx + 2, cy + y + 1, 0x900F1B, alpha);
            }
        }
        for (int i = 0; i < 7; i++) {
            int x = side ? winW - pad - UIS(2 + i % 3) : pad + UIS(i % 3);
            int y = winH / 5 + i * winH / 13;
            BlendRectWin(x, y, x + UIS(2), y + UIS(8 + i % 4), i % 2 ? 0xBC1827 : 0x87BDD0,
                         .12f + stress * .25f);
        }
    }
}

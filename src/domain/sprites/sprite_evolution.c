#include "common.h"
#include "sprite_bake.h"
void BakeEvolutionDetails(int kind, int pose) {
    int tier = SpriteBakeEvolutionTier();
    if (tier <= 0)
        return;
    unsigned ink = tier == 0   ? 0xA0B8C4
                   : tier == 1 ? 0x9E8BCB
                   : tier == 2 ? 0xAE5848
                               : 0xD83326,
             metal = 0x6B6461, bone = 0xC8AF7E;
    float cx = 128 + (pose >= 2 ? 6 : 0), cy = kind == 12 ? 45 : kind == 11 ? 98 : 70;
    if (kind == 0 || kind == 11 || kind == 12) {
        float width = kind == 11 ? 39 : kind == 12 ? 15 : 28;
        for (int side = -1; side <= 1; side += 2) {
            float x = cx + side * (width + 5);
            cvEllipse(x, cy, 6, 13, metal, 1);
            cvLine(x, cy - 13, x, cy + 13, 2, bone, .8f);
            if (tier == 3)
                for (int n = 0; n < 3; n++)
                    cvLine(x, cy - 10 + n * 8, x + side * (7 + n * 3), cy - 18 + n * 7, 2, ink,
                           .9f);
        }
        for (int n = 0; n < 5 + tier; n++) {
            float y = cy + 40 + n * 8;
            cvLine(cx - width, y, cx + width, y - 3, 3, metal, 1);
            cvLine(cx - width + 2, y - 1, cx + width - 2, y - 4, 1, bone, .7f);
        }
        cvLine(cx - 7, cy - 4, cx + 7, cy + 4, 2, 0x161719, 1);
        cvLine(cx - 7, cy + 4, cx + 7, cy - 4, 2, 0x161719, 1);
    } else if (kind == 1) {
        cvEllipse(cx, cy - 17, 25 + tier * 3, 4, metal, 1);
        cvLine(cx - 23, cy - 17, cx + 23, cy - 17, 2, ink, 1);
        for (int side = -1; side <= 1; side += 2) {
            cvEllipse(cx + side * 7, cy, 5, 6, bone, 1);
            if (tier == 3) {
                float fin[] = {cx + side * 17, cy + 31,        cx + side * 40,
                               cy + 63,        cx + side * 22, cy + 83};
                cvPolyGrad(fin, 3, 0xC0B9A6, 0x343C48, 1);
                cvLine(cx + side * 17, cy + 31, cx + side * 40, cy + 63, 2, ink, 1);
            }
        }
    } else if (kind == 7) {
        cvEllipse(cx - 30, cy + 45, 36, 18, 0x66533A, 1);
        cvLine(cx - 64, cy + 42, cx + 1, cy + 40, 3, bone, 1);
        cvLine(cx + 30, cy + 34, cx + 49, cy + 60, 4, metal, 1);
        for (int n = 0; n < tier + 2; n++)
            cvEllipse(cx - 50 + n * 14, cy + 52, 3, 2, ink, 1);
    } else if (kind == 8) {
        for (int n = 0; n < 6; n++)
            cvLine(cx - 8, cy + 28 + n * 10, cx + 8, cy + 21 + n * 10, 2, bone, .8f);
        cvLine(cx + 16, cy + 29, cx + 33, cy + 116, 3, metal, 1);
        cvEllipse(cx + 33, cy + 126, 17, 5, ink, .9f);
        if (tier >= 2)
            for (int n = 0; n < 5; n++)
                cvLine(cx - 20, cy + 60 + n * 13, cx - 40 - n * 2, cy + 107 + n * 12, 2, 0x71423C,
                       .9f);
    } else if (kind == 9) {
        float x = cx + 54;
        cvLine(x, cy - 40, x, 226, 4, metal, 1);
        cvEllipse(x, cy - 24, 15, 20, 0x343438, 1);
        cvEllipse(x, cy - 24, 9, 14, bone, 1);
        for (int n = 0; n < 3; n++)
            cvLine(cx - 9, cy - 5 + n * 7, cx + 9, cy - 5 + n * 7, 3, ink, 1);
        if (tier == 3) {
            cvLine(x - 16, cy - 25, x + 16, cy - 25, 3, ink, 1);
            cvLine(x - 12, cy - 34, x + 12, cy - 34, 2, bone, 1);
        }
    } else if (kind == 13) {
        for (int n = 0; n < 8; n++) {
            float x = cx - 35 + n * 10;
            cvLine(x, 112, x + 2, 124, 3, bone, 1);
        }
        if (tier == 3) {
            cvLine(cx, 136, cx + 28, 173, 8, 0x7E2228, 1);
            cvEllipse(cx + 28, 174, 8, 5, 0xB54D43, 1);
        }
    } else if (kind == 16) {
        cvEllipse(cx, 153, 31, 9, metal, 1);
        for (int n = 0; n < 5; n++)
            cvLine(cx - 26 + n * 13, 149, cx - 26 + n * 13, 160, 2, ink, 1);
        if (tier == 3) {
            cvLine(cx - 63, 190, cx + 63, 190, 2, bone, .85f);
            cvLine(cx - 63, 190, cx, 154, 1, ink, .8f);
            cvLine(cx + 63, 190, cx, 154, 1, ink, .8f);
        }
    }
    if (tier >= 2)
        for (int n = 0; n < 5; n++)
            cvLine(cx - 22 + n * 11, cy + 40, cx - 27 + n * 11, cy + 46 + n * 6, 1, ink, .8f);
}

/* The hand, shop and in-flight renderer all sample these physical objects. */
void BakeTacticalTool(int id, int tool) {
    SpriteBakeCanvas()->scale = 1;
    cvClear();
    if (tool == 1) {
        /* A forward-pointed ice-mirror shard, with distinct bevel planes. */
        float blade[] = {127, 22, 153, 89, 148, 165, 131, 214, 106, 177, 99, 100};
        float left[] = {127, 22, 124, 155, 106, 177, 99, 100};
        float face[] = {127, 22, 153, 89, 148, 165, 124, 155};
        cvPolyGrad(blade, 6, 0xDDF7FA, 0x4B718F, 1);
        cvPolyGrad(left, 4, 0x8DB5CB, 0x253F65, 1);
        cvPolyGrad(face, 4, 0xE8FFFF, 0x9DCCD8, 1);
        cvLine(127, 22, 124, 155, 2, 0xFFFFFF, 1);
        cvLine(99, 100, 106, 177, 2, 0xCBEEF7, 1);
        cvLine(112, 190, 143, 187, 7, 0x33444C, 1);
        cvLine(115, 198, 140, 195, 3, 0x9BA595, 1);
    } else if (tool == 0) {
        /* Porous acoustic baffles around a black cylindrical core. */
        cvLine(128, 72, 128, 200, 48, 0x1B3039, 1);
        for (int n = 0; n < 6; n++) {
            cvEllipse(128, 87 + n * 20, 35, 11, 0x779793, 1);
            cvEllipse(128, 84 + n * 20, 29, 7, 0xB2C1AD, 1);
            cvLine(108, 89 + n * 20, 148, 89 + n * 20, 3, 0x213A42, 1);
        }
        cvLine(128, 64, 128, 79, 13, 0x627D82, 1);
        cvEllipse(128, 64, 11, 4, 0xC9DDCA, 1);
        cvLine(106, 103, 106, 177, 4, 0x68D1CC, .9f);
    } else if (tool == 4) {
        /* Powder cartridge: brass nozzle, glass window and loose amber dust. */
        float body[] = {99, 196, 97, 112, 107, 97, 150, 97, 160, 113, 157, 196};
        cvPolyGrad(body, 6, 0x6E7B78, 0x28363F, 1);
        cvLine(107, 203, 149, 203, 9, 0x8D8970, 1);
        cvLine(116, 94, 141, 94, 16, 0xBAA771, 1);
        cvLine(119, 78, 138, 78, 11, 0xD3C290, 1);
        cvEllipse(128, 72, 12, 5, 0x3A4548, 1);
        cvLine(113, 124, 145, 124, 6, 0xCABB83, 1);
        cvLine(109, 170, 146, 170, 29, 0xB1A06E, .85f);
        for (int n = 0; n < 19; n++)
            cvEllipse(106 + (n * 17) % 44, 144 + (n * 11) % 44, 1.7f, 1.7f, 0xF0DBA0, 1);
        cvLine(103, 113, 103, 185, 3, 0xDDE4CD, .8f);
    } else if (tool == 6) {
        /* A caged pulse cell and two antennae, rather than a glowing orb. */
        float body[] = {92, 112, 113, 95, 153, 102, 168, 124, 164, 185, 106, 198, 89, 177};
        cvPolyGrad(body, 7, 0x657080, 0x29313F, 1);
        cvLine(111, 108, 111, 181, 7, 0x9D86B3, 1);
        cvLine(148, 111, 148, 179, 7, 0xA5B6C1, 1);
        for (int n = 0; n < 5; n++)
            cvLine(104, 127 + n * 10, 156, 123 + n * 10, 3, 0xBC9DCA, 1);
        cvLine(107, 103, 97, 61, 4, 0xB6C6CA, 1);
        cvLine(151, 105, 167, 74, 4, 0xB6C6CA, 1);
        cvEllipse(97, 60, 4, 4, 0xD5C8EC, 1);
        cvEllipse(167, 73, 4, 4, 0xD5C8EC, 1);
        cvLine(114, 197, 149, 190, 4, 0xDEB690, 1);
    } else if (tool == 3) {
        cvLine(128, 192, 128, 94, 23, 0x52646A, 1);
        cvEllipse(128, 165, 28, 23, 0x9AA499, 1);
        for (int n = 0; n < 4; n++)
            cvEllipse(128, 165, 8 + n * 5, 5 + n * 4, 0x314954, .8f);
        cvLine(128, 96, 128, 54, 7, 0xCFD5C7, 1);
        cvLine(128, 54, 100, 79, 6, 0x8FA4A7, 1);
        cvLine(128, 54, 156, 79, 6, 0x8FA4A7, 1);
        cvLine(100, 79, 97, 65, 4, 0xD2DACB, 1);
        cvLine(156, 79, 159, 65, 4, 0xD2DACB, 1);
    } else if (tool == 2) {
        float vial[] = {102, 201, 102, 115, 116, 104, 116, 88,
                        142, 88,  142, 104, 155, 115, 155, 201};
        cvPolyGrad(vial, 8, 0xA0C9BB, 0x2C5D58, 1);
        cvLine(111, 153, 147, 153, 24, 0x8AC4A2, .9f);
        cvLine(113, 84, 145, 84, 12, 0x4C6266, 1);
        cvLine(110, 115, 110, 192, 3, 0xD6E4CD, 1);
        cvLine(119, 137, 139, 173, 3, 0xE7E8C6, 1);
        cvLine(139, 137, 119, 173, 3, 0xE7E8C6, 1);
    } else {
        float packet[] = {88, 95, 157, 87, 170, 189, 99, 204};
        cvPolyGrad(packet, 4, 0xB5B4A0, 0x565F60, 1);
        cvLine(96, 106, 155, 98, 5, 0x3D4B51, 1);
        for (int n = 0; n < 4; n++)
            cvLine(107, 139 + n * 9, 147, 132 + n * 9, 2, 0x223D43, 1);
        cvLine(105, 135, 149, 172, 6, 0x9C6B62, 1);
        cvLine(102, 194, 161, 183, 3, 0xE2D9B7, 1);
    }
    cvFinish(SpriteBakeTarget(id));
}

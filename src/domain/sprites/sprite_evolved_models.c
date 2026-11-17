/* Four stages of the SAME reference-built creature, not four unrelated rigs.
 * Stage I establishes proportions and identity; later stages progressively
 * expose the cage, break the plates and loosen cables. No reference pixels. */
#include "common.h"
#include "sprite_bake.h"

typedef struct {
    int tier, side, walk;
    float x;
    unsigned seam;
} ModelLook;
static ModelLook Look(int pose) {
    int tier = SpriteBakeEvolutionTier();
    return (ModelLook){tier, pose >= 2, pose == 1 || pose == 3,
                       128 + (pose == 1 || pose == 3 ? 2 : 0),
                       tier == 3   ? 0xA85143
                       : tier == 2 ? 0xB59B65
                                   : 0x889C9C};
}
static void Pin(float x, float y, float radius) {
    cvEllipse(x, y, radius + 1, radius + 1, 0x182128, 1);
    cvEllipse(x - .6f, y - .6f, radius, radius, 0xB6B7A4, 1);
    cvLine(x - radius * .55f, y, x + radius * .55f, y, 1, 0x424C4F, 1);
}
static void Link(float x, float y, float xx, float yy, float width) {
    cvLine(x, y, xx, yy, width + 2, 0x111B22, 1);
    cvLine(x, y, xx, yy, width, 0x687675, 1);
    cvLine(x - 1, y, xx - 1, yy, width * .24f, 0xB6B8A4, .9f);
    Pin(x, y, width * .48f);
    Pin(xx, yy, width * .48f);
}
static void Armor(float x, float y, float w, float h, unsigned seam) {
    float outer[] = {x - w, y + 3, x - w + 3, y,     x + w - 3, y,
                     x + w, y + 3, x + w - 2, y + h, x - w + 2, y + h};
    cvPolyGrad(outer, 6, 0x777E76, 0x28343C, 1);
    cvLine(x - w + 2, y + 2, x + w - 2, y + 2, 1.4f, 0xC7C1A8, 1);
    cvLine(x - w + 2, y + 4, x - w + 2, y + h - 2, 1, 0xB5B59F, .8f);
    cvLine(x + w - 2, y + 4, x + w - 2, y + h - 2, 2, 0x1D2C32, 1);
    for (int n = 0; n < 4; n++) {
        float xx = x - w + 4 + (w * 2 - 8) * (n + 1) / 5;
        cvLine(xx, y + 5, xx - 1, y + h - 5, .7f, 0xC5C3A4, .17f);
    }
    Pin(x - w + 4, y + 5, 1.3f);
    Pin(x + w - 4, y + h - 5, 1.3f);
    cvLine(x - w + 4, y + h - 3, x + w - 4, y + h - 3, 1, seam, .6f);
}
static void Tear(float x, float y, float width, float length, int index) {
    float points[] = {x - width,       y,
                      x + width,       y + 2,
                      x + width * .6f, y + length * .47f,
                      x + width * .9f, y + length * .76f,
                      x + 1,           y + length - 6,
                      x - width * .2f, y + length,
                      x - width * .6f, y + length * .58f};
    cvPolyGrad(points, 7, index % 2 ? 0x535C58 : 0x686756, 0x18232B, 1);
    cvLine(x - width, y, x - width * .6f, y + length * .58f, 1, 0x9A9C83, .75f);
    cvLine(x + 1, y + length - 6, x + 3, y + length + 2, .8f, 0x7E8C7C, .6f);
}
static void Cable(float x, float y, float rx, float ry, unsigned color) {
    for (int n = 0; n < 26; n++) {
        float a = n * 6.2831853f / 26, b = (n + 1) * 6.2831853f / 26;
        cvLine(x + cosf(a) * rx, y + sinf(a) * ry, x + cosf(b) * rx, y + sinf(b) * ry, 2.4f,
               0x151E25, 1);
        cvLine(x + cosf(a) * rx - 1, y + sinf(a) * ry, x + cosf(b) * rx - 1, y + sinf(b) * ry, .9f,
               color, .9f);
    }
}
static void Crack(float x, float y, int tier, int left) {
    float d = left ? -1 : 1;
    cvLine(x, y, x + d * 4, y + 7, 1.5f, 0x111920, 1);
    cvLine(x + d * 4, y + 7, x - d * 2, y + 12, 1.5f, 0x111920, 1);
    if (tier == 3)
        cvLine(x - d * 2, y + 12, x + d * 7, y + 19, 1.2f, 0x9C5141, .9f);
}
static void Tape(float x, float y, float w, int red) {
    float p[] = {x - w, y + 1, x + w, y - 2, x + w - 1, y + 10, x - w + 2, y + 13};
    cvPolyGrad(p, 4, red ? 0x77504A : 0x6F9390, red ? 0x473834 : 0x374F53, 1);
    cvLine(x - w + 3, y + 3, x + w - 3, y + 1, .8f, 0xC6BA9B, .45f);
}
static void ChestCage(float x, float top, float bottom, float width, ModelLook m) {
    Link(x, top, x, bottom, 3.5f);
    for (int n = 0; n < 6; n++) {
        float y = top + 5 + n * (bottom - top - 13) / 6, ww = width * (1 - n * .055f);
        cvLine(x - ww, y + 4, x, y, 3.6f, 0x6B7469, 1);
        cvLine(x, y, x + ww, y + 4, 3.6f, 0x6B7469, 1);
        cvLine(x - ww, y + 2, x, y - 1, .8f, 0xC6BE9B, .85f);
    }
    if (m.tier < 2) {
        Armor(x, top, width + 2, (bottom - top) * .93f, m.seam);
        for (int n = 0; n < 4; n++)
            cvLine(x - width + 4, top + 9 + n * 10, x + width - 4, top + 6 + n * 10, 1, 0x9A9D88,
                   .8f);
        Tape(x - width * .5f, top + 18, 7, 0);
        Tape(x + width * .45f, bottom - 14, 8, 1);
    } else {
        float broken[] = {x - width - 2, top,        x - width + 10, top + 3,
                          x - width + 3, bottom - 6, x - width - 5,  bottom + 2};
        cvPolyGrad(broken, 4, 0x8B8870, 0x27343B, 1);
        Armor(x + width - 4, top + 8, 7, 18, m.seam);
        Tape(x + width * .3f, bottom - 13, 7, 1);
        if (m.tier == 3) {
            cvLine(x + 4, top + 8, x + 1, bottom + 10, 2, 0x945746, 1);
            Tear(x - width + 1, bottom - 9, 6, 31, 0);
            Cable(x + width + 5, (top + bottom) * .5f, 10, (bottom - top) * .43f, 0x8D8873);
        }
    }
}
static void BlindHead(float x, float y, float r, ModelLook m, int tall) {
    cvEllipse(x, y, r + 1, r * 1.42f, 0x101A22, 1);
    cvEllipse(x - 1, y - 2, r, r * 1.37f, 0x6F7A74, 1);
    cvEllipse(x - 3, y - 6, r * .63f, r * .75f, 0x99A08F, .55f);
    cvLine(x, y - r * 1.25f, x + 2, y - 2, 1, 0x303D40, .9f);
    for (int s = -1; s <= 1; s += 2) {
        float xx = x + s * r * .43f;
        cvLine(xx - 2, y - 4, xx + 2, y, 1.2f, 0xD2C6A3, 1);
        cvLine(xx - 2, y, xx + 2, y - 4, 1.2f, 0xD2C6A3, 1);
    }
    cvEllipse(x, y + r * .78f, r * .78f, r * .69f, 0x060F16, 1);
    for (int n = -2; n <= 2; n++) {
        float xx = x + n * r * .25f;
        cvLine(xx, y + r * .24f, xx + .5f, y + r * .56f, 1.7f, 0xC7BC94, 1);
        cvLine(xx, y + r * 1.14f, xx - .5f, y + r * .94f, 1.5f, 0x9A9278, 1);
    }
    if (m.tier >= 1) {
        for (int s = -1; s <= 1; s += 2) {
            Cable(x + s * (r + 4), y - 1, m.tier == 1 ? 3.5f : 6, 9, 0xA2A68E);
            if (m.tier >= 2) {
                cvLine(x + s * r, y + 2, x + s * (r + 12), y + 5, 2, 0x7E8070, 1);
                for (int n = 0; n < 3; n++)
                    cvLine(x + s * (r + 8), y - 5 + n * 5, x + s * (r + 13), y - 5 + n * 5, 1,
                           m.seam, .8f);
            }
        }
    }
    if (m.tier == 3) {
        Crack(x + r * .3f, y - r, 3, 1);
        cvLine(x - r * .7f, y + r * 1.2f, x - r * .4f, y + r * 2.1f, 1.5f, 0x7E5146, 1);
        if (tall)
            Link(x, y - r * 1.35f, x, y - r * 1.35f - 8, 2);
    }
}
static void Hands(float x, float y, int side, int tier) {
    Armor(x, y, 3.5f, 8, 0x73877E);
    for (int finger = 0; finger < 3; finger++) {
        float xx = x - 3 + finger * 3;
        cvLine(xx, y + 7, xx + side * 2, y + 15 + tier * 1.3f, 1.5f, 0x9A9F88, 1);
        if (tier >= 2)
            cvLine(xx + side * 2, y + 15 + tier * 1.3f, xx + side * 4, y + 17 + tier * 1.3f, 1,
                   0xBBB49A, 1);
    }
}
static void Hunter(int kind, int pose) {
    ModelLook m = Look(pose);
    int squat = kind == 11, tall = kind == 12;
    float head = squat ? 112 : tall ? 24 : 51, neck = squat ? 136 : tall ? 95 : 83;
    float hip = squat ? 203 : tall ? 158 : 177, width = squat ? 36 : tall ? 10 : 27;
    if (m.tier == 3)
        Cable(m.x + width + 8, neck + 9, 16, 30, 0x777D6B);
    BlindHead(m.x + (m.side ? 3 : 0), head, squat ? 19 : tall ? 10 : 15, m, tall);
    Link(m.x, head + 20, m.x, neck, 4);
    if (tall)
        for (int n = 0; n < 4; n++)
            cvLine(m.x - 5, head + 24 + n * 7, m.x + 5, head + 24 + n * 7, 1, 0xA6A58D, 1);
    ChestCage(m.x, neck, hip - 9, width, m);
    Armor(m.x, hip - 6, squat ? 23 : tall ? 8 : 15, 11, m.seam);
    for (int side = -1; side <= 1; side += 2) {
        float d = m.walk ? side * 4 : 0, sx = m.x + side * (m.side ? width * .7f : width);
        float elbow = squat ? neck + 34 : neck + 39, hand = squat ? 223 : hip + 12;
        Link(sx, neck + 8, sx + side * 8, elbow, squat ? 6 : 4.5f);
        Link(sx + side * 8, elbow, sx + side * 7, hand - 6, 4);
        Armor(sx + side * 2, neck - 3, squat ? 11 : 8, 17, m.seam);
        if (m.tier >= 2) {
            Crack(sx, neck + 1, m.tier, side < 0);
            Tear(sx, elbow - 9, 5, 24 + 5 * m.tier, side);
        }
        Hands(sx + side * 7, hand - 7, side, m.tier);
        float leg = m.x + side * (squat ? 19 : tall ? 6 : 11), knee = squat ? 224 : tall ? 205 : 214;
        Link(leg, hip + 5, leg + d, knee, squat ? 7 : 5);
        Link(leg + d, knee, leg - d * .4f, 244, 4.5f);
        Armor(leg + d, knee + 3, squat ? 7 : 5, 19, m.seam);
        cvLine(leg - d * .4f - 4, 248, leg - d * .4f + 6, 248, 4, 0x777D71, 1);
    }
}
static void LampEye(float x, float y, float radius) {
    cvEllipse(x, y, radius + 2, radius + 2, 0x303D43, 1);
    cvEllipse(x, y, radius + 1, radius + 1, 0xC5B99B, 1);
    cvEllipse(x, y, radius, radius, 0xF3D5A1, 1);
    cvEllipse(x - .7f, y - .7f, radius * .58f, radius * .58f, 0xFFF8DD, 1);
}
static void LightCreature(int pose) {
    ModelLook m = Look(pose);
    if (m.tier >= 1)
        for (int s = -1; s <= 1; s += 2)
            Cable(m.x + s * 21, 99, 13, 24, 0x8A8F78);
    for (int s = -1; s <= 1; s += 2) {
        float leg = m.x + s * 10, d = m.walk ? s * 4 : 0;
        Link(leg, 163, leg + d, 207, 4);
        Link(leg + d, 207, leg - d * .5f, 246, 3);
        Armor(leg + d, 210, 4.5f, 30, m.seam);
        cvLine(leg - 4, 249, leg + 4, 249, 3, 0xA3A68D, 1);
        Link(m.x + s * 17, 88, m.x + s * 28, 134, 4);
        Link(m.x + s * 28, 134, m.x + s * 23, 183, 3);
        Hands(m.x + s * 23, 181, s, m.tier);
        Armor(m.x + s * 18, 85, 7, 21, m.seam);
        if (m.tier >= 2) {
            Tear(m.x + s * 23, 99, 7, 75 + s * 5, 0);
            Tear(m.x + s * 16, 136, 4, 43, 1);
        }
    }
    ChestCage(m.x, 91, 158, 15, m);
    Link(m.x, 65, m.x, 88, 3);
    cvEllipse(m.x, 47, 14, 21, 0x29383F, 1);
    cvEllipse(m.x - 1, 44, 12, 18, 0x84918A, 1);
    LampEye(m.x - 6, 44, 4);
    LampEye(m.x + 6, 44, 4);
    Link(m.x, 54, m.x, 73, 2);
    if (m.tier >= 2)
        LampEye(m.x, 33, 3);
    if (m.tier == 3) {
        LampEye(m.x, 55, 3.4f);
        Crack(m.x + 7, 29, 3, 0);
        cvLine(m.x, 101, m.x, 143, 2, 0xD1AB78, 1);
    }
}
static void Spider(int pose) {
    ModelLook m = Look(pose);
    if (m.tier == 3) {
        for (int n = 0; n < 4; n++) {
            cvLine(40 + n * 5, 236, 128, 113 + n * 9, 1, 0xC1C6B6, .4f);
            cvLine(216 - n * 5, 236, 128, 113 + n * 9, 1, 0xC1C6B6, .4f);
        }
    }
    for (int side = -1; side <= 1; side += 2)
        for (int n = 0; n < 4; n++) {
            float x = m.x + side * (19 + n * 3), y = 150 + n * 10;
            float knee = m.x + side * (47 + n * 12), ky = 128 + n * 17;
            Link(x, y, knee, ky, 4.5f);
            Link(knee, ky, m.x + side * (86 - n * 7), 231 + n * 4, 3.2f);
            Armor(knee, ky - 4, 4.5f, 12, m.seam);
            if (m.tier >= 2)
                cvLine(knee, ky, knee + side * 8, ky + 13, 2, 0x8C8D76, 1);
        }
    cvEllipse(m.x, 156, 35, 28, 0x27353C, 1);
    cvEllipse(m.x - 2, 150, 32, 24, 0x7B8276, 1);
    for (int n = 0; n < 4; n++) {
        float yy = 132 + n * 12;
        cvLine(m.x - 27, yy, m.x + 27, yy + 2, 3, 0x343F42, 1);
        cvLine(m.x - 25, yy - 1, m.x + 25, yy + 1, 1, 0xBCB79B, 1);
    }
    cvEllipse(m.x, 185, 20, 13, 0x737F76, 1);
    for (int n = 0; n < 5; n++)
        cvEllipse(m.x - 12 + n * 6, 183 - (n % 2) * 4, 1.8f, 1.8f, 0xD17661, 1);
    for (int s = -1; s <= 1; s += 2) {
        Link(m.x + s * 8, 194, m.x + s * 15, 209, 2);
        cvLine(m.x + s * 15, 209, m.x + s * 22, 203, 2, 0xBBB695, 1);
    }
    if (m.tier >= 1)
        Cable(m.x, 141, 40, 17, 0x8A927C);
    if (m.tier >= 2) {
        Crack(m.x + 14, 139, m.tier, 0);
        Armor(m.x, 133, 10, 20, m.seam);
    }
}
static void Collector(int pose) {
    ModelLook m = Look(pose);
    float x = m.x + 19;
    if (m.tier >= 2) {
        Cable(x + 16, 106, 22, 26, 0x8A8B74);
        Tear(x + 28, 133, 5, 43, 0);
    }
    for (int side = -1; side <= 1; side += 2) {
        float d = m.walk ? side * 4 : 0;
        Link(x + side * 11, 177, x + side * 15 + d, 215, 5.5f);
        Link(x + side * 15 + d, 215, x + side * 16, 245, 4);
        Armor(x + side * 15 + d, 218, 6, 22, m.seam);
        cvLine(x + side * 16 - 5, 248, x + side * 16 + 6, 248, 4, 0x8A8E7A, 1);
    }
    ChestCage(x, 104, 169, 22, m);
    cvEllipse(x + 1, 83, 13, 16, 0x737F7B, 1);
    LampEye(x, 83, 5);
    Link(x + 22, 112, x + 36, 151, 4);
    Link(x + 36, 151, x + 29, 187, 4);
    Hands(x + 29, 186, 1, m.tier);
    Link(x - 21, 113, x - 41, 129, 4);
    Link(x - 41, 129, 87, 134, 4);
    /* Its defining salvaged dish, not a generic sack or blue circle. */
    cvEllipse(83, 116, 50, 25, 0x24333B, 1);
    cvEllipse(81, 113, 47, 23, 0x979889, 1);
    cvEllipse(80, 112, 42, 19, 0x5D6B6B, 1);
    for (int n = 0; n < 8; n++) {
        float a = n * 6.2831853f / 8;
        Pin(81 + cosf(a) * 43, 113 + sinf(a) * 20, 1.5f);
    }
    Tape(79, 108, 10, 1);
    Armor(x + 20, 169, 9, 17, m.seam);
    if (m.tier >= 1)
        cvLine(x + 1, 68, x + 1, 60, 2, 0xB2B39A, 1);
    if (m.tier == 3) {
        for (int n = 0; n < 4; n++) {
            cvLine(55 + n * 18, 139, 53 + n * 18, 153 + (n % 2) * 6, 1, 0xACA78D, 1);
            Tape(53 + n * 18, 154 + (n % 2) * 6, 3, n % 2);
        }
        Crack(65, 105, 3, 1);
        Crack(97, 109, 3, 0);
    }
}
static void Scavenger(int pose) {
    ModelLook m = Look(pose);
    if (m.tier >= 1)
        Cable(m.x + 17, 89, 19, 36, 0x7C8876);
    /* Fixed long-robed base, then progressively loose and asymmetric strips. */
    for (int n = 0; n < 9; n++) {
        float x = 103 + n * 6;
        float end = m.tier == 0 ? 247 : 247 - (n * 7) % 13;
        Tear(x, 112 + (n % 3) * 6, 4 + (m.tier >= 2 ? 2 : 0), end - 116, n);
    }
    ChestCage(m.x, 81, 147, 13, m);
    for (int side = -1; side <= 1; side += 2) {
        Link(m.x + side * 17, 87, m.x + side * 27, 141, 3.5f);
        Link(m.x + side * 27, 141, m.x + side * 29, 194, 3);
        Hands(m.x + side * 29, 192, side, m.tier);
        Armor(m.x + side * 16, 81, 6, 37, m.seam);
        if (m.tier >= 2)
            Tear(m.x + side * 19, 101, 6, 113 + side * 9, side);
    }
    cvEllipse(m.x, 52, 12, 18, 0x7B887F, 1);
    cvEllipse(m.x, 54, 9, 13, 0x24323B, 1);
    for (int n = 0; n < 3; n++)
        cvLine(m.x - 8, 47 + n * 6, m.x + 8, 47 + n * 6, 2, 0xB1B29C, 1);
    if (m.tier >= 2) {
        cvEllipse(m.x, 54, 12, 15, 0x111C24, .8f);
        cvLine(m.x, 47, m.x, 121, 2, 0x9A5D4C, 1);
        for (int n = 0; n < 6; n++)
            cvLine(m.x - 4, 61 + n * 7, m.x + 4, 58 + n * 7, 1.6f, 0xB7A287, 1);
    }
    if (m.tier == 3)
        for (int n = 0; n < 5; n++)
            Tear(96 + n * 15, 132 + (n % 2) * 12, 3, 80 + (n % 3) * 9, n);
}
static void Warden(int pose) {
    ModelLook m = Look(pose);
    float lamp = m.x + 65;
    float robe[] = {104, 143, 152, 143, 169, 190, 183, 248, 73, 248, 87, 190};
    cvPolyGrad(robe, 6, 0x717D76, 0x273740, 1);
    for (int n = 0; n < 5; n++)
        cvLine(94 + n * 16, 169, 81 + n * 22, 245, 1.1f, 0xA5A48B, .8f);
    if (m.tier >= 2)
        for (int n = 0; n < 5; n++)
            Tear(89 + n * 20, 165 + n % 2 * 9, 6, 75 - (n * 5) % 19, n);
    ChestCage(m.x, 91, 149, 28, m);
    for (int side = -1; side <= 1; side += 2) {
        Armor(m.x + side * 37, 88, 18, 14, m.seam);
        Link(m.x + side * 41, 99, m.x + side * 44, 134, 5);
        Link(m.x + side * 44, 134, m.x + side * (side == 1 ? 61 : 40), 161, 4);
        Hands(m.x + side * (side == 1 ? 61 : 40), 158, side, m.tier);
        if (m.tier >= 2) {
            Crack(m.x + side * 37, 90, m.tier, side < 0);
            Tear(m.x + side * 34, 102, 5, 55, side);
        }
    }
    float helmet[] = {m.x, 28, m.x - 17, 46, m.x - 12, 78, m.x, 91, m.x + 12, 78, m.x + 17, 46};
    cvPolyGrad(helmet, 6, 0x8E9B90, 0x263740, 1);
    cvLine(m.x, 29, m.x, 88, 1.3f, 0xC3BFA6, 1);
    cvLine(m.x - 17, 46, m.x, 28, 2, 0xB6B9A1, 1);
    for (int n = 0; n < 3; n++)
        cvLine(m.x - 7, 53 + n * 7, m.x + 7, 53 + n * 7, 2, m.tier >= 1 ? 0xE2ECE0 : 0x88B2B0, 1);
    Link(lamp, 56, lamp, 246, 3);
    cvEllipse(lamp, 66, 14, 19, 0x8B9A8A, 1);
    cvEllipse(lamp, 66, 10, 15, 0xD8BF8A, 1);
    for (int n = -1; n <= 1; n++)
        cvLine(lamp + n * 6, 49, lamp + n * 6, 83, 1.5f, 0x4F615F, 1);
    cvEllipse(lamp, 66, 14, 5, 0xA4AB92, .8f);
    if (m.tier >= 1)
        Cable(lamp, 66, 18, 22, 0x858F79);
    if (m.tier == 3) {
        Tape(m.x + 8, 111, 9, 1);
        Crack(m.x - 7, 44, 3, 0);
        for (int n = 0; n < 5; n++)
            cvEllipse(m.x - 17 + (n % 2) * 4, 163 + n * 9, 4, 5, 0x9D9F86, .85f);
    }
}
static void Mimic(int pose) {
    ModelLook m = Look(pose);
    float lid[] = {77, 101, 79, 42, 176, 35, 183, 96};
    cvPolyGrad(lid, 4, 0x777160, 0x2C302E, 1);
    cvLine(79, 42, 176, 35, 4, 0xB9B49B, 1);
    cvLine(81, 46, 82, 96, 3, 0xA4A58C, 1);
    for (int n = 0; n < 6; n++)
        cvLine(84, 49 + n * 7, 173, 42 + n * 7, 1, 0x9A8D70, .6f);
    Armor(m.x, 166, 49, 48, m.seam);
    Tape(m.x - 9, 188, 18, 1);
    cvEllipse(m.x, 132, 46, 30, 0x070F16, 1);
    for (int n = 0; n < 11; n++) {
        float x = 87 + n * 8;
        cvLine(x, 106, x + 1, 121 + (n % 3) * 3, 2.9f, 0xD7C99F, 1);
        cvLine(x, 161, x - 1, 145 - (n % 3) * 2, 2.9f, 0xBDB18E, 1);
    }
    LampEye(104, 103, 3.5f);
    LampEye(152, 100, 3.5f);
    cvEllipse(104, 103, 2.8f, 2.8f, 0xD65A42, 1);
    cvEllipse(152, 100, 2.8f, 2.8f, 0xD65A42, 1);
    for (int side = -1; side <= 1; side += 2)
        for (int n = 0; n < 2; n++) {
            float x = m.x + side * (22 + n * 19);
            Link(x, 212, x + side * (m.tier >= 2 ? 9 : 3), 246, 4);
            cvLine(x + side * 4 - 4, 249, x + side * 4 + 5, 249, 3.5f, 0x949B83, 1);
        }
    if (m.tier >= 1) {
        Armor(82, 163, 5, 44, m.seam);
        Armor(175, 163, 5, 44, m.seam);
        Crack(94, 174, m.tier, 0);
    }
    if (m.tier >= 2)
        for (int side = -1; side <= 1; side += 2) {
            Cable(m.x + side * 47, 141, 11, 22, 0x91584C);
            cvLine(m.x + side * 37, 111, m.x + side * 50, 132, 2, 0x96584B, 1);
        }
    if (m.tier == 3) {
        cvLine(m.x, 133, m.x + 17, 165, 7, 0x7A3C37, 1);
        cvLine(m.x + 17, 165, m.x + 43, 172, 5, 0xB4755F, 1);
        cvLine(m.x - 38, 177, m.x + 35, 201, 2, 0x918D76, 1);
        for (int n = 0; n < 5; n++)
            Pin(m.x - 35 + n * 16, 177 + n * 6, 2);
    }
}
int BakeEvolvedCreature(int kind, int pose) {
    int tier = SpriteBakeEvolutionTier();
    if (tier < 0 || tier > 3)
        return 0;
    switch (kind) {
    case 0:
    case 11:
    case 12:
        Hunter(kind, pose);
        break;
    case 1:
        LightCreature(pose);
        break;
    case 7:
        Collector(pose);
        break;
    case 8:
        Scavenger(pose);
        break;
    case 9:
        Warden(pose);
        break;
    case 13:
        Mimic(pose);
        break;
    case 16:
        Spider(pose);
        break;
    default:
        return 0;
    }
    return 1;
}

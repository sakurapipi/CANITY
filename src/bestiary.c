/*
 * Canity — 怪物图鉴数据
 *
 * 一条一行：名字 / 标签 / 它干什么 / 怎么对付。
 * 新增怪物只加一行（和 skills.c 一样的表驱动写法）。
 */
#include "common.h"
#include "bestiary.h"

int bestiarySel = 0;
int bestiarySeen = 0;

void BestiaryMarkSeen(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return;
    bestiarySeen |= (1 << id);
}

int BestiaryIsSeen(int id) {
    if (id < 0 || id >= BESTIARY_COUNT) return 0;
    return (bestiarySeen >> id) & 1;
}

const BestiaryEntry bestiary[BESTIARY_COUNT] = {
    {
        "SOUND HUNTER", "NOISE",
        "HUNTS NOISE. WALKS TO THE LAST SOUND IT HEARD.",
        "SPRINTING / SEARCHING / CRANKING MAKE NOISE. WALKING IS QUIET - BUT AFTER 5S IT WHISPERS."
    },
    {
        "LIGHT BEAST", "LIGHT",
        "CHASES YOUR FLASHLIGHT. FASTER THAN THE SOUND HUNTER.",
        "KILL THE LIGHT. IT LOSES YOU AND WANDERS OFF."
    },
    {
        "BATTERY LEECH", "DRAIN",
        "SMALL, SILENT, ONLY VISIBLE WHEN LIT. SIPS BATTERY WITHIN 3.5 TILES.",
        "SHINE ON IT FOR 0.6s AND IT FLEES. WATCH THE DRAIN WARNING."
    },
    {
        "FACE JUMPER", "QTE",
        "LEAPS AT YOUR FACE. TWO WINDOWS: DODGE, THEN PRESS THE SHOWN KEY.",
        "SPACE TO DODGE. MISS BOTH AND YOU LOSE SANITY."
    },
};


/* v2.0：图鉴是否全开（成就用） */
int BestiarySeenAll(void) {
    for (int i = 0; i < BESTIARY_COUNT; i++)
        if (!BestiaryIsSeen(i)) return 0;
    return 1;
}

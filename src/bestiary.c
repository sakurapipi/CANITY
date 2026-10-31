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
        "IT LIVES BY EARS. WALKS TO THE LAST SOUND IT HEARD.",
        "IT CANNOT SEE. WALK QUIETLY, THROW SOMETHING FAR, AND IT GOES THERE INSTEAD."
    },
    {
        "LIGHT BEAST", "LIGHT",
        "IT ONLY SEES YOUR FLASHLIGHT. FAST.",
        "KILL THE LIGHT AND IT WANDERS OFF. HIDING WORKS TOO."
    },
};


/* v2.0：图鉴是否全开（成就用） */
int BestiarySeenAll(void) {
    for (int i = 0; i < BESTIARY_COUNT; i++)
        if (!BestiaryIsSeen(i)) return 0;
    return 1;
}

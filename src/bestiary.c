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
        "IT CANNOT SEE WELL. WALK QUIETLY, THROW SOMETHING FAR, AND IT GOES THERE INSTEAD. UNKILLABLE - DO NOT TRY."
    },
    {
        "LIGHT BEAST", "LIGHT",
        "IT ONLY SEES YOUR FLASHLIGHT. FAST.",
        "KILL THE LIGHT AND IT WANDERS OFF. HIDING WORKS TOO. WITH THE LIGHT ON IT WILL ALWAYS FIND YOU."
    },
    {
        "BATTERY LEECH", "DRAIN",
        "SMALL, SILENT, ONLY VISIBLE WHEN LIT. SIPS BATTERY WITHIN 3.5 TILES.",
        "SHINE ON IT FOR 0.6s AND IT FLEES. IF THE BATTERY DIES YOU ARE BLIND - AND BLIND IS DEAD."
    },
    {
        "FACE JUMPER", "QTE",
        "LURKS NEXT TO COVER. SPRINGS WHEN YOU COME CLOSE.",
        "BACK OFF THE MOMENT YOU HEAR IT. PRESS SPACE TO BREAK FREE. NEVER APPROACH COVER IT IS SITTING ON."
    },
    {
        "SHADE", "DARK",
        "ONLY EXISTS WHILE YOUR LIGHT IS OFF. SLOW, BUT IT NEVER STOPS.",
        "TURN THE LIGHT ON AND IT VANISHES. IT CANNOT ENTER LIT GROUND."
    },
    {
        "SPIDER", "SPEED",
        "BLIND AND FAST. DASHES TOWARD THE LAST NOISE, THEN FREEZES.",
        "IT CANNOT TRACK YOU ONCE MOVING SILENTLY. WAIT FOR IT TO FREEZE, THEN WALK - DO NOT RUN."
    },
    {
        "ECHO", "TELEPORT",
        "IT DOES NOT WALK. IT BLINKS TO A SPOT NEAR YOU EVERY FEW SECONDS.",
        "STAY MOVING. ITS CONTACT HURTS MORE THAN THE OTHERS."
    },
    {
        "BOSS", "ALL",
        "THE ONE THAT WAS WAITING. EIGHT TIMES THE BODY, NO MERCY.",
        "KILL IT TO POWER THE BREAKER. THERE IS NO WAY AROUND IT."
    },
};


/* v2.0：图鉴是否全开（成就用） */
int BestiarySeenAll(void) {
    for (int i = 0; i < BESTIARY_COUNT; i++)
        if (!BestiaryIsSeen(i)) return 0;
    return 1;
}

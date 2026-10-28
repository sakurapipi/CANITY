/*
 * Canity — 怪物图鉴（BESTIARY）
 *
 * 图上不放贴图，只放"玩家该知道的资料"：名字、类型、威胁点、怎么对付。
 * 加一种怪 = 加一行，不用碰 UI。
 */
#ifndef CANITY_BESTIARY_H
#define CANITY_BESTIARY_H

#include "common.h"

typedef struct {
    const char* name;    /* 显示名 */
    const char* tag;     /* 类型标签，如 "NOISE" / "LIGHT" / "DRAIN" */
    const char* threat;  /* 它会干什么 */
    const char* counter; /* 怎么对付 */
} BestiaryEntry;

#define BESTIARY_COUNT 8   /* v2.2：4 -> 8（影子/盲螨/回响/Boss） */

extern const BestiaryEntry bestiary[BESTIARY_COUNT];
extern int bestiarySel;

/* 见过哪些怪（位掩码，进存档）。没见过 → 图鉴里显示 ??? */
extern int bestiarySeen;
void BestiaryMarkSeen(int id);
int  BestiaryIsSeen(int id);
int  BestiarySeenAll(void);   /* v2.0 */

#endif /* CANITY_BESTIARY_H */

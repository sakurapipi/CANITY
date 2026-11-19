#ifndef CANITY_EVENTS_H
#define CANITY_EVENTS_H

typedef struct {
    const char *nameEn, *nameZh, *shortEn, *shortZh;
    const char *descEn, *descZh, *tipEn, *tipZh;
    int unlock, weight, score, goldPct;
    float hearing;
    unsigned color;
} EventDef;

const EventDef *EventDefinition(int id);

#endif

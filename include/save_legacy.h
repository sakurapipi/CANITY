#ifndef CANITY_SAVE_LEGACY_H
#define CANITY_SAVE_LEGACY_H
#include "save.h"

int LoadLegacyBlob(const unsigned char *src, size_t size, int containerBytes,
                   int containerCount, int mapW, int mapH, SaveBlob *out);
size_t LegacyValidBytes(size_t oldSize, int containerBytes, int containerCount,
                        int mapW, int mapH);
#endif

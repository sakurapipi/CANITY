#include "common.h"
#include "save_legacy.h"

/* 计算"老布局"各段的偏移与长度。返回 0 = 不合理。 */
static int LegacySpan(int containerBytes, int containerCount, int mapW, int mapH,
                      size_t* preLen, size_t* oldContSz, size_t* oldMapSz,
                      size_t* oldExplSz, size_t* oldTailOff) {
    if (containerBytes != 32 && containerBytes != (int)sizeof(Container)) return 0;
    if (containerCount <= 0 || containerCount > MAX_CONTAINERS) return 0;
    if (mapW <= 0 || mapH <= 0 || mapW > MAP_WIDTH || mapH > MAP_HEIGHT) return 0;

    size_t pre = offsetof(SaveBlob, cont);
    size_t contSz = (size_t)containerBytes * (size_t)containerCount;
    size_t mapSz  = (size_t)mapW * (size_t)mapH * sizeof(int);
    size_t explSz = mapSz;

    if (preLen)     *preLen = pre;
    if (oldContSz)  *oldContSz = contSz;
    if (oldMapSz)   *oldMapSz = mapSz;
    if (oldExplSz)  *oldExplSz = explSz;
    if (oldTailOff) *oldTailOff = pre + contSz + mapSz + explSz;
    return 1;
}

int LoadLegacyBlob(const unsigned char* src, size_t size,
                          int containerBytes, int containerCount,
                          int mapW, int mapH, SaveBlob* out) {
    size_t preLen = 0, oldContSz = 0, oldMapSz = 0, oldExplSz = 0, oldTailOff = 0;
    if (!src || !out || size < 16) return 0;
    if (!LegacySpan(containerBytes, containerCount, mapW, mapH,
                    &preLen, &oldContSz, &oldMapSz, &oldExplSz, &oldTailOff)) return 0;

    memset(out, 0, sizeof(*out));

    /* ---- ① 前缀（cont 之前两种布局完全相同） ---- */
    memcpy(out, src, (preLen < size) ? preLen : size);

    /* ---- ② cont[] 逐字段展开 ---- */
    {
        size_t off = preLen;
        for (int i = 0; i < containerCount && i < MAX_CONTAINERS; i++) {
            if (off + (size_t)containerBytes > size) break;
            const unsigned char* p = src + off;
            if (containerBytes == (int)sizeof(Container)) {
                memcpy(&out->cont[i], p, sizeof(Container));
            } else {
                memcpy(&out->cont[i].x,        p + 0,  sizeof(float));
                memcpy(&out->cont[i].y,        p + 4,  sizeof(float));
                memcpy(&out->cont[i].active,   p + 8,  sizeof(int));
                memcpy(&out->cont[i].searched, p + 12, sizeof(int));
                memcpy(&out->cont[i].content,  p + 16, sizeof(int));
                memcpy(&out->cont[i].locked,   p + 20, sizeof(int));
                memcpy(&out->cont[i].lockType, p + 24, sizeof(int));
                memcpy(&out->cont[i].lockCode, p + 28, sizeof(int));
                out->cont[i].mimicShell = 0;
                out->cont[i].mimicHost  = 0;
            }
            off += (size_t)containerBytes;
        }
    }

    /* ---- ③ worldMap：24 宽 -> 36 宽，逐格重排 ---- */
    {
        size_t oldMapOff = preLen + oldContSz;
        size_t newMapOff = offsetof(SaveBlob, worldMap);
        if (oldMapOff != newMapOff && oldMapSz != 0) {
            /* 偏移不同也没关系：我们从 src 的 oldMapOff 读、写到结构里的 worldMap */
        }
        for (int y = 0; y < mapH; y++) {
            for (int x = 0; x < mapW; x++) {
                size_t o = oldMapOff + ((size_t)y * (size_t)mapW + (size_t)x) * sizeof(int);
                if (o + sizeof(int) > size) continue;
                int v = 0;
                memcpy(&v, src + o, sizeof(int));
                out->worldMap[y][x] = v;
            }
        }
        /* 新地图多出来的区域：一律当作**墙** —— 老档没有那些格子，
           给 0 会造出一片穿越地图边界的空地。 */
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++) {
                if (x < mapW && y < mapH) continue;
                out->worldMap[y][x] = 1;
            }
    }

    /* ---- ④ explored：同样逐格重排，多出来的留 0（未探索） ---- */
    {
        size_t oldExplOff = preLen + oldContSz + oldMapSz;
        for (int y = 0; y < mapH; y++)
            for (int x = 0; x < mapW; x++) {
                size_t o = oldExplOff + ((size_t)y * (size_t)mapW + (size_t)x) * sizeof(int);
                if (o + sizeof(int) > size) continue;
                int v = 0;
                memcpy(&v, src + o, sizeof(int));
                out->explored[y][x] = v;
            }
    }

    /* ---- ⑤ 尾部整体后移 ---- */
    {
        size_t newTailOff = offsetof(SaveBlob, flares);
        if (size > oldTailOff) {
            size_t n = size - oldTailOff;
            size_t cap = sizeof(SaveBlob) - newTailOff;
            if (n > cap) n = cap;
            memcpy((unsigned char*)out + newTailOff, src + oldTailOff, n);
        }
    }
    return 1;
}

/* 迁移之后，"当前布局里有多少字节是有效的" */
size_t LegacyValidBytes(size_t oldSize, int containerBytes, int containerCount,
                               int mapW, int mapH) {
    size_t oldTailOff = 0;
    if (!LegacySpan(containerBytes, containerCount, mapW, mapH,
                    NULL, NULL, NULL, NULL, &oldTailOff)) return 0;
    size_t newTailOff = offsetof(SaveBlob, flares);
    size_t tail = (oldSize > oldTailOff) ? (oldSize - oldTailOff) : 0;
    size_t v = newTailOff + tail;
    if (v > sizeof(SaveBlob)) v = sizeof(SaveBlob);
    return v;
}

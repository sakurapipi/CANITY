#include "evolution_save.h"
/* Pure validation shared by local loading and cloud admission. */
#include "common.h"
#include "save.h"
#include "save_format.h"

static size_t MinimumBlobBytes(int version) {
    if (version <= 31) {
        size_t width = version == 31 ? 36u : 24u;
        size_t container = version <= 28 ? 32u : 40u;
        return offsetof(SaveBlob, cont) + 8u * container + 2u * width * width * sizeof(int);
    }
    switch (version) {
    case 32: return offsetof(SaveBlob, flares);
    case 33: return offsetof(SaveBlob, echoBolts);
    case 34: return offsetof(SaveBlob, rngVersion);
    case 35: return offsetof(SaveBlob, difficultyPlus1);
    case 36: return offsetof(SaveBlob, archetypeRanks);
    case 37: return offsetof(SaveBlob, bestiaryExtraKnowledge);
    case 38: return offsetof(SaveBlob, metaExpansionRanks);
    case 39: return offsetof(SaveBlob, commonSkillRanks);
    case 40: return offsetof(SaveBlob, trainingGraduated);
    case 41: return offsetof(SaveBlob, serviceUsesV42);
    case 42: return offsetof(SaveBlob, decorV43);
    case 43: return offsetof(SaveBlob, collectorRunV44);
    case 44: return offsetof(SaveBlob, moneyBagPositionsV45);
    case 45: return offsetof(SaveBlob, pitchLook); /* Original v45 lacked this optional setting. */
    case 46: return sizeof(SaveBlob);
    default: return SIZE_MAX;
    }
}

static int BlobValid(const void* bytes, size_t length) {
    unsigned magic; int version;
    if (!bytes || length < 8) return 0;
    memcpy(&magic, bytes, 4); memcpy(&version, (const char*)bytes + 4, 4);
    return magic == SAVE_MAGIC && version >= 25 && version <= SAVE_VERSION &&
           length >= MinimumBlobBytes(version) && length <= sizeof(SaveBlob);
}

static int KnownSchemaSupported(unsigned short id, unsigned short schema) {
    switch (id) {
    case SC_SETTINGS: case SC_LOBBY: case SC_RUN: case SC_WORLD: case SC_LEGACY29:
        return schema == 1;
    default: return 1; /* Unknown IDs remain opaque and byte-preserved. */
    }
}

int SaveValidateBytes(const void* bytes, size_t length) {
    if (!bytes || length < 8 || length > SAVE_MAX_FILE_BYTES) return 0;
    unsigned magic; memcpy(&magic, bytes, 4);
    if (magic == SAVE_MAGIC) return BlobValid(bytes, length);
    if (magic != SAVE_ENVELOPE_MAGIC || length <= sizeof(SaveEnvelope)) return 0;
    SaveEnvelope envelope; memcpy(&envelope, bytes, sizeof(envelope));
    const unsigned char* payload = (const unsigned char*)bytes + sizeof(envelope);
    int payloadBytes = (int)(length - sizeof(envelope));
    if (!SaveEnvelopeValid(&envelope, payload, payloadBytes)) return 0;
    int count = SaveContainerCount(payload, payloadBytes);
    if (count < 1 || count > SAVE_MAX_CHUNKS) return 0;
    unsigned short ids[SAVE_MAX_CHUNKS];
    size_t used = SAVE_PAYLOAD_HEAD_SIZE + (size_t)count * sizeof(SaveChunkDesc);
    int legacy = 0;
    for (int i = 0; i < count; i++) {
        SaveChunkDesc desc; const void* body = NULL;
        if (!SaveContainerAt(payload, payloadBytes, i, &desc, &body)) return 0;
        for (int j = 0; j < i; j++) if (ids[j] == desc.id) return 0;
        ids[i] = desc.id;
        if (!KnownSchemaSupported(desc.id, desc.schema) || !EvolutionChunkValid(desc.id,desc.schema,body,(int)desc.size)) return 0;
        if (desc.id == SC_PROTOCOL_CHECKPOINT && (desc.schema != 1 || !BlobValid(body, desc.size))) return 0;
        if (desc.id == SC_DAILY_CLEAR && desc.schema == 1 && desc.size != 4) return 0;
        if (desc.id == SC_FLOOR_RULES && desc.schema == 1) {
            if (desc.size != SAVE_FLOOR_RULES_BYTES || *(const unsigned char*)body > EVENT_COUNT)
                return 0;
            float budget;
            memcpy(&budget, (const unsigned char*)body + 1, sizeof budget);
            if (!isfinite(budget) || budget < 0 || budget > FLARE_HEAL_FLOOR_BUDGET)
                return 0;
        }
        used += desc.size;
        if (desc.id == SC_LEGACY29) {
            if (desc.schema != 1 || !BlobValid(body, desc.size)) return 0;
            legacy++;
        }
    }
    return legacy == 1 && used == (size_t)payloadBytes;
}

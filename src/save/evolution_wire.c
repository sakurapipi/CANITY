/* Fixed little-endian words; C struct padding never becomes a file format. */
#include "common.h"
#include "evolution_save.h"
#include "save_format.h"
typedef char EvolutionFloatIs32Bit[sizeof(float) == 4 ? 1 : -1];
typedef struct {
    unsigned char *out;
    const unsigned char *in;
    int at, ok;
} Wire;
static void Put(Wire *w, unsigned v) {
    for (int i = 0; i < 4; i++)
        w->out[w->at++] = (unsigned char)(v >> (8 * i));
}
static unsigned Get(Wire *w) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++)
        v |= (unsigned)w->in[w->at++] << (8 * i);
    return v;
}
static void FloatPut(Wire *w, float f) {
    unsigned v;
    memcpy(&v, &f, 4);
    Put(w, v);
}
static float FloatGet(Wire *w) {
    unsigned v = Get(w);
    float f;
    memcpy(&f, &v, 4);
    if (!isfinite(f) || fabsf(f) > 4096)
        w->ok = 0;
    return f;
}
static void ActorPut(Wire *w, const EvoActor *a) {
    FloatPut(w, a->x);
    FloatPut(w, a->y);
    FloatPut(w, a->timer);
    FloatPut(w, a->cooldown);
    FloatPut(w, a->aux);
    FloatPut(w, a->aimX);
    FloatPut(w, a->aimY);
    FloatPut(w, a->signal);
    FloatPut(w, a->resist);
    Put(w, a->flags);
    Put(w, a->event);
    Put(w, (unsigned)a->phase);
    Put(w, (unsigned)a->count);
}
static void ActorGet(Wire *w, EvoActor *a) {
    a->x = FloatGet(w);
    a->y = FloatGet(w);
    a->timer = FloatGet(w);
    a->cooldown = FloatGet(w);
    a->aux = FloatGet(w);
    a->aimX = FloatGet(w);
    a->aimY = FloatGet(w);
    a->signal = FloatGet(w);
    a->resist = FloatGet(w);
    a->flags = Get(w);
    a->event = Get(w);
    a->phase = (int)Get(w);
    a->count = (int)Get(w);
    if (a->x < 0 || a->y < 0 || a->x >= MAP_WIDTH || a->y >= MAP_HEIGHT || fabsf(a->aimX) > 64 ||
        fabsf(a->aimY) > 64 || a->timer < -64 || a->timer > 90 || a->aux < -64 || a->aux > 90 ||
        a->signal < 0 || a->signal > 64 || a->phase < 0 || a->phase > 5 || a->count < 0 ||
        a->count > 100000 || a->cooldown < 0 || a->cooldown > 64 || a->resist < 0 || a->resist > 64)
        w->ok = 0;
}
int EvolutionWireEncode(const EvolutionSnapshot *v, void *bytes, int cap) {
    if (!v || !bytes || cap < EVOLUTION_WIRE_BYTES)
        return 0;
    Wire w = {(unsigned char *)bytes, NULL, 0, 1};
    Put(&w, 0x314F5645);
    Put(&w, 1);
    Put(&w, (unsigned)v->floor);
    Put(&w, (unsigned)v->tier);
    FloatPut(&w, v->plan);
    Put(&w, v->state.sequence);
    Put(&w, (unsigned)v->state.sharedCursor);
    Put(&w, (unsigned)(v->state.previousHide + 1));
    for (int i = 0; i < EVO_ACTORS; i++)
        ActorPut(&w, &v->state.actors[i]);
    for (int i = 0; i < EVO_ZONES; i++) {
        const EvoZone *z = &v->state.zones[i];
        FloatPut(&w, z->x);
        FloatPut(&w, z->y);
        FloatPut(&w, z->radius);
        FloatPut(&w, z->ttl);
        Put(&w, (unsigned)z->kind);
    }
    for (int i = 0; i < EVO_HIDE_MEMORIES; i++) {
        Put(&w, (unsigned)(v->state.hides[i].spot + 1));
        FloatPut(&w, v->state.hides[i].ttl);
    }
    for (int i = 0; i < SUSP_MAX; i++)
        Put(&w, v->state.soundIds[i]);
    for (int i = 0; i < SUSP_MAX; i++)
        Put(&w, v->state.sharedEvents[i]);
    return w.at;
}
int EvolutionWireDecode(EvolutionSnapshot *v, const void *bytes, int length) {
    if (!v || !bytes || length != EVOLUTION_WIRE_BYTES)
        return 0;
    Wire w = {NULL, (const unsigned char *)bytes, 0, 1};
    memset(v, 0, sizeof(*v));
    if (Get(&w) != 0x314F5645 || Get(&w) != 1)
        return 0;
    v->floor = (int)Get(&w);
    v->tier = (int)Get(&w);
    v->plan = FloatGet(&w);
    v->state.sequence = Get(&w);
    unsigned sharedCursor = Get(&w);
    if (sharedCursor >= SUSP_MAX)
        return 0;
    v->state.sharedCursor = (int)sharedCursor;
    v->state.previousHide = (int)Get(&w) - 1;
    if (v->floor < 1 || v->floor > 100000 || v->tier < 0 || v->tier > 3 || v->plan < 0 ||
        v->plan > 64 || v->state.previousHide < -1 ||
        v->state.previousHide >= HIDESPOT_MAX)
        w.ok = 0;
    for (int i = 0; i < EVO_ACTORS; i++)
        ActorGet(&w, &v->state.actors[i]);
    for (int i = 0; i < EVO_ZONES; i++) {
        EvoZone *z = &v->state.zones[i];
        z->x = FloatGet(&w);
        z->y = FloatGet(&w);
        z->radius = FloatGet(&w);
        z->ttl = FloatGet(&w);
        z->kind = (int)Get(&w);
        if (z->x < 0 || z->x >= MAP_WIDTH || z->y < 0 || z->y >= MAP_HEIGHT || z->radius < 0 ||
            z->radius > 3 || z->ttl < 0 || z->ttl > 8 || z->kind < 0 || z->kind > 2)
            w.ok = 0;
    }
    for (int i = 0; i < EVO_HIDE_MEMORIES; i++) {
        v->state.hides[i].spot = (int)Get(&w) - 1;
        v->state.hides[i].ttl = FloatGet(&w);
        if (v->state.hides[i].spot < -1 || v->state.hides[i].spot >= HIDESPOT_MAX ||
            v->state.hides[i].ttl < 0 || v->state.hides[i].ttl > 12)
            w.ok = 0;
    }
    for (int i = 0; i < SUSP_MAX; i++)
        v->state.soundIds[i] = Get(&w);
    for (int i = 0; i < SUSP_MAX; i++)
        v->state.sharedEvents[i] = Get(&w);
    return w.ok && w.at == length;
}
int TacticalWireEncode(const TacticalState *t, const Shot *shots, void *bytes, int cap) {
    if (!t || !shots || !bytes || cap < TACTICAL_WIRE_BYTES)
        return 0;
    Wire w = {(unsigned char *)bytes, NULL, 0, 1};
    Put(&w, 0x314C4354);
    Put(&w, 1);
    for (int i = 0; i < 7; i++)
        Put(&w, (unsigned)t->inventory[i]);
    for (int i = 0; i < 4; i++)
        Put(&w, (unsigned)t->quick[i]);
    for (int i = 0; i < 4; i++)
        Put(&w, (unsigned)t->key[i]);
    Put(&w, (unsigned)t->assistEscape);
    for (int i = 0; i < 2; i++) {
        FloatPut(&w, t->acoustic[i].x);
        FloatPut(&w, t->acoustic[i].y);
        FloatPut(&w, t->acoustic[i].ttl);
        Put(&w, (unsigned)t->acoustic[i].kind);
    }
    FloatPut(&w, t->solvent.x);
    FloatPut(&w, t->solvent.y);
    FloatPut(&w, t->solvent.ttl);
    Put(&w, (unsigned)t->solvent.kind);
    for (int i = 0; i < 12; i++)
        FloatPut(&w, t->dust[i]);
    FloatPut(&w, t->prismFlicker);
    for (int i = 0; i < MAX_SHOTS; i++) {
        const Shot *s = &shots[i];
        int active =
            s->active && s->kind >= TOOL_SHOT_FIRST && s->kind < TOOL_SHOT_FIRST + TOOL_COUNT;
        Put(&w, (unsigned)active);
        Put(&w, (unsigned)(active ? s->kind : TOOL_SHOT_FIRST));
        FloatPut(&w, active ? s->x : 0);
        FloatPut(&w, active ? s->y : 0);
        FloatPut(&w, active ? s->z : 0);
        FloatPut(&w, active ? s->vx : 0);
        FloatPut(&w, active ? s->vy : 0);
        FloatPut(&w, active ? s->vz : 0);
        FloatPut(&w, active ? s->life : 0);
        FloatPut(&w, active ? s->age : 0);
        FloatPut(&w, active ? s->charge : 0);
    }
    return w.at;
}
int TacticalWireDecode(TacticalState *t, Shot *shots, const void *bytes, int length) {
    if (!t || !shots || !bytes || length != TACTICAL_WIRE_BYTES)
        return 0;
    Wire w = {NULL, (const unsigned char *)bytes, 0, 1};
    memset(t, 0, sizeof(*t));
    memset(shots, 0, sizeof(Shot) * MAX_SHOTS);
    if (Get(&w) != 0x314C4354 || Get(&w) != 1)
        return 0;
    for (int i = 0; i < 7; i++) {
        t->inventory[i] = (int)Get(&w);
        if (t->inventory[i] < 0 || t->inventory[i] > (i == TOOL_REVEAL ? 3 : 2))
            w.ok = 0;
    }
    for (int i = 0; i < 4; i++) {
        t->quick[i] = (int)Get(&w);
        if (t->quick[i] < 0 || t->quick[i] >= 14)
            w.ok = 0;
    }
    for (int i = 0; i < 4; i++) {
        t->key[i] = (int)Get(&w);
        if (t->key[i] < 0 || t->key[i] >= 256)
            w.ok = 0;
        for (int j = 0; j < i; j++)
            if (t->key[i] != 0 && t->key[i] == t->key[j])
                w.ok = 0;
    }
    t->assistEscape = (int)Get(&w);
    if (t->assistEscape < 0 || t->assistEscape > 1)
        w.ok = 0;
    for (int i = 0; i < 2; i++) {
        ToolZone *z = &t->acoustic[i];
        z->x = FloatGet(&w);
        z->y = FloatGet(&w);
        z->ttl = FloatGet(&w);
        z->kind = (int)Get(&w);
        if (z->x < 0 || z->y < 0 || z->x >= MAP_WIDTH || z->y >= MAP_HEIGHT || z->ttl < 0 ||
            z->ttl > 16 || z->kind != TOOL_ACOUSTIC)
            w.ok = 0;
    }
    t->solvent.x = FloatGet(&w);
    t->solvent.y = FloatGet(&w);
    t->solvent.ttl = FloatGet(&w);
    t->solvent.kind = (int)Get(&w);
    if (t->solvent.x < 0 || t->solvent.y < 0 || t->solvent.x >= MAP_WIDTH ||
        t->solvent.y >= MAP_HEIGHT || t->solvent.ttl < 0 || t->solvent.ttl > 1.5f ||
        (t->solvent.ttl > 0 && t->solvent.kind != TOOL_SOLVENT))
        w.ok = 0;
    for (int i = 0; i < 12; i++) {
        t->dust[i] = FloatGet(&w);
        if (t->dust[i] < 0 || t->dust[i] > 16)
            w.ok = 0;
    }
    t->prismFlicker = FloatGet(&w);
    if (t->prismFlicker < 0 || t->prismFlicker > .5f)
        w.ok = 0;
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot *s = &shots[i];
        s->active = (int)Get(&w);
        s->kind = (int)Get(&w);
        s->x = FloatGet(&w);
        s->y = FloatGet(&w);
        s->z = FloatGet(&w);
        s->vx = FloatGet(&w);
        s->vy = FloatGet(&w);
        s->vz = FloatGet(&w);
        s->life = FloatGet(&w);
        s->age = FloatGet(&w);
        s->charge = FloatGet(&w);
        s->ignoreTarget = -1;
        if (s->active < 0 || s->active > 1 || s->kind < 32 || s->kind >= 39 ||
            (s->active && (s->x < 1 || s->y < 1 || s->x >= MAP_WIDTH - 1 ||
                           s->y >= MAP_HEIGHT - 1 || s->z < 0 || s->z > 4 || fabsf(s->vx) > 32 ||
                           fabsf(s->vy) > 32 || fabsf(s->vz) > 32 || s->life <= 0 || s->life > 2 ||
                           s->age < 0 || s->age > 2 || s->charge < .1f || s->charge > 1)))
            w.ok = 0;
    }
    return w.ok && w.at == length;
}
int EvolutionChunkValid(unsigned short id, unsigned short schema, const void *bytes, int length) {
    if (id != SC_EVOLUTION_V1 && id != SC_TACTICAL_ITEMS_V1)
        return 1;
    if (schema > 1)
        return 1; /* Future schemas remain opaque; the writer refuses replacement. */
    if (schema != 1)
        return 0;
    if (id == SC_EVOLUTION_V1) {
        EvolutionSnapshot state;
        return EvolutionWireDecode(&state, bytes, length);
    }
    TacticalState state;
    Shot shots[MAX_SHOTS];
    return TacticalWireDecode(&state, shots, bytes, length);
}

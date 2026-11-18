#include "decoy_runtime.h"
#include "world_api.h"
#include "state.h"
#include "present_api.h"
#include "item_effects.h"
#include "lang.h"
#include "audio.h"
#include "tactical_tools.h"
#include "sound_event.h"
#include "sound_propagation.h"
#include "extras.h"
#include "entity.h"
#include "spider.h"
#include "warden.h"
#include "mimic.h"

Decoy decoysArr[MAX_DECOY];

void PlaceDecoy(float x, float y) {
    int slot = -1;
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        /* 顶掉最老的那个 */
        float oldest = 1e9f;
        for (int i = 0; i < MAX_DECOY; i++)
            if (decoysArr[i].life < oldest) { oldest = decoysArr[i].life; slot = i; }
    }
    if (slot < 0) return;
    decoysArr[slot].x = x;
    decoysArr[slot].y = y;
    decoysArr[slot].life = DECOY_LIFE;
    decoysArr[slot].shoutT = DECOY_SHOUT_EVERY;
    decoysArr[slot].hp = DECOY_HP;
    decoysArr[slot].active = 1;
    decoysArr[slot].armed = 0;
}

void ArmDecoy(void) {
    /* 激活最近的一个未激活玩偶 */
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active || decoysArr[i].armed) continue;
        float d = Dist2(decoysArr[i].x, decoysArr[i].y, posX, posY);
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) {
        decoysArr[best].armed = 1;
        decoysArr[best].shoutT = 0.0f;   /* 激活立刻叫第一声 */
    }
}

void ShatterDecoy(int slot) {
    if ((unsigned)slot>=MAX_DECOY || !decoysArr[slot].active) return;
    Decoy *decoy = &decoysArr[slot];
    decoy->active = decoy->armed = 0;
    decoy->hp = 0;
    AudioPlayWorld(SFX_GLASS, decoy->x, decoy->y, .84f, 17, 3);
    UiShowActionError(Tr("DECOY DESTROYED","玩偶已被击毁"));
    uiDirty = 1;
}

int DecoyHitNear(float x,float y) {
    int best=-1;float nearest=.49f;
    for (int i=0;i<MAX_DECOY;i++) {
        const Decoy *d=&decoysArr[i];
        if (!d->active || Dist2(x,y,d->x,d->y)>nearest || !HasLOS(x,y,d->x,d->y)) continue;
        best=i;nearest=Dist2(x,y,d->x,d->y);
    }
    if(best<0)return 0;
    ShatterDecoy(best);
    return 1;
}

void DecoyTick(float dt) {
    if (SpiderActive()) DecoyHitNear(SpiderX(), SpiderY());
    if (CollectorActive()) DecoyHitNear(CollectorX(), CollectorY());
    if (ScavengerActive()) DecoyHitNear(ScavengerX(), ScavengerY());
    if (WardenActive()) DecoyHitNear(WardenX(), WardenY());
    for (int i = 0; i < MIM_MAX; i++)
        if (mimics[i].state == MIM_ANGRY) DecoyHitNear(mimics[i].x, mimics[i].y);

    /* v3.3：诱饵心跳 —— 每 DECOY_SHOUT_EVERY 秒叫一次，把怪引过去 */
    for (int i = 0; i < MAX_DECOY; i++) {
        if (!decoysArr[i].active) continue;
        decoysArr[i].life -= dt;
        if (decoysArr[i].life <= 0.0f) { decoysArr[i].active = 0; continue; }
        if (!decoysArr[i].armed) continue;
        decoysArr[i].shoutT -= dt;
        if (decoysArr[i].shoutT <= 0.0f) {
            decoysArr[i].shoutT = DECOY_SHOUT_EVERY;
            float strength=95.f*TacticalNoiseMul(decoysArr[i].x,decoysArr[i].y);
            if (strength > 0) {
                SoundEvent ev={.kind=SK_VOICE,.x=decoysArr[i].x,.y=decoysArr[i].y,
                    .strength=strength,.decay=5,.ttl=DECOY_SHOUT_EVERY+.35f,
                    .maxRadius=DECOY_SHOUT_R,.sourceEntityId=-1};
                SoundEmit(&ev);
                SoundPropBuild(ev.x,ev.y);
                for (int j=0;j<EXTRA_MAX;j++) {
                    Extra *actor=&extras[j];
                    if (!actor->active || actor->kind!=1 ||
                        Dist2(actor->x,actor->y,ev.x,ev.y)>=DECOY_SHOUT_R*DECOY_SHOUT_R ||
                        SoundPropCostAt(actor->x,actor->y)==PROP_UNREACHABLE) continue;
                    actor->hasTarget=1;actor->tx=ev.x;actor->ty=ev.y;actor->timer=0;
                }
            }
            AudioPlayWorld(SFX_DECOY,decoysArr[i].x,decoysArr[i].y,.70f,DECOY_SHOUT_R,3);
        }
    }
}

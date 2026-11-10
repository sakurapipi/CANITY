#include "common.h"
#include "run_economy.h"
#include "entity.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "wallet.h"
#include "bench.h"
#include "state.h"
#include "control_effect.h"
#include "bestiary.h"
#include "skills.h"
#include "relics.h"
#include "room_theme.h"
typedef struct {float engaged,quiet;int armed;float contactCooldown;} PursuitCredit;
static PursuitCredit credit[MON_HUNTER_MAX+LIGHTBEAST_MAX];
static int ledgerFloor=0,rhythmPaid=0;
static unsigned paidMask=0;
static void BindFloor(void){if(ledgerFloor!=currentLevel){ledgerFloor=currentLevel;paidMask=0;rhythmPaid=0;memset(credit,0,sizeof(credit));}}
void RunEconomyReset(void){ledgerFloor=0;paidMask=0;rhythmPaid=0;memset(credit,0,sizeof(credit));}
void RunEconomyTick(float dt){
    if(trainingMode || currentState!=STATE_PLAYING || !(dt>0))return;
    BindFloor();
    for(int i=0;i<MON_HUNTER_MAX+LIGHTBEAST_MAX;i++){
        Monster* m=i<MON_HUNTER_MAX?&monHunters[i]:&lightBeasts[i-MON_HUNTER_MAX];
        PursuitCredit* c=&credit[i];
        if(!m->active || (paidMask&(1u<<i))){memset(c,0,sizeof(*c));continue;}
        float distance=Dist2(m->x,m->y,posX,posY);
        int sight=HasLOS(m->x,m->y,posX,posY);
        if(ControlActive() || m->hitCooldown>c->contactCooldown+.1f){memset(c,0,sizeof(*c));c->contactCooldown=m->hitCooldown;continue;}
        c->contactCooldown=m->hitCooldown;
        int targeted=m->state==1 && m->stun<=0 && hiddenIn<0 && distance<64 && sight && Dist2(m->tx,m->ty,posX,posY)<4;
        if(targeted){c->engaged+=dt;c->quiet=0;if(c->engaged>=4)c->armed=1;}
        else if(!c->armed)c->engaged=fmaxf(0,c->engaged-dt*.5f);
        if(c->armed){
            if(m->state==0 && (hiddenIn>=0 || !sight) && distance>=36)c->quiet+=dt;else c->quiet=0;
            if(c->quiet>=6){
                int reward=(int)((35+fminf(65,currentLevel*4))*BenchEscapeGoldMul());
                BestiaryMarkEscaped(i<MON_HUNTER_MAX?BST_ID_HUNTER:BST_ID_LIGHT);
                WalletAddRun(reward);totalGoldEarned+=reward;paidMask|=1u<<i;memset(c,0,sizeof(*c));
            }
        }
    }
}
int RunEconomyRhythmGold(int hits,int bestCombo){
    if(trainingMode || hits<8)return 0;
    BindFloor();
    int amount=12+hits/4+bestCombo/5;
    if(BenchHas(BENCH_POCKET_CHANGE))amount=amount*6/5;
    if(amount>45)amount=45;
    if(rhythmPaid>=90)amount=0;
    else if(amount>90-rhythmPaid)amount=90-rhythmPaid;
    if(amount<0)amount=0;
    rhythmPaid+=amount;if(rhythmPaid>90)rhythmPaid=90;WalletAddRun(amount);totalGoldEarned+=amount;return amount;
}
void RunEconomyCapture(int* floor,unsigned* mask,int* gold){*floor=ledgerFloor;*mask=paidMask;*gold=rhythmPaid;}
void RunEconomyRestore(int floor,unsigned mask,int gold){ledgerFloor=floor;paidMask=mask&(31u|0x80000000u);rhythmPaid=gold<0?0:gold>90?90:gold;memset(credit,0,sizeof(credit));}

void RunEconomyFloorGold(void){
    if(trainingMode)return;
    BindFloor();if(paidMask&0x80000000u)return;
    int reward=(int)(GoldExitAt(currentLevel)*BenchExitGoldMul()*RelicShardMul()*modShardMul*RoomThemeLootMul()*SkillGoldMul()+.5f);
    reward+=containerCount*GOLD_NEWROOM;
    if(reward>0){WalletAddRun(reward);totalGoldEarned+=reward;}
    paidMask|=0x80000000u;
}

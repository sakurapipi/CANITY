#include "common.h"
#include "help_book.h"
#include "tactical_tools.h"
#include "ui_archive.h"
#include "world_api.h"
#include "skills.h"
#include "bestiary.h"
#include "bench.h"
#include "ui.h"
#include "ui_theme.h"
#include "lang.h"
#include "present_api.h"
#include "loop_api.h"
#include "state.h"
static int category=0,selection=0;
static char search[64];
typedef struct {
    const char *titleEn, *titleZh, *bodyEn, *bodyZh, *tipEn, *tipZh;
} HelpLesson;

static const HelpLesson controls[] = {
    {"Movement", "移动与观察", "Move and turn to check a route before sprinting. Sprint spends stamina and alerts hunters.",
     "先观察路口，再决定是否冲刺。冲刺消耗体力，也更容易惊动听声鬼。",
#ifdef CANITY_WEB
     "On touch screens, move with the left stick and look by dragging on the right.",
     "触屏操作时，左摇杆移动，拖动屏幕右侧转向。"},
#else
     "Move with your assigned keys and turn with the mouse.",
     "使用已绑定的方向键移动，用鼠标转向。"},
#endif
    {"Flashlight", "手电与充电", "Toggle the flashlight to avoid light beasts. Cranking restores power but makes noise.",
     "关灯可降低被追光鬼发现的风险。手摇可恢复电量，但会发出声音。",
     "Recharge from cover or at the rhythm console.", "可在藏身点或节律校准台补充电量。"},
    {"Interact", "交互与搜查", "Hold interact at a crate to search it. Locked crates open a puzzle. Use the same action at the powered breaker controls.",
     "靠近箱子后按住交互键搜查。锁箱会打开解谜面板。带保险丝到配电箱前，用同一个键通电。",
     "A stone landing near a visible lock can break it. Enemies hear the impact.", "投石落到视线可达的锁箱附近可以砸锁，冲击声会引来怪物。"},
    {"Throw and hide", "投掷与藏身", "Select a tool, hold throw and release. A loaded stone flies faster and flatter. Tap interact to enter or leave cover.",
     "选中道具，按住投掷键再松开。蓄力投石飞得更快、更平。轻按交互进入或离开藏身点。",
     "Place a decoy, then press C to arm it. Smoke blocks the flashlight beam in its cloud.", "放下傀儡后按 C 激活。进入烟团会失去手电照明。"},
    {"Map and shop", "地图与商店", "Open the map to check explored routes. While hidden, T opens supplies and Space opens rhythm calibration.",
     "打开地图查看已探索路线。藏好后按 T 打开补给，空格打开节律校准。",
     "Calibration: Q W E R. A single click previews a run skill; double-click buys it.", "校准使用 Q W E R。局内技能单击查看，双击购买。"},
};
static const HelpLesson fieldGuide[] = {
    {"First route", "开局路线", "Find cover and locate a return path before searching the floor. Keep one safe option between loot stops.",
     "先找到藏身点并记住退路，再依次搜查。搜下一处之前，确认身后还有安全路线。",
     "New Player Tutorial opens a guided course. B opens the practice arena.", "新手教程提供交互课程。按 B 可进入自由训练场。"},
    {"Read the threat", "识别威胁", "Watch the current threat color. Bank withdrawal and carried gold pressure can raise the next floor's tier.",
     "留意当前威胁颜色。银行取款和携带金币会影响下层威胁档位。",
     "Blue, violet, amber, blood red. Use the bestiary to check the relevant counter.", "冷蓝、蓝紫、琥珀黄、血红。到图鉴查看对应反制。"},
    {"Break pursuit", "甩掉追击", "Turn off light against a light beast. Break line of sight before hiding. Use sound traps to redirect hearing hunters.",
     "对追光鬼关灯。进藏身点之前先甩开视线。用诱饵改写听声鬼的调查位置。",
     "Switch tools in the backpack. A thrown decoy must be armed before it calls.", "在背包切换工具。放下傀儡后还要激活，它才会发声。"},
    {"Power the exit", "接通出口", "Search for the required fuses. Power the breaker, then reach the lit exit.",
     "搜齐所需保险丝，再接通配电箱，最后走到点亮的出口。",
     "1 or Space continues. 2 opens extraction confirmation.", "按 1 或空格继续，按 2 进入撤离确认。"},
    {"Bank and modes", "结算与模式", "Extract to bank your gains, or stay for a deeper floor. Higher tiers return less gold on defeat.",
     "撤离会把收益存入银行，也可继续深入。威胁档越高，失败返还越少。",
     "Today's challenge only advances on today's Daily Run map.", "今日挑战只在当天的每日挑战地图推进。"},
};

static const char *tabEn[]={"CONTROLS","FIELD MANUAL","META SKILLS","BESTIARY","RUN TOOLS"};
static const char *tabZh[]={"基础操作","探索手册","局外技能","怪物应对","局内工具"};

static int Count(void) {
    if (category==2) return SKILL_PLAYER_COUNT;
    if (category==3) return BESTIARY_COUNT;
    if (category==4) return TOOL_COUNT+BENCH_COUNT+INRUN_COUNT;
    return 5;
}
static int PlayerId(int ordinal) {
    for (int id=0;id<SKILL_COUNT;id++)
        if (SkillIsPlayerFacing(id) && ordinal--==0) return id;
    return 25;
}

static const char *EntryTitle(int row) {
    if (category==2) return SkillDisplayName(PlayerId(row));
    if (category==3) return Tr(bestiary[row].nameEn,bestiary[row].nameZh);
    if (category==4) {
        if (row<TOOL_COUNT) {
            return InventoryName(TOOL_FIRST_ITEM+row);
        }
        row-=TOOL_COUNT;
        return row<BENCH_COUNT?Tr(benchNodes[row].name,benchNodes[row].nameZh):
            Tr(inRunDefs[row-BENCH_COUNT].name,inRunDefs[row-BENCH_COUNT].nameZh);
    }
    const HelpLesson *lesson=category?&fieldGuide[row]:&controls[row];
    return Tr(lesson->titleEn,lesson->titleZh);
}

typedef struct {int left,right,tabsY,tabH,top,bottom,split,rowY,rowH,rows,first;} HelpLayout;
static HelpLayout Layout(void) {
    HelpLayout l;
    l.left=UIS(21); l.right=winW-l.left;
    l.tabsY=UIS(70); l.tabH=UIS(40);
    l.top=l.tabsY+l.tabH+UIS(10); l.bottom=winH-UIS(47);
    l.split=l.left+(l.right-l.left)*35/100;
    l.rowY=l.top+UIS(40); l.rowH=UIS(34);
    l.rows=(l.bottom-l.rowY-UIS(8))/l.rowH;
    if (l.rows<1) l.rows=1;
    l.first=selection-l.rows/2;
    if(l.first>Count()-l.rows) l.first=Count()-l.rows;
    if(l.first<0) l.first=0;
    return l;
}
void HelpBookKey(int key) {
    if(key>='1' && key<='5'){category=key-'1';selection=0;search[0]=0;}
    else if(key==VK_ESCAPE){currentState=prevState==STATE_PAUSED?STATE_PAUSED:STATE_MAIN_MENU;uiDirty=1;return;}
    if(key==VK_TAB || key==VK_RIGHT || key==VK_LEFT) {
        category=(category+(key==VK_LEFT?4:1))%5;selection=0;search[0]=0;
    } else if(key==VK_UP || key==VK_DOWN)selection=(selection+Count()+(key==VK_UP?-1:1))%Count();
    else if(category==2 && key==VK_BACK) {size_t n=strlen(search);if(n){n--;while(n>0 && ((unsigned char)search[n]&0xC0)==0x80)n--;search[n]=0;}}
    uiDirty=1;
}
void HelpBookText(unsigned codepoint) {
    if(currentState!=STATE_HELP || category!=2 || codepoint<32 || codepoint>0x10FFFF || (codepoint>=0xD800 && codepoint<=0xDFFF))return;
    if(codepoint>='a' && codepoint<='z')codepoint-=32;
    char text[4];int n=0;
    if(codepoint<128)text[n++]=(char)codepoint;
    else if(codepoint<2048){text[n++]=(char)(0xC0|(codepoint>>6));text[n++]=(char)(0x80|(codepoint&63));}
    else if(codepoint<65536){text[n++]=(char)(0xE0|(codepoint>>12));text[n++]=(char)(0x80|((codepoint>>6)&63));text[n++]=(char)(0x80|(codepoint&63));}
    else {text[n++]=(char)(0xF0|(codepoint>>18));text[n++]=(char)(0x80|((codepoint>>12)&63));text[n++]=(char)(0x80|((codepoint>>6)&63));text[n++]=(char)(0x80|(codepoint&63));}
    size_t used=strlen(search);if(used+n>=sizeof(search))return;
    memcpy(search+used,text,(size_t)n);search[used+n]=0;
    int found=SkillFindByName(search);
    for(int i=0;i<SKILL_PLAYER_COUNT;i++)if(PlayerId(i)==found){selection=i;break;}
    uiDirty=1;
}
static int HelpHeading(const char *text,int x,int y,unsigned color) {
    DrawTextSmallLiteralWin(x,y,text,color);
    return y+UIS(25);
}

static void DrawHelpDetail(HelpLayout l) {
    int rx=l.split+UIS(22), w=l.right-rx-UIS(20), y=l.top+UIS(20);
    int bottom=l.bottom-UIS(14);
    char label[128];
    snprintf(label,sizeof(label),"%s",EntryTitle(selection));
    UiTrimSmallText(label,w);
    DrawTextSmallLiteralWin(rx,y,label,UI_INK_TITLE);
    y+=UIS(30);
    BlendRectWin(rx,y,rx+w,y+UIS(2),UI_EDGE,1);
    y+=UIS(13);
    if (category==2) {
        int id=PlayerId(selection);
        y=HelpHeading(Tr("EFFECT","技能效果"),rx,y,UI_ACCENT);
        y=UiDossierText(SkillDisplayDescription(id),rx,y,w,bottom,UI_INK);
        if (search[0] && y+UIS(38)<bottom) {
            snprintf(label,sizeof(label),Tr("SEARCH: %s","搜索：%s"),search);
            UiTrimSmallText(label,w);
            HelpHeading(label,rx,y+UIS(12),UI_INFO);
        }
        return;
    }
    if (category==3) {
        const BestiaryEntry *b=&bestiary[selection];
        y=HelpHeading(Tr("HOW IT HUNTS","行动规律"),rx,y,UI_WARN);
        y=UiDossierText(Tr(b->behaviorEn,b->behaviorZh),rx,y,w,bottom,UI_INK)+UIS(12);
        if (y+UIS(25)<bottom) {
            y=HelpHeading(Tr("HOW TO COUNTER","反制方法"),rx,y,UI_ACCENT);
            UiDossierText(Tr(b->counterEn,b->counterZh),rx,y,w,bottom,UI_INK);
        }
        return;
    }
    if (category==4) {
        int row=selection;
        const char* desc;
        if (row<TOOL_COUNT) desc=ToolDetail(row);
        else {
            row-=TOOL_COUNT;
            desc=row<BENCH_COUNT?Tr(benchNodes[row].desc,benchNodes[row].descZh):
                Tr(inRunDefs[row-BENCH_COUNT].desc,inRunDefs[row-BENCH_COUNT].descZh);
        }
        y=HelpHeading(Tr("EFFECT","效果"),rx,y,UI_ACCENT);
        UiDossierText(desc,rx,y,w,bottom,UI_INK);
        return;
    }
    const HelpLesson *lesson=category?&fieldGuide[selection]:&controls[selection];
    y=UiDossierText(Tr(lesson->bodyEn,lesson->bodyZh),rx,y,w,bottom,UI_INK)+UIS(12);
    if (category==0 && y+UIS(56)<bottom) {
        static const int actions[5][2]={{ACT_MOVE_FWD,ACT_SPRINT},{ACT_FLASHLIGHT,ACT_CRANK},
            {ACT_INTERACT,-1},{ACT_THROW,ACT_INTERACT},{ACT_MAP,-1}};
        int a=actions[selection][0], b=actions[selection][1];
        char keys[96];
        if(b>=0)snprintf(keys,sizeof(keys),"%s  /  %s",VkName(g_bindings[a]),VkName(g_bindings[b]));
        else snprintf(keys,sizeof(keys),"%s",VkName(g_bindings[a]));
        y=HelpHeading(Tr("CURRENT BINDINGS","当前按键"),rx,y,UI_INK_DIM);
        y=HelpHeading(keys,rx,y,UI_ACCENT)+UIS(7);
    }
    if (y+UIS(25)<bottom) {
        y=HelpHeading(Tr("IN PRACTICE","实战提示"),rx,y,UI_ACCENT);
        UiDossierText(Tr(lesson->tipEn,lesson->tipZh),rx,y,w,bottom,UI_INFO);
    }
}

void HelpBookDraw(void) {
    HelpLayout l=Layout();
    BlendRectWin(0,0,winW,winH,UI_BG_PANEL,1);
    DrawTextMediumLiteralWin(l.left,UIS(20),Tr("FIELD REFERENCE","生存指南"),UI_INK_TITLE);
    int backLeft=l.right-UIS(108);
    UiRow(backLeft,UIS(18),l.right,UIS(57),UI_ROW_NORMAL,1);
    DrawTextSmallLiteralWin(backLeft+UIS(8),UIS(30),Tr("ESC BACK","ESC 返回"),UI_ACCENT);
    for (int tab=0;tab<5;tab++) {
        int left=l.left+(l.right-l.left)*tab/5;
        int right=l.left+(l.right-l.left)*(tab+1)/5;
        UiRow(left,l.tabsY,right-UIS(4),l.tabsY+l.tabH,tab==category?UI_ROW_SEL:UI_ROW_NORMAL,1);
        char name[80];
        snprintf(name,sizeof(name),"%s",Tr(tabEn[tab],tabZh[tab]));
        UiTrimSmallText(name,right-left-UIS(14));
        DrawTextSmallLiteralWin(left+UIS(7),l.tabsY+UIS(12),name,tab==category?UI_ACCENT:UI_INK);
    }
    UiPanel9(l.left,l.top,l.split-UIS(8),l.bottom,0);
    UiPanel9(l.split,l.top,l.right,l.bottom,0);
    char index[96];
    snprintf(index,sizeof(index),Tr("INDEX  %d/%d","索引  %d/%d"),selection+1,Count());
    DrawTextSmallLiteralWin(l.left+UIS(12),l.top+UIS(13),index,UI_INFO);
    for (int i=0;i<l.rows && l.first+i<Count();i++) {
        int row=l.first+i, y=l.rowY+i*l.rowH;
        UiRow(l.left+UIS(6),y,l.split-UIS(14),y+l.rowH-UIS(3),row==selection?UI_ROW_SEL:UI_ROW_NORMAL,1);
        char label[128];
        snprintf(label,sizeof(label),"%s",EntryTitle(row));
        UiTrimSmallText(label,l.split-l.left-UIS(32));
        DrawTextSmallLiteralWin(l.left+UIS(12),y+UIS(9),label,row==selection?UI_ACCENT:UI_INK);
    }
    DrawHelpDetail(l);
    char foot[160];
    snprintf(foot,sizeof(foot),"%s",category==2?
        Tr("TYPE A SKILL NAME TO SEARCH / UP AND DOWN TO BROWSE","输入技能名搜索 / 上下键浏览"):
        Tr("LEFT / RIGHT: CATEGORY   UP / DOWN OR WHEEL: BROWSE","左右键切换分类 / 上下键或滚轮浏览"));
    UiTrimSmallText(foot,l.right-l.left);
    DrawTextSmallLiteralWin(l.left,l.bottom+UIS(13),foot,UI_INK_DIM);
}

void HelpBookClick(int x,int y) {
    HelpLayout l=Layout();
    if (x>=l.right-UIS(108) && x<l.right && y>=UIS(18) && y<UIS(57)) {
        HelpBookKey(VK_ESCAPE);
    } else if(x>=l.left && x<l.right && y>=l.tabsY && y<l.tabsY+l.tabH) {
        int tab=(x-l.left)*5/(l.right-l.left);
        HelpBookKey('1'+tab);
    } else if(x>=l.left && x<l.split && y>=l.rowY && y<l.bottom) {
        int index=(y-l.rowY)/l.rowH;
        if (index<l.rows && l.first+index<Count()) {selection=l.first+index;uiDirty=1;}
    }
}

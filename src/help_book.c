#include "common.h"
#include "help_book.h"
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
static int Count(void) {return category==2?SKILL_PLAYER_COUNT:(category==3?BESTIARY_COUNT:(category==4?BENCH_COUNT+INRUN_COUNT:5));}
static int PlayerId(int ordinal) {for(int i=0;i<SKILL_COUNT;i++)if(SkillIsPlayerFacing(i) && ordinal--==0)return i;return 25;}
static void Paragraph(const char* text,int x,int* y,int width) {
    while(text && *text && *y<winH-UIS(100)) {
        char line[256];int bytes=0,fit=0;
        while(text[bytes] && bytes<240) {
            unsigned char c=(unsigned char)text[bytes];int n=c<128?1:(c<224?2:(c<240?3:4));
            if(bytes+n>=250)break;
            memcpy(line+bytes,text+bytes,(size_t)n);bytes+=n;line[bytes]=0;
            if(TextSmallLiteralWidth(line)>width)break;
            fit=bytes;
        }
        if(!fit)break;
        line[fit]=0;DrawTextSmallLiteralWin(x,*y,line,UI_INK);*y+=UIS(23);text+=fit;
    }
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
void HelpBookDraw(void) {
    static const char* en[5]={"CONTROLS","CORE LOOP","ALL META SKILLS","CREATURES","ECONOMY / RUN SKILLS"};
    static const char* zh[5]={"基础操作","核心循环","全部局外技能","怪物应对","经济与局内技能"};
    int x=UIS(25),top=UIS(100),split=winW*2/5;
    BlendRectWin(0,0,winW,winH,UI_BG_PANEL,1);
    for(int tab=0;tab<5;tab++) {
        int l=UIS(20)+(winW-UIS(40))*tab/5,r=UIS(20)+(winW-UIS(40))*(tab+1)/5-UIS(4);
        UiRow(l,UIS(22),r,UIS(68),tab==category?UI_ROW_SEL:UI_ROW_NORMAL,1);
        char label[128];snprintf(label,sizeof(label),"%d %s",tab+1,LangTr(en[tab],zh[tab]));UiTrimSmallText(label,r-l-UIS(10));
        DrawTextSmallLiteralWin(l+UIS(5),UIS(38),label,tab==category?UI_ACCENT:UI_INK_DIM);
    }
    UiTitle(winW/2,top,LangTr(en[category],zh[category]));
    UiSectionRule(winW/2,top+UIS(42),winW-UIS(90));
    int count=Count(),rows=(winH-UIS(270))/UIS(34);if(rows<1)rows=1;
    int first=selection-rows/2;if(first<0)first=0;
    if(first>count-rows)first=count>rows?count-rows:0;
    for(int i=first;i<count && i<first+rows;i++) {
        const char* title;
        if(category==2)title=SkillDisplayName(PlayerId(i));
        else if(category==3)title=LangTr(bestiary[i].nameEn,bestiary[i].nameZh);
        else if(category==4)title=i<BENCH_COUNT?LangTr(benchNodes[i].name,benchNodes[i].nameZh):LangTr(inRunDefs[i-BENCH_COUNT].name,inRunDefs[i-BENCH_COUNT].nameZh);
        else {static const char* entriesEn[5]={"MOVE / LOOK","LIGHT / RESOURCES","SEARCH / INTERACT","THROW / HIDE","ESCAPE / RECOVERY"};static const char* entriesZh[5]={"移动与观察","照明与资源","搜查与交互","投掷与躲藏","逃脱与恢复"};title=LangTr(entriesEn[i],entriesZh[i]);}
        int y=top+UIS(75)+(i-first)*UIS(34);
        if(i==selection)UiRowSel(x-UIS(8),y-UIS(4),split-UIS(20),y+UIS(28),.8f);
        char label[192];snprintf(label,sizeof(label),"%02d  %s",i+1,title);UiTrimSmallText(label,split-x-UIS(25));
        DrawTextSmallLiteralWin(x,y,label,i==selection?UI_ACCENT:UI_INK);
    }
    int y=top+UIS(80),rx=split+UIS(20),width=winW-rx-UIS(45);
    if(category==2){int id=PlayerId(selection);Paragraph(SkillDisplayName(id),rx,&y,width);y+=UIS(15);Paragraph(SkillDisplayDescription(id),rx,&y,width);y+=UIS(20);Paragraph(skillDefs[id].name,rx,&y,width);}
    else if(category==3){const BestiaryEntry* b=&bestiary[selection];Paragraph(LangTr(b->behaviorEn,b->behaviorZh),rx,&y,width);y+=UIS(20);Paragraph(LangTr(b->counterEn,b->counterZh),rx,&y,width);}
    else if(category==4){Paragraph(selection<BENCH_COUNT?LangTr(benchNodes[selection].desc,benchNodes[selection].descZh):LangTr(inRunDefs[selection-BENCH_COUNT].desc,inRunDefs[selection-BENCH_COUNT].descZh),rx,&y,width);y+=UIS(20);Paragraph(LangTr("Run nodes cost carried gold, reset each run, and raise threat. Bank gold buys permanent meta nodes. A larger withdrawal raises hearing pressure; the bank previews the tradeoff.","局内节点仅花携带金币，每局清空并增加威胁；银行金币购买永久局外节点。携带越多，听觉压力越高，银行可预览风险。"),rx,&y,width);}
    else {static const char* bodyEn[5]={"WASD moves, mouse looks. Shift sprints and uses stamina. The settings page lists your current bindings.","F toggles the flashlight. R cranks the battery. Manage sanity and light separately; darkness hides you from light beasts.","Hold E to search a container or power the breaker. Find fuses, then reach the exit. Each special door previews its own event and cost before activation.","Select a supply, hold the throw button and release. E enters or leaves a hiding spot. Closing the shop keeps you hidden. G uses Hush Pulse when purchased.","Choose deeper floors or bank your profit at the powered exit. While hidden, T opens supplies; F5 refreshes stock with a 60-second cooldown. While hiding, Space starts QWER rhythm calibration; two Spaces cancel."};static const char* bodyZh[5]={"WASD移动，鼠标观察，Shift冲刺消耗体力。设置页可查看并修改实际键位。","F切换手电，R手摇发电。理智与电量分别管理；关灯可甩掉只追可见光源的追光鬼。","按住E搜查容器或接通配电箱，找到保险丝后抵达出口。特殊房门会预告各自的事件与代价，确认后才会触发主题效果。","选择补给，按住投掷键蓄力后松开。E进入或离开藏身点；关闭商店不会解除躲藏。购买静息脉冲后，G释放该技能。","出口通电后可深入或结算。藏身后T打开补给，F5刷新库存，冷却60秒。躲藏时空格开始QWER节律校准，连按两次空格取消。"};Paragraph(LangTr(bodyEn[selection],bodyZh[selection]),rx,&y,width);}
    char footer[192];snprintf(footer,sizeof(footer),LangTr("TAB / LEFT / RIGHT: CATEGORY   UP / DOWN: BROWSE   ESC: BACK   %s","TAB或左右切分类  上下浏览  ESC返回  %s"),search);
    DrawTextSmallWin(x,winH-UIS(55),footer,UI_ACCENT);
    if(category==2)DrawTextSmallWin(x,winH-UIS(85),LangTr("TYPE A SKILL NAME TO LOCATE; BACKSPACE CLEARS","输入中文或英文技能名定位，退格删除"),UI_INK_DIM);
}

void HelpBookClick(int x,int y) {
    if(y>=UIS(22) && y<UIS(68) && x>=UIS(20) && x<winW-UIS(20)) {
        int tab=(x-UIS(20))*5/(winW-UIS(40));
        category=tab<5?tab:4;selection=0;search[0]=0;uiDirty=1;return;
    }
    int rows=(winH-UIS(270))/UIS(34);if(rows<1)rows=1;
    int count=Count(),first=selection-rows/2;
    if(first<0)first=0;
    if(first>count-rows)first=count>rows?count-rows:0;
    if(x<winW*2/5 && y>=UIS(175) && y<UIS(175)+rows*UIS(34)) {
        int row=first+(y-UIS(175))/UIS(34);if(row<count)selection=row;uiDirty=1;
    }
}

#include "events.h"
#include "loop_api.h"
#include "state.h"
#include "world_api.h"
#include "evolution_art.h"
#include "threat_tier.h"
#include "player_api.h"
/* Shared guide entrance; monster and room records keep independent selections. */
#include "common.h"
#include "ui_archive.h"
#include "ui.h"
#include "ui_text.h"
#include "ui_pages.h"
#include "ui_menu.h"
#include "ui_theme.h"
#include "bestiary.h"
#include "room_archetype.h"
#include "room_theme.h"
#include "hatch.h"
#include "sprites.h"
#include "render.h"
#include "lang.h"
#include "present_api.h"
static int archiveTab=ARCHIVE_MONSTERS,roomSelection=0,eventSelection=0;
static int detailScroll=0,detailScrollMax=0;
static int portraitTier=-1;
int UiArchiveTab(void){return archiveTab;}
int UiArchiveEntryCount(void){return archiveTab==ARCHIVE_ROOMS?ROOM_ARCHETYPE_COUNT:archiveTab==ARCHIVE_EVENTS?EVENT_COUNT:BESTIARY_COUNT;}
int UiArchiveSelection(void){return archiveTab==ARCHIVE_ROOMS?roomSelection:archiveTab==ARCHIVE_EVENTS?eventSelection:bestiarySel;}
void UiArchiveSelect(int entry){
    if(entry<0 || entry>UiArchiveEntryCount())return;
    if(archiveTab==ARCHIVE_ROOMS){if(roomSelection!=entry){detailScroll=detailScrollMax=0;}roomSelection=entry;}
    else if(archiveTab==ARCHIVE_EVENTS){
        if(eventSelection!=entry)detailScroll=detailScrollMax=0;
        eventSelection=entry;
    }
    else bestiarySel=entry;
    uiDirty=1;
}
void UiArchiveSetTab(int tab){
    if(tab<ARCHIVE_MONSTERS || tab>=ARCHIVE_TAB_COUNT || tab==archiveTab)return;
    archiveTab=tab;detailScroll=detailScrollMax=0;menuScrollOffsets[STATE_BESTIARY]=0;uiDirty=1;
}
void UiArchiveOpen(void){
    portraitTier=-1;
    archiveTab=ARCHIVE_MONSTERS;bestiarySel=0;roomSelection=eventSelection=0;detailScroll=detailScrollMax=0;
    menuScrollOffsets[STATE_BESTIARY]=0;uiDirty=1;
}
MenuGeo UiArchiveGeometry(void){
    MenuGeo g;memset(&g,0,sizeof(g));
    int pad=UIS(16),th=TextHeightWin();
    if(pad>winH/18)pad=winH/18;
    if(pad<4)pad=4;
    g.title=Tr("FIELD GUIDE","图鉴");
    g.panel=(RECT){pad,pad,winW-pad,winH-pad};g.titleY=g.panel.top+pad;
    g.firstY=g.titleY+th*2+UIS(28);g.count=UiArchiveEntryCount()+1;
    int available=g.panel.bottom-g.firstY-th-UIS(16);
    if(archiveTab==ARCHIVE_EVENTS) {
        available=g.panel.bottom-g.firstY-pad;
        g.rowH=th+UIS(8);
        if(available/g.count>g.rowH)g.rowH=available/g.count;
        g.maxVisibleRows=available/g.rowH;
        if(g.maxVisibleRows>g.count)g.maxVisibleRows=g.count;
        if(g.maxVisibleRows<0)g.maxVisibleRows=0;
        g.scrollOffset=UiArchiveSelection()-g.maxVisibleRows+1;
        if(g.scrollOffset>g.count-g.maxVisibleRows)g.scrollOffset=g.count-g.maxVisibleRows;
        if(g.scrollOffset<0)g.scrollOffset=0;
        return g;
    }
    g.rowH=available/g.count;
    if(g.rowH<th+UIS(10))g.rowH=th+UIS(10);
    g.maxVisibleRows=available/g.rowH;
    if(g.maxVisibleRows<1)g.maxVisibleRows=1;
    if(g.maxVisibleRows>g.count)g.maxVisibleRows=g.count;
    int selected=UiArchiveSelection();
    g.scrollOffset=selected-g.maxVisibleRows+1;
    if(g.scrollOffset<0)g.scrollOffset=0;
    if(g.scrollOffset>g.count-g.maxVisibleRows)g.scrollOffset=g.count-g.maxVisibleRows;
    return g;
}
RECT UiArchiveTabRect(MenuGeo g,int tab){
    int pad=UIS(10),th=TextHeightWin(),width=(g.panel.right-g.panel.left-pad*(ARCHIVE_TAB_COUNT+1))/ARCHIVE_TAB_COUNT;
    int left=g.panel.left+pad+(width+pad)*tab;
    return (RECT){left,g.titleY+th+UIS(6),left+width,g.firstY-UIS(8)};
}
static int ArchiveSplit(MenuGeo g) {
    int width=g.panel.right-g.panel.left;
    return g.panel.left+width*32/100;
}
int UiArchiveRowAt(int x,int y) {
    MenuGeo g=UiArchiveGeometry();
    for(int i=g.scrollOffset;i<g.count && i<g.scrollOffset+g.maxVisibleRows;i++) {
        RECT row=RowRect(g,i);
        if(x>=row.left && x<row.right && y>=row.top && y<row.bottom)return i;
    }
    return -1;
}
int UiArchiveInDetail(int x) { return x>=ArchiveSplit(UiArchiveGeometry()); }

int UiArchiveTabAt(int x,int y){
    MenuGeo g=UiArchiveGeometry();
    for(int tab=0;tab<ARCHIVE_TAB_COUNT;tab++){
        RECT r=UiArchiveTabRect(g,tab);
        if(x>=r.left && x<r.right && y>=r.top && y<r.bottom)return tab;
    }
    return -1;
}
void UiArchiveScrollDetail(int lines){
    if(archiveTab==ARCHIVE_MONSTERS)return;
    detailScroll+=lines;
    if(detailScroll<0)detailScroll=0;
    if(detailScroll>detailScrollMax)detailScroll=detailScrollMax;
    uiDirty=1;
}
static RECT DetailButtonRect(MenuGeo g,int down){
    int w=UIS(64),h=TextHeightWin()+UIS(8),right=g.panel.right-UIS(12);
    return (RECT){right-(2-down)*w-UIS(8),g.panel.bottom-h-UIS(8),right-(1-down)*w-UIS(12),g.panel.bottom-UIS(8)};
}
int UiArchiveDetailButtonAt(int x,int y){
    if(archiveTab==ARCHIVE_MONSTERS || UiArchiveSelection()>=UiArchiveEntryCount() || (archiveTab==ARCHIVE_EVENTS && !detailScrollMax))return 0;
    MenuGeo g=UiArchiveGeometry();
    for(int down=0;down<2;down++){
        RECT r=DetailButtonRect(g,down);
        if(x>=r.left && x<=r.right && y>=r.top && y<=r.bottom)return down?1:-1;
    }
    return 0;
}

int UiDossierText(const char* text,int x,int y,int width,int bottom,unsigned int color){
    if(!text || width<=0)return y;
    int height=TextMediumLiteralHeight(),step=height+UIS(4);
    while(*text && y+height<=bottom){
        char line[256];int used=0,fit=0;
        if(*text=='\n'){text++;y+=step;continue;}
        while(text[used] && text[used]!='\n' && used<240){
            unsigned char c=(unsigned char)text[used];int n=c<128?1:c<224?2:c<240?3:4;
            if(used+n>=250)break;
            int valid=1;for(int i=1;i<n;i++)if(!text[used+i] || ((unsigned char)text[used+i]&0xC0)!=0x80)valid=0;
            if(!valid)n=1;
            memcpy(line+used,text+used,(size_t)n);used+=n;line[used]=0;
            if(TextMediumLiteralWidth(line)>width)break;
            fit=used;
        }
        if(!fit)break;
        line[fit]=0;DrawTextMediumLiteralWin(x,y,line,color);text+=fit;y+=step;
        if(*text=='\n')text++;
    }
    return y;
}

void UiDossierPortrait(int spriteId, RECT box, int identified) {
    if (!SpritesReady() || !pixels || renderW <= 0 || renderH <= 0 || winW <= 0 || winH <= 0) return;
    Sprite* sprite = SpriteGet(spriteId);
    if (!sprite || !sprite->px) return;
    int sx = sprite->bx0, sy = sprite->by0;
    int sw = sprite->bx1-sx, sh = sprite->by1-sy;
    if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0 || sprite->bx1 > sprite->w || sprite->by1 > sprite->h) {
        sx = sy = 0; sw = sprite->w; sh = sprite->h;
    }
    int left = box.left*renderW/winW, top = box.top*renderH/winH;
    int bw = (box.right-box.left)*renderW/winW, bh = (box.bottom-box.top)*renderH/winH;
    if (bw <= 0 || bh <= 0 || sw <= 0 || sh <= 0) return;
    float k = (float)bw/sw; if ((float)bh/sh < k) k = (float)bh/sh;
    int w = (int)(sw*k), h = (int)(sh*k);
    if (w <= 0 || h <= 0) return;
    left += (bw-w)/2; top += (bh-h)/2;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        unsigned int pixel = sprite->px[(sy+y*sh/h)*sprite->w + sx+x*sw/w];
        float alpha = (float)(pixel >> 24)/255.0f;
        if (alpha <= 0.0f) continue;
        PutPixelBlend(left+x,top+y,identified ? (pixel&0xFFFFFFu) : 0x001C2935u,alpha);
    }
}

static int DisplayTier(void) {
    if(portraitTier>=0)return portraitTier;
    return prevState==STATE_PAUSED?CurrentRunThreatTier():PreviewThreatTier();
}
static RECT TierRect(MenuGeo g,int i){int left=g.panel.left+(g.panel.right-g.panel.left)*32/100+UIS(16);int w=(g.panel.right-UIS(16)-left)/5;int y=g.firstY+TextMediumLiteralHeight()*2+UIS(16);return (RECT){left+i*w,y,left+(i+1)*w-UIS(3),y+TextMediumLiteralHeight()+UIS(8)};}
int UiArchiveTierKey(int key){if(currentState!=STATE_BESTIARY || archiveTab!=ARCHIVE_MONSTERS)return 0;if(key>='1' && key<='4')portraitTier=key-'1';else if(key=='0')portraitTier=-1;else return 0;uiDirty=1;return 1;}
int UiArchiveTierClick(int x,int y){if(currentState!=STATE_BESTIARY || archiveTab!=ARCHIVE_MONSTERS)return 0;MenuGeo g=UiArchiveGeometry();for(int i=0;i<5;i++){RECT r=TierRect(g,i);if(x>=r.left && x<r.right && y>=r.top && y<r.bottom){portraitTier=i<4?i:-1;uiDirty=1;return 1;}}return 0;}

static void DrawMonsterArchive(MenuGeo g) {
    int th = TextHeightWin(), pad = UIS(16);
    int split = g.panel.left + (g.panel.right-g.panel.left)*32/100;
    int x = split+pad, right = g.panel.right-pad;
    BlendRectWin(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,UI_BG_PANEL,.99f);
    FrameRectWin(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,UI_EDGE,.95f,1);
    DrawTextWin(g.panel.left+pad,g.titleY,Tr("FIELD GUIDE","图鉴"),UI_ACCENT);
    char text[160];
    snprintf(text,sizeof(text),Tr("ACTIVE %d / %d","现行 %d / %d"),BestiaryCountSeen(bestiarySeen),BestiaryActiveCount());
    DrawTextWin(right-TextWidthWin(text),g.titleY,text,UI_ACCENT);
    BlendRectWin(split,g.firstY-UIS(8),split+1,g.panel.bottom-pad,UI_EDGE,.9f);
    for (int id=g.scrollOffset; id<=BESTIARY_COUNT && id<g.scrollOffset+g.maxVisibleRows; id++) {
        RECT row = RowRect(g,id);
        int selected = bestiarySel == id;
        if (selected) BlendRectWin(row.left,row.top,row.right,row.bottom,UI_BG_SEL,.95f);
        if (id == BESTIARY_COUNT) snprintf(text,sizeof(text),"< %s",Tr("BACK","返回"));
        else if (!BestiaryIsActive(id)) snprintf(text,sizeof(text),"%02d  %s",id+1,Tr("ARCHIVED","旧版记录"));
        else snprintf(text,sizeof(text),"%02d  %s",id+1,BestiaryIsSeen(id) ? Tr(bestiary[id].nameEn,bestiary[id].nameZh) : "???");
        ClampRowText(text,row.right-row.left-UIS(12));
        DrawTextWin(row.left+UIS(6),row.top+(row.bottom-row.top-th)/2,text,selected?UI_ACCENT:UI_INK_DIM);
    }
    int id = bestiarySel;
    if (id < 0 || id >= BESTIARY_COUNT) {
        UiDossierText(Tr("Return to the main menu. Your archive is kept.","返回主菜单。已发现的档案会保留。"),x,g.firstY,right-x,g.panel.bottom-pad,UI_INK_DIM);
        return;
    }
    const BestiaryEntry* entry = &bestiary[id];
    int seen = BestiaryIsSeen(id), stage = BestiaryStage(id), active = BestiaryIsActive(id);
    int y = UiDossierText(seen ? ((id==BST_ID_HUNTER)?Tr("SOUND HUNTERS / THREE FORMS","听声鬼 / 三种形态"):Tr(entry->nameEn,entry->nameZh)) : Tr("UNIDENTIFIED","未知生物"),x,g.firstY,right-x,g.panel.bottom-pad,UI_INK_TITLE);
    snprintf(text,sizeof(text),"%s / %s",active ? Tr("ACTIVE","现行") : Tr("ARCHIVED","旧版记录"),seen?Tr(entry->tagEn,entry->tagZh):"???");
    y = UiDossierText(text,x,y,right-x,g.panel.bottom-pad,UI_INK_DIM);
    int threatTier=DisplayTier();
    static const char* tierNames[5]={"1 / I","2 / II","3 / III","4 / IV","AUTO"};
    for(int t=0;t<5;t++){RECT r=TierRect(g,t);UiPanel(r.left,r.top,r.right,r.bottom,(t==portraitTier || (t==4 && portraitTier<0))?UI_PANEL_HOT:UI_PANEL_NORMAL);DrawTextSmallLiteralWin(r.left+UIS(5),r.top+UIS(4),tierNames[t],t<4?ThreatTierColor(t):UI_INK);}
    RECT tierRow=TierRect(g,0);if(y<tierRow.bottom+UIS(7))y=tierRow.bottom+UIS(7);
    int bottom = g.panel.bottom-th-pad;
    int imageSize = (right-x)*46/100;
    if (imageSize > (bottom-y)*42/100) imageSize = (bottom-y)*42/100;
    if (imageSize < 12) return;
    RECT image = {x,y+UIS(6),x+imageSize,y+UIS(6)+imageSize};
    BlendRectWin(image.left,image.top,image.right,image.bottom,UI_BG_ROW,1.0f);
    FrameRectWin(image.left,image.top,image.right,image.bottom,UI_EDGE,.9f,1);
    RECT inset = {image.left+UIS(5),image.top+UIS(5),image.right-UIS(5),image.bottom-UIS(5)};
    if(id==BST_ID_HUNTER) {
        int width=(inset.right-inset.left)/3;
        for(int variant=0;variant<3;variant++) {
            RECT portrait={inset.left+variant*width,inset.top,inset.left+(variant+1)*width,inset.bottom};
            DrawEvolutionPortrait(BST_ID_HUNTER,variant,threatTier,portrait,seen);
        }
    } else DrawEvolutionPortrait(id,0,threatTier,inset,seen);
    int sx = image.right+UIS(10), sy = image.top, sw = right-sx;
    snprintf(text,sizeof(text),Tr("RESEARCH %d / 3","认知 %d / 3"),stage);
    sy = UiDossierText(text,sx,sy,sw,image.bottom,UI_ACCENT);
    sy=UiDossierText(ThreatTierLabel(threatTier),sx,sy,sw,image.bottom,ThreatTierColor(threatTier));
    if (seen) {
        snprintf(text,sizeof(text),Tr("RISK %d / 5","危险 %d / 5"),BestiaryDanger(id));
        sy = UiDossierText(text,sx,sy,sw,image.bottom,0x00DB997Du);
        snprintf(text,sizeof(text),Tr("ESCAPES %d","逃脱 %d"),bestiaryEscapes[id]);
        sy = UiDossierText(text,sx,sy,sw,image.bottom,0x0095B5BCu);
        snprintf(text,sizeof(text),Tr("CAUGHT %d","被抓 %d"),bestiaryCaught[id]);
        sy = UiDossierText(text,sx,sy,sw,image.bottom,0x0095B5BCu);
    }
    y = image.bottom+UIS(10);
    if (!active) {
        UiDossierText(Tr("Legacy record. This creature no longer spawns and is excluded from completion.","历史记录：当前版本不生成，不计入现行全图鉴。"),x,y,right-x,bottom,UI_INK_DIM);
    } else if (!seen) {
        UiDossierText(Tr("Encounter this creature in a run to identify its portrait and behavior.","在局内遭遇后，解锁肖像与身份。研究逐层积累，逃脱能加速揭示。"),x,y,right-x,bottom,UI_INK_DIM);
    } else {
        int half = y + (bottom-y)/2;
        y = UiDossierText(Tr("BEHAVIOR","行为"),x,y,right-x,half,UI_ACCENT);
        UiDossierText(EvolutionGuideText(id,threatTier),x,y,right-x,half,0x00B6C7D1u);
        y = UiDossierText(Tr("COUNTERMEASURE","对策"),x,half+UIS(4),right-x,bottom,UI_ACCENT);
        UiDossierText(stage>=BST_COUNTER?Tr(entry->counterEn,entry->counterZh):Tr("Study across 5 floors or escape three times.","累计遭遇五层，或逃脱三次后揭示。"),x,y,right-x,bottom,0x00B6C7D1u);
    }
    char hint[128];snprintf(hint,sizeof(hint),"%s",Tr("1-4: FORM / 0: LIVE / LEFT-RIGHT: CATEGORY","一至四键预览形态，零键跟随当前档；左右分类"));
    UiTrimSmallText(hint,right-x);DrawTextSmallWin(x,bottom,hint,0x006E8497u);
}

/* The scrolling text pass never touches gameplay state or the live room event timer. */
static int ArchiveDetailText(const char* text,int x,int y,int width,int top,int bottom,unsigned color,int draw){
    int height=TextMediumLiteralHeight(),step=height+UIS(4);
    if(!text || width<=0)return y;
    while(*text){
        char line[256];int used=0,fit=0;
        if(*text=='\n'){text++;y+=step;continue;}
        while(text[used] && text[used]!='\n' && used<240){
            unsigned char c=(unsigned char)text[used];int bytes=c<128?1:c<224?2:c<240?3:4;
            if(used+bytes>=250)break;
            int valid=1;for(int k=1;k<bytes;k++)if(!text[used+k] || ((unsigned char)text[used+k]&0xC0)!=0x80)valid=0;
            if(!valid)bytes=1;
            memcpy(line+used,text+used,(size_t)bytes);used+=bytes;line[used]=0;
            if(TextMediumLiteralWidth(line)>width)break;
            fit=used;
        }
        if(!fit)break;
        line[fit]=0;
        if(draw && y>=top && y+height<=bottom)DrawTextMediumLiteralWin(x,y,line,color);
        text+=fit;y+=step;if(*text=='\n')text++;
    }
    return y;
}
static int RoomGuideBody(int theme,int x,int y,int width,int top,int bottom,int draw){
    static const char* riskEn[7]={
        "Sound moves, not your body. Hunters can still see or touch you. Leave the well before they arrive.",
        "The exclusive chest pool has 40% loss, 20% break-even and 40% profit odds. Open all vault chests to recover the full rolled total. Leaving early forfeits unclaimed gold.",
        "Prepare your exit route first. The map reveal also sends a loud signal to nearby hunters.",
        "The left and right choices are exclusive. A full destination resource cannot accept another transfer.",
        "A selected target can become unsafe. Recheck the direction before starting the central device; no rotation is forced.",
        "The covenant lasts this run and cannot be undone. Cancel at the second console before starting the central device.",
        "The heart is silent while hunters are stopped. Its final scream attracts attention; plan your escape."
    };
    static const char* riskZh[7]={
        "转移的是声音，不是身体；怪物仍能看见或碰到你。请在怪物到达前离开井口。",
        "独立奖池四成亏损、两成回本、四成盈利。开完全部专属箱才能取齐回款；提前离层会放弃未取金币。保险丝箱不参与奖池。",
        "请先规划离开路线。地图情报伴随着强噪声，会吸引附近猎手。",
        "左右两种转换只能选择一种；目标资源已满时无法继续转换。",
        "预选目标可能失效。启动中央装置前确认目标方向，换位不会自动旋转镜头。",
        "契约持续本局且无法撤销；启动中央装置前可在另一侧控制台取消确认。",
        "静止期间心脏安静，结束时尖啸会吸引注意；请提前规划逃生路线。"
    };
    y=ArchiveDetailText(Tr("EFFECT / COST","效果与代价"),x,y,width,top,bottom,UI_ACCENT,draw);
    y=ArchiveDetailText(RoomThemeGuideEffect(theme),x,y,width,top,bottom,UI_INK,draw)+UIS(6);
    y=ArchiveDetailText(Tr("PREPARE / ACTIVATE","准备与启动"),x,y,width,top,bottom,UI_ACCENT,draw);
    y=ArchiveDetailText(RoomThemeGuideControl(theme,0),x,y,width,top,bottom,UI_INK,draw);
    y=ArchiveDetailText(RoomThemeGuideControl(theme,1),x,y,width,top,bottom,UI_INK,draw);
    y=ArchiveDetailText(Tr("Then hold INTERACT at the special hatch. Controls never charge or grant supplies.",
                       "完成准备后，在特殊舱门长按交互。控制台本身不扣费，也不发放补给。"),x,y,width,top,bottom,UI_INK_DIM,draw)+UIS(6);
    y=ArchiveDetailText(Tr("RISK / COUNTERPLAY","风险与应对"),x,y,width,top,bottom,0x00DB997Du,draw);
    return ArchiveDetailText(g_lang==LANG_ZH?riskZh[theme]:riskEn[theme],x,y,width,top,bottom,UI_INK,draw);
}
static void DrawRoomBlueprint(int theme,int variant,RECT box){
    RoomTemplate layout;
    if(!RoomTemplateBuildCurrent(theme,variant,0,&layout))return;
    int cell=(box.right-box.left)/layout.w;
    if((box.bottom-box.top)/layout.h<cell)cell=(box.bottom-box.top)/layout.h;
    if(cell<1)return;
    int left=box.left+(box.right-box.left-layout.w*cell)/2;
    int top=box.top+(box.bottom-box.top-layout.h*cell)/2;
    for(int y=0;y<layout.h;y++)for(int x=0;x<layout.w;x++){
        unsigned color=layout.tiles[y][x]==ROOM_TILE_PILLAR?0x0089A9BBu:0x001E3443u;
        BlendRectWin(left+x*cell,top+y*cell,left+(x+1)*cell-1,top+(y+1)*cell-1,color,1);
    }
}
static void DrawDetailButtons(MenuGeo g) {
    for(int down=0;down<2;down++){
        RECT button=DetailButtonRect(g,down);
        int enabled=down?detailScroll<detailScrollMax:detailScroll>0;
        BlendRectWin(button.left,button.top,button.right,button.bottom,enabled?UI_BG_SEL:UI_BG_PANEL,.98f);
        FrameRectWin(button.left,button.top,button.right,button.bottom,UI_EDGE_HOT,.9f,1);
        const char* label=down?Tr("DOWN","下翻"):Tr("UP","上翻");
        DrawTextCenterWin((button.left+button.right)/2,button.top+UIS(3),label,enabled?UI_ACCENT:UI_INK_DIM);
    }
}
static void DrawRoomArchive(MenuGeo g){
    int th=TextHeightWin(),pad=UIS(16),split=g.panel.left+(g.panel.right-g.panel.left)*32/100;
    int x=split+pad,right=g.panel.right-pad,bottom=g.panel.bottom-th-UIS(24);
    BlendRectWin(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,UI_BG_PANEL,.99f);
    FrameRectWin(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,UI_EDGE_HOT,1,1);
    DrawTextWin(g.panel.left+pad,g.titleY,Tr("FIELD GUIDE","图鉴"),UI_ACCENT);
    const char* count=Tr("7 ROOM ARCHETYPES","七种主题房间");
    DrawTextWin(right-TextWidthWin(count),g.titleY,count,UI_INFO);
    BlendRectWin(split,g.firstY-UIS(8),split+1,g.panel.bottom-pad,UI_EDGE_HOT,.9f);
    for(int id=g.scrollOffset;id<g.count && id<g.scrollOffset+g.maxVisibleRows;id++){
        RECT row=RowRect(g,id);char text[96];
        if(id==roomSelection)BlendRectWin(row.left,row.top,row.right,row.bottom,UI_BG_SEL,.96f);
        if(id==ROOM_ARCHETYPE_COUNT)snprintf(text,sizeof(text),"< %s",Tr("BACK","返回"));
        else snprintf(text,sizeof(text),"%02d %s",id+1,RoomThemeName(id,g_lang==LANG_ZH));
        ClampRowText(text,row.right-row.left-UIS(10));
        DrawTextWin(row.left+UIS(5),row.top+(row.bottom-row.top-th)/2,text,id==roomSelection?UI_ACCENT:UI_INK);
    }
    if(roomSelection>=ROOM_ARCHETYPE_COUNT){
        UiDossierText(Tr("Return to the main menu. Monster and room records are separate.","返回主菜单。怪物与主题房间各自独立分类。"),x,g.firstY,right-x,bottom,UI_INK);
        return;
    }
    int theme=roomSelection;
    int y=UiDossierText(RoomThemeName(theme,g_lang==LANG_ZH),x,g.firstY,right-x,bottom,UI_INK_TITLE);
    int imageHeight=(bottom-y)*32/100;
    if(imageHeight>(right-x)/2)imageHeight=(right-x)/2;
    int minimumBody=(TextMediumLiteralHeight()+UIS(4))*3;
    if(imageHeight>bottom-y-minimumBody-UIS(12))imageHeight=bottom-y-minimumBody-UIS(12);
    if(imageHeight>=24){
    RECT image={x,y+UIS(4),x+(right-x)*48/100,y+UIS(4)+imageHeight};
    BlendRectWin(image.left,image.top,image.right,image.bottom,0x00121F2Cu,1);
    RECT inset={image.left+UIS(4),image.top+UIS(4),image.right-UIS(4),image.bottom-UIS(4)};
    UiDossierPortrait(SPR_ROOM_V2_BASE+theme*3,inset,1);
    int bx=image.right+UIS(8),half=(image.bottom-image.top)/2;
    RECT a={bx,image.top,right,image.top+half-UIS(2)};
    RECT b={bx,image.top+half+UIS(2),right,image.bottom};
    DrawRoomBlueprint(theme,0,a);DrawRoomBlueprint(theme,1,b);
    y=image.bottom+UIS(8);
    }
    int step=TextMediumLiteralHeight()+UIS(4);
    int end=RoomGuideBody(theme,x,y,right-x,y,bottom,0);
    detailScrollMax=end>bottom?(end-bottom+step-1)/step:0;
    if(detailScroll>detailScrollMax)detailScroll=detailScrollMax;
    RoomGuideBody(theme,x,y-detailScroll*step,right-x,y,bottom,1);
    DrawDetailButtons(g);
    RECT firstButton=DetailButtonRect(g,0);
    char hint[96];snprintf(hint,sizeof(hint),"%s",Tr("PGUP / PGDN","上下翻页"));
    UiTrimSmallText(hint,firstButton.left-x-UIS(8));
    if(firstButton.left-x>UIS(24))DrawTextSmallWin(x,g.panel.bottom-th-UIS(6),hint,UI_INK_DIM);
}
static int EventArchiveBody(const EventDef *event,int x,int y,int width,int top,int bottom,int draw) {
    char facts[128];
    snprintf(facts,sizeof facts,Tr("From floor %d   Clear score +%d","第 %d 层起出现   通过后 +%d 分"),
             event->unlock,event->score);
    y=ArchiveDetailText(facts,x,y,width,top,bottom,UI_INFO,draw)+UIS(9);
    y=ArchiveDetailText(Tr("WHAT CHANGES","事件效果"),x,y,width,top,bottom,event->color,draw);
    y=ArchiveDetailText(Tr(event->descEn,event->descZh),x,y,width,top,bottom,UI_INK,draw)+UIS(8);
    y=ArchiveDetailText(Tr("HOW TO RESPOND","应对方法"),x,y,width,top,bottom,event->color,draw);
    y=ArchiveDetailText(Tr(event->tipEn,event->tipZh),x,y,width,top,bottom,UI_INK,draw)+UIS(8);
    if(event->goldPct){
        snprintf(facts,sizeof facts,Tr("Eligible gold +%d%%, up to the floor limit.","指定来源的金币增加 %d%%，受本层上限限制。"),
                 event->goldPct);
        y=ArchiveDetailText(facts,x,y,width,top,bottom,UI_GOLD,draw);
    }
    return y;
}

static void DrawEventArchive(MenuGeo g) {
    int pad=UIS(10),split=ArchiveSplit(g),right=g.panel.right-pad;
    int x=split+UIS(16),bottom=g.panel.bottom-pad,y=g.firstY;
    int th=TextHeightWin();
    BlendRectWin(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,UI_BG_PANEL,.99f);
    FrameRectWin(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,UI_EDGE_HOT,1,1);
    DrawTextWin(g.panel.left+pad,g.titleY,Tr("FIELD GUIDE","图鉴"),UI_ACCENT);
    BlendRectWin(split,y-UIS(8),split+1,bottom,UI_EDGE_HOT,.9f);

    for(int i=g.scrollOffset;i<g.count && i<g.scrollOffset+g.maxVisibleRows;i++){
        RECT row=RowRect(g,i);
        const EventDef *event=EventDefinition(i);
        int selected=i==eventSelection;
        if(selected)BlendRectWin(row.left,row.top,row.right,row.bottom,UI_BG_SEL,.96f);
        int width=row.right-row.left-UIS(12);
        char label[96];
        if(event){
            snprintf(label,sizeof label,"%02d  %s",i+1,Tr(event->nameEn,event->nameZh));
            if(TextWidthWin(label)>width)
                snprintf(label,sizeof label,"%02d  %s",i+1,Tr(event->shortEn,event->shortZh));
        }else snprintf(label,sizeof label,"< %s",Tr("BACK","返回"));
        ClampRowText(label,width);
        DrawTextWin(row.left+UIS(6),row.top+(row.bottom-row.top-th)/2,
                    label,selected?UI_ACCENT:UI_INK_DIM);
    }

    const EventDef *event=EventDefinition(eventSelection);
    if(!event)return;
    int width=right-x;
    int titleY=y;
    int titleEnd=UiDossierText(Tr(event->nameEn,event->nameZh),x,titleY,width,bottom,event->color);
    int top=titleEnd+pad;
    BlendRectWin(x,top-UIS(4),right,top-UIS(3),event->color,.55f);
    int step=TextMediumLiteralHeight()+UIS(4);
    int end=EventArchiveBody(event,x,top,width,top,bottom-pad,0);
    int limit=bottom-pad;
    if(end>limit)limit=DetailButtonRect(g,0).top-UIS(4);
    detailScrollMax=end>limit?(end-limit+step-1)/step:0;
    if(detailScroll>detailScrollMax)detailScroll=detailScrollMax;
    EventArchiveBody(event,x,top-detailScroll*step,width,top,limit,1);
    if(detailScrollMax)DrawDetailButtons(g);
}
void UiDrawArchivePage(MenuGeo g){
    g=UiArchiveGeometry(); /* Hit testing and drawing use the same geometry. */
    if(archiveTab==ARCHIVE_ROOMS)DrawRoomArchive(g);
    else if(archiveTab==ARCHIVE_EVENTS)DrawEventArchive(g);
    else DrawMonsterArchive(g);
    for(int tab=0;tab<ARCHIVE_TAB_COUNT;tab++){
        RECT r=UiArchiveTabRect(g,tab);
        BlendRectWin(r.left,r.top,r.right,r.bottom,tab==archiveTab?UI_BG_SEL:UI_BG_PANEL,1);
        FrameRectWin(r.left,r.top,r.right,r.bottom,UI_EDGE_HOT,.95f,1);
        char label[96];snprintf(label,sizeof(label),"%s",tab==ARCHIVE_MONSTERS?Tr("MONSTERS","怪物图鉴"):tab==ARCHIVE_ROOMS?Tr("THEME ROOMS","主题房间"):Tr("SPECIAL EVENTS","特殊事件"));
        ClampRowText(label,r.right-r.left-UIS(12));
        DrawTextCenterWin((r.left+r.right)/2,r.top+(r.bottom-r.top-TextHeightWin())/2,label,tab==archiveTab?UI_ACCENT:UI_INK_DIM);
    }
}

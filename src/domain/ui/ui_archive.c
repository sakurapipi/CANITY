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
static int archiveTab=ARCHIVE_MONSTERS,roomSelection=0;
static int roomScroll=0,roomScrollMax=0;
int UiArchiveTab(void){return archiveTab;}
int UiArchiveEntryCount(void){return archiveTab==ARCHIVE_ROOMS?ROOM_ARCHETYPE_COUNT:BESTIARY_COUNT;}
int UiArchiveSelection(void){return archiveTab==ARCHIVE_ROOMS?roomSelection:bestiarySel;}
void UiArchiveSelect(int entry){
    if(entry<0 || entry>UiArchiveEntryCount())return;
    if(archiveTab==ARCHIVE_ROOMS){if(roomSelection!=entry){roomScroll=roomScrollMax=0;}roomSelection=entry;}
    else bestiarySel=entry;
    uiDirty=1;
}
void UiArchiveSetTab(int tab){
    if(tab<ARCHIVE_MONSTERS || tab>ARCHIVE_ROOMS || tab==archiveTab)return;
    archiveTab=tab;roomScroll=roomScrollMax=0;menuScrollOffsets[STATE_BESTIARY]=0;uiDirty=1;
}
void UiArchiveOpen(void){
    archiveTab=ARCHIVE_MONSTERS;bestiarySel=0;roomSelection=0;roomScroll=roomScrollMax=0;
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
    int pad=UIS(10),th=TextHeightWin(),width=(g.panel.right-g.panel.left-pad*3)/2;
    int left=g.panel.left+pad+(width+pad)*tab;
    return (RECT){left,g.titleY+th+UIS(6),left+width,g.firstY-UIS(8)};
}
int UiArchiveTabAt(int x,int y){
    MenuGeo g=UiArchiveGeometry();
    for(int tab=0;tab<2;tab++){
        RECT r=UiArchiveTabRect(g,tab);
        if(x>=r.left && x<=r.right && y>=r.top && y<=r.bottom)return tab;
    }
    return -1;
}
void UiArchiveScrollDetail(int lines){
    if(archiveTab!=ARCHIVE_ROOMS)return;
    roomScroll+=lines;
    if(roomScroll<0)roomScroll=0;
    if(roomScroll>roomScrollMax)roomScroll=roomScrollMax;
    uiDirty=1;
}
static RECT DetailButtonRect(MenuGeo g,int down){
    int w=UIS(64),h=TextHeightWin()+UIS(8),right=g.panel.right-UIS(12);
    return (RECT){right-(2-down)*w-UIS(8),g.panel.bottom-h-UIS(8),right-(1-down)*w-UIS(12),g.panel.bottom-UIS(8)};
}
int UiArchiveDetailButtonAt(int x,int y){
    if(archiveTab!=ARCHIVE_ROOMS || roomSelection>=ROOM_ARCHETYPE_COUNT)return 0;
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

static void DrawMonsterArchive(MenuGeo g) {
    int th = TextHeightWin(), pad = UIS(16);
    int split = g.panel.left + (g.panel.right-g.panel.left)*32/100;
    int x = split+pad, right = g.panel.right-pad;
    BlendRectWin(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,0x00090F17u,.99f);
    FrameRectWin(g.panel.left,g.panel.top,g.panel.right,g.panel.bottom,0x00485668u,.95f,1);
    DrawTextWin(g.panel.left+pad,g.titleY,Tr("FIELD GUIDE","图鉴"),UI_ACCENT);
    char text[160];
    snprintf(text,sizeof(text),Tr("ACTIVE %d / %d","现行 %d / %d"),BestiaryCountSeen(bestiarySeen),BestiaryActiveCount());
    DrawTextWin(right-TextWidthWin(text),g.titleY,text,0x008CCBD3u);
    BlendRectWin(split,g.firstY-UIS(8),split+1,g.panel.bottom-pad,0x00404C60u,.9f);
    for (int id=g.scrollOffset; id<=BESTIARY_COUNT && id<g.scrollOffset+g.maxVisibleRows; id++) {
        RECT row = RowRect(g,id);
        int selected = bestiarySel == id;
        if (selected) BlendRectWin(row.left,row.top,row.right,row.bottom,0x002B394Bu,.95f);
        if (id == BESTIARY_COUNT) snprintf(text,sizeof(text),"< %s",Tr("BACK","返回"));
        else if (!BestiaryIsActive(id)) snprintf(text,sizeof(text),"%02d  %s",id+1,Tr("ARCHIVED","旧版记录"));
        else snprintf(text,sizeof(text),"%02d  %s",id+1,BestiaryIsSeen(id) ? Tr(bestiary[id].nameEn,bestiary[id].nameZh) : "???");
        ClampRowText(text,row.right-row.left-UIS(12));
        DrawTextWin(row.left+UIS(6),row.top+(row.bottom-row.top-th)/2,text,selected?0x00FFE1A0u:0x0093A4B7u);
    }
    int id = bestiarySel;
    if (id < 0 || id >= BESTIARY_COUNT) {
        UiDossierText(Tr("Return to the main menu. Your archive is kept.","返回主菜单。已发现的档案会保留。"),x,g.firstY,right-x,g.panel.bottom-pad,0x0093A4B7u);
        return;
    }
    const BestiaryEntry* entry = &bestiary[id];
    int seen = BestiaryIsSeen(id), stage = BestiaryStage(id), active = BestiaryIsActive(id);
    int y = UiDossierText(seen ? ((id==BST_ID_HUNTER)?Tr("SOUND HUNTERS / THREE FORMS","听声鬼 / 三种形态"):Tr(entry->nameEn,entry->nameZh)) : Tr("UNIDENTIFIED","未知生物"),x,g.firstY,right-x,g.panel.bottom-pad,0x00F0E1C8u);
    snprintf(text,sizeof(text),"%s / %s",active ? Tr("ACTIVE","现行") : Tr("ARCHIVED","旧版记录"),seen?Tr(entry->tagEn,entry->tagZh):"???");
    y = UiDossierText(text,x,y,right-x,g.panel.bottom-pad,0x007DA7BEu);
    int bottom = g.panel.bottom-th-pad;
    int imageSize = (right-x)*46/100;
    if (imageSize > (bottom-y)*42/100) imageSize = (bottom-y)*42/100;
    if (imageSize < 12) return;
    RECT image = {x,y+UIS(6),x+imageSize,y+UIS(6)+imageSize};
    BlendRectWin(image.left,image.top,image.right,image.bottom,0x00131C29u,1.0f);
    FrameRectWin(image.left,image.top,image.right,image.bottom,0x00374B60u,.9f,1);
    RECT inset = {image.left+UIS(5),image.top+UIS(5),image.right-UIS(5),image.bottom-UIS(5)};
    if(id==BST_ID_HUNTER) {
        static const int variants[3]={SPR_MON_HUNTER,SPR_MON_HUNTER2,SPR_MON_HUNTER3};
        int width=(inset.right-inset.left)/3;
        for(int variant=0;variant<3;variant++) {
            RECT portrait={inset.left+variant*width,inset.top,inset.left+(variant+1)*width,inset.bottom};
            UiDossierPortrait(variants[variant],portrait,seen);
        }
    } else UiDossierPortrait(entry->spriteId,inset,seen);
    int sx = image.right+UIS(10), sy = image.top, sw = right-sx;
    snprintf(text,sizeof(text),Tr("KNOWLEDGE %d / 3","认知 %d / 3"),stage);
    sy = UiDossierText(text,sx,sy,sw,image.bottom,UI_ACCENT);
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
        UiDossierText(Tr("Legacy record. This creature no longer spawns and is excluded from completion.","历史记录：当前版本不生成，不计入现行全图鉴。"),x,y,right-x,bottom,0x0093A4B7u);
    } else if (!seen) {
        UiDossierText(Tr("Encounter this creature in a run to identify its portrait and behavior.","在局内遭遇后，解锁肖像与身份。研究逐层积累，逃脱能加速揭示。"),x,y,right-x,bottom,0x0093A4B7u);
    } else {
        int half = y + (bottom-y)/2;
        y = UiDossierText(Tr("BEHAVIOR","行为"),x,y,right-x,half,UI_ACCENT);
        UiDossierText(stage>=BST_BEHAVED?Tr(entry->behaviorEn,entry->behaviorZh):Tr("Study across 3 floors or escape twice.","累计遭遇三层，或逃脱两次后揭示。"),x,y,right-x,half,0x00B6C7D1u);
        y = UiDossierText(Tr("COUNTERMEASURE","对策"),x,half+UIS(4),right-x,bottom,0x008CCBD3u);
        UiDossierText(stage>=BST_COUNTER?Tr(entry->counterEn,entry->counterZh):Tr("Study across 5 floors or escape three times.","累计遭遇五层，或逃脱三次后揭示。"),x,y,right-x,bottom,0x00B6C7D1u);
    }
    char hint[128];snprintf(hint,sizeof(hint),"%s",Tr("LEFT/RIGHT: CATEGORY / ARROWS: ENTRY / ESC: BACK","左右切分类，上下选条目，ESC返回"));
    UiTrimSmallText(hint,right-x);DrawTextSmallWin(x,bottom,hint,0x006E8497u);
}

/* The scrolling text pass never touches gameplay state or the live room event timer. */
static int RoomGuideText(const char* text,int x,int y,int width,int top,int bottom,unsigned color,int draw){
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
    y=RoomGuideText(Tr("EFFECT / COST","效果与代价"),x,y,width,top,bottom,UI_ACCENT,draw);
    y=RoomGuideText(RoomThemeGuideEffect(theme),x,y,width,top,bottom,UI_INK,draw)+UIS(6);
    y=RoomGuideText(Tr("PREPARE / ACTIVATE","准备与启动"),x,y,width,top,bottom,UI_ACCENT,draw);
    y=RoomGuideText(RoomThemeGuideControl(theme,0),x,y,width,top,bottom,UI_INK,draw);
    y=RoomGuideText(RoomThemeGuideControl(theme,1),x,y,width,top,bottom,UI_INK,draw);
    y=RoomGuideText(Tr("Then hold INTERACT at the special hatch. Controls never charge or grant supplies.",
                       "完成准备后，在特殊舱门长按交互。控制台本身不扣费，也不发放补给。"),x,y,width,top,bottom,UI_INK_DIM,draw)+UIS(6);
    y=RoomGuideText(Tr("RISK / COUNTERPLAY","风险与应对"),x,y,width,top,bottom,0x00DB997Du,draw);
    return RoomGuideText(g_lang==LANG_ZH?riskZh[theme]:riskEn[theme],x,y,width,top,bottom,UI_INK,draw);
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
    roomScrollMax=end>bottom?(end-bottom+step-1)/step:0;
    if(roomScroll>roomScrollMax)roomScroll=roomScrollMax;
    RoomGuideBody(theme,x,y-roomScroll*step,right-x,y,bottom,1);
    for(int down=0;down<2;down++){
        RECT button=DetailButtonRect(g,down);
        int enabled=down?roomScroll<roomScrollMax:roomScroll>0;
        BlendRectWin(button.left,button.top,button.right,button.bottom,enabled?UI_BG_SEL:UI_BG_PANEL,.98f);
        FrameRectWin(button.left,button.top,button.right,button.bottom,UI_EDGE_HOT,.9f,1);
        const char* label=down?Tr("DOWN","下翻"):Tr("UP","上翻");
        DrawTextCenterWin((button.left+button.right)/2,button.top+UIS(3),label,enabled?UI_ACCENT:UI_INK_DIM);
    }
    RECT firstButton=DetailButtonRect(g,0);
    char hint[96];snprintf(hint,sizeof(hint),"%s",Tr("PGUP / PGDN","上下翻页"));
    UiTrimSmallText(hint,firstButton.left-x-UIS(8));
    if(firstButton.left-x>UIS(24))DrawTextSmallWin(x,g.panel.bottom-th-UIS(6),hint,UI_INK_DIM);
}
void UiDrawArchivePage(MenuGeo g){
    if(archiveTab==ARCHIVE_ROOMS)DrawRoomArchive(g);else DrawMonsterArchive(g);
    for(int tab=0;tab<2;tab++){
        RECT r=UiArchiveTabRect(g,tab);
        BlendRectWin(r.left,r.top,r.right,r.bottom,tab==archiveTab?UI_BG_SEL:UI_BG_PANEL,1);
        FrameRectWin(r.left,r.top,r.right,r.bottom,UI_EDGE_HOT,.95f,1);
        char label[96];snprintf(label,sizeof(label),"%s",tab==ARCHIVE_MONSTERS?Tr("MONSTERS","怪物图鉴"):Tr("THEME ROOMS","主题房间图鉴"));
        ClampRowText(label,r.right-r.left-UIS(12));
        DrawTextCenterWin((r.left+r.right)/2,r.top+(r.bottom-r.top-TextHeightWin())/2,label,tab==archiveTab?UI_ACCENT:UI_INK_DIM);
    }
}

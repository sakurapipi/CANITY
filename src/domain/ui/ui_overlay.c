/* Foreground notifications; composition order remains in ui.c. */
#include "ui_overlay.h"
#include "training_stations.h"
#include "common.h"
#include "monster_rules.h"
#include "hush.h"
#include "room_theme.h"
#include "perception.h"
#include "item_effects.h"
#include "run_rewards.h"
#include "state.h"
#include "threat_telegraph.h"
#include "wrench.h"
#include "campaign.h"
#include "daily.h"
#include "economy.h"
#include "threat.h"
#include "upgrade_run.h"
#include "wallet.h"
#include "warden.h"
#include "build_tags.h"
#include "cell_meta.h"
#include "plat.h"
#include "spider.h"
#include "control_effect.h"
#include "hatch.h"
#include "map_rooms.h"
#include "art_direction.h"
#include "achievements.h"
#include "events.h"
#include "ui.h"
#include "ui_text.h"
#include "ui_pages.h"
#include "ui_menu.h"
#include "lang.h"
#include "weapons.h"
#include "shop.h"
#include "bench.h"
#include "lockbox.h"   /* v3.0 锁盘 */   /* v2.7 改造树 */
#include "ui_theme.h"
#include "sprites.h"
#include "relics.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "skills.h"
#include "bestiary.h"
#include "score.h"
#include "entity.h"
#include "render.h"
#include "save.h"
#include "ui_theme.h"

typedef struct { char message[384]; double until; } UiToastState;
static UiToastState toast;

static void DrawLessonMarker(float wx, float wy) {
    float dx = wx - posX, dy = wy - posY;
    float distance = sqrtf(dx*dx + dy*dy);
    if (distance < .25f) return;
    float depth = dx*dirX + dy*dirY;
    float side = dy*dirX - dx*dirY;
    float cone = fmaxf(.20f, sqrtf(planeX*planeX + planeY*planeY));
    int cx = winW / 2;
    float ratio = depth > .05f ? side / (depth * cone) : (side < 0 ? -2 : 2);
    int x = cx + (int)(fmaxf(-.85f, fminf(.85f, ratio)) * cx);
    int y = winH * 42 / 100, r = UIS(8);
    for (int row=-r;row<=r;row++) {
        int half = r-abs(row), py=y+row;
        BlendRectWin(x-half-1,py,x+half+2,py+1,UI_BG_PANEL,.95f);
        BlendRectWin(x-half,py,x+half+1,py+1,UI_ACCENT,.85f);
    }
    char label[80];
    snprintf(label,sizeof(label),Tr("%s %.1f TILES","%s %.1f 格"),
             ratio<-.85f?"<":ratio>.85f?">":"",distance);
    int width=TextSmallLiteralWidth(label);
    BlendRectWin(x-width/2-UIS(5),y+r+UIS(3),x+width/2+UIS(5),y+r+UIS(25),UI_BG_PANEL,.88f);
    DrawTextSmallLiteralWin(x-width/2,y+r+UIS(6),label,UI_INK);
}

void DrawTutorialPanel(void) {
    if(!trainingMode || currentState!=STATE_PLAYING || calibActive || lockActive || (isMapOpen && mapFullScreen))return;
    if (!guideMode) {
        int station = TrainingStationAt(posX, posY);
        if (station < 0) return;
        char sign[240];
        snprintf(sign,sizeof(sign),"%s",TrainingStationText(station));
        UiTrimSmallText(sign,winW-UIS(40));
        int width=TextSmallLiteralWidth(sign), x=(winW-width)/2;
        BlendRectWin(x-UIS(8),UIS(9),x+width+UIS(8),UIS(43),UI_BG_PANEL,.95f);
        DrawTextSmallLiteralWin(x,UIS(17),sign,UI_INK);
        return;
    }
    int w=winW*43/100;if(w>UIS(400))w=UIS(400);
    int x=winW-w-UIS(12),top=UIS(16),pad=UIS(9),line=UIS(27),y=top+pad;
    BlendRectWin(x,top,x+w,top+UIS(170),UI_BG_PANEL,.96f);FrameRectWin(x,top,x+w,top+UIS(170),UI_EDGE_HOT,1,1);
    char text[256];snprintf(text,sizeof(text),Tr("COURSE %d/%d","课程 %d/%d"),TutorialStepNumber(),TutorialStepTotal());
    DrawTextSmallLiteralWin(x+pad,y,text,UI_ACCENT);y+=UIS(20);
    snprintf(text,sizeof(text),"%s",TutorialObjective());
    while(text[0] && TextMediumLiteralWidth(text)>w-pad*2){size_t n=strlen(text)-1;while(n && ((unsigned char)text[n]&0xC0)==0x80)n--;text[n]=0;}
    DrawTextMediumLiteralWin(x+pad,y,text,UI_INK_TITLE);y+=line;
    const char* hint=TutorialHint();
    for(int row=0;*hint && row<3;row++){
        int bytes=0,fit=0;
        while(hint[bytes] && bytes<230){unsigned char c=(unsigned char)hint[bytes];int n=c<128?1:c<224?2:c<240?3:4;
            if(bytes+n>=250)break;
            memcpy(text+bytes,hint+bytes,(size_t)n);bytes+=n;text[bytes]=0;
            if(TextMediumLiteralWidth(text)>w-pad*2)break;
            fit=bytes;
        }
        if(!fit)break;
        memcpy(text,hint,(size_t)fit);text[fit]=0;DrawTextMediumLiteralWin(x+pad,y,text,UI_INK);hint+=fit;y+=line;
    }
    float tx,ty;TutorialTarget(&tx,&ty);
    DrawLessonMarker(tx,ty);
    const char *assist = TutorialNeedsHelp()
        ? Tr("STUCK? B: FREE PRACTICE", "卡住了？按 B 自由练习")
        : Tr("B: PRACTICE ARENA","B：进入训练场");
    DrawTextSmallLiteralWin(x+pad,top+UIS(150),assist,UI_INFO);
}

void DrawSaveStatusBanner(void) {
    if (g_saveState != 2) return;
    /* v3.0：**不在菜单/安全屋里显示** ——
       用户截图：这条红色横幅横穿 SHOP/BENCH/TREE/REST 四页底部，
       还写着 "TRIED: <本机绝对路径>"，
       第一眼让人以为存档废了。
       现在只在游戏中（PLAYING/PAUSED）提示，且**不显示路径和错误码**。 */
    if (currentState != STATE_PLAYING && currentState != STATE_PAUSED &&
        currentState != STATE_MAIN_MENU) return;
    int th = TextHeightWin();
    int h = th + th / 2;
    /* v3.0 **BUG 修复**：原来 msg 指向字符串字面量，直接传给 ClampRowText，
       而 ClampRowText 内部会 `buf[k] = 0;` / `buf[n] = '.';` ——
       往 .rdata 段里写，触发访问违例（程序直接消失）。
       只要文字超宽需要截断就会命中，窗口一窄必崩。
       现在用可写缓冲区。 */
    static char msg[160];
    snprintf(msg, sizeof(msg), "%s",
             (g_lang == LANG_ZH)
                ? "存档写入失败  -  进度只在本次运行有效"
                : "SAVE FAILED  -  PROGRESS ONLY LASTS THIS SESSION");
    ClampRowText(msg, winW - UIS(20));

    /* v2.9：挪到屏幕**底部** —— 原来钉在最顶上，把安全屋的页签整行盖住了
       （用户截图：SAVE FAILED 压在 SHOP/BENCH/TREE/REST 上）。 */
    int by0 = winH - h - th - UIS(4);
    BlendRectWin(0, by0, winW, by0 + h, 0x00240000u, 1.0f);
    BlendRectWin(0, by0, winW, by0 + UIS(2), 0x00C03030u, 0.95f);
    DrawTextCenterWin(winW / 2, by0 + (h - th) / 2, msg, 0x00FF9090u);

    /* v3.0：**删掉路径行** —— 玩家不需要看到本机路径这种调试信息。
       要定位问题看 g_saveErrAll 就够了。 */
}

void UiShowActionError(const char* message){
    snprintf(toast.message, sizeof(toast.message), "%s", message ? message : "");
    toast.until=PlatNow()+4;
}

void DrawPickupToast(void) {
    /* 锁盘打开时不画任何拾取/藏身 toast；它原来画在锁盘之后，
       会和锁盘底部说明文字直接叠在一起。 */
    if (lockActive) return;
    if(toast.message[0] && PlatNow()<toast.until &&
       (currentState==STATE_PLAYING || currentState==STATE_SAFEHOUSE)){
        int width=DrawTextSmallWidth(toast.message),x=(winW-width)/2,y=UIS(90);
        BlendRectWin(x-UIS(8),y-UIS(4),x+width+UIS(8),y+FONT_H+UIS(7),UI_BG_PANEL,.95f);
        DrawTextSmallWin(x,y,toast.message,0x00FF7060u);return;
    }




    if (!pickupMsg || pickupMsgTimer <= 0.0f) return;
    if(currentState==STATE_PLAYING && MonsterChaseActive())return;

    /* ===== v2.4.52 **只在游玩中显示** =====
       用户截图：暂停菜单上浮着一行 "AT THE EXIT: LEAVE, OR GO DEEPER."
       —— 那是上一帧的事件提示，因为 toast 画在帧末（所有图层之上），
       暂停菜单先画、toast 后画，于是压在菜单上。
       现在非游玩状态直接不画。 */
    if (currentState != STATE_PLAYING && currentState != STATE_SAFEHOUSE) return;
    if(currentState==STATE_SAFEHOUSE){
        int width=DrawTextSmallWidth(pickupMsg);
        int x=(winW-width)/2,y=UIS(82);
        BlendRectWin(x-UIS(8),y-UIS(4),x+width+UIS(8),y+FONT_H+UIS(7),UI_BG_PANEL,.94f);
        DrawTextSmallWin(x,y,pickupMsg,UI_ACCENT);return;
    }

    /* ===== v2.4.53 **模态面板打开时不画 toast** =====
       用户截图：出口二选一面板上，toast 的 "AT THE EXIT: LEAVE, OR GO DEEPER."
       和面板标题 "THE EXIT IS OPEN" 叠在一起。
       按 art_direction.h 铁律十三：**事件提示不该和功能 UI 抢同一块区域**。
       模态面板打开时它是最重要的东西，其他提示一律让路。 */
    {

        if (exitChoiceOpen) return;
        if (settingsPopupOpen) return;
    }

    int tw = TextWidthWin(pickupMsg);
    int tx = (winW - tw) / 2;
    int ty;

    if (hiddenIn >= 0) {
        /* v2.4.26：**躲藏时用小号字，贴在最左下角** ——
           用户两次反馈：先压到方框下沿，字号又太大。
           现在：scale=1 的小字 + 左对齐 x=20 + y 贴状态面板上方，
           画的宽度按小号字重新算，绝不会伸到中间的方框里。 */
        int sw2 = DrawTextSmallWidth(pickupMsg);
        int shH = FONT_H + 2;
        tx = UIS(16);
        /* v2.4.27：**再往上挪 70** ——
           原来 y = winH-150 正好压在左下状态面板的上边框上
           （用户截图：黄框和 SANITY 面板贴在一起）。
           面板顶大约在 winH-160，留出 30 的空隙。 */
        ty = winH - UIS(220);
        if (ty < UIS(120)) ty = UIS(120);
        BlendRectWin(tx - UIS(4), ty - UIS(3), tx + sw2 + UIS(4), ty + shH + UIS(3),
                     0x00101010u, 0.80f);
        FrameRectWin(tx - UIS(4), ty - UIS(3), tx + sw2 + UIS(4), ty + shH + UIS(3),
                     0x00E8E060u, 0.55f, 1);
        DrawTextSmallWin(tx, ty, pickupMsg, 0x00F0E870u);
        return;
    } else {
        /* ================================================================
         * 2026-11-08（实机反馈）：**播报行压住 BOOST**
         * ----------------------------------------------------------------
         * 原来写死 ty = UIS(150)，而左上角第 5 行（BOOST）在 130..154 ——
         * 实测截图里 `THREAT: SOUND HUNTER…` 正好盖在 `BOOST +50%` 上。
         *
         * 现在按"最满 6 行"算出 HUD 区块下沿，再往下让 10px。
         * 同时：太长就降级成小号字（原来是直接画出去被窗口裁掉）。
         * ================================================================ */
        int hudBottom = UIS(18) + 6 * (TextHeightWin() + UIS(4));
        ty = hudBottom + UIS(10);
        if (ty < TextHeightWin() * 6) ty = TextHeightWin() * 6;
    }

    /* 2026-11-08：判据必须对**渲染缓冲**（renderW），不是窗口宽 ——
       文字是按窗口尺度排的，画进缓冲就溢出。实测 1280 窗口 / 800 缓冲下，
       `tw=992` 对 winW 判"不长"，但画出来右边被切掉一截。 */
    if (tw > renderW - 40) {
        /* 太长：小号字重算宽度并居中，绝不画到窗口外 */
        int sw3 = DrawTextSmallWidth(pickupMsg);
        int tx3 = (winW - sw3) / 2;
        if (tx3 < UIS(8)) tx3 = UIS(8);
        DrawTextSmallWin(tx3, ty, pickupMsg, 0x00F0E870u);
    } else {
        DrawTextWin(tx, ty, pickupMsg, 0x00E8E060u);
    }
}

/* A persistent framed room panel is the only owner of room interaction text. */
void UiDrawRoomHint(void) {
    if(currentState!=STATE_PLAYING || !roomTheme.active || !roomTheme.inside ||
       calibActive || lockActive || exitChoiceOpen || (isMapOpen && mapFullScreen))return;
    int x=UIS(20),pad=8,width=winW*48/100;
    if(width>UIS(380))width=UIS(380);
    int line=FONT_H+5,height=line*5+pad*2,y;
    int top=UIS(18)+TextHeightWin()*3+UIS(12),bottom=winH-UIS(150);
    if(!UiFindFreeBand(x,top,width,height,bottom,&y))return;
    BlendRectWin(x,y,x+width,y+height,UI_BG_PANEL,.96f);
    FrameRectWin(x,y,x+width,y+height,UI_EDGE,.9f,1);
    char heading[160];
    snprintf(heading,sizeof(heading),"%s / %s",RoomThemeName(roomTheme.theme,g_lang==LANG_ZH),
             roomTheme.spent?Tr("ACTIVE / USED","已生效 / 已用"):Tr("ROOM DEVICE","主题装置"));
    UiTrimSmallText(heading,width-pad*2);DrawTextSmallLiteralWin(x+pad,y+pad,heading,UI_ACCENT);
    const char* body=RoomThemeNotification();
    if(!body[0])body=(hoverKind==3 && hoverIndex>=0 && hoverIndex<roomPropCount && RoomPropIsControl(roomProps[hoverIndex].kind))?RoomThemeFixtureLine(hoverIndex):RoomThemeInstruction();
    char text[512];snprintf(text,sizeof(text),"%s",body);const char* cursor=text;
    for(int row=0;row<3 && *cursor;row++) {
        char part[512];int used=0,fit=0;
        while(cursor[used] && used<490){unsigned char c=(unsigned char)cursor[used];int bytes=c<128?1:c<224?2:c<240?3:4;
            memcpy(part+used,cursor+used,(size_t)bytes);used+=bytes;part[used]=0;
            if(TextSmallLiteralWidth(part)>width-pad*2)break;
            fit=used;
        }
        if(!fit)break;
        memcpy(part,cursor,(size_t)fit);part[fit]=0;cursor+=fit;
        DrawTextSmallLiteralWin(x+pad,y+pad+line*(row+1),part,UI_INK);
    }
    char action[128];
    if(roomTheme.spent)snprintf(action,sizeof(action),"%s",Tr("THIS FLOOR: SINGLE USE COMPLETE","本层一次性装置已结算"));
    else snprintf(action,sizeof(action),Tr("HOLD %s TO INTERACT / RELEASE TO CANCEL","按住%s交互，松开取消"),VkName(g_bindings[ACT_INTERACT]));
    UiTrimSmallText(action,width-pad*2);DrawTextSmallLiteralWin(x+pad,y+pad+line*4,action,UI_INK_DIM);
    if(holdingNow && (hoverKind==3 || hoverKind==4))UiBar(x+pad,y+height-5,width-pad*2,3,fminf(1,holdTimer/SEARCH_TIME),UI_BG_ROW,UI_ACCENT);
}
static int threatNoticeKind=0;
static double threatNoticeUntil=0;
void UiThreatNotice(int kind){threatNoticeKind=kind;threatNoticeUntil=g_timeSec+3.5;}
void UiDrawThreatNotice(void) {
    if(currentState==STATE_MAIN_MENU){threatNoticeKind=0;return;}
    if(currentState!=STATE_PLAYING || !threatNoticeKind || g_timeSec>threatNoticeUntil || calibActive || lockActive || exitChoiceOpen)return;
    const char* message=threatNoticeKind==1?Tr("Something is coming.","有某种不明生物正在靠近。"):
        Tr("You have been targeted.","你已被守门人锁定。");
    int width=DrawTextSmallWidth(message)+20,x=winW-width-UIS(20),y;
    if(x<UIS(20))x=UIS(20);
    if(!UiFindFreeBand(x,UIS(18)+TextHeightWin(),width,FONT_H+18,winH/2,&y))return;
    BlendRectWin(x,y,x+width,y+FONT_H+18,UI_BG_PANEL,.95f);
    FrameRectWin(x,y,x+width,y+FONT_H+18,UI_DANGER,.9f,1);
    DrawTextSmallLiteralWin(x+10,y+8,message,UI_WARN);
}

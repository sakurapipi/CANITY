#include "tactical_tools.h"
#include "skills.h"
#include "threat_tier.h"
#include "monster_evolution.h"
/*
 * Canity — 渲染层实现：像素缓冲 / 贴图与查表 / 多线程光栅 / 3D / 精灵 / 地图
 *
 * 代码从原 main.c 原样搬出，函数体未做任何逻辑改动。
 */
#include "protocol.h"
#include "threat_telegraph.h"
#include "common.h"
#include "room_theme.h"
#include "perception.h"
#include "room_material.h"
#include "collapse.h"
#include "echo_marks.h"
#include "item_effects.h"
#include "lang.h"
#include "phase.h"
#include "state.h"
#include "upgrade_run.h"
#include "warden.h"
#include "wrench.h"
#include "control_effect.h"
#include "spider.h"
#include "hatch.h"
#include "map_rooms.h"
#include "cell_meta.h"
#include "render.h"
#include "suspicion.h"
#include "art.h"
#include "bench.h"
#include "weapons.h"
#include "extras.h"
#include "sprites.h"
#include "loop_api.h"
#include "player_api.h"
#include "world_api.h"
#include "present_api.h"
#include "mimic.h"
#include "entity.h"
#include "ui.h"
#include "plat.h"

static float FlareLightAt(float wx,float wy);
void DrawCreatureActor(int spr,float wx,float wy,float halfH,float lightK,
                              int zTest,float facing,float stride,unsigned int tint,float tintMix,float activity);

int renderW = 800, renderH = 600;    /* 内部渲染分辨率 */
static const float fovPlaneLen[5] = {0.6494f, 0.7673f, 0.9163f, 1.0926f, 1.2972f};
unsigned int* pixels = NULL;
unsigned char* vignetteMask = NULL;

unsigned int texture[TEX_HEIGHT][TEX_WIDTH];
static unsigned int sceneTexture[THEME_COUNT][3][TEX_HEIGHT][TEX_WIDTH];

float zBuffer[MAX_SCREEN];
float flashTab[MAX_SCREEN];
int ceilTab[MAX_SCREEN];
float floorGain[MAX_SCREEN];   /* 0.52 + cone*1.30，按列预计算 */
static int floorGainI[MAX_SCREEN];   /* v1.9：同一增益的定点版（×256），地板内层免掉浮点 */


void BuildShadeTables(void);
void GenerateThemeTextures(int id);

static unsigned char noiseTab[256];
static int noiseReady = 0;

unsigned int floorBaseA = 0x00151515u, floorBaseB = 0x00202020u;
int themeId = 0;
/* 2026-11-16：上下视角做成**可开关**（默认关，保持 2.4.3 的水平镜头）。
   开关来源：设置页 "Vertical Look"；关闭时 pitchRatio 恒为 0。 */
int pitchLookEnabled = 0;
/* 2.4.7-RC: stable y-shear, not a rotated near plane. At collision distance
   the former rotated wall endpoint could cross zero depth and explode in size. */
float pitchRatio = 0.0f;
int horizon = 0;

void RenderSetLookMode(int enabled) {
    pitchLookEnabled=enabled ? 1 : 0;
    pitchRatio=0;horizon=0;
    /* Restore this mode's own lens immediately; do not inherit the other mode's
       normalization or blindness treatment until a later input/logic tick. */
    ApplyFov();uiDirty=1;
}
/* Vertical input stays available beside walls. Comfort rendering must never
   change collision, camera position or whether the player can look around. */
void RenderAddPitchInput(float delta) {
    if (!pitchLookEnabled) { pitchRatio=0;return; }
    if (!isfinite(pitchRatio)) pitchRatio=0;
    if (!isfinite(delta)) return;
    pitchRatio=fmaxf(-1,fminf(1,pitchRatio+delta));
}

void UpdatePitch(void) {
    if (!pitchLookEnabled) { pitchRatio=0;horizon=0;return; }
    if (!isfinite(pitchRatio)) pitchRatio = 0.0f;
    if (pitchRatio > 1.0f) pitchRatio = 1.0f;
    else if (pitchRatio < -1.0f) pitchRatio = -1.0f;
    /* Experimental look changes only the vertical offset; OFF exits above. */
    horizon = renderH > 0 ? (int)((float)renderH *
        tanf(pitchRatio * RENDER_LOOK_LIMIT_RADIANS)) : 0;
}
#define PREMUL_LEVELS 256
#define PREMUL_K8_SHIFT 2
static int storyMaterialActive;
static int SceneMaterialAt(int x,int y) {
    if (storyMaterialActive) return themeId;
    if ((unsigned)x<MAP_WIDTH && (unsigned)y<MAP_HEIGHT) {
        const CellMeta *m=&cellMeta[y][x];
        if ((m->roomId || (m->flags&CELLFLAG_ROOMWALL)) && m->themeId<THEME_COUNT)
            return m->themeId;
    }
    return themeId;
}

static unsigned SurfaceTexel(int material,int surface,int x,int y,int level) {
    unsigned c=sceneTexture[material][surface][y][x];
    unsigned r=(((c>>16)&255)*level)>>6;
    unsigned g=(((c>>8)&255)*level)>>6;
    unsigned b=((c&255)*level)>>6;
    return ((r>255?255:r)<<16)|((g>255?255:g)<<8)|(b>255?255:b);
}

/* v2.0：墙面凹凸光照 —— 由贴图亮度算梯度，墙上不再是纯平面。
   每像素只多一次 byte load + 一次加法。 */
static signed char bump[TEX_HEIGHT][TEX_WIDTH];

/* v2.0：mipmap。墙/地在远处一个像素吃掉多个 texel，单点采样就是摩尔纹
   （128 贴图的细节反而变成噪点）。这里预生成 5 级，按每像素 texel 数选级。 */
#define MIP_LEVELS 5
static unsigned int mipChain[MIP_LEVELS][TEX_WIDTH * TEX_HEIGHT];
static void SetStoryLight(int chapter);
static void SetThemeLight(int id);   /* v2.1 主题光色（雾/手电），定义在下面 */
static void DrawGroundShadow(int cx, int cyFloor, int halfW, float ty);   /* v2.1 落地阴影 */

void GenerateThemeTextures(int id) {
    if (id < 0 || id >= THEME_COUNT) id = 0;
    SetThemeLight(id);      /* Material, fog and flashlight share one validated identity. */
    themeId = id;
    storyMaterialActive=ProtocolPeacefulStory();
    if (storyMaterialActive) SetStoryLight(protocolRun.story);

    floorBaseA=ArtDollGrade(RoomMaterialSample(id,8,8,1));
    floorBaseB=ArtDollGrade(RoomMaterialSample(id,16,16,1));
    for (int material=0;material<THEME_COUNT;material++)
        for (int surface=0;surface<3;surface++)
            for (int y=0;y<TEX_HEIGHT;y+=2) for (int x=0;x<TEX_WIDTH;x+=2) {
                unsigned c=ArtDollGrade(storyMaterialActive && material==id?
                    StoryMaterialSample(protocolRun.story,x,y,surface):RoomMaterialSample(material,x,y,surface));
                sceneTexture[material][surface][y][x]=sceneTexture[material][surface][y][x+1]=c;
                sceneTexture[material][surface][y+1][x]=sceneTexture[material][surface][y+1][x+1]=c;
            }
    memcpy(texture,sceneTexture[id][0],sizeof texture);

    if (!noiseReady) {
        for (int i = 0; i < 256; i++) noiseTab[i] = (unsigned char)(rand() % 40);
        noiseReady = 1;
    }
    /* v2.0：生成 mip 链（只在换主题时跑一次） */
    for (int y = 0; y < TEX_HEIGHT; y++)
        for (int x = 0; x < TEX_WIDTH; x++)
            mipChain[0][y * TEX_WIDTH + x] = texture[y][x];
    for (int L = 1; L < MIP_LEVELS; L++) {
        int dw = TEX_WIDTH >> L, dh = TEX_HEIGHT >> L;
        int sw = dw << 1;
        for (int y = 0; y < dh; y++) {
            for (int x = 0; x < dw; x++) {
                int r = 0, g = 0, b = 0;
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        unsigned int t = mipChain[L - 1][(y * 2 + dy) * sw + (x * 2 + dx)];
                        r += (t >> 16) & 0xFF; g += (t >> 8) & 0xFF; b += t & 0xFF;
                    }
                }
                mipChain[L][y * dw + x] = (unsigned int)(((r / 4) << 16) | ((g / 4) << 8) | (b / 4));
            }
        }
    }

    /* v2.0：由贴图亮度算一遍凹凸梯度（只在换主题时跑一次） */
    for (int y = 0; y < TEX_HEIGHT; y++) {
        for (int x = 0; x < TEX_WIDTH; x++) {
            int xm = (x - 1) & (TEX_WIDTH - 1), xp = (x + 1) & (TEX_WIDTH - 1);
            int ym = (y - 1) & (TEX_HEIGHT - 1), yp = (y + 1) & (TEX_HEIGHT - 1);
            unsigned int a = texture[ym][xm], b = texture[yp][xp];
            int la = (int)(((a >> 16) & 0xFF) + ((a >> 8) & 0xFF) + (a & 0xFF)) / 3;
            int lb = (int)(((b >> 16) & 0xFF) + ((b >> 8) & 0xFF) + (b & 0xFF)) / 3;
            int d = (lb - la) / 4;
            if (d > 26) d = 26;
            if (d < -26) d = -26;
            bump[y][x] = (signed char)d;
        }
    }

    BuildShadeTables();
}

/* ------------------------------------------------------------------ */
/* 地图生成                                                            */
/* ------------------------------------------------------------------ */
/* Stable lens: explicit FOV settings control magnification. Blindness is a
   visibility effect in post-processing, never an automatic camera zoom. */
#include "domain/render/legacy/render_246_fov.inc"
void ApplyFov(void) {
    if(!pitchLookEnabled) { ApplyFovLegacy246(); return; }
    if(fovIndex<0 || fovIndex>=5)fovIndex=2;
    float length=sqrtf(dirX*dirX+dirY*dirY);
    if(!isfinite(length) || length<.000001f){dirX=1;dirY=0;}
    else {dirX/=length;dirY/=length;}
    float L=fovPlaneLen[fovIndex];
    /* v1.9：像素必须是方的。
       射线投射里 屏幕像素宽/高的比例 = (renderW/2)/L : renderH ，
       要保持 1:1 就得 L = (renderW/renderH)/2。
       原来的 L 是按 4:3 调好的，所以这里乘上「实际宽高比 / 4:3」即可 ——
       效果是宽屏上**水平视野变宽**（Hor+），而不是把画面拉扁。 */
    {
        float aspect = (renderH > 0) ? (float)renderW / (float)renderH : 4.0f / 3.0f;
        L *= aspect / (4.0f / 3.0f);
    }
    planeX = -dirY * L;
    planeY = dirX * L;
}

/* 该位置是否离已有物件太近（防止箱子/配电箱/花/怪互相叠在一起） */
/* ------------------------------------------------------------------ */
/* 3D 渲染                                                             */
/* ------------------------------------------------------------------ */

/* 把世界坐标投影成屏幕矩形（billboard） */
/* 2026-11-16：SpriteBox/ProjectSprite 改由 render.h 声明（拆出的
   render_decoy.c 也要用），定义仍在本文件。 */

/* v2.2：精灵亮度统一公式。
   审计发现同一件事有 4 份拷贝（箱子 / 怪 / 偷电怪 / 光敏怪），
   衰减曲线和下限（0.10 / 0.12 / 0.14）都不一样 —— 调一处另三处不动。
   现在全部走这一个函数。 */
static float SpriteShade(float ty, float minK) {
    float k = 1.0f - ty / 16.0f;
    if (k < minK) k = minK;
    if (k > 1.0f) k = 1.0f;
    if(!flashlightOn || battery<=0 || smokeDim<1)k*=.22f;
    return k;
}

static int PropCoversThreat(const SpriteBox* prop,float x,float y) {
    if(!HasLOS(posX,posY,x,y))return 0;
    SpriteBox actor=ProjectSprite(x,y,0,.8f);
    return actor.ok && actor.ty>prop->ty+.1f && actor.ty<18 &&
           abs(actor.centerX-prop->centerX)<(prop->width+actor.width)/2 &&
           actor.baseY>prop->baseY-prop->height && actor.baseY-actor.height<prop->baseY;
}
static float PropThreatAlpha(const SpriteBox* prop) {
    if(prop->height<renderH/8 || prop->ty>8)return 1;
    Monster* actors[MON_HUNTER_MAX+LIGHTBEAST_MAX];
    int count=MonsterCollect(actors,MON_HUNTER_MAX+LIGHTBEAST_MAX,1);
    for(int i=0;i<count;i++)if(PropCoversThreat(prop,actors[i]->x,actors[i]->y))return .38f;
    if(WardenActive() && PropCoversThreat(prop,WardenX(),WardenY()))return .38f;
    if(ScavengerActive() && PropCoversThreat(prop,ScavengerX(),ScavengerY()))return .38f;
    if(CollectorActive() && PropCoversThreat(prop,CollectorX(),CollectorY()))return .38f;
    if(SpiderActive() && PropCoversThreat(prop,SpiderX(),SpiderY()))return .38f;
    return 1;
}
void DrawBakedProp(int spr, float wx, float wy, float baseZ, float halfH, float lightK);
void DrawBakedPropArt(int spr, float wx, float wy, float baseZ, float halfH, float lightK);   /* v2.1 */

/* ==================================================================
 * 2026-11-06（v2.4-rc P1）：**lift 改成 baseZ**
 * ------------------------------------------------------------------
 * 原来第 3 个参数叫 lift，作用是从**顶部**把精灵往下裁：
 *     s.y0 = yTop + unit * lift;
 * 于是它只能让物件"变矮"，不能让底边离开地面 ——
 * 想做"货架三层叠起来"根本做不到。
 *
 * baseZ 是把**底边**抬到指定高度：
 *     cyBase = cyFloor - unit * baseZ;
 * 这样同一个物件可以用 2~3 层精灵叠出真实的高度差，
 * 走动时层间有视差，才像立体的。
 * baseZ = 0 时与旧行为**逐像素相同**。
 * ================================================================ */
/* Original 2.4.6 horizontal projection and clipping are the reference for
   both modes. Do not replace clipped model sizes with full sizes here: that
   changes nearby monsters/props even at zero degrees and with look disabled. */
static SpriteBox ProjectSpriteLevel(float wx, float wy, float baseZ, float halfH) {
    SpriteBox s = {0};
    float dx=wx-posX,dy=wy-posY;
    float det=planeX*dirY-dirX*planeY;
    if(!isfinite(det) || fabsf(det)<.000001f)return s;
    float invDet=1.0f/det;
    float tx=invDet*(dirY*dx-dirX*dy);
    float ty=invDet*(-planeY*dx+planeX*dy);
    if(!isfinite(tx) || !isfinite(ty) || ty<.05f)return s;
    float unit=(float)renderH/ty;
    int cx=(int)((renderW/2.0f)*(1.0f+tx/ty));
    int half=(int)(unit*.28f);
    int cyFloor=renderH/2-(int)(-.5f*unit);
    int cyBase=cyFloor-(int)(unit*baseZ);
    int yTop=cyBase-(int)(unit*halfH);
    s.ty=ty;
    s.x0=cx-half;if(s.x0<0)s.x0=0;
    s.x1=cx+half;if(s.x1>=renderW)s.x1=renderW-1;
    s.y0=yTop;if(s.y0<0)s.y0=0;
    s.y1=cyBase;if(s.y1>=renderH)s.y1=renderH-1;
    s.ok=s.x1>s.x0 && s.y1>s.y0;
    s.centerX=(s.x0+s.x1)/2;s.baseY=s.y1;
    s.width=s.x1-s.x0;s.height=s.y1-s.y0;
    return s;
}

#include "domain/render/legacy/render_246_sprite.inc"
SpriteBox ProjectSprite(float wx, float wy, float baseZ, float halfH) {
    if(!pitchLookEnabled) { return ProjectSpriteLegacy246(wx,wy,baseZ,halfH); }
    SpriteBox s=ProjectSpriteLevel(wx,wy,baseZ,halfH);
    /* Hard gate: OFF and ON at zero use the identical legacy projection. */
    if(!pitchLookEnabled || horizon==0 || !s.ok)return s;
    /* Translate the already established model geometry. Output clipping may
       hide pixels, but must not change the model size or the horizontal FOV. */
    s.baseY+=horizon;
    s.y0+=horizon;s.y1+=horizon;
    if(s.y0<0)s.y0=0;
    if(s.y1>=renderH)s.y1=renderH-1;
    s.ok=s.x1>s.x0 && s.y1>s.y0;
    return s;
}
/* ---------------- 精灵：统一的投影 + 排序绘制 ---------------- */

/* v1.9：精灵也吃光照。
   原来箱子 / 安全区 / 配电箱 / 光球都是 PutPixelAdd 平涂，
   不看距离也不看手电 —— 远处的箱子和贴脸一样亮，很出戏。
   每个精灵绘制前用它的距离算系数写进 spriteLight，PutPixelAdd 内部统一乘。 */
float spriteLight = 1.0f;

/* v2.1：每个主题自己的雾色 / 手电色。
   原来 7 个主题只换贴图颜色，雾和光是一套死常量，
   所以"冰窖"和"血肉洞窟"只差墙纸 —— 切换时玩家感觉不到换了地方。 */
static int themeFogR = 20, themeFogG = 25, themeFogB = 33;   /* 距离雾 */
static int themeLitR = 96, themeLitG = 98, themeLitB = 96;   /* 手电加色 */
static int themeAmbR = 22, themeAmbG = 27, themeAmbB = 36;   /* 墙体环境雾 */

static void SetStoryLight(int chapter) {
    static const unsigned char fog[][3]={{28,22,17},{19,25,27},{23,25,17}};
    const unsigned char *c=fog[chapter-1];
    themeFogR=c[0];themeFogG=c[1];themeFogB=c[2];
    themeLitR=105;themeLitG=103;themeLitB=96;
}
static void SetThemeLight(int id) {
    switch (id) {
    case 0: themeFogR=22; themeFogG=24; themeFogB=28; themeLitR=104; themeLitG=100; themeLitB=92;  break; /* BRICK    暖灰 */
    case 1: themeFogR=18; themeFogG=22; themeFogB=30; themeLitR=86;  themeLitG=92;  themeLitB=104; break; /* PRISON   冷灰 */
    case 2: themeFogR=40; themeFogG=14; themeFogB=8;  themeLitR=130; themeLitG=80;  themeLitB=60;  break; /* HELL     暖红 */
    case 3: themeFogR=20; themeFogG=29; themeFogB=24; themeLitR=96;  themeLitG=112; themeLitB=96; break; /* WARD     青白 */
    case 4: themeFogR=16; themeFogG=30; themeFogB=44; themeLitR=80;  themeLitG=120; themeLitB=140; break; /* ICE      冰蓝 */
    case 5: themeFogR=28; themeFogG=22; themeFogB=16; themeLitR=120; themeLitG=96;  themeLitB=70;  break; /* RUST     锈黄 */
    default:themeFogR=30; themeFogG=10; themeFogB=8;  themeLitR=124; themeLitG=76;  themeLitB=60;  break; /* FLESH    暗紫 */
    }
}



static void DrawCrateSprite(int i) {
    if (i < 0 || i >= containerCount) return;
    Container* c = &containers[i];
    /* 2026-11-05：这个槽被宝箱怪占着 —— 它由 DrawMimicSprite 自己画。
       这里再画一次就会出现「箱子上叠一个箱子」。 */
    if (c->mimicHost) return;
    int material=SceneMaterialAt((int)c->x,(int)c->y);
    int state=c->searched?1:c->locked?2:0;
    int sprite=storyMaterialActive?SPR_MISSION_CASE+2*(protocolRun.story-1)+!!c->searched:
        SPR_THEME_CRATE_BASE+material*3+state;
    DrawBakedPropArt(sprite,c->x,c->y,0,.34f,c->searched?.78f:1.0f);
}

/* 2026-11-05：宝箱怪。
   伪装态 / 识破态 → 华丽箱子（呼吸起伏在 mimic.c 的 MimicTick 里推进相位）
   现形态         → 箱子站起来的那只 */
static void DrawMimicSprite(int i) {
    if (i < 0 || i >= mimicCount) return;
    Mimic* m = &mimics[i];
    if (m->state == MIM_NONE || m->state == MIM_GONE) return;

    if (m->state == MIM_ANGRY) {
        DrawCreatureActor(SPR_MON_MIMIC,m->x,m->y,.52f,1,1,0,m->breathPh,0,0,1);
    } else {
        int material=SceneMaterialAt((int)m->x,(int)m->y);
        int locked=m->containerIdx>=0 && m->containerIdx<containerCount && containers[m->containerIdx].locked;
        float breath=.34f+.008f*sinf(m->breathPh);
        DrawBakedPropArt(SPR_THEME_CRATE_BASE+material*3+(locked?2:0),m->x,m->y,0,breath,1);
    }
}

/* v3.0：藏身点渲染。
   原来全项目 render.c/ui.c 对 hideSpots 零引用 —— 玩家根本看不见
   衣柜/床底/桌下，只有按下 E 之后闪 1.6 秒的一行字。
   这里复用箱子的精灵，缩小 + 压暗，和真箱子区分开。 */
/* 藏身点：垃圾桶。之前复用箱子精灵缩小压暗，玩家根本分不清是箱子还是藏身点。 */
static void DrawHideSpotSprite(int i) {
    if (i < 0 || i >= hideSpotCount) return;
    HideSpot* h = &hideSpots[i];
    if (!h->active) return;
    float k = (h->kind == 0) ? 1.00f : (h->kind == 1 ? 0.90f : 0.82f);
    int material=SceneMaterialAt((int)h->x,(int)h->y);
    if (!(protocolRun.flags&PROTOCOL_MISSIONS) && protocolRun.story==1 && i==0) {
        DrawBakedPropArt(SPR_STORY_PROP_BASE+STORY_POD*2+ProtocolShelterPowered(),h->x,h->y,0,.73f,1);
        return;
    }
    int sprite=storyMaterialActive?SPR_MISSION_COVER+2*(protocolRun.story-1):SPR_THEME_HIDE_BASE+material;
    DrawBakedPropArt(sprite,h->x,h->y,0,.67f,.9f*k);
}
static void DrawBreakerSprite(void) {
    if (!breaker.active) return;
    float k = breakerOpen ? 1.15f : 0.95f;
    int material=SceneMaterialAt((int)breaker.x,(int)breaker.y);
    DrawBakedPropArt(SPR_THEME_BREAKER_BASE+material*2+!!breakerOpen,breaker.x,breaker.y,0,.58f,k);
}

/* ------------------------------------------------------------------ */

/* v2.1：场景道具统一走烘焙精灵（原来是 relX/relY 硬编码的字符画） */
/* ===== v2.4.3 **道具的距离衰减** =====
   用户要求：道具也要「远处看不清、近处才看得清」。
   但道具比怪物**温和一档** —— 太黑会让玩家找不到箱子/配电箱。
     · 怪物：2.5 格全亮，11 格外只剩 5%
     · 道具：3.5 格全亮，13 格外剩 12%
   调参在这里改。 */
/* v2.4.3: 距离曲线已统一到 src/art.c 的 ArtPropLight() */
static float PropProximityLight(float wx, float wy) { return ArtPropLight(wx, wy); }

void DrawBakedProp(int spr, float wx, float wy, float baseZ, float halfH, float lightK) {
    SpriteBox s = ProjectSprite(wx, wy, baseZ, halfH);
    if (!s.ok) return;
    /* v3.2：贴脸时直接不画。箱子/配电箱没有碰撞，玩家可以站进模型里，
       这时 billboard 的投影会被放大到几倍屏幕、看起来像视角畸变。
       近平面裁掉比硬拉一个巨大精灵更干净；交互提示仍然保留。 */
    if (s.ty < 0.55f) return;
    int w = s.width, h = s.height;
    if (w < 2 || h < 2) return;
    /* v3.2：道具按**投影高度**缩放，不再用 max(w,h)。
       箱子的 halfH=0.34，但固定 half 宽是 0.56；
       用 max 会把箱子按宽度放大 1.65 倍，贴脸时尤其像一个巨大的方块。
       烘焙图的画布是正方形的，按高度画才能保持半高参数的真实含义。 */
    int size = h;

    float light = SpriteShade(s.ty, 0.12f) * PropProximityLight(wx, wy);   /* v2.4.3 距离衰减 */
    DrawGroundShadow(s.centerX, s.baseY, w / 3, s.ty);

    /* ==================================================================
     * 2026-11-06：**老物件保持原尺寸**
     * ------------------------------------------------------------------
     * 上一轮我给 Sprite 加了内容包围盒，并把包围盒高度映射成 halfH ——
     * 结果箱子/垃圾桶等老物件的视觉尺寸放大了约 2.7 倍
     * （它们的艺术只占画布的 37%，而 halfH 原来是按画布给的）。
     * 用户反馈：「箱子模型过大，隐藏点模型过大」。
     *
     * 这里恢复原有语义：**halfH 是画布高度**，按正方形绘制。
     * 需要"按艺术高度"精确控制的新物件（房间家具）走 DrawBakedPropArt。
     * ================================================================ */
    {
        int x = s.centerX - size / 2;
        int y = s.baseY - size;
        DrawSprite(spr, x, y, size, size, light * lightK, PropThreatAlpha(&s), 1, s.ty);
        return;
    }
}

/* 按**内容包围盒**绘制的版本：halfH 是艺术本身的高度，
   底边对齐地面，宽度按包围盒宽高比还原。
   房间家具（货架/控制台）用它 —— 它们的体积需要精确控制。 */
void DrawBakedPropArt(int spr, float wx, float wy, float baseZ, float halfH, float lightK) {
    SpriteBox s = ProjectSprite(wx, wy, baseZ, halfH);
    if (!s.ok) return;
    /* 贴脸不画（和旧路径一致，避免巨型精灵糊屏） */
    if (s.ty < 0.55f) return;
    int boxW = s.width;
    int h = s.height;
    if (boxW < 2 || h < 2) return;

    float light = SpriteShade(s.ty, 0.12f) * PropProximityLight(wx, wy);
    DrawGroundShadow(s.centerX, s.baseY, boxW / 3, s.ty);

    /* ==================================================================
     * 2026-11-06：**只画艺术包围盒，别画整块画布**
     * ------------------------------------------------------------------
     * 上一版把整个 128x128 画布缩放到"包围盒高度 = halfH"，
     * 于是画布被放大了 SPR_H/bboxH 倍 —— 箱子实测 2.67 倍。
     * 那 2.67 倍里绝大部分是全透明像素，但每个像素仍要走一遍采样循环。
     * 这就是用户说的"每次路过都掉帧"。
     *
     * 现在直接用带深度测试的子矩形 blit，源矩形就是包围盒本身：
     *   · 目标高度 = 投影高度
     *   · 目标宽度 = 投影高度 × 包围盒宽高比
     *   · 画的像素数从 (2.67×size)² 降到 size² 以内，约 7 倍开销削减
     * ================================================================ */
    Sprite* sp = SpriteGet(spr);
    int bw = 0, bh = 0;
    if (sp) { bw = sp->bx1 - sp->bx0 + 1; bh = sp->by1 - sp->by0 + 1; }

    if (sp && bw > 0 && bh > 0) {
        int dw = (int)((long)h * bw / bh);
        if (dw < 1) dw = 1;
        int dx = s.centerX - dw / 2;
        int dy = s.baseY - h;
        if(spr==SPR_PROP_SPIDER || spr==SPR_PROP_CRATE_MIMIC){
            float scale=(float)h/bh;
            int fullW=(int)(sp->w*scale+.5f),fullH=(int)(sp->h*scale+.5f);
            SpritePose pose={(float)g_timeSec+(spr==SPR_PROP_SPIDER?.8f:2.1f),(float)g_timeSec*1.8f,spr==SPR_PROP_SPIDER?.3f:0,0};
            float localLight=light*lightK+FlareLightAt(wx,wy)*.8f;
            DrawSpriteCreature(spr,dx-(int)(sp->bx0*scale),dy-(int)(sp->by0*scale),fullW,fullH,localLight,1,1,s.ty,0,0,&pose);
        }else{
            DrawSpriteSubZ(spr, sp->bx0, sp->by0, bw, bh, dx, dy, dw, h,light*lightK,PropThreatAlpha(&s),s.ty);
        }
    } else {
        /* 包围盒无效（全透明）：退回正方形绘制 */
        int x = s.centerX - h / 2;
        DrawSprite(spr, x, s.baseY - h, h, h, light * lightK, PropThreatAlpha(&s), 1, s.ty);
    }
}
/* v2.1：怪物改用烘焙精灵                                              */
/*   旧版是一个函数服务两种怪、共享一张剪影，只换眼睛颜色 —— 7 只怪长得   */
/*   一样。新版每只怪一张 128×128 烘焙图（见 sprites.c 的 BakeMonster）。  */
/* ------------------------------------------------------------------ */
/* v2.2：朝向选择（Doom 的 R_ProjectSprite 同款思路）
   算「玩家 → 怪」的角度与怪朝向的差，落到 4 个扇区；
   侧面用水平镜像覆盖另一侧 —— 只烘 2 个朝向就能覆盖 4 个视角。 */
static int a_back_light = 0;   /* v2.2：本次绘制是否背向镜头 */
static void MonsterSpriteSelect(int base, float wx, float wy, const Monster* m,
                                int* outSpr, int* outFlip, int* outFrame) {
    float dx = wx - posX, dy = wy - posY;
    /* v3.0：**朝向判据换掉**。
       原来用「怪在屏幕上的方位角」，但默认 16:9 + FOV85 的可视半角只有 50.7°，
       永远进不了 60° 的侧面扇区 → dir 恒为 0，6 张侧面帧全是白烘的图。
       现在用「玩家→怪 的射线」与「怪自己面朝的方向」之间的夹角：
       怪正对着你 = 正面，背对着你 = 背面，侧身 = 侧面。 */
    float ang = atan2f(dy, dx);
    float rel = ang - m->facing;          /* 怪朝向与「怪→玩家」方向的差 */
    /* rel≈π 表示怪正面朝着玩家 */
    rel = rel - 3.14159265f;
    while (rel > 3.14159265f) rel -= 6.2831853f;
    while (rel < -3.14159265f) rel += 6.2831853f;
    while (rel > 3.14159265f) rel -= 6.2831853f;
    while (rel < -3.14159265f) rel += 6.2831853f;
    float a = fabsf(rel);

    int dir;      /* 0 正面 / 1 侧面 */
    int flip = 0;
    a_back_light = 0;   /* v3.0：每次进来先复位，否则上一只怪的背光压暗会漏到下一只 */
    if (a < 1.047f) {            /* ±60°：正面 */
        dir = 0;
    } else if (a < 2.094f) {     /* 60~120°：侧面 */
        dir = 1;
        flip = (rel < 0.0f) ? 1 : 0;
    } else {                     /* 背后：用侧面剪影（看不到脸），再压暗一档 */
        dir = 1;
        flip = (rel < 0.0f) ? 1 : 0;
        a_back_light = 1;
    }

    /* v3.0：走路帧用**每只怪自己的相位**（建模审查 #3）。
       静止/硬直时相位不动，所以不会再原地踏步，多只怪也不会同相。 */
    int frame = ((int)(m->walkPhase * 3.0f)) & 1;

    if (dir == 0 && a < 1.047f) a_back_light = 0;
    *outSpr = base + dir * 2 + frame;
    *outFlip = flip;
    *outFrame = frame;
}

void DrawCreatureActor(int spr,float wx,float wy,float halfH,float lightK,
                              int zTest,float facing,float stride,unsigned int tint,float tintMix,float activity){
    SpriteBox box=ProjectSprite(wx,wy,0,halfH);
    if(!box.ok || box.ty<.55f || !SpritesReady())return;
    int w=box.width,h=box.height,size=w>h?w:h;if(size<2)return;
    int use=spr,mirror=0,frame=0;
    if(spr==SPR_MON_HUNTER || spr==SPR_MON_HUNTER2 || spr==SPR_MON_HUNTER3 || spr==SPR_MON_LIGHT || spr==SPR_MON_LEECH){
        Monster model;memset(&model,0,sizeof(model));model.active=1;model.x=wx;model.y=wy;model.facing=facing;model.walkPhase=stride;
        MonsterSpriteSelect(spr,wx,wy,&model,&use,&mirror,&frame);
    }
    float light=SpriteShade(box.ty,.10f)*ArtMonsterLight(wx,wy)*lightK;
    light+=FlareLightAt(wx,wy)*.85f;
    if(light>1.2f)light=1.2f;
    DrawGroundShadow(box.centerX,box.baseY,w/3,box.ty);
    SpritePose pose={(float)g_timeSec+spr*.27f,stride,fminf(1,activity+EvolutionPoseSignal(spr)*.4f),mirror};
    DrawSpriteCreature(use,box.centerX-size/2,box.baseY-size,size,size,light,1,zTest,box.ty,tint,tintMix,&pose);
}
static void DrawBakedMonster(int spr,float wx,float wy,float halfH,float lightK,int zTest,float facing,float walkPhase){
    DrawCreatureActor(spr,wx,wy,halfH,lightK,zTest,facing,walkPhase,0,0,.6f);
}


/* ===== v2.4.6 **距离压迫感**（全部怪物共用的唯一入口）=====
   用户反馈：只有新怪有衰减，听声怪/追光怪还是老样子，整体不统一。
   所以抽成这个函数，所有怪物绘制都必须经过它。

   规则：离玩家越近越亮、越远越黑。
     ≤ NPROX_NEAR  → 全亮（它就在你面前）
     ≥ NPROX_FAR   → 只留 NPROX_FLOOR 的一线轮廓
     中间          → 幂曲线，近处慢降、远处急降

   为什么这样有压迫感：看不清才是最恐怖的。
   8 格外只剩黑影，你直到 4~5 格才确定那是什么 —— 那时候已经晚了。 */
/* v2.4.3: 距离曲线已统一到 src/art.c 的 ArtMonsterLight() */


static void DrawBeastSprite(Monster* m,unsigned int eyeColor,int baseSpr){
    if(!m->active)return;
    int sprite=baseSpr>0?baseSpr:(eyeColor==0xC8F0FF?SPR_MON_LIGHT:SPR_MON_HUNTER);
    float halfH=.86f,tintMix=0;unsigned int tint=0;
    if(bossLevel && m==Hunter0())halfH=1.45f;
    if(m->stun>0){tint=0xFFFFFF;tintMix=fminf(.75f,m->stun/MONSTER_STUN*.75f);}
    DrawCreatureActor(sprite,m->x,m->y,halfH,1,1,m->facing,m->walkPhase,tint,tintMix,m->stun>0?0:m->state==1?1:.35f);
}


static void DrawMonster2Sprite(void) {
    if (!Hunter1()->active) return;
    DrawBeastSprite(Hunter1(), 0x00ff0000, SPR_MON_HUNTER2);
}
/* 第三只听声鬼（120 秒后出现）。配色再暗一档，
   三只同屏时玩家能从轮廓上分出"又来了一只"。 */
static void DrawMonster3Sprite(void) {
    if (!Hunter2()->active) return;
    DrawBeastSprite(Hunter2(), 0x00c02050, SPR_MON_HUNTER3);
}
static void DrawMonsterSprite(void) { DrawBeastSprite(Hunter0(), 0x00ff4020u, SPR_MON_HUNTER); }

/* v1.8 偷电怪：体型 55%，暗色，只有被手电光锥照到才可见。
   "看不见但电量在掉" 就是它的压迫感来源，所以这里宁可画得几乎融进黑里。 */
static void DrawLeechSprite(int idx){
    if(idx<0 || idx>=LEECH_MAX)return;
    Leech* leech=&leeches[idx];if(!leech->active)return;
    float dx=leech->x-posX,dy=leech->y-posY,d2=dx*dx+dy*dy;if(d2>64)return;
    float len=sqrtf(fmaxf(.001f,d2));
    float beam=(dx*dirX+dy*dirY)/len;
    int lit=flashlightOn && battery>0 && beam>.85f && HasLOS(posX,posY,leech->x,leech->y);
    if(!lit && !leech->draining)return;
    DrawCreatureActor(SPR_MON_PARASITE,leech->x,leech->y,.47f,1,1,0,(float)g_timeSec*1.8f,0,0,leech->draining?1:.5f);
}

/* 2026-11-06：追光怪两只。DrawBeastSprite 本来就是参数化的，
   所以这里只需要各自传自己的实体。 */
static void DrawLightBeastSprite(void)  { DrawBeastSprite(&lightBeast,  0x00c8f0ffu, SPR_MON_LIGHT); }
static void DrawLightBeastSprite2(void) { DrawBeastSprite(&lightBeast2, 0x00c8f0ffu, SPR_MON_LIGHT); }

/* 跳脸虫的脸：预渲染成一张 128x128 的纹理，每帧只做采样 + 缩放。
   逐像素现画的话，800x600 实测 11ms/帧 —— QTE 是实时反应，
   掉帧就等于「玩家看不清提示」，所以必须预渲染。

   纹理格式：高字节 = 颜色索引（1..6），低 24 位 = 原始 RGB。
   避免逐像素做浮点乘加（脸几乎占满屏幕，这是热路径上的主要开销）。 */
#define JUMPER_TEX_SIZE 128
#define JMP_COL_EDGE  1      /* 外缘 */
#define JMP_COL_SKIN  2      /* 皮肤 */
#define JMP_COL_DEEP  3      /* 皮肤深色 */
#define JMP_COL_MOUTH 4      /* 口腔 */
#define JMP_COL_TOOTH 5      /* 牙 */
#define JMP_COL_EYE   6      /* 眼睛（单独处理亮度） */
#define JMP_SHADE_STEPS 64   /* 亮度档数 */

/* ---------------- v1.11：统一弹道精灵（荧光棒 / 玻璃瓶） ----------------
   重写原因：老实现是逐列比 zBuffer，投射物贴墙飞时列与列互相打架，
   看上去就是"时不时消失一下"。这里把规则收敛成三条：

     ① 遮挡**只判中心一列**（带 0.85 深度偏移）→ 要么整颗可见，要么整颗被挡
     ② 球体内部不再做深度测试 → 没有逐像素打架
     ③ 半径下限 3px、上限 90px → 贴脸不会缩成点，远处不会糊满屏
*/
/* v1.11：光球是真实光源。
   墙面按列的命中点算「离光球多远」，越近越亮并带暖色 ——
   这样扔出去的荧光棒会把周围照亮，而不是自己亮、环境不动。 */
extern Thrown thrown[MAX_THROWN];

static float FlareLightAt(float wx, float wy) {
    float best = 0.0f;
    for (int i = 0; i < MAX_THROWN; i++) {
        if (!thrown[i].active) continue;
        float dx = thrown[i].x - wx, dy = thrown[i].y - wy;
        float d2 = dx * dx + dy * dy;
        const float R = 4.6f;
        if (d2 > R * R) continue;
        float f = 1.0f - sqrtf(d2) / R;
        f = f * f * 0.95f;
        /* v3.3 **亮度随剩余寿命衰减** ——
           原来 25 秒内亮度恒定，然后「啪」一下灭掉，玩家没有任何预兆。
           现在最后 8 秒线性变暗到 25%，熄灭前会明显发暗，
           「该找下一个光源了」变成可感知的信息。 */
        float life = thrown[i].life;
        float dim = 1.0f;
        if (life < 8.0f) {
            dim = 0.25f + 0.75f * (life / 8.0f);
            if (dim < 0.25f) dim = 0.25f;
        }
        f *= dim;
        if (f > best) best = f;
    }
    return best;
}

/* World sprites and light effects use the same horizon as walls and floors. */

static void DrawFlareSprite(int idx) {
    if (!thrown[idx].active) return;
    /* v3.3：最后 8 秒精灵也跟着变暗，和环境亮度同步 */
    float a = 1.0f;
    if (thrown[idx].life < 8.0f) {
        a = 0.30f + 0.70f * (thrown[idx].life / 8.0f);
        if (a < 0.30f) a = 0.30f;
    }
    float size = 0.17f * (0.75f + 0.25f * a);
    DrawOrbSprite(thrown[idx].x, thrown[idx].y, thrown[idx].z,
                  size, 0x00fff8e0u, 0x00ff9050u, a);
}

/* ---------------- v1.19 投掷物建模 ----------------
   砖头 = 横放的矩形块；酸瓶 = 细长瓶身 + 瓶颈；烟雾弹 = 罐体；
   玻璃瓶沿用小球（它本来就是飞行的碎玻璃反光）。 */
/* ===== v3.3 玩家诱饵「玩偶」的模型 =====
   用户要求建模。设计意图：
     · 看得出是个人偶（有头有身子有腿），不是又一个道具罐
     · 未激活时整体暗哑 —— 一眼能看出「还没生效」
     · 激活后一只眼睛亮起 + 随叫声做缩放脉冲 —— 一眼能看出「它在工作」
   全部用 PutPixelBlend 现场画，沿用项目零外部资源的原则。 */
/* ===== v7 新怪物建模 =====
   风格延续听声怪 / 追光怪：
     · 同一套 ProjectSprite 投影 + zBuffer 遮挡
     · 同一套 DrawGroundShadow 地面阴影
     · 同一套 SpriteShade 距离衰减
     · 眼白高光 = 怪物的「可识别点」
   区别只在轮廓和配色，让玩家一眼能分出「这是哪一只」。

   三只怪各有一个视觉识别点：
     拾荒者  矮胖 + 金色；身上有赃款时整体变亮
     清道夫  高瘦 + 病绿；正在开柜门时眼白变红
     回声    极矮 + 冷白微光；随时间衰减变暗
*/
/* ===== v2.4.0 新怪物建模 —— 直接复用成功的烘焙精灵 =====
   用户明确评价：听声怪 / 追光怪的模型是最好的，新做的三个「长得都很奇怪」。
   所以放弃自己用椭圆拼身体的方案，改成**用同一套烘焙精灵 + 不同色调**：
     · 主体剪影、走路帧、朝向帧、阴影、距离光衰 —— 和那两只完全一致
     · 只在颜色上区分：拾荒者暗金 / 清道夫病绿 / 守门人暗蓝
   这样质感立刻对齐，而且不用新画任何精灵。 */


static void DrawMoneyBagModel(int idx) {


    if (MoneyBagAmount(idx) <= 0) return;
    float wx = MoneyBagX(idx), wy = MoneyBagY(idx);
    float lf = MoneyBagLife(idx);
    /* 快过期时闪烁，提醒玩家去捡 */
    float blink = 1.0f;
    if (lf < 8.0f) blink = 0.55f + 0.45f * (float)((int)(lf * 6.0f) % 2);
    DrawBakedProp(SPR_PROP_CRATE, wx, wy, 0.0f, 0.22f, 0.85f * blink);
}

/* ==================================================================
 * 2026-11-05 重做 + 打磨：**世界里放着的假人玩偶**
 * ------------------------------------------------------------------
 * 用户对照图：手持精灵（左下角）已经认可，地上这个原来还是
 * "圆头 + 方身体 + 中间深色方块 + 两根牙签腿"，读起来像纸箱。
 *
 * 第二轮反馈：「再打磨细节，比如两条手臂、身下/身体上那个小黑点、两条腿」。
 *
 * ==================================================================
 * 调研：C 里做程序化像素建模，能加细节的六种手段（本文件全用了）
 * ------------------------------------------------------------------
 * ① **有序织纹**（weave）：按 2×2 棋盘给每个像素乘 0.90 的暗系数。
 *    麻布的"糙"来自规则的小尺度明暗交替；比随机噪点干净，缩小时
 *    会自然混成均匀亮度，不会闪。
 *
 * ② **多段明暗**（banding）：把一个平面切成 5 段而不是 3 段。
 *    段数 = 圆柱感的来源；3 段看起来像贴纸，5 段才有体积。
 *
 * ③ **环境光遮蔽**（AO）：两个部件相接的那一行**额外压暗**。
 *    这是让「拼起来的几何」读成「一个整体」的关键一步 ——
 *    关节处不暗，各部件就像浮在空中。
 *
 * ④ **负空间**（negative space）：手臂和身体之间留 1px 背景缝。
 *    不靠颜色区分部件，靠**看得见后面**区分（铁律二）。
 *
 * ⑤ **缝线虚线**（dash）：`x += 2` 的短划线，比实线更像手缝。
 *    实线读作"印刷的边框"，虚线才读作"针脚"。
 *
 * ⑥ **边缘光**（rim light）：左上轮廓提亮 1px。
 *    暗场里它负责把剪影从背景里"剥"出来。
 * ================================================================ */

/* 带边界检查的绘制小工具（PutPixelBlend 本身不查边界） */


static void DrawExtraSprite(int idx) {
    const Extra* e = ExtraAt(idx);
    if (!e || !e->active) return;
    if (!SpritesReady()) return;
    int spr = SPR_MON_ECHO;
    if (e->kind == 0) spr = SPR_MON_SHADE;
    else if (e->kind == 1) spr = SPR_MON_SPIDER;
    float halfH = (e->kind == 0) ? 1.05f : (e->kind == 1 ? 0.42f : 0.95f);
    DrawBakedMonster(spr, e->x, e->y, halfH, 0.85f, 1, 0.0f, (float)g_timeSec * 2.0f);
}

static void DrawSlashBlade(float wx, float wy, float scale, float alpha) {
    if (alpha <= 0.02f) return;
    SpriteBox s = ProjectSprite(wx, wy, 0.0f, 0.62f);
    if (!s.ok) return;

    int cx = (s.x0 + s.x1) / 2;
    int occ = cx < 0 ? 0 : (cx >= renderW ? renderW - 1 : cx);
    if (s.ty * 0.90f >= zBuffer[occ]) return;

    int hh = (s.y1 - s.y0) / 2;
    if (hh < 3) hh = 3;
    hh = (int)(hh * scale);
    int bw = hh / 5;
    if (bw < 2) bw = 2;
    int ycen = (s.y0 + s.y1) / 2;

    for (int y = -hh; y <= hh; y++) {
        float u = (float)y / (float)hh;
        int w = (int)(bw * (1.0f - u * u));      /* 中间厚两头尖 */
        if (w < 1) continue;
        int py = ycen + y;
        if (py < 0 || py >= renderH) continue;
        for (int dx = -w; dx <= w; dx++) {
            int px = cx + dx;
            if (px < 0 || px >= renderW) continue;
            float k = 1.0f - (float)(dx < 0 ? -dx : dx) / (float)(w + 1);
            unsigned int col = (k > 0.66f) ? 0x00F0FFFFu
                             : (k > 0.33f) ? 0x0098E8FFu : 0x0040A8FFu;
            PutPixelBlend(px, py, col, alpha * (0.45f + 0.55f * k));
        }
    }
    /* 刃口：朝前的一侧加一条白线 */
    if (alpha > 0.5f) {
        for (int y = -hh; y <= hh; y++) {
            float u = (float)y / (float)hh;
            int w = (int)(bw * (1.0f - u * u));
            int px = cx + w, py = ycen + y;
            if (px >= 0 && px < renderW && py >= 0 && py < renderH)
                PutPixelBlend(px, py, 0x00FFFFFFu, 0.55f * alpha);
        }
    }
}

static void DrawSlashSprite(void) {
    if (!slashActive) return;
    /* 残影：沿飞行方向往回拖两层 */
    DrawSlashBlade(slashX - slashDX * 0.55f, slashY - slashDY * 0.55f, 0.80f, 0.35f);
    DrawSlashBlade(slashX - slashDX * 1.10f, slashY - slashDY * 1.10f, 0.62f, 0.18f);
    DrawSlashBlade(slashX, slashY, 1.0f, 1.0f);
}

static void DrawShotSprite(int idx) {
    if (!shot[idx].active) return;
    DrawShotModel(idx);
}

/* v1.18：烟幕团 —— 地面上一团不断翻涌的灰烟 */
/* v3.3 地面胶水：一滩青色黏液。
   和烟团的关键区别：烟是**压暗**（遮蔽），胶水是**提亮**（反光湿滑），
   所以这里用加法式的青绿高光，同时把底色往暗里压一点点做出对比。
   画法照抄烟团的椭圆 + 圆心衰减，但颜色和遮罩相反。 */
/* v3.3 地面胶水：**贴地的横截面**，不是立体的团块。
   用户反馈：「胶水地面贴图是立体的，我希望你做成横截面的」。
   原来的写法用 ProjectSprite 拿一个竖直 billboard，
   在透视下看起来像地上立着的一团东西。
   现在改成**世界空间网格采样**：在半径内撒一圈采样点，
   每点单独投影 —— 远的点在屏幕上更靠上，自然就变成被压扁的地面椭圆，
   这才是正确的「地面贴花」画法。 */

/* Clustered soft puffs use a cached silhouette and are clipped before blending. */
static void DrawSmokePuff(float x, float y, float height, float scale, float opacity, int seed) {
    enum { MASK = 32 };
    static unsigned char density[3][MASK][MASK];
    static int ready;
    if (!ready) {
        for (int layer=0;layer<3;layer++) for (int v=0;v<MASK;v++) for (int u=0;u<MASK;u++) {
            float dx=(u+.5f)*2/MASK-1, dy=(v+.5f)*2/MASK-1;
            float wiggle=.12f*sinf(dx*11+layer*2)*sinf(dy*9-layer);
            float edge=fmaxf(0,1-dx*dx-dy*dy+wiggle);
            density[layer][v][u]=(unsigned char)fminf(255,edge*edge*340);
        }
        ready=1;
    }
    SpriteBox box=ProjectSprite(x,y,height,scale);
    if (!box.ok || box.x1<=box.x0 || box.y1<=box.y0) return;
    int x0=box.x0<0?0:box.x0, x1=box.x1>renderW?renderW:box.x1;
    int y0=box.y0<0?0:box.y0, y1=box.y1>renderH?renderH:box.y1;
    unsigned tint=seed==0?0x343943:seed==1?0x252932:0x424751;
    for (int py=y0;py<y1;py++) {
        int v=(py-box.y0)*MASK/(box.y1-box.y0);
        for(int px=x0;px<x1;px++) {
            if (box.ty>=zBuffer[px]) continue;
            int u=(px-box.x0)*MASK/(box.x1-box.x0);
            int alpha=density[seed][v][u];
            if (alpha>7) PutPixelBlend(px,py,tint,opacity*alpha/255);
        }
    }
}

static void DrawSmokeCloud(void) {
    spriteLight=1;
    if (smokeDim < 1) return; /* Inside: use the same world shading as an unlit flashlight. */
    static const float offsets[][3]={
        {-1.24f,0,.00f},{.95f,.48f,.10f},{0,-1.2f,.05f},
        {.28f,.85f,.35f},{-.55f,-.50f,.47f},{0,.05f,.7f}
    };
    for (int i=0;i<MAX_SMOKE;i++) {
        if (!smokes[i].active) continue;
        float remaining=fminf(1,smokes[i].life/SMOKE_LIFE);
        float age=SMOKE_LIFE-smokes[i].life;
        float growth=.68f+.32f*fminf(1,age*2.5f);
        float opacity=.84f*fminf(1,remaining*3);
        for (int part=0;part<6;part++) {
            float x=smokes[i].x+growth*offsets[part][0]+.16f*sinf(age*1.9f+part);
            float y=smokes[i].y+growth*offsets[part][1]+.14f*cosf(age*1.6f+part);
            DrawSmokePuff(x,y,offsets[part][2]+.04f*age,growth*(part<3?1.18f:.92f),opacity,part%3);
        }
    }
}

static void DrawShardSprite(void) {
    spriteLight = 1.0f;      /* v2.1：金币是自发光的 */

    for (int i = 0; i < MAX_SHOTS * SHARD_COUNT; i++) {
        if (!shards_debris[i].active) continue;
        SpriteBox s = ProjectSprite(shards_debris[i].x, shards_debris[i].y, shards_debris[i].z / 10.0f, 0.05f);
        if (!s.ok) continue;

        int cx = (s.x0 + s.x1) / 2;
        int cy = (s.y0 + s.y1) / 2;
        float lifeRatio = shards_debris[i].life / MAX_SHARD_LIFE;
        float alpha = 0.7f * lifeRatio;
        if (alpha < 0.05f) continue;

        /* v2.1：金币按 tint 分色（玻璃 / 血 / 尘），尺寸随距离缩放 */
        unsigned int scol = 0x00d0e0ffu;
        if (shards_debris[i].tint == 1)      scol = 0x00c02818u;   /* 血 */
        else if (shards_debris[i].tint == 2) scol = 0x00c0a070u;   /* 尘 */
        int rad = (s.x1 - s.x0) >> 1;
        if (rad < 1) rad = 1;
        if (rad > 4) rad = 4;
        for (int dy = -rad; dy <= rad; dy++) {
            for (int dx = -rad; dx <= rad; dx++) {
                if (dx == 0 && dy == 0) continue;
                if (abs(dx) + abs(dy) > rad) continue;     /* 菱形 */
                int x = cx + dx, y = cy + dy;
                if (x < 0 || x >= renderW || y < 0 || y >= renderH) continue;
                if (s.ty >= zBuffer[x]) continue;
                PutPixelAdd(x, y, scol, alpha);
            }
        }
        /* 中心 */
        if (cx >= 0 && cx < renderW && cy >= 0 && cy < renderH && s.ty < zBuffer[cx])
            PutPixelAdd(cx, cy, 0x00ffffffu, alpha * 1.5f);
    }
}

/* v2.1：安全屋。原来画的是一朵粉色花 + 地上紫斑 —— 在一个恐怖游戏里
   像误入了别的游戏。改成"一根矮的暖色光柱 + 地面光斑"，和出口那根
   冷色光柱形成对照（暖 = 安全，冷 = 出口）。 */
static void DrawFlowerSprite(void) {
    if (!safeRoom.active) return;
    /* v2.2 修复（用户实测：站到安全区里，灯糊满视野）：
       ① 这盏灯只有半米高，halfH 从 0.50 降到 0.30
       ② 玩家已经在安全屋里（距离 < 1.3 格）就干脆不画 ——
          你已经站在里面了，不该被自己的营地挡住视线 */
    {
        float dx = posX - safeRoom.x, dy = posY - safeRoom.y;
        if (dx * dx + dy * dy < 1.69f) return;
    }
    DrawBakedProp(SPR_PROP_SAFE, safeRoom.x, safeRoom.y, 0.0f, 0.30f, 1.0f);
}

/* ===== v2.4.4 **出口重新设计 v2**（按 art_direction.h + art.c）=====
   v2.4.3 那版还是太平：一整块灰 + 一条黑缝，没有体积感。

   这一版加了四层细节，全部用 src/art.c 的仿 3D 原语：

   ① 斜角边框   ArtBevelK —— 左上受光、右下背光，1~2 像素的明暗差就出厚度
   ② 内凹门板   ArtRecessK —— 门框里再嵌一层，上壁暗、下壁亮，读作「凹进去」
   ③ 门缝透视   缝从上往下加宽（近大远小），两侧被光溢出的边逐级变亮
   ④ 光柱抖动   用 ArtDitherOn 做有序抖动，渐层是分级清晰的像素画，不是糊成一团

   地标仍然是「门上方那道竖向光柱」，只有出口有。
*/
static void DrawExitBeamSprite(void) {
    SpriteBox s = ProjectSprite(exitX, exitY, 0.0f, 1.0f);
    if (!s.ok || s.ty < 0.55f) return;
    int w=s.x1-s.x0,h=s.y1-s.y0;
    if(w<3 || h<3)return;
    int open=breakerOpen && ProtocolExitReady();
    int sprite=storyMaterialActive?SPR_MISSION_EXIT+2*(protocolRun.story-1)+open:
        protocolRun.story?SPR_STORY_PROP_BASE+STORY_HATCH*2+open:
        (open?SPR_PROP_EXIT_OPEN:SPR_PROP_EXIT);
    DrawBakedPropArt(sprite,exitX,exitY,0,1.0f,1);
    float light=SpriteShade(s.ty,0.12f)*ArtPropLight(exitX,exitY);
    float pulse=breakerOpen?.82f+.18f*sinf(gameTime*2.4f):1;

    /* ---- 光柱：地标。有序抖动让它像像素画的光，不是模糊渐变 ---- */
    if (open && !storyMaterialActive) {
        int cxp = (s.x0 + s.x1) / 2;
        int beamHalf = (w / 8 > 2) ? w / 8 : 2;
        int beamH = (int)((s.y1 - s.y0) * 1.15f);
        int beamTop = s.y0 - beamH;
        if (beamTop < 0) beamTop = 0;

        for (int x = cxp - beamHalf - 2; x <= cxp + beamHalf + 2; x++) {
            if (x < 0 || x >= renderW) continue;
            if (s.ty >= zBuffer[x]) continue;

            float dxp = (float)(x - cxp) / (float)(beamHalf + 3);
            float edge = 1.0f - dxp * dxp;            /* 中间最亮，两侧衰减 */
            if (edge <= 0.0f) continue;

            for (int y = beamTop; y < s.y0; y++) {
                if (y < 0 || y >= renderH) continue;
                /* t: 0 = 光柱顶端（最淡）  1 = 接地处（最亮） */
                float t = (float)(y - beamTop) / (float)(beamH + 1);
                float a = edge * t * t * pulse;

                /* 有序抖动：a 越小，落点的像素越稀疏 —— 像真正的光束 */
                if (!ArtDitherOn(a, x, y, 1.35f)) continue;

                float k = light * (0.45f + 0.55f * a);
                pixels[(size_t)y * renderW + x] = ArtScale(ART_BEAM, k);
            }
        }
    }
}

/* 所有 billboard 按距离从远到近画，近的盖住远的（画家算法）
   数组上界 MAX_SPRITES = 容器 + 投掷物 + 配电箱 + 两只怪 + 安全屋 + 出口光柱，
   写满时会被下面的断言拦下来，不会像以前那样悄悄越界。 */
/* v3.0 改造树「听觉」：把最近的噪音源标成地面一点，玩家能"听"到方向 */
/* v3.0：靠近可藏家具时给一圈冷色描边 —— 让玩家一眼看出"这个能躲" */
static void DrawHideSpotHighlight(void) {

    if (hideSpotNear < 0 || hideSpotNear >= hideSpotCount) return;
    HideSpot* h = &hideSpots[hideSpotNear];
    if (!h->active) return;
    /* v3.3 **删掉这里的 SPR_PROP_SAFE** ——
       用户截图：「隐藏点把原来那个灯的模型删掉，保留作者留的垃圾桶」。
       原来藏身点被画了两次：
         ① DrawHideSpotSprite  → SPR_PROP_TRASH（作者 v3.3 的垃圾桶）✅
         ② 这里                  → SPR_PROP_SAFE（我早期的柜子，顶上带灯）
       两个精灵叠在同一格，看起来就是「垃圾桶上面压了个带灯的柜子」。
       现在只保留作者的垃圾桶。靠近提示改由 HUD 的 PRESS E / HIDE 承担，
       不再额外画一层模型。 */
    (void)h;
}

/* ==================================================================
 * 2026-11-06：**诊断用** —— 把舱门投影到屏幕上的框报出来
 * ------------------------------------------------------------------
 * 用户看不到门的时候，需要区分三种情况：
 *   ① 投影出来是空的（不在视野 / 太近被丢）
 *   ② 投影正常但精灵包围盒无效（画了个空图）
 *   ③ 都正常但被 zBuffer 全遮住了
 * 这个函数一次回答前两种。只在测试里调用。
 * ================================================================ */
int DebugHatchScreenBox(int idx, float hx, float hy, int* x0, int* y0, int* x1, int* y1,
                        int* hasBBox, int* bw, int* bh) {
    (void)idx;
    SpriteBox s = ProjectSprite(hx, hy, 0.0f, 0.72f);
    *x0 = s.x0; *y0 = s.y0; *x1 = s.x1; *y1 = s.y1;
    Sprite* sp = SpriteGet(SPR_HATCH_0);
    if (sp) { *bw = sp->bx1 - sp->bx0 + 1; *bh = sp->by1 - sp->by0 + 1; }
    else { *bw = 0; *bh = 0; }
    *hasBBox = (sp != NULL && *bw > 0 && *bh > 0) ? 1 : 0;
    return s.ok;
}


static void DrawListedHatch(int hi) {
    if(RoomThemeIsCurrent(hi))return; /* Central device is the only visible activator. */
    /* 2026-11-06：舱门（两扇对开，4 帧） */
    if (hi >= 0 && hi < MAX_HATCHES && hatches[hi].active) {
        /* 帧号由开门进度决定；SEALED=0，OPEN/FIRED=3 */
        float o = HatchOpen01(hi);
        int f = (int)(o * 3.0f + 0.5f);
        if (f < 0) f = 0;
        if (f > 3) f = 3;
        int spr = SPR_HATCH_0 + f;
        DrawBakedPropArt(spr, hatches[hi].x, hatches[hi].y, 0.0f, 0.72f, 1.0f);

        /* ================================================================
         * 2026-11-06（第 8 轮）：**主题指示灯改由渲染器直接画**
         * ----------------------------------------------------------------
         * 原来灯是烘进精灵的三块洋红 key，靠 SpriteAccentSet 换色。
         * 两个问题：
         *   ① 传给 accent 的是 cell_meta 的**墙面混色权重**（12~34/255），
         *      那根本不是颜料，换上去只会变黑；
         *   ② 128 画布 + 双线性放大之后几乎没有纯洋红像素，
         *      判据匹配不上，灯就一直是洋红色。
         * 现在：精灵里不画灯，这里按投影框在门框顶部点三颗。
         * ================================================================ */
        {
            /* 门框顶部三颗指示灯。用与精灵同一套投影，所以会跟着透视缩。
               ProjectSprite 就是本文件的静态函数，直接调。 */
            SpriteBox sb = ProjectSprite(hatches[hi].x, hatches[hi].y, 0.0f, 0.72f);
            if (sb.ok) {
                int boxH = sb.height;
                int boxW = sb.width;
                if (boxH > 8 && boxW > 8) {
                    unsigned int lamp = HatchLampRGB(hatches[hi].themeId);
                    /* 灯的亮度跟手电走：暗场里也该有一点自发光感 */
                    float litK = SpriteShade(sb.ty, 0.12f);
                    if (litK < 0.45f) litK = 0.45f;   /* 指示灯自己发亮 */
                    int ly = sb.baseY - boxH + boxH / 12;
                    int lr = boxH / 50;  if (lr < 1) lr = 1;   /* 灯要小，别抢门 */
                    int lw = lr * 2 + 1;
                    for (int k = 0; k < 3; k++) {
                        int lx = sb.centerX - boxW / 2 + boxW * (3 + k * 4) / 12;
                        for (int dy = 0; dy < lw; dy++) {
                            for (int dx = 0; dx < lw; dx++) {
                                int px = lx + dx, py = ly + dy;
                                if (px < 0 || px >= renderW) continue;
                                if (py < 0 || py >= renderH) continue;
                                /* ================================================================
                                 * 2026-11-06（用户实测）：**指示灯原来没有深度测试**
                                 * ----------------------------------------------------------------
                                 * "特殊房间的装置出现 3 个点，全局都能看得到，穿墙都能看得到"。
                                 * 原因是这里直接调 PutPixelBlend —— 它只做混合，不查 zBuffer。
                                 * 精灵本身走了 DrawSpriteSubZ（有 z），
                                 * 唯独这三颗灯绕过了深度。
                                 * ================================================================ */
                                if (sb.ty >= zBuffer[px]) continue;
                                PutPixelBlend(px, py, lamp, litK);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void DrawListedRoomProp(int ri) {
    /* v2.4-rc P3：房间家具（分层伪立体） */
    if (ri >= 0 && ri < MAX_ROOM_PROPS && roomProps[ri].active) {
        /* ================================================================
         * 2026-11-06（**精灵数过多导致掉帧**）
         * ----------------------------------------------------------------
         * 用户实测：「货架算三个精灵，箱子算两个精灵，精灵过多，
         * 每次路过都掉帧。」
         *
         * 分层伪立体的视觉收益，远小于它带来的绘制开销 ——
         * 尤其 36x36 之后一层有更多家具。
         * 现在**每个家具只画 1 个精灵**，改由 searchLeft 承担内容量：
         *   货架 3 次 / 木箱堆 2 次 / 控制台 2 次
         * ================================================================ */
        RoomProp* prop=&roomProps[ri];
        if(RoomPropIsLandmark(prop->kind)) {
            int theme=prop->kind-ROOMPROP_LANDMARK_FIRST,stage=RoomThemeVisualStage();
            float height=prop->height;
            /* Only the mechanical heart moves, and only its world sprite. */
            if(theme==6 && !(RoomThemeEventKind()==6 && RoomThemeEventRemaining()>0))
                height*=1.0f+.012f*sinf((float)g_timeSec*1.8f);
            int sprite=SPR_ROOM_V2_BASE+theme*3+stage;
            if(stage==1) {
                int phase=(int)(fmod(g_timeSec*4.0+(double)theme,3.0));
                if(phase>0)sprite=SPR_ROOM_MOTION_BASE+theme*2+phase-1;
            }
            DrawBakedPropArt(sprite,prop->x,prop->y,0,height,
                            stage==1?1.08f+.06f*sinf((float)g_timeSec*2.0f+theme):1.0f);
            return;
        }
        if(RoomPropIsControl(prop->kind)) {
            int offset=prop->kind-ROOMPROP_CONTROL_FIRST;
            if(offset/2==HATCH_BRICK)return;
            int sprite=SPR_ROOM_V2_CONTROL_BASE+offset;
            if(prop->searched && !roomTheme.spent) {
                int phase=sinf((float)g_timeSec*2.4f+offset)>0;
                sprite=SPR_ROOM_CONTROL_READY_BASE+phase*14+offset;
            }
            DrawBakedPropArt(sprite,prop->x,prop->y,0,prop->height,
                            roomTheme.spent?.65f:prop->searched?1.08f+RoomThemeControlPulse()*.18f:.8f);
            return;
        }
        int oneSpr = roomProps[ri].kind>=3 && roomProps[ri].kind<=9?SPR_ROOM_BOILER+roomProps[ri].kind-3:SPR_PROP_SHELF;
        if (roomProps[ri].kind == ROOMPROP_CONSOLE)     oneSpr = SPR_PROP_CONSOLE;
        else if (roomProps[ri].kind == ROOMPROP_CRATES) oneSpr = SPR_PROP_CRATE;
        DrawBakedPropArt(oneSpr, roomProps[ri].x, roomProps[ri].y, 0.0f, roomProps[ri].height, 1.0f);

    }
}

static void SortSpriteEntries(SpriteDrawEntry* list, int n) {
    /* 插入排序：远的先画 */
    for (int i = 1; i < n; i++) {
        int j = i - 1;
        float d2 = list[i].d2;
        int kind = list[i].kind, idx = list[i].idx;
        while (j >= 0 && list[j].d2 < d2) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1].d2 = d2;
        list[j + 1].kind = kind;
        list[j + 1].idx = idx;
    }
}

static void DrawSpriteEntries(const SpriteDrawEntry* list, int n) {
    for (int i = 0; i < n; i++) {
        switch (list[i].kind) {
            case 0: DrawCrateSprite(list[i].idx); break;
            case 1: DrawBreakerSprite(); break;
            case 2: DrawMonsterSprite(); break;
            case 13: DrawMonster2Sprite(); break;   /* v2.4.12 第二只听声怪 */
            case 14: DrawMonster3Sprite(); break;   /* 第三只听声鬼 */
            case 15: DrawMimicSprite(list[i].idx); break;   /* 2026-11-05 宝箱怪 */
        case 9: DrawLeechSprite(list[i].idx); break;
            case 5:
                if (list[i].idx == 1) DrawLightBeastSprite2();
                else                  DrawLightBeastSprite();
                break;
            case 6: DrawFlareSprite(list[i].idx); break;
            case 8: DrawShotSprite(list[i].idx); break;
            case 10: DrawSlashSprite(); break;
            case 12: DrawHideSpotSprite(list[i].idx); break;   /* v3.0 藏身点 */
        case 19: {   /* 2026-11-06：缚丝蛛 */

            DrawBakedPropArt(SPR_PROP_SPIDER, SpiderX(), SpiderY(), 0.0f, 0.30f, 1.0f);
            break;
        }
        case 18: {   /* 2026-11-06：相位信标（地面模型） */

            /* 半身高 0.34 -> 0.50：第一版太小，暗场里像块石头 */
            DrawBakedPropArt(SPR_PROP_PHASE, phaseX, phaseY, 0.0f, 0.50f, 1.25f);
            break;
        }
        case 21: {
            float x,y,life=TacticalJammerEffect(&x,&y);
            if (life>0) {
                DrawBakedPropArt(SPR_JAMMER_BURST,x,y,.09f,.28f+(1-life)*.88f,.45f+life*1.55f);
            }
            break;
        }
        case 20: {
            float x,y;
            int id=list[i].idx;
            int state=id==3?ProtocolAbilityMarker(&x,&y):ProtocolProp(id,&x,&y);
            if (state) {
                float light=state==2?1.6f:1.0f;
                if (id==PROTOCOL_TRACE) {
                    float pulse=1.1f+.22f*sinf((float)g_timeSec*(state==1?7.f:4.f));
                    DrawBakedPropArt(state==1?SPR_ABILITY_FIELD:SPR_ABILITY_DOUBLE,x,y,0,
                                     state==1?.55f:.73f,pulse);
                }
                else if (protocolRun.flags&PROTOCOL_MISSIONS) {
                    int stage=2*(protocolRun.story-1);
                    int sprite=id==PROTOCOL_TERMINAL?SPR_MISSION_TERMINAL+stage:
                        id==PROTOCOL_RELAY?SPR_MISSION_RECORD+stage:SPR_MISSION_COIL+stage;
                    float height=id==PROTOCOL_TERMINAL?.66f:protocolRun.story==3?.93f:.74f;
                    DrawBakedPropArt(sprite+state-1,x,y,0,height,light);
                }
                else {
                    const int models[PROTOCOL_DEVICE_COUNT]={
                        STORY_RADIO, STORY_CAPACITOR, STORY_CORE, STORY_RADIO, STORY_ROUTE, STORY_ROUTE
                    };
                    float height=id==PROTOCOL_RELAY?.45f:.65f;
                    DrawBakedPropArt(SPR_STORY_PROP_BASE+models[id]*2+state-1,x,y,0,height,light);
                }
            }
            break;
        }
        case 17: DrawListedHatch(list[i].idx); break;
        case 16: DrawListedRoomProp(list[i].idx); break;
            case 11: DrawExtraSprite(list[i].idx); break;
            case 7: DrawFlowerSprite(); break;
            default: DrawExitBeamSprite(); break;
        }
    }
}

static void DrawSpriteOverlays(void) {
    /* v1.18：烟幕团（不参与深度排序，直接在最后叠加） */
    DrawHideSpotHighlight();   /* v3.0 藏身点靠近提示 */
    DrawSmokeCloud();
    DrawGluePuddle();   /* v3.3 地面胶水 */
    DrawEchoRadarPulse();
    /* v3.3：诱饵玩偶 */
    {

        for (int di = 0; di < MAX_DECOY; di++)
            if (decoysArr[di].active) DrawDecoyModel(di);
    }
    /* v2.3.5：地上的钱袋 */
    for (int mb = 0; mb < 3; mb++) DrawMoneyBagModel(mb);
    /* v2.3.5：三只新怪 + 回响（v2.3.3 误删过一次，导致全部隐形） */
    DrawNewMonsters();

    DrawShardSprite();
}

void RenderSprites(void) {
    SpriteDrawEntry list[MAX_SPRITES];
    int n = 0;
    n = CollectSpriteScenery(list, n);
    n = CollectSpriteActorsAndMarkers(list, n);
    n = CollectSpriteEffectsAndExit(list, n);
    SortSpriteEntries(list, n);
    DrawSpriteEntries(list, n);
    DrawSpriteOverlays();
}

/* ------------------------------------------------------------------ */
/* 并行渲染：画面切成横带，主线程 + 工作线程一起画                       */
/* ------------------------------------------------------------------ */

/* 下面这几个是**两个平台都要用**的共享定义：
   查表（shadeLut）和三个分带绘制函数的前置声明。
   它们必须留在 #ifdef 之外，否则网页分支看不到。 */
#define SHADE_LUT_MAX 1024
static int shadeLut[SHADE_LUT_MAX];
static void RenderFloorBand(int y0, int y1);
static void RenderWallBand(int x0, int x1);
static void RenderPostBand(int y0, int y1);

#ifdef CANITY_WEB
/* ==================================================================
 * 2026-11-06（R4）：**网页端单线程渲染**
 * ------------------------------------------------------------------
 * WebAssembly 默认不带 pthread，而且 worker 池用的是 Win32 事件对象。
 * 这里给出同名的单线程实现：三个相位依次在主线程序列执行。
 * 逻辑与分带函数完全复用（RenderFloorBand/WallBand/PostBand），
 * 只是不分片 —— 画面结果逐像素相同，只是慢一些。
 * ================================================================== */
void InitWorkers(void) { /* PLATFORM_NOOP: The browser renderer runs serially and owns no native worker threads. */ }
void ShutdownWorkers(void) { /* PLATFORM_NOOP: The browser renderer runs serially and owns no native worker threads. */ }
void RunPhase(int phase) {
    if (phase == 1) { RenderFloorBand(0, renderH); return; }
    if (phase == 2) { RenderWallBand(0, renderW); return; }
    RenderPostBand(0, renderH);
}
#else
#define MAX_PARTS 9   /* 1 主线程 + 最多 8 个工作线程 */
typedef struct {
    HANDLE hThread;
    HANDLE hStart;   /* 自动复位：主线程派活 */
    HANDLE hDone;    /* 自动复位：工作线程完工 */
    int y0, y1;      /* 地板 / 后处理的行范围 */
    int x0, x1;      /* 墙体的列范围 */
    int phase;       /* 1=地板 2=墙体 3=后处理 */
} Worker;

static Worker g_workers[MAX_PARTS];
static int g_workerCount = 0;   /* 不含主线程 */
static int g_parts = 1;
static volatile LONG g_stop = 0;

static DWORD WINAPI WorkerMain(LPVOID param) {
    Worker* w = (Worker*)param;
    for (;;) {
        if (WaitForSingleObject(w->hStart, INFINITE) != WAIT_OBJECT_0) break;
        if (g_stop) break;
        if (w->phase == 1) RenderFloorBand(w->y0, w->y1);
        else if (w->phase == 2) RenderWallBand(w->x0, w->x1);
        else if (w->phase == 3) RenderPostBand(w->y0, w->y1);
        SetEvent(w->hDone);
    }
    return 0;
}

void InitWorkers(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int want = (int)si.dwNumberOfProcessors - 1;
    if (want > MAX_PARTS - 1) want = MAX_PARTS - 1;
    if (want < 0) want = 0;

    g_workerCount = 0;
    g_stop = 0;   /* 允许重复初始化（例如重启线程池） */
    for (int i = 0; i < want; i++) {
        Worker* w = &g_workers[i];
        w->hStart = CreateEvent(NULL, FALSE, FALSE, NULL);
        w->hDone = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!w->hStart || !w->hDone) break;
        w->phase = 0;
        w->hThread = CreateThread(NULL, 0, WorkerMain, w, 0, NULL);
        if (!w->hThread) break;
        g_workerCount++;
    }
    g_parts = g_workerCount + 1;
}

void ShutdownWorkers(void) {
    g_stop = 1;
    for (int i = 0; i < g_workerCount; i++) SetEvent(g_workers[i].hStart);
    for (int i = 0; i < g_workerCount; i++) {
        WaitForSingleObject(g_workers[i].hThread, 1000);
        CloseHandle(g_workers[i].hThread);
        CloseHandle(g_workers[i].hStart);
        CloseHandle(g_workers[i].hDone);
    }
    g_workerCount = 0;
    g_parts = 1;
}

static void SplitRange(int lo, int hi, int part, int parts, int* outLo, int* outHi) {
    int n = hi - lo;
    if (n < 0) n = 0;
    *outLo = lo + n * part / parts;
    *outHi = lo + n * (part + 1) / parts;
}

/* 派活并等所有分片画完；worker 数可以是 0（单线程退化） */
/* 一旦某次等 worker 超时，就永久降级成单线程渲染 */
static int g_workersBroken = 0;

void RunPhase(int phase) {
    int y0, y1;
    if (g_workersBroken) {          /* 不用线程了：整帧主线程画，宁可掉帧不崩 */
        if (phase == 1) { RenderFloorBand(0, renderH); return; }
        if (phase == 2) { RenderWallBand(0, renderW); return; }
        RenderPostBand(0, renderH);
        return;
    }
    for (int i = 0; i < g_workerCount; i++) {
        /* v2.0：三个相位统一按 (workerCount+1) 等分，主线程吃最后一份。
           原来 phase 1 让主线程独画上半屏、8 个 worker 只分下半屏 ——
           实际加速比只有 ~2× 而不是 9×。 */
        if (phase == 1) { SplitRange(0, renderH, i, g_parts, &g_workers[i].y0, &g_workers[i].y1); }
        else if (phase == 2) { SplitRange(0, renderW, i, g_parts, &g_workers[i].x0, &g_workers[i].x1); }
        else { SplitRange(0, renderH, i, g_parts, &g_workers[i].y0, &g_workers[i].y1); }
        g_workers[i].phase = phase;
        SetEvent(g_workers[i].hStart);
    }

    int p = g_workerCount;
    if (phase == 1) {
        /* 主线程吃第 g_workerCount 份（最后一段），和 worker 不重叠 */
        int a0, a1;
        SplitRange(0, renderH, g_workerCount, g_parts, &a0, &a1);
        RenderFloorBand(a0, a1);
    }
    else if (phase == 2) { int x0, x1; SplitRange(0, renderW, p, g_parts, &x0, &x1); RenderWallBand(x0, x1); }
    else { SplitRange(0, renderH, p, g_parts, &y0, &y1); RenderPostBand(y0, y1); }

    for (int i = 0; i < g_workerCount; i++) {
        /* v2.7：超时绝不能再往下走 —— worker 还在往 pixels/zBuffer 写，
           下一相位（甚至 InitPixels 重建 DIB）会跟它并发写同一块缓冲。 */
        if (WaitForSingleObject(g_workers[i].hDone, 2000) != WAIT_OBJECT_0) {
            g_workersBroken = 1;
            break;
        }
    }
    if (g_workersBroken) {
        /* 兜底：这一帧剩下的部分全部在主线程补完 */
        if (phase == 1) RenderFloorBand(0, renderH);
        else if (phase == 2) RenderWallBand(0, renderW);
        else RenderPostBand(0, renderH);
    }
}
#endif /* CANITY_WEB */


/* 亮度 → gamma 校正后的亮度，查表代替逐像素 powf。
   索引是「亮度 × 256」，值域 0..1023（对应亮度 0..4.0）。

   曲线说明：直接用 pow(v, 1/g) 会踩一个坑 ——
   v > 1（过曝区，比如手电近处的地板）时，1/g < 1 会把值「压回来」，
   于是 gamma 调大反而让亮的地方变暗，和「数值越大越亮」的预期相反。

   修复方式：**只动 v > 1 那一段**，v <= 1 保持原曲线不变：
     - v <= 1：out = pow(v, 1/g) × 256      （与原实现完全一致）
     - v >  1：原样输出（过曝部分不再被压暗）
   这样既修好了过曝区被压暗的问题，又不会顺带改变墙体的既有观感 ——
   墙体亮度 lit 通常在 0.1~1.5 之间，那一段曲线必须原封不动。 */
#define SHADE_ONE 256      /* 亮度 1.0 对应的索引 */

void BuildShadeTables(void) {
    float inv = 1.0f / gammaValue;
    for (int i = 0; i < SHADE_LUT_MAX; i++) {
        int o;
        if (i <= SHADE_ONE) {
            float v = (float)i / 256.0f;
            float g = powf(v, inv);
            o = (int)(g * 256.0f + 0.5f);
        } else {
            o = i;   /* 过曝区原样保留 */
        }
        if (o < 0) o = 0;
        if (o > SHADE_LUT_MAX - 1) o = SHADE_LUT_MAX - 1;
        shadeLut[i] = o;
    }
}

static unsigned int LerpSurfaceColor(unsigned int a,unsigned int b,unsigned int weight) {
    unsigned int inverse=256-weight;
    unsigned int rb=(((a&0x00ff00ffu)*inverse+(b&0x00ff00ffu)*weight)>>8)&0x00ff00ffu;
    unsigned int g=(((a&0x0000ff00u)*inverse+(b&0x0000ff00u)*weight)>>8)&0x0000ff00u;
    return rb|g;
}

static unsigned int SampleSmoothFloor(int material,float wx,float wy,int level) {
    float u=wx*TEX_WIDTH,v=wy*TEX_HEIGHT;
    int ix=(int)floorf(u),iy=(int)floorf(v);
    unsigned int fu=(unsigned int)((u-ix)*256),fv=(unsigned int)((v-iy)*256);
    if(fu>255)fu=255;
    if(fv>255)fv=255;
    int x0=ix&(TEX_WIDTH-1),x1=(ix+1)&(TEX_WIDTH-1);
    int y0=iy&(TEX_HEIGHT-1),y1=(iy+1)&(TEX_HEIGHT-1);
    unsigned int a=SurfaceTexel(material,1,x0,y0,level);
    unsigned int b=SurfaceTexel(material,1,x1,y0,level);
    unsigned int c=SurfaceTexel(material,1,x0,y1,level);
    unsigned int d=SurfaceTexel(material,1,x1,y1,level);
    return LerpSurfaceColor(LerpSurfaceColor(a,b,fu),LerpSurfaceColor(c,d,fu),fv);
}

/* 地板：棋盘只有两种底色，亮度查表；出口发光只在出口所在格判断 */
/* Thin world-space corner brackets replace flat coloured safety carpets. */
static unsigned SafetyFloorMarker(unsigned color,float fx,float fy,float wx,float wy,int open) {
    float ax=fabsf(fx-wx),ay=fabsf(fy-wy),edge=fmaxf(ax,ay),inner=fminf(ax,ay);
    if(edge>=.76f && edge<=.83f && inner>.43f)
        return open?0xA8BEC7u:0xAA8560u;
    return color;
}
#include "domain/render/legacy/render_246_floor.inc"
static void RenderFloorBand(int y0, int y1) {
    if(!pitchLookEnabled) { RenderFloorBandLegacy246(y0,y1); return; }
    int lightOnF = (flashlightOn && battery > 0.0f && smokeDim >= 1);
    /* Fade in spatial filtering while looking down, instead of snapping between
       2-texel blocks as the horizon moves. OFF/level retains the original texture. */
    unsigned int floorFilter=pitchLookEnabled ? (unsigned int)(fmaxf(0,fminf(1,-pitchRatio))*256) : 0;

    /* v1.12：y-shear 之后地板与天花板**不再是镜像关系**（镜像只在 horizon==0 时成立），
       所以这里改成单次全屏通道：每一行按 y 相对地平线判上下，分别采样地板/天花板。
       之前那套"地板行 + 镜像天花板"在抬头时会留下一条黑带，低头时上下颠倒。 */
    for (int y = y0; y < y1; y++) {
        unsigned int* row = pixels + (size_t)y * renderW;
        /* Inverse of RenderProjectHeight: a floor/wall seam has exactly the
           same forward depth. Do not rotate the ray again after y-shear. */
        float ry = RenderProjectionRowSlope(y);
        int isCeil = (ry > 0.0f);
        float t;
        if (isCeil) {
            float den = ry;
            if (den < 0.004f) den = 0.004f;
            t = 0.5f / den;
        } else {
            float den = -ry;
            if (den < 0.004f) den = 0.004f;
            t = 0.5f / den;
        }
        if (t > 60.0f) t = 60.0f;
        /* Forward and lateral ray coefficients retain the horizontal camera. */
        float stepX = t * planeX * 2.0f / renderW;
        float stepY = t * planeY * 2.0f / renderW;
        float fx = posX + t * dirX - t * planeX;
        float fy = posY + t * dirY - t * planeY;
        float rowDistance = t;

        /* 亮度：环境光 + 手电的平方衰减 */
        float ambF = 1.0f / (1.0f + rowDistance * (lightOnF ? 0.75f : 0.55f));
        float reachF = 1.0f - rowDistance / 5.5f;
        if (reachF < 0.0f) reachF = 0.0f;
        reachF *= reachF;

        float shadow = (ambF * (lightOnF?.28f:.055f) + (lightOnF ? reachF * 0.95f : 0.0f)) * smokeDim;
        int shadowI = (int)(shadow * 256.0f);
        if (shadowI < 0) shadowI = 0;
        if (shadowI > 1023) shadowI = 1023;

        /* 距离雾（天花板减半：天花板本来就不该被照亮太多） */
        float fogAmt = rowDistance * 0.075f;
        if (fogAmt > 0.8f) fogAmt = 0.8f;
        if (!lightOnF) fogAmt *= 0.04f;
        if (isCeil) fogAmt *= 0.5f;
        unsigned int fogR = (unsigned int)(fogAmt * (float)themeFogR);
        unsigned int fogG = (unsigned int)(fogAmt * (float)themeFogG);
        unsigned int fogB = (unsigned int)(fogAmt * (float)themeFogB);

        for (int x = 0; x < renderW; x++) {
            int cX = (int)fx, cY = (int)fy;
            int tx2 = ((int)((fx - cX) * (float)TEX_WIDTH)) & (TEX_WIDTH - 1);
            int ty2 = ((int)((fy - cY) * (float)TEX_HEIGHT)) & (TEX_HEIGHT - 1);
            tx2 &= ~1; ty2 &= ~1;

            int material=SceneMaterialAt(cX,cY);
            unsigned int color;
            if (!isCeil) {
                int k8 = (shadowI * floorGainI[x]) >> 8;
                if (k8 < 0) k8 = 0;
                else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
                k8 = shadeLut[k8];
                int lvl = k8 >> PREMUL_K8_SHIFT;
                color = SurfaceTexel(material,1,tx2,ty2,lvl);
                if(floorFilter)
                    color=LerpSurfaceColor(color,SampleSmoothFloor(material,fx,fy,lvl),floorFilter);

                if(safeRoom.active)color=SafetyFloorMarker(color,fx,fy,safeRoom.x,safeRoom.y,1);
                if (!storyMaterialActive) color=SafetyFloorMarker(color,fx,fy,exitX,exitY,breakerOpen);

                /* 雾 + 光球的暖光（简化：只按距离加暖色） */
                if (fogR | fogG | fogB) {
                    unsigned int cr = (color >> 16) & 0xFF, cg = (color >> 8) & 0xFF, cb = color & 0xFF;
                    cr += fogR; if (cr > 255) cr = 255;
                    cg += fogG; if (cg > 255) cg = 255;
                    cb += fogB; if (cb > 255) cb = 255;
                    color = (cr << 16) | (cg << 8) | cb;
                }
            } else {
                int cl = ceilTab[x] >> PREMUL_K8_SHIFT;
                if (cl < 0) cl = 0;
                else if (cl > PREMUL_LEVELS - 1) cl = PREMUL_LEVELS - 1;
                color = SurfaceTexel(material,2,tx2,ty2,cl);
            }

            row[x] = color;
            fx += stepX;
            fy += stepY;
        }
    }
}

/* 墙体：DDA 求交后整列查表，内层只有一次乘法和一次写 */
typedef struct {
    float rayDirX;
    float rayDirY;
    int mapX;
    int mapY;
    int side;
    float perpWallDist;
    int stepX, stepY;
} WallRay;

#include "domain/render/legacy/render_246_ray.inc"
static WallRay CastWallRay(int x) {
    if(!pitchLookEnabled) { return CastWallRayLegacy246(x); }
    float cameraX = 2.0f * x / (float)renderW - 1.0f;
    /* Geometry stays static in BOTH look modes. Sanity effects must not
       bend walls independently of the floor, props or collision grid. */
    float rayDirX = dirX + planeX * cameraX;
    float rayDirY = dirY + planeY * cameraX;

    int mapX = (int)posX, mapY = (int)posY;
    float deltaDistX = (rayDirX == 0) ? 1e30f : fabsf(1.0f / rayDirX);
    float deltaDistY = (rayDirY == 0) ? 1e30f : fabsf(1.0f / rayDirY);
    float sideDistX, sideDistY;
    int stepX, stepY, hit = 0, side = 0;

    if (rayDirX < 0) { stepX = -1; sideDistX = (posX - mapX) * deltaDistX; }
    else { stepX = 1; sideDistX = (mapX + 1.0f - posX) * deltaDistX; }
    if (rayDirY < 0) { stepY = -1; sideDistY = (posY - mapY) * deltaDistY; }
    else { stepY = 1; sideDistY = (mapY + 1.0f - posY) * deltaDistY; }

    while (hit == 0) {
        if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
        else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
        if (mapX < 0 || mapX >= MAP_WIDTH || mapY < 0 || mapY >= MAP_HEIGHT) break;
        if (worldMap[mapY][mapX] > 0) hit = 1;
    }

    float perpWallDist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
    if (perpWallDist < 0.1f) perpWallDist = 0.1f;
    zBuffer[x] = perpWallDist;
    WallRay result = { rayDirX, rayDirY, mapX, mapY, side, perpWallDist, stepX, stepY };
    return result;
}

#include "domain/render/legacy/render_246_wall.inc"
static void RenderWallSurface(int x, const WallRay* ray) {
    if(!pitchLookEnabled) { RenderWallSurfaceLegacy246(x,renderH/2,ray); return; }
    float rayDirX = ray->rayDirX;
    float rayDirY = ray->rayDirY;
    int mapX = ray->mapX;
    int mapY = ray->mapY;
    int architectureMaterial=-1;
    if(!storyMaterialActive && mapX>=0 && mapY>=0 && mapX<MAP_WIDTH && mapY<MAP_HEIGHT &&
       (cellMeta[mapY][mapX].flags&(CELLFLAG_ROOM_V2|CELLFLAG_ROOMWALL)))
        architectureMaterial=SceneMaterialAt(mapX,mapY);
    int side = ray->side;
    float perpWallDist = ray->perpWallDist;
    /* Both endpoints share forward depth. Pitch cannot turn one of them
       into a tiny/negative denominator at a nearby wall or corner. */
    float unit = RenderProjectionScale(perpWallDist);
    int yBottom = (int)RenderProjectHeight(0.0f, unit);
    int yTop = (int)RenderProjectHeight(1.0f, unit);
    if (yBottom <= yTop) return;
    int startF = yTop;
    int drawStart = yTop < 0 ? 0 : yTop;
    int drawEnd = yBottom > renderH ? renderH : yBottom;
    if (drawEnd <= drawStart) return;

    float wallX = (side == 0) ? (posY + perpWallDist * rayDirY) : (posX + perpWallDist * rayDirX);
    wallX -= floorf(wallX);
    int texX = (int)(wallX * (float)TEX_WIDTH);
    if (texX < 0) texX = 0;
    if (texX > TEX_WIDTH - 1) texX = TEX_WIDTH - 1;
    if ((side == 0 && rayDirX > 0) || (side == 1 && rayDirY < 0)) texX = TEX_WIDTH - texX - 1;

    float span = (float)(yBottom - yTop);
    if (span < 1.0f) span = 1.0f;
    float step = (float)TEX_HEIGHT / span;
    /* v2.0：每像素吃掉的 texel 数 → mip 级。step>1.5 说明在缩小，退到低一级 */
    int mipLod = 0;
    {
        float st = step;
        while (st > 1.5f && mipLod < MIP_LEVELS - 1) { st *= 0.5f; mipLod++; }
    }
    const unsigned int* mipSrc = mipChain[mipLod];
    int mipW = TEX_WIDTH >> mipLod;
    int mipMaskY = (TEX_HEIGHT >> mipLod) - 1;
    int mipShift = mipLod;
    float texPos = (float)(drawStart - startF) * step;

    /* v1.9 光照：
       ① 距离衰减改成 1/(1+d*k) 曲线 —— 近处亮、远处平滑收黑，没有硬断崖
       ② 距离雾：远处叠加一点冷色（青蓝），空间感立刻出来
       ③ 手电光斑用 smoothstep 软边，不再是硬切一刀 */
    /* v1.10：手电削弱。
       原来开灯只有 1/(1+d*0.22) 的衰减 —— 10 格外还亮着，等于整张地图都看得见。
       现在拆成两部分：
         环境光 amb：永远很暗（开灯 0.42 倍，关灯更暗）
         手电项   ：有效距离 8 格，按 (1-d/8)² 衰减，超出即 0
       所以光照范围由"手电照到哪"决定，不再是"整条走廊"。 */
    int lightOnW = (flashlightOn && battery > 0.0f && smokeDim >= 1);
    float amb = 1.0f / (1.0f + perpWallDist * (lightOnW ? 0.70f : 0.50f));
    /* v1.12：手电换成和荧光棒同一套光效 ——
       锥形（横向 flashTab）× 平方距离衰减，再叠一层暖色加色。
       原来只有"乘法提亮"，画面偏冷、也不像有个灯泡在照。 */
    float reach = 1.0f - perpWallDist / (9.0f*BenchLightRangeMul());
    if (reach < 0.0f) reach = 0.0f;
    reach *= reach;
    float flash = flashTab[x];
    float torch = lightOnW ? (flash * reach * 0.62f) : 0.0f;

    float lit = (amb * (lightOnW?.26f:.045f) + torch) * smokeDim;
    if (side == 1) lit *= 0.62f;

    float cone = flash - 0.35f;
    if (cone < 0.0f) cone = 0.0f;
    cone = cone * cone * (3.0f - 2.0f * cone > 1.0f ? 1.0f : 3.0f - 2.0f * cone);   /* smoothstep */

    /* 距离雾（冷色，加法叠加，零额外采样） */
    float fog = perpWallDist * 0.085f;
    if (fog > 0.85f) fog = 0.85f;
    if (!lightOnW) fog *= 0.04f;    /* 留一点，纯粹压黑会没有纵深 */

    /* 光斑也受有效距离约束：远处不许再加亮 */
    /* 光球照明：命中点离任一荧光棒越近越亮（暖色） */
    float hx = posX + rayDirX * perpWallDist;
    float hy = posY + rayDirY * perpWallDist;
    float fl = FlareLightAt(hx, hy);
    lit += fl;
    if (lit > 1.35f) lit = 1.35f;

    /* v1.12：手电改**白光**（略偏冷），和荧光棒的暖橙拉开差别 */
    int addR = (int)((cone * reach * (float)(themeLitR + 24) * 0.42f + fog * (float)themeAmbR
                      + fl * 210.0f + torch * (float)themeLitR) * smokeDim);
    int addG = (int)((cone * reach * (float)(themeLitG + 24) * 0.42f + fog * (float)themeAmbG
                      + fl * 150.0f + torch * (float)themeLitG) * smokeDim);
    int addB = (int)((cone * reach * (float)(themeLitB + 24) * 0.42f + fog * (float)themeAmbB
                      + fl * 78.0f  + torch * (float)themeLitB) * smokeDim);

    /* ==================================================================
     * 2026-11-06（v2.4-rc C4）：**主题房间的补光色**
     * ------------------------------------------------------------------
     * 计划里明确"不做第二套纹理集"（双倍纹理常驻内存），
     * 所以房间的辨识度靠**墙面补光**表现：
     *   同一张贴图，但这个房间的光偏冷/偏橙/偏紫。
     *
     * 成本：**每列一次**（不是每像素），普通区直接返回 0 跳过。
     * 这也是为什么不做地板 —— 地板是逐像素的，加查表会掉帧。
     * ================================================================ */
    {

        int tr = 0, tg = 0, tb = 0;
        if (!storyMaterialActive && architectureMaterial<0 && CellTintRGB(mapX, mapY, &tr, &tg, &tb)) {
            float k = smokeDim;          /* 浓烟里补光同样被压 */
            addR += (int)(tr * k);
            addG += (int)(tg * k);
            addB += (int)(tb * k);
        }
    }

    /* Room walls use the floor material; no screen-relative decorative bands. */
    int k8 = (int)(lit * 256.0f);
    if (k8 < 0) k8 = 0;
    else if (k8 > SHADE_LUT_MAX - 1) k8 = SHADE_LUT_MAX - 1;
    k8 = shadeLut[k8];      /* gamma 校正，和地板共用同一张表 */

    unsigned int* dst = pixels + (size_t)drawStart * renderW + x;

    /* v1.9：墙脚 / 墙顶 AO —— 沿墙面上下各压暗 16 像素，
       墙角有了"厚度"，不再是均匀一片贴图 */
    int aoBot = (architectureMaterial>=0?yBottom:drawEnd) - 16;
    int aoTop = (architectureMaterial>=0?yTop:drawStart) + 16;

    /* v2.1：竖直方向的光斑。
       原来亮度只按 x 算（flashTab[x]），同一列从上到下完全一样，
       整面墙就是一块均匀灰板 —— 贴纸感的真正来源。
       这里预生成 64 项竖直权重（中间亮、上下收），每像素多一次查表。 */
    int vLut[64];
    {
        for (int i = 0; i < 64; i++) {
            float v = ((float)i + 0.5f) / 64.0f;
            float dv = v - 0.46f;                    /* 略偏上：视线落点在下半 */
            float fall = 1.0f - dv * dv * 3.2f;      /* 中间 1，两端 <0 */
            if (fall < 0.18f) fall = 0.18f;
            vLut[i] = (int)(fall * 256.0f);
        }
    }

    for (int y = drawStart; y < drawEnd; y++) {
        int texY = ((int)texPos) & (TEX_HEIGHT - 1);
        texPos += step;
        /* v2.0：走了 mip 就按低位采样（lod 0 时与原路径完全等价） */
        unsigned int t = architectureMaterial>=0
            ?sceneTexture[architectureMaterial][0][texY&~1][texX&~1]
            :mipSrc[((texY >> mipShift) & mipMaskY) * mipW + (texX >> mipShift)];
        int vy = (int)(((float)(y - drawStart) * 64.0f) / (float)(drawEnd - drawStart + 1));
        /* The current room's lighting is attached to the actual wall, not
           the clipped viewport span: walking closer cannot stretch the wash. */
        if(architectureMaterial>=0)vy=texY*64/TEX_HEIGHT;
        if (vy < 0) vy = 0;
        if (vy > 63) vy = 63;
        int k8v = (k8 * vLut[vy]) >> 8;              /* 竖直光斑 */
        k8v += bump[texY][texX];                     /* v2.0：凹凸 */
        if (k8v < 0) k8v = 0;
        if (k8v > 1023) k8v = 1023;
        int r = (int)(((t >> 16) & 0xFF) * k8v) >> 8;
        int g = (int)(((t >> 8) & 0xFF) * k8v) >> 8;
        int b = (int)((t & 0xFF) * k8v) >> 8;

        r += addR; g += addG; b += addB;



        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        *dst = (unsigned int)((r << 16) | (g << 8) | b);
        dst += renderW;
    }

    /* v1.9：AO 从逐像素判断里挪出来，改成两段小循环。
       热点循环里少两次比较 + 三次乘法，而这两段只有 32 行。 */
    {
        int yb0 = aoBot > drawStart ? aoBot : drawStart;
        if(yb0>drawEnd)yb0=drawEnd;
        unsigned int* p = pixels + (size_t)(yb0<drawEnd?yb0:drawStart) * renderW + x;
        for (int y = yb0; y < drawEnd; y++, p += renderW) {
            int f = 256 - (y - aoBot) * 13;
            if (f < 116) f = 116;
            unsigned int c = *p;
            int rr = (int)((c >> 16) & 0xFF) * f >> 8;
            int gg = (int)((c >> 8) & 0xFF) * f >> 8;
            int bb = (int)(c & 0xFF) * f >> 8;
            *p = (unsigned int)((rr << 16) | (gg << 8) | bb);
        }
        int yet = aoTop < drawEnd ? aoTop : drawEnd;
        p = pixels + (size_t)drawStart * renderW + x;
        for (int y = drawStart; y < yet; y++, p += renderW) {
            int f = 256 - (aoTop - y) * 11;
            if (f < 140) f = 140;
            unsigned int c = *p;
            int rr = (int)((c >> 16) & 0xFF) * f >> 8;
            int gg = (int)((c >> 8) & 0xFF) * f >> 8;
            int bb = (int)(c & 0xFF) * f >> 8;
            *p = (unsigned int)((rr << 16) | (gg << 8) | bb);
        }
    }
}

static void RenderWallBand(int x0, int x1) {
    for (int x = x0; x < x1; x++) {
        WallRay ray = CastWallRay(x);
        RenderWallSurface(x, &ray);
    }
}

/* 后处理：低理智红边 + 回血白闪，按行并行
   这一段只在理智 < 50 时才跑，但跑起来是全屏逐像素的。
   优化点：把「乘强度」和「除 300」从每像素挪到每帧一次（各 256 项的查表），
   顺便用递增代替乘 3 来定位噪点。 */
static unsigned int* horrorSource=NULL;
static size_t horrorCapacity=0;
static int horrorReady=0,horrorW=0,horrorH=0;
static void PrepareHorrorSource(void){
    horrorReady=0;if(perception.stress<.02f)return;
    horrorW=(renderW+1)/2;horrorH=(renderH+1)/2;
    size_t count=(size_t)horrorW*horrorH;
    if(count>horrorCapacity){unsigned int* next=(unsigned int*)realloc(horrorSource,count*sizeof(unsigned int));if(!next)return;horrorSource=next;horrorCapacity=count;}
    for(int y=0;y<horrorH;y++)for(int x=0;x<horrorW;x++)horrorSource[(size_t)y*horrorW+x]=pixels[(size_t)(y*2)*renderW+x*2];
    horrorReady=1;
}
#include "domain/render/legacy/render_246_post.inc"
static void RenderPostBand(int y0,int y1) {
    if(!pitchLookEnabled) { RenderPostBandLegacy246(y0,y1); return; }
    float stress=perception.stress,crisis=perception.contamination;
    float beat=.5f+.5f*sinf((float)g_timeSec*6.0f);
    float blind=fmaxf(0,fminf(1,FxStrength(FX_BLIND)));
    int radius=1+(int)(stress*3),dark=(int)(256*perception.vignette);
    int doll=(ArtStyleDoll()&&craftTex!=NULL);
    for(int y=y0;y<y1;y++){
        unsigned int* row=pixels+(size_t)y*renderW;
        uint32_t noise=perception.seed^(uint32_t)(y*7919+107);
        /* No moving spatial offsets, including when Vertical Look is OFF. */
        int veinOffset=(int)(sinf(y*.037f)*36)+(int)(perception.seed%9)*3;
        for(int x=0;x<renderW;x++){
            unsigned int c=row[x];float edge=vignetteMask?vignetteMask[(size_t)y*renderW+x]/255.0f:0;
            int r=(c>>16)&255,g=(c>>8)&255,b=c&255;
            if(horrorReady){
                int sx=x/2;if(sx<radius)sx=radius;if(sx>=horrorW-radius)sx=horrorW-radius-1;
                int sy=y/2;if(sy<radius)sy=radius;if(sy>=horrorH-radius)sy=horrorH-radius-1;
                unsigned int a=horrorSource[(size_t)sy*horrorW+sx-radius],d=horrorSource[(size_t)sy*horrorW+sx+radius];
                unsigned int u=horrorSource[(size_t)(sy-radius)*horrorW+sx],v=horrorSource[(size_t)(sy+radius)*horrorW+sx];
                float blur=stress*.68f;
                r=(int)(r*(1-blur)+blur*(((a>>16)&255)+((d>>16)&255)+((u>>16)&255)+((v>>16)&255))*.25f);
                g=(int)(g*(1-blur)+blur*(((a>>8)&255)+((d>>8)&255)+((u>>8)&255)+((v>>8)&255))*.25f);
                b=(int)(b*(1-blur)+blur*((a&255)+(d&255)+(u&255)+(v&255))*.25f);
            }
            noise=noise*1664525u+1013904223u;
            int speck=(int)((((int)(noise>>24)-128)/128.0f)*perception.grain);
            float blood=vignetteOn?(stress*edge*.52f+crisis*(.10f+beat*.12f)):0;
            float vein=((x+veinOffset+128)%57<2 && (noise&128))?crisis*edge*.6f:0;
            if(vignetteOn && stress>.12f && edge>.48f){
                /* Red/cyan ink separation on the border; preserve central navigation. */
                int ink=((x/11+y/23)%7==0)?(int)(stress*14):0;
                r+=ink;b+=ink/3;g-=ink/2;
            }
            float shade=1-edge*dark/256.0f;
            r=(int)(r*shade*(1-blood)+145*blood-45*vein)+speck;
            g=(int)(g*shade*(1-blood*.9f)-32*vein)+speck;
            b=(int)(b*shade*(1-blood*.85f)-28*vein)+speck;
            /* Blindness reduces visibility without changing any ray/FOV. */
            float visibility=1-blind*.8f;
            r=(int)(r*visibility);g=(int)(g*visibility);b=(int)(b*visibility);
            if(r<0)r=0;
            if(r>255)r=255;
            if(g<0)g=0;
            if(g>255)g=255;
            if(b<0)b=0;
            if(b>255)b=255;
            if(doll){
                /* 2026-11-11：布偶整屏层。只乘织纹/渐晕，不做颜色分级
                   （分级已在贴图与精灵烘焙期完成）。 */
                int k=craftTex[(size_t)y*renderW+x];
                r=(r*k)>>7;if(r>255)r=255;
                g=(g*k)>>7;if(g>255)g=255;
                b=(b*k)>>7;if(b>255)b=255;
            }
            row[x]=(unsigned)(r<<16|g<<8|b);
        }
        if(doll)CraftFrameRow(y,row);
    }
}


/* v2.1：落地阴影。全项目只有抛出的光球有假阴影，怪物/箱子全都没有，
   于是所有东西都像"浮在地面上"。这里在精灵脚下压一个扁椭圆。 */
static void DrawGroundShadow(int cx, int cyFloor, int halfW, float ty) {
    if (halfW < 2) return;
    int rx = halfW;
    int ry = halfW / 3;
    if (ry < 1) ry = 1;
    int top = cyFloor - ry;
    for (int y = -ry; y <= ry; y++) {
        int py = top + y + ry;
        if (py < 0 || py >= renderH) continue;
        if ((float)py < 0.0f) continue;
        for (int x = -rx; x <= rx; x++) {
            int px = cx + x;
            if (px < 0 || px >= renderW) continue;
            float u = (float)(x * x) / (float)(rx * rx) + (float)(y * y) / (float)(ry * ry);
            if (u > 1.0f) continue;
            /* v2.0 修复：阴影原来不查深度，墙后的精灵阴影会透出来
               （用户实测「能隔墙看到影子」）。 */
            if (ty >= zBuffer[px]) continue;
            float a = 0.42f * (1.0f - u * 0.55f);
            PutPixelBlend(px, py, 0x00000000u, a);
        }
    }
}

/* ------------------------------------------------------------------ */
/* v2.1 第一人称手持模型                                                */
/*   原来玩家手上什么都没有：准星指向哪里、手上拿的是什么全靠想象。      */
/*   这里是纯手工绘制的：前臂 + 拳头 + 手持物，带走路摆动和开火后坐。     */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* v2.1 第一人称手持模型（烘焙版）                                       */
/*                                                                     */
/* 旧版是每帧用 5~10 个 VMRect 现拼矩形 —— 没有抗锯齿、没有细节、没有     */
/* 动画帧。新版走 Doom/Build 的正统做法：启动时把武器烘成 128×128 位图，   */
/* 运行时只做「双线性 blit + 亮度查表」。                                */
/* ------------------------------------------------------------------ */

void Render3D(void) {
    SpritesSetEvolutionTier(CurrentRunThreatTier());
    PerceptionUpdate(RoomThemeSanityRatio(sanity/fmaxf(1,SanityMax())),g_timeSec);
    int halfH = renderH / 2;

    /* 只清上半屏（下半会被地板整片覆盖） */
    memset(pixels, 0, (size_t)(halfH + 1) * renderW * sizeof(unsigned int));

    /* Stable illumination: no periodic low-charge or enemy-proximity strobe. */
    const float flick=perception.flicker;
    int lightOn=(flashlightOn && battery>0.0f && smokeDim >= 1);
    for (int x = 0; x < renderW; x++) {
        float f;
        if (lightOn) {
            /* 中心特别亮：核心 1.15 倍，边缘 0.55，用平方做成聚光斑 */
            float t = fabsf(x - renderW * 0.5f) / (renderW * 0.5f);
            if (t > 1.0f) t = 1.0f;
            float core = 1.0f - t * t * 0.75f;
            f = 0.40f + core * 0.75f;      /* 0.40 .. 1.15 */
            f *= flick;
        } else {
            /* v1.9：手电熄了就该「几乎看不见」。
               原来 0.32 太亮 —— 关灯还能看清走廊，手电就没意义了。 */
            f = 0.025f;   /* Torch off: only close-range silhouettes remain. */
        }
        flashTab[x] = f;
        float cone = f > 0.40f ? (f - 0.40f) / 0.75f : 0.0f;
        /* 基础值交给逐行的 shadow 决定（含有效距离），这里只加手电中心光斑 */
        floorGain[x] = 1.0f + cone * 1.10f;
        {
            int gi = (int)(floorGain[x] * 256.0f);
            if (gi < 0) gi = 0;
            if (gi > 1023) gi = 1023;
            floorGainI[x] = gi;
        }
        /* v2.1：天花板原来最亮只有 7/255（乘完贴图），上半屏是死黑。
           抬到 0.55 基数 + 150 上限，再乘上限乘的距离雾。 */
        ceilTab[x] = (int)((lightOn?.55f:.06f) * 150.0f + cone*.75f*150.0f);
    }

    RunPhase(1);
    RunPhase(2);
    RenderSprites();
    DrawViewModel();                 /* v2.1 第一人称手持模型 */
    PrepareHorrorSource();
    RunPhase(3);

    /* ==================================================================
     * 2026-11-06：**崩塌黑屏**（在所有后处理之后、送显之前）
     * ------------------------------------------------------------------
     * 直接写 pixels 缓冲而不是逐像素 PutPixelBlend：
     *   全屏 1280x720 是 92 万像素，PutPixelBlend 每次都要算混合，
     *   黑屏持续 2 秒（约 120 帧）= 1.1 亿次混合，会掉帧。
     *   这里只做一次乘算，而且 alpha 为 1 时可以整块清零。
     * ================================================================== */
    {

        float sb = screenBlackout;
        if (sb > 0.0f) {
            if (sb > 1.0f) sb = 1.0f;
            int k = (int)((1.0f - sb) * 256.0f);   /* 保留比例 */
            size_t total = (size_t)renderW * (size_t)renderH;
            if (k <= 0) {
                memset(pixels, 0, total * sizeof(unsigned int));
            } else {
                for (size_t p = 0; p < total; p++) {
                    unsigned int c = pixels[p];
                    int edge=vignetteMask?vignetteMask[p]:128;
                    float local=sb<.5f?sb*2*(.25f+.75f*edge/255.0f):(.5f+(sb-.5f)*2);
                    if(local>1)local=1;
                    int localK=(int)((1-local)*256);if(localK<k)localK=k;
                    unsigned int rr = ((c >> 16) & 0xFF) * (unsigned int)localK >> 8;
                    unsigned int gg = ((c >> 8) & 0xFF) * (unsigned int)localK >> 8;
                    unsigned int bb = (c & 0xFF) * (unsigned int)localK >> 8;
                    pixels[p] = (c & 0xFF000000u) | (rr << 16) | (gg << 8) | bb;
                }
            }
        }
    }

    /* ==================================================================
     * 2026-11-06（用户要求）：**被蛛网缠住的屏幕特效**
     * ------------------------------------------------------------------
     * 用户原话：「屏幕上会有那种被蛛网缠住的那种特效」。
     *
     * 做法：控制类型是蛛网时，在画面上画一层**放射状丝线** + 边缘压白。
     *   · 放射线从屏幕四角与四边中点向中心汇聚 —— 读起来就是"糊住了"
     *   · 边缘一圈白雾（越靠边越浓），把视野挤成中间一块
     *   · 强度跟剩余控制时间挂钩：快挣脱时变淡，给玩家正反馈
     *
     * 直接写 pixels：只画少量线段与边缘带，不是全屏逐像素混合。
     * ================================================================== */
    {
        /* 头文件已有原型，这里不要再写一遍（会撞类型） */

        if (ControlActive() && ControlCurrent() == CTRL_SPIDER_WEB) {
            /* 越接近挣脱越淡（5 秒起步 -> 2 秒下限） */
            /* 强度归一化**直接引用常量** ——
               之前写死 (t-2)/3，下限从 2 改到 1 之后公式就不同步了。 */

            float t = ControlTimeLeft();
            float span = SPIDER_WEB_WINDOW - SPIDER_WEB_MIN;
            float k = (span > 0.01f) ? (t - SPIDER_WEB_MIN) / span : 0.0f;
            if (k < 0.0f) k = 0.0f;
            if (k > 1.0f) k = 1.0f;
            k = 0.45f + 0.55f * k;

            int w = renderW, h = renderH;
            int cx = w / 2, cy = h / 2;

            /* ① 边缘白雾：距离中心越远越白 */
            for (int y = 0; y < h; y++) {
                int dy = (y > cy) ? (y - cy) : (cy - y);
                /* 只用纵向和横向较大的一维近似，省掉每像素开方 */
                for (int x = 0; x < w; x++) {
                    int dx = (x > cx) ? (x - cx) : (cx - x);
                    int d = (dx > dy) ? dx : dy;            /* 切比雪夫距离 */
                    int thr = (w < h ? w : h) * 3 / 10;
                    if (d <= thr) continue;
                    int a = (d - thr) * 256 / ((w < h ? w : h) / 2);
                    if (a > 200) a = 200;
                    a = (int)((float)a * k);
                    if (a <= 0) continue;
                    unsigned int c = pixels[(size_t)y * w + x];
                    unsigned int rr = ((c >> 16) & 0xFF);
                    unsigned int gg = ((c >> 8) & 0xFF);
                    unsigned int bb = (c & 0xFF);
                    rr = rr + ((230u - rr) * (unsigned)a >> 8);
                    gg = gg + ((230u - gg) * (unsigned)a >> 8);
                    bb = bb + ((235u - bb) * (unsigned)a >> 8);
                    pixels[(size_t)y * w + x] = (c & 0xFF000000u) | (rr << 16) | (gg << 8) | bb;
                }
            }

            /* ② 放射丝线：从八条边的锚点向中心画 */
            {
                static const int ax[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
                for (int i = 0; i < 8; i++) {
                    int sx, sy;
                    switch (ax[i]) {
                    case 0: sx = 0;      sy = 0;      break;
                    case 1: sx = cx;     sy = 0;      break;
                    case 2: sx = w - 1;  sy = 0;      break;
                    case 3: sx = w - 1;  sy = cy;     break;
                    case 4: sx = w - 1;  sy = h - 1;  break;
                    case 5: sx = cx;     sy = h - 1;  break;
                    case 6: sx = 0;      sy = h - 1;  break;
                    default: sx = 0;     sy = cy;     break;
                    }
                    /* 稍微偏离中心，避免 8 条线在同一个点打结 */
                    int tx = cx + (sx - cx) / 6;
                    int ty = cy + (sy - cy) / 6;
                    int steps = (w > h ? w : h);
                    for (int st = 0; st < steps; st++) {
                        float f = (float)st / (float)steps;
                        int px = sx + (int)((tx - sx) * f);
                        int py = sy + (int)((ty - sy) * f);
                        if (px < 0 || py < 0 || px >= w || py >= h) continue;
                        /* 丝线本身只占 1 像素，每 3 像素画 1 个点 = 虚线质感 */
                        if ((st % 3) != 0) continue;
                        unsigned int c = pixels[(size_t)py * w + px];
                        unsigned int rr = ((c >> 16) & 0xFF);
                        unsigned int gg = ((c >> 8) & 0xFF);
                        unsigned int bb = (c & 0xFF);
                        int a = (int)(150.0f * k);
                        rr = rr + ((245u - rr) * (unsigned)a >> 8);
                        gg = gg + ((245u - gg) * (unsigned)a >> 8);
                        bb = bb + ((245u - bb) * (unsigned)a >> 8);
                        pixels[(size_t)py * w + px] = (c & 0xFF000000u) | (rr << 16) | (gg << 8) | bb;
                    }
                }
            }
        }
    }
}
/* 小地图（在内部渲染分辨率上绘制）
   两种形态：
     角落小窗（默认）：cellSize 固定 8 -> 24*8 = 192px 见方，
                       贴在右下角，不挡视野，可以边走边看。
     全屏：cellSize 按屏幕大小算，铺满中央，用来规划路线。
   之前只有全屏一种形态 —— 打开就得停下，关掉才看得见游戏，
   失去了「导航」的意义。 */
#define MAP_CORNER_CELL 8

/* v3.0：**全屏地图的图例** ——
   玩家实测：「绿点是我吗？中间青点是谁？两个黄点是什么？出口在哪？」
   原来一个标注都没有，只能靠猜。 */

/* ------------------------------------------------------------------ */
/* 逻辑更新（固定步长，dt = LOGIC_STEP）                                */
/* ------------------------------------------------------------------ */

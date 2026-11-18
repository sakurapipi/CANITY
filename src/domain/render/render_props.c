#include "shot_spec.h"
#include "tactical_tools.h"
/* 2026-11-16：从 render.c 拆出的**世界道具/投掷物绘制**（域内解耦，行为不变）。
   这一簇包含投影工具 ProjectPoint、染色怪物、怪物列表、投掷物与胶水坑。
   它们之间只互相调用，不引用 render.c 的其他 static。 */
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

static int ProjectPoint(float wx, float wy, float z,
                        int* outX, int* outY, float* outUnit, float* outTy) {
    float dx = wx - posX, dy = wy - posY;
    float det = planeX * dirY - dirX * planeY;
    if (!isfinite(det) || fabsf(det) < 0.000001f) return 0;
    float invDet = 1.0f / det;
    float tx = invDet * (dirY * dx - dirX * dy);
    float ty = invDet * (-planeY * dx + planeX * dy);
    if (!isfinite(tx) || !isfinite(ty) || ty <= 0.05f) return 0;
    float unit = RenderProjectionScale(ty);
    *outX = (int)((renderW / 2.0f) * (1.0f + tx / ty));
    float hz=RenderProjectionCenterY();
    *outY = (int)(hz+unit*(.5f-z));
    *outUnit = unit;
    *outTy = ty;
    return 1;
}

void DrawOrbSprite(float wx, float wy, float z, float worldR,
                          unsigned int coreCol, unsigned int haloCol,
                          float alphaMul) {
    /* v2.1：光球是自发光体，不能继承上一张精灵的距离衰减。
       spriteLight 原来只在出口光柱那里被显式置 1，其余自发光精灵
       会沿用"上一个画完的精灵"的衰减 —— 荧光棒的亮度取决于旁边的箱子有多近。 */
    spriteLight = 1.0f;
    int cx, cy, cyFloor;
    float unit, ty;
    {
        float dx0 = wx - posX, dy0 = wy - posY;
        float det = planeX * dirY - dirX * planeY;
        if (!isfinite(det) || fabsf(det) < 0.000001f) return;
        float invDet = 1.0f / det;
        float tx = invDet * (dirY * dx0 - dirX * dy0);
        ty = invDet * (-planeY * dx0 + planeX * dy0);
        if (!isfinite(tx) || !isfinite(ty) || ty <= 0.05f) return;
        unit = RenderProjectionScale(ty);
        cx = (int)((renderW / 2.0f) * (1.0f + tx / ty));
        float hz=RenderProjectionCenterY();
        cyFloor = (int)(hz+unit*.5f);
        cy = (int)(hz+unit*(.5f-z));
    }

    int r = (int)(unit * worldR);
    if (r < 3) r = 3;
    if (r > 90) r = 90;
    if (cx + r < 0 || cx - r >= renderW || cy + r < 0 || cy - r >= renderH) return;

    /* ① 遮挡只判一次 */
    int ocx = cx < 0 ? 0 : (cx >= renderW ? renderW - 1 : cx);
    /* v2.0：去掉 0.85 偏置 —— 它让墙后 18% 距离内的光源仍然可见 */
    if (ty >= zBuffer[ocx]) return;

    /* ② 地面阴影：飞得越高越小越淡 */
    if (z > 0.02f) {
        float k = 1.0f / (1.0f + z * 1.6f);
        int sr = (int)(r * 0.75f * k);
        if (sr < 2) sr = 2;
        for (int sy = -sr; sy <= sr; sy++) {
            for (int sx = -sr; sx <= sr; sx++) {
                if (sx * sx + (sy * 2) * (sy * 2) > sr * sr) continue;
                int px = cx + sx, py = cyFloor + sy;
                if (px < 0 || px >= renderW || py < 0 || py >= renderH) continue;
                if (ty >= zBuffer[px]) continue;    /* v2.0：阴影不再画到墙后 */
                PutPixelBlend(px, py, 0x00000000u, 0.32f);
            }
        }
    }

    /* ③ 大范围柔光晕：让地面/墙面看起来被照亮 */
    {
        int gr = r * 3;
        if (gr > 120) gr = 120;
        float flick0 = 0.88f + 0.12f * sinf((float)g_timeSec * 21.0f);
        for (int yy = -gr; yy <= gr; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            for (int xx = -gr; xx <= gr; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                float d2 = (float)(xx * xx + yy * yy) / (float)(gr * gr);
                if (d2 >= 1.0f) continue;
                /* v2.0：光晕逐像素查深度 —— 半径最大 120px，原来整团能穿进隔壁房间 */
                if (ty >= zBuffer[px]) continue;
                float a = 1.0f - d2;
                PutPixelAdd(px, py, haloCol, a * a * 0.10f * flick0);
            }
        }
    }

    /* ② 球体：核心白 + 外圈色，加色混合 */
    float flick = 0.88f + 0.12f * sinf((float)g_timeSec * 21.0f);
    for (int yy = -r; yy <= r; yy++) {
        int py = cy + yy;
        if (py < 0 || py >= renderH) continue;
        for (int xx = -r; xx <= r; xx++) {
            int px = cx + xx;
            if (px < 0 || px >= renderW) continue;
            float d2 = (float)(xx * xx + yy * yy) / (float)(r * r);
            if (d2 >= 1.0f) continue;
            if (ty >= zBuffer[px]) continue;    /* v2.0：球体本身也要挡 */
            float a = 1.0f - d2;
            PutPixelAdd(px, py, haloCol, a * a * alphaMul * flick);
            if (d2 < 0.30f) PutPixelAdd(px, py, coreCol, (0.30f - d2) * 2.6f * alphaMul * flick);
        }
    }
}

/* 荧光棒（落地发光体也用同一套，只是 z=0、半径略大） */

static void DrawTintedBeast(int spr,float wx,float wy,float halfH,unsigned int tint,float tintMix){
    DrawCreatureActor(spr,wx,wy,halfH,1,1,atan2f(posY-wy,posX-wx),(float)g_timeSec*1.4f,tint,tintMix,.55f);
}

void DrawNewMonsters(void) {
    /* ---- 拾荒者：**只换麻袋颜色**，身体保持原样 ---- */
    {


        if (CollectorActive()) {
            /* v2.4.66：整体染色会把明暗关系洗掉，观感廉价。
               现在只把**麻袋**（烘焙时画的洋红 key）换成白/金，
               身体的三层暗灰完全不动。 */
            if (CollectorHasMoney()) SpriteAccentSet(0x00FFC040u);   /* 金：装满赃款 */
            else                     SpriteAccentSet(0x00E8F0FFu);   /* 白：空袋子 */
            DrawTintedBeast(SPR_MON_LEECH, CollectorX(), CollectorY(), 0.86f, 0, 0.0f);
            SpriteAccentClear();
        }
    }
    /* ---- 清道夫：高瘦 + 病绿；开柜门时眼白转红 ---- */
    {


        if (ScavengerActive()) {
            /* v2.4.7：撬柜门 **或** 正在翻箱子，两种都算警戒 */
            int grabbing = (ScavengerWarnT() > 0.0f) || ScavengerRifling();
            /* 清道夫：用追光怪的身体（更高瘦） + 病绿色调 */
            DrawTintedBeast(SPR_MON_SHADE, ScavengerX(), ScavengerY(),
                            1.00f, 0x00FF4030u, grabbing ? 0.28f : 0.00f);   /* v2.4.2 方案A：原色；撬柜门才加轻红调 */
        }
    }
    /* ---- 守门人：**两个烘焙变体，眼睛颜色直接烘死** ---- */
    {



        if (WardenActive()) {
            /* v2.4.81 **不再用洋红 key 换色**。
               调研结论：颜色变化用调色盘是对的，但**前提是 key 色绝对不能漏到屏幕上**。
               我在这条上翻车三次（品红眼睛、边缘杂色、绿色残留）。
               守门人只有两种状态，直接烘两个变体最稳：
                 SPR_MON_SPIDER   = 冷青眼（巡逻 / 堵门 / 失明）
                 SPR_MON_LEECH_W  = 血红眼（追击）
               一个 SpriteAccent 都不调，也就没有 key 泄漏的可能。 */
            int spr = WardenChasing() ? SPR_MON_WARDEN_CHASE : SPR_MON_SPIDER;
            DrawTintedBeast(spr, WardenX(), WardenY(), 0.94f, 0, 0.0f);
        }
    }
    /* ---- 回声：极矮 + 冷白微光，强度越弱越暗 ---- */
    {

        for (int i = 0; i < SUSP_MAX; i++) {
            if (susp[i].strength <= 0.0f) continue;
            if (susp[i].source != SUSP_THROW) continue;
            float a = susp[i].strength / 60.0f;   /* 刚扔出=1，衰减到 0 */
            if (a > 1.0f) a = 1.0f;
            if (a < 0.15f) a = 0.15f;
            /* 回响：小号的追光怪精灵 + 冷白偏青的色调，半透明感 */
            /* v2.4.2：回声用自己烘焙的模型（同心环 + 尖角核心），
               并让尺寸随剩余强度做一次「呼吸」，看起来在持续广播。 */
            DrawTintedBeast(SPR_MON_ECHO, susp[i].x, susp[i].y,
                            0.26f + 0.18f * a, 0x00FFFFFFu, 0.0f);
        }
    }
}

/* ===== v7 钱袋 =====
   用最小的箱子道具当模型，但外面套一圈金色脉冲，
   肉眼一眼能认出「那是钱」。金额越大脉冲越亮。 */

/* Rotating model-space sampling, with the same column depth test as props.
 * Rendering uses the catalog sprite, so no unknown-kind blue-ball fallback. */
static void DrawTacticalShot(int index,int cx,int cy,float unit,float depth) {
    Shot *projectile=&shot[index];
    int tool=projectile->kind-TOOL_SHOT_FIRST;
    Sprite *model=SpriteGet(SPR_TOOL_BASE+tool);
    if(!model || model->bx1<model->bx0 || model->by1<model->by0)return;
    int sw=model->bx1-model->bx0+1,sh=model->by1-model->by0+1;
    float h=fminf(renderH*.20f,fmaxf(5,unit*(tool==TOOL_PRISM?.45f:.27f)));
    float w=h*sw/sh,angle=projectile->age*4;
    if(tool==TOOL_PRISM) {
        int nx,ny;float nu,nd;
        if(ProjectPoint(projectile->x+projectile->vx*.05f,projectile->y+projectile->vy*.05f,
                        projectile->z+projectile->vz*.05f,&nx,&ny,&nu,&nd) && (nx!=cx || ny!=cy))
            angle=atan2f((float)(ny-cy),(float)(nx-cx))+1.5707963f;
        else angle=-.4f;
    }
    float co=cosf(angle),si=sinf(angle);
    int bound=(int)(fmaxf(w,h)*.72f)+2;
    for(int y=-bound;y<=bound;y++) {
        int py=cy+y;if(py<0 || py>=renderH)continue;
        for(int x=-bound;x<=bound;x++) {
            int px=cx+x;if(px<0 || px>=renderW || depth>=zBuffer[px])continue;
            float u=(co*x+si*y)/w+.5f,v=(-si*x+co*y)/h+.5f;
            if(u<0 || u>=1 || v<0 || v>=1)continue;
            int sx=model->bx0+(int)(u*sw),sy=model->by0+(int)(v*sh);
            unsigned c=model->px[sy*model->w+sx];
            if(c>>24)PutPixelBlend(px,py,c&0xFFFFFF,((c>>24)&255)/255.0f);
        }
    }
}

void DrawEchoRadarPulse(void) {
    if (echoScanT <= 0) return;
    float age = 6 - echoScanT;
    for (int wave = 0; wave < 3; wave++) {
        float phase = (age - wave * .24f) / 1.35f;
        if (phase <= 0 || phase >= 1) continue;
        float radius = ECHO_RADIUS * phase;
        for (int i = 0; i < 180; i++) {
            float angle = i * (6.2831853f / 180);
            float x = echoScanX + radius * cosf(angle), y = echoScanY + radius * sinf(angle);
            int px, py;
            float unit, depth;
            if (!ProjectPoint(x, y, .045f, &px, &py, &unit, &depth)) continue;
            if ((unsigned)px >= (unsigned)renderW || (unsigned)py >= (unsigned)renderH ||
                depth >= zBuffer[px]) continue;
            PutPixelBlend(px, py, 0x61F5D8, (1 - phase) * .82f);
            if (py + 1 < renderH)
                PutPixelBlend(px, py + 1, 0x3794CE, (1 - phase) * .50f);
        }
    }
}

void DrawShotModel(int idx) {
    float wx = shot[idx].x, wy = shot[idx].y, z = shot[idx].z;
    int cx, cy;
    float unit, ty;
    if (!ProjectPoint(wx, wy, z, &cx, &cy, &unit, &ty)) return;
    int occ = cx < 0 ? 0 : (cx >= renderW ? renderW - 1 : cx);
    if (ty * 0.85f >= zBuffer[occ]) return;

    int kind = shot[idx].kind;
    if (kind==12 || kind==13 || kind==SHOT_KIND_DECOY) {
        int size=(int)fmaxf(5,fminf(190,unit*.28f));
        int sprite=kind==SHOT_KIND_DECOY?SPR_DECOY_FOLDED:SPR_PHASE_PROJECTILE;
        DrawSprite(sprite,cx-size/2,cy-size/2,size,size,.90f,1,1,ty);
        return;
    }
    if(kind>=TOOL_SHOT_FIRST && kind<TOOL_SHOT_FIRST+TOOL_COUNT) {
        DrawTacticalShot(idx,cx,cy,unit,ty);
        return;
    }

    if (kind == 6) {
        /* v2.0 信号枪弹：一小团明黄的光点 + 外晕 */
        int r = (int)(unit * 0.05f); if (r < 2) r = 2;
        for (int dy = -r * 2; dy <= r * 2; dy++) {
            for (int dx = -r * 2; dx <= r * 2; dx++) {
                int px = cx + dx, py = cy + dy;
                if (px < 0 || px >= renderW || py < 0 || py >= renderH) continue;
                float d = sqrtf((float)(dx * dx + dy * dy)) / (float)(r * 2);
                if (d > 1.0f) continue;
                unsigned int col = (d < 0.45f) ? 0x00FFF8D0u
                                 : (d < 0.75f ? 0x00FFD040u : 0x00C06020u);
                PutPixelBlend(px, py, col, 0.95f - 0.5f * d);
            }
        }
        return;
    }
    /* ===== v3.3 新投掷物的飞行模型 =====
       kind 1  投石：不规则灰石，带高光面
       kind 3  胶水：矮胖圆罐 + 瓶颈，青绿色，看得出是「黏的」
       kind 10 回声：细长的哨形，淡紫，尖端朝前
       （kind 2 砖头 / kind 7 火瓶 / kind 9 镜片 已随系统删除） */

    if (kind == 1) {
        /* 投石：不规则多边形石块 */
        int w = (int)(unit * 0.075f); if (w < 3) w = 3;
        int h = (int)(unit * 0.065f); if (h < 3) h = 3;
        for (int yy = -h; yy <= h; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            /* 不规则边缘：宽度随行变化，看起来像块石头不是圆球 */
            int wob = ((yy * 37) ^ 0x55) % 3 - 1;
            int halfW = w + wob - (yy > h / 2 ? 1 : 0);
            for (int xx = -halfW; xx <= halfW; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                unsigned int c;
                if (yy < 0 && xx < 0)      c = 0x00B4B0A8u;   /* 左上高光面 */
                else if (yy > 0)           c = 0x0056504Au;   /* 下方阴影面 */
                else                       c = 0x00888478u;   /* 中间 */
                PutPixelBlend(px, py, c, 0.96f);
            }
        }
        return;
    }
    if (kind == 8) {
        /* 胶水瓶：矮胖罐 + 短瓶颈，青绿色，表面有湿滑高光 */
        int w = (int)(unit * 0.085f); if (w < 3) w = 3;
        int h = (int)(unit * 0.085f); if (h < 4) h = 4;
        for (int yy = -h; yy <= h; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            /* 上 1/3 收成瓶颈，下面是胖罐身 */
            int halfW = (yy < -h * 2 / 3) ? (w / 2 + 1) : w;
            for (int xx = -halfW; xx <= halfW; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                unsigned int c;
                if (yy < -h * 2 / 3)       c = 0x005A9A6Eu;   /* 瓶颈：深青 */
                else if (xx > halfW - 2)   c = 0x0090F0B4u;   /* 右侧湿滑高光 */
                else if (yy > h / 2)       c = 0x002A4A38u;   /* 罐底阴影 */
                else                       c = 0x004C8A62u;   /* 罐身 */
                PutPixelBlend(px, py, c, 0.96f);
            }
        }
        return;
    }
    if (kind == 10) {
        /* Armored radar puck: beveled shell, inset lens and rotating indicator. */
        int radius = (int)fminf(72, fmaxf(4, unit * .085f));
        float sweep = g_timeSec * 12;
        float sx = cosf(sweep), sy = sinf(sweep);
        for (int y = -radius; y <= radius; y++)
            for (int x = -radius; x <= radius; x++) {
                int px = cx + x, py = cy + y;
                if ((unsigned)px >= (unsigned)renderW || (unsigned)py >= (unsigned)renderH ||
                    ty >= zBuffer[px]) continue;
                int d = abs(x) + abs(y);
                if (abs(x) > radius * .85f || d > radius * 1.3f) continue;
                float r = (float)(x*x + y*y) / (radius*radius);
                unsigned color = y < 0 ? 0x74869D : 0x344354;
                if (d > radius) color = 0x111A27;
                if (r < .50f) color = r > .37f ? 0x75E8DD : 0x154050;
                if (r < .48f && x*sx + y*sy >= 0 && fabsf(y*sx - x*sy) < 1.2f)
                    color = 0xD0FFF5;
                PutPixelBlend(px, py, color, .98f);
            }
        return;
    }

    if (kind == 2) {
        /* 砖头（已废弃，保留代码以防老存档里的 shot 还在飞） */
        int w = (int)(unit * 0.17f); if (w < 3) w = 3;
        int h = (int)(unit * 0.08f); if (h < 2) h = 2;
        for (int yy = -h; yy <= h; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            for (int xx = -w; xx <= w; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                int shade = (yy < 0) ? 1 : 0;               /* 上亮下暗 */
                unsigned int c = shade ? 0x00C8A888u : 0x00886848u;
                PutPixelBlend(px, py, c, 0.95f);
            }
        }
    } else if (kind == 3) {
        /* 酸瓶：瓶身 + 瓶颈 */
        int w = (int)(unit * 0.09f); if (w < 2) w = 2;
        int h = (int)(unit * 0.14f); if (h < 4) h = 4;
        for (int yy = -h; yy <= h; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            int halfW = (yy < -h * 2 / 3) ? w / 2 : w;      /* 上半段收成瓶颈 */
            for (int xx = -halfW; xx <= halfW; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                unsigned int c = (yy > h / 3) ? 0x0080FF60u : 0x00C0FF90u;
                PutPixelBlend(px, py, c, 0.95f);
            }
        }
    } else if (kind == 4) {
        /* 烟雾弹：圆柱罐 */
        int w = (int)(unit * 0.10f); if (w < 3) w = 3;
        int h = (int)(unit * 0.15f); if (h < 4) h = 4;
        for (int yy = -h; yy <= h; yy++) {
            int py = cy + yy;
            if (py < 0 || py >= renderH) continue;
            for (int xx = -w; xx <= w; xx++) {
                int px = cx + xx;
                if (px < 0 || px >= renderW) continue;
                unsigned int c = 0x00909090u;
                if (xx > w - 2) c = 0x00C8C8C8u;            /* 右侧高光 */
                if (yy < -h + 2) c = 0x00B0B0B0u;           /* 顶部盖 */
                PutPixelBlend(px, py, c, 0.95f);
            }
        }
    } else {
        DrawOrbSprite(wx, wy, z, 0.12f, 0x00ffffffu, 0x0060c0ffu, 0.95f);
    }
}

/* v2.9 剑气：竖着的一道月牙刀光。
   - 刀身中间厚、两头尖（不是椭圆，椭圆看着像光球）
   - 中间亮、边缘青
   - 身后拖两层半透明的残影，做出"劈出去"的速度感
   渲染层只能用 PutPixelBlend（BlendRectWin 在 ui.c）。 */
/* v2.0 额外威胁：用一张剪影 + 按种类换眼睛颜色/体型 */

void DrawGluePuddle(void) {
    spriteLight = 1.0f;

    for (int i = 0; i < MAX_GLUE; i++) {
        if (!glues[i].active) continue;
        float t = glues[i].life / GLUE_LIFE;         /* 1 → 0 */
        if (t > 1.0f) t = 1.0f;
        if (t < 0.0f) t = 0.0f;
        float fade = fminf(1, t*4);
        float age = GLUE_LIFE*(1-t);
        float ripple = fminf(1, age*1.7f);
        float flow = (float)g_timeSec*1.9f;

        /* 网格密度：半径 2.5 格 → 采样 22x22，步长约 0.23 格。
           太稀会看到断点，太密单帧开销上去了（软件光栅，得省）。 */
        const int N = 22;
        float step = (GLUE_RADIUS * 2.0f) / (float)(N - 1);
        int cellR = (int)(UIS(3));
        if (cellR < 1) cellR = 1;

        for (int gy = 0; gy < N; gy++) {
            for (int gx = 0; gx < N; gx++) {
                float wx = glues[i].x - GLUE_RADIUS + (float)gx * step;
                float wy = glues[i].y - GLUE_RADIUS + (float)gy * step;
                float dx = wx - glues[i].x, dy = wy - glues[i].y;
                float d2n = (dx * dx + dy * dy) / (GLUE_RADIUS * GLUE_RADIUS);
                float edge = .92f + .06f*sinf(gx*.81f+gy*.39f) + .06f*cosf(gy*.77f-gx*.53f);
                if (d2n > fminf(1,edge)) continue;

                int px, py;
                float unit, ty;
                /* z = 0：贴地。这正是「横截面」的关键 —— 不用 billboard */
                if (!ProjectPoint(wx, wy, 0.0f, &px, &py, &unit, &ty)) continue;
                if (px < 0 || px >= renderW || py < 0 || py >= renderH) continue;
                if (ty >= zBuffer[px]) continue;       /* 被墙挡住就不画 */

                /* 形状：圆心最浓 → 边缘化开；再加一点不规则 */
                float core = 1.0f - d2n;
                float wob = 0.90f + 0.10f * sinf((float)gx * 1.7f + (float)gy * 2.3f);
                unsigned int col;
                float a;
                if (core > 0.72f) {                    /* 中心高光 */
                    col = 0x006fd08au;
                    a = (0.16f + 0.20f * core) * fade;
                } else if (d2n > 0.78f) {               /* 边缘一圈反光 */
                    col = 0x005cb87cu;
                    a = (0.34f + 0.28f * core) * fade * wob;
                } else {                                /* 主体 */
                    col = 0x003a6b4au;
                    a = (0.30f + 0.40f * core) * fade * wob;
                }
                float front = fabsf(d2n-ripple*ripple);
                if (age < .65f && front < .12f) {
                    col=0x94E7A6;
                    a=fmaxf(a, (1-front/.12f)*(1-ripple)*.76f);
                } else if ((gx*7+gy*11)%13==0 && sinf(gx*.7f+gy*.61f+flow)>.48f) {
                    col=0xADD9A0;
                    a*=.7f;
                }
                if (a > 0.85f) a = 0.85f;
                if (a <= 0.02f) continue;

                /* 每个采样点画一个小方块，方块之间自然连成一片 */
                int x0 = px - cellR, x1 = px + cellR;
                int y0 = py - cellR, y1 = py + cellR;
                if (x0 < 0) x0 = 0;
                if (y0 < 0) y0 = 0;
                if (x1 >= renderW) x1 = renderW - 1;
                /* 竖直方向压扁 —— 地面贴花在屏幕上总是扁的 */
                int yShrink = cellR / 2;
                if (yShrink < 1) yShrink = 1;
                y1 = py + yShrink;
                if (y1 >= renderH) y1 = renderH - 1;
                for (int yy = y0; yy <= y1; yy++)
                    for (int xx = x0; xx <= x1; xx++)
                        if (ty < zBuffer[xx]) PutPixelBlend(xx, yy, col, a);
            }
        }
    }
}


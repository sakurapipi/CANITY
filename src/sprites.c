#include "monster_evolution.h"
/*
 * Canity — v2.1 烘焙精灵管线
 *
 * 调研依据（见 docs/建模重造_调研与设计.md）：
 *   Doom / Build / Wolfenstein 的 sprite 都是**预烘焙位图 + 索引色查表**，
 *   运行时只做「查表 + 拷像素」。这个项目原来每帧用矩形现拼形状，是反的。
 *
 * 这里实现：
 *   1) 2× 超采样画布 + 基础图元（多边形/椭圆/线段/渐变/噪点/辉光）
 *   2) 降采样 → 抗锯齿边缘
 *   3) 24 档亮度 LUT → 运行时零浮点
 *   4) 双线性 blit + alpha + 可选 z 测试
 */

#include "sprites.h"
#include "common.h"
#include "render.h"
#include "art.h"

#include "domain/sprites/sprite_bake.h"

static Sprite g_sprites[SPR_COUNT];
static int g_ready = 0;
static int evolutionBakeTier=0;
int SpriteBakeEvolutionTier(void){return evolutionBakeTier;}
Sprite* SpriteBakeTarget(int id) { return &g_sprites[id]; }
int SpriteBakeIndex(const Sprite* sprite) { return (int)(sprite - g_sprites); }

static void BakeWrenchStrike(void) {
    SpriteBakeCanvasState* canvas=SpriteBakeCanvas();
    cvClear();
    float c = CANVAS_W * 0.5f;   /* 128 */

    /* 2026-11-16（重新设计）：上一版把头部放大到 1.35 倍，
       结果在 256x256 画布里变成一块"方板"，而且握把末端 y=258 直接落到画布外，
       包围盒被裁成只剩头 → 绘制时被拉成方框。
       这一版**严格沿用待机款的坐标框架**（ax0=(172,246) → ax1=(102,96)），
       只做"看得出是同一把、但姿态不同"的改动：
         · 头部 1.08 倍（不是 1.35）
         · 钳口从"半闭"改成"张开咬合"（V 形开口更大，能看到齿）
         · 颚面加一条亮边（受力）
         · 头部前方三道短促的冲击线（不是散点，不会撑大包围盒）
         · 握把加粗一点、缠带压紧（表示手在发力） */

    float ax0 = c + 44, ay0 = 246;      /* 与待机款完全相同的握把末端 */
    float ax1 = c - 26, ay1 = 96;       /* 与待机款相同的头颈 */
    float dx = ax1 - ax0, dy = ay1 - ay0;
    float L = sqrtf(dx * dx + dy * dy);
    float ux = dx / L, uy = dy / L;
    float vx = -uy, vy = ux;

    /* ---- 手柄：比待机粗 2px，其余同位置 ---- */
    cvLine(ax0 + 5, ay0 + 5, ax1 + 5, ay1 + 5, 34, 0x00080A0Eu, 1.0f);
    cvLine(ax0, ay0, ax1, ay1, 30, 0x004C443Au, 1.0f);
    cvLine(ax0 + vx * 6, ay0 + vy * 6, ax1 + vx * 6, ay1 + vy * 6, 8, 0x00766C5Eu, 1.0f);
    cvLine(ax0 - vx * 7, ay0 - vy * 7, ax1 - vx * 7, ay1 - vy * 7, 6, 0x00221C16u, 1.0f);

    /* 缠带：三道，比待机更紧（间距更小） */
    for (int i = 0; i < 3; i++) {
        float tt = 0.46f + (float)i * 0.11f;
        float bx = ax0 + dx * tt, by = ay0 + dy * tt;
        cvLine(bx + vx * 16, by + vy * 16, bx - vx * 16, by - vy * 16, 7, 0x001C1510u, 1.0f);
        cvLine(bx + vx * 14 + ux * 2, by + vy * 14 + uy * 2,
               bx - vx * 14 + ux * 2, by - vy * 14 + uy * 2, 4, 0x00463a2au, 1.0f);
    }

    /* ---- 头部：只放大 8%，位置不动 ---- */
    float hx = ax1, hy = ay1;
    const float k = 1.08f;
    float head[20] = {
        hx - 30 * k, hy - 46 * k,
        hx + 34 * k, hy - 40 * k,
        hx + 44 * k, hy - 12 * k,
        hx + 30 * k, hy + 22 * k,
        hx - 26 * k, hy + 26 * k,
        hx - 40 * k, hy + 2 * k,
        hx - 34 * k, hy - 22 * k,
        hx - 30 * k, hy - 34 * k,
        hx - 34 * k, hy - 6 * k,
        hx - 38 * k, hy - 26 * k
    };
    float outline[20];
    for (int i = 0; i < 10; i++) {
        outline[i * 2] = hx + (head[i * 2] - hx) * 1.10f;
        outline[i * 2 + 1] = hy + (head[i * 2 + 1] - hy) * 1.10f;
    }
    cvPoly(outline, 10, 0x00080A0Eu, 1.0f);
    cvPolyGrad(head, 10, 0x00C4BCACu, 0x005E564Au, 1.0f);

    /* 钳口：张开更大（V 形），开向左侧；比待机款明显 */
    {
        float mouth[8] = {
            hx - 46 * k, hy - 34 * k,     /* 上颚尖 */
            hx - 12 * k, hy - 22 * k,     /* 上颚根 */
            hx - 14 * k, hy + 16 * k,     /* 下颚根 */
            hx - 44 * k, hy + 24 * k      /* 下颚尖 */
        };
        int y0 = (int)(hy - 40 * k), y1 = (int)(hy + 30 * k);
        for (int y = y0; y <= y1; y++) {
            if (y < 0 || y >= CANVAS_H) continue;
            for (int x = (int)(hx - 54 * k); x <= (int)hx; x++) {
                if (x < 0 || x >= CANVAS_W) continue;
                float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
                int inside = 0;
                for (int i = 0; i < 4; i++) {
                    int j = (i + 1) % 4;
                    float ay = mouth[i * 2 + 1], by = mouth[j * 2 + 1];
                    if ((ay <= fy && by > fy) || (by <= fy && ay > fy)) {
                        float t2 = (fy - ay) / (by - ay);
                        if (fx < mouth[i * 2] + t2 * (mouth[j * 2] - mouth[i * 2])) inside ^= 1;
                    }
                }
                if (inside) canvas->pixels[(size_t)y * CANVAS_W + x] = 0;
            }
        }
    }

    /* 齿：沿开口边缘，上下各三颗（出招时咬合） */
    for (int i = 0; i < 3; i++) {
        float tx = hx - 42 * k + (float)i * 9.0f;
        cvLine(tx, hy - 30 * k, tx + 2, hy - 22 * k, 4, 0x00E8E0D0u, 1.0f);
        cvLine(tx, hy + 14 * k, tx + 2, hy + 6 * k,  4, 0x00D8D0C0u, 1.0f);
    }

    /* 颚面受力亮边 */
    cvLine(hx - 44 * k, hy - 33 * k, hx - 14 * k, hy - 23 * k, 2, 0x00FFF4DCu, 0.9f);
    cvLine(hx - 42 * k, hy + 22 * k, hx - 16 * k, hy + 14 * k, 2, 0x00F0E4C8u, 0.85f);

    /* 调节螺母 */
    {
        float nx = hx + 26 * k, ny = hy + 2 * k;
        cvEllipse(nx, ny, 12.0f, 12.0f, 0x00080A0Eu, 1.0f);
        cvEllipse(nx, ny, 9.0f, 9.0f, 0x0094897Au, 1.0f);
        cvEllipse(nx - 2, ny - 2, 3.4f, 3.4f, 0x00302A22u, 1.0f);
    }

    /* 摇柄（同位置） */
    {
        float kx = hx + 10, ky = hy + 30;
        cvEllipse(kx, ky, 8.0f, 8.0f, 0x00080A0Eu, 1.0f);
        cvEllipse(kx, ky, 5.6f, 5.6f, 0x007C7464u, 1.0f);
        cvLine(kx, ky + 6, kx + 4, ky + 26, 6, 0x00080A0Eu, 1.0f);
        cvLine(kx, ky + 6, kx + 4, ky + 26, 4, 0x00686052u, 1.0f);
    }

    /* 冲击线：从钳口前方甩出的三道短线（短、细，不会撑大包围盒） */
    cvLine(hx - 52 * k, hy - 30 * k, hx - 70 * k, hy - 40 * k, 3, 0x00FFEAB4u, 0.75f);
    cvLine(hx - 56 * k, hy - 4 * k,  hx - 76 * k, hy - 6 * k,  3, 0x00FFEAB4u, 0.65f);
    cvLine(hx - 52 * k, hy + 22 * k, hx - 70 * k, hy + 30 * k, 3, 0x00FFEAB4u, 0.55f);

    cvFinish(&g_sprites[SPR_WRENCH_STRIKE]);
}

static void BakeWrench(void) {
    SpriteBakeCanvasState* canvas=SpriteBakeCanvas();
    cvClear();
    float c = CANVAS_W * 0.5f;

    /* 整体轴向：从右下 45° 伸向左上（画面里读作"被握着"） */
    float ax0 = c + 44, ay0 = 246;      /* 握把末端（画面外） */
    float ax1 = c - 26, ay1 = 96;       /* 头颈 */
    float dx = ax1 - ax0, dy = ay1 - ay0;
    float L = sqrtf(dx * dx + dy * dy);
    float ux = dx / L, uy = dy / L;      /* 轴向单位向量 */
    float vx = -uy, vy = ux;             /* 法向 */

    /* ---- 手柄：外描边 + 主体 + 受光面 ---- */
    cvLine(ax0 + 5, ay0 + 5, ax1 + 5, ay1 + 5, 34, 0x00080A0Eu, 1.0f);
    cvLine(ax0, ay0, ax1, ay1, 28, 0x00484036u, 1.0f);
    /* 受光面：沿法向偏移一条细带 */
    cvLine(ax0 + vx * 6, ay0 + vy * 6, ax1 + vx * 6, ay1 + vy * 6, 8, 0x006E6456u, 1.0f);
    /* 阴影面 */
    cvLine(ax0 - vx * 7, ay0 - vy * 7, ax1 - vx * 7, ay1 - vy * 7, 6, 0x00241E18u, 1.0f);

    /* 缠带：四道，垂直轴向 */
    for (int i = 0; i < 4; i++) {
        float t = 0.42f + (float)i * 0.13f;
        float bx = ax0 + dx * t, by = ay0 + dy * t;
        cvLine(bx + vx * 16, by + vy * 16, bx - vx * 16, by - vy * 16, 7, 0x00201814u, 1.0f);
        cvLine(bx + vx * 14 + ux * 2, by + vy * 14 + uy * 2,
               bx - vx * 14 + ux * 2, by - vy * 14 + uy * 2, 4, 0x00463428u, 1.0f);
    }

    /* ---- 头部：管钳的开口钳口，开口朝左上 ---- */
    float hx = ax1, hy = ay1;
    /* 外轮廓（闭合多边形，先在 2× 画布上） */
    float head[20] = {
        hx - 30, hy - 46,      /* 上颚外角 */
        hx +  34, hy - 40,     /* 上颚根部 */
        hx +  44, hy - 12,     /* 右侧 */
        hx +  30, hy + 22,     /* 下颚根部 */
        hx -  26, hy + 26,     /* 下颚外角 */
        hx -  40, hy + 2,      /* 左侧 */
        hx -  34, hy - 22,     /* 左上 */
        hx - 30, hy - 34,      /* v3.0：上颚内凹（原来这点和末点重复） */
        hx - 34, hy - 6,
        hx - 38, hy - 26
    };
    /* 先把外框放大 8% 当描边 */
    float outline[20];
    for (int i = 0; i < 10; i++) {
        outline[i * 2] = hx + (head[i * 2] - hx) * 1.10f;
        outline[i * 2 + 1] = hy + (head[i * 2 + 1] - hy) * 1.10f;
    }
    cvPoly(outline, 10, 0x00080A0Eu, 1.0f);
    cvPolyGrad(head, 10, 0x00B4AC9Cu, 0x005A5348u, 1.0f);

    /* 开口：把嘴巴挖成背景色（用画布擦除） */
    {
        float mouth[8] = {
            hx - 40, hy - 30,
            hx - 4,  hy - 26,
            hx - 6,  hy + 10,
            hx - 38, hy + 8
        };
        /* 用 alpha 擦出开口：直接写 0 */
        int y0 = (int)hy - 34, y1 = (int)hy + 14;
        for (int y = y0; y <= y1; y++) {
            if (y < 0 || y >= CANVAS_H) continue;
            for (int x = (int)hx - 44; x <= (int)hx; x++) {
                if (x < 0 || x >= CANVAS_W) continue;
                float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
                /* 点在张开的多边形内？ */
                int inside = 0;
                for (int i = 0; i < 4; i++) {
                    int j = (i + 1) % 4;
                    float ay = mouth[i * 2 + 1], by = mouth[j * 2 + 1];
                    if ((ay <= fy && by > fy) || (by <= fy && ay > fy)) {
                        float t = (fy - ay) / (by - ay);
                        if (fx < mouth[i * 2] + t * (mouth[j * 2] - mouth[i * 2])) inside ^= 1;
                    }
                }
                if (inside) canvas->pixels[(size_t)y * CANVAS_W + x] = 0;
            }
        }
    }

    /* 上下颚的齿：只画在开口边缘，短而粗 */
    for (int i = 0; i < 3; i++) {
        float tx = hx - 36 + (float)i * 10.0f;
        cvLine(tx, hy - 30, tx + 2, hy - 20, 4, 0x00D8D0C0u, 1.0f);   /* 上齿 */
        cvLine(tx, hy + 8, tx + 2, hy - 2, 4, 0x00C0B8A8u, 1.0f);     /* 下齿 */
    }

    /* 调节螺母：头部右侧一个六角 */
    {
        float nx = hx + 26, ny = hy + 2;
        cvEllipse(nx, ny, 12.0f, 12.0f, 0x00080A0Eu, 1.0f);
        cvEllipse(nx, ny, 9.0f, 9.0f, 0x00908878u, 1.0f);
        cvEllipse(nx - 2, ny - 2, 3.4f, 3.4f, 0x00302A22u, 1.0f);
    }

    /* 锈斑 */
    cvSpeckle((int)hx, (int)hy + 4, 30, 90, 0x00703018u, 0.5f);
    cvSpeckle((int)(ax0 + dx * 0.55f), (int)(ay0 + dy * 0.55f), 22, 60, 0x00602810u, 0.4f);

    /* 摇柄：头部下侧伸出的曲柄（"crank" 的由来） */
    {
        float kx = hx + 10, ky = hy + 30;
        cvEllipse(kx, ky, 8.0f, 8.0f, 0x00080A0Eu, 1.0f);
        cvEllipse(kx, ky, 5.6f, 5.6f, 0x007A7262u, 1.0f);
        cvLine(kx, ky + 6, kx + 4, ky + 26, 6, 0x00080A0Eu, 1.0f);
        cvLine(kx, ky + 6, kx + 4, ky + 26, 4, 0x00665E50u, 1.0f);
    }

    cvFinish(&g_sprites[SPR_WRENCH]);
}

void SpritesInit(void) {
    if (g_ready) return;
    for (int i = 0; i < SPR_COUNT; i++) {
        g_sprites[i].w = SPR_W;
        g_sprites[i].h = SPR_H;
        g_sprites[i].px = (unsigned int*)malloc(sizeof(unsigned int) * SPR_W * SPR_H);
        g_sprites[i].shade = (unsigned int*)malloc(sizeof(unsigned int) * SPR_SHADES);
        if (!g_sprites[i].px || !g_sprites[i].shade) { g_ready = 0; return; }
        memset(g_sprites[i].px, 0, sizeof(unsigned int) * SPR_W * SPR_H);
    }
    BakeProtocolProps();
    BakeWrenchStrike();
    BakeWrench();
    /* 2026-11-05：BakePistol() / BakeSigil() 已删除 */
    /* v2.4.82：六个槽位，一一对应，不再有"复用空闲槽位"这种事 */
    BakeItem(SPR_ITEM_FLARE, 0);   /* 光球 */
    BakeItem(SPR_ITEM_SLING, 8);   /* 投石（石弹） */
    BakeItem(SPR_ITEM_SMOKE, 2);   /* 烟雾 */
    BakeItem(SPR_ITEM_GLUE,  6);   /* 胶水 */
    BakeItem(SPR_ITEM_ECHO,  9);   /* 回声（音叉） */
    BakeItem(SPR_ITEM_DECOY, 5);
    for(int tool=0;tool<7;tool++)BakeTacticalTool(SPR_TOOL_BASE+tool,tool);   /* 玩偶 */
    for (int p = 0; p < 4; p++) {
        BakeMonster(SPR_MON_HUNTER + p, 0, p);
    /* 听声鬼变体：第二只「匍匐者」、第三只「长颈」，各 4 帧 */
    BakeMonster(SPR_MON_HUNTER2 + p, 11, p);
    BakeMonster(SPR_MON_HUNTER3 + p, 12, p);
        BakeMonster(SPR_MON_LIGHT  + p, 1, p);
        BakeMonster(SPR_MON_LEECH  + p, 7, p);   /* v2.4.1: 槽位复用 -> 拾荒者 */
    }
    BakeMonster(SPR_MON_JUMPER, 3, 0);
    BakeMonster(SPR_MON_SHADE, 8, 0);   /* v2.4.1: 槽位复用 -> 清道夫 */
    BakeMonster(SPR_MON_SPIDER, 9, 0);   /* v2.4.1: 槽位复用 -> 守门人（三眼） */
    BakeMonster(SPR_MON_WARDEN_CHASE, 9, 2);   /* v2.4.81: 守门人 **追击变体**（红眼直接烘，不用 key） */
    BakeMonster(SPR_MON_ECHO, 10, 0);
    BakeMonster(SPR_MON_PARASITE, 14, 0);
    BakeMonster(SPR_MON_MIMIC, 13, 0);   /* 2026-11-05 宝箱怪现形态 */
    /* v2.4.84 调试导出 */
    if (getenv("CANITY_DUMP")) {
        /* 2026-11-16：把两把扳手都导出来，肉眼/像素比对"出招变体"是否真的不同 */
        SpriteDumpPPM(SPR_WRENCH,        "dump_wrench_idle.ppm");
        SpriteDumpPPM(SPR_WRENCH_STRIKE, "dump_wrench_strike.ppm");
        SpriteDumpPPM(SPR_MON_SPIDER,  "dump_warden_patrol.ppm");
        SpriteDumpPPM(SPR_MON_WARDEN_CHASE, "dump_warden_chase.ppm");
        exit(0);   /* 导完直接退出，方便脚本调用 */
    }   /* v2.4.2: 槽位复用 -> 回声标记 */
    BakeHatch(SPR_HATCH_0, 0);
    BakeHatch(SPR_HATCH_1, 1);
    BakeHatch(SPR_HATCH_2, 2);
    BakeHatch(SPR_HATCH_3, 3);
    BakeProp(SPR_PROP_CRATE, 0);
    BakeProp(SPR_PROP_BREAKER, 1);
    BakeProp(SPR_PROP_EXIT, 2);
    BakeProp(SPR_PROP_BREAKER_ON, 17);
    BakeProp(SPR_PROP_EXIT_OPEN, 18);
    BakeProp(SPR_PROP_SAFE, 3);
    BakeProp(SPR_PROP_CRATE_OPEN, 4);
    BakeProp(SPR_PROP_CRATE_LOCKED, 5);
    BakeProp(SPR_PROP_TRASH, 6);
    BakeProp(SPR_PROP_CRATE_MIMIC, 7);   /* 2026-11-05 宝箱怪伪装态 */
    BakeProp(SPR_PROP_SHELF, 8);          /* v2.4-rc 房间家具 */
    BakeProp(SPR_PROP_CONSOLE, 9);
    BakeProp(SPR_PROP_SHELF_FRAME, 10);   /* v2.4-rc P3 分层 */
    BakeProp(SPR_PROP_SHELF_BOARD, 11);
    BakeProp(SPR_PROP_CONSOLE_BASE, 12);
    BakeProp(SPR_PROP_CONSOLE_PANEL, 13);
    BakeProp(SPR_PROP_PHASE, 14);          /* 2026-11-06 相位信标：地面 */
    BakeProp(SPR_PHASE_HAND, 15);          /* 2026-11-06 相位信标：手持 */
    BakeProp(SPR_PROP_SPIDER, 16);         /* 2026-11-06 缚丝蛛 */
    for(int i=0;i<7;i++)BakeRoomFixture(SPR_ROOM_BOILER+i,i); /* Legacy save visuals. */
    for(int theme=0;theme<7;theme++) {
        for(int state=0;state<3;state++)BakeRoomLandmark(SPR_ROOM_V2_BASE+theme*3+state,theme,state);
        for(int phase=1;phase<3;phase++)BakeRoomLandmarkMotion(SPR_ROOM_MOTION_BASE+theme*2+phase-1,theme,phase);
        BakeRoomPortal(SPR_ROOM_V2_PORTAL_BASE+theme,theme);
        for(int side=0;side<2;side++) {
            BakeRoomControl(SPR_ROOM_V2_CONTROL_BASE+theme*2+side,theme,side);
            for(int state=0;state<2;state++)
                BakeRoomControlState(SPR_ROOM_CONTROL_READY_BASE+state*14+theme*2+side,theme,side,state+1);
        }
        BakeThemeProp(SPR_THEME_HIDE_BASE+theme,theme,0,0);
        for(int state=0;state<3;state++)BakeThemeProp(SPR_THEME_CRATE_BASE+theme*3+state,theme,1,state);
        for(int state=0;state<2;state++)BakeThemeProp(SPR_THEME_BREAKER_BASE+theme*2+state,theme,2,state);
    }
    BakeUiPanel(SPR_UI_PANEL, 0);
    BakeUiPanel(SPR_UI_PANEL_HOT, 1);
    BakeUiCursor();
    BakeUiRow();
    g_ready = 1;
    /* ==================================================================
     * **舱门样张导出必须放在所有烘焙之后**
     * ------------------------------------------------------------------
     * 我第一次把它放在 SpritesInit 开头 —— 那是烘之前，
     * 导出来 4 张全透明的图（我看了样张才发现，测试断言反而没抓到，
     * 因为 test_runtime 走的是另一条烘焙完成后的路径）。
     * ================================================================ */
    {
        const char* hd = getenv("CANITY_HATCH_DUMP");
        if (hd && hd[0]) {
            char hp[MAX_PATH];
            for (int f = 0; f < 4; f++) {
                snprintf(hp, sizeof(hp), "%s/hatch_%d.ppm", hd, f);
                SpriteDumpPPM(SPR_HATCH_0 + f, hp);
            }
        }
    }
}

Sprite* SpriteGet(int id) {
    if (!g_ready || id < 0 || id >= SPR_COUNT) return NULL;
    return &g_sprites[id];
}

int SpritesReady(void) { return g_ready; }

/* ------------------------------------------------------------------ */
/* 运行时 blit：双线性 + 亮度查表 + alpha + 深度测试                      */
/* ===== v2.4.84 **调试：把烘焙好的精灵写成 PPM** =====
   设置环境变量 CANITY_DUMP=1 启动，会把守门人两个变体写到 exe 同目录。
   这样能直接看到烘焙结果，不用靠游戏截图猜。 */
void SpriteDumpPPM(int id, const char* path) {
    Sprite* sp = &g_sprites[id];
    if (!sp->px || sp->w <= 0 || sp->h <= 0) return;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", sp->w, sp->h);
    for (int y = 0; y < sp->h; y++) {
        for (int x = 0; x < sp->w; x++) {
            unsigned int c = sp->px[y * sp->w + x];
            int a = (c >> 24) & 0xFF;
            int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            /* 透明处用棋盘格，方便看剪影 */
            if (a < 8) {
                int chk = ((x / 8) + (y / 8)) & 1;
                r = g = b = chk ? 90 : 130;
            }
            unsigned char px[3] = { (unsigned char)r, (unsigned char)g, (unsigned char)b };
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */

/* ===== v2.4.66 **局部换色（palette swap）** =====
   调研结论（像素画的标准做法）：整体叠加色调是最差的做法 ——
   它会把整只怪的明暗关系洗掉，观感立刻变廉价。
   正确做法是「调色板映射」：只把**特定源色**换成目标色，其余不动。

   实现：烘焙时用**洋红 0xFF00FF 当 key** 画那些要换色的部件
   （拾荒者的麻袋、守门人的眼睛和提灯），
   绘制时如果像素接近洋红，就换成 accentCol，其余像素完全不动。

   这样：身体保持原本的三层暗灰，只有"那个部件"变色。 */
static int g_accentOn = 0;
static unsigned int g_accentCol = 0x00FFFFFFu;

void SpriteAccentSet(unsigned int col) {
    g_accentOn = 1;
    g_accentCol = col & 0x00FFFFFFu;
}
void SpriteAccentClear(void) {
    g_accentOn = 0;
}

static void DrawSpriteSurface(int id, int x, int y, int w, int h,
                    float light, float alpha, int zTest, float zDist,
                    unsigned int tint, float tintMix,const SpritePose* pose,int mirror) {
    if (!g_ready || id<0 || id>=SPR_COUNT || w <= 0 || h <= 0) return;
    if (alpha <= 0.003f) return;
    Sprite* s = &g_sprites[id];

    int li = (int)(light * (float)(SPR_SHADES - 1) + 0.5f);
    if (li < 0) li = 0;
    if (li >= SPR_SHADES) li = SPR_SHADES - 1;
    unsigned int lum = s->shade[li];
    int lr = (lum >> 16) & 0xFF;      /* 0..255 的亮度系数 */

    float clock=pose?pose->time:0,stride=pose?pose->stride:0,energy=pose?pose->activity:0;
    if(!isfinite(clock))clock=0;
    if(!isfinite(stride))stride=0;
    if(!isfinite(energy))energy=0;
    if(energy<0)energy=0;
    if(energy>1)energy=1;
    float breath=pose?sinf(clock*1.65f):0;
    int rigid=id==SPR_MON_SPIDER || id==SPR_MON_WARDEN_CHASE;
    int insect=id==SPR_PROP_SPIDER;
    int beginY=y<0?-y:0,endY=h;
    if(endY>renderH-y)endY=renderH-y;
    int beginX=x<0?-x:0,endX=w;
    if(endX>renderW-x)endX=renderW-x;
    for (int dy = beginY; dy < endY; dy++) {
        int py = y + dy;
        if (py < 0 || py >= renderH) continue;
        float nv=((float)dy+.5f)/(float)h;
        float bend=0,spread=1,vshift=0;
        if(pose){
            float chest=1-fabsf(nv-(insect?.82f:.43f))*(insect?8:3.6f);if(chest<0)chest=0;
            float head=nv<.29f?(1-nv/.29f):0;
            float feet=nv>.65f?(nv-.65f)/.35f:0;
            float rigidK=rigid?.28f:1;
            spread+=breath*.014f*chest*rigidK;
            bend=(sinf(clock*.78f)*.007f+sinf(clock*2.9f)*.0015f)*head*rigidK;
            bend+=sinf(stride*3.0f)*.008f*feet*energy;
            vshift=breath*.003f*sinf(nv*3.14159265f)*rigidK;
            if(insect)bend+=sinf(clock*5.5f+nv*14)*.008f*feet;
            if(id==SPR_MON_PARASITE){bend+=sinf(clock*3.0f+nv*16)*.009f;spread+=sinf(clock*3.0f-nv*19)*.015f;}
            if(id==SPR_MON_MIMIC && nv<.62f)spread+=sinf(clock*1.8f)*.012f+EvolutionPoseSignal(id)*.10f;
            if((id==SPR_MON_LIGHT || id==SPR_MON_LIGHT_W) && nv>.25f && nv<.65f)spread+=EvolutionPoseSignal(id)*.08f;
            if(insect)spread+=EvolutionPoseSignal(id)*.06f;
            if(id==SPR_MON_ECHO)bend+=sinf(clock*2.4f+nv*11)*.008f;
        }
        float v=(nv+vshift)*(float)s->h-.5f;
        int ty0 = (int)floorf(v);
        float fy = v - (float)ty0;
        int ty1 = ty0 + 1;
        if(ty1<s->by0 || ty0>s->by1)continue;
        if (ty0 < 0) ty0 = 0;
        if (ty1 < 0) ty1 = 0;
        if (ty0 > s->h - 1) ty0 = s->h - 1;
        if (ty1 > s->h - 1) ty1 = s->h - 1;
        const unsigned int* row0 = s->px + (size_t)ty0 * s->w;
        const unsigned int* row1 = s->px + (size_t)ty1 * s->w;
        for (int dx = beginX; dx < endX; dx++) {
            int px = x + dx;
            if (px < 0 || px >= renderW) continue;
            if (zTest && zDist >= zBuffer[px]) continue;
            float nu=(((float)dx+.5f)/(float)w-.5f-bend)/spread+.5f;
            if(nu<0 || nu>1)continue;
            if(mirror)nu=1-nu;
            float u=nu*(float)s->w-.5f;
            int tx0 = (int)floorf(u);
            float fx = u - (float)tx0;
            int tx1 = tx0 + 1;
            if (tx0 < 0) tx0 = 0;
            if (tx1 < 0) tx1 = 0;
            if (tx0 > s->w - 1) tx0 = s->w - 1;
            if (tx1 > s->w - 1) tx1 = s->w - 1;
            unsigned int p00 = row0[tx0], p10 = row0[tx1];
            unsigned int p01 = row1[tx0], p11 = row1[tx1];
            float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy);
            float w01 = (1 - fx) * fy, w11 = fx * fy;
            float a = ((p00 >> 24) * w00 + (p10 >> 24) * w10 +
                       (p01 >> 24) * w01 + (p11 >> 24) * w11) / 255.0f;
            if (a < 0.004f) continue;
            float a00=(float)(p00>>24)*w00,a10=(float)(p10>>24)*w10;
            float a01=(float)(p01>>24)*w01,a11=(float)(p11>>24)*w11;
            float weight=a00+a10+a01+a11;if(weight<.5f)continue;
            float inv=1.0f/weight;
            int r=(int)((((p00>>16)&255)*a00+((p10>>16)&255)*a10+((p01>>16)&255)*a01+((p11>>16)&255)*a11)*inv);
            int g=(int)((((p00>>8)&255)*a00+((p10>>8)&255)*a10+((p01>>8)&255)*a01+((p11>>8)&255)*a11)*inv);
            int b=(int)(((p00&255)*a00+(p10&255)*a10+(p01&255)*a01+(p11&255)*a11)*inv);
            if (g_accentOn && r > 150 && b > 150 && g < 110) {
                r = (int)((g_accentCol >> 16) & 0xFF);
                g = (int)((g_accentCol >> 8) & 0xFF);
                b = (int)(g_accentCol & 0xFF);
            }
            /* 亮度查表（一次乘法替代三次浮点除） */
            r = (r * lr) >> 8;
            g = (g * lr) >> 8;
            b = (b * lr) >> 8;
            if (tintMix > 0.001f) {
                int tr = (tint >> 16) & 0xFF, tg = (tint >> 8) & 0xFF, tb = tint & 0xFF;
                r = r + (int)((tr - r) * tintMix);
                g = g + (int)((tg - g) * tintMix);
                b = b + (int)((tb - b) * tintMix);
            }
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            PutPixelBlend(px, py, (unsigned int)((r << 16) | (g << 8) | b),
                          a * alpha);
        }
    }
}
void DrawSpriteTint(int id,int x,int y,int w,int h,float light,float alpha,int zTest,float zDist,unsigned int tint,float tintMix){
    DrawSpriteSurface(id,x,y,w,h,light,alpha,zTest,zDist,tint,tintMix,NULL,0);
}
void DrawSpriteCreature(int id,int x,int y,int w,int h,float light,float alpha,int zTest,float zDist,unsigned int tint,float tintMix,const SpritePose* pose){
    DrawSpriteSurface(id,x,y,w,h,light,alpha,zTest,zDist,tint,tintMix,pose,pose?pose->mirror:0);
}


/* ==================================================================
 * 2026-11-16：**带旋转的精灵绘制**（只给第一人称武器用）
 * ------------------------------------------------------------------
 * 为什么加：DrawSprite 只有平移和缩放，没有角度 —— 扳手挥击时模型
 * 始终保持同一朝向，只能左右挪，看起来像"手抖"而不是"挥出去"。
 *
 * 参数：
 *   angle   弧度；正 = 顺时针（屏幕上往右下甩）
 *   pivotX/pivotY  旋转支点（0..1 归一化，相对绘制框），用握把做支点
 * 实现：对绘制框逐像素反算回源图坐标（先减支点、反向旋转、再加回来），
 *       双线性采样，和 DrawSpriteSurface 一样的亮度/alpha 处理。
 * ================================================================ */
void DrawSpriteRot(int id, int x, int y, int w, int h,
                   float light, float alpha, float angle,
                   float pivotX, float pivotY) {
    if (!g_ready || w <= 0 || h <= 0 || alpha <= 0.003f) return;
    if (id < 0 || id >= SPR_COUNT) return;
    Sprite* s = &g_sprites[id];
    if (!s->px || s->w <= 0 || s->h <= 0) return;

    int li = (int)(light * (float)(SPR_SHADES - 1) + 0.5f);
    if (li < 0) li = 0;
    if (li >= SPR_SHADES) li = SPR_SHADES - 1;
    int lr = (s->shade[li] >> 16) & 0xFF;

    float ca = cosf(angle), sa = sinf(angle);

    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < 0 || py >= renderH) continue;
        for (int i = 0; i < w; i++) {
            int pxi = x + i;
            if (pxi < 0 || pxi >= renderW) continue;

            /* 绘制框内归一化坐标（以支点为原点） */
            float u = ((float)i + 0.5f) / (float)w - pivotX;
            float v = ((float)j + 0.5f) / (float)h - pivotY;
            /* 反向旋转，回到未旋转的源图位置 */
            float su = u * ca + v * sa;
            float sv = -u * sa + v * ca;
            float fx = (su + pivotX) * (float)s->w - 0.5f;
            float fy = (sv + pivotY) * (float)s->h - 0.5f;
            if (fx < 0.0f || fy < 0.0f || fx >= (float)(s->w - 1) || fy >= (float)(s->h - 1)) continue;

            int x0 = (int)fx, y0 = (int)fy;
            float tx = fx - (float)x0, ty = fy - (float)y0;
            unsigned int p00 = s->px[(size_t)y0 * s->w + x0];
            unsigned int p10 = s->px[(size_t)y0 * s->w + x0 + 1];
            unsigned int p01 = s->px[(size_t)(y0 + 1) * s->w + x0];
            unsigned int p11 = s->px[(size_t)(y0 + 1) * s->w + x0 + 1];
            float w00 = (1.0f - tx) * (1.0f - ty), w10 = tx * (1.0f - ty);
            float w01 = (1.0f - tx) * ty,          w11 = tx * ty;
            float a = (((p00 >> 24) & 0xFF) * w00 + ((p10 >> 24) & 0xFF) * w10 +
                       ((p01 >> 24) & 0xFF) * w01 + ((p11 >> 24) & 0xFF) * w11) / 255.0f;
            if (a < 0.004f) continue;
            int r = (int)((((p00 >> 16) & 0xFF) * w00 + ((p10 >> 16) & 0xFF) * w10 +
                           ((p01 >> 16) & 0xFF) * w01 + ((p11 >> 16) & 0xFF) * w11) * lr) >> 8;
            int g = (int)((((p00 >> 8) & 0xFF) * w00 + ((p10 >> 8) & 0xFF) * w10 +
                           ((p01 >> 8) & 0xFF) * w01 + ((p11 >> 8) & 0xFF) * w11) * lr) >> 8;
            int b = (int)(((p00 & 0xFF) * w00 + (p10 & 0xFF) * w10 +
                           (p01 & 0xFF) * w01 + (p11 & 0xFF) * w11) * lr) >> 8;
            PutPixelBlend(pxi, py, (unsigned int)((r << 16) | (g << 8) | b), a * alpha);
        }
    }
}

void DrawSprite(int id, int x, int y, int w, int h,
                float light, float alpha, int zTest, float zDist) {
    DrawSpriteTint(id, x, y, w, h, light, alpha, zTest, zDist, 0, 0.0f);
}

/* 子矩形 blit（9-slice / 图标裁切） */
void DrawSpriteSub(int id, int sx, int sy, int sw, int sh,
                   int dx, int dy, int dw, int dh, float light, float alpha) {
    if (!g_ready || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    if (alpha <= 0.003f) return;
    Sprite* s = &g_sprites[id];
    int li = (int)(light * (float)(SPR_SHADES - 1) + 0.5f);
    if (li < 0) li = 0;
    if (li >= SPR_SHADES) li = SPR_SHADES - 1;
    int lr = (s->shade[li] >> 16) & 0xFF;

    int x0 = sx < 0 ? 0 : sx;
    int y0 = sy < 0 ? 0 : sy;
    int x1 = sx + sw; if (x1 > s->w) x1 = s->w;
    int y1 = sy + sh; if (y1 > s->h) y1 = s->h;
    if (x1 <= x0 || y1 <= y0) return;

    for (int j = 0; j < dh; j++) {
        int py = dy + j;
        if (py < 0 || py >= renderH) continue;
        int srcY = y0 + (j * sh) / dh;
        if (srcY >= s->h) srcY = s->h - 1;
        for (int i = 0; i < dw; i++) {
            int px = dx + i;
            if (px < 0 || px >= renderW) continue;
            int srcX = x0 + (i * sw) / dw;
            if (srcX >= s->w) srcX = s->w - 1;
            unsigned int p = s->px[(size_t)srcY * s->w + srcX];
            float a = (float)(p >> 24) / 255.0f;
            if (a < 0.004f) continue;
            int r = (int)(((p >> 16) & 0xFF) * lr) >> 8;
            int g = (int)(((p >> 8) & 0xFF) * lr) >> 8;
            int b = (int)((p & 0xFF) * lr) >> 8;
            PutPixelBlend(px, py, (unsigned int)((r << 16) | (g << 8) | b), a * alpha);
        }
    }
}

/* ==================================================================
 * 带深度测试 + **双线性采样**的子矩形 blit（世界道具用）
 * ------------------------------------------------------------------
 * 2026-11-06（第二轮）：第一版用的是**最近邻**采样。
 * 结果 1 像素的黑描边被放大成 3~4 像素的粗黑框 ——
 * 用户截图里的「箱子边框都露出来了」就是它。
 *
 * 烘焙图是 128x128，贴脸时要放大到 400px 以上；
 * 最近邻在这种倍率下会把所有细线（描边、铁角、木纹）都变成色块。
 * 所以这里和 DrawSpriteTint 用**同一套双线性**，
 * 只是把采样范围限制在艺术包围盒内 —— 既不糊边，也不画透明区。
 * ================================================================ */
void DrawSpriteSubZ(int id, int sx, int sy, int sw, int sh,
                    int dx, int dy, int dw, int dh,
                    float light, float alpha, float zDist) {
    if (!g_ready || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    if (alpha <= 0.003f) return;
    Sprite* s = &g_sprites[id];

    int li = (int)(light * (float)(SPR_SHADES - 1) + 0.5f);
    if (li < 0) li = 0;
    if (li >= SPR_SHADES) li = SPR_SHADES - 1;
    int lr = (s->shade[li] >> 16) & 0xFF;

    /* 裁剪到精灵内部 */
    int x0 = sx < 0 ? 0 : sx;
    int y0 = sy < 0 ? 0 : sy;
    int x1 = sx + sw; if (x1 > s->w) x1 = s->w;
    int y1 = sy + sh; if (y1 > s->h) y1 = s->h;
    if (x1 <= x0 || y1 <= y0) return;

    for (int j = 0; j < dh; j++) {
        int py = dy + j;
        if (py < 0 || py >= renderH) continue;

        /* 目标行 -> 源行（双线性，钳在 [y0, y1-1]） */
        float v = (float)y0 + (((float)j + 0.5f) / (float)dh) * (float)(y1 - y0) - 0.5f;
        int ty0 = (int)floorf(v);
        float fy = v - (float)ty0;
        int ty1 = ty0 + 1;
        if (ty0 < y0) ty0 = y0;
        if (ty0 > y1 - 1) ty0 = y1 - 1;
        if (ty1 < y0) ty1 = y0;
        if (ty1 > y1 - 1) ty1 = y1 - 1;

        const unsigned int* row0 = s->px + (size_t)ty0 * s->w;
        const unsigned int* row1 = s->px + (size_t)ty1 * s->w;

        for (int i = 0; i < dw; i++) {
            int px = dx + i;
            if (px < 0 || px >= renderW) continue;
            if (zDist >= zBuffer[px]) continue;      /* 深度测试先做，省掉后面的采样 */

            float u = (float)x0 + (((float)i + 0.5f) / (float)dw) * (float)(x1 - x0) - 0.5f;
            int tx0 = (int)floorf(u);
            float fx = u - (float)tx0;
            int tx1 = tx0 + 1;
            if (tx0 < x0) tx0 = x0;
            if (tx0 > x1 - 1) tx0 = x1 - 1;
            if (tx1 < x0) tx1 = x0;
            if (tx1 > x1 - 1) tx1 = x1 - 1;

            unsigned int p00 = row0[tx0], p10 = row0[tx1];
            unsigned int p01 = row1[tx0], p11 = row1[tx1];
            float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy);
            float w01 = (1 - fx) * fy,         w11 = fx * fy;

            float a = ((float)(p00 >> 24) * w00 + (float)(p10 >> 24) * w10 +
                       (float)(p01 >> 24) * w01 + (float)(p11 >> 24) * w11) / 255.0f;
            if (a < 0.004f) continue;

            /* 注意：**颜色要先按 alpha 加权再归一化**，
               否则半透明边缘会被黑色拉低 —— 那正是细描边发黑的来源。 */
            float aw = ((float)(p00 >> 24) * w00 + (float)(p10 >> 24) * w10 +
                        (float)(p01 >> 24) * w01 + (float)(p11 >> 24) * w11);
            if (aw < 0.5f) continue;
            int r = (int)((((p00 >> 16) & 0xFF) * (float)(p00 >> 24) * w00 +
                           ((p10 >> 16) & 0xFF) * (float)(p10 >> 24) * w10 +
                           ((p01 >> 16) & 0xFF) * (float)(p01 >> 24) * w01 +
                           ((p11 >> 16) & 0xFF) * (float)(p11 >> 24) * w11) / aw);
            int g = (int)((((p00 >> 8) & 0xFF) * (float)(p00 >> 24) * w00 +
                           ((p10 >> 8) & 0xFF) * (float)(p10 >> 24) * w10 +
                           ((p01 >> 8) & 0xFF) * (float)(p01 >> 24) * w01 +
                           ((p11 >> 8) & 0xFF) * (float)(p11 >> 24) * w11) / aw);
            int b = (int)(((p00 & 0xFF) * (float)(p00 >> 24) * w00 +
                           (p10 & 0xFF) * (float)(p10 >> 24) * w10 +
                           (p01 & 0xFF) * (float)(p01 >> 24) * w01 +
                           (p11 & 0xFF) * (float)(p11 >> 24) * w11) / aw);

            /* 2026-11-06：**局部换色**（七主题共用一套舱门靠它）。
               判据与 DrawSpriteTint 完全一致：只替换接近洋红 key 的像素，
               其余（金属、门板、阴影）保持原样。 */
            if (g_accentOn && r > 150 && b > 150 && g < 110) {
                r = (int)((g_accentCol >> 16) & 0xFF);
                g = (int)((g_accentCol >> 8) & 0xFF);
                b = (int)(g_accentCol & 0xFF);
            }
            r = (r * lr) >> 8; g = (g * lr) >> 8; b = (b * lr) >> 8;
            PutPixelBlend(px, py, (unsigned int)((r << 16) | (g << 8) | b), a * alpha);
        }
    }
}

/* 水平镜像 blit */
void DrawSpriteFlip(int id,int x,int y,int w,int h,float light,float alpha,int zTest,float zDist){
    DrawSpriteSurface(id,x,y,w,h,light,alpha,zTest,zDist,0,0,NULL,1);
}

void SpritesSetEvolutionTier(int tier) {
    if (tier < 0 || tier > 3)
        tier = 0;
    if (tier == evolutionBakeTier)
        return;
    evolutionBakeTier = tier;
    if (!g_ready)
        return;
    for (int p = 0; p < 4; p++) {
        BakeMonster(SPR_MON_HUNTER + p, 0, p);
        BakeMonster(SPR_MON_HUNTER2 + p, 11, p);
        BakeMonster(SPR_MON_HUNTER3 + p, 12, p);
        BakeMonster(SPR_MON_LIGHT + p, 1, p);
        BakeMonster(SPR_MON_LEECH + p, 7, p);
    }
    BakeMonster(SPR_MON_SHADE, 8, 0);
    BakeMonster(SPR_MON_SPIDER, 9, 0);
    BakeMonster(SPR_MON_WARDEN_CHASE, 9, 2);
    BakeMonster(SPR_MON_MIMIC, 13, 0);
    BakeProp(SPR_PROP_SPIDER, 16);
}

/* Reference creatures already carry their own metal/cloth finish. Do not
 * turn the entire silhouette into woven doll fabric in the final raster pass. */
int SpriteBakeReferenceModel(int id) {
    return (id>=SPR_MON_HUNTER && id<=SPR_MON_LEECH_SW) ||
        (id>=SPR_MON_HUNTER2 && id<=SPR_MON_HUNTER3_SW) ||
        id==SPR_MON_SHADE || id==SPR_MON_SPIDER || id==SPR_MON_WARDEN_CHASE ||
        id==SPR_MON_MIMIC || id==SPR_PROP_SPIDER ||
        (id>=SPR_GUIDE_CACHE_BASE && id<=SPR_GUIDE_CACHE_LAST);
}

/* Three lazy cache slots serve the triple-hunter spread. Preview bakes only
 * these targets and restores the world bake tier without rebaking live actors. */
int SpritesEvolutionPortrait(int family,int variant,int tier) {
    static int keys[3]={-1,-1,-1};
    static const int kinds[7]={0,1,16,7,8,9,13};
    if(family<0 || family>=7 || tier<0 || tier>3)return -1;
    if(!g_ready)SpritesInit();
    if(!g_ready)return -1;
    int slot=family==0 && variant>=0 && variant<3?variant:0;
    int key=family*12+slot*4+tier,id=SPR_GUIDE_CACHE_BASE+slot;
    if(keys[slot]!=key) {
        int previous=evolutionBakeTier;
        evolutionBakeTier=tier;
        int kind=family==0?(slot==0?0:slot==1?11:12):kinds[family];
        if(kind==16)BakeProp(id,kind);else BakeMonster(id,kind,0);
        evolutionBakeTier=previous;
        keys[slot]=key;
    }
    return id;
}

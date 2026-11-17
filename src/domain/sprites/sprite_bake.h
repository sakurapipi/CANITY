/* Private baking contract: startup only; not a gameplay/render API. */
#ifndef CANITY_SPRITE_BAKE_H
#define CANITY_SPRITE_BAKE_H
#include "sprites.h"
#define SUP 2
#define CANVAS_W (SPR_W * SUP)
#define CANVAS_H (SPR_H * SUP)
typedef struct {
    int scale;
    unsigned int pixels[CANVAS_W * CANVAS_H * SPR_CREATURE_SCALE * SPR_CREATURE_SCALE];
} SpriteBakeCanvasState;
SpriteBakeCanvasState* SpriteBakeCanvas(void);
#define RASTER_W (CANVAS_W * SpriteBakeCanvas()->scale)
#define RASTER_H (CANVAS_H * SpriteBakeCanvas()->scale)
Sprite* SpriteBakeTarget(int id);
int SpriteBakeIndex(const Sprite* sprite);
void cvClear(void);
void cvPx(int x,int y,unsigned int col,float a);
void cvPoly(const float* pts, int n, unsigned int col, float a);
void cvEllipse(float cx, float cy, float rx, float ry, unsigned int col, float a);
void cvLine(float x0, float y0, float x1, float y1, float wid,
                   unsigned int col, float a);
void cvPolyGrad(const float* pts, int n, unsigned int top, unsigned int bot, float a);
unsigned int SpriteBakeRandom(void);
void cvSpeckle(int cx, int cy, int radius, int count, unsigned int col, float a);
void cvGlow(float cx, float cy, float r, unsigned int col, float a);
void cvFinish(Sprite* s);
void BakeItem(int id, int kind);
void BakeMonster(int id, int kind, int pose);
void BakeHatch(int id, int frame);
void BakeProp(int id, int kind);
void BakeUiPanel(int id, int hot);
void BakeUiCursor(void);
void BakeUiRow(void);
void BakeRoomFixture(int id,int kind);
void BakeRoomLandmark(int id,int theme,int state);
void BakeRoomLandmarkMotion(int id,int theme,int phase);
void BakeRoomPortal(int id,int theme);
void BakeRoomControl(int id,int theme,int side);
void BakeRoomControlState(int id,int theme,int side,int state);
void BakeThemeProp(int id,int material,int kind,int state);
void BakeMisprintFringe(int kind);
void BakeCreatureSurface(int kind);
#endif

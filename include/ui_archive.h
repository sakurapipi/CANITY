#ifndef CANITY_UI_ARCHIVE_H
#define CANITY_UI_ARCHIVE_H
#include "common.h"
enum { ARCHIVE_MONSTERS=0, ARCHIVE_ROOMS=1 };
int UiArchiveTab(void);
void UiArchiveOpen(void);
void UiArchiveSetTab(int tab);
int UiArchiveEntryCount(void);
int UiArchiveSelection(void);
void UiArchiveSelect(int entry);
MenuGeo UiArchiveGeometry(void);
RECT UiArchiveTabRect(MenuGeo geometry,int tab);
int UiArchiveTabAt(int x,int y);
void UiArchiveScrollDetail(int lines);
int UiArchiveDetailButtonAt(int x,int y);
void UiDrawArchivePage(MenuGeo geometry);
/* Shared dossier presentation also used by shop, bank and upgrade pages. */
int UiDossierText(const char* text,int x,int y,int width,int bottom,unsigned int color);
void UiDossierPortrait(int spriteId,RECT box,int identified);
#endif

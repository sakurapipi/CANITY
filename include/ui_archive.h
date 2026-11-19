#ifndef CANITY_UI_ARCHIVE_H
#define CANITY_UI_ARCHIVE_H
#include "common.h"
enum { ARCHIVE_MONSTERS=0, ARCHIVE_ROOMS=1, ARCHIVE_EVENTS=2, ARCHIVE_TAB_COUNT };
int UiArchiveTab(void);
void UiArchiveOpen(void);
void UiArchiveSetTab(int tab);
int UiArchiveEntryCount(void);
int UiArchiveSelection(void);
void UiArchiveSelect(int entry);
MenuGeo UiArchiveGeometry(void);
RECT UiArchiveTabRect(MenuGeo geometry,int tab);
int UiArchiveTabAt(int x,int y);
int UiArchiveRowAt(int x,int y);
int UiArchiveInDetail(int x);
void UiArchiveScrollDetail(int lines);
int UiArchiveDetailButtonAt(int x,int y);
void UiDrawArchivePage(MenuGeo geometry);
int UiArchiveTierKey(int key);
int UiArchiveTierClick(int x,int y);
/* Shared dossier presentation also used by shop, bank and upgrade pages. */
int UiDossierText(const char* text,int x,int y,int width,int bottom,unsigned int color);
void UiDossierPortrait(int spriteId,RECT box,int identified);
#endif

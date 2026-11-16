/* 2.4.6-RC backend regression with in-memory platform/Steam doubles.
 * Run by tools/build_validate.py using the real persistence backend.
 * gcc -Iinclude -std=c99 -Wall -Wextra tests/test_save_cloud_backend.c
 *     src/save/save_backend.c src/save/save_container.c -o cloud_cases.exe
 * This links the real save backend, not the real Steam client or player saves.
 */
#include "common.h"
#include "save.h"
#include "save_format.h"
#include "plat.h"
#include "steam_zone.h"
#include <assert.h>

int g_saveState, g_saveError;
char g_savePath[MAX_PATH];
typedef struct { char path[MAX_PATH+64]; unsigned char data[512]; int len; } File;
static File files[64];
static int count, cloudLen, localLen, seenCapacity, failPrimary, failAll, failCopy;
static int verdict;
static unsigned char cloud[512], local[512];

static File* find(const char* path) {
    for(int i=0;i<count;i++)if(!strcmp(files[i].path,path))return &files[i];
    return NULL;
}
static File* put(const char* path,const void* bytes,int len) {
    assert(len>=0 && len<=512);
    File* file=find(path);
    if(!file){assert(count<64);file=&files[count++];snprintf(file->path,sizeof(file->path),"%s",path);}
    memcpy(file->data,bytes,(size_t)len);file->len=len;return file;
}
static int envelope(unsigned char* out,int payload,unsigned seed,unsigned stamp) {
    for(int i=0;i<payload;i++)out[sizeof(SaveEnvelope)+i]=(unsigned char)(seed+(unsigned)i);
    SaveEnvelope header;SaveEnvelopeSeal(&header,out+sizeof(header),payload,stamp);
    memcpy(out,&header,sizeof(header));return (int)sizeof(header)+payload;
}
static void reset(int withLocal) {
    memset(files,0,sizeof(files));count=0;
    g_saveState=g_saveError=seenCapacity=failPrimary=failAll=failCopy=0;g_savePath[0]=0;
    localLen=envelope(local,16,1,1000);cloudLen=envelope(cloud,80,7,2000);
    verdict=STEAM_SYNC_CLOUDWIN;
    if(withLocal)put("slot0.sav",local,localLen);
}
void SaveDiag(const char* fmt, ...) { (void)fmt; }
const char* SavePathForSlot(int i,int slot,char* out,size_t cap) {
    if(slot){out[0]=0;return out;}
    if(!i)snprintf(out,cap,"slot0.sav");else snprintf(out,cap,"fallback%d.sav",i);
    return out;
}
int PlatPathExists(const char* path) { return find(path)!=NULL; }
double PlatFileTime(const char* path) { return find(path)?1.0:0.0; }
int PlatReadFilePath(const char* path,void* out,int cap) {
    File* file=find(path);if(!file || cap<file->len)return -1;
    memcpy(out,file->data,(size_t)file->len);return file->len;
}
int PlatWriteFileAtomic(const char* path,const void* bytes,int len) {
    if(failAll || (failPrimary && !strcmp(path,"slot0.sav")))return 5;
    put(path,bytes,len);return 0;
}
int PlatCopyPath(const char* from,const char* to) {
    File* file=find(from);if(failCopy || !file)return -1;
    unsigned char copy[512];int len=file->len;memcpy(copy,file->data,(size_t)len);
    put(to,copy,len);return 0;
}
int SteamZoneReady(void) { return 1; }
int SteamZoneCloudExists(const char* name) { return !strcmp(name,"slot0.sav"); }
int SteamZoneCloudGetSize(const char* name) { (void)name;return cloudLen; }
int SteamZoneCloudUpload(const char* name,const void* bytes,int len) {
    (void)name;(void)bytes;(void)len;return 1;
}
int SteamZoneCloudPullNew(const char* name,void* out,int cap,int* len) {
    (void)name;if(cap<cloudLen)return 0;
    memcpy(out,cloud,(size_t)cloudLen);*len=cloudLen;return 1;
}
int SteamZoneCloudSyncEx(const char* name,void* out,int bytes,int* capacity,
                         void** conflict,int* conflictLen) {
    (void)name;assert(bytes==localLen);seenCapacity=*capacity;
    *conflict=NULL;*conflictLen=0;
    if(verdict==STEAM_SYNC_CONFLICT || *capacity<cloudLen){
        *conflict=malloc((size_t)cloudLen);assert(*conflict);
        memcpy(*conflict,cloud,(size_t)cloudLen);*conflictLen=cloudLen;
        return STEAM_SYNC_CONFLICT;
    }
    memcpy(out,cloud,(size_t)cloudLen);*capacity=cloudLen;return verdict;
}

static void test_larger_valid_cloud_uses_real_capacity(void) {
    reset(1);SaveBackendSyncAll();
    assert(seenCapacity==1024*1024);
    File* file=find("slot0.sav");assert(file && file->len==cloudLen);
    assert(!memcmp(file->data,cloud,(size_t)cloudLen));
    File* backup=find("slot0.sav.pre-cloud.bak");
    assert(backup && backup->len==localLen && !memcmp(backup->data,local,(size_t)localLen));
    assert(g_saveState!=2);
}
static void test_first_restore_reports_failed_writes(void) {
    reset(0);failAll=1;SaveBackendSyncAll();
    assert(g_saveState==2 && g_saveError==5 && count==0);
}
static void test_first_restore_can_use_writable_fallback(void) {
    reset(0);failPrimary=1;SaveBackendSyncAll();
    assert(!find("slot0.sav"));
    File* file=find("fallback1.sav");assert(file && file->len==cloudLen);
    assert(!strcmp(g_savePath,"fallback1.sav") && g_saveState!=2);
}
static void test_failed_backup_never_overwrites_primary(void) {
    reset(1);failCopy=1;SaveBackendSyncAll();
    File* file=find("slot0.sav");assert(file && file->len==localLen);
    assert(!memcmp(file->data,local,(size_t)localLen));
    assert(find("fallback1.sav"));
}
static void test_conflict_keeps_actual_cloud_and_local(void) {
    reset(1);verdict=STEAM_SYNC_CONFLICT;SaveBackendSyncAll();
    File* primary=find("slot0.sav"),*copy=find("slot0.sav.conflict-cloud.bak");
    assert(primary && primary->len==localLen && !memcmp(primary->data,local,(size_t)localLen));
    assert(copy && copy->len==cloudLen && !memcmp(copy->data,cloud,(size_t)cloudLen));
    assert(g_saveState==2 && g_saveError==SAVE_ERR_CLOUD_CONFLICT);
    unsigned char first[512];int firstLen=copy->len;memcpy(first,copy->data,(size_t)firstLen);
    cloudLen=envelope(cloud,90,19,2000);int before=count;SaveBackendSyncAll();
    copy=find("slot0.sav.conflict-cloud.bak");
    assert(copy && copy->len==firstLen && !memcmp(copy->data,first,(size_t)firstLen));
    assert(count==before+1);
    File* later=&files[count-1];assert(later->len==cloudLen && !memcmp(later->data,cloud,(size_t)cloudLen));
}
static void test_corrupt_cloud_does_not_replace_local(void) {
    reset(1);cloud[sizeof(SaveEnvelope)+1]^=0x80;SaveBackendSyncAll();
    assert(g_saveState==2 && g_saveError==SAVE_ERR_PAYLOAD);
    File* file=find("slot0.sav");assert(file && file->len==localLen && !memcmp(file->data,local,(size_t)localLen));
}
int main(void) {
    test_larger_valid_cloud_uses_real_capacity();
    test_first_restore_reports_failed_writes();
    test_first_restore_can_use_writable_fallback();
    test_failed_backup_never_overwrites_primary();
    test_conflict_keeps_actual_cloud_and_local();
    test_corrupt_cloud_does_not_replace_local();
    puts("cloud backend cases passed");return 0;
}

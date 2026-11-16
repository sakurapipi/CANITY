/* 2.4.6 stable cloud/recovery regression cases.
 * Links real persistence/validation/transaction code with in-memory OS/Steam I/O.
 */
#include "common.h"
#include "save.h"
#include "save_format.h"
#include "plat.h"
#include "steam_zone.h"
#include <assert.h>
#include <setjmp.h>
void SaveBackendTestResetCloudCache(void);
int g_saveState, g_saveError;
char g_savePath[MAX_PATH];
#define FIXTURE_CAP SAVE_MAX_FILE_BYTES
typedef struct { char path[MAX_PATH+96]; unsigned char* data; int len, active; double stamp; } File;
static File files[128];
static int count, cloudLen, localLen, seenCapacity, failPrimary, failAll, failSnapshot;
static int verdict, uploads, syncCalls, deleteCrash, failBackupDelete;
static int failMarker, partialRestore, snapshotAttempts;
static unsigned char cloud[FIXTURE_CAP], local[FIXTURE_CAP];
static jmp_buf powerLoss;
static File* find(const char* path) {
    for(int i=0;i<count;i++)if(files[i].active && !strcmp(files[i].path,path))return &files[i];
    return NULL;
}
static File* put(const char* path,const void* bytes,int len) {
    assert(len>=0 && (unsigned)len<=SAVE_MAX_FILE_BYTES);
    unsigned char* copy=malloc((size_t)len+1);assert(copy);memcpy(copy,bytes,(size_t)len);
    File* file=find(path);
    if(!file){assert(count<128);file=&files[count++];memset(file,0,sizeof(*file));snprintf(file->path,sizeof(file->path),"%s",path);}
    free(file->data);file->data=copy;file->len=len;file->active=1;file->stamp=1;return file;
}
static int envelope(unsigned char* out,int unknownBytes,unsigned seed,unsigned stamp) {
    SaveBlob* blob=calloc(1,sizeof(*blob));assert(blob);
    blob->magic=SAVE_MAGIC;blob->version=SAVE_VERSION;blob->hasRun=1;blob->level=(int)seed;
    assert(unknownBytes>=0 && (unsigned)unknownBytes < SAVE_MAX_FILE_BYTES-sizeof(SaveBlob)-128);
    unsigned char* unknown=malloc(unknownBytes?(size_t)unknownBytes:1);assert(unknown);
    memset(unknown,(int)seed,(size_t)unknownBytes);
    SaveChunkDesc chunks[2]={{SC_LEGACY29,1,sizeof(SaveBlob)},{0x7777,1,(unsigned)unknownBytes}};
    const void* bodies[2]={blob,unknown};
    int payload=SaveContainerPack(chunks,bodies,unknownBytes?2:1,out+sizeof(SaveEnvelope),
                                 (int)FIXTURE_CAP-(int)sizeof(SaveEnvelope),NULL);
    assert(payload>0);SaveEnvelope header;
    SaveEnvelopeSeal(&header,out+sizeof(header),payload,stamp);memcpy(out,&header,sizeof(header));
    free(unknown);free(blob);return (int)sizeof(header)+payload;
}
static void reset(int withLocal) {
    for(int i=0;i<count;i++)free(files[i].data);
    memset(files,0,sizeof(files));count=0;
    g_saveState=g_saveError=seenCapacity=failPrimary=failAll=failSnapshot=0;
    uploads=syncCalls=deleteCrash=failBackupDelete=0;
    failMarker=partialRestore=snapshotAttempts=0;g_savePath[0]=0;
    SaveBackendTestResetCloudCache();
    localLen=envelope(local,0,1,1000);cloudLen=envelope(cloud,80,7,2000);
    verdict=STEAM_SYNC_CLOUDWIN;
    if(withLocal)put("canity0.sav",local,localLen);
}
void SaveDiag(const char* fmt, ...) { (void)fmt; }
const char* SavePathForSlot(int i,int slot,char* out,size_t cap) {
    if(slot){out[0]=0;return out;}
    if(!i)snprintf(out,cap,"canity0.sav");else snprintf(out,cap,"fallback%d/canity0.sav",i);
    return out;
}
int PlatPathExists(const char* path) { return find(path)!=NULL; }
double PlatFileTime(const char* path) { File* file=find(path);return file?file->stamp:0; }
int PlatSetFileTime(const char* path,double stamp) {
    File* file=find(path);if(!file)return 2;file->stamp=stamp;return 0;
}
int PlatReadFilePath(const char* path,void* out,int cap) {
    File* file=find(path);if(!file || cap<file->len)return -1;
    memcpy(out,file->data,(size_t)file->len);return file->len;
}
int PlatWriteFileAtomic(const char* path,const void* bytes,int len) {
    if(failAll || (failMarker && strstr(path,".cloud-block")) ||
       (failPrimary && !strcmp(path,"canity0.sav")))return 5;
    put(path,bytes,len);return 0;
}
int PlatWriteFileExclusive(const char* path,const void* bytes,int len) {
    if(find(path))return 80;
    snapshotAttempts++;
    if(partialRestore && !strcmp(path,"canity0.sav")) { put(path,bytes,len/2); return 5; }
    if(failAll || (failSnapshot && strstr(path,"pre-cloud")))return 5;
    put(path,bytes,len);return 0;
}
int PlatDeletePath(const char* path) {
    if(failBackupDelete && !strcmp(path,"canity0.sav.bak"))return -32;
    File* file=find(path);if(!file)return 1;
    free(file->data);file->data=NULL;file->active=0;
    if(deleteCrash && !strcmp(path,"canity0.sav")){deleteCrash=0;longjmp(powerLoss,1);}
    return 0;
}
int PlatCopyPath(const char* from,const char* to) {
    File* file=find(from);if(!file)return -1;put(to,file->data,file->len);return 0;
}
int SteamZoneReady(void) { return 1; }
int SteamZoneCloudExists(const char* name) { return !strcmp(name,"canity0.sav"); }
int SteamZoneCloudGetSize(const char* name) { (void)name;return cloudLen; }
int SteamZoneCloudUpload(const char* name,const void* bytes,int len) {
    (void)name;assert(len>0 && (unsigned)len<=FIXTURE_CAP);
    memcpy(cloud,bytes,(size_t)len);cloudLen=len;uploads++;return 1;
}
int SteamZoneCloudPullNew(const char* name,void* out,int cap,int* len) {
    (void)name;if(cap<cloudLen)return 0;
    memcpy(out,cloud,(size_t)cloudLen);*len=cloudLen;return 1;
}
int SteamZoneCloudInspect(const char* name,const void* bytes,int length,void** remote,int* remoteLength) {
    (void)name;syncCalls++;
    *remote=malloc((size_t)cloudLen);assert(*remote);
    memcpy(*remote,cloud,(size_t)cloudLen);*remoteLength=cloudLen;
    if(length==cloudLen && !memcmp(bytes,cloud,(size_t)length))return STEAM_SYNC_SAMECL;
    return verdict;
}
int SteamZoneCloudSyncEx(const char* name,void* out,int bytes,int* capacity,
                         void** conflict,int* conflictLen) {
    (void)name;assert(bytes==localLen);seenCapacity=*capacity;syncCalls++;
    *conflict=NULL;*conflictLen=0;
    if(verdict==STEAM_SYNC_CONFLICT || *capacity<cloudLen){
        *conflict=malloc((size_t)cloudLen);assert(*conflict);
        memcpy(*conflict,cloud,(size_t)cloudLen);*conflictLen=cloudLen;return STEAM_SYNC_CONFLICT;
    }
    memcpy(out,cloud,(size_t)cloudLen);*capacity=cloudLen;return verdict;
}
static void original_survives(void) {
    File* file=find("canity0.sav");assert(file && file->len==localLen);
    assert(!memcmp(file->data,local,(size_t)localLen));
}
static void test_larger_valid_cloud_uses_real_capacity(void) {
    reset(1);assert(SaveValidateBytes(local,localLen));assert(SaveValidateBytes(cloud,cloudLen));
    SaveBackendSyncAll();original_survives();
    assert(SaveBackendCloudBlocked(0));
    assert(SaveBackendResolveCloud(0,SAVE_CLOUD_USE_REMOTE)==0);
    File* file=find("canity0.sav");assert(file && file->len==cloudLen);
    assert(!memcmp(file->data,cloud,(size_t)cloudLen));
    File* backup=find("canity0.sav.pre-cloud.bak");
    assert(backup && backup->len==localLen && !memcmp(backup->data,local,(size_t)localLen));
}
static void test_restore_failure_and_fallback(void) {
    reset(0);failAll=1;SaveBackendSyncAll();assert(g_saveState==2 && g_saveError==5);
    reset(0);failPrimary=1;SaveBackendSyncAll();assert(!find("canity0.sav"));
    assert(find("fallback1/canity0.sav") && !strcmp(g_savePath,"fallback1/canity0.sav"));
}
static void test_backup_failure_does_not_activate_fallback(void) {
    reset(1);failSnapshot=1;SaveBackendSyncAll();original_survives();
    assert(SaveBackendResolveCloud(0,SAVE_CLOUD_USE_REMOTE)==SAVE_ERR_CLOUD_BACKUP);
    original_survives();
    assert(!find("fallback1/canity0.sav"));assert(SaveBackendCloudBlocked(0));
    assert(find("canity0.sav.conflict-cloud.bak"));
}
static void test_persistent_conflict_blocks_normal_upload(void) {
    reset(1);verdict=STEAM_SYNC_CONFLICT;SaveBackendSyncAll();original_survives();
    File* copy=find("canity0.sav.conflict-cloud.bak");assert(copy && copy->len==cloudLen);
    assert(!memcmp(copy->data,cloud,(size_t)cloudLen));
    SaveBackendTestResetCloudCache();assert(SaveBackendCloudBlocked(0));
    assert(SaveBackendWrite("canity0.sav",local,localLen)==0);
    SaveBackendUpload("canity0.sav");assert(uploads==0);
    int before=syncCalls;SaveBackendSyncAll();assert(syncCalls==before);
    assert(SaveBackendResolveCloud(0,SAVE_CLOUD_USE_LOCAL)==0);
    assert(!SaveBackendCloudBlocked(0) && uploads==1);
}
static void test_invalid_internal_chunk_is_not_installed(void) {
    reset(1);
    /* Corrupt only the inner blob magic, then reseal a valid outer envelope. */
    const void* blob=NULL;int bytes=0;
    assert(SaveContainerFind(cloud+sizeof(SaveEnvelope),cloudLen-sizeof(SaveEnvelope),SC_LEGACY29,&blob,&bytes));
    memset((void*)blob,0,4);SaveEnvelope header;
    SaveEnvelopeSeal(&header,cloud+sizeof(header),cloudLen-sizeof(header),2000);
    memcpy(cloud,&header,sizeof(header));assert(!SaveValidateBytes(cloud,cloudLen));
    SaveBackendSyncAll();original_survives();assert(g_saveError==SAVE_ERR_PAYLOAD);
}
static void test_partial_delete_rolls_back_and_restart_recovers(void) {
    reset(1);put("canity0.sav.bak",local,localLen);failBackupDelete=1;
    assert(SaveBackendDeleteSlot(0)!=0);original_survives();
    assert(find("canity0.sav.delete-journal"));failBackupDelete=0;
    assert(SaveBackendRecoverDelete(0)==0);original_survives();
    assert(!find("canity0.sav.delete-journal"));
}
static void test_power_loss_before_commit_restores_original(void) {
    reset(1);put("canity0.sav.bak",local,localLen);deleteCrash=1;
    /* longjmp models process termination: the process-private allocation is
       deliberately abandoned; durable fake files survive for restart recovery. */
    if(!setjmp(powerLoss)){(void)SaveBackendDeleteSlot(0);assert(0);}
    assert(!find("canity0.sav") && find("canity0.sav.delete-journal"));
    assert(SaveBackendRecoverDelete(0)==0);original_survives();
}
static void test_committed_delete_does_not_restore_from_cloud(void) {
    reset(1);assert(SaveBackendDeleteSlot(0)==0);assert(!find("canity0.sav"));
    SaveBackendTestResetCloudCache();SaveBackendSyncAll();
    assert(!find("canity0.sav") && SaveBackendCloudBlocked(0));
}
static void test_valid_outer_invalid_directory_is_rejected(void) {
    for(int kind=0;kind<4;kind++) {
        reset(1);unsigned char* payload=cloud+sizeof(SaveEnvelope);
        if(kind==0){int count=65;memcpy(payload+4,&count,4);}
        else {
            SaveChunkDesc desc;memcpy(&desc,payload+SAVE_PAYLOAD_HEAD_SIZE,sizeof(desc));
            if(kind==1)desc.id=0x5555; /* No legacy block. */
            if(kind==2)desc.schema=2; /* Unsupported legacy schema. */
            if(kind==3)desc.id=0x7777; /* Duplicate unknown ID. */
            memcpy(payload+SAVE_PAYLOAD_HEAD_SIZE,&desc,sizeof(desc));
        }
        SaveEnvelope header;SaveEnvelopeSeal(&header,payload,cloudLen-sizeof(header),2000);
        memcpy(cloud,&header,sizeof(header));
        assert(!SaveValidateBytes(cloud,cloudLen));SaveBackendSyncAll();original_survives();
        assert(g_saveError==SAVE_ERR_PAYLOAD);
    }
}
static void test_first_restore_above_one_megabyte(void) {
    reset(0);cloudLen=envelope(cloud,1024*1024,9,2000);
    assert(SaveValidateBytes(cloud,cloudLen));SaveBackendSyncAll();
    File* file=find("canity0.sav");assert(file && file->len==cloudLen);
    assert(g_saveState!=2);
}
static void test_precloud_history_is_immutable(void) {
    reset(1);SaveBackendSyncAll();
    assert(SaveBackendResolveCloud(0,SAVE_CLOUD_USE_REMOTE)==0);
    File* first=find("canity0.sav.pre-cloud.bak");assert(first);
    int firstLen=first->len;unsigned char* original=malloc((size_t)firstLen);assert(original);
    memcpy(original,first->data,(size_t)firstLen);
    localLen=cloudLen;memcpy(local,cloud,(size_t)localLen);
    cloudLen=envelope(cloud,120,11,3000);SaveBackendSyncAll();
    assert(SaveBackendResolveCloud(0,SAVE_CLOUD_USE_REMOTE)==0);
    first=find("canity0.sav.pre-cloud.bak");
    assert(first && first->len==firstLen && !memcmp(first->data,original,(size_t)firstLen));
    int versions=0;for(int i=0;i<count;i++)if(files[i].active && strstr(files[i].path,"pre-cloud"))versions++;
    assert(versions>=2);free(original);
}
static void crash_with_pending_journal(void) {
    deleteCrash=1;
    if(!setjmp(powerLoss)){(void)SaveBackendDeleteSlot(0);assert(0);}
    assert(find("canity0.sav.delete-journal"));
}
static void test_conflicting_restore_keeps_journal(void) {
    reset(1);crash_with_pending_journal();
    unsigned char* different=malloc((size_t)localLen);assert(different);
    memcpy(different,local,(size_t)localLen);different[localLen-1]^=1;
    put("canity0.sav",different,localLen);
    assert(SaveBackendRecoverDelete(0)==SAVE_ERR_DELETE_CONFLICT);
    File* file=find("canity0.sav");assert(!memcmp(file->data,different,(size_t)localLen));
    assert(find("canity0.sav.delete-journal"));free(different);
}
static void test_partial_restore_is_not_success_on_retry(void) {
    reset(1);crash_with_pending_journal();partialRestore=1;
    assert(SaveBackendRecoverDelete(0)!=0);partialRestore=0;
    assert(SaveBackendRecoverDelete(0)==SAVE_ERR_DELETE_CONFLICT);
    assert(find("canity0.sav.delete-journal"));
}
static void test_marker_failure_still_attempts_snapshot(void) {
    reset(1);failMarker=1;SaveBackendSyncAll();
    assert(g_saveState==2 && snapshotAttempts>0);
    assert(!find("canity0.sav.cloud-block") && find("canity0.sav.conflict-cloud.bak"));
}
static void test_all_writes_fail_then_restart_requires_choice(void) {
    reset(1);failAll=1;SaveBackendSyncAll();
    assert(snapshotAttempts>0 && uploads==0);
    failAll=0;localLen=envelope(local,0,17,9000);put("canity0.sav",local,localLen);
    SaveBackendTestResetCloudCache();verdict=STEAM_SYNC_LOCALWIN;
    SaveBackendUpload("canity0.sav");assert(uploads==0);
    SaveBackendSyncAll();original_survives();assert(uploads==0 && SaveBackendCloudBlocked(0));
    assert(SaveBackendResolveCloud(0,SAVE_CLOUD_USE_LOCAL)==0 && uploads==1);
}
static void test_same_session_upload_checks_remote_again(void) {
    reset(1);cloudLen=localLen;memcpy(cloud,local,(size_t)localLen);
    SaveBackendSyncAll();assert(!SaveBackendCloudBlocked(0));
    localLen=envelope(local,0,21,4000);put("canity0.sav",local,localLen);
    SaveBackendUpload("canity0.sav");assert(uploads==1);
    cloudLen=envelope(cloud,80,33,5000); /* Another device diverged. */
    SaveBackendUpload("canity0.sav");assert(uploads==1 && SaveBackendCloudBlocked(0));
}
int main(void) {
    test_larger_valid_cloud_uses_real_capacity();test_restore_failure_and_fallback();
    test_backup_failure_does_not_activate_fallback();test_persistent_conflict_blocks_normal_upload();
    test_invalid_internal_chunk_is_not_installed();test_partial_delete_rolls_back_and_restart_recovers();
    test_power_loss_before_commit_restores_original();test_committed_delete_does_not_restore_from_cloud();
    test_valid_outer_invalid_directory_is_rejected();test_first_restore_above_one_megabyte();
    test_precloud_history_is_immutable();
    test_conflicting_restore_keeps_journal();test_partial_restore_is_not_success_on_retry();
    test_marker_failure_still_attempts_snapshot();test_all_writes_fail_then_restart_requires_choice();
    test_same_session_upload_checks_remote_again();
    puts("2.4.6 cloud and deletion scenarios passed");return 0;
}

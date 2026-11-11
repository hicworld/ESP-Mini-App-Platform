/* Execute production storage code with controlled filesystem/NVS failures.
 * Verification is deliberately stubbed: cryptography has separate tests. */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum fault { NONE, OPEN, WRITE, FLUSH, SYNC, CLOSE, READBACK_OTHER,
             NVS_SET, COMMIT_OLD, COMMIT_NEW, SPACE, SPACE_INVALID, INFO, CACHE_RENAME };
static enum fault fault;
static FILE *writer;
static FILE *test_fopen(const char *,const char *);
static size_t test_fwrite(const void *,size_t,size_t,FILE *);
static size_t test_fread(void *,size_t,size_t,FILE *);
static int test_fflush(FILE *);
static int test_fsync(int);
static int test_fclose(FILE *);
static int test_rename(const char *,const char *);
#define fopen test_fopen
#define fwrite test_fwrite
#define fread test_fread
#define fflush test_fflush
#define fsync test_fsync
#define fclose test_fclose
#include "../../firmware/main/app_manager.c"
#undef fopen
#undef fwrite
#undef fread
#undef fflush
#undef fsync
#undef fclose

#define fopen test_fopen
#define fwrite test_fwrite
#define fread test_fread
#define fflush test_fflush
#define fsync test_fsync
#define fclose test_fclose
#define rename test_rename
#include "../../firmware/main/download_cache.c"
#undef fopen
#undef fwrite
#undef fread
#undef fflush
#undef fsync
#undef fclose
#undef rename

typedef struct { app_info_t app; uint32_t valid; } package_t;
static package_t old_package,new_package,other_package;
static registry_t durable,pending;
static size_t durable_size;
static bool exists,pending_exists,seeded,storage_ok=true,nand_ok=true;
static app_state_t running=APP_STOPPED;
static esp_err_t clear_result=ESP_OK;
static char cleared_id[APP_ID_LENGTH];
static unsigned tests,failures;
#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s\n",__LINE__,#condition); failures++; return; } } while (0)

static FILE *test_fopen(const char *path,const char *mode) {
    if (strchr(mode,'w') && fault==OPEN) return NULL;
    FILE *f=fopen(path,mode);
    if (strchr(mode,'w')) writer=f;
    return f;
}
static size_t test_fwrite(const void *p,size_t size,size_t n,FILE *f) {
    if (fault==WRITE) return fwrite(p,size,n/2,f);
    return fwrite(p,size,n,f);
}
static size_t test_fread(void *p,size_t size,size_t n,FILE *f) {
    size_t read=fread(p,size,n,f);
    if (fault==READBACK_OTHER && size*n==sizeof(other_package) && read==n)
        memcpy(p,&other_package,sizeof(other_package));
    return read;
}
static int test_fflush(FILE *f) { int result=fflush(f); return fault==FLUSH?-1:result; }
static int test_fsync(int fd) { int result=fsync(fd); return fault==SYNC?-1:result; }
static int test_fclose(FILE *f) {
    bool is_writer=f==writer;
    if (is_writer) writer=NULL;
    int result=fclose(f);
    return is_writer && fault==CLOSE?-1:result;
}
static int test_rename(const char *from,const char *to) {
    if (fault==CACHE_RENAME) return -1;
    return rename(from,to);
}
bool hal_storage_ready(void) { return storage_ok; }
bool nand_storage_ready(void) { return nand_ok; }
esp_err_t nand_storage_info(uint64_t *total,uint64_t *free_bytes) {
    *total=1024*1024; *free_bytes=*total-4096;
    if (fault==SPACE) *free_bytes=4096;
    if (fault==SPACE_INVALID) *free_bytes=*total+1;
    return fault==INFO?ESP_FAIL:ESP_OK;
}
app_state_t runtime_state(void) { return running; }
esp_err_t apps_verify(const uint8_t *bytes,size_t size,app_info_t *info,const uint8_t **wasm) {
    if (size!=sizeof(package_t)) return ESP_ERR_INVALID_SIZE;
    package_t package; memcpy(&package,bytes,size);
    if (package.valid!=0x12345678) return ESP_ERR_INVALID_CRC;
    *info=package.app; *wasm=bytes; return ESP_OK;
}
esp_err_t esp_spiffs_info(const char *name,size_t *total,size_t *used) {
    (void)name;
    *total=1024*1024; *used=4096;
    if (fault==SPACE) *used=*total-4096;
    if (fault==SPACE_INVALID) *used=*total+1;
    return fault==INFO?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h) {
    (void)name; (void)mode; *h=1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; pending_exists=false; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size) {
    (void)h; (void)key;
    if (!exists) return ESP_ERR_NVS_NOT_FOUND;
    if (out && *size<durable_size) return ESP_ERR_INVALID_SIZE;
    if (out) memcpy(out,&durable,durable_size);
    *size=durable_size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *p,size_t size) {
    (void)h; (void)key;
    if (fault==NVS_SET) return ESP_FAIL;
    if (size!=sizeof(pending)) return ESP_ERR_INVALID_SIZE;
    memcpy(&pending,p,size); pending_exists=true; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h;
    if (fault==COMMIT_OLD) return ESP_FAIL;
    if (pending_exists) { durable=pending; exists=true; durable_size=sizeof(durable); pending_exists=false; }
    return fault==COMMIT_NEW?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *v) {
    (void)h; (void)key; if (!seeded) return ESP_ERR_NVS_NOT_FOUND;
    *v=1; return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t v) {
    (void)h; (void)key; seeded=v!=0; return ESP_OK;
}
esp_err_t app_data_clear(const char *id) {
    snprintf(cleared_id,sizeof(cleared_id),"%s",id);
    return clear_result;
}
static package_t package(const char *id,const char *version) {
    package_t p={0}; strcpy(p.app.id,id); strcpy(p.app.name,"Test app");
    strcpy(p.app.version,version); p.app.permissions=PERM_DISPLAY;
    p.app.wasm_size=8; p.valid=0x12345678; return p;
}
static esp_err_t install(const package_t *p) { return apps_install((const uint8_t*)p,sizeof(*p)); }
static void reset(void) {
    fault=NONE; exists=false; pending_exists=false; seeded=false;
    storage_ok=true; nand_ok=true; running=APP_STOPPED;
    clear_result=ESP_OK; cleared_id[0]=0;
    unlink("/store/com.test.app.0.app"); unlink("/store/com.test.app.1.app");
    unlink("/nand/apps/com.test.app.0.app"); unlink("/nand/apps/com.test.app.1.app");
    unlink(DOWNLOAD_CACHE_PART); unlink(DOWNLOAD_CACHE_READY);
    if (apps_init()!=ESP_OK) exit(2);
}
static bool current_is(const package_t *expected) {
    app_info_t a; uint8_t *bytes=NULL; size_t size=0;
    if (apps_list(&a,1)!=1 || strcmp(a.version,expected->app.version)) return false;
    if (apps_read(&a,&bytes,&size)!=ESP_OK) return false;
    bool ok=size==sizeof(*expected) && !memcmp(bytes,expected,size);
    free(bytes); return ok;
}
static void update_failure(enum fault injected,const char *name) {
    tests++; reset(); CHECK(install(&old_package)==ESP_OK);
    fault=injected; CHECK(install(&new_package)!=ESP_OK);
    fault=NONE;
    if (injected==NVS_SET || injected==COMMIT_OLD || injected==COMMIT_NEW) {
        CHECK(install(&new_package)==ESP_ERR_INVALID_STATE);
        CHECK(apps_uninstall(old_package.app.id)==ESP_ERR_INVALID_STATE);
    } else CHECK(current_is(&old_package));
    CHECK(apps_init()==ESP_OK);
    CHECK(current_is(injected==COMMIT_NEW?&new_package:&old_package));
    CHECK(install(&new_package)==ESP_OK); CHECK(current_is(&new_package));
    printf("PASS update failure: %s\n",name);
}
static void uninstall_failure(enum fault injected) {
    tests++; reset(); CHECK(install(&old_package)==ESP_OK);
    fault=injected; CHECK(apps_uninstall(old_package.app.id)!=ESP_OK);
    fault=NONE; CHECK(apps_init()==ESP_OK);
    if (injected==COMMIT_NEW) { CHECK(apps_list(NULL,APP_LIMIT)==0); CHECK(!apps_should_seed()); }
    else CHECK(current_is(&old_package));
    CHECK(install(&new_package)==ESP_OK); CHECK(current_is(&new_package));
    puts("PASS failed uninstall preserves recoverable files");
}
static void registry_corruption(unsigned mode) {
    tests++; reset(); CHECK(install(&old_package)==ESP_OK);
    switch (mode) {
      case 0: durable.schema=99; break;
      case 1: durable.count=APP_LIMIT+1; break;
      case 2: memset(durable.entries[0].id,'a',APP_ID_LENGTH); break;
      case 3: strcpy(durable.entries[0].path,"/store/../outside"); break;
      case 4: durable.entries[1]=durable.entries[0]; durable.count=2; break;
      case 5: durable_size--; break;
      case 6: durable.entries[0].permissions=0x80; break;
      case 7: durable.entries[0].wasm_size=APP_MAX_BYTES+1; break;
      case 8: strcpy(durable.entries[0].version,"broken"); break;
      case 9: durable.entries[0].name[0]=0; break;
    }
    registry_t before=durable;
    CHECK(apps_init()==ESP_ERR_INVALID_STATE);
    CHECK(!apps_should_seed()); CHECK(apps_list(NULL,APP_LIMIT)==0);
    CHECK(install(&new_package)==ESP_ERR_INVALID_STATE);
    CHECK(!memcmp(&before,&durable,sizeof(before)));
    printf("PASS registry corruption %u rejected without reseeding\n",mode);
}
static void lifecycle(void) {
    tests++; reset(); CHECK(apps_should_seed());
    CHECK(install(&old_package)==ESP_OK); CHECK(!apps_should_seed());
    CHECK(install(&new_package)==ESP_OK); CHECK(current_is(&new_package));
    CHECK(apps_init()==ESP_OK); CHECK(current_is(&new_package));
    CHECK(apps_uninstall(old_package.app.id)==ESP_OK);
    CHECK(apps_init()==ESP_OK); CHECK(apps_list(NULL,APP_LIMIT)==0); CHECK(!apps_should_seed());
    puts("PASS update/uninstall/empty registry restart");
}
static void guards(void) {
    tests++; reset(); CHECK(install(&old_package)==ESP_OK);
    running=APP_RUNNING; CHECK(install(&new_package)==ESP_ERR_INVALID_STATE);
    CHECK(apps_uninstall(old_package.app.id)==ESP_ERR_INVALID_STATE);
    running=APP_STOPPED; storage_ok=false; nand_ok=false; CHECK(install(&new_package)==ESP_ERR_INVALID_STATE);
    storage_ok=true; nand_ok=true; package_t bad=new_package; bad.valid=0;
    CHECK(install(&bad)==ESP_ERR_INVALID_CRC); CHECK(current_is(&old_package));
    puts("PASS running/unavailable storage/bad package guards");
}
static void clear_data_guards(void) {
    tests++; reset(); CHECK(install(&old_package)==ESP_OK);
    CHECK(apps_clear_data(old_package.app.id)==ESP_ERR_NOT_SUPPORTED);
    package_t storage=new_package; storage.app.permissions|=PERM_STORAGE;
    CHECK(install(&storage)==ESP_OK);
    CHECK(apps_clear_data(storage.app.id)==ESP_OK);
    CHECK(!strcmp(cleared_id,storage.app.id));
    CHECK(apps_clear_data("com.test.missing")==ESP_ERR_NOT_FOUND);
    running=APP_RUNNING;
    CHECK(apps_clear_data(storage.app.id)==ESP_ERR_INVALID_STATE);
    running=APP_STOPPED; clear_result=ESP_FAIL;
    CHECK(apps_clear_data(storage.app.id)==ESP_FAIL);
    puts("PASS clear-data installed/permission/runtime/error guards");
}
static void migrate_legacy(void) {
    tests++; reset(); nand_ok=false; CHECK(install(&old_package)==ESP_OK);
    CHECK(access("/store/com.test.app.0.app",F_OK)==0);
    nand_ok=true; CHECK(apps_migrate_to_nand()==ESP_OK);
    CHECK(access("/store/com.test.app.0.app",F_OK)==0);
    CHECK(access("/nand/apps/com.test.app.0.app",F_OK)==0);
    unlink("/store/com.test.app.0.app"); CHECK(current_is(&old_package));
    CHECK(apps_init()==ESP_OK); CHECK(current_is(&old_package));
    puts("PASS legacy package copied to NAND with compatible registry");
}
static void migrate_repairs_invalid_copy(void) {
    tests++; reset(); nand_ok=false; CHECK(install(&old_package)==ESP_OK);
    FILE *bad=fopen("/nand/apps/com.test.app.0.app","wb"); CHECK(bad!=NULL);
    CHECK(fwrite("bad",1,3,bad)==3); CHECK(fclose(bad)==0);
    nand_ok=true; CHECK(apps_migrate_to_nand()==ESP_OK); CHECK(current_is(&old_package));
    puts("PASS migration replaces invalid NAND copy from verified NOR package");
}
static void verified_reads(unsigned mode) {
    tests++; reset(); nand_ok=false; CHECK(install(&old_package)==ESP_OK);
    app_info_t app; CHECK(apps_list(&app,1)==1);
    nand_ok=true;
    switch (mode) {
      case 0: /* Exact signed legacy package remains a valid fallback. */ break;
      case 1: strcpy(app.version,"2.0.0"); break;
      case 2: strcpy(app.id,"com.test.other"); break;
      case 3: app.permissions|=PERM_STORAGE; break;
      case 4: storage_ok=false; break;
      case 5: storage_ok=false; nand_ok=false; break;
      case 6:
      case 7: {
        package_t bad=old_package;
        if (mode==6) bad.valid=0; else strcpy(bad.app.version,"2.0.0");
        FILE *f=fopen("/nand/apps/com.test.app.0.app","wb"); CHECK(f!=NULL);
        CHECK(fwrite(&bad,1,sizeof(bad),f)==sizeof(bad)); CHECK(fclose(f)==0);
        break;
      }
      case 8: nand_ok=false; strcpy(app.version,"2.0.0"); break;
    }
    uint8_t *bytes=NULL; size_t size=99;
    esp_err_t err=apps_read(&app,&bytes,&size);
    if (mode==0) { CHECK(err==ESP_OK); CHECK(size==sizeof(old_package)); free(bytes); }
    else {
        CHECK(err==(mode==4 || mode==5?ESP_ERR_INVALID_STATE:ESP_ERR_INVALID_CRC));
        CHECK(bytes==NULL && size==0);
    }
    printf("PASS verified read/fallback case %u\n",mode);
}
static void unavailable_legacy(void) {
    tests++; reset(); nand_ok=false; CHECK(install(&old_package)==ESP_OK);
    nand_ok=true; storage_ok=false;
    CHECK(apps_migrate_to_nand()==ESP_ERR_INVALID_STATE);
    CHECK(access("/nand/apps/com.test.app.0.app",F_OK)!=0);
    CHECK(apps_check()==ESP_FAIL);
    nand_ok=false; uint64_t total=0,used=0;
    CHECK(apps_storage_info(&total,&used,NULL)==ESP_ERR_INVALID_STATE);
    puts("PASS unmounted NOR cannot migrate, read or report usable capacity");
}
static void nand_only_health(void) {
    tests++; reset(); storage_ok=false; CHECK(install(&old_package)==ESP_OK);
    CHECK(apps_migrate_to_nand()==ESP_OK); CHECK(apps_check()==ESP_OK);
    CHECK(current_is(&old_package));
    running=APP_RUNNING; CHECK(apps_check()==ESP_ERR_INVALID_STATE);
    puts("PASS NAND-only healthy registry and runtime guard");
}
static void cache_lifecycle(void) {
    tests++; reset(); download_cache_t cache={0}; uint8_t *bytes=NULL; size_t size=0;
    static const uint8_t expected[]={1,2,3,4,5,6};
    CHECK(download_cache_begin(&cache,sizeof(expected))==ESP_OK);
    CHECK(download_cache_append(&cache,expected,2)==ESP_OK);
    CHECK(download_cache_append(&cache,expected+2,sizeof(expected)-2)==ESP_OK);
    CHECK(download_cache_commit(&cache)==ESP_OK);
    CHECK(access(DOWNLOAD_CACHE_PART,F_OK)!=0 && access(DOWNLOAD_CACHE_READY,F_OK)==0);
    CHECK(download_cache_load(&bytes,&size)==ESP_OK);
    CHECK(size==sizeof(expected) && !memcmp(bytes,expected,size)); free(bytes);
    CHECK(access(DOWNLOAD_CACHE_PART,F_OK)!=0 && access(DOWNLOAD_CACHE_READY,F_OK)!=0);
    CHECK(download_cache_begin(&cache,sizeof(expected))==ESP_OK);
    CHECK(download_cache_append(&cache,expected,2)==ESP_OK);
    CHECK(download_cache_recover()==ESP_OK);
    CHECK(access(DOWNLOAD_CACHE_PART,F_OK)!=0 && access(DOWNLOAD_CACHE_READY,F_OK)!=0);
    puts("PASS download cache commit, load, cleanup and interrupted recovery");
}
static void cache_failure(enum fault injected,const char *name) {
    tests++; reset(); download_cache_t cache={0}; static const uint8_t bytes[]={1,2,3,4};
    fault=injected;
    esp_err_t err=download_cache_begin(&cache,sizeof(bytes));
    if (injected==OPEN) CHECK(err!=ESP_OK);
    else {
        CHECK(err==ESP_OK);
        err=download_cache_append(&cache,bytes,sizeof(bytes));
        if (injected!=WRITE) { CHECK(err==ESP_OK); err=download_cache_commit(&cache); }
        CHECK(err!=ESP_OK);
    }
    fault=NONE; download_cache_abort(&cache);
    CHECK(access(DOWNLOAD_CACHE_PART,F_OK)!=0 && access(DOWNLOAD_CACHE_READY,F_OK)!=0);
    printf("PASS download cache failure cleanup: %s\n",name);
}
static void garden_permissions_reload(void) {
    tests++; reset();
    package_t p=new_package; p.app.permissions|=PERM_ENVIRONMENT|PERM_INPUT|PERM_AUDIO;
    CHECK(apps_install((const uint8_t*)&p,sizeof(p))==ESP_OK);
    CHECK(apps_init()==ESP_OK);
    app_info_t list[APP_LIMIT];CHECK(apps_list(list,APP_LIMIT)==1);
    CHECK(list[0].permissions==p.app.permissions);
    CHECK(apps_check()==ESP_OK);
    puts("PASS environment/input permissions survive registry reload");
}
int main(void) {
    old_package=package("com.test.app","1.0.0");
    new_package=package("com.test.app","2.0.0");
    other_package=package("com.test.other","3.0.0");
    const char *names[]={"none","open","short write","flush","sync","close",
        "different verified package on readback","NVS set","commit retains old","commit persists new",
        "full storage","invalid usage","SPIFFS info"};
    for (enum fault f=OPEN;f<=INFO;f++) update_failure(f,names[f]);
    for (enum fault f=NVS_SET;f<=COMMIT_NEW;f++) uninstall_failure(f);
    for (unsigned i=0;i<10;i++) registry_corruption(i);
    lifecycle(); guards(); clear_data_guards(); migrate_legacy(); migrate_repairs_invalid_copy();
    garden_permissions_reload();
    for (unsigned i=0;i<9;i++) verified_reads(i);
    unavailable_legacy(); nand_only_health();
    cache_lifecycle();
    const enum fault cache_faults[]={OPEN,WRITE,FLUSH,SYNC,CLOSE,CACHE_RENAME};
    const char *cache_names[]={"open","short write/full disk","flush","sync","close","rename"};
    for (unsigned i=0;i<sizeof(cache_faults)/sizeof(cache_faults[0]);i++) cache_failure(cache_faults[i],cache_names[i]);
    printf("STORAGE HOST RESULT %u/%u (mock NVS/verifier; real transaction C and files)\n",tests-failures,tests);
    return failures?1:0;
}

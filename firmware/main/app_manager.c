#include "app_validation.h"
#include "app_data.h"
#include "nand_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "esp_spiffs.h"
#include "nvs.h"

typedef struct { uint32_t schema, count; app_info_t entries[APP_LIMIT]; } registry_t;
static registry_t registry;
static nvs_handle_t handle;
static bool registry_ready;

#define LEGACY_APP_PREFIX "/store/"
#define NAND_APP_PREFIX NAND_STORAGE_PATH "/apps/"
#define NAND_ALLOCATION_MARGIN (16 * 1024)

static esp_err_t physical_path(const char *logical,bool nand,char *output,size_t capacity) {
    if (!logical || strncmp(logical,LEGACY_APP_PREFIX,strlen(LEGACY_APP_PREFIX))) return ESP_ERR_INVALID_ARG;
    int length=snprintf(output,capacity,"%s%s",nand?NAND_APP_PREFIX:LEGACY_APP_PREFIX,
                        logical+strlen(LEGACY_APP_PREFIX));
    return length>0 && (size_t)length<capacity?ESP_OK:ESP_ERR_INVALID_SIZE;
}

static esp_err_t read_file(const char *path,uint8_t **bytes,size_t *size) {
    FILE *f=fopen(path,"rb"); if (!f) return errno==ENOENT?ESP_ERR_NOT_FOUND:ESP_FAIL;
    if (fseek(f,0,SEEK_END)) { fclose(f); return ESP_FAIL; }
    long length=ftell(f); rewind(f);
    if (length<16 || length>APP_MAX_BYTES+APP_MAX_MANIFEST+96) { fclose(f); return ESP_ERR_INVALID_SIZE; }
    uint8_t *buffer=malloc(length); if (!buffer) { fclose(f); return ESP_ERR_NO_MEM; }
    bool ok=fread(buffer,1,length,f)==(size_t)length;
    if (fclose(f)) ok=false;
    if (!ok) { free(buffer); return ESP_FAIL; }
    *bytes=buffer; *size=length; return ESP_OK;
}

static bool same_identity(const app_info_t *left,const app_info_t *right) {
    return !strcmp(left->id,right->id) && !strcmp(left->name,right->name) &&
           !strcmp(left->version,right->version) && left->permissions==right->permissions &&
           left->wasm_size==right->wasm_size;
}

static esp_err_t read_verified(const char *path,const app_info_t *expected,uint8_t **bytes,size_t *size) {
    uint8_t *data=NULL; size_t length=0; const uint8_t *wasm=NULL;
    app_info_t checked={0};
    esp_err_t err=read_file(path,&data,&length);
    if (err==ESP_OK) err=apps_verify(data,length,&checked,&wasm);
    if (err==ESP_OK && !same_identity(&checked,expected)) err=ESP_ERR_INVALID_CRC;
    if (err!=ESP_OK) { free(data); return err; }
    *bytes=data; *size=length; return ESP_OK;
}

static esp_err_t write_verified(const char *path,const uint8_t *bytes,size_t size,const app_info_t *expected) {
    FILE *f=fopen(path,"wb"); if (!f) return ESP_FAIL;
    bool ok=fwrite(bytes,1,size,f)==size;
    if (fflush(f)) ok=false;
    if (fsync(fileno(f))) ok=false;
    if (fclose(f)) ok=false;
    if (!ok) { unlink(path); return ESP_FAIL; }
    uint8_t *readback=NULL; size_t readsize=0; const uint8_t *wasm=NULL;
    esp_err_t err=read_file(path,&readback,&readsize); app_info_t checked={0};
    if (err==ESP_OK && (readsize!=size || memcmp(readback,bytes,size))) err=ESP_ERR_INVALID_CRC;
    if (err==ESP_OK) err=apps_verify(readback,readsize,&checked,&wasm);
    if (err==ESP_OK && !same_identity(&checked,expected)) err=ESP_ERR_INVALID_CRC;
    free(readback);
    if (err!=ESP_OK) unlink(path);
    return err;
}
static esp_err_t commit(const registry_t *next) {
    esp_err_t err=nvs_set_blob(handle,"registry",next,sizeof(*next));
    if (err==ESP_OK) err=nvs_commit(handle);
    if (err==ESP_OK) registry=*next;
    else registry_ready=false; /* Reload durable registry on restart before another mutation. */
    return err;
}
esp_err_t apps_init(void) {
    if (handle) { nvs_close(handle); handle=0; }
    registry_ready=false; memset(&registry,0,sizeof(registry));
    if (!apps_storage_ready()) return ESP_ERR_INVALID_STATE;
    esp_err_t err=nvs_open("mini_apps",NVS_READWRITE,&handle); if (err!=ESP_OK) return err;
    registry_t candidate={0}; size_t size=sizeof(candidate); err=nvs_get_blob(handle,"registry",&candidate,&size);
    if (err==ESP_ERR_NVS_NOT_FOUND) { registry.schema=1; registry_ready=true; return ESP_OK; }
    if (err!=ESP_OK || size!=sizeof(candidate) || candidate.schema!=1 || candidate.count>APP_LIMIT) return ESP_ERR_INVALID_STATE;
    for (unsigned i=0;i<candidate.count;i++) {
        app_info_t *a=&candidate.entries[i];
        if (!memchr(a->id,0,sizeof(a->id)) || !memchr(a->path,0,sizeof(a->path)) ||
            !memchr(a->name,0,sizeof(a->name)) || !memchr(a->version,0,sizeof(a->version)) || !valid_id(a->id)) return ESP_ERR_INVALID_STATE;
        unsigned version[3];
        if (!a->name[0] || !parse_version(a->version,version) ||
            (a->permissions&~(PERM_DISPLAY|PERM_TOUCH|PERM_STORAGE|PERM_ENVIRONMENT|PERM_INPUT|PERM_AUDIO)) ||
            a->wasm_size<8 || a->wasm_size>APP_MAX_BYTES) return ESP_ERR_INVALID_STATE;
        char p0[100],p1[100]; snprintf(p0,sizeof(p0),LEGACY_APP_PREFIX "%s.0.app",a->id); snprintf(p1,sizeof(p1),LEGACY_APP_PREFIX "%s.1.app",a->id);
        if (strcmp(a->path,p0) && strcmp(a->path,p1)) return ESP_ERR_INVALID_STATE;
        for (unsigned j=0;j<i;j++) if (!strcmp(candidate.entries[j].id,a->id)) return ESP_ERR_INVALID_STATE;
    }
    registry=candidate; registry_ready=true;
    return ESP_OK;
}
bool apps_should_seed(void) {
    if (!registry_ready) return false;
    uint8_t seeded=0;
    esp_err_t err=nvs_get_u8(handle,"seeded",&seeded);
    if (err==ESP_ERR_NVS_NOT_FOUND) {
        size_t size=0;
        /* A persisted empty registry also records an intentional uninstall. */
        return nvs_get_blob(handle,"registry",NULL,&size)==ESP_ERR_NVS_NOT_FOUND;
    }
    return err==ESP_OK && seeded==0;
}
esp_err_t apps_mark_seeded(void) {
    if (!registry_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err=nvs_set_u8(handle,"seeded",1);
    if (err==ESP_OK) err=nvs_commit(handle);
    return err;
}
size_t apps_list(app_info_t *out,size_t capacity) {
    if (!registry_ready) return 0;
    size_t n=registry.count<capacity?registry.count:capacity;
    if (out) memcpy(out,registry.entries,n*sizeof(*out));
    return n;
}
esp_err_t apps_read(const app_info_t *info,uint8_t **bytes,size_t *size) {
    if (!info || !bytes || !size) return ESP_ERR_INVALID_ARG;
    *bytes=NULL; *size=0;
    char path[128];
    esp_err_t err=physical_path(info->path,true,path,sizeof(path));
    if (err!=ESP_OK) return err;
    if (nand_storage_ready()) {
        err=read_verified(path,info,bytes,size);
        if (err!=ESP_ERR_NOT_FOUND) return err;
    }
    /* A stale NOR slot may hold a different version after NAND updates.
     * Only a mounted, signed copy matching the registry is a valid fallback. */
    if (!hal_storage_ready()) return ESP_ERR_INVALID_STATE;
    return read_verified(info->path,info,bytes,size);
}

bool apps_storage_ready(void) { return nand_storage_ready() || hal_storage_ready(); }

esp_err_t apps_storage_info(uint64_t *total,uint64_t *used,bool *using_nand) {
    if (!total || !used) return ESP_ERR_INVALID_ARG;
    if (nand_storage_ready()) {
        uint64_t free_bytes=0; esp_err_t err=nand_storage_info(total,&free_bytes);
        if (err!=ESP_OK) return err;
        if (free_bytes>*total) return ESP_ERR_INVALID_SIZE;
        *used=*total-free_bytes; if (using_nand) *using_nand=true; return ESP_OK;
    }
    if (!hal_storage_ready()) return ESP_ERR_INVALID_STATE;
    size_t legacy_total=0,legacy_used=0; esp_err_t err=esp_spiffs_info("apps",&legacy_total,&legacy_used);
    if (err!=ESP_OK) return err;
    if (legacy_used>legacy_total) return ESP_ERR_INVALID_SIZE;
    *total=legacy_total; *used=legacy_used; if (using_nand) *using_nand=false; return ESP_OK;
}

esp_err_t apps_install(const uint8_t *bytes,size_t size) {
    if (!registry_ready || !apps_storage_ready() || runtime_state()==APP_RUNNING) return ESP_ERR_INVALID_STATE;
    app_info_t app; const uint8_t *wasm;
    esp_err_t err=apps_verify(bytes,size,&app,&wasm); if (err!=ESP_OK) return err;
    registry_t next=registry; unsigned index=0;
    while (index<next.count && strcmp(next.entries[index].id,app.id)) index++;
    if (index==APP_LIMIT) return ESP_ERR_NO_MEM;
    uint64_t total=0,used=0; bool use_nand=false;
    err=apps_storage_info(&total,&used,&use_nand);
    if (err!=ESP_OK) return err;
    size_t margin=use_nand?NAND_ALLOCATION_MARGIN:4096;
    if (used>total || size+margin>total-used) return ESP_ERR_NO_MEM;
    char first[100]; snprintf(first,sizeof(first),LEGACY_APP_PREFIX "%s.0.app",app.id);
    unsigned slot=index<next.count && !strcmp(next.entries[index].path,first)?1:0;
    snprintf(app.path,sizeof(app.path),LEGACY_APP_PREFIX "%s.%u.app",app.id,slot);
    char destination[128]; err=physical_path(app.path,use_nand,destination,sizeof(destination));
    if (err!=ESP_OK) return err;
    err=write_verified(destination,bytes,size,&app); if (err!=ESP_OK) return err;
    next.entries[index]=app; if (index==next.count) next.count++;
    err=commit(&next); /* NVS reference changes only after durable, verified data. */
    /* A failed NVS commit has an uncertain durable outcome. Keep both verified
     * files so either registry version remains usable after reboot. */
    return err;
}
esp_err_t apps_uninstall(const char *id) {
    if (!registry_ready || !valid_id(id) || runtime_state()==APP_RUNNING) return ESP_ERR_INVALID_STATE;
    registry_t next=registry; unsigned i=0;
    while (i<next.count && strcmp(next.entries[i].id,id)) i++;
    if (i==next.count) return ESP_ERR_NOT_FOUND;
    for (unsigned j=i+1;j<next.count;j++) next.entries[j-1]=next.entries[j];
    memset(&next.entries[--next.count],0,sizeof(app_info_t));
    esp_err_t err=commit(&next);
    if (err==ESP_OK) {
        char logical[100],nand[128];
        for (int s=0;s<2;s++) {
            snprintf(logical,sizeof(logical),LEGACY_APP_PREFIX "%s.%d.app",id,s); unlink(logical);
            if (physical_path(logical,true,nand,sizeof(nand))==ESP_OK) unlink(nand);
        }
    }
    return err;
}

esp_err_t apps_migrate_to_nand(void) {
    if (!registry_ready || !nand_storage_ready() || runtime_state()==APP_RUNNING) return ESP_ERR_INVALID_STATE;
    unsigned copied=0,already=0,legacy_verified=0;
    for (unsigned i=0;i<registry.count;i++) {
        const app_info_t *app=&registry.entries[i]; char destination[128];
        esp_err_t err=physical_path(app->path,true,destination,sizeof(destination)); if (err!=ESP_OK) return err;
        uint8_t *source=NULL,*existing=NULL; size_t source_size=0,existing_size=0; const uint8_t *wasm=NULL;
        err=read_file(destination,&existing,&existing_size);
        if (err==ESP_OK) {
            app_info_t checked={0}; err=apps_verify(existing,existing_size,&checked,&wasm);
            if (err==ESP_OK && same_identity(&checked,app)) {
                free(existing); already++;
                if (hal_storage_ready() && read_verified(app->path,app,&source,&source_size)==ESP_OK) legacy_verified++;
                free(source); continue;
            }
        }
        free(existing);
        if (!hal_storage_ready()) return ESP_ERR_INVALID_STATE;
        err=read_file(app->path,&source,&source_size); if (err!=ESP_OK) return err;
        app_info_t checked={0}; err=apps_verify(source,source_size,&checked,&wasm);
        if (err==ESP_OK && !same_identity(&checked,app)) err=ESP_ERR_INVALID_CRC;
        if (err==ESP_OK) err=write_verified(destination,source,source_size,app);
        free(source); if (err!=ESP_OK) return err; copied++; legacy_verified++;
    }
    printf("APP MIGRATION PASS copied=%u already=%u legacy_verified=%u legacy_mounted=%d\n",
           copied,already,legacy_verified,hal_storage_ready());
    return ESP_OK;
}

esp_err_t apps_check(void) {
    if (!registry_ready || runtime_state()==APP_RUNNING) return ESP_ERR_INVALID_STATE;
    unsigned healthy=0;
    for (unsigned i=0;i<registry.count;i++) {
        uint8_t *bytes=NULL; size_t size=0;
        esp_err_t err=apps_read(&registry.entries[i],&bytes,&size);
        free(bytes);
        printf("APP CHECK %s version=%s result=0x%x\n",registry.entries[i].id,
               registry.entries[i].version,(unsigned)err);
        if (err==ESP_OK) healthy++;
    }
    printf("APP CHECK RESULT %u/%u\n",healthy,(unsigned)registry.count);
    return healthy==registry.count?ESP_OK:ESP_FAIL;
}

esp_err_t apps_clear_data(const char *id) {
    if (!registry_ready || !valid_id(id) || runtime_state()==APP_RUNNING) return ESP_ERR_INVALID_STATE;
    unsigned i=0;
    while (i<registry.count && strcmp(registry.entries[i].id,id)) i++;
    if (i==registry.count) return ESP_ERR_NOT_FOUND;
    if (!(registry.entries[i].permissions&PERM_STORAGE)) return ESP_ERR_NOT_SUPPORTED;
    return app_data_clear(id);
}

#include "platform.h"
#include "app_validation.h"
#include "cJSON.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static store_entry_t entries[APP_LIMIT];
static size_t count;
static char source[320];
#define DEFAULT_STORE_URL "https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/store-deploy/index.json"
static bool valid_url(const char *url) {
    if (!url || strncmp(url,"https://",8) || strlen(url)>=sizeof(source) || !url[8]) return false;
    for (const unsigned char *p=(const unsigned char*)url;*p;p++)
        if (*p<=32 || *p>=127 || *p=='@' || *p=='#' || *p=='?' || *p=='\\') return false;
    const char *slash=strchr(url+8,'/'); return slash && slash>url+8 && slash[1];
}
static bool field(const cJSON *obj,const char *key,char *out,size_t capacity) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    if (!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring)>=capacity) return false;
    for (const unsigned char *p=(const unsigned char*)v->valuestring;*p;p++) if (*p<32 || *p==127) return false;
    strcpy(out,v->valuestring); return true;
}
static bool unique(const cJSON *obj) {
    for (const cJSON *a=obj->child;a;a=a->next) for (const cJSON *b=a->next;b;b=b->next)
        if (a->string && b->string && !strcmp(a->string,b->string)) return false;
    return true;
}
static bool relative_file(const char *s) {
    if (!*s || *s=='/' || strstr(s,"..")) return false;
    for (;*s;s++) if (!((*s>='a'&&*s<='z') || (*s>='A'&&*s<='Z') || (*s>='0'&&*s<='9') || *s=='/' || *s=='.' || *s=='-' || *s=='_')) return false;
    return true;
}
static bool parse(const uint8_t *bytes,size_t size,store_entry_t *next,size_t *length) {
    if (memchr(bytes,0,size)) return false;
    char *json=malloc(size+1); if (!json) return false;
    memcpy(json,bytes,size); json[size]=0;
    cJSON *root=cJSON_ParseWithLengthOpts(json,size+1,NULL,true); free(json);
    if (!root) return false;
    const cJSON *version=cJSON_GetObjectItemCaseSensitive(root,"schema_version");
    const cJSON *apps=cJSON_GetObjectItemCaseSensitive(root,"apps");
    bool ok=cJSON_IsObject(root) && unique(root) && cJSON_IsNumber(version) && version->valuedouble==1 && cJSON_IsArray(apps) && cJSON_GetArraySize(apps)<=APP_LIMIT;
    size_t used=0;
    const cJSON *item;
    if (ok) cJSON_ArrayForEach(item,apps) {
        store_entry_t *e=&next[used];
        const cJSON *size_field=cJSON_GetObjectItemCaseSensitive(item,"size");
        const cJSON *permissions=cJSON_GetObjectItemCaseSensitive(item,"permissions");
        ok=cJSON_IsObject(item) && unique(item) && field(item,"id",e->app.id,sizeof(e->app.id)) &&
            field(item,"name",e->app.name,sizeof(e->app.name)) && field(item,"version",e->app.version,sizeof(e->app.version)) &&
            field(item,"file",e->file,sizeof(e->file)) && relative_file(e->file) &&
            field(item,"sha256",e->sha256,sizeof(e->sha256)) && strlen(e->sha256)==64 &&
            cJSON_IsNumber(size_field) && size_field->valuedouble>=16 && size_field->valuedouble<=APP_MAX_BYTES+APP_MAX_MANIFEST+96 &&
            size_field->valuedouble==(size_t)size_field->valuedouble && cJSON_IsArray(permissions);
        if (!ok) break;
        for (unsigned j=0;j<64;j++) if (!((e->sha256[j]>='0'&&e->sha256[j]<='9') || (e->sha256[j]>='a'&&e->sha256[j]<='f'))) ok=false;
        e->size=(size_t)size_field->valuedouble;
        const cJSON *p; cJSON_ArrayForEach(p,permissions) {
            unsigned bit=0;
            if (cJSON_IsString(p)) {
                if (!strcmp(p->valuestring,"display")) bit=PERM_DISPLAY;
                else if (!strcmp(p->valuestring,"touch")) bit=PERM_TOUCH;
                else if (!strcmp(p->valuestring,"storage")) bit=PERM_STORAGE;
                else if (!strcmp(p->valuestring,"environment")) bit=PERM_ENVIRONMENT;
                else if (!strcmp(p->valuestring,"input")) bit=PERM_INPUT;
                else if (!strcmp(p->valuestring,"audio")) bit=PERM_AUDIO;
            }
            if (!bit || (e->app.permissions&bit)) ok=false;
            e->app.permissions|=bit;
        }
        for (size_t j=0;j<used;j++) if (!strcmp(next[j].app.id,e->app.id)) ok=false;
        if (!ok) break;
        used++;
    }
    cJSON_Delete(root); *length=used; return ok;
}
void store_init(void) {
    /* A full flash or NVS erase removes the user-selected catalog URL. Keep the
       production catalog usable out of the box, while still allowing a valid
       URL saved in NVS to override it. */
    strcpy(source,DEFAULT_STORE_URL);
    nvs_handle_t h;
    if (nvs_open("mini_store",NVS_READONLY,&h)!=ESP_OK) return;
    char saved[sizeof(source)]; size_t size=sizeof(saved);
    if (nvs_get_str(h,"url",saved,&size)==ESP_OK && valid_url(saved)) strcpy(source,saved);
    nvs_close(h);
}
const char *store_url(void) { return source; }
size_t store_count(void) { return count; }
const store_entry_t *store_entry(size_t index) { return index<count?&entries[index]:NULL; }
static store_entry_action_t classify(const app_info_t *available,const app_info_t *installed,size_t installed_count) {
    if (!available) return STORE_ENTRY_INVALID;
    for (size_t i=0;i<installed_count;i++) {
        if (strcmp(installed[i].id,available->id)) continue;
        int order=0;
        if (!compare_versions(available->version,installed[i].version,&order)) return STORE_ENTRY_INVALID;
        return order>0?STORE_ENTRY_UPDATE:order<0?STORE_ENTRY_DOWNGRADE:STORE_ENTRY_REINSTALL;
    }
    return STORE_ENTRY_INSTALL;
}
store_entry_action_t store_entry_action(size_t index) {
    if (index>=count) return STORE_ENTRY_INVALID;
    app_info_t installed[APP_LIMIT];
    size_t installed_count=apps_list(installed,APP_LIMIT);
    return classify(&entries[index].app,installed,installed_count);
}
const char *store_entry_action_name(store_entry_action_t action) {
    if (action==STORE_ENTRY_INSTALL) return "Install";
    if (action==STORE_ENTRY_UPDATE) return "Update";
    if (action==STORE_ENTRY_REINSTALL) return "Reinstall";
    if (action==STORE_ENTRY_DOWNGRADE) return "Older";
    return "Invalid";
}
esp_err_t store_refresh(const char *url) {
    if (!url) url=source;
    if (!valid_url(url)) return ESP_ERR_INVALID_ARG;
    uint8_t *bytes=NULL; size_t size=0,next_count=0;
    esp_err_t err=network_fetch(url,20*1024,&bytes,&size);
    if (err!=ESP_OK) return err;
    store_entry_t *next=calloc(APP_LIMIT,sizeof(*next));
    if (!next) err=ESP_ERR_NO_MEM;
    else if (!parse(bytes,size,next,&next_count)) err=ESP_ERR_INVALID_ARG;
    free(bytes);
    if (err==ESP_OK && strcmp(url,source)) {
        nvs_handle_t h; err=nvs_open("mini_store",NVS_READWRITE,&h);
        if (err==ESP_OK) { err=nvs_set_str(h,"url",url); if (err==ESP_OK) err=nvs_commit(h); nvs_close(h); }
    }
    if (err==ESP_OK) { memcpy(entries,next,sizeof(entries)); count=next_count; if (url!=source) strcpy(source,url); }
    free(next); return err;
}
esp_err_t store_install(size_t index) {
    if (index>=count || !source[0]) return ESP_ERR_INVALID_ARG;
    store_entry_action_t action=store_entry_action(index);
    if (action==STORE_ENTRY_DOWNGRADE) return ESP_ERR_INVALID_VERSION;
    if (action==STORE_ENTRY_INVALID) return ESP_ERR_INVALID_STATE;
    store_entry_t *e=&entries[index]; char url[480];
    const char *slash=strrchr(source,'/');
    if (!slash || snprintf(url,sizeof(url),"%.*s%s",(int)(slash-source+1),source,e->file)>=(int)sizeof(url)) return ESP_ERR_INVALID_SIZE;
    uint8_t *bytes=NULL; size_t size=0;
    esp_err_t err=network_fetch_app(url,e->size,e->size,e->sha256,&bytes,&size);
    if (err==ESP_OK) {
        app_info_t app; const uint8_t *wasm;
        err=apps_verify(bytes,size,&app,&wasm);
        if (err==ESP_OK && (strcmp(app.id,e->app.id) || strcmp(app.name,e->app.name) || strcmp(app.version,e->app.version) || app.permissions!=e->app.permissions)) err=ESP_ERR_INVALID_ARG;
        if (err==ESP_OK) err=apps_install(bytes,size);
    }
    free(bytes); return err;
}
bool store_selftest(void) {
    const char *valid="{\"schema_version\":1,\"apps\":[{\"id\":\"com.test.store\",\"name\":\"Test\",\"version\":\"0.1.0\",\"permissions\":[\"display\"],\"file\":\"apps/test.app\",\"size\":128,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}]}";
    store_entry_t *test=calloc(APP_LIMIT,sizeof(*test)); size_t length=0;
    if (!test) return false;
    bool ok=parse((const uint8_t*)valid,strlen(valid),test,&length) && length==1;
    for (unsigned i=0;i<8;i++) {
        cJSON *root=cJSON_Parse(valid);
        if (!root) { ok=false; break; }
        cJSON *apps=cJSON_GetObjectItemCaseSensitive(root,"apps"),*app=cJSON_GetArrayItem(apps,0);
        if (i==0) cJSON_ReplaceItemInObjectCaseSensitive(app,"file",cJSON_CreateString("../outside.app"));
        if (i==1) cJSON_AddItemToArray(apps,cJSON_Duplicate(app,true));
        if (i==2) cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(app,"permissions"),cJSON_CreateString("gpio"));
        if (i==3) cJSON_ReplaceItemInObjectCaseSensitive(app,"size",cJSON_CreateNumber(128.5));
        if (i==4) cJSON_ReplaceItemInObjectCaseSensitive(app,"size",cJSON_CreateNumber(APP_MAX_BYTES+4096));
        if (i==5) cJSON_ReplaceItemInObjectCaseSensitive(app,"sha256",cJSON_CreateString("invalid"));
        if (i==6) cJSON_AddNumberToObject(root,"schema_version",1);
        if (i==7) cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(app,"permissions"),cJSON_CreateString("display"));
        char *json=cJSON_PrintUnformatted(root); cJSON_Delete(root);
        memset(test,0,APP_LIMIT*sizeof(*test));
        if (!json || parse((const uint8_t*)json,strlen(json),test,&length)) ok=false;
        cJSON_free(json);
    }
    int order=99;
    ok=ok && compare_versions("0.10.0","0.9.0",&order) && order>0;
    ok=ok && compare_versions("1.2.3","1.2.3",&order) && order==0;
    ok=ok && compare_versions("1.2.2","1.2.3",&order) && order<0;
    ok=ok && !compare_versions("1.2","1.2.0",&order);
    app_info_t available={0},installed={0};
    strcpy(available.id,"com.test.store"); strcpy(installed.id,"com.test.store");
    strcpy(installed.version,"1.2.3");
    strcpy(available.version,"1.3.0"); ok=ok && classify(&available,&installed,1)==STORE_ENTRY_UPDATE;
    strcpy(available.version,"1.2.3"); ok=ok && classify(&available,&installed,1)==STORE_ENTRY_REINSTALL;
    strcpy(available.version,"1.2.2"); ok=ok && classify(&available,&installed,1)==STORE_ENTRY_DOWNGRADE;
    strcpy(available.id,"com.test.other"); ok=ok && classify(&available,&installed,1)==STORE_ENTRY_INSTALL;
    free(test);
    return ok && !valid_url("http://example.com/index.json") && !valid_url("https://user@example.com/index.json") &&
        valid_url("https://example.com/store/index.json") && valid_url(DEFAULT_STORE_URL);
}

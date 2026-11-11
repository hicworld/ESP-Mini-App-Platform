#include "platform.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static app_info_t installed;
static size_t installed_count;
static unsigned app_fetches;
static const char directory_json[]="{\"schema_version\":1,\"apps\":[{\"id\":\"com.test.store\",\"name\":\"Test\",\"version\":\"1.1.0\",\"permissions\":[\"display\"],\"file\":\"apps/test.app\",\"size\":128,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}]}";

size_t apps_list(app_info_t *out,size_t capacity) {
    if (out && capacity && installed_count) *out=installed;
    return installed_count;
}
esp_err_t network_fetch(const char *url,size_t maximum,uint8_t **bytes,size_t *size) {
    (void)url;
    size_t n=strlen(directory_json);
    if (n>maximum) return ESP_ERR_INVALID_SIZE;
    *bytes=malloc(n); if (!*bytes) return ESP_ERR_NO_MEM;
    memcpy(*bytes,directory_json,n); *size=n; return ESP_OK;
}
esp_err_t network_fetch_app(const char *url,size_t maximum,size_t expected_size,const char *expected_sha256,uint8_t **bytes,size_t *size) {
    (void)url; (void)maximum; (void)expected_size; (void)expected_sha256; (void)bytes; (void)size;
    app_fetches++; return ESP_FAIL;
}
esp_err_t apps_verify(const uint8_t *bytes,size_t size,app_info_t *info,const uint8_t **wasm) {
    (void)bytes; (void)size; (void)info; (void)wasm; return ESP_FAIL;
}
esp_err_t apps_install(const uint8_t *bytes,size_t size) { (void)bytes; (void)size; return ESP_FAIL; }
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle) { (void)name; (void)mode; *handle=1; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t handle,const char *key,char *value,size_t *size) { (void)handle; (void)key; (void)value; (void)size; return ESP_FAIL; }
esp_err_t nvs_set_str(nvs_handle_t handle,const char *key,const char *value) { (void)handle; (void)key; (void)value; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; return ESP_OK; }
void nvs_close(nvs_handle_t handle) { (void)handle; }

int main(void) {
    bool ok=store_selftest();
    if (store_refresh("https://example.com/store/index.json")!=ESP_OK || store_count()!=1) ok=false;
    strcpy(installed.id,"com.test.store"); installed_count=1;
    strcpy(installed.version,"1.2.0"); app_fetches=0;
    if (store_entry_action(0)!=STORE_ENTRY_DOWNGRADE || strcmp(store_entry_action_name(store_entry_action(0)),"Older") ||
        store_install(0)!=ESP_ERR_INVALID_VERSION || app_fetches) ok=false;
    strcpy(installed.version,"1.1.0");
    if (store_entry_action(0)!=STORE_ENTRY_REINSTALL) ok=false;
    strcpy(installed.version,"1.0.9"); app_fetches=0;
    if (store_entry_action(0)!=STORE_ENTRY_UPDATE || store_install(0)!=ESP_FAIL || app_fetches!=1) ok=false;
    installed_count=0;
    if (store_entry_action(0)!=STORE_ENTRY_INSTALL) ok=false;
    puts(ok?"PASS firmware store parser: valid directory, 8 rejection cases, update policy, downgrade preflight and URL policy":"FAIL firmware store parser");
    return ok?0:1;
}

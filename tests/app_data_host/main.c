/* Execute production app_data.c with a controlled in-memory NVS model. */
#include "platform.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../firmware/main/app_data.c"

static char open_namespace[16],durable_namespace[16],durable_id[APP_ID_LENGTH];
static char pending_id[APP_ID_LENGTH];
static bool durable_has_id,durable_has_value,pending_set,pending_erase,commit_fails;
static unsigned tests,failures;

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s\n",__LINE__,#condition); failures++; return; } } while (0)

esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle) {
    (void)mode; snprintf(open_namespace,sizeof(open_namespace),"%s",name); *handle=1; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_get_str(nvs_handle_t handle,const char *key,char *out,size_t *size) {
    (void)handle; (void)key;
    if (strcmp(open_namespace,durable_namespace) || !durable_has_id) return ESP_ERR_NVS_NOT_FOUND;
    size_t required=strlen(durable_id)+1;
    if (!out || *size<required) { *size=required; return out?ESP_ERR_INVALID_SIZE:ESP_OK; }
    memcpy(out,durable_id,required); *size=required; return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t handle,const char *key,const char *value) {
    (void)handle; (void)key; snprintf(pending_id,sizeof(pending_id),"%s",value); pending_set=true; return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t handle) { (void)handle; pending_erase=true; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    if (commit_fails) return ESP_FAIL;
    if (pending_erase) {
        durable_namespace[0]=0; durable_id[0]=0; durable_has_id=false; durable_has_value=false;
        pending_erase=false;
    }
    if (pending_set) {
        snprintf(durable_namespace,sizeof(durable_namespace),"%s",open_namespace);
        snprintf(durable_id,sizeof(durable_id),"%s",pending_id);
        durable_has_id=true; pending_set=false;
    }
    return ESP_OK;
}

static void reset(void) {
    memset(open_namespace,0,sizeof(open_namespace)); memset(durable_namespace,0,sizeof(durable_namespace));
    memset(durable_id,0,sizeof(durable_id)); memset(pending_id,0,sizeof(pending_id));
    durable_has_id=false; durable_has_value=false; pending_set=false; pending_erase=false; commit_fails=false;
}
static void invalid_arguments(void) {
    tests++; reset(); nvs_handle_t handle=99;
    CHECK(app_data_open("bad",&handle)==ESP_ERR_INVALID_ARG); CHECK(handle==99);
    CHECK(app_data_open("com.test.app",NULL)==ESP_ERR_INVALID_ARG);
    CHECK(app_data_clear("bad")==ESP_ERR_INVALID_ARG);
    puts("PASS invalid IDs and null handle rejected");
}
static void create_and_reopen(void) {
    tests++; reset(); nvs_handle_t handle=0;
    CHECK(app_data_open("com.test.app",&handle)==ESP_OK); CHECK(handle==1); CHECK(durable_has_id);
    char first[16]; snprintf(first,sizeof(first),"%s",open_namespace); nvs_close(handle);
    handle=0; CHECK(app_data_open("com.test.app",&handle)==ESP_OK); CHECK(!strcmp(first,open_namespace));
    CHECK(!strcmp(durable_id,"com.test.app"));
    puts("PASS namespace created, identified, and reopened deterministically");
}
static void distinct_namespaces(void) {
    tests++; reset(); nvs_handle_t handle=0;
    CHECK(app_data_open("com.test.first",&handle)==ESP_OK); char first[16]; snprintf(first,sizeof(first),"%s",open_namespace);
    CHECK(app_data_open("com.test.second",&handle)==ESP_OK); CHECK(strcmp(first,open_namespace));
    puts("PASS different application IDs use different namespaces");
}
static void collision_rejected(void) {
    tests++; reset(); nvs_handle_t handle=0;
    CHECK(app_data_open("com.test.app",&handle)==ESP_OK);
    snprintf(durable_id,sizeof(durable_id),"%s","com.other.app");
    CHECK(app_data_open("com.test.app",&handle)==ESP_ERR_INVALID_STATE); CHECK(handle==0);
    puts("PASS namespace identity collision rejected");
}
static void clear_and_recreate(void) {
    tests++; reset(); nvs_handle_t handle=0;
    CHECK(app_data_open("com.test.app",&handle)==ESP_OK); durable_has_value=true;
    CHECK(app_data_clear("com.test.app")==ESP_OK); CHECK(!durable_has_id); CHECK(!durable_has_value);
    CHECK(app_data_open("com.test.app",&handle)==ESP_OK); CHECK(durable_has_id);
    CHECK(app_data_clear("com.test.app")==ESP_OK);
    puts("PASS clear removes all data and a later open recreates identity");
}
static void empty_clear(void) {
    tests++; reset(); CHECK(app_data_clear("com.test.empty")==ESP_OK); CHECK(!durable_has_id);
    puts("PASS clearing an unused application namespace is idempotent");
}
static void commit_failures(void) {
    tests++; reset(); nvs_handle_t handle=0; commit_fails=true;
    CHECK(app_data_open("com.test.app",&handle)==ESP_FAIL); CHECK(handle==0); CHECK(!durable_has_id);
    commit_fails=false; CHECK(app_data_open("com.test.app",&handle)==ESP_OK); durable_has_value=true;
    commit_fails=true; CHECK(app_data_clear("com.test.app")==ESP_FAIL); CHECK(durable_has_id); CHECK(durable_has_value);
    puts("PASS create and clear commit failures are reported without fake success");
}

int main(void) {
    invalid_arguments(); create_and_reopen(); distinct_namespaces(); collision_rejected();
    clear_and_recreate(); empty_clear(); commit_failures();
    printf("APP DATA HOST RESULT %u/%u\n",tests-failures,tests);
    return failures?1:0;
}

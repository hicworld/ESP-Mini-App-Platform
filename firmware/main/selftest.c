#include "platform.h"
#include "app_data.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "wasm_export.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
typedef struct { const char *name; const uint8_t *data; size_t size; bool fail; int event; } fixture_t;
#include "fixtures.inc"
extern const uint8_t snake_start[] asm("_binary_snake_app_start");
extern const uint8_t snake_end[] asm("_binary_snake_app_end");
static bool app_data_clear_test(void) {
    const char *id="com.selftest.storage"; const char *key="probe";
    nvs_handle_t handle=0; uint32_t value=0x31415926,readback=0; size_t size=sizeof(readback);
    if (app_data_clear(id)!=ESP_OK || app_data_open(id,&handle)!=ESP_OK) return false;
    esp_err_t err=nvs_set_blob(handle,key,&value,sizeof(value));
    if (err==ESP_OK) err=nvs_commit(handle);
    nvs_close(handle); handle=0;
    if (err!=ESP_OK || app_data_clear(id)!=ESP_OK || app_data_open(id,&handle)!=ESP_OK) return false;
    err=nvs_get_blob(handle,key,&readback,&size);
    nvs_close(handle);
    bool ok=err==ESP_ERR_NVS_NOT_FOUND;
    if (app_data_clear(id)!=ESP_OK) ok=false;
    return ok;
}
static bool clear_fixture_data(void) {
    static const char *ids[]={
        "com.test.fixture",
        "com.test.storagewrite",
        "com.test.storagekeys",
        "com.selftest.storage",
    };
    bool ok=true;
    for (unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)
        if (app_data_clear(ids[i])!=ESP_OK) ok=false;
    return ok;
}
void platform_selftest(void) {
    diagnostics_mark("selftest:begin");
    puts("SELFTEST BEGIN"); fflush(stdout); vTaskDelay(pdMS_TO_TICKS(30));
    runtime_stop(); hal_set_canvas(NULL);
    /* Reclaim every test-only namespace up front. Clearing only the current
     * fixture can leave a later fixture near the global NVS reserve threshold. */
    bool fixture_data_ready=clear_fixture_data();
    unsigned passed=0,total=sizeof(fixtures)/sizeof(fixtures[0]);
    size_t before=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    mem_alloc_info_t pool_before={0},pool_after={0};
    diagnostics_mark("selftest:pool-info");
    bool pool_measured=wasm_runtime_get_mem_alloc_info(&pool_before);
    for (unsigned i=0;i<total;i++) {
        const fixture_t *f=&fixtures[i];
        esp_err_t cleanup=!strncmp(f->name,"storage",7) && !fixture_data_ready?ESP_FAIL:ESP_OK;
        /* Isolate both storage limit cases from the process-wide write window. */
        if (!strcmp(f->name,"storage_write_limit") || !strcmp(f->name,"storage_key_limit")) {
            vTaskDelay(pdMS_TO_TICKS(1100));
        }
        printf("SELFTEST BEGIN %s\n",f->name); fflush(stdout); vTaskDelay(pdMS_TO_TICKS(30));
        esp_err_t err=cleanup==ESP_OK?runtime_start_bytes(f->data,f->size):cleanup;
        bool event_started=!f->event || err==ESP_OK;
        if (err==ESP_OK && !strcmp(f->name,"storage_key_limit")) {
            /* At 300 ms spacing no uptime-second receives more than four
             * writes, so this isolates the eight-key namespace limit. */
            for (unsigned key=0;key<9 && runtime_state()==APP_RUNNING;key++) {
                runtime_event(f->event,(int)key,0);
                if (key<8) vTaskDelay(pdMS_TO_TICKS(300));
            }
        } else if (err==ESP_OK && f->event) runtime_event(f->event,10,10);
        bool failed=err!=ESP_OK || runtime_state()==APP_CRASHED;
        bool ok=event_started && failed==f->fail; passed+=ok;
        printf("SELFTEST %s %s: %s\n",f->name,ok?"PASS":"FAIL",failed?runtime_error():"running");
        runtime_stop(); vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!clear_fixture_data()) puts("SELFTEST WARNING: test namespace cleanup failed");
    bool store_ok=store_selftest(); total++; passed+=store_ok;
    printf("SELFTEST store_parser %s (valid index + 8 rejection cases + update policy)\n",store_ok?"PASS":"FAIL");
    bool clear_ok=app_data_clear_test(); total++; passed+=clear_ok;
    printf("SELFTEST app_data_clear %s (write, erase, reopen, missing key)\n",clear_ok?"PASS":"FAIL");
    bool snake_ok=runtime_start_bytes(snake_start,snake_end-snake_start)==ESP_OK;
    if (snake_ok) {
        runtime_event(2,380,350);
        uint32_t start=(uint32_t)platform_now_ms();
        for (unsigned i=1;i<=24;i++) { runtime_event(1,start+i*220,0); if (runtime_state()!=APP_RUNNING) snake_ok=false; }
        runtime_event(2,100,100); /* restart after wall collision */
        if (runtime_state()!=APP_RUNNING) snake_ok=false;
    }
    runtime_stop(); total++; passed+=snake_ok;
    printf("SELFTEST snake_c_sdk %s (init, movement, collision, restart)\n",snake_ok?"PASS":"FAIL");
    unsigned cycles=0;
    const fixture_t *normal=NULL;
    for(unsigned i=0;i<sizeof(fixtures)/sizeof(fixtures[0]);i++)
        if(!strcmp(fixtures[i].name,"ok")) {normal=&fixtures[i];break;}
    for (;cycles<100;cycles++) {
        if (!normal || runtime_start_bytes(normal->data,normal->size)!=ESP_OK) break;
        runtime_stop(); vTaskDelay(pdMS_TO_TICKS(1));
    }
    pool_measured=wasm_runtime_get_mem_alloc_info(&pool_after) && pool_measured;
    bool lifecycle_ok=cycles==100 && pool_measured && pool_after.total_free_size==pool_before.total_free_size;
    printf("SELFTEST lifecycle %s cycles=%u pool_free_before=%u pool_free_after=%u\n",lifecycle_ok?"PASS":"FAIL",cycles,(unsigned)pool_before.total_free_size,(unsigned)pool_after.total_free_size);
    total++; passed+=lifecycle_ok;
    printf("SELFTEST RESULT %u/%u heap_before=%u heap_after=%u\n",passed,total,(unsigned)before,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    runtime_stop(); launcher_refresh(); diagnostics_mark("selftest:complete");
}

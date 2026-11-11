/* Execute production OTA gates with controlled local service / bootloader
 * outcomes. This tests decisions and call order, not flash or power loss. */
#include "platform.h"
#include "esp_ota_ops.h"
#include <stdio.h>
#include <string.h>

static const esp_partition_t partition={0};
static bool have_partition,nand_ready;
static esp_ota_img_states_t image_state;
static app_state_t app_state;
static esp_err_t state_result,storage_result,apps_result,nand_result,confirm_result,rollback_result;
static unsigned storage_calls,app_calls,nand_calls,confirm_calls,rollback_calls,tests,failed;
static uint64_t capacity,usage;
static char phase[48];
#define CHECK(expr) do { if (!(expr)) { printf("FAIL line %u: %s\n",__LINE__,#expr); failed++; return; } } while (0)
const esp_partition_t *esp_ota_get_running_partition(void) { return have_partition?&partition:NULL; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *out) {
    (void)p; *out=image_state; return state_result;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { confirm_calls++; return confirm_result; }
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) { rollback_calls++; return rollback_result; }
app_state_t runtime_state(void) { return app_state; }
bool nand_storage_ready(void) { return nand_ready; }
esp_err_t nand_storage_selftest(void) { nand_calls++; return nand_result; }
esp_err_t apps_storage_info(uint64_t *total,uint64_t *used,bool *nand) {
    storage_calls++; *total=capacity; *used=usage; if (nand) *nand=nand_ready; return storage_result;
}
esp_err_t apps_check(void) { app_calls++; return apps_result; }
void diagnostics_mark(const char *value) { snprintf(phase,sizeof(phase),"%s",value); }
static void reset(void) {
    tests++; have_partition=true; nand_ready=true; image_state=ESP_OTA_IMG_PENDING_VERIFY;
    app_state=APP_STOPPED; state_result=storage_result=apps_result=nand_result=confirm_result=ESP_OK;
    /* A real successful rollback never returns. Use a sentinel to model this. */
    rollback_result=0x777;
    storage_calls=app_calls=nand_calls=confirm_calls=rollback_calls=0;
    capacity=100000; usage=1000; phase[0]=0;
}
static void preflight_cases(unsigned mode) {
    reset(); image_state=ESP_OTA_IMG_VALID;
    switch (mode) {
        case 1: image_state=ESP_OTA_IMG_PENDING_VERIFY; break;
        case 2: app_state=APP_RUNNING; break;
        case 3: apps_result=ESP_ERR_INVALID_CRC; break;
        case 4: nand_result=ESP_FAIL; break;
        case 5: have_partition=false; break;
        case 6: state_result=ESP_FAIL; break;
        case 7: nand_ready=false; break;
        case 8: storage_result=ESP_FAIL; break;
    }
    esp_err_t result=ota_preflight();
    CHECK(confirm_calls==0 && rollback_calls==0);
    if (mode==0 || mode==7) {
        CHECK(result==ESP_OK && app_calls==1 && nand_calls==(mode==0?1u:0u));
    } else CHECK(result!=ESP_OK);
    if (mode==1 || mode==2 || mode==5 || mode==6) CHECK(storage_calls==0 && app_calls==0);
    if (mode==3 || mode==8) CHECK(nand_calls==0);
    printf("PASS preflight case %u\n",mode);
}
#if CONFIG_PLATFORM_OTA_TEST_FAIL_BOOT
static void forced_boot(void) {
    reset(); CHECK(ota_confirm_boot(true,ESP_OK)==rollback_result);
    CHECK(confirm_calls==0 && rollback_calls==1 && storage_calls==0);
    CHECK(!strcmp(phase,"ota:forced-rollback"));
    puts("PASS forced rollback test image");
    reset(); image_state=ESP_OTA_IMG_VALID;
    CHECK(ota_confirm_boot(true,ESP_OK)==ESP_OK);
    CHECK(confirm_calls==0 && rollback_calls==0);
    puts("PASS forced rollback applies only to pending image");
}
#else
static void boot_cases(unsigned mode) {
    reset(); bool ready=true; esp_err_t network=ESP_OK;
    switch (mode) {
        case 1: image_state=ESP_OTA_IMG_VALID; break;
        case 2: ready=false; break;
        case 3: network=ESP_FAIL; break;
        case 4: storage_result=ESP_FAIL; break;
        case 5: capacity=0; break;
        case 6: usage=capacity+1; break;
        case 7: apps_result=ESP_ERR_INVALID_CRC; break;
        case 8: nand_result=ESP_FAIL; break;
        case 9: confirm_result=ESP_FAIL; break;
        case 10: apps_result=ESP_FAIL; rollback_result=ESP_FAIL; break;
        case 11: nand_ready=false; break;
        case 12: have_partition=false; break;
        case 13: state_result=ESP_FAIL; break;
    }
    esp_err_t result=ota_confirm_boot(ready,network);
    if (mode==0 || mode==11) {
        CHECK(result==ESP_OK && confirm_calls==1 && rollback_calls==0);
        CHECK(app_calls==1 && nand_calls==(mode==0?1u:0u));
        CHECK(!strcmp(phase,"ota:confirmed"));
    } else if (mode==1 || mode==12 || mode==13) {
        CHECK(confirm_calls==0 && rollback_calls==0 && storage_calls==0);
        CHECK((result==ESP_OK)==(mode==1));
    } else {
        CHECK(result==rollback_result && rollback_calls==1);
        CHECK(confirm_calls==(mode==9?1u:0u));
        if (mode==2 || mode==3) CHECK(storage_calls==0);
        if (mode==4 || mode==5 || mode==6) CHECK(app_calls==0);
        if (mode==7) CHECK(nand_calls==0 && !strcmp(phase,"ota:apps-failed"));
        if (mode==8) CHECK(!strcmp(phase,"ota:nand-failed"));
        if (mode==9) CHECK(!strcmp(phase,"ota:confirm-failed"));
    }
    printf("PASS boot confirmation case %u\n",mode);
}
#endif
int main(void) {
    for (unsigned i=0;i<9;i++) preflight_cases(i);
#if CONFIG_PLATFORM_OTA_TEST_FAIL_BOOT
    forced_boot();
#else
    for (unsigned i=0;i<14;i++) boot_cases(i);
#endif
    printf("OTA HEALTH HOST RESULT %u/%u\n",tests-failed,tests);
    return failed?1:0;
}

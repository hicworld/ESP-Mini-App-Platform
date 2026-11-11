#include "platform.h"
#include "nand_storage.h"
#include "esp_attr.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static RTC_NOINIT_ATTR struct { uint32_t magic; char phase[48]; } retained;
static char previous[48];
void diagnostics_init(void) {
    if (esp_reset_reason()!=ESP_RST_POWERON && retained.magic==0x454d4150 &&
        memchr(retained.phase,0,sizeof(retained.phase)))
        memcpy(previous,retained.phase,sizeof(previous));
    diagnostics_mark("boot");
}
void diagnostics_mark(const char *phase) {
    snprintf(retained.phase,sizeof(retained.phase),"%s",phase);
    retained.magic=0x454d4150;
}
void diagnostics_print(void) {
    const esp_partition_t *partition=esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state=ESP_OTA_IMG_UNDEFINED;
    if (partition) esp_ota_get_state_partition(partition,&ota_state);
    const esp_app_desc_t *app=esp_app_get_description();
    printf("STATUS version=%s partition=%s ota_state=%d reset=%d previous=%s phase=%s uptime_ms=%llu stack_min=%u storage=%d nand_init=%d nand=%d nand_mount=%s runtime=%d\n",
        app?app->version:PLATFORM_VERSION,partition?partition->label:"unknown",(int)ota_state,
        (int)esp_reset_reason(),previous,retained.phase,(unsigned long long)platform_now_ms(),
        (unsigned)uxTaskGetStackHighWaterMark(NULL),hal_storage_ready(),nand_storage_initialized(),
        nand_storage_ready(),esp_err_to_name(nand_storage_mount_result()),runtime_state());
}

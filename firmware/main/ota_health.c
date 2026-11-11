#include "platform.h"
#include "nand_storage.h"
#include "esp_ota_ops.h"
#include <stdio.h>

/* Local checks deliberately do not require an Internet connection: a sound
 * image must be confirmable after the access point disappears during reboot. */
static esp_err_t local_health(void) {
    if (runtime_state()!=APP_STOPPED) return ESP_ERR_INVALID_STATE;
    uint64_t total=0,used=0;
    esp_err_t err=apps_storage_info(&total,&used,NULL);
    if (err==ESP_OK && (!total || used>total)) err=ESP_ERR_INVALID_SIZE;
    if (err!=ESP_OK) { diagnostics_mark("ota:storage-failed"); return err; }
    err=apps_check();
    if (err!=ESP_OK) { diagnostics_mark("ota:apps-failed"); return err; }
    if (nand_storage_ready()) {
        err=nand_storage_selftest();
        if (err!=ESP_OK) { diagnostics_mark("ota:nand-failed"); return err; }
    }
    return ESP_OK;
}

esp_err_t ota_preflight(void) {
    const esp_partition_t *running=esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running) return ESP_ERR_INVALID_STATE;
    esp_err_t err=esp_ota_get_state_partition(running,&state);
    if (err!=ESP_OK) return err;
    /* Keep a confirmed fallback before any write to the other OTA slot. */
    if (state!=ESP_OTA_IMG_VALID) return ESP_ERR_INVALID_STATE;
    err=local_health();
    if (err==ESP_OK) { diagnostics_mark("ota:preflight-ok"); puts("OTA PREFLIGHT PASS"); }
    return err;
}

esp_err_t ota_confirm_boot(bool applications_ready,esp_err_t network_init_result) {
    const esp_partition_t *running=esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running) return ESP_ERR_INVALID_STATE;
    esp_err_t err=esp_ota_get_state_partition(running,&state);
    if (err!=ESP_OK) return err;
    if (state!=ESP_OTA_IMG_PENDING_VERIFY) return ESP_OK;
#if CONFIG_PLATFORM_OTA_TEST_FAIL_BOOT
    (void)applications_ready; (void)network_init_result;
    diagnostics_mark("ota:forced-rollback");
    puts("OTA validation deliberately failed by test build"); fflush(stdout);
    return esp_ota_mark_app_invalid_rollback_and_reboot();
#else
    if (!applications_ready || network_init_result!=ESP_OK) {
        err=ESP_ERR_INVALID_STATE;
        diagnostics_mark("ota:init-failed");
    } else err=local_health();
    if (err==ESP_OK) {
        err=esp_ota_mark_app_valid_cancel_rollback();
        if (err==ESP_OK) {
            diagnostics_mark("ota:confirmed");
            puts("OTA BOOT HEALTH PASS: image confirmed");
            return ESP_OK;
        }
        diagnostics_mark("ota:confirm-failed");
    }
    printf("OTA BOOT HEALTH FAIL error=0x%x; requesting rollback\n",(unsigned)err);
    fflush(stdout);
    /* If no valid rollback target exists, propagate the error to the caller;
     * never continue into the launcher and pretend this image was confirmed. */
    return esp_ota_mark_app_invalid_rollback_and_reboot();
#endif
}

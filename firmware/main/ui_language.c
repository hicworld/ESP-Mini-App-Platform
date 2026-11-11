#include "platform.h"
#include "nvs.h"

static ui_language_t current=UI_LANGUAGE_ZH;

esp_err_t ui_language_init(void) {
    nvs_handle_t handle=0;
    esp_err_t err=nvs_open("mini_ui",NVS_READONLY,&handle);
    if (err==ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err!=ESP_OK) return err;
    uint8_t value=UI_LANGUAGE_ZH;
    err=nvs_get_u8(handle,"language",&value);
    nvs_close(handle);
    if (err==ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err!=ESP_OK) return err;
    if (value>UI_LANGUAGE_EN) return ESP_ERR_INVALID_STATE;
    current=(ui_language_t)value;
    return ESP_OK;
}

ui_language_t ui_language_get(void) { return current; }
bool ui_language_is_chinese(void) { return current==UI_LANGUAGE_ZH; }
const char *ui_text(const char *zh,const char *en) { return ui_language_is_chinese()?zh:en; }

esp_err_t ui_language_set(ui_language_t language) {
    if (language!=UI_LANGUAGE_ZH && language!=UI_LANGUAGE_EN) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle=0;
    esp_err_t err=nvs_open("mini_ui",NVS_READWRITE,&handle);
    if (err!=ESP_OK) return err;
    err=nvs_set_u8(handle,"language",(uint8_t)language);
    if (err==ESP_OK) err=nvs_commit(handle);
    nvs_close(handle);
    if (err==ESP_OK) current=language;
    return err;
}

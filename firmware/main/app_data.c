#include "app_validation.h"
#include "app_data.h"
#include <stdio.h>
#include <string.h>

static void data_namespace(char out[16],const char *id) {
    uint32_t hash=2166136261u;
    for (const unsigned char *p=(const unsigned char *)id;*p;p++) hash=(hash^*p)*16777619u;
    snprintf(out,16,"app%08x",(unsigned)hash);
}

esp_err_t app_data_open(const char *id,nvs_handle_t *handle_out) {
    if (!valid_id(id) || !handle_out) return ESP_ERR_INVALID_ARG;
    *handle_out=0;
    char namespace_name[16],stored_id[APP_ID_LENGTH];
    data_namespace(namespace_name,id);
    esp_err_t err=nvs_open(namespace_name,NVS_READWRITE,handle_out);
    if (err!=ESP_OK) return err;
    size_t size=sizeof(stored_id);
    err=nvs_get_str(*handle_out,"__id",stored_id,&size);
    if (err==ESP_ERR_NVS_NOT_FOUND) {
        err=nvs_set_str(*handle_out,"__id",id);
        if (err==ESP_OK) err=nvs_commit(*handle_out);
    } else if (err==ESP_OK && strcmp(stored_id,id)) err=ESP_ERR_INVALID_STATE;
    if (err!=ESP_OK) { nvs_close(*handle_out); *handle_out=0; }
    return err;
}

esp_err_t app_data_clear(const char *id) {
    if (!valid_id(id)) return ESP_ERR_INVALID_ARG;
    char namespace_name[16],stored_id[APP_ID_LENGTH];
    data_namespace(namespace_name,id);
    nvs_handle_t handle=0;
    esp_err_t err=nvs_open(namespace_name,NVS_READWRITE,&handle);
    if (err!=ESP_OK) return err;
    size_t size=sizeof(stored_id);
    err=nvs_get_str(handle,"__id",stored_id,&size);
    if (err==ESP_OK && strcmp(stored_id,id)) err=ESP_ERR_INVALID_STATE;
    else if (err==ESP_ERR_NVS_NOT_FOUND) err=ESP_OK;
    if (err==ESP_OK) err=nvs_erase_all(handle);
    if (err==ESP_OK) err=nvs_commit(handle);
    nvs_close(handle);
    return err;
}

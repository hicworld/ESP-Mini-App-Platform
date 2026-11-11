#pragma once
#include "esp_err.h"
#include "nvs.h"

esp_err_t app_data_open(const char *id,nvs_handle_t *handle_out);
esp_err_t app_data_clear(const char *id);

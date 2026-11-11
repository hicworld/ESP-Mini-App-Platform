#pragma once
#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define NETWORK_SCAN_LIMIT 20
typedef struct {char ssid[33];int rssi;bool open,supported;} network_ap_t;
typedef struct {uint32_t revision;bool busy;esp_err_t result;unsigned count;network_ap_t ap[NETWORK_SCAN_LIMIT];} network_scan_t;
typedef enum {UI_LANGUAGE_ZH,UI_LANGUAGE_EN} ui_language_t;
ui_language_t ui_language_get(void);
bool ui_language_is_chinese(void);
const char *ui_text(const char *zh,const char *en);
typedef struct {int action;} ui_message_t;
#define UI_SETTINGS 1
void launcher_post(ui_message_t message);
esp_err_t network_scan_begin(void);
void network_scan_snapshot(network_scan_t *out);
esp_err_t network_connect(const char *ssid,const char *password);
bool network_ready(void);
void network_saved_ssid(char out[33]);
const char *network_connection_text(void);
esp_err_t environment_configure(const char *argument);
void network_setup_close(void);

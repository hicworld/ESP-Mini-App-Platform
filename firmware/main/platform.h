#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifndef PLATFORM_VERSION
#define PLATFORM_VERSION "0.4.11"
#endif
#define PLATFORM_API_VERSION 1
#define APP_LIMIT 12
#define APP_MAX_BYTES (256 * 1024)
#define APP_MAX_MANIFEST 2048
#define APP_ID_LENGTH 48
#define PERM_DISPLAY 1u
#define PERM_TOUCH 2u
#define PERM_STORAGE 4u
#define PERM_ENVIRONMENT 8u
#define PERM_INPUT 16u
#define PERM_AUDIO 32u
#define HW_DISPLAY (1u<<0)
#define HW_TOUCH (1u<<1)
#define HW_SPEAKER (1u<<2)
#define HW_MOTION (1u<<3)
#define HW_BUTTONS (1u<<4)
#define HW_WIFI (1u<<5)
#define HW_CAMERA (1u<<6)
#define HW_MICROPHONE (1u<<7)
#define HW_TEMPERATURE (1u<<8)
#define HW_HUMIDITY (1u<<9)
#define HW_LIGHT (1u<<10)
#define HW_IMU (1u<<11)
typedef struct { uint32_t required,optional; } app_requirements_t;
uint32_t platform_hardware_bit(const char *name);
uint32_t platform_hardware_available(void);
void platform_hardware_names(uint32_t mask,bool chinese,char *out,size_t capacity);
void platform_audio_init(void);
bool platform_audio_available(void);
void platform_audio_stop(void);
int platform_audio_command(int kind,int value);
void platform_audio_status(void);
#include "scene_render.h"
int hal_scene(const scene_cmd_t *commands,unsigned count,uint32_t background);
void environment_snapshot(environment_t *out);
void environment_init(void);
esp_err_t environment_configure(const char *argument);
void environment_status(void);
void garden_input_init(void);
bool garden_input_ready(void);
void garden_input_tick(void);
void garden_input_status(void);
typedef struct {
    char id[APP_ID_LENGTH];
    char name[40];
    char version[20];
    uint32_t permissions;
    uint32_t wasm_size;
    char path[100];
} app_info_t;
typedef enum { APP_STOPPED, APP_STARTING, APP_RUNNING, APP_CRASHED } app_state_t;
typedef enum { UI_LANGUAGE_ZH, UI_LANGUAGE_EN } ui_language_t;
esp_err_t ui_language_init(void);
ui_language_t ui_language_get(void);
bool ui_language_is_chinese(void);
const char *ui_text(const char *zh,const char *en);
esp_err_t ui_language_set(ui_language_t language);
typedef enum { UI_START, UI_STOP, UI_TOUCH, UI_REFRESH, UI_LIBRARY, UI_SETTINGS, UI_DETAILS, UI_CONFIRM_REMOVE, UI_REMOVE, UI_CONFIRM_CLEAR_DATA, UI_CLEAR_DATA, UI_INSTALL, UI_STORE, UI_STORE_REFRESH, UI_STORE_INSTALL, UI_NETWORK, UI_LANGUAGE, UI_SYSTEM_UPDATE, UI_SYSTEM_CHECK, UI_SYSTEM_INSTALL, UI_SYSTEM_CONFIRM } ui_action_t;
typedef struct { ui_action_t action; int index, x, y; } ui_message_t;
esp_err_t platform_board_init(void);
esp_err_t hal_storage_init(void);
esp_err_t hal_storage_status(void);
void hal_clear(uint32_t rgb);
void hal_rect(int x, int y, int w, int h, uint32_t rgb);
void hal_text(int x, int y, const char *text, uint32_t rgb);
void hal_set_canvas(void *canvas);
bool hal_storage_ready(void);
uint64_t platform_now_ms(void);
esp_err_t apps_init(void);
bool apps_should_seed(void);
esp_err_t apps_mark_seeded(void);
size_t apps_list(app_info_t *out, size_t capacity);
esp_err_t apps_verify(const uint8_t *bytes, size_t size, app_info_t *info,
                      const uint8_t **wasm);
esp_err_t apps_verify_requirements(const uint8_t *bytes,size_t size,app_info_t *info,
                                   const uint8_t **wasm,app_requirements_t *requirements);
esp_err_t apps_install(const uint8_t *bytes, size_t size);
esp_err_t apps_uninstall(const char *id);
esp_err_t apps_clear_data(const char *id);
esp_err_t apps_read(const app_info_t *info, uint8_t **bytes, size_t *size);
esp_err_t apps_migrate_to_nand(void);
esp_err_t apps_check(void);
bool apps_storage_ready(void);
esp_err_t apps_storage_info(uint64_t *total, uint64_t *used, bool *using_nand);
esp_err_t runtime_init(void);
esp_err_t runtime_start(const app_info_t *info);
esp_err_t runtime_start_bytes(const uint8_t *data, size_t size);
void platform_selftest(void);
void diagnostics_init(void);
void diagnostics_mark(const char *phase);
void diagnostics_print(void);
void runtime_stop(void);
void runtime_event(int event, int a, int b);
app_state_t runtime_state(void);
const char *runtime_error(void);
uint32_t runtime_missing_required_hardware(void);
uint32_t runtime_missing_optional_hardware(void);
esp_err_t network_init(void);
esp_err_t network_connect(const char *ssid, const char *password);
bool network_ready(void);
esp_err_t network_install(const char *url);
esp_err_t network_fetch(const char *url,size_t maximum,uint8_t **bytes,size_t *size);
esp_err_t network_fetch_app(const char *url,size_t maximum,size_t expected_size,
                            const char *expected_sha256,uint8_t **bytes,size_t *size);
esp_err_t network_ota(const char *url);
esp_err_t network_ota_checked(const char *url,const char *expected_version);
typedef struct { char version[20]; char url[256]; } system_update_info_t;
esp_err_t system_update_check(system_update_info_t *out);
esp_err_t ota_preflight(void);
esp_err_t ota_confirm_boot(bool applications_ready,esp_err_t network_init_result);
void launcher_init(void);
void launcher_refresh(void);
void launcher_tick(void);
void launcher_post(ui_message_t message);
void launcher_status(const char *text);
size_t catalog_count(void);
const char *catalog_name(size_t index);
const char *catalog_permissions(size_t index);
uint32_t catalog_permission_bits(size_t index);
esp_err_t catalog_install(size_t index);
bool upload_command(const char *command,const char *argument);
void upload_tick(void);
typedef struct { app_info_t app; char file[160],sha256[65]; size_t size; } store_entry_t;
typedef enum {
    STORE_ENTRY_INVALID=-1,
    STORE_ENTRY_INSTALL,
    STORE_ENTRY_UPDATE,
    STORE_ENTRY_REINSTALL,
    STORE_ENTRY_DOWNGRADE
} store_entry_action_t;
void store_init(void);
const char *store_url(void);
esp_err_t store_refresh(const char *url);
size_t store_count(void);
const store_entry_t *store_entry(size_t index);
store_entry_action_t store_entry_action(size_t index);
const char *store_entry_action_name(store_entry_action_t action);
esp_err_t store_install(size_t index);
bool store_selftest(void);

void network_setup_show(void *parent);
void network_setup_close(void);
void network_setup_tick(void);
const char *network_connection_text(void);
void network_saved_ssid(char out[33]);
#define NETWORK_SCAN_LIMIT 20
typedef struct { char ssid[33]; int rssi; bool open,supported; } network_ap_t;
typedef struct {
    uint32_t revision;
    bool busy;
    esp_err_t result;
    unsigned count;
    network_ap_t ap[NETWORK_SCAN_LIMIT];
} network_scan_t;
esp_err_t network_scan_begin(void);
void network_scan_snapshot(network_scan_t *out);

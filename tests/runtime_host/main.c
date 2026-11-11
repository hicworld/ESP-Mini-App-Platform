/* Host test for firmware/main/runtime.c: drive the WASM host API boundary with
 * doubles for WAMR, NVS and the HAL. Package verification is a test double too,
 * so this covers argument validation, permission checks and quota accounting --
 * not cryptography, real interpreter behaviour, or hardware timing. */
#include "platform.h"
#include "wasm_export.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_MEMORY_BYTES (4u * 65536u)
#define KEY_OFFSET 1024u
#define VALUE_OFFSET 2048u

static uint8_t g_app_memory[APP_MEMORY_BYTES];
static unsigned g_tests, g_failures;
#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); g_failures++; return; } } while (0)
#define RUN(fn) do { g_tests++; fn(); } while (0)

/* ---------------------------------------------------------------- WAMR double */

static NativeSymbol *g_symbols;
static uint32_t g_symbol_count;
static const char *g_native_module;
static uint32_t g_max_pages, g_last_instruction_limit, g_last_stack_size;
static bool g_init_fails;
static int g_module_object, g_instance_object, g_init_symbol, g_event_symbol;
static int g_exec_env_object;
static wasm_exec_env_t g_env;

static bool g_load_fails, g_instantiate_fails, g_exec_env_fails;
static uint32_t g_export_count;
static const char *g_export_name;
static uint32_t g_init_param_count = 0, g_init_result_count = 0;
static uint32_t g_event_param_count = 3, g_event_result_count = 0;
static wasm_valkind_t g_event_types[3] = { WASM_I32, WASM_I32, WASM_I32 };
static bool g_have_init = true, g_have_event = true, g_call_wasm_ok = true;
static unsigned g_call_count;
static uint32_t g_last_argc;
static char g_exception[256];
static bool g_exception_set;

struct InstantiationArgs2 { uint32_t stack, heap, pages; };

bool wasm_runtime_full_init(RuntimeInitArgs *args) {
    if (!args) return false;
    g_symbols = args->native_symbols;
    g_symbol_count = args->n_native_symbols;
    g_native_module = args->native_module_name;
    return !g_init_fails;
}
wasm_module_t wasm_runtime_load(const uint8_t *buf, uint32_t size, char *error_buf, uint32_t error_buf_size) {
    (void)buf; (void)size;
    if (g_load_fails) {
        if (error_buf && error_buf_size) snprintf(error_buf, error_buf_size, "stub load failure");
        return NULL;
    }
    return (wasm_module_t)&g_module_object;
}
void wasm_runtime_unload(wasm_module_t module) { (void)module; }
uint32_t wasm_runtime_get_export_count(wasm_module_t module) { (void)module; return g_export_count; }
void wasm_runtime_get_export_type(wasm_module_t module, int32_t index, wasm_export_t *out) {
    (void)module; (void)index;
    if (out) out->name = g_export_name ? g_export_name : "";
}
bool wasm_runtime_instantiation_args_create(struct InstantiationArgs2 **out) {
    struct InstantiationArgs2 *args = calloc(1, sizeof *args);
    if (!args) return false;
    *out = args;
    return true;
}
void wasm_runtime_instantiation_args_set_default_stack_size(struct InstantiationArgs2 *args, uint32_t size) {
    if (args) { args->stack = size; g_last_stack_size = size; }
}
void wasm_runtime_instantiation_args_set_host_managed_heap_size(struct InstantiationArgs2 *args, uint32_t size) {
    if (args) args->heap = size;
}
void wasm_runtime_instantiation_args_set_max_memory_pages(struct InstantiationArgs2 *args, uint32_t pages) {
    if (args) { args->pages = pages; g_max_pages = pages; }
}
void wasm_runtime_instantiation_args_destroy(struct InstantiationArgs2 *args) { free(args); }
wasm_module_inst_t wasm_runtime_instantiate_ex2(wasm_module_t module, struct InstantiationArgs2 *args,
                                                 char *error_buf, uint32_t error_buf_size) {
    (void)module; (void)args;
    if (g_instantiate_fails) {
        if (error_buf && error_buf_size) snprintf(error_buf, error_buf_size, "stub instantiate failure");
        return NULL;
    }
    return (wasm_module_inst_t)&g_instance_object;
}
void wasm_runtime_deinstantiate(wasm_module_inst_t instance) { (void)instance; }
wasm_exec_env_t wasm_runtime_create_exec_env(wasm_module_inst_t instance, uint32_t stack_size) {
    (void)instance; (void)stack_size;
    return g_exec_env_fails ? NULL : (wasm_exec_env_t)&g_exec_env_object;
}
void wasm_runtime_destroy_exec_env(wasm_exec_env_t env) { (void)env; }
wasm_function_inst_t wasm_runtime_lookup_function(wasm_module_inst_t instance, const char *name) {
    (void)instance;
    if (!name) return NULL;
    if (!strcmp(name, "app_init")) return g_have_init ? (wasm_function_inst_t)&g_init_symbol : NULL;
    if (!strcmp(name, "app_event")) return g_have_event ? (wasm_function_inst_t)&g_event_symbol : NULL;
    return NULL;
}
static bool is_event(wasm_function_inst_t func) { return func == (wasm_function_inst_t)&g_event_symbol; }
uint32_t wasm_func_get_param_count(wasm_function_inst_t func, wasm_module_inst_t instance) {
    (void)instance;
    return is_event(func) ? g_event_param_count : g_init_param_count;
}
uint32_t wasm_func_get_result_count(wasm_function_inst_t func, wasm_module_inst_t instance) {
    (void)instance;
    return is_event(func) ? g_event_result_count : g_init_result_count;
}
void wasm_func_get_param_types(wasm_function_inst_t func, wasm_module_inst_t instance, wasm_valkind_t *types) {
    (void)func; (void)instance;
    if (types) memcpy(types, g_event_types, sizeof g_event_types);
}
void wasm_runtime_set_instruction_count_limit(wasm_exec_env_t env, uint32_t count) {
    (void)env; g_last_instruction_limit = count;
}
bool wasm_runtime_call_wasm(wasm_exec_env_t env, wasm_function_inst_t func, uint32_t argc, uint32_t argv[]) {
    (void)env; (void)func; (void)argv;
    g_call_count++; g_last_argc = argc;
    return g_call_wasm_ok;
}
void wasm_runtime_set_exception(wasm_module_inst_t instance, const char *text) {
    (void)instance;
    if (text) { snprintf(g_exception, sizeof g_exception, "%s", text); g_exception_set = true; }
    else g_exception_set = false;
}
const char *wasm_runtime_get_exception(wasm_module_inst_t instance) {
    (void)instance;
    return g_exception_set ? g_exception : NULL;
}
wasm_module_inst_t wasm_runtime_get_module_inst(wasm_exec_env_t env) {
    (void)env; return (wasm_module_inst_t)&g_instance_object;
}
bool wasm_runtime_validate_app_addr(wasm_module_inst_t instance, uint32_t offset, uint32_t size) {
    (void)instance;
    if (offset > APP_MEMORY_BYTES) return false;
    return size <= APP_MEMORY_BYTES - offset;
}
void *wasm_runtime_addr_app_to_native(wasm_module_inst_t instance, uint32_t offset) {
    (void)instance;
    if (offset > APP_MEMORY_BYTES) return NULL;
    return g_app_memory + offset;
}

/* ----------------------------------------------------------------- NVS double */

#define NVS_SLOTS 64
typedef struct { char key[32]; uint8_t value[256]; size_t size; bool used; } nvs_slot_t;
static nvs_slot_t g_nvs[NVS_SLOTS];
static size_t g_stats_free = 1000;
static esp_err_t g_commit_result = ESP_OK;

struct nvs_opaque_iterator { unsigned index; };

void reset_nvs(void) {
    memset(g_nvs, 0, sizeof g_nvs);
    g_stats_free = 1000;
    g_commit_result = ESP_OK;
}
esp_err_t nvs_open(const char *namespace_name, int mode, nvs_handle_t *out) {
    (void)namespace_name; (void)mode;
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size) {
    (void)handle;
    if (!key || !size) return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < NVS_SLOTS; i++) {
        if (g_nvs[i].used && !strcmp(g_nvs[i].key, key)) {
            if (*size < g_nvs[i].size) { *size = g_nvs[i].size; return ESP_ERR_NVS_INVALID_SIZE; }
            memcpy(out, g_nvs[i].value, g_nvs[i].size);
            *size = g_nvs[i].size;
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size) {
    (void)handle;
    if (!key || size > sizeof g_nvs[0].value) return ESP_ERR_INVALID_SIZE;
    for (unsigned i = 0; i < NVS_SLOTS; i++) {
        if (g_nvs[i].used && !strcmp(g_nvs[i].key, key)) {
            memcpy(g_nvs[i].value, value, size);
            g_nvs[i].size = size;
            return ESP_OK;
        }
    }
    for (unsigned i = 0; i < NVS_SLOTS; i++) {
        if (!g_nvs[i].used) {
            snprintf(g_nvs[i].key, sizeof g_nvs[i].key, "%s", key);
            memcpy(g_nvs[i].value, value, size);
            g_nvs[i].size = size;
            g_nvs[i].used = true;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; return g_commit_result; }
esp_err_t nvs_get_stats(const char *partition_name, nvs_stats_t *stats) {
    (void)partition_name;
    if (!stats) return ESP_ERR_INVALID_ARG;
    stats->free_entries = g_stats_free;
    stats->used_entries = 0;
    stats->total_entries = g_stats_free;
    stats->namespace_count = 1;
    return ESP_OK;
}
esp_err_t nvs_entry_find_in_handle(nvs_handle_t handle, unsigned type, nvs_iterator_t *out) {
    (void)handle; (void)type;
    if (!out) return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < NVS_SLOTS; i++) {
        if (g_nvs[i].used) {
            struct nvs_opaque_iterator *it = malloc(sizeof *it);
            if (!it) return ESP_ERR_NO_MEM;
            it->index = i;
            *out = it;
            return ESP_OK;
        }
    }
    *out = NULL;
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_next(nvs_iterator_t *it) {
    if (!it || !*it) return ESP_ERR_NVS_NOT_FOUND;
    struct nvs_opaque_iterator *cursor = *it;
    for (unsigned i = cursor->index + 1; i < NVS_SLOTS; i++) {
        if (g_nvs[i].used) { cursor->index = i; return ESP_OK; }
    }
    free(cursor);
    *it = NULL;
    return ESP_ERR_NVS_NOT_FOUND;
}
void nvs_release_iterator(nvs_iterator_t it) { free(it); }

/* --------------------------------------------------------- platform doubles */

static uint64_t g_now_ms = 1000;
static unsigned g_hal_clear, g_hal_rect, g_hal_text, g_hal_scene;
static int g_hal_scene_result;
static bool g_scene_valid = true;
static unsigned g_audio_commands, g_audio_stops, g_launcher_posts, g_data_opens;
static esp_err_t g_verify_result = ESP_OK;
static app_info_t g_verify_info;
static app_requirements_t g_verify_requirements;
static const uint8_t *g_verify_wasm;
static uint32_t g_available_hardware=HW_DISPLAY|HW_TOUCH;
static environment_t g_environment;
static uint8_t g_package[32];

void hal_clear(uint32_t rgb) { (void)rgb; g_hal_clear++; }
void hal_rect(int x, int y, int w, int h, uint32_t rgb) {
    (void)x; (void)y; (void)w; (void)h; (void)rgb; g_hal_rect++;
}
void hal_text(int x, int y, const char *text, uint32_t rgb) {
    (void)x; (void)y; (void)text; (void)rgb; g_hal_text++;
}
int hal_scene(const scene_cmd_t *commands, unsigned count, uint32_t background) {
    (void)commands; (void)count; (void)background; g_hal_scene++; return g_hal_scene_result;
}
bool scene_validate(const scene_cmd_t *cmd, unsigned count) { (void)cmd; (void)count; return g_scene_valid; }
void environment_snapshot(environment_t *out) { if (out) *out = g_environment; }
uint64_t platform_now_ms(void) { return g_now_ms; }
void platform_audio_stop(void) { g_audio_stops++; }
int platform_audio_command(int kind, int value) { (void)kind; (void)value; g_audio_commands++; return 0; }
void diagnostics_mark(const char *phase) { (void)phase; }
void launcher_post(ui_message_t message) { (void)message; g_launcher_posts++; }
uint32_t platform_hardware_available(void) { return g_available_hardware; }
uint32_t platform_hardware_bit(const char *name) { (void)name;return 0; }
void platform_hardware_names(uint32_t mask,bool chinese,char *out,size_t capacity) {
    (void)chinese;if(out&&capacity)snprintf(out,capacity,"0x%lx",(unsigned long)mask);
}
bool ui_language_is_chinese(void) { return false; }
esp_err_t app_data_open(const char *id, nvs_handle_t *handle_out) {
    (void)id;
    if (!handle_out) return ESP_ERR_INVALID_ARG;
    g_data_opens++;
    *handle_out = 1;
    return ESP_OK;
}
esp_err_t app_data_clear(const char *id) { (void)id; return ESP_OK; }
esp_err_t apps_verify(const uint8_t *bytes, size_t size, app_info_t *info, const uint8_t **wasm) {
    (void)bytes; (void)size;
    if (g_verify_result != ESP_OK) return g_verify_result;
    *info = g_verify_info;
    *wasm = g_verify_wasm;
    return ESP_OK;
}
esp_err_t apps_verify_requirements(const uint8_t *bytes,size_t size,app_info_t *info,const uint8_t **wasm,app_requirements_t *requirements) {
    esp_err_t err=apps_verify(bytes,size,info,wasm);if(err==ESP_OK&&requirements)*requirements=g_verify_requirements;return err;
}
esp_err_t apps_read(const app_info_t *info, uint8_t **bytes, size_t *size) {
    (void)info; (void)bytes; (void)size;
    return ESP_ERR_NOT_FOUND;
}

#include "../../firmware/main/runtime.c"

/* ------------------------------------------------------------ call helpers */

typedef void (*fn_void_i)(wasm_exec_env_t, int32_t);
typedef void (*fn_void_void)(wasm_exec_env_t);
typedef void (*fn_void_iiiii)(wasm_exec_env_t, int32_t, int32_t, int32_t, int32_t, int32_t);
typedef int32_t (*fn_i_void)(wasm_exec_env_t);
typedef int32_t (*fn_i_ii)(wasm_exec_env_t, int32_t, int32_t);
typedef int32_t (*fn_i_iii)(wasm_exec_env_t, int32_t, int32_t, int32_t);
typedef int32_t (*fn_i_iiii)(wasm_exec_env_t, int32_t, int32_t, int32_t, int32_t);

static void *symbol(const char *name, const char *signature) {
    for (uint32_t i = 0; i < g_symbol_count; i++) {
        if (g_symbols[i].name && !strcmp(g_symbols[i].name, name) &&
            g_symbols[i].signature && !strcmp(g_symbols[i].signature, signature))
            return g_symbols[i].func_ptr;
    }
    printf("FAIL: missing native %s%s\n", name, signature);
    g_failures++;
    return NULL;
}
static void call_clear(int32_t rgb) {
    fn_void_i f = (fn_void_i)(void *)symbol("clear", "(i)");
    if (f) f(g_env, rgb);
}
static void call_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb) {
    fn_void_iiiii f = (fn_void_iiiii)(void *)symbol("rect", "(iiiii)");
    if (f) f(g_env, x, y, w, h, rgb);
}
static void call_text(int32_t x, int32_t y, int32_t offset, int32_t len, int32_t rgb) {
    fn_void_iiiii f = (fn_void_iiiii)(void *)symbol("text", "(iiiii)");
    if (f) f(g_env, x, y, offset, len, rgb);
}
static int32_t call_millis(void) {
    fn_i_void f = (fn_i_void)(void *)symbol("millis", "()i");
    return f ? f(g_env) : -1;
}
static int32_t call_hardware(void) {
    fn_i_void f=(fn_i_void)(void *)symbol("hardware","()i");return f?f(g_env):-1;
}
static void call_settings(void) {
    fn_void_void f = (fn_void_void)(void *)symbol("settings", "()");
    if (f) f(g_env);
}
static int32_t call_scene(int32_t offset, int32_t count, int32_t background) {
    fn_i_iii f = (fn_i_iii)(void *)symbol("scene", "(iii)i");
    return f ? f(g_env, offset, count, background) : -99;
}
static int32_t call_environment(int32_t offset, int32_t capacity) {
    fn_i_ii f = (fn_i_ii)(void *)symbol("environment", "(ii)i");
    return f ? f(g_env, offset, capacity) : -99;
}
static int32_t call_audio(int32_t kind, int32_t value) {
    fn_i_ii f = (fn_i_ii)(void *)symbol("audio", "(ii)i");
    return f ? f(g_env, kind, value) : -99;
}
static int32_t store_put(const char *key, const void *value, int32_t length) {
    memcpy(g_app_memory + KEY_OFFSET, key, strlen(key) + 1);
    if (length > 0 && value) memcpy(g_app_memory + VALUE_OFFSET, value, (size_t)length);
    fn_i_iiii f = (fn_i_iiii)(void *)symbol("kv_set", "(iiii)i");
    return f ? f(g_env, (int32_t)KEY_OFFSET, (int32_t)strlen(key), (int32_t)VALUE_OFFSET, length) : -99;
}
static int32_t store_get(const char *key, void *out, int32_t capacity) {
    memcpy(g_app_memory + KEY_OFFSET, key, strlen(key) + 1);
    fn_i_iiii f = (fn_i_iiii)(void *)symbol("kv_get", "(iiii)i");
    int32_t n = f ? f(g_env, (int32_t)KEY_OFFSET, (int32_t)strlen(key), (int32_t)VALUE_OFFSET, capacity) : -99;
    if (n > 0 && out) memcpy(out, g_app_memory + VALUE_OFFSET, (size_t)n);
    return n;
}

/* Reset per-entry quotas the way the host does before delivering an event. */
static void begin_frame(void) {
    g_exception_set = false;
    runtime_event(1, 0, 0);
}
static const uint8_t g_minimal_wasm[8] = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00 };
static const uint32_t ALL_PERMISSIONS = PERM_DISPLAY | PERM_TOUCH | PERM_STORAGE |
                                        PERM_ENVIRONMENT | PERM_INPUT | PERM_AUDIO;

static esp_err_t start_app_with(const char *id, uint32_t permissions,
                                const uint8_t *wasm, uint32_t wasm_size) {
    app_info_t info;
    memset(&info, 0, sizeof info);
    snprintf(info.id, sizeof info.id, "%s", id);
    snprintf(info.name, sizeof info.name, "Host test");
    snprintf(info.version, sizeof info.version, "0.1.0");
    info.permissions = permissions;
    info.wasm_size = wasm_size;
    g_verify_info = info;
    g_verify_wasm = wasm;
    g_verify_result = ESP_OK;
    g_verify_requirements=(app_requirements_t){0};
    return runtime_start_bytes(g_package, sizeof g_package);
}
static esp_err_t start_app_with_hardware(const char *id,uint32_t required,uint32_t optional) {
    g_verify_requirements=(app_requirements_t){.required=required,.optional=optional};
    app_info_t info={0};snprintf(info.id,sizeof info.id,"%s",id);snprintf(info.name,sizeof info.name,"Host test");
    snprintf(info.version,sizeof info.version,"0.1.0");info.permissions=PERM_DISPLAY;info.wasm_size=sizeof g_minimal_wasm;
    g_verify_info=info;g_verify_wasm=g_minimal_wasm;g_verify_result=ESP_OK;
    return runtime_start_bytes(g_package,sizeof g_package);
}
static esp_err_t start_app(const char *id, uint32_t permissions) {
    return start_app_with(id, permissions, g_minimal_wasm, (uint32_t)sizeof g_minimal_wasm);
}

/* ------------------------------------------------------------------- tests */

static void test_registration(void) {
    CHECK(g_symbols != NULL);
    CHECK(g_symbol_count == 11);
    CHECK(g_native_module && !strcmp(g_native_module, "platform"));
    CHECK(symbol("clear", "(i)") != NULL);
    CHECK(symbol("rect", "(iiiii)") != NULL);
    CHECK(symbol("text", "(iiiii)") != NULL);
    CHECK(symbol("millis", "()i") != NULL);
    CHECK(symbol("hardware", "()i") != NULL);
    CHECK(symbol("scene", "(iii)i") != NULL);
    CHECK(symbol("environment", "(ii)i") != NULL);
    CHECK(symbol("settings", "()") != NULL);
    CHECK(symbol("audio", "(ii)i") != NULL);
    CHECK(symbol("kv_get", "(iiii)i") != NULL);
    CHECK(symbol("kv_set", "(iiii)i") != NULL);
}

static void test_hardware_requirements(void) {
    g_available_hardware=HW_DISPLAY|HW_TOUCH;
    unsigned calls=g_call_count;
    CHECK(start_app_with_hardware("com.example.required",HW_DISPLAY|HW_CAMERA,0)==ESP_ERR_NOT_SUPPORTED);
    CHECK(runtime_state()==APP_CRASHED);
    CHECK(runtime_missing_required_hardware()==HW_CAMERA);
    CHECK(g_call_count==calls); /* app_init was never invoked */
    CHECK(start_app_with_hardware("com.example.optional",HW_DISPLAY,HW_CAMERA|HW_SPEAKER)==ESP_OK);
    CHECK(runtime_state()==APP_RUNNING);
    CHECK(runtime_missing_required_hardware()==0);
    CHECK(runtime_missing_optional_hardware()==(HW_CAMERA|HW_SPEAKER));
    CHECK((uint32_t)call_hardware()==g_available_hardware);
    runtime_stop();g_available_hardware=HW_DISPLAY|HW_TOUCH;
}

static void test_launch_limits(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    CHECK(runtime_state() == APP_RUNNING);
    CHECK(g_max_pages == 4);
    CHECK(g_last_instruction_limit == 50000);
    CHECK(g_last_stack_size == 16384);
    /* app_init ran through the guarded call path with no arguments. */
    CHECK(g_call_count >= 1);
    CHECK(g_last_argc == 0);
    runtime_stop();
    CHECK(runtime_state() == APP_STOPPED);
}

static void test_clear_permission(void) {
    CHECK(start_app("com.example.a", PERM_TOUCH) == ESP_OK);
    begin_frame();
    unsigned before = g_hal_clear;
    call_clear(0x123456);
    CHECK(g_hal_clear == before);
    runtime_stop();
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    begin_frame();
    before = g_hal_clear;
    call_clear(0x123456);
    CHECK(g_hal_clear == before + 1);
    runtime_stop();
}

static void test_missing_exports_rejected(void) {
    g_have_init = false;
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) != ESP_OK);
    CHECK(runtime_state() == APP_CRASHED);
    g_have_init = true;
    g_have_event = false;
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) != ESP_OK);
    g_have_event = true;
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    runtime_stop();
}

static void test_event_signature_rejected(void) {
    g_event_param_count = 2;
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) != ESP_OK);
    g_event_param_count = 3;
    g_event_types[2] = WASM_F32;
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) != ESP_OK);
    g_event_types[2] = WASM_I32;
    g_init_result_count = 1;
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) != ESP_OK);
    g_init_result_count = 0;
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    runtime_stop();
}

static void test_start_section_rejected(void) {
    const uint8_t with_start[11] = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x08, 0x01, 0x00 };
    CHECK(start_app_with("com.example.a", ALL_PERMISSIONS, with_start, (uint32_t)sizeof with_start) != ESP_OK);
    CHECK(runtime_state() == APP_CRASHED);
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    runtime_stop();
}

static void test_memory_policy_rejected(void) {
    /* memory section id 5: one memory with initial size 5 pages (> 4 allowed). */
    const uint8_t big_memory[13] = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
                                     0x05, 0x03, 0x01, 0x00, 0x05 };
    CHECK(start_app_with("com.example.a", ALL_PERMISSIONS, big_memory, (uint32_t)sizeof big_memory) != ESP_OK);
    /* import section id 2: importing a memory (kind 1) instead of a function (0). */
    const uint8_t import_memory[15] = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
                                        0x02, 0x06, 0x01, 0x01, 0x6d, 0x01, 0x66 };
    CHECK(start_app_with("com.example.a", ALL_PERMISSIONS, import_memory, (uint32_t)sizeof import_memory) != ESP_OK);
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    runtime_stop();
}

static void test_constructor_exports_rejected(void) {
    g_export_count = 1;
    g_export_name = "_initialize";
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) != ESP_OK);
    g_export_name = "__wasm_call_ctors";
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) != ESP_OK);
    g_export_count = 0;
    g_export_name = NULL;
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    runtime_stop();
}

static void test_rect_bounds(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    begin_frame();
    unsigned before = g_hal_rect;
    call_rect(0, 0, 480, 400, 0);
    CHECK(g_hal_rect == before + 1);
    begin_frame();
    before = g_hal_rect;
    call_rect(-1, 0, 10, 10, 0);
    call_rect(0, -1, 10, 10, 0);
    call_rect(0, 0, 0, 10, 0);
    call_rect(0, 0, 10, 0, 0);
    call_rect(0, 0, 481, 10, 0);
    call_rect(0, 0, 10, 401, 0);
    call_rect(475, 0, 10, 10, 0);
    call_rect(0, 395, 10, 10, 0);
    call_rect(480, 0, 1, 1, 0);
    call_rect(0, 400, 1, 1, 0);
    CHECK(g_hal_rect == before);
    runtime_stop();
}

static void test_draw_quota(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    begin_frame();
    unsigned before = g_hal_rect;
    for (unsigned i = 0; i < 96; i++) call_rect(0, 0, 1, 1, 0);
    CHECK(g_hal_rect == before + 96);
    g_exception_set = false;
    call_rect(0, 0, 1, 1, 0);
    CHECK(g_hal_rect == before + 96);
    CHECK(g_exception_set);
    begin_frame();
    call_rect(0, 0, 1, 1, 0);
    CHECK(g_hal_rect == before + 97);
    runtime_stop();
}

static void test_text_bounds(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    memset(g_app_memory, 'A', 512);
    begin_frame();
    unsigned before = g_hal_text;
    call_text(0, 0, 0, 160, 0);
    CHECK(g_hal_text == before + 1);
    begin_frame();
    before = g_hal_text;
    call_text(0, 0, 0, 161, 0);
    call_text(0, 0, 0, -1, 0);
    call_text(0, 0, -1, 10, 0);
    call_text(0, 0, (int32_t)APP_MEMORY_BYTES, 10, 0);
    call_text(0, 0, (int32_t)APP_MEMORY_BYTES - 4, 10, 0);
    call_text(0, 0, 0, 0, 0);
    CHECK(g_hal_text == before + 1);
    runtime_stop();
}

static void test_text_terminates_within_buffer(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    /* A 160-byte run with no NUL must still be copied safely into a 161 buffer. */
    memset(g_app_memory, 'x', 1024);
    begin_frame();
    unsigned before = g_hal_text;
    call_text(0, 0, 0, 160, 0);
    CHECK(g_hal_text == before + 1);
    runtime_stop();
}

static void test_scene_quota(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    scene_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.kind = SCENE_RECT;
    memcpy(g_app_memory + 256, &cmd, sizeof cmd);
    begin_frame();
    CHECK(call_scene(256, 1, 0x112233) == g_hal_scene_result || g_hal_scene == 1);
    CHECK(g_hal_scene == 1);
    /* scene consumes the whole frame budget: a second call in one entry fails. */
    g_exception_set = false;
    CHECK(call_scene(256, 1, 0) == -1);
    CHECK(g_exception_set);
    CHECK(g_hal_scene == 1);
    begin_frame();
    CHECK(call_scene(256, 0, 0) == -1);
    begin_frame();
    CHECK(call_scene(256, 97, 0) == -1);
    begin_frame();
    CHECK(call_scene(-1, 1, 0) == -1);
    begin_frame();
    CHECK(call_scene(256, 1, 0x1000000) == -1);
    begin_frame();
    CHECK(call_scene((int32_t)APP_MEMORY_BYTES - 8, 1, 0) == -1);
    runtime_stop();
}

static void test_scene_validation_failure(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    scene_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    memcpy(g_app_memory + 256, &cmd, sizeof cmd);
    g_scene_valid = false;
    begin_frame();
    unsigned before = g_hal_scene;
    CHECK(call_scene(256, 1, 0) == -1);
    CHECK(g_hal_scene == before);
    g_scene_valid = true;
    runtime_stop();
}

static void test_environment_permission(void) {
    CHECK(start_app("com.example.a", PERM_DISPLAY) == ESP_OK);
    begin_frame();
    CHECK(call_environment(512, (int32_t)sizeof(environment_t)) == -1);
    runtime_stop();
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    begin_frame();
    g_exception_set = false;
    CHECK(call_environment(512, (int32_t)sizeof(environment_t) - 1) == -1);
    CHECK(g_exception_set);
    begin_frame();
    CHECK(call_environment(-1, (int32_t)sizeof(environment_t)) == -1);
    begin_frame();
    memset(&g_environment, 0, sizeof g_environment);
    g_environment.temperature = 235;
    g_environment.daylight = 1;
    snprintf(g_environment.location, sizeof g_environment.location, "Shenzhen");
    CHECK(call_environment(512, (int32_t)sizeof(environment_t)) == (int32_t)sizeof(environment_t));
    environment_t copied;
    memcpy(&copied, g_app_memory + 512, sizeof copied);
    CHECK(copied.temperature == 235);
    CHECK(copied.daylight == 1);
    CHECK(!strcmp(copied.location, "Shenzhen"));
    runtime_stop();
}

static void test_audio_quota(void) {
    CHECK(start_app("com.example.a", PERM_DISPLAY) == ESP_OK);
    begin_frame();
    unsigned before = g_audio_commands;
    CHECK(call_audio(1, 0) == -1);
    CHECK(g_audio_commands == before);
    runtime_stop();
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    begin_frame();
    before = g_audio_commands;
    for (unsigned i = 0; i < 4; i++) CHECK(call_audio(1, 0) == 0);
    CHECK(g_audio_commands == before + 4);
    g_exception_set = false;
    CHECK(call_audio(1, 0) == -1);
    CHECK(g_audio_commands == before + 4);
    CHECK(g_exception_set);
    begin_frame();
    CHECK(call_audio(1, 0) == 0);
    CHECK(g_audio_commands == before + 5);
    runtime_stop();
}

static void test_settings_opens_host_page(void) {
    CHECK(start_app("com.example.a", PERM_DISPLAY) == ESP_OK);
    unsigned before = g_launcher_posts;
    call_settings();
    CHECK(g_launcher_posts == before + 1);
    runtime_stop();
}

static void test_millis_and_stop(void) {
    CHECK(start_app("com.example.a", PERM_DISPLAY) == ESP_OK);
    g_now_ms = 0xFFFFFFFFull + 250;
    CHECK(call_millis() == (int32_t)(uint32_t)g_now_ms);
    g_now_ms = 1000;
    unsigned before = g_audio_stops;
    runtime_stop();
    CHECK(g_audio_stops == before + 1);
}

static void test_storage_permission(void) {
    unsigned opens = g_data_opens;
    CHECK(start_app("com.example.a", PERM_DISPLAY) == ESP_OK);
    CHECK(g_data_opens == opens);
    begin_frame();
    g_exception_set = false;
    CHECK(store_put("score", "1", 1) == -1);
    CHECK(g_exception_set);
    CHECK(store_get("score", NULL, 0) == -1);
    runtime_stop();
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    CHECK(g_data_opens == opens + 1);
    runtime_stop();
}

static void test_storage_key_validation(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    reset_nvs();
    begin_frame();
    CHECK(store_put("score", "123", 3) == 3);
    begin_frame();
    CHECK(store_put("", "123", 3) == -1);
    CHECK(store_put("a-very-long-key-name", "123", 3) == -1);
    CHECK(store_put("has space", "123", 3) == -1);
    CHECK(store_put("has.dot", "123", 3) == -1);
    CHECK(store_put("__reserved", "123", 3) == -1);
    CHECK(store_put("emoji!", "123", 3) == -1);
    runtime_stop();
}

static void test_storage_value_limits(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    reset_nvs();
    begin_frame();
    char big[129];
    memset(big, 'v', sizeof big);
    CHECK(store_put("blob", big, 129) == -1);
    begin_frame();
    CHECK(store_put("blob", big, 128) == 128);
    char out[128];
    begin_frame();
    CHECK(store_get("blob", out, 128) == 128);
    CHECK(out[127] == 'v');
    begin_frame();
    CHECK(store_get("blob", out, 127) == -2);
    begin_frame();
    CHECK(store_get("absent", out, 128) == 0);
    begin_frame();
    CHECK(store_put("blob", big, -1) == -1);
    runtime_stop();
}

static void test_storage_roundtrip(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    reset_nvs();
    begin_frame();
    CHECK(store_put("best", "42", 2) == 2);
    char out[16] = { 0 };
    begin_frame();
    CHECK(store_get("best", out, 16) == 2);
    CHECK(!strcmp(out, "42"));
    /* Writing the same value again is a no-op that does not consume quota. */
    begin_frame();
    CHECK(store_put("best", "42", 2) == 2);
    runtime_stop();
}

static void test_storage_key_quota(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    reset_nvs();
    char key[8], value[4];
    for (unsigned i = 0; i < 8; i++) {
        g_now_ms += 1000; /* advance past the per-second write window */
        begin_frame();
        snprintf(key, sizeof key, "k%u", i);
        snprintf(value, sizeof value, "%u", i + 100);
        CHECK(store_put(key, value, 3) == 3);
    }
    g_now_ms += 1000;
    begin_frame();
    g_exception_set = false;
    CHECK(store_put("ninth", "999", 3) == -3);
    runtime_stop();
}

static void test_storage_write_window(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    reset_nvs();
    g_now_ms = 5000;
    char key[8], value[8];
    for (unsigned i = 0; i < 4; i++) {
        begin_frame();
        snprintf(key, sizeof key, "w%u", i);
        snprintf(value, sizeof value, "v%u", i);
        CHECK(store_put(key, value, (int32_t)strlen(value)) == (int32_t)strlen(value));
    }
    begin_frame();
    CHECK(store_put("w4", "v4", 2) == -3);
    g_now_ms = 6000;
    begin_frame();
    CHECK(store_put("w4", "v4", 2) == 2);
    g_now_ms = 1000;
    runtime_stop();
}

static void test_storage_space_exhausted(void) {
    CHECK(start_app("com.example.a", ALL_PERMISSIONS) == ESP_OK);
    reset_nvs();
    g_now_ms += 1000;
    g_stats_free = 64;
    begin_frame();
    CHECK(store_put("score", "1", 1) == -3);
    g_stats_free = 1000;
    begin_frame();
    CHECK(store_put("score", "1", 1) == 1);
    runtime_stop();
}

/* Regression: quota and permission state must not survive into the next app. */
static void test_state_reset_between_applications(void) {
    CHECK(start_app("com.example.first", ALL_PERMISSIONS) == ESP_OK);
    reset_nvs();
    g_now_ms = 7000;
    char key[8], value[8];
    for (unsigned i = 0; i < 4; i++) {
        begin_frame();
        snprintf(key, sizeof key, "a%u", i);
        snprintf(value, sizeof value, "x%u", i);
        CHECK(store_put(key, value, 2) == 2);
    }
    runtime_stop();

    /* Same clock window: before the fix the second app inherited the spent
     * write budget and its first write was rejected with -3. */
    CHECK(start_app("com.example.second", ALL_PERMISSIONS) == ESP_OK);
    begin_frame();
    CHECK(store_put("fresh", "y", 1) == 1);
    runtime_stop();

    /* Permissions must not survive a stop either. */
    begin_frame();
    g_exception_set = false;
    CHECK(store_put("after", "z", 1) == -1);
    CHECK(g_exception_set);
}

static void test_state_reset_clears_draw_quota(void) {
    CHECK(start_app("com.example.first", ALL_PERMISSIONS) == ESP_OK);
    begin_frame();
    for (unsigned i = 0; i < 96; i++) call_rect(0, 0, 1, 1, 0);
    runtime_stop();
    CHECK(start_app("com.example.second", ALL_PERMISSIONS) == ESP_OK);
    begin_frame();
    unsigned before = g_hal_rect;
    call_rect(0, 0, 1, 1, 0);
    CHECK(g_hal_rect == before + 1);
    runtime_stop();
}

int main(void) {
    g_env = (wasm_exec_env_t)&g_exec_env_object;
    memset(g_package, 0xAB, sizeof g_package);
    if (runtime_init() != ESP_OK) {
        printf("FAIL: runtime_init\n");
        return 1;
    }
    RUN(test_registration);
    RUN(test_hardware_requirements);
    RUN(test_launch_limits);
    RUN(test_clear_permission);
    RUN(test_missing_exports_rejected);
    RUN(test_event_signature_rejected);
    RUN(test_start_section_rejected);
    RUN(test_memory_policy_rejected);
    RUN(test_constructor_exports_rejected);
    RUN(test_rect_bounds);
    RUN(test_draw_quota);
    RUN(test_text_bounds);
    RUN(test_text_terminates_within_buffer);
    RUN(test_scene_quota);
    RUN(test_scene_validation_failure);
    RUN(test_environment_permission);
    RUN(test_audio_quota);
    RUN(test_settings_opens_host_page);
    RUN(test_millis_and_stop);
    RUN(test_storage_permission);
    RUN(test_storage_key_validation);
    RUN(test_storage_value_limits);
    RUN(test_storage_roundtrip);
    RUN(test_storage_key_quota);
    RUN(test_storage_write_window);
    RUN(test_storage_space_exhausted);
    RUN(test_state_reset_between_applications);
    RUN(test_state_reset_clears_draw_quota);
    printf("RUNTIME HOST RESULT %u/%u\n", g_tests - g_failures, g_tests);
    return g_failures ? 1 : 0;
}

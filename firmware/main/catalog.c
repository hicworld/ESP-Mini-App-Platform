#include "platform.h"
extern const uint8_t hello_start[] asm("_binary_builtin_app_start");
extern const uint8_t hello_end[] asm("_binary_builtin_app_end");
extern const uint8_t touch_start[] asm("_binary_touch_app_start");
extern const uint8_t touch_end[] asm("_binary_touch_app_end");
extern const uint8_t snake_start[] asm("_binary_snake_app_start");
extern const uint8_t snake_end[] asm("_binary_snake_app_end");
size_t catalog_count(void) { return 3; }
const char *catalog_name(size_t index) { return index==0?"Hello WASM":index==1?"Touch Paint":index==2?"Snake":"Unknown"; }
const char *catalog_permissions(size_t index) { return index==0?"Display":index==2?"Display, touch, storage":"Display, touch"; }
uint32_t catalog_permission_bits(size_t index) { return index==0?PERM_DISPLAY:index==2?PERM_DISPLAY|PERM_TOUCH|PERM_STORAGE:index<3?PERM_DISPLAY|PERM_TOUCH:0; }
esp_err_t catalog_install(size_t index) {
    if (index==0) return apps_install(hello_start,hello_end-hello_start);
    if (index==1) return apps_install(touch_start,touch_end-touch_start);
    if (index==2) return apps_install(snake_start,snake_end-snake_start);
    return ESP_ERR_INVALID_ARG;
}

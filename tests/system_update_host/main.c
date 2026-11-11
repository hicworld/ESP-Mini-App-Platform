#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *manifest;
esp_err_t network_fetch(const char *url,size_t maximum,uint8_t **bytes,size_t *size) {
    if (!strstr(url,"/system/latest.json")) return ESP_ERR_INVALID_ARG;
    *size=strlen(manifest);
    if (*size>maximum) return ESP_ERR_INVALID_SIZE;
    *bytes=malloc(*size);
    if (!*bytes) return ESP_ERR_NO_MEM;
    memcpy(*bytes,manifest,*size);
    return ESP_OK;
}

static bool rejects(const char *json) {
    manifest=json;
    system_update_info_t found;
    return system_update_check(&found)==ESP_ERR_INVALID_RESPONSE && !found.version[0] && !found.url[0];
}

int main(void) {
    manifest="{\"schema_version\":1,\"project\":\"esp_mini_platform\",\"version\":\"0.4.10\",\"file\":\"esp_mini_platform-0.4.10.bin\"}";
    system_update_info_t found;
    bool ok=system_update_check(&found)==ESP_OK && !strcmp(found.version,"0.4.10") &&
        !strcmp(found.url,"https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/esp_mini_platform-0.4.10.bin");
    ok=ok && rejects("{\"schema_version\":1,\"project\":\"wrong\",\"version\":\"0.4.10\",\"file\":\"esp_mini_platform-0.4.10.bin\"}");
    ok=ok && rejects("{\"schema_version\":1,\"project\":\"esp_mini_platform\",\"version\":\"0.4.10\",\"file\":\"../evil.bin\"}");
    ok=ok && rejects("{\"schema_version\":1,\"project\":\"esp_mini_platform\",\"version\":\"0.4.10\",\"file\":\"esp_mini_platform-0.4.9.bin\"}");
    ok=ok && rejects("{\"schema_version\":1,\"project\":\"esp_mini_platform\",\"version\":\"0.4.10\",\"version\":\"9.9.9\",\"file\":\"esp_mini_platform-0.4.10.bin\"}");
    ok=ok && rejects("{\"schema_version\":1,\"project\":\"esp_mini_platform\",\"version\":\"0.4.x\",\"file\":\"esp_mini_platform-0.4.x.bin\"}");
    puts(ok?"PASS system update manifest: trusted project, exact versioned file, malformed and duplicate fields rejected":"FAIL system update manifest");
    return ok?0:1;
}

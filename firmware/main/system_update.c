#include "platform.h"
#include "app_validation.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* The release pointer is updated only after its image has been published and
   verified. Files are restricted to this same HTTPS bucket. */
#define UPDATE_BASE "https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/"
#define UPDATE_INDEX UPDATE_BASE "system/latest.json"

static bool unique_keys(const cJSON *object) {
    for (const cJSON *a=object->child;a;a=a->next)
        for (const cJSON *b=a->next;b;b=b->next)
            if (a->string && b->string && !strcmp(a->string,b->string)) return false;
    return true;
}

esp_err_t system_update_check(system_update_info_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));
    uint8_t *bytes=NULL; size_t size=0;
    esp_err_t err=network_fetch(UPDATE_INDEX,2048,&bytes,&size);
    if (err!=ESP_OK) return err;
    /* ParseWithLengthOpts requires a terminating byte when requested. */
    char *json=malloc(size+1);
    if (!json) { free(bytes); return ESP_ERR_NO_MEM; }
    memcpy(json,bytes,size); json[size]=0; free(bytes);
    if (memchr(json,0,size)) { free(json); return ESP_ERR_INVALID_RESPONSE; }
    cJSON *root=cJSON_ParseWithLengthOpts(json,size+1,NULL,true);
    free(json);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *schema=cJSON_GetObjectItemCaseSensitive(root,"schema_version");
    const cJSON *project=cJSON_GetObjectItemCaseSensitive(root,"project");
    const cJSON *version=cJSON_GetObjectItemCaseSensitive(root,"version");
    const cJSON *file=cJSON_GetObjectItemCaseSensitive(root,"file");
    bool ok=cJSON_IsObject(root) && unique_keys(root) && cJSON_IsNumber(schema) && schema->valuedouble==1 &&
        cJSON_IsString(project) && !strcmp(project->valuestring,"esp_mini_platform") &&
        cJSON_IsString(version) && strlen(version->valuestring)<sizeof(out->version) &&
        cJSON_IsString(file) && strlen(file->valuestring)<100;
    unsigned parts[3];
    if (ok) ok=parse_version(version->valuestring,parts);
    if (ok) {
        const char *s=file->valuestring;
        ok=!strncmp(s,"esp_mini_platform-",18) && !strstr(s,"..") && !strchr(s,'/') && !strchr(s,'\\');
        for (const unsigned char *p=(const unsigned char*)s;ok && *p;p++)
            if (!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'||*p=='.')) ok=false;
        char expected[100];
        if (ok) {
            snprintf(expected,sizeof(expected),"esp_mini_platform-%s.bin",version->valuestring);
            ok=!strcmp(s,expected);
        }
    }
    if (ok) {
        strcpy(out->version,version->valuestring);
        ok=snprintf(out->url,sizeof(out->url),"%s%s",UPDATE_BASE,file->valuestring)<(int)sizeof(out->url);
    }
    cJSON_Delete(root);
    if (!ok) { memset(out,0,sizeof(*out)); return ESP_ERR_INVALID_RESPONSE; }
    return ESP_OK;
}

#include "app_validation.h"
#include <stdio.h>
#include "cJSON.h"
#include "mbedtls/pk.h"
#include "psa/crypto.h"

extern const uint8_t trusted_key_pem_start[] asm("_binary_trusted_key_pem_start");
extern const uint8_t trusted_key_pem_end[] asm("_binary_trusted_key_pem_end");
static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static bool str_field(const cJSON *m,const char *key,char *out,size_t size) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(m,key);
    if (!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring)>=size) return false;
    strcpy(out,v->valuestring); return true;
}
static bool number_is(const cJSON *m,const char *key,unsigned expected) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(m,key);
    return cJSON_IsNumber(v) && v->valuedouble==expected;
}
static bool platform_version_supported(const char *s) {
    unsigned needed[3],current[3];
    if (!parse_version(s,needed) || !parse_version(PLATFORM_VERSION,current)) return false;
    for (unsigned i=0;i<3;i++) if (needed[i]!=current[i]) return needed[i]<current[i];
    return true;
}
esp_err_t apps_verify_requirements(const uint8_t *data,size_t size,app_info_t *info,const uint8_t **wasm,app_requirements_t *requirements) {
    if (!data || !info || !wasm || size<16 || memcmp(data,"EMAP",4)) return ESP_ERR_INVALID_ARG;
    uint32_t ml=le32(data+4), wl=le32(data+8), sl=le32(data+12);
    if (!ml || ml>APP_MAX_MANIFEST || wl<8 || wl>APP_MAX_BYTES || sl<8 || sl>80 || size!=16u+ml+sl+wl) return ESP_ERR_INVALID_SIZE;
    diagnostics_mark("verify:crypto-init");
    if (psa_crypto_init()!=PSA_SUCCESS) return ESP_FAIL;
    uint8_t digest[32]; size_t n=0;
    if (psa_hash_compute(PSA_ALG_SHA_256,data+16,ml,digest,sizeof(digest),&n)!=PSA_SUCCESS) return ESP_FAIL;
    diagnostics_mark("verify:public-key");
    mbedtls_pk_context pk; mbedtls_pk_init(&pk);
    int err=mbedtls_pk_parse_public_key(&pk,trusted_key_pem_start,trusted_key_pem_end-trusted_key_pem_start);
    diagnostics_mark("verify:signature");
    if (!err) err=mbedtls_pk_verify(&pk,MBEDTLS_MD_SHA256,digest,32,data+16+ml,sl);
    mbedtls_pk_free(&pk);
    if (err) return ESP_ERR_INVALID_CRC;
    diagnostics_mark("verify:manifest");
    char manifest[APP_MAX_MANIFEST+1]; memcpy(manifest,data+16,ml); manifest[ml]=0;
    if (memchr(manifest,0,ml)) return ESP_ERR_INVALID_ARG;
    const char *end=NULL;
    cJSON *m=cJSON_ParseWithLengthOpts(manifest,ml+1,&end,true);
    if (!m) return ESP_ERR_INVALID_ARG;
    memset(info,0,sizeof(*info));
    char platform_min[20]="0.1.0"; unsigned app_version[3];
    /* Early signed EMAP v1 packages predate platform_min. Their original
     * contract is platform 0.1.0; do not invalidate installed packages. */
    bool minimum_ok=!cJSON_GetObjectItemCaseSensitive(m,"platform_min") ||
        str_field(m,"platform_min",platform_min,sizeof(platform_min));
    bool ok=cJSON_IsObject(m) && number_is(m,"manifest_version",1) && number_is(m,"api_version",1)
        && number_is(m,"wasm_size",wl) && str_field(m,"id",info->id,sizeof(info->id)) && valid_id(info->id)
        && str_field(m,"name",info->name,sizeof(info->name)) && str_field(m,"version",info->version,sizeof(info->version))
        && parse_version(info->version,app_version) && minimum_ok && platform_version_supported(platform_min);
    cJSON *entry=cJSON_GetObjectItemCaseSensitive(m,"entry");
    ok=ok && cJSON_IsString(entry) && !strcmp(entry->valuestring,"main.wasm");
    /* Reject duplicate top-level keys; parsers must agree on signed semantics. */
    for (cJSON *a=m->child; a; a=a->next) for (cJSON *b=a->next;b;b=b->next) if (a->string && b->string && !strcmp(a->string,b->string)) ok=false;
    app_requirements_t parsed={0};
    for (int field=0;field<3;field++) {
        const char *field_name=field==0?"permissions":field==1?"hardware":"optional_hardware";
        cJSON *values=cJSON_GetObjectItemCaseSensitive(m,field_name);
        if (field==2 && !values) continue; /* Signed legacy packages predate optional hardware. */
        if (!cJSON_IsArray(values)) { ok=false; continue; }
        unsigned seen=0;
        cJSON *v; cJSON_ArrayForEach(v,values) {
            unsigned bit=0;
            if (cJSON_IsString(v)) {
                if (!field && !strcmp(v->valuestring,"display")) bit=PERM_DISPLAY;
                else if (!field && !strcmp(v->valuestring,"touch")) bit=PERM_TOUCH;
                else if (!field && !strcmp(v->valuestring,"storage")) bit=PERM_STORAGE;
                else if (!field && !strcmp(v->valuestring,"environment")) bit=PERM_ENVIRONMENT;
                else if (!field && !strcmp(v->valuestring,"input")) bit=PERM_INPUT;
                else if (!field && !strcmp(v->valuestring,"audio")) bit=PERM_AUDIO;
                else if (field) bit=platform_hardware_bit(v->valuestring);
            }
            if (!bit || (seen&bit)) ok=false;
            seen|=bit;
        }
        if (!field) info->permissions=seen;
        else if (field==1) parsed.required=seen;
        else parsed.optional=seen;
    }
    if(parsed.required&parsed.optional)ok=false;
    const uint8_t *payload=data+16+ml+sl;
    if (psa_hash_compute(PSA_ALG_SHA_256,payload,wl,digest,32,&n)!=PSA_SUCCESS) ok=false;
    char hex[65]; for (int i=0;i<32;i++) snprintf(hex+i*2,3,"%02x",digest[i]);
    cJSON *sha=cJSON_GetObjectItemCaseSensitive(m,"sha256");
    ok=ok && cJSON_IsString(sha) && !strcmp(sha->valuestring,hex) && !memcmp(payload,"\0asm\1\0\0\0",8);
    cJSON_Delete(m);
    if (!ok) return ESP_ERR_INVALID_ARG;
    info->wasm_size=wl; *wasm=payload;if(requirements)*requirements=parsed;return ESP_OK;
}
esp_err_t apps_verify(const uint8_t *data,size_t size,app_info_t *info,const uint8_t **wasm) {
    return apps_verify_requirements(data,size,info,wasm,NULL);
}

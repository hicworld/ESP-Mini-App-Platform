#include "platform.h"
#include "download_cache.h"
#include "nand_storage.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_system.h"
#include "esp_netif_sntp.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
static EventGroupHandle_t events;
static atomic_uint retries;
static bool time_service;
static atomic_int connection_state;
static portMUX_TYPE scan_lock=portMUX_INITIALIZER_UNLOCKED;
static network_scan_t scan_result;
static void scan_done(void *data) {
    const wifi_event_sta_scan_done_t *done=data;
    uint16_t count=64;
    wifi_ap_record_t *records=calloc(count,sizeof(*records));
    network_scan_t *next=calloc(1,sizeof(*next));
    esp_err_t err=(!records||!next)?ESP_ERR_NO_MEM:(!done||done->status)?ESP_FAIL:ESP_OK;
    if (err==ESP_OK) err=esp_wifi_scan_get_ap_records(&count,records);
    if (err!=ESP_OK) esp_wifi_clear_ap_list();
    if (err==ESP_OK) for (unsigned i=0;i<count && next->count<NETWORK_SCAN_LIMIT;i++) {
        if (!records[i].ssid[0]) continue;
        bool duplicate=false;
        for (unsigned j=0;j<next->count;j++) if (!strncmp(next->ap[j].ssid,(const char *)records[i].ssid,32)) duplicate=true;
        if (duplicate) continue;
        network_ap_t *ap=&next->ap[next->count++];
        memcpy(ap->ssid,records[i].ssid,32);ap->ssid[32]=0;ap->rssi=records[i].rssi;
        ap->open=records[i].authmode==WIFI_AUTH_OPEN;
        ap->supported=ap->open||records[i].authmode==WIFI_AUTH_WPA2_PSK||records[i].authmode==WIFI_AUTH_WPA_WPA2_PSK||records[i].authmode==WIFI_AUTH_WPA3_PSK||records[i].authmode==WIFI_AUTH_WPA2_WPA3_PSK;
    }
    portENTER_CRITICAL(&scan_lock);
    uint32_t revision=scan_result.revision+1;
    if (next) scan_result=*next;
    else scan_result.count=0;
    scan_result.revision=revision;scan_result.busy=false;scan_result.result=err;
    portEXIT_CRITICAL(&scan_lock);
    free(next);free(records);
}
esp_err_t network_scan_begin(void) {
    if (!events) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&scan_lock);
    if (scan_result.busy) {portEXIT_CRITICAL(&scan_lock);return ESP_ERR_INVALID_STATE;}
    scan_result.busy=true;scan_result.count=0;scan_result.revision++;
    portEXIT_CRITICAL(&scan_lock);
    wifi_scan_config_t cfg={.show_hidden=false};
    esp_err_t err=esp_wifi_scan_start(&cfg,false);
    if (err!=ESP_OK) {
        portENTER_CRITICAL(&scan_lock);
        scan_result.busy=false;scan_result.result=err;scan_result.revision++;
        portEXIT_CRITICAL(&scan_lock);
    }
    return err;
}
void network_scan_snapshot(network_scan_t *out) {
    portENTER_CRITICAL(&scan_lock);*out=scan_result;portEXIT_CRITICAL(&scan_lock);
}
static void net_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg; (void)data;
    if (base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE) {scan_done(data);return;}
    if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(events,1); retries=0;atomic_store(&connection_state,2);
        if (time_service) esp_netif_sntp_start();
    }
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events,1);
        const wifi_event_sta_disconnected_t *disconnected=data;
        if (disconnected && disconnected->reason==WIFI_REASON_ASSOC_LEAVE && atomic_load(&connection_state)==1) return;
        if (++retries<6) {atomic_store(&connection_state,1);esp_wifi_connect();}
        else atomic_store(&connection_state,3);
    }
}
esp_err_t network_init(void) {
    if (nand_storage_ready()) download_cache_recover();
    events=xEventGroupCreate(); if (!events) return ESP_ERR_NO_MEM;
    esp_err_t err=esp_netif_init(); if (err!=ESP_OK) return err;
    err=esp_event_loop_create_default(); if (err!=ESP_OK) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_FAIL;
    esp_sntp_config_t time_cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    time_cfg.start=false;
    time_service=esp_netif_sntp_init(&time_cfg)==ESP_OK;
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&cfg); if (err!=ESP_OK) return err;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,net_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,net_event,NULL));
    esp_wifi_set_storage(WIFI_STORAGE_FLASH); esp_wifi_set_mode(WIFI_MODE_STA);
    err=esp_wifi_start();
    wifi_config_t saved={0};
    if (err==ESP_OK && esp_wifi_get_config(WIFI_IF_STA,&saved)==ESP_OK && saved.sta.ssid[0]) {atomic_store(&connection_state,1);esp_wifi_connect();}
    return err;
}
bool network_ready(void) { return events && (xEventGroupGetBits(events)&1); }
const char *network_connection_text(void) {
    switch (atomic_load(&connection_state)) {
    case 1:return ui_text("正在连接…","Connecting...");
    case 2:return ui_text("已连接","Connected");
    case 3:return ui_text("连接失败，请检查密码和信号","Connection failed. Check password and signal.");
    default:return ui_text("未连接","Disconnected");
    }
}
void network_saved_ssid(char out[33]) {
    wifi_config_t saved={0};out[0]=0;
    if (esp_wifi_get_config(WIFI_IF_STA,&saved)==ESP_OK) {memcpy(out,saved.sta.ssid,32);out[32]=0;}
    volatile unsigned char *p=(volatile unsigned char*)&saved;for (size_t i=0;i<sizeof(saved);i++)p[i]=0;
}
esp_err_t network_connect(const char *ssid,const char *password) {
    if (!events) return ESP_ERR_INVALID_STATE;
    if (!ssid || !password || !strlen(ssid) || strlen(ssid)>32 || (strlen(password)>0 && strlen(password)<8) || strlen(password)>63) return ESP_ERR_INVALID_ARG;
    network_scan_t scan;network_scan_snapshot(&scan);if (scan.busy) return ESP_ERR_INVALID_STATE;
    wifi_config_t cfg={0}; memcpy(cfg.sta.ssid,ssid,strlen(ssid)); memcpy(cfg.sta.password,password,strlen(password));
    cfg.sta.threshold.authmode=password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    atomic_store(&connection_state,1);xEventGroupClearBits(events,1);
    retries=6; esp_wifi_disconnect();
    esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&cfg); retries=0;
    if (err==ESP_OK) err=esp_wifi_connect();
    if (err!=ESP_OK) atomic_store(&connection_state,3);
    volatile unsigned char *wipe=(volatile unsigned char*)&cfg;for (size_t i=0;i<sizeof(cfg);i++)wipe[i]=0;
    return err;
}
esp_err_t network_fetch(const char *url,size_t maximum,uint8_t **output,size_t *output_size) {
    if (!output || !output_size || !maximum) return ESP_ERR_INVALID_ARG;
    *output=NULL; *output_size=0;
    if (!network_ready() || !url || strncmp(url,"https://",8)) return ESP_ERR_INVALID_ARG;
    if (runtime_state()==APP_RUNNING || time(NULL)<1735689600) return ESP_ERR_INVALID_STATE;
    esp_http_client_config_t cfg={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=15000,.disable_auto_redirect=true};
    esp_http_client_handle_t h=esp_http_client_init(&cfg); if (!h) return ESP_ERR_NO_MEM;
    esp_err_t err=esp_http_client_open(h,0); uint8_t *bytes=NULL;
    if (err==ESP_OK) {
        int64_t length=esp_http_client_fetch_headers(h);
        if (esp_http_client_get_status_code(h)!=200 || length<1 || length>maximum) err=ESP_ERR_INVALID_SIZE;
        else {
            bytes=malloc(length); if (!bytes) err=ESP_ERR_NO_MEM;
            else {
                int offset=0;
                while (offset<length) { int n=esp_http_client_read(h,(char*)bytes+offset,(int)length-offset); if (n<=0) { err=ESP_FAIL; break; } offset+=n; }
                if (err==ESP_OK) { *output=bytes; *output_size=(size_t)length; bytes=NULL; }
            }
        }
    }
    free(bytes); esp_http_client_close(h); esp_http_client_cleanup(h); return err;
}

static bool digest_matches(const uint8_t digest[32],const char *expected) {
    if (!expected) return true;
    if (strlen(expected)!=64) return false;
    char hex[65];
    for (unsigned i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",digest[i]);
    return !strcmp(hex,expected);
}

static esp_err_t verify_buffer(const uint8_t *bytes,size_t size,size_t expected_size,
                               const char *expected_sha256) {
    if (expected_size && size!=expected_size) return ESP_ERR_INVALID_SIZE;
    if (!expected_sha256) return ESP_OK;
    uint8_t digest[32]; size_t written=0;
    if (psa_crypto_init()!=PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256,bytes,size,digest,sizeof(digest),&written)!=PSA_SUCCESS ||
        written!=sizeof(digest) || !digest_matches(digest,expected_sha256)) return ESP_ERR_INVALID_CRC;
    return ESP_OK;
}

esp_err_t network_fetch_app(const char *url,size_t maximum,size_t expected_size,
                            const char *expected_sha256,uint8_t **output,size_t *output_size) {
    if (!output || !output_size || !maximum || expected_size>maximum) return ESP_ERR_INVALID_ARG;
    *output=NULL; *output_size=0;
    if (!nand_storage_ready()) {
        esp_err_t err=network_fetch(url,maximum,output,output_size);
        if (err==ESP_OK) err=verify_buffer(*output,*output_size,expected_size,expected_sha256);
        if (err!=ESP_OK) { free(*output); *output=NULL; *output_size=0; }
        return err;
    }

    uint64_t total=0,free_bytes=0; size_t needed=expected_size?expected_size:maximum;
    esp_err_t err=nand_storage_info(&total,&free_bytes);
    if (err!=ESP_OK) return err;
    if (free_bytes<needed+16*1024) return ESP_ERR_NO_MEM;
    if (!network_ready() || !url || strncmp(url,"https://",8) ||
        runtime_state()==APP_RUNNING || time(NULL)<1735689600) return ESP_ERR_INVALID_STATE;

    esp_http_client_config_t cfg={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,
        .timeout_ms=15000,.disable_auto_redirect=true};
    esp_http_client_handle_t h=esp_http_client_init(&cfg); if (!h) return ESP_ERR_NO_MEM;
    download_cache_t cache={0}; psa_hash_operation_t hash=PSA_HASH_OPERATION_INIT;
    bool hash_active=false; uint8_t digest[32]; size_t digest_size=0;
    err=esp_http_client_open(h,0);
    if (err==ESP_OK) {
        int64_t length=esp_http_client_fetch_headers(h);
        if (esp_http_client_get_status_code(h)!=200 || length<1 || length>(int64_t)maximum ||
            (expected_size && length!=(int64_t)expected_size)) err=ESP_ERR_INVALID_SIZE;
        else err=download_cache_begin(&cache,(size_t)length);
        if (err==ESP_OK) {
            if (psa_crypto_init()!=PSA_SUCCESS || psa_hash_setup(&hash,PSA_ALG_SHA_256)!=PSA_SUCCESS) err=ESP_FAIL;
            else hash_active=true;
        }
        uint8_t chunk[2048]; int64_t received=0;
        while (err==ESP_OK && received<length) {
            int request=(int)((length-received)<(int64_t)sizeof(chunk)?length-received:(int64_t)sizeof(chunk));
            int count=esp_http_client_read(h,(char *)chunk,request);
            if (count<=0) { err=ESP_FAIL; break; }
            err=download_cache_append(&cache,chunk,(size_t)count);
            if (err==ESP_OK && psa_hash_update(&hash,chunk,(size_t)count)!=PSA_SUCCESS) err=ESP_FAIL;
            received+=count;
        }
        if (err==ESP_OK && !esp_http_client_is_complete_data_received(h)) err=ESP_FAIL;
        if (err==ESP_OK && psa_hash_finish(&hash,digest,sizeof(digest),&digest_size)!=PSA_SUCCESS) err=ESP_FAIL;
        else if (err==ESP_OK) hash_active=false;
        if (err==ESP_OK && (digest_size!=sizeof(digest) || !digest_matches(digest,expected_sha256))) err=ESP_ERR_INVALID_CRC;
        if (err==ESP_OK) err=download_cache_commit(&cache);
        if (err==ESP_OK) err=download_cache_load(output,output_size);
        if (err==ESP_OK && *output_size!=(size_t)length) err=ESP_ERR_INVALID_SIZE;
    }
    if (hash_active) psa_hash_abort(&hash);
    if (err!=ESP_OK) {
        download_cache_abort(&cache); free(*output); *output=NULL; *output_size=0;
    }
    esp_http_client_close(h); esp_http_client_cleanup(h); return err;
}

esp_err_t network_install(const char *url) {
    uint8_t *bytes=NULL; size_t size=0;
    esp_err_t err=network_fetch_app(url,APP_MAX_BYTES+APP_MAX_MANIFEST+96,0,NULL,&bytes,&size);
    if (err==ESP_OK) err=apps_install(bytes,size);
    free(bytes); return err;
}
esp_err_t network_ota(const char *url) {
    if (!network_ready() || !url || strncmp(url,"https://",8) || runtime_state()==APP_RUNNING || time(NULL)<1735689600) return ESP_ERR_INVALID_STATE;
    esp_err_t preflight=ota_preflight();
    if (preflight!=ESP_OK) return preflight;
    /* Development OTA: HTTPS authenticity. Secure Boot signing is a separate release gate. */
    esp_http_client_config_t http={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=20000,.disable_auto_redirect=true};
    esp_https_ota_config_t cfg={.http_config=&http};
    diagnostics_mark("ota:download");
    esp_err_t err=esp_https_ota(&cfg);
    if (err==ESP_OK) { diagnostics_mark("ota:restart"); esp_restart(); }
    diagnostics_mark("ota:failed");
    return err;
}

esp_err_t network_ota_checked(const char *url,const char *expected_version) {
    if (!network_ready() || !url || strncmp(url,"https://",8) || !expected_version ||
        runtime_state()==APP_RUNNING || time(NULL)<1735689600) return ESP_ERR_INVALID_STATE;
    esp_err_t err=ota_preflight();
    if (err!=ESP_OK) return err;
    esp_http_client_config_t http={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,
        .timeout_ms=20000,.disable_auto_redirect=true};
    esp_https_ota_config_t cfg={.http_config=&http};
    esp_https_ota_handle_t handle=NULL;
    diagnostics_mark("ota:download");
    err=esp_https_ota_begin(&cfg,&handle);
    if (err!=ESP_OK) { diagnostics_mark("ota:failed"); return err; }
    esp_app_desc_t desc={0};
    err=esp_https_ota_get_img_desc(handle,&desc);
    if (err==ESP_OK && (strcmp(desc.project_name,"esp_mini_platform") ||
                        strcmp(desc.version,expected_version))) err=ESP_ERR_INVALID_VERSION;
    if (err==ESP_OK) {
        do { err=esp_https_ota_perform(handle); }
        while (err==ESP_ERR_HTTPS_OTA_IN_PROGRESS);
        if (err==ESP_OK && !esp_https_ota_is_complete_data_received(handle)) err=ESP_ERR_INVALID_SIZE;
    }
    if (err==ESP_OK) err=esp_https_ota_finish(handle);
    else esp_https_ota_abort(handle);
    if (err==ESP_OK) { diagnostics_mark("ota:restart"); esp_restart(); }
    diagnostics_mark("ota:failed");
    return err;
}

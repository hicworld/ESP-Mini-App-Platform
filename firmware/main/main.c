#include "platform.h"
#include "nand_storage.h"
#include "download_cache.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_pthread.h"
#include <pthread.h>
#include "soc/soc.h"
#include "soc/lp_system_reg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern const uint8_t builtin_app_start[] asm("_binary_builtin_app_start");
extern const uint8_t builtin_app_end[] asm("_binary_builtin_app_end");
static bool apps_ready;
static void seed(void) {
    if (!apps_ready || !apps_should_seed()) return;
    if (apps_list(NULL,APP_LIMIT)) {
        printf("builtin: skipped (existing registry)\n");
        printf("builtin marker: %s\n",esp_err_to_name(apps_mark_seeded()));
        return;
    }
    esp_err_t err=apps_install(builtin_app_start,builtin_app_end-builtin_app_start);
    printf("builtin: %s\n",esp_err_to_name(err));
    if (err==ESP_OK) printf("builtin marker: %s\n",esp_err_to_name(apps_mark_seeded()));
}
static void command(char *line) {
    char *arg=strchr(line,' '); if (arg) *arg++=0;
    if (upload_command(line,arg)) return;
    esp_err_t err=ESP_OK;
    if (!strcmp(line,"help")) puts("help | status | hardware | selftest | list | library | settings | language [zh|en] | bundle INDEX | store https://.../index.json | store_refresh | store_list | store_install INDEX | run APP_ID | stop | reboot | download | wifi SSID|PASSWORD | install https://... | remove APP_ID | clear APP_ID | ota https://... | ota_check | format CONFIRM | storage | nand | nand_test | nand_format CONFIRM | apps_migrate | apps_check | cache | upload_begin/upload_chunk/upload_commit/upload_abort");
    else if (!strcmp(line,"status")) diagnostics_print();
    else if (!strcmp(line,"hardware")) {
        char names[180];uint32_t available=platform_hardware_available();
        platform_hardware_names(available,false,names,sizeof(names));printf("HARDWARE available=0x%08lx names=%s\n",(unsigned long)available,names);
    }
    else if (!strcmp(line,"audio_status")) platform_audio_status();
    else if (!strcmp(line,"wifi_scan")) err=network_scan_begin();
    else if (!strcmp(line,"wifi_list")) {
        network_scan_t scan;network_scan_snapshot(&scan);
        printf("WIFI SCAN busy=%u count=%u result=%s\n",scan.busy,scan.count,esp_err_to_name(scan.result));
        for(unsigned i=0;i<scan.count;i++) printf("AP %u rssi=%d open=%u supported=%u ssid=%s\n",i,scan.ap[i].rssi,scan.ap[i].open,scan.ap[i].supported,scan.ap[i].ssid);
    }
    else if (!strcmp(line,"environment")) environment_status();
    else if (!strcmp(line,"location") && arg) err=environment_configure(arg);
    else if (!strcmp(line,"inputs")) garden_input_status();
    else if (!strcmp(line,"input") && arg) {
        int input=0;
        if (!strcmp(arg,"motion")) input=1;
        else if (!strcmp(arg,"a")) input=2;
        else if (!strcmp(arg,"b")) input=3;
        if (!input || runtime_state()!=APP_RUNNING) err=ESP_ERR_INVALID_ARG;
        else runtime_event(3,input,0);
    }
    else if (!strcmp(line,"library")) {
        for (size_t i=0;i<catalog_count();i++) printf("%u %s permissions=%s\n",(unsigned)i,catalog_name(i),catalog_permissions(i));
        launcher_post((ui_message_t){.action=UI_LIBRARY});
    }
    else if (!strcmp(line,"settings")) launcher_post((ui_message_t){.action=UI_SETTINGS});
    else if (!strcmp(line,"language")) {
        if (!arg) printf("language=%s\n",ui_language_is_chinese()?"zh":"en");
        else if (!strcmp(arg,"zh") || !strcmp(arg,"en")) {
            err=ui_language_set(!strcmp(arg,"zh")?UI_LANGUAGE_ZH:UI_LANGUAGE_EN);
            if (err==ESP_OK) launcher_refresh();
        } else err=ESP_ERR_INVALID_ARG;
    }
    else if ((!strcmp(line,"bundle") || !strcmp(line,"store_install")) && arg) {
        char *end; unsigned long index=strtoul(arg,&end,10);
        if (!*arg || arg[0]<'0' || arg[0]>'9' || *end || index>=APP_LIMIT) err=ESP_ERR_INVALID_ARG;
        else err=!strcmp(line,"bundle")?catalog_install(index):store_install(index);
        if (err==ESP_OK) launcher_refresh();
    }
    else if ((!strcmp(line,"store") && arg) || !strcmp(line,"store_refresh")) {
        err=store_refresh(!strcmp(line,"store")?arg:NULL);
        if (err==ESP_OK) { launcher_refresh(); launcher_post((ui_message_t){.action=UI_STORE}); }
    }
    else if (!strcmp(line,"store_list")) {
        printf("Store: %s\n",store_url()[0]?store_url():"not configured");
        for (size_t i=0;i<store_count();i++) { const store_entry_t *e=store_entry(i); printf("%u %s %s %s action=%s\n",(unsigned)i,e->app.id,e->app.version,e->app.name,store_entry_action_name(store_entry_action(i))); }
    }
    else if (!strcmp(line,"reboot") || !strcmp(line,"download")) {
        bool download=!strcmp(line,"download");
        puts(download?"Entering ROM download mode":"Restarting"); fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(200));
        if (download) REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG,LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
        esp_restart();
    }
    else if (!strcmp(line,"selftest")) platform_selftest();
    else if (!strcmp(line,"list")) {
        app_info_t apps[APP_LIMIT]; size_t n=apps_list(apps,APP_LIMIT);
        for (size_t i=0;i<n;i++) printf("%s %s %s\n",apps[i].id,apps[i].version,apps[i].name);
    } else if (!strcmp(line,"run") && arg && apps_ready) {
        app_info_t apps[APP_LIMIT]; size_t n=apps_list(apps,APP_LIMIT); err=ESP_ERR_NOT_FOUND;
        for (size_t i=0;i<n;i++) if (!strcmp(apps[i].id,arg)) {
            launcher_post((ui_message_t){.action=UI_START,.index=(int)i}); err=ESP_OK; break;
        }
    } else if (!strcmp(line,"touch") && arg) {
        unsigned x,y; char extra;
        if (sscanf(arg,"%u %u %c",&x,&y,&extra)!=2 || x>=480 || y>=400) err=ESP_ERR_INVALID_ARG;
        else if (runtime_state()!=APP_RUNNING) err=ESP_ERR_INVALID_STATE;
        else { runtime_event(2,(int)x,(int)y); if (runtime_state()!=APP_RUNNING) err=ESP_FAIL; }
    } else if (!strcmp(line,"stop")) launcher_post((ui_message_t){.action=UI_STOP});
    else if (!strcmp(line,"wifi") && arg) { char *password=strchr(arg,'|'); if (password) { *password++=0; err=network_connect(arg,password); } else err=ESP_ERR_INVALID_ARG; }
    else if (!strcmp(line,"install") && arg && apps_ready) { err=network_install(arg); if (err==ESP_OK) launcher_refresh(); }
    else if (!strcmp(line,"remove") && arg && apps_ready) { err=apps_uninstall(arg); if (err==ESP_OK) launcher_refresh(); }
    else if (!strcmp(line,"clear") && arg && apps_ready) err=apps_clear_data(arg);
    else if (!strcmp(line,"ota") && arg) err=network_ota(arg);
    else if (!strcmp(line,"ota_check")) err=ota_preflight();
    else if (!strcmp(line,"nand")) err=nand_storage_status();
    else if (!strcmp(line,"storage")) err=hal_storage_status();
    else if (!strcmp(line,"apps_check")) err=apps_check();
    else if (!strcmp(line,"nand_test")) err=nand_storage_selftest();
    else if (!strcmp(line,"apps_migrate")) err=apps_migrate_to_nand();
    else if (!strcmp(line,"cache")) err=download_cache_status();
    else if (!strcmp(line,"nand_format") && arg && !strcmp(arg,"CONFIRM") && runtime_state()==APP_STOPPED) {
        diagnostics_mark("nand:format");
        err=nand_storage_format();
    }
    else if (!strcmp(line,"format") && arg && !strcmp(arg,"CONFIRM") && !hal_storage_ready()) {
        diagnostics_mark("storage:format");
        err=esp_spiffs_format("apps");
        if (err==ESP_OK) err=hal_storage_init();
        if (err==ESP_OK) { err=apps_init(); apps_ready=err==ESP_OK; seed(); launcher_refresh(); }
    } else err=ESP_ERR_INVALID_ARG;
    printf("result: %s\n",esp_err_to_name(err));
}
static void *platform_main(void *argument) {
    (void)argument;
    diagnostics_init();
    ESP_ERROR_CHECK(nvs_flash_init()); /* Do not erase existing NVS on an error. */
    esp_err_t language=ui_language_init();
    ESP_LOGI("platform","UI language: %s (%s)",ui_language_is_chinese()?"zh":"en",esp_err_to_name(language));
    ESP_ERROR_CHECK(platform_board_init());
    ESP_ERROR_CHECK(runtime_init());
    esp_err_t storage=hal_storage_init();
    ESP_LOGI("platform","NOR mount: %s (optional legacy storage)",esp_err_to_name(storage));
    esp_err_t nand=nand_storage_init();
    ESP_LOGI("platform","NAND mount: %s (format is never automatic)",esp_err_to_name(nand));
    if (storage==ESP_OK || nand_storage_ready()) apps_ready=apps_init()==ESP_OK;
    if (apps_ready && nand_storage_ready()) {
        esp_err_t migration=apps_migrate_to_nand();
        ESP_LOGI("platform","App package migration: %s",esp_err_to_name(migration));
    }
    seed(); diagnostics_mark("launcher:init"); launcher_init();
    esp_err_t net=network_init(); ESP_LOGI("platform","network init: %s",esp_err_to_name(net));
    store_init();
    environment_init(); garden_input_init();platform_audio_init();
    ESP_ERROR_CHECK(ota_confirm_boot(apps_ready,net));
    puts("ESP Mini Platform ready. Type help. No auto-format is performed.");
    diagnostics_mark("ready");
    int fd=fileno(stdin); fcntl(fd,F_SETFL,O_NONBLOCK);
    char line[384]; size_t used=0; bool overflow=false;
    while (true) {
        launcher_tick();
        garden_input_tick();
        upload_tick();
        char c; for (unsigned batch=0;batch<64 && read(fd,&c,1)==1;batch++) {
            if (c=='\n' || c=='\r') {
                if (used && !overflow) { line[used]=0; command(line); }
                used=0; overflow=false; memset(line,0,sizeof(line));
            } else if (used<sizeof(line)-1) line[used++]=c; else overflow=true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return NULL;
}
void app_main(void) {
    /* WAMR's ESP-IDF port calls pthread_self(). A plain FreeRTOS task has
     * no pthread identity and triggers an IDF assertion on WASM execution. */
    esp_pthread_cfg_t cfg=esp_pthread_get_default_config();
    cfg.thread_name="mini_platform";
    cfg.stack_size=24*1024;
    cfg.prio=1;
    cfg.pin_to_core=0;
    ESP_ERROR_CHECK(esp_pthread_set_cfg(&cfg));
    pthread_t worker;
    int err=pthread_create(&worker,NULL,platform_main,NULL);
    ESP_ERROR_CHECK(err==0?ESP_OK:ESP_FAIL);
    ESP_ERROR_CHECK(pthread_detach(worker)==0?ESP_OK:ESP_FAIL);
}

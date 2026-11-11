#include "platform.h"
#include "app_data.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "wasm_export.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
static void *runtime_pool;
static wasm_module_t module;
static wasm_module_inst_t instance;
static wasm_exec_env_t execution;
static wasm_function_inst_t event_fn;
static uint8_t *package;
static nvs_handle_t data_handle;
static uint64_t write_window;
static unsigned writes_in_window;
static app_state_t state;
static char error_text[160];
static uint32_t permissions, draw_calls;
static uint32_t missing_required_hardware,missing_optional_hardware;
static unsigned audio_calls;
static int32_t native_hardware(wasm_exec_env_t e) { (void)e;return (int32_t)platform_hardware_available(); }
static int32_t native_audio(wasm_exec_env_t e,int32_t kind,int32_t value) {
    if(!(permissions&PERM_AUDIO)||++audio_calls>4) {wasm_runtime_set_exception(wasm_runtime_get_module_inst(e),"audio permission/quota exceeded");return -1;}
    return platform_audio_command(kind,value);
}
static bool allowed(wasm_exec_env_t e) {
    if (!(permissions&PERM_DISPLAY) || ++draw_calls>96) {
        wasm_runtime_set_exception(wasm_runtime_get_module_inst(e),"display permission/quota exceeded"); return false;
    }
    return true;
}
static void native_clear(wasm_exec_env_t e,int32_t rgb) { if (allowed(e)) hal_clear(rgb); }
static void native_rect(wasm_exec_env_t e,int32_t x,int32_t y,int32_t w,int32_t h,int32_t rgb) {
    if (allowed(e) && x>=0 && y>=0 && w>0 && h>0 && x<480 && y<400 && w<=480-x && h<=400-y) hal_rect(x,y,w,h,rgb);
}
static void native_text(wasm_exec_env_t e,int32_t x,int32_t y,int32_t offset,int32_t len,int32_t rgb) {
    if (!allowed(e)) return;
    if (len<0 || len>160 || !wasm_runtime_validate_app_addr(instance,(uint32_t)offset,(uint32_t)len)) {
        wasm_runtime_set_exception(instance,"invalid text buffer"); return;
    }
    char text[161]; memcpy(text,wasm_runtime_addr_app_to_native(instance,(uint32_t)offset),len); text[len]=0;
    if (x>=0 && y>=0 && x<480 && y<400) hal_text(x,y,text,rgb);
}
static int32_t native_millis(wasm_exec_env_t e) { (void)e; return (uint32_t)platform_now_ms(); }
static void native_settings(wasm_exec_env_t e) { (void)e; launcher_post((ui_message_t){.action=UI_NETWORK}); }
static int32_t native_scene(wasm_exec_env_t e,int32_t offset,int32_t count,int32_t background) {
    if (!allowed(e)) return -1;
    if (draw_calls!=1 || count<1 || count>96 || offset<0 || (uint32_t)background>0xffffff ||
        !wasm_runtime_validate_app_addr(instance,(uint32_t)offset,(uint32_t)count*sizeof(scene_cmd_t))) {
        wasm_runtime_set_exception(instance,"invalid scene buffer/quota"); return -1;
    }
    /* Module is single-threaded; copy before validation and host drawing. */
    scene_cmd_t *copy=malloc((size_t)count*sizeof(*copy));
    if (!copy) return -1;
    memcpy(copy,wasm_runtime_addr_app_to_native(instance,(uint32_t)offset),(size_t)count*sizeof(*copy));
    bool valid=scene_validate(copy,(unsigned)count); draw_calls=96;
    int result=valid?hal_scene(copy,(unsigned)count,(uint32_t)background):-1;
    free(copy);
    if (!valid) wasm_runtime_set_exception(instance,"invalid scene commands");
    return result;
}
static int32_t native_environment(wasm_exec_env_t e,int32_t offset,int32_t capacity) {
    (void)e;
    if (!(permissions&PERM_ENVIRONMENT)) { wasm_runtime_set_exception(instance,"environment permission denied"); return -1; }
    if (offset<0 || capacity<(int32_t)sizeof(environment_t) ||
        !wasm_runtime_validate_app_addr(instance,(uint32_t)offset,sizeof(environment_t))) {
        wasm_runtime_set_exception(instance,"invalid environment buffer"); return -1;
    }
    environment_t snapshot; environment_snapshot(&snapshot);
    memcpy(wasm_runtime_addr_app_to_native(instance,(uint32_t)offset),&snapshot,sizeof(snapshot));
    return sizeof(snapshot);
}
static bool valid_key(int32_t offset,int32_t length,char key[16]) {
    if (length<1 || length>15 || offset<0 ||
        !wasm_runtime_validate_app_addr(instance,(uint32_t)offset,(uint32_t)length)) return false;
    memcpy(key,wasm_runtime_addr_app_to_native(instance,(uint32_t)offset),(size_t)length); key[length]=0;
    if (length>=2 && key[0]=='_' && key[1]=='_') return false;
    for (int i=0;i<length;i++) {
        unsigned char c=(unsigned char)key[i];
        if (!(c>='a'&&c<='z') && !(c>='A'&&c<='Z') && !(c>='0'&&c<='9') && c!='_' && c!='-') return false;
    }
    return true;
}
static int32_t native_kv_get(wasm_exec_env_t e,int32_t key_offset,int32_t key_length,int32_t value_offset,int32_t capacity) {
    (void)e;
    if (!(permissions&PERM_STORAGE) || !data_handle) { wasm_runtime_set_exception(instance,"storage permission denied"); return -1; }
    char key[16]; if (!valid_key(key_offset,key_length,key) || capacity<0 || capacity>128 || value_offset<0) {
        wasm_runtime_set_exception(instance,"invalid storage arguments"); return -1;
    }
    uint8_t temp[128]; size_t size=sizeof(temp); esp_err_t err=nvs_get_blob(data_handle,key,temp,&size);
    if (err==ESP_ERR_NVS_NOT_FOUND) return 0;
    if (err!=ESP_OK) return -1;
    if (size>(size_t)capacity || !wasm_runtime_validate_app_addr(instance,(uint32_t)value_offset,(uint32_t)size)) return -2;
    memcpy(wasm_runtime_addr_app_to_native(instance,(uint32_t)value_offset),temp,size); return (int32_t)size;
}
static int32_t native_kv_set(wasm_exec_env_t e,int32_t key_offset,int32_t key_length,int32_t value_offset,int32_t value_length) {
    (void)e;
    if (!(permissions&PERM_STORAGE) || !data_handle) { wasm_runtime_set_exception(instance,"storage permission denied"); return -1; }
    char key[16]; if (!valid_key(key_offset,key_length,key) || value_length<0 || value_length>128 || value_offset<0 ||
        !wasm_runtime_validate_app_addr(instance,(uint32_t)value_offset,(uint32_t)value_length)) {
        wasm_runtime_set_exception(instance,"invalid storage arguments"); return -1;
    }
    const void *value=wasm_runtime_addr_app_to_native(instance,(uint32_t)value_offset);
    uint8_t old[128]; size_t old_size=sizeof(old);
    esp_err_t previous=nvs_get_blob(data_handle,key,old,&old_size);
    if (previous==ESP_OK && old_size==(size_t)value_length && !memcmp(old,value,old_size)) return value_length;
    if (previous!=ESP_OK && previous!=ESP_ERR_NVS_NOT_FOUND) return -1;
    if (previous==ESP_ERR_NVS_NOT_FOUND) {
        nvs_iterator_t iterator=NULL; unsigned count=0;
        esp_err_t scan=nvs_entry_find_in_handle(data_handle,NVS_TYPE_BLOB,&iterator);
        while (scan==ESP_OK) { count++; scan=nvs_entry_next(&iterator); }
        nvs_release_iterator(iterator);
        if (scan!=ESP_ERR_NVS_NOT_FOUND || count>=8) return -3;
    }
    nvs_stats_t stats;
    if (nvs_get_stats("nvs",&stats)!=ESP_OK || stats.free_entries<128) return -3;
    uint64_t window=platform_now_ms()/1000;
    if (window!=write_window) { write_window=window; writes_in_window=0; }
    if (writes_in_window>=4) return -3;
    writes_in_window++;
    esp_err_t err=nvs_set_blob(data_handle,key,value,(size_t)value_length);
    if (err==ESP_OK) err=nvs_commit(data_handle);
    return err==ESP_OK?value_length:-1;
}
static NativeSymbol symbols[]={
    {"clear",native_clear,"(i)",NULL}, {"rect",native_rect,"(iiiii)",NULL},
    {"text",native_text,"(iiiii)",NULL}, {"millis",native_millis,"()i",NULL},
    {"hardware",native_hardware,"()i",NULL},
    {"scene",native_scene,"(iii)i",NULL}, {"environment",native_environment,"(ii)i",NULL},
    {"settings",native_settings,"()",NULL},
    {"audio",native_audio,"(ii)i",NULL},
    {"kv_get",native_kv_get,"(iiii)i",NULL}, {"kv_set",native_kv_set,"(iiii)i",NULL}
};
esp_err_t runtime_init(void) {
    if (runtime_pool) return ESP_ERR_INVALID_STATE;
    runtime_pool=heap_caps_malloc(1024*1024,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!runtime_pool) return ESP_ERR_NO_MEM;
    RuntimeInitArgs args={0}; args.mem_alloc_type=Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf=runtime_pool;
    args.mem_alloc_option.pool.heap_size=1024*1024;
    args.native_module_name="platform"; args.native_symbols=symbols; args.n_native_symbols=sizeof(symbols)/sizeof(symbols[0]);
    if (wasm_runtime_full_init(&args)) return ESP_OK;
    free(runtime_pool); runtime_pool=NULL; return ESP_FAIL;
}
void runtime_stop(void) {
    platform_audio_stop();
    if (data_handle) { nvs_close(data_handle); data_handle=0; }
    if (execution) wasm_runtime_destroy_exec_env(execution);
    if (instance) wasm_runtime_deinstantiate(instance);
    if (module) wasm_runtime_unload(module);
    free(package); package=NULL; execution=NULL; instance=NULL; module=NULL; event_fn=NULL;
    /* Quota and permission state belongs to the application that just ended.
     * Leaving them set would let one app's write-window budget or permission
     * bits leak into the next launch. */
    write_window=0; writes_in_window=0; permissions=0; draw_calls=audio_calls=0;
    state=APP_STOPPED;
}
static void crashed(void) {
    const char *exception=instance?wasm_runtime_get_exception(instance):NULL;
    if (exception) snprintf(error_text,sizeof(error_text),"%s",exception);
    ESP_LOGE("runtime","%s",error_text); runtime_stop(); state=APP_CRASHED;
}
static bool read_u32(const uint8_t *bytes,size_t end,size_t *cursor,uint32_t *value) {
    *value=0;
    for (unsigned shift=0;shift<=28;shift+=7) {
        if (*cursor>=end) return false;
        uint8_t byte=bytes[(*cursor)++];
        if (shift==28 && (byte&0xf0)) return false;
        *value|=(uint32_t)(byte&127)<<shift;
        if (!(byte&128)) return true;
    }
    return false;
}
static bool memory_policy(const uint8_t *bytes,size_t cursor,size_t end,uint8_t section) {
    uint32_t count;
    if (!read_u32(bytes,end,&cursor,&count)) return false;
    if (section==5) {
        if (count>1) return false;
        for (uint32_t i=0;i<count;i++) {
            uint32_t flags,minimum,maximum;
            if (!read_u32(bytes,end,&cursor,&flags) || flags>1 ||
                !read_u32(bytes,end,&cursor,&minimum) || minimum>4) return false;
            if ((flags&1) && (!read_u32(bytes,end,&cursor,&maximum) || maximum<minimum)) return false;
        }
    } else {
        /* ABI v1 supports function imports only. This also prevents imported
         * memories bypassing the declared initial-memory check above. */
        for (uint32_t i=0;i<count;i++) {
            for (unsigned name=0;name<2;name++) {
                uint32_t length;
                if (!read_u32(bytes,end,&cursor,&length) || length>end-cursor) return false;
                cursor+=length;
            }
            uint32_t type;
            if (cursor>=end || bytes[cursor++]!=0 || !read_u32(bytes,end,&cursor,&type)) return false;
        }
    }
    return cursor==end;
}
static esp_err_t start_owned_package(size_t size) {
    state=APP_STARTING; error_text[0]=0;
    missing_required_hardware=missing_optional_hardware=0;
    app_info_t verified;app_requirements_t requirements={0}; const uint8_t *wasm;
    if (apps_verify_requirements(package,size,&verified,&wasm,&requirements)!=ESP_OK) { snprintf(error_text,sizeof(error_text),"Package verification failed"); crashed(); return ESP_FAIL; }
    uint32_t available=platform_hardware_available();
    missing_required_hardware=requirements.required&~available;
    missing_optional_hardware=requirements.optional&~available;
    if(missing_required_hardware) {
        char names[96];platform_hardware_names(missing_required_hardware,ui_language_is_chinese(),names,sizeof(names));
        snprintf(error_text,sizeof(error_text),ui_language_is_chinese()?"无法启动：缺少必要外设：%s":"Cannot start: required hardware missing: %s",names);
        crashed();return ESP_ERR_NOT_SUPPORTED;
    }
    permissions=verified.permissions;
    if (permissions&PERM_STORAGE && app_data_open(verified.id,&data_handle)!=ESP_OK) {
        snprintf(error_text,sizeof(error_text),"Application data unavailable"); crashed(); return ESP_FAIL;
    }
    /* A WASM start section executes during instantiation, before our execution
     * budget is attached. Reject it in the v1 reactor ABI. */
    size_t cursor=8;
    while (cursor<verified.wasm_size) {
        uint8_t section=wasm[cursor++]; uint32_t length=0; unsigned shift=0; uint8_t b=0;
        do {
            if (cursor>=verified.wasm_size || shift>=35) { snprintf(error_text,sizeof(error_text),"Invalid section"); crashed(); return ESP_FAIL; }
            b=wasm[cursor++];
            if (shift==28 && (b&0xf0)) { crashed(); return ESP_FAIL; }
            length|=(uint32_t)(b&127)<<shift; shift+=7;
        } while (b&128);
        if (section==8 || length>verified.wasm_size-cursor) { snprintf(error_text,sizeof(error_text),"Start section/invalid length rejected"); crashed(); return ESP_FAIL; }
        /* WAMR's maximum-pages option may raise itself to the initial size;
         * its loader may also optimize unused memory declarations away. */
        if ((section==2 || section==5) && !memory_policy(wasm,cursor,cursor+length,section)) {
            snprintf(error_text,sizeof(error_text),"WASM memory/import policy rejected"); crashed(); return ESP_FAIL;
        }
        cursor+=length;
    }
    diagnostics_mark("runtime:load");
    module=wasm_runtime_load((uint8_t*)wasm,verified.wasm_size,error_text,sizeof(error_text));
    if (!module) { crashed(); return ESP_FAIL; }
    for (uint32_t i=0;i<wasm_runtime_get_export_count(module);i++) {
        wasm_export_t symbol; wasm_runtime_get_export_type(module,(int32_t)i,&symbol);
        if (!strcmp(symbol.name,"__post_instantiate") || !strcmp(symbol.name,"__wasm_call_ctors") || !strcmp(symbol.name,"_initialize")) {
            snprintf(error_text,sizeof(error_text),"Automatic constructor exports are not permitted"); crashed(); return ESP_FAIL;
        }
    }
    struct InstantiationArgs2 *args=NULL;
    if (!wasm_runtime_instantiation_args_create(&args)) { crashed(); return ESP_ERR_NO_MEM; }
    wasm_runtime_instantiation_args_set_default_stack_size(args,16384);
    wasm_runtime_instantiation_args_set_host_managed_heap_size(args,0);
    wasm_runtime_instantiation_args_set_max_memory_pages(args,4);
    diagnostics_mark("runtime:instantiate");
    instance=wasm_runtime_instantiate_ex2(module,args,error_text,sizeof(error_text));
    wasm_runtime_instantiation_args_destroy(args);
    if (!instance) { crashed(); return ESP_FAIL; }
    diagnostics_mark("runtime:exec-env");
    execution=wasm_runtime_create_exec_env(instance,16384);
    wasm_function_inst_t init=wasm_runtime_lookup_function(instance,"app_init");
    event_fn=wasm_runtime_lookup_function(instance,"app_event");
    if (!execution || !init || !event_fn) { snprintf(error_text,sizeof(error_text),"Required ABI exports missing"); crashed(); return ESP_FAIL; }
    if (wasm_func_get_param_count(init,instance)!=0 || wasm_func_get_result_count(init,instance)!=0 ||
        wasm_func_get_param_count(event_fn,instance)!=3 || wasm_func_get_result_count(event_fn,instance)!=0) {
        snprintf(error_text,sizeof(error_text),"Wrong ABI entry signature"); crashed(); return ESP_FAIL;
    }
    wasm_valkind_t types[3]; wasm_func_get_param_types(event_fn,instance,types);
    if (types[0]!=WASM_I32 || types[1]!=WASM_I32 || types[2]!=WASM_I32) {
        snprintf(error_text,sizeof(error_text),"Events require three i32 arguments"); crashed(); return ESP_FAIL;
    }
    diagnostics_mark("runtime:app-init");
    draw_calls=audio_calls=0; wasm_runtime_set_instruction_count_limit(execution,50000);
    if (!wasm_runtime_call_wasm(execution,init,0,NULL)) { crashed(); return ESP_FAIL; }
    state=APP_RUNNING; return ESP_OK;
}
esp_err_t runtime_start(const app_info_t *info) {
    runtime_stop(); size_t size=0;
    esp_err_t err=apps_read(info,&package,&size);
    if (err!=ESP_OK) { snprintf(error_text,sizeof(error_text),"Cannot read application package"); state=APP_CRASHED; return err; }
    return start_owned_package(size);
}
esp_err_t runtime_start_bytes(const uint8_t *data,size_t size) {
    runtime_stop();
    if (!data || size<16 || size>APP_MAX_BYTES+APP_MAX_MANIFEST+96) return ESP_ERR_INVALID_SIZE;
    package=malloc(size); if (!package) return ESP_ERR_NO_MEM;
    memcpy(package,data,size); return start_owned_package(size);
}
void runtime_event(int event,int a,int b) {
    if (state!=APP_RUNNING || (event==2 && !(permissions&PERM_TOUCH))) return;
    if (event==3 && !(permissions&PERM_INPUT)) return;
    uint32_t args[3]={(uint32_t)event,(uint32_t)a,(uint32_t)b}; draw_calls=audio_calls=0;
    wasm_runtime_set_instruction_count_limit(execution,50000);
    if (!wasm_runtime_call_wasm(execution,event_fn,3,args)) crashed();
}
app_state_t runtime_state(void) { return state; }
const char *runtime_error(void) { return error_text; }
uint32_t runtime_missing_required_hardware(void) { return missing_required_hardware; }
uint32_t runtime_missing_optional_hardware(void) { return missing_optional_hardware; }

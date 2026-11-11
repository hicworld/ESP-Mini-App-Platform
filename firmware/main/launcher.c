#include "platform.h"
#include "app_validation.h"
#include "nand_storage.h"
#include "bsp/esp_mosaico.h"
#include "freertos/queue.h"
#include "esp_spiffs.h"
#include <stdio.h>
#include <string.h>
LV_FONT_DECLARE(garden_font);
static QueueHandle_t queue;
static lv_obj_t *root,*status;
static lv_obj_t *hardware_notice;
static app_info_t listed[APP_LIMIT];
static size_t count;
static uint64_t last_tick,last_status;
static uint64_t hardware_notice_until;
static unsigned page;
static int selected=-1;
static system_update_info_t available_system_update;
static bool has_system_update;
#define T(zh,en) ui_text((zh),(en))
void launcher_post(ui_message_t msg) { if (queue) xQueueSend(queue,&msg,0); }
static void action(lv_event_t *e) {
    uintptr_t data=(uintptr_t)lv_event_get_user_data(e);
    launcher_post((ui_message_t){.action=(ui_action_t)(data>>8),.index=(int)(data&255)});
}
static void touched(lv_event_t *e) {
    (void)e; lv_indev_t *input=lv_indev_active(); lv_point_t p;
    if (input) { lv_indev_get_point(input,&p); launcher_post((ui_message_t){.action=UI_TOUCH,.x=p.x,.y=p.y-64}); }
}
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y) {
    lv_obj_t *l=lv_label_create(parent); lv_label_set_text(l,text); lv_obj_set_pos(l,x,y);
    /* Labels inside transparent list containers otherwise inherit the
       container theme's dark text and disappear on the dark launcher. */
    lv_obj_set_style_text_color(l,lv_color_hex(0xe9eff8),0);
    lv_obj_set_width(l,430); lv_obj_remove_flag(l,LV_OBJ_FLAG_CLICKABLE); return l;
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int width,ui_action_t event,int index) {
    lv_obj_t *b=lv_button_create(parent); lv_obj_set_pos(b,x,y); lv_obj_set_size(b,width,44);
    lv_obj_t *l=lv_label_create(b); lv_label_set_text(l,text); lv_obj_center(l);
    lv_obj_add_event_cb(b,action,LV_EVENT_CLICKED,(void*)(((uintptr_t)event<<8)|(unsigned)index));
    return b;
}
static void permissions_text(char *out,size_t capacity,uint32_t permissions) {
    const uint32_t bits[]={PERM_DISPLAY,PERM_TOUCH,PERM_STORAGE,PERM_ENVIRONMENT,PERM_INPUT,PERM_AUDIO};
    const char *names[]={T("显示","display"),T("触控","touch"),T("存储","storage"),T("环境","environment"),T("输入","input"),T("音频","audio")};
    size_t used=0;out[0]=0;
    for(unsigned i=0;i<sizeof(bits)/sizeof(bits[0]);i++) if(permissions&bits[i]) {
        int written=snprintf(out+used,capacity-used,"%s%s",used?T("、",", "):"",names[i]);
        if(written<0 || (size_t)written>=capacity-used)break;
        used+=(size_t)written;
    }
}
static const char *store_action_name(store_entry_action_t action) {
    if(action==STORE_ENTRY_INSTALL)return T("安装","Install");
    if(action==STORE_ENTRY_UPDATE)return T("更新","Update");
    if(action==STORE_ENTRY_REINSTALL)return T("重装","Reinstall");
    if(action==STORE_ENTRY_DOWNGRADE)return T("旧版本","Older");
    return T("无效","Invalid");
}
static void reset_screen(const char *title,bool nav) {
    network_setup_close(); hal_set_canvas(NULL); lv_obj_clean(root); status=NULL;hardware_notice=NULL;hardware_notice_until=0;
    /* Keep CJK glyphs available for language choices and app names in English. */
    lv_obj_set_style_text_font(root,&garden_font,0);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x101d32),0);
    lv_obj_set_style_text_color(root,lv_color_hex(0xe9eff8),0);
    if (queue) xQueueReset(queue); /* discard clicks referring to the previous screen */
    label(root,title,20,16);
    if (nav) {
        button(root,T("首页","Home"),14,426,142,UI_STOP,0);
        button(root,T("应用库","App Library"),169,426,142,UI_LIBRARY,0);
        button(root,T("设置","Settings"),324,426,142,UI_SETTINGS,0);
    }
}
void launcher_status(const char *text) {
    if (status && bsp_display_lock(200)) { lv_label_set_text(status,text); bsp_display_unlock(); }
}
void launcher_refresh(void) {
    if (!root || !bsp_display_lock(500)) return;
    page=0; selected=-1; reset_screen(T("ESP MINI / 首页","ESP MINI / HOME"),true);
    status=label(root,apps_storage_ready()?T("选择应用，或打开应用库", "Choose an app or open App Library"):T("存储不可用，请连接 USB 设置", "Storage unavailable. Connect USB for setup."),20,55);
    count=apps_list(listed,APP_LIMIT);
    lv_obj_t *area=lv_obj_create(root); lv_obj_set_pos(area,10,112); lv_obj_set_size(area,460,295);
    lv_obj_set_style_bg_opa(area,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(area,0,0);
    if (!count) label(area,T("尚未安装应用，请打开应用库添加", "No apps installed. Open App Library to add one."),0,0);
    for (size_t i=0;i<count;i++) button(area,listed[i].name,(i%2)*218,(i/2)*58,204,UI_DETAILS,(int)i);
    bsp_display_unlock();
}
static void show_library(void) {
    runtime_stop(); if (!bsp_display_lock(500)) return;
    page=1; reset_screen(T("应用库","APP LIBRARY"),true);
    status=label(root,T("已签名示例","Signed examples"),20,55);
    lv_obj_set_width(status,290); button(root,T("在线应用","Online"),330,48,130,UI_STORE,0);
    for (size_t i=0;i<catalog_count();i++) {
        char info[128],permissions[96];permissions_text(permissions,sizeof(permissions),catalog_permission_bits(i));
        snprintf(info,sizeof(info),"%s\n%s: %s",catalog_name(i),T("权限","Permissions"),permissions);
        lv_obj_set_width(label(root,info,20,112+85*i),290);
        button(root,T("安装","Install"),330,120+85*i,130,UI_INSTALL,(int)i);
    }
    label(root,T("也可通过 USB 或 HTTPS 安装更多应用", "USB upload or HTTPS for more apps."),20,390);
    bsp_display_unlock();
}
static void show_store(void) {
    runtime_stop(); if (!bsp_display_lock(500)) return;
    page=5; reset_screen(T("在线应用","ONLINE APPS"),true);
    button(root,T("本地应用","Local apps"),20,52,210,UI_LIBRARY,0);
    button(root,T("刷新","Refresh"),250,52,210,UI_STORE_REFRESH,0);
    status=label(root,T("点击刷新加载在线目录", "Refresh to load the online catalog."),20,108);
    lv_obj_t *area=lv_obj_create(root); lv_obj_set_pos(area,10,166); lv_obj_set_size(area,460,244);
    lv_obj_set_style_bg_opa(area,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(area,0,0);
    for (size_t i=0;i<store_count();i++) {
        const store_entry_t *e=store_entry(i); char info[180],permissions[96];
        store_entry_action_t action=store_entry_action(i);
        permissions_text(permissions,sizeof(permissions),e->app.permissions);
        snprintf(info,sizeof(info),"%s %s\n%s: %s",e->app.name,e->app.version,T("权限","Permissions"),permissions);
        lv_obj_set_width(label(area,info,0,100*i),280);
        lv_obj_t *install=button(area,store_action_name(action),302,8+100*i,110,UI_STORE_INSTALL,(int)i);
        if (action==STORE_ENTRY_DOWNGRADE || action==STORE_ENTRY_INVALID) lv_obj_add_state(install,LV_STATE_DISABLED);
    }
    bsp_display_unlock();
}
static void settings_text(void) {
    uint64_t total=0,used=0; bool using_nand=false; char text[336];
    esp_err_t err=apps_storage_info(&total,&used,&using_nand);
    snprintf(text,sizeof(text),ui_language_is_chinese()?"平台 %s\nWi-Fi：%s\n已安装应用：%u\n应用包（%s）：%u / %u KiB\nNAND：%s":"Platform %s\nWi-Fi: %s\nInstalled apps: %u\nApp packages (%s): %u / %u KiB\nNAND: %s",
        PLATFORM_VERSION,network_ready()?T("已连接","Connected"):T("未连接","Disconnected"),(unsigned)apps_list(NULL,APP_LIMIT),
        err==ESP_OK?(using_nand?"NAND":"NOR"):T("不可用","unavailable"),
        err==ESP_OK?(unsigned)(used/1024):0,err==ESP_OK?(unsigned)(total/1024):0,
        nand_storage_ready()?T("就绪","Ready"):nand_storage_initialized()?T("已检测 / 未挂载","Detected / not mounted"):T("不可用","Unavailable"));
    launcher_status(text);
}
static void show_settings(void) {
    runtime_stop(); if (!bsp_display_lock(500)) return;
    page=2; reset_screen(T("设置","SETTINGS"),true); status=label(root,"",20,76);
    label(root,T("显示语言","Display language"),20,210);
    lv_obj_t *zh=button(root,"中文",20,246,210,UI_LANGUAGE,UI_LANGUAGE_ZH);
    lv_obj_t *en=button(root,"English",250,246,210,UI_LANGUAGE,UI_LANGUAGE_EN);
    lv_obj_set_style_bg_color(ui_language_is_chinese()?zh:en,lv_color_hex(0x37a88c),0);
    button(root,T("Wi-Fi / 天气","Wi-Fi / Weather"),20,302,440,UI_NETWORK,0);
    button(root,T("系统更新","System update"),20,358,440,UI_SYSTEM_UPDATE,0);
    bsp_display_unlock(); settings_text();
}
static void show_system_update(const char *message) {
    if (!bsp_display_lock(500)) return;
    page=7; reset_screen(T("系统更新","SYSTEM UPDATE"),true);
    char versions[100];
    snprintf(versions,sizeof(versions),ui_language_is_chinese()?"当前版本：%s\n最新版本：%s":"Current: %s\nLatest: %s",
        PLATFORM_VERSION,available_system_update.version[0]?available_system_update.version:T("未检查","Not checked"));
    label(root,versions,20,65);
    status=label(root,message?message:T("连接 Wi-Fi 后检查系统更新","Connect to Wi-Fi, then check for updates."),20,143);
    button(root,T("检查更新","Check for updates"),20,255,440,UI_SYSTEM_CHECK,0);
    if (has_system_update) button(root,T("安装更新","Install update"),20,315,440,UI_SYSTEM_INSTALL,0);
    bsp_display_unlock();
}
static void confirm_system_update(void) {
    if (!has_system_update || !bsp_display_lock(500)) return;
    page=8; reset_screen(T("确认系统更新","CONFIRM UPDATE"),true);
    char description[150];
    snprintf(description,sizeof(description),ui_language_is_chinese()?"%s → %s\n下载完成后设备会自动重启。":"%s → %s\nThe device restarts after installation.",
        PLATFORM_VERSION,available_system_update.version);
    label(root,description,20,95);
    status=label(root,T("请保持供电与 Wi-Fi 连接。","Keep power and Wi-Fi connected."),20,185);
    button(root,T("取消","Cancel"),20,310,210,UI_SYSTEM_UPDATE,0);
    button(root,T("确认升级","Update now"),250,310,210,UI_SYSTEM_CONFIRM,0);
    bsp_display_unlock();
}
static void show_details(int index,int confirm) {
    if (index<0 || (size_t)index>=count || !bsp_display_lock(500)) return;
    selected=index; page=3; reset_screen(confirm==1?T("删除应用？","REMOVE APP?"):confirm==2?T("清除应用数据？","CLEAR APP DATA?"):T("应用详情","APP DETAILS"),true);
    app_info_t *app=&listed[index]; char details[280],permissions[96];permissions_text(permissions,sizeof(permissions),app->permissions);
    snprintf(details,sizeof(details),ui_language_is_chinese()?"%s\n%s\n版本 %s\n\n权限：%s":"%s\n%s\nVersion %s\n\nPermissions: %s",app->name,app->id,app->version,permissions);
    label(root,details,20,82);
    status=label(root,confirm==1?T("卸载时会保留应用数据", "App data is kept when uninstalling."):confirm==2?T("此操作会永久删除应用保存的数据", "This permanently deletes saved app data."):"",20,276);
    if (confirm) {
        button(root,T("取消","Cancel"),20,350,210,UI_DETAILS,index);
        button(root,confirm==1?T("删除","Remove"):T("清除数据","Clear Data"),250,350,210,confirm==1?UI_REMOVE:UI_CLEAR_DATA,index);
    } else if (app->permissions&PERM_STORAGE) {
        button(root,T("打开","Open"),10,350,140,UI_START,index);
        button(root,T("清除数据","Clear Data"),170,350,140,UI_CONFIRM_CLEAR_DATA,index);
        button(root,T("卸载","Uninstall"),330,350,140,UI_CONFIRM_REMOVE,index);
    } else {
        button(root,T("打开","Open"),20,350,210,UI_START,index);
        button(root,T("卸载","Uninstall"),250,350,210,UI_CONFIRM_REMOVE,index);
    }
    bsp_display_unlock();
}
static void show_app(int index) {
    if (index<0 || (size_t)index>=count || !bsp_display_lock(500)) return;
    page=4; reset_screen(listed[index].name,false);
    button(root,T("首页","Home"),360,8,105,UI_STOP,0);
    lv_obj_t *canvas=lv_obj_create(root); lv_obj_remove_style_all(canvas); lv_obj_set_pos(canvas,0,64); lv_obj_set_size(canvas,480,400);
    lv_obj_set_style_bg_opa(canvas,LV_OPA_COVER,0); lv_obj_add_flag(canvas,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas,touched,LV_EVENT_CLICKED,NULL); hal_set_canvas(canvas); bsp_display_unlock();
    if (runtime_start(&listed[index])!=ESP_OK) { launcher_refresh(); launcher_status(runtime_error());return; }
    uint32_t missing=runtime_missing_optional_hardware();
    if(missing&&bsp_display_lock(200)) {
        char names[96],text[150];platform_hardware_names(missing,ui_language_is_chinese(),names,sizeof(names));
        snprintf(text,sizeof(text),ui_language_is_chinese()?"缺少可选外设：%s，部分功能不可用":"Optional hardware missing: %s. Some features unavailable",names);
        hardware_notice=lv_label_create(root);lv_label_set_text(hardware_notice,text);
        lv_obj_set_pos(hardware_notice,14,392);lv_obj_set_size(hardware_notice,452,58);
        lv_obj_set_style_bg_color(hardware_notice,lv_color_hex(0x463b1b),0);lv_obj_set_style_bg_opa(hardware_notice,LV_OPA_90,0);
        lv_obj_set_style_pad_all(hardware_notice,8,0);lv_obj_set_style_radius(hardware_notice,8,0);
        lv_obj_set_style_text_color(hardware_notice,lv_color_hex(0xffe49a),0);
        lv_obj_remove_flag(hardware_notice,LV_OBJ_FLAG_CLICKABLE);hardware_notice_until=platform_now_ms()+5000;
        bsp_display_unlock();
    }
}
void launcher_init(void) {
    queue=xQueueCreate(16,sizeof(ui_message_t)); configASSERT(queue);
    bsp_display_lock(-1); root=lv_screen_active(); lv_obj_set_style_bg_color(root,lv_color_hex(0x101d32),0);
    lv_obj_set_style_text_color(root,lv_color_hex(0xe9eff8),0); lv_obj_set_style_text_font(root,&lv_font_montserrat_20,0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE); bsp_display_unlock(); launcher_refresh();
}
void launcher_tick(void) {
    ui_message_t msg;
    if (xQueueReceive(queue,&msg,0)==pdTRUE) {
        if (msg.action==UI_START) show_app(msg.index);
        else if (msg.action==UI_STOP) { runtime_stop(); launcher_refresh(); }
        else if (msg.action==UI_TOUCH) runtime_event(2,msg.x,msg.y);
        else if (msg.action==UI_REFRESH) { if (runtime_state()!=APP_RUNNING) launcher_refresh(); }
        else if (msg.action==UI_LIBRARY) show_library();
        else if (msg.action==UI_SETTINGS) show_settings();
        else if (msg.action==UI_SYSTEM_UPDATE) {
            if (page!=8) { has_system_update=false; memset(&available_system_update,0,sizeof(available_system_update)); }
            show_system_update(NULL);
        }
        else if (msg.action==UI_SYSTEM_CHECK && page==7) {
            launcher_status(T("正在检查更新…","Checking for updates..."));
            system_update_info_t found={0}; esp_err_t err=system_update_check(&found);
            int order=0;
            has_system_update=err==ESP_OK && compare_versions(found.version,PLATFORM_VERSION,&order) && order>0;
            if (err==ESP_OK) available_system_update=found;
            else memset(&available_system_update,0,sizeof(available_system_update));
            show_system_update(!network_ready()?T("请先连接 Wi-Fi。","Connect to Wi-Fi first."):
                err==ESP_ERR_INVALID_SIZE?T("更新目录尚未发布或不可用。","Update catalog is unavailable."):
                err!=ESP_OK?esp_err_to_name(err):has_system_update?
                T("发现新版本，可以安装。","A newer version is available."):
                T("当前已是最新版本。","The system is up to date."));
        }
        else if (msg.action==UI_SYSTEM_INSTALL && page==7) confirm_system_update();
        else if (msg.action==UI_SYSTEM_CONFIRM && page==8 && has_system_update) {
            launcher_status(T("正在下载并安装，请勿断电…","Downloading and installing. Keep power on..."));
            esp_err_t err=network_ota_checked(available_system_update.url,available_system_update.version);
            launcher_status(esp_err_to_name(err));
        }
        else if (msg.action==UI_LANGUAGE && (msg.index==UI_LANGUAGE_ZH || msg.index==UI_LANGUAGE_EN)) {
            esp_err_t err=ui_language_set((ui_language_t)msg.index);show_settings();
            if(err!=ESP_OK)launcher_status(esp_err_to_name(err));
        }
        else if (msg.action==UI_NETWORK) {
            runtime_stop();if (bsp_display_lock(500)) {page=6;reset_screen("",false);network_setup_show(root);bsp_display_unlock();}
        }
        else if (msg.action==UI_STORE) show_store();
        else if (msg.action==UI_STORE_REFRESH && page==5) {
            launcher_status(T("正在加载目录…","Loading directory...")); esp_err_t err=store_refresh(NULL);
            show_store(); launcher_status(err==ESP_OK?T("目录已加载","Directory loaded."):esp_err_to_name(err));
        } else if (msg.action==UI_STORE_INSTALL && page==5) {
            store_entry_action_t action=store_entry_action(msg.index);
            launcher_status(action==STORE_ENTRY_UPDATE?T("正在下载更新…","Downloading update..."):T("正在下载并校验…","Downloading and verifying..."));
            esp_err_t err=store_install(msg.index);
            show_store();
            launcher_status(err!=ESP_OK?esp_err_to_name(err):action==STORE_ENTRY_UPDATE?T("更新完成，请返回首页启动", "Updated. Open Home to launch."):action==STORE_ENTRY_REINSTALL?T("重装完成，请返回首页启动", "Reinstalled. Open Home to launch."):T("安装完成，请返回首页启动", "Installed. Open Home to launch."));
        }
        else if (msg.action==UI_DETAILS) show_details(msg.index,0);
        else if (msg.action==UI_CONFIRM_REMOVE) show_details(msg.index,1);
        else if (msg.action==UI_CONFIRM_CLEAR_DATA) show_details(msg.index,2);
        else if (msg.action==UI_REMOVE && msg.index==selected && selected>=0 && (size_t)selected<count) {
            esp_err_t err=apps_uninstall(listed[selected].id); launcher_refresh(); launcher_status(esp_err_to_name(err));
        } else if (msg.action==UI_CLEAR_DATA && msg.index==selected && selected>=0 && (size_t)selected<count) {
            esp_err_t err=apps_clear_data(listed[selected].id); show_details(selected,0);
            launcher_status(err==ESP_OK?T("保存的数据已清除","Saved data cleared."):esp_err_to_name(err));
        } else if (msg.action==UI_INSTALL && page==1) {
            launcher_status(T("正在安装…","Installing...")); esp_err_t err=catalog_install(msg.index);
            show_library(); launcher_status(err==ESP_OK?T("安装完成，请返回首页启动","Installed. Open Home to launch."):esp_err_to_name(err));
        }
    }
    if (page==6) network_setup_tick();
    uint64_t now=platform_now_ms();
    if(hardware_notice&&now>=hardware_notice_until&&bsp_display_lock(50)) {
        lv_obj_delete(hardware_notice);hardware_notice=NULL;hardware_notice_until=0;bsp_display_unlock();
    }
    if (now-last_tick>=50) { last_tick=now; runtime_event(1,(uint32_t)now,0); }
    if (page==2 && now-last_status>=1000) { last_status=now; settings_text(); }
    if (runtime_state()==APP_CRASHED) { runtime_stop(); launcher_refresh(); launcher_status(runtime_error()); }
}

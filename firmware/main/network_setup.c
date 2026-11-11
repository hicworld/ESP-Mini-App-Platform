#include "platform.h"
#include "bsp/esp_mosaico.h"
#include <string.h>
#include <stdio.h>
LV_FONT_DECLARE(garden_font);
enum { WIFI_LIST, WIFI_PASSWORD, CITY };
static lv_obj_t *screen,*keyboard,*password,*latitude,*longitude,*city_name,*message,*networks;
static unsigned page;
static bool scan_pending,connect_pending,location_pending;
static network_scan_t visible_scan;
static bool visible_connected;
static char visible_ssid[33];
static network_ap_t selected_ap;
static char pending_ssid[33],pending_password[64],pending_location[128],result_text[96];
#define T(zh,en) ui_text((zh),(en))
static const struct { const char *name,*lat,*lon; } cities[]={
    {"","",""},{"Guangzhou","23.1291","113.2644"},{"Shenzhen","22.5431","114.0579"},
    {"Beijing","39.9042","116.4074"},{"Shanghai","31.2304","121.4737"},
    {"Hangzhou","30.2741","120.1551"},{"Chengdu","30.5728","104.0668"},{"","",""}
};
static void render(unsigned next);
static void wipe(void *data,size_t n) {volatile unsigned char *p=data;while(n--)*p++=0;}
static lv_obj_t *caption(const char *text,int x,int y,int width) {
    lv_obj_t *o=lv_label_create(screen);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,width);return o;
}
static void focus_field(lv_event_t *e) {
    if (!keyboard) return;
    lv_keyboard_set_textarea(keyboard,lv_event_get_target_obj(e));
    lv_obj_remove_flag(keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(keyboard);
}
static void keyboard_done(lv_event_t *e) {
    if (lv_event_get_code(e)==LV_EVENT_READY || lv_event_get_code(e)==LV_EVENT_CANCEL) {
        lv_keyboard_set_textarea(keyboard,NULL);lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    }
}
static lv_obj_t *field(int x,int y,int width,unsigned limit,bool secret,const char *chars) {
    lv_obj_t *o=lv_textarea_create(screen);
    lv_textarea_set_one_line(o,true);lv_textarea_set_max_length(o,limit);lv_textarea_set_password_mode(o,secret);
    if (secret) lv_textarea_set_password_bullet(o,"*");
    /* one_line changes height; set our touch target AFTER it. Never inherit text color. */
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,44);
    lv_obj_set_style_bg_color(o,lv_color_hex(0xffffff),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(o,lv_color_hex(0x273421),0);
    lv_obj_set_style_border_color(o,lv_color_hex(0x82916c),0);lv_obj_set_style_border_width(o,2,0);
    lv_obj_set_style_radius(o,8,0);lv_obj_set_style_pad_all(o,10,0);
    lv_obj_set_style_text_color(o,lv_color_hex(0x69735f),LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x273421),LV_PART_CURSOR);
    if (chars) lv_textarea_set_accepted_chars(o,chars);
    lv_obj_add_event_cb(o,focus_field,LV_EVENT_CLICKED,NULL);return o;
}
static void back(lv_event_t *e) { (void)e;launcher_post((ui_message_t){.action=UI_SETTINGS}); }
static void wifi_tab(lv_event_t *e) { (void)e;render(WIFI_LIST); }
static void city_tab(lv_event_t *e) { (void)e;render(CITY); }
static void refresh(lv_event_t *e) { (void)e;scan_pending=true;result_text[0]=0;lv_label_set_text(message,T("正在搜索附近 Wi-Fi…","Searching for nearby Wi-Fi...")); }
static lv_obj_t *control(const char *text,int x,int y,int width,lv_event_cb_t cb) {
    lv_obj_t *o=lv_button_create(screen);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,38);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x6e8059),0);lv_obj_set_style_text_color(o,lv_color_hex(0xffffff),0);
    lv_obj_t *label=lv_label_create(o);lv_label_set_text(label,text);lv_obj_center(label);
    lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,NULL);return o;
}
static void connect_clicked(lv_event_t *e) {
    (void)e;const char *p=selected_ap.open?"":lv_textarea_get_text(password);size_t length=strlen(p);
    if (!selected_ap.supported) {snprintf(result_text,sizeof(result_text),"%s",T("暂不支持此网络的认证方式","This network security is not supported."));lv_label_set_text(message,result_text);return;}
    if (!selected_ap.open && (length<8||length>63)) {snprintf(result_text,sizeof(result_text),"%s",T("密码需 8–63 字节，请重新输入","Password must contain 8-63 bytes."));lv_label_set_text(message,result_text);return;}
    memcpy(pending_ssid,selected_ap.ssid,sizeof(pending_ssid));memcpy(pending_password,p,length+1);connect_pending=true;
    if (password) lv_textarea_set_text(password,"");
    if (keyboard) lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    result_text[0]=0;lv_label_set_text(message,T("正在连接…","Connecting..."));
}
static void password_visibility(lv_event_t *e) {
    (void)e;lv_textarea_set_password_mode(password,!lv_textarea_get_password_mode(password));
}
static void choose_network(lv_event_t *e) {
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (index>=visible_scan.count) return;
    selected_ap=visible_scan.ap[index];render(WIFI_PASSWORD);
}
static void add_network_row(const network_ap_t *ap,const char *ssid,unsigned index,unsigned position,bool connected) {
        lv_obj_t *row=lv_button_create(networks);lv_obj_set_pos(row,0,(int)position*62);lv_obj_set_size(row,426,56);
        lv_obj_set_style_bg_color(row,lv_color_hex(0xffffff),0);lv_obj_set_style_text_color(row,lv_color_hex(0x37432f),0);
        lv_obj_set_style_border_color(row,lv_color_hex(0xc2ccb4),0);lv_obj_set_style_border_width(row,1,0);lv_obj_set_style_pad_all(row,8,0);
        lv_obj_t *name=lv_label_create(row);lv_obj_set_width(name,400);lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);lv_label_set_text(name,ssid);
        char text[96];
        if (connected && !ap) snprintf(text,sizeof(text),"%s",T("已连接","Connected"));
        else snprintf(text,sizeof(text),"%s   %s (%d dBm)",connected?T("已连接","Connected"):!ap->supported?T("暂不支持","Unsupported"):ap->open?T("无需密码","Open"):T("需要密码","Password"),ap->rssi>=-60?T("信号强","Strong"):ap->rssi>=-75?T("信号中","Medium"):T("信号弱","Weak"),ap->rssi);
        lv_obj_t *info=lv_label_create(row);lv_label_set_text(info,text);lv_obj_set_pos(info,0,23);
        if (!connected) lv_obj_add_event_cb(row,choose_network,LV_EVENT_CLICKED,(void*)(uintptr_t)index);
}
static void fill_networks(void) {
    lv_obj_clean(networks);
    unsigned position=0;
    if (visible_connected) {
        const network_ap_t *connected_ap=NULL;
        for(unsigned i=0;i<visible_scan.count;i++) if (!strcmp(visible_scan.ap[i].ssid,visible_ssid)) {connected_ap=&visible_scan.ap[i];break;}
        add_network_row(connected_ap,visible_ssid,0,position++,true);
    }
    for(unsigned i=0;i<visible_scan.count;i++) {
        const network_ap_t *ap=&visible_scan.ap[i];
        if (visible_connected && !strcmp(ap->ssid,visible_ssid)) continue;
        add_network_row(ap,ap->ssid,i,position++,false);
    }
    if (visible_scan.busy) lv_label_set_text(message,T("正在搜索附近 Wi-Fi…","Searching for nearby Wi-Fi..."));
    else if (visible_scan.result!=ESP_OK) lv_label_set_text(message,T("搜索未完成，请稍后点刷新","Scan failed. Tap Refresh to retry."));
    else if (!visible_scan.count && !visible_connected) lv_label_set_text(message,T("未发现网络，请靠近路由器后刷新","No networks found. Move closer and refresh."));
    else lv_label_set_text(message,T("点击网络名称连接","Tap a network to connect."));
}
static void city_changed(lv_event_t *e) {
    unsigned index=lv_dropdown_get_selected(lv_event_get_target_obj(e));if(index>=sizeof(cities)/sizeof(cities[0]))return;
    lv_textarea_set_text(latitude,cities[index].lat);lv_textarea_set_text(longitude,cities[index].lon);lv_textarea_set_text(city_name,cities[index].name);
}
static void save_city(lv_event_t *e) {
    (void)e;snprintf(pending_location,sizeof(pending_location),"%s %s 480 %s",lv_textarea_get_text(latitude),lv_textarea_get_text(longitude),lv_textarea_get_text(city_name));
    location_pending=true;lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
}
void network_setup_close(void) {
    if (keyboard) lv_keyboard_set_textarea(keyboard,NULL);
    if (password) lv_textarea_set_text(password,"");
    screen=keyboard=password=latitude=longitude=city_name=message=networks=NULL;
    scan_pending=connect_pending=location_pending=false;
    wipe(pending_password,sizeof(pending_password));pending_ssid[0]=pending_location[0]=result_text[0]=0;
}
static void render(unsigned next) {
    lv_obj_t *target=screen;network_setup_close();screen=target;page=next;
    lv_obj_clean(screen);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0xf5f1e6),0);lv_obj_set_style_text_color(screen,lv_color_hex(0x37432f),0);
    lv_obj_set_style_text_font(screen,&garden_font,0);
    caption(T("联网与天气","Network & Weather"),18,14,290);control(T("返回","Back"),370,8,92,back);
    control("Wi-Fi",18,52,210,wifi_tab);control(T("天气城市","Weather city"),248,52,214,city_tab);
    if (next==WIFI_LIST) {
        message=caption(T("正在搜索附近 Wi-Fi…","Searching for nearby Wi-Fi..."),18,104,318);control(T("刷新","Refresh"),370,95,92,refresh);
        networks=lv_obj_create(screen);lv_obj_set_pos(networks,14,142);lv_obj_set_size(networks,452,298);
        lv_obj_set_style_bg_opa(networks,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(networks,0,0);lv_obj_set_style_pad_all(networks,4,0);
        lv_obj_set_scroll_dir(networks,LV_DIR_VER);
        caption(T("自动搜索附近网络，点选后输入密码","Nearby networks are scanned automatically."),18,452,440);
        visible_scan.revision=UINT32_MAX;visible_connected=false;visible_ssid[0]=0;scan_pending=true;return;
    }
    if (next==WIFI_PASSWORD) {
        caption(selected_ap.ssid,18,103,440);
        if (!selected_ap.supported) {caption(T("暂不支持此网络的认证方式","This network security is not supported."),18,151,440);return;}
        if (!selected_ap.open) {
            caption(T("Wi-Fi 密码","Wi-Fi password"),18,139,300);control(T("显示/隐藏","Show/Hide"),338,130,124,password_visibility);
            password=field(18,177,444,63,true,NULL);lv_textarea_set_placeholder_text(password,T("点击这里输入密码","Tap to enter password"));
        } else caption(T("此网络无需密码，点击连接即可","This is an open network. Tap Connect."),18,151,440);
        control(T("连接","Connect"),18,235,136,connect_clicked);control(T("换个网络","Choose another"),174,235,170,wifi_tab);
        message=caption("",18,280,444);
    } else {
        lv_obj_t *list=lv_dropdown_create(screen);lv_obj_set_pos(list,18,100);lv_obj_set_size(list,444,38);
        lv_dropdown_set_options(list,ui_language_is_chinese()?"请选择城市\n广州\n深圳\n北京\n上海\n杭州\n成都\n自定义坐标":"Choose a city\nGuangzhou\nShenzhen\nBeijing\nShanghai\nHangzhou\nChengdu\nCustom coordinates");
        lv_dropdown_set_symbol(list,"v"); /* ASCII is present in the compact Chinese font. */
        lv_obj_set_style_text_font(lv_dropdown_get_list(list),&garden_font,0);
        lv_obj_set_style_bg_color(list,lv_color_hex(0xffffff),0);lv_obj_set_style_text_color(list,lv_color_hex(0x273421),0);
        lv_obj_set_style_bg_color(lv_dropdown_get_list(list),lv_color_hex(0xffffff),0);lv_obj_set_style_text_color(lv_dropdown_get_list(list),lv_color_hex(0x273421),0);
        lv_obj_add_event_cb(list,city_changed,LV_EVENT_VALUE_CHANGED,NULL);
        caption(T("纬度","Latitude"),18,144,180);caption(T("经度","Longitude"),248,144,180);
        latitude=field(18,164,210,10,false,"0123456789.-");longitude=field(248,164,214,11,false,"0123456789.-");
        caption(T("城市名称（英文）","City name (English)"),18,213,400);city_name=field(18,233,444,31,false,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_");
        control(T("保存城市","Save city"),18,287,166,save_city);message=caption(T("中国城市：北京时间 UTC+8","China cities use UTC+8"),196,291,268);
    }
    keyboard=lv_keyboard_create(screen);
    /* Keyboard constructor aligns to bottom. Reset alignment before absolute placement. */
    lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(keyboard,0,326);lv_obj_set_size(keyboard,480,154);
    lv_obj_set_style_text_font(keyboard,&lv_font_montserrat_14,0);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(0xdce3d3),0);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(0xffffff),LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard,lv_color_hex(0x273421),LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard,keyboard_done,LV_EVENT_ALL,NULL);
    if (password) lv_keyboard_set_textarea(keyboard,password);
    else lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
}
void network_setup_show(void *parent) {screen=parent;render(WIFI_LIST);}
void network_setup_tick(void) {
    char s[33]={0},p[64]={0},location[128]={0};bool scan=false,connect=false,save=false,list_visible=false;
    if (!bsp_display_lock(100))return;
    if (screen) {
        list_visible=page==WIFI_LIST;
        scan=scan_pending;connect=connect_pending;save=location_pending;scan_pending=connect_pending=location_pending=false;
        memcpy(s,pending_ssid,sizeof(s));memcpy(p,pending_password,sizeof(p));memcpy(location,pending_location,sizeof(location));wipe(pending_password,sizeof(pending_password));
    }
    bsp_display_unlock();
    if (scan) network_scan_begin();
    esp_err_t err=ESP_OK;if (connect)err=network_connect(s,p);if(save)err=environment_configure(location);wipe(p,sizeof(p));
    network_scan_t snapshot;network_scan_snapshot(&snapshot);
    char connected_ssid[33]={0};bool connected=list_visible && network_ready();
    if (connected) {network_saved_ssid(connected_ssid);connected=connected_ssid[0]!=0;}
    if (!bsp_display_lock(100))return;
    if (screen&&message) {
        if (page==WIFI_LIST && (snapshot.revision!=visible_scan.revision || connected!=visible_connected || strcmp(connected_ssid,visible_ssid))) {
            visible_scan=snapshot;visible_connected=connected;memcpy(visible_ssid,connected_ssid,sizeof(visible_ssid));fill_networks();
        }
        if (save)snprintf(result_text,sizeof(result_text),"%s",err==ESP_OK?T("已保存，联网后更新","Saved. Updates after connecting."):T("坐标或名称无效","Invalid coordinates or city name."));
        if (connect)snprintf(result_text,sizeof(result_text),"%s",err==ESP_OK?"":T("连接请求失败，请重试","Connection request failed. Retry."));
        const char *text=page==WIFI_PASSWORD?(result_text[0]?result_text:network_connection_text()):result_text;
        if (page!=WIFI_LIST && text[0] && strcmp(lv_label_get_text(message),text))lv_label_set_text(message,text);
    }
    bsp_display_unlock();
}

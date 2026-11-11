#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
/* Compile actual product UI against real LVGL; only radio/platform services are mocked. */
static ui_language_t mock_language=UI_LANGUAGE_ZH;
ui_language_t ui_language_get(void) {return mock_language;}
bool ui_language_is_chinese(void) {return mock_language==UI_LANGUAGE_ZH;}
const char *ui_text(const char *zh,const char *en) {return ui_language_is_chinese()?zh:en;}
#include "network_setup.c"
static network_scan_t mock_scan;
static unsigned connections;
static bool mock_ready;
static char mock_ssid[33];
static uint16_t pixels[480*480];
void launcher_post(ui_message_t message) {(void)message;}
esp_err_t network_scan_begin(void) {mock_scan.busy=true;mock_scan.count=0;mock_scan.revision++;return ESP_OK;}
void network_scan_snapshot(network_scan_t *out) {*out=mock_scan;}
bool network_ready(void) {return mock_ready;}
void network_saved_ssid(char out[33]) {memcpy(out,mock_ssid,33);}
esp_err_t network_connect(const char *s,const char *p) {
    assert(!strcmp(s,"Test-WiFi")||!strcmp(s,"Open-WiFi"));
    assert(!strcmp(p,"testpass9")||!p[0]);connections++;return ESP_OK;
}
const char *network_connection_text(void) {return ui_text("未连接","Disconnected");}
esp_err_t environment_configure(const char *s) {assert(strstr(s,"Guangzhou"));return ESP_OK;}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) {(void)a;(void)p;lv_display_flush_ready(d);}
static void snapshot(const char *name) {
    lv_obj_update_layout(lv_screen_active());lv_refr_now(NULL);
    FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n480 480\n255\n");
    for(unsigned i=0;i<480*480;i++){uint16_t v=pixels[i];unsigned char rgb[]={((v>>11)&31)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);
}
static void completed_scan(bool open) {
    mock_scan.busy=false;mock_scan.result=ESP_OK;mock_scan.count=1;mock_scan.revision++;
    mock_scan.ap[0]=(network_ap_t){.rssi=-48,.open=open,.supported=true};strcpy(mock_scan.ap[0].ssid,open?"Open-WiFi":"Test-WiFi");network_setup_tick();
}
static lv_obj_t *button_named(const char *name) {
    for(unsigned i=0;i<lv_obj_get_child_count(screen);i++) {
        lv_obj_t *o=lv_obj_get_child(screen,i);
        if(lv_obj_check_type(o,&lv_button_class)&&lv_obj_get_child_count(o)&&!strcmp(lv_label_get_text(lv_obj_get_child(o,0)),name))return o;
    }assert(0);return NULL;
}
static void click(lv_obj_t *o) {lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);}
static const char *row_name(unsigned index) {return lv_label_get_text(lv_obj_get_child(lv_obj_get_child(networks,index),0));}
static const char *row_info(unsigned index) {return lv_label_get_text(lv_obj_get_child(lv_obj_get_child(networks,index),1));}
int main(void) {
    lv_init();lv_display_t *d=lv_display_create(480,480);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
    network_setup_show(lv_screen_active());network_setup_tick();assert(mock_scan.busy);completed_scan(false);
    assert(lv_obj_get_child_count(networks)==1);snapshot("wifi-list.ppm");click(lv_obj_get_child(networks,0));
    assert(password&&keyboard&&!lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN));assert(lv_keyboard_get_textarea(keyboard)==password);
    lv_obj_update_layout(screen);assert(lv_obj_get_height(password)==44);assert(lv_obj_get_y(password)+lv_obj_get_height(password)<lv_obj_get_y(keyboard));
    assert(lv_obj_get_y(keyboard)==326&&lv_obj_get_height(keyboard)==154);
    assert(lv_obj_get_y(keyboard)+lv_obj_get_height(keyboard)<=480);
    snapshot("wifi-password.ppm");click(button_named("连接"));network_setup_tick();assert(!connections&&strstr(lv_label_get_text(message),"8–63"));
    /* Exercise LVGL keyboard's real default value-changed callback. */
    const char *const *map=lv_buttonmatrix_get_map(keyboard);unsigned key=0;bool typed=false;
    for(unsigned i=0;map[i][0];i++){if(!strcmp(map[i],"\n"))continue;if(!strcmp(map[i],"t")){lv_buttonmatrix_set_selected_button(keyboard,key);lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,NULL);typed=true;break;}key++;}
    assert(typed&&!strcmp(lv_textarea_get_text(password),"t"));lv_textarea_add_text(password,"estpass9");
    assert(lv_textarea_get_password_mode(password));click(button_named("显示/隐藏"));assert(!lv_textarea_get_password_mode(password));
    snapshot("wifi-keyboard.ppm");click(button_named("连接"));assert(!lv_textarea_get_text(password)[0]);network_setup_tick();assert(connections==1);
    click(password);assert(!lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN));
    click(button_named("天气城市"));lv_obj_t *dropdown=NULL;
    for(unsigned i=0;i<lv_obj_get_child_count(screen);i++){lv_obj_t *o=lv_obj_get_child(screen,i);if(lv_obj_check_type(o,&lv_dropdown_class))dropdown=o;}
    assert(dropdown);lv_dropdown_set_selected(dropdown,1);lv_obj_send_event(dropdown,LV_EVENT_VALUE_CHANGED,NULL);
    click(latitude);assert(lv_keyboard_get_textarea(keyboard)==latitude);snapshot("wifi-city.ppm");click(button_named("保存城市"));network_setup_tick();
    assert(strstr(lv_label_get_text(message),"已保存"));
    click(button_named("Wi-Fi"));network_setup_tick();completed_scan(true);click(lv_obj_get_child(networks,0));assert(!password);click(button_named("连接"));network_setup_tick();assert(connections==2);
    click(button_named("Wi-Fi"));network_setup_tick();mock_scan.busy=false;mock_scan.count=0;mock_scan.revision++;network_setup_tick();assert(strstr(lv_label_get_text(message),"未发现"));
    mock_scan.result=ESP_FAIL;mock_scan.revision++;network_setup_tick();assert(strstr(lv_label_get_text(message),"未完成"));
    mock_scan.result=ESP_OK;mock_scan.count=2;mock_scan.revision++;
    mock_scan.ap[0]=(network_ap_t){.rssi=-48,.supported=true};strcpy(mock_scan.ap[0].ssid,"Test-WiFi");
    mock_scan.ap[1]=(network_ap_t){.rssi=-62,.supported=true};strcpy(mock_scan.ap[1].ssid,"Home-WiFi");
    mock_ready=true;strcpy(mock_ssid,"Home-WiFi");network_setup_tick();
    assert(lv_obj_get_child_count(networks)==2&&!strcmp(row_name(0),"Home-WiFi"));
    assert(strstr(row_info(0),"已连接")&&!strcmp(row_name(1),"Test-WiFi"));
    mock_ready=false;network_setup_tick();
    assert(!strcmp(row_name(0),"Test-WiFi")&&!strstr(row_info(1),"已连接"));
    mock_ready=true;strcpy(mock_ssid,"Hidden-WiFi");network_setup_tick();
    assert(lv_obj_get_child_count(networks)==3&&!strcmp(row_name(0),"Hidden-WiFi")&&strstr(row_info(0),"已连接"));
    mock_language=UI_LANGUAGE_EN;network_setup_show(lv_screen_active());network_setup_tick();
    assert(button_named("Weather city")&&button_named("Refresh")&&strstr(row_info(0),"Connected"));snapshot("wifi-list-en.ppm");
    network_setup_close();lv_obj_clean(lv_screen_active());puts("PASS real LVGL: scan list, connection-first order and status, visible password, keyboard typing, validation, reveal, submit, city, open network, empty/error scans");
    return 0;
}

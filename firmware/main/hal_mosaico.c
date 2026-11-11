#include "platform.h"
#include "bsp/esp_mosaico.h"
#include "esp_spiffs.h"
#include "esp_partition.h"
#include <stdio.h>
#include "esp_timer.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "esp_heap_caps.h"
static lv_obj_t *scene_canvas;
static void *scene_pixels;
LV_FONT_DECLARE(garden_font);
static lv_obj_t *canvas;
static bool storage_ready;
static esp_err_t storage_mount_result=ESP_ERR_INVALID_STATE;
static unsigned objects;
uint64_t platform_now_ms(void) { return esp_timer_get_time()/1000; }
esp_err_t platform_board_init(void) {
    const tinyusb_config_t usb=TINYUSB_DEFAULT_CONFIG();
    if (tinyusb_driver_install(&usb)==ESP_OK) {
        const tinyusb_config_cdcacm_t cdc={.cdc_port=TINYUSB_CDC_ACM_0};
        if (tinyusb_cdcacm_init(&cdc)==ESP_OK) tinyusb_console_init(TINYUSB_CDC_ACM_0);
    }
    esp_err_t err=bsp_power_init(); if (err!=ESP_OK) return err;
    if (!bsp_display_start()) return ESP_FAIL;
    return bsp_display_brightness_set(35);
}
esp_err_t hal_storage_init(void) {
    if (storage_ready) return ESP_OK;
    const esp_vfs_spiffs_conf_t cfg={.base_path="/store",.partition_label="apps",.max_files=5,.format_if_mount_failed=false};
    storage_mount_result=esp_vfs_spiffs_register(&cfg);
    storage_ready=storage_mount_result==ESP_OK; return storage_mount_result;
}
bool hal_storage_ready(void) { return storage_ready; }
esp_err_t hal_storage_status(void) {
    const esp_partition_t *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"apps");
    printf("NOR mounted=%d mount_result=%s\n",storage_ready,esp_err_to_name(storage_mount_result));
    if (!partition) return ESP_ERR_NOT_FOUND;
    printf("NOR partition=apps offset=0x%lx size=%lu\n",(unsigned long)partition->address,(unsigned long)partition->size);
    if (storage_ready) {
        size_t total=0,used=0; esp_err_t err=esp_spiffs_info("apps",&total,&used);
        if (err==ESP_OK) printf("NOR total=%u used=%u\n",(unsigned)total,(unsigned)used);
        return err;
    }
    /* Read only: distinguish an erased partition from nonblank/unmountable data. */
    uint8_t buffer[1024];
    for (size_t offset=0;offset<partition->size;offset+=sizeof(buffer)) {
        size_t count=partition->size-offset;
        if (count>sizeof(buffer)) count=sizeof(buffer);
        esp_err_t err=esp_partition_read(partition,offset,buffer,count);
        if (err!=ESP_OK) return err;
        for (size_t i=0;i<count;i++) if (buffer[i]!=0xff) {
            printf("NOR erased=0 first_nonblank=0x%lx (no writes performed)\n",(unsigned long)(offset+i));
            return ESP_OK;
        }
    }
    puts("NOR erased=1 (no filesystem; no writes performed)");
    return ESP_OK;
}
/* Called under the display lock, before the launcher removes the old root. */
void hal_set_canvas(void *object) {
    if (scene_canvas) { lv_obj_delete(scene_canvas); scene_canvas=NULL; }
    free(scene_pixels); scene_pixels=NULL;
    canvas=object; objects=0;
}
int hal_scene(const scene_cmd_t *commands,unsigned count,uint32_t background) {
    if (!canvas || !bsp_display_lock(200)) return -1;
    if (!scene_canvas) {
        /* Use LVGL's actual stride handler (the BSP may add DMA padding). */
        size_t stride=lv_draw_buf_width_to_stride(480,LV_COLOR_FORMAT_RGB565);
        scene_pixels=heap_caps_aligned_alloc(128,stride*400,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!scene_pixels) { bsp_display_unlock(); return -1; }
        lv_obj_clean(canvas); objects=0;
        scene_canvas=lv_canvas_create(canvas);
        lv_canvas_set_buffer(scene_canvas,scene_pixels,480,400,LV_COLOR_FORMAT_RGB565);
        lv_obj_remove_flag(scene_canvas,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(scene_canvas,0,0);
    }
    lv_canvas_fill_bg(scene_canvas,lv_color_hex(background),LV_OPA_COVER);
    lv_draw_buf_t *buf=lv_canvas_get_draw_buf(scene_canvas);
    for (unsigned i=0;i<count;i++) {
        const scene_cmd_t *c=&commands[i];
        if (c->kind==SCENE_TEXT) {
            lv_layer_t layer; lv_canvas_init_layer(scene_canvas,&layer);
            lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
            d.text=c->text; d.font=&garden_font; d.color=lv_color_hex(c->rgb);
            lv_area_t a={c->x,c->y,c->x+c->w-1,c->y+c->h-1};
            layer._clip_area=a;
            lv_draw_label(&layer,&d,&a); lv_canvas_finish_layer(scene_canvas,&layer);
        } else scene_shape((uint16_t*)buf->data,buf->header.stride/2,c);
    }
    lv_obj_invalidate(scene_canvas); bsp_display_unlock(); return 0;
}
void hal_clear(uint32_t rgb) {
    if (!canvas || !bsp_display_lock(200)) return;
    lv_obj_clean(canvas); scene_canvas=NULL; free(scene_pixels); scene_pixels=NULL;
    objects=0; lv_obj_set_style_bg_color(canvas,lv_color_hex(rgb),0); bsp_display_unlock();
}
void hal_rect(int x,int y,int w,int h,uint32_t rgb) {
    if (!canvas || !bsp_display_lock(200)) return;
    if (objects<96) {
        lv_obj_t *r=lv_obj_create(canvas); lv_obj_remove_style_all(r);
        lv_obj_set_pos(r,x,y); lv_obj_set_size(r,w,h); lv_obj_set_style_bg_color(r,lv_color_hex(rgb),0);
        lv_obj_set_style_bg_opa(r,LV_OPA_COVER,0); lv_obj_remove_flag(r,LV_OBJ_FLAG_CLICKABLE); objects++;
    }
    bsp_display_unlock();
}
void hal_text(int x,int y,const char *text,uint32_t rgb) {
    if (!canvas || !bsp_display_lock(200)) return;
    if (objects<96) {
        lv_obj_t *label=lv_label_create(canvas); lv_label_set_text(label,text); lv_obj_set_pos(label,x,y);
        lv_obj_set_width(label,480-x); lv_obj_set_style_text_color(label,lv_color_hex(rgb),0); objects++;
    }
    bsp_display_unlock();
}

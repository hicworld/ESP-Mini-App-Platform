#include "platform.h"
#include "bsp/esp_mosaico.h"
#include "driver/gpio.h"
#include <stdio.h>
#include "sdkconfig.h"
static bool ready,pir_previous;
static int pir_pin,key_pin[2];
static uint64_t boot_ms,last_motion;
static bool motion_sent;
static unsigned motion_count,key_count[2];
static struct { bool candidate,stable; uint64_t since; } keys[2];
void garden_input_init(void) {
#if CONFIG_PLATFORM_INTERACTION_V02
    /* User-supplied Interaction V0.2 schematic: SW1/SW2 short to GND.
     * Do not reuse the old button/LED driver, whose pinout differs. */
#if CONFIG_PLATFORM_INTERACTION_LEFT
    bsp_subboard_slot_t slot=BSP_SUBBOARD_SLOT_LEFT;
#else
    bsp_subboard_slot_t slot=BSP_SUBBOARD_SLOT_RIGHT;
#endif
    esp_err_t err=bsp_subboard_init(); if (err!=ESP_OK) return;
    pir_pin=bsp_subboard_map_gpio(slot,GPIO_NUM_4);
    key_pin[0]=bsp_subboard_map_gpio(slot,GPIO_NUM_13);
    key_pin[1]=bsp_subboard_map_gpio(slot,GPIO_NUM_12);
    gpio_config_t pir={.pin_bit_mask=1ULL<<pir_pin,.mode=GPIO_MODE_INPUT,.pull_down_en=GPIO_PULLDOWN_ENABLE};
    gpio_config_t buttons={.pin_bit_mask=(1ULL<<key_pin[0])|(1ULL<<key_pin[1]),.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    err=gpio_config(&pir); if (err==ESP_OK) err=gpio_config(&buttons);
    ready=err==ESP_OK; boot_ms=platform_now_ms(); pir_previous=gpio_get_level(pir_pin)!=0;
    printf("INPUT init=%s PIR=%d A=%d B=%d warmup=30s\n",esp_err_to_name(err),pir_pin,key_pin[0],key_pin[1]);
#endif
}
bool garden_input_ready(void) { return ready; }
void garden_input_tick(void) {
    if (!ready) return;
    uint64_t now=platform_now_ms(); bool pir=gpio_get_level(pir_pin)!=0;
    if (now-boot_ms>=30000 && pir && !pir_previous && (!motion_sent||now-last_motion>=10000)) {
        runtime_event(3,1,0); motion_sent=true;last_motion=now;motion_count++;
    }
    pir_previous=pir;
    for (int i=0;i<2;i++) {
        bool down=gpio_get_level(key_pin[i])==0;
        if (down!=keys[i].candidate) { keys[i].candidate=down;keys[i].since=now; }
        if (down!=keys[i].stable && now-keys[i].since>=40) {
            keys[i].stable=down; if (down) { key_count[i]++;runtime_event(3,i+2,0); }
        }
    }
}
void garden_input_status(void) {
    printf("INPUT ready=%d PIR=%d A=%d B=%d warmup=%d motion_count=%u A_count=%u B_count=%u\n",ready,ready?gpio_get_level(pir_pin):-1,
        ready?gpio_get_level(key_pin[0]):-1,ready?gpio_get_level(key_pin[1]):-1,
        ready && platform_now_ms()-boot_ms<30000,motion_count,key_count[0],key_count[1]);
}

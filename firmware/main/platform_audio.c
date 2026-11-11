#include "platform.h"
#include "audio_synth.h"
#include "bsp/esp_mosaico.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <stdio.h>
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static int wanted_music,pending_fx,volume=55;
static unsigned epoch;
static uint64_t last_fx;
static TaskHandle_t task;
static atomic_int device_result=ESP_ERR_INVALID_STATE;
static atomic_uint blocks;
static void player(void *arg) {
    (void)arg;garden_synth_t synth;garden_synth_init(&synth);
    esp_codec_dev_handle_t speaker=NULL;bool attempted=false,muted=true;unsigned seen=0;
    int16_t mono[320],stereo[640];
    for(;;) {
        portENTER_CRITICAL(&lock);
        int music=wanted_music,fx=pending_fx,vol=volume;unsigned generation=epoch;pending_fx=0;
        portEXIT_CRITICAL(&lock);
        if(seen!=generation){garden_synth_init(&synth);seen=generation;}
        garden_synth_music(&synth,music);if(fx)garden_synth_effect(&synth,fx);
        bool active=music||synth.fx;
        if(active&&vol&&!attempted) {
            attempted=true;speaker=bsp_audio_codec_speaker_init();
            esp_codec_dev_sample_info_t fs={.sample_rate=GARDEN_AUDIO_RATE,.channel=2,.bits_per_sample=16};
            int result=speaker?esp_codec_dev_open(speaker,&fs):ESP_FAIL;
            if(!result) result=esp_codec_dev_set_out_vol(speaker,85);
            if(!result) result=esp_codec_dev_set_out_mute(speaker,true);
            atomic_store(&device_result,result);ESP_LOGI("audio","speaker init result=%d, rate=%d",result,GARDEN_AUDIO_RATE);
            portENTER_CRITICAL(&lock);bool obsolete=generation!=epoch;portEXIT_CRITICAL(&lock);
            if(obsolete)continue;
        }
        bool silence=!active||!vol;
        if(speaker&&atomic_load(&device_result)==ESP_OK&&silence!=muted) {
            int result=esp_codec_dev_set_out_mute(speaker,silence);
            if(result)atomic_store(&device_result,result);else muted=silence;
        }
        if(!active){vTaskDelay(pdMS_TO_TICKS(20));continue;}
        garden_synth_render(&synth,mono,320,vol);
        if(silence||!speaker||atomic_load(&device_result)!=ESP_OK){vTaskDelay(pdMS_TO_TICKS(20));continue;}
        for(unsigned i=0;i<320;i++)stereo[2*i]=stereo[2*i+1]=mono[i];
        int result=esp_codec_dev_write(speaker,stereo,sizeof(stereo));
        if(result){atomic_store(&device_result,result);esp_codec_dev_set_out_mute(speaker,true);muted=true;}
        else atomic_fetch_add(&blocks,1);
    }
}
void platform_audio_init(void) {
    if(!task&&xTaskCreate(player,"garden_audio",6144,NULL,2,&task)!=pdPASS)atomic_store(&device_result,ESP_ERR_NO_MEM);
}
bool platform_audio_available(void) { return task!=NULL; }
void platform_audio_stop(void) {
    portENTER_CRITICAL(&lock);wanted_music=pending_fx=0;epoch++;last_fx=0;portEXIT_CRITICAL(&lock);
}
int platform_audio_command(int kind,int value) {
    if(!task)return -1;
    if((kind==1&&(value<0||value>2))||(kind==2&&(value<1||value>5))||(kind==3&&(value< -1||value>75))||kind<1||kind>3)return -2;
    uint64_t now=platform_now_ms();int result=0;
    portENTER_CRITICAL(&lock);
    if(kind==1)wanted_music=value;
    if(kind==2){if(last_fx&&now-last_fx<150)result=-3;else{pending_fx=value;last_fx=now;}}
    if(kind==3){if(value>=0)volume=value;result=volume;}
    portEXIT_CRITICAL(&lock);return result;
}
void platform_audio_status(void) {
    portENTER_CRITICAL(&lock);int music=wanted_music,vol=volume;portEXIT_CRITICAL(&lock);
    printf("AUDIO worker=%u codec=%d music=%d volume=%d blocks=%u\n",task!=NULL,atomic_load(&device_result),music,vol,atomic_load(&blocks));
}

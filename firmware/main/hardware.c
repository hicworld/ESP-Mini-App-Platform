#include "platform.h"
#include <stdio.h>
#include <string.h>

typedef struct { const char *id,*zh,*en; uint32_t bit; } hardware_item_t;
static const hardware_item_t items[]={
    {"display","屏幕","display",HW_DISPLAY},{"touch","触摸屏","touch",HW_TOUCH},
    {"speaker","扬声器","speaker",HW_SPEAKER},{"motion","人体感应器","motion sensor",HW_MOTION},
    {"buttons","按键","buttons",HW_BUTTONS},{"wifi","Wi-Fi","Wi-Fi",HW_WIFI},
    {"camera","摄像头","camera",HW_CAMERA},{"microphone","麦克风","microphone",HW_MICROPHONE},
    {"temperature","温度传感器","temperature sensor",HW_TEMPERATURE},
    {"humidity","湿度传感器","humidity sensor",HW_HUMIDITY},
    {"light","光照传感器","light sensor",HW_LIGHT},{"imu","运动传感器","IMU",HW_IMU}
};
uint32_t platform_hardware_bit(const char *name) {
    if(!name)return 0;
    for(unsigned i=0;i<sizeof(items)/sizeof(items[0]);i++)if(!strcmp(name,items[i].id))return items[i].bit;
    return 0;
}
uint32_t platform_hardware_available(void) {
    uint32_t available=HW_DISPLAY|HW_TOUCH|HW_WIFI;
    if(platform_audio_available())available|=HW_SPEAKER;
    if(garden_input_ready())available|=HW_MOTION|HW_BUTTONS;
    return available;
}
void platform_hardware_names(uint32_t mask,bool chinese,char *out,size_t capacity) {
    if(!out||!capacity)return;
    out[0]=0;size_t used=0;
    for(unsigned i=0;i<sizeof(items)/sizeof(items[0]);i++)if(mask&items[i].bit) {
        int n=snprintf(out+used,capacity-used,"%s%s",used?(chinese?"、":", "):"",chinese?items[i].zh:items[i].en);
        if(n<0||(size_t)n>=capacity-used)break;
        used+=(size_t)n;
    }
}

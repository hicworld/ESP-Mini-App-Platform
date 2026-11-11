#include "platform.h"
#include "artwork.h"

static scene_cmd_t commands[8];
static const char *gesture="Tap the screen";
static uint32_t last_update;
static platform_imu_sample_t imu;
static platform_magnet_sample_t magnet;
static platform_battery_sample_t battery;
static platform_orientation_t orientation;
static int has_imu,has_magnet,has_battery,has_orientation;

static void label(scene_cmd_t *c,int x,int y,const char *value,uint32_t rgb) {
    c->kind=SCENE_TEXT; c->rgb=rgb;
    c->x=x; c->y=y; c->w=480-x; c->h=30;
    int i=0; while(value[i] && i<31) { c->text[i]=value[i]; i++; }
    c->text[i]=0;
}
static void number(scene_cmd_t *c,int x,int y,const char *prefix,int value) {
    char line[32]; int n=0;
    while(*prefix && n<20) line[n++]=*prefix++;
    if(value<0) { line[n++]='-'; value=-value; }
    char digits[12]; int count=0;
    do { digits[count++]=(char)('0'+value%10); value/=10; } while(value && count<11);
    while(count && n<31) line[n++]=digits[--count];
    line[n]=0; label(c,x,y,line,0xe1e9d9);
}
static void refresh_sensors(void) {
    uint32_t hw=platform_hardware();
    has_imu=(hw&PLATFORM_HW_IMU) && platform_imu(&imu,sizeof(imu))==sizeof(imu);
    has_magnet=(hw&PLATFORM_HW_MAGNETOMETER) && platform_magnet(0,&magnet,sizeof(magnet))==sizeof(magnet);
    has_battery=(hw&PLATFORM_HW_BATTERY) && platform_battery(&battery,sizeof(battery))==sizeof(battery);
    has_orientation=(hw&PLATFORM_HW_IMU) &&
        platform_orientation(&orientation,sizeof(orientation))==sizeof(orientation);
}
static void draw(void) {
    label(&commands[0],24,20,"中文 / First Batch API",0xf9dfa0);
    label(&commands[1],24,69,gesture,0xffffff);
    label(&commands[2],24,115,"Swipe or long press",0xbbd1bc);
    if(has_imu) number(&commands[3],24,177,"Accel X mg: ",imu.ax_mg);
    else label(&commands[3],24,177,"IMU unavailable",0xe6a75f);
    if(has_magnet) number(&commands[4],24,218,"Mag X uT*100: ",magnet.x_ut100);
    else label(&commands[4],24,218,"Magnet unavailable",0xe6a75f);
    if(has_battery) number(&commands[5],24,259,"Battery %: ",battery.percent);
    else label(&commands[5],24,259,"Battery unavailable",0xe6a75f);
    if(has_orientation) number(&commands[6],24,300,"Roll md: ",orientation.roll_md);
    else label(&commands[6],24,300,"Tilt unavailable",0xe6a75f);
    label(&commands[7],24,350,"Image: RGB565 asset",0xbbd1bc);
    if(platform_scene(commands,8,0x1b2a24)==0)
        (void)platform_image(388,18,ARTWORK_WIDTH,ARTWORK_HEIGHT,artwork_pixels);
}
void app_init(void) {
    last_update=platform_millis(); refresh_sensors(); draw();
}
void app_event(int type,int a,int b) {
    (void)b;
    if(type==1) {
        uint32_t now=(uint32_t)a;
        if(now-last_update<500u)return;
        last_update=now; refresh_sensors(); draw();
    } else if(type==2) {
        gesture="Tap";
        if(platform_hardware()&PLATFORM_HW_HAPTIC)(void)platform_feedback(1,55,90);
        draw();
    } else if(type==4) { gesture="Touch down"; draw(); }
    else if(type==5) { gesture="Moving"; draw(); }
    else if(type==6) { gesture="Touch up"; draw(); }
    else if(type==7) {
        gesture="Long press";
        if(platform_hardware()&PLATFORM_HW_LED)(void)platform_feedback(2,100,300);
        draw();
    } else if(type==8) {
        gesture=a==1?"Swipe left":a==2?"Swipe right":a==3?"Swipe up":"Swipe down";
        draw();
    }
}

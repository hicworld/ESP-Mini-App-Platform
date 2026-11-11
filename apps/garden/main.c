#include "platform.h"
static scene_cmd_t commands[96];
static unsigned count;
static uint32_t now,last_draw,visit_at,wind_at,feed_at,water_at,pet_at,last_motion;
static int visited,wind,feeding,watering,petting,motion_seen,mode,details,water_x=123,water_y=320;
static environment_t env;
static int music_on=1,sound_volume=55,music_track=-1;
static void update_music(void) {
    int track=music_on?(env.daylight==0?2:1):0;
    if(track!=music_track){platform_audio(1,track);music_track=track;}
}
static void add(unsigned kind,int x,int y,int w,int h,int p,int q,unsigned color,const char *text) {
    if (count>=96) { count++; return; }
    scene_cmd_t *c=&commands[count++];
    c->kind=kind; c->rgb=color; c->x=x; c->y=y; c->w=w; c->h=h; c->p=p; c->q=q;
    unsigned n=0; if (text) while (text[n]&&n<31) {c->text[n]=text[n];n++;} c->text[n]=0;
}
static void rect(int x,int y,int w,int h,int radius,unsigned color) { add(SCENE_RECT,x,y,w,h,radius,0,color,0); }
static void oval(int x,int y,int w,int h,unsigned color) { add(SCENE_ELLIPSE,x,y,w,h,0,0,color,0); }
static void tri(int x,int y,int x2,int y2,int x3,int y3,unsigned color) { add(SCENE_TRIANGLE,x,y,x2,y2,x3,y3,color,0); }
static void text(int x,int y,int width,const char *s,unsigned color) { add(SCENE_TEXT,x,y,width,22,0,0,color,s); }
static int active(int flag,uint32_t start,unsigned duration) { return flag && (uint32_t)(now-start)<duration; }
static int wave(unsigned period) { unsigned t=now%period; return (int)(t<period/2?t:period-t)*8/(int)period-2; }
static void greet(int motion) {
    if (motion && motion_seen && (uint32_t)(now-last_motion)<10000) return;
    if (motion) { last_motion=now; motion_seen=1; }
    visit_at=wind_at=now; visited=wind=1; petting=feeding=0;
    platform_audio(2,motion?4:5);
}
static void pet_anim(void) { petting=1;pet_at=now;visited=feeding=0;platform_audio(2,1); }
static void feed_anim(void) { feeding=1;feed_at=now;visited=petting=0;platform_audio(2,2); }
static void water_anim(int x,int y) { watering=1;petting=feeding=visited=0;water_at=now;water_x=x<32?32:x>440?440:x;water_y=y<255?255:y>330?330:y;platform_audio(2,3); }
static int pulse(uint32_t age,unsigned period,int amplitude) {
    unsigned t=age%period;return (int)(t<period/2?t:period-t)*amplitude*2/(int)period;
}
static void temperature(char *s,int value) {
    unsigned n=0,v=(unsigned)(value<0?-value:value);
    if (value<0) s[n++]='-';
    if (v>=1000) s[n++]=(char)('0'+v/1000);
    if (v>=100) s[n++]=(char)('0'+(v/100)%10);
    s[n++]=(char)('0'+(v/10)%10); s[n++]='.'; s[n++]=(char)('0'+v%10); s[n++]=' ';s[n++]='C';s[n]=0;
}
static const char *weather_name(void) {
    if (!(env.flags&2)) return "暂无天气";
    if (env.weather==0) return "晴";
    if (env.weather<=3) return "多云";
    if (env.weather<=48) return "雾";
    if ((env.weather>=71&&env.weather<=77)||env.weather==85||env.weather==86) return "雪";
    if (env.weather>=95) return "雷雨";
    return "雨";
}
static void draw(void) {
    count=0;
    int night=env.daylight==0, breeze=active(wind,wind_at,5000),look=active(visited,visit_at,4000);
    int feed=active(feeding,feed_at,4000),water=active(watering,water_at,4000),pet=active(petting,pet_at,3000);
    int sway=breeze?wave(1200)*3:wave(4000);
    int rainy=(env.flags&2)&&env.weather>=51,cloudy=(env.flags&2)&&env.weather>0;
    int snow=(env.weather>=71&&env.weather<=77)||env.weather==85||env.weather==86;
    unsigned ink=night?0xf5e8cd:0x53604a,sky=night?0x313d59:rainy?0xd8e2dc:0xf7ecd8;
    unsigned grass=night?0x627761:0xa5b888,leaf=night?0x526d59:0x819b67;
    rect(0,0,480,62,0,night?0x293449:0xfff8e9);
    text(16,10,220,"风来小院",ink);
    text(16,34,250,(env.flags&8)?env.location:"请先设置城市",ink);
    char clock[6]="--:--",temp[12];
    if (env.flags&1) {
        unsigned hour=env.seconds/3600,minute=env.seconds/60%60;
        clock[0]=(char)('0'+hour/10);clock[1]=(char)('0'+hour%10);clock[3]=(char)('0'+minute/10);clock[4]=(char)('0'+minute%10);
    }
    text(382,9,86,clock,ink);
    if (env.flags&2) temperature(temp,env.temperature); else {temp[0]='-';temp[1]='-';temp[2]=' ';temp[3]='C';temp[4]=0;}
    text(382,34,86,temp,ink);
    if (!cloudy) {
        oval(352,86,48,48,night?0xf7e6b2:0xefc785);
        if (night) oval(340,80,45,44,sky);
    } else { oval(295,92,80,28,night?0x677281:0xf3f1e9);oval(330,82,44,34,night?0x677281:0xf3f1e9); }
    if (night) for (int i=0;i<4;i++) oval(110+i*48,78+(i%2)*33,3,3,0xf3e5bc);
    oval(0,171,390,135,night?0x526d60:0xb5c39a); oval(160,161,320,156,night?0x6c8068:0xc6d0a5);
    rect(0,244,480,110,0,grass);
    oval(20,285,126,41,night?0x748f91:0xa9c9bb); oval(42,297,77,10,night?0x9aafaa:0xd8e2c5);
    rect(72,163,15,116,6,0x927356);
    oval(29+sway,127,99,80,leaf);oval(45+sway,100,75,86,leaf);oval(85+sway,142,52,58,leaf);
    rect(333,204,99,86,8,night?0xc1aa82:0xe6cb9f);tri(319,210,382,158,445,210,0xb88969);
    rect(371,246,25,44,9,0xa68663);rect(345,221,22,24,4,night?0xf1cc83:0xffedc6);rect(403,221,18,24,4,night?0xf1cc83:0xffedc6);
    oval(365,295,39,12,0xd5ccb0);oval(340,313,41,12,0xd5ccb0);oval(311,332,43,12,0xd5ccb0);
    /* Bunny: ears and head rise toward the viewer; body gently breathes. */
    unsigned age=(uint32_t)(now-visit_at);
    int lift=look?(age<600?(int)age/50:age>3400?(int)(4000-age)/50:12):0;
    int chew=feed?pulse(now-feed_at,800,8):0,rub=pet?pulse(now-pet_at,1000,7)-3:0;
    int head_y=228-lift+(feed?8+chew:0)+(pet?4:0),breathe=pet?pulse(now-pet_at,1000,4):wave(3200)>0?1:0;
    unsigned animal_first=count;
    oval(170,284,72,12,night?0x526951:0x8eaa77);oval(181,250+breathe,54,40,0xf1e6cd);
    oval(187+sway/3,head_y-27,13,42,0xf1e6cd);oval(210+sway/3,head_y-30,13,45,0xf1e6cd);
    oval(191+sway/3,head_y-19,5,28,0xe6bcab);oval(214+sway/3,head_y-22,5,29,0xe6bcab);
    oval(178,head_y,57,46,0xf8eedb);
    oval(190,head_y+18,4,pet?2:look?6:feed?2:3,0x4d5142);oval(216,head_y+18,4,pet?2:look?6:feed?2:3,0x4d5142);
    oval(203,head_y+28,5,4,0xc99182);oval(183,head_y+27,9,4,0xe7b8a3);oval(220,head_y+27,9,4,0xe7b8a3);
    /* Cat reacts to the same breeze, independently of the touch mode. */
    for (unsigned i=animal_first+2;i<count;i++) commands[i].x+=rub;
    int cy=259-lift/2+(feed?5+chew/2:0)+(pet?3:0);
    oval(259,290,65,11,night?0x526951:0x8eaa77);oval(269,271,42,25,0xd7a46f);
    oval(305,273+sway/2-(pet?pulse(now-pet_at,700,10):0),24,12,0xd7a46f);
    tri(270,cy+9,269,cy-9,285,cy+2,0xd7a46f);tri(297,cy+2,312,cy-9,311,cy+12,0xd7a46f);
    oval(268,cy,46,32,0xe4b47f);oval(279,cy+13,3,pet?1:look?5:feed?1:2,0x4d5142);oval(299,cy+13,3,pet?1:look?5:feed?1:2,0x4d5142);oval(289,cy+22,4,3,0x9f7259);
    for (int i=0;i<4;i++) {
        int x=24+i*99,y=337-(i%2)*17;
        int watered=water && x>water_x-60 && x<water_x+60;
        if (watered) y-=pulse(now-water_at,1200,6);
        rect(x,y-14,3,22,1,leaf);oval(x-5+sway,y-20,14,12,i%2?0xe8c384:0xf1d8c0);
    }
    if (breeze && !details) for (int i=0;i<3;i++) {
        int x=15+(int)((now/12+(unsigned)i*89)%415),y=155+i*32;
        rect(x,y,30,2,1,night?0xa2b9a0:0xe5edcc);oval(x+7,y+6,9,4,0xb6bc76);
    }
    if (rainy && !details) for (int i=0;i<4;i++) {
        int x=26+i*77,y=83+(int)((now/14+(unsigned)i*29)%104);
        oval(x,y,3,snow?3:10,night?0x9fb9c6:0xadc1c4);
    }
    if (feed && !details) {
        oval(223,302,80,15,0xe1c7a0);
        int crumbs=4-(int)((now-feed_at)/1000);
        for (int i=0;i<crumbs;i++) oval(234+i*15,305+(i%2)*3,6,5,0xb38758);
    }
    if (pet && !details) {
        int hy=192-(int)((now-pet_at)%1400)/100;
        oval(239,hy,9,9,0xd59086);oval(247,hy,9,9,0xd59086);tri(239,hy+5,256,hy+5,247,hy+17,0xd59086);
    }
    if (water && !details) {
        oval(water_x-22,water_y+3,46,9,0x789e81);
        rect(water_x-27,water_y-69,28,22,6,0x78aaa5);
        tri(water_x-2,water_y-61,water_x+17,water_y-47,water_x-2,water_y-51,0x78aaa5);
        for (int i=0;i<3;i++) oval(water_x-3+i*7,water_y-42+(int)(((now-water_at)/12+(unsigned)i*13)%36),5,10,0x6caeac);
    }
    if (!details && (look||pet||feed||water)) {rect(155,171,158,28,12,0xfff8e9);text(171,177,139,pet?"摸摸好开心":feed?"开饭啦":water?"小花喝水啦":"你来啦",0x667250);}
    rect(0,353,480,47,0,night?0x293449:0xfff8e9);
    const char *labels[3]={"摸摸","浇水","喂食"};
    for (int i=0;i<3;i++) {rect(15+i*156,359,140,33,12,mode==i?0x82916c:night?0x475845:0xebeeda);text(65+i*156,367,90,labels[i],mode==i?0xffffff:ink);}
    if (details) {
        rect(36,96,408,240,18,0xfff8e9);
        text(58,117,360,"当地室外天气",0x53604a);text(58,151,360,weather_name(),0x53604a);
        text(58,181,360,env.flags&4?"天气缓存待更新":env.flags&2?"天气：Open-Meteo":"请联网并设置城市",0x53604a);
        text(58,211,360,env.daylight<0?"昼夜信息待获取":env.daylight?"当地现在是白天":"当地现在是夜晚",0x53604a);
        rect(58,249,170,42,12,0x82916c);text(85,260,130,"联网与城市",0xffffff);text(264,260,150,"轻点关闭",0x53604a);
        rect(58,298,170,28,10,0xebeeda);text(72,302,145,music_on?"音乐：开":"音乐：关",0x53604a);
        rect(240,298,180,28,10,0xebeeda);text(253,302,155,sound_volume==0?"声音：静音":sound_volume<=35?"音量：轻柔":sound_volume<=55?"音量：适中":"音量：大",0x53604a);
    }
    platform_scene(commands,(int)count,sky); last_draw=now;
}
void app_init(void) {
    now=platform_millis();last_draw=now;visited=wind=feeding=watering=petting=motion_seen=mode=details=0;
    platform_environment(&env,sizeof(env));music_on=1;music_track=-1;sound_volume=platform_audio(3,-1);if(sound_volume<0)sound_volume=55;update_music();draw();
}
void app_event(int type,int a,int b) {
    now=platform_millis();
    if (type==1) {
        if ((uint32_t)(now-last_draw)<200) return;
        platform_environment(&env,sizeof(env));update_music();draw();return;
    }
    if (type==3) {
        if (a==1 || a==2) greet(a==1);
        else if (a==3) feed_anim();
        else return;
    } else if (type==2) {
        if (a<0||a>=480||b<0||b>=400) return;
        if (details) {
            if (a>=58&&a<228&&b>=249&&b<291) {platform_settings();return;}
            if(b>=298&&b<332&&a>=58&&a<228){music_on=!music_on;update_music();draw();return;}
            if(b>=298&&b<332&&a>=240&&a<420){sound_volume=sound_volume==0?35:sound_volume==35?55:sound_volume==55?75:0;platform_audio(3,sound_volume);draw();return;}
            details=0;
        }
        else if (b<62) details=1;
        else if (b>=353) {mode=a/160; if(mode==2) feed_anim(); else if(mode==1) water_anim(water_x,water_y); else pet_anim();}
        else if (b<170) greet(0);
        else if (mode==1) water_anim(a,b);
        else if (mode==2) feed_anim();
        else if (a>=170&&a<=330&&b>=195&&b<=305) pet_anim();
        else return;
    } else return;
    draw();
}

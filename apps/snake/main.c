#include "platform.h"

enum { COLS=20, ROWS=12, MAX_BODY=64 };
static unsigned char body_x[MAX_BODY],body_y[MAX_BODY];
static unsigned length,score,best,food_x,food_y,random_state;
static int direction,next_direction,started,ended;
static uint32_t last_move;
static scene_cmd_t commands[96];
static unsigned command_count;
static const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
static void add(unsigned kind,int x,int y,int w,int h,unsigned rgb,const char *text) {
    scene_cmd_t *c=&commands[command_count++];
    c->kind=kind;c->rgb=rgb;c->x=x;c->y=y;c->w=w;c->h=h;c->p=0;c->q=0;
    unsigned n=0;if(text)while(text[n]&&n<31){c->text[n]=text[n];n++;}c->text[n]=0;
}
static void rect(int x,int y,int w,int h,uint32_t rgb) { add(SCENE_RECT,x,y,w,h,rgb,0); }
static void text(int x,int y,int w,const char *s,uint32_t rgb) { add(SCENE_TEXT,x,y,w,24,rgb,s); }
static void number(char *out,unsigned value) {
    char digits[10]; unsigned count=0;
    do { digits[count++]=(char)('0'+value%10); value/=10; } while (value);
    for (unsigned i=0;i<count;i++) out[i]=digits[count-1-i]; out[count]=0;
}
static void food(void) {
    volatile unsigned char occupied[COLS*ROWS];
    for (unsigned i=0;i<COLS*ROWS;i++) occupied[i]=0;
    for (unsigned i=0;i<length;i++) occupied[body_y[i]*COLS+body_x[i]]=1;
    random_state=random_state*1664525u+1013904223u;
    unsigned cell=random_state%(COLS*ROWS);
    /* Bounded search always terminates: the body occupies at most 64/240 cells. */
    for (unsigned i=0;i<COLS*ROWS;i++) {
        unsigned candidate=(cell+i)%(COLS*ROWS);
        if (!occupied[candidate]) { food_x=candidate%COLS; food_y=candidate/COLS; return; }
    }
}
static void draw(void) {
    char value[12];command_count=0;
    text(20,10,64,"Score",0xe9eff8);number(value,score);text(90,10,50,value,0x63dfbd);
    text(150,10,54,"Best",0xe9eff8);number(value,best);text(210,10,50,value,0xf6c65b);
    if(ended)text(290,10,170,"Tap: restart",0xf6c65b);
    else if(!started)text(300,10,160,"Tap to start",0xe9eff8);
    rect(38,48,404,244,0x233a53);
    rect(40,50,400,240,0x091322);
    rect(41+(int)food_x*20,51+(int)food_y*20,18,18,0xf46e78);
    for (unsigned i=0;i<length;i++)
        rect(41+body_x[i]*20,51+body_y[i]*20,18,18,i?0x37a88c:0x63dfbd);
    const char *names[4]={"Right","Down","Left","Up"};
    const int x[4]={350,200,50,200},y[4]={332,352,332,302};
    for (unsigned i=0;i<4;i++) {
        rect(x[i],y[i],80,38,0x233a53);
        text(x[i]+8,y[i]+6,64,names[i],0xe9eff8);
    }
    (void)platform_scene(commands,(int)command_count,0x101d32);
}
static void reset(void) {
    length=3; score=0; direction=next_direction=0; started=ended=0;
    for (unsigned i=0;i<length;i++) { body_x[i]=(unsigned char)(7-i); body_y[i]=5; }
    random_state=platform_millis()|1u; food(); last_move=platform_millis(); draw();
}
static void finish(void) {
    ended=1;
    if (score>best) { best=score; platform_kv_set("best",4,&best,sizeof(best)); }
    draw();
}
void app_init(void) {
    best=0;
    if (platform_kv_get("best",4,&best,sizeof(best))!=sizeof(best) || best>MAX_BODY-3) best=0;
    reset();
}
void app_event(int type,int a,int b) {
    if (type==2) {
        if (ended) { reset(); return; }
        int requested=-1;
        if (a>=200 && a<280 && b>=302 && b<340) requested=3;
        else if (a>=200 && a<280 && b>=352 && b<390) requested=1;
        else if (a>=50 && a<130 && b>=332 && b<370) requested=2;
        else if (a>=350 && a<430 && b>=332 && b<370) requested=0;
        if (requested>=0 && requested!=(direction+2)%4) {
            next_direction=requested;
            if (!started) { started=1; last_move=(uint32_t)platform_millis(); draw(); }
        }
        return;
    }
    if (type!=1 || !started || ended || (uint32_t)((uint32_t)a-last_move)<200) return;
    last_move=(uint32_t)a; direction=next_direction;
    int x=body_x[0]+dx[direction],y=body_y[0]+dy[direction];
    if (x<0 || x>=COLS || y<0 || y>=ROWS) { finish(); return; }
    int grow=(unsigned)x==food_x && (unsigned)y==food_y;
    unsigned occupied_length=grow?length:length-1;
    for (unsigned i=0;i<occupied_length;i++) if (body_x[i]==x && body_y[i]==y) { finish(); return; }
    if (grow && length<MAX_BODY) { length++; score++; }
    for (unsigned i=length-1;i>0;i--) { body_x[i]=body_x[i-1]; body_y[i]=body_y[i-1]; }
    body_x[0]=(unsigned char)x; body_y[0]=(unsigned char)y;
    if (length==MAX_BODY) { finish(); return; }
    if (grow) food(); draw();
}

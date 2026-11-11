#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "scene_render.h"
static uint16_t guarded[480*400+2];
int main(void) {
    scene_cmd_t c={.kind=SCENE_RECT,.rgb=0xffffff,.w=480,.h=400};
    assert(scene_validate(&c,1)); guarded[0]=0x1234;guarded[480*400+1]=0x5678;
    scene_shape(guarded+1,480,&c);assert(guarded[1]==0xffff&&guarded[480*400]==0xffff);
    c.x=479;c.y=399;c.w=1;c.h=1;c.kind=SCENE_ELLIPSE;
    assert(scene_validate(&c,1));scene_shape(guarded+1,480,&c);
    c.w=2;assert(!scene_validate(&c,1));c.x=-1;assert(!scene_validate(&c,1));
    c=(scene_cmd_t){.kind=SCENE_TRIANGLE,.x=0,.y=0,.w=479,.h=399,.p=0,.q=399,.rgb=0xff0000};
    assert(scene_validate(&c,1));scene_shape(guarded+1,480,&c);c.p=480;assert(!scene_validate(&c,1));
    scene_cmd_t large[6];for(int i=0;i<6;i++)large[i]=(scene_cmd_t){.kind=SCENE_RECT,.w=480,.h=400};
    assert(!scene_validate(large,6));assert(!scene_validate(large,97));
    c=(scene_cmd_t){.kind=SCENE_TEXT,.w=10,.h=10};memset(c.text,'x',32);assert(!scene_validate(&c,1));
    c.text[31]=0;assert(scene_validate(&c,1));
    unsigned seed=1;
    for(int i=0;i<5000;i++) {
        seed=seed*1664525u+1013904223u;int x=seed%480;
        seed=seed*1664525u+1013904223u;int y=seed%400;
        c=(scene_cmd_t){.kind=i%2+1,.x=x,.y=y,.w=1+(int)(seed%(480-x)),.h=1+(int)(seed%(400-y)),.p=(int)(seed%200),.rgb=seed&0xffffff};
        assert(scene_validate(&c,1));scene_shape(guarded+1,480,&c);
        assert(guarded[0]==0x1234&&guarded[480*400+1]==0x5678);
    }
    puts("PASS scene renderer: quotas, invalid coordinates/text, edge pixels and 5000 guard-buffer shapes");
}

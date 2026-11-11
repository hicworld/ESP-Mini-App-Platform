#include "scene_render.h"
#include <string.h>
#include <math.h>
static int min(int a,int b) { return a<b?a:b; }
static int max(int a,int b) { return a>b?a:b; }
bool scene_validate(const scene_cmd_t *c,unsigned count) {
    if (!c || !count || count>96) return false;
    unsigned area=0;
    for (unsigned i=0;i<count;i++,c++) {
        if (c->kind<SCENE_RECT || c->kind>SCENE_TEXT || c->rgb>0xffffff ||
            c->x<0 || c->x>=480 || c->y<0 || c->y>=400) return false;
        if (c->kind==SCENE_TRIANGLE) {
            if (c->w<0 || c->w>=480 || c->p<0 || c->p>=480 || c->h<0 || c->h>=400 || c->q<0 || c->q>=400) return false;
            area+=(max(c->x,max(c->w,c->p))-min(c->x,min(c->w,c->p))+1)*
                  (max(c->y,max(c->h,c->q))-min(c->y,min(c->h,c->q))+1);
        } else {
            if (c->w<1 || c->h<1 || c->w>480-c->x || c->h>400-c->y ||
                c->p<0 || c->p>200 || c->q!=0) return false;
            if (c->kind==SCENE_TEXT && (!memchr(c->text,0,32) || c->p)) return false;
            area+=(unsigned)c->w*c->h;
        }
        if (area>1000000) return false;
    }
    return true;
}
static void span(uint16_t *pixels,size_t stride,int x,int y,int width,uint16_t color) {
    for (int n=0;n<width;n++) pixels[(size_t)y*stride+x+n]=color;
}
static int edge(int ax,int ay,int bx,int by,int x,int y) { return (x-ax)*(by-ay)-(y-ay)*(bx-ax); }
void scene_shape(uint16_t *pixels,size_t stride,const scene_cmd_t *c) {
    uint16_t color=(uint16_t)(((c->rgb>>8)&0xf800)|((c->rgb>>5)&0x7e0)|((c->rgb>>3)&31));
    if (c->kind==SCENE_TRIANGLE) {
        int x0=min(c->x,min(c->w,c->p)),x1=max(c->x,max(c->w,c->p));
        int y0=min(c->y,min(c->h,c->q)),y1=max(c->y,max(c->h,c->q));
        if (!edge(c->x,c->y,c->w,c->h,c->p,c->q)) return;
        for (int y=y0;y<=y1;y++) for (int x=x0;x<=x1;x++) {
            int a=edge(c->x,c->y,c->w,c->h,x,y),b=edge(c->w,c->h,c->p,c->q,x,y),d=edge(c->p,c->q,c->x,c->y,x,y);
            if ((a>=0&&b>=0&&d>=0)||(a<=0&&b<=0&&d<=0)) pixels[(size_t)y*stride+x]=color;
        }
    } else if (c->kind==SCENE_RECT || c->kind==SCENE_ELLIPSE) {
        int radius=min(c->p,min(c->w,c->h)/2);
        for (int row=0;row<c->h;row++) {
            int inset=0;
            if (c->kind==SCENE_ELLIPSE) {
                float y=(2.0f*row+1-c->h)/c->h;
                inset=(int)(c->w*(1.0f-sqrtf(1.0f-y*y))*0.5f);
            } else if (radius && (row<radius || row>=c->h-radius)) {
                float y=row<radius?radius-row-0.5f:row-(c->h-radius)+0.5f;
                inset=(int)(radius-sqrtf(radius*radius-y*y));
            }
            span(pixels,stride,c->x+inset,c->y+row,c->w-2*inset,color);
        }
    }
}

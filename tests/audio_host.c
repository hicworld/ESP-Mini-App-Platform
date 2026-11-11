#include "audio_synth.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
static void u32(FILE *f,uint32_t v){for(unsigned i=0;i<4;i++)fputc((v>>(i*8))&255,f);}
static void u16(FILE *f,uint16_t v){fputc(v&255,f);fputc(v>>8,f);}
static void render(const char *path,int music,int fx,unsigned seconds) {
    FILE *f=fopen(path,"wb");assert(f);unsigned frames=seconds*GARDEN_AUDIO_RATE;
    fwrite("RIFF",1,4,f);u32(f,36+frames*2);fwrite("WAVEfmt ",1,8,f);u32(f,16);u16(f,1);u16(f,1);u32(f,GARDEN_AUDIO_RATE);u32(f,GARDEN_AUDIO_RATE*2);u16(f,2);u16(f,16);fwrite("data",1,4,f);u32(f,frames*2);
    garden_synth_t s;garden_synth_init(&s);garden_synth_music(&s,music);garden_synth_effect(&s,fx);
    struct {int16_t data[320];uint32_t guard;} b={.guard=0xa55a1234};int peak=0;uint64_t energy=0;
    for(unsigned n=0;n<frames;n+=320){garden_synth_render(&s,b.data,320,75);assert(b.guard==0xa55a1234);for(unsigned i=0;i<320;i++){int a=abs(b.data[i]);if(a>peak)peak=a;energy+=(uint64_t)a*a;}fwrite(b.data,2,320,f);}
    fclose(f);assert(peak>300&&peak<24000&&energy>100000);printf("PASS %s peak=%d\n",path,peak);
    garden_synth_music(&s,0);garden_synth_effect(&s,0);garden_synth_render(&s,b.data,320,75);for(unsigned i=0;i<320;i++)assert(!b.data[i]);
}
int main(void) {
    render("day-garden.wav",1,0,48);render("night-musicbox.wav",2,0,64);
    const char *names[]={"pet-chime.wav","feeding.wav","watering.wav","welcome.wav","breeze.wav"};
    for(int i=0;i<5;i++)render(names[i],0,i+1,3);
    garden_synth_t a,b;garden_synth_init(&a);garden_synth_init(&b);garden_synth_music(&a,1);garden_synth_music(&b,1);
    int16_t x[320],y[320];garden_synth_render(&a,x,320,30);garden_synth_render(&b,y,123,30);garden_synth_render(&b,y+123,197,30);assert(!memcmp(x,y,sizeof(x)));
    for(unsigned i=0;i<60;i++)garden_synth_render(&a,x,320,0);for(unsigned i=0;i<320;i++)assert(!x[i]);
    puts("PASS synth: 2 scores, 5 finite effects, headroom, buffer guards, chunk continuity, stop and settled mute");return 0;
}

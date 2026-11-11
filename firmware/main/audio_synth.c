/* Original short pentatonic scores and procedural effects; no sampled recordings. */
#include "audio_synth.h"
#include "audio_tables.h"
#include <string.h>
static const uint8_t day[64]={
  72,0,76,79,81,79,76,0,74,0,76,79,76,74,72,0,
  69,0,72,76,79,76,72,0,67,0,69,72,74,0,0,0,
  76,0,79,81,84,81,79,0,76,0,74,72,74,76,79,0,
  81,79,76,72,69,72,74,0,76,74,72,0,0,0,0,0};
static const uint8_t night[64]={
  72,0,0,79,0,76,0,0,69,0,0,76,0,72,0,0,
  67,0,0,74,0,72,0,0,69,0,0,72,0,0,0,0,
  76,0,0,81,0,79,0,0,74,0,0,79,0,76,0,0,
  72,0,0,76,0,74,0,0,72,0,0,0,0,0,0,0};
static const uint8_t roots[8]={48,45,53,55,48,45,53,55};
static int sine(uint32_t phase) {return audio_sine[phase>>23];}
static void note(garden_voice_t *v,unsigned midi,unsigned length,int amp) {
    v->phase=0;v->step=audio_steps[midi];v->age=0;v->length=length;v->amplitude=amp;
}
static int voice(garden_voice_t *v) {
    if (!v->length||v->age>=v->length)return 0;
    uint32_t left=v->length-v->age;
    int envelope=(int)(left*1024/v->length);envelope=envelope*envelope/1024;
    if(v->age<128)envelope=envelope*(int)v->age/128;
    int wave=(sine(v->phase)*7+sine(v->phase*2)*2+sine(v->phase*3))/10;
    v->phase+=v->step;v->age++;
    return (int)((int64_t)wave*v->amplitude*envelope/(32768*1024));
}
void garden_synth_init(garden_synth_t *s) {memset(s,0,sizeof(*s));s->rng=2463534242u;}
void garden_synth_music(garden_synth_t *s,int music) {
    if(s->music==music)return;
    s->music=music;s->clock=0;memset(s->voices,0,sizeof(s->voices));
}
void garden_synth_effect(garden_synth_t *s,int effect) {s->fx=effect;s->fx_age=s->fx_phase=0;s->filter=0;}
static int effect(garden_synth_t *s) {
    if(!s->fx)return 0;
    unsigned age=s->fx_age++,length=s->fx>=4?28800:s->fx==3?24000:11200;
    if(age>=length){s->fx=0;return 0;}
    s->rng^=s->rng<<13;s->rng^=s->rng>>17;s->rng^=s->rng<<5;
    s->filter+=((int)(s->rng&65535)-32768-s->filter)/32;
    int envelope=(int)((length-age)*1024/length);if(age<320)envelope=envelope*(int)age/320;
    int wave=0;
    if(s->fx==5)wave=s->filter*2;
    else if(s->fx==3) {
        /* A soft pour under rounded, lower droplets.  The old effect used
           bright notes and much more noise, which sounded harsh on the
           small onboard speaker. */
        static const uint8_t drops[]={79,83,76,81,74,79,72,76};
        unsigned period=3200,t=age%period,part=age/period;
        unsigned midi=drops[part<sizeof(drops)?part:sizeof(drops)-1];
        s->fx_phase+=audio_steps[midi]+(uint32_t)(period-t)*3000u;
        int e=(int)((period-t)*1024/period);if(t<120)e=e*(int)t/120;
        int tone=(sine(s->fx_phase)*3+sine(s->fx_phase*2))/4;
        wave=tone*e/6144+s->filter/14;
    } else {
        unsigned period=s->fx==2?2400:3200,t=age%period,part=age/period;
        static const uint8_t pet[]={72,76,79,76},food[]={76,72,67,0},hello[]={72,76,79,84};
        unsigned midi=part<4?(s->fx==1?pet[part]:s->fx==2?food[part]:hello[part]):0;
        if(midi){s->fx_phase+=audio_steps[midi];int e=(int)((period-t)*1024/period);if(t<80)e=e*(int)t/80;wave=sine(s->fx_phase)*e/3072;}
        if(s->fx==4)wave+=s->filter;
    }
    int gain=s->fx<=2?4:s->fx==3?2:1;
    return wave*envelope/4096*gain;
}
void garden_synth_render(garden_synth_t *s,int16_t *mono,size_t frames,int volume) {
    if(volume<0)volume=0;
    if(volume>75)volume=75;
    for(size_t i=0;i<frames;i++) {
        if(s->music) {
            unsigned beat=s->music==2?16000:12000,step=s->clock/beat;
            if(s->clock%beat==0) {
                unsigned midi=(s->music==2?night:day)[step%64];
                if(midi)note(&s->voices[0],midi,beat,4200);
                unsigned root=roots[(step/8)%8];
                if(step%4==0)note(&s->voices[1],root,beat*4,2500);
                if(step%2==0)note(&s->voices[2],root+12+(step%4?7:0),beat*2,1800);
            }
            s->clock=(s->clock+1)%(beat*64);
        }
        int music=voice(&s->voices[0])+voice(&s->voices[1])+voice(&s->voices[2]);
        int sample=music*(s->fx?2:3)/3+effect(s);
        int target=volume*256;if(s->gain<target)s->gain++;else if(s->gain>target)s->gain--;
        /* More level for the small onboard speaker, with clipping headroom. */
        sample=sample*s->gain/(55*256);
        if(sample>30000)sample=30000;
        if(sample< -30000)sample= -30000;
        mono[i]=(int16_t)sample;
    }
}

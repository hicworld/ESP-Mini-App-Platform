#pragma once
#include <stdint.h>
#include <stddef.h>
#define GARDEN_AUDIO_RATE 16000
typedef struct {uint32_t phase,step,age,length;int amplitude;} garden_voice_t;
typedef struct {garden_voice_t voices[3];uint32_t clock,fx_age,fx_phase,rng;int music,fx,filter,gain;} garden_synth_t;
void garden_synth_init(garden_synth_t *s);
void garden_synth_music(garden_synth_t *s,int music);
void garden_synth_effect(garden_synth_t *s,int effect);
void garden_synth_render(garden_synth_t *s,int16_t *mono,size_t frames,int volume);

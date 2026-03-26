/*
 * VirtualOrgan — virtual pipe organ engine for Linux
 * Copyright (C) 2026 Brandon Blodget
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <string.h>
#include "voice.h"

void voice_pool_init(VoicePool *pool)
{
    memset(pool, 0, sizeof(*pool));
}

Voice *voice_pool_note_on(VoicePool *pool, uint8_t note, uint8_t velocity,
                          const Sample *sample, int division,
                          const int *out_channels, int num_out, int src_channel_offset,
                          bool mono_source)
{
    if (!sample || !sample->data)
        return NULL;

    /* Find a free voice slot */
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!pool->voices[i].active) {
            Voice *v = &pool->voices[i];
            v->active    = true;
            v->note      = note;
            v->velocity  = velocity;
            v->sample    = sample;
            v->position  = 0;
            v->phase     = VOICE_ATTACK;
            v->note_held = true;
            v->release_pos = 0;
            v->division  = division;
            v->num_out_channels = num_out;
            v->src_channel_offset = src_channel_offset;
            v->mono_source = mono_source;
            for (int c = 0; c < num_out && c < MAX_OUTPUT_CHANNELS; c++)
                v->out_channels[c] = out_channels[c];
            pool->active_count++;
            return v;
        }
    }

    return NULL;  /* pool full */
}

void voice_pool_note_off(VoicePool *pool, uint8_t note)
{
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &pool->voices[i];
        if (v->active && v->note == note)
            v->note_held = false;
    }
}

/* Write one frame to the voice's routed output channels.
 * Maps sample channels (from src_channel_offset) to output buffers. */
static inline void output_frame(const Voice *v, int pos, float gain,
                                float **bufs, int buf_idx)
{
    const Sample *s = v->sample;
    /* Iterate output channels and map to sample channels from this perspective */
    for (int i = 0; i < v->num_out_channels; i++) {
        int out_ch = v->out_channels[i];
        int src_ch = v->src_channel_offset + (v->mono_source ? 0 : i);
        if (src_ch >= s->channels) src_ch = v->src_channel_offset;
        bufs[out_ch][buf_idx] += s->data[src_ch][pos] * gain;
    }
}

/* Render one sustain frame. Loop points are chosen at zero-crossings
 * by the sample set creator, so no crossfade is needed at the boundary. */
static inline void render_sustain(Voice *voice, const Sample *s,
                                  float gain, float **bufs, int buf_idx)
{
    output_frame(voice, voice->position, gain, bufs, buf_idx);
    voice->position++;
    if (voice->position >= s->loop_end)
        voice->position = s->loop_start;
}

/* Release fade-out duration in frames (set by voice_set_release_fade) */
static int release_fade_frames = 12000;  /* default ~250ms at 48kHz */

static inline void begin_release(Voice *voice)
{
    voice->release_pos = 0;
    voice->phase = VOICE_RELEASE;
}

bool voice_render(Voice *voice, float **bufs, int num_channels, int nframes,
                  float expression_gain)
{
    (void)num_channels;  /* routing is per-voice now */
    const Sample *s = voice->sample;
    float gain = (float)voice->velocity / 127.0f * expression_gain;

    for (int i = 0; i < nframes; i++) {
        /* Bounds check */
        if (voice->position < 0 || voice->position >= s->frames) {
            voice->active = false;
            return false;
        }

        switch (voice->phase) {
        case VOICE_ATTACK:
            output_frame(voice, voice->position, gain, bufs, i);
            voice->position++;

            if (!voice->note_held) {
                begin_release(voice);
                break;
            }

            if (s->has_loop && voice->position >= s->loop_start)
                voice->phase = VOICE_SUSTAIN;
            break;

        case VOICE_SUSTAIN:
            if (!voice->note_held) {
                begin_release(voice);
                break;
            }

            render_sustain(voice, s, gain, bufs, i);
            break;

        case VOICE_RELEASE: {
            /* Fade out from current position, continuing loop */
            float fade = 1.0f - (float)voice->release_pos / release_fade_frames;
            if (fade <= 0.0f) {
                voice->active = false;
                return false;
            }

            output_frame(voice, voice->position, gain * fade, bufs, i);
            voice->position++;
            voice->release_pos++;

            /* Keep looping if we have loop points */
            if (s->has_loop && voice->position >= s->loop_end)
                voice->position = s->loop_start;

            /* End if no loop and past sample end */
            if (voice->position >= s->frames) {
                voice->active = false;
                return false;
            }
            break;
        }
        }
    }

    return true;
}

void voice_set_release_fade(int ms, int sample_rate)
{
    release_fade_frames = ms * sample_rate / 1000;
    if (release_fade_frames < 1) release_fade_frames = 1;
    printf("voice: release fade = %dms (%d frames)\n", ms, release_fade_frames);
}

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

#include <string.h>
#include "voice.h"

void voice_pool_init(VoicePool *pool)
{
    memset(pool, 0, sizeof(*pool));
}

Voice *voice_pool_note_on(VoicePool *pool, uint8_t note, uint8_t velocity, const Sample *sample)
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

/* Compute crossfade weights for sustain loop boundary.
 * Sets fade_out and fade_in weights, and wrap_pos for the blend source.
 * Returns true if crossfade is active for this position. */
static inline bool sustain_crossfade(const Sample *s, int position,
                                     int *wrap_pos, float *fade_out, float *fade_in)
{
    int loop_len = s->loop_end - s->loop_start;
    int cf = CROSSFADE_FRAMES;
    if (cf > loop_len / 2)
        cf = loop_len / 2;

    int dist_to_end = s->loop_end - position;
    if (cf > 0 && dist_to_end <= cf && dist_to_end > 0) {
        *fade_out = (float)dist_to_end / (float)cf;
        *fade_in  = 1.0f - *fade_out;
        *wrap_pos = s->loop_start + (cf - dist_to_end);
        return true;
    }
    return false;
}

/* Advance position after sustain frame, wrapping at loop end. */
static inline void sustain_advance(Voice *voice, const Sample *s)
{
    int loop_len = s->loop_end - s->loop_start;
    int cf = CROSSFADE_FRAMES;
    if (cf > loop_len / 2)
        cf = loop_len / 2;

    voice->position++;
    if (voice->position >= s->loop_end) {
        voice->position = s->loop_start + cf;
        if (voice->position >= s->loop_end)
            voice->position = s->loop_start;
    }
}

/* Write one frame from sample to all output channels with gain.
 * Mono samples are duplicated to all output channels. */
static inline void output_frame(const Sample *s, int pos, float gain,
                                float **bufs, int num_channels, int buf_idx)
{
    int src_channels = s->channels;
    for (int ch = 0; ch < num_channels; ch++) {
        int src_ch = (ch < src_channels) ? ch : 0;  /* mono → duplicate ch0 */
        bufs[ch][buf_idx] += s->data[src_ch][pos] * gain;
    }
}

/* Write one crossfaded frame to all output channels. */
static inline void output_crossfade_frame(const Sample *s, int pos, int wrap_pos,
                                          float fade_out, float fade_in, float gain,
                                          float **bufs, int num_channels, int buf_idx)
{
    int src_channels = s->channels;
    for (int ch = 0; ch < num_channels; ch++) {
        int src_ch = (ch < src_channels) ? ch : 0;
        float val = s->data[src_ch][pos] * fade_out
                  + s->data[src_ch][wrap_pos] * fade_in;
        bufs[ch][buf_idx] += val * gain;
    }
}

bool voice_render(Voice *voice, float **bufs, int num_channels, int nframes)
{
    const Sample *s = voice->sample;
    float gain = (float)voice->velocity / 127.0f;

    for (int i = 0; i < nframes; i++) {
        /* Bounds check */
        if (voice->position < 0 || voice->position >= s->frames) {
            voice->active = false;
            return false;
        }

        switch (voice->phase) {
        case VOICE_ATTACK:
            output_frame(s, voice->position, gain, bufs, num_channels, i);
            voice->position++;

            if (!voice->note_held) {
                voice->phase = VOICE_RELEASE;
                break;
            }

            if (s->has_loop && voice->position >= s->loop_start)
                voice->phase = VOICE_SUSTAIN;
            break;

        case VOICE_SUSTAIN:
            if (!voice->note_held) {
                voice->phase = VOICE_RELEASE;
                output_frame(s, voice->position, gain, bufs, num_channels, i);
                voice->position++;
                break;
            }

            {
                int wrap_pos;
                float fade_out, fade_in;
                if (sustain_crossfade(s, voice->position, &wrap_pos, &fade_out, &fade_in))
                    output_crossfade_frame(s, voice->position, wrap_pos,
                                           fade_out, fade_in, gain,
                                           bufs, num_channels, i);
                else
                    output_frame(s, voice->position, gain, bufs, num_channels, i);
            }

            sustain_advance(voice, s);
            break;

        case VOICE_RELEASE:
            output_frame(s, voice->position, gain, bufs, num_channels, i);
            voice->position++;

            if (voice->position >= s->frames) {
                voice->active = false;
                return false;
            }
            break;
        }
    }

    return true;
}

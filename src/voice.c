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

/* Render one sustain frame. Loop points are chosen at zero-crossings
 * by the sample set creator, so no crossfade is needed at the boundary. */
static inline void render_sustain(Voice *voice, const Sample *s,
                                  float gain, float **bufs,
                                  int num_channels, int buf_idx)
{
    output_frame(s, voice->position, gain, bufs, num_channels, buf_idx);
    voice->position++;
    if (voice->position >= s->loop_end)
        voice->position = s->loop_start;
}

/* Begin crossfade transition to release tail.
 * If sample has loop points, crossfade from current position to loop_end.
 * If no loop points, apply a short fade-out from current position. */
static inline void begin_release(Voice *voice, const Sample *s)
{
    if (s->has_loop) {
        voice->xfade_from = voice->position;
        voice->xfade_to = s->loop_end;
        voice->xfade_pos = 0;
        voice->phase = VOICE_RELEASE_XFADE;
    } else {
        /* No loop points: just fade out over CROSSFADE_FRAMES */
        voice->xfade_pos = 0;
        voice->phase = VOICE_RELEASE;
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
                begin_release(voice, s);
                break;
            }

            if (s->has_loop && voice->position >= s->loop_start)
                voice->phase = VOICE_SUSTAIN;
            break;

        case VOICE_SUSTAIN:
            if (!voice->note_held) {
                begin_release(voice, s);
                break;
            }

            render_sustain(voice, s, gain, bufs, num_channels, i);
            break;

        case VOICE_RELEASE_XFADE: {
            /* Crossfade from current playback position to release tail */
            int from_pos = voice->xfade_from + voice->xfade_pos;
            int to_pos   = voice->xfade_to + voice->xfade_pos;

            if (from_pos >= s->frames) from_pos = s->frames - 1;
            if (to_pos >= s->frames) to_pos = s->frames - 1;

            float fade_out = 1.0f - (float)voice->xfade_pos / CROSSFADE_FRAMES;
            float fade_in  = (float)voice->xfade_pos / CROSSFADE_FRAMES;

            output_crossfade_frame(s, from_pos, to_pos,
                                   fade_out, fade_in, gain,
                                   bufs, num_channels, i);
            voice->xfade_pos++;

            if (voice->xfade_pos >= CROSSFADE_FRAMES) {
                /* Crossfade complete, continue playing release tail */
                voice->position = voice->xfade_to + voice->xfade_pos;
                voice->phase = VOICE_RELEASE;
            }
            break;
        }

        case VOICE_RELEASE:
            if (!s->has_loop) {
                /* No loop: fade out from current position */
                float fade = 1.0f - (float)voice->xfade_pos / CROSSFADE_FRAMES;
                if (fade <= 0.0f) {
                    voice->active = false;
                    return false;
                }
                output_frame(s, voice->position, gain * fade, bufs, num_channels, i);
                voice->position++;
                voice->xfade_pos++;
            } else {
                output_frame(s, voice->position, gain, bufs, num_channels, i);
                voice->position++;
            }

            if (voice->position >= s->frames) {
                voice->active = false;
                return false;
            }
            break;
        }
    }

    return true;
}

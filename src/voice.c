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

Voice *voice_pool_note_on(VoicePool *pool, uint8_t note, uint8_t velocity,
                          const Sample *sample, int division,
                          const int *out_channels, int num_out, int src_channel_offset)
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
            v->division  = division;
            v->num_out_channels = num_out;
            v->src_channel_offset = src_channel_offset;
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
        int src_ch = v->src_channel_offset + i;
        if (src_ch >= s->channels) src_ch = v->src_channel_offset; /* duplicate first */
        bufs[out_ch][buf_idx] += s->data[src_ch][pos] * gain;
    }
}

/* Write one crossfaded frame to the voice's routed output channels. */
static inline void output_crossfade_frame(const Voice *v, int pos, int wrap_pos,
                                          float fade_out, float fade_in, float gain,
                                          float **bufs, int buf_idx)
{
    const Sample *s = v->sample;
    for (int i = 0; i < v->num_out_channels; i++) {
        int out_ch = v->out_channels[i];
        int src_ch = v->src_channel_offset + i;
        if (src_ch >= s->channels) src_ch = v->src_channel_offset;
        float val = s->data[src_ch][pos] * fade_out
                  + s->data[src_ch][wrap_pos] * fade_in;
        bufs[out_ch][buf_idx] += val * gain;
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

/* Begin transition to release tail.
 * Crossfade from the current sustain position into a phase-aligned
 * position in the release tail. Since loop_start and loop_end are at
 * the same waveform phase, our offset within the loop tells us the
 * equivalent offset into the release:
 *   release_start = loop_end + (current_pos - loop_start)
 * If no loop points, apply a short fade-out from current position. */
static inline void begin_release(Voice *voice, const Sample *s)
{
    if (s->has_loop) {
        int offset;
        if (voice->position >= s->loop_start) {
            /* In sustain: offset within the loop */
            offset = voice->position - s->loop_start;
            int loop_len = s->loop_end - s->loop_start;
            if (loop_len > 0) offset = offset % loop_len;
        } else {
            /* In attack: mirror distance from loop_start.
             * N frames before loop_start → N frames before loop_end
             * in the release tail. */
            int dist = s->loop_start - voice->position;
            offset = (s->loop_end - s->loop_start) - dist;
            if (offset < 0) offset = 0;
        }

        int release_pos = s->loop_end + offset;
        /* Ensure release + crossfade fits within sample */
        if (release_pos + CROSSFADE_FRAMES >= s->frames)
            release_pos = s->loop_end;

        voice->xfade_from = voice->position;
        voice->xfade_to = release_pos;
        voice->xfade_pos = 0;
        voice->phase = VOICE_RELEASE_XFADE;
    } else {
        /* No loop points: just play to end of sample */
        voice->phase = VOICE_RELEASE;
    }
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

            render_sustain(voice, s, gain, bufs, i);
            break;

        case VOICE_RELEASE_XFADE: {
            /* Crossfade from current playback position to release tail */
            int from_pos = voice->xfade_from + voice->xfade_pos;
            int to_pos   = voice->xfade_to + voice->xfade_pos;

            if (from_pos >= s->frames) from_pos = s->frames - 1;
            if (to_pos >= s->frames) to_pos = s->frames - 1;

            float fade_out = 1.0f - (float)voice->xfade_pos / CROSSFADE_FRAMES;
            float fade_in  = (float)voice->xfade_pos / CROSSFADE_FRAMES;

            output_crossfade_frame(voice, from_pos, to_pos,
                                   fade_out, fade_in, gain, bufs, i);
            voice->xfade_pos++;

            if (voice->xfade_pos >= CROSSFADE_FRAMES) {
                /* Crossfade complete, continue playing release tail */
                voice->position = voice->xfade_to + voice->xfade_pos;
                voice->phase = VOICE_RELEASE;
            }
            break;
        }

        case VOICE_RELEASE:
            if (voice->position >= s->frames) {
                voice->active = false;
                return false;
            }

            output_frame(voice, voice->position, gain, bufs, i);
            voice->position++;
            break;
        }
    }

    return true;
}

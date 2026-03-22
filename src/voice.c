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

/* Render a single frame from a voice in SUSTAIN phase.
 * Handles crossfade at loop boundary and wrapping. */
static inline float render_sustain_frame(Voice *voice, const Sample *s)
{
    int loop_len = s->loop_end - s->loop_start;
    int cf = CROSSFADE_FRAMES;

    /* Clamp crossfade to half the loop length to avoid overlap */
    if (cf > loop_len / 2)
        cf = loop_len / 2;

    float val;
    int dist_to_end = s->loop_end - voice->position;

    if (cf > 0 && dist_to_end <= cf && dist_to_end > 0) {
        /* Crossfade: blend end of loop with start of loop */
        float fade_out = (float)dist_to_end / (float)cf;
        float fade_in  = 1.0f - fade_out;
        int wrap_pos = s->loop_start + (cf - dist_to_end);
        val = s->data[voice->position] * fade_out
            + s->data[wrap_pos] * fade_in;
    } else {
        val = s->data[voice->position];
    }

    voice->position++;

    /* Wrap at loop end back to start (past crossfade region) */
    if (voice->position >= s->loop_end) {
        voice->position = s->loop_start + cf;
        if (voice->position >= s->loop_end)
            voice->position = s->loop_start;
    }

    return val;
}

bool voice_render(Voice *voice, float *buf, int nframes)
{
    const Sample *s = voice->sample;
    float gain = (float)voice->velocity / 127.0f;

    for (int i = 0; i < nframes; i++) {
        /* Bounds check */
        if (voice->position < 0 || voice->position >= s->frames) {
            voice->active = false;
            return false;
        }

        float sample_val = 0.0f;

        switch (voice->phase) {
        case VOICE_ATTACK:
            sample_val = s->data[voice->position];
            voice->position++;

            if (!voice->note_held) {
                /* Note released during attack */
                if (s->has_loop)
                    voice->phase = VOICE_RELEASE;
                else
                    voice->phase = VOICE_RELEASE;
                break;
            }

            if (s->has_loop && voice->position >= s->loop_start)
                voice->phase = VOICE_SUSTAIN;
            break;

        case VOICE_SUSTAIN:
            if (!voice->note_held) {
                /* Note released — transition to release phase */
                voice->phase = VOICE_RELEASE;
                /* Don't render this frame as sustain, let release handle it */
                sample_val = s->data[voice->position];
                voice->position++;
                break;
            }

            sample_val = render_sustain_frame(voice, s);
            break;

        case VOICE_RELEASE:
            sample_val = s->data[voice->position];
            voice->position++;

            if (voice->position >= s->frames) {
                voice->active = false;
                buf[i] += sample_val * gain;
                return false;
            }
            break;
        }

        buf[i] += sample_val * gain;
    }

    return true;
}

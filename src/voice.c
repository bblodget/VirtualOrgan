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
            v->active   = true;
            v->note     = note;
            v->velocity = velocity;
            v->sample   = sample;
            v->position = 0;
            pool->active_count++;
            return v;
        }
    }

    return NULL;  /* pool full */
}

void voice_pool_note_off(VoicePool *pool, uint8_t note)
{
    /* Phase 2: immediate stop. Phase 3 will add release tails. */
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &pool->voices[i];
        if (v->active && v->note == note) {
            v->active = false;
            pool->active_count--;
        }
    }
}

bool voice_render(Voice *voice, float *buf, int nframes)
{
    const Sample *s = voice->sample;
    int remaining = s->frames - voice->position;

    if (remaining <= 0) {
        voice->active = false;
        return false;
    }

    /* Velocity scaling: velocity 127 = full volume */
    float gain = (float)voice->velocity / 127.0f;

    int to_render = (nframes < remaining) ? nframes : remaining;
    const float *src = s->data + voice->position;

    for (int i = 0; i < to_render; i++)
        buf[i] += src[i] * gain;

    voice->position += to_render;

    /* If we've reached the end, deactivate */
    if (voice->position >= s->frames) {
        voice->active = false;
        return false;
    }

    return true;
}

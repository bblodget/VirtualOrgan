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

#ifndef VOICE_H
#define VOICE_H

#include <stdbool.h>
#include <stdint.h>
#include "sampler.h"

#define MAX_VOICES 128

typedef struct {
    bool         active;
    uint8_t      note;
    uint8_t      velocity;
    const Sample *sample;
    int          position;  /* current playback position in frames */
} Voice;

typedef struct {
    Voice   voices[MAX_VOICES];
    int     active_count;
} VoicePool;

void voice_pool_init(VoicePool *pool);

/* Activate a voice for the given note. Returns pointer to voice, or NULL if pool full. */
Voice *voice_pool_note_on(VoicePool *pool, uint8_t note, uint8_t velocity, const Sample *sample);

/* Deactivate all voices playing the given note. */
void voice_pool_note_off(VoicePool *pool, uint8_t note);

/* Render nframes of audio from a single voice into buf (additive).
 * Returns false if voice finished and was deactivated. */
bool voice_render(Voice *voice, float *buf, int nframes);

#endif

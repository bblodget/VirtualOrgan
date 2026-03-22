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
#define CROSSFADE_FRAMES 64  /* ~1.5ms at 44.1kHz */

typedef enum {
    VOICE_ATTACK,        /* playing from 0 toward loop_start */
    VOICE_SUSTAIN,       /* looping between loop_start and loop_end */
    VOICE_RELEASE_XFADE, /* crossfading from current pos to release tail */
    VOICE_RELEASE,       /* playing release tail to end of sample */
} VoicePhase;

typedef struct {
    bool         active;
    uint8_t      note;
    uint8_t      velocity;
    const Sample *sample;
    int          position;   /* current playback position in frames */
    VoicePhase   phase;
    bool         note_held;  /* true while key is down */
    int          xfade_from; /* source position for release crossfade */
    int          xfade_to;   /* destination position (loop_end) */
    int          xfade_pos;  /* progress counter (0 to CROSSFADE_FRAMES) */
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

/* Render nframes of audio from a single voice into output buffers (additive).
 * bufs is an array of num_channels output buffers.
 * Mono samples are duplicated to all channels.
 * Returns false if voice finished and was deactivated. */
bool voice_render(Voice *voice, float **bufs, int num_channels, int nframes);

#endif

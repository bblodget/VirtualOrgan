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
#include "config.h"

#define MAX_VOICES 128

typedef enum {
    VOICE_ATTACK,   /* playing from 0 toward loop_start */
    VOICE_SUSTAIN,  /* looping between loop_start and loop_end */
    VOICE_RELEASE,  /* fading out from current position */
} VoicePhase;

typedef struct {
    bool         active;
    uint8_t      note;
    uint8_t      velocity;
    const Sample *sample;
    int          position;   /* current playback position in frames */
    VoicePhase   phase;
    bool         note_held;  /* true while key is down */
    int          division;   /* division index (-1 if no divisions) */
    int          out_channels[MAX_OUTPUT_CHANNELS]; /* 0-indexed output buffer indices */
    int          num_out_channels;                  /* how many outputs this voice writes to */
    int          src_channel_offset;                /* first sample channel for this perspective */
    int          release_pos; /* frames into fade-out (0 to RELEASE_FADE_FRAMES) */
} Voice;

typedef struct {
    Voice   voices[MAX_VOICES];
    int     active_count;
} VoicePool;

void voice_pool_init(VoicePool *pool);

/* Activate a voice for the given note.
 * out_channels/num_out are 0-indexed output buffer indices for this voice.
 * src_channel_offset is the first sample channel for this perspective.
 * division is the index into config divisions (-1 if none).
 * Returns pointer to voice, or NULL if pool full. */
Voice *voice_pool_note_on(VoicePool *pool, uint8_t note, uint8_t velocity,
                          const Sample *sample, int division,
                          const int *out_channels, int num_out, int src_channel_offset);

/* Deactivate all voices playing the given note. */
void voice_pool_note_off(VoicePool *pool, uint8_t note);

/* Render nframes of audio from a single voice into output buffers (additive).
 * bufs is an array of num_channels output buffers.
 * Mono samples are duplicated to all channels.
 * expression_gain is applied as additional volume (1.0 = full, from division).
 * Returns false if voice finished and was deactivated. */
bool voice_render(Voice *voice, float **bufs, int num_channels, int nframes,
                  float expression_gain);

#endif

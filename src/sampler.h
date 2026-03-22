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

#ifndef SAMPLER_H
#define SAMPLER_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_MIDI_NOTES 128

typedef struct {
    float  **data;       /* per-channel sample buffers: data[channel][frame] */
    int      channels;   /* number of channels (1=mono, 2=stereo, etc.) */
    int      frames;     /* number of frames per channel */
    int      sample_rate;
    int      loop_start; /* frame index where sustain loop begins */
    int      loop_end;   /* frame index where sustain loop ends */
    bool     has_loop;   /* true if valid loop points were found */
} Sample;

typedef struct {
    Sample  samples[MAX_MIDI_NOTES];  /* indexed by MIDI note number */
    int     count;                    /* number of loaded samples */
} SampleBank;

/* Load all WAV files from directory into bank.
 * Pattern uses {note} for MIDI note number and {name} for note name.
 * Examples: "{note:03d}.wav", "{note:03d}-{name}.wav"
 * If bytes_out is non-NULL, adds the total bytes loaded to *bytes_out.
 * Returns number of samples loaded, or -1 on error. */
int sampler_load(SampleBank *bank, const char *dir, const char *pattern, size_t *bytes_out);

/* Free all loaded samples. */
void sampler_free(SampleBank *bank);

#endif

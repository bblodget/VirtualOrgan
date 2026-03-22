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
#include <stdlib.h>
#include <string.h>
#include <sndfile.h>
#include "sampler.h"

static const char *note_names[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* Expand a filename pattern for a given MIDI note number.
 * Supported placeholders:
 *   {note:03d}  — zero-padded 3-digit MIDI note number (e.g. "060")
 *   {note:02d}  — zero-padded 2-digit MIDI note number (e.g. "60")
 *   {note}      — unpadded MIDI note number (e.g. "60")
 *   {name}      — note name with sharp as # (e.g. "C#")
 *   {octave}    — octave number (e.g. "4" for middle C)
 */
static int expand_pattern(const char *pattern, int note, char *out, size_t out_size)
{
    const char *p = pattern;
    char *o = out;
    char *end = out + out_size - 1;

    const char *name = note_names[note % 12];
    int octave = (note / 12) - 1;

    while (*p && o < end) {
        if (*p == '{') {
            if (strncmp(p, "{note:03d}", 10) == 0) {
                o += snprintf(o, end - o, "%03d", note);
                p += 10;
            } else if (strncmp(p, "{note:02d}", 10) == 0) {
                o += snprintf(o, end - o, "%02d", note);
                p += 10;
            } else if (strncmp(p, "{note}", 6) == 0) {
                o += snprintf(o, end - o, "%d", note);
                p += 6;
            } else if (strncmp(p, "{name}", 6) == 0) {
                o += snprintf(o, end - o, "%s", name);
                p += 6;
            } else if (strncmp(p, "{octave}", 8) == 0) {
                o += snprintf(o, end - o, "%d", octave);
                p += 8;
            } else {
                *o++ = *p++;
            }
        } else {
            *o++ = *p++;
        }
    }
    *o = '\0';
    return 0;
}

static int load_sample(SampleBank *bank, const char *path, int note, size_t *bytes_out)
{
    SF_INFO info = {0};
    SNDFILE *sf = sf_open(path, SFM_READ, &info);
    if (!sf)
        return -1;

    int frames = (int)info.frames;
    float *data = malloc(frames * info.channels * sizeof(float));
    if (!data) {
        sf_close(sf);
        return -1;
    }

    sf_readf_float(sf, data, frames);

    /* Read loop points from WAV smpl/instrument chunk */
    SF_INSTRUMENT inst = {0};
    bool has_loop = false;
    int loop_start = 0, loop_end = 0;
    if (sf_command(sf, SFC_GET_INSTRUMENT, &inst, sizeof(inst)) == SF_TRUE) {
        if (inst.loop_count > 0) {
            loop_start = (int)inst.loops[0].start;
            loop_end   = (int)inst.loops[0].end;
            if (loop_start >= 0 && loop_end > loop_start && loop_end <= frames)
                has_loop = true;
        }
    }

    sf_close(sf);

    /* If stereo or more, downmix to mono (take channel 0) */
    if (info.channels > 1) {
        float *mono = malloc(frames * sizeof(float));
        if (mono) {
            for (int i = 0; i < frames; i++)
                mono[i] = data[i * info.channels];
            free(data);
            data = mono;
        }
    }

    bank->samples[note].data = data;
    bank->samples[note].frames = frames;
    bank->samples[note].sample_rate = info.samplerate;
    bank->samples[note].has_loop = has_loop;
    bank->samples[note].loop_start = loop_start;
    bank->samples[note].loop_end = loop_end;
    bank->count++;
    *bytes_out += frames * sizeof(float);
    return 0;
}

int sampler_load(SampleBank *bank, const char *dir, const char *pattern)
{
    memset(bank, 0, sizeof(*bank));
    size_t total_bytes = 0;

    for (int note = 0; note < MAX_MIDI_NOTES; note++) {
        char filename[256];
        expand_pattern(pattern, note, filename, sizeof(filename));

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, filename);

        if (load_sample(bank, path, note, &total_bytes) == 0) {
            /* loaded successfully */
        }
    }

    printf("sampler: loaded %d samples from '%s' (%.1f MB)\n",
           bank->count, dir, total_bytes / (1024.0 * 1024.0));
    return bank->count;
}

void sampler_free(SampleBank *bank)
{
    for (int i = 0; i < MAX_MIDI_NOTES; i++) {
        free(bank->samples[i].data);
        bank->samples[i].data = NULL;
    }
    bank->count = 0;
}

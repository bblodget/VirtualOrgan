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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sndfile.h>

#define SAMPLE_RATE 48000
#define DURATION    2.0
#define AMPLITUDE   0.5

/* Convert MIDI note number to frequency in Hz */
static double midi_to_freq(int note)
{
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

static int generate_wav(const char *dir, int note)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%03d.wav", dir, note);

    SF_INFO info = {
        .samplerate = SAMPLE_RATE,
        .channels   = 1,
        .format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16,
    };

    SNDFILE *sf = sf_open(path, SFM_WRITE, &info);
    if (!sf) {
        fprintf(stderr, "Error creating %s: %s\n", path, sf_strerror(NULL));
        return -1;
    }

    int nframes = (int)(SAMPLE_RATE * DURATION);
    double freq = midi_to_freq(note);
    float *buf = malloc(nframes * sizeof(float));
    if (!buf) {
        sf_close(sf);
        return -1;
    }

    for (int i = 0; i < nframes; i++) {
        double t = (double)i / SAMPLE_RATE;
        buf[i] = (float)(AMPLITUDE * sin(2.0 * M_PI * freq * t));
    }

    /* Apply a short fade-in and fade-out to avoid clicks */
    int fade = SAMPLE_RATE / 100; /* 10ms */
    for (int i = 0; i < fade && i < nframes; i++) {
        float g = (float)i / fade;
        buf[i] *= g;
        buf[nframes - 1 - i] *= g;
    }

    sf_writef_float(sf, buf, nframes);
    sf_close(sf);
    free(buf);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <output_dir>\n", argv[0]);
        return 1;
    }

    const char *dir = argv[1];
    int count = 0;

    for (int note = 36; note <= 84; note++) {
        if (generate_wav(dir, note) == 0)
            count++;
    }

    printf("Generated %d test samples in %s/\n", count, dir);
    return 0;
}

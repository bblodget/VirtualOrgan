#ifndef SAMPLER_H
#define SAMPLER_H

#include <stdint.h>

#define MAX_MIDI_NOTES 128

typedef struct {
    float   *data;       /* interleaved sample data (mono float32) */
    int      frames;     /* number of frames */
    int      sample_rate;
} Sample;

typedef struct {
    Sample  samples[MAX_MIDI_NOTES];  /* indexed by MIDI note number */
    int     count;                    /* number of loaded samples */
} SampleBank;

/* Load all WAV files from directory into bank.
 * Pattern uses {note} for MIDI note number and {name} for note name.
 * Examples: "{note:03d}.wav", "{note:03d}-{name}.wav"
 * Returns number of samples loaded, or -1 on error. */
int sampler_load(SampleBank *bank, const char *dir, const char *pattern);

/* Free all loaded samples. */
void sampler_free(SampleBank *bank);

#endif

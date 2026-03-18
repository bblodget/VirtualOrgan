#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sndfile.h>
#include "sampler.h"

int sampler_load(SampleBank *bank, const char *dir)
{
    memset(bank, 0, sizeof(*bank));

    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "sampler: cannot open directory '%s'\n", dir);
        return -1;
    }

    struct dirent *ent;
    size_t total_bytes = 0;

    while ((ent = readdir(d)) != NULL) {
        /* Expect files named NNN.wav */
        int note = atoi(ent->d_name);
        if (note < 0 || note >= MAX_MIDI_NOTES)
            continue;

        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || strcmp(ext, ".wav") != 0)
            continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        SF_INFO info = {0};
        SNDFILE *sf = sf_open(path, SFM_READ, &info);
        if (!sf) {
            fprintf(stderr, "sampler: cannot open '%s': %s\n", path, sf_strerror(NULL));
            continue;
        }

        /* Read as mono float */
        int frames = (int)info.frames;
        float *data = malloc(frames * sizeof(float));
        if (!data) {
            sf_close(sf);
            continue;
        }

        sf_readf_float(sf, data, frames);
        sf_close(sf);

        /* If stereo or more, we already read interleaved — just take channel 0 */
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
        bank->count++;
        total_bytes += frames * sizeof(float);
    }

    closedir(d);
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

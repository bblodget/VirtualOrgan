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
#include <string.h>
#include "mixer.h"

/* Master gain — adjustable at runtime via keyboard controls */
static float master_gain = 0.10f;

/* Soft clamp to prevent harsh digital clipping.
 * Uses tanh for a smooth saturation curve. */
static inline float soft_clip(float x)
{
    if (x > 1.0f || x < -1.0f)
        return tanhf(x);
    return x;
}

void mixer_render(VoicePool *pool, float **bufs, int num_channels, int nframes)
{
    /* Clear output buffers */
    for (int ch = 0; ch < num_channels; ch++)
        memset(bufs[ch], 0, nframes * sizeof(float));

    /* Render each active voice into output buffers */
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &pool->voices[i];
        if (!v->active)
            continue;

        bool still_active = voice_render(v, bufs, num_channels, nframes);

        if (!still_active)
            pool->active_count--;
    }

    /* Apply master gain and soft clipping to all channels */
    float g = master_gain;
    for (int ch = 0; ch < num_channels; ch++) {
        for (int i = 0; i < nframes; i++)
            bufs[ch][i] = soft_clip(bufs[ch][i] * g);
    }
}

float mixer_get_gain(void)
{
    return master_gain;
}

void mixer_set_gain(float gain)
{
    if (gain < 0.01f) gain = 0.01f;
    if (gain > 2.0f) gain = 2.0f;
    master_gain = gain;
}

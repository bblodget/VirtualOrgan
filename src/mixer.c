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

/* Master gain to prevent clipping when multiple voices play.
 * 0.15 allows ~6 simultaneous voices at full velocity before clipping. */
#define MASTER_GAIN 0.15f

/* Soft clamp to prevent harsh digital clipping.
 * Uses tanh for a smooth saturation curve. */
static inline float soft_clip(float x)
{
    if (x > 1.0f || x < -1.0f)
        return tanhf(x);
    return x;
}

void mixer_render(VoicePool *pool, float *left, float *right, int nframes)
{
    /* Clear output buffers */
    memset(left, 0, nframes * sizeof(float));
    memset(right, 0, nframes * sizeof(float));

    /* Render each active voice into a mono mix buffer */
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &pool->voices[i];
        if (!v->active)
            continue;

        bool still_active = voice_render(v, left, nframes);

        if (!still_active)
            pool->active_count--;
    }

    /* Apply master gain and soft clipping, duplicate to stereo */
    for (int i = 0; i < nframes; i++) {
        left[i] = soft_clip(left[i] * MASTER_GAIN);
        right[i] = left[i];
    }
}

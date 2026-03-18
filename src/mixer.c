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

#include <string.h>
#include "mixer.h"

void mixer_render(VoicePool *pool, float *left, float *right, int nframes)
{
    /* Clear output buffers */
    memset(left, 0, nframes * sizeof(float));
    memset(right, 0, nframes * sizeof(float));

    /* Render each active voice into a mono mix buffer, then copy to stereo */
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &pool->voices[i];
        if (!v->active)
            continue;

        /* voice_render adds into the buffer, so we render into left
         * and then copy to right for simple stereo (mono duplicated). */
        bool still_active = voice_render(v, left, nframes);

        if (!still_active)
            pool->active_count--;
    }

    /* Duplicate left to right for Phase 2 stereo */
    memcpy(right, left, nframes * sizeof(float));
}

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

#ifndef MIXER_H
#define MIXER_H

#include "voice.h"
#include "config.h"

/* Render all active voices into output buffers.
 * bufs is an array of num_channels output buffers, each with nframes floats.
 * config is used to look up per-division expression gain (may be NULL). */
void mixer_render(VoicePool *pool, float **bufs, int num_channels, int nframes,
                  const OrganConfig *config);

/* Get/set master gain (0.01 to 2.0). */
float mixer_get_gain(void);
void mixer_set_gain(float gain);

#endif

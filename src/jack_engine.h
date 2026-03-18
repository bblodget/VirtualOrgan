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

#ifndef JACK_ENGINE_H
#define JACK_ENGINE_H

#include "ring_buffer.h"
#include "voice.h"
#include "sampler.h"
#include "config.h"

typedef struct {
    RingBuffer  *ring_buffer;
    VoicePool   *voice_pool;
    SampleBank  *sample_bank;
    OrganConfig *config;
} JackEngineCtx;

/* Start the JACK client. Returns 0 on success. */
int jack_engine_start(JackEngineCtx *ctx);

/* Stop and disconnect the JACK client. */
void jack_engine_stop(void);

#endif

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

#ifndef MIDI_H
#define MIDI_H

#include <stdbool.h>
#include "ring_buffer.h"

/* Start MIDI input thread. If fake_midi is true, generates test events
 * instead of reading from ALSA sequencer. Returns 0 on success. */
int midi_start(RingBuffer *rb, bool fake_midi);

/* Stop MIDI input thread and clean up. */
void midi_stop(void);

#endif

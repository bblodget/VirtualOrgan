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

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define MIDI_NOTE_ON  0x90
#define MIDI_NOTE_OFF 0x80
#define MIDI_CC       0xB0

typedef struct {
    uint8_t type;      /* MIDI_NOTE_ON, MIDI_NOTE_OFF, MIDI_CC */
    uint8_t channel;
    uint8_t note;      /* note number or CC number */
    uint8_t velocity;  /* velocity or CC value */
} MidiEvent;

#define RING_BUFFER_SIZE 256  /* must be power of 2 */

typedef struct {
    MidiEvent        events[RING_BUFFER_SIZE];
    atomic_uint      write_pos;
    atomic_uint      read_pos;
} RingBuffer;

void ring_buffer_init(RingBuffer *rb);

/* Producer: push event into buffer. Returns false if full. */
bool ring_buffer_push(RingBuffer *rb, const MidiEvent *event);

/* Consumer: pop event from buffer. Returns false if empty. RT-safe. */
bool ring_buffer_pop(RingBuffer *rb, MidiEvent *event);

#endif

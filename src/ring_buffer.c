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

#include "ring_buffer.h"

void ring_buffer_init(RingBuffer *rb)
{
    atomic_store(&rb->write_pos, 0);
    atomic_store(&rb->read_pos, 0);
}

bool ring_buffer_push(RingBuffer *rb, const MidiEvent *event)
{
    unsigned w = atomic_load(&rb->write_pos);
    unsigned r = atomic_load(&rb->read_pos);
    unsigned next = (w + 1) & (RING_BUFFER_SIZE - 1);

    if (next == r)
        return false;  /* full */

    rb->events[w] = *event;
    atomic_store(&rb->write_pos, next);
    return true;
}

bool ring_buffer_pop(RingBuffer *rb, MidiEvent *event)
{
    unsigned r = atomic_load(&rb->read_pos);
    unsigned w = atomic_load(&rb->write_pos);

    if (r == w)
        return false;  /* empty */

    *event = rb->events[r];
    atomic_store(&rb->read_pos, (r + 1) & (RING_BUFFER_SIZE - 1));
    return true;
}
